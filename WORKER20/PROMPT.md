# Worker 20: meshsim and the software at v1.26, the replayer's v1.3 changes

You are bringing meshsim's replayer and its software from v1.25 to **v1.26**: **exactly the replayer changes in
`wip/planning/DECISIONS-v1.26.md`**, and nothing else. Read that file first, then `DECISIONS-v1.3.md` sections 12, 23 and 24,
then spec 4.19 in `wip/spec/machine-spec.md`, and the closed sessions' reports in `wip/planning/sessions/` (WORKER11 built the
v1.25 replayer model, the acceptance kit and the CL4 checks; WORKER9 wrote the deliveries).

REPOSITORIES
- **meshsim**, https://github.com/nemmart/meshsim: `wip/meshsim/` (the replayer model, `ReplayDriver`, StageB, the acceptance
  kit, `sw/replay/replay_ctl.c` and recipe 26), your `WORKER20/`. Work on a branch from `main` and push it for the integrator to
  merge.
- **jolt-asic-planning**, https://github.com/Quarky93/jolt-asic-planning, branch `tracer-standalone-main`: WORKER9's
  `delivery_writer`, if you regenerate deliveries rather than convert them (on your own branch).
- **Quest** (the runner): claim task numbers in its `NUMBERS.md` first (pull, append, commit, push; on a rejected push, pull and
  take the next), and end task scripts with an explicit `exit` before any embedded data. The 2²⁹ deliveries live on the runner.
- Clone each with the token from your user preferences (as `https://x-access-token:<token>@github.com/...`); never write a token
  into a file.

THE WORK
1. The replayer model: the 68-word record, the stream word's bits-in and values-out (follow WORKER19's layout; until it lands,
   keep it behind one parsing function), 4 KB read- and write-log queues, the flush on a state write, the padding rule, the size
   checks and `ERRORS` [3]-[6].
2. The software: `replay_ctl.c` and recipe 26 (credits, masks, the new queue sizes), the kit, `ReplayDriver`, the synthetic
   deliveries; tests for each change, including each error bit and the flush.
3. The real deliveries: convert the existing 2²⁹ windows to 68 words with value counts (or regenerate them; propose which),
   then rerun the acceptance on every window of both workloads in stages A, B and F, and report the stall counts with 4 KB
   queues against v1.25's 8 KB.
4. The quick and full suites, and the full-size sort unchanged (1,229,008 cycles).

RULES
- **Propose a plan first and wait for the owner's go-ahead.**
- Measure and report; the spec is WORKER19's (follow its text; raise disagreements through the integrator).
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean `git status`; long runs go
  to the runner.

DELIVERABLES: the branch, the acceptance results, and `WORKER20/REPORT.md`.
