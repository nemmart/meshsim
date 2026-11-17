// bench.cpp: see bench.h.
#include "bench.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace hl {

static const char* const FATE_NAMES[] = {"whole", "gone", "first0", "header", "data", "forge-ack", "forge-stop",
                                          "forge-lost", "forge-recovery", "forge-resend", "forge-number", "forge-number-back", "forge-ack-data"};
const char* fate_name(Fate f) { return FATE_NAMES[unsigned(f)]; }
bool fate_from(const std::string& name, Fate& f) {
    for (unsigned i = 0; i < unsigned(Fate::COUNT); i++) if (name == FATE_NAMES[i]) { f = Fate(i); return true; }
    return false;
}

// ---- the wire ----

uint8_t Wire::damaged(uint32_t index, uint8_t b) const {
    switch (fate) {
    case Fate::WHOLE: return b;
    case Fate::GONE: return 0;
    case Fate::FIRST0: return index == 0 ? 0 : b;
    case Fate::HEADER: return index == 5 ? b ^ 1 : b;
    case Fate::DATA: return index == HEADER_BYTES ? b ^ 1 : b;
    case Fate::FORGE_ACK_DATA: return index < HEADER_BYTES ? forged[index] : index == HEADER_BYTES ? b ^ 1 : b;
    default: return index < HEADER_BYTES ? forged[index] : b;
    }
}

void Wire::push(uint8_t b, const Tap& tap) {
    if (tap.start) { at = 0; len = tap.bytes; fate = Fate::WHOLE; memcpy(hdr, tap.hdr, HEADER_BYTES); ordinal++; }
    const uint8_t sent = b;
    if (at < len) {
        b = damaged(at, b);
        if (++at == len) { fate = Fate::WHOLE; memset(forged, 0, sizeof forged); memset(hdr, 0, sizeof hdr); }
    }
    if (next_zero) b = 0;
    if (next_random) b = random_byte;
    b = uint8_t((b ^ next_xor) | (b ? 0 : next_or));
    next_xor = next_or = 0; next_zero = next_random = false;
    changed = b != sent;
    ring[pos] = b; pos = (pos + 1) % uint32_t(ring.size());
}

void Wire::set_fate(Fate f, const Numbers& nums) {
    fate = f;
    Header h = unpack(hdr);
    switch (f) {
    case Fate::FORGE_ACK: case Fate::FORGE_ACK_DATA: h.ack[FG] = nums.succ(h.ack[FG]); break;   // the forged fields are the foreground's
    case Fate::FORGE_STOP: h.stop[FG] = !h.stop[FG]; break;
    case Fate::FORGE_LOST: h.flags |= F_LOST; break;
    case Fate::FORGE_RECOVERY: h.flags |= F_RECOVERY; break;
    case Fate::FORGE_RESEND: h.resend[FG] = true; break;
    case Fate::FORGE_NUMBER: h.number[FG] = nums.succ(h.number[FG]); break;
    case Fate::FORGE_NUMBER_BACK: h.number[FG] = h.number[FG] <= 1 ? nums.last : h.number[FG] - 1; break;
    default: break;
    }
    pack(h, forged);
    const uint32_t first = (pos + uint32_t(ring.size()) - 1) % uint32_t(ring.size());
    ring[first] = damaged(0, hdr[0]);
}

bool Wire::noise() {
    const uint32_t lastb = (pos + uint32_t(ring.size()) - 1) % uint32_t(ring.size());
    if (!idle() || ring[lastb] != 0) return false;
    ring[lastb] = 0x5A;
    return true;
}

// ---- the bench ----

void flow_word(unsigned from, unsigned stream, uint64_t index, uint8_t out[WORD_BYTES]) {
    uint64_t x = (index + 1) * 0x9E3779B97F4A7C15ull ^ (uint64_t(from) << 56 | uint64_t(stream) << 48);
    x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 32;
    out[0] = uint8_t(0xA0 | from << 1 | stream);
    memcpy(out + 1, &index, 8);
    memcpy(out + 9, &x, 7);
}

