/* mesh-v1.2-draft.h -- WORKER6's draft of what the v1.2 recipes (sw/RECIPES.md) assume. NOT the real header.
 *
 * WORKER4 generates the real mesh.h from the register table and reconciles it with this draft. Every name here
 * follows DECISIONS-v1.2 (cited as Dn); anything the log leaves open is marked (proposed) and listed in
 * WORKER6/REPORT.md. Section 14's rules: one struct per block, reached through a pointer (tile->EVENTS); every
 * member volatile uint32_t, accessed one word at a time and never copied whole (some loads consume).
 *
 * Build: riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32 -std=gnu11 (anonymous unions, D14/W3-27). */
#ifndef MESH_V12_DRAFT_H
#define MESH_V12_DRAFT_H
#define MESH_H_VERSION 0x0102   /* 1.2 draft */

/* ---- the address map (D5): an 8-bit region [31:24] and a 24-bit byte offset [23:0]; a C pointer is the packet address ---- */
#define REGION_DMEM        0x00000000   /* data memory, 32 KB */
#define REGION_PMEM        0x01000000   /* program memory, 32 KB: fetch and load; only packets write it (D5) */
#define REGION_BOOTROM     0x02000000   /* boot ROM, fetch and load; the reset vector (D22, D36) */
#define REGION_SRAM        0x03000000   /* big SRAM */
#define REGION_PERIPH(n)   (0x03000000 + ((n) << 24))   /* peripheral n = 1-3: regions 0x04-0x06 */
#define DMEM_SIZE          0x8000
#define PMEM_SIZE          0x8000
/* a big-SRAM chunk address (the engines' unit, 21 bits) to a packet address (D29): region 0x03, bytes */
#define SRAM_ADDR(chunk)   (REGION_SRAM | ((uint32_t)(chunk) << 2))

/* data-memory layout (proposed: the log fixes the blocks' shape, not their addresses; v1.1 put them at the top) */
#define DMEM_RESERVED_0    0x00000000   /* word 0: never placed, meshsim flags any write (D27) */
#define COMMAND_BASE       0x00007FC0   /* the command block: 16 slots; slot 0 the command word (D6) */
#define REPLY_BASE         0x00007BC0   /* the reply block: two words per source, 128 sources (D6); 8-byte aligned */
#define STACK_TOP          0x00007BC0   /* the stack grows down from below the reply block */
#define REPLY_WORD(src)    (REPLY_BASE + 8u * (src))   /* byte address of src's reply pair */

/* endpoints (spec 2.1) */
#define DST_CC1            64u   /* C&C 1 */
#define DST_CC2            65u   /* C&C 2, the file system */
#define DST_CC3            66u   /* C&C 3, the pager */
#define DST_BROADCAST      126u  /* tiles 0-63 */

/* ---- register blocks (D9, D10, D11, D14) ---- */
#define MESH_RO const volatile uint32_t   /* read-only: a store doesn't compile (W3-27) */
#define MESH_WO volatile uint32_t         /* write-only: reads 0 */
#define MESH_RW volatile uint32_t

#ifndef __ASSEMBLER__
#include <stdint.h>

/* words 0-31 of every peripheral's window (D10): "extends PeripheralStd" is this member list at the top (D14) */
#define PERIPHERAL_STD_MEMBERS                                                                            \
    MESH_RO PARAMS;       /* 0: [7:0] type, [15:8] version; 0: nothing to say */                         \
    MESH_RO STATUS;       /* 1: [0] run in progress, [1] DMA in progress */                               \
    MESH_RO ERRORS;       /* 2: sticky until the reset (C&C 1's unit-error bit [0] is live) */           \
    MESH_WO PULSE_RUN;    /* 3: store 1 to start a run; ignored while one runs */                         \
    MESH_WO PULSE_DMA;    /* 4: store 1 to start the DMA set up in DMA_HDR/DMA_ADDR */                    \
    MESH_RO EVENTS;       /* 5: a load returns the events and consumes one count from each set bit in EVENTS_MASK */ \
    MESH_RO EVENTS_PEEK;  /* 6: what a load of EVENTS would return; consumes nothing */                   \
    MESH_RW EVENTS_MASK;  /* 7: the events that count (wake the tile's bit, are consumed); 0 at reset */  \
    MESH_RW DMA_HDR;      /* 8: count [11:0], queue [13:12], counter, mode, dst (D10, D12) */             \
    MESH_RW DMA_ADDR;     /* 9: the DMA's 32-bit destination, advancing per packet */                     \
    uint32_t _reserved_std[22]   /* 10-31: reserved standard words (read 0; meshsim flags) */

