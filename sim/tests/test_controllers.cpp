// M1's threads test (PLAN-M1, W5): N controllers on N threads (W4's run_independent), each running a different golden,
// every trace equal to its golden and every digest to the same golden's run alone, under all six debug-check option
// sets (ready-driven, shuffle seeds 1-3, instance-major, step-all). Here N = 3, every golden in 23 rounds;
// the runner runs N up to 12, pinned (Quest). Also run under TSan (make tsan-units). Run from sim/.
#include <string>

#include "golden_run.h"
#include "harness.h"

using namespace test;

TEST(controllers_three_threads_every_golden) {
    std::string d = check_parallel(mixed_order(read_runs()), 3);
    if (!d.empty()) fprintf(stderr, "%s", d.c_str());
    CHECK(d.empty());
}

TEST(controllers_mixed_order_visits_every_run_once) {
    auto runs = read_runs();
    auto m = mixed_order(runs);
    CHECK_EQ(m.size(), runs.size());
    for (const RunSpec& r : runs) {
        unsigned seen = 0;
        for (const RunSpec& x : m) seen += x.name == r.name;
        CHECK_EQ(seen, 1u);
    }
    CHECK(m[1].name != runs[1].name);
}
