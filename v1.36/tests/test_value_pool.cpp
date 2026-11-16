// ValuePool: unit tests, the seeded differential test against std::set and std::map, death tests.
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "base/alloc_guard.h"
#include "base/arena.h"
#include "base/value_pool.h"
#include "harness.h"

using sim::Block;
using sim::u128;
using sim::ValuePool;

namespace {
u128 v128(uint64_t hi, uint64_t lo) { return (u128(hi) << 64) | lo; }
}

TEST(pool_words_needed_rounds_to_lines) {
    CHECK_EQ(ValuePool::words_needed(1), 16u);    // 2 words of values -> 8, the bitmap -> 8
    CHECK_EQ(ValuePool::words_needed(4), 16u);
    CHECK_EQ(ValuePool::words_needed(5), 24u);    // 10 words of values -> 16, + 8
    CHECK_EQ(ValuePool::words_needed(600), 1216u);  // 1200 -> 1200, 600 bits -> 10 words -> 16
}

TEST(pool_lowest_free_first_and_values_kept) {
    Block b(ValuePool::words_needed(130), "t");
    ValuePool p;
    p.init(b.words(), 130, "vals");
    for (uint32_t i = 0; i < 130; ++i) {
        CHECK_EQ(p.alloc(), i);
        p[i] = v128(i, ~uint64_t(i));
    }
    CHECK_EQ(p.in_use(), 130u);
    p.free(100);
    p.free(3);
    p.free(64);
    CHECK_EQ(p.alloc(), 3u);
    CHECK_EQ(p.alloc(), 64u);
    CHECK_EQ(p.alloc(), 100u);
    for (uint32_t i = 0; i < 130; ++i)
        if (i != 3 && i != 64 && i != 100) CHECK(p[i] == v128(i, ~uint64_t(i)));
    CHECK(p.allocated(5) && !p.allocated(130));
}

TEST(pool_values_are_16_byte_aligned) {
    Block b(ValuePool::words_needed(8), "t");
    ValuePool p;
    p.init(b.words(), 8, "vals");
    for (int i = 0; i < 8; ++i) {
        const uint32_t k = p.alloc();
        CHECK((reinterpret_cast<uintptr_t>(&p[k]) & 15) == 0);
    }
}

TEST(pool_cycle_loop_ops_never_allocate) {
    Block b(ValuePool::words_needed(256), "t");
    ValuePool p;
    p.init(b.words(), 256, "vals");
    uint32_t live[256];
    unsigned n = 0;
    u128 acc = 0;
    sim::begin_run();
    for (uint64_t i = 0; i < 100000; ++i) {
        if (n < 256 && (i % 3 != 0)) {
            live[n] = p.alloc();
            p[live[n]] = v128(i, i);
            ++n;
        } else if (n > 0) {
            const unsigned j = unsigned(i % n);
            acc += p[live[j]];
            p.free(live[j]);
            live[j] = live[--n];
        }
    }
    sim::end_run();
    CHECK(acc != 0);
}

namespace {
void pool_differential(uint64_t seed) {
    test::Rng rng(seed);
    for (uint32_t cap : {1u, 64u, 100u, 1000u}) {
        Block b(ValuePool::words_needed(cap), "t");
        ValuePool p;
        p.init(b.words(), cap, "vals");
        std::set<uint32_t> free_set;
        for (uint32_t i = 0; i < cap; ++i) free_set.insert(i);
        std::map<uint32_t, u128> live;
        std::vector<uint32_t> live_list;  // for picking one at random
        for (int op = 0; op < 250000; ++op) {
            const uint64_t k = rng.below(10);
            if (k < 5) {
                if (free_set.empty()) {
                    CHECK_EQ(p.in_use(), cap);
                    continue;
                }
                const uint32_t want = *free_set.begin();
                const uint32_t got = p.alloc();
                CHECK_EQ(got, want);
                free_set.erase(want);
                const u128 v = v128(rng.next(), rng.next());
                p[got] = v;
                live[got] = v;
                live_list.push_back(got);
            } else if (k < 9) {
                if (live_list.empty()) continue;
                const size_t j = size_t(rng.below(live_list.size()));
                const uint32_t i = live_list[j];
                CHECK(p[i] == live[i]);
                p.free(i);
                free_set.insert(i);
                live.erase(i);
                live_list[j] = live_list.back();
                live_list.pop_back();
            } else if (!live_list.empty()) {
                const uint32_t i = live_list[size_t(rng.below(live_list.size()))];
                CHECK(p[i] == live[i]);
                CHECK(p.allocated(i));
            }
            CHECK_EQ(p.in_use(), live.size());
        }
        for (const auto& [i, v] : live) CHECK(p[i] == v);
    }
}
}  // namespace

