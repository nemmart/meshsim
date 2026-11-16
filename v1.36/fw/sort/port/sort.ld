/* sort.ld -- the sort image's layout, the port layer's (../PORTING.md), in place of the sort's sort.ld: one image, every
 * part resident, as meshsim v1.25's. cargo xtask runs it through the C preprocessor with the configuration (mesh.h, as
 * assembly sees it), so that every address comes from the specification's map.
 *   IMEM (from IMEM_BASE, 0x0100_0000): .text, crt0 first, at IMEM_BASE: the entry, _start, where the boot ROM jumps
 *     (rtl/tile/base/spec.md 6.5).
 *   DMEM (from DMEM_DATA, past the 16 reserved bytes and the arguments' row, mesh.h): .rodata and .data, which the
 *     master tile lands with the code, in whole 16-byte rows; then .bss, not loaded (crt0 zeroes it); then the stack,
 *     growing down from STACK_TOP, below the reply block, DMEM's top 1 KiB. */
#include "mesh.h"

OUTPUT_ARCH("riscv")
ENTRY(_start)

/* The least stack left between .bss and the reply block: meshsim's sort measured 192 B for the split's deepest call. */
STACK_MIN = 1024;

MEMORY {
  IMEM (rx) : ORIGIN = IMEM_BASE, LENGTH = IMEM_BYTES
  DMEM (rw) : ORIGIN = DMEM_DATA, LENGTH = STACK_TOP - DMEM_DATA
}

/* The two segments the master tile lands: the code and the data (.bss in the data segment's memory size, not its
 * file). */
PHDRS {
  code PT_LOAD;
  data PT_LOAD;
}

SECTIONS {
  .text : {
    KEEP(*(.text.init))
    *(.text .text.*)
    . = ALIGN(16);
  } > IMEM :code
  .data : {
    _data_start = .;
    *(.rodata .rodata.* .srodata .srodata.*)
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(16);
  } > DMEM :data
  .bss (NOLOAD) : { _bss_start = .; *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > DMEM :data
  _end = .;
  /* not loaded: the buffers' addresses, for cargo xtask (port.c) */
  .sort_layout 0 (INFO) : { KEEP(*(.sort_layout)) }
  /* the sort's test-only force path (sort.ld's), and what a static link has no use for */
  /DISCARD/ : { *(.text.bt_force) *(.eh_frame) *(.riscv.attributes) *(.comment) *(.note.*) *(.rela.*) }

  ASSERT(_start == IMEM_BASE, "_start must be at IMEM_BASE, where the boot ROM jumps")
  ASSERT(_end + STACK_MIN <= STACK_TOP, "data, .bss and the least stack do not fit below the reply block")
}
