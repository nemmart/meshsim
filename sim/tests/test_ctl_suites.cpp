// test_ctl_suites.cpp (WORKER23; P5): v1.3's control suites in make check. Each suite program (suite_*, built beside
// this binary) runs as run_tests.sh runs its step (v1.3's arguments; stdout and stderr together); its output must equal
// ref's log of the same step byte for byte (tests/ctlsuites/<step>.log, from oracle/ref/ctlsuites.sh), and it must exit
// 0. Run from sim/.
#include <unistd.h>

#include <cstdio>
#include <regex>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
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
std::string mask_d44(const std::string& s) { return std::regex_replace(s, std::regex("dump done at \\+[0-9]+"), "dump done at +<D44>"); }

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

// cccores: one figure masked (D44). errorPath runs on two stand-ins for W20's requests 9 and 10 (tests/ctl/ctl.h):
// equal in the release builds; with the checker on (debug, suite) the injected ERRORS count as a violation until W20's
// operations replace the stand-ins, so the comparison runs only without the checker for now.
constexpr bool kCheckerOn = sim::check::kOn;
TEST(cccores_equals_ref_log) { if (!kCheckerOn) run_step("cccores", "suite_cccores", "-1", true); }
TEST(cccores_shuf_equals_ref_log) { if (!kCheckerOn) run_step("cccores-shuf", "suite_cccores", "4", true); }
TEST(ccbench_equals_ref_log) { run_step("ccbench", "suite_ccbench", ""); }
