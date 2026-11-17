// header.h: the link's header word (HL, "A packet"), packed and unpacked, and the packet numbers' arithmetic.
//
//   byte 0       stream [3:0] (1 foreground, 2 background; never 0), flags [7:4]: resend, lost, recovery, stop
//   byte 1       length: the data's 128-bit words, 0 to 32
//   bytes 2-4    packet number, 18 bits (byte 4's low two bits are its top two)
//   bytes 4-6    acknowledge, 18 bits (byte 4's high six bits are its low six; byte 6's low four its top four)
//   byte 6       hops in its high four bits
//   byte 7       reserved, 0. Bit 0 is the restart flag of QUESTIONS.md (proposed, behind Rules::restart)
//   bytes 8-11   the data checksum: the CRC-32C of the data's bytes; of no data, 0
//   bytes 12-15  the header CRC: the CRC-32C of bytes 0 to 11
#pragma once

#include <cstdint>

#include "crc32c.h"

namespace hl {

constexpr unsigned HEADER_BYTES = 16, WORD_BYTES = 16;

enum : uint8_t { F_RESEND = 1, F_LOST = 2, F_RECOVERY = 4, F_STOP = 8 };   // the flags field's bits, [4] to [7] of the word
constexpr uint8_t R_RESTART = 1;                                         // the reserved byte's bit 0 (proposed)

struct Header {
    uint8_t stream = 0;      // as on the wire: 1 or 2
    uint8_t flags = 0;
    uint8_t length = 0;      // words
    uint32_t number = 0, ack = 0;
    uint8_t hops = 0;
    uint8_t reserved = 0;
    uint32_t dsum = 0;       // the data checksum
};

// The 16 bytes as they go on the link, the header CRC computed.
inline void pack(const Header& h, uint8_t b[HEADER_BYTES]) {
    const uint64_t low = uint64_t(h.stream & 15) | uint64_t(h.flags & 15) << 4 | uint64_t(h.length) << 8 |
                         uint64_t(h.number & 0x3FFFF) << 16 | uint64_t(h.ack & 0x3FFFF) << 34 |
                         uint64_t(h.hops & 15) << 52 | uint64_t(h.reserved) << 56;
    for (unsigned i = 0; i < 8; i++) b[i] = uint8_t(low >> (8 * i));
    for (unsigned i = 0; i < 4; i++) b[8 + i] = uint8_t(h.dsum >> (8 * i));
    const uint32_t crc = crc32c(b, 12);
    for (unsigned i = 0; i < 4; i++) b[12 + i] = uint8_t(crc >> (8 * i));
}

// The fields of 16 bytes. The header CRC is not looked at here.
inline Header unpack(const uint8_t b[HEADER_BYTES]) {
    uint64_t low = 0;
    for (unsigned i = 0; i < 8; i++) low |= uint64_t(b[i]) << (8 * i);
    Header h;
    h.stream = uint8_t(low & 15); h.flags = uint8_t(low >> 4 & 15); h.length = uint8_t(low >> 8 & 0xFF);
    h.number = uint32_t(low >> 16 & 0x3FFFF); h.ack = uint32_t(low >> 34 & 0x3FFFF);
    h.hops = uint8_t(low >> 52 & 15); h.reserved = uint8_t(low >> 56);
    h.dsum = uint32_t(b[8]) | uint32_t(b[9]) << 8 | uint32_t(b[10]) << 16 | uint32_t(b[11]) << 24;
    return h;
}

// Packet numbers run 1 to last, then 1 again (HL: none is 0 or all ones). An acknowledge field of 0 is "none yet".
struct Numbers {
    uint32_t last = 0x3FFFE;
    constexpr uint32_t succ(uint32_t n) const { return n >= last ? 1 : n + 1; }      // succ(0) is 1
    // How many steps forward from a to b, both 1 to last: 0 to last - 1.
    constexpr uint32_t dist(uint32_t a, uint32_t b) const { return b >= a ? b - a : b + last - a; }
    // From an acknowledge value (0 for none yet) forward to another that is not 0.
    constexpr uint32_t dist_ack(uint32_t a, uint32_t b) const { return a == 0 ? b : dist(a, b); }
    // b is after a by no more than half the numbers: a packet missed. The other half is behind: one seen before.
    constexpr bool ahead(uint32_t a, uint32_t b) const { const uint32_t d = dist(a, b); return d >= 1 && d <= last / 2; }
};

}  // namespace hl
