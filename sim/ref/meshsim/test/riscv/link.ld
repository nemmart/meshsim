OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY { CODE (rx) : ORIGIN = 0x01000000, LENGTH = 0x7F00  DATA (rw) : ORIGIN = 0x00000010, LENGTH = 0x7BB0 }
SECTIONS {
  .text : { *(.text.init) *(.text .text.*) } > CODE
  .data : { *(.data .data.* .sdata .sdata* .rodata .rodata* .bss .bss* .sbss*) } > DATA
  /DISCARD/ : { *(.eh_frame) *(.riscv.attributes) }
}
