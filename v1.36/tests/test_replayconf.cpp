// test_replayconf: the replayer against the RTL every cycle. Each fixture in tests/replayer/ is a replayconf script
// (tests/replayconf.h: the ideal controller's inputs on one of the unit tests' windows, some with a mutation for an
// error path; `replayconf --gen NAME` makes it) and the RTL's observations of it, recorded from Snarky One's
// `replayer_unit` under Verilator (tests/replayer/README.md). The model, given the same inputs, must observe the same
// in every cycle, character for character: STATUS.busy, ERRORS, EVENTS, the words 32-35 and every row leaving the row
// port.
#include <cstdio>
#include <memory>
#include <string>

#include "harness.h"
#include "portconf.h"
#include "replayconf.h"

namespace {

const std::string kDir = "tests/replayer/";
const char* kTraces[] = {
    "program-raw", "program-lru", "program-no-store-log", "program-twice", "program-short", "rows-0", "rows-0-empty", "padding-lru",
    "core-fed", "core-fed-offset", "core-fed-cut", "core-fed-nop-1", "core-fed-nop-3", "core-fed-nop-offset", "core-fed-remap",
    "pext", "not-the-last-row", "branch-not-taken", "transfer-then-advice-2", "transfer-then-advice-3",
    "under-code-values", "under-code-words", "under-code-lru", "under-rlog-values", "under-rlog-words", "under-wlog-values",
    "under-transfer", "under-then-pass", "left-code-value", "left-rlog-value", "left-wlog-value", "left-code-word", "left-rlog-entry",
    "invalid-class-3", "invalid-operand", "invalid-opcode", "invalid-rom-511", "overrun"};

// The first differing line, for the report.
void first_difference(const std::string& a, const std::string& b) {
    size_t p = 0, line = 1;
    while (p < a.size() && p < b.size() && a[p] == b[p]) { if (a[p] == '\n') line++; p++; }
    size_t s = a.rfind('\n', p), sa = s == std::string::npos ? 0 : s + 1;
    std::fprintf(stderr, "  line %zu\n    the RTL: %.300s\n    ours:    %.300s\n", line, a.substr(sa, a.find('\n', sa) - sa).c_str(),
                 b.substr(sa, b.find('\n', sa) - sa).c_str());
}

bool read_fixture(const char* name, std::string& script, std::string& ref) {
    bool ok1 = false, ok2 = false;
    script = portconf::read_command("gzip -dc " + kDir + name + ".script.gz", &ok1);
    ref = portconf::read_command("gzip -dc " + kDir + name + ".obs.gz", &ok2);
    if (!ok1 || !ok2) std::fprintf(stderr, "  %s: cannot read the fixture\n", name);
    return ok1 && ok2;
}

bool check_trace(const char* name) {
    std::string script, ref;
    if (!read_fixture(name, script, ref)) return false;
    auto r = std::make_unique<units::Replayer>(0);
    std::string ours = replayconf::run(script, 1u << 16, r.get());
    if (ours == ref) return true;
    std::fprintf(stderr, "  %s differs from the RTL\n", name);
    first_difference(ref, ours);
    return false;
}

// A fixture's last value of a field, and its rows: what the RTL observed.
uint64_t last_of(const std::string& obs, const std::string& field) {
    size_t p = obs.rfind(" " + field + "=");
    return p == std::string::npos ? 0 : std::strtoull(obs.c_str() + p + field.size() + 2, nullptr, 16);
}
size_t rows_of(const std::string& obs) {
    size_t n = 0;
    for (size_t p = obs.find(" row="); p != std::string::npos; p = obs.find(" row=", p + 1)) n++;
    return n;
}

}  // namespace

TEST(every_trace_equals_the_rtl_every_cycle) {
    int bad = 0;
    for (const char* n : kTraces) bad += !check_trace(n);
    CHECK_EQ(bad, 0);
}

// The fixtures hold what their scenarios are for: the RTL's own ERRORS and row counts, against 17.8.1 and 17.8.10's
// rules for each mutation (the rows before an error stop are pushed, the stop's row is not).
TEST(the_fixtures_errors_and_rows_are_the_specifications) {
    struct Want { const char* name; uint64_t errors; size_t rows; };
    const Want want[] = {
        {"program-raw", 0, 25}, {"program-lru", 0, 25}, {"program-no-store-log", 0, 25}, {"program-twice", 0, 50},
        {"program-short", 1 << 5, 10}, {"rows-0", 1 << 5, 0}, {"rows-0-empty", 0, 0}, {"padding-lru", 0, 3000},
        {"core-fed", 0, 600}, {"core-fed-offset", 0, 350}, {"core-fed-cut", 0, 10}, {"core-fed-nop-1", 0, 16}, {"core-fed-nop-3", 0, 16},
        {"core-fed-nop-offset", 0, 16}, {"core-fed-remap", 0, 8}, {"pext", 0, 3}, {"not-the-last-row", 0, 6},
        {"branch-not-taken", 1 << 5, 3}, {"transfer-then-advice-2", 0, 2}, {"transfer-then-advice-3", 0, 3},
        {"under-code-values", 1 << 2, 3}, {"under-code-words", 1 << 2, 4}, {"under-code-lru", 1 << 2, 40},
        {"under-rlog-values", 1 << 3, 1}, {"under-rlog-words", 1 << 3, 2}, {"under-wlog-values", 1 << 4, 1}, {"under-transfer", 1 << 3, 1},
        {"under-then-pass", 1 << 3, 1 + 3},
        {"left-code-value", 1 << 5, 25}, {"left-rlog-value", 1 << 5, 25}, {"left-wlog-value", 1 << 5, 25}, {"left-code-word", 1 << 5, 25},
        {"left-rlog-entry", 1 << 5, 25},
        {"invalid-class-3", 1 << 1, 0}, {"invalid-operand", 1 << 1, 2}, {"invalid-opcode", 1 << 1, 2}, {"invalid-rom-511", 1 << 1, 1},
        {"overrun", 1 << 0, 25}};
    CHECK_EQ(sizeof want / sizeof want[0], sizeof kTraces / sizeof kTraces[0]);
    int bad = 0;
    for (const Want& w : want) {
        std::string script, ref;
        if (!read_fixture(w.name, script, ref)) { bad++; continue; }
        if (last_of(ref, "err") != w.errors || rows_of(ref) != w.rows) {
            bad++;
            std::fprintf(stderr, "  %s: the RTL's ERRORS 0x%llx and %zu rows, expected 0x%llx and %zu\n", w.name, (unsigned long long)last_of(ref, "err"),
                         rows_of(ref), (unsigned long long)w.errors, w.rows);
        }
    }
    CHECK_EQ(bad, 0);
}