// The image of a header that would pass every check: of length 0, with "recovery" and an acknowledge field. What
// a receiver makes of it, if it ever takes a packet's data for a packet.
void Link1::word(unsigned from, unsigned stream, uint64_t index, uint8_t out[WORD_BYTES]) const {
    if (!cfg.images) { flow_word(from, stream, index, out); return; }
    const uint64_t packet = index / cfg.words;             // a packet's data: the 20 bytes of the image, then zeros
    Header h; h.stream = 1; h.flags = F_RECOVERY;
    h.number[FG] = uint32_t(packet % 5) + 1; h.ack[FG] = uint32_t(packet % 2) + 1; h.number[BG] = 1;
    uint8_t image[2 * WORD_BYTES] = {};
    pack(h, image);
    memcpy(out, image + (index % cfg.words ? WORD_BYTES : 0), WORD_BYTES);
    if (index % cfg.words > 1) memset(out, 0, WORD_BYTES);
}

bool Link1::flip(unsigned e, unsigned s) {
    LinkEnd& le = end[e];
    if (le.kept(s) == 0) return false;
    le.r.s[s].data[size_t(le.r.s[s].head) * cfg.p.max_words * WORD_BYTES + 3] ^= 0x10;
    excused = true;
    return true;
}

static Params with_window(Params p, unsigned window) { if (window) p.window = window; return p; }

Link1::Link1(const Config& c) : cfg(c), end{LinkEnd(with_window(c.p, c.window0)), LinkEnd(c.p)}, wire{Wire(c.delay), Wire(c.delay)} {
    for (unsigned e = 0; e < 2; e++) for (unsigned s = 0; s < STREAMS; s++) src[e][s].total = c.packets[e][s];
}

