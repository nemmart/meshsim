# meshsim on spec 2026-10-26n (checked against 26r): deviations

The simulator session of 2026-10-27 (`SIM-CHANGES-v1.1-26n.md`). What meshsim does differently from the spec, or
leaves out:

1. **No request-bus receivers.** The request port and the tree are modelled; no target has a receiver (the endpoint map
   is deferred, item 82), so the root drops and flags every request. Tests strap a target to a tile's receive queue
   (`Config.targetReceiver`, test only); with no strap every `REQUEST_RECEIVE` is an underflow.
2. **The sender's port has nothing attached** on tiles 0-63; the sender records its packets in a test-only sink. Every
   mesh tile's peripheral is the replayer, which has no output queues, so a `SENDER_SEND` is ignored and flagged.
3. **The UART is unwired only:** TX bytes are printed to the tile's console, never stall; `UART_DIVISOR` reads its reset
   value (434 at 25 MHz); no shifter timing.
4. **Register and table-window loads take 2 cycles,** the worst case the spec allows and meshsim's reference.
5. **The request bus's hop latency** (3 cycles per hop over pipelined wires, one-request buffers per node input) is
   meshsim's choice; the spec gives only `reqbus_latency` as an estimate.
6. **Read-port results and incoming traffic:** meshsim still checks 4.3's rule (invariant 14), although with the
   token-protected loopback it measures no loss when the rule is broken (`deliverables/SPEC-OPEN-26n.md` item 2).
7. **Removed (part M):** SortTest's C&C now waits `stage_delay` = 32 cycles after `CMD_STAGE`'s GO store, the spec's
   value (6.2), instead of its own 64.
8. **Simulator-only header bits** carry a read return's token ([46:41]) and two tags ([61], [62]); they don't change
   routing or landing.
9. **Run elsewhere:** CtrlTest full, EngineTest full, the skew table, the full sweep and the 43 full-size P4 runs don't
   fit this session's turns; they ran on the runner machine from `runner/` (all pass: `RESULTS-full-size.md`).

---

# meshsim v1.0: what changed, and what deviates

The v1.0 code milestone (`WORKER18_PROMPT.md`), built on meshsim M15. Measurements are in `RESULTS.md`.

## What changed

**Hardware model (V1.0-CHANGES items 1–4).**
- The wake timer is gone: register, wake source, latch, timer late, and the timer's part of the done gate. `WFI` wakes on GO or the local wake.
- `AUGMENT_BASE_ADDRESS` and `GATHER_BASE` are one register, `DMA_RECORD_BASE_ADDRESS`, used by augment and gather.
- Arm is merged into start. `DMA_PULSE` = done [0], start [1]; [3:2] reserved, flagged. Start loads the run from `DMA_CONFIG` and begins the read-ahead; `rd_running` is 1 from the start; the first request is offered as soon as its entries are in: 4L + 3 cycles after the start store (4L + 2 after the cycle it takes effect) for a group run, 2L + 2 (2L + 1) for a stream run; S₀ is that cycle. `rd_ready` is gone (`TILE_STATUS` [2] spare). A start while running (lead-in included) is ignored and flagged. In one store, start acts before done.
- 11 conditions in [10:0] (no timer late).

**Registers.** The 13 v1.0 registers at 0x800 + 4i, generated into `mesh.h` 1.0. Field macros carry their register's prefix: `DMA_CFG_`, `DMA_PULSE_`, `TILE_ST_`, `TILE_COND_`, `ROUTER_HDR_`. `RegDoc` and `RegDocTest` read the names directly (the mapping mode is gone); REGISTERS.md's tables are regenerated.

