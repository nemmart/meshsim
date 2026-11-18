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
| 245 | `suites` at `fd3ff56d`: `./run_tests.sh all --check`, the 37 proof runs | queued |
| 246 | `suites-varied`: the same with every chip's LPDDR jittered | queued |
| 247 | `check`: `make check` | queued |
