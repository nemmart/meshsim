// header.h: the link's header word (HL, "A packet", as of 2026-11-17: one run of packet numbers for a link), packed
// and unpacked, the restart packet's fixed data, and the packet numbers' arithmetic.
//
//   byte 0       the stream whose data the packet carries, in its low four bits (1 foreground, 2 background; never 0);
//                resend, lost, recovery and restart in bits 4 to 7
//   byte 1       length: the data's 128-bit words, 0 to 32
//   bits 16-33   packet number: this packet's on the link; in a packet of length 0, the next packet with data's
//   bits 34-51   acknowledge: the last packet number received in order on the link; 0 for none yet
//   bits 52-55   hops
//   bit 56, 57   foreground stop, background stop;  bits 58-63 reserved, 0
//   bytes 8-11   the data checksum: the CRC-32C of the data's bytes; of no data, 0
//   bytes 12-15  the header CRC: the CRC-32C of bytes 0 to 11
#pragma once

#include <cstdint>

#include "crc32c.h"

namespace hl {

constexpr unsigned HEADER_BYTES = 16, WORD_BYTES = 16, STREAMS = 2;
constexpr unsigned FG = 0, BG = 1;     // a stream's index here; on the wire the foreground is 1 and the background 2

enum : uint8_t { F_RESEND = 1, F_LOST = 2, F_RECOVERY = 4, F_RESTART = 8 };   // byte 0's bits 4 to 7

struct Header {
    uint8_t stream = 0;      // as on the wire: 1 or 2
    uint8_t flags = 0;
    uint8_t length = 0;      // words
    uint32_t number = 0, ack = 0;
    uint8_t hops = 0;
    bool stop[STREAMS] = {};
    uint8_t reserved = 0;    // bits 58 to 63
    uint32_t dsum = 0;       // the data checksum
};

// The 16 bytes as they go on the link, the header CRC computed.
inline void pack(const Header& h, uint8_t b[HEADER_BYTES]) {
    const uint64_t low = uint64_t(h.stream & 15) | uint64_t(h.flags & 15) << 4 | uint64_t(h.length) << 8 |
                         uint64_t(h.number & 0x3FFFF) << 16 | uint64_t(h.ack & 0x3FFFF) << 34 | uint64_t(h.hops & 15) << 52 |
                         uint64_t(h.stop[FG]) << 56 | uint64_t(h.stop[BG]) << 57 | uint64_t(h.reserved & 63) << 58;
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
    h.number = uint32_t(low >> 16 & 0x3FFFF); h.ack = uint32_t(low >> 34 & 0x3FFFF); h.hops = uint8_t(low >> 52 & 15);
    h.stop[FG] = low >> 56 & 1; h.stop[BG] = low >> 57 & 1; h.reserved = uint8_t(low >> 58);
    h.dsum = uint32_t(b[8]) | uint32_t(b[9]) << 8 | uint32_t(b[10]) << 16 | uint32_t(b[11]) << 24;
    return h;
}

// The restart packet's data (HL, "Starting a link again"): the first 32 primes as bytes, 2 to 131, sixteen times
// over: 32 words. Its CRC-32C is 0xC8981D21.
constexpr unsigned RESTART_WORDS = 32;
constexpr uint32_t RESTART_SUM = 0xC8981D21u;
constexpr uint8_t PRIMES[32] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
                                59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131};
constexpr uint8_t restart_byte(unsigned i) { return PRIMES[i % 32]; }

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
