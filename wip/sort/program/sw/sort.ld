/* sort.ld -- the sort image's layout (worker 6; v1.2: WORKER8). One image, no overlays (v1.1's M7 overlays and the
 * shared region are gone: 32 KB + 32 KB hold everything), on the v1.2 map (spec 2.2, CONVENTIONS 1):
 *   program memory 0x01000000-0x01007EFF: .text (crt0 first). The interim resident loader sits at 0x01007F00 until
 *     meshsim's boot ROM (WORKER4's phase 4), so the image stops below it.
 *   data memory 0x00000010-0x00007BBF: .rodata and .data (loaded), then .bss (zeroed by crt0), then the stack, growing
 *     down from STACK_TOP (0x7BC0), below the reply block (0x7BC0) and the command block (0x7FC0). Bytes 0x00-0x0F are
 *     reserved (D37.4). */
OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY {
  PMEM (rx) : ORIGIN = 0x01000000, LENGTH = 0x7F00
  DMEM (rw) : ORIGIN = 0x00000010, LENGTH = 0x7BB0
}
SECTIONS {
  .text : { *(.text.init) *(.text .text.*) . = ALIGN(16); } > PMEM
  .data : { _data_start = .; *(.rodata .rodata.* .srodata .srodata.*) *(.data .data.* .sdata .sdata.*) . = ALIGN(16); } > DMEM
  .bss (NOLOAD) : { _bss_start = .; *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > DMEM
  _end = .;
  /DISCARD/ : { *(.text.bt_force) *(.eh_frame) *(.riscv.attributes) *(.comment) *(.note.*) }
}