typedef struct { PERIPHERAL_STD_MEMBERS; uint32_t _defined[96]; } PeripheralStd;

/* the replayer, peripheral type 1 (W3-REVIEW-2 section 5, proposed): words 32-33 are v1.1's PERIPH_STATUS_0-1 */
typedef struct {
    PERIPHERAL_STD_MEMBERS;
    MESH_RO RUN_STATUS;   /* 32: rows run in this pass [15:0], next offset [31:16] (v1.1's PERIPH_STATUS_0; proposed) */
    MESH_RO PRESENTED;    /* 33: the instruction word of the expansion to supply (v1.1's PERIPH_STATUS_1) */
    uint32_t _defined[94];
} ReplayerRegs;

/* C&C 1's peripheral, Ingress/Egress and the UART (D28, D36). The order is D28's; the offsets are proposed */
typedef struct {
    PERIPHERAL_STD_MEMBERS;
    MESH_RO ERROR_LINES[3];   /* 32-34: live, by endpoint id; bit 64 unused */
    MESH_RO READY_LINES[2];   /* 35-36: the tiles' sticky READY bits */
    MESH_RW READY_EXPECT[2];  /* 37-38: the tiles C&C 1 waits for */
    MESH_WO PULSE_GO;         /* 39: clears READY 0-63 and drives GO, in one store; restarts the watchdog */
    MESH_RW WATCHDOG;         /* 40: 0 off (reset); N > 0 starts it; a write or PULSE_GO restarts it */
    MESH_RW LED;              /* 41: the software pin, bit 0 */
    MESH_WO UART_TX;          /* 42 */
    MESH_RO UART_RX;          /* 43 */
    MESH_RW UART_DIVISOR;     /* 44 */
    MESH_RW UART_STATUS;      /* 45: line errors, write 1 to clear */
    uint32_t _defined[82];    /* 46-127: the host interface's words (deferred) */
} Cnc1Regs;

/* the controller's own block, 0xFFFFF800 (D11, D30) */
typedef struct {
    MESH_RO PARAMS;               /* 0x800 */
    MESH_RO STATUS;               /* 0x804: diagnostic (D18) */
    MESH_RO ERRORS;               /* 0x808 */
    MESH_WO PULSE_DONE;           /* 0x80C */
    MESH_WO PULSE_DMA;            /* 0x810: start the address generator's run in DMA_CONFIG; clears EVENTS [7] (D17) */
    uint32_t _was_pulse_go;       /* 0x814: reserved (PULSE_GO moved to C&C 1's peripheral, D28) */
    MESH_RO EVENTS;               /* 0x818 */
    MESH_RO EVENTS_PEEK;          /* 0x81C */
    MESH_RW EVENTS_MASK;          /* 0x820: GO | counters done at reset */
    MESH_RW EVENTS_REQUIRED;      /* 0x824: 0 at reset */
    MESH_RW ROUTER_HDR;           /* 0x828: no-increment [0], counter, mode, dst (D7) */
    MESH_RW ROUTER_ADDR;          /* 0x82C: the next packet's address; advances by its size unless no-increment */
    MESH_WO ROUTER_SET_W0, ROUTER_SET_W1, ROUTER_SET_W2;           /* 0x830-0x838: cleared by every send */
    MESH_WO ROUTER_SEND_32, ROUTER_SEND_64, ROUTER_SEND_128;       /* 0x83C-0x844: the last store sends */
    union { MESH_RO REQUEST_GET_W0; MESH_WO REQUEST_SET_W0; };     /* 0x848 */
    union { MESH_RO REQUEST_GET_W1; MESH_WO REQUEST_SET_W1; };     /* 0x84C */
    union { MESH_RO REQUEST_GET_W2; MESH_WO REQUEST_SET_W2; };     /* 0x850 */
    union { MESH_RO REQUEST_RECEIVE; MESH_WO REQUEST_SEND; };      /* 0x854: a load pops; a store pushes */
    MESH_RW DMA_CONFIG;           /* 0x858 */
    MESH_RW DMA_RECORD_BASE_ADDRESS;   /* 0x85C: chunks */
    MESH_RW WRITE_COUNTER[4];     /* 0x860-0x86C: WRITE_COUNTER_1-4 are [0]-[3]; a store adds */
    MESH_RW SRAM_RD_ADDR_QUEUE;   /* 0x870: a store queues a read (chunks); a load returns the free entries */
    MESH_RW SRAM_RD_DEST_ADDR;    /* 0x874: data-memory byte offset, size, counter (D13) */
    uint32_t _reserved[96];       /* 0x878-0x9F4 */
    MESH_WO SIM_EXIT;             /* 0x9F8: simulator only, ends a meshsim test (D36; proposed: "beside the trace word") */
    MESH_WO TRACE;                /* 0x9FC: simulator only (D29, D32) */
} TileRegs;

