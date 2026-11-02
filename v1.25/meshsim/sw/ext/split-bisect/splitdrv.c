/* splitdrv.c -- worker 4's split (split.c, with worker 8's SPLIT_BARRIER patch) on meshsim v4.0, booted by C&C.
 * The tile's n_me arrives by block copy from big SRAM at PARAMS; its sorted run of (key, index) records sits at
 * RUN_CHUNK (0x60000, as on worker 4's ISS). Command 1: run split_get_cuts (66 barriers inside, each answered by
 * C&C with a one-line command and GO); then wait for command 2 and exit. c[] and n[] are static: the same address on
 * every tile (BINDING.md). */
#include <stdint.h>
#include "split.h"
#include "split_regs.h"
#define PARAMS 0xF0000u
#define RUN_CHUNK 0x60000u
static struct { uint32_t n_me, pad; } P;
uint32_t c[65], n[64];
volatile uint32_t resolved, steps[SPLIT_ROUNDS + 1];
#define STREAM_W ((volatile uint32_t *)0x2800u)
#define DMA_CONFIG SR_REG(0xFFFFF814u)
#define MBOX0 (*(volatile uint32_t *)0x5C00u)
static void wait_cmd(void) { DMA_PULSE = DMA_PULSE_DONE; wfi(); (void)MBOX0; }
/* the split's barrier (-DSPLIT_BARRIER=split_barrier, build.sh): as wait_for_command() in mesh.h v5 */
void split_barrier(void) { __asm__ volatile ("sw %0, %1(zero)\n\twfi" :: "r"(DMA_PULSE_DONE), "i"((int)0xFFFFF818) : "memory"); (void)MBOX0; }
int main(void) {
    STREAM_W[2 * 255] = (PARAMS & 0xFFFFF) | (CM_WORD(&P) << 20);
    STREAM_W[2 * 255 + 1] = 1u | SR_MODE_CMEM << 24 | 2u << 27;  /* count 1, d 0, mode 5, 64-bit */
    DMA_CM_REMAINING = 1;
    DMA_CONFIG = 0u | 255u << 1 | 1u << 9;               /* stream run, start 255, count 1 */
    DMA_PULSE = 2u;                                       /* start */
    wfi();
    wait_cmd();
    split_get_cuts(RUN_CHUNK, P.n_me, 0, c, n);
    resolved = split_resolved_round;
    for (int r = 0; r <= SPLIT_ROUNDS; r++) steps[r] = split_steps[r];
    wait_cmd();
    return 0;
}
