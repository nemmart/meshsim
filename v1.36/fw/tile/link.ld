/* Memory layout of a rebuilt tile's program (start.S and one program). cargo xtask
 * runs it through the C preprocessor with the capacities, so that every address comes
 * from tile_map.h.
 *
 * Code runs from IMEM, which the core only fetches from, its first word _start at
 * IMEM_BASE, where the boot ROM jumps (6.5); read-only data, .data and .bss live in
 * DMEM from its first byte past the reserved 16 (6.3) and the arguments (tile_map.h)
 * up to its top. The test writes the code into IMEM and the read-only data
 * and .data into DMEM, each at its load address, and zeroes .bss (the part of the
 * data segment past its file bytes). Linked with a 16-byte page size, each segment
 * starts a 16-byte row, with no more padding than that.
 */
#include "tile_map.h"

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    IMEM (rx) : ORIGIN = IMEM_BASE, LENGTH = IMEM_BYTES
    DMEM (rw) : ORIGIN = ARGUMENTS + ARGUMENT_BYTES, LENGTH = DMEM_BYTES - DMEM_RESERVED - ARGUMENT_BYTES
}

/* The two segments the test writes: the file's own headers are in neither. */
PHDRS
{
    code PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > IMEM :code

    .rodata : ALIGN(16) {
        *(.rodata .rodata.* .srodata .srodata.*)
    } > DMEM :data

    .data : ALIGN(4) {
        *(.data .data.* .sdata .sdata.*)
    } > DMEM :data

    .bss (NOLOAD) : ALIGN(4) {
        *(.sbss .sbss.* .bss .bss.*)
    } > DMEM :data

    /* The RISC-V attributes would get a program header of their own, which the
     * test skips: discarded, the file holds only the two segments' headers. */
    /DISCARD/ : {
        *(.comment)
        *(.riscv.attributes)
    }
}
