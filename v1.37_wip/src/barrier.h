// barrier.h: the threads' barrier. Between cycles only: nothing in a cycle waits.
//
// A barrier on one atomic counter and a round number. It waits with the CPU's pause hint for `spins` rounds, then yields
// (so a thread sharing the CPU still runs) or, with `block`, sleeps on a futex; ~0u spins only (for a partition whose intervals are long, or threads that share a core). The choice is
// Options::barrier, made by measurement.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace sim::detail {

class Barrier {
public:
    void init(unsigned n, bool block, unsigned spins = 2000, bool flags = false) {
        n_ = n; block_ = block; spins_ = spins; left_.store(n); flags_ = flags;
        epoch_.reset(flags ? new Epoch[n] : nullptr);
    }
    // The watchdog (debug builds): a wait longer than `secs` seconds calls report(ctx) (which prints every thread's
    // state) and is fatal, so a hang explains itself, in CI too.
    void watchdog(double secs, void (*report)(void*), void* ctx) { secs_ = secs; report_ = report; ctx_ = ctx; }
    // Every thread calls it once per round; all return after the last has arrived. Release/acquire: what a thread wrote
    // before arriving is visible to every thread after it returns.
    void wait();
    // thread t's arrival (t < n). With `flags` (Options::barrier): no shared counter; each thread bumps its own
    // epoch (its own line) and waits until every other thread's has reached it: no contended read-modify-write, and the
    // last arrival is seen by each waiter in one line transfer. Otherwise the counter barrier above.
    void wait(unsigned t);

private:
    struct alignas(64) Epoch { std::atomic<uint64_t> v{0}; };
    std::unique_ptr<Epoch[]> epoch_;
    bool flags_ = false;
    alignas(64) std::atomic<uint32_t> left_{0};
    alignas(64) std::atomic<uint32_t> round_{0};
    unsigned n_ = 1;
    bool block_ = false;
    unsigned spins_ = 2000;          // pauses before yielding or sleeping (~0u: spin only)
    double secs_ = 0;
    void (*report_)(void*) = nullptr;
    void* ctx_ = nullptr;
};

}  // namespace sim::detail
