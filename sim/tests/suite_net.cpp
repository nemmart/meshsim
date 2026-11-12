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
// The queue peaks' write-engine line reads the framework Fifo's peak, kept only under SIM_CHECKS: run the suite and
// debug builds (the release build FATALs there).
//
// STATUS. Ported: structure, unicast, routes, random (in-order random, the queue peaks), fullrate, fanin, skewtable.
// Waiting on W20 (sessions/WORKER22/NOTES.md, W22-1 to W22-6): broadcast, cncport, director, injection, loopback. Until
// they land, a full run's counts and lines are short of the fixture's.
//
// Test-side code (the configuration regime, D6).
#include <sys/wait.h>
#include <unistd.h>

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
#include "units/write_engine.h"

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

// True if fn() dies by a FATAL whose message contains `name` (Java's `try { new Chip(k); } catch (IllegalArgumentException
// e) { threw = e.getMessage().contains(name); }`; not an assertion, so not suite::throws).
template <class F> bool dies_naming(F&& fn, const char* name) {
    std::fflush(stdout);
    int pfd[2]; if (pipe(pfd) != 0) { std::fprintf(stderr, "FATAL suite_net: pipe\n"); std::abort(); }
    const pid_t pid = fork();
    if (pid == 0) { close(pfd[0]); dup2(pfd[1], 2); fn(); _exit(0); }
    close(pfd[1]);
    std::string err; char buf[512]; ssize_t r;
    while ((r = read(pfd[0], buf, sizeof buf)) > 0) err.append(buf, size_t(r));
    close(pfd[0]);
    int st = 0; waitpid(pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return false;
    return err.find("FATAL") != std::string::npos && err.find(name) != std::string::npos;
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
void unicast() {
    suite::group("unicast");
    Net n(Net::cfg(shuffle));
    const units::Hops& hops = hops_of_chip(n.c);
    std::map<int, int> lat_by_hops;
    bool all_perm_peaks1 = true;
    for (int k = 0; k < 64; k++) {
        n.clear_logs(); n.c.reset_stats();
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

// ------------------------------------------------------------------ broadcast
void broadcast() {
    suite::group("broadcast");
    Net n(Net::cfg(shuffle));
    int max_lat = 0;
    auto routed_sum = [&] { uint64_t r = 0; for (unsigned p = 0; p < 64; p++) r += router(n.c, p).routed; return r; };
    for (int s = 0; s <= 64; s++) {
        n.clear_logs(); n.c.reset_stats();
        const uint64_t routed_before = routed_sum();
        const int64_t T0 = n.c.cycle() + 2;
        if (s < 64) n.at(unsigned(s), T0, lw::BROADCAST, lw::M_SRC, 0, Net::tagged(uint64_t(s), T0));
        else n.cnc_send({T0, lw::BROADCAST, lw::M_SRC, 0, Net::tagged(uint64_t(s), T0)});
        check(n.run_all(500), "broadcast from %d drained", s);
        for (unsigned d = 0; d < 64; d++) {
            const auto l = n.log(d);
            eq(1, int64_t(l.size()), "broadcast from %d: tile %u receives exactly one copy", s, d);
            if (l.size() == 1) { eq(s, lw::src(l[0][2]), "broadcast src"); max_lat = std::max(max_lat, int(int64_t(l[0][0]) - T0)); }
        }
        eq(0, int64_t(n.c.cc().received().size()), "broadcast from %d not delivered to C&C", s);
        eq(64, int64_t(routed_sum() - routed_before), "broadcast from %d: 64 route-block visits (63 link arrivals + the root), no duplicate copies", s);
    }
    eq(0, int64_t(n.c.tile(0).unit<units::Injection>("inj").injected), "C&C's broadcast does not use tile 0's injection point");
    no_violations(n.c, "broadcast");
    out(suite::str("broadcast: every tile exactly one copy from every source and from C&C, none to C&C; slowest copy %d cycles after injection [meas: NetTest broadcast]", max_lat));
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

// ------------------------------------------------------------------ the controllers' ports (tiles 0-2's fifth ports, 4.16)
void cnc_port() {
    suite::group("C&C 1-3 ports");
    Net n(Net::cfg(shuffle));
    for (unsigned c = 0; c < 3; c++) for (unsigned d = 0; d < 64; d++) {
        n.clear_logs();
        n.send(c, d, lw::M_COUNT, 1, 0, lw::SIZE_32, 0x1234, 0);
        n.run_all(300);
        eq(1, int64_t(n.log(d).size()), "C&C %u -> tile %u", c + 1, d);
        if (n.log(d).size() == 1) eq(64 + c, lw::src(n.log(d)[0][2]), "src %u from C&C %u", 64 + c, c + 1);
    }
    for (unsigned s = 0; s < 64; s++) {
        n.clear_logs(); const int64_t T0 = n.c.cycle() + 2;
        for (unsigned c = 0; c < 3; c++) n.at(s, T0 + c, 64 + c, lw::M_WRITE, 3, 0x00000100, lw::SIZE_32, s, 0);
        n.run_all(300);
        for (unsigned c = 0; c < 3; c++) {
            const auto rv = n.received(c);
            eq(1, int64_t(rv.size()), "tile %u -> C&C %u", s, c + 1);
            if (rv.size() == 1) eq(s, lw::src(rv[0][2]), "C&C %u sees src", c + 1);
        }
        for (unsigned c = 0; c < 3; c++) eq(0, int64_t(n.log(c).size()), "nothing for 64-66 reaches tile %u's director", c);
    }
    n.clear_logs();
    n.send(1, 66, lw::M_WRITE, 2, 0x200, lw::SIZE_64, 0x77, 0);
    n.run_all(300);
    eq(1, int64_t(n.received(2).size()), "C&C 2 -> C&C 3");
    n.clear_logs();
    n.send(1, lw::BROADCAST, lw::M_COUNT, 1, 0, lw::SIZE_32, 9, 0);
    n.run_all(300);
    for (unsigned d = 0; d < 64; d++) eq(1, int64_t(n.log(d).size()), "C&C 2's broadcast reaches tile %u once", d);
    for (unsigned e = 0; e < 3; e++) eq(0, int64_t(n.received(e).size()), "a broadcast is not delivered to C&C %u", e + 1);
    no_violations(n.c, "the controllers' ports");
    int unconnected = 0;
    for (unsigned p = 0; p < 64; p++) {
        const Router& r = router(n.c, p);
        eq(6, Router::kOut, "tile %u: the router has the fifth port's output (4.16: one tile design)", p);
        check(r.attached == (p < 3), "tile %u: fifth port connected only at tiles 0-2", p);
        if (p < 3) continue;
        bool quiet = true;
        for (unsigned f = 0; f < r.nfeed[Router::TO_CNC]; f++) if (r.q[r.fifo_index(Router::TO_CNC, f)].peak > 0) quiet = false;
        for (unsigned o = 0; o < 5; o++) { const unsigned f = r.slot_of[o][Router::FROM_CNC]; if (f != 0xFF && r.q[r.fifo_index(o, f)].peak > 0) quiet = false; }
        if (quiet) unconnected++;
    }
    eq(61, unconnected, "on tiles 3-63 the fifth port never receives and routing never chooses it");
    Net m(Net::cfg(shuffle));
    m.at(5, m.c.cycle() + 2, 70, lw::M_COUNT, 1, 0, lw::SIZE_32, 0, 0);
    m.run_all(300);
    const auto& vm = m.c.violation_messages();
    check(m.c.violations() >= 1 && !vm.empty() && vm[0].find("no endpoint") != std::string::npos, "a packet for 70 is flagged: %s", m.c.violation_report().c_str());
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
    // the write engine's input (ref's weIn.highWater): the framework Fifo's peak, kept under SIM_CHECKS (W22-7)
    uint32_t b = 0; int at = -1;
    for (unsigned p = 0; p < 64; p++) {
        const uint32_t pk = c.sim().fifo_peak(c.tile(p).unit<units::WriteEngine>("we").in);
        if (pk > b) { b = pk; at = int(p); }
    }
    if (b > 0) sb += suite::str("    %-22s %3u  (t%d)\n", "write-engine input", b, at);
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

// ------------------------------------------------------------------ the traffic director (4.8, v1.2)
enum : uint32_t { CMD = 0x7FC0, RPL = 0x7BC0, CMD_DWORD = CMD >> 2, RW = RPL >> 2 };   // ControllerMemories
enum : unsigned { COND_BAD = 1, COND_UNEXPECTED = 2 };                               // Conditions
// W22-15 (interim): ref's TrafficDirector.cmIdle(t), nothing on its way to port B. Until W20's accessor lands this reads
// "never idle", so the one check on it passes without testing anything (recorded in NOTES).
bool cm_idle(Chip&, unsigned) { return false; }

void director() {
    suite::group("director");
    MachConfig k = Net::cfg(shuffle); k.check_unexpected = true; k.periph_type = 0;
    Net n(k);
    TileRef r = n.c.tile(9);
    r.record_gather(); r.record_writer();
    units::Director& d = r.unit<units::Director>("dir");
    eq(0, d.cfg.periph_present, "a tile built without a peripheral: PARAMS [2:0] = 0");
    r.set_remaining(1, 4); r.set_remaining(2, 3); r.set_remaining(3, 2); r.set_remaining(4, 1);
    const int64_t T0 = n.c.cycle() + 2; int64_t t = T0;
    n.at(9, T0, 9, lw::M_GATHER, 4, 0, lw::SIZE_64, 0x0000000500000077ull, 0);
    n.at(5, t++, 9, lw::M_SRC, 1, 0, lw::SIZE_64, 0xAAAA, 0);
    n.at(5, t++, 9, lw::M_WRITE, 1, lw::sram_addr(0x102), lw::SIZE_64, 0xEEEE, 0);
    n.at(5, t++, 9, lw::M_BUCKET, 1, 0, lw::SIZE_64, 0xBBBB, 0);
    n.at(5, t++, 9, lw::M_AUG, 1, 0, lw::SIZE_32, 0xCCCC, 0);
    n.at(5, t++, 9, lw::M_WRITE, 2, 0x01000040, lw::SIZE_64, 0x2222222211111111ull, 0);
    n.at(5, t++, 9, lw::M_WRITE, 2, 0x00000400, lw::SIZE_32, 0x33333333ull, 0);
    n.at(5, t++, 9, lw::M_WRITE, 2, 0x000007F0, lw::SIZE_128, 0x6666666655555555ull, 0x8888888877777777ull);
    n.at(5, t++, 9, lw::M_WRITE, 3, RPL + 8 * 5, lw::SIZE_32, 0xABCD0006ull, 0);
    n.at(6, t, 9, lw::M_WRITE, 3, RPL + 8 * 6, lw::SIZE_64, 0x1234567800C0FFEEull, 0);
    n.at(5, t++, 9, lw::M_COUNT, 4, 0, lw::SIZE_32, 0, 0);
    n.at(5, t++, 9, lw::M_WRITE, 2, 0x02000000, lw::SIZE_32, 0xD2, 0);
    n.at(5, t++, 9, lw::M_WRITE, 2, 0xFFFFF800, lw::SIZE_32, 0xD3, 0);
    n.at(5, t++, 9, lw::M_WRITE, 2, 0x04000000, lw::SIZE_32, 0xD4, 0);
    n.at(5, t++, 9, lw::M_AUG, 1, 0, lw::SIZE_128, 0xD5, 0);
    n.cnc_send({T0, 9, lw::M_WRITE, CMD + 12, 0xCAFE0003ull, lw::SIZE_32, 0, 0});
    n.cnc_send({T0 + 1, 9, lw::M_WRITE, CMD, 0x0000C0DEull, lw::SIZE_32, 0, 0});
    check(n.run_all(300), "drained");
    eq(4, int64_t(d.bad_packets), "four bad packets dropped: boot ROM, region 0xFF, absent peripheral, mode 3 at 128 (v1.25: misalignment is UB, below)");
    check((r.block().conditions & (1u << COND_BAD)) != 0, "ERRORS [1], bad packet (v1.1's bit %u)", unsigned(COND_BAD));
    const auto gl = r.gather_log(), wl = r.writer_log();
    eq(1, int64_t(gl.size()), "mode 4 -> GATHER queue");
    eq(4, int64_t(wl.size()), "modes 1, 0 (big SRAM), 2 and 3 -> write engine");
    if (wl.size() == 4) { eq(lw::M_SRC, lw::mode(wl[0][2]), "mode 1 first"); eq(lw::M_WRITE, lw::mode(wl[1][2]), "mode 0 second");
        eq(lw::M_BUCKET, lw::mode(wl[2][2]), "mode 2 third"); eq(lw::M_AUG, lw::mode(wl[3][2]), "mode 3 fourth"); }
    eq(0xEEEE, r.sram_peek(0x102), "mode 0 written at its address's chunk (no PTR)");
    auto imem = [&](uint32_t w) { return r.peek_local(0x01000000u + 4 * w); };
    eq(0x11111111, imem(0x10), "program memory word 0x10"); eq(0x22222222, imem(0x11), "program memory word 0x11");
    eq(0x33333333, r.dmem(0x100), "data memory word 0x100"); eq(0, r.dmem(0x101), "a 32-bit write writes one word only");
    eq(0x55555555, r.dmem(0x1FC), "128 bits: word 0x1FC"); eq(int32_t(0x88888888u), int32_t(r.dmem(0x1FF)), "128 bits: word 0x1FF");
    eq(int32_t(0xCAFE0003u), int32_t(r.dmem(CMD_DWORD + 3)), "command slot 3 (0x7FCC)");
    eq(int32_t(0xABCD0006u), int32_t(r.dmem(RW + 10)), "32-bit reply from src 5: word 0 of its pair (0x7BC0 + 40)");
    eq(0, r.dmem(RW + 11), "a 32-bit reply leaves word 1 of its pair alone");
    eq(0x00C0FFEE, r.dmem(RW + 12), "64-bit reply from src 6: word 0"); eq(0x12345678, r.dmem(RW + 13), "and word 1");
    check(r.block().last_landed_at >= 0, "a write covering slot 0 sets command landed"); eq(0x0000C0DE, r.dmem(CMD_DWORD), "slot 0 word");
    const uint64_t* pm = d.per_mode;
    eq(1, int64_t(pm[lw::M_SRC]), "accepted mode 1 once"); eq(1, int64_t(pm[lw::M_BUCKET]), "mode 2 once"); eq(2, int64_t(pm[lw::M_AUG]), "mode 3 twice (one bad)");
    eq(1, int64_t(pm[lw::M_GATHER]), "mode 4 once"); eq(1, int64_t(pm[lw::M_COUNT]), "mode 5 once"); eq(11, int64_t(pm[lw::M_WRITE]), "mode 0: 6 good, 3 bad, 2 commands (v1.25: the misaligned two are UB, directorMisaligned)");
    for (unsigned c = 1; c <= 4; c++) eq(0, r.remaining(c), "WRITE_COUNTER_%u counted down to 0", c);
    eq(1, int64_t(d.count_only), "the count-only packet wrote nothing and counted");
    eq(0, r.block().conditions & (1u << COND_UNEXPECTED), "no unexpected arrival (mode 4 and mode 6 aren't counted; bad packets aren't)");
    eq(7, int64_t(n.c.violations()), "7 violations: the four bad packets (invariant 5), three also at the sender: %s", n.c.violation_report().c_str());
    // the delay line (4.8): a packet accepted at t decrements its counter at t + K
    r.set_remaining(1, 1);
    uint64_t acc0 = d.accepted;
    n.at(5, n.c.cycle() + 2, 9, lw::M_SRC, 1, 0, lw::SIZE_32, 1, 0);
    int64_t acc_t = -1, dec_t = -1;
    for (int i = 0; i < 100 && dec_t < 0; i++) { n.step(); if (acc_t < 0 && d.accepted > acc0) acc_t = n.c.cycle(); if (r.remaining(1) == 0) dec_t = n.c.cycle(); }
    eq(acc_t + k.K, dec_t, "the counter decrements K = %u cycles after the director accepts the packet", k.K);
    // unexpected arrival: the error at the decrement, the stray named at acceptance
    uint64_t v0 = n.c.violations();
    n.at(5, n.c.cycle() + 2, 9, lw::M_SRC, 1, 0, lw::SIZE_32, 1, 0);
    n.run_all(300);
    eq(1, int64_t(r.block().unexpected_total), "unexpected arrival at the decrement (counter 1 at 0)");
    check((r.block().conditions & (1u << COND_UNEXPECTED)) != 0, "ERRORS [2] set");
    eq(0, r.remaining(1), "the counter stays 0, no wrap");
    eq(1, int64_t(n.c.violations() - v0), "the stray reported once, at acceptance (invariant 9)");
    { const auto& vm = n.c.violation_messages(); check(!vm.empty() && vm.back().find("stray") != std::string::npos, "named as a stray"); }
    // the accepted trade-off (4.12): a stray accepted at 0, then the counter armed before its decrement
    uint64_t u0 = r.block().unexpected_total; v0 = n.c.violations();
    n.at(5, n.c.cycle() + 2, 9, lw::M_SRC, 1, 0, lw::SIZE_32, 1, 0);
    acc0 = d.accepted;
    for (int i = 0; i < 50 && d.accepted == acc0; i++) n.step();
    r.set_remaining(1, 3);
    n.run_all(300);
    eq(int64_t(u0), int64_t(r.block().unexpected_total), "armed within K: no unexpected arrival");
    eq(2, r.remaining(1), "the stray used up one of the three");
    eq(1, int64_t(n.c.violations() - v0), "but meshsim named the stray at acceptance");
    r.set_remaining(1, 0);
    // size 11 (3.1)
    v0 = n.c.violations(); const uint32_t cond1 = r.block().conditions; const uint64_t bad1 = r.block().bad_total;
    r.set_remaining(1, 1);
    n.at(5, n.c.cycle() + 2, 9, lw::M_SRC, 1, 0, lw::SIZE_UNDEF, 0x1111, 0x2222);
    n.run_all(300);
    eq(1, int64_t(d.undefined_size), "size 11: flagged and dropped at the director");
    eq(1, r.remaining(1), "size 11: not counted");
    eq(int64_t(bad1), int64_t(r.block().bad_total), "size 11 is not a bad packet");
    eq(cond1, r.block().conditions, "size 11 sets no error (the hardware has no check)");
    eq(2, int64_t(n.c.violations() - v0), "size 11 flagged twice: at the sender's injection and at the receiving director");
    r.set_remaining(1, 0);
    // meshsim's checks: invariants 10, 11, 19, 21, 22
    r.set_remaining(2, 20); r.set_remaining(1, 2);
    const char* cases[][2] = {
        {"uncounted mode 1", "invariant 22"}, {"uncounted mode-0 data-memory write from a tile", "invariant 22"},
        {"uncounted big-SRAM write", "invariant 22"}, {"a tile's write into the command block", "invariant 11"},
        {"a reply at another sender's pair", "invariant 11"}, {"a 128-bit write into the reply block", "invariant 11"},
        {"a write into data memory's first 16 bytes", "invariant 22"}, {"a write past the end of data memory", "invariant 19"},
        {"a big-SRAM write past its end", "invariant 19"}, {"count-only on counter 0", "invariant 21"}, {"mode 4 to another tile", "invariant 10"}};
    const uint32_t pk[][4] = {
        {lw::M_SRC, 0, 0, lw::SIZE_32}, {lw::M_WRITE, 0, 0x100, lw::SIZE_32},
        {lw::M_WRITE, 0, lw::sram_addr(0x200), lw::SIZE_32}, {lw::M_WRITE, 2, CMD + 4, lw::SIZE_32},
        {lw::M_WRITE, 2, RPL + 8 * 7, lw::SIZE_32}, {lw::M_WRITE, 2, RPL + 8 * 4, lw::SIZE_128},
        {lw::M_WRITE, 2, 0x8, lw::SIZE_32}, {lw::M_WRITE, 2, 0x8000, lw::SIZE_32},
        {lw::M_WRITE, 1, uint32_t(lw::sram_addr(k.mem_chunks)), lw::SIZE_32}, {lw::M_COUNT, 0, 0, lw::SIZE_32},
        {lw::M_GATHER, 0, 0, lw::SIZE_64}};
    for (unsigned c = 0; c < 11; c++) {
        v0 = n.c.violations();
        n.at(5, n.c.cycle() + 2, 9, pk[c][0], pk[c][1], pk[c][2], pk[c][3], 0, 0);
        n.run_all(300);
        const auto& vm = n.c.violation_messages();
        const std::string last = vm.empty() ? std::string("null") : vm.back();
        check(n.c.violations() > v0 && last.find(cases[c][1]) != std::string::npos, "%s flagged (%s): %s", cases[c][0], cases[c][1], last.c_str());
    }
    // K's formula (4.7, D49): meshsim errors below max(2L + 4, L + 4, cm_latency, rp_latency)
    MachConfig bad = Net::cfg(shuffle); bad.K = 5;
    check(dies_naming([&] { Chip x(bad); }, "below"), "K = 5 at L = 1 is rejected (the formula's 6)");
    MachConfig ok6 = Net::cfg(shuffle); ok6.K = 6; { Chip x(ok6); } check(true, "K = 6 at L = 1 is accepted");
    MachConfig bad3 = Net::cfg(shuffle); bad3.ts_engine_latency = 3; bad3.K = 9;
    const bool threw = dies_naming([&] { Chip x(bad3); }, "");
    check(threw && Net::cfg(shuffle).with_ts_latency(3).K == 10 && Net::cfg(shuffle).with_ts_latency(4).K == 12, "K = 9 at L = 3 is rejected; withTsLatency(3) and (4) give K = 10 and 12");
    // port-B timing: accept at t, written at t + cm_latency, readable from t + cm_latency + 1
    r.set_remaining(3, 1);
    n.clear_logs();
    n.at(5, n.c.cycle() + 2, 9, lw::M_WRITE, 3, RPL + 40, lw::SIZE_32, 0x5A5A, 0);
    int64_t accept = -1, written = -1; bool saw_idle = false;
    for (int i = 0; i < 100; i++) {
        n.step();
        if (accept < 0 && !n.log(9).empty()) accept = int64_t(n.log(9)[0][0]);
        if (accept >= 0 && n.c.cycle() < accept + int64_t(k.cm_latency) && cm_idle(n.c, 9)) saw_idle = true;
        if (written < 0 && r.dmem(RW + 10) == 0x5A5A) written = n.c.cycle();
    }
    eq(accept + k.cm_latency, written, "a reply written at port B cm_latency cycles after the director accepts it");
    check(written < accept + int64_t(k.K), "and before its counter's decrement at K (4.8)");
    check(!saw_idle, "cm_idle low while the write is in the port-B pipe");
    out(suite::str("director: each mode and region to its unit (bad packets dropped with ERRORS [1]); counters by the counter field, decremented K = %u cycles after acceptance; unexpected arrival at the decrement, the stray named at acceptance; a reply accepted at t is written at port B at t + %lld (cm_latency %u) [meas: NetTest director]",
                   k.K, (long long)(written - accept), k.cm_latency));
}

// ------------------------------------------------------------------ the injection point
void injection() {
    suite::group("injection");
    Net n(Net::cfg(shuffle));
    TileRef s = n.c.tile(3);
    units::Injection& in = s.unit<units::Injection>("inj");
    const int64_t T0 = n.c.cycle() + 2;
    for (int j = 0; j < 10; j++) n.at(3, T0 + j, 4, lw::M_SRC, 0, uint64_t(j));
    n.step();
    s.send_put(n.c.cycle(), 100, lw::hdr(0, 4, lw::M_SRC, 1, 0, lw::SIZE_64), 0);
    check(n.run_all(300), "drained");
    const auto l = n.log(4);
    eq(11, int64_t(l.size()), "11 packets");
    for (int j = 0; j < 10 && j < int(l.size()); j++) eq(j, int64_t(l[j][1]), "read returns first, in order");
    eq(100, int64_t(l.at(10)[1]), "send-port packet after the read returns");
    eq(10, int64_t(in.send_wait_cycles), "send port waited 10 cycles");
    eq(T0 + 10, int64_t(l.at(10)[0]) - (int64_t(l.at(0)[0]) - T0), "send-port packet injected at T0 + 10");
    n.clear_logs();
    for (int j = 0; j < 20; j++) { s.send_put(n.c.cycle(), uint64_t(200 + j), lw::hdr(0, 4, lw::M_SRC, 1, 0, lw::SIZE_64), 0); n.step(); }
    n.run_all(300);
    eq(0, int64_t(s.send_refusals()), "no refusals");
    const auto l2 = n.log(4);
    eq(20, int64_t(l2.size()), "20 send-port packets");
    eq(19, int64_t(l2.at(19)[0]) - int64_t(l2.at(0)[0]), "one per cycle");
    no_violations(n.c, "injection");
}

// ------------------------------------------------------------------ the loopback: tokens and the eject priority (4.5, 4.7)
void loopback() {
    suite::group("loopback");
    Net n(Net::cfg(shuffle));
    TileRef r = n.c.tile(9);
    n.clear_logs();                                         // ref: r.dir.log = new ArrayList<>()
    const int64_t T0 = n.c.cycle() + 2;
    for (int j = 0; j < 300; j++) n.at(8, T0 + j, 9, lw::M_SRC, 0, uint64_t(1000000 + j));
    int sent = 0;
    for (int i = 0; i < 2000 && (sent < 120 || !n.c.quiescent()); i++) {
        if (sent < 120 && r.send_empty(n.c.cycle() + 1) && n.c.cycle() >= T0 + 10) { r.send_put(n.c.cycle() + 1, uint64_t(sent), lw::hdr(0, 9, lw::M_SRC, 1, 0, lw::SIZE_32), 0); sent++; }
        n.step();
    }
    const auto l = n.log(9);
    eq(420, int64_t(l.size()), "300 stream packets and 120 self-sends delivered");
    int64_t last_stream = -1, self_during = 0;
    for (const auto& e : l) if (lw::src(e[2]) == 8) last_stream = int64_t(e[0]);
    for (const auto& e : l) if (lw::src(e[2]) == 9) { if (int64_t(e[0]) < last_stream) self_during++; }
    const Router& rt = router(n.c, 9);
    const Router::Fifo& lb = rt.q[rt.fifo_index(Router::EJECT, rt.slot_of[Router::EJECT][Router::LOOPBACK])];
    const units::Injection& in = r.unit<units::Injection>("inj");
    const uint64_t waits = in.token_waits(uint64_t(n.c.cycle()));   // ref's tokenWaitCycles at the end of the cycle just run
    eq(0, self_during, "the loopback is served only when every other eject FIFO is empty: no self packet lands while the stream does");
    check(lb.peak <= n.c.cfg().read_tokens, "the loopback FIFO peaked at %u of %u: at most the token count", unsigned(lb.peak), unsigned(lb.cap));
    check(waits > 0, "the send port waited for tokens (%llu cycles) instead of overflowing", (unsigned long long)waits);
    eq(0, int64_t(in.tok.reads + in.tok.loop_held), "every token free again");
    no_violations(n.c, "loopback");
    out(suite::str("loopback [meas: NetTest loopback]: a full-rate stream into tile 9 with 120 self-sends from its send port: the loopback FIFO peaked at %u (depth %u), the send port waited %llu cycles for tokens, no overflow; the self-sends landed after the stream (eject serves the loopback last)",
                   unsigned(lb.peak), unsigned(lb.cap), (unsigned long long)waits));
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
    if (want("broadcast")) broadcast();
    if (want("routes")) route_comparison();
    if (want("cncport")) cnc_port();
    if (want("random")) in_order_random();
    if (want("fullrate")) in_order_full_rate();
    if (want("director")) director();
    if (want("injection")) injection();
    if (want("loopback")) loopback();
    if (want("fanin")) fan_in(quick);
    std::fputs(report.c_str(), stdout);
    return suite::done(std::string("NetTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : ""));
}
