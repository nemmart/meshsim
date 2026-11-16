// host_link.h: the link from the host, at the receiver (v1.37; ../v1.4_wip/planning/HOST-LINK.md, "HL"): its packets,
// their two checks, and a packet file read as Ingress's black box reads it (INGRESS-INTERFACE.md, "In v1.37: a black
// box and a file"). No simulator types: a test or a tool uses it alone.
//
// A PACKET (HL) is a header word of 128 bits, then its data, 0 to 32 words of 128 bits, every word as 16 bytes on the
// link, the least significant byte first. The header word:
//   [3:0] stream (1 foreground, 2 background; never 0)   [7:4] flags   [15:8] length, the data's words
//   [35:16] packet number   [55:36] acknowledge   [63:56] reserved, 0
//   [95:64] the data checksum, over the data's bytes   [127:96] the header CRC, over bits [95:0], the word's first 12 bytes
// Both checks are CRC-32C (Castagnoli): the polynomial 0x1EDC6F41, bits reflected, the register starting at 0xFFFFFFFF
// and its complement taken at the end, over the bytes in the order they go on the link; the ASCII string "123456789"
// gives 0xE3069283, and no bytes give 0.
//
// THE FILE is the link's bytes in order. A zero byte where a header's first byte is expected is idle, and skipped: a
// header's first byte holds the stream and is never 0 (HL, "Why the zeros work").
//
// THE CHECKS of a packet, in HL's order, the first that fails naming what is wrong:
//   1. the header CRC                                                            -> CRC
//   2. the fields a header cannot have: a stream that is not 1 or 2, a length
//      above 32, a reserved bit                                                  -> FORMAT
//   3. the data checksum                                                         -> CRC
//   4. the packet number: the next one of its stream, 1 first and 1 again after
//      0xFFFFE. A packet of length 0 takes no number: its field holds the number
//      the stream's next packet with data will carry (owner, 2026-11-16)         -> SEQUENCE
// The flags and the acknowledge field need the way back to the host, and are not looked at.
#pragma once

#include <cstddef>
#include <cstdint>

namespace units::hostlink {

constexpr unsigned MAX_WORDS = 32;                 // a packet's data
constexpr unsigned FOREGROUND = 1, BACKGROUND = 2;  // the streams' numbers on the link
constexpr uint32_t FIRST_NUMBER = 1, LAST_NUMBER = 0xFFFFE;

// CRC-32C of n bytes (above).
inline uint32_t crc32c(const uint8_t* bytes, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= bytes[i];
        for (int k = 0; k < 8; k++) crc = crc >> 1 ^ (crc & 1 ? 0x82F63B78u : 0);   // the polynomial, reflected
    }
    return ~crc;
}

struct Packet {
    unsigned stream = 0, flags = 0, length = 0;    // stream: 1 or 2
    uint32_t number = 0, acknowledge = 0;
    uint64_t lo[MAX_WORDS] = {}, hi[MAX_WORDS] = {};   // the data's words
};

enum class Check : uint8_t { OK, CRC, FORMAT, SEQUENCE };

// A packet file, read packet by packet.
struct Reader {
    const uint8_t* bytes = nullptr;
    size_t size = 0, at = 0;
    uint32_t expect[2] = {FIRST_NUMBER, FIRST_NUMBER};   // each stream's next packet number (foreground, background)

    enum class Next : uint8_t { PACKET, END, CUT };
    void open(const uint8_t* b, size_t n) { bytes = b; size = n; rewind(); }
    void rewind() { at = 0; expect[0] = expect[1] = FIRST_NUMBER; }

    // The file's next packet: PACKET, with its check's result and the bytes it took on the link, the idle zeros before
    // it included (after a check that failed nothing more is to be read: the link has lost its place); END at the
    // file's end; CUT if the file ends inside a packet, which then never arrives.
    Next next(Packet& p, Check& check, size_t& taken) {
        const size_t from = at;
        while (at < size && bytes[at] == 0) at++;        // idle
        taken = at - from;
        if (at == size) return Next::END;
        if (size - at < 16) { at = size; return Next::CUT; }
        const uint8_t* h = bytes + at;
        auto u32 = [&](unsigned i) { return uint32_t(h[i]) | uint32_t(h[i + 1]) << 8 | uint32_t(h[i + 2]) << 16 | uint32_t(h[i + 3]) << 24; };
        const uint64_t low = uint64_t(u32(0)) | uint64_t(u32(4)) << 32;
        p.stream = unsigned(low & 15); p.flags = unsigned(low >> 4 & 15); p.length = unsigned(low >> 8 & 0xFF);
        p.number = uint32_t(low >> 16 & 0xFFFFF); p.acknowledge = uint32_t(low >> 36 & 0xFFFFF);
        at += 16; taken += 16;
        check = Check::OK;
        if (crc32c(h, 12) != u32(12)) { check = Check::CRC; return Next::PACKET; }
        if ((p.stream != FOREGROUND && p.stream != BACKGROUND) || p.length > MAX_WORDS || (low >> 56) != 0) { check = Check::FORMAT; return Next::PACKET; }
        const size_t data = size_t(p.length) * 16;
        if (size - at < data) { at = size; return Next::CUT; }
        const uint8_t* d = bytes + at;
        at += data; taken += data;
        if (crc32c(d, data) != u32(8)) { check = Check::CRC; return Next::PACKET; }
        for (unsigned w = 0; w < p.length; w++) {
            uint64_t v[2] = {0, 0};
            for (unsigned b = 0; b < 16; b++) v[b / 8] |= uint64_t(d[16 * w + b]) << (8 * (b % 8));
            p.lo[w] = v[0]; p.hi[w] = v[1];
        }
        uint32_t& want = expect[p.stream - 1];
        if (p.number != want) { check = Check::SEQUENCE; return Next::PACKET; }
        if (p.length > 0) want = want == LAST_NUMBER ? FIRST_NUMBER : want + 1;
        return Next::PACKET;
    }
};

}  // namespace units::hostlink
