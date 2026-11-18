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

## Stage 4: the link and the chain (28)

### What was built

| | where |
|---|---|
| **the chip's link is WORKER8's link end as it stands** (`../updater_wip/link`, `hostlink::Node`): in Ingress/Egress, the pins `link_up` and `link_down` (a byte each way a link cycle, `link_rate` link cycles a chip cycle, 1, 2, 4 or 8, or one every `link_every`) and `chain_next`; the `Node` clocked in the unit's settle, made new at the reset (which starts the link again, "I have started again"); its words landing into 17.6.3's queues a word a cycle from the packet buffer, the room of the words landed and to land (28.13.6); Egress's link packets pushed into it when its room takes them; `link_error` and `format_error` from its two ends; the chip never calls `clear_error()` (28.13.2). The `Node` allocates: its calls are made with the allocation guard lifted (`LinkAllocs`) | `src/units/ingress_egress.h` (THE LINK, THE PINS); `Makefile`, `src/units/tracer.mk` (the link's objects) |
| **the host's end** in the harnesses: WORKER8's `End` (`tests/host_end.h`, `Mach::attach_host`), fed the packet file's data link packets, the chip's replies taken out by hops; the line's noise and a hook on the End for the tests | `tests/host_end.h`, `tests/mach.*`, `tests/proof_bench.*`, `tests/ingress_bench.h` |
| **a chain with no chips**: a host `End` and two or three Ingress/Egress units back to back, a harness on their registers | `tests/test_ingress_chain.cpp` |
| **chapter 28 in the files** (MILESTONE step 2c): gold's packetizer, reader and vectors, its Python reader, and the tests' writer and reader (`tests/packet_writer.h`, `src/units/host_link.h`): one run of numbers for the link, `fg_stop` and `bg_stop` in byte 7 and reserved [63:58], the data masked (28.6), the restart link packet (28.9). The worked examples of 28.5 (four), 28.6 (three) and 28.9 (its data checksum) are copied into gold's `PacketsTest`, `check_vectors.py`, `test_ingress_link` and `test_packet_vectors`, and each matches byte for byte | `../updater_wip/pipeline/gold/src/gold/Packet*.java`, `test/packets/` (62 single link packets, 6 files, 41 damaged files); `tests/packet_writer.h`, `src/units/host_link.h` |
| **the capture** (C5): the host `End`'s data link packets as first sent, read off the line; the proof bench fails unless they are the packet file's byte for byte. In the proof runs that is gold's file (`--packets`); in `test_master` the tests' writer's | `tests/host_end.h` (THE CAPTURE), `tests/proof_bench.cpp` |
| **`proof_run --fast-link` went** (C6): it was the fastest link under another name; `--link-rate 8` is the same timing. `proof::Case::cycles_per_byte` became `link_every` | `tests/proof_run_main.cpp`, `tests/test_master.cpp`, `../updater_wip/tasks/v137-check/body.sh` |
| the planted bugs | `tests/chain_teeth.sh` |

### Decisions

