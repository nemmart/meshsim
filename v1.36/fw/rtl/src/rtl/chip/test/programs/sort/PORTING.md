# The sort on Snarky One's tile

The mesh-sorter's tile sort software, meshsim v1.25's (the sort design, the
submodule `external/meshsim`, its `v1.25/meshsim/sw/ext/`), vendored and ported to
the base tile ([the base tile specification](../../../../tile/base/spec.md), which
refines meshsim's machine). `cargo xtask sort-images` builds it
([sort_images.rs](../../../../../xtask/src/sort_images.rs), which also says how it
is audited and what the chip's test gets). The master tile's firmware, which runs
it, is in [../firmware](../firmware/src/bin/master.rs). meshsim v1.35 holds the
same sources in `v1.35/fw/sort/`: `program/sw/` byte for byte, and `skew/planner/`
and `split/` with `program/patches/` applied; its SortTest is `sort_test`
(`v1.35/tests/sort_main.cpp` and `sort_input.h`).

## Sources

Vendored unchanged from `v1.25/meshsim/sw/ext/`, unless listed under
[Changes](#changes):

| Directory | From | What |
| --- | --- | --- |
| `sw/` | `ext/sort/sw/` | the sort: the command loop (`main.c`, here a fixed schedule), the local sorts (`local.c`), the exchanges and the reduction (`exchange.c`), the transfer (`transfer.c`, `config.c`), TH and BT (`skew.c`, `bt_plan.c`, `bt_run.c`), `sort.h` |
| `skew-planner/` | `ext/skew-planner/` | the skew planner: `skew.h`, `skew_common.c`, `skew_th.c`, `skew_bt.c` |
| `split-bisect/` | `ext/split-bisect/` | the split: `split.c` (its `SPLIT_BARRIER` hook), `split.h`, `split_regs.h` |

The port layer, `port/`, replaces meshsim's generated `sw/mesh.h` 1.2, its
`sw/crt0.S` and the sort's `sort.ld`:

| File | What |
| --- | --- |
| `mesh.h` | the sort's view of the tile: every name of meshsim's mesh.h the sort uses, at the specification's addresses and in its layouts, and the port's helpers; the configuration's checks |
| `crt0.S` | start-up, from the boot ROM's jump, and the image's end, a jump to the reset PC |
| `port.c` | the input-load stand-in, from the image's arguments, the tile's exit request to the master tile and the wait for its reply, and the buffers' layout table for cargo xtask |
| `sort.ld` | the image's layout: one image, every part resident, as v1.25's |

## Changes

Each change is marked "Snarky One port" in its file: in the header, or on the
changed line.

| File | Change | Why |
| --- | --- | --- |
| `sw/local.c`, `sw/config.c`, `sw/transfer.c`, `sw/skew.c`, `sw/bt_run.c` | a store to `DMA_CONFIG` becomes `dma_config()`, and one to `DMA_RECORD_BASE_ADDRESS` `record_base()` | v1.25's `DMA_CONFIG` holds the transfer, the receive digits and the gather's *k*, which the specification splits into `DMA_CONFIG`, `RECEIVE_CONFIG` and `GATHER_CONFIG` (8.1, 12.1, 13); its record base is `GATHER_CONFIG.record_base` |
| `sw/skew.c`, `sw/bt_run.c` | a store of a `GROUP` entry's high word becomes `set_group_hi()` | v1.25's high word (first stream, streams, gap) is the specification's word 0 above R and word 1 (8.4) |
| `skew-planner/skew.h` | `HW_GROUP_R` stores R through `set_group_r()` | the same: R shares word 0 |
| `split-bisect/split_regs.h`, `split-bisect/split.c` | `SRAM_RD_DEST_ADDR`'s store becomes `sr_read_dest32()`, and `SRAM_RD_ADDR_QUEUE`'s store and load `READ_SEND` and `READ_FREE` | the read unit's setup is two registers, `READ_HDR` and `READ_DEST_ADDR`, and its queue's word and free count two (10) |
| `sw/main.c` | the command loop becomes a fixed schedule: `main` runs each phase, in C&C's order for the run's format, as its command's case did, then the barrier (`sort_barrier()`), and returns 0 after L2's, to the master tile; the format is the image's argument | there are no commands (the specification's 6.5): an image's phases are fixed in it, and its arguments are data in DMEM; meshsim's exit word is the simulator's |

## The port layer's decisions

**The map.** The specification's (5.1, 5.2): DMEM at 0, IMEM at 0x0100_0000, the
scratchpad at 0x0300_0000, the tile window at 0xFFFF_F800, the tables from
0xFFFF_E000, in v1.25's order and layout. `mesh.h`'s `TileRegs` is the tile
window, its block 2's words and block 5's counters under v1.25's names
(`ROUTER_*`, `WRITE_COUNTER[n − 1]`); `TableRegs` the tables. DMEM's first row
past its reserved 16 bytes holds the image's arguments (`ARGS_BASE`: the records
per tile and the format), its data follows (`DMEM_DATA`), and its top 1 KiB is,
by v1.25's convention, the reply block, with the stack below it. cargo xtask gives
the preprocessor the sizes from Veryl.toml (`MESH_SIZE`, `IMEM_BYTES`,
`DMEM_BYTES`, `SCRATCHPAD_BYTES`). The sort is the 64-tile, 4 MiB sort (its `NT`,
`& 63`, `AREA` placement), so the build refuses any other mesh or scratchpad size.

**Layouts.** The header block (size, counter, mode) is the low byte of every
header register, with `dst` above it (5.4): `HDR_*`, `ROUTER_HDR_W` and
`STREAM_COUNT_W` build it, so the sort's headers and `STREAM_HDR` entries keep
their callers. v1.25's per-stream gap (`STREAM_SRC_W`) is the specification's
per-transfer `DMA_CONFIG.gap`: every stream and transfer the sort runs has gap 0,
and `dma_config()` stores 0. `GROUP_LO_R` and `GROUP_HI` keep v1.25's two words,
which `set_group()`, `set_group_hi()` and `set_group_r()` store into the
specification's (8.4), loading the entry's other fields where they share a word
(a table load, 6.4; the core may load any entry, 8.6). `dma_config()` keeps v1.25's
meaning of one register: every receive and gather setting is the last
`DMA_CONFIG`'s.

**Events.** `EV_*` are v1.25's names at the specification's bits (7.2). `wfi()`
is the WFI and then a load of `EVENTS`, one asm statement; `wait_all()` loops on
the events it waits for, the specification having no `EVENTS_REQUIRED`.
`arrive_and_wait()`, the barrier, is three fixed instructions: the store to
`ARRIVE`, the WFI (the canonical wait, which only the release ends, 7.3) and the
load of `EVENTS` that consumes the release; `sort_barrier()` (main.c) is its one
copy, where every phase and every split barrier ends. cargo xtask checks the
disassembly: every WFI is followed at once by a load of `EVENTS`, and exactly one,
the barrier's, comes right after its `ARRIVE`.

**Lockstep (6.4).** From the release's wake to a start store every tile runs the
same instructions in the same cycles: from a barrier's, `sort_barrier()`'s return
into `main`'s schedule (crt0's start-up, `.bss`'s zeroing, ends at the first),
then `k_start()`, `v_start()`, `th_phase()` or `bt_phase()` to `start_run()`; its
loads and stores are DMEM's and the tile window's, at their fixed latencies.

**Boot.** The image is one ELF: crt0 at IMEM's base, then every function. Each
compute tile's boot ROM arms counter 7 and sends the master tile a `LOAD`; once
every one has come, the master broadcasts the image's code and its loaded data
(`.rodata`, `.data`), then the arguments' row, as uncounted 128-bit `WRITE`s, then
one `COUNT` on counter 7, from its write unit, and releases the tiles once each
has arrived (6.5); the ROM jumps to IMEM's base, `_start`. crt0 sets the stack,
zeroes `.bss`, and keeps the boot ROM's `mie.MEIE`, `mstatus.MIE` 0 and `mtvec`,
the ROM's handler: a trap sends the master tile reports 24 and 25 and parks.

**The input-load stand-in.** SortTest pokes `S.n_in` into each tile (sort.h:
"INPUT-LOAD STAND-IN"). Here it is the image's argument `ARGS[ARG_RECORDS]`,
which `port_start()` stores. The records are in the scratchpad from the first
cycle: the chip's test writes them into each tile's scratchpad SRAMs in the model
during the chip's reset (its sort.rs), as SortTest's `loadInput` pokes them, and
reads the final records from them after the run.

**The end.** After L2's barrier `main` returns 0 and `port_exit()` sends the master
tile one request (11.5): op 2, the master's firmware's `EXIT`, words 1–3 `main`'s
return value, `S.n_recv` and `ERRORS`, and word 0 its reply's counter, `CTR_REPLY`,
armed first, and address, the master's pair of the reply block (`EXIT_REPLY`,
11.4). The request tree paces the 64 tiles' requests, which leave `main` in the
same cycle. The tile waits for the reply: a request that asks for none stays
outstanding until the master pops it (11.3), and the boot ROM's `LOAD`, which the
image's end sends, would then be a second, past the master's per-tile limit of
one. Then crt0 ends the image (6.5): a jump to the reset PC, every counter at 0,
where the boot ROM sends its next `LOAD` and waits for the load; so the master tile
can time a load's `COUNT` to the tile by its arrival (the chip's test of the
links' latency).

**The build.** v1.25's flags (`-O2 -fno-jump-tables`, function and data sections,
`-ffreestanding -fno-builtin -Wall -Wno-array-bounds`, `--no-relax`,
`--gc-sections`), for `rv32ic_zicond_zicsr_zmmul` and `ilp32` with the pinned
xPack GCC, and `-Werror`, `--orphan-handling=error` and `--fatal-warnings`. The
link discards the empty `.rela.text.init` the linker makes for crt0's calls; a
static link has no dynamic relocations. The sorter's check stands: no libgcc
division or multiplication helper is linked.
