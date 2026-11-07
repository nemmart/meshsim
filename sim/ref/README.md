# sim/ref: the reference (frozen)

v1.3's Java simulator, copied from `v1.3/` with the siblings it reads (`sw/`, `sort/`, `tables/`, `gold/`): the exactness
oracle for the new framework (`sim/README.md`). **It is frozen**: it builds and runs as it did in `v1.3/`
(`meshsim/run_tests.sh`), and nothing here changes, with the one exception below (sim/planning/DECISIONS.md D5).
Development of the Java simulator carries on in `wip/meshsim`, not here.

## The one change: the retire-trace observer (WORKER2, 2026-11-07)

`meshsim/src/Observer.java`, and seven call sites, each one line guarded by `if (Observer.ON)`:

| where | what it reports |
|---|---|
| `Controller.execute`, at its end | an instruction completed (pc, word, operands as read, rd's value) |
| `Controller.trap` | a trap taken |
| `Controller.haltWith` | the core stopped |
| `Controller.runCycle` | a WFI that slept is left |
| `Controller.execute`, the `REQUEST_SEND` stall | a request refused by the full request FIFO |
| `ControllerMemories.runCycle` | a port-B write into the controller's memories |
| `TileControl.endCycle` | GO and a command landing, and the request FIFO's head freed by the bus |

The hooks only read. They write one tile's trace in the format of `sim/tests/TRACE-FORMAT.md`.

**Switching it on**: the system property `meshsim.observe`.
- `-Dmeshsim.observe=FILE,TILE` traces tile TILE's controller into FILE from the start of the JVM.
- `-Dmeshsim.observe=on` turns the hooks on with no output until a driver calls `Observer.start(writer, tile)`, as
  `sim/tests/ref/TraceRun.java` does (one program on one tile; `sim/tests/golden/regen.sh` runs it for every golden trace).
- Absent (the default), the observer is off.

**Off, it changes nothing.** `Observer.ON` is a `static final boolean` read from the property when the class loads. It is not
a compile-time constant, so javac keeps the guarded calls; with it false the JIT removes them. The claim rests on the
measured proof, not on that mechanism:
- `RiscvTest` and `CtrlTest` (seeds -1 and 4): logs and every chip's fingerprint (`-Dmeshsim.digest`) identical to the
  pristine copy's, with the observer off and on (WORKER2, locally);
- the quick suite (`run_tests.sh quick`'s 39 Java steps, `sim/tests/ref/prove_observer.sh`): masked logs, exit codes and
  fingerprints against the pristine copy, off and on: Quest 123 (`sim/planning/sessions/WORKER2/REPORT.md` has the verdict).

With it on, only the trace file is new: the logs and fingerprints stay identical.
