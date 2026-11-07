// ValuePool: the thread's array of 128-bit values and its free indices. A payload too big to travel inline travels as
// an index into this array (sim/README.md). The capacity is set at init and the array never grows: alloc when none is
// free is fatal in every build; freeing an index out of range or not allocated is DCHECK.
//
// Free slots are a Bitmap (1 = free). alloc returns the lowest free index, so allocation is deterministic; a hint (the
// lowest word that may hold a free bit) keeps it from rescanning from 0.
//
// Storage layout: the values (2 words each, from the start of the storage, so 16-byte aligned), then the bitmap on its own
// lines. Cycle-loop code: no allocation, no STL, header-only.
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/bitmap.h"
#include "base/fatal.h"
#include "base/words.h"

namespace sim {

using u128 = unsigned __int128;

class ValuePool {
public:
    static size_t words_needed(uint32_t capacity) {
        return round_to_lines(2 * size_t(capacity)) + Bitmap::words_needed(capacity);
    }

    // Starts with every index free; the values are not cleared.
    void init(uint64_t* storage, uint32_t capacity, const char* name) {
        SIM_CHECK_STORAGE(storage, "value pool", name);
        values_ = reinterpret_cast<u128*>(storage);
        free_.init(storage + round_to_lines(2 * size_t(capacity)), capacity, name);
        free_.set_all();
        capacity_ = capacity;
        in_use_ = 0;
        hint_ = 0;
        name_ = name;
    }

    uint32_t alloc() {
        const size_t nw = free_.word_count();
        size_t w = hint_;
        while (w < nw && free_.word(w) == 0) ++w;
        if (__builtin_expect(w == nw, 0)) die_full();
        hint_ = w;
        const uint32_t i = uint32_t((w << 6) + size_t(__builtin_ctzll(free_.word(w))));
        free_.clear(i);
        ++in_use_;
        return i;
    }

    void free(uint32_t i) {
        DCHECK(i < capacity_, "value pool '%s': free(%u) out of range (capacity %u)", name_, i, capacity_);
        DCHECK(!free_.test(i), "value pool '%s': free(%u) of an index not allocated", name_, i);
        free_.set(i);
        --in_use_;
        if ((i >> 6) < hint_) hint_ = i >> 6;
    }

    u128& operator[](uint32_t i) {
        check(i);
        return values_[i];
    }
    const u128& operator[](uint32_t i) const {
        check(i);
        return values_[i];
    }

    bool allocated(uint32_t i) const { return i < capacity_ && !free_.test(i); }
    uint32_t in_use() const { return in_use_; }
    uint32_t capacity() const { return capacity_; }
    const char* name() const { return name_; }

private:
    void check(uint32_t i) const {
        DCHECK(i < capacity_, "value pool '%s': index %u out of range (capacity %u)", name_, i, capacity_);
        DCHECK(!free_.test(i), "value pool '%s': index %u is not allocated", name_, i);
    }
    [[noreturn]] __attribute__((cold, noinline)) void die_full() const {
        FATAL("value pool '%s' full: capacity %u", name_, capacity_);
    }

    u128* values_ = nullptr;
    Bitmap free_;
    uint32_t capacity_ = 0, in_use_ = 0;
    size_t hint_ = 0;
    const char* name_ = "";
};

}  // namespace sim
