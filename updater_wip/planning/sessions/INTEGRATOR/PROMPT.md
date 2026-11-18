# The v1.37 integrator (updater_wip): from where it stands to a frozen v1.37

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for
proving the Jolt zkVM. A proof is cut into segments; each ASIC proves one. Before it can, the host sends it the
data that builds its memory images, over a link of packets. In `v1.37_wip/` the whole chip runs from the host's
data: the master tile's firmware drives the Updater, which builds the images in LPDDR, and the Tracer runs on
them through the cache.

**The goal** (owner). v1.37 is:

| part | in v1.37 |
|---|---|
| the cache's controller, the Updater's controller | **full simulation of the state machines** |
| AXI: the arbiter and LPDDR | a model |
| Ingress/Egress and the UART (IEU) | **a black box behind its real registers** |
| the master tile | **its real firmware** |
| the memory tile | nothing runs on it; no RW engine |
| at the end of a segment | the master writes a reply frame out, with data that shows the segment ran |
| the daisy chain | DRIVER ↔ ASIC0 ↔ ASIC1, three processes; ASIC0 runs segment 0 and ASIC1 segment 1 |

The plan, its picture and its checks are `updater_wip/planning/MILESTONE-v1.37.md`.

**Your part is all of what is left,** and the integrator's job with it. Until now each piece was built by a
worker on a branch, to a brief, and merged by the design session. The pieces are built. What is left is joining
them, and the owner wants it done in one session, quickly: **you do the work yourself, stage by stage, and you
merge into `main` and queue the runner's tasks yourself.** The first four rows of the table are the owner's
first goal. Reach them first (stage 1), then the reply and the chain.

## Where it stands (2026-11-18)

| piece | by | state |
|---|---|---|
| the cache's controller, the AXI arbiter, the LPDDR model | WORKER5 | on `main`, behind `MachConfig::cache_ctl`. The Tracer runs on it in the 37 proof runs |
| the Updater's controller | WORKER6 | on `main`, behind `--updater-ctl`. It runs on **a test LPDDR and a stand-in cache of its own,** not yet on the real ones |
| the forward count, the word port, the firmware that wakes once a frame; the header with 18-bit numbers and hops | WORKER7 | on `main` |
| the two joined on the data side: the shim gone, the controller fed by Ingress's forwarder | WORKER9, step 1 | on `main` (Quests 222 to 226) |
| **the joint:** the Updater's controller on the real arbiter and the real cache | WORKER9, step 2 | **not started.** Branch `updater-w9` holds nothing beyond `main`. Its brief and plan are `sessions/WORKER9/PROMPT.md` (from "PIECE 2") and `NOTES.md` |
| the link's protocol on its own: a link end, a chain node, the host's end | WORKER8 | built and tested, on branch **`updater-w8`** at `5c8ad13a`, **not merged.** `updater_wip/link/`; the one header to use is `hostlink.h` (`End`, `Node`) |
| the wire between processes: shared memory, a sync every 64 cycles, the launcher | WORKER10 | built and tested, on branch **`updater-w10`** at `2a9674b9`, **not merged.** `updater_wip/wire/` |
| the IEU black box | WORKER3, 4, 7 | on `main`: fed from a packet file; nothing goes back to the host; no Egress; one ASIC; a bad packet is fatal |
| gold: the frames, the packetizer, the vectors | WORKER1, 2, 7 | on `main`, with the header as WORKER7 built it: numbers for each stream, no mask |
| the driver, Egress, the reply frame, the chain in the black box | | **not started** |

- **The design documents are up to date as of today** (`v1.4_wip/planning/DESIGN-NOTES-2026-11-16.md`, section
  34). Where v1.37's code is behind them, `INGRESS-INTERFACE.md` and `UPDATER-INTERFACE.md` say so under
  "In v1.37".
- Both unmerged branches touch only their own folders, so each is a plain merge.

## Read first

1. The top-level `README.md`; `updater_wip/README.md`; **`updater_wip/planning/MILESTONE-v1.37.md`** in full.
2. `v1.37_wip/RUNNING-v1.37.md` and `CHANGES-v1.37.md`.
3. In `v1.4_wip/planning/`: **`INGRESS-INTERFACE.md`**, `UPDATER-INTERFACE.md`, `CACHE-INTERFACE.md`,
   `AXI-INTERFACE.md` and `HOST-FRAMES.md` in full; `DESIGN-NOTES-2026-11-16.md` sections 21, 22 and 34.
   - `HOST-LINK.md` is long. Read "Where this document stands", "A packet", "The chain, and the hops field",
     "Holding the host back" and "The link as hardware". The rest is what WORKER8's code implements; you use
     that code and do not write the protocol again.
