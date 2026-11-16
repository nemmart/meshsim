// Bitmap: n bits in 64-bit words, n set at init. Bits at or beyond n are never set (DCHECK on every index), so
// find_first and for_each_set need no masking.
//
// Cycle-loop code: no allocation, no STL, header-only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "base/fatal.h"
#include "base/words.h"

namespace sim {

class Bitmap {
public:
    static size_t words_needed(size_t nbits) { return round_to_lines(nwords(nbits)); }

    // Starts all clear.
    void init(uint64_t* storage, size_t nbits, const char* name) {
        SIM_CHECK_STORAGE(storage, "bitmap", name);
        words_ = storage;
        nbits_ = nbits;
        name_ = name;
        clear_all();
    }

    void set(size_t i) {
        check(i);
        words_[i >> 6] |= bit(i);
    }
    void clear(size_t i) {
        check(i);
        words_[i >> 6] &= ~bit(i);
    }
    bool test(size_t i) const {
        check(i);
        return (words_[i >> 6] >> (i & 63)) & 1;
    }

    // The lowest set bit at or after `from`, or size() if there is none.
    size_t find_first(size_t from = 0) const {
        if (from >= nbits_) return nbits_;
        size_t w = from >> 6;
        uint64_t x = words_[w] & (~uint64_t(0) << (from & 63));
        const size_t nw = nwords(nbits_);
        while (x == 0) {
            if (++w == nw) return nbits_;
            x = words_[w];
        }
        return (w << 6) + size_t(__builtin_ctzll(x));
    }

    // f(i) for every set bit, ascending. f must not change this bitmap's words not yet visited.
    template <typename F>
    void for_each_set(F&& f) const {
        const size_t nw = nwords(nbits_);
        for (size_t w = 0; w < nw; ++w)
            for (uint64_t x = words_[w]; x; x &= x - 1) f((w << 6) + size_t(__builtin_ctzll(x)));
    }

    bool any() const {
        const size_t nw = nwords(nbits_);
        for (size_t w = 0; w < nw; ++w)
            if (words_[w]) return true;
        return false;
    }
    size_t popcount() const {
        size_t c = 0;
        const size_t nw = nwords(nbits_);
        for (size_t w = 0; w < nw; ++w) c += size_t(__builtin_popcountll(words_[w]));
        return c;
    }
    void clear_all() { std::memset(words_, 0, nwords(nbits_) * sizeof(uint64_t)); }
    void set_all() {
        const size_t nw = nwords(nbits_);
        if (nw == 0) return;
        std::memset(words_, 0xff, nw * sizeof(uint64_t));
        if (nbits_ & 63) words_[nw - 1] = (uint64_t(1) << (nbits_ & 63)) - 1;
    }

    size_t size() const { return nbits_; }
    // Word access, for structures built on a bitmap (the value pool's hint).
    uint64_t word(size_t w) const { return words_[w]; }
    size_t word_count() const { return nwords(nbits_); }
    const char* name() const { return name_; }

private:
    static size_t nwords(size_t nbits) { return (nbits + 63) >> 6; }
    static uint64_t bit(size_t i) { return uint64_t(1) << (i & 63); }
    void check(size_t i) const { DCHECK(i < nbits_, "bitmap '%s': bit %zu out of range (size %zu)", name_, i, nbits_); }

    uint64_t* words_ = nullptr;
    size_t nbits_ = 0;
    const char* name_ = "";
};

}  // namespace sim
