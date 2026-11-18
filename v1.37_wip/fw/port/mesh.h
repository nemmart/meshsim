/* mesh.h -- the port layer: v1.35's firmware interface (meshsim v1.35's generated fw/include/mesh.h, whose names the
 * sort keeps from meshsim v1.25's) on v1.36's tile (spec/machine-spec.md, spec/jolt/; the specification's names in
 * the comments). v1.35's programs include it in place of v1.35's mesh.h, so that each keeps its source: every name
 * they use keeps its meaning here, at v1.36's addresses and in its layouts; what cannot keep its meaning by an address
 * and a layout alone is a helper, which a program calls in place of a store (the sort: sort/PORTING.md, Changes), or
 * is gone, and the program that used it changed (fw/README.md, "What changed"). v1.36's own interface, in v1.36's
 * names, is fw/include/mesh.h; this header repeats none of it but what v1.35's names need.
 *
 * The sort's part is Snarky One's port layer (sort/PORTING.md), unchanged but for its request names, v1.35's.
 * Included by C, by crt0.S and, through the C preprocessor, by the sort's sort.ld: assembly and the link script see
 * only the #defines of plain numbers. fw/build.sh defines MESH_SIZE, IMEM_BYTES, DMEM_BYTES and SCRATCHPAD_BYTES as
 * the configuration it builds for; there are deliberately no defaults. The map itself is the specification's (5.1,
 * 5.2), fixed. The sort is the sort design's 64-tile, 4 MiB sort (its NT, AREA and buffer placement), and so is
 * everything here: it builds only for that configuration. */
#ifndef MESH_H
#define MESH_H

#if !defined(MESH_SIZE) || !defined(IMEM_BYTES) || !defined(DMEM_BYTES) || !defined(SCRATCHPAD_BYTES)
#error "MESH_SIZE, IMEM_BYTES, DMEM_BYTES and SCRATCHPAD_BYTES must be defined: fw/build.sh passes its configuration's"
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
#define REGION_PERIPH(n)   ((0x03 + (n)) << 24)   /* peripheral n = 1-3's region: packets only */
#define IMEM_BASE          REGION_PMEM
#define SRAM_ADDR(chunk)   (REGION_SRAM | ((uint32_t)(chunk) << 2))   /* a packet's address of a scratchpad word */

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

/* tile ids (4.1): TILE_ID_BITS = 7 at 8x8, its top value none, the one below it broadcast; the special tiles (2.2) */
#define TILE_COUNT         (MESH_SIZE * MESH_SIZE)
#define DST_CC1            TILE_COUNT          /* the master tile */
#define DST_CC2            (TILE_COUNT + 1)    /* the memory tile */
#define DST_CC3            (TILE_COUNT + 2)    /* the tracer tile */
#define DST_BROADCAST      126u                /* every compute tile */

/* ---- Region 0xFF (5.2, 5.5): the tile window at 0xFFFF_F800, the tables from 0xFFFF_E000 ---- */
#define TILE_BASE          0xFFFFF800
#define TABLE_BASE         0xFFFFE000
/* the registers assembly reaches, as x0-relative offsets from TILE_BASE (one lw or sw each) */
#define OFF_ERRORS         0x008
#define OFF_EVENTS         0x00C
#define OFF_EVENTS_MASK    0x014
#define OFF_ARRIVE         0x018
#define OFF_REQUEST_SET_W0 0x100
#define OFF_REQUEST_SET_W1 0x104
#define OFF_REQUEST_SET_W2 0x108
#define OFF_REQUEST_SEND   0x10C
#define OFF_WRITE_COUNTER_1 0x144
#define X0REL(off)         ((off) - 0x800)

/* ---- PARAMS (7.1) ---- */
#define PARAMS_ID(p)       ((p) & 0x7Fu)       /* tile_id */

/* ---- STATUS (7.1) ---- */
#define ST_RD_RUNNING      (1u << 1)           /* dma_busy: the DMA unit's transfer (v1.35's address generator's run) */

/* ---- EVENTS, EVENTS_PEEK, EVENTS_MASK (7.2): meshsim's names at this tile's bits ---- */
#define EV_GO              (1u << 0)           /* release */
#define EV_RUN_DONE        (1u << 1)           /* dma_done: DMA_START clears it */
#define EV_CTRS_DONE       (1u << 2)           /* all_counters_done */
#define EV_REQ_PENDING     (1u << 3)           /* request_pending (live; a tile with a receive queue) */
#define EV_PERIPH(n)       (1u << (3 + (n)))   /* peripheral_n, n = 1-3 (live) */
#define EV_CTR(n)          (1u << (6 + (n)))   /* counter_n_done, n = 1-7 */
#define EV_RESET_MASK      (EV_GO | EV_CTRS_DONE)

