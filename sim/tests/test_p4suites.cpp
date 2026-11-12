// test_p4suites.cpp (WORKER22; P4): v1.3's network, memory and engine suites in make check. Each suite program (built
// beside this binary) runs as run_tests.sh runs a quick step, stdout and stderr together; its output must equal ref's
// log of the same step byte for byte (tests/p4suites/<step>.log, from oracle/ref/p4suites.sh), and it must exit 0.
// The suites read statistics kept only under SIM_CHECKS (the Fifo peaks, big SRAM's and the read queue's): in the
// timing builds (release) the comparison is skipped, and runs in the debug builds (and run_tests.sh's suite build).
// Run from sim/.
#include <unistd.h>

#include <cstdio>
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

void run_step(const char* command, const char* log) {
    if (!sim::check::kOn) { std::printf("     (%s: skipped, the suites need SIM_CHECKS)\n", log); return; }
    const std::string cmd = self_dir() + "/" + command + " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    CHECK(p != nullptr);
    std::string out;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int status = pclose(p);
    std::ifstream f(std::string("tests/p4suites/") + log, std::ios::binary);
    CHECK(f.good());
    std::stringstream ref;
    ref << f.rdbuf();
    if (out != ref.str()) {
        ::test::fail(__FILE__, __LINE__, "%s's output differs from ref's %s:\n--- C++\n%s--- ref\n%s", command, log,
                     out.c_str(), ref.str().c_str());
        return;
    }
    CHECK_EQ(status, 0);
}

}  // namespace

TEST(net_quick_equals_ref_log) { run_step("suite_net -1 quick", "net-quick.log"); }
TEST(net_shuf_equals_ref_log) { run_step("suite_net 5 quick", "net-shuf.log"); }
TEST(net_shuf2_equals_ref_log) { run_step("suite_net 11 quick", "net-shuf2.log"); }
TEST(searchbench_equals_ref_log) { run_step("suite_search", "searchbench.log"); }
TEST(engine_equals_ref_log) { run_step("suite_engine -1", "engine.log"); }
TEST(eng_shuf_equals_ref_log) { run_step("suite_engine 4", "eng-shuf.log"); }
TEST(reqbus_equals_ref_log) { run_step("suite_reqbus -1", "reqbus.log"); }
TEST(reqbus_shuf_equals_ref_log) { run_step("suite_reqbus 4", "reqbus-shuf.log"); }
// the debug options (p4_debug.h): the same log under step-all and the instance-major order (the rest: p4_debug_checks.sh)
TEST(net_quick_step_all_equals_ref_log) { run_step("suite_net -1 quick --step-all", "net-quick.log"); }
TEST(engine_instance_major_equals_ref_log) { run_step("suite_engine -1 --instance-major", "engine.log"); }
