/* Memory layout of the chip's firmware (start.S, src/), the master tile's and the memory tile's, on the base tile's map
 * (rtl/tile/base/spec.md 5.1, 6.3): the code in IMEM, from its base, 0x0100_0000, its first word _start, where the boot
 * ROM jumps (6.5); the data in DMEM, from its first byte past the 16 reserved, the stack growing down from DMEM's top.
 *
 * DMEM's first data is the summary (.summary, the master's src/bin/master.rs), which the chip's test reads, then the
 * read-only data (the master's run, the image it loads on the tiles, linked in by cargo xtask as an object of its own),
 * .data and .bss. The chip's tests place the master's IMEM and DMEM, and the memory tile's, in their SRAMs during the
 * chip's reset, .bss's zeros included (rtl/chip/test/src/bench.rs). IMEM_BYTES and DMEM_BYTES must be given with
 * --defsym; cargo xtask (xtask/src/sort_images.rs) passes the capacities configured in Veryl.toml. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

IMEM_BASE = 0x01000000;
DMEM_FIRST = 0x10;
STACK_BYTES = 1K;

MEMORY
{
    IMEM (rx) : ORIGIN = 0x01000000, LENGTH = 16M
    DMEM (rw) : ORIGIN = 0x00000010, LENGTH = 16M - 16
}

SECTIONS
{
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > IMEM

    .summary : ALIGN(16) {
        KEEP(*(.summary))
    } > DMEM

    .rodata : ALIGN(16) {
        *(.rodata .rodata.* .srodata .srodata.*)
    } > DMEM

    .data : ALIGN(4) {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > DMEM

    .bss (NOLOAD) : ALIGN(4) {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DMEM

    __global_pointer$ = __data_start + 0x800;
    __stack_top = DMEM_BYTES;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame .eh_frame_hdr)
    }

    ASSERT(_start == IMEM_BASE, "_start must be IMEM's first word, where the boot ROM jumps")
    ASSERT(ADDR(.summary) == DMEM_FIRST, "the summary must be DMEM's first data, where the chip's test reads it")
    ASSERT(ADDR(.text) + SIZEOF(.text) <= IMEM_BASE + IMEM_BYTES, "the firmware does not fit in IMEM")
    ASSERT(__bss_end + STACK_BYTES <= __stack_top, "the data and a 1 KiB stack do not fit in DMEM")
}