4. In `updater_wip/planning/sessions/`: `WORKER9/PROMPT.md` and `NOTES.md`; `WORKER6-REPORT.md` ("What the
   integrator does to join it"); `WORKER5-REPORT.md`; `WORKER7-REPORT.md`.
5. On `updater-w8`: `updater_wip/link/README.md` ("Using the link end": the two examples, what a user promises
   and what it may rely on) and `hostlink.h`; `sessions/WORKER8/REPORT.md`.
6. On `updater-w10`: `updater_wip/wire/README.md` (the loop a process runs) and `sessions/WORKER10/REPORT.md`,
   sections 4 and 5.

## The stages

Each stage ends with its checks passing, a commit, a merge into `main`, and a report to the owner of a few
lines. Then go on to the next without waiting.

### Stage 0: the two merges

- Merge `updater-w8` and `updater-w10` into `main` (`git merge --no-ff`). Run `make check` in `updater_wip/link/`
  and the wire's own checks, without its matrix under load, to see that they pass where you are.
- WORKER8's session may still push reports of runner results to its branch. Merge again when it does.

### Stage 1: the joint. Both state machines on, in the whole chip

**This is the owner's first goal.** It is WORKER9's brief from "PIECE 2: THE CONTROLLER ON THE REAL ARBITER AND
THE REAL CACHE" to "THE FIGURES", and you take it over as written there:

- the Updater's controller on the arbiter's client port 1 and the cache's look-up port 1, one LPDDR model for
  the Updater and the cache alike;
- a setting for the joined machine, and a statement of which combinations of settings run;
- **the 37 proof runs with both controllers on:** every image equal to gold's, `PAGE_COUNT` 0 at every `done`,
  the Tracer's last record equal to record *s* + 1, no error bit, no stale line; again with the LPDDR model's
  timing varied;
- the Updater and the Tracer on the cache at once, shown to overlap;
- planted bugs at the joints, each caught;
- with every setting off, `./run_tests.sh all --check` and `make check` as before.

**Each disagreement between two pieces is a finding:** the case, which piece was wrong against which document
or which document was unclear, and the fix. The joint is the first test of `UPDATER-INTERFACE.md`,
`CACHE-INTERFACE.md` and `AXI-INTERFACE.md` against each other.

Of that brief's figures, take the ones that cost little. The run with two proofs (the Updater building the next
proof's images while the Tracer runs) is wanted by the owner if it is cheap; if it is not, say what it would
take and go on.

### Stage 2: Egress and the reply frame, on one chip

- **Egress in the black box,** as `INGRESS-INTERFACE.md`, "Egress: sending to the host", has it: the outgoing
  queue, `EGRESS_COUNT`, the credit, `egress_error`.
- **The master's firmware sends a reply frame at the end of its segment** (owner: "something that shows that it
  ran"). The frame is `MILESTONE-v1.37.md`'s, "The reply frame": 72 words, the Tracer's end-state record with a
  few counts. The record must equal gold's record *s* + 1, so it can be checked exactly.
- Checked on one chip, with the harness collecting what Egress sends: the frame whole, in order, equal to what
  gold's files give, on the proof runs; planted bugs in the firmware's sending, each caught.

### Stage 3: the link end and the chain in the black box

- **The link's part of the black box becomes WORKER8's `Node`:** a link end towards the host and one away from
  it, the forwarding buffers, the pin. The black box keeps what is the chip's: the two queues, the DMA, the
  forward count, the service flags, Egress's queue, the registers.
- With it come the things the link end's rules need: **the queues back to 16 KB,** "stop" from a queue that is
  half full, and the error the core sees when a link starts again (`INGRESS-INTERFACE.md`, "Errors").
- **Tested with no chips and one process:** a host `End`, and two or three black boxes wired back to back.
  Packets from the host reach every one; replies come back sorted by hops; a stalled background does not hold
  the foreground. The milestone's planted bugs in the chain, each caught.

### Stage 4: the driver

- A process of its own (`MILESTONE-v1.37.md`, "The driver"): it reads gold's data, sends it through WORKER8's
  host `End`, collects what comes back, sorts it by hops, and checks each ASIC's reply against gold. "A reply
  does not come" is counted in cycles.
- **Tested before any chip:** the driver and stand-in ASICs made of `Node`, as processes on WORKER10's wire
  under its launcher.

### Stage 5: DRIVER ↔ ASIC0 ↔ ASIC1

- The chip's simulation takes `--clients N --client k`, and calls the wire's `exchange` on its link ends every
  cycle. The three run under `wire-launch`.
- **The milestone's checks** (`MILESTONE-v1.37.md`, "How the milestone is checked"), every row: the images, the
  trace, the replies, the same to the cycle, a chain of one, both controllers on, the planted bugs, and what
  does not change. On the small inputs where you are, and 2²⁹ on the runner.

### Stage 6: the freeze, with the owner

- Gold's packetizer, reader and vectors brought to the final protocol, so that the two golds agree
  (`MILESTONE-v1.37.md`, step 2c).
- `RUNNING-v1.37.md` and `CHANGES-v1.37.md` brought up to date; the design brought into v1.37's specification.
- What is frozen, and when, is the owner's word.

## Decided, so that you are not held up

| question | for this milestone |
|---|---|
| the reply frame's fields | as `MILESTONE-v1.37.md` proposes. The owner does not mind what the data is |
| v1.36's old stream words, and the UART as redesigned | **left as built.** Not part of the chain |
| the register and bit numbers | the design has them packed, into the places the old stream words leave. Since those words stay, **v1.37 keeps its numbers from before the packing,** and the new ones go beside them: `EGRESS_COUNT` at word 83, `egress_credit` at `EVENTS` [10]. Say in your plan where the new error bits go |
| the data on the link | the chain runs at a byte a cycle, as WORKER8's link end does. `--link-rate` stays for the file-fed path |
| the file-fed path | kept, as it is, for the single chip and for comparison. Its packet file has the header from before the final protocol, which a link end would refuse; so with a link end the data comes from the driver, which cuts gold's frames into packets itself. A frame still ends at the end of a packet |
| whether the two controllers are on by default | off until stage 5 passes; then ask the owner |
| the foreground stream towards the chip | nothing is defined, and the milestone needs nothing of it |
| the link's two open questions (`HOST-LINK.md`, Open, both †) | leave the model as WORKER8 built it |

## Rules

- **Propose one plan for the whole of it first,** in `sessions/INTEGRATOR/NOTES.md`, and wait for the owner's
  go-ahead: for each stage what you will build, how you will check it, the planted bugs, and what you expect to
  go wrong. **After the go-ahead, work through the stages without asking again.**
- **Stop and ask only for what is the owner's to decide:** a change to a design document; a change to the
  link's protocol or to the wire; a check that cannot be made to pass without loosening it; anything that would
  change hardware Tony has built (that is a request to him, in `RTL-CHANGE-REQUESTS.md`, and the owner's to
  make). Ask one question at a time.
- **Keep replies to the owner short.** A few lines at the end of a stage: what passed, what was found, what is
  next.
- **Branch `v137-int`,** from `main`. Merge it into `main` yourself at the end of each stage, when the stage's
  checks pass (`git merge --no-ff`, then pull without rebasing, and push). Merge `main` in often; never rebase.
  Frozen folders are never edited.
- **Exactness is the requirement:** a difference in a check is a bug, never a tolerance.
- **Reference cycles move, and only on purpose.** With every setting off the proof runs' cycles are Quest 220's,
  to the cycle, until you change the queues' size or the path the data takes. When they must move, move them
  once, say why, and record the new ones.
- **`updater_wip/link/` and `updater_wip/wire/` are used as they are.** If one of them has to change, that is a
  finding: say so first, and its own checks must still pass.
- **The design documents in `v1.4_wip/planning/` are not yours to edit.** Keep what you would change in them as
  a list, `sessions/INTEGRATOR/FOR-THE-DOCUMENTS.md`, for the owner and the design session. The design session
  that wrote them can be asked through the owner.
- The master tile's firmware images are built with the pinned xPack toolchain and committed with their hashes
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

The link's protocol and its searches (WORKER8); the wire's design (WORKER10); the RW engine on the arbiter; the
old stream words and the new UART; the foreground stream's frames; anything in v1.4 beyond what v1.37 needs.

## Deliverables

- **`v1.37_wip/`:** the joined machine; Egress; the chain in the black box; `--clients` and `--client`; the
  driver; the tests, the planted bugs, the runner's task; the running guide and `CHANGES-v1.37.md`.
- **`sessions/INTEGRATOR/NOTES.md`:** the plan, and a "Choices" list of what you decided that no document said.
- **`sessions/INTEGRATOR/REPORT.md`,** added to at each stage: what was joined; each disagreement found, with
  the document that let it; each check's result; the figures.
- **`sessions/INTEGRATOR/FOR-THE-DOCUMENTS.md`:** what you would change in the design documents.
