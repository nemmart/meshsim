/* mesh.h -- the sort's view of a Snarky One tile: the port layer's (../PORTING.md), in place of meshsim's generated
 * mesh.h 1.2 (meshsim v1.25, meshsim/sw/mesh.h). Every name of meshsim's mesh.h that the vendored sort, its planner
 * and its split use keeps its meaning here, at the base tile specification's addresses and in its layouts
 * (rtl/tile/base/spec.md; the specification's names in the comments); what cannot keep its meaning by an address and
 * a layout alone is a port helper, which the vendored code calls in place of a store (PORTING.md, Changes).
 *
 * Included by the sort's C, by crt0.S and, through the C preprocessor, by sort.ld: assembly and the link script see
 * only the #defines of plain numbers. cargo xtask (xtask/src/sort_images.rs) defines MESH_SIZE, IMEM_BYTES,
 * DMEM_BYTES and SCRATCHPAD_BYTES as Veryl.toml configures them; there are deliberately no defaults. The map itself
 * is the specification's (5.1, 5.2), fixed. The sort is the sort design's 64-tile, 4 MiB sort (its NT, AREA and
 * buffer placement), so it builds only for that configuration. */
#ifndef MESH_H
#define MESH_H

#if !defined(MESH_SIZE) || !defined(IMEM_BYTES) || !defined(DMEM_BYTES) || !defined(SCRATCHPAD_BYTES)
#error "MESH_SIZE, IMEM_BYTES, DMEM_BYTES and SCRATCHPAD_BYTES must be defined: cargo xtask passes Veryl.toml's"
#endif
#if MESH_SIZE != 8
#error "the sort is the sort design's 64-tile sort: MESH_SIZE must be 8"
#endif
#if SCRATCHPAD_BYTES != 4194304
#error "the sort places its buffers in the sort design's 4 MiB big SRAM: SCRATCHPAD_BYTES must be 4 MiB"
#endif
#if DMEM_BYTES % 16 != 0 || DMEM_BYTES < 8192
#error "DMEM must be whole 16-byte rows, and hold the arguments, the sort's state and the reply block"
#endif

/* ---- The map (5.1): an 8-bit region [31:24] and a 24-bit byte offset; a C pointer is a packet's address ---- */
#define REGION_DMEM        0x00000000   /* DMEM: the core's loads and stores */
#define REGION_PMEM        0x01000000   /* IMEM: fetched only; packets write it */
#define REGION_SRAM        0x03000000   /* the scratchpad: packets only */
#define IMEM_BASE          REGION_PMEM

/* DMEM's layout: its first 16 bytes reserved (6.3); then the image's arguments, one row, which the master tile writes
 * in its load (6.5): ARG_RECORDS, the records this tile holds, and ARG_FORMAT, the run's FMT_*; then the image's data;
 * and at DMEM's top, by the sort's convention (meshsim's CONVENTIONS 2), the reply block, two words per source, and
 * below it the stack, growing down. */
#define DMEM_FIRST         0x00000010   /* the first byte an image may use */
#define ARGS_BASE          DMEM_FIRST   /* the arguments' row */
#define ARG_RECORDS        0            /* word 0: the records this tile holds (the input-load stand-in, port.c) */
#define ARG_FORMAT         1            /* word 1: FMT_KV32 or FMT_K128 (sort.h) */
#define DMEM_DATA          (ARGS_BASE + 16)   /* the image's data, past the arguments' row */
#define REPLY_BASE         (DMEM_BYTES - 0x400)
#define STACK_TOP          REPLY_BASE
#define REPLY_WORD(src)    (REPLY_BASE + 8u * (src))   /* byte address of source src's reply pair */

/* tile ids (4.1): TILE_ID_BITS = 7 at 8x8, its top value none, the one below it broadcast */
#define TILE_COUNT         (MESH_SIZE * MESH_SIZE)
#define DST_CC1            TILE_COUNT          /* the master tile */
#define DST_BROADCAST      126u                /* every compute tile */