TEST(pool_differential_seed_1) { pool_differential(1); }
TEST(pool_differential_seed_2) { pool_differential(2); }
TEST(pool_differential_seed_3) { pool_differential(3); }

DEATH_TEST(pool_full_is_fatal, "value pool 'vals' full: capacity 65") {
    Block b(ValuePool::words_needed(65), "t");
    ValuePool p;
    p.init(b.words(), 65, "vals");
    for (int i = 0; i < 66; ++i) p.alloc();
}
DEBUG_DEATH_TEST(pool_double_free, "value pool 'vals': free\\(2\\) of an index not allocated") {
    Block b(ValuePool::words_needed(8), "t");
    ValuePool p;
    p.init(b.words(), 8, "vals");
    for (int i = 0; i < 4; ++i) p.alloc();
    p.free(2);
    p.free(2);
}
DEBUG_DEATH_TEST(pool_free_out_of_range, "value pool 'vals': free\\(8\\) out of range \\(capacity 8\\)") {
    Block b(ValuePool::words_needed(8), "t");
    ValuePool p;
    p.init(b.words(), 8, "vals");
    p.free(8);
}
DEBUG_DEATH_TEST(pool_read_freed_index, "value pool 'vals': index 1 is not allocated") {
    Block b(ValuePool::words_needed(8), "t");
    ValuePool p;
    p.init(b.words(), 8, "vals");
    p.alloc();
    (void)p[1];
}
DEBUG_DEATH_TEST(pool_storage_misaligned, "value pool 'vals': storage 0x[0-9a-f]+ is not 64-byte aligned") {
    Block b(32, "t");
    ValuePool p;
    p.init(b.words() + 4, 8, "vals");
}

// ---- SlotPool: N-word slots (payloads by reference, a size class per pool) ----
using sim::SlotPool;

TEST(slot_pool_wide_class_sizes_allocates_and_frees) {
    CHECK_EQ(SlotPool::words_needed(16, 5), 80u + 8u);       // five 16-word slots (80, whole lines), the bitmap a line
    Block b(SlotPool::words_needed(16, 5), "t");
    SlotPool p;
    p.init(b.words(), 16, 5, "w16");
    CHECK_EQ(p.slot_words(), 16u);
    uint32_t a = p.alloc(), c = p.alloc(), d = p.alloc();
    CHECK_EQ(a, 0u); CHECK_EQ(c, 1u); CHECK_EQ(d, 2u);
    for (unsigned k = 0; k < 16; k++) { p.slot(a)[k] = 100 + k; p.slot(c)[k] = 200 + k; p.slot(d)[k] = 300 + k; }
    CHECK_EQ(p.slot(c) - p.slot(a), 16);                         // adjacent slots, 16 words apart
    CHECK_EQ(p.slot(c)[15], 215u);
    p.free(c);
    CHECK_EQ(p.alloc(), 1u);                                     // the lowest free index again
    CHECK_EQ(p.slot(a)[0], 100u); CHECK_EQ(p.slot(d)[15], 315u);
    CHECK_EQ(p.in_use(), 3u);
}

DEATH_TEST(slot_pool_full_is_fatal_naming_the_class, "slot pool 'w16' full: capacity 5 \\(slots of 16 words\\)") {
    Block b(SlotPool::words_needed(16, 5), "t");
    SlotPool p;
    p.init(b.words(), 16, 5, "w16");
    for (int i = 0; i < 6; ++i) p.alloc();
}
