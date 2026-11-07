// barrier.cpp (WORKER4): see barrier.h.
#include "barrier.h"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <climits>

namespace sim::detail {

namespace {
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
    for (unsigned k = 0; round_.load(std::memory_order_acquire) == r; k++) {
        if (!block_ || k < 2000) { pause(); continue; }
        futex(&round_, FUTEX_WAIT_PRIVATE, r);                          // returns at once if the round moved on
    }
}

}  // namespace sim::detail
