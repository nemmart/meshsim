# Worker 19: the v1.26 spec, v1.25 plus the replayer's v1.3 changes

You are bringing meshsim's spec from v1.25 to **v1.26**: the same machine (an ASIC for proving the Jolt zkVM; the replayer is
the tile peripheral that re-executes a window of Jolt's trace from the run log), with **exactly the replayer changes listed in
`wip/planning/DECISIONS-v1.26.md`**, and nothing else. Read that file first, then `DECISIONS-v1.3.md` sections 12, 23 and 24
for the reasons, then spec 4.19 (the replayer) and 4.17 (peripherals) in `wip/spec/machine-spec.md`.

REPOSITORY: https://github.com/nemmart/meshsim (clone with the meshsim token from your user preferences, as
`https://x-access-token:<token>@github.com/...`; never write a token into a file). The frozen `v1.25/` is the baseline; edit
only `wip/spec/` and your `WORKER19/`, committing to `main`.

THE WORK
1. Each of the seven changes in DECISIONS-v1.26 section 1 into `wip/spec/machine-spec.md`: the 68-word record (4.19.2), the
   stream word's new layout with bits in and values out (propose it first), the 4 KB queues (4.19.8, 4.19.10's table), the flush
   on a state write, the padding rule, the size checks, and `ERRORS` [3]-[6] (4.19.9, 4.19.11). Sweep for every place the old
   behaviour is stated (67 words, 8 KB, `PULSE_RUN` emptying the queues, the controller loop's mask handling, invariants).
2. `wip/spec/CHANGES-v1.26.md`: the delta from v1.25, one row per section touched, citing DECISIONS-v1.26.
3. A guide for the RTL from v1.25 to v1.26: extend `CHANGES-to-v1.25.md` with a v1.26 step, or write a short
   `CHANGES-v1.25-to-v1.26.md`; propose which.

RULES
- **Propose a plan first, including the stream word's layout, and wait for the owner's go-ahead.**
- Spec only: WORKER20 changes meshsim and the software in parallel and follows your text; put anything it needs to know early
  in `WORKER19/NOTES.md`.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean `git status`.

DELIVERABLES: the spec changes, `CHANGES-v1.26.md`, the guide, and `WORKER19/REPORT.md`.
