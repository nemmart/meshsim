/* Recipe 22: the image's start-up. The ROM jumps here with MIE = 0 (W1-03) and mtvec at the ROM's handler;
 * both stay so until boot part 2 (D37.2). */
#include "../../wip/sw/mesh-v1.2-draft.h"
    .section .text.start, "ax"
    .globl _start
/* RECIPE crt0 */
_start:
    li   sp, STACK_TOP                  /* the stack grows down from below the reply block */
    la   t0, __bss_start                /* zero .bss: .bss is not loaded */
    la   t1, __bss_end
1:  bgeu t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:  call main                           /* never returns; interrupts stay off until boot part 2 (D37.2) */
3:  j    3b
/* END */
