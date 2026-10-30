# WORKER4 notes: meshsim to v1.2

Branch `v1.2-meshsim`, from `main` at `4e012ad`. Inputs read against `wip/` (identical to `v1.2_wip/`'s snapshot at
`6decc23` for the spec, recipes, draft header and decisions).

## Environment

- The container needs `apt-get install gcc-riscv64-unknown-elf openjdk-21-jdk-headless` (java is present, javac and
  the RISC-V toolchain are not). `test/riscv/bin` (the riscv-tests binaries) is committed, so no riscv-tests checkout.
- One CPU, 3 GB: `run_tests.sh quick` takes about 25 minutes here; the full run goes to the Quest runner.
- A single tool command is capped at 5 minutes, so long runs go in the background (`nohup bash ./run_tests.sh quick`).
- `run_tests.sh` rewrites the committed `results/*.log`; those are restored (`git checkout -- wip/meshsim/results`)
  before every commit, so a commit carries only deliberate result changes.

## v1.1 baseline (`run_tests.sh quick`, 2026-10-30)

Every suite passes except `RecipeTest`'s 46 `[snippets]` checks (both seeds): `wip/sw/RECIPES.md` is now the v1.2
book, and RecipeTest still checks it against v1.1's `sw/tests/rcp_*`. Expected; it goes away when phase 6 replaces the
v1.1 recipe suite with the v1.2 recipes.

| suite | passed | failed |
|---|---|---|
| BusTest | 154 | 0 |
| NetTest (−1, 5, 11) | 30,602 each | 0 |
| MemTest (−1, 3, 8) | 173 each | 0 |
| EngineTest (−1, 4) | 356 | 0 |
| CtrlTest (−1, 4) | 7,158 | 0 |
| RiscvTest | 96 | 0 |
| ReadPortGatherTest (−1, 4) | 499 | 0 |
| AccountingTest | 11 | 0 |
| SearchBench | 56 | 0 |
| AgTest (−1, 4) | 222 | 0 |
| RecipeTest (−1, 4) | 2,277 | **46** (the `[snippets]` checks, above) |
| CondTest (−1, 4) | 396 | 0 |
| RegDocTest | 300 | 0 |
| HooksTest (−1, 4) | 153 | 0 |
| PeripheralSlotTest (−1, 4) | 43 | 0 |
| SenderTest (−1, 4) | 48 | 0 |
| RequestBusTest (−1, 4) | 49 (reqbus_latency 17) | 0 |
| ReplayerTest | 1,183 | 0 |
| SortTest (L1/L2; built-in sets at 64 × ≤ 2,000; seed 4) | 54; 124; 124 | 0 |

## Decisions of my own (to be listed in REPORT.md)

- **The phase-1 line** (agreed with the owner, 2026-10-30): phase 1 ends with the Java-level hardware suites rewritten
  to v1.2 and passing (BusTest, NetTest, MemTest, EngineTest, AgTest, the Java side of ReadPortGatherTest); the C-driven
  suites report "PENDING (phase 2)", since the v1.1 programs use v1.1's modes, addresses and register map until phase 2
  regenerates `mesh.h` and rewrites them; the sort's steps report "PENDING (sort port)".
- **PHASE-1 INTERIM** marks code that keeps the build whole between phase 1 and the phase that replaces it: TileControl's
  send header, read-port setup and counter registers (v1.1's map, counters 1-4), CnC's command and image addresses,
  Sender's address step, PeripheralSlot's hand-over (the replayer still takes v1.1's word address).
- **The sort's suites** (`check_layout.sh`, `sw/ext/sort/build.sh`, SortTest and its runs) leave `run_tests.sh` when
  the packet path changes in phase 1: the sort is not mine and is ported onto this simulator afterwards. Listed in
  QUESTIONS.md (W4-01) for confirmation.

## Log

- **Phase 1** (2026-10-30, in progress): LinkWord (256-bit word, 32-bit address, counter field, modes 0-7, bad-packet
  rules), Fields (STREAM layout, the common header layout), SRAMReadQueue (counter field, 32-bit destination, misaligned
  reads not performed with payload 0, no spanning), AddressGenerator (STREAM fields, d = 64-66 and 126, entry checks),
  ReadPort (byte destination, counter), GatherEngine (counter pass-through), InjectionPath (checks), Router and Chip (C&C
  1-3 at tiles 0-2), Endpoint, TrafficDirector (rewritten: region switch, K delay line, seven counters, checks), WriteEngine
  (mode 0 by address, misaligned PTR dropped but counted, no spanning), ControllerMemories and BankedMemory (32 KB + 32 KB,
  byte-addressed port B, command landed on covering slot 0), Config (K, rp_latency, kMin, withTsLatency).
  Tests: NetTest, MemTest, EngineTest (+ A2A, a fence test), AgTest, ReadPortGatherTest (Java parts), BusTest, AgBench
  rewritten or rechecked; run_tests.sh reworked (test/PENDING lists the sources left out of the build; each pending suite
  prints and logs "PENDING (<phase>)").
  Found while testing: quiescence must include the K delay line (runAll stopped before the last decrements); a sweep of
  ts_engine_latency must raise K to its bound (Config.withTsLatency); v1.1's mixed-size group (64, 32, 128 into one PTR)
  now misaligns the PTR and is dropped and flagged, so AgTest uses 128, 64, 32, 32.
  Not yet testable from Java and left to phase 2: additive arming through the counter registers (TileControl's
  register path; counters 5-7 have no v1.1 register), the done gate's effect on the per-counter events.
- **Phase 0** (2026-10-30): branch, toolchain, baseline (above); read the prompt, CHANGES-v1.2, spec 1–4.18 and 5–6.5,
  WORKER3-REVIEW-2 section 6. Still to read in full: 4.19, 6.6–6.7, 8, 9, 10, 13, the recipes and the draft header.

## Known-failing at each push

| push | known-failing |
|---|---|
| phase 0 (no code changed) | RecipeTest `[snippets]` (46, both seeds), as in the baseline |
| phase 1, simulator rewritten (`src/` compiles) | **the tests don't compile** (about 230 errors): every suite fails until the Java suites are rewritten |
