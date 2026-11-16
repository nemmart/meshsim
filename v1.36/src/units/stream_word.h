// stream_word.h: the record's stream word (the 128-bit record word 1), as `ref`'s Replayer.java lays it out (v1.26.1, DECISIONS-v1.26 item 16).
//
// Stream s = 0 code, 1 read log, 2 write log: its 128-bit words as sent (padding included) at [16s+15:16s]; its values
// out at [48+17s+16:48+17s]; compressed at [99+s]; the Tracer's advice values at [124:102] (a replayer ignores them);
// [127:125] spare. `ref`'s StreamSize refuses words above 0xFFFF and values above 0x1FFFF, and streamWord advice above
// 2^22 (IllegalArgumentException): FATAL here.
#pragma once

#include <cstdint>

#include "base/fatal.h"

namespace units {
namespace streamword {

struct Size {
    uint32_t words = 0, values = 0;
    bool compressed = false;
};

constexpr int SW_ADVICE = 102, SW_ADVICE_BITS = 23;
inline int sw_words(int s) { return 16 * s; }
inline int sw_flag(int s) { return 99 + s; }
inline int sw_values(int s) { return 48 + 17 * s; }

// n <= 64 bits of a 128-bit word {lo, hi} from bit pos.
inline uint64_t bits128(uint64_t lo, uint64_t hi, int pos, int n) {
    uint64_t v = 0;
    for (int k = 0; k < n; k++) {
        const int b = pos + k;
        const uint64_t bit = b < 64 ? lo >> b : hi >> (b - 64);
        v |= (bit & 1) << k;
    }
    return v;
}
inline void put128(uint64_t* w, int pos, int n, uint64_t v) {
    for (int k = 0; k < n; k++) {
        const int b = pos + k;
        const uint64_t bit = (v >> k) & 1;
        if (b < 64) w[0] = (w[0] & ~(uint64_t(1) << b)) | bit << b;
        else w[1] = (w[1] & ~(uint64_t(1) << (b - 64))) | bit << (b - 64);
    }
}

inline Size make(int64_t words, int64_t values, bool compressed) {
    if (words < 0 || words > 0xFFFF || values < 0 || values > 0x1FFFF)
        FATAL("stream size %lld words, %lld values", (long long)words, (long long)values);
    Size s;
    s.words = uint32_t(words);
    s.values = uint32_t(values);
    s.compressed = compressed;
    return s;
}

inline uint32_t advice(uint64_t lo, uint64_t hi) { return uint32_t(bits128(lo, hi, SW_ADVICE, SW_ADVICE_BITS)); }

inline void parse(uint64_t lo, uint64_t hi, Size out[3]) {
    for (int s = 0; s < 3; s++)
        out[s] = make(int64_t(bits128(lo, hi, sw_words(s), 16)), int64_t(bits128(lo, hi, sw_values(s), 17)),
                      bits128(lo, hi, sw_flag(s), 1) != 0);
}

// The stream word for three sizes and the advice field (0 to 2^22): w = {lo, hi}.
inline void build(const Size& code, const Size& rlog, const Size& wlog, int64_t advice_values, uint64_t w[2]) {
    if (advice_values < 0 || advice_values > (int64_t(1) << 22))
        FATAL("advice values %lld: 0 to 2^22", (long long)advice_values);
    w[0] = w[1] = 0;
    put128(w, SW_ADVICE, SW_ADVICE_BITS, uint64_t(advice_values));
    const Size* ss[3] = {&code, &rlog, &wlog};
    for (int s = 0; s < 3; s++) {
        put128(w, sw_words(s), 16, ss[s]->words);
        put128(w, sw_values(s), 17, ss[s]->values);
        put128(w, sw_flag(s), 1, ss[s]->compressed ? 1 : 0);
    }
}

}  // namespace streamword
}  // namespace units
