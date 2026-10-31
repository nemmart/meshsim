# Worker 11: meshsim's replayer to v1.25, and the acceptance harness

You are bringing meshsim's replayer to **v1.25**, a milestone between v1.2 and v1.3: v1.2 with the replayer and its tables
brought up to jolt-cpp's current expansions (commit `821e1bd`), part of an ASIC for proving the Jolt zkVM. v1.25 is accepted
when **every window of two 2²⁹-row workloads (EVM Zone and Cyclone), replayed on meshsim, matches jolt-cpp's trace row for
row**. Your job is the replayer model, its tests, and **the harness that runs those acceptance runs on the Quest runner**.

REPOSITORY AND BRANCH
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- **Work on a branch `v1.25-meshsim`, made from the tip of `v1.2-meshsim`.** WORKER4 is still finishing v1.2's simulator
  there (phase 6: the recipes as suites), which barely touches the replayer; merge `v1.2-meshsim` into your branch when it
  moves (merge, don't rebase). Leave merging into `main` to the owner and the integrator. **Don't touch `v1.2_wip/`.**
- The Quest runner (its repository and `PIPELINE.md` are in your user preferences) runs the big jobs. It can't reach private
  repositories, so a task carries what it needs, as WORKER8's `runner/` kit did.

INPUTS
- **`wip/planning/DECISIONS-v1.25.md`**, all of it: what changes (1, 4, 5), acceptance (2), deliveries (3), the definition
  of done.
- **The v1.25 spec**, being written by WORKER10 on the branch `v1.25` (`wip/spec/machine-spec.md` 4.19, `replayer-opcodes.md`,
  `CHANGES-v1.25.md`). Follow it as it lands; until then, the log and WORKER9's work are the reference.
- **The tables:** `wip/tables/` on `main`, the universal replayer tables from jolt-cpp `821e1bd`, with their README (including
  **the normalized template-index key**) and manifest.
- **WORKER9's work:** `WORKER9/REPORT.md`, `WORKER9/QUESTIONS.md`, and `WORKER9/replayer-opcodes-821e1bd/` (the opcode
  appendix's harness outputs, with test vectors for every opcode, non-contiguous PEXT masks included). Its table builder in
  jolt-asic-planning PR #2 (`tracer/tables/`) contains a resolver written to match `Replayer.java`.
- meshsim's replayer: `src/Replayer.java`, `test/ReplayerTest.java`, `test/StageB.java`, `test/ReplayDelivery.java`,
  `sw/replay/replay_ctl.c`, `REPLAYER-NOTES.md` and **`REPLAY-INPUT-REQUIREMENTS.md`** (the delivery format).

WHAT TO DO
1. **The replayer model:** the six new opcodes (`VirtualLoadB/H/W`, `VirtualStoreB/H/W`) and the changed ones (`ShiftData*`'s
   operands with `shift_offset`, the narrow memory rows' immediate, `VirtualHostIO`, the AMO\*.W bodies), the renumbering;
   the expansion word's `offset_imm` [63] and `sign_imm` [62] (the hard-wired signed-immediate list goes); **PEXT for any
   mask** (drop invariant 16's narrowing and its meshsim check for PEXT); the decode table's invalid class; `seq_remaining`
   counting traced words.
2. **The controller's side:** `replay_ctl.c`'s template lookup with the normalized key, and loading the universal tables.
3. **Tests:** `ReplayerTest` and StageB synthetic at v1.25, every opcode checked against WORKER9's regenerated vectors.
   Update `REPLAY-INPUT-REQUIREMENTS.md` for the index key and anything else the tables changed.
4. **The acceptance harness:** a Quest task kit that takes a delivery (WORKER9's part 3 is building the delivery writer;
   format per `REPLAY-INPUT-REQUIREMENTS.md`), replays every window, Stage A and Stage B, compares every row with the
   reference, and reports per window and in total; split across runner jobs so a full 2²⁹ program finishes in reasonable
   time. Estimate the runner hours for both workloads. Test the kit on synthetic windows until a real delivery exists.

RULES
- **The spec and the log are the design.** Where they're silent, ambiguous or wrong, record it in `WORKER11/QUESTIONS.md`
  with your proposed answer, mark it in the code, and carry on.
- **Your scope:** the replayer model, its tests and harnesses, `sw/replay/`, `REPLAY-INPUT-REQUIREMENTS.md`, `REPLAYER-NOTES.md`,
  and the acceptance kit, on `v1.25-meshsim`, plus `WORKER11/`. Not the rest of the simulator (WORKER4's), the sort, the
  spec (WORKER10's), the tables (WORKER9's) or the decisions logs.
- **Keep the last few tool calls of every turn for committing and pushing.** Background jobs don't survive the end of a turn:
  long runs finish within one or go to the runner. Don't `pkill -f` or `pgrep -f` with a pattern that appears in your own
  command line. Keep `WORKER11/NOTES.md` current.

DELIVERABLES
- The branch `v1.25-meshsim`: the replayer at v1.25, its tests passing, the universal tables loaded.
- The acceptance kit, tested on synthetic windows, with a runner-hours estimate.
- `WORKER11/REPORT.md` and `WORKER11/QUESTIONS.md`.