/* ---- The header block (4.1, 5.4), at bits [7:0] of every header register: size [1:0], counter [4:2], mode [7:5];
 * a header register's dst at [14:8] ---- */
#define HDR_SIZE(s)        ((uint32_t)(s) & 0x3u)
#define HDR_CTR(c)         (((uint32_t)(c) & 0x7u) << 2)
#define HDR_MODE(m)        (((uint32_t)(m) & 0x7u) << 5)
#define HDR_DST(d)         (((uint32_t)(d) & 0x7Fu) << 8)

/* sizes (4.1); modes (4.2), meshsim's numbering, the same; counters, meshsim's convention (v1.35's CONVENTIONS 5) */
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
#define CTR_PERIPH         4u   /* writes to this tile's peripherals */
#define CTR_REQ(k)         (5u + (k))   /* k = 0-2: request slot k's reply and data */
#define CTR_LOAD           7u   /* the boot ROM's load counter (6.5): a load's COUNT is on it */

/* WRITE_HDR (9): the header block, dst, and no_increment at [15] */
#define ROUTER_NOINC       (1u << 15)
#define ROUTER_HDR_W(dst, mode, ctr)  (HDR_DST(dst) | HDR_MODE(mode) | HDR_CTR(ctr))

/* STREAM (8.3): STREAM_SRC_ADDR [20:0]; STREAM_DEST_ADDR; STREAM_HDR, the header block, dst [14:8] and count [31:15].
 * meshsim's STREAM_SRC_W carries a gap per stream, which DMA_CONFIG holds here, one per transfer: every stream the
 * sort writes has gap 0, and so has every DMA_CONFIG (dma_config). A compute tile's dst is relative (0 the tile
 * itself), a special tile's absolute (8.3). */
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
 * the gather's k - 1 at [26:24] (GATHER_CONFIG's value_size, 13), which dma_config stores in their registers. A store
 * of DMA_CONFIG_RUN alone stores the transfer, gap 0. */
#define RUN_STREAMS        0u
#define RUN_GROUPS         1u
#define DMA_CFG_RUN(kind, start, count) (((uint32_t)(kind) & 1u) | (((uint32_t)(start) & 0xFFu) << 1) | (((uint32_t)(count) & 0x1FFu) << 9))
#define DMA_CFG_BUCKETDIG(d)  (((uint32_t)(d) & 0x7u) << 18)
#define DMA_CFG_HISTDIG(d)    (((uint32_t)(d) & 0x7u) << 21)
#define DMA_CFG_KM1(k)        (((uint32_t)(k) & 0x7u) << 24)
#define DMA_RUN_MASK       0x3FFFFu
#define READ_PORT_DEPTH    64u   /* READ_QUEUE_DEPTH, the base tile's (rtl/tile/base/README.md) */

/* ---- A peripheral's window (17.1): its control block, its DMA (17.4) ---- */
/* DMA_HDR: the header block [7:0], its size the DMA's; dst [14:8], absolute; count [26:15]; queue [28:27] */
#define DMA_HDR_W(count, queue, dst, mode, ctr) \
    (HDR_CTR(ctr) | HDR_MODE(mode) | HDR_DST(dst) | (((uint32_t)(count) & 0xFFFu) << 15) | (((uint32_t)(queue) & 0x3u) << 27))