**Simulator alignment (items 5–8, section 13a).**
- Every FIFO is full by its count before the cycle's pop: the overflow FIFOs, the area queues (`ready_w` = count < depth, `ready_r` = count + `push_w` < depth) and the token FIFO.
- A run ends when the address generator has walked its last entry; the last gap-after is not waited (a stream run used to wait it).
- Window loads from `PTR`, `STREAM` and `GROUP`, and sub-word loads from the window or the registers, are bus errors. `hello.c`'s and `regs.c`'s table readbacks moved into Java (`CtrlTest` reads the tables through the simulator).
- HALT holds the engines in reset (13a): the address generator to IDLE, the GATHER queue and active register, the read port's queue and in-flight count and the token FIFO clear.
- An out-of-range read (a smaller device) makes no area access: its token is marked zero and released in order with payload 0 (per part for a read spanning two rows).
- 13a audit: everything else matches (send port, router round robin and hop, C&C's receive FIFO, READY and RUN/HALT edges, a controller halted by an error, chip reset, the parameters, the read-port result). Misaligned reads and writes were already supported as the spec now says (two rows, merged in the return path).

**Spec rulings of 12 October (package 12u).** Digit selectors 5–7 are treated as 0 and flagged; a run count of 0 ends at once with no requests; stream or group numbers past the table's end wrap and are flagged; `SRAM_RD_DEST_ADDR` advances on every store (meshsim already did); a packet sent to dst 65 is flagged; `DMA_PULSE` [3:2] flagged. Tested in `AgTest` ("v1.0 rulings").

**Software.**
- The sort program: no arm, no `rd_ready` polling, the v1.0 names. `arm.c` is `config.c`, its functions `config_*` (`sort.ld` follows). The images are 96 bytes smaller (overlays xfer −40, th −16, btrun −40); the kernel is the same size.
- The split: `split_regs.h` at the v1.0 addresses (it hard-codes them; `RegDocTest` now checks every one against `mesh.h`); its standalone driver `splitdrv.c` too.
- The skew planner: two comments. The patches (`sort/program/patches/`) are regenerated against the v1.0 originals.
- The recipes: the timer recipe is replaced by a synchronised start by identical code (set-up command, then a start command); REPORT's replies are paced by a delay loop of exactly 4 (me + 1) cycles; arm/start merged everywhere; the cycles table is regenerated. `cond.c` and `rcp_status.c` lose the timer-late cases.
- CONVENTIONS.md: the lead-in stated both ways, no timer, no arm, the bus-error list of 4.11, history removed. REGISTERS.md: the `DMA_PULSE` entry with the lead-in table, `rd_ready` and "armed" gone, Appendix A removed (resolved in the v1.0 spec), the worked examples' diagrams fixed (`regdoc/src/ex*`), `REGISTERS.pdf` rebuilt.
- Every version string is v1.0 (meshsim v1.0, `mesh.h` 1.0, the kit).

**Tests.** `TimerTest` is gone. New or changed: `AgTest` (the rulings), `AgBench` (start to first request), `RecipeTest` (the sync start, GO to first request for 6.4, the REPORT pacing), `CondTest`, `CtrlTest`, `EngineTest`, `BusTest`, `MemTest`, `NetTest` (to the count-before-pop rule), `RegDocTest` (sub-word and window loads, the split's header, `../sw/mesh.h`). `PhaseNeutral` has a v1.0 start rule; `VerifySummary` compares M15 with v1.0. `tools/check_layout.sh` (first step of `run_tests.sh`) checks meshsim's build copies (`sw/ext/`, `sw/mesh.h`) against `../sort/` and `../sw/`.

**Found by the full-size BT run, fixed: `rd_running` right after a start.** BT calls `wait_tables()` straight after `start_run()` to write the next layer's tables. In M15 that poll waited on `rd_ready`; in v1.0's first cut a status computed before the address generator acted on the start showed `rd_running` = 0, so BT wrote `STREAM` during the lead-in, meshsim flagged it and the run hung. meshsim now sets `rd_running` (and clears `rd_done`) at the end of the start store's cycle, so the next instruction's load sees it (`AgTest`); REGISTERS.md and CONVENTIONS say so, and `SPEC-TEXT-fan-in-and-area-queue.md` has wording for 8.6. No suite reached BT before (it is never chosen at 64 × 2,000): `run_tests.sh` now runs a forced-BT sort at 64 × 16,384 in both formats, and `SortTest` checks a forced run took BT.

## Measured changes the spec must carry

- **Fan-in (3.2) and the area queue (4.1):** the count-before-pop rule makes 63 × 1 unsafe even without skew and lowers some safe bursts by one (16 senders: 3 → 2; 3: 16 → 15; 2: 31 → 30); an area queue holds at most depth − 1 after a served cycle. Exact text: `SPEC-TEXT-fan-in-and-area-queue.md`.
- **A spanning read in a group run** costs a cycle and sets drift (now in the spec, 4.1).

## Deviations and what I couldn't do

1. **Not run here, for the laptop:** the 92 kit runs at 64 × 2,000 and the split's 25 sets on both versions, and the two full-size BT runs as part of the kit's verdict. Run here instead (`RESULTS.md`): six runs at 64 × 2,000 (direct, LB, TH, each format) and the two full-size BT runs on both versions, all phase-level neutral. `verify-v1.0/` is the laptop kit.
2. **Dropped from the hand-off (history or design v1 tooling):** meshsim's `PROGRESS.md`, its M11 rename tools and M13/M14 comparison scripts; the split's research history (`steps/`, its `RESULTS.md`) and its host-ISS harness (`mtiss.c`, `driver.c`, `report.py`, `build.sh`, `tools/`), which needs design v1's Python to make its data; the planner's Python model; the sort program's `tools/export_keys.py` (design v1) and its copy of `SortTest.java`. The split's test is meshsim's `SplitRun` on `SplitSets`; the planner keeps its harness as a regression test (`sort/skew/harness/check.sh`, both formats reproduce the expected results).
3. **Milestone notes in the simulator's comments:** stripped mechanically from about 130 comment lines (comments only: every one of the 112 compiled class files is byte-identical before and after). **89 comment lines** still carry a milestone, date or DEV number, in sentences the script couldn't safely rewrite: listed in `meshsim-notes-comments.txt`. **31 string literals** (test output and labels) carry one too, left because they are log text: `meshsim-notes-strings.txt`.
4. **Duplicated sources, by design:** meshsim builds the sort from its own copies (`meshsim/sw/ext/`), as before; `check_layout.sh` keeps them equal to `sort/`. The docs (`sw/`) exist once; meshsim's tests read them from `../sw/`.
5. **Accounting:** the phase CSVs count a run in the phase that starts it (`agRuns`, `peak_agTable`); M15 counted it where it was armed. `PhaseNeutral --v1.0` allows that move.
6. **The generic `OverflowFIFO`** in `Buses.java` (used only by `BusTest`, not by the design) keeps M15's rule (a pop makes room in its own cycle); the design's FIFOs (`WordFIFO`) and queues use v1.0's.
