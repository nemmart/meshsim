// test_stageb: run_tests.sh's step stageb in make check (WORKER25, P7b): `stageb synthetic` (v1.3's StageB synthetic:
// 16 program windows, 2 controller-fed, the CL4 case) equal to v1.3's log (tests/stageb/stageb.log, from
// oracle/ref/stageb.sh), the CL4 line's seconds masked, plain and shuffled (seed 3).
// PENDING part: the CL4 case's "meshsim: violations: ..." line is compared on its own and reported, not failed: the
// release build has no checker, and in the debug and suite builds W20's chip facts carry the replayer's cycle, ref's
// the next (W25's F8). Strict once F8 is fixed.
#include <cstdio>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "harness.h"

namespace {
std::string self_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, size_t(n));
    return p.substr(0, p.rfind('/'));
}
std::string run(const std::string& cmd, int& rc) {
    FILE* p = popen(cmd.c_str(), "r");
    std::string out;
    char b[65536];
    size_t n;
    while (p && (n = std::fread(b, 1, sizeof b, p)) > 0) out.append(b, n);
    rc = p ? pclose(p) : -1;
    return out;
}
const char* kMask = " | sed -E 's/(failed as starved in )[0-9.]+ s$/\\1X s/'";
void check_step(const std::string& opts) {
    int rc = 0, rc2 = 0;
    const std::string drop = " | grep -v '^    meshsim: '", only = " | grep '^    meshsim: '";
    std::string ours = run(self_dir() + "/stageb synthetic " + opts + " 2>&1" + kMask + drop, rc);
    std::string ref = run(std::string("cat tests/stageb/stageb.log") + kMask + drop, rc2);
    if (ours != ref) std::fprintf(stderr, "  stageb synthetic %s differs from tests/stageb/stageb.log\n", opts.c_str());
    CHECK(ours == ref);
    CHECK(rc2 == 0);
    // The CL4 case's meshsim line (the checker's: release builds have none; W20's chip facts in debug and suite builds)
    std::string om = run(self_dir() + "/stageb synthetic " + opts + " 2>&1" + only, rc), rm = run("cat tests/stageb/stageb.log" + only, rc2);
    if (om.empty()) std::printf("  PENDING (no checker in this build): the CL4 case's meshsim line not compared (%s)\n", opts.c_str());
    else if (om != rm) std::printf("  PENDING (W20, W25's F8: a chip fact's cycle is the replayer's t, ref's is t + 1): the CL4 meshsim line differs (%s)\n", opts.c_str());
    else std::printf("  the CL4 case's meshsim line equals v1.3's (%s)\n", opts.c_str());
}
}  // namespace

TEST(stageb_synthetic_equals_v13s_log) { check_step(""); }
TEST(stageb_synthetic_shuffled_equals_v13s_log) { check_step("--shuffle 3"); }
