# The v1.37 build session (updater_wip): the simulator, the firmware and the tests, to the specification

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for
proving the Jolt zkVM. A proof is cut into segments; each ASIC proves one. Before it can, the host sends it the
data that builds its memory images, over a link of packets along a chain of ASICs. The master tile's firmware
takes that data in, the Updater builds the images in LPDDR, and the Tracer runs on them through the cache.

**Where things stand.** v1.37 is made in the order every version was: the specification, then the simulator,
then the firmware, then the tests (owner). **The specification is written, reviewed and accepted:**
`v1.37_wip/spec/`. The simulator and firmware in `v1.37_wip/` were built before it, piece by piece, from design
notes. Most of what the specification describes exists and works. Some of it exists and has never been joined,
some is built to older numbers, and some is not built.

**Your part is to bring the simulator, then the firmware, then the tests, to the specification,** and you are
the integrator while you do: you merge into `main` and queue the runner's tasks yourself.

## The authority

1. **`v1.37_wip/spec/` is the authority.** Read `spec/README.md` for which document holds what. The
   simulator models what it says, at the register, packet and cycle level, as v1.36's simulator models v1.36's.
2. **The worklist is `updater_wip/planning/sessions/SPEC/FOR-THE-SIMULATOR.md`:** every place the
   specification's writer found where `main` differs from it, by section, with the file and line. Each row ends
   closed, with the commit that closed it. You will find more; add them.
3. **Where the specification is wrong, unclear or silent, that is a question for the owner,** not a choice of
   yours. Keep them in `sessions/INTEGRATOR/FOR-THE-SPEC.md`, ask the ones that hold you up one at a time, and
   do not edit `spec/` without the owner's word.
4. **The design notes in `v1.4_wip/planning/` are history.** They say how each decision was reached. Where
   they and the specification differ, the specification wins.

## What v1.37's machine is (owner)

| part | in the simulator |
|---|---|
| the cache's controller, the AXI arbiter | **the state machines,** as 23 has them. They are on `main` (`cache_ctl.*`, `axi_arbiter.*`), behind a setting |
| the Updater's controller | **the state machine,** as 17.10 has it. On `main` (`updater_ctl.h`), behind a setting, and never yet joined to the real cache and arbiter |
| LPDDR | a model (23.12) |
| Ingress/Egress and the UART | **a black box behind its real registers:** what 17.6 says at its registers, its word port, its region and its link, without modelling its insides as hardware. Its link is WORKER8's link end, which is the specification's (chapter 28) |
| the master tile | **its real firmware,** to chapter 30 |
| the memory tile | nothing of v1.37's proof runs on it. The RW engine is v1.36's, moved to peripheral 2 |
| the chain | the chip has `link_up`, `link_down` and `chain_next` (3, 18). Run as DRIVER ↔ ASIC0 ↔ ASIC1, three processes |

**The state machines that are built are the basis** (owner, 2026-11-18). The cache's controller and the
arbiter (`cache_ctl.*`, `axi_arbiter.*`, `cache_system.h`), the Updater's controller (`updater_ctl.h`,
`updater_top.h`) and the link end (`updater_wip/link/`) are the modules of v1.37's simulator. **Do not write
any of them again from the specification.** The specification was written from them and checked against them
by their authors. Your work on them is to join them, to wire them into the chip, and to make the few changes
the worklist names.

**The specified machine is the machine.** When a stage is done, what it built is on by default and is what the
tests run. A black box that a state machine replaces (the Updater's, v1.36's cache model) stays only where a
test compares against it. Say in your plan how each setting ends.

## Read first

1. The top-level `README.md`; `updater_wip/README.md`; `v1.37_wip/RUNNING-v1.37.md` and `CHANGES-v1.37.md`.
2. **`v1.37_wip/spec/`:** `README.md`, `OVERVIEW.md`, `GUIDELINES.md`, and **`CHANGES-v1.36-to-v1.37.md`,** the
   guide to what changed, unit by unit. Then each chapter as you reach its stage.
3. **`sessions/SPEC/FOR-THE-SIMULATOR.md`** in full, and `sessions/SPEC/NOTES.md` sections 8 to 11 (how each
   question ended).
4. `updater_wip/planning/MILESTONE-v1.37.md`: the picture of the chain, the driver, and how the whole is
   checked.