void Link1::step() {
    In in[2];
    Offer off[2][STREAMS];
    uint8_t data[2][STREAMS][MAX_WORDS * WORD_BYTES];
    for (unsigned e = 0; e < 2; e++) {
        in[e].rx_byte = wire[1 - e].out();
        in[e].power_on = power[e]; power[e] = false;
        in[e].rx_forwards = e == 1 && cfg.wrong != 0;
        for (unsigned s = 0; s < STREAMS; s++) {
            in[e].say_stop[s] = fill[e][s] * 2 >= cfg.queue_words;
            in[e].rx_free[s] = cfg.queue_words > fill[e][s] ? cfg.queue_words - fill[e][s] : 0;
            const Source& so = src[e][s];
            if (so.made < so.avail) {
                for (unsigned w = 0; w < cfg.words; w++) word(e, s, uint64_t(so.made) * cfg.words + w, data[e][s] + w * WORD_BYTES);
                off[e][s].length = uint8_t(cfg.words);
                off[e][s].hops = (e == 0 && s == FG && cfg.wrong && so.made + 1 == cfg.wrong) ? 15 : 0;
                off[e][s].dsum = crc32c(data[e][s], cfg.words * WORD_BYTES); off[e][s].data = data[e][s];
                in[e].offer[s] = &off[e][s];
            }
        }
    }
    stop = false;
    uint32_t acked_was[2][STREAMS];
    for (unsigned e = 0; e < 2; e++) for (unsigned s = 0; s < STREAMS; s++) acked_was[e][s] = end[e].r.s[s].acked;
    for (unsigned e = 0; e < 2; e++) {
        const Taken took = end[e].step(in[e]);
        for (unsigned s = 0; s < STREAMS; s++) if (took.tx[s]) { src[e][s].made++; if (off[e][s].hops == 15) excused = true; }
        if (end[e].r.rx_valid) deliver(e);
        wire[e].push(end[e].r.tx_byte, end[e].tap);
        started_[e] = end[e].tap.start;
        if (trace) {
            const LinkEnd& le = end[e]; const Events& v = le.ev;
            const char* mode = le.r.restart || le.r.restart_answer ? (le.r.restart ? (le.r.restart_answer ? "RESTART+ANSWER" : "RESTART") : "RESTART-ANSWER")
                             : le.r.lost ? (le.r.answer ? "LOST+ANSWER" : "LOST") : le.r.answer ? "ANSWER" : "RUN";
            if (le.tap.start) {
                const Header h = unpack(le.tap.hdr);
                fprintf(trace, "%6llu  end %u sends   %s, numbers %u %u, acks %u %u%s%s%s%s%s%s%s\n", (unsigned long long)t, e,
                        h.length ? (h.stream == 1 ? "foreground data" : "background data") : "length 0", h.number[FG], h.number[BG], h.ack[FG], h.ack[BG],
                        h.resend[FG] ? " RESEND-fg" : "", h.resend[BG] ? " RESEND-bg" : "", h.stop[FG] ? " STOP-fg" : "", h.stop[BG] ? " STOP-bg" : "",
                        h.flags & F_LOST ? " LOST" : "", h.flags & F_RECOVERY ? " RECOVERY" : "", h.flags & F_RESTART ? " RESTART" : "");
            }
            if (v.zeros_start) fprintf(trace, "%6llu  end %u sends   %u zeros\n", (unsigned long long)t, e, cfg.p.zeros);
            if (v.rx_bad_header) fprintf(trace, "%6llu  end %u         a header's CRC fails: it has lost its place\n", (unsigned long long)t, e);
            if (v.rx_bad_data) fprintf(trace, "%6llu  end %u         a packet's data fails its checksum\n", (unsigned long long)t, e);
            if (v.rx_bad_fields) fprintf(trace, "%6llu  end %u         a header with a field that cannot be\n", (unsigned long long)t, e);
            if (v.rx_good) {
                const Header h = unpack(le.r.rx_hdr);
                fprintf(trace, "%6llu  end %u gets    %s, numbers %u %u, acks %u %u%s%s%s\n", (unsigned long long)t, e,
                        h.length ? (h.stream == 1 ? "foreground data" : "background data") : "length 0", h.number[FG], h.number[BG], h.ack[FG], h.ack[BG],
                        v.delivered ? ": taken" : "", v.ahead ? ": ahead of the one expected" : "", v.behind ? ": seen before" : v.refused ? ": no room" : "");
            }
            if (v.went_back) fprintf(trace, "%6llu  end %u         goes back to the oldest not acknowledged\n", (unsigned long long)t, e);
            if (v.restarted) fprintf(trace, "%6llu  end %u         the link starts again under it: error\n", (unsigned long long)t, e);
            if (v.mode_change) fprintf(trace, "%6llu  end %u         now %s\n", (unsigned long long)t, e, mode);
            if (in[e].power_on) fprintf(trace, "%6llu  end %u         POWER-ON\n", (unsigned long long)t, e);
        }
        if (end[e].ev.restarted) {
            gaps_allowed = must_report = true;
            if (!excused && bad.empty()) bad = "the link started again, and nothing had happened that could excuse it";
        }
        for (unsigned s = 0; s < STREAMS; s++) {           // the stop bit, watched from outside
            if (end[e].ev.restarted || in[e].power_on) stopped[e][s] = false;
            if (end[e].ev.stop_taken[s] >= 0) stopped[e][s] = end[e].ev.stop_taken[s] != 0;
        }
        // A sender whose packets are kept, on a link in step, hears an acknowledgement before long.
        for (unsigned s = 0; s < STREAMS; s++) {
            const bool waits = end[e].kept(s) != 0 && end[e].r.s[s].next == end[e].r.s[s].tail && end[0].running() && end[1].running() && !end[e].r.s[s].peer_stop &&
                               wire[0].fate == Fate::WHOLE && wire[1].fate == Fate::WHOLE;
            if (!waits || end[e].r.s[s].acked != acked_was[e][s]) waited[e][s] = 0;
            else if (++waited[e][s] > still_bound() && bad.empty()) {
                char m[128]; snprintf(m, sizeof m, "a sender stood still: no acknowledgement in %u cycles on a link in step (end %u, stream %u)", waited[e][s], e, s + 1);
                bad = m;
            }
            if (waited[e][s] > most_waited) most_waited = waited[e][s];
        }
        if (end[e].tap.start && (end[e].tap.kind == Kind::DATA || end[e].tap.kind == Kind::RESENT) &&
            stopped[e][unpack(end[e].tap.hdr).stream - 1] && bad.empty())
            bad = "a data packet started on a stream the other end has stopped";
        if (in[e].power_on) {                  // the chip behind this end starts again: its queues are empty
            gaps_allowed = excused = true;
            for (unsigned s = 0; s < STREAMS; s++) { fresh[e][s] = true; fill[e][s] = 0; }
        }
        stop = stop || end[e].ev.any() || end[e].tap.start || end[e].tap.end || in[e].power_on;
        for (unsigned s = 0; s < STREAMS; s++) {
            if (end[e].kept(s) > end[e].p.window && bad.empty()) bad = "more kept than the window";
            if (fill[e][s] > most_fill[e][s]) most_fill[e][s] = fill[e][s];
        }
    }
    t++;
}

