# WORKER11 notes (keep current: the work must survive a reset)

Branch `v1.25-meshsim`, from `v1.2-meshsim` @ 35cc4b4. Owner's go-ahead 2026-10-30 (plan: model, controller, tests,
acceptance kit). Until WORKER10's `v1.25` spec branch exists, the design is DECISIONS-v1.25 and WORKER9's work.

## Environment (a fresh sandbox)
- `apt-get update; apt-get install -y openjdk-21-jdk-headless gcc-riscv64-unknown-elf` (javac and the controller's toolchain).
- Code lives in `wip/meshsim/` (src, test, sw/replay). Build: `javac -d build src/*.java`, then the tests minus
  `test/PENDING`; `sh sw/build.sh; sh sw/replay/build.sh`.
- Baseline at 35cc4b4: ReplayerTest 1188 passed; StageB synthetic 18/18.
- `wip/tables/` copied from `main` (09f4390) onto the branch: the universal tables.
- Owner (2026-10-30): the runner-hours estimate comes from meshsim's measured replay speed (rows per wall-clock second,
  Stage A and Stage B separately) on synthetic windows, scaled to ~5,700 (Cyclone) and ~5,400 (EVM Zone) windows of
  65,536 rows. Not from task 065 (that timed the sort).

- DECISIONS-v1.25 s7 (2026-11-01): Q1-Q4 answered (QUESTIONS.md). **Acceptance tasks generate their shard of windows
  with WORKER9's delivery writer, replay at once, keep only results, discard the delivery** (no 31 GB deliveries stored).
  The synthetic generator stays as the kit's test input until the writer exists.
- Runner: 16 threads, ~62 GB, 45-min task timeout (TASK_TIMEOUT), MAX_ATTEMPTS 3; kits embedded as base64 tgz (task 074).
- v1.2-meshsim merged in (11f9904); wip/tables refreshed from main (d38b60c: replayer tables unchanged, tracer/ added).

## Progress
- [x] model (ReplayerAlu renumbering, new opcodes, PEXT, sign_imm [86], offset_imm, HostIO, E[15] on every load,
      misalignment flagged); ReplayerTest 1457/0 on data/vec-821e1bd.tsv + opcode table; StageB synthetic 18/18
- [ ] controller (key, universal tables, bitmap-driven fast path)
- [ ] tests (vec.tsv at 821e1bd, unit tests, StageB synthetic), REPLAY-INPUT-REQUIREMENTS.md
- [ ] synthetic delivery generator + acceptance kit + speed measurement + estimate
