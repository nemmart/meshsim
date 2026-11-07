// Bench-only transports (W1's question 2 and where a delivery entry's destination goes). Not part of sim/src/base.
//
// FixedRing<W>, FixedBucket<W>: W1's Ring and Bucket with the width a template parameter; the same method set and the same
//   checks (full fatal in every build), so the only difference measured is the compile-time width.
// SoaBucket<K>: a delivery bucket whose destination ids sit in a uint32_t array beside the words (layout E2), rather than
//   in a word of each entry (E1, W1's Bucket at width + 1). K is the width in words, or 0 for a runtime width.
// copy_words<K>(d, s, n): the copy a transport does: K words if K > 0 (compile-time), else n (runtime, as W1's memcpy).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "base/fatal.h"
#include "base/words.h"

namespace bench {

using sim::round_to_lines;

template <unsigned K>
inline void copy_words(uint64_t* d, const uint64_t* s, unsigned n) {
    if constexpr (K > 0) {
        (void)n;
        std::memcpy(d, s, K * sizeof(uint64_t));
    } else {
        std::memcpy(d, s, size_t(n) * sizeof(uint64_t));
    }
}

template <unsigned W>
class FixedRing {
    static_assert(W > 0);

public:
    static size_t words_needed(unsigned width, unsigned depth) { return round_to_lines(size_t(width) * depth); }
    void init(uint64_t* storage, unsigned width, unsigned depth, const char* name) {
        SIM_CHECK_STORAGE(storage, "fixed ring", name);
        if (width != W || depth == 0) FATAL("fixed ring '%s': width %u (want %u), depth %u", name, width, W, depth);
        words_ = storage;
        depth_ = depth;
        head_ = count_ = 0;
        name_ = name;
    }
    uint64_t* push() {
        if (__builtin_expect(count_ == depth_, 0)) die_full();
        unsigned i = head_ + count_;
        if (i >= depth_) i -= depth_;
        ++count_;
        return words_ + size_t(i) * W;
    }
    void push(const uint64_t* w) { std::memcpy(push(), w, W * sizeof(uint64_t)); }
    const uint64_t* peek(unsigned i = 0) const {
        DCHECK(i < count_, "fixed ring '%s': peek(%u) with count %u", name_, i, count_);
        unsigned j = head_ + i;
        if (j >= depth_) j -= depth_;
        return words_ + size_t(j) * W;
    }
    void pop() {
        DCHECK(count_ > 0, "fixed ring '%s': pop when empty", name_);
        --count_;
        if (++head_ == depth_) head_ = 0;
    }
    unsigned count() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == depth_; }
    static constexpr unsigned width() { return W; }
    unsigned depth() const { return depth_; }

private:
    [[noreturn]] __attribute__((cold, noinline)) void die_full() const {
        FATAL("fixed ring '%s' full: capacity %u (width %u)", name_, depth_, W);
    }
    uint64_t* words_ = nullptr;
    unsigned depth_ = 0, head_ = 0, count_ = 0;
    const char* name_ = "";
};

template <unsigned W>
class FixedBucket {
    static_assert(W > 0);

public:
    static size_t words_needed(unsigned width, uint32_t capacity) { return round_to_lines(size_t(width) * capacity); }
    void init(uint64_t* storage, unsigned width, uint32_t capacity, const char* name) {
        SIM_CHECK_STORAGE(storage, "fixed bucket", name);
        if (width != W) FATAL("fixed bucket '%s': width %u (want %u)", name, width, W);
        words_ = storage;
        capacity_ = capacity;
        count_ = 0;
        name_ = name;
    }
    uint64_t* append() {
        if (__builtin_expect(count_ == capacity_, 0)) die_full();
        return words_ + size_t(count_++) * W;
    }
    void append(const uint64_t* w) { std::memcpy(append(), w, W * sizeof(uint64_t)); }
    const uint64_t* begin() const { return words_; }
    void clear() { count_ = 0; }
    uint32_t count() const { return count_; }
    static constexpr unsigned width() { return W; }

private:
    [[noreturn]] __attribute__((cold, noinline)) void die_full() const {
        FATAL("fixed bucket '%s' full: capacity %u (width %u)", name_, capacity_, W);
    }
    uint64_t* words_ = nullptr;
    uint32_t capacity_ = 0, count_ = 0;
    const char* name_ = "";
};

template <unsigned K>
class SoaBucket {
public:
    static size_t words_needed(unsigned width, uint32_t capacity) {
        return round_to_lines(size_t(width) * capacity) + round_to_lines((size_t(capacity) + 1) / 2);
    }
    void init(uint64_t* storage, unsigned width, uint32_t capacity, const char* name) {
        SIM_CHECK_STORAGE(storage, "soa bucket", name);
        if (width == 0 || (K > 0 && width != K)) FATAL("soa bucket '%s': width %u (fixed %u)", name, width, K);
        words_ = storage;
        dests_ = reinterpret_cast<uint32_t*>(storage + round_to_lines(size_t(width) * capacity));
        width_ = width;
        capacity_ = capacity;
        count_ = 0;
        name_ = name;
    }
    // The words of the next entry, its destination recorded beside them.
    uint64_t* append(uint32_t dest) {
        if (__builtin_expect(count_ == capacity_, 0)) die_full();
        dests_[count_] = dest;
        return words_ + size_t(count_++) * width();
    }
    const uint64_t* begin() const { return words_; }
    const uint32_t* dests() const { return dests_; }
    void clear() { count_ = 0; }
    uint32_t count() const { return count_; }
    unsigned width() const {
        if constexpr (K > 0) return K;
        return width_;
    }

private:
    [[noreturn]] __attribute__((cold, noinline)) void die_full() const {
        FATAL("soa bucket '%s' full: capacity %u (width %u)", name_, capacity_, width_);
    }
    uint64_t* words_ = nullptr;
    uint32_t* dests_ = nullptr;
    unsigned width_ = 0;
    uint32_t capacity_ = 0, count_ = 0;
    const char* name_ = "";
};

}  // namespace bench
