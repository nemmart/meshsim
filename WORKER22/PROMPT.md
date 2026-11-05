# Worker 22: the Tracer's bring-up in meshsim (B1)

You are building the **Tracer** into meshsim, the cycle-level simulator of an ASIC for proving the Jolt zkVM: C&C 3's
peripheral, the RV64 row machine that executes the guest program and writes each window's run log, which the tiles' replayers
(spec 4.19, already in meshsim) turn back into Jolt's trace rows. **Build it from the spec: 4.21 The Tracer** in
`wip/spec/machine-spec.md` (with 4.17, 8.16 and section 13's items 144-157), which states every layout, register, bit and
timing parameter. `wip/planning/DECISIONS-v1.3.md` sections 1-25 are the record behind it. Also read 4.19, the replayer's
model and `replay_ctl.c` and recipe 26 (how a controller drives a peripheral and feeds tiles), and
`wip/planning/sessions/WORKER20-REPORT.md` and `WORKER25-REPORT.md` (the replayer's acceptance, which this extends).

**You are also 4.21's review.** Wherever the text is silent, ambiguous or contradicts itself, don't settle it in the model:
log it in `WORKER22/SPEC-QUESTIONS.md` (the section, the question, what you'd propose) and ask. The owner rules, and the spec
is fixed to match.

REPOSITORIES
- **meshsim**, https://github.com/nemmart/meshsim: code on your own branch, `v1.3-tracer`, made from `main`; the integrator
  merges. Notes and report in `WORKER22/` on `main`.
- **Inputs, as data:** section 16's segment files, written by gold (`wip/gold/`, the Java emulator, our reference) or
  WORKER16's tool; the guests' ELFs and inputs (jolt-asic-planning `tracer-standalone-main`, now at jolt-cpp 88dca85);
  WORKER9's 2²⁹ deliveries and the runner's persistent data (`~questrunner/data`, PIPELINE.md). The writers are moving to
  v1.3's formats (v1.26.1's record in frame 2, the map key pc − lowest_address in frame 6, frame 7): until they land, adapt
  in your setup tools and say so.
- Clone with the tokens from your user preferences; never write a token into a file.

THE WORK
1. **The Tracer's model** (4.21): the region and record, control (the two pulses, the word-0 write, RUN_CONFIG, words
   32-43, the events and credits), execution through the universal tables and the controller-fed path, advice from the read
   queue, the jump LRU and hash probe, the 16 KB caches, the range comparator and the misalignment rule, the 66-bit log FIFO,
   set 1 and set 2, the output queues and the drain by service requests (done last), every ERRORS bit.
2. **A black-box memory system** (4.21.5, 4.21.9): (root, depth, line) reads and writes over files held as section 20's
   trees, with 4.21.5's fill statuses and 4.21.9's two latencies as parameters, so cycle counts are estimates.
3. **Host-side setup tools:** the guest's `.pages` file from the ELF and the segment's updates (pages not written shared and
   not owned); the jump map (static table plus frame 6's supplement); word 66 and the hash registers from frame 7; the startup
   record from frame 2.
4. **C&C 3's firmware:** load the state, run 64 windows of RUN_CONFIG = 65,536, answer the expand, expansion-credit and advice
   events, serve the four service requests (queue 0 to tiles n+1 and n, the streams to tile n's memory), wait for done, pulse
   run; then the tiles replay.
5. **Acceptance:** the segment replayed on the full chip gives jolt-cpp's rows, window by window, compression on and off; the
   Tracer's streams checked against WORKER9's deliveries where they should agree; small inputs first, then segments of both
   workloads at 2²⁹, on the Quest runner. Report cycles per row and per segment against the 100 ms budget.

RULES
- **Propose a plan first, and wait for the owner's go-ahead**: the model's structure, the memory system's interface and
  parameters, the tools, which segments you'll accept on and how.
- Out of scope: the file system (F1, WORKER23), the memory system's internals, the Updater, the proving stages.
- The quick and full suites and the full-size sort must still pass.
- Quest tasks: claim numbers in the Quest repository's NUMBERS.md first; smoke-test kits locally; an explicit exit before any
  embedded kit. Background jobs don't survive a turn; long runs go to the runner.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean git status.

DELIVERABLES: the Tracer in meshsim on v1.3-tracer, the tools and firmware, the acceptance results, SPEC-QUESTIONS.md,
WORKER22/REPORT.md.