/* ---- Region 0xFF (5.2, 5.5): the tile window at 0xFFFF_F800, the tables from 0xFFFF_E000 ---- */
#define TILE_BASE          0xFFFFF800
#define TABLE_BASE         0xFFFFE000
/* the registers assembly reaches, as x0-relative offsets from TILE_BASE (one lw or sw each) */
#define OFF_EVENTS         0x00C
#define OFF_ARRIVE         0x018
#define X0REL(off)         ((off) - 0x800)

/* ---- PARAMS (7.1) ---- */
#define PARAMS_ID(p)       ((p) & 0x7Fu)       /* tile_id */

/* ---- EVENTS, EVENTS_PEEK, EVENTS_MASK (7.2): meshsim's names at this tile's bits ---- */
#define EV_GO              (1u << 0)           /* release */
#define EV_RUN_DONE        (1u << 1)           /* dma_done: DMA_START clears it */
#define EV_CTRS_DONE       (1u << 2)           /* all_counters_done */
#define EV_CTR(n)          (1u << (6 + (n)))   /* counter_n_done, n = 1-7 */
#define EV_RESET_MASK      (EV_GO | EV_CTRS_DONE)

/* ---- The header block (4.1, 5.4), at bits [7:0] of every header register: size [1:0], counter [4:2], mode [7:5];
 * a header register's dst at [14:8] ---- */
#define HDR_SIZE(s)        ((uint32_t)(s) & 0x3u)
#define HDR_CTR(c)         (((uint32_t)(c) & 0x7u) << 2)
#define HDR_MODE(m)        (((uint32_t)(m) & 0x7u) << 5)
#define HDR_DST(d)         (((uint32_t)(d) & 0x7Fu) << 8)

/* sizes (4.1); modes (4.2), meshsim's numbering, the same; counters, the sort's convention (meshsim's CONVENTIONS 5) */
#define SIZE_32            0u
#define SIZE_64            1u
#define SIZE_128           2u
#define MODE_WRITE         0u
#define MODE_SRC           1u   /* APPEND */
#define MODE_BUCKET        2u
#define MODE_AUGMENT       3u
#define MODE_GATHER        4u
#define MODE_COUNT         5u
#define CTR_NONE           0u
#define CTR_SRAM           1u   /* scratchpad writes */
#define CTR_CM             2u   /* DMEM writes: vectors, read-unit results */
#define CTR_REPLY          3u   /* tile-to-tile replies */

/* WRITE_HDR (9): the header block, dst, and no_increment at [15] */
#define ROUTER_NOINC       (1u << 15)
#define ROUTER_HDR_W(dst, mode, ctr)  (HDR_DST(dst) | HDR_MODE(mode) | HDR_CTR(ctr))

/* STREAM (8.3): STREAM_SRC_ADDR [20:0]; STREAM_DEST_ADDR; STREAM_HDR, the header block, dst [14:8] and count [31:15].
 * meshsim's STREAM_SRC_W carries a gap per stream, which DMA_CONFIG holds here, one per transfer: every stream the
 * sort writes has gap 0, and so has every DMA_CONFIG (dma_config). */
#define STREAM_SRC_W(chunk, gap)      ((uint32_t)(chunk) & 0x1FFFFFu)
#define STREAM_COUNT_W(n, size, ctr, mode, d) \
    (HDR_SIZE(size) | HDR_CTR(ctr) | HDR_MODE(mode) | HDR_DST(d) | (((uint32_t)(n) & 0x1FFFFu) << 15))

/* GROUP (8.4): word 0 rotations [17:0], first_stream [25:18], streams - 1 [29:26]; word 1 gap [7:0]. The sort writes
 * a group's R and its other fields apart, as meshsim's two words, low (R) and high (start [7:0], t - 1 [11:8], gap
 * [19:12]): GROUP_LO_R and GROUP_HI build those, and set_group, set_group_hi and set_group_r store them here. */
#define GROUP_R_MASK       0x3FFFFu
#define GROUP_LO_R(r)      ((uint32_t)(r) & GROUP_R_MASK)
#define GROUP_HI(start, t, gap) (((uint32_t)(start) & 0xFFu) | ((((uint32_t)(t) - 1) & 0xFu) << 8) | (((uint32_t)(gap) & 0xFFu) << 12))

