// FATAL and DCHECK: the framework's only error handling (no exceptions; sim/README.md, DECISIONS D4).
//
// FATAL(fmt, ...)       always on: prints "FATAL file:line: <message>" to stderr with fprintf, then std::abort().
// DCHECK(cond, fmt, ...) debug builds only (SIM_DEBUG): FATAL with the condition's text if cond is false. In release
//                        the condition is still compiled (so it type-checks and nothing goes unused) but never evaluated.
//
// Capacity overflows ("full") are FATAL in every build; misuse (pop when empty, double free, index out of range) is DCHECK.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sim {

#ifdef SIM_DEBUG
inline constexpr bool kDebug = true;
#else
inline constexpr bool kDebug = false;
#endif

[[noreturn]] void fatal_at(const char* file, int line, const char* fmt, ...) __attribute__((format(printf, 3, 4), cold));

}  // namespace sim

#define FATAL(...) ::sim::fatal_at(__FILE__, __LINE__, __VA_ARGS__)

#define DCHECK(cond, fmt, ...)                                                                                   \
    do {                                                                                                         \
        if (::sim::kDebug && __builtin_expect(!(cond), 0))                                                       \
            ::sim::fatal_at(__FILE__, __LINE__, "check failed: %s: " fmt, #cond __VA_OPT__(, ) __VA_ARGS__);     \
    } while (0)