#define PEV_RUN_DONE       (1u << 0)   /* done */
#define PEV_DMA_DONE       (1u << 1)   /* dma_done */
/* the master tile's peripheral 1, Ingress/Egress (17.6), v1.35's C&C 1's peripheral: its events */
#define CC1_EV_ALL_READY   (1u << 2)   /* all_arrived: live, (ARRIVALS & ARRIVE_EXPECT) == ARRIVE_EXPECT, not 0 */
#define CC1_EV_WATCHDOG    (1u << 3)   /* watchdog */
/* the replayer (17.8): its events, its region's parts, its credits, RUN_CONFIG's store_log */
#define RP_EV_CODE         (1u << 2)   /* code_credit */
#define RP_EV_RLOG         (1u << 3)   /* read_log_credit */
#define RP_EV_WLOG         (1u << 4)   /* write_log_credit */
#define RP_EV_EXP          (1u << 5)   /* expansion_credit */
#define RP_EV_NEWEXP       (1u << 6)   /* new_expansion */
#define RP_STATE           (REGION_PERIPH(1) + 0x000000u)   /* the state word: part 0 from its record's word 0 */
#define RP_Q_CODE          (REGION_PERIPH(1) + 0x100000u)   /* the queues' parts: 128-bit packets only */
#define RP_Q_RLOG          (REGION_PERIPH(1) + 0x200000u)
#define RP_Q_WLOG          (REGION_PERIPH(1) + 0x300000u)
#define RP_Q_EXP           (REGION_PERIPH(1) + 0x400000u)
#define RP_HALF_CODE       128u   /* entries a credit (17.8.8) */
#define RP_HALF_LOG        128u
#define RP_HALF_EXP        128u
#define RP_RC_STORE_LOG    (1u << 24)
/* the Tracer (17.9): its events, packed from bit 0 (17.9.1), EXPANSION_TEMPLATE's valid, its region's parts */
#define TR_EV_ADVICE       (1u << 2)   /* advice_credit */
#define TR_EV_EXP          (1u << 3)   /* expansion_credit */
#define TR_EV_NEWEXP       (1u << 4)   /* new_expansion */
#define TR_EV_SVC_STATE    (1u << 5)   /* state_ready; code_ready, read_log_ready and write_log_ready above it */
#define TR_EV_SVC_CODE     (1u << 6)
#define TR_EV_SVC_RLOG     (1u << 7)
#define TR_EV_SVC_WLOG     (1u << 8)
#define TR_TEMPLATE_VALID  (1u << 31)
#define TR_STATE           (REGION_PERIPH(1) + 0x000000u)   /* the record, 68 x 16 bytes; word 0 first */
#define TR_TABLES          (REGION_PERIPH(1) + 0x001000u)   /* the decode table, then the ROM: 768 x 16 bytes */
#define TR_Q_ADVICE        (REGION_PERIPH(1) + 0x200000u)   /* the read queue: the segment's advice */
#define TR_Q_EXP           (REGION_PERIPH(1) + 0x400000u)   /* the expansion queue */
#define TR_HALF            128u        /* entries a credit, every queue (17.9.3, 17.9.6) */

/* ---- Requests (11.1, 11.5): word 0's target [9:7] and op [17:10], the target's fields above; the requester [6:0]
 * the tree's. v1.35's C&C 1 format, REQ_CC1, is v1.36's: the reply's counter [20:18] and an argument [31:21]. ---- */
#define REQ_TARGET(t)      (((uint32_t)(t) & 0x7u) << 7)
#define TARGET_CC1         0u   /* the master tile */
#define TARGET_CC2         1u   /* the memory tile */
#define TARGET_CC3         2u   /* the tracer tile */
#define REQ_CC1(op, ctr, arg) (REQ_TARGET(TARGET_CC1) | (((uint32_t)(op) & 0xFFu) << 10) | (((uint32_t)(ctr) & 0x7u) << 18) | ((uint32_t)(arg) << 21))
#define REQ_OP(w0)         (((w0) >> 10) & 0xFFu)
#define REQ_REPLY_CTR(w0)  (((w0) >> 18) & 0x7u)
#define REQ_ARG(w0)        ((w0) >> 21)
#define REQ_REQUESTOR(w0)  ((w0) & 0x7Fu)
#define REQ_NUMBER(w0)     (((w0) >> 18) & 0xFFu)   /* a REPORT's number (11.5) */
#define REQ_OP_REPORT      1u   /* the master tile's: a report, no reply (11.5) */
#define REQ_OP_RESULT      2u   /* the master's firmware's: a tile's result, its reply at REQ_ARG << 4 on REQ_REPLY_CTR */
#define REQ_OP_LOAD        3u   /* the master tile's: the boot ROM's, the tile waits for its next image (6.5) */
#define REQ_OP_ARMED       1u   /* the tracer tile's firmware's (17.9.10): the tile armed its start and totals counters */
#define REQ_OP_STREAMS_ARMED 2u /*   and its stream counter */

/* The reset PC, the boot ROM's first byte (5.1, 6.5), to which the image ends by jumping */
#define RESET_PC           0x02000000

#ifndef __ASSEMBLER__
#include <stdint.h>

#define MESH_RO const volatile uint32_t
#define MESH_WO volatile uint32_t
#define MESH_RW volatile uint32_t

