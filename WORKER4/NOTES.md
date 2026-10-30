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

- **The sort's suites** (`check_layout.sh`, `sw/ext/sort/build.sh`, SortTest and its runs) leave `run_tests.sh` when
  the packet path changes in phase 1: the sort is not mine and is ported onto this simulator afterwards. Listed in
  QUESTIONS.md (W4-01) for confirmation.

## Log

- **Phase 0** (2026-10-30): branch, toolchain, baseline (above); read the prompt, CHANGES-v1.2, spec 1–4.18 and 5–6.5,
  WORKER3-REVIEW-2 section 6. Still to read in full: 4.19, 6.6–6.7, 8, 9, 10, 13, the recipes and the draft header.

## Known-failing at each push

| push | known-failing |
|---|---|
| phase 0 (no code changed) | RecipeTest `[snippets]` (46, both seeds), as in the baseline |
