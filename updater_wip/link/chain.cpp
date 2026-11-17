// chain.cpp: see chain.h.
#include "chain.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace hl {

static Params with_window(Params p, unsigned window) { if (window) p.window = window; return p; }

Chain::Chain(const ChainConfig& c) : cfg(c), host(with_window(c.p, c.host_window)) {
    for (unsigned i = 0; i < c.nodes; i++) node.emplace_back(c.p, i + 1 < c.nodes, c.fwd_words);
    for (unsigned w = 0; w < 2 * c.nodes; w++) wire.emplace_back(w < c.delay.size() && c.delay[w] ? c.delay[w] : 1);
    src.resize(2 + c.nodes);
    src[0].left = c.packets[0]; src[1].left = c.packets[1];
    for (unsigned i = 0; i < c.nodes; i++) src[2 + i].left = c.packets[2];
    fill.assign(1 + 2 * c.nodes, 0); most_fill = fill;
    got_down.assign(2 * c.nodes, 0); got_up.assign(c.nodes, 0);
    stopped.assign(2 * ends(), 0); waited.assign(2 * ends(), 0); acked_was.assign(2 * ends(), 0); turn.assign(ends(), 0);
    started_v.assign(2 * c.nodes, 0);
    fg_packets_at.assign(c.nodes, 0); replies_at.assign(c.nodes, 0); reply_let.resize(c.nodes);
    last_damage.assign(c.nodes, 0); dirty.assign(c.nodes, 0);
}

void Chain::word(unsigned flow, uint64_t index, uint8_t out[WORD_BYTES]) const {
    if (cfg.content == 1) { memset(out, 0, WORD_BYTES); return; }
    flow_word(flow >> 1, flow & 1, index, out);
    if (cfg.content == 2) { out[0] = out[9]; memcpy(out + 1, out + 10, 6); }       // nothing in it but a hash
}

Hash128 Chain::canon() const {
    std::vector<uint8_t> v; v.reserve(1024);
    Writer w{v};
    host.canon(w);
    for (const auto& n : node) n.canon(w);
    for (const auto& wi : wire) wi.canon(w);
    for (const auto& s : src) const_cast<ChainSource&>(s).io(w);
    for (auto f : fill) w(f);
    for (auto g : got_down) w(g);
    for (auto g : got_up) w(g);
    for (auto s : stopped) w(s);
    for (auto x : waited) w(x);
    for (auto x : turn) w(x);
    w(gaps_allowed); w(must_report); w(excused); w(budget);
    for (auto s : started_v) w(s);
    return hash128(v.data(), v.size());
}

std::vector<uint8_t> Chain::whole() {
    const uint64_t cycle = t; t = 0;
    std::vector<uint32_t> ord;
    for (auto& w : wire) { ord.push_back(w.ordinal); w.ordinal = 0; }
    std::vector<uint8_t> v = save();
    t = cycle;
    for (unsigned i = 0; i < wire.size(); i++) wire[i].ordinal = ord[i];
    return v;
}

