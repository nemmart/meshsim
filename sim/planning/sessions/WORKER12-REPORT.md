# WORKER12 report: M2's G4 (the address generator, the gather engine, the sort end to end)

Branch `sim-w12`, merged into `main` (last c4cdab7). Day-by-day detail is in `WORKER12/NOTES.md`; this is what the next
workers need.

**State at hand-off:** the chip runs M2Sort's p4 uniform k128 sort equal to `ref` from end to end, at both sizes.
- **16K** (`sort16k`): every one of the 248 fingerprint points of every unit of every tile and the chip; M2Sort's
  verification PASS; the 98 phases; TOTAL 496,619. Checked on one thread, on 2, 4 and 8 domains, under shuffle,
  instance-major and step-all, and in gcc debug (ASan, UBSan, the missed-wake detector).
- **Full size** (`sort64k`, Quest 154): all 321 points, PASS, the 98 phases, TOTAL 1,229,687, on one thread and on 8
  domains.

No fingerprint differed at any point, so there is no D30 case in G4. `make check` is clean on gcc and clang (debug,
release, `tsan`, `tsan-units`, `tsan-chip`); run it build by build, since one call exceeds the 5-minute tool limit.

## Files

| file | what |
|---|---|
| `src/units/ag.h` | the address generator (`AddrGen`), a line-by-line port of `AddressGenerator.java` with its field names |
| `src/units/gather.h` | the gather engine (`Gather`), as `GatherEngine.java` |
| `src/units/g4.mk` | my build rules (`test_sort`), included by `sim/Makefile` |
| `tests/test_ag.cpp`, `tests/test_gather.cpp` | the engines' rules, case by case, worked by hand from ref's code |
| `tests/ref/EngConf.java`, `engconf.sh`, `tests/engconf/`, `tests/test_engconf.cpp` | the engines' conformance against ref, every cycle |
| `tests/sort_input.h` | SortTest's `readKeys`, `value32`, `valueHalf`, `loadInput`, `Oracle` |
| `tests/test_sort.cpp` | the sort's driver and its checks (the boot in make check; the run on demand) |
| `planning/sessions/WORKER12-tasks/` | `mk_task.sh` (a non-timing Quest task with an embedded kit), `sort64k-body.sh` (Quest 154) |

Edits to other workers' files, each agreed and noted in NOTES:
- **W8's register block, by the split agreed with W11** (`regblock.h`, `tile_ctl.h/.cpp`, `director.h`; my registers and
  bits only): PULSE_DMA; STATUS rd_running, rd_done and gather_busy; EVENTS [10]; ERRORS [3] (drift) and [6]
  (GATHER_OVF) as events.
- `regmap.h`: PULSE_DMA's store is modelled.
- `test_rv32.cpp`: PULSE_DMA's death test became a test that its store reaches the block.
- `statefp_g1.*`: real "ag" and "gather" writers.
- W11's `mem_stand.h`: the G4 clients' ends for the G1 benches.

## 1. The address generator (`ag.h`)

- **A port, not a redesign.** It keeps ref's field names and order (`StateFp.ag` reads them as they are): the start, the
  two read-ahead rings (`ag_lookahead`, default 2L + 2), the STREAM reader, stream runs (gaps, count-0 streams, the
  last gap not waited), group runs (S_g, `rotSched`, R = 0 groups, late starts and underruns moving the timetable),
  the refusal and retry, the done test on `held_at_start`, drift as an event, and `checkEntry`'s counts.
- **Its configuration** (kind, run start, run count) is read from the block's `dma` RegFile at every step, never
  copied, because ref's fields are overwritten by every DMA_CONFIG store.
- **The start pulse** is a Wire from the block carrying the count of PULSE_DMA starts, driven at level 0 of t + 1 (the
  shifted `endCycle(t)`). A change is a start, so two starts never look like one.
- **Status** goes to the block as a Reg (running and done at the end of t). The block ORs in its own pulse of that
  endCycle for ref's `runningForStatus` and `doneForStatus`.
