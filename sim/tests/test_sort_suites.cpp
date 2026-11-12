// test_sort_suites.cpp (WORKER24, P6): the sort's quick checks in make check. sort_test (built beside this binary) runs
// as run_tests.sh runs a step, from sim/:
//   - sort-p1: its output equal to ref's log of the step (tests/sort/ref/sort-p1.log) byte for byte, the inputs line's
//     first field (the simulator's own hash: v1.3's meshsim.jar, owner's Q2) aside, and both p1 runs' state equal to
//     SortFp's at every point it took (tests/sort/fp, SORT_FP);
//   - two runs of the `sort` step, the direct path (uniform, k128) and TH with stage 2 (reverse, kv32): every point of
//     their SortFp references equal, and every check passing.
// The `sort` step's whole log waits for W20's PhaseLog (its peak lines); run_tests.sh has the long steps.
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
// the program's stdout and, apart, its stderr; its exit status
int run(const std::string& args, std::string& out, std::string& err) {
    char tmpl[] = "/tmp/test_sort_suites_XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd >= 0) close(fd);
    const std::string cmd = "SORT_FP=tests/sort/fp " + self_dir() + "/sort_test " + args + " 2>" + tmpl;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    const int status = pclose(p);
    std::ifstream f(tmpl, std::ios::binary);
    std::stringstream e; e << f.rdbuf(); err = e.str();
    std::remove(tmpl);
    return status;
}
std::string mask_first_field(const std::string& log) {         // "inputs: <simulator> ..." (run_tests.sh's mask)
    if (log.rfind("inputs: ", 0) != 0) return log;
    const size_t a = 8, b = log.find(' ', a);
    return log.substr(0, a) + "<simulator>" + log.substr(b);
}
size_t count(const std::string& s, const std::string& x) { size_t n = 0; for (size_t p = s.find(x); p != std::string::npos; p = s.find(x, p + 1)) n++; return n; }

}  // namespace

TEST(sort_p1_equals_refs_log_and_fingerprints) {
    std::string out, err;
    const int st = run("-1 p1", out, err);
    std::ifstream f("tests/sort/ref/sort-p1.log", std::ios::binary);
    CHECK(f.good());
    std::stringstream ref; ref << f.rdbuf();
    if (mask_first_field(out) != mask_first_field(ref.str())) {
        ::test::fail(__FILE__, __LINE__, "sort-p1's output differs from ref's:\n--- C++\n%s--- ref\n%s", out.c_str(), ref.str().c_str());
        return;
    }
    CHECK_EQ(st, 0);
    CHECK_EQ(count(err, "fp: all 8 reference points equal"), size_t(2));   // p1-uniform-k128, p1-uniform-kv32
    CHECK_EQ(count(err, "DIFFERS"), size_t(0));
}

TEST(sort_direct_and_th_paths_equal_refs_at_every_point) {
    for (const char* only : {"uniform-k128", "reverse-kv32"}) {
        std::string out, err;
        const int st = run(std::string("-1 sort only=") + only, out, err);
        CHECK_EQ(st, 0);
        const bool ok = err.find("fp: all ") != std::string::npos && count(err, "DIFFERS") == 0 && out.find(" 0 failed") != std::string::npos;
        if (!ok) { ::test::fail(__FILE__, __LINE__, "sort only=%s:\n%s%s", only, out.c_str(), err.c_str()); return; }
    }
}
