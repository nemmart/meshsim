# WORKER19 report: P1, the stand-alone tree

P1 (V135-INVENTORY rows A1-A13, I1-I3, I5's benches, C26; D42, D43): **nothing v1.35 builds, tests or runs reads
`sim/ref`**, everything that needs Java or `sim/ref` is in `sim/oracle/` (excluded at the freeze), and the Java that stays
is `sim/pipeline/`, self-contained, with only a JDK and v1.35's C++ `convert_delivery` as prerequisites. Branch
`sim-w19`; details and every check's numbers in `NOTES.md`.

## The proof (step 6), at e8fb2ed

A copy of `sim/` with `ref/` and `oracle/` removed, on this session's machine (Ubuntu 24.04, 1 CPU, gcc 13, clang
18.1.3):

| | result |
|---|---|
| `make check`'s TSan runs (gcc, clang: controllers, chip, threads) | pass |
| `tools/check_copies.sh` | 325 files equal to v1.3's |
| gcc debug, gcc release | every test passes (`make check`, one run) |
| clang debug, clang release | 64 of 64 tests pass each (`make check` stopped with the turn after the gcc builds; the two clang builds' `make run` rerun in the foreground, build then every test of `$(TESTS)` in order) |
| `fw/build.sh --check` | 64 images equal to the fixtures' copies, 93 to `fw/images/` |
| `gen_regs check` | mesh.h and REGISTERS.md equal to the generator's output |
| `pipeline/check.sh`, from a copy holding only `pipeline/` and that copy's `build/clang-release/convert_delivery` | all passed: the 335 copies, 47 key and split outputs, the pinned sources, a v1.25 conversion, jumpmap's 11 guests |

Before P1: 15 of 58 tests failed without `ref/` (W18's measurement, reproduced).

## What moved where

- **`data/`**: hops.txt, the vectors, the stream matrices, `tables/` (the replayer's tables, the opcode table, the
  manifest). **`fw/`**: the boot ROM, the RISC-V test images, every firmware source (sw/tests, the controllers, the
  replay controller, the Tracer's firmware, the v1.2 recipes with the book's Python, the sort with its planner, split
  and patches: one copy, C26 gone), the programmer docs (beside `recipes/`, as the recipe sources name them), the M1
  programs in `fw/m1/`. `sim/COPIES.sha256` lists all 325 copies with their `sim/ref` paths.
- **`fw/build.sh [--check|--build-only]`** builds every image as v1.3's six scripts did: `oracle/fw_crosscheck.sh`
  builds v1.3's own in a scratch `sim/ref`: 157 image files byte-identical. The fixture folders' scripts take their
  images from it.
- **`tools/regs/`**: mesh.h and REGISTERS.md from a C++ register table, byte-identical to v1.3's Java generators run
  live (`oracle/regs_crosscheck.sh`); `tests/test_regs` checks the committed files and the table against `regmap.h`.
- **`oracle/`**: `oracle/ref/` (all of `tests/ref/`; W20 moves its three new drivers itself), `oracle/regen/` (the five
  regen scripts), `oracle/conformance.sh`, `oracle/bench/TracerPlay.java`, `oracle/quest/inputs_make_task.sh`, and the
  cross-checks: `fw_crosscheck`, `regs_crosscheck`, `pipeline_crosscheck`, `convert_crosscheck`,
  `synthetic_crosscheck`. Every moved check passes from its new place (golden 67 SAME, fp-m1 67 SAME, g1-replay,
  routerconf, tileslotconf, tracersuites).
- **`pipeline/`**: `keys/` (KeyGen, SplitSets), `gold/`, `jumpmap/`, `inputs/` (with the two pinned gold and jumpmap
  snapshots in place of git archive, classes byte-identical to v1.3's build). `FROM-V1.3.sha256` and the checks
  against v1.3's outputs as above.
- **`tools/delivery/` (D42)**: `convert_delivery` and `synthetic_delivery`, v1.3's ConvertDelivery and SyntheticDelivery
  in C++; `inputs/` calls `convert_delivery` and reads `data/tables`.

## D42: the two delivery tools

- **convert_delivery**: byte-identical to v1.3's ConvertDelivery on three v1.25 deliveries from v1.25's own generator
  (every record, link and manifest) and on `pipeline/inputs/test/v125-small`. Its JSON writer prints doubles as JDK 21's
  `Double.toString` (9,939 values, `tests/test_delivery_json`; Java renders at least two digits: 4.9E-324).
  **On the runner, Quest 175: ALL PASS** (godspeed, kit sim-w19 11e9173, clang 18.1.3, no Java): W9's deliveries, the
  selected windows (evm-zone-29 12 windows, cyclone-29 13) and every window of both programs (8,192 each, 32,768
  records and 57,348 links each, 7 s per program): each converted manifest's sha256 **equal to the one Quest 164
  recorded with v1.3's ConvertDelivery** (selected 0a8c8f74 and c003780b, full 5bffa524 and 2d5e368e; the manifest holds
  every converted record's sha256 and size), every record on disk listed in it, every link's target the source file
  of its name. 14 verdicts, all PASS (`results/175-w19-convert/`).
- **synthetic_delivery**: the cause of v1.3's irreproducibility found: an **unseeded SplittableRandom** gives every
  inline's advice (so every window from the first inline on differs run to run, not only the last), and **Map.of's
  salted order** shuffles the manifest's small maps. D43: K = 0xD42, the advice from SplittableRandom(seed ^ K), the maps
  in source order. Byte-identical to v1.3 patched in exactly those two places on replayconf's four deliveries, WORKER9's
  layout, a shard and the calibration (128 x 65,536 rows, 1,413 files); against v1.3 itself equal with no inlines, and
  with inlines equal up to exactly where two patched runs with different K first differ.

## Findings

1. **The split's driver is inherited dead code** (owner's ruling): `splitdrv.c` doesn't compile against mesh.h 1.2 in
   v1.3, `run_tests.sh` never built it or ran SplitRun. Recorded in V135-INVENTORY's D9 row; the source kept, not built.
2. **SyntheticDelivery's nondeterminism** is broader than W15's "last window" (above; D43).
3. **v1.26.1's ConvertDelivery** wrote its "converted" key order differently from run to run (105's cyclone and 106's
   evm manifests); v1.3's fixed it, and 164's recorded manifests are v1.3's order, equal to 105's evm and 107's cyclone.
4. `test_mach` (W20's, from main) looked for the boot ROM where it isn't without `ref/`: repointed to
   `fw/boot/bootrom.hex` and `data/hops.txt` (two lines in `tests/mach.cpp`).
5. The inventory's `loader.S`/`loader.ld` (A6) don't exist in v1.3: nothing to copy.

## Left for later

- Python still in the tree: the recipe book's three scripts (beside `fw/recipes/`, unchanged; P5 decides) and
  `pipeline/inputs/lib/inputs.py` (v1.3's, unchanged).
- The image folders: the fixtures' and `fw/images/` (owner: P8 consolidates).
- `bench/w10/`'s task makers keep a fallback to `sim/ref` for kits of revisions before P1 (`mk_task.sh` carries
  `sim/data`, `sim/fw` and `pipeline/keys` from P1 on; the sort benches take KeyGen from `pipeline/keys`).
- `pipeline/gold/check.sh` (2^29) needs jolt-asic-planning and the reference files: not run here.