5. In `updater_wip/planning/sessions/`: `WORKER9/PROMPT.md` from "PIECE 2" (the joint, in detail) and
   `WORKER9/NOTES.md`; `WORKER6-REPORT.md` ("What the integrator does to join it"); `WORKER5-REPORT.md`;
   `WORKER7-REPORT.md`; the four `SPEC/REVIEW-WORKERn-ANSWER.md`, which say where each author's code differs.
6. `updater_wip/link/README.md` ("Using the link end") and `hostlink.h`; `updater_wip/wire/README.md` and
   `sessions/WORKER10/REPORT.md` sections 4 and 5.

## The stages

Each stage ends with its checks passing, its rows of the worklist closed, a commit, a merge into `main`, and a
few lines to the owner. Then go on to the next without waiting.

**The simulator:**

### Stage 1: the memory side (17.10, 17.11, 23, 17.7)

- **The joint.** The Updater's controller on the arbiter's client port 1 and the cache's look-up port 1, one
  LPDDR for all, with the Tracer on the same cache. This is WORKER9's second piece, never started: its brief
  from "PIECE 2" to "THE FIGURES" says what to build and what the joined machine must show. Each disagreement
  between two pieces is a finding: which was wrong against which section, and the fix.
- **The memory system as the memory tile's peripheral 1** (17.11): its window, `ERRORS` [0], its error on the
  tile's error line. The RW engine to peripheral 2 and to the arbiter's client 2, on the one LPDDR.
- **The reset** reaches the cache, the arbiter and the LPDDR model (23.6.10, 23.11).
- **The Updater's rows:** `free_page_credit` at `EVENTS` [2], in every place at once; a bad address and the two
  limits as UB that stops the simulator.
- **Checked:** the 37 proof runs on the joined machine, every image equal to gold's, `PAGE_COUNT` 0 at every
  `done`, the Tracer's last record right, no error bit, no stale line; again with LPDDR's timing varied; the
  Updater and the Tracer shown to overlap on the cache; planted bugs at the joints, each caught.

### Stage 2: the router (16)

- The line between the special tiles on east and west, with the ring's rule and no flow control; every special
  tile's DMA on the south input.
- **Checked:** the master tile's traffic to the tracer tile goes by the line; the router's own tests; the proof
  runs. Cycles move here, on purpose.

### Stage 3: Ingress/Egress at its registers (17.6)

- The packed numbers; v1.36's host interface out, with its tests and the images that use it; the UART as
  redesigned; the queues at `INGRESS_QUEUE_WORDS` 1,024, a parameter; words landing one a cycle; the DMA on
  south; `PARAMS.version` 1.
- **Egress:** the outgoing queue in its region, `EGRESS_COUNT`, `egress_credit`, `egress_error`.
- The firmware's header and images move with the numbers, so that everything still runs. What the firmware
  newly does comes in stage 5.
- **Checked:** every row of 17.6's tables by a test at the registers; the UB of invariant 35; the proof runs.

### Stage 4: the link and the chain (28, 17.6.3, 17.6.8, 18)

- **The black box's link becomes WORKER8's `Node`:** a link end up, a link end down, the forwarding buffers,
  `chain_next`. A damaged link packet is no error; `link_error` and `format_error` as 17.6 has them; the reset
  restarts the link.
- **The chip's input is `link_up`,** a byte each way a cycle, in place of a packet file. In one process the
  host's side is WORKER8's `End`, fed with gold's frames.
- **Gold's packetizer, reader and vectors** brought to chapter 28, so that gold's link packets and the link
  end's agree byte for byte.
- **Checked with no chips:** a host end and two or three black boxes back to back. Then one chip behind a host
  end: the proof runs as before. The milestone's planted bugs in the chain, each caught.

**The firmware:**

### Stage 5: chapter 30

- The store to `UART_CONTROL` in every image that prints; the tracer tile's result carrying its last record to
  the master tile; **the reply frame** through Egress at the segment's end (29.14, 30.7).
- **Checked:** chapter 30 section by section against `master.c` and `tracer_ctl.c`; the reply whole, in order
  and equal to what gold's files give, in the words 29.14 says can be compared; planted bugs in the firmware,
  each caught.

**The tests:**

### Stage 6: the driver and the three processes

