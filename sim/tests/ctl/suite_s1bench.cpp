// suite_s1bench.cpp: v1.3's S1Bench (sim/ref/meshsim/test/S1Bench.java) on the harness
// and the S1 measurement (src/units/s1.h), check for check and line for line. run_tests.sh's step `s1bench` (full
// mode); its output equals ref's log (tests/ctlsuites/s1bench.log). S1 is measured in the debug and suite builds.
//
// Item S1, measured (DECISIONS-v1.2 S1; spec 4.8, 4.10, invariant 24): for every counted write into big SRAM, PTR and
// HIST, acceptance -> complete, against K, across workloads and ts_engine_latency L = 1-4 (K = max(8, kMin(L))), and the
// table-window case. Workloads, from C: the v1.2 recipes' radix sort (rcp_bucket), the all-to-all (rcp_a2a) and gather
// (rcp_gather). meshsim flags a write complete later than K + 4; the margin reported is K minus the largest latency.
#include <cstdio>
#include <string>

#include "units/director.h"
#include "../java_random.h"
#include "../suite.h"
#include "ctl.h"

using mach::Mach;

namespace {
int32_t next_int(test::JavaRandom& r) { return r.next(32); }
uint64_t rec(uint32_t lo, uint32_t hi) { return uint64_t(lo) | uint64_t(hi) << 32; }
}  // namespace

int main() {
    for (unsigned L = 1; L <= 4; L++) {
        for (const char* w : {"rcp_bucket", "rcp_a2a", "rcp_gather"}) {
            const mach::MachConfig k = Mach::cfg(-1).with_ts_latency(L);
            Mach m(k);
            m.boot(w);
            test::JavaRandom r(L);
            const std::string ws = w;
            if (ws == "rcp_bucket") {
                for (unsigned t = 0; t < 64; t++) for (uint32_t i = 0; i < 200; i++) m.c.tile(t).sram_poke64(2 * i, rec(uint32_t(next_int(r)), i));
                m.c.cc().command(ctl::BROADCAST, 1 << 24 | 200).go();
            } else if (ws == "rcp_a2a") {
                for (unsigned t = 0; t < 64; t++) for (uint32_t i = 0; i < 512; i++) m.c.tile(t).sram_poke64(2 * i, rec(i, t));
                m.c.cc().command(ctl::BROADCAST, 1 << 24).go().command(ctl::BROADCAST, 2 << 24).go().command(ctl::BROADCAST, 3 << 24).go();
            } else {
                for (unsigned t = 0; t < 64; t++) { const uint32_t n = 50 + t; for (uint32_t i = 0; i < n; i++) m.c.tile(t).sram_poke64(2 * i, rec(i, (i * 7) % n)); }
                m.c.cc().command(ctl::BROADCAST, 1 << 24 | 2).go();
            }
            m.c.cc().command(ctl::BROADCAST, 0x7E << 24).go();
            const bool ok = m.run([&] { return m.all_stopped(); }, 2000000);
            suite::check(ok && m.all_exited(), "L = %u, %s: ran to its exit: %s", L, w, m.halt_summary().c_str());
            uint64_t late = 0, sram_max = 0, ptr_max = 0, hist_max = 0;   // ref's chip.s1, summed over the tiles
            for (unsigned t = 0; t < 64; t++) {
                const auto& s1 = m.c.unit<units::Director>(units::tile_unit(t, "dir")).s1;
                late += s1->late; sram_max = std::max(sram_max, s1->sram_max); ptr_max = std::max(ptr_max, s1->ptr_max); hist_max = std::max(hist_max, s1->hist_max);
            }
            suite::eq(0, int64_t(late), "L = %u, %s: no write complete later than K + 4", L, w);
            const uint64_t worst = std::max(sram_max, std::max(ptr_max, hist_max));
            suite::check(m.c.violations() == 0, "L = %u, %s: no violations %s", L, w, m.c.violation_report().c_str());
            suite::check(worst <= k.K, "L = %u, %s: every write complete by its decrement (K = %u)", L, w, k.K);
            suite::eq(2LL * L + 4, int64_t(sram_max), "L = %u, %s: the big-SRAM write path is 2L + 4 (D49)", L, w);
            std::printf("L = %u, K = %u, %-11s %s [meas: S1Bench]\n", L, k.K, (ws + ":").c_str(), m.c.s1_report().c_str());
        }
    }
    return suite::done("S1Bench");
}
