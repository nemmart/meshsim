# Worker 9 (updater_wip): joining the controllers

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for proving
the Jolt zkVM. In `v1.37_wip/` the whole chip runs from the host's packet file: the master tile's firmware drives
the Updater, which builds the memory images in LPDDR, and the Tracer runs on them through the cache. The milestone
this is heading for is `updater_wip/planning/MILESTONE-v1.37.md`; you are its step 3.

**Three pieces were built apart, by workers who shared no code, and have never met:**

| piece | by | where | checked how |
|---|---|---|---|
| the cache's controller, the AXI arbiter, the LPDDR model | WORKER5 | on `main`, behind `MachConfig::cache_ctl` | the Tracer on it in the 37 proof runs; the Updater a black box beside it |
| the Updater's controller | WORKER6 | on `main`, behind `--updater-ctl` | against a **test LPDDR and a stand-in cache** of its own, with v1.37's segment queue as a **shim** on its data input |
| the forward count: Ingress feeds the Updater over the word port; the firmware wakes once a frame; the link's new header | WORKER7 | on branch **`updater-w7`** at `4844852a`, checked, **not merged** | with the Updater a black box |

**Your part is to join them:** the Updater's real controller, fed by Ingress's forwarder, on the real arbiter and
the real cache, with the Tracer on the same cache, in the whole chip on the proof runs. **This meeting is the test
of the interface documents** the three were built from. Wherever two pieces disagree, find which document let
them, and say so.

