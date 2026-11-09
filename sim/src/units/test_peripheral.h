// test_peripheral.h: the suites' test peripheral, type 15 (sim/ref/meshsim/test/TestPeripheral.java; WORKER15).
//  - events: [0] run done (depth 1), [1] DMA done (the slot's), [2] an ordinary event (depth 1, post), [3] a credit
//    (depth 2, credit; a post to a full count is flagged), [4] live (level);
//  - a run lasts run_length cycles; STATUS [0] while it runs; ERRORS [0] when a test calls fault;
//  - words: 32 the runs started (R), 33 a scratch word (R/W), 34 a constant (R), the rest undefined;
//  - region writes are recorded when they land (rp_latency after the director), at their byte offset;
//  - two output queues: after out_start(q, n) it makes n 128-bit words, word i's low half q << 56 | the queue's running
//    count and its high half the complement, one per cycle from the next cycle (or all at once with burst).
// Not lazily settled (as ref's): its inputs act at once. Test code; fixed arrays.
#pragma once

#include <cstdint>

#include "units/event_counts.h"
#include "units/peripheral.h"

namespace units {

struct TestPeripheral : PeripheralBase<TestPeripheral> {
    static constexpr unsigned MAXA = 64, MAXQ = 256;
    EventCounts ev;
    bool running_ = false;
    int64_t run_ends = 0;
    int run_length = 10, runs = 0;
    uint32_t scratch = 0;
    int errors_ = 0;
    struct Accept { int64_t land_t; int src; uint32_t offset; int size; uint64_t lo, hi; } accepts[MAXA] = {};   // a ring
    unsigned n_accepts = 0, n_run_calls = 0;
    unsigned taken[2] = {};
    int64_t avail_at[2][MAXQ] = {};
    unsigned n_avail[2] = {};
    bool burst = false;
    uint64_t n_violations = 0;

    TestPeripheral() {
        const uint8_t d[] = {1, 0, 1, 2, 0};
        const bool f[] = {false, false, false, true, false};
        ev.init(5, d, f);
    }
    int type() const { return 15; }
    int params() const { return 15 | 3 << 8; }
    EventCounts& events() { return ev; }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    void post(int64_t t) { ev.post(t, 2); }
    void credit(int64_t t) { ev.post(t, 3); }
    void level(bool on) { ev.live(4, on); }
    void fault() { errors_ |= 1; }
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
        accepts[n_accepts++ % MAXA] = {t + RP_LATENCY, src, offset, size, lo, hi};   // the last MAXA kept, all counted
    }
    void reset(int64_t) { running_ = false; ev.clear_all(); errors_ = 0; }
    void run(int64_t t) {
        n_run_calls++;
        if (running_) { n_violations++; return; }   // "a run while running, ignored"
        running_ = true; runs++; run_ends = t + run_length;
    }
    void step(int64_t t) { if (running_ && t >= run_ends) { running_ = false; ev.post(t, 0); } }
    bool running(int64_t) const { return running_; }
    int errors(int64_t) const { return errors_; }
    bool word(int64_t, int w, uint32_t& v) const {
        if (w == 32) { v = uint32_t(runs); return true; }
        if (w == 33) { v = scratch; return true; }
        if (w == 34) { v = 0x7E57; return true; }
        return false;
    }
    int write_word(int64_t, int w, uint32_t v) { if (w == 33) { scratch = v; return 1; } return w == 32 || w == 34 ? 0 : -1; }
    uint64_t violation_count() const { return n_violations; }
    bool writable(int w) const { return w == 33; }
    int out_queues() const { return 2; }
    bool out_ready(int64_t t, int q) const { return q < 2 && taken[q] < n_avail[q] && avail_at[q][taken[q] % MAXQ] <= t; }
    void out_take(int64_t t, int q, uint64_t w[2]) {
        if (!out_ready(t, q)) FATAL("test peripheral: no word");
        unsigned i = taken[q]++;
        w[0] = uint64_t(q) << 56 | i; w[1] = ~w[0];
    }
    void out_start(int64_t t, int q, int64_t words) {
        int64_t from = n_avail[q] == 0 ? t + 1 : avail_at[q][(n_avail[q] - 1) % MAXQ] + (burst ? 0 : 1);
        if (from < t + 1) from = t + 1;
        for (int64_t i = 0; i < words; i++) {
            if (n_avail[q] - taken[q] == MAXQ) FATAL("test peripheral: more than %u words outstanding", MAXQ);
            avail_at[q][n_avail[q]++ % MAXQ] = burst ? from : from + i;   // a ring: entries before taken[q] are done
        }
    }
};

}  // namespace units