- **UB stops are named and fatal:** a start while running, a run past the table's end, reserved modes 6-7, and gather
  buffers not 128-bit aligned. The last reads PTR[me] through W11's PTR view (TableStore::ptr_view: PTR as at the end of
  t - 1, as ref's AG reads it before the table store steps).
- **Wakes:** `next()` while running, since ref's timetable counts every cycle; otherwise only on the start Wire or a table
  return. Timed wakes across gaps and rotations are a possible speed item; not done.
- **Footprint (W10's study):** about 3 KB a tile. The rings are sized for the maximum lookahead (64) where the sort uses
  6, plus the per-run statistics arrays. Sizing the rings to the configured lookahead at construction is a small change
  if it ever matters.

## 2. The gather engine (`gather.h`)

- **The step** follows `GatherEngine.runCycle`: a refusal is retried with nothing advancing; otherwise it takes a request
  when idle, then makes one read a cycle. k and the base are read live from the `dma` RegFile each cycle (ref does);
  `wide` and `left` are fixed when the request is taken.
- **STATUS's gather_busy** is ref's `GatherEngine.busy(t)`, the OR of four terms no single unit knows. The block ORs
  three inputs:
  - the gather's own Reg (queue non-empty after the take, `left > 0`, or a put at t);
  - W11's `gather_srq_busy` Reg (the Hold occupied at the end of t, or reads in flight: only the consumer knows whether
    it took the word);
  - the director's `gather_put` event (ref's `isEmpty` counts the queue's pending put).
- **The queue's overflow** reaches ERRORS [6] as an event through W10's `overflow_steps` (D29), judged at the put's
  t + 1 as the router's (D28).

## 3. EngConf: the engines against ref, every cycle

`tests/ref/EngConf.java` (ref's own AddressGenerator, TableStore and GatherEngine, read by reflection, nothing in `sim/ref`
changed) and `tests/test_engconf.cpp` (ours, on W11's real table store) run the same seeded script.
- **The script:** random STREAM and GROUP tables (every mode but 6-7; size 11, misaligned sources and odd destinations,
  counted as violations), runs started only while idle, mode-4 requests, and the read queue taking one client a cycle
  (gather first) at phased rates.
- **Each cycle's observation:** both Hold puts, rd_running and rd_done, the AG's and gather's StateFp streams, the
  three-way busy, drift and violations.
- **Coverage:** EngConf reaches what the sort never does, including group-run refusals (drift: 173-898 events a fixture),
  underruns, late group starts, R = 0 groups with gaps, count-0 streams, and k odd and even.
- **Results:** the four fixtures (L = 1, 2, 3) are equal under every debug option; the 24 long runs of 10^5 cycles are
  equal; a negative check reports a perturbed line.
- **Generator note:** the first version let the gather (client 0) starve the AG for most of each run, so the gather's
  rate was lowered.

## 4. The sort's driver (`test_sort.cpp`)

- **The program:** SortTest.Machine.boot (`run_when_ready(0)`, without Mach's extra `.call`), a release point, then
  M2Sort's sequence exactly as M2Sort queues it (L1, SPLIT + 66 barriers, ..., L2, EXIT; `at_all_ready(0)` between).
- **The run:** cycle by cycle until C&C is idle and every core waits in WFI. That is B = 14,791, as ref.
- **At B, between cycles:** SortTest's input load (big SRAM through W11's `poke`, S.n_in at the image's `S`), then
  `release_next()` and `wake("cnc")`.
- **After B:** every golden point is compared, every 10,000 cycles and at each GO and all-READY, for every unit of every
  tile and the chip.
- **The phases:** a GO opens a phase and the all-READY a later GO waits on closes it. The last (EXIT) closes at the run's
  end, as PhaseLog.close.
- **At the end:** M2Sort's verification (the Oracle, receive sizes, values, every core exited 0, violations summed over
  the units, ERRORS) and TOTAL from `NAME.out`.

## 4b. The register block publishes only what changed (D31)

TileCtl's step used to recompute all 33 registers through `RegBlockState::reg`'s switch and write each into the
register file, which the engine compares with its stored value. That loop was a third of the block's step and 12.4% of
the sort.
- **Now:** `RegBlockState::publish(pub)` compares the registers' values (straight-line, the same as `reg()`'s) with
  TileCtl's copy of the register file as last written. It returns a mask of those that changed, and TileCtl writes only
  them; `dma`'s two registers likewise.
