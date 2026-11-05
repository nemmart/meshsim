/* Bare RV64 guests for the item 113 differential runs: code at RAM_START (0x80000000), as both tracers load it.
 * Two program headers: .text alone in an executable segment, .data/.bss and the stack in a separate, page-aligned,
 * non-executable one, because jolt-cpp decodes the whole executable segment as code (the tracer session's
 * guest-split.ld). No jolt-sdk: each program sets sp itself and ends with `j .`. */
OUTPUT_ARCH("riscv")
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);          /* R X */
  data PT_LOAD FLAGS(6);          /* R W */
}
SECTIONS {
  . = 0x80000000;
  .text : { *(.text.init) *(.text .text.*) } :text
  . = ALIGN(0x1000);
  .data : { *(.data .data.* .sdata .sdata.* .rodata .rodata.* .srodata .srodata.*) } :data
  .bss  : { *(.bss .bss.* .sbss .sbss.* COMMON) } :data
  . = ALIGN(16);
  _stack_bottom = .;
  . += 4096;
  _stack_top = .;
  /DISCARD/ : { *(.riscv.attributes) *(.comment) }
}
