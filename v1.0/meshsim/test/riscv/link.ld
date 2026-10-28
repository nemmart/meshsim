OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY { CODE (rx) : ORIGIN = 0x0000, LENGTH = 0x1F00  DATA (rw) : ORIGIN = 0x4000, LENGTH = 0x1C00 }
SECTIONS {
  .text : { *(.text.init) *(.text .text.*) } > CODE
  .data : { *(.data .data.* .sdata .sdata* .rodata .rodata* .bss .bss* .sbss*) } > DATA
  /DISCARD/ : { *(.eh_frame) *(.riscv.attributes) }
}