/* The tile window (5.5): eight blocks of 16 words. Block 2's words keep meshsim's names (ROUTER_*), block 4's
 * (REQUEST_SET_*, REQUEST_GET_*) and block 5's counters too (WRITE_COUNTER[n - 1] is COUNTER_n). */
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
    MESH_WO REQUEST_SET_W0, REQUEST_SET_W1, REQUEST_SET_W2, REQUEST_SEND;     /* REQUEST_OUT_W0-W2, REQUEST_SEND */
    MESH_RO REQUEST_GET_W0, REQUEST_GET_W1, REQUEST_GET_W2, REQUEST_RECEIVE;  /* REQUEST_IN_W0-W2, REQUEST_RECEIVE */
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

/* A peripheral's control block and DMA (17.1, 17.4), as every peripheral's window begins; v1.35's names: PULSE_RUN
 * is START, DMA_ADDR is DMA_DEST_ADDR, PULSE_DMA is DMA_START. */
#define PERIPHERAL_STD \
    MESH_RO PARAMS; MESH_RO STATUS; MESH_RO ERRORS; MESH_RO EVENTS; MESH_RO EVENTS_PEEK; MESH_RW EVENTS_MASK; \
    MESH_WO PULSE_RUN; uint32_t _block_0[9]; \
    MESH_RW DMA_HDR; MESH_RW DMA_ADDR; MESH_WO PULSE_DMA; uint32_t _block_1[13];

/* the master tile's Ingress/Egress (17.6), v1.35's C&C 1's peripheral: READY is ARRIVALS, READY_EXPECT ARRIVE_EXPECT,
 * PULSE_GO RELEASE. v1.37 (17.6.1): UART_TX and UART_RX in block 1, beside the DMA's words, and UART_CONTROL at 42 in
 * place of v1.36's UART_DIVISOR and UART_STATUS (the UART's levels are in STATUS); the programs that used those
 * changed with it (cc/cc1.c). */
typedef struct {
    MESH_RO PARAMS; MESH_RO STATUS; MESH_RO ERRORS; MESH_RO EVENTS; MESH_RO EVENTS_PEEK; MESH_RW EVENTS_MASK;
    MESH_WO PULSE_RUN; uint32_t _block_0[9];
    MESH_RW DMA_HDR; MESH_RW DMA_ADDR; MESH_WO PULSE_DMA; uint32_t _block_1[11];
    MESH_WO UART_TX;
    MESH_RO UART_RX;
    MESH_RO ERROR_LINES[3];
    MESH_RO READY[2];
    MESH_RW READY_EXPECT[2];
    MESH_WO PULSE_GO;
    MESH_RW WATCHDOG;
    MESH_RW LED;
    MESH_RW UART_CONTROL;
} Cnc1Regs;

/* the replayer (17.8.1) */
typedef struct {
    PERIPHERAL_STD
    MESH_RW RUN_CONFIG;
    MESH_RO ROWS_RUN;
    MESH_RO EXPANSION_OFFSET;
    MESH_RO EXPANSION_OPCODE;
} ReplayerRegs;

/* the Tracer (17.9.1) */
typedef struct {
    PERIPHERAL_STD
    MESH_RW RUN_CONFIG;
    MESH_RO ROWS_RUN;
    MESH_RO EXPANSION_OFFSET;
    MESH_RO EXPANSION_OPCODE;
    MESH_RO STATE_LENGTH;
    MESH_RO CODE_LENGTH;
    MESH_RO READ_LOG_LENGTH;
    MESH_RO WRITE_LOG_LENGTH;
    MESH_RW HASH_MUL;
    MESH_RW HASH_STEP;
    MESH_RW HASH_LINES;
    MESH_WO EXPANSION_TEMPLATE;
    MESH_RO STAT[8];            /* STAT_CYCLES, then the seven others (17.9.8) */
} TracerRegs;

_Static_assert(sizeof(TileRegs) == 0x200, "the tile window");
_Static_assert(__builtin_offsetof(TileRegs, ARRIVE) == 0x018 && __builtin_offsetof(TileRegs, DMA_CONFIG) == 0x040 &&
               __builtin_offsetof(TileRegs, ROUTER_HDR) == 0x080 && __builtin_offsetof(TileRegs, READ_HDR) == 0x0C0 &&
               __builtin_offsetof(TileRegs, REQUEST_SET_W0) == 0x100 && __builtin_offsetof(TileRegs, RECEIVE_CONFIG) == 0x140 &&
               __builtin_offsetof(TileRegs, GATHER_CONFIG) == 0x180, "5.5's offsets");
