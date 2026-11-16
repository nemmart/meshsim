// TypeArena, alloc_big and Block: unit tests, a seeded differential test against std::vector, death tests.
#include <cstdint>
#include <vector>

#include "base/arena.h"
#include "harness.h"

using sim::TypeArena;

namespace {
int g_live = 0;  // objects constructed and not yet destroyed
struct Unit {
    uint64_t a, b;
    uint32_t id;
    Unit(uint64_t a_, uint64_t b_, uint32_t id_) : a(a_), b(b_), id(id_) { ++g_live; }
    ~Unit() { --g_live; }
};
}  // namespace

TEST(arena_objects_contiguous_in_order) {
    {
        TypeArena<Unit> ar(10, "units");
        for (uint32_t i = 0; i < 10; ++i) {
            Unit* u = ar.make(i, i * 2, i);
            CHECK(u == ar.data() + i);
        }
        CHECK_EQ(g_live, 10);
        CHECK_EQ(ar.size(), 10u);
        CHECK_EQ(ar[7].b, 14u);
        CHECK((reinterpret_cast<uintptr_t>(ar.data()) & 63) == 0);
    }
    CHECK_EQ(g_live, 0);  // destructors ran
}

TEST(alloc_big_aligned_and_zeroed) {
    for (size_t bytes : {size_t(1), size_t(64), size_t(1000), size_t(2) << 20, (size_t(5) << 20) + 3}) {
        auto* p = static_cast<unsigned char*>(sim::alloc_big(bytes, "big"));
        const uintptr_t align = bytes >= (size_t(2) << 20) ? (uintptr_t(2) << 20) : 64;
        CHECK((reinterpret_cast<uintptr_t>(p) & (align - 1)) == 0);
        for (size_t i = 0; i < bytes; i += 4093) CHECK_EQ(p[i], 0u);
        CHECK_EQ(p[bytes - 1], 0u);
        sim::free_big(p);
    }
}

TEST(block_is_line_aligned) {
    sim::Block b(3, "b");
    CHECK(sim::line_aligned(b.words()));
    b.words()[2] = 5;
    CHECK_EQ(b.words()[2], 5u);
}

namespace {
void arena_differential(uint64_t seed) {
    test::Rng rng(seed);
    for (size_t cap : {size_t(1), size_t(1000), size_t(100000)}) {
        TypeArena<Unit> ar(cap, "units");
        std::vector<Unit> model;
        model.reserve(cap);
        const int ops = cap == 1 ? 1000 : 300000;
        for (int op = 0; op < ops; ++op) {
            if (ar.size() < cap && rng.below(3) == 0) {
                const uint64_t a = rng.next(), b = rng.next();
                ar.make(a, b, uint32_t(ar.size()));
                model.emplace_back(a, b, uint32_t(model.size()));
            } else if (ar.size() > 0) {
                const size_t i = size_t(rng.below(ar.size()));
                CHECK(ar[i].a == model[i].a && ar[i].b == model[i].b && ar[i].id == model[i].id);
                ar[i].a ^= 1;  // permanent state is mutable
                model[i].a ^= 1;
            }
        }
        CHECK_EQ(ar.size(), model.size());
    }
}
}  // namespace

TEST(arena_differential_seed_1) { arena_differential(1); }
TEST(arena_differential_seed_2) { arena_differential(2); }
TEST(arena_differential_seed_3) { arena_differential(3); }

DEATH_TEST(arena_full_is_fatal, "arena 'cores' full: capacity 2") {
    TypeArena<Unit> ar(2, "cores");
    for (uint32_t i = 0; i < 3; ++i) ar.make(i, i, i);
}
DEBUG_DEATH_TEST(arena_index_past_size, "arena 'cores': index 1 with size 1") {
    TypeArena<Unit> ar(4, "cores");
    ar.make(1u, 2u, 3u);
    (void)ar[1];
}
DEATH_TEST(alloc_big_failure_is_fatal, "alloc_big 'huge': [0-9]+ bytes failed") {
    (void)sim::alloc_big(~size_t(0) >> 2, "huge");
}