/* the table window, 0xFFFF2000 (D11, proposed address: v1.1's low 16 bits) */
typedef struct {
    MESH_RW HIST[256];            /* 17-bit counts */
    MESH_WO PTR[256];             /* 21-bit chunk addresses; a load is a bus error */
    MESH_WO STREAM_SRC[256];
    MESH_WO STREAM_DEST[256];
    MESH_WO STREAM_COUNT[256];
    MESH_WO GROUP[64][2];         /* [g][0] low, [g][1] high */
} TableRegs;

_Static_assert(sizeof(PeripheralStd) == 0x200 && sizeof(ReplayerRegs) == 0x200 && sizeof(Cnc1Regs) == 0x200, "window size");
_Static_assert(sizeof(TileRegs) == 0x200, "block size");
_Static_assert(__builtin_offsetof(TileRegs, EVENTS) == 0x18 && __builtin_offsetof(TileRegs, ROUTER_HDR) == 0x28 &&
               __builtin_offsetof(TileRegs, REQUEST_SEND) == 0x54 && __builtin_offsetof(TileRegs, WRITE_COUNTER) == 0x60 &&
               __builtin_offsetof(TileRegs, SRAM_RD_DEST_ADDR) == 0x74 && __builtin_offsetof(TileRegs, TRACE) == 0x1FC, "D11 offsets");
_Static_assert(__builtin_offsetof(PeripheralStd, DMA_ADDR) == 36 && __builtin_offsetof(ReplayerRegs, RUN_STATUS) == 128 &&
               __builtin_offsetof(Cnc1Regs, ERROR_LINES) == 128, "D10 offsets");
_Static_assert(__builtin_offsetof(TableRegs, PTR) == 0x400 && __builtin_offsetof(TableRegs, GROUP) == 0x1400 &&
               sizeof(TableRegs) == 0x1600, "table window");

/* the pointers (D14): tile->EVENTS. A whole-block copy needs an explicit *tile and would consume events */
#define tile          ((volatile TileRegs *)0xFFFFF800u)
#define peripheral(n) ((volatile PeripheralStd *)(0xFFFFF800u + 0x200u * (n)))   /* n = 1-3 */
#define replayer      ((volatile ReplayerRegs *)0xFFFFFA00u)   /* peripheral 1 on a tile that has the replayer */
#define cc1           ((volatile Cnc1Regs *)0xFFFFFA00u)       /* peripheral 1 on C&C 1 */
#define tables        ((volatile TableRegs *)0xFFFF2000u)
#define COMMAND       ((volatile uint32_t *)COMMAND_BASE)       /* COMMAND[0] the command word, [1-15] arguments */
#define REPLY         ((volatile uint32_t *)REPLY_BASE)         /* REPLY[2 src], REPLY[2 src + 1] */
#endif /* __ASSEMBLER__ */

