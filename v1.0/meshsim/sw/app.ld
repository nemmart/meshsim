/* Program images (meshsim v4.0, design v2): code in instruction memory below the resident loader, data in data
 * memory below the mailbox; the stack grows down from STACK_TOP (0x5C00). .bss is not loaded: crt0 zeroes it. */
OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY {
  IMEM (rx) : ORIGIN = 0x0000, LENGTH = 0x1F00
  DMEM (rw) : ORIGIN = 0x4000, LENGTH = 0x1C00
}
SECTIONS {
  .text : { *(.text.init) *(.text .text.*) } > IMEM
  .data : { *(.rodata .rodata.* .srodata .srodata.*) *(.data .data.* .sdata .sdata.*) . = ALIGN(8); } > DMEM
  .bss (NOLOAD) : { _bss_start = .; *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > DMEM
  _end = .;
  /DISCARD/ : { *(.eh_frame) *(.riscv.attributes) *(.comment) }
}
