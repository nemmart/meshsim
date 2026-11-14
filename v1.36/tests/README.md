# tests: how meshsim is checked against v1.3

*For anyone who needs to trust or rerun meshsim's results: how exactness against v1.3 is established, and what each
fixture folder is and how it was made.*

## Three kinds of evidence

v1.35's meshsim reproduces v1.3's Java simulator (`ref`: v1.3's with the trace hooks, outside the frozen copy; v1.3's own is
`v1.3/meshsim/`): the same results, and the same cycle counts
except for the deliberate differences (D30; `spec/CHANGES-v1.3-to-v1.35.md`). Each fixture below was made by running
`ref`; each test compares meshsim with it.

- **Goldens:** what `ref` printed or traced for a whole program or run (a retire trace, a suite's log, a sort's
  fingerprints), compared line for line. A suite's log is compared with its timing masked (`./run_tests.sh --check`);
  cycles are never masked.
- **Conformance drivers:** one unit, or one kind of port, driven by a scripted sequence of inputs every cycle, the same
  script in `ref` and in meshsim; the observations (`*.obs`, from `*.script`) must be equal. They cover the edges a
  whole program rarely reaches. The port kinds' rules are `src/PORTS.md`.
- **Fingerprints:** a canonical hash of the machine's state at fixed points (`*.corefp`, the sort's `fp`, the chains'
  `*.starts`), so a long run is checked without a full trace. The formats are `TRACE-FORMAT.md`.

## Tests and suites

- **`make check`** builds and runs every `test_*.cpp` here, in the debug and release builds with gcc and clang
  (`BUILDING.md`). Most compare against a fixture folder; the `*conf` and `golden*` tests are the conformance drivers
  and goldens above.
- **`./run_tests.sh`** runs v1.3's suites, ported, with v1.3's step names and logs; `--check` compares each log with its
  reference (`RUNNING.md`). The suites' sources are `suite_*.cpp`, `ctl/`, `stageb.cpp`, `sort_test.cpp` and the
  `*.mk` files; `suite.h` is their shared harness, `mach.h` the machine (`src/units/README.md`).

## The fixture folders

| folder | what |
|---|---|
| `golden/` | the M1 programs' retire traces (`RUNS` lists each: tile, images, schedule), and their images |
| `golden-m2/` | M2's references: the programs' outputs and hashes (its `README.md`) |
| `golden-m3/` | M3's references, T2's unit lines among them (its `README.md`) |
| `golden-chain/` | each segment start of the 128-segment chains of both workloads, as hashes |
| `fp-m1/` | the core's fingerprints for the M1 programs (`*.corefp`) |
| `g1-replay/` | one tile's control boundary, cycle by cycle, for two programs |
| `m3-images/` | M3's controller images (C&C 1-3, the replayer's and Tracer's controls) and their boot references |
| `portconf/`, `routerconf/`, `memconf/`, `engconf/`, `slotconf/`, `cncconf/`, `cnc1conf/` | conformance scripts and observations: the port kinds, the router, the memory path, the engines, the peripheral slot, C&C's model, C&C 1's peripheral |
| `tileslotconf/` | a tile's controller with its slot: programs and observations |
| `reqconf/` | the request bus under five loads (calm, drop, fan-in, full, mix) |
| `tracerconf/`, `tracerparts/` | the Tracer's cases and its parts (the LRU coder, the memory system) |
| `replayer/` | the replayer's suites: row cases, the ALU sweep and reports |
| `ctlsuites/`, `p2suites/`, `p4suites/`, `tracersuites/`, `stageb/` | v1.3's suite logs, the references of `run_tests.sh --check`; `PROVENANCE` in each says how they were made |
| `ctl/` | the control suites' sources (code, not fixtures) |
| `sort/` | the sort's fingerprints and its kit's references (its `README.md`) |
| `acceptance/` | the acceptance kit: build, calibration and its task (its `README.md`) |
| `delivery/` | doubles as Java prints them, for the delivery tools' output |
| `mach/`, `suite/` | the harness's self-tests' expected output |
| `freeze/` | the freeze check: its body and the task that runs it |

The `p4_*.sh` and `tracer_teeth.sh` scripts are checks run on the suites (debug-build checks, the logs' line counts,
that the Tracer's tests fail when the Tracer is broken).

## How the fixtures were made

Every fixture was generated from v1.3's Java simulator by the scripts of the oracle folder, which is not in this
frozen copy; they are in the repository's history at the commit that froze this folder
(`git log --diff-filter=A --format=%h -- v1.35/README.md` names it). `PROVENANCE` files and the `RUNS` lists name each
fixture's command and `ref`'s commit. **The frozen copy can verify its fixtures but not regenerate them.**
