# Worker 21: the Tracer's spec chapter (SP1)

You are writing the spec for the **Tracer**, C&C 3's peripheral in meshsim's machine (an ASIC for proving the Jolt zkVM): the
RV64 row machine that executes the guest program and writes each window's run log, the streams the tiles' replayers (spec 4.19)
consume. Its design is decided in `wip/planning/DECISIONS-v1.3.md` sections 1-25: they are the record, and the spec states
them. Read them first (13 and 17-25 supersede parts of 1-12, marked in place), then `DECISIONS-v1.26.md` section 3 (v1.26.1:
RUN_CONFIG, words 32-35, the stream word with advice, the superset record), then spec 4.17, 4.19 (the replayer, the chapter's
model and closest relative), 6.6-6.7 and 7. WORKER24's figures 9.1-9.3 in `wip/diagrams/` draw the Tracer and its record.

REPOSITORY: https://github.com/nemmart/meshsim (clone with the meshsim token from your user preferences, as
`https://x-access-token:<token>@github.com/...`; never write a token into a file). **Work on a branch, `v1.3-spec`, made from
`v1.26.1`** (WORKER25's point release, which changes the same spec; the integrator merges it first, then yours). Edit only
`wip/spec/` and your `WORKER21/`.

THE WORK
1. **A new section, 4.21 The Tracer (peripheral type 3)**, written as 4.19 is:
   - what it is, and what it shares with the replayer (section 25);
   - **the region and the record**: the 68-word superset record (DECISIONS-v1.26 item 17), word 1's advice field counting
     down live, word 66 (the files, log₂ K, `lowest_address` in 4 KB units), word 67;
   - **control** (section 24): exactly two pulses, `PULSE_RUN` and `PULSE_DMA`, with no start pulse; the word-0 write does all
     the resets and invalidates the caches and the jump LRU; word 0 is written only at a segment's start (a mid-segment rewrite
     is UB); `RUN_CONFIG` (sticky; the Tracer uses its row count and ignores the flags); words 32-35 as on the replayer;
     `HASH_MUL` and `HASH_STEP` (section 18); the wake flags expand, done, advice and DMA done;
   - **execution** (sections 13, 18, 25): the 128-register machine, the templates, the controller-fed path, advice from the read
     queue, the jump LRU and hash probe, the program's end detected by `JAL x0, 0` and then 4.19.2's end-of-trace encoding,
     padding rows; a JALR or branch to 0 is the map error;
   - **memory** (section 21): the 16 KB caches, the (root, depth, line) interface, and **the range comparator**,
     `(address − lowest_address) < 8·K` on the full 64-bit address, with its own `ERRORS` bit;
   - **output** (sections 2-4, 6, 24): the FIFOs, set 1 sizing as it traces, set 2 compressing on the way out after done, the
     packers and output queues; **at done n, queue 0 is part 0 read in place**: its state words (0, 2-66) are window n+1's
     start, and word 1's three output fields and word 67 are run n's totals; C&C 3 sends them to tiles n+1 and n; in the first
     implementation the DMA writes into the tiles' memory, so no credits pace it;
   - **boundaries** (sections 5, 24), and **errors** (section 25).
   **The memory system is a black box:** specify only the Tracer's side of its interface (6.7 and 7 as far as that needs), with
   latencies as parameters.
2. **The rest of v1.3 outside 4.21:**
   - the DMA's counts in 128-bit words (4.17.9);
   - the broadcast of controller-fed templates, and the tile's trim by the bitmap (section 25);
   - 4.19's ground-truth paragraph (item 113), which still names LayerZero-Research/jolt 735355e as upstream and jolt-cpp as
     gold, brought to section 17: a16z at 7189216 decides (the verifier's source), gold is the Java emulator (`wip/gold/`), and
     jolt-cpp 88dca85 is the pin;
   - the register, invariant and parameter tables, and open items into section 13.
3. **`wip/spec/CHANGES-v1.3.md`**: the delta from v1.26.1, one row per section touched, citing DECISIONS-v1.3.

PROPOSE with the plan (the owner rules):
- the Tracer's window from word 36 up (the queue lengths, `HASH_MUL`, `HASH_STEP`);
- the `ERRORS` and `EVENTS` bits;
- word 66's bit positions;
- where the per-stream compression enables live (one candidate: `RUN_CONFIG`'s reserved [31:26] on the Tracer);
- the advice flag's threshold;
- how a template number returns with an expansion;
- where the jump-target table lives (section 18's open SRAM question);
- the fill bus's error responses (a hole, past a file's length).

OPEN QUESTION, to answer with a proposal: how a proof's K and layout reach the chip, so that the Tracer (word 66) and the proving
side take them from one source (frame 1, a new frame, or the program file).

RULES
- **Propose a plan first, with the outline and the proposals above, and wait for the owner's go-ahead.**
- Spec only. WORKER22 builds the Tracer in meshsim in parallel and follows your text: put anything it needs early (layouts,
  register numbers, error bits) in `WORKER21/NOTES.md`, and read its notes for what the model finds.
- Where the decisions leave something unsaid, propose it (marked *(Proposed)*, gathered in section 13) rather than invent it
  silently; where they conflict, ask.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean `git status`.

DELIVERABLES: 4.21 and the changes elsewhere in `wip/spec/`, `CHANGES-v1.3.md`, `WORKER21/REPORT.md`.
