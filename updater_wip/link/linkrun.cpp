// linkrun: the random runs (NOTES.md, section 7). A run is a seed. From it are drawn the chain's length, the sizes,
// and the parameters of every process: each wire's noise (single flipped bits; bursts with their gap, length,
// density and kind; damage aimed at kinds of packet) and delay, each queue's consumer (how long it takes words, how
// long it stops, how fast it is), the host's traffic on both streams and each node's replies. Quantities that span
// orders of magnitude are drawn on a log scale, and every process can be drawn "never". The processes run all at
// once and know nothing of the protocol's state; only the aimed damage looks at what kind of packet is starting.
//
// A run ends when the traffic and the noise stop: then every queue is emptied and everything must arrive. Every
// property of the chain's bench is checked every cycle. A failing seed fails again, the same, when run again.
//
//   linkrun [--seeds A B] [--cycles N] [--nodes N] [--rules SPEC] [--host-window N] [--status N] [--quiet-wires]
//           [--saturate] [--forge] [--plant N] [--long-noise] [--seconds S] [-v]
// One line a seed with -v; then the totals: the figures, and the count of every rare event (0 is said).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "chain.h"

using namespace hl;

namespace {

struct Rng {                               // xoshiro256**, seeded by splitmix64: the same run under any compiler
    uint64_t s[4];
    explicit Rng(uint64_t seed) { for (auto& x : s) { seed += 0x9E3779B97F4A7C15ull; uint64_t z = seed; z = (z ^ z >> 30) * 0xBF58476D1CE4E5B9ull; z = (z ^ z >> 27) * 0x94D049BB133111EBull; x = z ^ z >> 31; } }
    uint64_t next() {
        auto rotl = [](uint64_t x, int k) { return x << k | x >> (64 - k); };
        const uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    double uni() { return double(next() >> 11) * (1.0 / 9007199254740992.0); }
    uint64_t below(uint64_t n) { return n ? next() % n : 0; }
    bool chance(double p) { return uni() < p; }
    double logu(double lo, double hi) { return lo * std::exp(uni() * std::log(hi / lo)); }   // log-uniform
    uint64_t geometric(double mean) { const double u = uni(); return mean <= 1 ? 1 : uint64_t(1 + -std::log(1 - u) * mean); }
};

constexpr unsigned KINDS = 8;              // Kind's values
const char* const KIND_NAMES[KINDS] = {"-", "data", "sent again", "status", "lost", "recovery", "lost+recovery", "restart"};

struct WireNoise {
    double flip_rate = 0;                  // single bits: a bit
    uint64_t next_flip = ~0ull;            // bits until the next
    double burst_gap = 0;                  // mean cycles between bursts; 0 never
    double burst_ceiling = 1, density = 0;
    unsigned kind = 0;                     // 0 bits flipped, 1 every byte zero, 2 every byte random, 3 zeros turned to bytes
    uint64_t next_burst = ~0ull, burst_left = 0;
    double aimed[KINDS] = {};              // the chance for a packet of each kind
};

struct Consumer { double run_mean = 0, stall_mean = 0; unsigned every = 1; uint64_t until = 0; bool running = true; };   // run_mean 0: never stops

struct Traffic { double len_lo = 1, len_hi = 1, gap_lo = 0, gap_hi = 0; uint64_t next = ~0ull; bool cut_random = false; };

struct Totals {
    uint64_t runs = 0, failed = 0, cycles = 0, tail = 0, tail_max = 0, packets = 0, words_down = 0, words_up = 0;
    uint64_t fate[unsigned(Fate::COUNT)][KINDS] = {};
    uint64_t sent[KINDS] = {}, bad_header = 0, bad_data = 0, bad_fields = 0, refused = 0, ahead = 0, behind = 0, went_back = 0;
    uint64_t restarts = 0, kept_bad = 0, made_wrong = 0, ack_behind = 0, ack_beyond = 0, wraps = 0, wraps_again = 0;
    uint64_t stop_in_recovery = 0, header_in_recovery = 0, both_lost = 0, bursts = 0, burst_cycles = 0, flips = 0, stalls = 0;
    uint64_t bytes_data = 0, bytes_status = 0, bytes_round = 0;
    uint64_t most_kept = 0, most_kept_host = 0, most_fill = 0, most_fwd = 0, most_waited = 0, most_turn = 0, hop_wait = 0, end_wait = 0, reply_wait = 0;
    uint64_t recoveries = 0, recovery_sum = 0, recovery_max = 0, recovery_max_seed = 0, by_nodes[17] = {};
    double goodput_sum = 0;
};

struct Options {
    uint64_t seed_from = 1, seed_to = 1, cycles = 1000000;
    int nodes = 0; std::string rules = "proposed"; int host_window = -1, status = 0, plant = 0;
    bool quiet_wires = false, saturate = false, forge = false, long_noise = false, verbose = false;
    std::string forge_not;                 // forged fates left out, by name, between commas
    uint64_t seconds = 0;                  // start no new seed after this long; 0: no limit
};

// One run. Returns "" or the property that failed.
std::string run(const Options& o, uint64_t seed, Totals& tot) {
    Rng rng(seed);
    ChainConfig cfg;
    std::string err;
    if (!apply_rules(o.rules, cfg.p, err)) return err;
    static const unsigned lengths[] = {1, 1, 1, 1, 2, 2, 2, 4, 4, 16};
    cfg.nodes = o.nodes ? unsigned(o.nodes) : lengths[rng.below(10)];
    const bool real_width = rng.chance(0.4);
    cfg.p.number_bits = real_width ? 18 : unsigned(4 + rng.below(7));
    cfg.p.window = std::min<unsigned>(8, (cfg.p.last_number() - 1) / 2);
    const unsigned host_windows[] = {0, 0, 0, 16, 64};
    cfg.host_window = o.host_window >= 0 ? unsigned(o.host_window) : host_windows[rng.below(5)];
    if (cfg.host_window > (cfg.p.last_number() - 1) / 2) cfg.host_window = (cfg.p.last_number() - 1) / 2;
    if (o.status) cfg.p.status_timer = unsigned(o.status);
    cfg.p.plant = Plant(o.plant);
    cfg.content = rng.chance(0.6) ? 0 : rng.chance(0.4) ? 1 : 2;
    for (unsigned w = 0; w < 2 * cfg.nodes; w++) cfg.delay.push_back(unsigned(rng.logu(1, 256)));
    Chain c(cfg);
    if (real_width && rng.chance(0.7)) {   // both ends of every link start just under the wrap
        const uint32_t n = cfg.p.last_number() - uint32_t(rng.below(40));
        for (unsigned e = 0; e < c.ends(); e++) for (auto& st : c.end(e).r.s) { st.last = st.acked = n; st.next = st.tail = c.host.nums.succ(n); }
    }
    // The processes' parameters.
    const unsigned pattern = o.quiet_wires ? 0 : unsigned(rng.below(6));   // 0 none; 1 one wire; 2 one link; 3 every wire; 4, 5 some
    const unsigned one = unsigned(rng.below(2 * cfg.nodes));
    std::vector<WireNoise> noise(2 * cfg.nodes);
    for (unsigned w = 0; w < noise.size(); w++) {
        const bool on = pattern == 3 || (pattern == 1 && w == one) || (pattern == 2 && w / 2 == one / 2) || (pattern >= 4 && rng.chance(0.5));
        if (!on) continue;
        WireNoise& nz = noise[w];
        if (rng.chance(0.5)) { nz.flip_rate = rng.logu(1e-9, 1e-3); nz.next_flip = rng.geometric(1 / nz.flip_rate); }
        if (rng.chance(0.6)) {
            nz.burst_gap = rng.logu(1e2, o.long_noise ? 1e7 : 1e6);
            nz.burst_ceiling = rng.logu(1, o.long_noise ? 3e8 : double(o.cycles) / 4);
            nz.density = rng.logu(1e-4, 0.5); nz.kind = unsigned(rng.below(4));
            nz.next_burst = rng.geometric(nz.burst_gap);
        }
        if (rng.chance(0.5)) for (unsigned k = 1; k < KINDS; k++) if (rng.chance(0.6)) nz.aimed[k] = rng.logu(1e-4, 0.5);
    }
    std::vector<Consumer> eat(c.n_queues());
    for (auto& e : eat) if (rng.chance(0.6)) { e.run_mean = rng.logu(1, 1e6); e.stall_mean = rng.logu(1, 1e6); e.every = unsigned(rng.logu(1, 64)); }
    std::vector<Traffic> traffic(c.n_sources());
    for (unsigned i = 0; i < traffic.size(); i++) {
        Traffic& tr = traffic[i];
        if (!o.saturate && rng.chance(0.15)) continue;                     // nothing at all from this source
        if (i == 0) { tr.len_lo = 1; tr.len_hi = 100; tr.gap_lo = 10; tr.gap_hi = rng.logu(1e2, 1e6); }
        else if (i == 1) { tr.len_lo = 1; tr.len_hi = rng.logu(10, 1e5); tr.gap_lo = 0; tr.gap_hi = rng.logu(1, 1e6); }
        else { tr.len_lo = 1; tr.len_hi = rng.logu(2, 2000); tr.gap_lo = 100; tr.gap_hi = rng.logu(1e3, 1e6); }
        if (o.saturate) { tr.gap_lo = 0; tr.gap_hi = 1; if (i == 1) tr.len_hi = 1e4; }
        tr.cut_random = rng.chance(0.3);
        tr.next = uint64_t(rng.logu(1, 1e4));
    }
    const bool queries = rng.chance(0.3);                                  // every node replies at once, at a query
    uint64_t next_query = queries ? uint64_t(rng.logu(1e3, 1e5)) : ~0ull;
    // The run.
    uint64_t fates[unsigned(Fate::COUNT)][KINDS] = {}, wraps = 0, wraps_again = 0, stop_in_recovery = 0, header_in_recovery = 0, bursts = 0, burst_cycles = 0, flips = 0, stalls = 0;
    auto let_frame = [&](unsigned i, uint64_t words) {                     // a frame, cut into packets; it ends at a packet's end
        ChainSource& so = c.src[i];
        while (words) {
            const uint64_t most = cfg.p.max_words;
            const uint8_t len = uint8_t(std::min<uint64_t>(words, traffic[i].cut_random ? 1 + rng.below(most) : most));
            so.let(len); words -= len;
            if (i == 0) c.fg_let.push_back(c.t); else if (i >= 2) c.reply_let[i - 2].push_back(c.t);
        }
    };
    while (c.t < o.cycles && c.bad.empty()) {
        // The traffic.
        for (unsigned i = 0; i < traffic.size(); i++) {
            Traffic& tr = traffic[i];
            if (c.t < tr.next) continue;
            const uint64_t backlog = c.src[i].let_words - c.src[i].made_words;
            if (backlog < (i >= 2 ? cfg.out_words : 20000)) let_frame(i, uint64_t(rng.logu(tr.len_lo, tr.len_hi)));
            tr.next = c.t + 1 + uint64_t(rng.logu(tr.gap_lo + 1, tr.gap_hi + 1));
        }
        if (c.t >= next_query) {
            for (unsigned i = 2; i < traffic.size(); i++) if (c.src[i].let_words - c.src[i].made_words < cfg.out_words) let_frame(i, 1 + rng.below(72));
            next_query = c.t + uint64_t(rng.logu(1e3, 1e6));
        }
        // The consumers.
        for (unsigned q = 0; q < eat.size(); q++) {
            Consumer& e = eat[q];
            if (e.run_mean && c.t >= e.until) { e.running = !e.running; e.until = c.t + rng.geometric(e.running ? e.run_mean : e.stall_mean); stalls += !e.running; }
            if (e.running && c.fill[q] && c.t % e.every == 0) c.fill[q]--;
        }
        // The noise on the byte each wire is about to carry.
        for (unsigned w = 0; w < noise.size(); w++) {
            WireNoise& nz = noise[w];
            Wire& wi = c.wire[w];
            if (nz.flip_rate) {
                if (nz.next_flip < 8) { wi.next_xor ^= uint8_t(1u << nz.next_flip); nz.next_flip += rng.geometric(1 / nz.flip_rate); flips++; }
                nz.next_flip -= 8;
            }
            if (nz.burst_gap) {
                if (!nz.burst_left && c.t >= nz.next_burst) { nz.burst_left = uint64_t(rng.logu(1, nz.burst_ceiling)); bursts++; }
                if (nz.burst_left) {
                    burst_cycles++;
                    if (nz.kind == 0) { for (unsigned b = 0; b < 8; b++) if (rng.chance(nz.density)) wi.next_xor ^= uint8_t(1u << b); }
                    else if (rng.chance(std::min(1.0, nz.density * 8))) {
                        if (nz.kind == 1) wi.next_zero = true;
                        else if (nz.kind == 2) { wi.next_random = true; wi.random_byte = uint8_t(rng.next()); }
                        else wi.next_or = uint8_t(1 + rng.below(255));
                    }
                    if (--nz.burst_left == 0) nz.next_burst = c.t + rng.geometric(nz.burst_gap);
                }
            }
        }
        c.step();
        // Damage aimed at the packet that has just started; and what is counted from outside.
        for (unsigned w = 0; w < noise.size(); w++) {
            if (!c.started(w)) continue;
            const LinkEnd& le = c.end(w & 1 ? c.lower(w / 2) : c.upper(w / 2));
            const unsigned k = unsigned(le.tap.kind);
            const Header h = unpack(le.tap.hdr);
            if (k == unsigned(Kind::DATA) || k == unsigned(Kind::RESENT)) {
                const uint32_t n = h.number[h.stream - 1];
                if (n == 1 || n == le.nums.last) { (k == unsigned(Kind::DATA) ? wraps : wraps_again)++; }
            }
            if (noise[w].aimed[k] && rng.chance(noise[w].aimed[k])) {
                Fate f;
                do f = Fate(1 + rng.below(o.forge ? unsigned(Fate::COUNT) - 1 : 4)); while ((f == Fate::DATA || f == Fate::FORGE_ACK_DATA) && le.tap.bytes == HEADER_BYTES);
                if (o.forge_not.find(std::string(",") + fate_name(f) + ",") != std::string::npos) f = Fate::HEADER;
                c.fate(w, f); fates[unsigned(f)][k]++;
            }
        }
        for (unsigned e = 0; e < c.ends(); e++) {
            const LinkEnd& le = c.end(e);
            if (!le.running() && (le.ev.stop_taken[FG] >= 0 || le.ev.stop_taken[BG] >= 0)) stop_in_recovery++;
            if (le.ev.rx_bad_header && (le.r.answer || le.count.rx_bad_header > 1) && !le.running()) header_in_recovery++;
        }
    }
    // The end: no more traffic, no more noise, every queue emptied. Everything must arrive.
    const uint64_t at_end = c.t;
    if (c.bad.empty()) c.quiet(6000000);
    // What it came to.
    uint64_t down = 0, up = 0, packets = 0;
    for (unsigned i = 0; i < cfg.nodes; i++) { down += c.got_down[2 * i] + c.got_down[2 * i + 1]; up += c.got_up[i]; }
    tot.runs++; tot.cycles += c.t; tot.tail += c.t - at_end; tot.tail_max = std::max(tot.tail_max, c.t - at_end);
    tot.by_nodes[cfg.nodes]++;
    for (unsigned e = 0; e < c.ends(); e++) {
        const Counts& n = c.end(e).count;
        for (unsigned k = 0; k < KINDS; k++) { tot.sent[k] += n.sent[k]; packets += n.sent[k]; }
        tot.bad_header += n.rx_bad_header; tot.bad_data += n.rx_bad_data; tot.bad_fields += n.rx_bad_fields; tot.refused += n.refused;
        tot.ahead += n.ahead; tot.behind += n.behind; tot.went_back += n.went_back; tot.restarts += n.restarts; tot.kept_bad += n.kept_bad;
        tot.made_wrong += n.made_wrong; tot.ack_behind += n.ack_behind; tot.ack_beyond += n.ack_beyond;
        tot.bytes_data += n.bytes_data; tot.bytes_status += n.bytes_status; tot.bytes_round += n.bytes_round;
        (e == 0 ? tot.most_kept_host : tot.most_kept) = std::max<uint64_t>(e == 0 ? tot.most_kept_host : tot.most_kept, n.most_kept);
    }
    tot.both_lost += [&] { uint64_t b = 0; for (unsigned e = 0; e < c.ends(); e++) b += c.end(e).count.sent[unsigned(Kind::BOTH)]; return b; }();
    for (unsigned f = 0; f < unsigned(Fate::COUNT); f++) for (unsigned k = 0; k < KINDS; k++) tot.fate[f][k] += fates[f][k];
    tot.packets += packets; tot.words_down += down; tot.words_up += up;
    tot.wraps += wraps; tot.wraps_again += wraps_again; tot.stop_in_recovery += stop_in_recovery; tot.header_in_recovery += header_in_recovery;
    tot.bursts += bursts; tot.burst_cycles += burst_cycles; tot.flips += flips; tot.stalls += stalls;
    for (auto f : c.most_fill) tot.most_fill = std::max<uint64_t>(tot.most_fill, f);
    for (const auto& nd : c.node) for (unsigned s = 0; s < STREAMS; s++) tot.most_fwd = std::max<uint64_t>({tot.most_fwd, nd.fwd_down[s].most, nd.fwd_up[s].most});
    tot.most_waited = std::max<uint64_t>(tot.most_waited, c.most_waited); tot.most_turn = std::max<uint64_t>(tot.most_turn, c.most_turn);
    tot.hop_wait = std::max(tot.hop_wait, c.hop_wait_max); tot.end_wait = std::max(tot.end_wait, c.end_wait_max); tot.reply_wait = std::max(tot.reply_wait, c.reply_wait_max);
    tot.recoveries += c.recoveries; tot.recovery_sum += c.recovery_sum;
    if (c.recovery_max > tot.recovery_max) { tot.recovery_max = c.recovery_max; tot.recovery_max_seed = seed; }
    tot.goodput_sum += double(c.got_down[2 * (cfg.nodes - 1)] + c.got_down[2 * (cfg.nodes - 1) + 1]) * WORD_BYTES / double(c.t);
    if (!c.bad.empty()) tot.failed++;
    if (o.verbose || !c.bad.empty())
        printf("seed %llu: %u nodes, %u-bit numbers, host window %u, wires %u; %llu cycles, %llu packets, %llu words down, %llu up; rest in %llu: %s\n",
               (unsigned long long)seed, cfg.nodes, cfg.p.number_bits, c.host.p.window, pattern, (unsigned long long)c.t, (unsigned long long)packets,
               (unsigned long long)down, (unsigned long long)up, (unsigned long long)(c.t - at_end), c.bad.empty() ? "ok" : ("FAILED: " + c.bad).c_str());
    return c.bad;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto num = [&]() { return i + 1 < argc ? strtoull(argv[++i], nullptr, 0) : 0; };
        if (a == "--seeds") { o.seed_from = num(); o.seed_to = num(); }
        else if (a == "--seed") o.seed_from = o.seed_to = num();
        else if (a == "--cycles") o.cycles = num();
        else if (a == "--nodes") o.nodes = int(num());
        else if (a == "--rules") o.rules = argv[++i];
        else if (a == "--host-window") o.host_window = int(num());
        else if (a == "--status") o.status = int(num());
        else if (a == "--plant") o.plant = int(num());
        else if (a == "--quiet-wires") o.quiet_wires = true;
        else if (a == "--saturate") o.saturate = true;
        else if (a == "--forge") o.forge = true;
        else if (a == "--long-noise") o.long_noise = true;
        else if (a == "--seconds") o.seconds = num();
        else if (a == "--forge-not") { o.forge = true; o.forge_not = std::string(",") + argv[++i] + ","; }
        else if (a == "-v") o.verbose = true;
        else { printf("linkrun: unknown option %s\n", a.c_str()); return 2; }
    }
    Totals t;
    const time_t began = time(nullptr);
    uint64_t last = o.seed_from;
    for (uint64_t seed = o.seed_from; seed <= o.seed_to; seed++) {
        if (o.seconds && uint64_t(time(nullptr) - began) >= o.seconds) break;
        run(o, seed, t); last = seed;
    }
    o.seed_to = last;
    printf("\nlinkrun: rules %s; seeds %llu to %llu; %llu cycles of traffic and noise each%s%s%s\n", o.rules.c_str(), (unsigned long long)o.seed_from,
           (unsigned long long)o.seed_to, (unsigned long long)o.cycles, o.quiet_wires ? "; whole wires" : "", o.saturate ? "; every source always sending" : "", o.forge ? "; forged headers among the damage" : "");
    printf("runs %llu (chains of 1, 2, 4, 16: %llu, %llu, %llu, %llu); FAILED %llu\n", (unsigned long long)t.runs, (unsigned long long)t.by_nodes[1],
           (unsigned long long)t.by_nodes[2], (unsigned long long)t.by_nodes[4], (unsigned long long)t.by_nodes[16], (unsigned long long)t.failed);
    printf("cycles %llu; packets %llu; words delivered: %llu from the host (every node counted), %llu to it\n", (unsigned long long)t.cycles,
           (unsigned long long)t.packets, (unsigned long long)t.words_down, (unsigned long long)t.words_up);
    printf("coming to rest after the noise: %llu cycles on average, %llu at most\n", (unsigned long long)(t.runs ? t.tail / t.runs : 0), (unsigned long long)t.tail_max);
    printf("bytes sent: %llu in packets with data, %llu in packets of length 0 in step (%.2f%% of the data's), %llu in recovery and restart rounds\n",
           (unsigned long long)t.bytes_data, (unsigned long long)t.bytes_status, t.bytes_data ? 100.0 * double(t.bytes_status) / double(t.bytes_data) : 0, (unsigned long long)t.bytes_round);
    printf("the last node's bytes of data a cycle, averaged over the runs: %.4f\n", t.runs ? t.goodput_sum / double(t.runs) : 0);
    printf("the most: packets a node kept on a stream %llu, the host %llu; words in a queue %llu; words in a forwarding buffer %llu\n", (unsigned long long)t.most_kept,
           (unsigned long long)t.most_kept_host, (unsigned long long)t.most_fill, (unsigned long long)t.most_fwd);
    printf("the longest: a foreground packet waiting for the line with nothing else to wait for %llu cycles; a sender without an acknowledgement on a link in step %llu\n",
           (unsigned long long)t.most_turn, (unsigned long long)t.most_waited);
    printf("the longest, stalls and noise included: a foreground packet from its turn at the host to the first node %llu, to the last %llu; a reply from let out to the host %llu\n",
           (unsigned long long)t.hop_wait, (unsigned long long)t.end_wait, (unsigned long long)t.reply_wait);
    printf("recoveries (a link's last damaged byte to its next delivery, or to nothing owed): %llu, %llu cycles on average, %llu at most (seed %llu)\n", (unsigned long long)t.recoveries,
           (unsigned long long)(t.recoveries ? t.recovery_sum / t.recoveries : 0), (unsigned long long)t.recovery_max, (unsigned long long)t.recovery_max_seed);
    printf("\nrare events, counted:\n");
    printf("  %-18s", "damage aimed at");
    for (unsigned k = 1; k < KINDS; k++) printf(" %13s", KIND_NAMES[k]);
    printf("\n");
    for (unsigned f = 1; f < unsigned(Fate::COUNT); f++) {
        if (f > unsigned(Fate::DATA) && !o.forge) continue;
        printf("  %-18s", fate_name(Fate(f)));
        for (unsigned k = 1; k < KINDS; k++) printf(" %13llu", (unsigned long long)t.fate[f][k]);
        printf("\n");
    }
    struct { const char* name; uint64_t n; } rows[] = {
        {"packets sent: data", t.sent[1]}, {"sent again", t.sent[2]}, {"status (length 0, in step)", t.sent[3]}, {"\"lost\"", t.sent[4]}, {"\"recovery\"", t.sent[5]},
        {"\"lost\" and \"recovery\" together (both ends lost)", t.sent[6]}, {"restart", t.sent[7]},
        {"header CRC failed", t.bad_header}, {"data checksum failed", t.bad_data}, {"a field that cannot be", t.bad_fields},
        {"a header CRC failed during a recovery", t.header_in_recovery}, {"a stop bit taken during a recovery", t.stop_in_recovery},
        {"a packet ahead of the one expected, or a number ahead", t.ahead}, {"a packet seen before", t.behind}, {"a packet with no room (G11)", t.refused},
        {"a sender went back", t.went_back}, {"the number wrapped on a new packet", t.wraps}, {"the number wrapped on a packet sent again", t.wraps_again},
        {"bursts of noise", t.bursts}, {"cycles inside a burst", t.burst_cycles}, {"single bits flipped", t.flips}, {"consumer stalls", t.stalls},
        {"an acknowledgement behind (G20)", t.ack_behind}, {"an acknowledgement beyond what was sent (G20)", t.ack_beyond},
        {"a link started again", t.restarts}, {"a kept packet found bad (G21)", t.kept_bad}, {"a packet made wrong (G22)", t.made_wrong}};
    for (const auto& r : rows) printf("  %-58s %14llu%s\n", r.name, (unsigned long long)r.n, r.n ? "" : "   <- never happened");
    return t.failed != 0;
}
