# INTEGRATOR report: v1.37 built to its specification

*Added to at each stage. The plan, the go-ahead and the choices are `NOTES.md`; where the specification is wrong,
unclear or silent, `FOR-THE-SPEC.md`; the worklist's rows are closed in `../SPEC/FOR-THE-SIMULATOR.md`. Paths without a
folder are in `v1.37_wip/`. Branch `v137-build`.*

## Stage 1: the memory side (17.10, 17.11, 23, 17.7)

### What was built

| | where |
|---|---|
| **the memory system, the memory tile's peripheral 1** (17.11): type 6, version 0; no words, no work, no DMA; `ERRORS` [0] `lpddr_error` sticky from the cache's error flag, so on the tile's error line, the master's `ERROR_LINES_2` [1] and its NMI; a `START` and a `WRITE` to its region the simulator's stop (invariant 38); the reset emptying the cache and the arbiter. It steps every cycle once attached | `src/units/memory_system.h`; the slot's `PT_MEMSYS` (`periph_slot.*`, `peripheral.h`); `tile_ctl.cpp` (the memory tile's two peripherals, its DMA peripheral 2's) |
| **its hardware with its clients' ports** (23.10): WORKER5's `CacheSystem` given the Updater's client port 1 and look-up port 1 (`updater_axi`, `updater_lookup`, `updater_top.h`'s `AxiDev` and `LookupDev`), the RW engine's client 2 (`rw`, through `RwEngineOnArbiter`), and the Tracer's `fill` as before. **The edge of a cycle is taken lazily,** at the first `begin(t + 1)` of any client or of the memory system's own step: so it follows every client's step of *t* whatever order the three tiles are stepped in, and the cycle is the units' convention exactly (every output of *t*, then every edge of *t*), with no latency on the ports (23.1; C2). A cycle no client steps in is an edge with every client idle, and the simulator stops if the units were not idle then | `src/units/cache_system.h` |
| **the joint's checks** (WORKER9's, what 23.6.8 asks of a client, counted and failing the run): an update naming a page whose write is not answered; a direct write to a page the cache holds a line of, or a read of one it holds a dirty line of; an update's reply with LPDDR not holding the entry; a look-up out of turn; a fill request out of turn, or a fill response the Tracer did not take. WORKER5's stale-line check stays on, FATAL | `cache_system.h` |
| **the Updater is its controller** in every chip with the host link, on the memory system; `UpdaterTop`'s two port interfaces gain `begin(t)`, called at the top of its step; the black box is kept for the comparisons alone. **Gone:** `MachConfig::cache_ctl`, `proof_run --cache`, `--updater-ctl`, `--updater-ctl-seed`, `MESHSIM_UPDATER_CTL`, `CACHE_CTL` and the test pieces in the chip. **New:** `MachConfig::lpddr_seed`, `proof_run --lpddr-seed`, `MESHSIM_LPDDR_SEED`, `run_tests.sh`'s `LPDDR_SEED` | `src/units/updater_top.h`; `tests/mach.*`, `updater_view.h`, `proof_bench.*`, `proof_run_main.cpp`, `segment_bench.*`; `run_tests.sh` |
| **the RW engine, peripheral 2** (17.7): window 0xFFFF_FC00, region 0x05, its port the arbiter's client 2 on the one LPDDR; `mesh.h` generated so (`tools/regs`: the peripheral's slot by its prefix; types 5 and 6 added); `memory.c` on `EVENT_PERIPHERAL(2)`; `memory.elf` rebuilt. A chip without the host link has a file store of its own for it | `tools/regs/regtable.h`, `regs_gen.h`; `fw/include/mesh.h`, `fw/REGISTERS.md`, `fw/cc/memory.c`; `tests/mach.cpp` |
| **the Updater's rows** (17.10): `free_page_credit` at `EVENTS` [2] in both Updaters, `host.h`, `master.c` (`master.elf` rebuilt) and the benches; a bad root or free page address, a root with bit 31 set and `RUN_CONFIG.root` 3 the simulator's stop, in both (so the comparisons hold); a run of values past index 2^32 − 1 and a page at 2^23 or above the controller's stop | `src/units/updater_ctl.h`, `updater_top.h`, `updater.*`; `fw/include/host.h` |
| **the joined bench**: `ctl_bench`'s `Options::joined`, the controller on a `CacheSystem` in place of its test pieces, the joint's checks in place of theirs; a second client on the fill port (`other_client`) | `tests/ctl_bench.h` |
| the tests | `tests/test_memory_system.cpp` (6); `tests/test_memory.cpp` (the memory run on the arbiter, with a tap at the engine's port: 3); `tests/test_updater_ctl.cpp` (the joined tests: 7; the death tests: 3); `tests/test_updater.cpp` (3 death tests); `tests/test_master.cpp` (6 runs with LPDDR jittered, in place of WORKER5's 6 on the cache); `tests/test_regs.cpp` |
| the planted bugs | `tests/joined_teeth.sh` |
| the runner's task | `updater_wip/tasks/v137-check/`: `suites-varied` in place of `suites-cache`, `suites-ctl` and `suites-ctl-varied` |

