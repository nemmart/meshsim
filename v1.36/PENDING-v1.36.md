# What is pending in v1.36

*For whoever continues v1.36: every test, suite, tool and behaviour that the move to the RTL's hardware has not reached
yet, why, and what closes it; and every latency where the simulator still differs from the RTL.* Nothing listed here was
weakened or deleted: a pending test is not built (`pending.mk`; `make pending` lists them), a pending suite step is
printed by `./run_tests.sh` as `PENDING` and counted in its summary, and its sources and reference logs stay as they
were. An item leaves this page in the commit that ports it.

v1.36's simulator is being brought to the RTL's hardware in stages (`spec/CHANGES-v1.35-to-v1.36.md`, section 2). The
platform stage has done the register map, the register bus, the tile control unit, the units' registers, the core's
minimum and the boot, for one compute tile: **acceptance A**, `tests/test_rtl_tile.cpp`, runs the RTL's own tile
programs on it (`fw/rtl/tile/`). The stages after it are named below as they are in the plan: the NoC and topology
(P6), the special tiles and the request tree (P7), the sort end to end (P8), the units' remaining behaviour (P9), the
old suites (P11), the register tables and firmware headers (P12), the replayer (17.8), the Tracer (17.9) and the core
(`spec/core.md`).

## 1. Acceptance A: what of the RTL's tile test is pending

