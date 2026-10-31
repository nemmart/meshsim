# WORKER11 report: meshsim's replayer at v1.25, and the acceptance kit

Branch **`v1.25-meshsim`** (from `v1.2-meshsim`, which is merged in as it moves). Everything is in `wip/meshsim/`.

## 1. The replayer model at v1.25

The model follows jolt-cpp `821e1bd` and the universal tables (`wip/tables/replayer`), per DECISIONS-v1.25 s1, s4-s7.
Changes are listed in `REPLAYER-NOTES.md` ("v1.25"), and the formats in `REPLAY-INPUT-REQUIREMENTS.md`.

**Checked against WORKER10's spec** (branch `v1.25`, the 10-point list in `WORKER10/REPORT.md`): there is no
disagreement (`QUESTIONS.md`). Two things were added for the list:
- the row's `imm_signed` at word 0 [86], as modelled since Q1;
- PEXT with a non-contiguous mask counted as a statistic.

The appendix's opcode table was also checked by script against the model (numbers, names, classes, cycles).

**Tests:**
- **`ReplayerTest` 1461/0:**
  - the opcode table;
  - the ALU against WORKER9's 252 vectors at `821e1bd`;
  - the unit tests, including the hand-checked program re-derived for the 3-row LB.
- **StageB synthetic 18/18.**
- **`SyntheticDelivery`:** random RV64IMAC programs (including atomics, division with jolt-cpp's advice, every load
  and store width, ECALL, host I/O, CSR instructions and every inline) run through the universal tables.
  - The functional replay is compared with a plain RV64 interpreter after every ALU, M, memory, branch, jump and atomic
    instruction.
  - 12 seeds × 4 windows of 65,536 rows gave about 10K checked instructions each and 0 mismatches.
  - A deliberately broken `ShiftDataB` is caught.
  - Its deliveries replay OK in Stage A in every variant, and in Stage B.

**The controller (`replay_ctl.c`) at v1.25:**
- It looks templates up by the normalized key.
- The fast path holds only the templates in the window's bitmap.
- The NOP-policy check moved to run time, because the key drops rd.
- The layout is v2, with the universal templates at chunk 0x58000.
- **A start-up stall was found and fixed.** v1.2's fast-path table (128 slots, up to 4,096 multipliers tried) never
  finds a collision-free layout for about 40 templates, so the search took millions of cycles. It now uses 512 slots and
  256 tries; a key that loses its slot takes the slow path, as before.

## 2. The acceptance kit (`acceptance/`)

`run_shard.sh` reads a shard of windows from the delivery that WORKER9's writer task leaves on the runner box
(DECISIONS-v1.25 s7, revised), and only reads it.
- **Delivery layout:** WORKER9's (`/tmp/w9-deliveries/PROGRAM/` with `PROGRAM.manifest.json` beside it) or a plain
  `manifest.json`.
- **Integrity checks:**
  - the manifest's SHA-256 (`-m`) first;
  - then every file a window's replay reads (the record, the streams its flags name, `rows.bin`, `end.bin`), with a
    mismatch reported as CORRUPT;
  - the tables against `wip/tables/manifest.json`.
- **Replay:** `-j` JVMs run Stage A (the model with the ideal controller) and Stage B (`replay_ctl.c` on a meshsim tile).
  Each compares every row byte for byte, checks the streams are consumed exactly and the end state matches, and requires
  no conditions or violations.
- **Results** go to `OUTDIR` only: `results.csv`, `SUMMARY.txt` (rows per JVM-second) and `VERDICT.txt`.
- **Tested end to end** from the unpacked kit on synthetic deliveries in both layouts:
  - pass;
  - a corrupted `rlog-lru.bin` reported as CORRUPT;
  - a wrong manifest SHA-256 refused;
  - an 8,192-window manifest parses in about 1 s.

`calibrate.sh` builds a 128-window synthetic delivery in scratch and replays it at 1, 4, 8 and 16 JVMs.
**Quest task 077** runs it on the runner.

## 3. Stage B on a minimal machine (the owner's point 3)

**The finding:** Stage B's time was spread over every tile's router, SRAM, queues and control: meshsim runs all 64
tiles each cycle while one replays. In a Stage B replay the other 63 tiles only wait in their boot ROMs, and nothing
reaches them: tile 0's streams come from its own big SRAM to its own replayer.

**What I did:**
- **`Chip.parked`** (opt-in, default off, nothing else uses it): parked tiles' components don't run.
- **Stage B "on tile 0 alone"** parks tiles 1-63.
- **A guard at the end of every run:** `Chip.parkedUntouched` (every parked tile quiescent, no link busy) or the window
  fails.
- **Stage F**, the same replay on the full chip. `run_shard.sh -x K` adds it on every K-th window and requires its cycle
  count to equal Stage B's.

**Result:** identical rows, cycles and stalls to the full chip on every window tried (synthetic, 16K and 65,536 rows),
and about 4-7× faster:

| | per 65,536-row window (sandbox, one JVM, warm) |
|---|---|
| Stage A | 0.16-0.33 s |
| Stage B, tile 0 alone | ~1.8 s (36K rows/s) |
| Stage B, full chip | 7.4-13 s |

It's cheap (about 20 lines in `Chip`, opt-in). A smaller topology (`hops.txt`) isn't possible: meshsim's network is the
fixed 8×8 torus.

## 4. The hours and the shard plan (preliminary: replaced by task 077's measurements)

**Windows:** 8,192 per program (2²⁹ rows padded). The real rows are 354.9M (EVM Zone, about 5,415 windows) and 371.5M
(Cyclone, about 5,669). All 16,384 windows are replayed, padding included.

**From the sandbox** (1 CPU): about 2.1 s per window for A + B (tile 0 alone).
- That is 16,384 × 2.1 s ≈ **9.6 JVM-hours for both programs**, or **about 36 minutes on 16 threads** if they scale.
- On the full chip it would be about 40 JVM-hours, or 2.6 hours on 16 threads.

**Plan, per program, after its writer task:**

    run_shard.sh -d /tmp/w9-deliveries -p evm-zone-29 -f 0 -n 8192 -j 16 -x 64 -m <manifest sha256> $RES/evm-zone-29

The estimate is about 18 minutes for A + B, plus about 1 minute for the 128 full-chip windows. That fits one task under
the 45-minute limit with more than 2× margin. If task 077 shows the box slower or scaling worse, the shard is split in
two (`-f 0 -n 4096`, `-f 4096 -n 4096`).

**Inputs needed from WORKER9:** each delivery's path (expected `/tmp/w9-deliveries`, from tasks 070/071) and its
manifest's SHA-256 (in `results/070…/manifest.sha256`). The tasks are made with
`acceptance/make_task.sh KIT TASK './run_shard.sh …'` once those exist.

## 5. Open

- Task 077's numbers, to finalize section 4.
- The acceptance tasks themselves, after WORKER9's writer tasks have run.
