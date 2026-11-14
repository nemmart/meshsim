// java_random.h: java.util.Random's generator, for ref's start skew (Chip: Random(skewSeed).nextInt(2) per tile).
#pragma once

#include <cstdint>

namespace test {
struct JavaRandom {
    uint64_t seed;
    explicit JavaRandom(int64_t s) : seed((uint64_t(s) ^ 0x5DEECE66Dull) & ((1ull << 48) - 1)) {}
    int32_t next(int bits) {
        seed = (seed * 0x5DEECE66Dull + 0xB) & ((1ull << 48) - 1);
        return int32_t(seed >> (48 - bits));
    }
    // nextInt(bound) for a power-of-two bound (all ref needs here).
    int32_t next_int_pow2(int32_t bound) { return int32_t((int64_t(bound) * int64_t(next(31))) >> 31); }
};
// ref's start skew of a tile (Config.skew true, skewSeed 1).
inline int start_skew(unsigned tile, int64_t seed = 1) {
    JavaRandom r(seed);
    int s = 0;
    for (unsigned i = 0; i <= tile; i++) s = r.next_int_pow2(2);
    return s;
}
}  // namespace test
