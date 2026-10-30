/* PHASE-2 INTERIM (until phase 4's boot ROM, 6.2): v1.1's resident loader moved to the v1.2 map, at LOADER_BASE
 * (0x01007F00, the top 256 bytes of program memory). HALT -> RUN starts here. It reads the command word in command slot
 * 0 and the first slot of its argument block (slot 1 for an even command, 8 for an odd one); CMD_JUMP jumps to the
 * address there, anything else is a simulator-only exit (SIM_EXIT = 0xBAD10ADE). */
#include "mesh.h"
    .section .text.loader, "ax"
    .globl _loader
_loader:
    li    t3, COMMAND_BASE
    lw    t4, 0(t3)
    srli  t5, t4, 23
    andi  t5, t5, 1
    addi  a0, t3, 4 * ARGS_EVEN
    beqz  t5, 2f
    addi  a0, t3, 4 * ARGS_ODD
2:  lw    a0, 0(a0)
    srli  t5, t4, 24
    li    t6, CMD_JUMP
    bne   t5, t6, 1f
    jr    a0
1:  li    t0, 0xBAD10ADE
    sw    t0, X0REL(OFF_SIM_EXIT)(zero)
3:  j     3b
