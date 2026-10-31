# Worker 8: the sort to v1.2

You are porting the sort to v1.2. The sort is the mesh's first workload (part of an ASIC for proving the Jolt
zkVM): 64 tiles sort keys by local sorts, a split by exact cuts, an all-to-all and a merge. It ran on v1.1's
simulator at full size. The machine has changed a lot in v1.2, and meshsim is being ported to it by another session
(WORKER4); your job is the sort's software, so that it runs on the v1.2 simulator and its results can be compared
with v1.1's.

REPOSITORY AND BRANCHES
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- **The v1.2 simulator is on the branch `v1.2-meshsim`**, still being worked on by WORKER4 (phases 3-6: peripherals,
  boot, checks, recipes). **Work on your own branch, `v1.2-sort`, made from `v1.2-meshsim`**, and merge
  `v1.2-meshsim` into yours whenever you need WORKER4's latest (merge, don't rebase). Leave merging into `main` to the
  owner and the integrator.
- For the long runs (the full-size sort), use the Quest runner: its repository and `PIPELINE.md` are in your user
  preferences. It can't reach meshsim, so a task carries the code it needs.

INPUTS
- **`wip/spec/machine-spec.md` v1.2: the authority**, and `wip/spec/CHANGES-v1.2.md`, the map from v1.1.
- **The recipes**, `wip/sw/RECIPES.md` and `wip/sw/CONVENTIONS.md` (with their compiled sources in
  `wip/sw/recipes/`): how a v1.2 tile program does each job the sort does (all-to-all, bucket writes, augmented
  writes, gathers, the read port, exchanges and replies, waiting for the tables, requests and their limits, fences).
  Follow them; where the sort needs something they don't cover, say so.
- **The generated v1.2 header**, `wip/meshsim/sw/mesh.h` on `v1.2-meshsim` (one struct per register block), and
  `wip/sw/REGISTERS.md`.
- **The sort:** `wip/sort/` (program, split, skew, skew-planner) and meshsim's copy in `wip/meshsim/sw/ext/`
  (`tools/check_layout.sh` keeps the two in step). v1.1's results in `wip/results/` are your baseline.
- `wip/planning/reviews/WORKER3-REVIEW-2.md`, section 6: a file-by-file migration inventory. It predates several
  decisions, so check each row against the spec. `wip/planning/DECISIONS-v1.2.md` for the reasoning.
- `WORKER4/NOTES.md` and `WORKER4/REPORT.md` on `v1.2-meshsim`: what the simulator does now, and what is still
  interim (until WORKER4's phase 4, programs start through an interim loader and v1.1's RUN/HALT line, not the boot ROM).

WHAT CHANGES, at least (the spec and CHANGES are the full list)
- Byte addresses in packets, 32-bit chunk addresses inside the engines (shift by 2 between them); the new region map.
- The modes (0 write at address, 1 by source, 2 bucket, 3 augmented, 4 gather request, 5 count only); seven counters
  and the counter conventions; the renumbered `EVENTS`; replies as mode-0 writes; each request's own reply address.
- Natural alignment everywhere, and no spanning: every `PTR` aligned to its packets' size, `STREAM_SRC` and the
  `GROUP` sources to the read size; no mixing of sizes through one `PTR`.
- A stream's count is 17 bits; a controller may stream to the broadcast.
- Waiting for the tables by events (counter done and DMA run done), not `wait_tables()`'s poll.
- Requests: at most 1, 3 and 1 outstanding to C&C 1, 2 and 3; a read is outstanding until its data lands.
- Staging into big SRAM is counted; boot overlays and commands are the only uncounted traffic.
- No HALT, no REPORT, no per-tile UART: debug output through the trace word; programs end through `SIM_EXIT`.

YOUR SCOPE
- **Yours:** `wip/sort/`, `wip/meshsim/sw/ext/`, the sort's steps in `wip/meshsim/run_tests.sh` (now reported as
  "PENDING (sort port)": bring them back as the sort runs), and anything the sort's own build needs.
- **Not yours:** the simulator (`wip/meshsim/src/`, `test/`) and meshsim's low-level software, which are WORKER4's;
  the spec, the recipes, the decisions logs. If the sort needs a simulator change (C&C's side of the sort, a harness
  feature), record it in `WORKER8/REQUESTS.md` for WORKER4 rather than making it.

DELIVERABLES
- The branch `v1.2-sort`: the sort building and passing its suites on the v1.2 simulator at the sizes `run_tests.sh`
  uses, then at full size on the Quest runner.
- `WORKER8/REPORT.md`: what changed, file by file; the v1.2 results against v1.1's (cycles per phase, and every
  difference explained); what the recipes didn't cover.
- `WORKER8/QUESTIONS.md` (for the integrator: the spec silent, ambiguous or wrong, each with your proposed answer)
  and `WORKER8/REQUESTS.md` (for WORKER4).

RULES
- The spec is the design. Don't invent hardware behaviour; carry on with your proposal, marked, and record the
  question.
- This prompt is the go-ahead to change the files in your scope on `v1.2-sort`, and to write in `WORKER8/`.
- Commit and push to your branch as you go, at least every turn, even if a build is broken (say so in
  `WORKER8/NOTES.md`). Keep NOTES.md current so the work survives a reset.
