/* split_regs.h -- the split's view of the machine (v1.2 port: WORKER8). v1.1's own register addresses are gone: the
 * split includes the generated mesh.h 1.2 (W3-REVIEW-2 s.6: "better: include the generated header") and this file
 * keeps only its few names on top of it. Counters (CONVENTIONS 5): the exchanges' words on counter 3 (replies), the
 * read port's results on counter 2 (controller memory).
 * Snarky One port (../PORTING.md): the read unit's setup in READ_HDR and READ_DEST_ADDR (sr_read_dest32); a read queued by READ_SEND, the free entries in READ_FREE.
 */
#ifndef SPLIT_REGS_H
#define SPLIT_REGS_H
#include <stdint.h>
#include "mesh.h"

#define ME() tile_index()
/* the reply block: source s's reply at REPLY[2 s] (and [2 s + 1] for a 64-bit one) */
#define SR_REPLY REPLY
/* an exchange's header to dst: a mode-0 write to REPLY_WORD(me) there (ROUTER_ADDR, set once per exchange), no-increment,
 * counted on counter 3 (RECIPES 7) */
#define SR_HDR(dst) (ROUTER_HDR_W((dst), MODE_WRITE, CTR_REPLY) | ROUTER_NOINC)
/* the read port's setup for 32-bit results landing from p on, counted on counter 2 (RECIPES 11) */
#define SR_RD_DEST32(p) ((uint32_t)(uintptr_t)(p))
static inline void sr_read_dest32(uint32_t p) { read_dest(p, SIZE_32, CTR_CM); }   /* Snarky One port: READ_HDR, READ_DEST_ADDR */
/* every armed counter done (counters done, under the reset mask), as v1.1's wfi() for counters done: the split arms one
 * counter at a time (2 or 3), as DECISIONS-v1.2 47 (Q3) requires */
static inline void sr_wait(void) { while (!(wfi() & EV_CTRS_DONE)) ; }

#ifdef SPLIT_TRACE
#define TRACE(x) sim_trace(x)                                /* the simulator-only trace word (8.15) */
#else
#define TRACE(x) ((void)0)
#endif
#endif
