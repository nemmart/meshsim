// suite_net.cpp (WORKER22, P4, inventory C3): v1.3's NetTest (sim/ref/meshsim/test/NetTest.java) on W20's harness
// (tests/mach.h's Net, the coreless machine; tests/suite.h's T). Every assertion and printed line is Java's, in Java's
// order, so the output equals run_tests.sh's logs: net (`-1`, and `-1 quick` in quick mode), net-shuf (`5 quick`),
// net-shuf2 (`11 quick`), skewtable (`-1 skewtable 40`); fixtures tests/p4suites/, from oracle/ref/p4suites.sh.
//
//   suite_net [seed] [quick | skewtable [N]] [--only=GROUP,...]
//
// --only (not Java's) runs the named groups alone, for checking a group's lines against the fixture before the whole
// suite is ported: structure unicast broadcast routes cncport random fullrate director injection loopback fanin.
//
// STATUS. Ported: structure, unicast, routes, random (in-order random, the queue peaks), fullrate, fanin, skewtable.
// Waiting on W20 (sessions/WORKER22/NOTES.md, asks 1-6): broadcast, cncport, director, injection, loopback. Until
// they land, a full run's counts and lines are short of the fixture's.
//
// Test-side code (the configuration regime, D6).
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mach.h"
#include "p4_java.h"
#include "suite.h"
#include "units/director.h"
#include "units/inject.h"
#include "units/linkword.h"
#include "units/regblock.h"
#include "units/router.h"

using namespace mach;
namespace lw = units::lw;
using units::Router;
using suite::check;
using suite::eq;

