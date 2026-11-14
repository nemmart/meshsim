# Start-up of the chip's firmware, the master tile's and the memory tile's. The boot
# ROM jumps here, IMEM's base, from the image the chip's test placed in IMEM and DMEM
# before the reset ended (rtl/tile/base/spec.md 6.5: PARAMS.boot_local 1 on both
# tiles). .bss's zeros are placed with the image, so this code only sets the global
# pointer and the stack pointer and calls main, which does not return (src/bin/).
# mtvec stays at the boot ROM's handler unless the program moves it.

    .section .text.start, "ax"
    .globl _start
_start:
    .option push
    .option norelax
    la      gp, __global_pointer$
    .option pop
    la      sp, __stack_top
    call    main
1:
    wfi
    j       1b
