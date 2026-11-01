# v1.2 → v1.2.1: what changed since Tony's copy

**Tony's copy** is the v1.2 spec as he took it: tag `v1.2-tony`, folder `v1.2_wip/` (also kept as `v1.2/` on `main`).
**v1.2.1** is this folder: the same spec with the fixes made while the v1.2 simulator was built (F1–F4), the errata found
since (E1–E9) and two cleanups that change the spec (CL1, CL2), with meshsim and the software kept in line. The errata
themselves, as an answer to "what's wrong in my copy", are in `v1.2/ERRATA.md`.

## The diff

The paths differ between the two tags (`v1.2_wip/spec/` in `v1.2-tony`, `v1.2.1/spec/` in `v1.2.1`):

    git diff v1.2-tony:v1.2_wip/spec/machine-spec.md v1.2.1:v1.2.1/spec/machine-spec.md
    git diff v1.2-tony:v1.2_wip/spec/CHANGES-v1.2.md v1.2.1:v1.2.1/spec/CHANGES-v1.2.md
    git diff v1.2-tony:v1.2_wip/spec/ v1.2.1:v1.2.1/spec/        # the whole spec folder: only those two files change

Before the integrator tags `v1.2.1`, use `origin/v1.2.1` in its place. `replayer-opcodes.md` and `schedule.md` are unchanged.

## Spec changes, by section

**Affects the RTL:** *yes*, the hardware must change; *note only*, the text is corrected or completed but the hardware
specified elsewhere in Tony's copy is unchanged; *no*, a reference or the changelog.

| section | affects the RTL | source | the change |
|---|---|---|---|
| 4 (the tile's units) | yes | F3 | the replayer is listed as peripheral type 4, not 1 |
| 4.8 the traffic director, K | yes | F4 (D49, W4-08) | meshsim's big-SRAM write path is 2L + 4, so meshsim's K ≥ max(2L + 4, L + 4, `cm_latency`, `rp_latency`) (was 2L + 3); **K in hardware is implementation-defined**: it need only cover the implementation's slowest write path |
| 4.8 the traffic director, K | no (note) | F2 | one K for every destination, deliberately; the read-port search cost measured |
| 4.8 the traffic director, the tables | yes | F1 (D45) | of the table memories only `HIST` is loadable through the table window |
| 4.11 the controller, CSRs | note only | E9 (D48, CL5) | `mtime`'s rate is implementation-defined; software bounds its waits with `mcycle` |
| 4.17.1 the peripherals and their types | yes | E1 (D46) | the types listed: 0 none, 1 Ingress/Egress, 2 Updater/Binder/RW, 3 Tracer, **4 the replayer**; none open |
| 4.17.4 the standard window | yes | F3 (D46) | the peripheral-type paragraph (`PARAMS` [7:0]) |
| 4.17.4 the standard window | note only | E9 (D48, CL5) | a peripheral without a DMA reads words 8–9 (`DMA_HDR`, `DMA_ADDR`) as 0, a load not flagged, a store flagged; word 4 stays reserved. In hardware these read 0 like any reserved word: the change is meshsim's flag |
| 4.19 the replayer | yes | F3 | heading: peripheral type 4 |
| 4.19.1 overview, the memories | note only | E8 (CL2) | decode table "built as 256 × 128" |
| 4.19.9 the words, v1.2 against v1.1 | yes | F3 (`76c02f6`) | `PARAMS` [7:0] = type 4 |
| 4.19.10 the replayer's map | **yes** | E7 (CL1) | **one 1 MB part per queue: 1 code (0x100000), 2 read log (0x200000), 3 write log (0x300000, was 0x280000), 4 expansion (0x400000, was 0x300000), 5–7 unmapped**; offset bits [22:20] alone name the destination (no bit-19 split) |
| 4.19.10 the replayer's map | note only | E8 (CL2) | implementation note: the decode table built as 256 × 128 bits, one row per packet, a lookup selecting 16 bits by *e* & 7 |
| 5 timing summary, write counters | yes | F4 | K implementation-defined; meshsim's formula 2L + 4 |
| 5 timing summary, controller | yes | F1 (D45) | loads from the register block and the peripheral windows 2 cycles, from the table window 1 (was "1 or 2, fixed per register") |
| 8.1 general rules, timing | note only | E2 (D45) | the general rule restated as 4.11 and F1 have it (2 cycles block and windows, 1 table window and memories) |
| 8.11 the replayer's window | yes | F3 | `PARAMS` [7:0] = 4, reset 4 |
| 9 invariant 24 | yes | F4 | K covers the implementation's slowest write path (meshsim: the 2L + 4 formula) |
| 10 parameters, `reg_load_latency` and load-use | yes | F1 (D45) | 2 for the block and windows, 1 for the table window |
| 10 parameters, K | yes / no | F4; E6 | K implementation-defined, meshsim's 2L + 4 (F4); its section reference 4.7 → 4.8 (E6) |
| 10 parameters, the replayer | yes / note | F3; E8 | type 4; decode table "built as 256 × 128" |
| 11 logic budget, controller | note only | E5 (D45) | the 32 KB memories' one-cycle access at 1 GHz confirmed by Tony; item 134 closed |
| 12 the control island | note only | E5 (D45) | the same |
| 13 item 23 | no | F3 | the replayer is type 4 |
| 13 item 111 | note only | E3 (D45) | "1 or 2 cycles" superseded by D45 |
| `CHANGES-v1.2.md` | no | E4 | round 4: decisions 45–49 |

## Simulator and software (not part of the spec diff)

| where | affects the RTL | source | the change |
|---|---|---|---|
| `meshsim/src/Replayer.java` | no (model) | CL1 | the write port decodes the part from offset [22:20] alone: parts 1–4 the code, read-log, write-log and expansion queues, 5–7 (and any offset from 0x800000) unmapped |
| `sw/mesh.h`, `meshsim/sw/mesh.h`, `meshsim/src/MeshHeader.java`, `sw/mesh-v1.2-draft.h` | note: **any RTL testbench or firmware with the queue addresses** | CL1 | `RP_Q_WLOG` = region + 0x300000, `RP_Q_EXP` = region + 0x400000 (`RP_Q_CODE`, `RP_Q_RLOG` unchanged); the draft header kept identical (header_diff.py strict) |
| `meshsim/sw/replay/replay_ctl.c`, recipe 26 (`sw/recipes/rcp_replay.c`) | no | CL1 | source unchanged (they use the macros); rebuilt |
| `meshsim/test/ReplayDriver.java`, `ReplayerUnitTests.java` | no | CL1 | the Java driver's word addresses; part 5 unmapped, and one write per part (ReplayerTest 1188 → 1195 checks) |
| `meshsim/test/StageB.java` | no | CL4 | a window whose running pass delivers no row for 50,000 cycles fails at once ("starved", with the queues' occupancy) instead of running to the 2M-cycle cap |
| `sw/recipes/RECIPES.tmpl.md` | no | CL3 | section 14's measured figures (and the two "Cycles" lines) moved into the template, so `gen_book.py` reproduces `RECIPES.md` byte for byte |
| `spec/CHANGES-v1.2.md` | no | E4 | as above |

## Verification

Quest task 088 (branch `v1.2.1` at `090fc40`): run_tests.sh's quick suite, its full extras with the sort's BT pair, and the
full-size sort p4/uniform-k128 at 64 × 65,536. Results in `WORKER15/REPORT.md`.