/* register offsets for assembly (the boot ROM and the handler use x0-relative accesses: 0xFFFFF800 + off) */
#define TILE_BASE          0xFFFFF800
#define OFF_PARAMS         0x000
#define OFF_STATUS         0x004
#define OFF_ERRORS         0x008
#define OFF_PULSE_DONE     0x00C
#define OFF_EVENTS         0x018
#define OFF_EVENTS_PEEK    0x01C
#define OFF_EVENTS_MASK    0x020
#define OFF_EVENTS_REQUIRED 0x024
#define OFF_REQUEST_SET_W0 0x048
#define OFF_REQUEST_SET_W1 0x04C
#define OFF_REQUEST_SET_W2 0x050
#define OFF_REQUEST_SEND   0x054
#define OFF_WRITE_COUNTER_1 0x060
#define X0REL(off)         ((off) - 0x800)          /* the 12-bit signed immediate reaching TILE_BASE + off from x0 */

/* ---- PARAMS (D18) ---- */
#define PARAMS_PERIPH_PRESENT(p)   ((p) & 0x7u)          /* bit n-1: peripheral n present */
#define PARAMS_ID(p)               (((p) >> 20) & 0x7Fu) /* endpoint id */
#define PARAMS_SIZE(p)             ((p) >> 27)           /* big-SRAM size in 256 KB units - 1 */

/* ---- STATUS (D18): diagnostic; programs wait by events (D17) ---- */
#define ST_RD_RUNNING      (1 << 0)
#define ST_RD_DONE         (1 << 1)
#define ST_GATHER_BUSY     (1 << 2)
#define ST_SEND_BUSY       (1 << 3)
#define ST_DONE            (1 << 4)
#define ST_LANDED          (1 << 5)
#define ST_REQUEST_BUSY    (1 << 6)

/* ---- ERRORS (D18): sticky until the reset; any bit raises the hard interrupt and the error line ---- */
#define ERR_ROUTER_OVF     (1 << 0)
#define ERR_BAD_PACKET     (1 << 1)    /* bad packet or misaligned access (D26) */
#define ERR_UNEXPECTED     (1 << 2)
#define ERR_DRIFT          (1 << 3)
#define ERR_WE_IN_OVF      (1 << 4)
#define ERR_TABLE_OVF      (1 << 5)
#define ERR_GATHER_OVF     (1 << 6)
#define ERR_READ_PORT      (1 << 7)
#define ERR_DONE_EARLY     (1 << 8)
#define ERR_BUS            (1 << 9)
#define ERR_ILLEGAL        (1 << 10)
#define ERR_PERIPH(n)      (1 << (10 + (n)))   /* n = 1-3, live: that peripheral's ERRORS non-zero */
#define ERR_RECV_UNDERFLOW (1 << 14)

/* ---- EVENTS, EVENTS_PEEK, EVENTS_MASK, EVENTS_REQUIRED (D16, D30) ---- */
#define EV_CTR(n)          (1 << ((n) - 1))   /* write counter n done, n = 1-4 (sticky) */
#define EV_CTRS_DONE       (1 << 4)    /* all four at 0, one armed since it last fired (sticky) */
#define EV_GO              (1 << 5)    /* sticky */
#define EV_COMMAND         (1 << 6)    /* command landed (sticky) */
#define EV_RUN_DONE        (1 << 7)    /* the address generator's run done; cleared by PULSE_DMA (sticky) */
#define EV_REQ_ROOM        (1 << 8)    /* request FIFO no longer full (sticky) */
#define EV_REQ_PENDING     (1 << 9)    /* a received request is waiting (live) */
#define EV_PERIPH(n)       (1 << (9 + (n)))   /* n = 1-3: that peripheral's EVENTS & EVENTS_MASK non-zero (live) */
#define EV_RESET_MASK      (EV_GO | EV_CTRS_DONE)

/* ---- the common header block, bits [31:17] (D12, D30) ---- */
#define HDR_SIZE(s)        (((uint32_t)(s) & 0x3u) << 17)
#define HDR_CTR(c)         (((uint32_t)(c) & 0x7u) << 19)
#define HDR_MODE(m)        (((uint32_t)(m) & 0x7u) << 22)
#define HDR_DST(d)         (((uint32_t)(d) & 0x7Fu) << 25)

