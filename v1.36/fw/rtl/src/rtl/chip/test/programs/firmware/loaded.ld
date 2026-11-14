/* Memory layout of the images the master tile loads (rtl/tile/base/spec.md 6.5), the compute tiles' replay image and the
 * tracer tile's (src/bin/replay.rs, src/bin/tracer.rs), on the base tile's map (5.1, 6.3): the code in IMEM, from its
 * base, 0x0100_0000, its first word _start, where the boot ROM jumps; the data in DMEM from TILE_DATA, past the 16
 * reserved bytes and the arguments' row (DMEM_FIRST, as the sort's image keeps it: sort/port/mesh.h), the stack growing
 * down from DMEM's top. The master lands the code and the loaded data (.rodata, .data), in whole 16-byte rows, and
 * nothing else: loaded_start.S zeroes .bss, before main, some 7 cycles a word; .noinit, after it, holds what an image
 * writes or lands before it reads it, which nothing else zeroes (the core-fed set's tables, src/lib.rs, emptied where
 * the image chooses, and the buffers packets land in); and .summary, last, an image's report, which the chip's test
 * reads after the run (the tracer tile's, src/bin/tracer.rs).
 * IMEM_BYTES and DMEM_BYTES must be given with --defsym; cargo xtask (xtask/src/sort_images.rs) passes the
 * capacities configured in Veryl.toml. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

IMEM_BASE = 0x01000000;
TILE_DATA = 0x20;
STACK_BYTES = 1K;

MEMORY
{
    IMEM (rx) : ORIGIN = 0x01000000, LENGTH = 16M
    DMEM (rw) : ORIGIN = 0x00000020, LENGTH = 16M - 32
}

SECTIONS
{
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > IMEM

    .rodata : ALIGN(16) {
        __data_start = .;
        *(.rodata .rodata.* .srodata .srodata.*)
    } > DMEM

    .data : ALIGN(4) {
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > DMEM

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DMEM

    .noinit (NOLOAD) : ALIGN(16) {
        *(.noinit .noinit.*)
        . = ALIGN(4);
    } > DMEM

    .summary (NOLOAD) : ALIGN(16) {
        KEEP(*(.summary))
        . = ALIGN(4);
        __image_end = .;
    } > DMEM

    __global_pointer$ = __data_start + 0x800;
    __stack_top = DMEM_BYTES;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame .eh_frame_hdr)
    }

    ASSERT(_start == IMEM_BASE, "_start must be IMEM's first word, where the boot ROM jumps")
    ASSERT(ADDR(.rodata) == TILE_DATA, "the loaded data must start at TILE_DATA, where the master lands it")
    ASSERT(ADDR(.text) + SIZEOF(.text) <= IMEM_BASE + IMEM_BYTES, "the image does not fit in IMEM")
    ASSERT(__image_end + STACK_BYTES <= __stack_top, "the data and a 1 KiB stack do not fit in DMEM")
}