// A packet for node i itself, taken by its upper end: each word must be the next of the host's stream.
void Chain::deliver_down(unsigned i) {
    const LinkEnd& le = node[i].up;
    const unsigned s = le.r.rx_stream;
    uint64_t& got = got_down[2 * i + s];
    for (unsigned w = 0; w < le.r.rx_length; w++) {
        const uint8_t* have = le.rx_data() + w * WORD_BYTES;
        uint8_t want[WORD_BYTES];
        word(s, got, want);
        if (memcmp(have, want, WORD_BYTES) != 0) {
            bool known = false; uint64_t index = 0;
            for (uint64_t k = got > 2048 ? got - 2048 : 0; k < got + 2048 && !known; k++) { word(s, k, want); if (!memcmp(have, want, WORD_BYTES)) { known = true; index = k; } }
            if (known && gaps_allowed && index > got) { got = index + 1; continue; }
            if (bad.empty()) {
                char m[160];
                if (!known) snprintf(m, sizeof m, "a word delivered that was not sent (node %u, stream %u)", i, s + 1);
                else snprintf(m, sizeof m, "word %llu delivered where word %llu was next: %s (node %u, stream %u)", (unsigned long long)index,
                              (unsigned long long)got, index < got ? "delivered twice" : "words lost", i, s + 1);
                bad = m;
            }
            return;
        }
        got++;
    }
    if (got > src[s].made_words && bad.empty()) bad = "more words delivered than were sent";
    if (s == FG && fg_packets_at[i] < fg_let.size()) {         // from its turn at the host (let out, and the one before it taken) to here
        const uint64_t k = fg_packets_at[i]++;
        const uint64_t waitd = t - std::max(fg_let[k], k && k - 1 < fg_taken.size() ? fg_taken[k - 1] : 0);
        if (i == 0) hop_wait_max = std::max(hop_wait_max, waitd);
        if (i + 1 == cfg.nodes) end_wait_max = std::max(end_wait_max, waitd);
    }
    uint32_t& f = fill[1 + 2 * i + s];
    f += le.r.rx_length; most_fill[1 + 2 * i + s] = std::max(most_fill[1 + 2 * i + s], f);
    if (f > cfg.queue_words && bad.empty()) { char m[96]; snprintf(m, sizeof m, "queue overrun (node %u, stream %u)", i, s + 1); bad = m; }
}

// A packet taken by the host: its hops say whose reply it is, and each word must be the next of that node's.
void Chain::deliver_up() {
    const unsigned s = host.r.rx_stream, i = host.r.rx_hops;
    if ((s != FG || i >= cfg.nodes) && bad.empty()) { bad = s != FG ? "background data towards the host" : "a reply with hops beyond the chain"; return; }
    if (i >= cfg.nodes) return;
    uint64_t& got = got_up[i];
    for (unsigned w = 0; w < host.r.rx_length; w++) {
        const uint8_t* have = host.rx_data() + w * WORD_BYTES;
        uint8_t want[WORD_BYTES];
        word(2 * (2 + i), got, want);
        if (memcmp(have, want, WORD_BYTES) != 0) {
            bool known = false; uint64_t index = 0;
            for (uint64_t k = got > 2048 ? got - 2048 : 0; k < got + 2048 && !known; k++) { word(2 * (2 + i), k, want); if (!memcmp(have, want, WORD_BYTES)) { known = true; index = k; } }
            if (known && gaps_allowed && index > got) { got = index + 1; continue; }
            if (bad.empty()) {
                char m[160];
                snprintf(m, sizeof m, known ? "at the host, node %u's reply word %llu where word %llu was next: %s" : "at the host, under hops %u, a word that node did not send",
                         i, (unsigned long long)index, (unsigned long long)got, index < got ? "delivered twice" : "words lost");
                bad = m;
            }
            return;
        }
        got++;
    }
    if (got > src[2 + i].made_words && bad.empty()) bad = "more words delivered than were sent";
    if (replies_at[i] < reply_let[i].size()) reply_wait_max = std::max(reply_wait_max, t - reply_let[i][replies_at[i]++]);
    fill[0] += host.r.rx_length; most_fill[0] = std::max(most_fill[0], fill[0]);
    if (fill[0] > cfg.queue_words && bad.empty()) bad = "queue overrun (the host)";
}

// Both ends of a link have nothing kept and nothing to ask for: after damage, the link is back as it was.
static bool nothing_owed(const LinkEnd& a, const LinkEnd& b) {
    for (const LinkEnd* e : {&a, &b}) for (unsigned s = 0; s < STREAMS; s++) if (e->kept(s) || e->r.s[s].need) return false;
    return true;
}

