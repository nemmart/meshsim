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

**Recipe 26 (DECISIONS-v1.25, definition of done, W4).** `wip/sw/recipes/rcp_replay.c` now has `template_key(w)`, and
`replay_loop` passes the presented word's key to `template_lookup`.
- **Test side:** `wip/meshsim/sw/recipes_support.c`'s `template_lookup` is keyed by key.
- **The book:** the prose in `RECIPES.tmpl.md` (a "lookup is by key" note and walk step 6); `RECIPES.md` regenerated
  with `gen_book.py`.
- **Checks:** `check_book.py` 46 blocks, 0 differ; `build.sh` all compiled; meshsim's `build_recipes.sh` 19 built;
  `RecipeTest` 2042/0.
- **The recipe's key against the model's:** compiled natively, `template_key` equals `ReplayTables.key` (the Java
  model's) on 20,000 words (random, inline, CSR patterns).

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
**Quest task 078** (`078-w11-calibrate`, renumbered from 077, which WORKER4 also took) runs it on the runner.

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

## 4. The hours and the shard plan (measured: Quest task 078)

**Task 078** (`078-w11-calibrate`, runner `godspeed`, 16 threads, 62 GB, Java 21):
- 128 synthetic 65,536-row windows, generated in 5 s (653 MB of scratch).
- Replayed at 1, 4, 8 and 16 JVMs; every window matched in every stage.

| JVMs | windows | Stage A, s/window | Stage B (tile 0 alone), s/window | wall |
|---|---|---|---|---|
| 1 | 8 | 0.02 | 0.49 | 4 s |
| 4 | 32 | 0.03 | 0.62 | 5 s |
| 8 (with stage F) | 32 | — | 1.16 | — |
| 16 | 128 | 0.16 | 2.10 | 20 s |

- **Stage F (the full chip)** on the 8-JVM run's 32 windows: 14.3 s per window, about 12× slower than Stage B alone.
  Its cycle counts equal Stage B's window for window, so parking the idle tiles holds on the runner too.
- **Throughput saturates:** per-JVM time grows with parallelism (memory bandwidth, or hyperthreads sharing cores).
  Throughput levels off at about **6.4 windows per second (about 23,000 an hour) from 4 JVMs up**. The wall times are
  whole seconds, so the 4-JVM figure is coarse; the 16-JVM run (128 windows in 20 s) is the one to plan with.

**The hours:**
- 8,192 windows per program (2²⁹ rows padded; all replayed, padding included) take **about 21 minutes per program** at
  16 JVMs, about 43 minutes for both programs.
- Stage F on every 64th window (128 per program, `-x 64`) adds about 128 × 14 s / 16 ≈ 2 minutes.
- **One task per program, about 23-25 minutes**, under the runner's 45-minute limit with nearly 2× margin.
- On the full chip alone it would be about 4 hours per program (8,192 × 14.3 s / 8 JVMs at the 8-JVM rate). This is
  what parking the idle tiles saves.

**The tasks** (DECISIONS-v1.25 s7 revised; WORKER9's tasks 079 and 080 write the deliveries):
- **Location:** `/tmp/w9-deliveries` on the box, with one combined `manifest.json` for `evm-zone-29` and `cyclone-29`
  and the universal tables in each program directory.
- **One task per program:**

      ./run_shard.sh -d /tmp/w9-deliveries -p evm-zone-29 -f 0 -n 8192 -j 16 -x 64 -m <manifest.json SHA-256> $RES/evm-zone-29
      ./run_shard.sh -d /tmp/w9-deliveries -p cyclone-29  -f 0 -n 8192 -j 16 -x 64 -m <manifest.json SHA-256> $RES/cyclone-29

- **Fallback:** if real windows turn out much slower than synthetic ones, each program splits into two tasks
  (`-f 0 -n 4096` and `-f 4096 -n 4096`). This is unlikely: the synthetic windows run at 3.15-3.3 cycles per row in
  Stage B, the range expected of real ones.
- **Not used:** the older `/tmp/w9-selected-windows-821e1bd.tgz` predates `imm_signed` [86].

## 5. The acceptance tasks

WORKER9's tasks 079 and 080 wrote the deliveries to `/tmp/w9-deliveries` (EVM Zone 46 GB, Cyclone 44 GB; 8,192
windows each, every one proven by the writer's own replay).
- **The tasks read the per-program manifests,** `PROGRAM.manifest.json`, checked by SHA-256 (`-M`, `-m`). The combined
  `manifest.json` changed when task 080 added Cyclone, and the per-program files don't.

| task | program | per-program manifest SHA-256 |
|---|---|---|
| **083** `083-w11-accept-evm-zone-29` | evm-zone-29 | `7142e894c80a0c9d491f21c852a942885e0abd97cfe6aefd0101d716824e138f` |
| **084** `084-w11-accept-cyclone-29` | cyclone-29 | `f8a0622e144a944c51386b5b36f81101dcdeb16e5a8d950c1e9ca0e3097f8982` |

- **Numbers** claimed in Quest's `NUMBERS.md` (ed5aa54). **Kit SHA-256:** `9a1acd88…`.
- **Each task runs:** `run_shard.sh -d /tmp/w9-deliveries -p PROGRAM -M …/PROGRAM.manifest.json -f 0 -n 8192 -j 16 -x 64 -m SHA`.
- **Not used:** the older `w9-selected-windows-821e1bd.tgz` (it predates `imm_signed`).

## 6. For the merge: changes outside the replayer

One change is in WORKER4's area of the simulator (`src/Chip.java`): it is opt-in, off by default, and nothing but Stage B
uses it.
- **`Chip.parked`** (`boolean[]`, null by default): tiles whose components `Chip.runCycle` skips. With it null, the
  loop is v1.2's unchanged.
- **`Chip.parkedUntouched()`**: true if every parked tile is quiescent and no link is busy. Stage B's check.
- Used only by `test/StageB.java` (`StageB.LONE`, `-Dstageb.lone`, ReplayShard's stage B), which parks tiles 1-63 and
  fails the window if `parkedUntouched()` is false. Stage F (the full chip) keeps it null.

Everything else is replayer-side:
- `src/Replayer.java` and `src/ReplayerAlu.java`;
- `sw/replay/` (the controller and its layout);
- the replay tests in `test/` (`Replay*`, `StageB`, `SyntheticDelivery`), with `data/vec-821e1bd.tsv`;
- `acceptance/`;
- the two replay documents;
- `run_tests.sh`'s replayer line.
