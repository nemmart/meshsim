/* Shared prologue and markers. MARK(n) is `addi t6, x0, n`: a one-row ADDI whose rd value n brackets the rows
 * under test, so they can be found in either tracer's rows by rd_reg = 31 (t6) and rd = n. */
#define MARK(n) addi t6, x0, n
    .data
    .balign 8
_guest_data: .dword 0         /* every guest has a non-empty data segment (no empty LOAD at address 0) */
    .section .text.init
    .globl _start
_start:
    la    sp, _stack_top