// A packet handed over by end e: each word must be the next of its flow.
void Link1::deliver(unsigned e) {
    const LinkEnd& le = end[e];
    const unsigned s = le.r.rx_stream;
    for (unsigned w = 0; w < le.r.rx_length; w++) {
        const uint8_t* got = le.rx_data() + w * WORD_BYTES;
        uint8_t want[WORD_BYTES];
        word(1 - e, s, delivered[e][s], want);
        if (memcmp(got, want, WORD_BYTES) != 0) {
            uint64_t index = 0; bool known = false;        // which word of the flow is it, if any? (the next few, or one before)
            for (uint64_t i = delivered[e][s] > 64 ? delivered[e][s] - 64 : 0; i < delivered[e][s] + 64 && !known; i++) {
                word(1 - e, s, i, want);
                if (memcmp(got, want, WORD_BYTES) == 0) { known = true; index = i; }
            }
            if (known && gaps_allowed && (index > delivered[e][s] || fresh[e][s])) { delivered[e][s] = index + 1; fresh[e][s] = false; continue; }
            if (bad.empty()) {
                char m[160];
                if (!known) snprintf(m, sizeof m, "a word delivered that was not sent (end %u, stream %u)", e, s + 1);
                else snprintf(m, sizeof m, "word %llu delivered where word %llu was next: %s (end %u, stream %u)",
                              (unsigned long long)index, (unsigned long long)delivered[e][s],
                              index < delivered[e][s] ? "delivered twice" : "words lost", e, s + 1);
                bad = m;
            }
            return;
        }
        delivered[e][s]++; fresh[e][s] = false;
    }
    fill[e][s] += le.r.rx_length;
    if (fill[e][s] > cfg.queue_words && bad.empty()) {
        char m[96]; snprintf(m, sizeof m, "queue overrun (end %u, stream %u)", e, s + 1); bad = m;
    }
}

bool Link1::done() const {
    if (must_report && !end[0].r.error && !end[1].r.error) return false;
    for (unsigned e = 0; e < 2; e++) {
        if (!end[e].running()) return false;
        for (unsigned s = 0; s < STREAMS; s++) {
            if (src[e][s].made != src[e][s].total || end[e].kept(s) != 0) return false;
            if (!gaps_allowed && delivered[1 - e][s] != uint64_t(src[e][s].total) * cfg.words) return false;
        }
    }
    return true;
}

long Link1::quiet(uint64_t bound) {
    for (uint64_t n = 0; n < bound; n++) {
        if (!bad.empty()) return -1;
        if (done()) return long(n);
        for (unsigned e = 0; e < 2; e++) for (unsigned s = 0; s < STREAMS; s++) { src[e][s].avail = src[e][s].total; fill[e][s] = 0; }
        step();
    }
    if (bad.empty()) {
        std::string m = "nothing more goes wrong, and it does not finish:";
        if (must_report && !end[0].r.error && !end[1].r.error) m += " the link started again and no error is up;";
        char b[200];
        for (unsigned e = 0; e < 2; e++) {
            snprintf(b, sizeof b, " end %u %s%s%s%s", e, end[e].r.lost ? "lost " : "", end[e].r.answer ? "answering " : "",
                     end[e].r.restart ? "restarting " : "", end[e].r.restart_answer ? "restart-answering " : "");
            m += b;
            for (unsigned s = 0; s < STREAMS; s++) {
                snprintf(b, sizeof b, "[stream %u: made %u of %u, kept %u, delivered %llu of them]", s + 1, src[e][s].made,
                         src[e][s].total, end[e].kept(s), (unsigned long long)(delivered[1 - e][s] / cfg.words));
                m += b;
            }
        }
        bad = m;
    }
    return -1;
}

