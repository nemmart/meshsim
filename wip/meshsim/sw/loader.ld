/* PHASE-2 INTERIM: the resident loader at the top 256 bytes of program memory (Registers.LOADER_BASE). */
OUTPUT_ARCH("riscv")
ENTRY(_loader)
MEMORY { LOADER (rx) : ORIGIN = 0x01007F00, LENGTH = 0x100 }
SECTIONS { .text : { *(.text.loader) } > LOADER  /DISCARD/ : { *(.riscv.attributes) *(.comment) } }