void Chain::step() {
    const unsigned n = cfg.nodes;
    // Every unit's inputs, from the wires and the queues as they stand.
    In hin;
    Offer off[2]; uint8_t data[2][MAX_WORDS * WORD_BYTES];
    hin.rx_byte = wire[1].out();
    hin.say_stop[FG] = fill[0] * 2 >= cfg.queue_words; hin.rx_free[FG] = cfg.queue_words > fill[0] ? cfg.queue_words - fill[0] : 0;
    hin.rx_free[BG] = 0;
    for (unsigned s = 0; s < STREAMS; s++) {
        if (!src[s].waiting()) continue;
        const uint8_t len = src[s].lens[src[s].made];
        for (unsigned w = 0; w < len; w++) word(s, src[s].made_words + w, data[s] + w * WORD_BYTES);
        off[s].length = len; off[s].dsum = crc32c(data[s], size_t(len) * WORD_BYTES); off[s].data = data[s];
        hin.offer[s] = &off[s];
    }
    std::vector<NodeIn> nin(n);
    std::vector<Offer> own(n);
    std::vector<std::vector<uint8_t>> own_data(n);
    for (unsigned i = 0; i < n; i++) {
        nin[i].up_rx = wire[2 * i].out();
        if (i + 1 < n) nin[i].down_rx = wire[2 * (i + 1) + 1].out();
        for (unsigned s = 0; s < STREAMS; s++) {
            const uint32_t f = fill[1 + 2 * i + s];
            nin[i].own_stop[s] = f * 2 >= cfg.queue_words;
            nin[i].own_free[s] = cfg.queue_words > f ? cfg.queue_words - f : 0;
        }
        ChainSource& so = src[2 + i];
        if (so.waiting()) {
            const uint8_t len = so.lens[so.made];
            own_data[i].resize(size_t(len) * WORD_BYTES);
            for (unsigned w = 0; w < len; w++) word(2 * (2 + i), so.made_words + w, own_data[i].data() + w * WORD_BYTES);
            own[i].length = len; own[i].dsum = crc32c(own_data[i].data(), own_data[i].size()); own[i].data = own_data[i].data();
            nin[i].own_offer = &own[i];
        }
    }
    for (unsigned e = 0; e < ends(); e++) for (unsigned s = 0; s < STREAMS; s++) acked_was[2 * e + s] = end(e).r.s[s].acked;
    std::vector<std::array<uint32_t, STREAMS>> room(ends());              // what each end could take this cycle, for a failure's message
    room[0] = {hin.rx_free[FG], hin.rx_free[BG]};
    for (unsigned i = 0; i < n; i++) {
        for (unsigned s = 0; s < STREAMS; s++) {
            room[1 + 2 * i][s] = i + 1 < n ? std::min(nin[i].own_free[s], node[i].fwd_down[s].free_words()) : nin[i].own_free[s];
            if (i + 1 < n) room[2 + 2 * i][s] = node[i].fwd_up[s].free_words();
        }
    }
    // Which ends have a foreground packet waiting for the line, with nothing but the line to wait for.
    std::vector<uint8_t> fg_waits(ends(), 0);
    for (unsigned e = 0; e < ends(); e++) {
        const LinkEnd& le = end(e);
        const bool has = e == 0 ? src[0].waiting() : (e & 1) ? (!node[(e - 1) / 2].fwd_up[FG].empty() || src[2 + (e - 1) / 2].waiting()) : !node[(e - 2) / 2].fwd_down[FG].empty();
        const LinkEnd::StreamRegs& st = le.r.s[FG];
        fg_waits[e] = has && le.running() && !st.peer_stop && st.next == st.tail && le.kept(FG) < le.p.window;
    }
    // The clock.
    stop = false;
    const Taken th = host.step(hin);
    for (unsigned s = 0; s < STREAMS; s++) if (th.tx[s]) { src[s].made_words += src[s].lens[src[s].made]; src[s].made++; }
    if (th.tx[FG] && !fg_let.empty()) fg_taken.push_back(t);
    if (host.r.rx_valid) deliver_up();
    wire[0].push(host.r.tx_byte, host.tap);
    for (unsigned i = 0; i < n; i++) {
        node[i].step(nin[i]);
        if (node[i].own_taken) { ChainSource& so = src[2 + i]; so.made_words += so.lens[so.made]; so.made++; }
        if (node[i].own_passed) { own_passed++; if (bad.empty()) bad = "a node sent a packet of its own before one it was forwarding"; }
        if (node[i].up.r.rx_valid) deliver_down(i);
        wire[2 * i + 1].push(node[i].up.r.tx_byte, node[i].up.tap);
        if (i + 1 < n) wire[2 * (i + 1)].push(node[i].down.r.tx_byte, node[i].down.tap);
    }
    std::vector<uint8_t> dirty_now(n, 0);
    for (unsigned l = 0; l < n; l++) {
        dirty_now[l] = wire[2 * l].changed || wire[2 * l + 1].changed || wire[2 * l].fate != Fate::WHOLE || wire[2 * l + 1].fate != Fate::WHOLE;
        if (dirty_now[l]) { last_damage[l] = t; dirty[l] = 1; }
        else if (dirty[l] && end(upper(l)).running() && end(lower(l)).running() &&
                 (end(upper(l)).ev.delivered || end(lower(l)).ev.delivered || nothing_owed(end(upper(l)), end(lower(l))))) {
            const uint64_t took = t - last_damage[l];
            dirty[l] = 0; recoveries++; recovery_sum += took; recovery_max = std::max(recovery_max, took);
        }
    }
    for (unsigned w = 0; w < n_wires(); w++) started_v[w] = end(w & 1 ? lower(w / 2) : upper(w / 2)).tap.start;
    // The properties of each end, watched from outside it.
    for (unsigned e = 0; e < ends(); e++) {
        const LinkEnd& le = end(e);
        const unsigned link = e == 0 ? 0 : (e - 1) / 2 + ((e & 1) ? 0 : 1);
        const LinkEnd& other = end(e == upper(link) ? lower(link) : upper(link));
        stop = stop || le.ev.any() || le.tap.start || le.tap.end;
        if (le.ev.restarted) {
            gaps_allowed = must_report = true;
            if (!excused && bad.empty()) bad = "the link started again, and nothing had happened that could excuse it";
        }
        for (unsigned s = 0; s < STREAMS; s++) {
            if (le.kept(s) > le.p.window && bad.empty()) bad = "more kept than the window";
            if (le.ev.restarted) stopped[2 * e + s] = 0;
            if (le.ev.stop_taken[s] >= 0) stopped[2 * e + s] = le.ev.stop_taken[s] != 0;
            const bool waits = le.kept(s) != 0 && le.r.s[s].next == le.r.s[s].tail && le.running() && other.running() && !le.r.s[s].peer_stop && !dirty_now[link] &&
                               wire[2 * link].fate == Fate::WHOLE && wire[2 * link + 1].fate == Fate::WHOLE;
            uint32_t& w = waited[2 * e + s];
            if (!waits || le.r.s[s].acked != acked_was[2 * e + s]) w = 0;
            else if (++w > still_bound() && bad.empty()) {
                char m[320];
                snprintf(m, sizeof m, "a sender stood still: no acknowledgement in %u cycles on a link in step (end %u, stream %u): it has acknowledged %u, "
                         "next %u, new %u; the other end has taken %u, owes resend %d, asked %d (%u cycles ago), room for %u words",
                         w, e, s + 1, le.r.s[s].acked, le.r.s[s].next, le.r.s[s].tail, other.r.s[s].last, other.r.s[s].need, other.r.s[s].asked,
                         other.r.s[s].ask_timer, room[e == upper(link) ? lower(link) : upper(link)][s]);
                bad = m;
            }
            most_waited = std::max(most_waited, w);
        }
        // A foreground packet's turn: behind one packet at most, whatever the background is doing.
        if (!fg_waits[e] || (le.tap.start && unpack(le.tap.hdr).stream == 1 && unpack(le.tap.hdr).length)) turn[e] = 0;
        else if (++turn[e] > 2 * HEADER_BYTES + cfg.p.max_words * WORD_BYTES + 2 && bad.empty()) {
            char m[128]; snprintf(m, sizeof m, "a foreground packet waited %u cycles for the line (end %u)", turn[e], e);
            bad = m;
        }
        most_turn = std::max(most_turn, turn[e]);
        if (le.tap.start && (le.tap.kind == Kind::DATA || le.tap.kind == Kind::RESENT) && stopped[2 * e + unpack(le.tap.hdr).stream - 1] && bad.empty())
            bad = "a data packet started on a stream the other end has stopped";
    }
    t++;
}

