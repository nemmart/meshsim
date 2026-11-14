// test_replay_lists: the replayer's error lists, the simulator's account of each error (units::Replayer::list_*).
// Every error-path fixture of tests/replayer/ (test_replayconf's scripts) replayed on the model: the list its
// mutation belongs to has exactly the entries the mutation makes, and each names its cause; the others are empty.
// v1.36: v1.35's reference (lists.txt, ref's lists of ref's scripts) went with ref's model; the expectations here are
// each scenario's own (tests/replayconf_main.cpp), and the RTL's ERRORS for them are test_replayconf's.
// Also the kept-entries limit: a replayer keeping 1 entry per list counts all of them and keeps the first.
#include <cstdio>
#include <memory>
#include <string>

#include "harness.h"
#include "portconf.h"
#include "replay_report.h"
#include "replayconf.h"

namespace {

const std::string kDir = "tests/replayer/";
using R = units::Replayer;

std::unique_ptr<R> replay(const char* name, unsigned capacity) {
    bool ok = false;
    std::string script = portconf::read_command("gzip -dc " + kDir + name + ".script.gz", &ok);
    if (!ok) FATAL("test_replay_lists: cannot read %s%s.script.gz", kDir.c_str(), name);
    auto r = std::make_unique<R>(0, R::LOG_QUEUE_ENTRIES, capacity);
    replayconf::run(script, 1u << 16, r.get());
    return r;
}

}  // namespace

TEST(every_list_names_its_error) {
    struct Want { const char* name; unsigned list; uint64_t count; const char* text; };
    const Want want[] = {
        {"under-code-values", R::L_UNDERFLOWS, 1, "an instruction past the code stream's 3 values"},
        {"under-code-words", R::L_UNDERFLOWS, 1, "the code stream's next value runs past its 1 words"},
        {"under-rlog-values", R::L_UNDERFLOWS, 1, "a pop past the read log's 1 values"},
        {"under-rlog-words", R::L_UNDERFLOWS, 1, "the read log's next value runs past its 1 words"},
        {"under-wlog-values", R::L_UNDERFLOWS, 1, "a pop past the write log's 1 values"},
        {"under-transfer", R::L_UNDERFLOWS, 1, "a pop past the read log's 1 values (a control transfer's next instr_idx)"},
        {"program-short", R::L_LEFTOVER, 1, "code: "},
        {"left-code-value", R::L_LEFTOVER, 1, "code: 1 of 22 values not popped"},
        {"left-rlog-value", R::L_LEFTOVER, 1, "read log: 1 of 5 values not popped"},
        {"left-wlog-value", R::L_LEFTOVER, 1, "write log: 1 of 2 values not popped"},
        {"left-code-word", R::L_LEFTOVER, 1, "code: 1 of 7 words not taken"},
        {"left-rlog-entry", R::L_LEFTOVER, 1, "read log: 1 entries pushed past its 2 words"},
        {"branch-not-taken", R::L_LEFTOVER, 1, "read log: 1 of 1 values not popped"},
        {"invalid-class-3", R::L_INVALID, 1, "decode-table class 3"},
        {"invalid-operand", R::L_INVALID, 1, "a reserved operand code"},
        {"invalid-opcode", R::L_INVALID, 1, "opcode 163"},
        {"invalid-rom-511", R::L_INVALID, 1, "a template runs past ROM word 511 (start 511, row 1)"},
        {"overrun", R::L_OVERRUNS, 2, "queue 1"},
    };
    int bad = 0;
    for (const Want& w : want) {
        auto r = replay(w.name, 4096);
        for (unsigned l = 0; l < R::N_LISTS; l++) {
            const uint64_t n = r->list_count(l), expect = l == w.list ? w.count : 0;
            if (n != expect) { bad++; std::fprintf(stderr, "  %s: list %u has %llu entries, expected %llu: %s\n", w.name, l, (unsigned long long)n, (unsigned long long)expect, rreport::list_string(*r, l).c_str()); }
        }
        if (r->list_count(w.list) > 0 && std::string(r->list_entry(w.list, 0)).find(w.text) == std::string::npos) {
            bad++;
            std::fprintf(stderr, "  %s: its entry doesn't name '%s': %s\n", w.name, w.text, r->list_entry(w.list, 0));
        }
    }
    for (const char* clean : {"program-raw", "program-lru", "core-fed", "core-fed-nop-offset", "pext", "not-the-last-row", "padding-lru"}) {
        auto r = replay(clean, 4096);
        if (!r->clean()) { bad++; std::fprintf(stderr, "  %s is not clean: %s\n", clean, rreport::error_report(*r).c_str()); }
    }
    CHECK_EQ(bad, 0);
}

TEST(kept_entries_and_counts) {
    auto big = replay("overrun", 4096), small = replay("overrun", 1);
    CHECK_EQ(big->list_count(R::L_OVERRUNS), uint64_t(2));
    CHECK_EQ(small->list_count(R::L_OVERRUNS), uint64_t(2));
    CHECK(std::string(small->list_entry(R::L_OVERRUNS, 0)) == big->list_entry(R::L_OVERRUNS, 0));
    CHECK(rreport::list_string(*small, R::L_OVERRUNS).find("(2 in all, 1 kept)") != std::string::npos);
    CHECK_EQ(small->cond_events(R::ERR_OVERRUN), uint64_t(2));
}
