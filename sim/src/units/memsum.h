// memsum.h: a memory's part of the canonical state fingerprint, kept by the unit that owns the memory (WORKER8;
// sim/tests/AUDIT-M2.md 6.2, sim/tests/statefp.h). A memory is one word of its unit's stream: the sum mod 2^64 of
// fmix64(region << 56 ^ index << 32 ^ value) over its non-zero 32-bit words, order-free, so the owner updates it at
// every write (two fmix64 and an add) and a fingerprint never scans the memory. Plain code: allowed in a step.
#pragma once

#include <cstddef>
#include <cstdint>

namespace units {

inline uint64_t fmix64(uint64_t k) {
    k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ull; k ^= k >> 33;
    return k;
}
inline uint64_t mem_term(uint32_t region, uint64_t index, uint32_t value) {
    return fmix64(uint64_t(region) << 56 ^ index << 32 ^ uint64_t(value));
}
// StateFp's regions.
enum : uint32_t { FP_DMEM = 1, FP_PMEM = 2, FP_SRAM = 3, FP_STREAM_SRC = 4, FP_STREAM_DEST = 5, FP_STREAM_COUNT = 6,
                  FP_GROUP = 7, FP_PTR = 8, FP_HIST = 9 };

struct MemSum {
    uint32_t region = 0;
    uint64_t sum = 0;
    explicit MemSum(uint32_t r = 0) : region(r) {}
    void write(uint64_t index, uint32_t old_v, uint32_t new_v) {
        if (old_v == new_v) return;
        if (old_v) sum -= mem_term(region, index, old_v);
        if (new_v) sum += mem_term(region, index, new_v);
    }
    // The full sum over words[0..n) at index k * stride + offset (StateFp.setHash): initial images and checks.
    static uint64_t scan(uint32_t region, const uint32_t* words, size_t n, uint64_t stride = 1, uint64_t offset = 0) {
        uint64_t s = 0;
        for (size_t k = 0; k < n; k++) if (words[k]) s += mem_term(region, k * stride + offset, words[k]);
        return s;
    }
};

}  // namespace units
