# WORKER19 notes: P1, the stand-alone tree

Branch `sim-w19`, from main e90facf. Rows A1-A13, I1-I3, I5's `bench/` scripts, C26 (`V135-INVENTORY.md`).

## The plan (agreed with the owner, 2026-11-10)

### Layout, new in `sim/`

| folder | holds | rows |
|---|---|---|
| `data/` | `hops.txt`, `vec-821e1bd.tsv`, the stream matrices (`zipfraw`, `trace`, `one99`), `tables/replayer/`, `opcode_table.txt`, `manifest.json`; `MANIFEST.sha256`, each file's hash and its `ref/` path | A1, A4, A5 |
| `fw/` | `include/mesh.h`; `boot/` (`bootrom.S`, `.ld`, `.hex`); `crt0.S`, `app.ld`, loader; `tests/` (sw/tests' C); `m1/` (today's `fw/*.S` and `.tb`, moved); `cc/`; `replay/`; `tracer/`; `recipes/` (with the recipe book's Python unchanged: `gen_book.py`, `check_book.py`, `regions.py`; P5 decides their fate); `sort/{program,split,skew}` (one copy: no `sw/ext`, so no layout check, C26); `riscv-tests/` (bins, `build.sh`, `link.ld`, `riscv_test.h`; the riscv-tests checkout at 3df703c a documented prerequisite); `docs/` (CONVENTIONS, RECIPES, REGISTERS, the draft header, `regdoc/`) | A2, A3, A6-A10, A13 |
| `fw/build.sh [--check]` | builds every image; `--check` compares with the committed images | A12 |
| `fw/images/` | prebuilt images of the programs that have none committed yet. **The existing fixture folders stay where the tests read them** (`tests/golden/images`, `golden-m2/images`, `m3-images`): owner, Q1; consolidating is P8's | A12 |
| `tools/regs/` | the mesh.h and REGISTERS.md generator, C++, from its own register table; a make-check test that `src/units/regmap.h` agrees with that table (regmap.h itself not rewritten: W20 may be in it) | A11 |
| `pipeline/` | the Java that stays (D40): `gold`, `jumpmap`, `inputs/` (from `v1.3/`), KeyGen, SplitSets, ConvertDelivery, SyntheticDelivery with the meshsim classes they pull in; its own `build.sh` (a JDK only); each tool built and run there | D40, F1-F5 |
| `oracle/` | everything that needs Java or `sim/ref` (D41 Q8): `tests/ref/*` (W20's and W21's new drivers included), every `regen.sh`, `make conformance`, `bench/w14/TracerPlay.java`, and the image cross-check (images built from `sim/ref` by ref's own scripts against `fw/build.sh`'s); excluded at the freeze | I3, I4 |

### Order (one mechanical commit each, saying what moved)

1. Data files into `data/`; every reader repointed (the 15 tests, `t2`, `chain`, the benches, `bench/w10/mk_task.sh`);
   `make check` in a copy with no `ref/`.
2. Firmware sources into `fw/`, `fw/build.sh --check`; `fw/build.sh`'s old job, `golden/build_images.sh`,
   `m3-images/build.sh`, `build_req.sh` repointed to `fw/`.
3. The mesh.h generator.
4. `oracle/`. **Tell the integrator before moving `tests/ref/`** (W20 and W21 add drivers there), so they merge main
   right after.
5. `pipeline/`.
6. The proof: a copy of `sim/` without `ref/` and `oracle/` builds and passes `make check` on gcc and clang;
   `pipeline/` builds alone. Then `REPORT.md`.

### Checking

Locally, with the exact toolchain v1.3's images were built with (Ubuntu 24.04's gcc-riscv64-unknown-elf
13.2.0-11ubuntu1+12), clang 18.1.3, gcc 13, OpenJDK 21: every image, `mesh.h` and REGISTERS.md byte-identical to
v1.3's, every copied file byte-identical to its `ref/` original (sha256).

## Progress

- **Step 1 (3f96e88): the data files.** 159 files copied (`COPIES.sha256`), every reader repointed. W18's
  measurement before: 15 of 58 tests fail without `ref/`.
