// net.cpp: the hops table (ref's Hops.java) and the start skews (Chip's Random(skewSeed)). Configuration only.
#include "units/net.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "base/fatal.h"

namespace units {

// ref's Hops.parse: every "k:[b,a]" after removing '#' comments; then validate.
Hops parse_hops(const char* text) {
    std::string s;
    for (const char* p = text; *p;) {                                    // drop comments
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        s += *p++;
    }
    Hops h;
    bool set[64] = {};
    const char* p = s.c_str();
    while (*p) {                                                         // the pattern (\d+)\s*:\s*\[\s*(-?\d+)\s*,\s*(-?\d+)\s*\]
        if (*p < '0' || *p > '9') { p++; continue; }
        const char* start = p;
        long k = std::strtol(p, const_cast<char**>(&p), 10);
        auto ws = [&] { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++; };
        auto num = [&](long& v) {
            const char* q = p;
            if (*q == '-') q++;
            if (*q < '0' || *q > '9') return false;
            v = std::strtol(p, const_cast<char**>(&p), 10);
            return true;
        };
        long b = 0, a = 0;
        ws(); if (*p != ':') { p = start + 1; continue; } p++; ws();
        if (*p != '[') { p = start + 1; continue; } p++; ws();
        if (!num(b)) { p = start + 1; continue; } ws();
        if (*p != ',') { p = start + 1; continue; } p++; ws();
        if (!num(a)) { p = start + 1; continue; } ws();
        if (*p != ']') { p = start + 1; continue; } p++;
        if (k < 0 || k > 63) FATAL("hops: entry %ld out of range", k);
        h.b[k] = int8_t(b); h.a[k] = int8_t(a); set[k] = true;
    }
    for (int k = 0; k < 64; k++) {                                       // Hops.validate
        if (!set[k]) FATAL("invalid hops table: hops[%d] missing", k);
        int bk = h.b[k], ak = h.a[k];
        if (std::abs(bk) > 4 || std::abs(ak) > 4) FATAL("invalid hops table: hops[%d] exceeds |4|", k);
        if (((8 * ak + bk - k) & 63) != 0) FATAL("invalid hops table: hops[%d] does not satisfy k = 8a + b (mod 64)", k);
        int best = 1 << 30;
        for (int aa = -4; aa <= 4; aa++)
            for (int bb = -4; bb <= 4; bb++)
                if (((8 * aa + bb - k) & 63) == 0 && std::abs(aa) + std::abs(bb) < best) best = std::abs(aa) + std::abs(bb);
        if (std::abs(ak) + std::abs(bk) != best) FATAL("invalid hops table: hops[%d] not minimal", k);
    }
    return h;
}

Hops load_hops(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) FATAL("hops: cannot open %s", path);
    std::string s;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) s.append(buf, n);
    std::fclose(f);
    return parse_hops(s.c_str());
}

// java.util.Random's nextInt(2): (2 * next(31)) >> 31, i.e. the top bit of next(31).
void start_skews(const NetConfig& c, uint8_t out[64]) {
    uint64_t seed = (uint64_t(c.skew_seed) ^ 0x5DEECE66Dull) & ((1ull << 48) - 1);
    for (unsigned i = 0; i < 64; i++) {
        if (!c.skew) { out[i] = 0; continue; }
        seed = (seed * 0x5DEECE66Dull + 0xB) & ((1ull << 48) - 1);
        int32_t next31 = int32_t(seed >> (48 - 31));
        out[i] = uint8_t((int64_t(2) * next31) >> 31);
    }
}

}  // namespace units
