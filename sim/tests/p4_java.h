// p4_java.h (WORKER22, P4): the bits of the Java library v1.3's network, memory and engine suites lean on, so their
// ported output and their random choices are Java's exactly. Test-side code (the configuration regime, D6).
//
//   java.util.Random          JRandom: next(bits), nextInt(), nextInt(bound), nextLong(), nextDouble(), nextBoolean()
//   Collections.shuffle(l, r) jshuffle(v, r): for i = size .. 2, swap(i - 1, r.nextInt(i)) (ArrayList is RandomAccess)
//   List<Integer>.toString()  jlist_int(v): "[1, 2, 3]"
//
// (tests/java_random.h is W20's start-skew draw, nextInt for a power of two only; this one is the whole generator.)
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace p4 {

struct JRandom {
    uint64_t seed;
    explicit JRandom(int64_t s) : seed((uint64_t(s) ^ 0x5DEECE66Dull) & ((1ull << 48) - 1)) {}
    int32_t next(int bits) {
        seed = (seed * 0x5DEECE66Dull + 0xB) & ((1ull << 48) - 1);
        return int32_t(int64_t(seed) >> (48 - bits));
    }
    int32_t next_int() { return next(32); }
    int32_t next_int(int32_t bound) {
        int32_t r = next(31);
        const int32_t m = bound - 1;
        if ((bound & m) == 0) return int32_t((int64_t(bound) * int64_t(r)) >> 31);
        for (int32_t u = r; int32_t(uint32_t(u) - uint32_t(r = u % bound) + uint32_t(m)) < 0; u = next(31)) {}
        return r;
    }
    int64_t next_long() { return int64_t(uint64_t(int64_t(next(32))) << 32) + int64_t(next(32)); }
    bool next_boolean() { return next(1) != 0; }
    double next_double() { return double((int64_t(next(26)) << 27) + next(27)) * 0x1.0p-53; }
};

template <class T>
void jshuffle(std::vector<T>& v, JRandom& r) {
    for (size_t i = v.size(); i > 1; i--) std::swap(v[i - 1], v[size_t(r.next_int(int32_t(i)))]);
}

inline std::string jlist_int(const std::vector<int>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); i++) { if (i) s += ", "; s += std::to_string(v[i]); }
    return s + "]";
}

}  // namespace p4