/* sizes (link word [161:160]); modes (D6); counters (D3, D30) */
#define SIZE_32            0u
#define SIZE_64            1u
#define SIZE_128           2u
#define MODE_WRITE         0u   /* write at address: any region a packet may reach; commands and replies too */
#define MODE_SRC           1u   /* big-SRAM write by source, at PTR[src] */
#define MODE_BUCKET        2u   /* bucket write, at PTR[digit] */
#define MODE_AUGMENT       3u   /* augmented write at PTR[src]: 32 or 64 bits, never 128 */
#define MODE_GATHER        4u   /* gather request, local only; never counted; its counter field passes to the values */
#define CTR_NONE           0u   /* uncounted: commands, boot overlays, and nothing else (D17) */
/* The recipes' counter convention (CONVENTIONS 5): v1.1's four counters, one to one. A convention only. */
#define CTR_SRAM           1u
#define CTR_CM             2u
#define CTR_REPLY          3u
#define CTR_PERIPH         4u

/* ROUTER_HDR (D7): no counter default, on purpose (W3-REVIEW-2 section 6: a default 0 makes silent uncounted writes) */
#define ROUTER_NOINC       1u   /* [0]: ROUTER_ADDR stays put after a send */
#define ROUTER_HDR_W(dst, mode, ctr)  (HDR_DST(dst) | HDR_MODE(mode) | HDR_CTR(ctr))

/* STREAM entries (D4, D30) */
#define STREAM_SRC_W(chunk, gap)      (((uint32_t)(chunk) & 0x1FFFFFu) | (((uint32_t)(gap) & 0x7u) << 21))
#define STREAM_COUNT_W(n, size, ctr, mode, d) \
    (((uint32_t)(n) & 0x1FFFFu) | HDR_SIZE(size) | HDR_CTR(ctr) | HDR_MODE(mode) | (((uint32_t)(d) & 0x7Fu) << 25))
#define STREAM_COUNT_MAX   0x1FFFF   /* 131,071 elements */
/* GROUP (v1.1 layout, unchanged by the log) */
#define GROUP_LO_R(r)      ((uint32_t)(r) & 0x3FFFFu)
#define GROUP_HI(start, t, gap) (((uint32_t)(start) & 0xFFu) | ((((uint32_t)(t) - 1) & 0xFu) << 8) | (((uint32_t)(gap) & 0xFFu) << 12))
/* DMA_CONFIG (v1.1 layout, unchanged by the log; now readable) */
#define RUN_STREAMS        0u
#define RUN_GROUPS         1u
#define DMA_CFG_RUN(kind, start, count) (((uint32_t)(kind) & 1u) | (((uint32_t)(start) & 0xFFu) << 1) | (((uint32_t)(count) & 0x1FFu) << 9))
#define DMA_CFG_BUCKETDIG(d)  (((uint32_t)(d) & 0x7u) << 18)
#define DMA_CFG_HISTDIG(d)    (((uint32_t)(d) & 0x7u) << 21)
#define DMA_CFG_KM1(k)        (((uint32_t)(k) & 0x7u) << 24)
/* SRAM_RD_DEST_ADDR (D13): data memory only; mode 0 and dst = the tile itself */
#define RD_DEST(byte_off, size, ctr)  (((uint32_t)(byte_off) & 0x1FFFFu) | HDR_SIZE(size) | HDR_CTR(ctr))
#define READ_PORT_DEPTH    64u
/* a peripheral's DMA_HDR (D10) */
#define DMA_HDR_W(count, queue, dst, mode, ctr) \
    (((uint32_t)(count) & 0xFFFu) | (((uint32_t)(queue) & 0x3u) << 12) | HDR_DST(dst) | HDR_MODE(mode) | HDR_CTR(ctr))

