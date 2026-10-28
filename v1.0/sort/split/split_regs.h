/* split_regs.h -- the registers the split uses, at mesh.h 1.0's addresses (V1.0-CHANGES.md's table; the split keeps its
 * own header so it builds on its host ISS too). test/RegDocTest checks every address and field here against mesh.h. */
#ifndef SPLIT_REGS_H
#define SPLIT_REGS_H
#include <stdint.h>

#define SR_REG(a) (*(volatile uint32_t *)(a))
#define DMA_PULSE      SR_REG(0xFFFFF818u)   /* [0] done [1] start */
#define TILE_STATUS     SR_REG(0xFFFFF800u)   /* tile index in [26:21] (mesh.h TILE_ST_TILE) */
#define DMA_CM_REMAINING      SR_REG(0xFFFFF820u)   /* mode-5 down-counter */
#define DMA_REPLY_REMAINING   SR_REG(0xFFFFF824u)   /* mode-7 and read-port down-counter */
#define ROUTER_SET_HDR           SR_REG(0xFFFFF808u)   /* dst [6:0], mode [10:7], addr [30:11] */
#define ROUTER_SET_HI            SR_REG(0xFFFFF80Cu)
#define ROUTER_SEND_LO           SR_REG(0xFFFFF810u)
#define SRAM_RD_ADDR_QUEUE SR_REG(0xFFFFF82Cu)   /* write: queue a read; read: free entries (spec 4.3) */
#define SRAM_RD_DEST_ADDR  SR_REG(0xFFFFF830u)   /* data-memory word of the next result */
#define TRACE_REG         SR_REG(0xFFFFFFF0u)   /* ISS only: phase marks for the cost report */

#define DMA_PULSE_DONE 1u
#define ME() ((TILE_STATUS >> 21) & 63u)
#define SR_MODE_CMEM  5u   /* controller-memory write: two words (mesh.h MODE_CMEM) */
#define SR_MODE_REPLY 7u   /* reply: one data-memory word (mesh.h MODE_REPLY) */
#define ROUTER_HDR(dst, mode, addr) ((uint32_t)(dst) | (uint32_t)(mode) << 7 | (uint32_t)(addr) << 11)
/* packed controller-memory word of a data-memory byte address (spec 4.11) */
#define CM_WORD(p) (0x800u + (((uint32_t)(uintptr_t)(p) - 0x4000u) >> 2))

static inline void wfi(void) { __asm__ volatile("wfi" ::: "memory"); }

#ifdef SPLIT_TRACE
#define TRACE(x) (TRACE_REG = (x))
#else
#define TRACE(x) ((void)0)
#endif
#endif
