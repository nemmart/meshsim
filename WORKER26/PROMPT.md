# Worker 26: the host side of the Tracer's bring-up: the jump-target map (section 18) and the start images

You are building the host tool that makes the Tracer's jump-target map: the hash table from a jump's target pc to its base
instr_idx, which the Tracer probes on a jump that misses its 64-entry LRU. Its design is DECISIONS-v1.3 section 18 and spec
4.21.4 (with items 146 and 148): read those first, then section 16 (frame 6), D1 in WORKER18/UPSTREAM-DIFFS.md (the code is the
ELF's text sections), and spec 4.21.5 and 4.21.9 (the map is a file read over the fill bus, through the shared 512 KB cache).
WORKER22 is building the Tracer in meshsim and will load your map; WORKER18 owns gold (wip/gold/, the Java emulator).

REPOSITORY: https://github.com/nemmart/meshsim (and jolt-asic-planning for the guests' ELFs; one token from your user
preferences works for both; never write a token into a file). Work on a branch, `v1.3-jumpmap`, made from `main`; notes and
report in `WORKER26/` on `main`.

THE WORK
1. **The static table, from the ELF:** every direct target (branch and JAL offsets), every return address (the instruction
   after each call), and every code pointer found in data (an aligned 64-bit word in the data or read-only sections equal to
   an instruction start), each with its base instr_idx from gold's bytecode. Measure what the data scan adds; section 18
   measured the first two kinds only.
2. **The hash:** entries (key u32, base u32) with key o = pc − lowest_address, a free entry 0xFFFFFFFF; the line the top k
   bits of (o × p1) mod 2³²; p1 an odd 32-bit multiplier, p3 a 24-bit prime step; choose k, p1 and p3 so the table is at most
   half full and every key is found within 3 lines (home, then two steps).
3. **The supplement (frame 6):** the run's targets the static table lacks, from gold's run of each workload (every jump's
   target), and its insertion by the lookup's own rule, checking no inserted key lands more than two lines from home. Section
   18 leaves open who inserts (C&C 3 at proof start, or the host sending patched lines): propose, and for bring-up produce
   the merged table.
3b. **Each segment's start image** (owner, 2026-11-05, moved here from WORKER22): gold's cycle-0 image (the ELF's sections and
   the inputs) plus segments 0 to k−1's updates (frames 4 or 5) applied in order; **owned** = the pages segment k itself
   writes (its own update list), every other page shared and not owned. **Check each against gold's memory at segment k's first
   row.** Also word 66's values (log₂ K, `lowest_address`) and the hash constants, standing in for frame 7 until gold writes it.
4. **Outputs WORKER22 can load:** the table as a file image of 128 B lines, the three constants, frame 6, and per segment the
   start image as a list of pages (each its 4 KB content and its owned flag) with word 66's values, in a format you
   agree with WORKER22 (and WORKER18, for gold writing frame 6).
5. **Measurements, both workloads at 2²⁹:** static entries and table size; distinct targets per segment; supplement sizes per
   proof; the fullest line and the longest probe; and the cache footprint, the distinct table lines each segment's jumps
   touch, against a table of every instruction start.

RULES
- **Propose a plan first and wait for the owner's go-ahead.**
- Java, using gold's ELF and bytecode classes, unless you propose otherwise.
- Keep the last tool calls of every turn for committing and pushing, and end each turn with a clean git status.

DELIVERABLES: the tool on v1.3-jumpmap, the tables and frame 6 for both workloads, WORKER26/REPORT.md.