/* the standard peripheral bits (D10) */
#define PST_RUN            (1 << 0)
#define PST_DMA            (1 << 1)
#define PEV_RUN_DONE       (1 << 0)
#define PEV_DMA_DONE       (1 << 1)
#define PARAMS_TYPE(p)     ((p) & 0xFFu)
#define PTYPE_REPLAYER     1u
/* the replayer's events (W3-REVIEW-2 section 5, proposed): credits have depth 2, the others 1 */
#define RP_EV_CODE         (1 << 2)
#define RP_EV_RLOG         (1 << 3)
#define RP_EV_WLOG         (1 << 4)
#define RP_EV_EXP          (1 << 5)
#define RP_EV_NEWEXP       (1 << 6)
#define RP_NEXT_OFFSET(s)  ((s) >> 16)
/* the replayer's region 0x04 in bytes (spec 4.19.10's word map x 4; windows at offset [22:20]; proposed) */
#define RP_STATE           (REGION_PERIPH(1) + 0x000000u)   /* state word, 16 bytes */
#define RP_STREAMS         (REGION_PERIPH(1) + 0x000010u)   /* stream word */
#define RP_IMAGE           (REGION_PERIPH(1) + 0x000020u)   /* register image, 64 x 16 bytes */
#define RP_Q_CODE          (REGION_PERIPH(1) + 0x100000u)   /* queue ports: 128-bit packets only */
#define RP_Q_RLOG          (REGION_PERIPH(1) + 0x200000u)
#define RP_Q_WLOG          (REGION_PERIPH(1) + 0x280000u)
#define RP_Q_EXP           (REGION_PERIPH(1) + 0x300000u)
#define RP_HALF_CODE       128u   /* entries per credit (spec 4.19.8) */
#define RP_HALF_LOG        256u
#define RP_HALF_EXP        128u

/* C&C 1's peripheral events (D28, proposed bits) */
#define CC1_EV_ALL_READY   (1 << 2)    /* live: (READY & EXPECT) == EXPECT and EXPECT != 0 */
#define CC1_EV_WATCHDOG    (1 << 3)    /* depth 1 */
#define CC1_EV_UART_RX     (1 << 4)    /* live */
#define CC1_EV_UART_TX     (1 << 5)    /* live */
#define CC1_ERR_UNIT       (1 << 0)    /* its ERRORS [0]: OR(ERROR_LINES), live */

/* ---- the request port (spec 3.3, 4.18): W0 [6:0] requestor (written 0), [9:7] target, the rest the target's ---- */
#define REQ_TARGET(t)      (((uint32_t)(t) & 0x7u) << 7)
#define TARGET_CC1         0u   /* spec draft 3.3, proposed */
#define TARGET_CC2         1u
#define TARGET_CC3         2u
/* C&C 1's request format: undefined in the log. Placeholder: op [17:10], sequence or argument [31:18] */
#define REQ_CC1(op, seq)   (REQ_TARGET(TARGET_CC1) | (((uint32_t)(op) & 0xFFu) << 10) | ((uint32_t)(seq) << 18))
#define REQ_OP_REPORT      1u   /* the handler's reports (D20, D21) */
#define REQ_OP_STARTED     2u   /* C&C 2's and C&C 3's start-up (D31) */
#define REQ_OP_RESULT      3u   /* a tile's result for C&C 1 (the self-test's signature) */

/* ---- the command convention (CONVENTIONS 4): [31:24] type, [23] parity, [22:0] argument ---- */
#define ARGS_EVEN          1u
#define ARGS_ODD           8u
#define CMD_TYPE(w)        ((uint32_t)(w) >> 24)
#define CMD_PARITY(w)      (((uint32_t)(w) >> 23) & 1u)
#define CMD_ARG(w)         ((uint32_t)(w) & 0x7FFFFFu)
#define CMD_WORD(type, parity, arg) (((uint32_t)(type) << 24) | ((uint32_t)(parity) << 23) | ((uint32_t)(arg) & 0x7FFFFFu))
#define CMD_JUMP           0x4A   /* the boot command: slot 1 holds the entry address (D36) */
#define CMD_STAGE          0x53   /* announced staging: arm the counter with the argument, wait, done (6.2) */

#ifndef __ASSEMBLER__
#define CMD_ARGS(w)        (COMMAND + (CMD_PARITY(w) ? ARGS_ODD : ARGS_EVEN))

