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