/* DMA_CONFIG (8.1): kind [0], start [8:1], count [17:9], gap [20:18]. The sort builds meshsim's DMA_CONFIG, its
 * transfer in the same bits, the bucket and histogram digits at [20:18] and [23:21] (RECEIVE_CONFIG's, 12.1, here) and
 * the gather's k - 1 at [26:24] (GATHER_CONFIG's value_size, 13), which dma_config stores in their registers. */
#define RUN_STREAMS        0u
#define RUN_GROUPS         1u
#define DMA_CFG_RUN(kind, start, count) (((uint32_t)(kind) & 1u) | (((uint32_t)(start) & 0xFFu) << 1) | (((uint32_t)(count) & 0x1FFu) << 9))
#define DMA_CFG_BUCKETDIG(d)  (((uint32_t)(d) & 0x7u) << 18)
#define DMA_CFG_HISTDIG(d)    (((uint32_t)(d) & 0x7u) << 21)
#define DMA_CFG_KM1(k)        (((uint32_t)(k) & 0x7u) << 24)
#define DMA_RUN_MASK       0x3FFFFu
#define READ_PORT_DEPTH    64u   /* READ_QUEUE_DEPTH, the base tile's (rtl/tile/base/README.md) */

/* ---- Requests (11.1, 11.5): word 0's target [9:7] and op [17:10]; the requester is the tree's ---- */
#define REQ_TARGET(t)      (((uint32_t)(t) & 0x7u) << 7)
#define REQ_OP(op)         (((uint32_t)(op) & 0xFFu) << 10)
#define TARGET_CC1         0u
#define REQ_OP_EXIT        2u   /* the master's firmware's (11.5): a tile's report on main's return (port.c) */
/* EXIT's fields, word 0 [31:18]: its reply's counter [20:18] and its DMEM address over 16 [31:21] (11.4) */
#define EXIT_REPLY(addr, ctr) ((((uint32_t)(ctr) & 0x7u) << 18) | ((((uint32_t)(addr) >> 4) & 0x7FFu) << 21))

/* The reset PC, the boot ROM's first byte (5.1, 6.5), to which the image ends by jumping */
#define RESET_PC           0x02000000

#ifndef __ASSEMBLER__
#include <stdint.h>

#define MESH_RO const volatile uint32_t
#define MESH_WO volatile uint32_t
#define MESH_RW volatile uint32_t

/* The tile window (5.5): eight blocks of 16 words. Block 2's words keep meshsim's names (ROUTER_*), and block 5's
 * counters too (WRITE_COUNTER[n - 1] is COUNTER_n). */
typedef struct {
    /* block 0: the tile control unit (7) */
    MESH_RO PARAMS;
    MESH_RO STATUS;
    MESH_RO ERRORS;
    MESH_RO EVENTS;             /* a load consumes what it returns of EVENTS_MASK's events */
    MESH_RO EVENTS_PEEK;
    MESH_RW EVENTS_MASK;
    MESH_WO ARRIVE;
    uint32_t _block_0[9];
    /* block 1: the DMA unit (8) */
    MESH_RW DMA_CONFIG;
    MESH_WO DMA_START;
    uint32_t _block_1[14];
    /* block 2: the write unit (9) */
    MESH_RW ROUTER_HDR;         /* WRITE_HDR */
    MESH_RW ROUTER_ADDR;        /* WRITE_DEST_ADDR */
    MESH_WO ROUTER_SET_W0, ROUTER_SET_W1, ROUTER_SET_W2;       /* WRITE_W0-W2 */
    MESH_WO ROUTER_SEND_32, ROUTER_SEND_64, ROUTER_SEND_128;   /* WRITE_SEND_32, _64, _128 */
    uint32_t _block_2[8];
    /* block 3: the read unit (10) */
    MESH_RW READ_HDR;
    MESH_RW READ_DEST_ADDR;
    MESH_WO READ_SEND;
    MESH_RO READ_FREE;
    uint32_t _block_3[12];
    /* block 4: the request unit (11) */
    MESH_WO REQUEST_OUT_W0, REQUEST_OUT_W1, REQUEST_OUT_W2, REQUEST_SEND;
    MESH_RO REQUEST_IN_W0, REQUEST_IN_W1, REQUEST_IN_W2, REQUEST_RECEIVE;
    uint32_t _block_4[8];
    /* block 5: the receive unit (12) */
    MESH_RW RECEIVE_CONFIG;
    MESH_RW WRITE_COUNTER[7];   /* COUNTER_1-7: a store adds */
    uint32_t _block_5[8];
    /* block 6: the gather unit (13) */
    MESH_RW GATHER_CONFIG;
    uint32_t _block_6[15];
    uint32_t _block_7[16];
} TileRegs;