- **Step 2 (51f9fef): the firmware sources.** `fw/build.sh` reproduces v1.3's build_cc, build_recipes, sw/build,
  replay, tracer and sort builds. `oracle/fw_crosscheck.sh`: 157 image files byte-identical to v1.3's own build of a
  scratch `sim/ref`. Fixture folders' scripts take their images from it (`--check` passes; golden's MANIFEST.md prose
  only). Owner: the A13 docs sit at `fw/` top level beside `recipes/` (the recipe sources name `../RECIPES.md`,
  `../mesh-v1.2-draft.h`), not `fw/docs/`.
- **The split driver**: inherited dead code (owner): doesn't compile in v1.3, never built or run there. Recorded in
  V135-INVENTORY's D9 row; source kept, not built.
- **Proof so far, at 51f9fef in a copy without `ref/`**: clang and gcc, debug and release, all 58 tests pass
  (each build in the foreground; `make run` per build is what `make check` runs).
- `COPIES.sha256` extended to the firmware sources and docs: 323 files, every file under `fw/` but `build.sh`,
  `images/` and `m1/` (W2's own) a checked copy of `sim/ref`.
- No `loader.S`/`loader.ld` exists in v1.3 (the inventory's A6 lists them): nothing to copy.
- **Step 3: the mesh.h generator.** `tools/regs/regtable.h` (the table, from Registers.java, with LinkWord's and
  Conditions' constants), `regs_gen.h` (MeshHeader and RegDoc ported; their fixed text taken from the Java text
  blocks by a one-off script, escapes resolved and trailing spaces stripped as Java does), `gen_regs` (write or check).
  Byte-identical on the first build to the committed files and to v1.3's generators run live
  (`oracle/regs_crosscheck.sh`). `tests/test_regs` in make check: the two files are the generator's output, and the
  table agrees with `src/units/regmap.h` word by word (load and store sides, names, reserved words), plus the address
  map, event, status, error and PARAMS bits; a one-byte change to mesh.h fails it.
- **Step 4, waiting for the owner's word before moving `tests/ref/`.** The sort (rule: needs Java or `sim/ref` to
  run): `tests/ref/` whole (35 .java, 19 scripts, `tileslotconf_progs.py`) -> `oracle/ref/` (same depth, so each
  script's `SIM=../..` holds; `tests/ref/` paths rewritten); the five `regen.sh` (golden, golden-m2, golden-m3, fp-m1,
  g1-replay) -> `oracle/regen/<fixture>.sh`, pointing at their fixture folder; the Makefile's `conformance` target ->
  `oracle/conformance.sh`; `bench/w14/TracerPlay.java` -> `oracle/bench/`. Stay (committed data make check reads):
  every fixture, `tests/tracersuites/`, `tests/tracerconf/cases.log.gz`, `m3-images/*.ref*`. Comments naming
  `tests/ref` (the .mk files, AUDIT-*, PORTS.md, READMEs) repointed; planning records left as written.
- **Step 5, a question:** the four input tools' compile closure in v1.3's Java. KeyGen and SplitSets stand alone;
  ConvertDelivery and SyntheticDelivery need ReplayDelivery, ReplayDriver, ReplayTables, ReplayWindow and the Java
  `Replayer` (SyntheticDelivery instantiates it for its reference rows), and `Replayer` reaches `Bus` -> `Chip`, so
  javac needs 49 of the simulator's 52 source files (57 files, 10.8 K lines; found by iterating javac to success).
- **Step 5, part: `pipeline/keys/`** (owner: KeyGen and SplitSets now, the rest after the owner's decision on
  ConvertDelivery and SyntheticDelivery). Copied unchanged (COPIES.sha256); `pipeline/build.sh` (a JDK only),
  `pipeline/check.sh`: 47 outputs (uniform at 16,384 and 65,536, the hashes golden-m2/HASHES records for the sort's
  reference runs; every key and split set at 2,000) equal to v1.3's, run from a copy holding only `pipeline/`;
  `oracle/pipeline_crosscheck.sh` reproduces every expected hash with sim/ref's tools.
- **Step 4 prepared, not run** (waiting for the owner's word that W20 has pushed): `WORKER19/step4.sh` does the whole
  move mechanically (run from the repo top). Dry run on a scratch clone: 33 renames, 99 files repointed, no
  `tests/ref` left outside `ref/` and `planning/`; from their new places `oracle/regen/fp-m1.sh --check` (67 SAME),
  `oracle/regen/g1-replay.sh --check` (SAME) and `oracle/ref/routerconf.sh --check` pass.
- **Step 5, part 2: `pipeline/gold`, `pipeline/jumpmap`** (owner: they stay Java whatever the replayer decision), from
  `v1.3/` (siblings, as there, so their relative paths hold), listed in `pipeline/FROM-V1.3.sha256` (321 files). One
  edit: `gold/check.sh`'s hard-cut lists, copied into `gold/test/cuts/` from `v1.3/planning/sessions/WORKER16-data/w3`.
  From a copy holding only `pipeline/` (`pipeline/check.sh`, 20 s): gold's own tests (inline emit digests, 5,677 inline
  vectors agree), jumpmap's fixtures for 11 guests (gold's segment files and every jumpmap output) regenerated byte for
  byte equal to v1.3's committed `out/`, the keys' 47 outputs. `gold/check.sh` (2^29) needs jolt-asic-planning and the
  reference files: not run here.
