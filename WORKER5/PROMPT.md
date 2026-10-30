# Worker 5: the spec, v1.2

You are writing version 1.2 of the machine spec for the mesh, part of an ASIC for proving the Jolt zkVM. The
v1.2 design was settled over a long session with the owner and Tony and recorded as a decisions log, then
reviewed three times. The spec, `wip/spec/machine-spec.md`, is still v1.1. Your job is to make it describe
v1.2: **one current, complete description of the machine**, which the hardware (Tony), the simulator and the
software are built from. The log stays as the history.

A recipes session (WORKER6) is writing v1.2's programming recipes in parallel, from the same log. When both of
you are done, you will check its recipes against your spec in a second round.

REPOSITORY
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- `wip/` is the work in progress; `v1.0/` and `v1.1/` are frozen. Commit to `main`; run `git pull --rebase`
  before every push. Push as you go.

INPUTS
- **`wip/planning/DECISIONS-v1.2.md`: the authority for every change.** Read "Where the design stands", then
  every section. Later sections revise earlier ones and superseded points are marked; when two places
  disagree, the later section wins. Section 33 lists what is deferred.
- `wip/spec/machine-spec.md` (v1.1): the starting point and the authority for everything the log doesn't
  change. Keep its conventions: the labels (O, T, H, S, *(Proposed)*, [estimate]), its section numbering where
  the subject survives, its style.
- **Checklists of what changes, section by section:** `wip/planning/reviews/WORKER2-REVIEW-2.md` section 4, `wip/planning/reviews/WORKER1-REVIEW-2.md`
  and `wip/planning/reviews/WORKER3-REVIEW-2.md` (their "spec sections to update"), and the three `wip/planning/reviews/WORKER*-REVIEW-3.md` files.
- The reviews in `wip/planning/reviews/`, for the reasoning behind decisions.

WHAT TO WRITE
- **Rewrite `machine-spec.md` as v1.2**, describing the machine as it now is, not how it changed. Remove what
  v1.2 removes (HALT and the RUN/HALT nets, REPORT, the four named counters, the sender, the resident loader,
  spanning access, v1.1's modes and bad-packet rules, `ts_busy`, `mem_idle`/`cm_idle`), rather than marking it.
- **Every section the log touches**, in particular: the link word and its layout; the 32-bit map with the boot
  ROM; the modes; four counters and K; natural alignment and the bad-packet rules; the controller's register
  block, the standard peripheral window and C&C 1's peripheral, as complete register tables (section 8);
  events, counted and live; errors and terminal traps; the toast pin and the hard and soft resets; the boot
  ROM, listed exactly, and the boot sequence; the engines as their controllers' peripherals.
- **Section 9 (invariants)** per section 27: say which are simulator checks. **Section 10 (parameters)**: K
  and its formula, the counter field, the stream count, the boot ROM. **Section 11 (budget)**: the 256-bit
  word's cost (section 2 of the log). **Section 13 (open items)**: close what v1.2 settles, add the log's
  pending questions and section 33's deferred items as open items, without designing them.
- **`wip/spec/CHANGES-v1.2.md`**: what changed from v1.1, section by section, each item citing its decision
  section, so a v1.1 reader can find their way.
- **The hardware/meshsim split** (27): state for each rule whether the hardware checks it or meshsim does.

RULES
- **Don't design.** Where the log is silent, ambiguous or self-contradictory, record a question in
  `WORKER5/QUESTIONS.md` (what, where, your proposed answer), write your proposal into the spec marked
  *(Proposed)*, and carry on. The integrator brings the questions to the owner.
- Don't edit the decisions log, the recipes, meshsim or the sort.
- This prompt is the go-ahead to change `wip/spec/` and to write in `WORKER5/`. Change nothing else.
- Keep `WORKER5/NOTES.md` as you go, so the work survives a context compaction.

DELIVERABLES
- `wip/spec/machine-spec.md` v1.2 and `wip/spec/CHANGES-v1.2.md`.
- `WORKER5/QUESTIONS.md`, and `WORKER5/REPORT.md`: what you changed, what you left *(Proposed)*, and anything
  the recipes session should know.
