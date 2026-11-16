/* Memory layout for firmware test programs (start.S, isa_edges.S, Rust).
 *
 * Code runs from IMEM, which the core only fetches from (a data access to IMEM
 * faults). Read-only data, .data and .bss live in DMEM, with the stack at the top
 * of DMEM. The boot load writes the code into IMEM and the read-only data and
 * .data into DMEM, each at its run address, and zeroes .bss (the part of the data
 * segment past its file bytes); the rest of DMEM is dirty.
 * IMEM_BYTES and DMEM_BYTES must be given with --defsym; `cargo xtask` passes
 * the capacities configured in Veryl.toml. There are deliberately no defaults here:
 * a script default can silently win over --defsym in some linkers.
 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

STACK_BYTES = 1K;

MEMORY
{
    IMEM (rx) : ORIGIN = 0x00000000, LENGTH = 1024M
    DMEM (rw) : ORIGIN = 0x10000000, LENGTH = 1024M
}

SECTIONS
{
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > IMEM

    .rodata : ALIGN(4) {
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
    __stack_top = ORIGIN(DMEM) + DMEM_BYTES;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame .eh_frame_hdr)
    }

    ASSERT(ADDR(.text) + SIZEOF(.text) <= ORIGIN(IMEM) + IMEM_BYTES, "program does not fit in IMEM")
    ASSERT(__bss_end + STACK_BYTES <= __stack_top, "data and a 1 KiB stack do not fit in DMEM")
}
