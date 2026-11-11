// test_tracer_suites.cpp (WORKER21; P7a): v1.3's Tracer suites in make check. Each suite program (suite_memsys,
// suite_rvc, suite_tracer, built beside this binary) runs as run_tests.sh runs a step, stdout and stderr together; its
// output must equal ref's log of the same step byte for byte (tests/tracersuites/<step>.log, from
// oracle/ref/tracersuites.sh), and it must exit 0. Run from sim/.
#include <unistd.h>

#include <cstdio>
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

void run_step(const char* program, const char* log) {
    const std::string cmd = self_dir() + "/" + program + " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    CHECK(p != nullptr);
    std::string out;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int status = pclose(p);
    std::ifstream f(std::string("tests/tracersuites/") + log, std::ios::binary);
    CHECK(f.good());
    std::stringstream ref;
    ref << f.rdbuf();
    if (out != ref.str()) {
        ::test::fail(__FILE__, __LINE__, "%s's output differs from ref's %s:\n--- C++\n%s--- ref\n%s", program, log,
                     out.c_str(), ref.str().c_str());
        return;
    }
    CHECK_EQ(status, 0);
}

}  // namespace

TEST(memsys_equals_ref_log) { run_step("suite_memsys", "memsys.log"); }
TEST(rvc_equals_ref_log) { run_step("suite_rvc", "rvc.log"); }
TEST(tracer_equals_ref_log) { run_step("suite_tracer", "tracer.log"); }
