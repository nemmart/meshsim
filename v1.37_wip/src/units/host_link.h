// host_link.h: a packet file of chapter 28's link packets, read packet by packet (v1.37; spec/host-link.md 28.3 to
// 28.6, from stage 4; ../v1.4_wip/planning/HOST-LINK.md before). The chip's receiver is the link's Node
// (link/, in ingress_egress.h); this is the tests' reader of a file of link packets: gold's vectors
// (test_packet_vectors), the packets a proof is sent as (HostEnd::from_packet_file), packetize. No simulator types.
//
// A LINK PACKET is a header word of 128 bits, then its data, 0 to 32 words of 128 bits, every word as 16 bytes on the
// link, the least significant byte first. The header word (28.3):
//   [3:0] stream (1 foreground, 2 background; never 0)   [7:4] flags   [15:8] length, the data's words
//   [33:16] number, 18 bits, one run for the link (28.4)   [51:34] ack, 18 bits   [55:52] hops
//   [56] fg_stop   [57] bg_stop   [63:58] reserved, 0
//   [95:64] the data checksum, over the data as held   [127:96] the header CRC, over the word's first 12 bytes
// Both checks are CRC-32C (Castagnoli): the polynomial 0x1EDC6F41, bits reflected, the register starting at 0xFFFFFFFF
// and its complement taken at the end, over the bytes in the order they go on the link; the ASCII string "123456789"
// gives 0xE3069283, and no bytes give 0.
// THE DATA IS MASKED on the line (28.6): each byte XORed with the low byte of a register started at
// header_crc | 0x10000 and shifted eight times as the CRC-32C register is with a zero bit before each byte.
//
// THE FILE is the link's bytes in order. A zero byte where a header's first byte is expected is idle, and skipped: a
// header's first byte holds the stream and is never 0 (28.7).
//
// THE CHECKS of a link packet, in 28.7's order, the first that fails naming what is wrong:
//   1. the header CRC                                                            -> CRC
//   2. the fields a header cannot have: a stream that is not 1 or 2, a length
//      above 32, a reserved bit                                                  -> FORMAT
//   3. the data checksum, the mask taken off                                     -> CRC
//   (restart) a link packet with `restart` (flags bit 3) that is not 28.9's restart link packet: stream 1, length 32,
//      number 1, hops 0, its data the first 32 primes as bytes sixteen times over: damaged, as 28.9 has it     -> CRC
//      The restart link packet is read (`restart` set in the packet) and takes no number; the numbers start again
//      from 1 after it
//   4. the number: the link's next, whatever the stream, 1 first and 1 again
//      after 0x3FFFE. A link packet of length 0 takes no number: its field holds
//      the number the next link packet with data will carry                      -> SEQUENCE
// The flags, the ack field, the hops field and the stop bits are not tested: no value of them is an error; they are
// read, and kept in the packet.
#pragma once

#include <cstddef>
#include <cstdint>

namespace units::hostlink {

constexpr unsigned MAX_WORDS = 32;                 // a packet's data
constexpr unsigned FOREGROUND = 1, BACKGROUND = 2;  // the streams' numbers on the link
constexpr uint32_t FIRST_NUMBER = 1, LAST_NUMBER = 0x3FFFE;

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
    unsigned hops = 0, stops = 0;                  // stops: fg_stop + 2 bg_stop
    bool restart = false;                          // 28.9's restart link packet: its data goes to no queue
    uint64_t lo[MAX_WORDS] = {}, hi[MAX_WORDS] = {};   // the data's words
};

enum class Check : uint8_t { OK, CRC, FORMAT, SEQUENCE };

// A packet file, read packet by packet.
struct Reader {
    const uint8_t* bytes = nullptr;
    size_t size = 0, at = 0;
    uint32_t expect = FIRST_NUMBER;                      // the link's next number, both streams (28.4)
    size_t header_at = 0;                                // where the last packet's header was taken to start

    enum class Next : uint8_t { PACKET, END, CUT };
    void open(const uint8_t* b, size_t n) { bytes = b; size = n; rewind(); }
    void rewind() { at = 0; expect = FIRST_NUMBER; }

    // The file's next packet: PACKET, with its check's result and the bytes it took on the link, the idle zeros before
    // it included (after a check that failed nothing more is to be read: the link has lost its place); END at the
    // file's end; CUT if the file ends inside a packet, which then never arrives.
    Next next(Packet& p, Check& check, size_t& taken) {
        const size_t from = at;
        while (at < size && bytes[at] == 0) at++;        // idle
        taken = at - from;
        if (at == size) return Next::END;
        header_at = at;
        if (size - at < 16) { at = size; return Next::CUT; }
        const uint8_t* h = bytes + at;
        auto u32 = [&](unsigned i) { return uint32_t(h[i]) | uint32_t(h[i + 1]) << 8 | uint32_t(h[i + 2]) << 16 | uint32_t(h[i + 3]) << 24; };
        const uint64_t low = uint64_t(u32(0)) | uint64_t(u32(4)) << 32;
        p.stream = unsigned(low & 15); p.flags = unsigned(low >> 4 & 15); p.length = unsigned(low >> 8 & 0xFF);
        p.number = uint32_t(low >> 16 & 0x3FFFF); p.acknowledge = uint32_t(low >> 34 & 0x3FFFF); p.hops = unsigned(low >> 52 & 15);
        p.stops = unsigned(low >> 56 & 3);
        at += 16; taken += 16;
        check = Check::OK;
        if (crc32c(h, 12) != u32(12)) { check = Check::CRC; return Next::PACKET; }
        if ((p.stream != FOREGROUND && p.stream != BACKGROUND) || p.length > MAX_WORDS || (low >> 58) != 0) { check = Check::FORMAT; return Next::PACKET; }
        const size_t data = size_t(p.length) * 16;
        if (size - at < data) { at = size; return Next::CUT; }
        uint8_t d[MAX_WORDS * 16];
        uint32_t reg = u32(12) | 0x10000u;               // the mask (28.6)
        for (size_t i = 0; i < data; i++) {
            for (int k = 0; k < 8; k++) reg = reg >> 1 ^ (reg & 1 ? 0x82F63B78u : 0);
            d[i] = uint8_t(bytes[at + i] ^ reg);
        }
        at += data; taken += data;
        if (crc32c(d, data) != u32(8)) { check = Check::CRC; return Next::PACKET; }
        for (unsigned w = 0; w < p.length; w++) {
            uint64_t v[2] = {0, 0};
            for (unsigned b = 0; b < 16; b++) v[b / 8] |= uint64_t(d[16 * w + b]) << (8 * (b % 8));
            p.lo[w] = v[0]; p.hi[w] = v[1];
        }
        p.restart = (p.flags & 8) != 0;
        if (p.restart) {                                 // 28.9: the pattern, or damaged
            static const uint8_t primes[32] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
                                               59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131};
            bool pattern = p.stream == FOREGROUND && p.length == MAX_WORDS && p.number == FIRST_NUMBER && p.hops == 0;
            for (size_t i = 0; pattern && i < data; i++) pattern = d[i] == primes[i % 32];
            if (!pattern) { check = Check::CRC; return Next::PACKET; }
            expect = FIRST_NUMBER;                       // it takes no number; the numbers start again
            return Next::PACKET;
        }
        if (p.number != expect) { check = Check::SEQUENCE; return Next::PACKET; }
        if (p.length > 0) expect = expect == LAST_NUMBER ? FIRST_NUMBER : expect + 1;
        return Next::PACKET;
    }
};

}  // namespace units::hostlink
