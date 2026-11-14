# meshsim v1.36

*For anyone arriving here: what v1.36 is, where to start for what you want to do, and where everything is.*

> **v1.36 is in progress.** Its specification (`spec/`) is written: it describes the hardware the RTL (Snarky One)
> implements, in the RTL's terms. **The simulator (`src/`), the firmware (`fw/`), the tests and the other documents
> of this folder are still v1.35's**, copied unchanged, and are being brought to the specification in the stages
> listed under Status. Until a stage is done, what it covers runs and reads as v1.35.

**v1.36 is the mesh processor as the RTL builds it.** The machine is a 64-tile mesh processor specified in `spec/`;
meshsim is its cycle-level simulator. The RTL was written against v1.3 and v1.35, and building it decided what they
left open and changed some of what they decided. v1.36 ports those changes back: its specification is the RTL's
(the platform and the core, with their names, register layouts and vocabulary) with what meshsim's specification
has beyond it kept in the same terms, and its simulator and firmware are to follow it, reproducing the RTL's
results and its cycle counts. `spec/CHANGES-v1.35-to-v1.36.md` is the guide: what to change, the timing table, the
name table from v1.35's terms, and three reversals of recorded rulings that wait for the owner.

## Where to start

| you are | you want | start at |
|---|---|---|
| the RTL engineer | what the specification says of the RTL, and what waits for a ruling | `spec/README.md`, then `spec/CHANGES-v1.35-to-v1.36.md` |
| the simulator or firmware author bringing v1.36 up | what to change, stage by stage | `spec/CHANGES-v1.35-to-v1.36.md` |
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

**In progress.** v1.35 is the last frozen version (`../v1.35/`).

| stage | what | state |
|---|---|---|
| the specification | `spec/`: the platform, the core, the replayer, the Tracer, the kept chapters, the guide | written; three reversals of recorded rulings and the new open items (`spec/machine-spec.md` chapter 26) wait for the owner |
| 1 | the register map and the names: windows and blocks, header layout, `PARAMS`, `STATUS`, `EVENTS`, `ERRORS`, the tables; `fw/include/mesh.h` and `fw/REGISTERS.md` regenerated | to do |
| 2 | the control protocol and the boot: no commands, the barrier's rules, the `LOAD` boot, the nets' latencies | to do |
| 3 | the core: C and Zicond, the NMI and MEIP, the CSRs, its cycle costs | to do |
| 4 | the NoC, the topology and the requests: attach tiles, the special tiles' routers, the request tree | to do |
| 5 | the DMA, write and read units | to do |
| 6 | the receive, gather and inject units and the scratchpad | to do |
| 7 | the peripherals: the control block and the DMA, Ingress/Egress and the host interface, the RW engine | to do |
| 8 | the replayer | to do |
| 9 | the Tracer and the segment | to do |
| the freeze | the suites and reference logs re-made on v1.36's simulator, every `[meas: …]` figure measured again, the documents of this folder made true of v1.36 | to do |

Each stage is a section of the guide (`spec/CHANGES-v1.35-to-v1.36.md`). `CHANGES-v1.35.md`, `BUILDING.md`,
`RUNNING.md` and `ERRATA.md` are v1.35's pages and describe the simulator as it still is; what is open in the design
is `spec/machine-spec.md` chapter 26.

## Glossary

| term | meaning |
|---|---|
| meshsim | the simulator, whatever its implementation (D37) |
| `ref` | the reference v1.35 was checked against: v1.3's Java simulator with retire-trace observer hooks added (D5; `Observer.java`, and one guarded line at each call site in `Controller.java`, `ControllerMemories.java` and `TileControl.java`; every other file identical to v1.3's). It is kept outside the frozen copy; v1.3's own simulator is `v1.3/meshsim/` |
| D*n* | decision *n* of v1.35, in `planning/DECISIONS.md`; v1.3's are in `spec/history/` |
| Quest *n* | task *n* on the project's measurement machine, where the long runs and timings were made |
| `[meas: …]` | in the spec, a measured figure and what it was measured on: the RTL, or a version of the simulator (`spec/machine-spec.md`'s preamble) |
| the RTL | Snarky One, the hardware design; v1.36's specification is its specification |
| M1, M2, M3 | the stages the simulator was built in: M1 the framework and one RV32 controller, matching `ref` instruction for instruction; M2 the sort, multithreaded; M3 v1.3 as it runs, with the Tracer and the replayers. Fixture folders are named after them |
