// The type registry is per configuration (E1): ids in the order types are first added to that Config, whatever other
// threads build at the same time. The process-wide part is only each type's pure description.
#include <atomic>
#include <thread>
#include <vector>

#include "config.h"
#include "harness.h"

using namespace sim;

namespace {
struct TA { FifoOut out; explicit TA(Ports& p) { p.out(out, "out", 1); } Wake step(Ctx&) { return Wake::none(); } };
struct TB { FifoIn in; explicit TB(Ports& p) { p.in(in, "in", 1, 2); } Wake step(Ctx&) { return Wake::none(); } };
struct TC { explicit TC(Ports&) {} Wake step(Ctx&) { return Wake::none(); } };
struct TD { explicit TD(Ports&) {} Wake step(Ctx&) { return Wake::none(); } };

// Builds A, B, C (in `order`) plus D, returns the ids of A, B, C, D in that machine.
std::vector<uint16_t> ids(const char* order) {
    Config cfg;
    TA* a = nullptr; TB* b = nullptr;
    for (const char* p = order; *p; p++) {
        if (*p == 'A') a = &cfg.add<TA>("a");
        if (*p == 'B') b = &cfg.add<TB>("b");
        if (*p == 'C') cfg.add<TC>("c");
    }
    cfg.add<TD>("d");
    cfg.connect(a->out, b->in);
    Sim sim = cfg.build();
    return { sim.type_of("a"), sim.type_of("b"), sim.type_of("c"), sim.type_of("d") };
}
}  // namespace

TEST(type_ids_follow_each_configs_first_add_order) {
    CHECK(ids("ABC") == (std::vector<uint16_t>{ 0, 1, 2, 3 }));
    CHECK(ids("CAB") == (std::vector<uint16_t>{ 1, 2, 0, 3 }));    // C first in this machine: id 0, whatever came before
    CHECK(ids("BCA") == (std::vector<uint16_t>{ 2, 0, 1, 3 }));
}

TEST(two_configs_on_two_threads_get_the_same_ids) {
    // Each thread builds the same machine many times at once; the ids are the same every time, on both threads.
    std::atomic<int> bad{0};
    auto work = [&](const char* order, std::vector<uint16_t> expect) {
        for (int i = 0; i < 200; i++) if (ids(order) != expect) bad++;
    };
    std::thread t1(work, "ABC", std::vector<uint16_t>{ 0, 1, 2, 3 });
    std::thread t2(work, "ABC", std::vector<uint16_t>{ 0, 1, 2, 3 });
    std::thread t3(work, "CAB", std::vector<uint16_t>{ 1, 2, 0, 3 });
    t1.join(); t2.join(); t3.join();
    CHECK_EQ(bad.load(), 0);
}
