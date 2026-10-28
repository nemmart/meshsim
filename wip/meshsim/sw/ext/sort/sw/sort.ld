/* sort.ld -- the sort image's layout (worker 6; overlays: worker 8, M7, the memory map agreed with the lead 3 Oct).
 *
 * Instruction memory (0x0000-0x1EFF; the resident loader is at 0x1F00):
 *   .text       the kernel, resident: crt0, the command loop and ov_load (main.c), the exchanges (exchange.c), the
 *               planner's shared helpers (skew_common.c's cost functions, udiv32,
 *               stage_addr).
 *   the region  at _ov_base, one image at a time (the OVERLAY below, NOCROSSREFS: no image may call another):
 *     .ov_local   L1, L2 (local.c)                                 at boot; again at L2's start
 *     .ov_split   worker 4's split (split_bisect.o)                L1's tail
 *     .ov_xfer    XB, RED, stage 1, the direct/LB configure, K, V (transfer.c, config.c; sw_prepare, sw_stage1)   SPLIT's
 *                 tail; again when stage 2 or 3 settles on direct or LB (TH's or DESC's tail)
 *     .ov_th      stage 2 and TH (skew.c, skew_th.c; bt_select, bt_estimate)   RED's tail (stage 2 wanted)
 *     .ov_btplan  SWEEP, DESC (bt_plan.c; the sweep, descriptors, held statistics, decision)   TH's tail (BT)
 *     .ov_btrun   BT's layer phases (bt_run.c; bt_hop*)            DESC's tail (BT)
 *   Load addresses (LMA) start at STAGE_LMA, consecutive, each image a multiple of 8 bytes: build.sh concatenates the
 *   images in this order into sort.stage.bin, which C&C stages at stage_base() at boot; ov_load() copies one in.
 * Data memory (0x4000-0x5BBF; the stack grows down from 0x5BC0, below the reply and command blocks): .rodata and
 *   .data (all of it resident), then .bss
 *   with the SHARED region: the split's state over the planner's Tile and RED's buffers (.bss.plan), never live
 *   together; the split's outputs c and n are in S, outside it. crt0 zeroes .bss once, at boot. */
OUTPUT_ARCH("riscv")
ENTRY(_start)
MEMORY {
  IMEM (rx) : ORIGIN = 0x0000, LENGTH = 0x1F00
  DMEM (rw) : ORIGIN = 0x4000, LENGTH = 0x1BC0
}
SECTIONS {
  .text : {
    *(.text.init)
    EXCLUDE_FILE(*local.o *transfer.o *config.o *skew.o *skew_th.o *skew_bt.o *bt_plan.o *bt_run.o *split_bisect.o *skew_common.o) *(.text .text.*)
    *skew_common.o(.text.run_cost .text.run_cost_flat .text.red_cost .text.udiv32 .text.stage_addr)
    . = ALIGN(8);
  } > IMEM
  OVERLAY : NOCROSSREFS AT (0x10000000) {
    .ov_local  { *local.o(.text .text.*) . = ALIGN(8); }
    .ov_split  { *split_bisect.o(.text .text.*) . = ALIGN(8); }
    .ov_xfer   { *transfer.o(.text .text.*) *config.o(.text .text.*) *skew_common.o(.text.sw_prepare .text.sw_stage1) . = ALIGN(8); }
    .ov_th     { *skew.o(.text .text.*) *skew_th.o(.text .text.*) *skew_bt.o(.text.bt_select .text.bt_estimate) . = ALIGN(8); }
    .ov_btplan { *bt_plan.o(.text .text.*) *skew_bt.o(.text.bt_sweep_init .text.bt_sweep_cell .text.bt_sweep_cell_c .text.bt_sweep_done .text.bt_desc .text.bt_held_stats .text.bt_decide) . = ALIGN(8); }
    .ov_btrun  { *bt_run.o(.text .text.*) *skew_bt.o(.text.bt_hop1_src .text.bt_hop1_m .text.bt_hop2_m .text.bt_hop2_d) . = ALIGN(8); }
  } > IMEM
  _ov_base = ADDR(.ov_local);
  .data : { *(.rodata .rodata.* .srodata .srodata.*) *(.data .data.* .sdata .sdata.*) . = ALIGN(8); } > DMEM
  _bss_start = .;
  OVERLAY : {
    .shared_split { *split_*.o(.sbss .sbss.* .bss .bss.* COMMON) *(.bss.split) . = ALIGN(8); }
    .shared_plan  { *(.bss.plan) . = ALIGN(8); }
  } > DMEM
  _shared_start = ADDR(.shared_split);
  _shared_end = .;
  .bss (NOLOAD) : { *(.sbss .sbss.* .bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; } > DMEM
  _end = .;
  /DISCARD/ : { *(.text.bt_force) *(.eh_frame) *(.riscv.attributes) *(.comment) *(.note.*) }
}
