// Storage conventions shared by the cycle-loop structures.
//
// Every structure takes its storage from outside: `words_needed(...)` says how many 64-bit words it uses, rounded up to
// whole cache lines (kLineWords), and `init(storage, ...)` checks (DCHECK) that the storage is 64-byte aligned. So the
// sizing pass can add them up and carve one block per thread, every structure on its own lines.
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/fatal.h"

namespace sim {

inline constexpr size_t kLineBytes = 64;
inline constexpr size_t kLineWords = kLineBytes / sizeof(uint64_t);

constexpr size_t round_to_lines(size_t words) { return (words + kLineWords - 1) / kLineWords * kLineWords; }

inline bool line_aligned(const void* p) { return (reinterpret_cast<uintptr_t>(p) & (kLineBytes - 1)) == 0; }

#define SIM_CHECK_STORAGE(storage, kind, name) \
    DCHECK(::sim::line_aligned(storage), "%s '%s': storage %p is not 64-byte aligned", kind, name, (const void*)(storage))

}  // namespace sim
