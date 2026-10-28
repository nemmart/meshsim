/* split.h -- the split (design v2 step 3): design v1's bisection on key values
 * (Sorter v1.0, S1 with carried brackets and early exit), ported to the
 * controller read port (spec 4.3).  See BINDING.md.
 *
 *   void split_get_cuts(uint32_t run_chunk, uint32_t n_me, uint32_t shaped,
 *                       uint32_t c[65], uint32_t n[64]);
 *
 * Called on every tile, in one lockstep phase sequence (it contains its own
 * barriers: DMA_PULSE done + WFI, then GO from C&C: 66 times for the sort
 * (shaped = 0: the seed exchange adds one), 65 with shaped = 1).
 *   run_chunk  big-SRAM chunk address of the tile's sorted run: n_me 64-bit
 *              (key, index) records, the key in the low word (chunk run_chunk + 2i);
 *   n_me       the run's length;
 *   shaped     0: R_q = floor(q N / 64) (the sort); 1: R_q = n_0 + ... + n_{q-1}
 *              (the shaped test sets only);
 *   c[65]      out: c[q] = the number of this tile's records whose global rank in
 *              (key, tile, position) order is below R_q (c[0] = 0, c[64] = n_me),
 *              i.e. the state after XA (walk-through 5.3);
 *   n[64]      out: every tile's run length (exchanged in the first phase).
 * c[] and n[] must be at the same data-memory address on every tile (they receive
 * mode-7 words by convention: array base + sender).
 */
#ifndef SPLIT_H
#define SPLIT_H
#include <stdint.h>

void split_get_cuts(uint32_t run_chunk, uint32_t n_me, uint32_t shaped, uint32_t c[65], uint32_t n[64]);

#define SPLIT_ROUNDS 32         /* fixed (lead, 3 Oct): bisection on 32-bit keys needs at most 32 */

/* statistics for the cost report (ISS builds) */
extern volatile uint32_t split_steps[SPLIT_ROUNDS + 1];   /* this tile's search steps per round */
extern volatile uint32_t split_resolved_round;            /* round in which this owner resolved (0: trivial) */
#endif
