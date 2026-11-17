# WORKER4 report: the threshold, in v1.37's Ingress and the master tile's firmware

*Closed and merged into `main` on 2026-11-16. The files this one calls `NOTES.md` and `REPORT.md` are
`WORKER4-NOTES.md` and `WORKER4-REPORT.md`, beside it; its prompt is in the repository's history.*

Branch `updater-w4`, from `main` at `5b92d1f2`. The plan and the choices are in `NOTES.md` (the owner's go-ahead,
2026-11-16 20:54: "your choices stand"); section 9 there says where the work differs from the plan. No questions:
every † was workable as written. Paths without a folder are in `v1.37_wip/`. II is `INGRESS-INTERFACE.md`, HL
`HOST-LINK.md`, UI `UPDATER-INTERFACE.md`.

## What changed

| | what | where |
|---|---|---|
| Ingress's black box | each stream holds 1,024 words (16 KB) as a ring of words, the headers not kept; `…_WORDS` is the words waiting in every packet arrived; `FOREGROUND_THRESHOLD` and `BACKGROUND_THRESHOLD`, words 82 and 83, R/W, 1 at the reset; the flag posted when nothing is owed and the words waiting reach the threshold, the words owed then being the words waiting, counted down by the DMA; looked at on an arrival, at the last owed word's read-out and at a threshold store; a transfer runs across packets up to the words waiting; the link waits while a packet's data does not fit; the link at 1, 2, 4 or 8 bytes a cycle | `src/units/ingress_egress.h` |
| the Updater | the segment queue 512 entries with a credit of 256; the free page queue unchanged | `src/units/updater.h`, `updater.cpp` |
| the firmware | the owner's sequence: the threshold 1 between frames, min(256, the frame's words) after its header word, lowered to what is left as the tail comes, 1 at its end; "the words available" in place of the packet, the count loaded only after a flag with nothing in flight and the known words used up (C7); the frame-ends-at-a-packet check dropped; the allowance at 256; the scrap 256 rows and frame 7's records routed in runs (C14); wakes and transfers counted in its results; the threshold's size a word placed with the image (C10) | `fw/cc/master.c`, `master_layout.h`, `fw/include/host.h`; `fw/images/cc/master.elf` and `fw/images/MANIFEST` (the other 17 images unchanged) |
| the harness | `proof_run --threshold W --link-rate B`; the wakes, transfers, threshold stores, the master's cycles awake to the hand-over, the link's last byte and its waits for room; `proof_numbers.py`'s second and third tables | `tests/proof_run_main.cpp`, `proof_bench.*`, `proof_numbers.py` |
| the tests | below; a fifth fixture `kin-big` (gold); the packet writer's `packed` option | `tests/test_ingress_link.cpp`, `test_updater.cpp`, `test_master.cpp`, `fw_teeth.sh`, `packet_writer.h`, `updater/make_fixtures.sh` |
| the task | `updater_wip/tasks/v137-check/`: a third item, `faster` | `make_task.sh`, `body.sh` |
| the documents | `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, the README's top; the unit's header comment | |

Built with the pinned xPack `riscv-none-elf-gcc` 15.2.0-1; `fw/build.sh --check`: 18 images equal to `fw/images/`.

## The checks

| check | where | result |
|---|---|---|
| **`./run_tests.sh all --check`**, every v1.36 step against its v1.36 log with the switch off, and v1.37's steps | here (clang, 2 CPUs, the toolchain on PATH); the runner (**Quest 206**, `84b50cd9`) | **all passed** both places; no log differs. Here 5 steps SKIPPED (the workloads' data); on the runner 1 (`fw-build`: no xPack there; it passes here) |
| **`proof-real`, `proof-matrix`, `proof-29`**: the 37 runs WORKER3 reported, every image against gold's, `PAGE_COUNT` 0 at every done, no error bit, the Tracer's last record against record s + 1, and for `proof-real` every row and delivery against the recorded files | Quest 206 | **3, 24 and 10 of them pass** |
| **`make check`**: every test, debug and release, gcc and clang, TSan | **Quest 207** | **pass**, 1,123 s |
| **Ingress alone** (`test_ingress_link`, 38 tests): thresholds of 1, of part of a packet (5), of several (33, 100) and of the whole buffer (1,024), each flag's cycle and count by hand; a threshold of 0 acting as 1; a threshold written while words wait (lower: the flag at once, seen in the next cycle; higher: none; while words are owed: none until they are read out); a read-out across a packet's end, and one transfer of 42 words across four packets; the rule for a second flag (none while owed, at once at the last owed word, and the case of a flag posted mid-transfer for words that transfer then takes); both streams at once with thresholds 1 and 64; the buffer full, the link waiting, room returned word by word, the 33rd packet's arrival 528 cycles after the room; 2, 4 and 8 bytes a cycle; every damaged packet and error as before; a transfer past the words waiting is UB | here | pass |
| **WORKER2's vectors** (`test_packet_vectors`): the CRC values, 43 packets, 5 files, 32 damaged files, through the registers and the DMA | here | all agree |
| **the Updater alone** (`test_updater`, 20 tests), with the queue test at 512 entries (the 513th push dropped and `queue_overrun`; credits after 256 and 512 entries) | here | pass |
| **the firmware's tail**: frames of the header word alone (**gold's**: `kin-lru`'s frame 5, `jumps-base`'s 6 and 9, `divs`'s 6, each 0 words; no writer was needed), frames shorter than a packet (frames 1, 4, 8 and the small 9s), frames that are no multiple of 32 or of 256 (26 to 1,584 words), a frame of more than the buffer and three credits (`kin-big`'s data image, 1,584 words); `test_master_tail_frames` counts them in the fixtures | here (`test_master`) | every fixture passes |
| **the threshold at 32, 64 and 128** (and 1 and 1,024) | `test_master` on all five fixtures; the matrix's 24 runs at 32, 64, 128 (**Quest 208**) | the same images, every check: 5 x 5 here, **72 of 72** on the runner |
| **a frame ending inside a packet**: every fixture cut into packets of 32 and of 13 words regardless of frames | `test_master` | pass: the same images |
| the link slower than the master (4 cycles a byte), and as fast as taken at thresholds 1 and 32, on every fixture; 2, 4, 8 bytes a cycle on two | `test_master` | pass |
| **the link at 2, 4 and 8 bytes a cycle** on `proof-29`'s 10 runs and the matrix's 24 | Quest 208 | **102 of 102** pass |
| **`fw_teeth.sh`**: the master's 13 planted bugs and `host.h`'s 2, four of them new (the threshold not lowered at the tail; not set back to 1; set to the frame's whole length; the record-routing slip moved to the new code) | here | **every one caught** by `test_master`. One bug is not planted, and the header says why: the count loaded with a transfer in flight is harmless in every run (NOTES section 9) |
| `tools/doccheck.sh` | here | 0 missing |

Quest 206, 207 and 208 ran `updater-w4` at `84b50cd9`. What changed after it is documents, and
`tests/proof_numbers.py`'s measure of the rate the firmware keeps up with (now over the whole run's cycles awake),
whose tables were made again here from the runner's logs.

### The recorded figures that moved, and why

- **v1.36's**: none. Every v1.36 log is as it was (`--check`), and the switch keeps v1.36's runs byte-identical.
- **v1.37's**: the cycle counts of every proof run (WORKER3's report, its Stage 3 tables; `DESIGN-NOTES-2026-11-16.md`
  section 15's table) moved, for three reasons: the master wakes for 256 words rather than a packet, so its
  transfers and the Updater's waits change; the segment queue is 512, so the first two credits cover 512 words; and
  Ingress holds 1,024 words, so the link waits later. The runs are 0.3% to 1.3% shorter at a byte a cycle; the
  master's share of the run falls by three (below). `RUNNING-v1.37.md`'s "about 30 million cycles" still holds.
- No golden log of a v1.37 step holds a cycle count.

## The figures

The simulator's cycles, with the black boxes' timing (WORKER3's `NOTES.md` section 6), not the RTL's. Before is
`main` as merged (Quest 204, WORKER3's runs); after is Quest 206 and 208. All of it is in Quest's
`results/206-v137-w4-suites/proof-numbers.md` and `results/208-v137-w4-faster/{faster,threshold}-numbers.md`.

### Before and after, at a byte a cycle

| run | cycles to the exit | the Updater waiting on data | the master awake | its share | awake a packet | the rate it could keep up with |
|---|---|---|---|---|---|---|
| EVM Zone 2²⁹, s 0 to 84 | 26.8-30.6 M → 26.7-30.3 M (−0.3 to −1.0%) | 2.57 M → 2.29-2.30 M (−10 to −11%) | 2.75 M → 0.92 M (÷3.0) | 9.0-10.3% → 3.0-3.4% | 332-334 → 111-112 cycles | 1.58 → 4.72-4.77 bytes a cycle |
| Cyclone 2²⁹, s 0 to 88 | 31.2-42.3 M → 31.1-42.0 M (−0.4 to −0.8%) | 10.5 M → 10.1-10.2 M (−3%) | 10.4 M → 3.1 M (÷3.3) | 24.6-33.5% → 7.4-10.1% | 325-327 → 97-98 | 1.62 → 5.40-5.44 |
| the three recorded segments | −0.3 to −0.4% | −3.4 to −3.6% | ÷2.9 to ÷3.1 | 7.0-9.6% → 2.4-3.2% | 352-358 → 117-122 | 1.47-1.50 → 4.33-4.53 |
| the matrix, 24 runs | −1.0 to −1.3% | −2.5 to −3.7% | ÷2.9 to ÷3.3 | 30-43% → 10-13% | 337-355 → 102-119 | 1.49-1.57 → 4.45-5.16 |

"The rate it could keep up with" is the link's bytes over the master's cycles awake in the whole run: what the
firmware could take if it were the only limit. It counts the boot and the segment's results against it, so it is
low. (Over the cycles to the hand-over alone it reads 4.8 for EVM Zone 2²⁹ at s 83, where the hand-over comes at the
link's end.) Before the threshold the firmware could keep up with about 1.6 bytes a cycle; now it can keep up with
4.3 to 5.4.

**The wakes a frame** (Quest 206; wakes / Ingress's transfers, a wake being any return from WFI: a flag, a credit, a
transfer's landing):

| run | 1 | 2 (program) | 4, 5 (base) | 3 | 4, 5 | 6 (data) | 7 (records) | 8 (advice) | 9 (segments, to the hand-over) |
|---|---|---|---|---|---|---|---|---|---|
| EVM Zone 2²⁹, s 42 | 2/2 | 1,344/493 | 2/2, 348/119 | 7/7 | 2/2, 4/2 | 6/3 | 74/42 | 22/14 | 657/281 |
| Cyclone 2²⁹, s 44 | 2/2 | 2,208/786 | 2/2, 307/106 | 7/7 | 2/2, 2/1 | 1,787/601 | 76/41 | 386/196 | 5,843/2,129 |

- On the frames for the Updater a transfer moves up to 256 words, where v1.37 moved a packet of 32 for each wake at
  least: EVM Zone's program image is 493 transfers. A transfer costs 2.3 to 3.0 wakes: the flag, the transfer's
  landing, and a credit every other one.
- In all: 3,089 to 3,109 wakes for EVM Zone 2²⁹ and 11,288 to 11,427 for Cyclone, against 8,277 and 32,006 packets.
- Frame 7 (the records, 8,704 words at N 128): 42 transfers. Before C14 it would have been one transfer a record and
  32 rows at most to scrap; on the `kin-big` guest that was 195 transfers for 4,352 words, 24 after.

**The Updater's cycles waiting on data** fall only 3 to 11%. At a byte a cycle the link sets the pace before and
after, and the Updater still waits on data for 54% (EVM Zone 2²⁹) and 60% (Cyclone 2²⁹) of its busy cycles, against
57% and 60%. What the threshold frees is the master's core, not the link.

**The threshold's size** (the matrix, 24 runs each, at a byte a cycle; the same images at every size):

| threshold | wakes, mean | the master awake, mean | the rate it could keep up with | cycles to the exit, mean |
|---|---|---|---|---|
| 32 | 14,356 | 3.76 M | 1.03-1.08 bytes a cycle | 7,166,421 |
| 64 | 7,411 | 2.04 M | 1.90-1.97 | 7,166,677 |
| 128 | 4,483 | 1.27 M | 2.98-3.24 | 7,167,467 |
| 256 | 2,690 | 0.83 M | 4.45-5.16 | 7,169,045 |

The cost goes almost exactly with the wakes, so it is the cost of a wake, not of a word. At 32, a packet's worth,
this firmware is slower than v1.37's (1.05 against 1.5 bytes a cycle): about 360 instructions and 2 wakes a transfer,
against WORKER3's 220 instructions a packet. The new bookkeeping (the count's load after a flag, the threshold, the
counts for these figures) costs about half again a wake. At 256 a wake carries 8 times the words, which is where the
factor of 3 comes from. The program is still not tuned.

### The link faster: 2, 4 and 8 bytes a cycle

Quest 208: every run passes at every rate, with the same images. EVM Zone 2²⁹ and Cyclone 2²⁹ are means over their
five segments, which differ by under 2%.

| | rate | the link's last byte | as the rate alone would give | the link waited for room | the Updater busy | of it waiting on data | on memory | decoding (the rest) | the master awake, over the link's time |
|---|---|---|---|---|---|---|---|---|---|
| EVM Zone 2²⁹ | 1 | 4.50 M | 4.37 M | 0.13 M | 4.23 M | 2.30 M | 1.22 M | 0.71 M | 0.20 |
| | 2 | 2.71 M | 2.18 M | 0.52 M | 2.54 M | 0.60 M | 1.22 M | 0.71 M | 0.31 |
| | 4 | 2.17 M | 1.09 M | 1.08 M | 2.04 M | 0.11 M | 1.22 M | 0.71 M | 0.38 |
| | 8 | 2.10 M | 0.55 M | 1.56 M | 1.99 M | 0.06 M | 1.22 M | 0.71 M | 0.35 |
| Cyclone 2²⁹ | 1 | 17.99 M | 16.90 M | 1.09 M | 16.98 M | 10.17 M | 3.69 M | 3.11 M | 0.17 |
| | 2 | 10.65 M | 8.45 M | 2.20 M | 10.10 M | 3.30 M | 3.69 M | 3.11 M | 0.29 |
| | 4 | 7.44 M | 4.22 M | 3.21 M | 7.13 M | 0.32 M | 3.69 M | 3.11 M | 0.39 |
| | 8 | 7.19 M | 2.11 M | 5.08 M | 7.00 M | 0.19 M | 3.69 M | 3.11 M | 0.35 |
| matrix, Cyclone 22 raw, full table | 8 | 1.41-1.46 M | 0.66 M | 0.74-0.80 M | 1.30-1.36 M | 0.08-0.09 M | 0.58-0.64 M | 0.63 M | 0.61-0.62 |
| matrix, EVM Zone 23 LRU, on a base | 8 | 1.05-1.06 M | 0.35 M | 0.70-0.71 M | 0.96 M | 0.05 M | 0.47 M | 0.44 M | 0.50 |

Cycles to the exit, EVM Zone 2²⁹ at s 42: 29.6 M at 1, 28.1 M at 2, 27.6 M at 4, 27.5 M at 8 (tracing, about 24 M, is
the rest). Cyclone 2²⁹ at s 44: 42.0, 34.7, 31.4, 31.2 M.

**Where the limit is: the Updater, and within it the memory side first.**

- **At 1 byte a cycle the link is the limit.** The Updater waits on data for 54 to 60% of its busy cycles, and the
  master is awake for under a fifth of the link's time.
- **At 2 bytes a cycle the limit moves.** The link waits 0.5 M cycles (EVM Zone) and 2.2 M (Cyclone) for room in
  Ingress, while the Updater still waits on data for a quarter to a third of its time. The link and the Updater take
  turns: during the program image and the pages written by the memory segments the Updater is behind, and elsewhere
  the link is.
- **From 4 bytes a cycle the Updater is the limit.**
  - The link's last byte hardly moves from 4 to 8: 2.17 M to 2.10 M cycles on EVM Zone, 7.44 M to 7.19 M on Cyclone.
  - The Updater is busy for 94 to 97% of the link's time and waits on data for 5% of it or less.
  - What it is busy with: the memory side (pages and half pages read and written at 336 and 208 cycles, look-ups,
    entries written through) is 1.22 M of EVM Zone's 1.99 M (61%) and 3.69 M of Cyclone's 7.00 M (53%). The rest is
    decoding, a 64-bit word a cycle.
  - The black box does the two one after the other, so the link's bytes reach the images at about 2.1 bytes a cycle
    on EVM Zone 2²⁹, 2.3 on Cyclone 2²⁹, and 2.6 to 3.8 on the small inputs.
- **The firmware is not the limit at any of these rates.** Its cycles awake are at most 0.39 of the link's time at
  2²⁹ (0.67 on the small inputs), and by the figure above it could keep up with 4.3 to 5.4 bytes a cycle. If the
  Updater overlapped its memory work with decoding, decoding alone would allow about 6 bytes a cycle on EVM Zone
  (0.71 M cycles for 4.37 MB) and 5.4 on Cyclone, and the firmware would then be the next limit, near 5.
- The memory side's figures are the black box's (a page 336 cycles, nothing overlapped), not the RTL's. What this
  says for the design is the order of the limits: the Updater's memory work, then its decoding, then the firmware.

## A frame that ends in the middle of a packet

The check is dropped (item 4). With a read-out that runs across packets, the firmware does not know where packets
start, and Ingress knows nothing of frames.

- **A file whose frames are packed across packets now runs.** The next frame's header word is simply the next word
  on the stream, whatever packet it is in. Every fixture cut into packets of 32 and of 13 words regardless of frames
  gives gold's images, at every check (`master_frames_across_packets`). Gold's packetizer never makes such a file,
  since each frame starts a packet.
- **What is lost:** the check caught a frame whose header gave the wrong length, if the error ended away from a
  packet's end. Now the master takes the word after the stated length for the next frame's header word.
  - A type outside 1 to 9, a header frame whose fields do not add up, or a data frame whose length is not its header
    frame's still ends the run (`MR_FRAME`), and almost any data word fails those.
  - A data word that happens to read as a plausible frame header would be taken for one. Only the frames' own fields
    stand between that and a wrong image now.
- **A file that ends inside a packet** is as before: the packet never arrives, nothing is an error, and the master
  waits (`master_link_cut`).

## What I would change in INGRESS-INTERFACE.md (for the owner)

1. **The register table:** `STATUS` [3], [4]: "a word is waiting" (not "a packet"). `EVENTS` [8], [9]: "the words
   waiting reached the stream's threshold". Words 80 and 81: "the words waiting on the stream, in every packet that
   has arrived and passed its checks" (the threshold section says so; the table still has the packet). Words 82
   and 83 as built (their †).
2. **"The rule for a service flag"** is still written a packet at a time, steps 1 to 5. Replace it with the
   threshold's rule as built:
   - the flag is posted when nothing is owed and the words waiting reach the threshold;
   - at the post the stream owes the words waiting, which the DMA counts down;
   - the stream is looked at on an arrival, at the last owed word's read-out, and at a threshold store.
   This is "the words the register showed" made exact: the register is live, so a core that loads it after the flag
   may see more than was owed (C3).
3. **A sentence for firmware writers:** a flag can be posted while the core is still reading words it has already
   counted, for words that same transfer then takes. So the core should load the count only after a flag, with no
   transfer of its own in flight and what it last loaded used up (C7). The count is then exact, and may be 0.
4. **A threshold of 0 acts as 1** (a flag needs a word). A threshold above the buffer, 1,024, is never reached, which
   is the firmware's mistake (C4).
5. **"Reading words out":** "a count beyond what is left of the packet is undefined" becomes "beyond the words
   waiting".
6. **"Frames":** keep "a frame ends at the end of a packet" as the packetizer's rule, but say the chip does not
   depend on it. The "Open" item on the check is decided (dropped): what is lost is a check of the frames' lengths
   (above).
   - "What the core does with a background packet" (copy one word, decide, send the rest of this packet and each
     following one) becomes words: the header word, then the frame's words, in transfers of up to the threshold's
     worth.
7. **"Credits for the Updater":** "reads a background packet out to the Updater only when the allowance covers it"
   becomes "reads out to the Updater as many words as the allowance covers". "One credit covers eight full packets"
   stays true, with the credit at 256.
8. **The threshold section:** "v1.37's black box as merged holds four packets" and "Not in v1.37 as merged" are now
   history; v1.37 holds 1,024 words a stream and has the threshold.
   - Say that the threshold must be lowered as the frame's tail comes, not only set at the header word. "The smaller
     of a credit and what is left" means what is left when the core next waits.
   - Say that the link waits when a packet's data does not fit (C2).
9. **The figures:** at 256 the firmware's cost is about 100 cycles awake a packet's worth, 4.3 to 5.4 bytes a cycle,
   against 330 and 1.6. The Updater, and its memory work, is the next limit, near 2 to 4 bytes a cycle in v1.37's
   black box.
10. **A suggestion, not a correction:** a DMA transfer that discards words (a count with no destination). The master
    reads every record but two of frame 7, and every advice log but one of frame 8, into a scrap area of its
    scratchpad, which takes room and write traffic. In the real system that is 68(N − 2) words of records alone.

## What I would change in HOST-LINK.md (for the owner)

1. **"Holding the host back":** the buffer is 16 KB of data words; say whether the headers count (the black box keeps
   data words alone, C1). "Stop when half full" is then 512 words.
2. **"The size of a packet":** the firmware no longer counts packets. Its wakes are the threshold's, a wake carrying
   up to 256 words whatever the packets' size, and it runs the same frames cut into packets of 32 words or of 13
   (`master_frames_across_packets`). So the size can be chosen for the link alone, as II says, and the 1,024-byte
   packets' one advantage is gone.
3. **A frame and a packet:** "each frame starts a new packet" (the packetizer's) can become "may": nothing on the
   chip depends on it any more.
4. **The rate:** HL budgets 1 GB a second. v1.37's black boxes show that the chip's side could take 2 bytes a cycle
   before the Updater limits it, and no more than about 2.1 to 3.8 with the Updater's memory work as the black box
   models it. That is worth recording beside the link's own figures, as what the receiving side can absorb.

## For UPDATER-INTERFACE.md

Nothing to correct: the segment queue of 512 with a credit of 256 is as the owner ruled. One finding for the real
controller: the black box decodes and waits on memory one after the other, and that sum is what limits the link from
2 bytes a cycle. A controller that overlaps them would move the limit to its decoding and then to the master's
firmware.

## Open

- **The black boxes' timing is still WORKER3's:** the Updater's costs, the link's rate. The order of the limits is
  this report's finding, not their sizes.
- **The firmware's cost a wake** (about 360 instructions and 2 wakes a transfer) is not tuned. At thresholds below
  64 it is worse than v1.37's packet-at-a-time program.
- **The foreground stream:** its threshold is built and tested in Ingress; the firmware leaves it at 1 and v1.37's
  files have no foreground.