namespace {

int64_t shuffle = -1;
std::string report;
std::set<std::string> only;

void out(const std::string& s) { report += s; report += '\n'; }
bool want(const char* g) { return only.empty() || only.count(g); }
void no_violations(Chip& c, const char* what) {
    c.scan_overflows();
    if (c.violations() == 0) check(true, "%s", what);
    else check(false, "%s: %s", what, c.violation_report().c_str());
}

const char* const OUT_NAME[] = {"to-right", "to-left", "to-down", "to-up", "eject", "to-cnc"};
const char* const PORT_NAME[] = {"from-left", "from-right", "from-up", "from-down", "loopback", "from-cnc"};

Router& router(Chip& c, unsigned k) { return c.tile(k).unit<Router>("router"); }
const units::Hops& hops_of_chip(Chip& c) { return *router(c, 0).hops; }
int hops_of(const units::Hops& h, int k) { return std::abs(int(h.a[k])) + std::abs(int(h.b[k])); }
unsigned digit(uint64_t key, unsigned d) { return d == 0 ? 0 : unsigned(key >> (8 * (d - 1)) & 0xFF); }   // LinkWord.digit

// ------------------------------------------------------------------ structure
void structure() {
    suite::group("structure");
    Net n(Net::cfg(shuffle));
    int zero = 0, one = 0;
    const std::vector<int> sk = n.c.start_skew();
    for (int i = 0; i < 64; i++) {
        for (int d = 0; d < 4; d++) eq(3, n.c.cfg().link_latency, "link t%d dir %d latency", i, d);   // one latency for every link (H1)
        if (sk[i] == 0) zero++; else if (sk[i] == 1) one++;
    }
    eq(64, zero + one, "every start skew is 0 or +1");
    check(zero > 0 && one > 0, "both skews occur (seed 1): %d / %d", zero, one);
    MachConfig k = Net::cfg(shuffle); k.skew = false;
    { Chip c0(k); int s = 0; for (int x : c0.start_skew()) s += x; eq(0, s, "skew off: all 0"); }
    const uint64_t h = lw::hdr(0x55, 0x2A, 6, 5, 0xFF1ABCDE, lw::SIZE_64);
    eq(0x55, lw::src(h), "src"); eq(0x2A, lw::dst(h), "dst"); eq(6, lw::mode(h), "mode"); eq(5, lw::counter(h), "counter");
    eq(int32_t(0xFF1ABCDE), int32_t(lw::addr(h)), "address (32 bits)"); eq(lw::SIZE_64, lw::size(h), "size");
    eq(0, int64_t(h >> 54), "header fits 54 bits");
    const uint64_t h7 = lw::hdr(0x7F, 0x7F, 7, 7, 0xFFFFFFFFu, 3);
    eq(7, lw::mode(h7), "mode 7 round-trips (3 bits)"); eq(7, lw::counter(h7), "counter 7 beside it"); eq(-1, int32_t(lw::addr(h7)), "address all ones");
    eq(3, lw::size(h7), "size 11 round-trips"); eq(0, int64_t(h7 >> 54), "every field set: still 54 bits");
    for (unsigned sz = 0; sz < 3; sz++) { eq(1 << sz, lw::chunks(sz), "size %u: 1 << size chunks", sz); eq(4 << sz, lw::bytes(sz), "size %u: 4 << size bytes", sz); }
    check(!lw::read_port_tag(h) && lw::read_port_tag(h | lw::TAG_READPORT), "the simulator-only read-port tag is header bit 63");
    eq(0, int64_t(lw::TAG_READPORT & lw::HDR_MASK), "the tags are outside the link word's 54 header bits");
    eq(0x0300040C, lw::sram_addr(0x103), "the packet address of chunk c is 0x03000000 | c << 2 (1, Addresses)");
    using lw::bad_packet;
    check(!bad_packet(lw::M_WRITE, lw::SIZE_128, 0x00000010, 1), "mode 0, 128 bits at 0x10: good");
    check(bad_packet(lw::M_WRITE, lw::SIZE_128, 0x00000018, 1), "mode 0, 128 bits at 0x18: misaligned");
    check(bad_packet(lw::M_WRITE, lw::SIZE_64, 0x01000004, 1), "mode 0, 64 bits at 0x01000004: misaligned");
    check(!bad_packet(lw::M_WRITE, lw::SIZE_32, 0x03000004, 1), "mode 0, 32 bits into big SRAM: good");
    check(bad_packet(lw::M_WRITE, lw::SIZE_32, 0x02000000, 1), "mode 0 to the boot ROM: bad");
    check(bad_packet(lw::M_WRITE, lw::SIZE_32, 0x07000000, 7), "mode 0 to region 0x07: bad");
    check(bad_packet(lw::M_WRITE, lw::SIZE_32, 0xFFFFF800, 7), "mode 0 to region 0xFF: bad");
    check(!bad_packet(lw::M_WRITE, lw::SIZE_32, 0x04000000, 1), "mode 0 to peripheral 1, present: good");
    check(bad_packet(lw::M_WRITE, lw::SIZE_32, 0x05000000, 1), "mode 0 to peripheral 2, absent: bad");
    check(bad_packet(lw::M_AUG, lw::SIZE_128, 0, 1), "mode 3 at 128 bits: bad");
    check(!bad_packet(lw::M_SRC, lw::SIZE_64, 0xFF000004, 0), "mode 1 ignores the address, region included");
    check(!bad_packet(lw::M_COUNT, lw::SIZE_128, 0x02000001, 0), "mode 5 ignores it too");
    check(!bad_packet(6, lw::SIZE_32, 0x02000001, 0), "modes 6-7 are not bad packets (discarded silently)");
    eq(0x13, lw::src(lw::with_src(h, 0x13)), "stamp src");
    eq(6, lw::mode(lw::with_src(h, 0x13)), "stamp keeps mode"); eq(int32_t(0xFF1ABCDE), int32_t(lw::addr(lw::with_src(h, 0x13))), "and the address");
    eq(0x78, digit(0x12345678ull, 1), "digit 1 = LS byte"); eq(0x12, digit(0x12345678ull, 4), "digit 4");
    eq(0, digit(0x12345678ull, 0), "digit 0 = none");
    out(suite::str("structure: links all 3 cycles; start skew seed 1: %d tiles +0, %d tiles +1", zero, one));
}

// ------------------------------------------------------------------ unicast, every offset
void reset_stats(Chip& c) {
    (void)c;   // W20 ask 2 (Chip::reset_stats, FATAL today): only allPermPeaks1 reads the peaks, and a running maximum
               // over offsets is <= 1 exactly when every offset's is, so the check's outcome is the same meanwhile
}

void unicast() {
    suite::group("unicast");
    Net n(Net::cfg(shuffle));
    const units::Hops& hops = hops_of_chip(n.c);
    std::map<int, int> lat_by_hops;
    bool all_perm_peaks1 = true;
    for (int k = 0; k < 64; k++) {
        n.clear_logs(); reset_stats(n.c);
        const int64_t T0 = n.c.cycle() + 2;
        for (int i = 0; i < 64; i++) n.at(i, T0, (i + k) & 63, lw::M_SRC, 0, Net::tagged(k, T0));
        check(n.run_all(500), "offset %d drained", k);
        int lat = -1; bool same = true;
        for (int i = 0; i < 64; i++) {
            const auto l = n.log((i + k) & 63);
            eq(1, int64_t(l.size()), "offset %d: tile %d receives one packet", k, (i + k) & 63);
            if (l.size() != 1) continue;
            const auto& e = l[0];
            eq(i, lw::src(e[2]), "offset %d: src stamped", k);
            eq((i + k) & 63, lw::dst(e[2]), "offset %d: dst kept", k);
            eq(lw::M_SRC, lw::mode(e[2]), "offset %d: mode kept", k);
            const int L = int(int64_t(e[0]) - T0);
            if (lat < 0) lat = L; else if (L != lat) same = false;
        }
        check(same, "offset %d: every tile the same latency (lockstep permutation)", k);
        if (n.max_router_peak() > 1) all_perm_peaks1 = false;
        const int h = hops_of(hops, k);
        const auto it = lat_by_hops.find(h);
        const bool had = it != lat_by_hops.end();
        const int prev = had ? it->second : 0;
        if (!had) lat_by_hops[h] = lat;
        check(!had || prev == lat, "offset %d: latency depends on hop count only (%d hops: %d vs %s)", k, h, lat,
              had ? std::to_string(prev).c_str() : "null");
    }
    check(all_perm_peaks1, "a one-packet permutation never queues (every router FIFO peak <= 1)");
    eq(0, n.late(), "no late injections"); no_violations(n.c, "unicast");
    const int base = lat_by_hops[0];
    std::string sb = "unicast latency, injection point take -> director accept [meas: NetTest unicast], by hops h:";
    for (const auto& e : lat_by_hops) {
        sb += suite::str(" h=%d:%d", e.first, e.second);
        eq(base + 4LL * e.first, e.second, "latency(h) = latency(0) + h * (link 3 + 1)");
    }
    out(sb + suite::str("  (= %d + 4h; spec 5's estimate h(k + 2) + 4 = 5h + 4)", base));
    // a lone packet in an idle machine has the same latency as the lockstep permutation
    n.clear_logs();
    const int64_t T0 = n.c.cycle() + 2;
    n.at(17, T0, (17 + 27) & 63, lw::M_SRC, 0, 0);
    n.run_all(200);
    eq(base + 4LL * hops_of(hops, 27), int64_t(n.log((17 + 27) & 63).at(0)[0]) - T0, "lone packet latency");
}

// ------------------------------------------------------------------ unicast vs broadcast routes (in-order scope)
std::vector<int> path(Router* const* r, int src, int dst) {
    std::vector<int> p; int at = src; p.push_back(at);
    for (int i = 0; i < 20; i++) {
        const unsigned m = r[at]->unicast(unsigned(src), unsigned(dst));
        if (m & (Router::M_EJ | Router::M_CNC)) return p;
        at = (m & Router::M_RIGHT) ? (at + 1) & 63 : (m & Router::M_LEFT) ? (at + 63) & 63 : (m & Router::M_DOWN) ? (at + 8) & 63 : (at + 56) & 63;
        p.push_back(at);
    }
    std::fprintf(stderr, "FATAL suite_net: no route %d -> %d\n", src, dst); std::abort();
}
std::vector<int> bpath(Router* const* r, int src, int dst) {
    std::map<int, int> parent; std::vector<std::array<int, 2>> q; size_t qh = 0;
    q.push_back({src, int(Router::LOOPBACK)}); parent[src] = -1;
    while (qh < q.size()) {
        const auto x = q[qh++]; const int at = x[0];
        const unsigned m = r[at]->broadcast(unsigned(x[1]), unsigned(src));
        const int nx[4][3] = {{int(Router::M_RIGHT), (at + 1) & 63, int(Router::FROM_LEFT)}, {int(Router::M_LEFT), (at + 63) & 63, int(Router::FROM_RIGHT)},
                              {int(Router::M_DOWN), (at + 8) & 63, int(Router::FROM_UP)}, {int(Router::M_UP), (at + 56) & 63, int(Router::FROM_DOWN)}};
        for (const auto& e : nx) if (m & unsigned(e[0])) {
            if (parent.count(e[1])) { std::fprintf(stderr, "FATAL suite_net: broadcast reaches %d twice\n", e[1]); std::abort(); }
            parent[e[1]] = at; q.push_back({e[1], e[2]});
        }
    }
    std::vector<int> p; for (int at = dst; at != -1; at = parent.at(at)) p.insert(p.begin(), at);
    return p;
}

void route_comparison() {
    suite::group("routes");
    Net n(Net::cfg(shuffle));
    Router* r[64]; for (int i = 0; i < 64; i++) r[i] = &router(n.c, unsigned(i));
    const units::Hops& hops = hops_of_chip(n.c);
    std::vector<int> differing;
    for (int k = 1; k < 64; k++) {
        bool differ = false;
        for (int s = 0; s < 64; s++) {
            const std::vector<int> u = path(r, s, (s + k) & 63), b = bpath(r, s, (s + k) & 63);
            eq(hops_of(hops, k), int64_t(u.size()) - 1, "unicast offset %d takes the minimal hop count", k);
            if (u != b) differ = true;
        }
        if (differ) differing.push_back(k);
    }
    out("routes: a unicast packet and a broadcast copy from one source to one destination take different paths for offsets " + p4::jlist_int(differing)
        + " (for 8 to 64 of the 64 sources each; the broadcast tree's copy can take 8 hops where the unicast takes at most 7), so in-order delivery holds within unicast and within broadcast, not between the two [NetTest routes]");
}

// ------------------------------------------------------------------ in-order delivery
void check_order(Net& n, const std::string& what) {
    int unicast_bad = 0, bcast_bad = 0; long count = 0;
    for (unsigned d = 0; d < 64; d++) {
        int64_t last_u[65], last_b[65];
        std::fill(last_u, last_u + 65, -1); std::fill(last_b, last_b + 65, -1);
        for (const auto& e : n.log(d)) {
            count++;
            const unsigned s = lw::src(e[2]); const int64_t seq = int64_t(e[1] >> 32);
            const bool b = lw::dst(e[2]) == lw::BROADCAST;
            int64_t* last = b ? last_b : last_u;
            if (seq <= last[s]) { if (b) bcast_bad++; else unicast_bad++; }
            last[s] = seq;
        }
    }
    eq(0, unicast_bad, "%s: unicast packets out of order per (src, dst)", what.c_str());
    eq(0, bcast_bad, "%s: broadcast copies out of order per (src, dst)", what.c_str());
    out(suite::str("%s: %ld packets delivered, in order per (src, dst) within unicast and within broadcast", what.c_str(), count));
}

// Chip.queuePeaks: per (output, input port) the highest peak over the tiles and its first tile; then the write engine's input
std::string queue_peaks(Chip& c) {
    c.scan_overflows();
    std::string sb;
    for (unsigned o = 0; o < 6; o++) for (unsigned port = 0; port < 6; port++) {
        int64_t best = -1; int at = -1;
        for (unsigned p = 0; p < 64; p++) {
            const Router& r = router(c, p);
            const unsigned f = r.slot_of[o][port];
            if (f != 0xFF && int64_t(r.q[r.fifo_index(o, f)].peak) > best) { best = r.q[r.fifo_index(o, f)].peak; at = int(p); }
        }
        if (best > 0) sb += suite::str("    %-22s %3lld  (t%d)\n", (std::string(OUT_NAME[o]) + "<" + PORT_NAME[port]).c_str(), (long long)best, at);
    }
    // the write engine's input peak (ref's weIn.highWater): the framework's Fifo keeps no peak yet (NOTES: ask 7)
    return sb;
}

void in_order_random() {
    suite::group("in-order random");
    Net n(Net::cfg(shuffle));
    const std::vector<int> sk = n.c.start_skew();
    p4::JRandom rnd(42);
    int64_t seq[64] = {}; int64_t sent = 0;
    for (int t = 0; t < 3000; t++)
        for (int s = 0; s < 64; s++) {
            const int64_t T0 = 2 + t + sk[s];
            if (rnd.next_double() < 0.002) { n.at(s, T0, lw::BROADCAST, lw::M_SRC, 0, uint64_t(seq[s]++) << 32); sent += 64; }
            else if (rnd.next_double() < 0.2) { const unsigned d = unsigned(rnd.next_int(64)); n.at(s, T0, d, lw::M_SRC, 0, uint64_t(seq[s]++) << 32); sent++; }
        }
    check(n.run_all(20000), "drained");
    int64_t got = 0; for (unsigned d = 0; d < 64; d++) got += int64_t(n.log(d).size());
    eq(sent, got, "every packet delivered");
    check_order(n, "random traffic (p = 0.2 unicast + 0.002 broadcast per tile per cycle, 3,000 cycles, skew on)");
    eq(0, n.late(), "no late injections"); no_violations(n.c, "random traffic");
    out("  queue peaks:\n" + queue_peaks(n.c));
}

void in_order_full_rate() {
    suite::group("in-order full rate");
    const int offsets[] = {1, 7, 8, 9, 27, 32, 33, 36, 63};
    unsigned worst = 0;
    for (int k : offsets) {
        Net n(Net::cfg(shuffle));
        const units::Hops& hops = hops_of_chip(n.c);
        const std::vector<int> sk = n.c.start_skew();
        const int P = std::max(1, std::max(std::abs(int(hops.a[k])), std::abs(int(hops.b[k]))));
        for (int s = 0; s < 64; s++) for (int j = 0; j < 500; j++) n.at(s, 2 + int64_t(j) * P + sk[s], (s + k) & 63, lw::M_SRC, 0, uint64_t(j) << 32);
        check(n.run_all(5000), "offset %d drained", k);
        for (unsigned d = 0; d < 64; d++) eq(500, int64_t(n.log(d).size()), "offset %d: tile %u receives 500", k, d);
        check_order(n, suite::str("paced at the link bound, offset %d (one per %d)", k, P));
        eq(0, n.late(), "offset %d: one packet per cycle sustained at every injection point", k);
        no_violations(n.c, suite::str("full rate offset %d", k).c_str());
        worst = std::max(worst, n.max_router_peak());
    }
    out(suite::str("permutations at the link bound (every tile 500 packets, one per max(|a|,|b|) cycles, skew 0/+1): router FIFO peak %u of 16 [meas: NetTest in-order full rate]", worst));
}

// ------------------------------------------------------------------ fan-in (spec 3.2's table) and the skew table
// Burst W packets from each sender to receiver r from the same cycle (+ start skew): {no overflow ? 1 : 0, r's eject
// peak, packets r received}; first_ovf gets the first overflowing FIFO's name (ref's hardwareFifos, earliest cycle,
// ref's order on a tie).
std::array<int, 3> fan_run(int r, const std::vector<int>& senders, int W, int64_t skew_seed, bool skew, std::string* first_ovf) {
    MachConfig k = Net::cfg(shuffle); k.skew_seed = skew_seed; k.skew = skew;
    Net n(k);
    const std::vector<int> sk = n.c.start_skew();
    for (int s : senders) for (int j = 0; j < W; j++) n.at(unsigned(s), 2 + j + sk[s], unsigned(r), lw::M_SRC, 0, uint64_t(j) << 32);
    n.run_all(100000);
    std::string first; uint64_t fc = UINT64_MAX; bool other = false;
    for (unsigned p = 0; p < 64; p++) {
        const Router& rt = router(n.c, p);
        for (unsigned o = 0; o < 6; o++) for (unsigned f = 0; f < Router::kNFeeders[o]; f++) {
            const Router::Fifo& q = rt.q[rt.fifo_index(o, f)];
            if (q.overflows && q.first_overflow < fc) { fc = q.first_overflow; first = suite::str("t%u.%s<%s", p, OUT_NAME[o], PORT_NAME[Router::kFeeders[o][f]]); }
        }
        // ref's other hardware FIFOs (the write engine's and GATHER's inputs, the table and HIST queues): their ERRORS bits
        const uint32_t cond = n.c.tile(p).block().conditions;
        using RB = units::RegBlockState;
        if (cond & (1u << RB::COND_WE_IN_OVF | 1u << RB::COND_GATHER_OVF | 1u << RB::COND_TABLE_OVF)) other = true;
    }
    if (other && first.empty()) { std::fprintf(stderr, "FATAL suite_net: a non-router FIFO overflowed (its name: W20's overflow scan)\n"); std::abort(); }
    if (first_ovf && !first.empty()) *first_ovf += first;
    unsigned peak = 0;
    const Router& rr = router(n.c, unsigned(r));
    for (unsigned f = 0; f < Router::kNFeeders[Router::EJECT]; f++) peak = std::max(peak, unsigned(rr.q[rr.fifo_index(Router::EJECT, f)].peak));
    const int got = int(n.log(unsigned(r)).size());
    return {first.empty() && !other ? 1 : 0, int(peak), got};
}

int largest_safe(int r, const std::vector<int>& senders, int64_t seed, bool skew) {
    int lo = 0, hi = 64;
    if (fan_run(r, senders, 64, seed, skew, nullptr)[0] == 1) return 64;
    while (hi - lo > 1) { const int mid = (lo + hi) / 2; if (fan_run(r, senders, mid, seed, skew, nullptr)[0] == 1) lo = mid; else hi = mid; }
    return lo;
}

std::vector<int> by_near(int r, const units::Hops& hops) {
    std::vector<int> v(63); for (int i = 0; i < 63; i++) v[i] = (r + 1 + i) & 63;
    std::stable_sort(v.begin(), v.end(), [&](int x, int y) {
        const int hx = hops_of(hops, (r - x) & 63), hy = hops_of(hops, (r - y) & 63);
        return hx != hy ? hx < hy : ((x - r) & 63) < ((y - r) & 63);
    });
    return v;
}
const units::Hops& default_hops() { static Chip* c = new Chip(MachConfig::defaults()); return hops_of_chip(*c); }

void skew_table(int seeds) {
    suite::group("fan-in under skew");
    const int r = 27;
    const std::vector<int> near_all = by_near(r, default_hops());
    const int counts[] = {63, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1};
    out(suite::str("fan-in with 0/+1 start skew [meas: NetTest skewtable %d]: receiver tile %d; every sender bursts W packets from cycle 2 + its skew;", seeds, r));
    out(suite::str("safe W = no FIFO overflow anywhere, for every sender set (nearest, farthest, 3 random) and every skew pattern (seeds 1..%d)", seeds));
    out(suite::str("  %-8s %-10s %-10s %-10s %-10s %-12s %s", "senders", "safe W", "nearest", "farthest", "random", "total", "W with no skew (nearest)"));
    for (int S : counts) {
        std::vector<int> nr(static_cast<size_t>(S)), fr(static_cast<size_t>(S));
        for (int i = 0; i < S; i++) { nr[i] = near_all[i]; fr[i] = near_all[62 - i]; }
        std::vector<std::vector<int>> sets = {nr, fr};
        for (int64_t rs = 1; rs <= 3; rs++) {
            std::vector<int> all = near_all; p4::JRandom jr(rs * 7919); p4::jshuffle(all, jr);
            sets.emplace_back(all.begin(), all.begin() + S);
        }
        std::vector<int> best(sets.size());
        for (size_t k = 0; k < sets.size(); k++) { best[k] = 64; for (int64_t seed = 1; seed <= seeds; seed++) best[k] = std::min(best[k], largest_safe(r, sets[k], seed, true)); }
        int w = 64; for (int b : best) w = std::min(w, b);
        const int rnd = std::min(best[2], std::min(best[3], best[4]));
        out(suite::str("  %-8d %-10s %-10d %-10d %-10d %-12s %d", S, w == 64 ? ">= 64" : std::to_string(w).c_str(), best[0], best[1], rnd,
                       w == 64 ? "-" : std::to_string(S * w).c_str(), largest_safe(r, nr, 1, false)));
    }
}

void fan_in(bool quick) {
    suite::group("fan-in");
    const int r = 27;
    const std::vector<int> near_all = by_near(r, default_hops());
    const int counts[] = {63, 32, 16, 8, 4, 1};
    const int spec[] = {1, 1, 3, 5, 21, 64};
    out(suite::str("fan-in to tile %d: largest W per sender with no FIFO overflow, every sender bursting W packets from the same cycle (+ its start skew) [meas: NetTest fan-in]", r));
    out(suite::str("  %-8s %-6s %-9s %-9s %-9s %-14s %-12s %s", "senders", "spec", "nearest", "farthest", "random*3", "nearest,noskew", "eject peak", "first overflow at W+1 (nearest)"));
    for (int ci = 0; ci < 6; ci++) {
        const int S = counts[ci];
        std::vector<int> nr(static_cast<size_t>(S)), fr(static_cast<size_t>(S));
        for (int i = 0; i < S; i++) { nr[i] = near_all[i]; fr[i] = near_all[62 - i]; }
        const int wn = largest_safe(r, nr, 1, true), wf = largest_safe(r, fr, 1, true);
        int wr = 64;
        if (!quick) for (int64_t seed = 1; seed <= 3; seed++) {
            std::vector<int> all = near_all; p4::JRandom jr(seed * 7919); p4::jshuffle(all, jr);
            wr = std::min(wr, largest_safe(r, std::vector<int>(all.begin(), all.begin() + S), seed, true));
        }
        const int wns = largest_safe(r, nr, 1, false);
        const std::array<int, 3> at = fan_run(r, nr, wn, 1, true, nullptr);
        eq(int64_t(S) * wn, at[2], "%d senders x %d: every packet arrives", S, wn);
        std::string fo;
        if (wn < 64) fan_run(r, nr, wn + 1, 1, true, &fo);
        out(suite::str("  %-8d %-6s %-9d %-9d %-9s %-14d %-12s %s", S, spec[ci] == 64 ? "none" : std::to_string(spec[ci]).c_str(), wn, wf,
                       quick ? "-" : std::to_string(wr).c_str(), wns, (std::to_string(at[1]) + " of 16").c_str(), wn < 64 ? fo.c_str() : "no overflow to 64"));
        check(std::min(wn, std::min(wf, wr)) >= 1 || S == 63, "at least one packet per sender is safe below 63 senders");
    }
    // 63 x 1 sits exactly at the eject FIFO's depth: how often does the 0/+1 start skew push it over?
    std::vector<int> all(63); for (int i = 0; i < 63; i++) all[i] = (r + 1 + i) & 63;
    const std::array<int, 3> ns = fan_run(r, all, 1, 1, false, nullptr);
    eq(0, ns[0], "63 x 1 with no skew: overflows (v1.0: a push at full overflows even with a same-cycle pop; M15: fitted at 16 of 16)");
    const int seeds = quick ? 10 : 40; int ovf = 0;
    for (int rr : {27, 0, 63}) {
        std::vector<int> s3(63); for (int i = 0; i < 63; i++) s3[i] = (rr + 1 + i) & 63;
        for (int64_t seed = 1; seed <= seeds; seed++) if (fan_run(rr, s3, 1, seed, true, nullptr)[0] == 0) ovf++;
    }
    out(suite::str("  63 x 1: overflows with no skew (v1.0's FIFO rule); with 0/+1 skew it overflows in %d of %d skew patterns (receivers 27, 0, 63; seeds 1..%d) [meas: NetTest fan-in]", ovf, 3 * seeds, seeds));
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a;
    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--only=", 7) == 0) {
            std::string s = argv[i] + 7; size_t p = 0;
            while (p <= s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); only.insert(s.substr(p, q - p)); p = q + 1; }
        } else a.push_back(argv[i]);
    }
    if (!a.empty()) shuffle = std::atoll(a[0].c_str());
    if (a.size() > 1 && a[1] == "skewtable") {
        skew_table(a.size() > 2 ? std::atoi(a[2].c_str()) : 20);
        std::fputs(report.c_str(), stdout);
        return suite::done("NetTest skewtable");
    }
    const bool quick = a.size() > 1 && a[1] == "quick";
    if (want("structure")) structure();
    if (want("unicast")) unicast();
    // broadcast: W20 asks 1, 2
    if (want("routes")) route_comparison();
    // cncPort: W20 asks 1, 6
    if (want("random")) in_order_random();
    if (want("fullrate")) in_order_full_rate();
    // director: W20 asks 4, 5, 6; injection, loopback: W20 ask 3
    if (want("fanin")) fan_in(quick);
    std::fputs(report.c_str(), stdout);
    return suite::done(std::string("NetTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : ""));
}
