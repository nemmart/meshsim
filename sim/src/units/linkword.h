// linkword.h: the link word's helpers G1's units need beyond the `net.h` (sim/ref/meshsim/src/LinkWord.java).
// net.h's `units::lw` is the link word's definition (agreed 2026-11-07); this file adds to the same namespace only what
// net.h doesn't have, so a unit includes this one and sees both.
#pragma once

#include <cstdint>

#include "units/net.h"

namespace units::lw {

enum : unsigned { N_MODES = 8, N_COUNTERS = 7, R_REGS = 0xFF };   // counter field 0: uncounted; 1-7 WRITE_COUNTER_1-7
constexpr bool counted_mode(unsigned m) { return m <= M_AUG || m == M_COUNT; }
constexpr bool read_port_tag(uint64_t h) { return (h & TAG_READPORT) != 0; }
constexpr unsigned region(uint32_t a) { return a >> 24; }
constexpr uint32_t offset(uint32_t a) { return a & 0xFFFFFF; }
constexpr uint32_t sram_addr(uint32_t chunk) { return uint32_t(R_SRAM) << 24 | (chunk & 0x1FFFFF) << 2; }
constexpr unsigned chunks(unsigned size) { return 1u << size; }   // 1, 2, 4 (8 for the undefined code)
constexpr unsigned bytes(unsigned size) { return 4u << size; }    // 4, 8, 16
constexpr bool aligned(uint32_t a, unsigned size) { return size == SIZE_UNDEF || (a & (bytes(size) - 1)) == 0; }

// A bad packet (3.1, LinkWord.badPacket): `periph_present` is the receiver's PARAMS [2:0]. Size 11 is undefined, not bad.
constexpr bool bad_packet(unsigned mode, unsigned size, uint32_t a, uint32_t periph_present) {
    if (mode == M_AUG) return size == SIZE_128;
    if (mode != M_WRITE) return false;
    if (!aligned(a, size)) return true;
    unsigned r = region(a);
    if (r == R_DMEM || r == R_PMEM || r == R_SRAM) return false;
    if (r >= R_PERIPH1 && r <= R_PERIPH3) return (periph_present >> (r - R_PERIPH1) & 1) == 0;
    return true;   // 0x02, 0x07-0xFE, 0xFF
}

}  // namespace units::lw
