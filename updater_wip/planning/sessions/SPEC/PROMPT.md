# The v1.37 specification session (updater_wip)

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for
proving the Jolt zkVM. A proof is cut into segments; each ASIC proves one. Before it can, the host sends it the
data that builds its memory images, over a link of packets along a chain of ASICs. The master tile's firmware
takes that data in, the Updater builds the images in LPDDR, and the Tracer runs on them through the cache.

**Where things stand.** Every earlier version was made in one order: the specification, then the simulator, then
the firmware, then the tests. For v1.37 the order was turned round. Over three days the design was worked out
with the owner in planning documents, and workers built pieces of the simulator and firmware straight from
them. **v1.37's specification folder is still v1.36's, unchanged.** The owner has now ruled that v1.37 goes the
tried way: **the specification for the full system first.** The simulator is then brought to it, then the
firmware, then the tests.

**Your part is that specification:** `v1.37_wip/spec/`, brought from v1.36's to v1.37's, complete enough that
someone who has read nothing else could build the simulator, write the firmware and write the RTL from it.

## What v1.37 is (owner)

| part | |
|---|---|
| the cache's controller and the Updater's controller | state machines, specified to the cycle where the interfaces need it |
| AXI: the arbiter and LPDDR | specified as interfaces; LPDDR itself a model |
| Ingress/Egress and the UART | the master tile's peripheral 1, behind its registers |
| the Updater | the master tile's peripheral 2 |
| the host link | packets, numbering, sending again, recovery, the restart, "stop", the chain of ASICs |
| the frames | what the host sends, and the reply the master sends at the end of a segment |
| the master tile's firmware | what it must do with each frame, and at a segment's end |
| the memory tile | nothing new runs on it; the RW engine is v1.36's, unchanged |

## The sources

**The design is in `v1.4_wip/planning/`.** These are working documents: they hold the decisions, how each was
reached, what was measured, and remarks on what the simulator has today. The specification states the design
and none of the rest.

| source | holds |
|---|---|
| `INGRESS-INTERFACE.md` | Ingress/Egress as the master tile's core sees it: the registers, the forward count, the word port, the service flags, the DMA, Egress, the UART, reset, errors |
| `HOST-LINK.md` | the link: the packet and its header, the two checks, the mask, the CRC units, a bad packet, recovery, the restart, "stop", what a sender keeps, the chain, the link as hardware |
| `HOST-FRAMES.md` | the frames inside the packets |
| `UPDATER-INTERFACE.md` | the Updater: its registers, its runs, its data input, the free page queue |
| `CACHE-INTERFACE.md` | the cache: look-up and update, what it guarantees, what it asks of its clients, the fill port |
| `AXI-INTERFACE.md` | the arbiter, its clients, LPDDR |
| `RTL-CHANGE-REQUESTS.md` | what is asked of Tony, the RTL engineer, and what is agreed |
| `DESIGN-NOTES-2026-11-14.md`, `-15.md`, `-16.md` | how each decision was reached; `-16.md` section 34 is the latest summary |
| `updater_wip/planning/MILESTONE-v1.37.md` | the reply frame as proposed; the picture of the chain |
| `updater_wip/planning/sessions/` | each worker's report and notes: what it built, measured and found |
| branch `updater-w8`: `updater_wip/link/` and `sessions/WORKER8/` | the link's protocol as a model that passes every search and campaign, `REPORT.md`, and **`HARDWARE.md`**: every register of a link end with its width |
| branch `updater-w10`: `updater_wip/wire/` | the wire between simulator processes: not hardware, not specified |
| `v1.37_wip/src/`, `v1.37_wip/fw/` | what is built. Evidence of what works, not the authority |

**Which wins when two disagree:**

1. **The owner's ruling,** wherever it is recorded, the latest first. `(owner)` marks them.
2. **For the link's protocol, WORKER8's model** where `HOST-LINK.md` is silent or unclear: it is the only form of
   the protocol that has been searched. Where the two plainly differ, ask.
3. **The design document** over the simulator. Where a document says "as built" or "in v1.37", that is the
   simulator's state on a day, not the design.