Hash128 Link1::canon() const {
    std::vector<uint8_t> v; v.reserve(512);
    Writer w{v};
    for (const auto& e : end) e.canon(w);
    for (const auto& wi : wire) wi.canon(w);
    for (const auto& e : src) for (const auto& s : e) { w(s.made); w(s.avail); }
    w(fill); w(delivered); w(gaps_allowed); w(fresh); w(must_report); w(excused); w(stopped); w(waited); w(power); w(budget); w(started_);
    return hash128(v.data(), v.size());
}

std::vector<uint8_t> Link1::whole() {
    const uint64_t cycle = t; const uint32_t o0 = wire[0].ordinal, o1 = wire[1].ordinal;
    t = 0; wire[0].ordinal = wire[1].ordinal = 0;
    std::vector<uint8_t> v = save();
    t = cycle; wire[0].ordinal = o0; wire[1].ordinal = o1;
    return v;
}

std::vector<uint8_t> Link1::save() { std::vector<uint8_t> v; Writer w{v}; io(w); return v; }
void Link1::load(const std::vector<uint8_t>& bytes) { Reader rd{bytes.data()}; io(rd); }

// ---- cases ----

namespace {
struct RuleName { const char* name; bool Rules::*field; };
const RuleName RULES[] = {
    {"resend_ahead", &Rules::resend_ahead}, {"ask_every", &Rules::ask_every}, {"zero_number", &Rules::zero_number},
    {"lost_answers", &Rules::lost_answers}, {"flush", &Rules::flush},
    {"refuse", &Rules::refuse}, {"made_wrong", &Rules::made_wrong}, {"long_is_lost", &Rules::long_is_lost},
    {"stop_lift_back", &Rules::stop_lift_back}, {"ack_now", &Rules::ack_now}, {"lost_uses_ack", &Rules::lost_uses_ack},
    {"restart", &Rules::restart}, {"tell_behind", &Rules::tell_behind}, {"check_kept", &Rules::check_kept},
};
struct SizeName { const char* name; unsigned Params::*field; };
const SizeName SIZES[] = {
    {"number_bits", &Params::number_bits}, {"max_words", &Params::max_words}, {"window", &Params::window},
    {"zeros", &Params::zeros}, {"flush_zeros", &Params::flush_zeros}, {"status_timer", &Params::status_timer},
    {"resend_timer", &Params::resend_timer}, {"reask", &Params::reask},
};
}  // namespace

bool apply_rules(const std::string& spec, Params& p, std::string& err) {
    size_t i = 0;
    auto word = [&]() { const size_t j = spec.find_first_of("+-,", i); std::string w = spec.substr(i, j == std::string::npos ? j : j - i); i = j; return w; };
    const std::string first = word();
    if (first == "as_written") p.rules = Rules::as_written();
    else if (first == "proposed") p.rules = Rules();
    else { err = "rules: " + first; return false; }
    while (i != std::string::npos && i < spec.size()) {
        const char sign = spec[i++];
        const std::string w = word();
        bool found = false;
        if (sign == ',') {
            const size_t eq = w.find('=');
            for (const auto& sz : SIZES) if (eq != std::string::npos && w.substr(0, eq) == sz.name) { p.*sz.field = unsigned(strtoul(w.c_str() + eq + 1, nullptr, 0)); found = true; }
            if (eq != std::string::npos && w.substr(0, eq) == "plant") { p.plant = Plant(strtoul(w.c_str() + eq + 1, nullptr, 0)); found = true; }
        } else for (const auto& rn : RULES) if (w == rn.name) { p.rules.*rn.field = sign == '+'; found = true; }
        if (!found) { err = "rules: " + w; return false; }
    }
    return true;
}

