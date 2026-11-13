// test_shard_summary: the acceptance kit's summary step (tests/acceptance/shard_summary.cpp) against v1.3's
// P7b). Each case in tests/acceptance/summary/ (a results.csv and run_shard.sh's arguments: OK lines of stages A, B and
// F with stall counts, a B/F cycle disagreement, FAIL, CORRUPT, tables and malformed lines, a blank line, a range, "all"
// from the manifest, -x adding stage F, an empty file, a passing shard) is summarised by shard_summary in a scratch
// directory; SUMMARY.txt, VERDICT.txt and the exit code must equal what v1.3's own Python wrote
// (oracle/ref/shardsummary.sh: <case>/expect/).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "harness.h"
#include "portconf.h"

namespace {

// The directory of this binary: shard_summary is built beside it (as test_tracer_suites finds its suites).
std::string self_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, size_t(n));
    return p.substr(0, p.rfind('/'));
}

const char* kCases[] = {"c1", "c2", "c3", "c4", "c5", "c6"};

std::string slurp(const std::string& path) {
    bool ok = false;
    std::string s = portconf::read_command("cat " + path, &ok);
    return ok ? s : "(unreadable " + path + ")";
}

}  // namespace

TEST(every_case_equals_v13s_summary) {
    const std::string bin = self_dir() + "/shard_summary", dir = "tests/acceptance/summary/";
    char tmpl[] = "/tmp/test_shard_summary.XXXXXX";
    const char* tmp = mkdtemp(tmpl);
    CHECK(tmp != nullptr);
    int bad = 0;
    for (const char* c : kCases) {
        std::string args = slurp(dir + c + "/args");
        while (!args.empty() && (args.back() == '\n' || args.back() == ' ')) args.pop_back();
        std::string out = std::string(tmp) + "/" + c;
        std::string cmd = "mkdir -p " + out + " && cp " + dir + c + "/results.csv " + out + "/ && " + bin + " " + out + " " + args + " " + dir + "manifest.json";
        int rc = std::system(cmd.c_str());
        int code = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
        std::string want_exit = slurp(dir + c + "/expect/EXIT");
        bool same = slurp(out + "/SUMMARY.txt") == slurp(dir + c + "/expect/SUMMARY.txt") && slurp(out + "/VERDICT.txt") == slurp(dir + c + "/expect/VERDICT.txt") &&
                    std::to_string(code) + "\n" == want_exit;
        if (!same) { std::fprintf(stderr, "  case %s differs from v1.3's summary (exit %d, v1.3's %s)\n", c, code, want_exit.c_str()); bad++; }
    }
    if (std::system((std::string("rm -rf ") + tmp).c_str()) != 0) bad++;
    CHECK_EQ(bad, 0);
}
