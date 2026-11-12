// suite_search.cpp (WORKER22, P4, inventory C11): v1.3's SearchBench (sim/ref/meshsim/test/SearchBench.java): search
// steps through the controller read port (fw/tests/search.c on every tile of the machine as specified), cycles per step
// against the number of active searches, the lower bounds checked. Every assertion and printed line is Java's, so the
// output equals run_tests.sh's log searchbench (fixture tests/p4suites/searchbench.log).
//
//   suite_search [seed]
//
// search.c's mark() is a TRACE store (8.15): the step marks come from W20's TRACE log (TileRef::record_trace, H6).
// Java's %.1f, %.2f (HALF_UP on the shortest digits) and %,d: p4_java.h.
//
// Test-side code (the configuration regime, D6).
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "mach.h"
#include "p4_debug.h"
#include "p4_java.h"
#include "suite.h"
#include "units/linkword.h"

using namespace mach;
namespace lw = units::lw;
using suite::check;
using suite::eq;

namespace {

const int BS[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 63};
const char* const VARIANT[] = {"state by index", "state packed"};

using ByActive = std::map<int, std::array<int64_t, 2>>;   // active -> {cycles, steps} (Java's TreeMap, key order)

std::array<double, 2> fit(const ByActive& m) {             // SearchBench.fit: the same operations in the same order
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& e : m) {
        const double x = e.first, w = double(e.second[1]), y = double(e.second[0]) / w;
        n += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y;
    }
    const double b = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return {(sy - b * sx) / n, b};
}

int lower_bound_u(const std::vector<uint32_t>& k, uint32_t target) {
    int lo = 0, hi = int(k.size());
    while (lo < hi) { const int mid = int(unsigned(lo + hi) >> 1); if (k[size_t(mid)] < target) lo = mid + 1; else hi = mid; }
    return lo;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a;
    for (int i = 1; i < argc; i++) if (!p4::debug_flag(argv[i])) a.push_back(argv[i]);
    const int64_t shuffle = !a.empty() ? std::atoll(a[0].c_str()) : -1;
    std::string sb = "search steps through the controller read port (spec 4.3's loop with early exit, sw/tests/search.c, -O2), every tile at once [meas: SearchBench]\n";
    for (int N : {65536, 4096}) {
        Mach m(p4::dbg(Mach::cfg(shuffle)));
        p4::JRandom r(N);
        std::vector<std::vector<uint32_t>> keys(64, std::vector<uint32_t>(size_t(N)));
        std::vector<std::array<int32_t, 64>> targets(64);
        for (unsigned p = 0; p < 64; p++) {
            TileRef t = m.c.tile(p);
            std::vector<uint32_t> k(static_cast<size_t>(N));
            for (int i = 0; i < N; i++) k[size_t(i)] = uint32_t(r.next_int());
            std::sort(k.begin(), k.end());
            for (int i = 0; i < N; i++) { keys[p][size_t(i)] = k[size_t(i)]; t.sram_poke(uint32_t(i), k[size_t(i)]); }
            t.sram_poke(0xF0000, uint32_t(N)); t.sram_poke(0xF0001, 0);
            for (int i = 0; i < 64; i++) { targets[p][size_t(i)] = i == 0 ? 0 : i == 1 ? -1 : r.next_int(); t.sram_poke(0xF0002 + uint32_t(i), uint32_t(targets[p][size_t(i)])); }
        }
        const Mach::Image im = m.boot("search");
        for (unsigned p = 0; p < 64; p++) m.c.tile(p).record_trace();      // search.c's mark(): a TRACE store
        ByActive by_active[2];
        int64_t total[2][64] = {};
        int bad = 0, k = 0;
        for (int v = 0; v < 2; v++) for (int B : BS) {
            uint64_t d0[64]; size_t base[64];
            for (unsigned p = 0; p < 64; p++) { d0[p] = m.c.tile(p).block().dones; base[p] = m.c.tile(p).trace_log().size(); }
            m.c.cc().command(lw::BROADCAST, 1u << 24 | uint32_t(v) << 8 | uint32_t(B)).go();
            m.run([&] { return !m.c.cc().busy(); }, 5000000);
            for (unsigned p = 0; p < 64; p++) d0[p] = m.c.tile(p).block().dones;
            const bool ok = m.run([&] { for (unsigned p = 0; p < 64; p++) if (m.c.tile(p).block().dones <= d0[p]) return false; return true; }, 5000000);
            check(ok, "N = %d, %s, B = %d: every tile finished", N, VARIANT[v], B);
            for (unsigned p = 0; p < 64; p++) {
                const auto mk = m.c.tile(p).trace_log();
                const int steps = int(mk.size()) - int(base[p]) - 1;
                for (int s = 0; s < steps; s++) {
                    const int na = int32_t(m.word(p, im, "nact", unsigned(s)));
                    auto& e = by_active[v][na];
                    e[0] += mk[base[p] + size_t(s) + 1][0] - mk[base[p] + size_t(s)][0]; e[1]++;
                }
                if (steps >= 0) total[v][B] = std::max(total[v][B], mk.back()[0] - mk[base[p]][0]);
                int32_t c = 0; for (int i = 0; i < B; i++) c = int32_t(uint32_t(c) + uint32_t(lower_bound_u(keys[p], uint32_t(targets[p][size_t(i)])) * (i + 1)));
                if (int32_t(m.word(p, im, "chk", unsigned(k & 63))) != c) bad++;
            }
            k++;
        }
        m.c.cc().command(lw::BROADCAST, 2u << 24).go();
        const bool exited = m.run([&] { return m.all_stopped(); }, 100000) && m.all_exited();
        check(exited, "N = %d: every tile exited: %s", N, m.halt_summary().c_str());
        eq(0, bad, "N = %d: every run's lower bounds exact on every tile (checksums)", N);
        check(m.c.violations() == 0, "N = %d: no violations %s", N, m.c.violation_report().c_str());
        int64_t ovf = 0; for (unsigned p = 0; p < 64; p++) ovf += int64_t(m.c.tile(p).block().rp_errors);   // ReadPort.overflows
        eq(0, ovf, "N = %d: no read-port overflow", N);
        sb += "  N = " + p4::jgroup(N) + suite::str(" (a full search takes %d steps):\n", 32 - __builtin_clz(unsigned(N)));
        sb += "    active searches   cycles per step: by index | packed    per search-step: by index | packed\n";
        for (int na : BS) {
            const auto a = by_active[0].find(na), b = by_active[1].find(na);
            if (a == by_active[0].end() || b == by_active[1].end()) continue;
            const double ca = double(a->second[0]) / double(a->second[1]), cb = double(b->second[0]) / double(b->second[1]);
            sb += suite::str("    %15d   %s | %s    %s | %s\n", na, p4::jfixed(ca, 1, 26).c_str(), p4::jfixed(cb, 1, 6).c_str(),
                             p4::jfixed(ca / na, 2, 24).c_str(), p4::jfixed(cb / na, 2, 5).c_str());
        }
        for (int v = 0; v < 2; v++) {
            const auto f = fit(by_active[v]);
            sb += suite::str("    %-15s a step costs %s + %s x active cycles (least squares over every step); 63 searches from first step to last: %s cycles\n",
                             (std::string(VARIANT[v]) + ":").c_str(), p4::jfixed(f[0], 1).c_str(), p4::jfixed(f[1], 2).c_str(), p4::jgroup(total[v][63]).c_str());
        }
    }
    std::fputs(sb.c_str(), stdout);
    return suite::done("SearchBench");
}