| what | why | what closes it |
|---|---|---|
| the `replayer` program (kind 5) and its rows on the row port | the simulator's replayer is v1.35's (4.19): its tables come from files, not through its region, its region and words are v1.35's, and it has no row port a test reads | the replayer stage (17.8); then the run in `test_rtl_tile.cpp`, as `base_tile_test.rs`'s `replayer` |
| `errors` with error 10, `peripheral_1` (257 pushes into the replayer's code queue) | the code queue is at 17.8.9's address only in v1.36's replayer | the same |
| the `special` program (kind 4, `base_tile_special_test.rs`) | a special tile's base: its straps, its receive queue, a peripheral with a DMA | the special tiles (P7) |
| the RTL's model with all three attach ports at every tile id | a simulated tile has the attach port of its place in the chip (tile 0, 7 or 15: 2.1), so the runs at an attach tile use its one port | nothing: the RTL's model is a 1 × 1 chip's tile; the chip's attach ports are checked at chip level (P6) |
| the request link's beats and its handshake monitor; the row port's | the simulator's request unit hands over whole requests; the framework's ports have no ready or valid | the request tree (P7); the row port with the replayer |
| cycle-exact expectations | `base_tile_test.rs` has none but its bounds (the barrier program's delay, the timeouts), which the test keeps | |

## 2. Tests of `make check` that are not built (`pending.mk`)

| what | why | what closes it |
|---|---|---|
| `test_rv32`, `test_regblock`, `test_golden`, `test_controllers`, `test_broken_units`, `test_statefp_core`, `test_g1_replay`, `test_core_m2`, `test_statefp_g1`; the tools `golden`, `bench_controller` | v1.35's M1 bench (`src/units/controller.h`: v1.35's core with v1.35's register block) and its goldens: retire traces and fingerprints of v1.35's images, on v1.35's ISA, traps and map | the core stage (`v1.36-core`): the core's tests against the RTL's programs (riscv-tests and the core's own, built by `mise run core-images`); `controller.h` goes with it |
| `test_g1_tile`, `test_tile_slot`, `test_tileslotconf` | one tile's control part driven by v1.35's stand: GO, the command block, v1.35's registers and programs | superseded by `test_rtl_tile` for the tile; the slot's part with the peripherals (P7) |
| `test_ag`, `test_gather`, `test_mem`, `test_engconf`, `test_memconf` | the units driven directly against v1.3's observations; their drivers hold v1.35's `DMA_CONFIG`, record base and table layouts | the units' behaviour stage (P9), which changes these units' timing and so their references |
| `test_mach`, `test_checkself`, `test_chip`, `test_sort`, `test_cc_units`, `test_cc_req`, `test_t2_harness`, `test_stageb`, `test_p4suites`, `test_ctl_suites`, `test_sort_suites`; the tools `replay_shard`, `stageb` | the machine (`tests/mach.h`) as v1.3's suites drive it: C&C's model or C&C 1–3 as cores, GO and DONE, commands, v1.35's images | the chip (P6–P8): the machine with the master, memory and tracer tiles and the RTL's images; then each test ported or replaced (P11) |
| `test_regs` | the register-table generator's tables are v1.35's map | P12 |

## 3. Steps of `./run_tests.sh` that are pending

Listed by the runner itself; each is a suite built on the machine above, on v1.35's firmware, or a build check of v1.35's
images. The steps that run: `layout`, `bus`, `rtl-tile` (acceptance A), `replayer`, `memsys`, `rvc`, `tracer`.

## 4. Behaviour the simulator still has from v1.35

Each is a difference from the RTL that acceptance A does not reach, or reaches and survives. Where the old behaviour
would silently lose what the hardware keeps, the simulator stops (noted).

| what | the RTL | the simulator | closes |
|---|---|---|---|
| the write engine | a fixed pipeline, no input FIFO, never refuses (12.4) | v1.35's input FIFO (4) and stages; **an overflow stops the simulator** (`TileCtl`) | P9 |
| `PTR`, `HIST` | no request FIFOs, no overflow error (12.5) | v1.35's 16-entry request FIFOs; **an overflow stops the simulator** | P9 |
| the gather unit | while it is busy the address generator takes no read (14.1); `gather_busy` is "the queue holds a request or a request's last read not yet taken" (13) | the read arbiter's priority alone; `STATUS.gather_busy` is v1.35's OR of three inputs, in-flight reads included | P9 |
| the scratchpad | banks, a write wins its bank, `READ_LATENCY` fixed, reads return in take order (15) | v1.35's areas with request queues | P9 |
| the DMA unit | `DMA_CONFIG` latched at the start; the lead-ins of 8.2; an empty transfer busy for one cycle | read live at every step; v1.35's lead-ins (an empty transfer: as 8.2) | P9 |
| tokens | a loopback packet's token is freed when the director takes its copy (14.2) | v1.35's: when it leaves the loopback queue | P9 |
| the router | attach ports on the attach tiles only, the arbiters' order, a reserved dst dropped, no start skew (16) | v1.35's, with the attach tiles moved to 0, 7 and 15 | P6 |
| the peripheral slot | the control block of 17.1 (`EVENTS` 3, `EVENTS_PEEK` 4, `EVENTS_MASK` 5, `START` 6, the DMA in block 1), `DMA_HDR`'s layout | v1.35's slot behind the register bus, which presents 17.1's words (`TileCore::Env::slot_word`); `DMA_HDR`'s fields are v1.35's | P7 |
| the special tiles | the master, memory and tracer tiles (2.1) | v1.35's C&C 1–3 as units 64–66 with their v1.35 peripherals; `default_straps` gives them the specification's straps | P7 |
| the checker | | v1.35's facts about hardware that is gone are no longer recorded: the command block, the reply block, the stray check, the table-window check of the core | P11 |

## 5. Timing left for the timing stage

Every latency or ordering where the simulator still differs from the RTL, with the RTL's figure. *t* is the cycle the
core issues an access, *w* the first cycle a `WFI`'s wake condition holds.

| what | the RTL | the simulator | the RTL's source |
|---|---|---|---|
| the core's cycle costs | the next instruction retires 1 cycle after a branch not taken, 4 after a taken branch or a `JALR`, 3 after a `JAL` or a ready return; a load's result used right after it waits 1; a multiply is two stages with no forwarding; the instruction after a CSR write is held 1 cycle | v1.35's knobs: 1 instruction a cycle, +1 for a taken branch or any jump, 3 for a multiply, load-use 0 | `rtl/blocks/core/spec.md` §1: Control transfers, Data, Multiply, Pipeline event priority; `v1.36-core` |
| a trap's entry | from the trapping instruction's writeback, a redirect | the handler's first instruction in the next cycle | the same: Pipeline event priority |
| the canonical wait after a `WFI` | from the cycle fetch has filled its queue behind the `WFI` (at most four requests after it retires); `core_status.canonical_wait` a cycle later | `Rv32Timing::canonical_wait_delay` = 3 cycles after the `WFI`, whatever the code | the same: Wait for interrupt, Lockstep regions, Core ports |
| `canonical_wait` falling | 1 through the first cycle `wfi_sleeping` is 0 again (*w* + 1) | falls in *w* | the same: Core ports |
| the resume after a wake | the next instruction retires in *w* + 3; an interrupt the wake brings is taken in *w* + 2 | the same (`Rv32Timing::wfi_resume`), the simulator's "executes" standing for "retires" | the same: Wait for interrupt |
| a window store | takes effect at the end of *t* + 1 | the same for the tile control unit's and the units' registers behind it; the write unit's, the read unit's and `REQUEST_OUT_W0`–`W2` are the core's side's, so program order alone holds | `rtl/blocks/register_bus/src/register_bus.veryl`, Timing |
| `READ_SEND` | the read is queued at the end of *t* + 1 | at *t* | the same |
| `REQUEST_SEND` | pushed at the end of *t* + 1; the stall judged in *t* + 1 | handed to the tree at *t*; the stall judged at *t* on the core's own count | the same; `rtl/blocks/request_unit` |
| a send | the packet is injected in *t* + 2 at the earliest, and stalls only while the holding register is full and not injected that cycle | injected in *t* + 2 at the earliest; the core runs up to two sends ahead of a full holding register before it stalls | `rtl/tile/base/spec.md` 9, 14.3 |
| a faulting access to region 0xFF | answered in *t* + 1 | a faulting window load waits its cycle first | `register_bus.veryl`, Timing |
| a peripheral's live event or error | its level in cycle *c* raises the wake or the NMI in *c* + 1, a cycle after a load would see it | in *c* | `rtl/blocks/tile_control_unit/src/tile_control_unit.veryl`, header |
| the units behind the tile control unit | the write engine's pipeline, `TABLE_LATENCY`, `READ_LATENCY`, the DMA's lead-ins and the end of a transfer, the gather unit's reads, the router's queues and links | v1.35's (`ts_engine_latency`, `sram_read_latency`, `K` = `LAND_LATENCY` = 8 and `cm_latency` = `WRITE_PORT_LATENCY` = 2 already the RTL's) | `rtl/tile/base/spec.md` 8, 12–16; stages P6 and P9 |
| the nets | `BARRIER_ARRIVE` a flip-flop's pulse the cycle after the tile is ready; `BARRIER_RELEASE` reaches every tile in the same cycle | the pulse is put on `line` in that cycle; the chip's links for it and for the release are v1.35's (`ready_latency`, `go_latency`, the start skew) | `rtl/tile/base/spec.md` 7.3, 18; P7 |