bool Chain::done() const {
    bool error = false;
    for (unsigned e = 0; e < ends(); e++) {
        const LinkEnd& le = end(e);
        if (!le.running() || le.kept(FG) || le.kept(BG)) return false;
        error = error || le.r.error;
    }
    if (must_report && !error) return false;
    for (const auto& s : src) if (s.waiting() || s.left) return false;
    for (const auto& nd : node) for (unsigned s = 0; s < STREAMS; s++) if (!nd.fwd_down[s].empty() || !nd.fwd_up[s].empty()) return false;
    if (gaps_allowed) return true;
    for (unsigned i = 0; i < cfg.nodes; i++) {
        if (got_down[2 * i] != src[0].made_words || got_down[2 * i + 1] != src[1].made_words) return false;
        if (got_up[i] != src[2 + i].made_words) return false;
    }
    return true;
}

long Chain::quiet(uint64_t bound) {
    for (uint64_t k = 0; k < bound; k++) {
        if (!bad.empty()) return -1;
        if (done()) return long(k);
        for (unsigned i = 0; i < src.size(); i++) while (src[i].left) offer(i);
        for (auto& f : fill) f = 0;
        step();
    }
    if (bad.empty()) {
        std::string m = "nothing more goes wrong, and it does not finish:";
        char b[160];
        for (unsigned e = 0; e < ends(); e++) {
            const LinkEnd& le = end(e);
            snprintf(b, sizeof b, " end %u %s%s%s%s[kept %u %u]", e, le.r.lost ? "lost " : "", le.r.answer ? "answering " : "", le.r.restart ? "restarting " : "",
                     le.r.restart_answer ? "restart-answering " : "", le.kept(FG), le.kept(BG));
            m += b;
        }
        for (unsigned i = 0; i < cfg.nodes; i++) {
            snprintf(b, sizeof b, " node %u got %llu %llu of %llu %llu, the host %llu of its %llu;", i, (unsigned long long)got_down[2 * i], (unsigned long long)got_down[2 * i + 1],
                     (unsigned long long)src[0].made_words, (unsigned long long)src[1].made_words, (unsigned long long)got_up[i], (unsigned long long)src[2 + i].made_words);
            m += b;
        }
        bad = m;
    }
    return -1;
}