READ FIRST: the top-level `README.md`; `updater_wip/README.md` and `planning/MILESTONE-v1.37.md`; in
`v1.4_wip/planning/`: `UPDATER-INTERFACE.md`, `AXI-INTERFACE.md`, `CACHE-INTERFACE.md` and `INGRESS-INTERFACE.md`
in full, and `DESIGN-NOTES-2026-11-16.md` sections 17, 18, 21 and 23; in `updater_wip/planning/sessions/`:
**`WORKER5-REPORT.md`** and `WORKER5-NOTES.md`, **`WORKER6-REPORT.md`** ("What the integrator does to join it") and
`WORKER6-NOTES.md`; on branch `updater-w7`, `updater_wip/planning/sessions/WORKER7/REPORT.md` and `NOTES.md`
(section 4, the wiring); in `v1.37_wip/`: `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, `src/units/axi_port.h`,
`lookup_port.h`, `word_port.h`, `updater_top.h`, `updater_ctl.h`, `segment_shim.h`, `cache_system.h`,
`cache_ctl.h`, `axi_arbiter.h`, `lpddr_model.h`, `periph_slot.*`, `tests/ctl_bench.h`, `tests/updater_view.h`,
`tests/test_updater_ctl.cpp`, `tests/proof_bench.*`, `tests/mach.*`, `run_tests.sh`.

PIECE 1: MERGE `updater-w7`, AND TAKE THE SHIM OUT
- **It is not a plain merge.** WORKER7 took the segment queue, its region part and `segment_credit` out of the
  Updater's black box. WORKER6's bench and view feed the black box through that queue (`tests/ctl_bench.h`,
  `tests/updater_view.h`), and its controller sits behind the shim. Git reports conflicts in
  `tests/proof_bench.cpp` and `tests/proof_run_main.cpp`; the real work is in the files it merges cleanly.
- **What it comes to:**
  - `segment_shim.h` and every line of `updater_top.h` marked `shim` go;
  - `UpdaterTop` gets the two sink methods the slot wires (`port_sink()`, `port_take(t, word)`; WORKER7's notes),
    so that Ingress's forwarder drives the controller as it drives the black box;
  - the bench feeds both Updaters on the word port, and compares them as before;
  - WORKER6's tests, its planted bugs and its runner items follow.
- **Checked:** everything WORKER6 checked and everything WORKER7 checked, on the merged tree: the controller
  alone against gold; against the black box, the stores byte for byte, on gold's fixtures and the random
  scenarios; its 14 planted bugs and WORKER7's 20, each caught; the whole chip on the new firmware with the black
  box, and with the controller on its test LPDDR and stand-in cache (`test_master`, the proof runs).
- With every setting off, the proof runs' cycles are WORKER7's (Quest 220), to the cycle.

PIECE 2: THE CONTROLLER ON THE REAL ARBITER AND THE REAL CACHE
- `UpdaterTop::lpddr` on the arbiter's client port 1; `UpdaterTop::cache` on the cache's look-up port 1; one LPDDR
  model, on the file store's pages, for the Updater and the cache alike.
- **A setting for the joined machine,** off by default: both controllers on. Say what the settings are now and
  which combinations run: both black boxes; the cache's controller alone; the Updater's alone on its test
  pieces; both, joined.
- The test LPDDR and the stand-in cache stay for the controller's own tests.
- **Expect disagreements,** and treat each as a finding: a reply the Updater did not expect; an order of bursts
  the arbiter or the model refuses; a line in the cache that LPDDR no longer agrees with (WORKER5's stale-line
  check stays on in every run); a wait that never ends. For each: the case, which piece was wrong against which
  document, or which document was unclear, and the fix. Fix code on either side; **do not edit the documents or
  the three headers.**

WHAT THE JOINED MACHINE MUST SHOW
- **The 37 proof runs with both controllers on:** every image equal to gold's as its run ended and at the end;
  `PAGE_COUNT` 0 at every `done`; the Tracer's last record equal to record *s* + 1; no error bit; the cache's
  error flag clear; no stale line; no read of LPDDR that nothing wrote; every rule the arbiter and the model
  check.
- **Again with the timing varied:** the LPDDR model's jitter mode, several seeds. The same results.
- **The Updater and the Tracer on the cache at once.** The Updater's last run goes on while the Tracer traces, so
  for the first time two clients' requests overlap. Show that they do, and report how often a request was served
  while a miss waited, and how often one was replayed.
  - The owner: on the real system the Updater is often building the next proof's images while the Tracer runs. If
    a run with two proofs, the second's images built during the first's tracing, is cheap to make, make it; if it
    is not, say what it would take.
- **Planted bugs at the joints,** each caught: the Updater on the wrong client port or look-up port; an update
  sent before its page's write response; the cache answering a look-up from a line an update has since changed;
  the arbiter granting two writes at once; one more of your own for each disagreement you found.
- **With every setting off,** `./run_tests.sh all --check` and `make check` as before.

THE FIGURES
- The Updater's cycles a page by kind (new, copied, patched; with the Tracer's copy), on the real cache and
  arbiter, beside WORKER6's on its test pieces and the black box's.
- A cache line's wait at the arbiter behind the Updater's page bursts: the mean and the longest. This is what
  `TURN_BEATS` and "weighting the turns toward the cache" (`AXI-INTERFACE.md`, Open) wait on.
- The Tracer's cycles a segment with the Updater's last run beside it, against WORKER5's with the black box.
- The whole run to the hand-over, and the master's time awake, against Quest 220's.

RULES
- **Propose your plan first** in `NOTES.md` and wait for the owner's go-ahead before writing code: how piece 1's
  merge is resolved file by file, the wiring of piece 2, the settings, the checks, the planted bugs, what you
  expect to disagree.
- **Branch `updater-w9`,** from `main`. Your notes, questions and report are in
  `updater_wip/planning/sessions/WORKER9/`. The integrator merges into `main`. Frozen folders are never edited.
- **You have `v1.37_wip/` to yourself.** WORKER8 is working in `updater_wip/link/`, on the link's protocol, and
  touches nothing of yours. Merge `main` in often (never rebase).
- The master tile's firmware is WORKER7's and should not need changing; if it does, say why first. Its images
  are built with the pinned xPack toolchain and committed with their hashes.
- Gold and the packetizer are not changed.
- Exactness is the requirement: a difference in a check is a bug, never a tolerance.
- Long runs go to the runner (the Quest repository; `PIPELINE.md` there); `updater_wip/tasks/v137-check/` is the
  task to extend. If your session cannot attach Quest, do not work around it: commit, push, and say which items
  to queue; the integrator queues them.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never write
  a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.
- Keep replies to the owner short.

NOT YOUR WORK: the link's protocol (WORKER8); Egress, the chain, the reply frame and the three processes (the
milestone's later steps); the RW engine on the arbiter; the specification.

DELIVERABLES
- `v1.37_wip/`: the merged tree with the shim gone; the joined machine behind its setting; the tests, the
  planted bugs, the runner's items; the running guide and `CHANGES-v1.37.md` brought up to date.
- `WORKER9/NOTES.md`: the plan, how the merge was resolved, the settings, the "Choices" list.
- `WORKER9/REPORT.md`: what was joined; **each disagreement found, with the document that let it**; each check's
  result; the figures; and what you would change in `UPDATER-INTERFACE.md`, `AXI-INTERFACE.md`,
  `CACHE-INTERFACE.md` and the three headers, as lists for the owner.
