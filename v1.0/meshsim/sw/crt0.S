/* Program start-up (meshsim v4.0): entered from the resident loader's CMD_JUMP with the code in instruction memory and
 * .rodata/.data in data memory. Sets the stack, zeroes .bss, calls main(); if main returns, ebreak with a0 = its
 * return value (a simulator-only exit; real programs loop on wait_for_command()). */
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
    ebreak
3:  j     3b