std::string play_chain(const Case& c, const std::string& rules) {
    ChainConfig cfg;
    cfg.p = c.cfg.p;
    std::string err;
    if (!apply_rules(rules, cfg.p, err)) return err;
    cfg.nodes = c.chain; cfg.queue_words = c.cfg.queue_words; cfg.fwd_words = c.fwd; cfg.words = c.cfg.words; cfg.still = c.cfg.still;
    cfg.delay.assign(2 * c.chain, c.cfg.delay);
    for (unsigned i = 0; i < 3; i++) cfg.packets[i] = c.cpackets[i];
    Chain b(cfg);
    while (b.t < c.quiet_at && b.bad.empty()) {
        for (const Event& e : c.events) {
            if (e.what == Event::FATE || e.cycle != b.t) continue;
            if (e.what == Event::NOISE) b.wire[e.a].noise();
            else if (e.what == Event::OFFER) b.offer(e.a);
            else if (e.what == Event::DRAIN) b.drain(e.a, e.n);
        }
        b.step();
        for (unsigned w = 0; w < b.n_wires(); w++) {
            if (!b.started(w)) continue;
            for (const Event& e : c.events) if (e.what == Event::FATE && e.a == w && e.cycle == b.wire[w].ordinal) b.fate(w, Fate(e.n));
        }
    }
    if (b.bad.empty()) b.quiet(c.bound);
    return b.bad;
}

}  // namespace hl
