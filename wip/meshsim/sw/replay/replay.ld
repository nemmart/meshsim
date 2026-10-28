/* The replay controller's image: code from 0x0000 below the resident loader (0x1F00), data from 0x4000 below the
 * reply block (0x5BC0, 4.12); the stack shares data memory, growing down from 0x5BC0. */
OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY {
  IMEM (rx) : ORIGIN = 0x0000, LENGTH = 0x1F00
  DMEM (rw) : ORIGIN = 0x4000, LENGTH = 0x1BC0
}
SECTIONS {
  .text : { *(.text.init) *(.text .text.*) } > IMEM
  .data : { *(.rodata .rodata.* .srodata .srodata.*) *(.data .data.* .sdata .sdata.*) . = ALIGN(16); } > DMEM
  .bss (NOLOAD) : { _bss_start = .; *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(16); _bss_end = .; } > DMEM
  _end = .;
  /DISCARD/ : { *(.eh_frame) *(.riscv.attributes) *(.comment) }
}
