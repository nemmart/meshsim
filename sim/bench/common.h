// W6's benchmarks: shared helpers (configuration-time and cycle-loop alike: no allocation, no STL in what the loop calls).
#pragma once

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <x86intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "base/fatal.h"

namespace bench {

// splitmix64, as the tests' Rng: the same sequence on every compiler.
inline uint64_t splitmix(uint64_t& s) {
    uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
inline uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
// An order-sensitive fold of one word into a digest.
inline uint64_t fold(uint64_t h, uint64_t w) { return mix64(h ^ (w + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))); }

inline double now_s() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
}

inline void pin_to(int cpu) {
    if (cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0) FATAL("pin to cpu %d failed", cpu);
}

// "0,2,4" or "0-11" or "12-15,0" into cpus[], returns the count.
inline int parse_cpus(const char* s, int* cpus, int max) {
    int n = 0;
    while (*s) {
        char* e;
        long a = strtol(s, &e, 10);
        if (e == s) FATAL("bad cpu list '%s'", s);
        long b = a;
        if (*e == '-') {
            s = e + 1;
            b = strtol(s, &e, 10);
        }
        for (long c = a; c <= b; ++c) {
            if (n == max) FATAL("cpu list too long");
            cpus[n++] = int(c);
        }
        s = (*e == ',') ? e + 1 : e;
    }
    return n;
}

// Start line for the worker threads: every thread builds its partition, then they leave together.
struct StartLine {
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    void arrive_and_wait() {
        ready.fetch_add(1);
        while (!go.load(std::memory_order_acquire)) _mm_pause();
    }
    void release_when(int n) {
        while (ready.load() < n) _mm_pause();
        go.store(true, std::memory_order_release);
    }
};

// Minimal "--key value" argument access (configuration time).
struct Args {
    int argc;
    char** argv;
    const char* get(const char* key, const char* dflt) const {
        for (int i = 1; i + 1 < argc; ++i)
            if (!strcmp(argv[i], key)) return argv[i + 1];
        return dflt;
    }
    bool flag(const char* key) const {
        for (int i = 1; i < argc; ++i)
            if (!strcmp(argv[i], key)) return true;
        return false;
    }
    long num(const char* key, long d) const {
        const char* v = get(key, nullptr);
        return v ? strtol(v, nullptr, 10) : d;
    }
    double real(const char* key, double d) const {
        const char* v = get(key, nullptr);
        return v ? strtod(v, nullptr) : d;
    }
};

}  // namespace bench
