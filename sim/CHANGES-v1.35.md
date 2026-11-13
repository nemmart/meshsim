# What changes in v1.35

*For the RTL engineer and for v1.3's users: what v1.35 changes, and for each way of running v1.3, what to run now.*

## For the RTL

**Nothing changes.** v1.35's hardware is v1.3's: every normative statement, register, number and parameter of the
spec stands. What changed in the spec's text (it was restructured, and its statements about the simulator made true
of v1.35), and where the simulator differs from v1.3's on purpose, are `spec/CHANGES-v1.3-to-v1.35.md`.

## For users

v1.35's meshsim is v1.3's Java simulator rewritten in C++; it builds with `make` (`BUILDING.md`) and its programs are
in `build/<cc>-<mode>/`. The offline input tools stay Java, in `pipeline/` (D40).

| v1.3 | v1.35 |
|---|---|
| `meshsim/run_tests.sh [all\|quick]` | `./run_tests.sh [all\|quick]`: the same steps, step names and logs; `--check` compares each log with its reference (`RUNNING.md`) |
| `SortTest p1`, `sort`, `run …` (with `cfg.<field>=value`) | `sort_test`, the same modes and arguments |
| `SortCompare` | `sort_compare NEW_DIR REF_DIR [out.md]` |
| `tools/logdiff.py` | dropped: it mapped one rename between two versions' logs (M10 to M11), not a generic tool; comparing two logs with their timings masked is `./run_tests.sh --check` |
| `Sweep quick\|full` | `sweep quick\|full` |
| `TracerT2` | `t2` |
| `TracerT1` | `chain`, every segment of both workloads on the full chip (D41) |
| `StageB` | `stageb` |
| the acceptance kit (`ReplayShard`, `acceptance/*.sh`) | `replay_shard`, `shard_summary` and `tests/acceptance/` |
| `ConvertDelivery`, `SyntheticDelivery` | `convert_delivery`, `synthetic_delivery` (D42; below) |
| `Registers`, `MeshHeader`, `RegDoc` (writing `mesh.h`, `REGISTERS.md`) | `gen_regs` (`tools/regs/`) |
| the firmware's build scripts (`sw/build.sh` and the others) | `fw/build.sh` |
| `gold`, jumpmap, `KeyGen`, `SplitSets`, `inputs/` | unchanged, in Java: `pipeline/` |
| `-Dmeshsim.digest=FILE` (`Chip.digest`) | the canonical state fingerprint (D41) |
| `-Dmeshsim.skip` (idle-skip) | always on: the simulator is event-driven; the debug option `step_all` steps every unit |
| `-Dmeshsim.observe` (the retire trace) | `log_trace` and the retire trace (`tests/TRACE-FORMAT.md`) |

**`Config`'s fields** are `MachConfig`'s (`tests/mach.h`), set as `field=value` where a driver takes them
(`src/units/README.md` lists them against the spec). Only the fields some suite, kit, sweep or documented option uses
were carried (D41); these were dropped:

| v1.3 field | why |
|---|---|
| `idleSkip` | v1.3's own speed option; v1.35 is event-driven |
| `startSkew` | set from `skewSeed`; no driver sets it (`skew`, `skew_seed` do) |
| `receiveQueueDepth` | an array no driver, kit or sweep sets (the request bus's default, 128, 256, 128) |
| `periphTypes` | an array no driver sets (`test_periph_tile` instead) |
| `testPeripheral` | a Java function: `test_periph_tile`, the replayer on one tile |
| `hops` | the hop table: `data/hops.txt` |
| `errorLatency` | set by no v1.3 driver other than as `readyLatency`; the simulator uses `ready_latency` for both lines |

`MachConfig::dropped()` gives the same list, and a driver refuses a dropped field with its reason.

**What behaves differently:**
- **`synthetic_delivery` is reproducible** (D43). v1.3's SyntheticDelivery drew the inlines' advice unseeded and wrote
  its manifest's small maps in a varying order, so its output differed run to run. v1.35's seeds the advice from the
  seed and writes the maps in source order; up to the first inline's advice, and in full with no inlines, it is
  byte-identical to v1.3's.
- **`ready_latency` below 3 is refused** (D47): the chip builder stops with a message naming the field. The kit's
  sweep point `ready-1` therefore ends with no result, listed with the other refused points.
- **Stage B's must-fail window** (D46): the suite runner no longer counts Stage B's deliberate "(CL4: must fail)" line
  as a failure.
