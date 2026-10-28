# meshsim v1.0 — the mesh sorter's cycle-level simulator

meshsim models the machine of `spec/machine-spec.md` (v1.0) cycle by cycle: 64 tiles on an 8 × 8 mesh, C&C on tile 0's
fifth port, each tile with its router, injection point, traffic director, big SRAM (8 areas), SRAM read and write
queues, address generator, write engine, GATHER engine, controller read port, table memories and an RV32I + Zmmul
controller. Tile programs are C against `mesh.h` 1.0 (`../sw/`); the sort program, the split and the planner build
against this tree (`sw/ext/`). Java 17 or later; the tile software needs `riscv64-unknown-elf-gcc` (and picolibc).

```
run_tests.sh      check the layout, build sw/, compile and run every suite (logs in results/); "quick" skips the long runs
src/              the simulator (one class per unit; Registers.java generates sw/mesh.h, RegDoc.java REGISTERS.md's tables)
test/             the suites (below), the sort harness (SortTest, SortSummary, SortCompare, PhaseNeutral, KeyGen, Inputs),
                  the split's (SplitRun, SplitSets) and the verification summary (VerifySummary)
test/riscv/       riscv-tests rv32ui/rv32um: build script and the built binaries (bin/)
sw/               mesh.h (generated), crt0.S, app.ld, loader.S, loader.ld, build.sh; tests/*.c (the test programs; rcp*.c and
                  rcp.h: the recipes of ../sw/RECIPES.md); ext/: the copies of ../sort/ that the sort image is built from
                  (sort/, skew-planner/, split-bisect/; tools/check_layout.sh checks them against ../sort/)
kit/              the measurement kit's sources (assemble.sh builds it; the built kit is ../kit/)
data/             stream matrices (zipfraw, trace, one99) for the all-to-all tests
hops.txt          the routing offset table hops[k] (spec 4.6), validated at load
results/          the logs of the last run_tests.sh
tools/            check_layout.sh, recipes_sync.py (RECIPES.md's code blocks from sw/tests), logdiff.py
```

**Build and run:** `./run_tests.sh` (everything, about two hours on one core) or `./run_tests.sh quick`; one suite:
`javac -d build src/*.java test/*.java; java -cp build CtrlTest -1`. Generated files: `java -cp build Registers
sw/mesh.h`, `java -cp build RegDoc` (REGISTERS.md's tables).

**The suites.** BusTest (bus primitives and the FIFO rule), NetTest (links, routing, ordering, fan-in; `skewtable N`),
MemTest (big SRAM, its queues), EngineTest (address generator, write engine, all-to-alls), CtrlTest (control, the
controller, registers, invariants; `full`: the all-to-all from C), RiscvTest, ReadPortGatherTest, AccountingTest,
SearchBench, AgTest (read-ahead, the table ports, the v1.0 rulings), AgBench (the lead-in and read-ahead measurements),
RecipeTest (every recipe and its snippet in RECIPES.md), CondTest (conditions, the done gate, the error line, REPORT),
RegDocTest (REGISTERS.md against the generator, mesh.h and the simulator), SortTest (the sort), Sweep (parameters).
Each takes a shuffle seed (`-1`: none): with a seed the children of every unit are stepped in a random order each
cycle, which must change nothing.

**Model conventions.** Every interaction goes through a timed bus: a put at t is visible at t + 1, so the result does
not depend on the order units are stepped. A FIFO is full by its count before the cycle's pop. A register store at t
acts from t + 1; status computed at t is read at t + 1. `Config.machine()` is the machine as specified (reset in HALT,
tiles booted by C&C); `Config.defaults()` gives coreless tiles driven from Java. `Chip` checks the invariants of spec 9
after every cycle and reports violations; `Chip.startSkew` gives each tile its 0/+1 start skew.
