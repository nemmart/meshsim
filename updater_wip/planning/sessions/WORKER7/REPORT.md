# WORKER7 report: the forward count in v1.37

Branch `updater-w7`, from `main` at `a9ebf875`, with `main` merged at `96379529` (WORKER5's Part B) before any code.
The plan and the choices are in `NOTES.md` (the owner's go-ahead, 2026-11-16 23:49, with C1 in the owner's words);
section 9 there says where the work differs from the plan. No questions: every † was workable as written. Paths
without a folder are in `v1.37_wip/`. II is `INGRESS-INTERFACE.md`, UI `UPDATER-INTERFACE.md`, HL `HOST-LINK.md`.

**Status: built and checked here. The runs that need the workloads' data (the three proof steps, `make check`, the
faster links at 2²⁹) are not run: the Quest repository could not be attached to this session (below, "Not done").**

## What was built

| | what | where |
|---|---|---|
| Ingress's black box | queues of 512 words, one constant (`QUEUE_WORDS`); `DMA_FORWARD_COUNT` at word 82 ([30:0] count, [31] discard; a store replaces it; a load gives what remains); the thresholds and words 82/83 as WORKER4 had them gone, 83 unmapped; a word a cycle from the background queue to the port, or dropped, from the registers at the cycle's start; the service flag on the rise of II's asking level; a transfer in flight from `DMA_START` to `dma_done`; the three undefined cases stop the simulator | `src/units/ingress_egress.h` |
| the word port's wiring | in the slot: peripheral 1's source to peripheral 2's sink, found by their methods (`port_source`, `port_sent`; `port_sink`, `port_take`), whatever the types; both outputs of a cycle taken before any step, the move at its edge; a harness's sink for tests (`ext_sink`); not idle while a word can move. Two optional hooks: `out_end` (a transfer's end, `dma_done`'s cycle) and `settle` (the end of the cycle) | `src/units/periph_slot.h`, `.cpp` |
| the Updater black box's input | the sink: 4 words behind the port (C5), `ready` while fewer are held, whether a run is on or not; only the reset empties them. The segment queue, region part 1, `segment_credit` (`EVENTS` [2] reads 0) and the segment queue's part of `queue_overrun` gone; `STAT_STALL_DATA` is the input empty when a word is needed | `src/units/updater.h`, `.cpp` |
| the firmware | `host.h`: `DMA_FORWARD_COUNT`, its fields, `HL_QUEUE_WORDS`; the segment queue's words gone. `master.c`: section "The firmware" below. The image rebuilt with the pinned xPack `riscv-none-elf-gcc` 15.2.0-1: `fw/images/cc/master.elf`, 7,008 bytes of code (7.6 KB before), its hash in `fw/images/MANIFEST`; the other 17 images unchanged | `fw/include/host.h`, `fw/cc/master.c`, `master_layout.h`, `fw/images/` |
| the harness | the benches feed the Updater on its port (`updater_bench`: valid at random from a seed); `proof_run --threshold` gone; the figures: the count's stores, the words forwarded and dropped, the master's share of the hand-over's cycle, its cycles awake at each frame (`MR_FRAME_AWAKE`) | `tests/updater_bench.*`, `updater_frames_main.cpp`, `proof_bench.*`, `proof_run_main.cpp`, `proof_numbers.py` |
| the tests | below | `tests/test_ingress_link.cpp`, `ingress_bench.h`, `test_updater.cpp`, `test_master.cpp`, `fw_teeth.sh` |
| the task | `v137-check`'s `faster` item: the matrix as fast as taken in place of WORKER4's thresholds | `updater_wip/tasks/v137-check/` |
| the documents | `README.md`, `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, the units' header comments | |

### The firmware

- **A frame's header word** by Ingress's DMA, to DMEM, waited for to its landing (`dma_done` is masked off).
- **A frame of update words** (2, 5, 6): the last run's `done` if one is out, block 2, `START`, then
  `DMA_FORWARD_COUNT` = the frame's length, once. Its `done` is waited for by the next run only, so frames 7 and 8
  go on while frame 6's run does.
- **Frames 1, 3, 4** to the parser, copied as they arrive, up to 32 words a transfer.
- **Frame 7:** dropped up to record *s*; records *s* and *s* + 1 copied (word 66 of *s* to DMEM, as before); the rest
  dropped. **Frame 8:** the counts to the parser; dropped up to log *s*; log *s* copied; the rest dropped. Drops in a
  row are written as one count.
- **Frame 9:** "before *s*" started, then one count for the frame; at its `done`, segment *s*'s run (`continue`,
  `tracer_copy`); at its `done`, the rest's run and the hand-over.
- **Its discipline:** a count is stored, or a background transfer started, only with the count at 0 and no transfer
  of its own in flight. A wait for words loads the count and `BACKGROUND_WORDS` and sleeps until the count is 0 and
  words are there: a flag is a reason to look, never proof (O5). A copy of at most `BACKGROUND_WORDS` is then defined
  (O6).
- **Gone:** the allowance and the pump, the thresholds and `avail`, the segment queue's credit, the scrap area (its
  256 rows left unused so that no address after it moves), `system_config`'s threshold word (kept, unused).

The orders it relies on, with why each holds, are `NOTES.md`'s O1 to O6. Each is tested with the timing varied
(below).

## The checks

| check | where | result |
|---|---|---|
| **`test_ingress_link`**, 43 tests: the count written before, during and after the words arrive, each word's cycle by hand; a count across 40 packets of 0 to 32 words, sent and dropped; a store of 0 mid-frame; a store replacing a running count; a load giving what remains, `discard` as written; the sink's `ready` at random, a move exactly where valid and ready are both 1, 1,900 words once and in order, the link held too; forwarding beside a foreground transfer in the same cycles (a file with both streams); 512 words holding the link, room freed by the port and by the drop, the 17th packet 528 cycles after; every line of the service rule for both queues, with a case that would post a flag too many (words arriving while it asks, while the count runs) and one too few (a transfer ending with words left, the count reaching 0 with words there), and a transfer of 0 words (C6); the three undefined cases, and a count stored the cycle after `dma_done` defined; the link's checks, errors and timing as before | here | **pass** |
| WORKER2's vectors (`test_packet_vectors`) | here | all agree |
| **`test_updater`**, 23 tests: the port's 4 words, idle or busy; kept across `START`, emptied by the reset; part 1 now UB; `EVENTS` [2] 0; every earlier test moved from the queue to the port; `continue` across runs and gold's fixtures with `valid` at random | here | **pass** |
| **`test_master`**, 34 tests: every fixture at several *s*, with the cache's controller (WORKER5's 6), the error path, the cut file, the teeth; and for the count: `kin-big`'s 1,584-word frame (three times the queue; at most 6 wakes for it); `divs` at *s* 60 (log *s* empty, logs before and, made in the test, after it); **every fixture at five timings** (the link as fast as taken with the Updater's look-up at 3,000 cycles and at 6; 8 bytes a cycle at 300; 4 cycles a byte at 6 and at 3,000); frames ending inside packets; 2, 4 and 8 bytes a cycle. In each: every image equal to gold's, `PAGE_COUNT` 0 at every `done`, no error bit, the link read to its end and every word of it copied, forwarded or dropped, the count 0 at the end | here (clang and gcc) | **pass** |
| **`fw_teeth.sh`**, 20 bugs: in `master.c`, the 9 earlier ones that still apply (the record's rewritten for the new code) and 9 on the count (one long, frame 9's one short, an update frame dropped, the drops sent to the Updater, drops in a row as two counts, a copy without the count's 0, a copy's landing not waited for, a run without the last `done`, the wrong log); in `host.h`, the service flag's bit and `DMA_FORWARD_COUNT`'s discard at bit 30. WORKER4's bugs on the allowance, the thresholds and the segment queue are gone with them | here | **every one caught**; 3 of the new ones by the simulator's stop on an undefined case. "Drops in a row" is caught only by `master_divs_empty_log` with logs after *s* (NOTES section 9) |
| `fw/build.sh --check` | here | 18 images equal to `fw/images/` |
| **`./run_tests.sh quick --check` and `all --check`** (at `6dc61724`'s tree; after it only the master's per-frame stamps changed, and `test_master`, the teeth and `fw/build.sh --check` were run again on the final image) | here, gcc (clang's ASan runtime is missing on this machine, so `suite_bus`'s debug build fails with clang before any step; it is not this work's) | **all passed**; no log differs. 5 steps SKIPPED: the workloads' data (`tracer-block`, `segment-real`, `proof-real`, `proof-matrix`, `proof-29`) |
| `proof-real`, `proof-matrix`, `proof-29`; `make check`; the faster links at 2²⁹ | the runner | **not run** (below) |

## The figures

The simulator's cycles, with the black boxes' timing (the link a byte a cycle; a page read or written 336 cycles),
not the RTL's. **Only the guests could be run here.** Their full tables, before and after, are `GUESTS-FIGURES.md`.

### The guests at a byte a cycle, before (WORKER4's threshold at 256) and after

| guest, *s* | wakes, before → after | to the hand-over | the master awake to the hand-over | of it, at frame 1 (the boot) | after the boot |
|---|---|---|---|---|---|
| kin-raw, 20 | 83 → 52 | 74 → 43 | 121,166 → 108,528 | 85,942 | about 35,200 → 22,600 |
| kin-lru, 60 | 82 → 54 | 73 → 45 | 122,124 → 110,024 | 85,942 | about 36,200 → 24,100 |
| jumps-base, 0 | 79 → 46 | 71 → 38 | 121,321 → 109,217 | 85,942 | about 35,400 → 23,300 |
| divs, 20 | 81 → 54 | 72 → 45 | 121,107 → 108,897 | 85,942 | about 35,200 → 23,000 |
| kin-big, 20 | 97 → 51 | 88 → 42 | 124,834 → 108,909 | 85,942 | about 38,900 → 23,000 |

("Before" at frame 1 is the same boot: the program up to the first frame did not change.)

**The wakes a frame**, after (wakes / Ingress's DMA transfers / cycles awake, `kin-big` at *s* 20):

| frame | 1 | 2 (program) | 3 | 4 | 5 (table) | 6 (data, 1,584 words) | 7 (records) | 8 (advice) | 9 (to the hand-over) |
|---|---|---|---|---|---|---|---|---|---|
| after | 2 / 2 / 714 | 2 / 1 / 519 | 5 / 4 / 14,244 | 2 / 2 / 674 | 2 / 1 / 613 | 4 / 1 / 1,198 | 13 / 8 / 1,401 | 2 / 2 / 2,951 | 3 / 0 / 695 |

- **A frame of update words costs the master 2 to 4 wakes and 450 to 1,200 cycles awake, whatever its length.** Before,
  `kin-big`'s data image alone was 7 transfers and their wakes; at 2²⁹ WORKER4 counted 1,344 wakes for EVM Zone's
  program image and 5,843 for Cyclone's frame 9 (WORKER4-REPORT.md). Those become 2 and 3.
- **What is left on the guests is per proof, not per word:** the boot (85,942 cycles: `map_format`'s 128 KB, the
  LOADs), frame 3's parse (about 14,200 at *N* 64, a 32-bit word at a time), frame 8's counts (about 3,000), frame 7's
  copies of two records (about 1,400), and the pages queued for the runs (a page of the map and a push each).
- **Over the link's time** the guests are no measure: their frames are about 100 KB, so after the boot the master is
  awake for 20 to 26% of the link's time to the hand-over at a byte a cycle, all of it those fixed costs. The owner's
  10% is a figure for the real workloads, where the link's time to the hand-over is 3.9 M cycles (EVM Zone 2²⁹) and
  16.9 M (Cyclone 2²⁹).
- **An estimate for 2²⁹, not a measurement:** the fixed costs scaled to *N* 128 (the boot 86,000; frame 3 about 28,000;
  frame 8 about 6,000; the rest about 5,000) and the pages at about 50 cycles each (WORKER3's 966 to 1,030 for EVM
  Zone, 3,827 to 4,100 for Cyclone) give about 175,000 cycles awake to the hand-over on EVM Zone (4.5% of 3.9 M; 2.3%
  after the boot) and 325,000 on Cyclone (1.9%). Before: 0.17 to 0.20 of the link's time (WORKER4). **At a byte a
  cycle the 10% looks met with room to spare; the runner's figures are what says so.**
- **With a faster link** the hand-over comes sooner and the fixed costs stay, so the share rises: on the guests, after
  the boot, 23 to 26% at 1 byte a cycle, 52 to 54% at 4, 65 to 67% at 8. At 2²⁹ the Updater limits the link from 2 to
  4 bytes a cycle (WORKER4: the link's last byte at 2.1 M cycles for EVM Zone at 8), so by the same estimate the share
  there would be about 8% with the boot and 4% without. **The limit is still the Updater**, its memory work first; the
  firmware's cost no longer grows with the link's bytes.
- **The hand-over comes about 3% later on the guests at a byte a cycle** (199,006 against 192,515 for `kin-raw`): the
  queues are 512 words where WORKER4's were 1,024, and the link stalls sooner while the master boots (105,249 cycles
  waiting for room against 98,019). It is the queue's size, not the count; at 2²⁹ the boot is 2% of the link's time.

## Not done

- **The runner's runs.** Attaching `nemmart/quest` to this session (needed to claim Quest numbers and push the
  tasks) was refused by the session's permission check, so `proof-real`, `proof-matrix`, `proof-29`, `make check`
  and the `faster` item were not run. The tasks are ready: `updater_wip/tasks/v137-check/make_task.sh NNN-name
  suites|check|faster` on this branch's head. Their figures (the master's share at 2²⁹ at 1, 2, 4 and 8 bytes a cycle,
  set beside WORKER4's Quest 206 and 208) are what the 10% is to be judged on.
- `make check` (four builds and TSan) was not run here either: two CPUs, and clang's ASan runtime is missing.

## What I would change in INGRESS-INTERFACE.md (for the owner)

1. **"The rule for a service flag":** add the owner's ruling: a transfer is in flight from a queue while the DMA is
   busy with a transfer whose `DMA_HDR.queue` names it, from `DMA_START` to `dma_done` (`STATUS.dma_busy`). And say
   the rule is one level, "asks", with the flag posted when it rises: that is the three lines exactly, and a reader
   cannot get a special case wrong.
2. **A transfer of 0 words** counts as a transfer: at its end, with words waiting, a flag (C6). Say so, or say it
   posts none.
3. **The forward count, per cycle:** "from the registers at the start of the cycle": a word that arrives at the end
   of t is the queue's from t + 1 for both ways out (C4). **`discard` stays as written when the count reaches 0, and a
   load reads both fields** (C3).
4. **For firmware writers,** three sentences: a flag is a reason to look, never proof (a transfer's end can post one
   that is still pending when the next count is stored, O5); a load of the count that reads 0 is exact, more may be
   stale; and drops in a row must be one count, since a store replaces a running count.
5. **"There is no event for the count reaching 0" (Open):** none is needed. The firmware learns it from the next
   flag or the Updater's `done`, and a frame of update words costs it 2 to 4 wakes.
6. **The next frame's flag can come before the last run's `done`:** the count reaches 0 when the last word has entered
   the Updater, not when it is decoded. Worth a line under "Frames", since the firmware must wait for the `done`
   before the next run's block 2.
7. **The register table:** `STATUS` [3], [4] "the queue holds words"; word 83 unmapped.
8. **"In v1.37":** the right column is now as built.
9. **"Holding the host back":** the link's next packet waits until its data fits in the room left (WORKER4's C2,
   unchanged). It is HL's "stop", which v1.37 does not model.

## What I would change in UPDATER-INTERFACE.md (for the owner)

1. **"The data input":** "a few words": the black box keeps 4, with `ready` a flip-flop; 2 would keep a decoder of
   one word a cycle fed. Say that the number is the controller's, and that nothing outside depends on it.
2. **A frame's padding:** say that it is always under 128 bits, so it is never a whole word on the input: the next
   frame's run with `continue` 0 clears it from the decoder, and nothing is left in the input (HOST-FRAMES.md makes
   it so; the firmware relies on it, O1).
3. **`STAT_STALL_DATA`:** "cycles waiting for a word on the data input", as built: the input empty when the decoder
   needs a word.
4. **"In v1.37":** the right column is now as built. `EVENTS` [2] reads 0.

## What I would change in word_port.h (for the owner)

1. Nothing is wrong. One addition: **the method names the slot wires**: a source has `port_source()` and
   `port_sent(t)`, a sink `port_sink()` and `port_take(t, word)`, and the slot calls `port_sent` and `port_take` after
   every step of the cycle the word moves in. WORKER6's `UpdaterTop` needs those two names to take the black box's
   place behind the port; today they are in `periph_slot.h`'s header, not here.
