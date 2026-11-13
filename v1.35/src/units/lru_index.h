// lru_index.h: a fixed-capacity index of 64-bit keys in access order: what `ref`'s
// LinkedHashMap(..., accessOrder = true) does for the Tracer's two caches and its jump LRU. A key's slot (0 .. cap) is
// stable while the key is present, so the caller keeps its values in parallel arrays indexed by slot.
//
//   find(k)    the slot, moved to the most recent end (LinkedHashMap.get); -1 if absent
//   insert(k)  a new key at the most recent end (put of an absent key); the index may hold capacity + 1 keys until the
//              caller evicts the eldest (ref's put, then remove the iterator's first when size() > capacity)
//   eldest()   the least recent slot, -1 if empty; remove(slot); clear()
//   oldest_first(): iteration from the eldest (ref's entrySet order), next(slot)
// Sized once at construction (configuration); the operations allocate nothing (D6). Lookup by open addressing (linear
// probing, backward-shift deletion), order by an intrusive doubly-linked list.
#pragma once

#include <cstdint>

#include "base/fatal.h"

namespace units {
namespace tracer {

class LruIndex {
public:
    LruIndex() = default;
    LruIndex(const LruIndex&) = delete;
    LruIndex& operator=(const LruIndex&) = delete;
    ~LruIndex() { release(); }

    // Configuration: room for `capacity` keys (plus the one a put holds before its eviction).
    void init(int capacity) {
        release();
        if (capacity < 1) FATAL("LruIndex: capacity %d", capacity);
        cap_ = capacity;
        slots_ = capacity + 1;
        hbits_ = 1;
        while ((1 << hbits_) < 4 * slots_) hbits_++;
        key_ = new uint64_t[size_t(slots_)];
        prev_ = new int32_t[size_t(slots_)];
        next_ = new int32_t[size_t(slots_)];
        table_ = new int32_t[size_t(1) << hbits_];
        clear();
    }

    int capacity() const { return cap_; }
    int size() const { return n_; }
    uint64_t key(int slot) const { return key_[slot]; }

    void clear() {
        for (int i = 0; i < (1 << hbits_); i++) table_[i] = -1;
        free_ = -1;
        for (int s = slots_ - 1; s >= 0; s--) {
            next_[s] = free_;
            free_ = s;
        }
        head_ = tail_ = -1;
        n_ = 0;
    }

    // The slot of k without changing the order, or -1.
    int peek(uint64_t k) const {
        for (uint32_t h = hash(k);; h = (h + 1) & mask()) {
            const int s = table_[h];
            if (s < 0) return -1;
            if (key_[s] == k) return s;
        }
    }
    // The slot of k, made the most recent (LinkedHashMap.get in access order), or -1.
    int find(uint64_t k) {
        const int s = peek(k);
        if (s >= 0 && s != tail_) {
            unlink(s);
            link_tail(s);
        }
        return s;
    }
    // A new key (absent: FATAL otherwise) at the most recent end; its slot.
    int insert(uint64_t k) {
        if (free_ < 0) FATAL("LruIndex: full (%d keys): the caller evicts after each insert", n_);
        uint32_t h = hash(k);
        for (;; h = (h + 1) & mask()) {
            const int s = table_[h];
            if (s < 0) break;
            if (key_[s] == k) FATAL("LruIndex: key 0x%llx inserted twice", (unsigned long long)k);
        }
        const int s = free_;
        free_ = next_[s];
        key_[s] = k;
        table_[h] = s;
        link_tail(s);
        n_++;
        return s;
    }
    int eldest() const { return head_; }
    int next(int slot) const { return next_[slot]; }  // toward the most recent; -1 after it
    void remove(int s) {
        // backward-shift deletion from the hash table
        uint32_t i = hash(key_[s]);
        while (table_[i] != s) i = (i + 1) & mask();
        for (uint32_t j = (i + 1) & mask();; j = (j + 1) & mask()) {
            const int t = table_[j];
            if (t < 0) break;
            const uint32_t home = hash(key_[t]);
            // t may move to i if its home is not cyclically in (i, j]
            const bool in_range = i <= j ? (home > i && home <= j) : (home > i || home <= j);
            if (!in_range) {
                table_[i] = t;
                i = j;
            }
        }
        table_[i] = -1;
        unlink(s);
        next_[s] = free_;
        free_ = s;
        n_--;
    }

private:
    uint32_t mask() const { return (uint32_t(1) << hbits_) - 1; }
    uint32_t hash(uint64_t k) const { return uint32_t((k * 0x9E3779B97F4A7C15ull) >> (64 - hbits_)); }
    void unlink(int s) {
        if (prev_[s] >= 0) next_[prev_[s]] = next_[s]; else head_ = next_[s];
        if (next_[s] >= 0) prev_[next_[s]] = prev_[s]; else tail_ = prev_[s];
    }
    void link_tail(int s) {
        prev_[s] = tail_;
        next_[s] = -1;
        if (tail_ >= 0) next_[tail_] = s; else head_ = s;
        tail_ = s;
    }
    void release() {
        delete[] key_;
        delete[] prev_;
        delete[] next_;
        delete[] table_;
        key_ = nullptr;
        prev_ = next_ = table_ = nullptr;
    }

    int cap_ = 0, slots_ = 0, hbits_ = 0, n_ = 0;
    int head_ = -1, tail_ = -1, free_ = -1;
    uint64_t* key_ = nullptr;
    int32_t* prev_ = nullptr;
    int32_t* next_ = nullptr;
    int32_t* table_ = nullptr;
};

}  // namespace tracer
}  // namespace units
