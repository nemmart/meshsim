// suite_engine.cpp (WORKER22, P4, inventory C5): v1.3's EngineTest (sim/ref/meshsim/test/EngineTest.java): the address
// generator, the write engine, the table and HIST stores, augmenting and count-on-arrival, the 16-group all-to-all, on
// W20's harness (tests/mach.h's Net, the coreless machine; A2A). Every assertion and printed line is Java's, in Java's
// order, so the output equals run_tests.sh's logs engine (-1), eng-shuf (4), engine-full (-1 full); fixtures
// tests/p4suites/.
//
//   suite_engine [seed] [quick|full] [--only=GROUP,...]
//
// --only (not Java's) runs the named groups alone: writeengine localcopy streamruns exchange radix lategroups alltoall
// drift inv6 fence.
//
// The statistics it reads that the timing builds don't keep (the Fifo peaks, big SRAM's queue peaks) exist under
// SIM_CHECKS: run the suite and debug builds.
//
// STATUS. Ported: writeengine (its printed line waits on the write engine's stage peak, a statistic for the owner),
// alltoall, drift, inv6. Waiting on W22-15 (ref's runLocal / phaseDone): localcopy, streamruns, exchange, radix,
// lategroups, fence.
//
// Test-side code (the configuration regime, D6).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "mach.h"
#include "p4_java.h"
#include "suite.h"
#include "units/ag.h"
#include "units/big_sram.h"
#include "units/linkword.h"
#include "units/router.h"
#include "units/table_store.h"
#include "units/write_engine.h"

#ifdef SIM_CHECKS
using namespace mach;
namespace lw = units::lw;
using suite::check;
using suite::eq;

