// barrier.cpp: see barrier.h.
#include "barrier.h"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sched.h>
#include <unistd.h>

#include <climits>
#include <ctime>

#include "base/fatal.h"

namespace sim::detail {

namespace {
[[maybe_unused]] double seconds() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return double(t.tv_sec) + 1e-9 * double(t.tv_nsec); }
inline void pause() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}
long futex(std::atomic<uint32_t>* a, int op, uint32_t v) {
    return syscall(SYS_futex, reinterpret_cast<uint32_t*>(a), op, v, nullptr, nullptr, 0);
}
}  // namespace

void Barrier::wait() {
    if (n_ == 1) return;
    const uint32_t r = round_.load(std::memory_order_acquire);
    if (left_.fetch_sub(1, std::memory_order_acq_rel) == 1) {        // the last to arrive opens the next round
        left_.store(n_, std::memory_order_relaxed);
        round_.store(r + 1, std::memory_order_release);
        if (block_) futex(&round_, FUTEX_WAKE_PRIVATE, INT_MAX);
        return;
    }
    double since = 0;
    for (unsigned k = 0; round_.load(std::memory_order_acquire) == r; k++) {
        if constexpr (kDebug) if (secs_ > 0 && ((k & 1023) == 1023 || (block_ && k >= spins_))) {   // the watchdog
            const double now = seconds();
            if (since == 0) since = now;
            else if (now - since > secs_) {
                if (report_) report_(ctx_);
                FATAL("barrier: waited more than %.0f s for the other threads (their state above)", secs_);
            }
        }
        if (k < spins_) { pause(); continue; }
        if (!block_) { sched_yield(); continue; }                       // not starving a thread sharing the CPU
        const timespec tick{0, 100000000};                              // 0.1 s: returns at once if the round moved on
        syscall(SYS_futex, reinterpret_cast<uint32_t*>(&round_), FUTEX_WAIT_PRIVATE, r, &tick, nullptr, 0);
    }
}

void Barrier::wait(unsigned t) {
    if (!flags_) return wait();
    if (n_ == 1) return;
    const uint64_t e = epoch_[t].v.load(std::memory_order_relaxed) + 1;
    epoch_[t].v.store(e, std::memory_order_release);
    double since = 0;
    unsigned k = 0;
    for (unsigned s = 0; s < n_; s++) {
        if (s == t) continue;
        while (epoch_[s].v.load(std::memory_order_acquire) < e) {
            if constexpr (kDebug) if (secs_ > 0 && (k & 1023) == 1023) {   // the watchdog
                const double now = seconds();
                if (since == 0) since = now;
                else if (now - since > secs_) {
                    if (report_) report_(ctx_);
                    FATAL("barrier: waited more than %.0f s for the other threads (their state above)", secs_);
                }
            }
            if (k++ < spins_) pause(); else sched_yield();
        }
    }
}

}  // namespace sim::detail
