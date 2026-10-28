/* The resident loader (spec 4.11, 6.2), at LOADER_BASE (0x1F00), 64 words at most. HALT -> RUN starts here.
 * PROVISIONAL and minimal (the M6 session owns it):
 *   1. read the command (the boot marker) in mailbox slot 0 and the first slot of its argument block (slot 1 for an
 *      even command, 32 for an odd one) FIRST;
 *   2. PTR[64] = top of big SRAM - STAGE_CHUNKS, so C&C's mode-2 staging packets (src 64) land in the top 64 KB;
 *   3. CMD_JUMP -> jump to the address in the argument block's first slot; anything else -> ebreak. */
#include "mesh.h"
    .section .text.loader, "ax"
    .globl _loader
_loader:
    li    t3, MAILBOX_BASE
    lw    t4, 0(t3)
    srli  t5, t4, CMD_PARITY_SHIFT
    andi  t5, t5, 1
    addi  a0, t3, 4 * ARGS_EVEN
    beqz  t5, 2f
    addi  a0, t3, 4 * ARGS_ODD
2:  lw    a0, 0(a0)
    lw    t0, TILE_STATUS_ADDR - 0x100000000(zero)
    srli  t0, t0, TILE_ST_SIZE_SHIFT
    addi  t0, t0, 1
    slli  t0, t0, AREA_CHUNKS_SHIFT
    li    t1, STAGE_CHUNKS
    sub   t0, t0, t1
    li    t2, PTR_BASE + 4 * 64
    sw    t0, 0(t2)
    srli  t5, t4, 24
    li    t6, CMD_JUMP
    bne   t5, t6, 1f
    jr    a0
1:  ebreak