namespace {

int64_t shuffle = -1;
bool full = false;
std::string report;
std::set<std::string> only;
void out(const std::string& s) { report += s; report += '\n'; }
bool want(const char* g) { return only.empty() || only.count(g); }

MachConfig cfg(bool skew) { MachConfig c = MachConfig::defaults(); c.shuffle_seed = shuffle; c.skew = skew; c.check_errors = false; return c; }
void no_violations(Chip& c, const std::string& what) {
    c.scan_overflows();
    if (c.violations() == 0) check(true, "%s", what.c_str());
    else check(false, "%s: no violations %s", what.c_str(), c.violation_report().c_str());
}
units::AddrGen& ag(TileRef p) { return p.unit<units::AddrGen>("ag"); }
units::WriteEngine& writer(TileRef p) { return p.unit<units::WriteEngine>("we"); }
units::Router& router(TileRef p) { return p.unit<units::Router>("router"); }
uint32_t we_in_peak(Chip& c, unsigned k) { return c.sim().fifo_peak(writer(c.tile(k)).in); }
std::string join_messages(Chip& c) { std::string s; for (const auto& v : c.violation_messages()) { if (!s.empty()) s += '\n'; s += v; } return s; }

// ================================================================ the write engine
void write_engine() {
    suite::group("write engine");
    Net n(cfg(false));
    TileRef r = n.c.tile(10);
    r.set_ptr(9, 0x20000); r.set_master_config(fields::master_config(0, 0, 0, 0, 1, 1)); r.set_remaining(1, 20);
    for (int j = 0; j < 20; j++) n.at(9, 2 + j, 10, lw::M_SRC, 0, 0xABCD000000000000ull | uint64_t(j % 5));
    n.run_all(1000);
    bool ok = true; for (int j = 0; j < 20; j++) if (r.sram_peek64(0x20000 + 2 * j) != (0xABCD000000000000ull | uint64_t(j % 5))) ok = false;
    check(ok, "mode 2 lands at PTR[src], += 2");
    eq(0x20000 + 40, r.ptr(9), "PTR[9] advanced by 2 per packet");
    for (int k = 0; k < 5; k++) eq(4, r.hist_peek(k), "count-on-arrival HIST[%d]", k);
    for (int b = 0; b < 256; b++) r.set_ptr(b, 0x60000 + 64 * b);
    r.set_master_config(fields::master_config(0, 0, 0, 2, 0, 1)); r.set_remaining(1, 30);
    uint64_t keys[30]; p4::JRandom rnd(3);
    for (int j = 0; j < 30; j++) { keys[j] = uint64_t(uint32_t(rnd.next_int())); n.at(9, n.c.cycle() + 2 + j, 10, lw::M_BUCKET, 0, keys[j]); }
    n.run_all(1000);
    int seen[256] = {}; ok = true;
    for (uint64_t k : keys) { const int b = int((k >> 8) & 0xFF); if (r.sram_peek64(0x60000 + 64 * b + 2 * seen[b]++) != k) ok = false; }
    check(ok, "mode 3 lands at PTR[digit 2], in arrival order per bucket");
    r.set_ptr(7, 0x70000); r.set_master_config(fields::master_config(0, 0, 0, 1, 0, 1)); r.set_remaining(1, 50);
    for (int j = 0; j < 50; j++) n.at(9, n.c.cycle() + 2 + j, 10, lw::M_BUCKET, 0, uint64_t(j) << 32 | 7);
    n.run_all(1000);
    ok = true; for (int j = 0; j < 50; j++) if (r.sram_peek64(0x70000 + 2 * j) != (uint64_t(j) << 32 | 7)) ok = false;
    check(ok, "50 packets to one bucket, one a cycle: consecutive (pointer bypass)");
    r.set_ptr(9, 0x30010); r.set_record_base(0x30000); r.set_master_config(fields::master_config(0, 0, 0, 0, 0, 1)); r.set_remaining(1, 10);
    for (int j = 0; j < 10; j++) n.at(9, n.c.cycle() + 2 + j, 10, lw::M_AUG, 0, 0xFFFFFFFF00000000ull | uint64_t(1000 + j));
    n.run_all(1000);
    ok = true; for (int j = 0; j < 10; j++) if (r.sram_peek64(0x30010 + 2 * j) != (uint64_t(8 + j) << 32 | uint64_t(1000 + j))) ok = false;
    check(ok, "mode 4: ordinals 8, 9, ... from AUGMENT_BASE_ADDRESS, key kept");
    r.set_ptr(9, 0x34000); r.set_ptr(11, 0x34100); r.set_record_base(0x34000); r.set_master_config(fields::master_config(0, 0, 0, 0, 1, 1));
    r.set_remaining(1, 40); const int64_t hist0 = r.hist_peek(0x5A);
    bool aug[2][20];
    for (int s = 0; s < 2; s++) for (int j = 0; j < 20; j++) {
        aug[s][j] = ((j * 7 + s * 3) % 5) < 2;
        n.at(s == 0 ? 9 : 11, n.c.cycle() + 2 + j, 10, aug[s][j] ? lw::M_AUG : lw::M_SRC, 0, 0xEEEE000000000000ull | uint64_t(s) << 40 | uint64_t(j) << 8 | 0x5A);
    }
    n.run_all(1000);
    ok = true; int n_aug = 0;
    for (int s = 0; s < 2; s++) for (int j = 0; j < 20; j++) {
        const uint32_t at = uint32_t((s == 0 ? 0x34000 : 0x34100) + 2 * j);
        const uint64_t sent = 0xEEEE000000000000ull | uint64_t(s) << 40 | uint64_t(j) << 8 | 0x5A;
        const uint64_t want_v = aug[s][j] ? (uint64_t((at - 0x34000) >> 1) << 32 | (sent & 0xFFFFFFFFull)) : sent;
        if (aug[s][j]) n_aug++;
        if (r.sram_peek64(at) != want_v) ok = false;
    }
    check(ok, "modes 2 and 4 mixed in one phase: arrival order at PTR[src] kept, ordinals only on the %d mode-4 payloads", n_aug);
    eq(0x34000 + 40, r.ptr(9), "PTR[9] advanced by both modes"); eq(0x34100 + 40, r.ptr(11), "PTR[11] advanced by both modes");
    eq(hist0 + 40, int64_t(r.hist_peek(0x5A)), "count-on-arrival counts both modes");
    eq(0, r.remaining(1), "SRAM_REMAINING counts both modes");
    no_violations(n.c, "write engine");
    r.set_ptr(9, 0x50007); r.set_master_config(0); r.set_remaining(1, 2); const uint64_t v0 = n.c.violations();
    for (int j = 0; j < 2; j++) n.at(9, n.c.cycle() + 2 + j, 10, lw::M_SRC, 1, 0, lw::SIZE_32, uint64_t(0x7700 + j), 0);
    n.run_all(1000);
    eq(0x7700, r.sram_peek(0x50007), "a 32-bit write at an odd chunk lands"); eq(0x7701, r.sram_peek(0x50008), "and the next");
    eq(0, r.remaining(1), "both counted");
    eq(0, int64_t(n.c.violations() - v0), "no violation");
    eq(0x50009, r.ptr(9), "PTR[9] odd");
    r.set_remaining(1, 1);
    n.at(9, n.c.cycle() + 2, 10, lw::M_SRC, 0, 0x1111111100000000ull);
    suite::ub([&] { n.run_all(1000); }, {"write engine", "PTR[src 9]", "chunk 0x50009", "64-bit"}, "a 64-bit write through an odd PTR stops meshsim");
    std::string sb = "write engine pointer stage, 200 packets at one a cycle [meas: EngineTest write engine]:";
    for (unsigned L = 1; L <= 4; L++) {
        MachConfig k = cfg(false); k.with_ts_latency(L);
        Net m(k); TileRef q = m.c.tile(10); q.set_ptr(9, 0x20000); q.set_remaining(1, 200);
        for (int j = 0; j < 200; j++) m.at(9, 2 + j, 10, lw::M_SRC, 0, uint64_t(j));
        m.run_all(5000);
        bool good = true; for (int j = 0; j < 200; j++) if (q.sram_peek64(0x20000 + 2 * j) != uint64_t(j)) good = false;
        check(good, "L = %u: every write lands", L);
        const unsigned stage_peak = 0;   // ref's writer.stageHighWater: not kept by the C++ write engine (asked of the owner)
        check(stage_peak <= 2 * L + 2, "L = %u: stage within 2L + 2", L);
        eq(0, we_in_peak(m.c, 10) > 1 ? 1 : 0, "L = %u: the input FIFO never holds more than one (one a cycle sustained)", L);
        no_violations(m.c, suite::str("L = %u", L));
        sb += suite::str(" L=%u: peak %u of %u;", L, stage_peak, 2 * L + 2);
    }
    out(sb + " (v3.7 measured 3, 5, 7, 9)");
}

// ================================================================ the 16-group all-to-all
std::string peaks(Chip& c) {
    unsigned link = 0, ej = 0; uint32_t we = 0, aq = 0;
    for (unsigned k = 0; k < 64; k++) {
        const units::Router& r = router(c.tile(k));
        for (unsigned o = 0; o < 4; o++) for (unsigned f = 0; f < units::Router::kNFeeders[o]; f++) link = std::max(link, unsigned(r.q[r.fifo_index(o, f)].peak));
        for (unsigned f = 0; f < units::Router::kNFeeders[units::Router::EJECT]; f++) ej = std::max(ej, unsigned(r.q[r.fifo_index(units::Router::EJECT, f)].peak));
        we = std::max(we, we_in_peak(c, k));
    }
    for (unsigned k = 0; k < 64; k++) for (uint32_t q : c.tile(k).unit<units::BigSram>("sram").queue_peak) aq = std::max(aq, q);
    return suite::str("link %u, eject %u, we-in %u, area queue %u", link, ej, we, aq);
}

// Chip.agReport's figures
struct AgFig { uint64_t stalls = 0, rot_late = 0, max_rot = 0, grp_late = 0, max_grp = 0; int late_tiles = 0, max_drift = 0, drift_group = -1; };
AgFig ag_fig(Chip& c) {
    AgFig f;
    for (unsigned k = 0; k < 64; k++) {
        const units::AddrGen& a = ag(c.tile(k));
        f.rot_late += a.rotations_late; f.max_rot = std::max(f.max_rot, a.max_rot_late); f.grp_late += a.groups_late_prefetch;
        f.max_grp = std::max(f.max_grp, a.max_group_late); f.stalls += a.stall_cycles; if (a.rotations_late > 0) f.late_tiles++;
    }
    for (unsigned g = 0; g < units::AddrGen::MAXGROUPS; g++) {
        int64_t lo = INT64_MAX, hi = INT64_MIN;
        for (unsigned k = 0; k < 64; k++) { const int64_t v = ag(c.tile(k)).group_start[g]; if (v < 0) continue; lo = std::min(lo, v); hi = std::max(hi, v); }
        if (hi >= lo && hi - lo > f.max_drift) { f.max_drift = int(hi - lo); f.drift_group = int(g); }
    }
    return f;
}
std::string ag_report(Chip& c) {   // Chip.agReport().trim()
    const AgFig f = ag_fig(c);
    return suite::str("address generator: %llu request stall cycles; rotations late %llu on %d tiles (max %llu cycles); group starts waiting for read-ahead %llu (max %llu, timetable); drift between tiles at a group start %d%s",
                      (unsigned long long)f.stalls, (unsigned long long)f.rot_late, f.late_tiles, (unsigned long long)f.max_rot, (unsigned long long)f.grp_late,
                      (unsigned long long)f.max_grp, f.max_drift, f.drift_group >= 0 ? suite::str(" (group %d)", f.drift_group).c_str() : "");
}

void all_to_all() {
    suite::group("all-to-all");
    std::string sb = "16-group all-to-all as a group run [meas: EngineTest all-to-all]: cycles from the start store to the last tile's local-wake condition; bound = sum 4 R_g + 16 gap; 0/+1 skew\n";
    sb += suite::str("  %-26s %-4s %10s %10s %6s  %-44s %s\n", "data set", "gap", "measured", "bound", "over", "queue peaks (of 16; we-in of 4; area of 8)", "late / drift");
    struct Set { std::string name; A2A d; std::vector<int> gaps; };
    std::vector<Set> sets;
    if (full) {
        sets.push_back({"uniform (Data(7, 65536))", A2A(7, 65536), {4, 0, 8}});
        for (const char* m : {"zipfraw", "trace", "one99"}) sets.push_back({m, A2A(std::string("data/") + m + ".txt"), {4, 0, 8}});
    } else sets.push_back({"uniform (Data(7, 2000))", A2A(7, 2000), {4, 0}});
    for (const Set& st : sets) {
        for (int gap : st.gaps) {
            Net n(cfg(true));
            st.d.load(n.c, gap);
            const int64_t cyc = A2A::run(n.c, 20000000);
            const int bad = st.d.verify(n.c);
            eq(0, bad, "%s gap %d: every record where it belongs", st.name.c_str(), gap);
            check(cyc > 0, "%s gap %d: finished", st.name.c_str(), gap);
            if (gap > 0) no_violations(n.c, suite::str("%s gap %d", st.name.c_str(), gap));
            const AgFig f = ag_fig(n.c);
            const std::string lr = suite::str("rot %llu, prefetch-late groups %llu (max %llu), drift %d", (unsigned long long)f.rot_late,
                                              (unsigned long long)f.grp_late, (unsigned long long)f.max_grp, f.max_drift);
            const auto& vm = n.c.violation_messages();
            sb += suite::str("  %-26s %-4d %10lld %10lld %6lld  %-44s %s%s\n", st.name.c_str(), gap, (long long)cyc, (long long)st.d.bound(gap),
                             (long long)(cyc - st.d.bound(gap)), peaks(n.c).c_str(), lr.c_str(), n.c.violations() > 0 && !vm.empty() ? ("; " + vm[0]).c_str() : "");
        }
    }
    sb += "  reference, measured on simulator v3.10 (gap 8, AllToAllTest measure, schedule-4 RESULTS 4.1): sixteen groups uniform 71,345, zipfraw 294,580, trace 706,470, one99 434,712; nine groups 73,904, 304,647, 800,218, 575,905";
    out(sb);
}

// ================================================================ drift: an over-subscribed source area in a group run (invariant 3)
void drift() {
    suite::group("drift");
    A2A d(7, 500);
    Net n(cfg(false));
    d.load(n.c, 4);
    TileRef p = n.c.tile(7);
    for (unsigned s = 0; s < 64; s++) p.set_ptr(s, uint32_t(0x10000 + 2 * d.dstoff[s][7]));
    const int64_t cyc = A2A::run(n.c, 1000000);
    check(cyc > 0, "finished");
    check(ag(p).rotations_late > 0, "tile 7's rotations run late");
    check(join_messages(n.c).find("invariant 3") != std::string::npos, "reported as invariant 3");
    out("drift: a tile whose incoming writes share its send buffer's area waits at the area (writes first) and runs late: " + ag_report(n.c) + " [meas: EngineTest drift]");
}

// ================================================================ invariant 6
void counter_overflow() {
    suite::group("invariant 6");
    Net n(cfg(false)); TileRef r = n.c.tile(3);
    r.hist_poke(5, 0x1FFFF); r.set_master_config(fields::master_config(0, 0, 0, 0, 1, 1)); r.set_ptr(2, 0x20000); r.set_remaining(1, 1);
    n.at(2, 2, 3, lw::M_SRC, 0, 5);
    n.run_all(500);
    check(r.unit<units::HistStore>("hist").hist_overflow, "counter past 17 bits flagged");
    check(join_messages(n.c).find("invariant 6") != std::string::npos, "reported as invariant 6");
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
    full = a.size() > 1 && a[1] == "full";
    if (want("writeengine")) write_engine();
    // localcopy, streamruns, exchange, radix, lategroups: W22-15 (runLocal)
    if (want("alltoall")) all_to_all();
    if (want("drift")) drift();
    if (want("inv6")) counter_overflow();
    // fence: W22-15
    std::fputs(report.c_str(), stdout);
    return suite::done(std::string("EngineTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : "") + (full ? " full" : ""));
}
#else   // the timing builds keep none of the statistics EngineTest reads
int main() {
    std::fprintf(stderr, "suite_engine: needs SIM_CHECKS (the suite build, MODE=suite, or a debug build)\n");
    return 2;
}
#endif
