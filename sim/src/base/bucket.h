// Bucket: one delivery bucket, an append-only array of fixed-width entries of 64-bit words. The entry width and the
// capacity are set at init (the width is a parameter: a benchmark measured two words against three for a link word). Cleared by
// resetting the count. Appending to a full bucket is fatal in every build; reading past the count is DCHECK.
//
// The wheel of buckets is the. Cycle-loop code: no allocation, no STL, header-only. A later fixed-width form would keep
// this exact set of methods, so callers don't change.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "base/fatal.h"
#include "base/words.h"

namespace sim {

class Bucket {
public:
    static size_t words_needed(unsigned width, uint32_t capacity) { return round_to_lines(size_t(width) * capacity); }

    void init(uint64_t* storage, unsigned width, uint32_t capacity, const char* name) {
        SIM_CHECK_STORAGE(storage, "bucket", name);
        if (width == 0) FATAL("bucket '%s': width must be at least 1", name);
        words_ = storage;
        width_ = width;
        capacity_ = capacity;
        count_ = 0;
        name_ = name;
    }

    // The next entry; its words are the caller's to write.
    uint64_t* append() {
        if (__builtin_expect(count_ == capacity_, 0)) die_full();
        return words_ + size_t(count_++) * width_;
    }
    void append(const uint64_t* w) { std::memcpy(append(), w, size_t(width_) * sizeof(uint64_t)); }

    const uint64_t* entry(uint32_t i) const {
        DCHECK(i < count_, "bucket '%s': entry(%u) with count %u", name_, i, count_);
        return words_ + size_t(i) * width_;
    }
    // The entries as one array, count() * width() words, front to back.
    const uint64_t* begin() const { return words_; }

    void clear() { count_ = 0; }
    uint32_t count() const { return count_; }
    bool empty() const { return count_ == 0; }
    unsigned width() const { return width_; }
    uint32_t capacity() const { return capacity_; }
    const char* name() const { return name_; }

private:
    [[noreturn]] __attribute__((cold, noinline)) void die_full() const {
        FATAL("bucket '%s' full: capacity %u (width %u)", name_, capacity_, width_);
    }

    uint64_t* words_ = nullptr;
    unsigned width_ = 0;
    uint32_t capacity_ = 0, count_ = 0;
    const char* name_ = "";
};

}  // namespace sim
