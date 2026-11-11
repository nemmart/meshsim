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
