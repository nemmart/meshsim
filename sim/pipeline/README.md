# pipeline/: the offline input tools (Java)

What makes the simulator's inputs, kept in Java (DECISIONS D40-D41): self-contained, with its own build, **a JDK its
only prerequisite** (Java 21; `jumpmap/test/make_jump_guest.py` also uses python3), and nothing in the simulator's
build, tests or runs reads it.

| folder | what | from |
|---|---|---|
| `keys/` | `KeyGen` (the sort's key sets, bit-exact with numpy 2.x) and `SplitSets` (the split's 25 data sets) | v1.3's `meshsim/test/`, unchanged |
| `gold/` | gold, Jolt's tracer in Java: the segment files (its README) | `v1.3/gold`, unchanged but `check.sh`'s path to the hard-cut lists (now `test/cuts/`) |
| `jumpmap/` | the jump-target table, frame 6, the initial image, each segment's pages (its README) | `v1.3/jumpmap`, unchanged |
| `inputs/` | the pinned build of every simulation input (its README): jolt-cpp's tracer and the delivery writer, gold, jumpmap, ConvertDelivery | `v1.3/inputs`; `lib/classes.sh` and `build.sh` take gold and jumpmap from `src/`, the two pinned snapshots (`src/SNAPSHOTS.sh`), not git archive |

`sh build.sh` builds everything (gold's own tests included); `sh check.sh` builds and checks the outputs against
v1.3's: the copies (`FROM-V1.3.sha256`), the key sets (`keys/EXPECTED.sha256`), and jumpmap's fixtures regenerated
(gold's segment files and jumpmap's outputs for 11 guests) against the committed `jumpmap/out/`. `gold/check.sh` checks
the 2^29 segment files against the reference files, where jolt-asic-planning and those files are.
Still to come (WORKER19, the owner deciding): ConvertDelivery and SyntheticDelivery. Until then `inputs/` still reads
the repository's `v1.26.1/meshsim` (ConvertDelivery) and `v1.26.1/tables` (equal to `sim/data/tables`).