std::string rules_text(const Params& p) {
    std::string s = "proposed";
    const Rules d;
    for (const auto& rn : RULES) if (p.rules.*rn.field != d.*rn.field) s += std::string(p.rules.*rn.field ? "+" : "-") + rn.name;
    if (p.resend_timer) s += ",resend_timer=" + std::to_string(p.resend_timer);
    return s;
}

bool parse_case(const std::string& path, Case& c, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path; return false; }
    std::string line;
    c = Case();
    c.cfg.p = Params::tiny(); c.cfg.queue_words = 8; c.cfg.words = 1;
    while (std::getline(f, line)) {
        if (const size_t h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream in(line);
        std::string k;
        if (!(in >> k)) continue;
        Event ev{};
        std::string w;
        if (k == "sizes") {                  // name=value ...
            while (in >> w) {
                const size_t eq = w.find('=');
                const std::string name = w.substr(0, eq);
                const unsigned v = unsigned(strtoul(w.c_str() + eq + 1, nullptr, 0));
                bool found = false;
                for (const auto& sz : SIZES) if (name == sz.name) { c.cfg.p.*sz.field = v; found = true; }
                if (name == "queue") { c.cfg.queue_words = v; found = true; }
                if (name == "delay") { c.cfg.delay = v; found = true; }
                if (name == "words") { c.cfg.words = v; found = true; }
                if (name == "images") { c.cfg.images = v != 0; found = true; }
                if (name == "window0") { c.cfg.window0 = v; found = true; }
                if (name == "wrong") { c.cfg.wrong = v; found = true; }
                if (name == "still") { c.cfg.still = v; found = true; }
                if (!found) { err = "sizes: " + w; return false; }
            }
        } else if (k == "chain") { in >> c.chain; }
        else if (k == "cpackets") { in >> c.cpackets[0] >> c.cpackets[1] >> c.cpackets[2]; }
        else if (k == "fwd") { in >> c.fwd; }
        else if (k == "packets") { in >> c.cfg.packets[0][0] >> c.cfg.packets[0][1] >> c.cfg.packets[1][0] >> c.cfg.packets[1][1]; }
        else if (k == "fate") { unsigned a; Fate fa; in >> a >> ev.cycle >> w; if (!fate_from(w, fa)) { err = "fate: " + w; return false; } ev.what = Event::FATE; ev.a = uint8_t(a); ev.n = uint32_t(fa); c.events.push_back(ev); }
        else if (k == "noise") { unsigned a; in >> a >> ev.cycle; ev.what = Event::NOISE; ev.a = uint8_t(a); c.events.push_back(ev); }
        else if (k == "offer") { unsigned a, b; in >> a >> b >> ev.cycle; ev.what = Event::OFFER; ev.a = uint8_t(a); ev.b = uint8_t(b - 1); c.events.push_back(ev); }
        else if (k == "drain") { unsigned a, b; in >> a >> b >> ev.cycle >> ev.n; ev.what = Event::DRAIN; ev.a = uint8_t(a); ev.b = uint8_t(b - 1); c.events.push_back(ev); }
        else if (k == "power") { unsigned a; in >> a >> ev.cycle; ev.what = Event::POWER; ev.a = uint8_t(a); c.events.push_back(ev); }
        else if (k == "flip") { unsigned a, b; in >> a >> b >> ev.cycle; ev.what = Event::FLIP; ev.a = uint8_t(a); ev.b = uint8_t(b - 1); c.events.push_back(ev); }
        else if (k == "quiet") { in >> c.quiet_at >> c.bound; }
        else if (k == "run") {
            Run r; std::string verdict;
            in >> r.rules >> verdict;
            r.ok = verdict == "ok";
            if (!r.ok && verdict != "fail") { err = "run: " + verdict; return false; }
            std::getline(in, r.text);
            while (!r.text.empty() && r.text[0] == ' ') r.text.erase(0, 1);
            c.runs.push_back(r);
        } else { err = "unknown line: " + k; return false; }
        if (in.fail() && !in.eof()) { err = "bad line: " + line; return false; }
    }
    return true;
}

