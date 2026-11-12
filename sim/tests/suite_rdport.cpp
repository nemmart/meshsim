// suite_rdport.cpp (WORKER22, P4, inventory C7): v1.3's ReadPortGatherTest (sim/ref/meshsim/test/
// ReadPortGatherTest.java): the controller read port (4.3) and the GATHER engine (4.4), on W20's harness: the coreless
// machine with the real GATHER engine, and fw/tests/rdport.c and gather.c booted on the machine as specified. Every
// assertion and printed line is Java's (this suite prints as it goes, as Java's out()), so the output equals
// run_tests.sh's logs rdport (-1), rdport-shuf (4), rdport-full (-1 full); fixtures tests/p4suites/.
//
//   suite_rdport [seed] [full] [--only=GROUP,...]
//     groups: fromc traffic tolerance overlap disjoint reserved gather counting gatherc
//
// STATUS. Ported: fromc, reserved, gather (but its 128-bit packet count: the write engine's writtenBySize, a statistic
// for the owner), counting, gatherc. Waiting: traffic, tolerance, overlap (the read port's host queue from the harness,
// ref's readPort.hostQueue / idle / allAccepted: W22-18), disjoint (W22-14: Chip::new_phase, big SRAM's disjoint facts).
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
#include "units/director.h"
#include "units/gather.h"
#include "units/inject.h"
#include "units/linkword.h"
#include "units/router.h"
#include "units/write_engine.h"

#ifdef SIM_CHECKS
using namespace mach;
namespace lw = units::lw;
using suite::check;
using suite::eq;

