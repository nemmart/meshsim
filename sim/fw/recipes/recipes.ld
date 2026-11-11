/* The v1.2 recipes' image layout (wip/sw/recipes/, linked by meshsim; their crt0.S is recipe 22's): code in program
 * memory from 0x01000000 (.text.start first), .rodata and .data in data memory from 0x10, the stack from 0x7BC0 down. */
OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY {
  PMEM (rx) : ORIGIN = 0x01000000, LENGTH = 0x8000
  DMEM (rw) : ORIGIN = 0x00000010, LENGTH = 0x7BB0
}
SECTIONS {
  .text : { *(.text.start) *(.text.init) *(.text .text.*) } > PMEM
  .data : { _data_start = .; *(.rodata .rodata.* .srodata .srodata.*) *(.data .data.* .sdata .sdata.*) . = ALIGN(16); } > DMEM
  .bss (NOLOAD) : { __bss_start = .; _bss_start = .; *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(16); __bss_end = .; _bss_end = .; } > DMEM
  _end = .;
  /DISCARD/ : { *(.eh_frame) *(.riscv.attributes) *(.comment) }
}
