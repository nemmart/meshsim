// Bucket: unit tests, the seeded differential test against std::vector, death tests.
#include <cstdint>
#include <vector>

#include "base/alloc_guard.h"
#include "base/arena.h"
#include "base/bucket.h"
#include "harness.h"

using sim::Block;
using sim::Bucket;

TEST(bucket_words_needed_rounds_to_lines) {
    CHECK_EQ(Bucket::words_needed(2, 4), 8u);
    CHECK_EQ(Bucket::words_needed(3, 3), 16u);
    CHECK_EQ(Bucket::words_needed(3, 0), 0u);
}

TEST(bucket_append_read_clear) {
    for (unsigned width : {2u, 3u}) {  // the two link-word forms a benchmark measured
        Block b(Bucket::words_needed(width, 5), "t");
        Bucket k;
        k.init(b.words(), width, 5, "t+3");
        for (int round = 0; round < 3; ++round) {
            for (uint64_t i = 0; i < 5; ++i) {
                uint64_t* e = k.append();
                for (unsigned j = 0; j < width; ++j) e[j] = i * 10 + j + round;
            }
            CHECK_EQ(k.count(), 5u);
            for (uint32_t i = 0; i < 5; ++i)
                for (unsigned j = 0; j < width; ++j) CHECK_EQ(k.entry(i)[j], i * 10 + j + round);
            CHECK_EQ(k.begin()[width * 4 + 1], 41u + round);  // the flat view: entries front to back
            k.clear();
            CHECK(k.empty());
        }
    }
}

TEST(bucket_cycle_loop_ops_never_allocate) {
    Block b(Bucket::words_needed(3, 64), "t");
    Bucket k;
    k.init(b.words(), 3, 64, "t");
    uint64_t acc = 0;
    sim::begin_run();
    for (uint64_t cycle = 0; cycle < 10000; ++cycle) {
        for (uint64_t i = 0; i < cycle % 65; ++i) {
            const uint64_t w[3] = {cycle, i, cycle ^ i};
            k.append(w);
        }
        for (uint32_t i = 0; i < k.count(); ++i) acc += k.entry(i)[2];
        k.clear();
    }
    sim::end_run();
    CHECK(acc > 0);
}

namespace {
void bucket_differential(uint64_t seed) {
    test::Rng rng(seed);
    struct Config {
        unsigned width;
        uint32_t capacity;
    };
    for (const Config& c : {Config{1, 1}, Config{2, 37}, Config{3, 37}, Config{3, 512}}) {
        Block b(Bucket::words_needed(c.width, c.capacity), "t");
        Bucket k;
        k.init(b.words(), c.width, c.capacity, "t");
        std::vector<std::vector<uint64_t>> model;
        for (int op = 0; op < 250000; ++op) {
            const uint64_t r = rng.below(100);
            if (r < 60) {
                if (model.size() == c.capacity) continue;
                std::vector<uint64_t> w(c.width);
                for (auto& x : w) x = rng.next();
                if (r & 1) {
                    k.append(w.data());
                } else {
                    uint64_t* e = k.append();
                    for (unsigned j = 0; j < c.width; ++j) e[j] = w[j];
                }
                model.push_back(w);
            } else if (r < 97) {
                if (model.empty()) continue;
                const uint32_t i = uint32_t(rng.below(model.size()));
                for (unsigned j = 0; j < c.width; ++j) CHECK_EQ(k.entry(i)[j], model[i][j]);
            } else {
                for (size_t i = 0; i < model.size(); ++i)  // a delivery walk, front to back
                    for (unsigned j = 0; j < c.width; ++j) CHECK_EQ(k.begin()[i * c.width + j], model[i][j]);
                k.clear();
                model.clear();
            }
            CHECK_EQ(k.count(), model.size());
        }
    }
}
}  // namespace

TEST(bucket_differential_seed_1) { bucket_differential(1); }
TEST(bucket_differential_seed_2) { bucket_differential(2); }
TEST(bucket_differential_seed_3) { bucket_differential(3); }

DEATH_TEST(bucket_full_is_fatal, "bucket 'wheel\\[17\\]' full: capacity 3 \\(width 2\\)") {
    Block b(8, "t");
    Bucket k;
    k.init(b.words(), 2, 3, "wheel[17]");
    for (int i = 0; i < 4; ++i) k.append();
}
DEATH_TEST(bucket_zero_width_is_fatal, "bucket 'z': width must be at least 1") {
    Block b(8, "t");
    Bucket k;
    k.init(b.words(), 0, 3, "z");
}
DEBUG_DEATH_TEST(bucket_entry_past_count, "bucket 't': entry\\(1\\) with count 1") {
    Block b(8, "t");
    Bucket k;
    k.init(b.words(), 2, 3, "t");
    k.append();
    (void)k.entry(1);
}
DEBUG_DEATH_TEST(bucket_storage_misaligned, "bucket 't': storage 0x[0-9a-f]+ is not 64-byte aligned") {
    Block b(16, "t");
    Bucket k;
    k.init(b.words() + 1, 2, 3, "t");
}
