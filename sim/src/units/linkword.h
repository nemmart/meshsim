// linkword.h: the link word's helpers G1's units need (sim/ref/meshsim/src/LinkWord.java; WORKER8). W9's `units/net.h`
// (namespace `units::lw`) is the shared definition of the link word (agreed 2026-11-07); until it reaches main this file
// carries the accessors too, in its own namespace `units::lwx` so both can be included together. When net.h is on main,
// only what net.h lacks stays here (bytes, chunks, aligned, region, offset, bad_packet, counted_mode, N_COUNTERS).
//
// A link word is three untyped words on every bus (D17), in ref's order:
//     w[0] = payload [63:0]      w[1] = the header      w[2] = payload [127:64]
// The header (ref's simulator layout): src [6:0], dst [13:7], mode [16:14], counter [19:17], size [21:20], address [53:22];
// simulator-only bits: the SRAM read queue's token [59:54] with TAG_FROM_SRQ (62), a held loopback token TAG_TOKEN (61),
// a read-port return TAG_READPORT (63); bit 60 spare. They travel with the word and are part of the canonical
// fingerprint (AUDIT-M2 6.2); none changes a routing, landing or counting decision.
#pragma once

#include <cstdint>

namespace units::lwx {

// ---- endpoints (2.1) ----
enum : uint32_t { CC1 = 64, CC2 = 65, CC3 = 66, BROADCAST = 126, NONE = 127, DMA1 = CC1 + 8 };
inline bool controller(uint32_t id) { return id >= CC1 && id <= CC3; }
inline bool dma_source(uint32_t id) { return id >= DMA1 && id <= DMA1 + 2; }
inline uint32_t tile_of(uint32_t id) { return controller(id) ? id - CC1 : dma_source(id) ? id - DMA1 : id; }

// ---- modes, sizes, counters, regions (3.1, 2.2) ----
enum : uint32_t { M_WRITE = 0, M_SRC = 1, M_BUCKET = 2, M_AUG = 3, M_GATHER = 4, M_COUNT = 5, M_RSVD6 = 6, M_RSVD7 = 7,
                  N_MODES = 8 };
inline bool reserved_mode(uint32_t m) { return m >= M_RSVD6; }
inline bool counted_mode(uint32_t m) { return m <= M_AUG || m == M_COUNT; }
enum : uint32_t { SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2, SIZE_UNDEF = 3 };
enum : uint32_t { N_COUNTERS = 7 };   // WRITE_COUNTER_1-7; counter field 0: uncounted
enum : uint32_t { R_DMEM = 0x00, R_PMEM = 0x01, R_BOOTROM = 0x02, R_SRAM = 0x03, R_PERIPH1 = 0x04, R_PERIPH3 = 0x06,
                  R_REGS = 0xFF };
inline uint32_t region(uint32_t addr) { return addr >> 24; }
inline uint32_t offset(uint32_t addr) { return addr & 0xFFFFFF; }
inline uint32_t sram_addr(uint32_t chunk) { return R_SRAM << 24 | (chunk & 0x1FFFFF) << 2; }

// ---- the header word ----
constexpr uint64_t SRC_MASK = 0x7F;
constexpr uint64_t TAG_READPORT = uint64_t(1) << 63, TAG_FROM_SRQ = uint64_t(1) << 62, TAG_TOKEN = uint64_t(1) << 61;
constexpr unsigned TOKEN_SHIFT = 54;
constexpr uint64_t HDR_MASK = (uint64_t(1) << 54) - 1;

inline uint64_t hdr(uint32_t src, uint32_t dst, uint32_t mode, uint32_t counter, uint32_t addr, uint32_t size) {
    return (src & 0x7F) | uint64_t(dst & 0x7F) << 7 | uint64_t(mode & 7) << 14 | uint64_t(counter & 7) << 17
           | uint64_t(size & 3) << 20 | uint64_t(addr) << 22;
}
inline uint32_t src(uint64_t h) { return uint32_t(h & 0x7F); }
inline uint32_t dst(uint64_t h) { return uint32_t(h >> 7 & 0x7F); }
inline uint32_t mode(uint64_t h) { return uint32_t(h >> 14 & 7); }
inline uint32_t counter(uint64_t h) { return uint32_t(h >> 17 & 7); }
inline uint32_t size(uint64_t h) { return uint32_t(h >> 20 & 3); }
inline uint32_t addr(uint64_t h) { return uint32_t(h >> 22); }
inline uint32_t token(uint64_t h) { return uint32_t(h >> TOKEN_SHIFT & 0x3F); }
inline bool read_port_tag(uint64_t h) { return (h & TAG_READPORT) != 0; }
inline uint64_t with_src(uint64_t h, uint32_t s) { return (h & ~SRC_MASK) | (s & 0x7F); }
inline uint64_t with_dst(uint64_t h, uint32_t d) { return (h & ~(uint64_t(0x7F) << 7)) | uint64_t(d & 0x7F) << 7; }

// ---- sizes and alignment ----
inline uint32_t chunks(uint32_t size) { return 1u << size; }   // 1, 2, 4 (8 for the undefined code)
inline uint32_t bytes(uint32_t size) { return 4u << size; }    // 4, 8, 16
inline bool aligned(uint32_t addr, uint32_t size) { return size == SIZE_UNDEF || (addr & (bytes(size) - 1)) == 0; }
inline bool chunk_aligned(uint32_t chunk, uint32_t size) { return size == SIZE_UNDEF || (chunk & (chunks(size) - 1)) == 0; }

// A bad packet (3.1, LinkWord.badPacket): `periph_present` is the receiver's PARAMS [2:0]. Size 11 is undefined, not bad.
inline bool bad_packet(uint32_t mode, uint32_t size, uint32_t addr, uint32_t periph_present) {
    if (mode == M_AUG) return size == SIZE_128;
    if (mode != M_WRITE) return false;
    if (!aligned(addr, size)) return true;
    uint32_t r = region(addr);
    if (r == R_DMEM || r == R_PMEM || r == R_SRAM) return false;
    if (r >= R_PERIPH1 && r <= R_PERIPH3) return (periph_present >> (r - R_PERIPH1) & 1) == 0;
    return true;   // 0x02, 0x07-0xFE, 0xFF
}

// The payload's bits above its size cleared (3.1).
inline uint64_t mask_lo(uint64_t lo, uint32_t size) { return size == SIZE_32 ? lo & 0xFFFFFFFFu : lo; }
inline uint64_t mask_hi(uint64_t hi, uint32_t size) { return size == SIZE_128 || size == SIZE_UNDEF ? hi : 0; }

}  // namespace units::lwx
