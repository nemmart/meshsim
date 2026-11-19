// crc32c.h: CRC-32C as HOST-LINK.md ("HL") defines it, a byte a cycle, as the link's CRC units compute it.
// Polynomial 0x1EDC6F41, reflected 0x82F63B78; the register starts at all ones and is complemented at the end.
// "123456789" gives 0xE3069283; no bytes give 0; the 16 bytes of a good header give 0x48674BC7.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hl {

constexpr uint32_t CRC_START = 0xFFFFFFFFu;          // a unit's register before the first byte: all ones, not zero
constexpr uint32_t CRC_GOOD_HEADER = 0x48674BC7u;    // crc_result() over all 16 bytes of a header whose CRC is right

constexpr std::array<uint32_t, 256> crc_table() {
    std::array<uint32_t, 256> t{};
    for (uint32_t b = 0; b < 256; b++) {
        uint32_t c = b;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (c & 1 ? 0x82F63B78u : 0);
        t[b] = c;
    }
    return t;
}
inline constexpr std::array<uint32_t, 256> CRC_TABLE = crc_table();

// One byte into the unit's register.
constexpr uint32_t crc_step(uint32_t reg, uint8_t byte) { return reg >> 8 ^ CRC_TABLE[(reg ^ byte) & 0xFF]; }
constexpr uint32_t crc_result(uint32_t reg) { return ~reg; }

inline uint32_t crc32c(const uint8_t* bytes, size_t n) {
    uint32_t reg = CRC_START;
    for (size_t i = 0; i < n; i++) reg = crc_step(reg, bytes[i]);
    return crc_result(reg);
}

}  // namespace hl