**Settings now** (NOTES section 1): the cache's controller and the Updater's controller are the machine; the black box
and v1.37's memory model (`mem_system.h`) remain for the tests that compare against them (`ctl_bench`, `test_updater`;
the Tracer-alone steps and v1.36's segment runs, which compare with the RTL's figures). The test LPDDR and the stand-in
cache remain for `test_updater_ctl`.

### Disagreements at the joints

**None was found.** WORKER9's expected E1 to E8 (gaps inside a read burst between the arbiter's pieces, `wready`
falling between pieces, W beats with or before the address, a write-back and a page write at once, replays with two
real clients, the stale-line check on index lines, a wait that never ends, the Tracer's first fill against `done`) were
each exercised and none failed: the joined bench on gold's five fixtures at four *s*, the 90 random scenarios, both
fixed and with the LPDDR model jittered, the second client at once (requests served under a miss 66 to 81 times, replays
31 to 32, a run), and `test_master`'s 34 runs on the chip. The 37 proof runs on the real workloads are the runner's
(below).

### The checks

| check | result |
|---|---|
| the units' own tests: `test_cache` 13, `test_axi_arbiter`, `test_lpddr_model`; `test_updater_ctl` 34 (WORKER6's on the test pieces, and the joined); `updater_ctl_teeth.sh` WORKER6's 14 | pass |
| the joined bench: gold's fixtures alone, fixed and at three seeds; against the black box, the stores byte for byte, fixed and at two seeds; the 90 random scenarios against the black box run by run, fixed (60) and jittered (30); a second client on `fill` at once, three timings, every reply right; none of the joint's checks | pass |
| the Tracer alone on the cache (`TRACER_CACHE=1 tracer_block --synthetic 1, 2, 3`): **the same cycles as `main`'s, to the cycle** (the lazy edge changes nothing the Tracer sees) | pass |
| `test_memory_system`: PARAMS, the window, the two UB stops; an LPDDR error on a fill read's fetch reaching `ERRORS` [0] and the master's `ERROR_LINES_2` [1]; the reset clearing `ERRORS` and emptying the cache; the RW engine's write job through client 2 into LPDDR | 6 pass |
| `test_memory`: the memory run (the master's READ, WRITE, READ back and 300 WRITEs of 0) on the arbiter, its bursts at the engine's port as the RTL's test checks them, fixed and at two seeds | 3 pass |
| `test_master`: the five guests on the joined machine, the error path, the cut file, the teeth, and six runs with the LPDDR model jittered | 34 pass |
| `./run_tests.sh quick --check` (clang; the toolchain on `PATH`) | **all passed**; no v1.36 log differs; one step skipped (`tracer-block`: no workloads' data here) |
| `fw/build.sh`: only `master.elf` and `memory.elf` changed (the other 16 images byte for byte as before) | |
| **the planted bugs** (`joined_teeth.sh`): J1 the Updater on client 3; J2 on look-up port 3; J3 an update before its write's response; J4 the cache answering from a line an update changed; J5 the arbiter granting a second write piece; J7 the joint a cycle out; J8 the memory system's error not on `ERRORS`; J9 the reset not reaching the cache; J10 the RW engine on client 3; J11 the bad root as `lpddr_error`; J12 the firmware's credit bit at [3] | **11 of 11 caught.** J7 was at first caught only by `test_memory_system`: a fill request kept a cycle too long is re-sent and answered twice, harmlessly to the Tracer. The fill port's handshake check (a response the Tracer did not take) was added for it, and `test_master` now catches it too. WORKER9's J6 (the cache's reply to an update before its write's response) is not planted: the reply is the update slot's, at the response, and moving it is a restructuring of `cache_ctl.cpp`'s slot, not a one-line bug |
| the 37 proof runs on the joined machine; again with LPDDR jittered; `make check` | the runner: Quests 245, 246, 247 (below) |

### The reference cycles

They move in this stage, on purpose: the Updater is its controller on the real cache (the black box's timing goes),
and the Tracer's memory is the cache (the model goes). **On the guests** (`test_master`, 35 runs printed): every run
between the same and 1,024 cycles more than `main`'s (the harness counts in 512s); for example `kin-raw` *s* 0 236,544 →
237,568 with its run repeated, `divs` *s* 20 230,400 → 230,912. **Nothing else moved:** every golden log of
`./run_tests.sh quick --check` is byte for byte as before. The proof runs' new cycles are Quest 245's.

### The figures

`test_updater_ctl ctl_figures_a_page` (a page by kind, the controller on the test pieces, joined, and the black box) and
every proof run's clients' line (look-ups, updates, the overlap, each client's bursts and its pieces' waits at the
arbiter) are printed; the real workloads' come with Quest 245 (`cache-figures.txt`).

**Two proofs** (the owner's overlap): the joined bench's second client is that overlap on a bench, the Tracer's traffic
pattern beside the Updater's runs. In the chip it would take the frame loop and the barrier made one loop in
`master.c`, a packet file of two proofs and a second set of images in the proof bench (WORKER9's section 4): not made.

### The runner

| Quest | item | result |
|---|---|---|
| 245 | `suites` at `fd3ff56d`: `./run_tests.sh all --check`, the 37 proof runs | **PASS**: every step; the 37 proof runs (3 real, 24 of the matrix, 10 at 29); no golden log differs. The two `fw-build` steps skipped (the runner has no RISC-V toolchain; the images are this machine's, `fw/build.sh --check`) |
| 246 | `suites-varied`: the same with every chip's LPDDR jittered (`LPDDR_SEED` 7) | **PASS**: the same, no log differs |
| 247 | `check`: `make check` | **PASS**: gcc and clang, debug and release, 1,613 s |

The figures of the 37 runs are in `runner/`: `245-proof-numbers.md` and `245-cache-figures.txt`, and 246's. **The
overlap is real:** cycles with the Updater's look-up and a fill request both outstanding, up to 33,187 in a run
(`cyclone-29`, *s* 0; 86,603 with LPDDR jittered), and a fill served under a miss up to 1,751 times (2,870); the
joint's checks 0 in all 74 runs; no burst on client 2 in a proof.

## Stage 2: the router (16)

### What was built

| | where |
|---|---|
| **the line between the special tiles** (16.1): a special tile's router with east and west links, the master tile's east to the memory tile's west and the memory tile's east to the tracer tile's west, each a link of three stages with no flow control (`NetConfig::link_latency`) | `src/units/router.h`, `router.cpp` (`line`, `step_line`); `src/units/chip.cpp`, `chip.h` |
| **its routes** (16.2): a packet for the tile ejects; one for another special tile, from local or the DMA, takes the line by the ring's rule (east if its `dst` is above the tile's id); one arriving on the line goes on the way it came; every other packet from local or the DMA leaves by the attach port; from the attach port, a packet not for the tile goes nowhere, as before | `router.h` |
| **its queues and arbiters** (16.3): east from local, west and south; west from local, east and south; the eject from local, west, east, south and the attach port; the attach output from local and south, each output's arbiter in that order. A packet into a full queue on the line is `router_overflow` on its router (invariant 40) | `router.h` (`kFeedersLine`) |
| **every special tile's peripheral DMA on its router's south input** (16.4); `dma_room` carries the line's queues (bits 8 + 2*d* east, 9 + 2*d* west, and bit 16, the line); the slot's DMA judges its room there (`dma_input` 3) | `router.h` (`dma_ready`), `chip.cpp`, `periph_slot.h` |
| without the line, the special tile's router stays the RTL's as built: `test_router`'s comparison with the RTL and `test_special_router`'s v1.36 tests | |
| the tests | `tests/test_special_router.cpp`: every pair of the 67 tiles from local and from each DMA (a compute tile's packet for a special tile never on the line), the broadcasts, the line's overflow counted where its queue is, its arbiter's order, an attach port's arrival for another special tile going nowhere with no route error; `test_memory` (the memory tile's answers to the master all by the line, west); every proof in `test_master` (the master's loads into the tracer tile by the line, none lost) |
| the planted bugs | `tests/line_teeth.sh` |

### Disagreements

**None.** One planted bug went uncaught at first (L4 below) and turned out to change nothing a packet does, only the
router's route-error count: there is no queue from the attach port to the line (16.3), so a route there goes nowhere.
The test now checks the count.

### The checks

| check | result |
|---|---|
| `test_special_router` (19), `test_router` | pass |
| `test_memory`: the memory run's 347 answers by the line (`line_out` at the memory tile equal to its DMA packets, `line_in` at the master tile the same, nothing by the attach port) | pass |
| `test_master`: every proof's loads into the tracer tile by the line | 34 pass |
| `./run_tests.sh quick --check` | **all passed**; no log differs |
| **the reference cycles:** `test_segment`'s v1.36 segments (to C&C 1's exit, polled every 512 cycles) the same as `main`'s; the proof runs', against stage 1's (Quest 245): the runner's (below) | |
| **the planted bugs** (`line_teeth.sh`): L1 the ring's rule reversed; L2 a packet on the line sent back the way it came; L3 a special tile's packet for another by the attach port; L4 an attach port's arrival for another special tile put on the line; L5 the slot's DMA judging its room at the west input (v1.36's); L6 the west output's queues in the wrong order; L7 a DMA's packet for another special tile by the attach port | **7 of 7 caught.** L4 only once `test_special_router` checked the route errors (`ec6730a7`, on the branch with stage 3's first commit; it reaches `main` with stage 3's merge) |
| the 37 proof runs on the line; `make check` | the runner: Quests 248, 249 (below) |

### The runner

| Quest | item | result |
|---|---|---|
| 248 | `suites` at `f24979c6`: `./run_tests.sh all --check`, the 37 proof runs | **PASS**: every step; the 37 proof runs (3 real, 24 of the matrix, 10 at 29); no golden log differs; the two `fw-build` steps skipped (no RISC-V toolchain on the runner) |
| 249 | `check`: `make check` | **PASS**: gcc and clang, debug and release, 1,623 s |

**The reference cycles did not move.** Every run's cycles are stage 1's (Quest 245) to the cycle: the master's
answers from the memory tile now come by the line, and the segment's end is polled every 512 cycles. What moved is
inside: in 14 of the 37 runs the Updater's busy cycles and its waits on memory by up to 45 cycles, its waits on data
by up to 19; the master's cycles awake in none of them. The figures are in `runner/248-proof-numbers.md` and
`248-cache-figures.txt`.

## Stage 3: Ingress/Egress at its registers (17.6)

### What was built

| | where |
|---|---|
| **17.6.1's words and bits**: `UART_TX` and `UART_RX` at words 30 and 31 in block 1, beside the DMA's; `UART_CONTROL` at 42; `FOREGROUND_WORDS`, `BACKGROUND_WORDS`, `DMA_FORWARD_COUNT`, `EGRESS_COUNT` at 64 to 67; `STATUS` [2] and [3] the queues, [12:8] and [20:16] the UART's FIFOs, [24] and [25] its line errors; `ERRORS` [1] `link_error`, [2] `format_error`, [3] `egress_error`; `EVENTS` [6] and [7] the service flags, [8] `egress_credit`; `PARAMS` version 1. v1.36's LVDS stream gone (words 64 to 72, `stream_last`, `stream_in`, `stream_out`, `STREAM_BYTES`, `STREAM_DEPTH`) | `src/units/ingress_egress.h` |
| **the bus and the slot**: words 30 and 31 a DMA peripheral's own words (the slot publishes their values and bits in `Win::R_LOW`, `R_LOWBITS`); a peripheral without a DMA still reads 0 in all of block 1 (17.1) | `src/units/register_bus.h`, `periph_slot.*`, `tile_ctl.h` |
| **the incoming queues** (17.6.3): `INGRESS_QUEUE_WORDS`, 1,024 (`MachConfig::ingress_queue_words`, setting `ingressQueueWords`; a power of two from 64 to 4,096); a link packet's words landing a 128-bit word a cycle from the packet buffer, one link packet after another, the first at the end of the cycle it is taken, each counted in `…_WORDS` as it lands; the room of the words landed and still to land (28.13.6) | `ingress_egress.h` |
| **Egress** (17.6.8): the outgoing queue of 512 words, filled by 128-bit `WRITE`s to region 0x04 part 1, the rest of the address ignored; `EGRESS_COUNT` counting down as each word lands, its last word marked; a link packet offered at 32 words of a frame or its last word; `egress_credit` counted to 2, 2 at the reset, one back each 256 words gone; `egress_error` for a word into a full queue or with the count at 0. Until stage 4 its link packets go to a harness's callback (none: they stay) | `ingress_egress.h` |
| **the UART** (17.6.9): off at the reset, `UART_CONTROL` 0, which is its reset (both FIFOs emptied, the line errors 0, the line out idle, stores dropped, the events 0); its divider and the two enables gating the line alone; the line errors in `STATUS` until the UART's reset | `ingress_egress.h` |
| **the UB the simulator stops on** (17.6.15, invariant 35): a `START`; a DMA count past the queue; a background transfer with the count running, or a count stored during one; `EGRESS_COUNT` stored while not 0; a `WRITE` into the region of other than 128 bits or to a part other than 1; a frame's words from two sources; a non-zero `UART_CONTROL` with a divider of 0 | `ingress_egress.h` |
| **every chip's master tile is v1.37's**: Ingress/Egress with its DMA and the Updater's controller as peripheral 2 in every chip with the special tiles. `MachConfig::host_link` is gone (its setting `hostLink` refused by name); `MachConfig::proof` (setting `proof`) says only that a proof comes from the host: the Tracer and the Updater on one store | `src/units/tile_ctl.*`, `periph_slot.*`; `tests/mach.*`, `segment_bench.*`, `test_memory_system.cpp` |
| **the register table and the firmware**: `mesh.h` and `REGISTERS.md` regenerated with 17.6.1 (and the part `IE_EGRESS`); `host.h`'s link names now mesh.h's; the port layer's `Cnc1Regs` at v1.37's offsets; `cc1.c` turns the UART on where it first prints, its dump; `cc1.elf` and `master.elf` rebuilt | `tools/regs/`; `fw/include/`, `fw/port/mesh.h`, `fw/cc/cc1.c`, `master.c` |
| the tests | `tests/test_ingress_egress.cpp` rewritten (36: the control block and the reset values, the window map with block 1, the barrier's and the watchdog's as before, the UART's line both ways at its new words, its reset and enables, Egress's count, link packets, credit and errors, every UB); `test_ingress_link` and `test_packet_vectors` at the new words and the landing; `test_rtl_stream`'s UART at its words; `test_regs`, `test_mach`, `test_master`, the proof bench |
| the planted bugs | `tests/ie_teeth.sh`, rewritten for v1.37's unit |

### Decisions

- **`cc1.c` turns the UART on at its dump, not at its boot.** 30.2 asks for "one store of its setting to
  `UART_CONTROL`" if the image prints. A store at the boot moved v1.36's segment runs (`test_segment`'s
  `chip_trace_test_full` by 1,024 cycles, `test_sort`'s barrier by 10) though they never print: v1.36's program on a
  chip whose UART is off at the reset is the same program until it prints, and the reference cycles move only on
  purpose. `master.c` does not print.
- **Egress's link packets go nowhere until stage 4** (a harness may take them: `egress_out`), so its credit is tested at
  the unit, with a harness taking them at once or holding them.
- **The black box's link errors** are `link_error`, with `format_error` for a field that cannot be: the nearest 17.6.1
  has until stage 4, where a damaged link packet is no error at all.

### Disagreements

None with the specification. Two of my own bugs found on the way: the bus first gave words 30 and 31 to every
peripheral, so a DMA-less peripheral's block 1 would have faulted there (17.1 says it reads 0); and the v1.36 UART
test's expectation of a reset divisor.

### The checks

| check | result |
|---|---|
| `test_ingress_egress` (36), `test_ingress_link` (44), `test_packet_vectors` (6), `test_rtl_stream` (1), `test_regs` (12), `test_mach` (11), `test_memory_system`, `test_rtl_tile` | pass |
| `test_master`: the guests, the error path (the black box's damaged packets now `link_error`), the teeth | pass |
| `./run_tests.sh quick --check` | **all passed**; no log differs |
| **the reference cycles:** `test_segment`'s and `test_sort`'s the same as `main`'s; `test_rtl_stream`'s UART run each figure one cycle later than the RTL chip's (its program's setting is `lui` and `addi`, one instruction more: the first start bit 35, the led's changes, the status after 644, the done word after 5,211); the proof runs' move (the queues of 1,024, the landing): the runner's | |
| **the planted bugs** (`ie_teeth.sh`, 44): the UART's line both ways (bit order, a bit's length, the sample point, the synchroniser, the divider mid-character, the stop bit, the overrun, a low level starting a character, a pop of a character entering in the load's cycle); its control (a store dropped or kept with the UART off or `enable_write` 0, `enable_write` 0 sending, `enable_read` 0 listening, a store of 0 not resetting, `uart_tx` 1 in reset, the enables swapped, the events swapped, a divider of 0 not caught); 17.6.1 (version 0, the queues' words at 80 and 81, `background_ready` at [4]); the queues (a link packet landing all at once, the room of the words landed alone); Egress (the count falling as words leave, a credit each 512, a frame's last word not ending its link packet, a word kept in a full queue, a word with the count at 0 no error, a count stored while not 0 not caught, a frame from two sources not caught); the bus (`UART_RX` not an own word), the slot (a low word's stall not published, the queue's size not given), the core's path (no pop told, a pop a cycle late) | **44 of 44 caught** (one plant fixed to build: its first form left a parameter unused). NOTES' "stop at full" is stage 4's: the stop bit is the link end's |
| the 37 proof runs; with LPDDR jittered; `make check` | the runner (below) |

### The runner

| Quest | item | result |
|---|---|---|
| 250 | `suites` at `f873c715`: `./run_tests.sh all --check`, the 37 proof runs | **PASS**: every step; the 37 proof runs; no golden log differs; `fw-build` skipped (no RISC-V toolchain on the runner) |
| 251 | `suites-varied`: the same with every chip's LPDDR jittered (`LPDDR_SEED` 7) | **PASS**: the same, no log differs |
| 252 | `check`: `make check` | **PASS**: gcc and clang, debug and release, 1,643 s |

**The proof runs' cycles moved, on purpose: every run is 15,360 to 76,288 cycles shorter than at stage 2** (Quest 248).
The incoming queues went from 512 words to 17.6.3's 1,024, so the link waits for room less: over the 37 runs its
waits for room fell from 10,677,833 cycles to 9,207,045, and the link's last byte, and with it the hand-over and the
end, came earlier by about as much. The Updater's busy and waiting-on-data cycles fall with them; its waits on memory
move by a few hundred either way. The figures are in `runner/250-proof-numbers.md` and `251-proof-numbers.md`.
