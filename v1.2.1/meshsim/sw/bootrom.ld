/* The boot ROM at the reset vector (6.2): region 0x02. */
OUTPUT_ARCH("riscv")
ENTRY(_reset)
MEMORY { ROM (rx) : ORIGIN = 0x02000000, LENGTH = 0x1000 }
SECTIONS { .text : { *(.bootrom) } > ROM  /DISCARD/ : { *(.riscv.attributes) *(.comment) } }