/* The tables (5.2, 8.3, 8.4, 12.5), in meshsim's order and layout: each a window of 256 words, GROUP two words a
 * group. */
typedef struct {
    MESH_RW HIST[256];
    MESH_RW PTR[256];
    MESH_RW STREAM_SRC[256];    /* STREAM_SRC_ADDR */
    MESH_RW STREAM_DEST[256];   /* STREAM_DEST_ADDR */
    MESH_RW STREAM_COUNT[256];  /* STREAM_HDR */
    MESH_RW GROUP[64][2];
} TableRegs;

_Static_assert(sizeof(TileRegs) == 0x200, "the tile window");
_Static_assert(__builtin_offsetof(TileRegs, ARRIVE) == 0x018 && __builtin_offsetof(TileRegs, DMA_CONFIG) == 0x040 &&
               __builtin_offsetof(TileRegs, ROUTER_HDR) == 0x080 && __builtin_offsetof(TileRegs, READ_HDR) == 0x0C0 &&
               __builtin_offsetof(TileRegs, REQUEST_OUT_W0) == 0x100 && __builtin_offsetof(TileRegs, RECEIVE_CONFIG) == 0x140 &&
               __builtin_offsetof(TileRegs, GATHER_CONFIG) == 0x180, "5.5's offsets");
_Static_assert(__builtin_offsetof(TableRegs, PTR) == 0x400 && __builtin_offsetof(TableRegs, STREAM_COUNT) == 0x1000 &&
               __builtin_offsetof(TableRegs, GROUP) == 0x1400, "5.2's windows");

#define tile          ((volatile TileRegs *)TILE_BASE)
#define tables        ((volatile TableRegs *)TABLE_BASE)
#define ARGS          ((volatile uint32_t *)ARGS_BASE)      /* ARGS[ARG_RECORDS], ARGS[ARG_FORMAT] */
#define REPLY         ((volatile uint32_t *)REPLY_BASE)     /* REPLY[2 src], REPLY[2 src + 1] */

/* ---- helpers: meshsim's, at this tile ---- */
static inline unsigned tile_index(void) { return PARAMS_ID(tile->PARAMS); }
static inline void set_stream(unsigned i, uint32_t src, uint32_t dest, uint32_t count)
    { tables->STREAM_SRC[i] = src; tables->STREAM_DEST[i] = dest; tables->STREAM_COUNT[i] = count; }

/* GROUP[g] from meshsim's two words (GROUP_LO_R, GROUP_HI); its R alone (the planner's HW_GROUP_R); its other fields
 * alone (the sort's high words). A table load takes one cycle (6.4), and the core may load any entry (8.6). */
static inline uint32_t group_fields(uint32_t hi) { return (hi & 0xFFu) << 18 | ((hi >> 8) & 0xFu) << 26; }
static inline void set_group(unsigned g, uint32_t lo, uint32_t hi)
    { tables->GROUP[g][0] = lo | group_fields(hi); tables->GROUP[g][1] = (hi >> 12) & 0xFFu; }
static inline void set_group_hi(unsigned g, uint32_t hi)
    { tables->GROUP[g][0] = (tables->GROUP[g][0] & GROUP_R_MASK) | group_fields(hi); tables->GROUP[g][1] = (hi >> 12) & 0xFFu; }
static inline void set_group_r(unsigned g, uint32_t r)
    { tables->GROUP[g][0] = (tables->GROUP[g][0] & ~GROUP_R_MASK) | GROUP_LO_R(r); }

/* meshsim's DMA_CONFIG, in this tile's three registers: the transfer in DMA_CONFIG, its gap 0; the digits in
 * RECEIVE_CONFIG, bucket_digit [2:0] and histogram_digit [5:3], meshsim's [23:18]; k - 1 in GATHER_CONFIG's
 * value_size [23:21], beside its record_base. Every receive and gather setting is the last DMA_CONFIG's, as in meshsim,
 * whose one register holds them all. Stored only with the tables, the counters and the gather unit idle: after the
 * previous transfer's arrivals and dma_done. */
