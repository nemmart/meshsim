# WORKER22 notes (P4: the network, memory and engine suites)

Branch `sim-w22`, from main 67d42fb. Rows: C3, C4, C5, C6, C7, C11, C17, G1.

## The steps (v1.3's `run_tests.sh` names)

| row | suite | steps |
|---|---|---|
| C3 | NetTest | `net` (quick: `-1 quick`; all: `-1`, the fan-in table differs), `net-shuf` (5 quick), `net-shuf2` (11 quick), `skewtable` (`-1 skewtable 40`, all only) |
| C4 | MemTest | `mem` (-1), `mem-shuf` (3), `mem-shuf2` (8) |
| C5 | EngineTest | `engine` (-1), `eng-shuf` (4), `engine-full` (`-1 full`, all only) |
| C6 | AgTest | `agtest` (-1), `agtest-shuf` (4) |
| C7 | ReadPortGatherTest | `rdport` (-1), `rdport-shuf` (4), `rdport-full` (`-1 full`, all only); boots `rdport`, `gather` |
| C11 | SearchBench | `searchbench`; boots `search` |
| C17 | RequestBusTest | `reqbus` (-1), `reqbus-shuf` (4); boots `req` |
| G1 | AgBench | `agbench` (all only) |

The images (rdport, gather, search, req) are built by `fw/build.sh` and committed (W19). Every config field these
suites set is in `MachConfig` already. ref's shuffled logs differ from the unshuffled ones only in the header line
(`shuffled child order, seed N`, where printed) and the summary line.

## The plan (agreed with the owner, 2026-11-11)

1. **Setup**: branch, clang and a JDK in the container, `make check` clean on main.
2. **Fixtures**: `oracle/ref/p4suites.sh` runs each step in a scratch copy of `ref` (quick and all), `--fixtures` writes
   the logs, `--check` regenerates and compares. Regenerated, not taken from `ref/results` (W20's F1: a stale log
   there). Fixtures in `tests/p4suites/` with a `PROVENANCE`. The long steps (`skewtable 40`, `engine-full`,
   `rdport-full`, `agbench`) go to the runner (a Quest task) if slow locally.
3. **Per suite**, in order NetTest, RequestBusTest, MemTest, AgTest, EngineTest, ReadPortGatherTest, SearchBench,
   AgBench:
   - map every Java access onto `tests/mach.h`, list the gaps;
   - harness operations and violation kinds missing: asked of W20 through the integrator; read-only accessors for a
     unit's existing state: added here, noted below and in a comment at the accessor;
   - `tests/suite_<name>.cpp` on `suite.h`, Java's arguments;
   - a make-check test: the quick logs byte for byte, plain and at the step's shuffle seed, plus step-all and both
     orders; fp only where the log can't localise (EngineTest's all-to-all).
4. **Wiring**: `tests/p4suites.mk`, one `include` line in the Makefile (integrator told when it's in); the steps join
   W20's `run_tests.sh` when it lands.
5. **Close**: the full steps on the runner; `REPORT.md`.

## The owner's answers (2026-11-11)

- HooksTest is W23's (C15, P5); the prompt named it here by mistake.
- I own ReadPortGatherTest (C7) and RequestBusTest (C17), so the rdport, gather and req programs' logs; W23 relies on
  them and doesn't port them again.
- Read-only accessors for a unit's existing state: added by me, each noted here and in a comment. Anything that changes
  a unit's behaviour or adds state goes to the owner first.
- `p4suites.mk` with one include line: fine; tell the owner when it's in (the Makefile is shared).

## Accessors added to units (read-only, existing state)

(none yet)

## Asks of W20 (through the integrator)

(none yet)

## Progress

- 2026-11-11: plan agreed; branch made.
