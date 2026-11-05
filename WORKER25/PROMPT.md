# Worker 25: v1.26.1, the point release

You are making **v1.26.1**: v1.26 (the frozen `v1.26/`) plus exactly the four changes in `wip/planning/DECISIONS-v1.26.md`
section 3 (items 13-16), and nothing else: RUN_CONFIG at word 32 on both units (the 24-bit row count and the two per-pass flags),
words 32-35 shared by the replayer and the Tracer with ROWS_RUN widened to 24 bits, the state word's flags reserved, and the
stream word rearranged with the advice field. Read that file whole first (items 1-12 are v1.26), then `v1.26/README.md`, spec
4.17 and 4.19 in `wip/spec/machine-spec.md`, `DECISIONS-v1.3.md` sections 23-24 (the Tracer's side of the same layouts), and
the closed sessions' reports `wip/planning/sessions/WORKER19-REPORT.md` and `WORKER20-REPORT.md` (who made v1.26 and how).
Tony (RTL) has not started on v1.26 and is waiting for this release.

REPOSITORIES
- **meshsim**, https://github.com/nemmart/meshsim: `wip/spec/`, `wip/meshsim/` (the replayer model, `ReplayDriver`, the
  acceptance kit, `sw/replay/replay_ctl.c` and recipe 26, `mesh.h` and the register reference), your `WORKER25/`. Work on a
  branch, `v1.26.1`, from `main`; the integrator merges and freezes.
- **Quest** (the runner): claim task numbers in its `NUMBERS.md` first; end task scripts with an explicit `exit` before any
  embedded data. The converted 2²⁹ deliveries live in the runner's persistent data (`~questrunner/data`; PIPELINE.md).
- Clone each with the token from your user preferences (as `https://x-access-token:<token>@github.com/...`); never write a token
  into a file.

THE WORK
1. **The spec**: items 13-16 in 4.17, 4.19.2, 4.19.8-4.19.10, 8.11 and section 13 (the register map, the stream word's table,
   the checks [5]/[6] following RUN_CONFIG [24], a pass of any row count); `wip/spec/CHANGES-v1.26.1.md` (the delta from
   v1.26, each item citing DECISIONS-v1.26); and **the RTL guide from v1.25 straight to v1.26.1**, since Tony starts from v1.25.
   The row count of 0 is proposed (run nothing, done at once): mark it so.
2. **meshsim and the software**: the replayer model, `mesh.h`, the register reference, `replay_ctl.c` and recipe 26 (writing
   RUN_CONFIG, words 0-65 loaded verbatim every pass), `ReplayDriver`, the kit, the synthetic deliveries; tests for each change:
   a short pass, a row count of 0, the flags from RUN_CONFIG, [5]/[6] with the store log off, the new stream word, ROWS_RUN past
   2¹⁶, a RUN_CONFIG write while running.
3. **The real deliveries**: convert the runner's 2²⁹ windows to the new stream word (and the flags out of word 0), then rerun
   v1.26's acceptance: every window of both workloads, stages A, B and F (as Quest 098, 099), with the stall counts.
4. **The suites**: quick and full pass; the full-size sort unchanged (p4/uniform-k128: 1,229,008 cycles).
5. **The release**: propose `v1.26.1/`'s contents, following `v1.26/`'s, with its README and results; the integrator copies it
   and deletes `v1.26/` after (owner).

OUT OF SCOPE: anything else of the Tracer's (SP1, WORKER21); gold and WORKER16's segment tools, which write the same record and
stream word (WORKER18 and WORKER16 follow up); v1.3.

RULES
- **Propose a plan first and wait for the owner's go-ahead.**
- Where items 13-16 leave something unsaid, propose it (marked *(Proposed)*), and ask; don't widen the release.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean `git status`; long runs go
  to the runner, and background jobs don't survive a turn.

DELIVERABLES: the `v1.26.1` branch (the spec, the change notes, the RTL guide, meshsim, the firmware, the kit), the acceptance
results, `WORKER25/REPORT.md`.
