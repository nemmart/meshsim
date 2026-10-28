/* split_regs.h -- the registers the split uses, at mesh.h 1.1's addresses (spec 2026-10-26n; the split keeps its own
 * header so it builds on its host ISS too). test/RegDocTest checks every address here against mesh.h. */
#ifndef SPLIT_REGS_H
#define SPLIT_REGS_H
#include <stdint.h>

#define SR_REG(a) (*(volatile uint32_t *)(a))
#define TILE_STATUS           SR_REG(0xFFFFF800u)   /* endpoint id (the tile index) in [26:20] (mesh.h TILE_ST_ID) */
#define TILE_PULSE            SR_REG(0xFFFFF808u)   /* [0] done [1] start */
#define ROUTER_SET_HDR        SR_REG(0xFFFFF80Cu)   /* dst [6:0], mode [10:7], addr [31:11] */
#define ROUTER_SET_W0         SR_REG(0xFFFFF810u)   /* payload [31:0] of the next 64-bit packet */
#define ROUTER_SEND_32        SR_REG(0xFFFFF81Cu)   /* payload [31:0]: sends a 32-bit packet */
#define ROUTER_SEND_64        SR_REG(0xFFFFF820u)   /* payload [63:32]: sends a 64-bit packet */
#define DMA_CM_REMAINING      SR_REG(0xFFFFF834u)   /* mode 6, read-port results included; a write adds */
#define DMA_REPLY_REMAINING   SR_REG(0xFFFFF838u)   /* mode 8 (replies); a write adds */
#define SRAM_RD_ADDR_QUEUE    SR_REG(0xFFFFF840u)   /* write: queue a read; read: free entries (spec 4.3) */
#define SRAM_RD_DEST_ADDR     SR_REG(0xFFFFF844u)   /* setup: destination word [13:0], size [15:14] */
#define WAKE_EVENTS           SR_REG(0xFFFFF848u)   /* a load consumes the pending events under WAKE_MASK */
#define TRACE_REG             SR_REG(0xFFFFFFF0u)   /* ISS only: phase marks for the cost report */

#define TILE_PULSE_DONE 1u
#define ME() ((TILE_STATUS >> 20) & 63u)
#define SR_MODE_CM    6u   /* controller-memory write (mesh.h MODE_CM) */
#define SR_MODE_REPLY 8u   /* reply, 32 or 64 bits, landing by source at REPLY[2 src] (mesh.h MODE_REPLY) */
#define ROUTER_HDR(dst, mode, addr) ((uint32_t)(dst) | (uint32_t)(mode) << 7 | (uint32_t)(addr) << 11)
/* packed controller-memory word of a data-memory byte address (spec 4.11) */
#define CM_WORD(p) (0x800u + (((uint32_t)(uintptr_t)(p) - 0x4000u) >> 2))
/* the read port's setup word for 32-bit reads to the packed word w (mesh.h SRAM_RD_DEST(w, SRAM_RD_SIZE_32)) */
#define SR_RD_DEST32(w) ((uint32_t)(w) & 0x3FFFu)
/* the reply block: source s's reply at REPLY[2 s] (and [2 s + 1] for a 64-bit one) (mesh.h REPLY) */
#define SR_REPLY ((volatile uint32_t *)0x5BC0u)

/* WFI, then a load of WAKE_EVENTS: it consumes counters done (the reset WAKE_MASK holds it and GO), as mesh.h's wfi() */
static inline void wfi(void) { __asm__ volatile("wfi" ::: "memory"); (void)WAKE_EVENTS; }

#ifdef SPLIT_TRACE
#define TRACE(x) (TRACE_REG = (x))
#else
#define TRACE(x) ((void)0)
#endif
#endif
