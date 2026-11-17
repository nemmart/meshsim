// packet_writer.h: a frame file wrapped in the link's packets, for the tests (v1.37; ../v1.4_wip/planning/HOST-LINK.md
// and INGRESS-INTERFACE.md, "In v1.37: a black box and a file").
//
// The packetizer is gold's (../updater_wip/pipeline/gold: `gold packets`, WORKER2's), and gold's vectors and packet
// files are what Ingress's black box (src/units/host_link.h, ingress_egress.h) is checked against
// (tests/test_packet_vectors.cpp). This writer is the tests' own, for a test that makes its packets on the spot from a
// frame file: written from HOST-LINK.md before gold's was there, by the reader's author, so by itself it shows only
// that the two agree with each other. Its output equals gold's, byte for byte, on gold's small.frames and on every
// packet file of the proof steps (pipeline/frames/build.sh requires it). It shares no code with the reader: its
// CRC-32C is table-driven, the reader's bit by bit.
//
// A frame file's packets: every packet on the background stream (2); each frame starts a new packet; a frame's packets
// hold 32 words of data but its last, which holds what remains; packet numbers from 1, and after 0xFFFFE, 1; the
// acknowledge field, the flags and the reserved bits 0.
//
// Test-side code (the configuration regime): the STL throughout.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace packet_writer {

// CRC-32C (Castagnoli): polynomial 0x1EDC6F41 reflected, initial value and final XOR 0xFFFFFFFF.
inline uint32_t crc32c(const uint8_t* bytes, size_t n) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0x82F63B78u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

struct Fields {
    unsigned stream = 2, flags = 0, length = 0, reserved = 0;
    uint32_t number = 1, acknowledge = 0;
};
// A header word's 16 bytes for these fields and this data, both checks right.
inline void header(uint8_t out[16], const Fields& f, const uint8_t* data, size_t data_bytes) {
    const uint64_t low = uint64_t(f.stream & 15) | uint64_t(f.flags & 15) << 4 | uint64_t(f.length & 0xFF) << 8 |
                         uint64_t(f.number & 0xFFFFF) << 16 | uint64_t(f.acknowledge & 0xFFFFF) << 36 | uint64_t(f.reserved & 0xFF) << 56;
    for (unsigned b = 0; b < 8; b++) out[b] = uint8_t(low >> (8 * b));
    const uint32_t check = crc32c(data, data_bytes);
    for (unsigned b = 0; b < 4; b++) out[8 + b] = uint8_t(check >> (8 * b));
    const uint32_t crc = crc32c(out, 12);
    for (unsigned b = 0; b < 4; b++) out[12 + b] = uint8_t(crc >> (8 * b));
}
// One packet appended: `words` 128-bit words of data, 16 bytes each.
inline void packet(std::vector<uint8_t>& out, const Fields& f, const uint8_t* data) {
    uint8_t h[16];
    header(h, f, data, size_t(f.length) * 16);
    out.insert(out.end(), h, h + 16);
    out.insert(out.end(), data, data + size_t(f.length) * 16);
}

struct Options {
    uint32_t first_number = 1;
    unsigned idle_zeros = 0;        // zero bytes before every packet
};
// A frame file's bytes as a packet file's. False and `err` if the frame file is not whole frames.
inline bool packetize(const std::vector<uint8_t>& frames, std::vector<uint8_t>& out, std::string& err, const Options& o = Options()) {
    out.clear();
    uint32_t number = o.first_number;
    for (size_t at = 0; at < frames.size();) {
        if (at + 16 > frames.size()) { err = "a frame's header word past the file's end"; return false; }
        const uint32_t length = uint32_t(frames[at + 12]) | uint32_t(frames[at + 13]) << 8 | uint32_t(frames[at + 14]) << 16 | uint32_t(frames[at + 15]) << 24;
        const size_t words = 1 + size_t(length);                      // the frame's header word and its data
        if (at + 16 * words > frames.size()) { err = "a frame's data past the file's end"; return false; }
        for (size_t w = 0; w < words; w += 32) {
            Fields f;
            f.length = unsigned(words - w < 32 ? words - w : 32);
            f.number = number;
            number = number == 0xFFFFE ? 1 : number + 1;
            out.insert(out.end(), o.idle_zeros, 0);
            packet(out, f, &frames[at + 16 * w]);
        }
        at += 16 * words;
    }
    return true;
}

}  // namespace packet_writer
