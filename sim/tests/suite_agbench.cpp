// suite_agbench.cpp: v1.3's AgBench (sim/ref/meshsim/test/AgBench.java): the address
// generator's read-ahead measured on the coreless machine (tests/mach.h): the smallest ag_lookahead with no cycle lost,
// with and without read-queue stalls; runs of zero-length groups; start to the first request. Every printed line is
// Java's (AgBench prints as it goes and has no assertions), so the output equals run_tests.sh's log agbench (fixture
// tests/p4suites/agbench.log, made on the runner by Quest 176).
//
//   suite_agbench [all|lookahead|zero|ready]
//
// One tile (0) runs; everything goes to the tile itself in mode 2 (d = 0). A run that ref's would throw on (never done,
// the start taking effect elsewhere than the store's cycle + 1, an unexpected violation) is FATAL here.
//
// Test-side code (the configuration regime, D6).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "mach.h"
#include "p4_debug.h"
#include "suite.h"
#include "units/ag.h"
#include "units/linkword.h"

using namespace mach;
namespace lw = units::lw;

namespace {

// Tables for tile p; returns {kind, start, count, elements}
using Setup = std::function<std::array<uint32_t, 4>(TileRef p, uint32_t src_base)>;
constexpr uint32_t SRC = 0x1000, DST_OTHER = 0x80000, DST_SAME = 0x10000;   // areas 0 and 4; DST_SAME in area 0, with the sources

struct Res { int64_t cycles = 0, late = 0, underrun = 0, table_wait = 0, start_to_first = 0, stalls = 0, group0_late = 0; std::vector<int64_t> group_start; };

Res run(unsigned L, int la, bool stalls, bool /*one_store*/, int /*idle*/, const Setup& s) {
    MachConfig k = MachConfig::defaults(); k.check_unexpected = false; k.check_errors = false; k.with_ts_latency(L);
    if (la > 0) k.ag_lookahead = la;
    Chip c(p4::dbg(k)); TileRef p = c.tile(0);
    const auto r = s(p, SRC);
    p.set_ptr(0, stalls ? DST_SAME : DST_OTHER); p.set_remaining(1, r[3]);
    p.set_master_config(fields::master_config(r[0], r[1], r[2], 0, 0, 1));
    const int64_t store_at = c.cycle();                     // the host pulse stands for a DMA_PULSE store in this cycle
    Res out;
    p.pulse(fields::PULSE_START);
    const units::AddrGen& a = p.unit<units::AddrGen>("ag");
    if (!c.run_until([&] { return a.done; }, 2000000)) FATAL("AgBench: never done");
    out.start_to_first = a.first_request_at < 0 || a.started_at < 0 ? -1 : a.first_request_at - a.started_at;
    if (a.started_at != store_at + 1) FATAL("AgBench: the start took effect at %lld, store at %lld", (long long)a.started_at, (long long)store_at);
    out.cycles = a.done_at - a.started_at;
    out.late = int64_t(a.groups_late_prefetch); out.stalls = int64_t(a.stall_cycles); out.table_wait = int64_t(a.table_wait_cycles);
    out.underrun = std::max<int64_t>(0, int64_t(a.underrun_cycles));
    out.group0_late = a.group_late_by[0];
    out.group_start.assign(a.group_start, a.group_start + units::AddrGen::MAXGROUPS);
    for (const auto& m : c.violation_messages())             // under stalls, late rotations are expected
        if (!(stalls && m.find("rotation started late") != std::string::npos)) FATAL("AgBench: violations:\n%s", c.violation_report().c_str());
    return out;
}

// g groups of t streams, R rotations each (R[] cycles through), gap 0, the given size
Setup groups(uint32_t g, uint32_t t, std::vector<uint32_t> Rs, uint32_t size) {
    return [=](TileRef p, uint32_t base) {
        uint32_t stream = 0, src = base, el = 0; const uint32_t cpe = 1u << size;
        for (uint32_t i = 0; i < g; i++) {
            const uint32_t R = Rs[i % Rs.size()];
            p.set_group(i, fields::group_lo(R), fields::group_hi(stream & 255, t, 0));
            for (uint32_t s = 0; s < t; s++, stream++) { p.set_stream(stream & 255, fields::stream_src(src), 0, fields::stream_count(R, size, 1, lw::M_SRC, 0)); src += R * cpe; el += R; }
        }
        return std::array<uint32_t, 4>{fields::RUN_GROUPS, 0, g, el};
    };
}
// a stream run of n streams of count c, gap after gap
Setup streams(uint32_t n, uint32_t cnt, uint32_t gap) {
    return [=](TileRef p, uint32_t base) {
        for (uint32_t s = 0; s < n; s++) p.set_stream(s, fields::stream_src(base + 2 * cnt * s, gap), 0, fields::stream_count(cnt, fields::SIZE_64, 1, lw::M_SRC, 0));
        return std::array<uint32_t, 4>{fields::RUN_STREAMS, 0, n, n * cnt};
    };
}
// group A (t = 4, R = 1), k zero-length groups (R = 0, gap 0), group B (t = 4, R = 2), group C (t = 4, R = 1)
Setup zeros(uint32_t k) {
    return [=](TileRef p, uint32_t base) {
        uint32_t src = base, stream = 0, g = 0;
        std::vector<uint32_t> R(k + 3, 0); R[0] = 1; R[k + 1] = 2; R[k + 2] = 1;
        for (uint32_t i = 0; i < k + 3; i++) {
            p.set_group(g++, fields::group_lo(R[i]), fields::group_hi(stream, 4, 0));
            for (uint32_t s = 0; s < 4; s++, stream++) { p.set_stream(stream, fields::stream_src(src), 0, fields::stream_count(R[i], fields::SIZE_64, 1, lw::M_SRC, 0)); src += 2 * R[i]; }
        }
        return std::array<uint32_t, 4>{fields::RUN_GROUPS, 0, k + 3, 4 * 4};
    };
}
// a stream run: a count-4 stream, k count-0 gap-0 streams, a count-4 stream
Setup zero_streams(uint32_t k) {
    return [=](TileRef p, uint32_t base) {
        p.set_stream(0, fields::stream_src(base), 0, fields::stream_count(4, fields::SIZE_64, 1, lw::M_SRC, 0));
        for (uint32_t i = 1; i <= k; i++) p.set_stream(i, fields::stream_src(base), 0, fields::stream_count(0, fields::SIZE_64, 1, lw::M_SRC, 0));
        p.set_stream(k + 1, fields::stream_src(base + 8), 0, fields::stream_count(4, fields::SIZE_64, 1, lw::M_SRC, 0));
        return std::array<uint32_t, 4>{fields::RUN_STREAMS, 0, k + 2, 8};
    };
}

void lookahead() {
    std::printf("\n== ag_lookahead: cycles lost to the read-ahead (run cycles above ag_lookahead 64), by ag_lookahead 1..12 (searched to 20); min = the smallest from which no more groups start late and no more visits wait for an entry than at ag_lookahead 64 (group starts late + underrun cycles + stream-run table waits) [meas: AgBench lookahead]\n");
    struct Shape { const char* name; Setup s; };
    const std::vector<Shape> shapes = {
        {"16 groups t=4 R=1 (the sort's shortest)", groups(16, 4, {1}, fields::SIZE_64)},
        {"64 groups t=1 R=1 (one visit per group)", groups(64, 1, {1}, fields::SIZE_64)},
        {"64 groups t=2 R=1", groups(64, 2, {1}, fields::SIZE_64)},
        {"16 groups t=16 R=1", groups(16, 16, {1}, fields::SIZE_64)},
        {"16 groups t=4 R=1, 128-bit", groups(16, 4, {1}, fields::SIZE_128)},
        {"16 groups t=4 R=3,1,0,2", groups(16, 4, {3, 1, 0, 2}, fields::SIZE_64)},
        {"stream run 63 x count 1, gap 0", streams(63, 1, 0)},
        {"stream run 63 x count 1, gap 4", streams(63, 1, 4)},
        {"stream run 32 x count 3, gap 0", streams(32, 3, 0)},
    };
    for (bool stalls : {false, true}) {
        std::printf(stalls ? "\n-- with read-queue stalls (writes land in the source area and win ties)\n" : "\n-- no stalls\n");
        std::printf("  %-42s %3s %6s  %-44s %4s %5s\n", "shape", "L", "cycles", "lost at ag_lookahead 1..12", "min", "2L+2");
        if (stalls) std::printf("  (cycles: start to rd_done at ag_lookahead 64, stalls included)\n");
        for (const Shape& sh : shapes)
            for (unsigned L = 1; L <= 4; L++) {
                const Res ref = run(L, 64, stalls, false, 64, sh.s);
                std::string sb, se; int min = -1;
                for (int la = 1; la <= 20; la++) {
                    const Res x = run(L, la, stalls, false, 64, sh.s);
                    const int64_t lost = x.cycles - ref.cycles, ev = (x.late + x.underrun + x.table_wait) - (ref.late + ref.underrun + ref.table_wait);
                    if (la <= 12) { sb += std::to_string(lost) + (la < 12 ? " " : ""); se += std::to_string(ev) + (la < 12 ? " " : ""); }
                    if (ev == 0 && min < 0) min = la;
                    if (ev != 0) min = -1;
                    if (lost < 0 && !stalls) FATAL("AgBench: faster than the reference");
                }
                std::printf("  %-42s %3u %6lld  %-44s %4s %5u%s\n", sh.name, L, (long long)ref.cycles, sb.c_str(), min < 0 ? ">20" : std::to_string(min).c_str(), 2 * L + 2,
                            stalls ? suite::str("  (stall cycles %lld)", (long long)ref.stalls).c_str() : "");
                if (stalls) std::printf("  %-42s %3s %6s  %-44s\n", "  read-ahead events above ag_lookahead 64", "", "", se.c_str());
            }
    }
}

void zero() {
    std::printf("\n== runs of k zero-length groups (R = 0, gap 0) between a t=4 R=1 group and a t=4 R=2 group: cycles group B starts late (actual - scheduled) [meas: AgBench zero]\n");
    const uint32_t ks[] = {0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
    std::string h; for (uint32_t k : ks) h += suite::str("%5u", k);
    std::printf("  %-28s%s\n", "k =", h.c_str());
    for (unsigned L = 1; L <= 4; L++) {
        std::string sb, sbc;
        for (uint32_t k : ks) {
            const Res x = run(L, -1, false, false, 64, zeros(k));
            sb += suite::str("%5lld", (long long)(x.group_start[k + 1] - 4));
            sbc += suite::str("%5lld", (long long)(x.group_start[k + 2] - (4 + 8)));
        }
        std::printf("  %-28s%s\n", suite::str("L %u: B late by", L).c_str(), sb.c_str());
        std::printf("  %-28s%s\n", suite::str("L %u: C late by (B's + its own)", L).c_str(), sbc.c_str());
    }
    std::printf("  stream run: a count-4 stream, k count-0 gap-0 streams, a count-4 stream: cycles above k = 0\n");
    for (unsigned L = 1; L <= 4; L++) {
        std::string sb; const int64_t base = run(L, -1, false, false, 64, zero_streams(0)).cycles;
        for (uint32_t k : ks) sb += suite::str("%5lld", (long long)(run(L, -1, false, false, 64, zero_streams(k)).cycles - base));
        std::printf("  %-28s%s\n", suite::str("L %u: stream run", L).c_str(), sb.c_str());
    }
}

void ready() {
    std::printf("\n== start to the first request [meas: AgBench start]: cycles from the start taking effect (the cycle after the DMA_PULSE\n");
    std::printf("   store) to the address generator's first request; spec 8.2 (v1.0): 4 L + 2 for a group run, 2 L + 1 for a stream run\n");
    std::printf("  %-34s %s\n", "", "L=1  L=2  L=3  L=4");
    for (uint32_t t : {1u, 4u, 8u, 16u}) {
        std::string a;
        for (unsigned L = 1; L <= 4; L++) a += suite::str("%4lld ", (long long)run(L, -1, false, false, 0, groups(2, t, {2}, fields::SIZE_64)).start_to_first);
        std::printf("  %-34s %s\n", suite::str("group run, group 0 t=%u", t).c_str(), a.c_str());
    }
    {
        std::string a, b;
        for (unsigned L = 1; L <= 4; L++) { a += suite::str("%4lld ", (long long)run(L, -1, false, false, 0, streams(8, 2, 0)).start_to_first); b += suite::str("%4u ", 2 * L + 1); }
        std::printf("  %-34s %s\n", "stream run", a.c_str());
        std::printf("  %-34s %s\n", "(2 L + 1 for reference)", b.c_str());
    }
    {
        std::string a;
        for (unsigned L = 1; L <= 4; L++) a += suite::str("%4lld ", (long long)run(L, -1, false, true, 0, streams(8, 2, 0)).cycles);
        std::printf("  %-34s %s\n", "stream run 8 x 2: start to rd_done", a.c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a;
    for (int i = 1; i < argc; i++) if (!p4::debug_flag(argv[i])) a.push_back(argv[i]);
    const std::string what = !a.empty() ? a[0] : "all";
    std::printf("AgBench on M8 (read-ahead)\n");
    if (what == "all" || what == "lookahead") lookahead();
    if (what == "all" || what == "zero") zero();
    if (what == "all" || what == "ready") ready();
    return 0;
}
