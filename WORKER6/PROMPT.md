# Worker 6: the recipes, v1.2

You are writing the v1.2 programming recipes for the mesh, part of an ASIC for proving the Jolt zkVM. The
recipes (`wip/sw/RECIPES.md`) show how a tile program does each common job, step by step, in C against the
machine's registers. In v1.1 they were also a usability check on the hardware: writing out what a tile actually
executes is where protocol problems show up. **That is half your job here.** The v1.2 design was settled in a
decisions log and reviewed three times, but one bug ("done early" in the boot sequence) was found only when
someone wrote out the steps a tile executes. Find the rest.

A spec session (WORKER5) is rewriting the spec to v1.2 in parallel, from the same log. When you are both done,
it will check your recipes against its spec. There is no v1.2 simulator yet, so your code can't run; write it
carefully enough that it will.

REPOSITORY
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- `wip/` is the work in progress; `v1.0/` and `v1.1/` are frozen. Commit to `main`; run `git pull --rebase`
  before every push. Push as you go.

INPUTS
- **`wip/planning/DECISIONS-v1.2.md`: the design.** Read "Where the design stands", then every section. Later
  sections revise earlier ones; when two places disagree, the later section wins. Section 14 settles the header
  (one C struct per block, accessed through pointers), section 36 the boot ROM and boot sequence exactly.
- `wip/sw/RECIPES.md` and `wip/sw/CONVENTIONS.md` (v1.1): the starting point, 21 recipes. The tested versions
  are `wip/meshsim/sw/tests/rcp_*.c`.
- `wip/spec/machine-spec.md` (v1.1), for whatever the log doesn't change. WORKER5's v1.2 draft may appear in
  the same file as you work; the log remains your authority.
- `wip/planning/reviews/WORKER3-REVIEW-2.md` section 6 (the migration inventory) and the reviews in `wip/planning/reviews/`.

WHAT TO WRITE
- **Port every v1.1 recipe** that still applies to v1.2: modes, four counters, byte addresses, natural
  alignment, the register block, `EVENTS` with `EVENTS_REQUIRED`, `ROUTER_HDR`/`ROUTER_ADDR` with the
  no-increment bit, replies as mode-0 writes, commands at slot 0. Retire the ones whose subject is gone
  (REPORT; HALT), saying why.
- **Add recipes for what v1.2 adds**, each walked through step by step:
  - the boot ROM and the boot sequence, including step 5's announced staging (31, 36);
  - the error handler: report, park, the conventions in 20;
  - waiting for the tables by events (17);
  - a peripheral's counted credits: the replayer's refill loop, with credit initialisation and masking an
    idle queue (10);
  - using a peripheral window through its struct (10, 14);
  - C&C 1's side of a phase: `READY_EXPECT`, `PULSE_GO`, the watchdog, draining reports, the toast path (28,
    35, 36). C&C 1 is a behavioural model in meshsim, so this recipe describes its firmware for later.
- **Section 15's "rules that bite" and 16's "gaps and awkwardness"**, rewritten for v1.2.
- **A draft header**, `wip/sw/mesh-v1.2-draft.h`: the structs, macros and constants your recipes assume, per
  section 14. WORKER4 will generate the real `mesh.h` later and reconcile it with your draft.
- **For every recipe, walk the execution:** what each tile and C&C 1 do, in order, and what can arrive when.
  Check each against the counters' rules (arming, done early, unexpected arrival), the `done` gate, the
  GO-to-arming window, alignment and the events' consume rules.

RULES
- **Don't design.** When a recipe can't be written cleanly, or the walk-through shows a protocol problem,
  record a finding in `WORKER6/FINDINGS.md` (what, the steps that show it, a suggested fix) and write the
  recipe the best way the design allows, marked. The integrator brings findings to the owner.
- Don't edit the decisions log, the spec, meshsim or the sort.
- This prompt is the go-ahead to change `wip/sw/RECIPES.md`, `wip/sw/CONVENTIONS.md`, to create
  `wip/sw/mesh-v1.2-draft.h`, and to write in `WORKER6/`. Change nothing else.
- Keep `WORKER6/NOTES.md` as you go.

DELIVERABLES
- `wip/sw/RECIPES.md` v1.2, `wip/sw/CONVENTIONS.md` updated, `wip/sw/mesh-v1.2-draft.h`.
- `WORKER6/FINDINGS.md`: every protocol problem or awkwardness the walk-throughs found, most important first.
- `WORKER6/REPORT.md`: recipes ported, added and retired, and what the spec session should check.
