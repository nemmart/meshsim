# Worker 15: v1.2.1, the v1.2 spec and simulator brought in line

You are preparing **v1.2.1** of the mesh, part of an ASIC for proving the Jolt zkVM. v1.2 is a waypoint: its spec, simulator
(meshsim) and software sit in `v1.2/`. The RTL engineer's sessions are reviewing **the v1.2 spec as he took it**, tagged
**`v1.2-tony`** (its `v1.2_wip/` folder; that spec is byte-for-byte the one the simulator work started from). Since then we've
fixed and cleaned up a number of things. Rather than send him a list, we bring `v1.2/` up to date, keep the simulator in line,
re-verify, and tag it **v1.2.1**, so his sessions can simply **diff `v1.2-tony` against `v1.2.1`**. ("Frozen" for v1.2 is loose:
`v1.2/` may be updated for these point releases.)

REPOSITORY AND BRANCH
- Clone https://github.com/nemmart/meshsim with the meshsim token from your user preferences (as
  `https://x-access-token:<token>@github.com/nemmart/meshsim.git`). Never write a token into a file.
- **Work on a branch `v1.2.1`, made from `main`.** You change only `v1.2/` and `WORKER15/`. The integrator merges your branch and
  tags `v1.2.1`. `wip/` (v1.25) is other sessions' (WORKER10 and WORKER11 are making the same cleanups there).

WHAT GOES INTO v1.2.1, measured from `v1.2-tony`
1. **The v1.2 fixes already in `v1.2/`** (made during the simulator work): load latencies applied everywhere and only `HIST`
   loadable through the table window (`7f714c8`); one K for every destination (`191ca33`); the peripheral types (`1beb971`,
   `76c02f6`); K's formula as meshsim's 2L + 4 and K implementation-defined in hardware (`5fb4e44`). Nothing to do but describe.
2. **`v1.2/ERRATA.md` E1-E8**, applied to `v1.2/`:
   - **Spec text (E1-E6):** 4.17.1's peripheral types; 8.1's load rule; item 111's stale rule; the item-134 notes in sections 11 and
     12; the K row's 4.8. WORKER10 made the same fixes in `wip/spec/` (commits `0b4ba20`, `2a107ab`): use them as your model, but
     apply only what is v1.2's, never v1.25's replayer changes.
   - **`v1.2/spec/CHANGES-v1.2.md` (E4):** add entries for DECISIONS-v1.2 sections 45-49.
   - **The cleanups (E7, E8; `wip/planning/CLEANUP.md` CL1, CL2):** the replayer's queue ports, one 1 MB part each (1 code, 2 read
     log, 3 write log at 0x300000, 4 expansion at 0x400000) in spec 4.19.10; the decode table's organization note (256 × 128).
3. **The simulator and software in line with CL1**, in `v1.2/`: meshsim's replayer model, `replay_ctl.c`, recipe 26 and the book
   (`v1.2/sw/recipes/`, `gen_book.py`, `check_book.py`), `v1.2/meshsim`'s tests. WORKER11 is making the same change in `wip/`'s
   v1.25 replayer; look at its commit when it lands, but remember v1.2's replayer predates v1.25's (old opcode numbering and
   tables), so port the address change, not the rest.
4. **Re-verification on the Quest runner** (`v1.2/meshsim/tools/run_quick.sh`, `run_full.sh`, plus the sort at full size with a
   rebuilt kit): every suite and the sort pass on `v1.2/` as changed. Claim task numbers in the Quest repository's `NUMBERS.md`
   first; smoke-test kits locally; end task scripts with an explicit `exit` before any embedded kit.
5. **`v1.2/CHANGES-v1.2.1.md`, the changelog from `v1.2-tony` to v1.2.1:** every change, grouped by spec section, each with
   whether it **affects the RTL** (yes / no / note only), its decision or erratum, and a one-line summary; at the top, the exact
   commands for the diff (the paths differ: `v1.2_wip/spec/` in `v1.2-tony`, `v1.2/spec/` in `v1.2.1`, e.g.
   `git diff v1.2-tony:v1.2_wip/spec/machine-spec.md v1.2.1:v1.2/spec/machine-spec.md`).
6. **`v1.2/ERRATA.md`:** mark E1-E8 as fixed in v1.2.1; keep the file for errata found after v1.2.1.

INPUTS
- `v1.2/` and the tag `v1.2-tony`; `v1.2/ERRATA.md`; `wip/planning/CLEANUP.md`; `wip/planning/DECISIONS-v1.2.md` (sections 45-49);
  `wip/planning/sessions/WORKER4-REPORT.md` (how the simulator, its suites and the runner kits work).

RULES
- Apply what's listed; don't redesign. Anything that looks wrong beyond E1-E8 goes in `WORKER15/QUESTIONS.md` with your proposed
  answer (it may become E9 and later).
- **Keep the last few tool calls of every turn for committing and pushing**, and end each turn with a clean `git status`.
  Background jobs don't survive a turn: long runs finish within one or go to the runner. Keep `WORKER15/NOTES.md` current.

DELIVERABLES
- The branch `v1.2.1`: `v1.2/` updated (spec, CHANGES-v1.2, the simulator, software and recipes), verified on the runner.
- `v1.2/CHANGES-v1.2.1.md`, the updated `v1.2/ERRATA.md`, and `WORKER15/REPORT.md` (what changed, the runner results, anything
  left).
