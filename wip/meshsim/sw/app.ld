/* Program images (meshsim v1.2, spec 2.2, 4.11, 4.12): code in program memory from 0x01000000 (below the interim
 * resident loader at 0x01007F00), .rodata and .data in data memory from DMEM_FIRST (0x10: the first 16 bytes are
 * reserved) up to the reply block (0x7BC0-0x7FBF) and the command block (0x7FC0-0x7FFF); the stack grows down from
 * STACK_TOP (0x7BC0). .bss is not loaded: crt0 zeroes it. */
OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY {
  PMEM (rx) : ORIGIN = 0x01000000, LENGTH = 0x7F00
  DMEM (rw) : ORIGIN = 0x00000010, LENGTH = 0x7BB0
}
SECTIONS {
  .text : { *(.text.init) *(.text .text.*) } > PMEM
  .data : { _data_start = .; *(.rodata .rodata.* .srodata .srodata.*) *(.data .data.* .sdata .sdata.*) . = ALIGN(16); } > DMEM
  .bss (NOLOAD) : { _bss_start = .; *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > DMEM
  _end = .;
  /DISCARD/ : { *(.eh_frame) *(.riscv.attributes) *(.comment) }
}
