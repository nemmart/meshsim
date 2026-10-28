/* Start-up for the replay controller (local: mesh.h's STACK_TOP predates 26n's reply block). The stack grows down
 * from 0x5BC0, the bottom of the reply block (4.12); crt0 zeroes .bss, calls main(), and on return executes ebreak
 * with a0 = main's value (meshsim's exit for test programs). */
    .section .text.init
    .globl _start
_start:
    li    sp, 0x5BC0
    la    t1, _bss_start
    la    t2, _bss_end
1:  bgeu  t1, t2, 2f
    sw    zero, 0(t1)
    addi  t1, t1, 4
    j     1b
2:  call  main
    ebreak
3:  j     3b
