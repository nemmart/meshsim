// test_replayconf: the replayer against ref every cycle. Each fixture in tests/replayer/ is a ReplayConf script
// (ReplayDriver's ideal controller on a synthetic delivery's window, every call to the replayer recorded; some with
// mutations for the error paths) and ref's observations of it; the C++ replayer, given the same calls, must observe the
// same, character for character (tests/replayconf.h). oracle/ref/replayconf.sh --fixtures | --check (needs a JDK).
#include <cstdio>
#include <memory>
#include <string>

#include "harness.h"
#include "portconf.h"
#include "replayconf.h"

namespace {

const std::string kDir = "tests/replayer/";
const char* kTraces[] = {"s1-lru-w0", "s4-pick-w1", "s2-pick-w0", "m-extra-code", "m-flood", "m-passes", "m-under-rlog",
                         "m-left-rlog", "m-under-wlog", "m-left-wlog", "m-under-code", "m-bad-decode", "m-rows0"};

// The first differing line, for the report.
void first_difference(const std::string& a, const std::string& b) {
    size_t p = 0, line = 1;
    while (p < a.size() && p < b.size() && a[p] == b[p]) { if (a[p] == '\n') line++; p++; }
    size_t s = a.rfind('\n', p), sa = s == std::string::npos ? 0 : s + 1;
    std::fprintf(stderr, "  line %zu\n    ref:  %.300s\n    ours: %.300s\n", line, a.substr(sa, a.find('\n', sa) - sa).c_str(),
                 b.substr(sa, b.find('\n', sa) - sa).c_str());
}

bool check_trace(const char* name) {
    bool ok1 = false, ok2 = false;
    std::string script = portconf::read_command("gzip -dc " + kDir + name + ".script.gz", &ok1);
    std::string ref = portconf::read_command("gzip -dc " + kDir + name + ".obs.gz", &ok2);
    if (!ok1 || !ok2) { std::fprintf(stderr, "  %s: cannot read the fixture\n", name); return false; }
    auto r = std::make_unique<units::Replayer>(0);
    std::string ours = replayconf::run(script, 1u << 16, r.get());
    if (ours == ref) return true;
    std::fprintf(stderr, "  %s differs from ref\n", name);
    first_difference(ref, ours);
    return false;
}

}  // namespace

TEST(every_trace_equals_ref_every_cycle) {
    int bad = 0;
    for (const char* n : kTraces) bad += !check_trace(n);
    CHECK_EQ(bad, 0);
}
