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
- [x] controller: replay_ctl.c keys presented words (tkey) and looks up by key; fast path holds only the window's
      bitmap templates, NOP-policy check moved to run time (DM_NOP); layout v2: templates at chunk 0x58000 (1.4 MB).
      Java side: test/ReplayTables.java (load, key, lookup, manifest check); ReplayWindow/ReplayDriver keyed by key;
      ReplayDelivery.tablesFor (program dir, else delivery tables/, else ../tables/replayer). StageB synthetic 18/18
      (controller-fed 3.513 cycles/row, was 3.452: the key's cost on the fast path).
- [x] test/SyntheticDelivery.java: random RV64IMAC programs (atomics, M with jolt-cpp's advice, every load/store
      width, branches, jumps, ECALL, HostIO, CSR, inlines) through the universal tables by a functional replay, each
      checked instruction compared with a plain RV64 interpreter (x1-x31, pc, memory); writes a delivery (tables/,
      streams raw+LRU, state/state-lru/state-pick, rows, end, manifest). Seeds 2-12 x 4 windows x 65,536: ~10K
      instructions each, 0 mismatches; a mutation (ShiftDataB ignoring shift_offset) is caught. Lessons: jolt-cpp's
      self-transfer is sequential (appendix 1.4); keep pc far from 0 (pc 0 is the program end): JALR off x2-x4 only,
      mtvec read-only.
- [x] Stage A (ReplayDelivery) on synthetic deliveries: every window OK in all three variants, ~0.3 s per window.
- [x] Stage B on 65,536-row windows: OK. The first try hung in start-up: build_fast's multiplier search (128 slots,
      4,096 tries) never finds a collision-free table for ~40 bitmap templates; now 512 slots (8 KB), 256 tries.
- Speeds (this sandbox, one JVM, synthetic windows, test/ReplayShard): Stage A 76-388 ms per 65,536-row window
  (warm ~0.1 s); Stage B 9.7-15 s per window (~5-6.5K rows/s; meshsim simulates the whole 64-tile chip). RSS ~100-140 MB
  per JVM. Background jobs die at the end of a tool call here: run long things in the foreground under `timeout`.
- [ ] REPLAY-INPUT-REQUIREMENTS.md (key, sign bit, VirtualLoad/Store logs, E[15], tables location)
- [ ] synthetic delivery generator + acceptance kit + speed measurement + estimate

## 2026-11-01 (turn 3)
- v1.25 spec (branch v1.25 @ fcb09f9) checked with WORKER10's 10-point list: no disagreement (QUESTIONS.md); PEXT
  non-contiguous masks now a statistic (Replayer.pextNonContiguous).
- run_shard.sh reads its shard from the delivery kept on the runner box (s7 revised): -d DELIVERY -m MANIFEST_SHA256,
  every file a replay reads checked (CORRUPT), read-only. Tested: pass, corrupted rlog-lru caught, wrong manifest sha caught.
- **Stage B on tile 0 alone** (Chip.parked, opt-in; StageB.LONE; ReplayShard stage B; stage F = full chip): profile showed
  the time spread over all 64 tiles' routers/SRAM/queues. Parking tiles 1-63 gives identical rows, cycles and stalls
  on every window tried; end-of-run guard Chip.parkedUntouched. ~1.8 s per 65,536-row window warm (36K rows/s) vs
  7.9-13 s full chip: ~4-7x. run_shard -s ABF checks B and F cycles agree.
- Kit reads WORKER9's layout (/tmp/w9-deliveries/PROG/, PROG.manifest.json beside, keys without window prefix);
  `-x K` adds full-chip Stage F on every K-th window (cycles must equal B's).
- **Quest task 077-w11-calibrate** pushed (quest 84fd71b; kit sha 6c3d5307..., built before -x, which calibrate doesn't use).
  Next: read results/077-w11-calibrate/calib/j*/SUMMARY.txt, finalize REPORT section 4, then the shard tasks once
  WORKER9 gives delivery paths + manifest SHA-256 (tasks/hold/070, 071 write /tmp/w9-deliveries/{evm-zone-29,cyclone-29}).
- Quest identity: git config user.name WORKER11 in /home/claude/quest.
