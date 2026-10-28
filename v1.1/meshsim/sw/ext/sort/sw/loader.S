/* The resident loader (spec 4.11, 6.2), at LOADER_BASE (0x1F00), 64 words at most; HALT -> RUN starts here.
 * Owned by the sort session (worker 6). From meshsim v4.0 M4's minimal loader:
 *   1. read the command word in command slot 0 and its argument block's first slot FIRST (slot 1 even, slot 8 odd);
 *   2. PTR[64] = stage_base() = top of big SRAM - STAGE_CHUNKS (mesh.h v3; 0xF4000 on 4 MB): C&C's staging, below
 *      the top 128 KB of sort scratch;
 *   3. CMD_JUMP -> jump to the address in the block's first slot; anything else -> ebreak.
 * Command set: CMD_JUMP only for now. Overlays, if the sort needs them, are swapped by the running image (6.5). */
#include "sort.h"
    .section .text.loader, "ax"
    .globl _loader
_loader:
    li    t3, COMMAND_BASE
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
    slli  t0, t0, SIZE_UNIT_SHIFT
    li    t1, STAGE_CHUNKS
    sub   t0, t0, t1
    li    t2, PTR_BASE + 4 * 64
    sw    t0, 0(t2)
    srli  t5, t4, 24
    li    t6, CMD_JUMP
    bne   t5, t6, 1f
    jr    a0
1:  ebreak