4. **For hardware Tony has built** (v1.36's units, the Tracer, the replayer, the tile), **the RTL is the truth,**
   as `spec/GUIDELINES.md` section 1 has it. A change to one of those is a request to him, not a line of
   specification: state it as designed, and mark that it waits on him. Units he has only pencilled in are ours
   to specify.

## What to produce

**Read `v1.37_wip/spec/README.md` and `GUIDELINES.md` first.** They say which document holds what and how the
text is meant. Keep that structure, those terms and that voice.

1. **`machine-spec.md`,** brought to v1.37. Where each thing goes is yours to propose in the outline. To start
   from:
   - 17.6, Ingress/Egress: rewritten. The old host interface's words are gone, the UART is the redesigned one,
     and the register and bit numbers are the packed ones.
   - The Updater: a new peripheral section, with the Jolt-specific ones in `jolt/`.
   - Chapter 23, the memory system, which v1.36 has as deferred: now the real thing. The cache's controller,
     the arbiter and LPDDR, with their interfaces.
   - The host link and the frames: chapters or documents of their own.
   - The chapters that gather things from all the others: 18 (nets and reset: the pin that says another ASIC
     follows; what a reset reaches), 19 (parameters), 20 (invariants), 21 (timing), 24 (budgets: the link's
     flip-flops and memory), 26 (open items).
2. **The master tile's firmware, as a contract:** what it does at boot, with each frame, with the Updater's
   runs and the free page queue, at a segment's end (the reply frame), and on an error.
3. **`OVERVIEW.md`** brought up to date.
4. **`CHANGES-v1.36-to-v1.37.md`:** the guide from v1.36, as `CHANGES-v1.35-to-v1.36.md` is from v1.35: what
   changed unit by unit, the name table, and where v1.36's sections went.
5. **`sessions/SPEC/FOR-THE-SIMULATOR.md`:** every place where v1.37's simulator or firmware, as it is on
   `main`, differs from the specification you have written. This is the worklist for the session that builds
   next. The sources already name some: the queues' size, the register numbers, the old stream words, the UART,
   the packet's header, the mask, a bad packet being fatal, no Egress, no chain.

## The daggers

**The planning documents mark with † what was proposed and never ruled on. A specification cannot say
"proposed".** Each one ends in one of three ways:

- **ruled on by the owner,** and stated;
- **an open item** in chapter 26, numbered, with what depends on it;
- **waiting on Tony,** if it changes hardware he has built.

How to get there:

- **List every dagger first,** in `sessions/SPEC/NOTES.md`, by document, each with your recommendation and what
  it would cost to change later.
- **Bring them to the owner one at a time,** those that a program or the RTL would see first. For each: the
  question in a sentence or two, your recommendation, and why. No lists of questions.
- **Look for the silences too:** what a builder would need that no source states. The firmware's contract and
  the errors are the likeliest places. Each is a question like a dagger.

## Rules

- **Propose an outline first,** in `sessions/SPEC/NOTES.md`, and wait for the owner's go-ahead: the documents
  and sections, where each source's content goes, what of v1.36's text is kept, moved or replaced, the list of
  daggers, and the order you will write in. **After the go-ahead, write section by section without asking
  again,** stopping only for the daggers and silences.
- **Keep replies to the owner short.** A few lines when a chapter is done.
- **State the design, not its history.** No "was", no "as first written", no worker's name, no measurement
  that is not a figure a builder needs. The history stays in the planning documents.
- **Exactness.** Every register, bit, width, constant and worked example is checked against its source, and
  the examples (the CRC's, the headers, the mask's bytes, the restart packet's checksum) are computed again by
  you, not copied. A figure you cannot confirm is a question.
- **One name for one thing,** through every document. Where the sources use two, pick the specification's and
  put the other in the name table.
- **The specification is what you edit:** `v1.37_wip/spec/` and your own session folder. Not the simulator, not
  the firmware, not the planning documents. Where a planning document is wrong, say so in your notes; the
  specification supersedes it.
- **Branch `v137-spec`,** from `main`. Commit and push after each section. Merge into `main` yourself when the
  owner says a part is accepted (`git merge --no-ff`, then pull without rebasing, and push). Merge `main` in
  often; never rebase. Frozen folders (`v1.36/` and earlier) are never edited.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never
  write a token into a file.**
- End each turn with a clean `git status`.

## Not your work

The simulator and the firmware; the driver and the wire between processes, which are test equipment; the file
system and whatever else is v1.4's and not v1.37's; the foreground stream's commands, which nobody has designed
(the reply frame is the one foreground frame there is); a new design for anything already ruled on.

## Deliverables

- `v1.37_wip/spec/`: the specification of v1.37, with its overview and its guide from v1.36.
- `sessions/SPEC/NOTES.md`: the outline; the list of daggers and silences with how each ended; the name
  choices; what you found wrong or contradictory in the planning documents.
- `sessions/SPEC/FOR-THE-SIMULATOR.md`: the simulator's and firmware's differences from the specification.
