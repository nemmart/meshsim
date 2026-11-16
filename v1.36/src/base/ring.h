// Ring: a FIFO of fixed-width slots of 64-bit words. Width (words per slot) and depth (slots) are set at init, from a
// declaration, and never change. Full is fatal in every build; misuse (peek or pop past the count) is DCHECK.
//
// Cycle-loop code: no allocation, no STL, no virtual calls, header-only. The words are untyped; the ring never looks at
// them. A later fixed-width form would keep this exact set of methods, so callers don't change.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "base/fatal.h"
#include "base/words.h"

namespace sim {

class Ring {
public:
    static size_t words_needed(unsigned width, unsigned depth) { return round_to_lines(size_t(width) * depth); }

    void init(uint64_t* storage, unsigned width, unsigned depth, const char* name) {
        SIM_CHECK_STORAGE(storage, "ring", name);
        if (width == 0 || depth == 0) FATAL("ring '%s': width %u and depth %u must both be at least 1", name, width, depth);
        words_ = storage;
        width_ = width;
        depth_ = depth;
        head_ = 0;
        count_ = 0;
        name_ = name;
    }

    // The slot to fill (the newest); its words are the caller's to write.
    uint64_t* push() {
        if (__builtin_expect(count_ == depth_, 0)) die_full();
        unsigned i = head_ + count_;
        if (i >= depth_) i -= depth_;
        ++count_;
        return words_ + size_t(i) * width_;
    }
    void push(const uint64_t* w) { std::memcpy(push(), w, size_t(width_) * sizeof(uint64_t)); }

    // The i-th oldest slot (0 = the head).
    const uint64_t* peek(unsigned i = 0) const {
        DCHECK(i < count_, "ring '%s': peek(%u) with count %u", name_, i, count_);
        unsigned j = head_ + i;
        if (j >= depth_) j -= depth_;
        return words_ + size_t(j) * width_;
    }
    uint64_t* peek_mut(unsigned i = 0) { return const_cast<uint64_t*>(static_cast<const Ring*>(this)->peek(i)); }

    void pop() {
        DCHECK(count_ > 0, "ring '%s': pop when empty", name_);
        --count_;
        if (++head_ == depth_) head_ = 0;
    }

    unsigned count() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == depth_; }
    unsigned width() const { return width_; }
    unsigned depth() const { return depth_; }
    const char* name() const { return name_; }

private:
    [[noreturn]] __attribute__((cold, noinline)) void die_full() const {
        FATAL("ring '%s' full: capacity %u (width %u)", name_, depth_, width_);
    }

    uint64_t* words_ = nullptr;
    unsigned width_ = 0, depth_ = 0, head_ = 0, count_ = 0;
    const char* name_ = "";  // cold: only for messages
};

}  // namespace sim