/* ---- helpers ---- */
static inline unsigned tile_index(void) { return PARAMS_ID(tile->PARAMS); }
static inline uint32_t sram_chunks(void) { return (PARAMS_SIZE(tile->PARAMS) + 1u) << 16; }   /* 256 KB = 0x10000 chunks */
static inline void set_stream(unsigned i, uint32_t src, uint32_t dest, uint32_t count)
    { tables->STREAM_SRC[i] = src; tables->STREAM_DEST[i] = dest; tables->STREAM_COUNT[i] = count; }
static inline void set_group(unsigned g, uint32_t lo, uint32_t hi) { tables->GROUP[g][0] = lo; tables->GROUP[g][1] = hi; }

/* WFI, then a load of EVENTS: returns every pending event and consumes those in EVENTS_MASK (spec 4.12). At the
 * reset mask it consumes counters done (and a GO). Software re-checks after every wake (D10: spurious wakes). */
static inline uint32_t wfi(void) { __asm__ volatile ("wfi" ::: "memory"); return tile->EVENTS; }
/* Wait until every event in ev is pending, then consume them (D17's "EVENTS_REQUIRED and WFI"). GO stays in the
 * mask; nothing else wakes. Restores the reset mask and EVENTS_REQUIRED = 0. Not between WFI and a start store. */
static inline uint32_t wait_all(uint32_t ev) {
    tile->EVENTS_MASK = EV_GO | ev;
    tile->EVENTS_REQUIRED = ev;
    uint32_t e;
    do e = wfi(); while ((e & ev) != ev);    /* the wake term holds only when all are pending; re-check anyway */
    tile->EVENTS_REQUIRED = 0;
    tile->EVENTS_MASK = EV_RESET_MASK;
    return e;
}
/* wait_for_command (6.3): pulse done and WFI as two back-to-back instructions; on waking (by GO alone: the done
 * gate) the load of EVENTS consumes GO (keep EV_GO in EVENTS_MASK); then read slot 0. x0-relative store, as v1.1. */
static inline uint32_t wait_for_command(void) {
    __asm__ volatile ("sw %0, %1(zero)\n\twfi" :: "r"(1u), "i"(X0REL(OFF_PULSE_DONE)) : "memory");
    (void)tile->EVENTS;
    return COMMAND[0];
}
/* start the address generator's run in DMA_CONFIG; clears EVENTS [7] (D17) */
static inline void start_run(void) { tile->PULSE_DMA = 1; }
/* STATUS.rd_running: a level a helper may poll when it can't know what ran before (D17) */
static inline void wait_rd_idle(void) { while (tile->STATUS & ST_RD_RUNNING) ; }

/* the send port (D7): ROUTER_HDR and ROUTER_ADDR, then one store per 32 bits, the last store sending */
static inline void send_to(uint32_t hdr, uint32_t addr) { tile->ROUTER_HDR = hdr; tile->ROUTER_ADDR = addr; }
static inline void send32(uint32_t w0) { tile->ROUTER_SEND_32 = w0; }
static inline void send64(uint32_t w1, uint32_t w0) { tile->ROUTER_SET_W0 = w0; tile->ROUTER_SEND_64 = w1; }
static inline void send128(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0)
    { tile->ROUTER_SET_W0 = w0; tile->ROUTER_SET_W1 = w1; tile->ROUTER_SET_W2 = w2; tile->ROUTER_SEND_128 = w3; }
/* the request port (4.18): words 0-2, then the last store pushes */
static inline void request_send(uint32_t w3, uint32_t w2, uint32_t w1, uint32_t w0)
    { tile->REQUEST_SET_W0 = w0; tile->REQUEST_SET_W1 = w1; tile->REQUEST_SET_W2 = w2; tile->REQUEST_SEND = w3; }

/* simulator only (D36): end a meshsim test with a value */
static inline void sim_exit(uint32_t v) { tile->SIM_EXIT = v; for (;;) ; }
#endif /* __ASSEMBLER__ */
#endif /* MESH_V12_DRAFT_H */
