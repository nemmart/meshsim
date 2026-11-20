/* link.ld: the link-only RV32 build (SHELL-SPEC sections 3 and 12). The addresses are nominal: the build is for the
 * sizes and for the undefined symbols, not for running. No __global_pointer$, so no gp relaxation. */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY { RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 1M }

SECTIONS {
    .text   : { KEEP(*(.text.start)) *(.text .text.*) } > RAM
    .rodata : { *(.rodata .rodata.*) } > RAM
    .srodata : { *(.srodata .srodata.*) } > RAM
    .data   : { *(.data .data.*) } > RAM
    .sdata  : { *(.sdata .sdata.*) } > RAM
    .bss (NOLOAD) : { . = ALIGN(4); __bss_start = .; *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
    .stack (NOLOAD) : { . = ALIGN(16); . += 2048; __stack_top = .; } > RAM
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) }
}
