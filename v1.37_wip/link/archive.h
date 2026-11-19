// archive.h: a unit's registers written to bytes and read back, by one function for both. The search keeps a state
// as these bytes and knows it again by their hash.
#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace hl {

struct Writer {
    std::vector<uint8_t>& out;
    template <class T> void operator()(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + sizeof v);
    }
    void bytes(const std::vector<uint8_t>& v, size_t n) { out.insert(out.end(), v.begin(), v.begin() + long(n)); }
};

struct Reader {
    const uint8_t* at;
    template <class T> void operator()(T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        memcpy(&v, at, sizeof v); at += sizeof v;
    }
    void bytes(std::vector<uint8_t>& v, size_t n) { memcpy(v.data(), at, n); at += n; }
};

// A 128-bit hash of bytes: two multiply-and-fold streams with different constants.
struct Hash128 {
    uint64_t a, b;
    bool operator==(const Hash128&) const = default;
};
inline Hash128 hash128(const uint8_t* p, size_t n) {
    auto mix = [](uint64_t h, uint64_t v, uint64_t k) {
        h ^= v * k; h = (h << 29 | h >> 35) * 0x9E3779B97F4A7C15ull; return h ^ h >> 32;
    };
    uint64_t a = 0x243F6A8885A308D3ull ^ n, b = 0x13198A2E03707344ull + n;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t v; memcpy(&v, p + i, 8); a = mix(a, v, 0xC2B2AE3D27D4EB4Full); b = mix(b, v ^ a, 0x165667B19E3779F9ull); }
    uint64_t v = 0; memcpy(&v, p + i, n - i);
    a = mix(a, v, 0xC2B2AE3D27D4EB4Full); b = mix(b, v ^ a, 0x165667B19E3779F9ull);
    a = mix(a, b, 0xFF51AFD7ED558CCDull); b = mix(b, a, 0xC4CEB9FE1A85EC53ull);
    return {a, b};
}

}  // namespace hl
