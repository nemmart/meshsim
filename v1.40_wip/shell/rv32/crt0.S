/* crt0.S: the stack, the bss zeroed, shell_main; it never returns on the target. */
    .section .text.start, "ax"
    .globl _start
_start:
    la    sp, __stack_top
    la    t0, __bss_start
    la    t1, __bss_end
1:  bgeu  t0, t1, 2f
    sw    zero, 0(t0)
    addi  t0, t0, 4
    j     1b
2:  call  shell_main
3:  j     3b
