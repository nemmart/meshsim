# meshsim v1.35

*For anyone arriving here: what v1.35 is, where to start for what you want to do, and where everything is.*

**v1.35 is v1.3's mesh sorter, unchanged in hardware, on a new simulator.** The machine is a 64-tile mesh processor
specified in `spec/`; meshsim is its cycle-level simulator (the name stays whatever the implementation, D37). v1.35's
meshsim is written in C++ (`src/`) and reproduces v1.3's Java simulator: the same results, and the same cycle counts
except for a few deliberate differences, each listed in `spec/CHANGES-v1.3-to-v1.35.md`. Nothing that runs the
simulator uses Java; only the offline tools that make its inputs stay in Java (`pipeline/`, D40). The folder is
stand-alone: it builds and runs everything from its own sources (D39).

## Where to start

| you are | you want | start at |
|---|---|---|
| the RTL engineer | what to build, and what to compare the RTL against | `spec/README.md`, then `CHANGES-v1.35.md` |
| a firmware author | to write or change a unit's program, build it and run it on a tile | `fw/README.md` |
| the proof-pipeline user | to make inputs and run a replay, a segment or a proof | `BUILDING.md`, `RUNNING.md`, `pipeline/README.md` |
| someone adding a unit | to model a new unit or peripheral | `src/README.md` |
| an integrator taking over | where the project stands and why | this page, then `planning/` |
| the owner | to know it is right without reading it all | `spec/OVERVIEW.md` |

## Layout

| folder | what |
|---|---|
| `spec/` | the machine specification: the platform, the Jolt-specific units, the schedule, and their history |
| `src/` | the simulator: the framework (`src/`, `src/base/`) and the model of the chip (`src/units/`) |
| `tests/` | the tests and suites, and the fixtures that tie them to v1.3 |
| `fw/` | the tile and controller programs, their committed images, and the firmware docs |
| `data/` | the input data sets and tables |
| `pipeline/` | the offline input tools (Java) |
| `tools/` | helper scripts and tools: the register-table generator, the delivery converters |
| `bench/` | the simulator's speed benchmarks |
| `planning/` | history: the decisions, plans and session reports |

`Makefile` and `run_tests.sh` are the build and the suite runner (`BUILDING.md`, `RUNNING.md`).

## Status

v1.35 is frozen. Its freeze check (`tests/freeze/`) verifies `./run_tests.sh all --check`, the full-size sort (one
thread and 6 domains), T2 on both workloads, segments 0–127 of both workloads as chains, and the acceptance kit's
calibration. The checks the spec promises that neither v1.3's simulator nor v1.35's has are listed in
`spec/CHANGES-v1.3-to-v1.35.md`; what is open in the design is `spec/machine-spec.md` section 13.

## Glossary

| term | meaning |
|---|---|
| meshsim | the simulator, whatever its implementation (D37) |
| `ref` | the reference v1.35 was checked against: v1.3's Java simulator with retire-trace observer hooks added (`Observer.java`, and one guarded line at each call site in `Controller.java`, `ControllerMemories.java` and `TileControl.java`; every other file identical to v1.3's). It is kept outside the frozen copy; v1.3's own simulator is `v1.3/meshsim/` |
| D*n* | decision *n* of v1.35, in `planning/DECISIONS.md`; v1.3's are in `spec/history/` |
| Quest *n* | task *n* on the project's measurement machine, where the long runs and timings were made |
| `[meas: …]` | in the spec, a figure measured on a simulator (`spec/machine-spec.md`'s preamble) |
| the RTL | the hardware design the spec is for |
| M1, M2, M3 | the stages the simulator was built in: M1 the framework and one RV32 controller, matching `ref` instruction for instruction; M2 the sort, multithreaded; M3 v1.3 as it runs, with the Tracer and the replayers. Fixture folders are named after them |
