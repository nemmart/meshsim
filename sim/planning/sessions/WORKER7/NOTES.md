# WORKER7 notes: the plan (proposed 2026-11-07, waiting for the go-ahead)

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
