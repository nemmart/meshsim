// net.h (WORKER9, G2): the link word as ref lays it out, the network's configuration, the hops table. Constants and
// pure functions only; shared read-only with the other groups (W8's director reads the header's fields from here).
//
// THE LINK WORD (D17; ref's LinkWord.java, agreed with W8 2026-11-07): three untyped words on every bus that carries a
// packet, in ref's order:
//     w[0] payload [63:0]
//     w[1] the header: src [6:0], dst [13:7], mode [16:14], counter [19:17], size [21:20], address [53:22];
//          simulator-only bits, carried exactly as ref carries them: token [59:54] with TAG_FROM_SRQ (62), TAG_TOKEN (61),
//          TAG_READPORT (63). Bit 60 spare.
//     w[2] payload [127:64]
// No receiver's routing, landing or counting decision reads the simulator-only bits; the canonical fingerprint does.
#pragma once

#include <cstdint>

namespace units {

namespace lw {
// Endpoints (2.1): 0-63 the tiles, 64-66 C&C 1-3 (at tiles 0-2's fifth ports), 72-74 their DMAs (sources only), 126 the
// broadcast, 127 none.
enum : unsigned { CC1 = 64, CC2 = 65, CC3 = 66, DMA1 = 72, BROADCAST = 126, NONE = 127 };
enum : unsigned { M_WRITE = 0, M_SRC = 1, M_BUCKET = 2, M_AUG = 3, M_GATHER = 4, M_COUNT = 5, M_RSVD6 = 6, M_RSVD7 = 7 };
enum : unsigned { SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2, SIZE_UNDEF = 3 };
enum : unsigned { R_DMEM = 0x00, R_PMEM = 0x01, R_BOOTROM = 0x02, R_SRAM = 0x03, R_PERIPH1 = 0x04, R_PERIPH3 = 0x06 };

constexpr uint64_t SRC_MASK = 0x7F;
constexpr uint64_t TAG_READPORT = 1ull << 63, TAG_FROM_SRQ = 1ull << 62, TAG_TOKEN = 1ull << 61;
constexpr unsigned TOKEN_SHIFT = 54;
constexpr uint64_t HDR_MASK = (1ull << 54) - 1;

constexpr uint64_t hdr(unsigned src, unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size) {
    return (src & 0x7Fu) | uint64_t(dst & 0x7Fu) << 7 | uint64_t(mode & 7u) << 14 | uint64_t(counter & 7u) << 17
           | uint64_t(size & 3u) << 20 | uint64_t(addr) << 22;
}
constexpr unsigned src(uint64_t h) { return unsigned(h & 0x7F); }
constexpr unsigned dst(uint64_t h) { return unsigned(h >> 7 & 0x7F); }
constexpr unsigned mode(uint64_t h) { return unsigned(h >> 14 & 7); }
constexpr unsigned counter(uint64_t h) { return unsigned(h >> 17 & 7); }
constexpr unsigned size(uint64_t h) { return unsigned(h >> 20 & 3); }
constexpr uint32_t addr(uint64_t h) { return uint32_t(h >> 22); }
constexpr unsigned token(uint64_t h) { return unsigned(h >> TOKEN_SHIFT & 0x3F); }
constexpr uint64_t with_src(uint64_t h, unsigned s) { return (h & ~SRC_MASK) | (s & 0x7Fu); }
constexpr bool controller(unsigned id) { return id >= CC1 && id <= CC3; }
constexpr bool dma_source(unsigned id) { return id >= DMA1 && id <= DMA1 + 2; }
constexpr unsigned tile_of(unsigned id) { return controller(id) ? id - CC1 : dma_source(id) ? id - DMA1 : id; }
constexpr bool reserved_mode(unsigned m) { return m >= M_RSVD6; }
constexpr uint64_t mask_lo(uint64_t lo, unsigned size) { return size == SIZE_32 ? lo & 0xFFFFFFFFull : lo; }
constexpr uint64_t mask_hi(uint64_t hi, unsigned size) { return size == SIZE_128 || size == SIZE_UNDEF ? hi : 0; }
}  // namespace lw

// The hops table (spec 4.6; ref's Hops.java): hops[k] = (b_k, a_k), supplied as configuration (sim/ref/meshsim/hops.txt),
// validated as ref validates it (|b|, |a| <= 4; k = 8a + b mod 64; minimal).
struct Hops {
    int8_t b[64] = {}, a[64] = {};
};
// Parses ref's format ("k:[b,a]", '#' comments). Configuration only: fatal (naming the entry) on an invalid table.
Hops parse_hops(const char* text);
Hops load_hops(const char* path);

// The network's and C&C's configuration (ref's Config, the fields G2's units read; the D12 header's keys). Defaults are
// Config.machine()'s.
struct NetConfig {
    unsigned link_latency = 3;          // every link direction (overrides per link: not modelled in M2)
    unsigned queue_depth = 16;          // every router FIFO, C&C's included, and the endpoints' receive FIFOs
    unsigned intra_tile_latency = 1;    // injection -> router, router -> director
    unsigned read_tokens = 64;          // the token pool (4.5)
    int loopback_depth = -1;            // the eject output's loopback FIFO; -1: read_tokens
    bool loopback_last = true;          // the eject selector takes the loopback FIFO last (4.7)
    bool loopback_tokens = true;        // a send-port loopback packet takes a token (4.5)
    unsigned go_latency = 8, ready_latency = 8;
    int error_latency = -1;             // -1: ready_latency (M2: must equal it, one line per tile carries both)
    unsigned cnc_reaction = 1;          // all READY seen -> clear and GO
    bool cnc_go_settle = true;          // C&C sends nothing until GO has reached every tile
    unsigned cnc_rx_depth = 64;         // C&C's receive FIFO
    bool skew = true;                   // start skew: +0 or +1 per tile on GO, from Java's Random(skew_seed)
    int64_t skew_seed = 1;
    // C&C as cores (M3, AUDIT-M3 3.4; ref's Config.ccLinkLatency, the host FIFO's 4, Config.ccDmaDepth)
    unsigned cc_link_latency = 1, cc_host_depth = 4, cc_dma_depth = 4;
    unsigned loopback_fifo_depth() const { return loopback_depth > 0 ? unsigned(loopback_depth) : read_tokens; }
    unsigned error_lat() const { return error_latency >= 0 ? unsigned(error_latency) : ready_latency; }
};

// ref's start skews (Chip: `new Random(skewSeed).nextInt(2)` per tile in order), or all 0 with skew off.
void start_skews(const NetConfig& c, uint8_t out[64]);

// The mesh (Chip.neighbour, arrivalPort): output directions TO_RIGHT 0, TO_LEFT 1, TO_DOWN 2, TO_UP 3; a word leaving
// tile k by direction d arrives at neighbour(k, d) on input arrival_port(d).
inline unsigned neighbour(unsigned k, unsigned d) {
    switch (d) { case 0: return (k + 1) & 63; case 1: return (k + 63) & 63; case 2: return (k + 8) & 63; default: return (k + 56) & 63; }
}
inline unsigned arrival_port(unsigned d) { return d; }   // TO_RIGHT -> FROM_LEFT (0), TO_LEFT -> FROM_RIGHT (1), ... (ref's numbering)

}  // namespace units
