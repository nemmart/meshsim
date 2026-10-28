/* ===================================================================================================================
 * LOCAL REGISTER HEADER: a stand-in for mesh.h until meshsim's v1.1 tile lands.
 *
 * Every constant here is taken from spec/machine-spec.md at mesh-sorter-v1.1-2026-10-26n, section 8 (and 4.2, 4.11,
 * 4.12, 4.17, 4.19). The mesh.h in that package is still the pre-slot v1.1 (its offsets after 0x80C differ: e.g. 0x810
 * is ROUTER_SET_W1 there and ROUTER_SET_W0 in the spec), so it is NOT included (26n's mesh.h is unchanged from 26k's). When meshsim generates mesh.h for the
 * v1.1 tile, delete this file and include mesh.h; every name here is meant to match the spec's register and field names.
 * =================================================================================================================== */
#ifndef REPLAY_REGS_H
#define REPLAY_REGS_H
#include <stdint.h>

#define REG(off)   (*(volatile uint32_t *)(0xFFFFF000u + (off)))

/* 8.1 register map (offsets within the top 2 KB) */
#define TILE_STATUS            REG(0x800)   /* R  8.2 */
#define TILE_CONDITIONS        REG(0x804)   /* R  8.3 */
#define TILE_PULSE             REG(0x808)   /* W  8.6 */
#define ROUTER_SET_HDR         REG(0x80C)   /* W  8.4: dst [6:0], mode [10:7], addr [31:11] */
#define ROUTER_SET_W0          REG(0x810)   /* W  payload [31:0] */
#define ROUTER_SET_W1          REG(0x814)   /* W  payload [63:32] */
#define ROUTER_SET_W2          REG(0x818)   /* W  payload [95:64] */
#define ROUTER_SEND_32         REG(0x81C)
#define ROUTER_SEND_64         REG(0x820)
#define ROUTER_SEND_128        REG(0x824)   /* W  payload [127:96]; sends a 128-bit packet */
#define DMA_CONFIG             REG(0x828)   /* W  8.5 */
#define DMA_RECORD_BASE_ADDRESS REG(0x82C)
#define DMA_SRAM_REMAINING     REG(0x830)   /* R/W, a write adds (8.7) */
#define DMA_CM_REMAINING       REG(0x834)   /* R/W, a write adds; mode 6 */
#define DMA_REPLY_REMAINING    REG(0x838)
#define DMA_PERIPH_REMAINING   REG(0x83C)   /* R/W, a write adds; mode 9, on landing (8.11, 4.17.5) */
#define SRAM_RD_ADDR_QUEUE     REG(0x840)
#define SRAM_RD_DEST_ADDR      REG(0x844)
#define WAKE_EVENTS            REG(0x848)   /* R: a load consumes (claims) the events under WAKE_MASK (8.10) */
#define WAKE_PEEK              REG(0x84C)   /* R: no side effects */
#define WAKE_MASK              REG(0x850)
#define WAKE_REQUIRED          REG(0x854)
#define PERIPH_STATUS_0        REG(0x870)   /* replayer: rows run [15:0], next offset [31:16] (8.11) */
#define PERIPH_STATUS_1        REG(0x874)   /* replayer: the presented instruction word */

/* 8.2 TILE_STATUS */
#define ST_RD_RUNNING          (1u << 0)
#define ST_RD_DONE             (1u << 1)
#define ST_PERIPH_RUNNING      (1u << 3)
#define ST_SEND_BUSY           (1u << 5)
#define ST_PERIPH_TYPE(s)      (((s) >> 12) & 0xF)
#define ST_ENDPOINT_ID(s)      (((s) >> 20) & 0x7F)

/* 8.6 TILE_PULSE */
#define PULSE_DONE             (1u << 0)
#define PULSE_START            (1u << 1)
#define PULSE_PERIPH_RUN       (1u << 2)

/* 8.4 ROUTER_SET_HDR */
#define HDR(dst, mode, addr)   (((uint32_t)(dst) & 0x7F) | (((uint32_t)(mode) & 0xF) << 7) | ((uint32_t)(addr) << 11))

/* 8.5 DMA_CONFIG: a stream run of `count` streams from `first` */
#define CONFIG_STREAM_RUN(first, count)  ((0u) | (((uint32_t)(first) & 0xFF) << 1) | (((uint32_t)(count) & 0x1FF) << 9))

/* 8.9 the table window: STREAM_SRC, STREAM_DEST, STREAM_COUNT (write-only) */
#define STREAM_SRC             ((volatile uint32_t *)0x2800)   /* source chunk [20:0] */
#define STREAM_DEST            ((volatile uint32_t *)0x2C00)   /* destination word [20:0], d [27:21] */
#define STREAM_COUNT           ((volatile uint32_t *)0x3000)   /* count [17:0], mode [21:18], size [23:22], gap [26:24] */
#define STREAM_DEST_ENTRY(word, d)            (((uint32_t)(word) & 0x1FFFFF) | (((uint32_t)(d) & 0x7F) << 21))
#define STREAM_COUNT_ENTRY(n, mode, size, gap) (((uint32_t)(n) & 0x3FFFF) | (((uint32_t)(mode) & 0xF) << 18) \
                                              | (((uint32_t)(size) & 3) << 22) | (((uint32_t)(gap) & 7) << 24))
#define SIZE_128               2

/* modes (3.1) */
#define MODE_CMEM              6            /* controller-memory write (packed word address) */
#define MODE_PERIPH            9            /* the peripheral write */

/* 8.10 WAKE_EVENTS bits */
#define EV_SRAM_DONE           (1u << 0)
#define EV_CM_DONE             (1u << 1)
#define EV_REPLY_DONE          (1u << 2)
#define EV_PERIPH_DONE         (1u << 3)    /* DMA_PERIPH_REMAINING done */
#define EV_GO                  (1u << 8)
#define EV_COUNTERS_DONE       (1u << 9)
#define EV_RUN_DONE            (1u << 11)
/* the replayer's service lines, PERIPH_SERVICE_1-6 = [18:13] (4.19.9) */
#define EV_CODE_LOW            (1u << 13)
#define EV_RLOG_LOW            (1u << 14)
#define EV_WLOG_LOW            (1u << 15)
#define EV_EXP_LOW             (1u << 16)
#define EV_NEW_EXP             (1u << 17)
#define EV_PERIPH_SVC_DONE     (1u << 18)
#define EV_PERIPH_LINES        (0x3Fu << 13)

/* 4.11: packed controller-memory word addresses; data memory 0x4000-0x5FFF is packed words 0x800-0xFFF */
#define PACKED_WORD(p)         (0x800u + (((uint32_t)(uintptr_t)(p) - 0x4000u) >> 2))

/* 4.19.10: the replayer's mode-9 address map (32-bit word addresses) */
#define RP_STATE_WORD          0x00000
#define RP_DECODE_TABLE        0x00400
#define RP_ROM                 0x00800
#define RP_CODE_PORT           0x40000
#define RP_RLOG_PORT           0x80000
#define RP_WLOG_PORT           0xA0000
#define RP_EXP_PORT            0xC0000

static inline void wfi(void) { __asm__ volatile ("wfi" ::: "memory"); }

#endif
