# Start-up of the images the master tile loads (loaded.ld): the compute tiles' replay image and the tracer tile's. The
# boot ROM jumps here, IMEM's base, once the load has landed (rtl/tile/base/spec.md 6.5). The master lands the code and
# the loaded data alone, and no hardware clears DMEM, so this code zeroes .bss, sets the global pointer and the stack
# pointer and calls main, which ends the image itself (src/lib.rs, end_image). mtvec stays at the boot ROM's handler.

    .section .text.start, "ax"
    .globl _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top
    la      t0, __bss_start
    la      t1, __bss_end
1:
    bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    call    main
3:
    wfi
    j       3b
