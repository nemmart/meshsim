# How big are the two controllers? (updater_wip)

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

- **First tell the owner, in a few lines, how you will count and what you will assume, and wait for the
  go-ahead.** Then do it in one pass.
- **Keep the reply to the owner short:** one table, three rows (the cache's controller, the Updater's
  controller, the arbiter), with the states, the control word's width, the flip-flops, the logic's gates low
  and high, and the memory in bits. Then two or three lines on what dominates and how sure you are.
- Branch `v137-sizing`, from `main`. Commit your report and your counting to
  `updater_wip/planning/sessions/SIZING/`, push, and say when it is there.
- Attach the repository to the session to push, or clone with the token from your user preferences. **Never
  write a token into a file.**
- End with a clean `git status`.

## Deliverables

- `sessions/SIZING/REPORT.md`: the table; for each controller its states, its control word signal by signal,
  its registers, its datapath operators and the gate estimate with its assumptions; where the code and the
  specification differ.
- `sessions/SIZING/`: the counting itself, as a script or tables.
