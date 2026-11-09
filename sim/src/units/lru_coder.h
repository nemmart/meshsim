// lru_coder.h: the Tracer's LRU stream encoder and its packer (spec 4.21.6; 4.19.15; DECISIONS-v1.3 sections 3, 4; item
// 151), ported from `ref`'s LruCoder.java (WORKER14). The exact inverse of the replayer's decoder: a 16-entry table, all
// zeros at a window's start (reset).
//   - code and read log: a true LRU of 16 entries; the write log: 15 LRU entries and E[15], the last load, set by every
//     load word (tag 0) in row order (last_load), as the decoder does;
//   - a hit is coded 1, then its 4-bit index; a value matching several entries takes the lowest matching index, and in
//     the write log a match with E[15] is coded as index 15 first;
//   - a miss is coded 0, a size bit, then the value: 32 bits exactly when its top 32 bits are 0, else 64 (item 151). The
//     table is then updated as the decoder updates it (move to front; a miss pushes at the front and drops the LRU end,
//     E[15] untouched in the write log).
// Bits go least significant first into 128-bit words (Bits). Set 1 (sizing) counts only (`encode(v)`); set 2 emits into a
// packer (`encode(v, pk, out)`), whose full words go to `out(lo, hi)`: a callable, so the cycle loop calls no virtual or
// std::function (D6). Everything here is fixed-size.
#pragma once

#include <cstdint>
#include <cstring>

namespace units {
namespace tracer {

// A packer's bit stream: least significant bit first into 128-bit words (4.19.15).
struct Bits {
    uint64_t lo = 0, hi = 0;  // the word being packed
    int fill = 0;             // its bits
    int64_t words = 0;        // words emitted

    void reset() { lo = hi = 0; fill = 0; words = 0; }

    // Appends the low n bits of v (n <= 64); each full word goes to out(lo, hi).
    template <class Out>
    void put(uint64_t v, int n, Out&& out) {
        while (n > 0) {
            const int take = n < 128 - fill ? n : 128 - fill;
            const uint64_t m = take == 64 ? ~uint64_t(0) : (uint64_t(1) << take) - 1, part = v & m;
            if (fill < 64) {
                lo |= part << fill;
                if (fill + take > 64) hi |= part >> (64 - fill);
            } else {
                hi |= part << (fill - 64);
            }
            fill += take;
            n -= take;
            v = take == 64 ? 0 : v >> take;
            if (fill == 128) {
                out(lo, hi);
                words++;
                lo = hi = 0;
                fill = 0;
            }
        }
    }
    // The stream's end: its last partial word committed (the rest of it padding, zeros).
    template <class Out>
    void finish(Out&& out) {
        if (fill > 0) {
            out(lo, hi);
            words++;
            lo = hi = 0;
            fill = 0;
        }
    }
};

class LruCoder {
public:
    bool is_write = false;
    int64_t bits = 0, hits = 0, misses = 0, values = 0;

    explicit LruCoder(bool write = false) : is_write(write) { reset(); }
    void reset() {
        std::memset(e_, 0, sizeof e_);
        bits = hits = misses = values = 0;
    }
    void last_load(uint64_t v) {
        if (is_write) e_[15] = v;
    }

    // The bits v would take, without coding it (the scan's room check).
    int peek_bits(uint64_t v) const {
        if (is_write && e_[15] == v) return 5;
        const int n = is_write ? 15 : 16;
        for (int i = 0; i < n; i++)
            if (e_[i] == v) return 5;
        return (v >> 32) != 0 ? 66 : 34;
    }

    // Codes v, counting only (set 1); returns the bits it takes.
    int encode(uint64_t v) {
        auto none = [](uint64_t, uint64_t) {};
        return code(v, nullptr, none);
    }
    // Codes v into the packer pk (set 2), full words to out(lo, hi); returns the bits it takes.
    template <class Out>
    int encode(uint64_t v, Bits& pk, Out&& out) {
        return code(v, &pk, out);
    }

    const uint64_t* table() const { return e_; }

private:
    template <class Out>
    int code(uint64_t v, Bits* sink, Out& out) {
        values++;
        int idx = -1;
        if (is_write && e_[15] == v) {
            idx = 15;
        } else {
            const int n = is_write ? 15 : 16;
            for (int i = 0; i < n; i++)
                if (e_[i] == v) {
                    idx = i;
                    break;
                }
        }
        int nb;
        if (idx >= 0) {
            hits++;
            nb = 5;
            if (sink) {
                sink->put(1, 1, out);
                sink->put(uint64_t(idx), 4, out);
            }
            if (!(is_write && idx == 15)) {
                std::memmove(e_ + 1, e_, sizeof(uint64_t) * idx);
                e_[0] = v;
            }
        } else {
            misses++;
            const bool wide = (v >> 32) != 0;
            nb = wide ? 66 : 34;
            if (sink) {
                sink->put(0, 1, out);
                sink->put(wide ? 1 : 0, 1, out);
                sink->put(v, wide ? 64 : 32, out);
            }
            std::memmove(e_ + 1, e_, sizeof(uint64_t) * (is_write ? 14 : 15));
            e_[0] = v;
        }
        bits += nb;
        return nb;
    }

    uint64_t e_[16];
};

}  // namespace tracer
}  // namespace units