std::string case_text(const Case& c) {
    std::ostringstream o;
    const Params& p = c.cfg.p;
    o << "sizes number_bits=" << p.number_bits << " max_words=" << p.max_words << " window=" << p.window << " zeros=" << p.zeros
      << " flush_zeros=" << p.flush_zeros << " status_timer=" << p.status_timer << " queue=" << c.cfg.queue_words
      << " reask=" << p.reask << " delay=" << c.cfg.delay << " words=" << c.cfg.words << " images=" << c.cfg.images
      << " window0=" << c.cfg.window0 << " wrong=" << c.cfg.wrong << " still=" << c.cfg.still << "\n";
    if (c.chain) o << "chain " << c.chain << "\ncpackets " << c.cpackets[0] << " " << c.cpackets[1] << " " << c.cpackets[2] << "\nfwd " << c.fwd << "\n";
    else o << "packets " << c.cfg.packets[0][0] << " " << c.cfg.packets[0][1] << " " << c.cfg.packets[1][0] << " " << c.cfg.packets[1][1] << "\n";
    for (const Event& e : c.events) {
        switch (e.what) {
        case Event::FATE: o << "fate " << unsigned(e.a) << " " << e.cycle << " " << fate_name(Fate(e.n)) << "\n"; break;
        case Event::NOISE: o << "noise " << unsigned(e.a) << " " << e.cycle << "\n"; break;
        case Event::OFFER: o << "offer " << unsigned(e.a) << " " << unsigned(e.b) + 1 << " " << e.cycle << "\n"; break;
        case Event::DRAIN: o << "drain " << unsigned(e.a) << " " << unsigned(e.b) + 1 << " " << e.cycle << " " << e.n << "\n"; break;
        case Event::POWER: o << "power " << unsigned(e.a) << " " << e.cycle << "\n"; break;
        case Event::FLIP: o << "flip " << unsigned(e.a) << " " << unsigned(e.b) + 1 << " " << e.cycle << "\n"; break;
        }
    }
    o << "quiet " << c.quiet_at << " " << c.bound << "\n";
    for (const Run& r : c.runs) o << "run " << r.rules << (r.ok ? " ok" : " fail ") << r.text << "\n";
    return o.str();
}

std::string play(const Case& c, const std::string& rules, Link1* out, FILE* trace) {
    if (c.chain) return play_chain(c, rules);
    Config cfg = c.cfg;
    std::string err;
    if (!apply_rules(rules, cfg.p, err)) return err;
    Link1 b(cfg);
    b.trace = trace;
    while (b.t < c.quiet_at && b.bad.empty()) {
        for (const Event& e : c.events) {
            if (e.what == Event::FATE || e.cycle != b.t) continue;
            if (e.what == Event::NOISE) b.wire[e.a].noise();
            else if (e.what == Event::OFFER) b.offer(e.a, e.b);
            else if (e.what == Event::DRAIN) b.drain(e.a, e.b, e.n);
            else if (e.what == Event::POWER) b.power[e.a] = true;
            else if (e.what == Event::FLIP) b.flip(e.a, e.b);
        }
        b.step();
        for (unsigned w = 0; w < 2; w++) {
            if (!b.started(w)) continue;
            for (const Event& e : c.events)
                if (e.what == Event::FATE && e.a == w && e.cycle == b.wire[w].ordinal) {
                    b.fate(w, Fate(e.n));
                    if (trace) fprintf(trace, "%6llu  WIRE          that packet from end %u: %s\n", (unsigned long long)b.t - 1, w, fate_name(Fate(e.n)));
                }
        }
    }
    if (trace && b.bad.empty()) fprintf(trace, "%6llu  from here nothing more goes wrong\n", (unsigned long long)b.t);
    if (b.bad.empty()) { if (trace && c.bound > 600) { Link1 shown = b; shown.quiet(600); } b.trace = nullptr; b.quiet(c.bound); }
    if (out) *out = b;
    return b.bad;
}

}  // namespace hl
