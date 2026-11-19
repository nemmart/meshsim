# How big are the two controllers? (updater_wip)

You are sizing the two controllers in v1.37 of meshsim, the repository `nemmart/meshsim`, on `main`. This brief
is also in the repository, at `updater_wip/planning/sessions/SIZING/PROMPT.md`.

**The project.** meshsim is the simulator and specification for a parallel compute mesh, part of an ASIC for
proving the Jolt zkVM. v1.37's simulator has two controllers written as state machines, each specified from its
code and checked against it by its author:

| controller | what it does | its code, in `v1.37_wip/src/units/` | its specification, in `v1.37_wip/spec/machine-spec.md` |
|---|---|---|---|
| **the cache's controller** | serves the Tracer's line reads and writes and the clients' look-ups from a shared cache in front of LPDDR; fetches, evicts and writes back | `cache_ctl.h`, `cache_ctl.cpp` | 23.6, and 23.6.5 above all: "The controller: its registers and its states" |
| **the Updater's controller** | decodes the host's update words and builds the memory images in LPDDR: walks the index tree, takes free pages, reads, patches and writes pages | `updater_ctl.h` (and `updater_top.h` for its window) | 17.10: 17.10.2 the decoder, 17.10.5 a run, 17.10.7 its registers |

**The owner's question:** how big is each? **How many states, how wide is the control word, and about how many
gates to build it.** The answer goes to the people deciding whether these are small blocks of logic or something
that needs a closer look, so it must be a figure they can trust with its reasoning shown, not a guess.

**Why the owner asks:** "we're really replacing a general processor with a fixed logic controller. Is that the
right call?" The work these two do could have been firmware on a tile's core. So the figures are wanted beside
the core's, and beside what a core would take to do the same work.

## What to report, for each controller

1. **The states.** Each controller is several small machines working together (the cache has a front end, a
   walker, a miss handler and an AXI reader and writer; the Updater has a decoder and a run with a fault walk).
   For each machine: its named states, listed, and how many flip-flops hold its state. Then the total. Count
   from the code, and say where the specification's list differs.
2. **The control word.** Take it to mean every signal the state logic drives into the rest of the block in a
   cycle: register enables, multiplexer selects, memory read and write enables, counter loads and steps, and
   the handshake outputs. List them with their widths and give the total width. Say also how many different
   control words are ever used, which tells whether a small table or plain logic would hold them.
3. **The gates.** An estimate in NAND2 equivalents, in three parts kept apart:
   - **the flip-flops,** from a count of every register and its width. The specification has a count
     (about 6,880 for the cache's controller, 4,193 for the Updater, 2,048 of those its LRU table). Count again
     from the code and say where you differ;
   - **the logic between them:** the next-state and output logic, and the datapath's adders, comparators,
     counters and multiplexers, each counted with its width;
   - **the memories,** in bits, not gates: they are SRAM and are sized apart.

   Give a low and a high figure with the assumptions that separate them, and say which two or three things
   dominate. State your gate costs for a flip-flop, a full adder, a comparator bit and a multiplexer bit, so the
   figure can be redone with another library's numbers.

**The AXI arbiter** (`axi_arbiter.*`, 23.11; 1,539 flip-flops in the specification) is not one of the two, but
it is small and sits between them. Give it the same three figures as a third line, kept apart.

## The comparison with a processor

4. **The core's size, beside theirs.** The tile's core is RV32I with C, Zmmul, Zicsr and Zicond, `WFI`, the CSRs
   and the trap logic, with IMEM and DMEM of 32 KiB each (`spec/core.md`; `machine-spec.md` 6 and 24, which
   budgets about 0.15 mm² for the core, its memories and its registers together, as a rough estimate). Estimate
   its gates the same way and with the same costs as the controllers', its logic and its memories apart, and
   say how sure you are: its insides are not modelled gate by gate in the simulator, so use the specification
   and what is published for cores of its kind, and name what you used.
5. **What a core would take to do the same work.** For each controller's main loop, write the firmware a core
   would run in its place, in outline, and count its instructions and so its cycles:
   - **the Updater:** per update word decoded and placed (a header word, a value into a page already held, a
     value that starts a new page), and per page fault walked. Set it beside the rate the link delivers: a
     byte a cycle, so a 64-bit update word every 8 cycles (`spec/host-link.md`; the forward count in 17.6);
   - **the cache:** per look-up answered from a cached line, and per miss. Set it beside the controller's own
     figures in 23.6.7, the look-up of 6 cycles above all.

   Use the core's real cost a cycle from `spec/core.md` (what a load, a store, a branch and a multiply take),
   and say what else a core would need that it does not have today: a path of its own to LPDDR, to the cache's
   memory, to the page memory.
   - There is one measured point to check against. With the master tile's firmware only copying the host's
     data to the Updater, it was awake 17 to 20% of the link's time; taken out of that path it is 2.4 to 7.0%
     (`v1.4_wip/planning/INGRESS-INTERFACE.md`, "In v1.37").
6. **The other side of the trade.** In a few lines: what is fixed in each controller that firmware could have
   changed after the chip is made (the update word's format and its LRU code in the decoder; the cache's
   eviction and write-back rules), and what stays in firmware today (which runs, which roots, which free pages,
   what each frame means: `spec/master-firmware.md`).

Give figures and what they rest on. Whether it is the right call is the owner's to judge.

## How

- **Count from the code,** with the specification as the guide to what each register and state is for. Where
  the simulator does in one step what hardware must spread over cycles, or holds something a chip would not
  (a check, a statistic for the simulator's own use, a queue modelled as a C++ container), say so and count
  what hardware needs.
- **Write the counting as a script or a table that can be read and redone,** in your session folder, not as
  figures typed into a report.
- If a synthesis tool is on your machine (`yosys`), you may write a small synthesizable model of a controller's
  state logic to check your estimate of the logic between the flip-flops. That is a cross-check, not the
  deliverable, and it is not worth more than an hour.
- **Change nothing** in `v1.37_wip/` or in `spec/`. Where you find the specification's count wrong, report it.

## Rules

- **Do not ask questions, and do not wait for a go-ahead: this brief is the owner's go-ahead.** Do the whole
  count in one pass. Where something is unclear, choose, and write the choice and its reason in the report.
- **Keep the reply to the owner short:** one table, four rows (the cache's controller, the Updater's
  controller, the arbiter, the core), with the states, the control word's width, the flip-flops, the logic's
  gates low and high, and the memory in bits. Under it a second small table: the cycles a core would take
  against the cycles each controller takes, for an update word and for a look-up. Then two or three lines on
  what dominates and how sure you are.
- Branch `v137-sizing`, from `main`. Commit your report and your counting to
  `updater_wip/planning/sessions/SIZING/`, push, and say when it is there.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never
  write a token into a file.**
- End with a clean `git status`.

## Deliverables

- `sessions/SIZING/REPORT.md`: the table; for each controller its states, its control word signal by signal,
  its registers, its datapath operators and the gate estimate with its assumptions; where the code and the
  specification differ; the core's estimate and its sources; the firmware outlines with their instruction
  counts; what is fixed in logic that firmware could have changed.
- `sessions/SIZING/`: the counting itself, as a script or tables.
