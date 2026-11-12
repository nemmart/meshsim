// test_suite_replayer: run_tests.sh's step replayer in make check (WORKER25, P7b): suite_replayer (v1.3's ReplayerTest)
// run as the runner runs it (stdout and stderr), equal to v1.3's log (tests/replayer/replayer.log, from
// oracle/ref/replayertest.sh), exit 0.
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
}  // namespace

TEST(replayer_step_equals_v13s_log) {
    int rc = 0, rc2 = 0;
    std::string ours = run(self_dir() + "/suite_replayer 2>&1", rc);
    std::string ref = run("cat tests/replayer/replayer.log", rc2);
    CHECK_EQ(rc2, 0);
    if (ours != ref) std::fprintf(stderr, "  suite_replayer's output differs from tests/replayer/replayer.log; ours ends: %.200s\n",
                                  ours.substr(ours.size() > 200 ? ours.size() - 200 : 0).c_str());
    CHECK(ours == ref);
    CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0);
}
