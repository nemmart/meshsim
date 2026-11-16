# Firmware startup for test programs on the core.
#
# The boot load has written the read-only data and .data into DMEM and zeroed
# .bss (link.ld), so before main runs this code only sets the global pointer and
# the stack pointer. The rest of DMEM, the stack included, is dirty.
#
# main's return value is reported to the test memory's result device:
# 0 reports success; anything else reports failure with that value as the code.

    .section .text.start, "ax"
    .globl _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top

    call    main

    # Report the result: 0x2000_0000 is "pass", 0x2000_0004 is "fail with code".
    li      t0, 0x20000000
    bnez    a0, report_failure
    sw      zero, 0(t0)
    j       finished
report_failure:
    sw      a0, 4(t0)
finished:
    j       finished
