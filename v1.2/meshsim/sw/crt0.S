/* Program start-up (meshsim v1.2): entered from the boot ROM's jump (6.2) with the code in program memory and .rodata/.data
 * in data memory. Sets the stack, zeroes .bss, calls main(); when main returns, its return value goes to the
 * simulator-only exit word (8.15), which ends the test. Real programs loop on wait_for_command(). */
#include "mesh.h"
    .section .text.init
    .globl _start
_start:
    li    sp, STACK_TOP
    la    t1, _bss_start
    la    t2, _bss_end
1:  bgeu  t1, t2, 2f
    sw    zero, 0(t1)
    addi  t1, t1, 4
    j     1b
2:  call  main
    sw    a0, X0REL(OFF_SIM_EXIT)(zero)
3:  j     3b
