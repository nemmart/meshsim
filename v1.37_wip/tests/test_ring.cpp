// Ring: unit tests, the seeded differential test against std::deque, death tests.
#include <cstdint>
#include <deque>
#include <vector>

#include "base/alloc_guard.h"
#include "base/arena.h"
#include "base/ring.h"
#include "harness.h"

using sim::Block;
using sim::Ring;

TEST(ring_words_needed_rounds_to_lines) {
    CHECK_EQ(Ring::words_needed(1, 1), 8u);
    CHECK_EQ(Ring::words_needed(1, 8), 8u);
    CHECK_EQ(Ring::words_needed(1, 9), 16u);
    CHECK_EQ(Ring::words_needed(3, 5), 16u);
    CHECK_EQ(Ring::words_needed(2, 64), 128u);
}

TEST(ring_depth_one_width_one) {
    Block b(Ring::words_needed(1, 1), "t");
    Ring r;
    r.init(b.words(), 1, 1, "r");
    CHECK(r.empty() && !r.full());
    r.push()[0] = 42;
    CHECK(r.full());
    CHECK_EQ(r.count(), 1u);
    CHECK_EQ(r.peek()[0], 42u);
    r.pop();
    CHECK(r.empty());
    r.push()[0] = 43;  // wraps to the same slot
    CHECK_EQ(r.peek()[0], 43u);
}

TEST(ring_wraps_and_keeps_order) {
    Block b(Ring::words_needed(3, 5), "t");
    Ring r;
    r.init(b.words(), 3, 5, "r");
    uint64_t next = 0, expect = 0;
    for (int round = 0; round < 7; ++round) {  // 7 rounds of 3 pushes and 3 pops cross the end several times
        for (int k = 0; k < 3; ++k) {
            const uint64_t w[3] = {next, next * 10, next * 100};
            r.push(w);
            ++next;
        }
        for (unsigned i = 0; i < r.count(); ++i) CHECK_EQ(r.peek(i)[0], expect + i);
        for (int k = 0; k < 3; ++k) {
            const uint64_t* s = r.peek();
            CHECK(s[0] == expect && s[1] == expect * 10 && s[2] == expect * 100);
            r.pop();
            ++expect;
        }
    }
    CHECK(r.empty());
}

TEST(ring_fills_exactly_to_depth) {
    Block b(Ring::words_needed(2, 4), "t");
    Ring r;
    r.init(b.words(), 2, 4, "r");
    for (uint64_t i = 0; i < 4; ++i) r.push()[0] = i;
    CHECK(r.full());
    CHECK_EQ(r.peek(3)[0], 3u);
    r.peek_mut(1)[1] = 77;
    CHECK_EQ(r.peek(1)[1], 77u);
}

TEST(ring_cycle_loop_ops_never_allocate) {
    Block b(Ring::words_needed(3, 16), "t");
    Ring r;
    r.init(b.words(), 3, 16, "r");
    uint64_t sum = 0;
    sim::begin_run();
    for (uint64_t i = 0; i < 100000; ++i) {
        if (!r.full()) {
            uint64_t* s = r.push();
            s[0] = s[1] = s[2] = i;
        }
        if (i % 3 == 0 && !r.empty()) {
            sum += r.peek()[0] + r.count();
            r.pop();
        }
    }
    sim::end_run();
    CHECK(sum > 0);
}

namespace {
void ring_differential(uint64_t seed) {
    struct Config {
        unsigned width, depth;
    };
    const Config configs[] = {{1, 1}, {3, 5}, {2, 64}, {3, 7}};
    test::Rng rng(seed);
    for (const Config& c : configs) {
        Block b(Ring::words_needed(c.width, c.depth), "t");
        Ring r;
        r.init(b.words(), c.width, c.depth, "r");
        std::deque<std::vector<uint64_t>> model;
        for (int op = 0; op < 250000; ++op) {
            const uint64_t k = rng.below(10);
            if (k < 4) {  // push
                if (model.size() == c.depth) {
                    CHECK(r.full());
                    continue;
                }
                std::vector<uint64_t> w(c.width);
                for (auto& x : w) x = rng.next();
                if (k & 1)
                    r.push(w.data());
                else {
                    uint64_t* slot = r.push();  // the slot, filled word by word
                    for (unsigned j = 0; j < c.width; ++j) slot[j] = w[j];
                }
                model.push_back(w);
            } else if (k < 8) {  // pop
                if (model.empty()) {
                    CHECK(r.empty());
                    continue;
                }
                for (unsigned j = 0; j < c.width; ++j) CHECK_EQ(r.peek()[j], model.front()[j]);
                r.pop();
                model.pop_front();
            } else if (!model.empty()) {  // peek anywhere
                const unsigned i = unsigned(rng.below(model.size()));
                for (unsigned j = 0; j < c.width; ++j) CHECK_EQ(r.peek(i)[j], model[i][j]);
            }
            CHECK_EQ(r.count(), model.size());
            CHECK_EQ(r.empty(), model.empty());
            CHECK_EQ(r.full(), model.size() == c.depth);
        }
    }
}
}  // namespace

TEST(ring_differential_seed_1) { ring_differential(1); }
TEST(ring_differential_seed_2) { ring_differential(2); }
TEST(ring_differential_seed_3) { ring_differential(3); }

DEATH_TEST(ring_full_is_fatal, "ring 'north_in' full: capacity 4 \\(width 3\\)") {
    Block b(Ring::words_needed(3, 4), "t");
    Ring r;
    r.init(b.words(), 3, 4, "north_in");
    for (int i = 0; i < 5; ++i) r.push();
}
DEATH_TEST(ring_zero_width_is_fatal, "ring 'z': width 0 and depth 4 must both be at least 1") {
    Block b(8, "t");
    Ring r;
    r.init(b.words(), 0, 4, "z");
}
DEBUG_DEATH_TEST(ring_pop_empty, "ring 'r': pop when empty") {
    Block b(8, "t");
    Ring r;
    r.init(b.words(), 1, 4, "r");
    r.pop();
}
DEBUG_DEATH_TEST(ring_peek_past_count, "ring 'r': peek\\(2\\) with count 2") {
    Block b(8, "t");
    Ring r;
    r.init(b.words(), 1, 4, "r");
    r.push();
    r.push();
    (void)r.peek(2);
}
DEBUG_DEATH_TEST(ring_storage_misaligned, "ring 'r': storage 0x[0-9a-f]+ is not 64-byte aligned") {
    Block b(16, "t");
    Ring r;
    r.init(b.words() + 1, 1, 4, "r");
}
