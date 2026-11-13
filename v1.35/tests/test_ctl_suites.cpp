// test_ctl_suites.cpp: v1.3's control suites in make check. Each suite program (suite_*, built beside
// this binary) runs as run_tests.sh runs its step (v1.3's arguments; stdout and stderr together); its output must equal
// ref's log of the same step byte for byte (tests/ctlsuites/<step>.log, from oracle/ref/ctlsuites.sh), and it must exit
// 0. Run from sim/.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "harness.h"

namespace {

std::string self_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, size_t(n));
    return p.substr(0, p.rfind('/'));
}

// D44 (D33 reached): CcCoresTest's error path prints C&C 1's UART dump end one cycle later here (C&C 1's peripheral
// applies stores at t + 1); that one figure is masked on both sides, as run_tests.sh's D44_MASK.
// (No std::regex: gcc 13's -Wmaybe-uninitialized false warning inside it fails the gcc debug build.)
std::string mask_d44(const std::string& s) {
    static const std::string key = "dump done at +";
    std::string r;
    size_t at = 0;
    for (size_t p = s.find(key); p != std::string::npos; p = s.find(key, at)) {
        size_t q = p + key.size();
        while (q < s.size() && s[q] >= '0' && s[q] <= '9') q++;
        r.append(s, at, p - at);
        r += q > p + key.size() ? "dump done at +<D44>" : key;
        at = q;
    }
    return r + s.substr(at);
}

void run_step(const char* step, const char* program, const char* args, bool d44 = false) {
    const std::string cmd = self_dir() + "/" + program + " " + args + " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    CHECK(p != nullptr);
    std::string out;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int status = pclose(p);
    std::ifstream f(std::string("tests/ctlsuites/") + step + ".log", std::ios::binary);
    CHECK(f.good());
    std::stringstream ref;
    ref << f.rdbuf();
    std::string want = ref.str();
    if (d44) { out = mask_d44(out); want = mask_d44(want); }
    if (out != want) {
        ::test::fail(__FILE__, __LINE__, "step %s (%s %s): output differs from ref's log:\n--- C++\n%s--- ref\n%s", step, program,
                     args, out.c_str(), ref.str().c_str());
        return;
    }
    CHECK_EQ(status, 0);
}

}  // namespace

// cccores: one figure masked (D44: D33's UART cycle in errorPath's printed line)
TEST(cccores_equals_ref_log) { run_step("cccores", "suite_cccores", "-1", true); }
TEST(cccores_shuf_equals_ref_log) { run_step("cccores-shuf", "suite_cccores", "4", true); }
TEST(ccbench_equals_ref_log) { run_step("ccbench", "suite_ccbench", ""); }
TEST(hooks_equals_ref_log) { run_step("hooks", "suite_hooks", "-1"); }
TEST(hooks_shuf_equals_ref_log) { run_step("hooks-shuf", "suite_hooks", "4"); }
TEST(regdoc_equals_ref_log) { run_step("regdoc", "suite_regdoc", ""); }
// accounting: the log and results/phases-a2a.csv equal to ref's; the CSV's write-engine and GATHER peaks exist with the
// checker's builds only (phase_log.h), so in the debug build (the release build has no peaks to compare)
#include "check.h"
TEST(accounting_equals_ref_log_and_csv) {
    if (!sim::check::kOn) return;
    run_step("accounting", "suite_accounting", "-1");
    CHECK_EQ(std::system("cmp -s results/phases-a2a.csv tests/ctlsuites/phases-a2a.csv"), 0);
}
// s1bench: S1 is measured in the checker's builds only (src/units/s1.h)
TEST(s1bench_equals_ref_log) { if (sim::check::kOn) run_step("s1bench", "suite_s1bench", ""); }
// cond: the checker's messages are part of its log (the debug and suite builds)
TEST(cond_equals_ref_log) { if (sim::check::kOn) run_step("cond", "suite_cond", "-1"); }
TEST(cond_shuf_equals_ref_log) { if (sim::check::kOn) run_step("cond-shuf", "suite_cond", "4"); }
// ctrl, ctrl-shuf: the checker's builds only (registers' violation count, invariants' messages need its facts)
TEST(ctrl_equals_ref_log) { if (sim::check::kOn) run_step("ctrl", "suite_ctrl", "-1"); }
TEST(ctrl_shuf_equals_ref_log) { if (sim::check::kOn) run_step("ctrl-shuf", "suite_ctrl", "4"); }
TEST(recipes_equals_ref_log) { run_step("recipes", "suite_recipes", "-1"); }
TEST(recipes_shuf_equals_ref_log) { run_step("recipes-shuf", "suite_recipes", "4"); }
