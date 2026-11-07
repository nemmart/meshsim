// A tiny test harness (no framework): TEST, DEATH_TEST, DEBUG_DEATH_TEST, CHECK, CHECK_EQ, and a seeded PRNG.
//
// TEST(name) { ... }                    runs in process; a failing CHECK reports and returns from the test function.
// DEATH_TEST(name, "regex") { ... }     runs in a forked child with stderr captured; passes when the child dies of SIGABRT
//                                       and its stderr matches the POSIX extended regex.
// DEBUG_DEATH_TEST(name, "regex")       the same, run only in debug builds (for DCHECKs); skipped in release.
// A test binary runs every test, or those whose names contain argv[1]; exit status 1 if any failed.
#pragma once

#include <cstdint>

namespace test {

using Fn = void (*)();
struct Registrar {
    Registrar(const char* name, Fn fn, const char* death_pattern, bool debug_only);
};
void fail(const char* file, int line, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

// splitmix64: our own, so the sequence is the same on every compiler and library.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    uint64_t below(uint64_t n) { return next() % n; }
};

}  // namespace test

#define SIM_TEST_REG(name, pattern, debug_only)                                  \
    static void name();                                                          \
    static ::test::Registrar name##_registrar{#name, name, pattern, debug_only}; \
    static void name()

#define TEST(name) SIM_TEST_REG(name, nullptr, false)
#define DEATH_TEST(name, pattern) SIM_TEST_REG(name, pattern, false)
#define DEBUG_DEATH_TEST(name, pattern) SIM_TEST_REG(name, pattern, true)

#define CHECK(c)                                               \
    do {                                                       \
        if (!(c)) {                                            \
            ::test::fail(__FILE__, __LINE__, "CHECK(%s)", #c); \
            return;                                            \
        }                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                                                          \
    do {                                                                                                        \
        const auto check_a_ = (a);                                                                              \
        const auto check_b_ = (b);                                                                              \
        if (!(check_a_ == check_b_)) {                                                                          \
            ::test::fail(__FILE__, __LINE__, "CHECK_EQ(%s, %s): %llu != %llu", #a, #b,                         \
                         (unsigned long long)(check_a_), (unsigned long long)(check_b_));                      \
            return;                                                                                             \
        }                                                                                                       \
    } while (0)
