// Bitmap: unit tests, the seeded differential test against std::vector<bool>, death tests.
#include <cstdint>
#include <vector>

#include "base/alloc_guard.h"
#include "base/arena.h"
#include "base/bitmap.h"
#include "harness.h"

using sim::Bitmap;
using sim::Block;

TEST(bitmap_words_needed_rounds_to_lines) {
    CHECK_EQ(Bitmap::words_needed(1), 8u);
    CHECK_EQ(Bitmap::words_needed(512), 8u);
    CHECK_EQ(Bitmap::words_needed(513), 16u);
}

TEST(bitmap_word_boundaries) {
    Block b(Bitmap::words_needed(130), "t");
    Bitmap m;
    m.init(b.words(), 130, "m");
    CHECK(!m.any());
    CHECK_EQ(m.find_first(), 130u);
    for (size_t i : {0, 63, 64, 65, 127, 128, 129}) m.set(i);
    CHECK_EQ(m.popcount(), 7u);
    CHECK_EQ(m.find_first(), 0u);
    CHECK_EQ(m.find_first(1), 63u);
    CHECK_EQ(m.find_first(64), 64u);
    CHECK_EQ(m.find_first(66), 127u);
    CHECK_EQ(m.find_first(130), 130u);
    CHECK_EQ(m.find_first(1000), 130u);
    std::vector<size_t> seen;
    m.for_each_set([&](size_t i) { seen.push_back(i); });
    CHECK(seen == (std::vector<size_t>{0, 63, 64, 65, 127, 128, 129}));
    m.clear(64);
    CHECK(!m.test(64) && m.test(65));
    CHECK_EQ(m.find_first(64), 65u);
}

TEST(bitmap_set_all_stops_at_size) {
    Block b(Bitmap::words_needed(65), "t");
    Bitmap m;
    m.init(b.words(), 65, "m");
    m.set_all();
    CHECK_EQ(m.popcount(), 65u);
    CHECK_EQ(m.word(1), 1u);
    m.clear_all();
    CHECK(!m.any());
    Block b2(Bitmap::words_needed(128), "t");
    Bitmap full;
    full.init(b2.words(), 128, "m");
    full.set_all();
    CHECK_EQ(full.popcount(), 128u);
}

TEST(bitmap_cycle_loop_ops_never_allocate) {
    Block b(Bitmap::words_needed(1000), "t");
    Bitmap m;
    m.init(b.words(), 1000, "m");
    size_t acc = 0;
    sim::begin_run();
    for (size_t i = 0; i < 100000; ++i) {
        m.set((i * 7919) % 1000);
        if (i % 3 == 0) m.clear((i * 104729) % 1000);
        acc += m.find_first(i % 1000) + m.test(i % 1000) + m.any();
        if (i % 1000 == 0) m.for_each_set([&](size_t j) { acc += j; });
    }
    acc += m.popcount();
    sim::end_run();
    CHECK(acc > 0);
}

namespace {
void bitmap_differential(uint64_t seed) {
    test::Rng rng(seed);
    for (size_t n : {1, 63, 64, 65, 1000}) {
        Block b(Bitmap::words_needed(n), "t");
        Bitmap m;
        m.init(b.words(), n, "m");
        std::vector<bool> model(n, false);
        for (int op = 0; op < 200000; ++op) {
            const uint64_t k = rng.below(100);
            const size_t i = size_t(rng.below(n));
            if (k < 35) {
                m.set(i);
                model[i] = true;
            } else if (k < 70) {
                m.clear(i);
                model[i] = false;
            } else if (k < 85) {
                CHECK_EQ(m.test(i), bool(model[i]));
            } else if (k < 98) {
                const size_t from = size_t(rng.below(n + 3));  // includes from >= n
                size_t want = n;
                for (size_t j = from; j < n; ++j)
                    if (model[j]) {
                        want = j;
                        break;
                    }
                CHECK_EQ(m.find_first(from), want);
            } else if (k < 99) {
                std::vector<size_t> got, want;
                m.for_each_set([&](size_t j) { got.push_back(j); });
                for (size_t j = 0; j < n; ++j)
                    if (model[j]) want.push_back(j);
                CHECK(got == want);
            } else if (rng.below(50) == 0) {
                const bool all = rng.below(2);
                if (all)
                    m.set_all();
                else
                    m.clear_all();
                model.assign(n, all);
            }
            size_t pc = 0;
            for (bool x : model) pc += x;
            CHECK_EQ(m.popcount(), pc);
            CHECK_EQ(m.any(), pc != 0);
        }
    }
}
}  // namespace

TEST(bitmap_differential_seed_1) { bitmap_differential(1); }
TEST(bitmap_differential_seed_2) { bitmap_differential(2); }
TEST(bitmap_differential_seed_3) { bitmap_differential(3); }

DEBUG_DEATH_TEST(bitmap_set_out_of_range, "bitmap 'ready': bit 65 out of range \\(size 65\\)") {
    Block b(8, "t");
    Bitmap m;
    m.init(b.words(), 65, "ready");
    m.set(65);
}
DEBUG_DEATH_TEST(bitmap_test_out_of_range, "bitmap 'ready': bit 100 out of range \\(size 64\\)") {
    Block b(8, "t");
    Bitmap m;
    m.init(b.words(), 64, "ready");
    (void)m.test(100);
}
DEBUG_DEATH_TEST(bitmap_storage_misaligned, "bitmap 'm': storage 0x[0-9a-f]+ is not 64-byte aligned") {
    Block b(16, "t");
    Bitmap m;
    m.init(b.words() + 2, 64, "m");
}