_Static_assert(__builtin_offsetof(TableRegs, PTR) == 0x400 && __builtin_offsetof(TableRegs, STREAM_COUNT) == 0x1000 &&
               __builtin_offsetof(TableRegs, GROUP) == 0x1400, "5.2's windows");
_Static_assert(__builtin_offsetof(Cnc1Regs, PULSE_RUN) == 0x018 && __builtin_offsetof(Cnc1Regs, DMA_HDR) == 0x040 &&
               __builtin_offsetof(Cnc1Regs, ERROR_LINES) == 0x080 && __builtin_offsetof(Cnc1Regs, READY_EXPECT) == 0x094 &&
               __builtin_offsetof(Cnc1Regs, PULSE_GO) == 0x09C && __builtin_offsetof(Cnc1Regs, UART_TX) == 0x078 &&
               __builtin_offsetof(Cnc1Regs, UART_CONTROL) == 0x0A8,
               "17.1's and 17.6's offsets");
_Static_assert(__builtin_offsetof(ReplayerRegs, EXPANSION_OPCODE) == 0x08C && __builtin_offsetof(TracerRegs, CODE_LENGTH) == 0x094 &&
               __builtin_offsetof(TracerRegs, EXPANSION_TEMPLATE) == 0x0AC && __builtin_offsetof(TracerRegs, STAT) == 0x0B0,
               "17.8.1's and 17.9.1's offsets");

#define tile          ((volatile TileRegs *)TILE_BASE)
#define tables        ((volatile TableRegs *)TABLE_BASE)
#define cc1           ((volatile Cnc1Regs *)0xFFFFFA00u)       /* peripheral 1 on the master tile */
#define replayer      ((volatile ReplayerRegs *)0xFFFFFA00u)   /* peripheral 1 on a compute tile */
#define tracer        ((volatile TracerRegs *)0xFFFFFA00u)     /* peripheral 1 on the tracer tile */
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
/* v1.35's wait for a command (pulse done, WFI, GO) is v1.36's barrier: there are no commands (6.5, 7.3), and the
 * command word it returned is gone (0). The caller's EVENTS_MASK must hold the release: the reset mask. */
static inline uint32_t wait_for_command(void) { arrive_and_wait(); return 0; }
/* starts the transfer DMA_CONFIG holds; clears EVENTS.dma_done (8.2) */
static inline void start_run(void) { tile->DMA_START = 1; }
/* STATUS.dma_busy: a level a helper may poll when it can't know what ran before */
static inline void wait_rd_idle(void) { while (tile->STATUS & ST_RD_RUNNING) ; }

/* the write unit (9): the header and the address, then one store per 32 bits, the last store sending */
static inline void send_to(uint32_t hdr, uint32_t addr) { tile->ROUTER_HDR = hdr; tile->ROUTER_ADDR = addr; }
static inline void send32(uint32_t w0) { tile->ROUTER_SEND_32 = w0; }
static inline void send64(uint32_t w1, uint32_t w0) { tile->ROUTER_SET_W0 = w0; tile->ROUTER_SEND_64 = w1; }
static inline void send128(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0)
    { tile->ROUTER_SET_W0 = w0; tile->ROUTER_SET_W1 = w1; tile->ROUTER_SET_W2 = w2; tile->ROUTER_SEND_128 = w3; }
/* the request unit (11.1): words 0-2, then the last store sends */
static inline void request_send(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0)
    { tile->REQUEST_SET_W0 = w0; tile->REQUEST_SET_W1 = w1; tile->REQUEST_SET_W2 = w2; tile->REQUEST_SEND = w3; }

/* the cycle counter (6.2): the low 32 bits, for bounded waits */
static inline uint32_t mcycle(void) { uint32_t c; __asm__ volatile ("csrr %0, mcycle" : "=r"(c)); return c; }

/* v1.35's simulator-only exit word is gone (5.5): a program that ends the run, the master's, writes its value and the
 * word EXIT_DONE into sim_exit_word (crt0.S), which the test reads, and parks: mie 0, WFI for good. */
#define EXIT_DONE          0x45584954u   /* "EXIT" */
extern volatile uint32_t sim_exit_word[2];   /* [0] EXIT_DONE once [1], the value, is written */
static inline __attribute__((noreturn)) void sim_exit(uint32_t v) {
    sim_exit_word[1] = v;
    sim_exit_word[0] = EXIT_DONE;
    __asm__ volatile ("csrw mie, zero" ::: "memory");
    for (;;) __asm__ volatile ("wfi" ::: "memory");
}
#endif /* __ASSEMBLER__ */
#endif /* MESH_H */
