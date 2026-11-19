/* RV32 planner image for the cycle-count emulator: code at 0, data at 0x8000 */
ENTRY(_start)
SECTIONS
{
  . = 0x0;
  .text : { *(.text*) }
  . = 0x8000;
  .rodata : { *(.rodata*) *(.srodata*) }
  .data : { *(.data*) *(.sdata*) }
  .bss : { *(.bss*) *(.sbss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) }
}