namespace {

int64_t shuffle = -1;
bool full = false;
std::set<std::string> only;
bool want(const char* g) { return only.empty() || only.count(g); }
void out(const std::string& s) { std::fputs((s + "\n").c_str(), stdout); std::fflush(stdout); }   // Java's out(): printed at once
std::string strip_trailing(std::string s) { while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back(); return s; }

MachConfig cfg() { MachConfig c = MachConfig::defaults(); c.real_responder = true; c.shuffle_seed = shuffle; return c; }
void no_violations(Chip& c, const std::string& what) {
    if (c.violations() == 0) check(true, "%s: no invariant violations", what.c_str());
    else check(false, "%s: no invariant violations\n%s", what.c_str(), c.violation_report().c_str());
}
units::AddrGen& ag(TileRef p) { return p.unit<units::AddrGen>("ag"); }
int64_t run_all(Chip& c, int64_t max) {                 // ReadPortGatherTest.runAll
    const int64_t t0 = c.cycle(); bool st[64] = {};
    const std::vector<int> sk = c.start_skew();
    for (int64_t i = 0; i < max; i++) {
        for (unsigned k = 0; k < 64; k++) if (!st[k] && c.cycle() >= t0 + sk[k]) { c.tile(k).pulse(fields::PULSE_START); st[k] = true; }
        c.step();
        bool all = true; for (unsigned k = 0; k < 64; k++) if (!st[k] || !c.tile(k).phase_done()) { all = false; break; }
        if (all) { c.scan_overflows(); return c.cycle() - t0; }
    }
    c.scan_overflows();
    return -1;
}
void stream_run(TileRef p, uint32_t start, uint32_t count) { p.set_master_config(fields::master_config(fields::RUN_STREAMS, start, count, 0, 0, 1)); }

// ================================================================ the read port from C (4.3)
void read_port_from_c() {
    suite::group("read port from C");
    MachConfig k = Mach::cfg(shuffle); k.check_errors = false; k.max_violation_messages = 100;
    Mach m(k);
    for (unsigned p = 0; p < 64; p++) for (uint32_t x = 0; x < 0x1100; x++) m.c.tile(p).sram_poke(0x1000 + x, p << 20 | x);
    const Mach::Image im = m.boot("rdport");
    const bool ok = m.run([&] { return m.all_stopped(); }, 20000);
    check(ok && m.all_exited(), "every tile ran rdport: %s", m.halt_summary().c_str());
    for (unsigned p = 0; p < 64; p++) {
        const std::string tl = suite::str("tile %u: ", p);
        int bad = 0;
        for (unsigned i = 0; i < 40; i++) if (m.word(p, im, "res", i) != (p << 20 | (i * 37 & 0xFFF))) bad++;
        if (p != 7) eq(0, bad, "%s40 results at the destination + 4 i bytes", tl.c_str());
        eq(64, m.word(p, im, "r", 0), "%s64 free entries at the start", tl.c_str());
        check(m.word(p, im, "r", 1) < 64, "%sfewer free while reads are outstanding (%u)", tl.c_str(), m.word(p, im, "r", 1));
        eq(64, m.word(p, im, "r", 2), "%s64 free after the local wake: every entry freed as its return was injected", tl.c_str());
        eq(0, m.word(p, im, "r", 3), "%sWRITE_COUNTER_2 counted down to 0", tl.c_str());
        eq(int32_t((im.sym("wide") + 128) | lw::SIZE_128 << 17 | 2u << 19), int32_t(m.word(p, im, "r", 6)),
           "%sSRAM_RD_DEST_ADDR reads the next destination, advanced by the size in bytes, with the size and counter", tl.c_str());
        bool w64 = true, w128 = true;
        for (unsigned i = 0; i < 16; i++) if (m.word(p, im, "wide", i) != (p << 20 | (0x100 + i))) w64 = false;
        for (unsigned i = 0; i < 16; i++) if (m.word(p, im, "wide", 16 + i) != (p << 20 | (0x200 + i))) w128 = false;
        check(w64, "%s8 64-bit reads: two words each, at the destination + 8 i bytes", tl.c_str());
        check(w128, "%s4 128-bit reads: four words each, at the destination + 16 i bytes", tl.c_str());
    }
    eq(1 << 7, m.word(7, im, "r", 4), "tile 7: the 65th store into a full queue set the read-port-error condition, and only it");
    eq(1, int64_t(m.c.tile(7).block().rp_errors), "tile 7: one read dropped");
    {
        const auto& vm = m.c.violation_messages();
        bool found = false; for (const auto& v : vm) if (v.find("tile 7") != std::string::npos && v.find("invariant 8") != std::string::npos) found = true;
        check(vm.size() == 1 && found, "the overflow (invariant 8), and nothing else, reported: %s", m.c.violation_report().c_str());
    }
    int bad = 0; for (unsigned i = 0; i < 64; i++) if (m.word(7, im, "res", i) != (7u << 20 | (0x1000 + i))) bad++;
    eq(0, bad, "tile 7: the 64 queued reads all landed");
    out("read port from C [meas: ReadPortGatherTest read port from C]: 40 32-bit, 8 64-bit and 4 128-bit reads land at the setup register's destination + the size in bytes per read, counting on their counter; the free count reads 64, then less, then 64 after counters done; a 65th store into a full queue is dropped with the read-port error");
}

// ================================================================ reserved streams
void reserved_streams() {
    suite::group("reserved streams");
    Chip c(cfg());
    TileRef p = c.tile(3), q4 = c.tile(4);
    p.set_stream(0, fields::stream_src(0x1000), 0, fields::stream_count(1, fields::SIZE_UNDEF, 1, lw::M_SRC, 1));
    p.set_stream(1, fields::stream_src(0x1000), 0, fields::stream_count(1, fields::SIZE_128, 1, lw::M_AUG, 1));
    p.set_stream(2, fields::stream_src(0x1000), 0x0200, fields::stream_count(1, fields::SIZE_64, 2, lw::M_WRITE, 1));
    stream_run(p, 0, 3);
    q4.set_remaining(1, 1); q4.set_remaining(2, 1); q4.set_ptr(3, 0x40000);
    p.pulse(fields::PULSE_START);
    c.run(100);
    eq(1, int64_t(ag(p).size_violations), "a stream with read size 11 (undefined): reported");
    eq(1, int64_t(ag(p).aug128_violations), "a mode-3 stream at read size 128: reported (4.2)");
    eq(1, int64_t(q4.unit<units::Director>("dir").bad_packets), "the 128-bit mode-3 packet is dropped at its director (a bad packet)");
    eq(1, int64_t(q4.unit<units::Director>("dir").undefined_size), "the size-11 packet: flagged and dropped at the director (meshsim's choice)");
    check((q4.block().conditions & (1u << 1)) != 0, "and the bad packet sets ERRORS [1]");
    eq(0, q4.remaining(2), "the good mode-0 packet landed and counted");
    const uint32_t es[3][3] = {{6, 1, fields::RUN_STREAMS}, {7, 0, fields::RUN_STREAMS}, {7, 0, fields::RUN_GROUPS}};
    for (const auto& e : es) {
        Chip cr(cfg()); TileRef q = cr.tile(3);
        q.set_stream(0, fields::stream_src(0x1000), 0, fields::stream_count(e[1], fields::SIZE_64, 1, e[0], 1));
        if (e[2] == fields::RUN_GROUPS) { q.set_group(0, fields::group_lo(2), fields::group_hi(0, 1, 0)); q.set_master_config(fields::master_config(fields::RUN_GROUPS, 0, 1, 0, 0, 1)); }
        else stream_run(q, 0, 1);
        q.pulse(fields::PULSE_START);
        const std::string rm = suite::str("reserved mode %u", e[0]);
        suite::ub([&] { cr.run(100); }, {"tile 3", "address generator", rm.c_str()}, "a stream in mode %u at count %u%s", e[0], e[1], e[2] == fields::RUN_GROUPS ? " in a group run" : "");
    }
}

// ================================================================ the GATHER engine
int ppv(int k) { return (k & 1) == 1 ? k : k / 2; }    // value packets per gathered value (v1.25 s8c)

void gather_run(int k, int N, std::string& sb) {
    Chip c(cfg());
    const uint32_t IDX = 0x60000, VAL = 0x20000, OUT = 0xA0000;
    std::vector<std::vector<int>> perm(64);
    for (unsigned p = 0; p < 64; p++) {
        TileRef t = c.tile(p);
        p4::JRandom r(100 + int64_t(p));
        std::vector<int> pm(static_cast<size_t>(N)); for (int i = 0; i < N; i++) pm[size_t(i)] = i;
        for (int i = N - 1; i > 0; i--) { const int j = r.next_int(i + 1); std::swap(pm[size_t(i)], pm[size_t(j)]); }
        perm[p] = pm;
        for (int i = 0; i < N; i++) t.sram_poke64(IDX + 2 * uint32_t(i), uint64_t(uint32_t(pm[size_t(i)])) << 32 | 0xAB000000ull | uint32_t(i));
        for (int v = 0; v < N; v++) for (int i = 0; i < k; i++) t.sram_poke64(VAL + 2 * uint32_t(k * v + i), uint64_t(p) << 48 | uint64_t(v) << 8 | uint32_t(i));
        t.set_stream(195, fields::stream_src(IDX), 0, fields::stream_count(uint32_t(N), fields::SIZE_64, 1, lw::M_GATHER, 0));
        t.set_ptr(p, OUT);
        t.set_record_base(VAL);                              // ref's gather.base (DMA_RECORD_BASE_ADDRESS)
        t.set_remaining(1, uint32_t(N * ppv(k)));
        t.set_master_config(fields::master_config(fields::RUN_STREAMS, 195, 1, 0, 0, uint32_t(k)));
    }
    const int64_t cyc = run_all(c, 10000000);
    int64_t bad = 0;
    for (unsigned p = 0; p < 64; p++) for (int i = 0; i < N; i++) for (int e = 0; e < k; e++)
        if (c.tile(p).sram_peek64(OUT + 2 * uint32_t(k * i + e)) != (uint64_t(p) << 48 | uint64_t(perm[p][size_t(i)]) << 8 | uint32_t(e))) bad++;
    check(cyc > 0 && bad == 0, "k = %d, N = %d: every value landed in sorted order (%lld wrong)", k, N, (long long)bad);
    no_violations(c, suite::str("gather k = %d", k));
    uint32_t gq = 0; uint64_t ag_stall = 0; uint32_t area = 0;
    for (unsigned p = 0; p < 64; p++) {
        gq = std::max(gq, c.sim().fifo_peak(c.tile(p).unit<units::Gather>("gather").in));
        ag_stall = std::max(ag_stall, ag(c.tile(p)).stall_cycles);
        for (uint32_t a : c.tile(p).unit<units::BigSram>("sram").queue_peak) area = std::max(area, a);
    }
    sb += suite::str("  k = %d  N = %6d  %9lld cycles  %s per value (packets per value + 1 = %d)  GATHER queue peak %2u of 16  address generator stalled %llu cycles, area queue peak %u%s\n",
                     k, N, (long long)cyc, p4::jfixed(double(cyc) / N, 3, 6).c_str(), ppv(k) + 1, gq, (unsigned long long)ag_stall, area, gq >= 16 ? "  OVERFLOW" : "");
    int64_t reads = 0, packets128 = 0;
    for (unsigned p = 0; p < 64; p++) { reads += int64_t(c.tile(p).unit<units::Gather>("gather").reads); packets128 += -1; }   // ref's writer.writtenBySize[2]: not kept by the C++ write engine (asked of the owner)
    eq(int64_t(64) * N * ppv(k), reads, "k = %d: %d read(s) and packet(s) per value", k, ppv(k));
    eq((k & 1) == 0 ? int64_t(64) * N * ppv(k) : 0, packets128, "k = %d: %s value packets", k, (k & 1) == 0 ? "128-bit" : "64-bit");
}

void gather_counting_and_alignment() {
    suite::group("gather: counting and alignment (v1.25)");
    const uint32_t IDX = 0x60000, VAL = 0x20000, OUT = 0xA0000; const int N = 40;
    for (int k : {1, 2, 3, 4, 8}) for (bool old : {false, true}) {
        if (old && (k & 1) == 1) continue;
        Chip c(cfg()); TileRef p = c.tile(7);
        for (int i = 0; i < N; i++) p.sram_poke64(IDX + 2 * uint32_t(i), uint64_t(N - 1 - i) << 32 | uint32_t(i));
        for (int v = 0; v < N; v++) for (int i = 0; i < k; i++) p.sram_poke64(VAL + 2 * uint32_t(k * v + i), uint64_t(v) << 8 | uint32_t(i));
        p.set_stream(195, fields::stream_src(IDX), 0, fields::stream_count(N, fields::SIZE_64, 1, lw::M_GATHER, 0));
        p.set_ptr(7, OUT); p.set_record_base(VAL);
        const int arm = old ? N * k : N * ppv(k);
        p.set_remaining(1, uint32_t(arm));
        p.set_master_config(fields::master_config(fields::RUN_STREAMS, 195, 1, 0, 0, uint32_t(k)));
        p.pulse(fields::PULSE_START);
        c.run(5000);
        if (!old) {
            bool ok = true; for (int i = 0; i < N; i++) for (int e = 0; e < k; e++) if (p.sram_peek64(OUT + 2 * uint32_t(k * i + e)) != (uint64_t(N - 1 - i) << 8 | uint32_t(e))) ok = false;
            check(ok, "k = %d: the values landed in index order", k);
            eq(0, p.remaining(1), "k = %d: armed with n x %d (packets per value), the counter finishes at 0", k, ppv(k));
            eq(int64_t(OUT) + 2LL * k * N, p.ptr(7), "k = %d: PTR[me] advanced by 2k per value (%s)", k, (k & 1) == 0 ? "4 per 128-bit packet" : "2 per 64-bit packet");
        } else eq(N * k / 2, p.remaining(1), "k = %d: armed with the old n x k, the counter is left at n x k / 2: not done", k);
    }
    const uint32_t offs[3][2] = {{2, 0}, {0, 2}, {0, 1}};
    for (const auto& off : offs) {
        Chip c(cfg()); TileRef p = c.tile(7);
        p.set_stream(195, fields::stream_src(IDX), 0, fields::stream_count(4, fields::SIZE_64, 1, lw::M_GATHER, 0));
        p.set_ptr(7, OUT + off[1]); p.set_record_base(VAL + off[0]);
        p.set_remaining(1, 4); p.set_master_config(fields::master_config(fields::RUN_STREAMS, 195, 1, 0, 0, 1));
        p.pulse(fields::PULSE_START);
        const char* what = off[0] != 0 ? "DMA_RECORD_BASE_ADDRESS" : "PTR[me]";
        suite::ub([&] { c.run(200); }, {"tile 7", "gather", what}, "k = 1, %s off a 128-bit boundary by %u chunk(s): stops at the run's start", what, std::max(off[0], off[1]));
    }
}

void gather() {
    suite::group("gather");
    std::string sb = "local gather on every tile: the index stream (mode 1, d = 0, 64-bit) from area 3, values from area 1, output at PTR[me] in area 5; 0/+1 skew [meas: ReadPortGatherTest gather]:\n";
    const int N = 4096;
    for (int k : {1, 2, 3, 4, 8}) gather_run(k, N, sb);
    if (full) gather_run(2, 65536, sb);
    out(strip_trailing(sb));
}

void gather_from_c() {
    suite::group("gather from C");
    const int N = full ? 65536 : 4096, k = 2;
    Mach m(Mach::cfg(shuffle));
    const uint32_t IDX = 0x60000, VAL = 0x20000, OUT = 0xA0000;
    std::vector<std::vector<int>> perm(64);
    for (unsigned p = 0; p < 64; p++) {
        TileRef t = m.c.tile(p);
        p4::JRandom r(100 + int64_t(p));
        std::vector<int> pm(static_cast<size_t>(N)); for (int i = 0; i < N; i++) pm[size_t(i)] = i;
        for (int i = N - 1; i > 0; i--) { const int j = r.next_int(i + 1); std::swap(pm[size_t(i)], pm[size_t(j)]); }
        perm[p] = pm;
        for (int i = 0; i < N; i++) t.sram_poke64(IDX + 2 * uint32_t(i), uint64_t(uint32_t(pm[size_t(i)])) << 32 | uint32_t(i));
        for (int v = 0; v < N; v++) for (int i = 0; i < k; i++) t.sram_poke64(VAL + 2 * uint32_t(k * v + i), uint64_t(p) << 48 | uint64_t(v) << 8 | uint32_t(i));
    }
    const Mach::Image im = m.boot("gather");
    m.c.cc().command(lw::BROADCAST, 1u << 24 | uint32_t(N)).go().command(lw::BROADCAST, 2u << 24).go();
    const bool ok = m.run([&] { return m.all_stopped(); }, 5000000);
    check(ok && m.all_exited(), "every tile ran gather to its exit: %s", m.halt_summary().c_str());
    int64_t bad = 0;
    for (unsigned p = 0; p < 64; p++) for (int i = 0; i < N; i++) for (int e = 0; e < k; e++)
        if (m.c.tile(p).sram_peek64(OUT + 2 * uint32_t(k * i + e)) != (uint64_t(p) << 48 | uint64_t(perm[p][size_t(i)]) << 8 | uint32_t(e))) bad++;
    eq(0, bad, "every value landed in sorted order");
    no_violations(m.c, "gather from C");
    const auto ph = m.c.cc().phase_log();
    const int64_t cyc = ph.at(1)[0] - ph.at(0)[1];
    out("gather 1 from C (sw/tests/gather.c, one-command phase, " + p4::jgroup(N) + " values, k = 2) [meas: ReadPortGatherTest gather from C]: C&C's GO -> all 64 READY " +
        p4::jgroup(cyc) + " cycles (" + p4::jfixed(double(cyc) / N, 3) + " per value)" + (full ? "; the walk-through estimates 196,648" : ""));
    (void)im;
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
    if (shuffle >= 0) out(suite::str("shuffled child order, seed %lld", (long long)shuffle));
    if (want("fromc")) read_port_from_c();
    // traffic, tolerance, overlap: W22-18 (the read port's host queue); disjoint: W22-14
    if (want("reserved")) reserved_streams();
    if (want("gather")) gather();
    if (want("counting")) gather_counting_and_alignment();
    if (want("gatherc")) gather_from_c();
    return suite::done(std::string("ReadPortGatherTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : "") + (full ? " full" : ""));
}
#else
int main() {
    std::fprintf(stderr, "suite_rdport: needs SIM_CHECKS (the suite build, MODE=suite, or a debug build)\n");
    return 2;
}
#endif