- **"Agree byte for byte"** (C5) is the strict reading: the `End`'s data link packets as first sent, exactly as on the
  line, against the file's. I first allowed a header carrying what came back (an `ack`, a stop bit) to be sealed
  again without it; in every proof none needed it (the chip's replies come after the last data link packet), so the
  allowance went: a check with an allowance is not the check.
- **A restart link packet in a file** (gold's and every reader's `restart` check): one with `restart` set that is not
  28.9's restart link packet is refused, as 28.9's "damaged"; the restart link packet takes no number, its data goes
  to no queue, and the numbers start again from 1 after it; one inside a frame is refused. For the readers of files
  only: the chip's are the `Node`'s rules.
- **`restart` is no longer one of the flags that are never checked:** gold's "every flag" vector and the tests' header
  tests set the other three.
- **The tests' files of two streams number both as one run** (28.4), as the reader now requires.

### Disagreements

None with the specification. **One addition to the link end** (a finding, not a change to its state machines):
`Node::peek(Stream, Word&)`, a look at the oldest word of a queue without taking it, for the DMA's and the forward
count's view of the landed words (the black box had it). It reads `InQueue`'s front and changes nothing; WORKER8's
`make check` passes with it.

### The checks

| check | result |
|---|---|
| `test_ingress_link` (25), `test_ingress_chain` (5), `test_packet_vectors` (6), `test_ingress_egress` | pass |
| `test_master` (34): every proof through the link and the host's `End`, the capture byte for byte (174, 160 or 213 data link packets a fixture, none sent again), the noise on the line recovered with no error bit, the host's link started again | pass |
| **the capture has teeth:** the `End` fed one byte changed in its fifth link packet | caught: "the first difference at byte 1672" |
| gold's `build.sh`: packets (262,593 checks), the guests' frames 80 of 80 wrapped and read back, the segments byte-identical to v1.36 gold's on 168 runs | all ok |
| the link's own `make check` (`updater_wip/link`) | ok |
| `./run_tests.sh quick --check` | **all passed**; no log differs |
| **the planted bugs** (`chain_teeth.sh`): hops not counted; a header not sealed again; an own link packet before a forwarded one; a stop bit ignored; a reply's link packets out of order; `chain_next` ignored; `clear_error()` called by the chip | **7 of 7 caught** |
| the 37 proof runs, the capture against gold's files; with LPDDR jittered; `make check` | the runner (below) |

### The reference cycles

**They move once, on purpose: the link's start, its status link packets and its acknowledgements.** `test_master`'s
proofs against stage 3's, cycle for cycle (the end is polled every 512): at a byte a cycle, 7,168 to 8,704 more
cycles; at 2, 4 and 8 bytes a cycle, 512 to 4,096 more; at 4 cycles a byte, 30,208 to 33,792 more. The `End` sends
three restart link packets before its first data in every run. The simulation is slower: a median of 831,000 cycles a
second at stage 3 and 511,000 at stage 4, the link's two ends clocked a byte at a time.

### The runner

Stages 4 and 5 together, at `d54c8601`:

| Quest | item | result |
|---|---|---|
| 253 | `suites`: `./run_tests.sh all --check`, the 37 proof runs, each through the link, the capture against gold's packet file, the reply checked | **PASS**: every step; the 37 proof runs, every one's data link packets as first sent byte for byte gold's packet file (8,277 a run at 2²⁹, none sent again) and its reply right; no golden log differs |
| 254 | `suites-varied`: the same with LPDDR jittered | queued |
| 255 | `check`: `make check` | queued |

**The proof runs' cycles, stages 4 and 5 against stage 3 (Quest 250):** every run 14,336 to 71,168 cycles longer,
0.06% to 0.26%. The link's last byte comes 13,815 to 95,951 cycles later: its own link packets (the start, the status
link packets, the acknowledgements) share the line, and its waits for room grew (the stop bit is now the `Node`'s, at
half a queue: `evm-zone-23` *s* 0's 134,326 cycles to 178,871). The reply adds a few hundred cycles at the end. The
figures are in `runner/253-proof-numbers.md`.

## Stage 5: chapter 30

### What was built

| | where |
|---|---|
| **the tracer tile's result carries its last record** (30.6): when its load names a place in the master tile (its header words 21–23, which the master's load writes; v1.36's harnesses leave them 0 and nothing changes for them), its end streams the record's 68 words from where the last window's output queue 0 landed them into the master's scratchpad, and writes a word of figures, {rows traced, the Tracer's `ERRORS`, its status}, into the master's data memory, both counted on the master's counter 4, before main returns and crt0 sends the result | `fw/tracer/tracer_ctl.c` (step 6), `tracer_layout.h` |
| **the reply** (30.7, 29.14): built in the master's scratchpad at `ML_REPLY`, the header word (type 16, length 71), *s*, *N*, the rows traced, `PAGE_COUNT`; the 64-bit `mcycle`, the last run's `STAT_CYCLES` and `STAT_STALL_DATA`; the four `ERRORS`; then rows 4–71, the tracer tile's record, which its result put there. 72 to `EGRESS_COUNT`, the 72 rows into Egress's part 1 by one stream of the DMA unit, and `EGRESS_COUNT` awaited at 0; then the exit. Its figures in `master_results` (`MR_REPLY_MCYCLE`, `MR_TRACER_*`, `MR_REPLIED`) | `fw/cc/master.c` (`reply`, `hand_over`), `master_layout.h`, `include/host.h` (`FRAME_REPLY`, `mcycleh`) |
| **a header frame one row longer than its fields** now stops the master (30.8): `parse_end` also refuses a whole row copied and left unread | `fw/cc/master.c` (`parse_end`) |
| the images, rebuilt with the pinned xPack 15.2.0-1: `master.elf` `d1bb737f…`, then with `parse_end` (the MANIFEST's), `tracer_ctl.elf` `4009da91…`; every other image byte for byte as before | `fw/images/` |
| **the bench**: the run goes on past the master's exit until the host's `End` has the reply (`Proof::settled`; the run's cycles stay the exit's); the reply checked word by word | `tests/segment_bench.*`, `tests/proof_bench.*` |
| the tests | `test_master`: the reply in every proof; `master_stops_on_what_a_frame_says` (30.8's checks, seven frames changed); `test_sort`: the dump's characters leave on the UART line |
| the planted bugs | `tests/fw_teeth.sh`, ten more |

### Chapter 30, rule by rule

| rule | kept by (`fw/cc/master.c` unless named) | shown by |
|---|---|---|
| 30.1: *N* and *s*, the images, the tables, placed before the reset ends | `main` 619: `system_config`, `boot_desc`, checked (`MR_CONFIG`) | every proof of `test_master` |
| 30.2.1: the UART turned on by one store, if the image prints | `master.c` does not print; `cc1.c` 230 at its dump (and its `CC_UART` command) | `test_sort`: the dump's characters on the line (planted: no store, caught) |
| 30.2.2: its masks, its tile's and Ingress/Egress's | `main` 619–623 | every proof |
| 30.2.3: the loads as v1.36's; the tracer tile's held back | `boot` 314 | every proof (130 `LOAD`s, the tracer tile's after the hand-over) |
| 30.2.4: the segment's first barrier | `boot`, `barrier` 287 | every proof |
| 30.2.5: the page map, page 0 never handed out | `map_format` 237, `map_take` 247 | the image checks; planted: page 0 not marked, caught |
| 30.2, the watchdog the firmware's to set | none is set | |
| 30.3, a wake: the count and `BACKGROUND_WORDS` loaded, acted on only with the count 0 and words waiting | `words_ready` 336 | `master_orders_with_the_timing_varied`; planted: a copy without the count's 0, caught |
| 30.3, a frame's start: the header word by the DMA, its landing awaited | `frame`, `copy` 347 | every proof; planted: a copy's landing not waited for, caught |
| 30.3: frames 1, 3, 4 copied in pieces no longer than the frame's words to come | `next32`, `copy` | `master_tail_frames`, `master_frames_across_packets` |
| 30.3: frames 2, 5, 6, the run started, then the count once | `update_frame` 406 | every proof; planted: a count one long, dropped, caught |
| 30.3: frame 7: records before *s* dropped, *s* copied, *s* + 1 only when *s* < *N* − 1 | 560–569 | `master_kin_lru_last` (*s* 63); planted: record *s* + 1 kept for *s*, caught |
| 30.3: frame 8: the *N* counts, log *s* | 572 | every proof; planted: log *s* − 1, caught |
| 30.3: frame 9, three runs, one count | 587 | every proof; planted: the count one short, caught |
| 30.3: a type not 1 to 9 a stop, the reply's 16 among them | `default` 610 | `master_stops_on_what_a_frame_says` |
| 30.3: drops in a row one count | `skip`, `drop_now` 328 | planted: two counts, caught |
| 30.4: each run set up while idle, a frame for the Updater waiting for the last `done` | `run_start` 386, `run_wait` 397 | planted: a run started without the last `done`, caught |
| 30.4: the table of runs (roots, `continue`, `tracer_copy`, the counts) | `frame`'s cases | gold's images in every proof; planted: three of the rows wrong, caught |
| 30.4: every root a new page; the free page queue exactly `PAGE_COUNT` pages under its credit; `PAGE_COUNT` 0 at every `done` | `new_root` 269, `feed_pages` 271 | every proof (`PAGE_COUNT` at every `done`); `master_out_of_pages` |
| 30.5: word 66, the load of one kind from one unit, in order, *s* + 1 only when there is one | `hand_over` 413 | every proof's hand-over checks; planted: word 66 without the table's root, the advice count one short, caught |
| 30.6: the segment as v1.36's; **the tracer tile's result carries its last record** | the barriers, the results; `tracer_ctl.c` step 6 | every proof: the reply's record against the tracer tile's; planted: the result without the record, the rows a window short, caught |
| 30.7.1: the frame built in its own memory, 72 rows, `length` 71 | `reply` 452 | every proof: the reply's header word |
| 30.7.2: 72 stored in `EGRESS_COUNT`, which is 0 | 466 | planted: `EGRESS_COUNT` 71, caught (`egress_error`, the trap) |
| 30.7.3: the rows into part 1 of region 0x04, from one sending unit, in order | one stream | planted: the record before the header, caught |
| 30.7.4: three link packets on the foreground | Egress | every proof: the reply's link packets end after 32, 64 and 72 words, hops 0 |
| 29.14's words: *s*, *N*, rows, `PAGE_COUNT`; `mcycle`, the statistics; the errors 0; the record | `reply` | every proof: *s* and *N*, the rows against the replayers' rows, `PAGE_COUNT` and the statistics against the Updater's own, `mcycle` against the master's results, the error words 0, the record against the tracer tile's own and, but for the last segment, words 0 and 2–65 against record *s* + 1; planted: *s* and *N* swapped, `STAT_CYCLES` wrong, record *s* + 1 sent in place of the tracer tile's, caught |
| 30.8: every error stops the chip; the handler reports and parks | `master_trap` 156, `finish` 141 | `master_out_of_pages`, `master_noise_…`, the trap cases |
| 30.8's table: an unknown type, `compression` above 1, a header frame short or long, frame 3's version, the counts, the order, the lengths, the room | 484, `parse_end` 375, 507, 524, the `MR_D_ORDER` and `MR_D_LENGTH` checks, `finish(MR_ROOM)` | `master_stops_on_what_a_frame_says` (seven of them; the counts' check and the room are `MR_D_COUNTS` and `master_out_of_pages`) |
| 30.9: what it costs | | the runner's figures (stage 4's and 5's tables) |

### Decisions

- **How the result carries the record** (FOR-THE-SPEC 5): a result is a four-word request, so the record goes first,
  by the tracer tile's DMA into the master's memory where its load said, and the figures by its core, both counted.
  The master builds the reply around it, so the record is never copied twice. The rows traced are the windows run
  times `RUN_CONFIG.rows`, computed at the end, so that v1.36's segment runs execute nothing new in their windows.
- **The master waits for `EGRESS_COUNT` 0 and parks** (FOR-THE-SPEC 6); the harness runs on until the host's end has
  the reply, at most 2²⁰ cycles, and the run's cycles stay the exit's.
- **The reply is checked against the simulator's own values** where gold has none (the rows, `PAGE_COUNT`, `mcycle`,
  the statistics) and against record *s* + 1 for the state, as 29.14 says the host would.

### Disagreements

**One bug of the firmware's own, against 30.8:** a header frame one row longer than its fields was not a stop.
`parse_end` refused only rows not yet copied, and a whole row already in the parser's buffer went unseen, so the
next frame was read from the wrong place (it stopped later on a type of 0x17). Found by the new test; fixed.

### The checks

| check | result |
|---|---|
| `test_master` (35): the reply at the host's end in every proof; the seven frame checks | pass |
| `test_sort` (6), with the dump's characters on the UART line | pass |
| `./run_tests.sh quick --check` | **all passed**; no log differs (the tracer program's new step runs only when its header asks) |
| the images: every one but `master.elf` and `tracer_ctl.elf` byte for byte as before | as built |
| **the planted bugs** (`fw_teeth.sh`): the reply's *s* and *N* swapped; `EGRESS_COUNT` 71; record *s* + 1 for the tracer tile's; the record before the header; `STAT_CYCLES` wrong; the result without the record; the rows a window short; no store to `UART_CONTROL` in an image that prints; a whole unread row not seen; frame 3's version not checked; and WORKER7's and the earlier ones | below |
| the 37 proof runs, the reply checked; `make check` | the runner (below) |

**Not planted:** the reply sent without waiting for the tracer tile's record, a race these timings never provoke (the
record lands long before the master has every result), as the handshakes' are.

### The reference cycles

The exit moves by the reply, a few hundred cycles: `test_master`'s proofs end 0 or 512 cycles (one poll) later than at
stage 4. v1.36's runs do not move.

### The runner

With stage 4's: Quests 253 to 255 (above).

## Stage 6: the driver and the three processes

### What was built

| | where |
|---|---|
| **the chip on the wire**: its two link ports on WORKER10's wire (`../updater_wip/wire`, used as it is, built in place with the simulator's flags), `exchange` on the end towards the driver and, with an ASIC below, the one away from it, every chip cycle from cycle 0; `chain_next` 1 when there is one below. `proof_run --clients N --client k` (the launcher's arguments, `wire_args.h`), *s* defaulting to *k* | `tests/wire_ends.h`, `Mach::attach_wire`; `tests/proof_run_main.cpp` |
| **a chip's end**: its run goes on past the master's exit until its link end has first sent towards the driver its own reply's link packets and those of every ASIC below it, three each, and 4,096 cycles more; then `finish()`. A chip whose proof failed finishes at once | `tests/proof_bench.cpp` (`settled`) |
| **the driver**: gold's frame file through WORKER8's `End`, each frame a new link packet of at most 32 words, the stop bits the `End`'s; the replies taken by hops (`take_from`); each checked against gold's files in the words they give (`chain_reply.h`: the header word, *s* and *N*, the rows traced, `PAGE_COUNT` 0, the error words 0, the record's state against record *s* + 1); a reply that has not come by `--limit` cycles a failure | `tests/driver_main.cpp`, `tests/chain_reply.h` |
| **a stand-in ASIC**: the link's `Node` alone, its frames checked byte for byte, its reply made from gold's files (`--silent K`: one that never replies) | `tests/standin_asic_main.cpp` |
| **the wire loses nothing**: a damaged link packet, or one ahead of its number, at any link end fails the chip or the driver | `tests/proof_bench.cpp`, `tests/driver_main.cpp` |
| `wire_launch`, `driver`, `standin_asic` in the build; gold's references for segment 1 of the five fixtures | `tests/updater.mk`; `tests/updater/*-s1.images.txt`, `make_fixtures.sh` |
| the checks | `tests/chain_run.sh` (`run_tests.sh`'s step `chain`); the runner's items `chain-evm-zone`, `chain-cyclone` (2²⁹) |
| the planted bugs | `tests/chain_proc_teeth.sh` |

### Decisions

- **When a chip finishes** (the plan's open point, settled without changing the wire): the wire does not tell a
  process that a neighbour has finished, so a chip decides from its own link end. A chip above another must clock
  its link until the replies from below have passed through it: it counts the link packets with data it has first
  sent up, which on a wire that loses nothing is all that is needed, and adds a margin.
- **The driver's output byte is a flip-flop:** the `End`'s byte of a cycle is what it made in the last, as a chip's
  ports are, so that a cycle's output is final before its input is taken, as the wire requires.
- **What the driver checks of a reply** is what gold's files give; the chip's own words (`mcycle`, the statistics,
  the record's words 1, 66 and 67) are the chip's harness's to check, as stage 5 does on a single chip.

### The checks

| check | result |
|---|---|
| `chain_run.sh` on the five fixtures (kin-raw, kin-lru, jumps-base, divs, kin-big): the stand-ins | both replies right, in each |
| a chain of two chips: each chip every check of a proof run (gold's images, its last record against record *s* + 1, the master's results, no damaged link packet), the driver both replies right, sorted by hops | pass, all five |
| the same run twice | every line of every process the same, the wire's waits and the wall time apart |
| a chain of one | the chip passes, its reply right |
| a reply that never comes (stand-in ASIC 1 silent) | the driver fails at its limit, 400,000 cycles, and names ASIC 1 |
| **the planted bugs across processes** (`chain_proc_teeth.sh`): hops not counted; a forwarded header not sealed again; a stop bit ignored; forwarded link packets out of order; `chain_next` ignored; the replies sorted by hops the wrong way round; a reply that never comes not counted in cycles; a chip cycle without its exchange | **8 of 8 caught**. Stage 4's other two (an own link packet before a forwarded one; `clear_error()` called by the chip) do not show across processes (each reply comes whole and is sorted by hops; nothing is reset mid-run) and stay caught by `test_ingress_chain` |
| 2²⁹, both workloads, segments 0 and 1, twice | the runner (below) |

The small inputs here are the fixtures (guests); the real workloads are the runner's. A chain of two on a fixture is
about 240,000 cycles in 1.2 s on this machine's two cores.

### The runner

To be queued once Quests 254 and 255 are in: `chain-evm-zone` and `chain-cyclone`.