- **Why it's exactly equivalent:** the engine ignores a write of an unchanged value, and TileCtl is the only writer.
- **Guard:** debug builds compare every register with `reg()` after each publish. **A register added to `reg()` must be
  added to `publish()`;** the guard names any that is missed.
- **Checked:** wfi, regs and a2a (every unit every cycle; every tile), the 16K sort in debug and release, and make check
  (c6d480c). W10 measures it on the full-size sort.

## 5. To reproduce

```
# the key sets (KeyGen; sha256 as HASHES) and the 16K hash-only golden
javac -d CLS ref/meshsim/src/*.java ref/meshsim/test/KeyGen.java
java -cp CLS KeyGen KEYS/keys-16384 --n 16384 --only uniform        # 3db703f3...
java -cp CLS KeyGen KEYS/keys-65536 --n 65536 --only uniform        # a7636b68...
KEEP=FPDIR tests/golden-m2/regen.sh --check sort16k-fp              # SAME; a few minutes
make CC_NAME=gcc MODE=release build/gcc-release/test_sort
SORT_KEYS=KEYS/keys-16384/uniform.keys SORT_FP=FPDIR/sort16k-fp.fp build/gcc-release/test_sort
# the full size (the golden is Quest 135's, $DATA/w7-golden-m2/fb6f21e/sort64k-fp.fp on the runner)
SORT_SIZE=64k SORT_KEYS=KEYS/keys-65536/uniform.keys SORT_FP=.../sort64k-fp.fp build/clang-release/test_sort sort_runs
#   SORT_THROUGH=N, SORT_PARTS=P (row bands), SORT_BLOCK=1 (the barrier sleeps: more threads than CPUs),
#   SORT_OPT=shuffle|instance|stepall, SORT_DUMP=1 (dump the first differing unit)
tests/ref/engconf.sh --check                                        # EngConf's fixtures equal ref's regeneration
tests/ref/engconf.sh 100000 DIR && ENGCONF_LONG=DIR build/.../test_engconf long
```

On the runner, Quest 154 does all of the full-size work. Its body is `WORKER12-tasks/sort64k-body.sh`, made with
`mk_task.sh 154-... sort64k-body.sh "header"`.

## 6. What's owed, and notes

- **The register block's speed change (D31):** to be measured by W10 on the full-size sort (§4b).
- **Possible work from W10's hot-lines write-up** on the AG and gather, through the integrator. The obvious candidates
  are timed wakes across the AG's gaps and rotations (it now steps every cycle while running) and the ring sizes. Either
  must keep every check above exact, or come as a D30 proposal.
- **The 16K sort on 8 domains** is checked only on the runner (Quest 154); the one-CPU container can't finish it.
- **`Oracle` handles the M2Sort (unshaped) case only.**
- **For whoever maintains the runner tasks:** in a task's `results/NNN/run.log`, the last VERDICT line and the "done"
  line appear twice (Quest 154: lines 42/46 and 44/48). The likely cause: the runner captures the task's stdout into the
  same `run.log` the task tees into, so the two writers overlap. Because the task builds `VERDICTS.txt` from `run.log`,
  the repeated last verdict shows there too (154's has eight lines for seven verdicts). It is cosmetic: the verdicts are
  the same, and reading them as a set is enough. Every task built on this pattern (W10's too) shares it. Writing the
  task's own log under another name, or the runner's capture elsewhere, would remove it.
