// SlotPool: a thread's pool of fixed-size slots of N words, with its free indices (generalising the ValuePool for
// payloads passed by reference: sim.h, PAYLOADS AND WIDTHS). A payload too wide to travel inline travels as an index into
// the pool of its size class. The capacity and the slot size are set at init and never change: alloc when none is free
// is fatal in every build (naming the pool); freeing an index out of range or not allocated is DCHECK.
//
// ValuePool: the 2-word (128-bit) view of a SlotPool, the original interface (values as `u128`), kept for its users and
// tests.
//
// Free slots are a Bitmap (1 = free). alloc returns the lowest free index, so allocation is deterministic; a hint (the
// lowest word that may hold a free bit) keeps it from rescanning from 0.
//
// Storage layout: the slots (N words each, from the start of the storage, so line-aligned), then the bitmap on its own
// lines. Cycle-loop code: no allocation, no STL, header-only.
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/bitmap.h"
#include "base/fatal.h"
#include "base/words.h"

namespace sim {

using u128 = unsigned __int128;

class SlotPool {
public:
    static size_t words_needed(unsigned slot_words, uint32_t capacity) {
        return round_to_lines(size_t(slot_words) * capacity) + Bitmap::words_needed(capacity);
    }

    // Starts with every index free; the slots are not cleared. `kind` names the pool in messages ("slot pool").
    void init(uint64_t* storage, unsigned slot_words, uint32_t capacity, const char* name, const char* kind = "slot pool") {
        SIM_CHECK_STORAGE(storage, kind, name);
        if (slot_words == 0) FATAL("%s '%s': slots of 0 words", kind, name);
        words_ = storage;
        slot_words_ = slot_words;
        free_.init(storage + round_to_lines(size_t(slot_words) * capacity), capacity, name);
        free_.set_all();
        capacity_ = capacity;
        in_use_ = 0;
        hint_ = 0;
        name_ = name;
        kind_ = kind;
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
        DCHECK(i < capacity_, "%s '%s': free(%u) out of range (capacity %u)", kind_, name_, i, capacity_);
        DCHECK(!free_.test(i), "%s '%s': free(%u) of an index not allocated", kind_, name_, i);
        free_.set(i);
        --in_use_;
        if ((i >> 6) < hint_) hint_ = i >> 6;
    }

    uint64_t* slot(uint32_t i) {
        check(i);
        return words_ + size_t(i) * slot_words_;
    }
    const uint64_t* slot(uint32_t i) const {
        check(i);
        return words_ + size_t(i) * slot_words_;
    }

    bool allocated(uint32_t i) const { return i < capacity_ && !free_.test(i); }
    uint32_t in_use() const { return in_use_; }
    uint32_t capacity() const { return capacity_; }
    unsigned slot_words() const { return slot_words_; }
    const char* name() const { return name_; }

private:
    void check(uint32_t i) const {
        DCHECK(i < capacity_, "%s '%s': index %u out of range (capacity %u)", kind_, name_, i, capacity_);
        DCHECK(!free_.test(i), "%s '%s': index %u is not allocated", kind_, name_, i);
    }
    [[noreturn]] __attribute__((cold, noinline)) void die_full() const {
        FATAL("%s '%s' full: capacity %u (slots of %u words)", kind_, name_, capacity_, slot_words_);
    }

    uint64_t* words_ = nullptr;
    unsigned slot_words_ = 0;
    Bitmap free_;
    uint32_t capacity_ = 0, in_use_ = 0;
    size_t hint_ = 0;
    const char* name_ = "";
    const char* kind_ = "slot pool";
};

class ValuePool {
public:
    static size_t words_needed(uint32_t capacity) { return SlotPool::words_needed(2, capacity); }
    void init(uint64_t* storage, uint32_t capacity, const char* name) { p_.init(storage, 2, capacity, name, "value pool"); }
    uint32_t alloc() { return p_.alloc(); }
    void free(uint32_t i) { p_.free(i); }
    u128& operator[](uint32_t i) { return *reinterpret_cast<u128*>(p_.slot(i)); }
    const u128& operator[](uint32_t i) const { return *reinterpret_cast<const u128*>(p_.slot(i)); }
    bool allocated(uint32_t i) const { return p_.allocated(i); }
    uint32_t in_use() const { return p_.in_use(); }
    uint32_t capacity() const { return p_.capacity(); }
    const char* name() const { return p_.name(); }

private:
    SlotPool p_;
};

}  // namespace sim
