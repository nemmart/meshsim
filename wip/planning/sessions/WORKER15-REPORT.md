# WORKER15 report: v1.2.1, the v1.2 spec and simulator brought in line

Branch `v1.2.1` (from `main`; rebuilt by the integrator onto `v1.2.1/` when the folders moved, main merged at `135da30`).
Everything is in `v1.2.1/`, except `v1.2/ERRATA.md`. The changelog for Tony's sessions is `v1.2.1/CHANGES-v1.2.1.md`,
with the diff commands at the top (`git diff v1.2-tony:v1.2_wip/spec/machine-spec.md v1.2.1:v1.2.1/spec/machine-spec.md`).

## What changed

**The spec** (`v1.2.1/spec/machine-spec.md` against `v1.2-tony`; every hunk is in the changelog by section):
- **F1–F4**, already in the folder when v1.2.1 began: the load latencies, one K for every destination, the peripheral types,
  K's formula and K implementation-defined.
- **E1–E3, E5, E6, E10, E11** were ported from WORKER10's wip fixes, taking only `machine-spec.md`'s v1.2 hunks, never v1.25's
  replayer changes. They cover 4.17.1's types and 4.19's intro (type 4), 8.1's load rule and 4.12's `EVENTS` load (2 cycles),
  item 111 superseded, item 134's notes in sections 11 and 12, the K row's 4.8, and 4.19.11's duplicated sentence.
- **E7 (CL1)**: the replayer's map in 4.19.10 is one 1 MB part per queue (1 code, 2 read log, 3 write log at 0x300000,
  4 expansion at 0x400000), with a paragraph stating the old layout.
- **E8 (CL2)**: the decode table's 256 × 128 note, in 4.19.10, 4.19.1 and the parameters.
- **E9 (CL5)**: D48 is stated in 4.17.4 (words 8–9 read 0 without a DMA, a load not flagged, a store flagged; word 4 stays
  reserved, as meshsim does) and in 4.11 (`mtime`'s rate implementation-defined).

**`spec/CHANGES-v1.2.md` (E4):** round 4 adds D45–D49.

**The simulator and software (CL1, CL3, CL4):**
- meshsim's replayer write port now decodes [22:20] alone.
- `mesh.h` changed in the generator, both copies and the draft header, which header_diff.py still checks strictly.
- `ReplayDriver` and the unit tests are updated. `replay_ctl.c` and recipe 26 use the macros, so they were only rebuilt.
- StageB now fails a starved window in seconds instead of hanging.
- Section 14's measured figures are in the recipe template, so the book regenerates byte for byte.

**`v1.2/ERRATA.md`:** E1–E11 are marked fixed in v1.2.1, each with its commit, and there is an empty "Found after v1.2.1" section.

## Verification

Local runs, on one core:

| suite | before | after |
|---|---|---|
| ReplayerTest | 1188/0 | 1195/0 |
| RegDocTest | 269/0 | 269/0 |
| StageB synthetic | 18/0 | 18/0 |
| RecipeTest | | 2042/0 |
| check_book | | 46/0 |

A controller built on the old map fails 10 of 18 StageB windows, each in seconds with the "starved" message, where before
CL4 it hung.

**Quest task 088** (`090fc40`) covers the quick suite, the full extras with the sort's BT pair, and p4/uniform-k128 at
64 × 65,536. Its kit is the v1.2.1 tree built at Java 17, plus 086's sortrunner with a rebuilt jar. Only the jar's hash
changes; the sort image, boot ROM and hops.txt are byte-identical to 077/086's. The kit was smoke-tested from a fresh
unpack: 7 quick steps passed, and the sort at n = 2,000 ran in 246,447 cycles.

**Result: passed** (run on the runner, 16 threads, 14 minutes):
- the quick suite, 34 of 34 steps (ReplayerTest 1195/0, StageB synthetic 18/0, RecipeTest 2042/0, RegDocTest 269/0 among
  them);
- the full extras, 10 of 10;
- p4/uniform-k128 at 64 × 65,536, sorted and checked in 1,360,082 cycles, identical to task 086's (the sort doesn't touch
  the replayer, and its inputs are byte-identical).

E1's intro fix, E10 and E11 came after 088 and change spec text only. No code has changed since `090fc40`.

## Ready to merge

Yes: `v1.2.1` is ready to merge. It is merged with main up to `c2de4ae`, with no conflicts left. The integrator's steps
after the merge are tagging `v1.2.1` (the changelog's diff commands name the tag) and pointing Tony at
`v1.2/ERRATA.md` and `v1.2.1/CHANGES-v1.2.1.md`.

## Left over

- CL4 in `wip/`'s acceptance kit Stage B is WORKER11's.
- `gen_book.py` prints "wrote wip/sw/RECIPES.md" when run from `v1.2.1/`. This is cosmetic.