static inline void dma_config(uint32_t cfg) {
    tile->DMA_CONFIG = cfg & DMA_RUN_MASK;
    tile->RECEIVE_CONFIG = (cfg >> 18) & 0x3Fu;
    tile->GATHER_CONFIG = (tile->GATHER_CONFIG & 0x1FFFFFu) | ((cfg >> 24) & 0x7u) << 21;
}
/* meshsim's DMA_RECORD_BASE_ADDRESS: GATHER_CONFIG's record_base [20:0], its value_size kept */
static inline void record_base(uint32_t chunk) {
    tile->GATHER_CONFIG = (tile->GATHER_CONFIG & ~0x1FFFFFu) | (chunk & 0x1FFFFFu);
}
/* meshsim's SRAM_RD_DEST_ADDR, in this tile's two registers: READ_HDR's size and counter, READ_DEST_ADDR's address */
static inline void read_dest(uint32_t byte_addr, uint32_t size, uint32_t ctr) {
    tile->READ_HDR = HDR_SIZE(size) | HDR_CTR(ctr);
    tile->READ_DEST_ADDR = byte_addr;
}

/* WFI, then a load of EVENTS: returns every pending event and consumes those EVENTS_MASK selects (7.2). At the reset
 * mask it consumes all_counters_done (and a release). Software re-checks after every wake. */
static inline uint32_t wfi(void) {
    uint32_t events;
    __asm__ volatile ("wfi\n\tlw %0, %1(zero)" : "=r"(events) : "i"(X0REL(OFF_EVENTS)) : "memory");
    return events;
}
/* Waits until every event in ev has been seen, consuming them: EVENTS_MASK = ev, then WFI and a load of EVENTS until
 * the loads have returned each; then the reset mask. meshsim's wait_all, without its EVENTS_REQUIRED (7.2: an AND of
 * events is the program's loop). Not between a WFI and a start store. */
static inline uint32_t wait_all(uint32_t ev) {
    tile->EVENTS_MASK = ev;
    uint32_t seen = 0;
    do seen |= wfi() & ev; while (seen != ev);
    tile->EVENTS_MASK = EV_RESET_MASK;
    return seen;
}
/* The barrier (7.3), where every phase ends: ARRIVE and WFI as two back-to-back instructions, the canonical wait,
 * which only the release ends (the arrival gate); then the load of EVENTS, which consumes the release (the reset mask
 * has it). Fixed instructions, the same on every tile: from the release's wake on, every tile runs the same code in
 * the same cycles (6.4, Lockstep). */
static inline void arrive_and_wait(void) {
    uint32_t events;
    __asm__ volatile ("sw %[one], %[arrive](zero)\n\t"
                      "wfi\n\t"
                      "lw %[events], %[events_offset](zero)"
                      : [events] "=&r"(events)
                      : [one] "r"(1u), [arrive] "i"(X0REL(OFF_ARRIVE)), [events_offset] "i"(X0REL(OFF_EVENTS))
                      : "memory");
    (void)events;
}
/* starts the transfer DMA_CONFIG holds; clears EVENTS.dma_done (8.2) */
static inline void start_run(void) { tile->DMA_START = 1; }

/* the write unit (9): the header and the address, then one store per 32 bits, the last store sending */
static inline void send_to(uint32_t hdr, uint32_t addr) { tile->ROUTER_HDR = hdr; tile->ROUTER_ADDR = addr; }
static inline void send32(uint32_t w0) { tile->ROUTER_SEND_32 = w0; }
static inline void send64(uint32_t w1, uint32_t w0) { tile->ROUTER_SET_W0 = w0; tile->ROUTER_SEND_64 = w1; }
/* the request unit (11.1): words 0-2, then the last store sends */
static inline void request_send(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0)
    { tile->REQUEST_OUT_W0 = w0; tile->REQUEST_OUT_W1 = w1; tile->REQUEST_OUT_W2 = w2; tile->REQUEST_SEND = w3; }
#endif /* __ASSEMBLER__ */
#endif /* MESH_H */
