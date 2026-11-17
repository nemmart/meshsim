// header.h: the link's header of 20 bytes (HL, "A packet"; owner, 2026-11-17), packed and unpacked, and the packet
// numbers' arithmetic. Every header says where both streams stand.
//
//   byte 0       the stream whose data the packet carries, in its low four bits (1 foreground, 2 background; never 0);
//                lost, recovery and restart in bits 4, 5 and 6; bit 7 is 0
//   byte 1       length: the data's 128-bit words, 0 to 32
//   bits 16-33   foreground number: in a foreground packet with data, the packet's; in any other, the number the
//                foreground's next packet with data will carry
//   bits 34-51   foreground acknowledge: the last foreground number received in order; 0 for none yet
//   bits 52-69   background number;  bits 70-87  background acknowledge
//   byte 11      hops in its low four bits; then foreground stop, foreground resend, background stop, background resend
//   bytes 12-15  the data checksum: the CRC-32C of the data's bytes; of no data, 0
//   bytes 16-19  the header CRC: the CRC-32C of bytes 0 to 15
#pragma once

#include <cstdint>

#include "crc32c.h"

namespace hl {

constexpr unsigned HEADER_BYTES = 20, WORD_BYTES = 16, STREAMS = 2;
constexpr unsigned FG = 0, BG = 1;     // a stream's index here; on the wire the foreground is 1 and the background 2

enum : uint8_t { F_LOST = 1, F_RECOVERY = 2, F_RESTART = 4, F_RESERVED = 8 };   // byte 0's bits 4 to 7

struct Header {
    uint8_t stream = 0;      // as on the wire: 1 or 2
    uint8_t flags = 0;       // lost, recovery, restart: the link's
    uint8_t length = 0;      // words
    uint8_t hops = 0;
    uint32_t number[STREAMS] = {}, ack[STREAMS] = {};
    bool stop[STREAMS] = {}, resend[STREAMS] = {};
    uint32_t dsum = 0;       // the data checksum
};

// The 20 bytes as they go on the link, the header CRC computed.
inline void pack(const Header& h, uint8_t b[HEADER_BYTES]) {
    using u128 = unsigned __int128;
    const u128 low = u128(h.stream & 15) | u128(h.flags & 15) << 4 | u128(h.length) << 8 |
                     u128(h.number[FG] & 0x3FFFF) << 16 | u128(h.ack[FG] & 0x3FFFF) << 34 |
                     u128(h.number[BG] & 0x3FFFF) << 52 | u128(h.ack[BG] & 0x3FFFF) << 70 | u128(h.hops & 15) << 88 |
                     u128(h.stop[FG]) << 92 | u128(h.resend[FG]) << 93 | u128(h.stop[BG]) << 94 | u128(h.resend[BG]) << 95 |
                     u128(h.dsum) << 96;
    for (unsigned i = 0; i < 16; i++) b[i] = uint8_t(low >> (8 * i));
    const uint32_t crc = crc32c(b, 16);
    for (unsigned i = 0; i < 4; i++) b[16 + i] = uint8_t(crc >> (8 * i));
}

// The fields of 20 bytes. The header CRC is not looked at here.
inline Header unpack(const uint8_t b[HEADER_BYTES]) {
    using u128 = unsigned __int128;
    u128 low = 0;
    for (unsigned i = 0; i < 16; i++) low |= u128(b[i]) << (8 * i);
    Header h;
    h.stream = uint8_t(low & 15); h.flags = uint8_t(low >> 4 & 15); h.length = uint8_t(low >> 8 & 0xFF);
    h.number[FG] = uint32_t(low >> 16 & 0x3FFFF); h.ack[FG] = uint32_t(low >> 34 & 0x3FFFF);
    h.number[BG] = uint32_t(low >> 52 & 0x3FFFF); h.ack[BG] = uint32_t(low >> 70 & 0x3FFFF);
    h.hops = uint8_t(low >> 88 & 15);
    h.stop[FG] = low >> 92 & 1; h.resend[FG] = low >> 93 & 1; h.stop[BG] = low >> 94 & 1; h.resend[BG] = low >> 95 & 1;
    h.dsum = uint32_t(low >> 96);
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