- **The driver** (`MILESTONE-v1.37.md`, "The driver"): a process that reads gold's data, sends it through the
  host's `End`, sorts what comes back by hops, and checks each ASIC's reply against gold. Tested first against
  stand-in ASICs made of `Node`, on WORKER10's wire under its launcher.
- **DRIVER ↔ ASIC0 ↔ ASIC1:** the chip's simulation takes `--clients N --client k` and calls the wire's
  `exchange` on its link ends every cycle.
- **Checked:** every row of `MILESTONE-v1.37.md`, "How the milestone is checked". On the small inputs where you
  are, and 2²⁹ on the runner.

### Stage 7: closing, with the owner

- Every row of the worklist closed or made an open item with the owner's word.
- The invariants of chapter 20 each a check in the simulator; chapter 21's timing rows measured and, where the
  specification's figure is wrong, reported.
- `RUNNING-v1.37.md` and `CHANGES-v1.37.md` brought up to date. What is frozen, and when, is the owner's word.

## Rules

- **Propose one plan for the whole of it first,** in `sessions/INTEGRATOR/NOTES.md`, and wait for the owner's
  go-ahead: for each stage what you will build, how each setting ends, how you will check it, the planted bugs,
  and what you expect to go wrong. **After the go-ahead, work through the stages without asking again.**
- **Stop and ask only for what is the owner's to decide:** a question about the specification that holds you
  up; a change to the link's code or the wire's; a check that cannot be made to pass without loosening it;
  anything that would change hardware Tony has built. Ask one question at a time.
- **Keep replies to the owner short.** A few lines at the end of a stage: what passed, what was found, what is
  next.
- **Branch `v137-build`,** from `main`. Merge it into `main` yourself at the end of each stage, when the stage's
  checks pass (`git merge --no-ff`, then pull without rebasing, and push). Merge `main` in often; never rebase.
  Frozen folders (`v1.36/` and earlier) are never edited.
- **Exactness is the requirement:** a difference in a check is a bug, never a tolerance.
- **Reference cycles move only on purpose.** Each stage that changes timing (the joint, the line, the queues,
  the link) moves them once: say why, show that nothing else moved, and record the new ones.
- **The two state machines and the link end are specified from their code and reviewed against it.** Change
  them only where a row of the worklist or a finding says so, and keep their own tests passing: `make check` in
  `updater_wip/link/`, the cache's, the arbiter's and the Updater's unit tests.
- **`updater_wip/wire/` is test equipment and is used as it is.**
- The firmware images are built with the pinned xPack toolchain and committed with their hashes
  (`RUNNING-v1.37.md`).
- **Never write a token into a file.** Never `pkill -f`; use `pgrep` and kill by PID.
- Commit and push after each completed piece, and end each turn with a clean `git status`.

**The runner** (the Quest repository; `PIPELINE.md` there):

- 16 CPUs, 62 GB, a limit of 3 hours 30 minutes a task. `updater_wip/tasks/v137-check/` is the task to extend.
- Claim a number in `NUMBERS.md` and push the claim before you write the task: pull and read the file again
  just before. Never push to Quest while the runner has result commits it has not pushed.
- A task that is killed is run again, up to three times, unless it is first moved to `tasks/hold/`.
- Tasks 238 to 242 in `tasks/hold/` are WORKER8's long runs of the link. They are the owner's to release.
- **Keep the runner's tasks short and to the point** (owner). A test that loads every core to slow the others
  can take hours there and show nothing new.

## Not your work

A new design for anything the specification states; the link's protocol and its searches; error recovery after
a stop (item 181); flow control on the special tiles' line (item 182); the file system; the foreground stream's
frames but the reply; the RW engine beyond its move.

## Deliverables

- **`v1.37_wip/`:** the simulator, the firmware and the tests, to the specification.
- **`sessions/SPEC/FOR-THE-SIMULATOR.md`:** every row closed, with its commit.
- **`sessions/INTEGRATOR/NOTES.md`:** the plan, and a "Choices" list of what you decided that the
  specification did not say.
- **`sessions/INTEGRATOR/REPORT.md`,** added to at each stage: what was built; each disagreement found, with
  the section; each check's result; the reference cycles as they moved.
- **`sessions/INTEGRATOR/FOR-THE-SPEC.md`:** where the specification is wrong, unclear or silent.
