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

## Progress
- [ ] model (ReplayerAlu renumbering, new opcodes, PEXT, sign_imm, offset_imm, HostIO)
- [ ] controller (key, universal tables, bitmap-driven fast path)
- [ ] tests (vec.tsv at 821e1bd, unit tests, StageB synthetic), REPLAY-INPUT-REQUIREMENTS.md
- [ ] synthetic delivery generator + acceptance kit + speed measurement + estimate
