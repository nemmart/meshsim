# WORKER7 notes: the plan (proposed 2026-11-07; go-ahead 2026-11-07)

Branch `sim-w7`. Nothing below is built yet; `sim/ref` is not touched (D5 stays W2's seven call sites).

## What I start from

- **The run.** `p4/uniform-k128` is `SortTest -1 run kind=uniform n=65536 fmt=k128 ready=8` (v1.26.1's `runner/p4`
  jobs; 1,229,687 cycles L1 GO → L2 all READY, WORKER28 BASELINES-8). `SortTest.sortRun`: build `Chip(Config.machine())`,
  boot through C&C (image broadcast, CMD_JUMP, GO), the input-load stand-in (`sram.poke`, `pokeS`), the fixed command
  sequence with `atAllReady` callbacks (the oracle split only if the image has `ORACLE`), `finish`, verification by peeks.
- **C&C.** `Config.ccCores` is false by default, so the sort runs the **Java sequencer `CnC`**, not the cores. To confirm
  dynamically in item 1, and to state exactly what of `CnC` the framework must reproduce (command/GO broadcast timing,
  READY collection, `waitUntil`/`call` points, `loadWords`).
- **Fingerprints today:** `-Dmeshsim.digest` hashes 20 counters per tile **at exit only**. `Chip.digest()` is a plain
  method, so a driver outside `sim/ref` can call it between cycles: fingerprints at intervals with no change to `ref`.
- Local box: 1 CPU, 3 GB, JRE only. I install a JDK and `gcc-riscv64-unknown-elf` with apt (as W2 did) to build the
  sort and `sw/tests` images; images are committed with a manifest, since the runner has no riscv toolchain (D4).

## The plan, by the prompt's five items

**1. What the sort exercises** (`SortCover.java` in `sim/tests/ref/`, ref's default package).
- Static: every unit `Chip`/`Tile` builds under `Config.machine()`, every bus and its kind (PORTS.md section 0's table).
- Dynamic: run the sort at n = 16,384 locally (full size on the runner), polling between cycles only state ref already
  exposes (unit counters, bus occupancies and overflow counts, `PhaseLog` peaks, AG/memory reports, request-bus stats,
  `inWfi`/`halt`), so each unit, bus and chip-level mechanism is marked used or never used, per sort step.
- The controller's side: tile 0's (and one far tile's) retire trace via the observer, reduced to the register-block and
  window offsets loaded/stored, the commands and the interrupt/WFI paths taken.
- Output: a table in `AUDIT-M2.md` section 1: used, unused (replayer, Tracer, peripherals, ...: stated, not assumed), and
  the harness stand-ins (boot, input load, `pokeS`, oracle, verification) with what C++ must offer for each.

**2. The coupling audit**, `sim/tests/AUDIT-M2.md`, in AUDIT-M1's form, for each unit item 1 marks used: state; ports
onto PORTS.md's kinds; every same-cycle coupling (direct calls) with the cycle it takes effect; what within
`Chip.runCycle` / `Tile.runCycle` order matters. Built from WORKER29-DESIGN section 3 and WORKER28-NOTES, re-checked
against the source and against PORTS.md where W3 already corrected W29. Evidence on order: the sort runs identically under
shuffled child order (the kit's shuffle group), so I separate the order the shuffle can't move (request bus first, slot,
`endCycle`, C&C) from what it can. Chip-level work (`RequestBus.runCycle`, overflow scans, `checkInvariants`, READY and
error-line loops) each with the event that should drive it (D23). Ends with **unit groups for parallel workers** and
the order to build and check them (one tile, `regs`/`wfi`, mesh tests, sort 16 K, full).

**3. The references**, `sim/tests/golden-m2/` with `RUNS`, `regen.sh [--check]` (W2's pattern: images against the
manifest, header, body):
- `regs`, `wfi` (single tile, through the router): retire traces of the tiles they run on, D12 headers.
- NetTest and the all-to-all: their logs' figures, interval fingerprints, cycle counts.
- Sort n = 16,384 (k128, uniform, ready 8): cycle count, PhaseLog CSV, per-step CSV, verification, fingerprints every
  K cycles, digest at exit; retire traces of a few tiles if their size allows git, else runner.
- Full sort: the same set, **on the runner** under `~questrunner/data` (or `~/queue/data`), recorded here by sha256.
- The header extends D12 to the chip: every `Config` field, `stage_delay`, shuffle seed, images' and key set's sha256.

**4. Localising a mismatch.** Inventory what ref offers between "fingerprint differs" and "this put at t differs"
(interval digests via `Chip.digest()`, per-tile counters, PhaseLog per phase, the violation and condition logs, the
observer on one tile), and test it by bisecting a deliberately perturbed run (a driver-side poke, not a ref change).
If that's not fine enough to develop units one at a time, **propose** (not build) a put/take trace on a tile's named
buses: the hook sites (`WordDelayLine`/`WordFIFO`/`WordHold` put and take, guarded as D5's), the format, the volume per
tile at 16 K, and the proof cost (Quest 123's quick-suite proof again).

**5. The baselines** (D21): one Quest task on main's `sim/ref`, pinned to one detected P-core with W5's `machine.inc`
(`sim/tools/machine.sh` isn't on main yet), the machine recorded; a timing driver `SortTime.java` calling `SortTest`'s
own pieces, reporting wall time for **setup** (JVM start → chip built, booted, input loaded) and **the run** (the
sequence to finish), and verification separately; three runs, each alone on the box, observer off. The same task (or one
before it) produces the full-size references of item 3, timing runs kept apart from them.

## Order and Quest tasks

1 and 3's local runs first (they feed 2), then 2, then one runner task for the full-size references and the baselines,
then 4, then the report. Probably two Quest numbers, claimed per the sessions README just before each task.

## Questions for the owner

1. **Setup:** is "setup" as in item 5 (JVM start, chip build, boot through C&C, input load), with the boot's simulated
   cycles counted as setup, as the 1,229,687 excludes them?
2. **Per-tile retire traces of the sort:** which tiles? I propose tile 0 and tile 63 at n = 16,384, on the runner with
   hashes, in git only if small.
3. **Interval fingerprints:** every 10,000 cycles unless you prefer another spacing.

## The owner's answers and addition (2026-11-07, with the go-ahead)

1. **Setup** is everything before the sort's first GO: JVM start, chip construction, image and input load. **The run** is
   SortTest's own count (L1's GO to L2's all READY: 1,229,687), and the ~4 s aim is for it. **The boot** through C&C
   gets its own line, in cycles and in seconds.
2. Retire traces: tiles 0 and 63 at 16 K, plus an interior tile if cheap (27). Tiles 0-2 have special routers
   (`Router(..., idx < 3)`): tile 0 stands for that class.
3. Interval fingerprints every 10 K cycles by default, plus a driver option to fingerprint every cycle in a window, for
   bisecting.
4. **The gap: a canonical state fingerprint.** `Chip.digest()` hashes ref's own objects, which C++ can't reproduce. Define
   a fingerprint from state both sides have (per tile and unit: memories and SRAM, architectural registers, the register
   block, what is queued or in flight on each named bus), in a canonical order and encoding; find what `digest()`
   covers; propose it in `AUDIT-M2.md` (contents, how each side computes it, cost per call); **build the ref side**
   (a driver reading ref's state between cycles, no change to `sim/ref`). The C++ side comes with the units.

## Found on the way

- `p4/uniform-k128` reads `KeyGen`'s `uniform.keys` (`KeyGen DIR --n 65536 --only uniform`), not SortTest's built-in
  `kind=uniform`: `SortTest -1 run label=p4/uniform-k128 keys=.../uniform.keys fmt=k128 ready=8` (Quest 118, 120). The
  16 K reference uses `KeyGen --n 16384 --only uniform` the same way.
- The sort image built here (Ubuntu 24.04's riscv64-unknown-elf-gcc 13.2) is byte-identical to Quest 118's
  (`sort.code.bin` 2ab3c746..., `sort.data.bin` 412d9f3f..., `sort.sym` 4a742353...). The image has no `ORACLE`: the
  real split runs.
- Quest 120's 110.1 s is the whole JVM (`date` around `java SortTest`): key file read, chip, boot, input, the run and
  verification together. The split asked for in item 5 is new.

## Progress (2026-11-07)

- Drivers (sim/tests/ref): `StateFp` (the canonical fingerprint, ref's side), `FpHooks` (fingerprints, windows, dumps,
  coverage, cost), `M2Sort` (the sort as SortTest runs it), `M2Prog` (regs, wfi, the C all-to-all as CtrlTest runs them).
  Proofs: ref's digest unchanged with every hook on; fingerprints identical with idle-skip off and under shuffle seed 4.
- `sim/tests/AUDIT-M2.md` (draft): sections 0-9.
- `sim/tests/golden-m2`: images + manifest, RUNS, regen.sh, HASHES; the small runs and the 16K sort regenerated and
  checked (all SAME).
- Quest 134 (claimed and pushed 2026-11-07): the baseline (3 x M2Sort split + SortTest whole JVM, one detected P-core),
  the full-size references into `$DATA/w7-golden-m2/f47b12f`, `regen.sh --check` of the local set. Waiting.
- A lesson: `pkill -f` with a pattern that also matched my own command killed the shell (the prompt's rule); not repeated.

## Next
- Quest 134's results: the baseline table (setup, boot, counted run), the full-size HASHES and files into golden-m2.
- A D12 header in the `.fp` files (the configuration's keys, the images' and keys' sha256), then re-hash.
- `REPORT.md`.

## Closing (2026-11-07)
- Quest 134: the baseline (REPORT section 6) and the full-size references. Its one FAIL was the task's own file count (11
  is right, it expected 13); parked in `tasks/hold/` so the runner would not retry it.
- Quest 135: the full-size references again at fb6f21e with the `.fp` D12 header: 10 files identical to 134's, the
  fingerprints identical; HASHES equal to the local file.
- AUDIT-M2 3.3: the write counters' order-free model, with `pendcheck`'s evidence (0 everywhere).
