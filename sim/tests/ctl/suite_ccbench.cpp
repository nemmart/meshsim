// suite_ccbench.cpp (WORKER23, P5, inventory G3): v1.3's CcBench (sim/ref/meshsim/test/CcBench.java) on W20's harness,
// line for line. run_tests.sh's step `ccbench` (full mode); its output equals ref's log (tests/ctlsuites/ccbench.log).
//
// WORKER14's measurements: C&C 1-3 as cores (Config.ccCores) against the Java model (CnC).
//  - the boot (6.2), reset release to all 64 tiles running their image, with rcp_boot (recipe 22's tile side: the
//    RECIPES table's 120 cycles, 62 broadcast packets) and with cmd; the cores' boot broken down;
//  - a phase: back-to-back broadcast commands (cmd's type 1, one argument) with READY/GO, GO to GO in the steady state.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "ctl.h"
#include "../suite.h"

using namespace ctl;
using mach::Chip;
using mach::Mach;

namespace {

int64_t all_running(Chip& c) {   // the latest firstImageAt over the 64 tiles, -1 while any hasn't run its image
    int64_t r = 0;
    for (unsigned k = 0; k < 64; k++) { const int64_t f = core(c, k).first_image_at; if (f < 0) return -1; r = std::max(r, f); }
    return r;
}

void boot(const std::string& img) {
    Mach j(Mach::cfg(-1));
    j.boot(img);
    j.run([&] { return all_running(j.c) >= 0; }, 20000);
    const int64_t model = all_running(j.c);
    Mach m(Mach::cores_cfg(-1));
    m.boot_cores(img);
    int64_t started = -1, all_ready = -1, go = -1;
    const uint64_t gos = cc1p(m.c).gos;
    while (all_running(m.c) < 0 && m.c.cycle() < 20000) {
        m.c.step();
        if (started < 0 && m.mbox(3) >= 1) started = m.c.cycle();
        if (all_ready < 0 && cc1p(m.c).ready_bits(m.c.cycle()) == ~uint64_t(0)) all_ready = m.c.cycle();
        // ref's gos counts PULSE_GO in the store's cycle; C&C 1's peripheral here applies the store at t + 1 (D33) and
        // keeps the store's cycle as last_go_store: ref's cycle of the GO (the GO net, and the tiles, are unshifted)
        if (go < 0 && cc1p(m.c).gos > gos) go = int64_t(cc1p(m.c).last_go_store);
    }
    const int64_t cc1 = core(m.c, 64).first_image_at, cc23 = std::max(core(m.c, 65).first_image_at, core(m.c, 66).first_image_at),
                  all = all_running(m.c);
    const Mach::Image t = Mach::Image::load(img, m.c.cfg());
    const Mach::Image& c1 = m.cc1;
    std::printf("boot '%s' (%zu + %zu packets to each tile): the Java model %lld cycles; cores %lld cycles\n", img.c_str(),
                (t.code.size() + 3) / 4, (t.data.size() + 3) / 4, (long long)model, (long long)all);
    std::printf("  the host lands C&C 1's image (%zu packets) and CMD_JUMP: C&C 1 running at %lld\n",
                (c1.code.size() + 3) / 4 + (c1.data.size() + 3) / 4 + 2, (long long)cc1);
    std::printf("  C&C 1 stages the commands and streams C&C 2/3's images: both running at %lld (+%lld)\n", (long long)cc23,
                (long long)(cc23 - cc1));
    std::printf("  both \"started\" requests served by %lld (+%lld)\n", (long long)started, (long long)(started - cc23));
    std::printf("  the tiles' image streamed, all READY at %lld (+%lld); GO at %lld (+%lld); all 64 running at %lld (+%lld)\n",
                (long long)all_ready, (long long)(all_ready - started), (long long)go, (long long)(go - all_ready),
                (long long)all, (long long)(all - go));
    std::printf("  from C&C 1 running: %lld cycles; violations %llu\n", (long long)(all - cc1), (unsigned long long)m.c.violations());
}

// The cycles of the next n GO pulses (ref's cc1p.gos, polled after each cycle): `count` the GOs so far, `store` the
// cycle of the last one's store (ref's cycle of it: the C&C 1 peripheral's last_go_store, D33; the model's lastGoAt).
std::vector<int64_t> gos(Mach& m, unsigned n, const std::function<uint64_t()>& count, const std::function<int64_t()>& store) {
    std::vector<int64_t> at(n);
    unsigned k = 0; uint64_t g = count();
    while (k < n && m.c.cycle() < 2000000) {
        m.c.step();
        if (count() > g + 1) FATAL("ccbench: two GOs in one cycle");
        if (count() > g && k < n) { g++; at[k++] = store(); }
    }
    return at;
}
std::string stats(const std::vector<int64_t>& at, unsigned last) {
    int64_t mn = INT64_MAX, mx = 0, sum = 0;
    for (size_t i = at.size() - last; i < at.size(); i++) { const int64_t d = at[i] - at[i - 1]; mn = std::min(mn, d); mx = std::max(mx, d); sum += d; }
    return suite::str("%s (%lld-%lld)", java_fixed(double(sum) / last, 1).c_str(), (long long)mn, (long long)mx);
}

void phase(unsigned n) {
    Mach j(Mach::cfg(-1));
    j.boot("cmd");
    j.run([&] { return all_running(j.c) >= 0 && j.all_in_wfi(); }, 20000);
    for (unsigned i = 0; i < n; i++) j.c.cc().command(BROADCAST, 1 << 24 | 1, {1}).go();
    // ref's j.c.cc1p.gos: on the Java model the chip's C&C 1 peripheral counts the model's GO pulses
    const std::vector<int64_t> mg = gos(j, n, [&] { return model_gos(j.c); }, [&] { return j.c.unit<units::Cnc>("cnc").last_go_at; });
    Mach m(Mach::cores_cfg(-1));
    m.boot_cores("cmd");
    m.run([&] { return m.cores_booted() && m.all_in_wfi(); }, 20000);
    m.cc1_command(Mach::CC_PHASE, n << 16 | 1, {1 << 24 | 1, 1});
    const std::vector<int64_t> cg = gos(m, n, [&] { return cc1p(m.c).gos; }, [&] { return int64_t(cc1p(m.c).last_go_store); });
    std::printf("phase: %u back-to-back broadcast commands (cmd type 1, one argument), READY then GO each\n", n);
    std::printf("  GO to GO, steady state (the last %u): the Java model %s; cores %s\n", n / 2, stats(mg, n / 2).c_str(), stats(cg, n / 2).c_str());
    std::printf("  violations: model %llu, cores %llu\n", (unsigned long long)j.c.violations(), (unsigned long long)m.c.violations());
}

}  // namespace

int main() {
    for (const char* img : {"rcp_boot", "cmd"}) boot(img);
    phase(32);
    return 0;
}
