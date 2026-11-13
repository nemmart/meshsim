// test_slotconf: the peripheral slot against ref call for call. Each fixture in tests/slotconf/ is a SlotConf
// script (ref's PeripheralSlot on two tiles of a Chip built as PeripheralSlotTest builds it, a test peripheral on one and
// the replayer on the other, driven by a seeded script) and ref's observations of it; units::SlotCore, given the same
// calls, must observe the same, character for character (tests/slotconf.h). oracle/ref/slotconf.sh --fixtures | --check.
#include <cstdio>
#include <string>

#include "harness.h"
#include "portconf.h"
#include "slotconf.h"

TEST(every_slot_script_equals_ref_call_for_call) {
    int bad = 0;
    for (int s = 1; s <= 5; s++) {
        bool ok1 = false, ok2 = false;
        const std::string base = "tests/slotconf/seed" + std::to_string(s);
        std::string script = portconf::read_command("gzip -dc " + base + ".script.gz", &ok1);
        std::string ref = portconf::read_command("gzip -dc " + base + ".obs.gz", &ok2);
        CHECK(ok1 && ok2);
        std::string ours = slotconf::run(script);
        if (ours == ref) continue;
        bad++;
        size_t p = 0, line = 1;
        while (p < ref.size() && p < ours.size() && ref[p] == ours[p]) { if (ref[p] == '\n') line++; p++; }
        std::fprintf(stderr, "  seed %d differs from ref at line %zu\n", s, line);
    }
    CHECK_EQ(bad, 0);
}
