// event_counts.h: a peripheral's counted events (spec 4.17.3).
//
// sim/ref/meshsim/src/EventCounts.java exactly: every EVENTS bit is backed by a small counter of a given depth, and reads
// 1 while its count is above 0. The peripheral adds 1 when the event happens (post; adding to a full count leaves it
// full, and a bit marked flag_full counts the post as a violation); a consuming load of EVENTS subtracts 1 from each
// bit it returned set under EVENTS_MASK (consume). A bit of depth 0 is live: the peripheral supplies its level.
//
// Timing (4.11): a post at t is visible from t + 1 (`value(t)` discounts a bit posted or set at t by one, as ref does,
// whatever its count); a consuming load's effect is applied by the slot at the end of its cycle.
//
// ref's onConsume callback is replaced by consume's return value: the mask of the bits it took a count from.
#pragma once

#include <cstdint>

#include "base/fatal.h"

namespace units {

struct EventCounts {
    static constexpr unsigned MAX_BITS = 32;
    static constexpr int64_t NEVER = INT64_MIN;

    unsigned n = 0;                    // bits in use (ref: depth.length)
    uint8_t depth[MAX_BITS] = {};      // per bit: 0 live, 1 an ordinary event, 2 a double-buffered queue's credits
    bool flag_full[MAX_BITS] = {};     // per bit: a post to a full counter is a violation (credits)
    uint8_t count[MAX_BITS] = {};
    int64_t last_post_at[MAX_BITS];
    uint32_t live_bits = 0;
    uint64_t full_posts = 0;           // posts to a full flagged counter; the last one's bit (the slot reports it)
    unsigned last_full_bit = 0;

    EventCounts() { for (unsigned b = 0; b < MAX_BITS; b++) last_post_at[b] = NEVER; }
    // Configuration: n bits with their depths and full-flags.
    void init(unsigned bits, const uint8_t* depths, const bool* flags) {
        if (bits > MAX_BITS) FATAL("EventCounts: %u bits, at most %u", bits, MAX_BITS);
        n = bits;
        for (unsigned b = 0; b < bits; b++) { depth[b] = depths[b]; flag_full[b] = flags[b]; }
    }

    // The event on bit b happens at t (visible from t + 1).
    void post(int64_t t, unsigned b) {
        if (count[b] >= depth[b]) {
            if (flag_full[b]) { full_posts++; last_full_bit = b; }
            return;
        }
        count[b]++;
        last_post_at[b] = t;
    }
    // Set bit b's count (the replayer's credits at a start).
    void set(int64_t t, unsigned b, unsigned v) { count[b] = uint8_t(v < depth[b] ? v : depth[b]); last_post_at[b] = t; }
    void clear(unsigned b) { count[b] = 0; }
    void clear_all() { for (unsigned b = 0; b < MAX_BITS; b++) count[b] = 0; }
    // A live bit's level.
    void live(unsigned b, bool on) { if (on) live_bits |= 1u << b; else live_bits &= ~(1u << b); }

    // EVENTS as a load sampling at t reads it: counts as at the end of t - 1 (a post at t not yet), the live bits.
    uint32_t value(int64_t t) const {
        uint32_t v = live_bits;
        for (unsigned b = 0; b < n; b++)
            if (depth[b] > 0 && int(count[b]) - (last_post_at[b] == t ? 1 : 0) > 0) v |= 1u << b;
        return v;
    }
    // A consuming load's effect: one count from each counted bit in `bits` (they were returned set). Returns the bits it
    // took a count from (ref's onConsume, called once per such bit).
    uint32_t consume(uint32_t bits) {
        uint32_t took = 0;
        for (unsigned b = 0; b < n; b++)
            if ((bits >> b & 1) && depth[b] > 0 && count[b] > 0) { count[b]--; took |= 1u << b; }
        return took;
    }
    unsigned count_of(unsigned b) const { return count[b]; }
};

}  // namespace units
