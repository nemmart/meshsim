/* Recipe 22: the image's start-up. The ROM jumps here with MIE = 0 (W1-03). */
#include "../../wip/sw/mesh-v1.2-draft.h"
    .section .text.start, "ax"
    .globl _start
/* RECIPE crt0 */
_start:
    li   sp, STACK_TOP                  /* the stack grows down from below the reply block */
    la   t0, trap_handler               /* this image's handler (recipe 24); the ROM's served until now */
    csrw mtvec, t0
    la   t0, __bss_start                /* zero .bss: .bss is not loaded */
    la   t1, __bss_end
1:  bgeu t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:  li   t0, 1 << 11                    /* enable the hard interrupt: mie.MEIE (assumed; Q1, the CSR list) */
    csrs mie, t0
    csrsi mstatus, 1 << 3               /* and mstatus.MIE: from here an error traps (FINDINGS F2) */
    call main                           /* main never returns: it loops in wait_for_command() */
3:  j    3b
/* END */
