OUTPUT_ARCH("riscv")
ENTRY(_loader)
MEMORY { LOADER (rx) : ORIGIN = 0x1F00, LENGTH = 0x100 }
SECTIONS { .text : { *(.text.loader) } > LOADER  /DISCARD/ : { *(.riscv.attributes) *(.comment) } }
