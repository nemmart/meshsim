// Permanent state, allocated once at configuration time (sim/README.md, "Memory"). Not for the cycle loop.
//
// alloc_big(bytes, name): one zeroed, 64-byte aligned allocation of its own; 2 MB aligned with madvise(MADV_HUGEPAGE) from
//   2 MB up (huge pages measured ~10% in WORKER29). For big arrays (memories, tables), so they never sit between a type's
//   objects, and for the blocks the sizing pass carves structures from. free_big() returns it. Failure is fatal.
// Block: a Block of n words from alloc_big, freed when it goes (the convenient owner of a structure's storage).
// TypeArena<T>: all objects of one type, contiguous, capacity fixed at construction; make(args...) constructs the next one
//   in place, fatal when full; destructors run, newest first, when the arena goes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "base/fatal.h"
#include "base/words.h"

namespace sim {

void* alloc_big(size_t bytes, const char* name);
void free_big(void* p);

class Block {
public:
    Block(size_t words, const char* name) : words_(static_cast<uint64_t*>(alloc_big(words * sizeof(uint64_t), name))) {}
    ~Block() { free_big(words_); }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    uint64_t* words() const { return words_; }

private:
    uint64_t* words_;
};

template <typename T>
class TypeArena {
    static_assert(alignof(T) <= kLineBytes, "TypeArena: alignment above a cache line");

public:
    TypeArena(size_t capacity, const char* name)
        : data_(static_cast<T*>(alloc_big((capacity ? capacity : 1) * sizeof(T), name))), capacity_(capacity), name_(name) {}
    ~TypeArena() {
        while (size_ > 0) data_[--size_].~T();
        free_big(data_);
    }
    TypeArena(const TypeArena&) = delete;
    TypeArena& operator=(const TypeArena&) = delete;

    template <typename... A>
    T* make(A&&... args) {
        if (size_ == capacity_) FATAL("arena '%s' full: capacity %zu", name_, capacity_);
        T* p = ::new (static_cast<void*>(data_ + size_)) T(std::forward<A>(args)...);
        ++size_;
        return p;
    }

    T& operator[](size_t i) {
        DCHECK(i < size_, "arena '%s': index %zu with size %zu", name_, i, size_);
        return data_[i];
    }
    const T& operator[](size_t i) const {
        DCHECK(i < size_, "arena '%s': index %zu with size %zu", name_, i, size_);
        return data_[i];
    }
    T* data() { return data_; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    const char* name() const { return name_; }

private:
    T* data_;
    size_t size_ = 0, capacity_;
    const char* name_;
};

}  // namespace sim
