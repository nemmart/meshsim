# How big the two controllers are, and whether fixed logic was the right call

Recorded 2026-11-19. v1.37 has two controllers built as fixed logic where firmware on a tile's core might have
done the work: **the cache's controller** (`machine-spec.md` 23.6) and **the Updater's controller** (17.10). The
owner asked how big each is: how many states, how wide the control word, about how many gates. And why he asked:
"we're really replacing a general processor with a fixed logic controller. Is that the right call?"

A session sized them from the code, with the AXI arbiter and the tile's core beside them. **Its full report, with
every register, every control signal, the firmware outlines and the scripts that do the counting, is
`updater_wip/planning/sessions/SIZING/REPORT.md`.** This is what it found. It is not part of the specification:
the specification states the machine, and this is an estimate about it.

## The sizes

| | states | control word | flip-flops | logic between them, kGE | in all, kGE | memory |
|---|---|---|---|---|---|---|
| **the cache's controller** | 12 named (the walker 4, the miss handler 8); 40 flip-flops of state | 110 bits, 85 different words in use | 6,652 | 17.8 to 46.6 | **41 to 84** | 512 KiB of data and 13.5 KiB of tags: the cache itself |
| **the Updater's controller** | 18 named; 26 flip-flops of state | 95 bits, 93 different words in use | 4,193 | 18.8 to 34.9 | **42 to 59** | 4 KiB of page memory, 1 KiB of free page queue |
| the AXI arbiter | none named; 45 flip-flops of slot, list and turn state | 49 bits, 11 different words | 1,539 | 3.8 to 8.2 | **10 to 17** | none |
| the tile's core | a pipeline of four stages | | about 1,900 to 2,000 | 23.1 to 31.5 | **34 to 43** | 64 KiB of IMEM and DMEM, and the boot ROM |

- A gate is a NAND2 equivalent (GE); a flip-flop is counted at 5.67. The report states its cost for an adder
  bit, a comparator bit and a multiplexer bit, so the figures can be redone with a real library's.
- **Each controller is about one and a half cores of logic.** Like for like, the cache's controller is 1.2 to 2.0
  cores and the Updater's 1.2 to 1.4. The arbiter is a third of one.
- **The control words are small enough that plain logic holds them;** a table would be larger.

**What dominates:**

- **In the cache's controller, the width of a line.** Five registers of 1,024 bits hold 5,120 of its 6,652
  flip-flops, and the 1,024-bit multiplexers round them are most of its logic. The low figure assumes a chip
  keeps three of the five and builds only the one look-up port that v1.37 uses. A chip could keep fewer than
  the code does: the report's "lean" count is 4,157.
- **In the Updater, the LRU decoder:** its table of 2,048 flip-flops, the table's 32-to-1 read and the bit
  buffer's two shifters are about 25 to 35 of its 42 to 59 kGE. That is the price of the compressed update
  words.

**How sure:**

- The flip-flop counts are exact from the code.
- The logic is a range. Synthesizing the largest pieces with yosys gave 5% to 22% above the report's own costs,
  inside the range.
- The core is the least sure, about ±30%: the simulator does not model its insides gate by gate. The estimate
  agrees with the published figure for a comparable open core (Ibex with a one-cycle multiplier, 33 to 36 kGE).

## What a core would take to do the same work

The report outlines the firmware a core would run in each controller's place, as real instructions that
assemble, and counts cycles at the core's own costs (`spec/core.md`). Waits on LPDDR are left out on both sides.

| one item of work | firmware on the core, cycles | the controller, cycles | what it must keep up with |
|---|---|---|---|
| the Updater: a raw value into the page held | 25; 5.5 in a long run, unrolled | 1 | the link: 8 cycles for a raw update word |
| the Updater: an LRU-coded value | 51 | 1 | the link: 2.2 cycles for a code |
| the Updater: one index level of a page fault | 7,459 | about 5 | |
| the cache: a look-up of a depth-2 file, both lines cached | 105 to 214 | 6 | |
| the cache: a fill read of a depth-3 file, all lines cached | 482 | 10 | |

- **A core keeps up with the link only on raw update words in long runs,** and is then busy about 0.86 to 0.89
  of the link's time, the copying counted, with nothing else to do. Raw values in short runs need about 3
  cores' worth of time. LRU-coded frames need 23 on the test fixtures' mix of codes, and 10 to 17 at the
  compression the proof runs have.
- **The cache in firmware is 17 to 48 times slower on a hit,** and it is the cache that sets the Tracer's speed.
- **A core would also need hardware it does not have:** a path of its own between its memory and LPDDR, the
  cache's memories mapped where it can read them (a 1,024-bit line is then 32 loads), and windows onto the
  look-up and fill ports. The outlines assume all of it.
- **One measured point agrees.** With the master tile's firmware only copying the host's data to the Updater, it
  was awake 17 to 20% of the link's time; out of that path it is 2.4 to 7.0% (`INGRESS-INTERFACE.md`).

## The other side of the trade

**What is now fixed in logic, and could have been changed after the chip is made had it been firmware:**

- **in the Updater:** the update word's format and its LRU code; the rule that skips reading a whole page or
  half; the copy-on-write walk and its order; the index's format; its reach of 2²³ pages;
- **in the cache's controller:** the geometry (128-byte lines, 8 ways by 512 sets) and true LRU; write-back for
  the Tracer's writes and write-through for updates; one update in flight; no list of pending writes.

**What stays in firmware** (`spec/master-firmware.md`): which runs are made and in what order; each run's roots
and settings; the free pages; what each frame means; the hand-over to the tracer tile; every check of a frame's
contents.

So the split is **mechanism in logic, policy in firmware.** The processor has not been replaced: the master
tile's core still decides everything, and the controllers carry out the per-word and per-line work that a core
cannot do at the link's rate or the Tracer's.

## The answer to the owner's question

**For about one and a half cores of logic each, the controllers do what would take a dozen or more cores in
firmware, and those cores would need new paths to LPDDR and the cache besides.** The reason is speed, not size:
the controllers are not smaller than a core. The cost is that the update word's format and the cache's rules
cannot be patched after the chip is made. (The design session's reading of the figures; the judgment is the
owner's.)

## What it corrected in the specification

The report counted the cache's controller at 6,652 flip-flops where 23.6.5 had 6,876. The walker's tag row is
the tag memory's output and not a register of the walker's (216 bits), and five widths were off by a few bits.
23.6.5 and the budget in chapter 24 were corrected on 2026-11-19.