- **`inputs/`, a question for the owner:** v1.3's `inputs/` doesn't build from v1.3's gold: `lib/classes.sh` builds gold
  and jumpmap by `git archive` of the meshsim repository at two pins (`GOLD_SRC` d97ecde: gold differs from v1.3's in 10
  files, jumpmap in 3; `GOLD_V13_SRC` eeb64b5: gold differs from v1.3's in 1 file), plus v1.26.1's ConvertDelivery and
  its tables (v1.26.1's tables equal `sim/data/tables`). Stand-alone, it needs those pinned snapshots committed into
  `pipeline/` (and ConvertDelivery, which waits).
- **Step 5, part 3: `pipeline/inputs/`** (owner: commit the pinned snapshots). `inputs/src/d97ecde/{gold,jumpmap}/src`
  and `inputs/src/eeb64b5/gold/src` (71 files) are `git archive` of `wip/` at `GOLD_SRC` and `GOLD_V13_SRC`, recorded
  in `src/SNAPSHOTS.sha256` (each with its commit:path) and `src/SNAPSHOTS.sh` (the pins' last commits: gold 7ef9039,
  jumpmap 4788e0e, gold v1.3 eeb64b5). Edited: `lib/classes.sh` compiles from `src/` instead of git archive, and
  writes VERSION from SNAPSHOTS.sh; `build.sh`'s default GOLD_COMMIT is SNAPSHOTS.sh's (it was `git log` at the pin,
  the same 7ef9039). Checked: the classes (gold, jumpmap, gold v1.3, ConvertDelivery's meshsim, VERSION) byte for
  byte equal to v1.3's classes.sh (`oracle/pipeline_crosscheck.sh`); `pipeline/check.sh` from a pipeline-only copy
  checks the snapshots' hashes and builds them. Still reading the repository until the owner decides:
  `v1.26.1/meshsim` (ConvertDelivery, `MESHSIM_REL`) and `v1.26.1/tables` (`TABLES_REL`; equal to `sim/data/tables`);
  `quest/make_task.sh` (a Quest kit maker, reads `wip/planning/...`) not touched.
- **Step 4 done (7717eb3)**, `step4.sh` run on main as merged (W20's three new drivers are on sim-w20 only: W20 moves
  them itself). 33 renames, 99 files repointed. From their new places, with sim/ref: `oracle/regen/golden.sh --check`
  (67 traces SAME), `fp-m1.sh --check` (67 SAME), `g1-replay.sh --check` (SAME), `oracle/ref/routerconf.sh`,
  `tileslotconf.sh`, `tracersuites.sh --check` (equal ref's regeneration), `oracle/regs_crosscheck.sh`. A copy without
  `ref/` and `oracle/`: clang release builds, 63 tests; `test_mach` (W20's, merged from main) failed there, looking for
  the boot ROM at `data/bootrom.hex`, `fw/bootrom.hex` or `ref/`: repointed to `fw/boot/bootrom.hex` and `data/hops.txt`
  (1e2a363), and then all 63 pass.
- `pipeline/inputs/quest/make_task.sh` -> `oracle/quest/inputs_make_task.sh` (owner: Quest task infrastructure; inputs/
  doesn't use it), its HERE and MESHSIM repointed; `pipeline/check.sh` still passes from a pipeline-only copy.

## D42: ConvertDelivery and SyntheticDelivery in C++ (owner, 2026-11-11)

**SyntheticDelivery's nondeterminism, found** (sim/ref, JDK 21, seed 7, 3 windows of 2,048 rows, three runs):
1. `final SplittableRandom rnd = new SplittableRandom();` (line 274) is **unseeded**: `Func.advice()` returns
   `rnd.nextLong()` for every advice word that isn't a division's, i.e. the inlines'. That value goes into the read log
   and the registers, so **every window from the first inline's advice on differs run to run**, not only the last:
   with seed 7 window 0 already differs (rlog, wlog, rows, end.bin; state records from window 1). With
   `--inline-weight 0` (no inlines) every window file is identical run to run.
2. `Map.of(...)` (lines 440, 445, 460: each file's {bytes, sha256}, the counts) iterates in an order salted per JVM
   run, so `manifest.json`'s key order differs run to run even when every file is identical.
W15's "the last window" was a particular case (deliveries whose inlines fell late).

**The plan for the C++ (`tools/delivery/`, built by make, both in make check's builds):**
- `synthetic_delivery`: SyntheticDelivery's options and files; SplittableRandom ported exactly; the inlines' advice from
  a seeded generator (the deliberate difference: `new SplittableRandom(seed ^ K)`, K fixed and recorded), the
  manifest's maps in the source's order (bytes, sha256; instructions, read_log_words, write_log_words, padding_rows).
  Checked (oracle side): (a) against a scratch copy of sim/ref's SyntheticDelivery patched only in those two places
  (sed), byte for byte on every file of every window, for every seed ref's tests use (replayconf's syn1-syn4, the
  calibration's seed 11 at 128 windows, StageB synthetic's); (b) against the unpatched Java: every file equal with
  `--inline-weight 0`, and up to the first inline otherwise; the manifest equal as JSON (key order aside).
- `convert_delivery`: v1.3's ConvertDelivery (its fixed "converted" key order), the JSON reader and writer as Java's
  (LinkedHashMap order, Long and Double values printed as Java prints them). Checked: on v1.25 deliveries made locally
  by v1.25's own SyntheticDelivery, against v1.3's ConvertDelivery (every record, every link, the manifest); then on the
  runner, the conversions Quests 105-107 and W15's Stage A used (records byte for byte; their manifests were written
  by v1.26.1's, whose "converted" key order varied run to run: compared on the records and as JSON).
- `pipeline/inputs/` then calls `convert_delivery` (a path to the C++ tool) and reads `sim/data/tables`; `pipeline/`
  holds only self-contained Java.
- **convert_delivery, done locally** (`tools/delivery/convert_delivery.cpp`, `json.h`: Java's reader and writer,
  LinkedHashMap order, Long, Double printed as Double.toString; no exceptions, D4: a window's error is returned). Built
  by make in every build (`tools/delivery/delivery.mk`). `oracle/convert_crosscheck.sh`: on three v1.25 deliveries made
  by v1.25's own SyntheticDelivery (seeds 7, 11 in WORKER9's layout, 5; 39 records, 67 links), every record, every link
  (name and target) and the manifest byte for byte equal to v1.3's ConvertDelivery, on the first build. Still to do:
  the runner's real conversions (Quests 105-107, W15's Stage A), then synthetic_delivery.
- **Java's decimal printing checked** (`tests/test_delivery_json`, make check): 9,939 doubles (edges, every power of
  ten and its neighbours, random bits, random short decimals) through `json::java_double` equal JDK 21's
  `Double.toString` (fixture `tests/delivery/java_doubles.txt` from `oracle/ref/JavaDoubles.java`). One fix: Java renders
  at least two digits (4.9E-324, not 5.0E-324).
- **Quest 175 (pushed, `tasks/175-w19-convert.sh`, copy in WORKER19/tasks/):** convert_delivery on W9's deliveries on
  the runner. The references are the converted manifests' sha256 that 164 recorded with sim/ref's ConvertDelivery
  (selected: evm 0a8c8f74, cyclone c003780b; full: evm 5bffa524, cyclone 2d5e368e), equal to 105's evm and 107's cyclone
  (v1.26.1's, in the order that happened to come out; 106's evm and 105's cyclone came out in the other order). The
  manifest holds every converted record's sha256 and size, so its hash is every record's bytes; the task also checks the
  records on disk against it, and the links' targets and counts (57,348 per full program, as 164's logs).
- **synthetic_delivery, done** (`tools/delivery/synthetic_delivery.cpp`: SplittableRandom ported exactly (JDK 17-21),
  the functional replay, the RV64 reference, the generator with every random draw in Java's left-to-right order, W15's
  `ReplayWindow` for the streams). D42's difference: K = 0xD42, the inlines' advice from SplittableRandom(seed ^ K); the
  manifest's maps in source order (header and DECISIONS). `oracle/synthetic_crosscheck.sh`, on the first build:
  (1) v1.3 patched in exactly those two places (sed, checked to apply exactly): every file and the manifest byte for
  byte on replayconf's syn1-syn4, WORKER9's layout and a shard (`--first 3`); (2) v1.3 itself: every file equal with
  `--inline-weight 0` and the manifest equal as JSON; with inlines, the first difference is exactly where two patched
  runs with K and K' first differ (syn1: window 0, rows.bin byte 15,337; syn2: window 3; syn3: window 2; syn4: window 0).
  The calibration (seed 11, 128 windows of 65,536 rows) with `--calibration`: all 1,413 files byte for byte. Stage B's synthetic windows aren't
  SyntheticDelivery's (StageB builds them with ReplayWindow and ReplayerUnitTests.programWindow): nothing to check here.
- **inputs/ on convert_delivery and data/tables** (D42): `pins.sh` drops TABLES_REL and MESHSIM_REL; `build.sh` takes
  TABLES (default v1.35's `data/tables/replayer`) and CONVERT_DELIVERY (default `../../build/clang-release/`), and
  checks both; `lib/classes.sh` builds no meshsim/ classes. `pipeline/check.sh` needs the C++ `convert_delivery` and
  converts a small committed v1.25 delivery (`inputs/test/v125-small`, from v1.25's generator) to v1.3's manifest
  sha256 (b9e8ec4b...); from a copy holding only `pipeline/` and `build/clang-release/convert_delivery`: all passed.
  Nothing in `pipeline/` reads the repository but comments.
- **Step 6, the proof (e8fb2ed, a copy without `ref/` and `oracle/`)**: `make -j2 check` passed the TSan runs,
  check_copies (325), gcc debug and gcc release; it stopped with the turn in clang debug's build. Rerun in the foreground:
  clang debug and clang release, build then every test of `$(TESTS)` (64 each) in order: all pass. In the same copy
  `fw/build.sh --check` and `gen_regs check` pass; `pipeline/check.sh` from a copy holding only `pipeline/` and the
  proof's `convert_delivery`: all passed. The sort benches (`bench/w10/sort64k*-body.sh`) take KeyGen from
  `pipeline/keys` (fallback for older kits), and `mk_task.sh` carries `sim/pipeline/keys`. REPORT.md written; Quest 175
  pending (the runner idle).
- **Quest 175: ALL PASS** (results 2026-11-12, godspeed): the four converted manifests equal 164's (v1.3's), every
  record and link checked; 14 verdicts. REPORT.md updated. P1 closed.
