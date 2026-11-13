// test_replay_lists: the replayer's error lists against ref's. Every committed ReplayConf script
// (tests/replayer/*.script.gz: clean windows and the error-path mutations) replayed on the C++ replayer as
// test_replayconf replays it; every list (ref's Replayer.violations, invalids, overruns, underflows, leftovers,
// badWrites), its size and every entry's text, equal to ref's (tests/replayer/lists.txt, oracle/ref/replaylists.sh).
// Also the kept-entries limit: a replayer keeping 2 entries per list counts all of them and keeps ref's first two.
#include <cstdio>
#include <memory>
#include <string>

#include "harness.h"
#include "portconf.h"
#include "replay_report.h"
#include "replayconf.h"

namespace {

const std::string kDir = "tests/replayer/";
// The scripts in the shell glob's order (replaylists.sh: tests/replayer/*.script.gz).
const char* kScripts[] = {"m-bad-decode", "m-extra-code", "m-flood", "m-left-rlog", "m-left-wlog", "m-passes", "m-rows0",
                          "m-under-code", "m-under-rlog", "m-under-wlog", "s1-lru-w0", "s2-pick-w0", "s4-pick-w1"};

std::string lists(const units::Replayer& r) {
    using R = units::Replayer;
    const char* names[] = {"violations", "invalid", "overruns", "underflows", "leftover"};
    const unsigned ids[] = {R::L_VIOLATIONS, R::L_INVALID, R::L_OVERRUNS, R::L_UNDERFLOWS, R::L_LEFTOVER};
    std::string s;
    for (unsigned k = 0; k < 5; k++) s += std::string(names[k]) + " " + std::to_string(r.list_count(ids[k])) + " " + rreport::list_string(r, ids[k]) + "\n";
    s += "bad writes 0 []\n";
    s += std::string("clean ") + (r.clean() ? "true" : "false") + "\n";
    return s;
}

}  // namespace

TEST(every_list_equals_refs) {
    bool ok = false;
    std::string ref = portconf::read_command("cat " + kDir + "lists.txt", &ok);
    CHECK(ok);
    std::string ours;
    for (const char* n : kScripts) {
        bool ok1 = false;
        std::string script = portconf::read_command("gzip -dc " + kDir + n + ".script.gz", &ok1);
        CHECK(ok1);
        auto r = std::make_unique<units::Replayer>(0, units::Replayer::LOG_QUEUE_ENTRIES, 4096);
        replayconf::run(script, 1u << 16, r.get());
        ours += std::string(n) + "\n" + lists(*r);
    }
    if (ours != ref) {
        size_t p = 0;
        while (p < ours.size() && p < ref.size() && ours[p] == ref[p]) p++;
        size_t s = ref.rfind('\n', p);
        s = s == std::string::npos ? 0 : s + 1;
        std::fprintf(stderr, "  ref:  %.300s\n  ours: %.300s\n", ref.substr(s).c_str(), ours.substr(s).c_str());
    }
    CHECK(ours == ref);
}

TEST(kept_entries_and_counts) {
    bool ok1 = false;
    std::string script = portconf::read_command("gzip -dc " + kDir + "m-extra-code.script.gz", &ok1);
    CHECK(ok1);
    auto big = std::make_unique<units::Replayer>(0, units::Replayer::LOG_QUEUE_ENTRIES, 4096);
    auto small = std::make_unique<units::Replayer>(0, units::Replayer::LOG_QUEUE_ENTRIES, 2);
    replayconf::run(script, 1u << 16, big.get());
    replayconf::run(script, 1u << 16, small.get());
    using R = units::Replayer;
    CHECK_EQ(small->list_count(R::L_VIOLATIONS), big->list_count(R::L_VIOLATIONS));
    CHECK_EQ(small->list_count(R::L_OVERRUNS), uint64_t(40));
    CHECK(std::string(small->list_entry(R::L_VIOLATIONS, 1)) == big->list_entry(R::L_VIOLATIONS, 1));
    CHECK(rreport::head(*small, R::L_OVERRUNS).find("... (40 in all)") != std::string::npos);
}
