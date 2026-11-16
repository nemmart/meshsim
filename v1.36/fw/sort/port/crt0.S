/* Program start-up on the RV32 core, the port layer's (../PORTING.md), in place of meshsim's crt0.S: entered from the
 * boot ROM's jump to IMEM's base at the release (rtl/tile/base/spec.md 6.5), with the code in IMEM, and .rodata,
 * .data and the arguments' row (mesh.h, ARGS_BASE) in DMEM, all landed by the master tile's load.
 *   - the stack below the reply block (STACK_TOP); .bss zeroed;
 *   - port_start(): the arguments read (port.c);
 *   - main, the sort's fixed schedule of phases, each ending at the barrier; its return value to port_exit(), the
 *     tile's EXIT request to the master tile, and the wait for its reply (port.c);
 *   - then the image's end (6.5): a jump to the reset PC, every counter at 0, where the boot ROM asks for the next
 *     load.
 * Every tile runs this code alike from the release, as the sort's first phase needs (6.4, Lockstep). The ROM leaves
 * mstatus.MIE 0 and mie.MEIE set, so the wake releases a WFI and never traps, and mtvec at its own handler, which the
 * image keeps: a trap sends the master tile the ROM's reports 24 and 25 and parks (6.2, 6.5). */
#include "mesh.h"
    .section .text.init, "ax"
    .globl _start
_start:
    li    sp, STACK_TOP
    la    t1, _bss_start
    la    t2, _bss_end
1:  bgeu  t1, t2, 2f
    sw    zero, 0(t1)
    addi  t1, t1, 4
    j     1b
2:  call  port_start
    call  main
    call  port_exit                 /* a0: main's return value */
    li    t0, RESET_PC              /* the image's end (6.5) */
    jr    t0
