# WORKER4 notes: the threshold, in v1.37's Ingress and the master tile's firmware

Branch `updater-w4`, from `main` at `5b92d1f2` (the prompt with the 8 KB segment queue, the 256-word credit, and the
threshold the smaller of 256 and the words left in the frame). Paths without a folder are in `v1.37_wip/`. II is
`INGRESS-INTERFACE.md`, HL `HOST-LINK.md`, UI `UPDATER-INTERFACE.md`.

**Status: the owner's go-ahead (2026-11-16, 20:54): "Go ahead with your plan. Your choices stand." The header-only frame: from gold if it can make one, otherwise the tests' writer. Built; section 9 says where it differs from the plan.**

## 1. What changes, and where

| piece | files | what |
|---|---|---|
| Ingress's black box | `src/units/ingress_egress.h` | each stream's buffer becomes 1,024 words (data words, the header stripped), held as a ring of words, not of packets; `…_WORDS` is the words waiting in every packet that has arrived and passed; `FOREGROUND_THRESHOLD` (word 82) and `BACKGROUND_THRESHOLD` (word 83), R/W, 1 at the reset; the new rule for a flag (section 2); a DMA transfer may run across packets up to the words waiting; the link waits while the next packet does not fit; the link's rate as bytes a cycle (1, 2, 4, 8) beside today's knob |
| the Updater | `src/units/updater.h`, `updater.cpp` | the segment queue 512 entries with a credit of 256 (`QUEUE`, `HALF` made per queue: the free page queue stays 256 and 128). Nothing else |
| the firmware's header | `fw/include/host.h` | the two threshold words; `UPD_CREDIT` 256; `HL_BUFFER_WORDS` 1,024 |
| the master's program | `fw/cc/master.c`, `master_layout.h` | the owner's sequence (section 3); the packet model replaced by "the words available"; the frame-ends-at-a-packet check dropped; a count of wakes, in all and for each frame, in its results |
| the images | `fw/images/cc/master.elf` and its hash | rebuilt with the pinned xPack 15.2.0-1 (fetched from its GitHub release here); `fw/build.sh --check` equal. The other 17 images unchanged |
| the harness | `tests/proof_bench.*`, `proof_run_main.cpp`, `proof_numbers.py` | `--threshold W` (the firmware's credit-sized threshold, default 256) and `--link-rate B` (bytes a cycle); the wakes in the figures |
| the tests | `tests/test_ingress_link.cpp`, `test_packet_vectors.cpp`, `test_updater.cpp`, `test_master.cpp`, `fw_teeth.sh` | section 5 |
| the documents | `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, the unit's header comment | brought up to date |
| the tasks | `updater_wip/tasks/v137-check/` | reused; a step for the faster links added (section 6) |

`v1.4_wip/` is not edited: what I would change in II and HL goes in the report as lists.

## 2. Ingress: the threshold in the black box

State a stream keeps: the words held (arrived, passed, not yet read out), in a ring of 1,024; its threshold; and
**the words owed**, what the register showed when the last flag was posted, counted down as the DMA takes words.

- **The flag:** posted when nothing is owed and the words held reach the threshold; at the post, the words owed
  become the words held. So after a flag there is none until the core has read out what the register showed.
- **"Looked at again":** the stream is checked at the three points where the condition can change: a packet's
  arrival, the DMA taking the last word owed, and a store to the threshold. That is the level rule exactly, so a
  threshold written while words wait posts at once if they reach it.
- **`…_WORDS`** reads the words held, live, as II says.
- **The DMA:** `out_ready` while any word is held; a transfer of more than the words held at its start is UB and
  stops the simulator, as today for more than the packet.
- **The link waits** while the next packet's data does not fit in the room left (1,024 less the words held), and
  starts the cycle after a read-out makes room, as it does today at four packets. A length-0 packet needs no room.
- **The rate:** `link_bytes_per_cycle` (1 by default; 2, 4, 8): a packet of *b* bytes arrives ⌈*b* / rate⌉ cycles
  after the last. At 1 every arrival is what it is today.
- **With the switch off nothing is touched**; with it on and the threshold left at 1, a run differs from today
  only where two packets wait at once (the register now shows both).

## 3. The master's firmware

**The owner's sequence:** the threshold is 1 between frames; after the header word it is the smaller of 256 and the
words left in the frame; at the frame's end, 1 again. For every frame, whatever its data's destination.

How the program keeps it:

- **`avail` replaces `pkt_left`:** the words the program knows are held and has not started to read. It loads
  `BACKGROUND_WORDS` only with no transfer of its own in flight and `avail` at 0, after a flag; then the register
  is exact (no word of it is already counted) and covers at least what Ingress owes. A flag that comes while
  `avail` is not yet used up is kept until it is.
- **The threshold** is written only when it changes: after the header word, min(256, length); when `avail` is used
  up mid-frame and min(256, words left) is below the value set, that (the tail: once or twice a frame); 1 when the
  frame ends. A frame of the header word alone never leaves 1.
- **The Updater's words (the pump):** one transfer of min(`avail`, the frame's words left for the Updater, the
  allowance) whenever that is not 0. With the threshold and the credit both 256, a wake, a credit and a transfer
  go together; with 31 words of the header's packet left over, a transfer is up to 287.
- **The firmware's own frames** (1, 3, 4 and 8's counts through the parser; 7 and 8 to the staging area and scrap):
  as today, with each piece min(`avail`, what is wanted, and for the parser its 32-word buffer). A header frame of
  up to 256 words comes in one wake and several transfers.
- **The check that a frame's header word is a packet's first word is dropped** (`MR_D_PACKET`, II's †, item 4).
  What a file whose frame ends inside a packet then does: the next frame's header word is simply the next word on
  the stream. Tested both ways (section 5).
- **Counts for the measurements:** the wakes (each WFI's return) in all and for each frame, into `master_results`;
  three instructions a wake.

## 4. The Updater

`QUEUE` and `HALF` become the segment queue's 512 and 256; the free page queue keeps 256 and 128. Credits are still
counted to depth 2, filled at the reset only. `test_updater`'s queue tests are moved to the new sizes, with a push
of the 513th entry as the overrun.

## 5. The tests

| what II and the prompt ask | where |
|---|---|
| thresholds of 1, part of a packet (5 of 32), several packets (100), the whole buffer (1,024) | `test_ingress_link`: the cycle of each flag, the register at it, against hand counts |
| a threshold written while words wait: lower (posts at once), higher (no flag), written while words are owed (nothing until they are read out) | `test_ingress_link` |
| a read-out across a packet's end; one transfer of every word held (several packets); one past them (UB, a death test) | `test_ingress_link` |
| the rule for a second flag: none before the words shown are read out, even with the threshold reached; then at once | `test_ingress_link` |
| both streams at once, different thresholds, independent | `test_ingress_link` |
| the buffer: 1,024 words held and the link waiting; the room freed word by word; a 32-word packet waiting for 32 | replaces `ingress_link_stream_holds_four` |
| the link at 2, 4 and 8 bytes a cycle: the arrival cycles | `test_ingress_link` |
| WORKER2's vectors and damaged files as before | `test_packet_vectors`, reading out by the words held |
| the firmware's tail: frames not a multiple of 256 or of 32 (the guests' frames, most of them), frames shorter than a packet (frames 1 and 4, the advice counts), a frame of the header word alone | `test_master`. **The last:** frame 6 with no update words, if gold makes it for a guest whose data image is its program image; if it cannot, a frame file made by the tests' writer. The report says which |
| a frame ending inside a packet | `test_master`: the guests' frames packed end to end into packets by the tests' writer: the images equal gold's |
| the threshold at 32, 64 and 128 | `test_master` on every fixture, and `proof_run --threshold` on the matrix's runs: images, records and results equal; cycles may differ |
| the planted bugs | `fw_teeth.sh`: the pump without the allowance re-pointed at the new line; added: the threshold not lowered at a frame's tail (the run hangs: the limit catches it), not set back to 1 at a frame's end, the register read with a transfer in flight |
| everything v1.37 passes | `./run_tests.sh all --check` here, `fw/build.sh --check`; `proof-real`, `proof-matrix`, `proof-29`, `all --check` and `make check` on the runner |

No golden log of a v1.37 step holds a cycle count (the steps are `check`s); v1.36's are untouched. The figures that
move are WORKER3's in its report and `RUNNING-v1.37.md`'s "about 30 million cycles": the report says which and why.

## 6. Measuring

On WORKER3's runs (`proof-real` 3, `proof-matrix` 24, `proof-29` 10), before (main as merged; WORKER3's Quest 204
figures, the same cycles) and after:

- the master's cycles awake a packet and their share of the run;
- the wakes a frame, and in all;
- the Updater's `STAT_STALL_DATA`;
- the link rate the firmware could keep up with: bytes on the link over its cycles awake before the hand-over.

Then the link at 2, 4 and 8 bytes a cycle, on `proof-29`'s ten runs and the matrix: the cycles to the hand-over,
the Updater's waits (data, pages, memory), the master's share, and the link's own waiting for room. The limit is
whichever stops shrinking first: the firmware (its share near 100% before the hand-over), the Updater's decode (a
64-bit word a cycle: 8 bytes a cycle of raw words at best), or the memory side (`STAT_STALL_MEMORY` growing to
fill the run). The report says which, at each rate.

Long runs on the runner: `updater_wip/tasks/v137-check/` with a step for the faster links. Quest's `NUMBERS.md`
re-read and the claim pushed just before each task, as the prompt says.

## 7. Order of work, one commit and push each

1. Ingress's black box and its tests; WORKER2's vectors; with the switch off, every v1.36 step unchanged.
2. The Updater's queue and its tests.
3. `host.h`, `master.c`, the image; `test_master` with the tail, the packing and the other thresholds; the teeth.
4. The harness's options; `all --check` here; the runner's tasks (proof steps, `make check`, the faster links).
5. The figures, `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, `REPORT.md` with the lists for II and HL.

## 8. Choices

Where the documents are silent, the simplest reading:

| | choice | why; the alternative |
|---|---|---|
| C1 | the buffer is 1,024 data words a stream; headers are not stored | II counts words; the header is stripped on arrival |
| C2 | a packet is taken off the link only when its data fits whole in the room left; room returns as each word is read out | "the link waits when a buffer is full"; per packet, as today. Half full for "stop" is HL's and not modelled (no way back) |
| C3 | "the words the register showed" are the words held in the cycle the flag is posted; Ingress keeps them as the words owed | the register is live, so the value the core loads may be larger; the firmware reads out all it loaded (C7), so the owed words are always read out first |
| C4 | a flag needs at least one word held: a threshold of 0 acts as 1. The threshold is 32 bits as written; one above 1,024 is never reached, which is the firmware's mistake, not an error | the register's width is not said |
| C5 | a threshold store takes effect at the end of its cycle, the slot's rule; a flag it causes is seen from the next cycle | v1.36's timing for every store |
| C6 | the stream is looked at on an arrival, at the last owed word's read-out, and at a threshold store: the same as checking every cycle | II's "then, and whenever the threshold is written" |
| C7 | the firmware loads `…_WORDS` only after a flag, with nothing of its own in flight and what it last loaded used up | then the count is exact; loading it at every flag would count words twice |
| C8 | the firmware writes the threshold only when it changes | each store is a cycle and a register write; the value is the same |
| C9 | the pump transfers min(available, the frame's words left for the Updater, the allowance), not whole packets | packets no longer matter to the firmware; a transfer of what the allowance covers keeps the queue fed |
| C10 | the credit-sized threshold is a word placed with the master's image (`system_config`, 0 meaning 256), so one image runs at 32, 64, 128 and 256 | three more images to build and commit, for a test |
| C11 | the wakes are counted by the firmware, in its results | the core model has no count of WFI returns; adding one touches v1.36's core |
| C12 | the link's faster rates as bytes a cycle, a second knob; `link_cycles_per_byte` stays for the tests that use it | one knob of either kind loses the other's cases |
| C13 | foreground: Ingress has its threshold; the firmware leaves it at 1 | II: "the foreground stream keeps its threshold at 1"; v1.37's files have no foreground |

## Questions

None yet: every † taken is workable as written. `QUESTIONS.md` is opened if one is not.

## 9. As built, where it differs from the plan

- **The header-only frame is gold's.** Gold already makes them: `kin-lru`'s frame 5 (the table added to its base with
  nothing new), `jumps-base`'s frames 6 and 9, `divs`'s frame 6, all 0 words. No writer was needed.
- **A fifth fixture, `kin-big`** (gold, `make_fixtures.sh`): keccak256 with 24 KB of untrusted advice, a data image of
  1,584 words. With the segment queue at 512 the old fixtures' largest frame for the Updater (334) no longer reached
  the allowance, so `fw_teeth.sh`'s "the pump without the allowance" would have gone uncaught; `kin-big`'s frame is
  larger than Ingress's buffer and three credits. The four old fixtures are made again byte for byte.
- **C14: the scrap is 256 rows, and frame 7's records are routed in runs.** The per-frame counts showed frame 7 (68 N
  words: the records) taking a transfer a record and 32 rows at most to scrap: 195 transfers for `kin-big`'s 4,352
  words. The scrap grew into the unused rows after it (`ML_SCRAP_ROWS` 256; the advice log starts 32 rows later, at
  0x48880), and the records before s, and those after s + 1, go as one run each: 24 transfers. Without it the
  threshold would gain nothing on frame 7.
- **The planted bugs.** "The count loaded with a transfer in flight" (C7 broken) is not planted: it counts words twice
  only when a flag comes in the middle of a transfer that took every word the master knew of, and the master then
  reads words that have arrived and loads an exact count again, so no run goes wrong (planted, it passed every test,
  at 1 to 8 bytes a cycle and thresholds of 1 to 64). `fw_teeth.sh`'s header says so, as it does for the handshakes
  whose races no run provokes. In its place: the threshold set to the frame's whole length (frame 7 is longer than
  the buffer: the master never wakes). Added and caught: the threshold not lowered at a frame's tail (caught by
  `divs` at 32, whose frame 9 of 51 words is the file's last), not set back to 1 at a frame's end (caught by the new
  slow-link test, where the master is ahead of the link at every frame), and the record-routing bug moved to the new
  code.
- **Tests added beyond the plan:** `master_slow_link` (4 cycles a byte), `master_fast_link_small_thresholds`
  (thresholds 1 and 32 as fast as taken), both on every fixture.
- **The master's cycles awake to the hand-over** are taken by the harness at segment s's run's done (the hand-over
  follows it); the link rate the firmware keeps up with is the link's bytes over them. They include the boot (about
  100,000 cycles of `map_format` and the LOADs), which is small at 2^29 and not on the guests.
- **The tasks:** Quest 206 (`suites`), 207 (`check`), 208 (`faster`, a new item of `v137-check`: the 2^29 and matrix
  runs at 2, 4, 8 bytes a cycle and the matrix at thresholds 32, 64, 128, in parallel).

