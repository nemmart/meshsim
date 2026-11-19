# WORKER7 notes: the forward count in v1.37

Branch `updater-w7`, from `main` at `a9ebf875`. Paths without a folder are in `v1.37_wip/`. II is
`INGRESS-INTERFACE.md`, UI `UPDATER-INTERFACE.md`, HL `HOST-LINK.md`, HF `HOST-FRAMES.md`.

**Status: the owner's go-ahead (2026-11-16, 23:49): "Go ahead with the plan as written." C1 approved, in the owner's words below; `main` merged first (WORKER5's Part B, the cache's controller behind `cache_ctl`).**

## 1. What changes, and where

| piece | files | what |
|---|---|---|
| Ingress's black box | `src/units/ingress_egress.h` | each queue `QUEUE_WORDS` = 512 (one constant); words 82 and 83 (the thresholds) and the owed-words rule go; **`DMA_FORWARD_COUNT` at word 82** (section 2); the source side of the word port; the service rule as II has it; the three UB cases stop the simulator |
| the slot | `src/units/periph_slot.h`, `.cpp` | the wiring (section 4): peripheral 1's source to peripheral 2's sink, found by the methods they have, not by type; and an optional hook `out_end(t, q)` so that Ingress knows when a DMA transfer has ended (the slot's end, `dma_done`'s cycle) |
| the black box Updater | `src/units/updater.h`, `.cpp` | the segment queue, region part 1, `segment_credit` (`EVENTS` [2], now reads 0) and the segment queue's part of `queue_overrun` go; the sink side of the port with **4 words** behind it (section 3); `STAT_STALL_DATA` counts cycles with that buffer empty. Nothing else |
| the firmware's header | `fw/include/host.h` | `DMA_FORWARD_COUNT` and its discard bit in place of the thresholds; the segment queue's words and credit gone; `HL_QUEUE_WORDS` 512 |
| the master's program | `fw/cc/master.c`, `master_layout.h` | section 5. The allowance, the pump, the thresholds, `avail`'s bookkeeping, the segment-queue credit and the scrap area go |
| the image | `fw/images/cc/master.elf`, `fw/images/MANIFEST` | rebuilt with the pinned xPack 15.2.0-1; the other 17 images unchanged |
| the harness | `tests/proof_bench.*`, `proof_run_main.cpp`, `proof_numbers.py`, `updater_bench.*`, `updater_frames_main.cpp` | `--threshold` goes; the benches feed the Updater on its port; the figures of section 7 |
| the tests | `tests/test_ingress_link.cpp`, `test_packet_vectors.cpp`, `test_updater.cpp`, `test_master.cpp`, `fw_teeth.sh`, `packet_writer.h` | section 6 |
| the documents | `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, the units' header comments | brought up to date |
| the task | `updater_wip/tasks/v137-check/` | reused: `suites`, `check`, `faster` (the threshold sweep replaced by nothing; the rates 2, 4, 8 kept) |

Not touched: `v1.4_wip/`, `word_port.h`, gold, the packetizer, WORKER5's and WORKER6's units, every other image.

## 2. Ingress: the forward count

State per queue: a ring of `QUEUE_WORDS` words; whether a DMA transfer from it is in flight (from `out_start` to the
slot's `out_end`); the asking level as it stood at the end of the last cycle. For the background queue also
`DMA_FORWARD_COUNT`.

- **The count** (word 82, R/W, 0 at the reset): [30:0] `count`, [31] `discard`. A store presented in p replaces it at
  the end of p (the slot's rule), so it acts from p + 1. A load reads it as it stood at the end of the last cycle.
- **In cycle t,** from the registers at its start: if `count` is not 0 and the queue holds a word, the head
  - with `discard` 1: leaves at the end of t, `count` − 1;
  - with `discard` 0: is offered on the port (`valid` 1, `data` the head); it leaves, and `count` − 1, at the end of
    t if the sink's `ready` was 1 in t.
  A word that arrives at the end of t is offered from t + 1 (it is at the tail; the head is unaffected).
- **The service rule as a level:** a queue *asks* while it holds words, no DMA transfer from it is in flight, and
  (for the background) `count` is 0. The flag is posted at the end of a cycle in which the level rises. That gives
  II's three lines exactly: a word into an empty queue (rises), `count` reaching 0 with words (rises), a DMA
  transfer ending with words left (rises: it was 0 while in flight); and nothing for words that arrive while it
  already asks (no rise). The level is evaluated at the end of every cycle the slot steps, after the port's move,
  the DMA's take and the link's arrivals, and after a count store.
- **The UB cases stop the simulator** (FATAL, as the DMA's do): `DMA_START` from the background queue with `count`
  not 0; a count store while a DMA transfer from the background queue is in flight; a DMA count beyond the words in
  the queue (kept from today).
- **The link** waits while a packet's data does not fit in the room left (WORKER4's C2, unchanged), now 512 words.
- **Idle:** Ingress is not idle while it is discarding (count not 0, `discard`, a word held). A send that can move
  wakes the slot through the port (section 4).

## 3. The black box Updater's input

- **4 words behind the port** (`INPUT_WORDS`): `ready` in cycle t is "fewer than 4 held at the start of t", a
  flip-flop. A word that moves in t is pushed at the end of t, after the Updater's own step of t, so its decoder sees
  it from t + 1.
- **Why 4:** the decoder takes at most 66 bits a cycle, under one word, so with `ready` registered 2 words already
  keep it fed at the port's rate; 4 is WORKER6's input FIFO (its C1), so the black box and the real controller hold
  the same backlog and the whole chip's timing differs between them only by the controllers. Alternative: 2.
- **`ready` does not depend on `busy`:** an idle Updater fills its 4 words and they wait. Only the reset empties
  them; `START` does not.
- The decoder's `fill` pops this buffer instead of the segment queue; `STAT_STALL_DATA` is the cycles it is empty
  when a word is needed (UI: "waiting for a word on the data input").
- Region part 1 becomes unmapped: a `WRITE` there is UB, as parts 0 and 3 to 7 are. `EVENTS` [2] reads 0.

## 4. The wiring

In `SlotCore::run_cycle(t)`, before any peripheral steps: if peripheral 1 has a source (`port_source()`, Ingress
with the host link) and peripheral 2 a sink (`port_sink()`), take both outputs of t. After every peripheral has
stepped t: if `wordport::moves(src, snk)`, call `source.port_sent(t)` and `sink.port_take(t, src)`. The slot asks
only for those four methods (a `requires` test, as it does for `stopped()`), so **any Updater with a sink** is
wired the same way: the black box, or WORKER6's `UpdaterTop` once its shim is deleted. Its idle test adds: not idle
while a move is possible.

This is the only change to the slot besides `out_end`. I will tell WORKER6 the four method names through its notes'
reader (the integrator), not in its files.

## 5. The master's firmware

**Its discipline:** a count is stored, or a background DMA started, only when `count` is 0 and no background DMA is
in flight. It learns that `count` is 0 by loading it: a load of 0 is exact (only the core raises it), a load of
more may be stale. A flag is a reason to look, never proof: on each wake it loads the count and
`BACKGROUND_WORDS` and acts only on `count` 0 and words waiting.

| frame | what the firmware does |
|---|---|
| any: the header word | on the flag, with `count` 0 and a word waiting: one word by the DMA to DMEM, on `CTR_LINK`; `dma_done` masked off (only the landing matters, II) |
| 1, 3, 4 (headers) | to the parser: copy min(`BACKGROUND_WORDS`, the frame's words left, 32) a transfer, as they arrive (a flag when a transfer ends with words left, or a word arrives in an empty queue) |
| 2, 5, 6 (update words) | wait for the last run's `done` if one is out; block 2, `START`, then `DMA_FORWARD_COUNT` = the frame's length; on to the next frame. The run's `done` is waited for only by the next frame that needs the Updater |
| 7 (records) | discard 68 *s*; copy records *s* and *s* + 1 (word 66 of *s* to DMEM, as today); discard the rest |
| 8 (advice) | copy the *N* counts (*N*/4 words) to the parser; discard the logs before *s*; copy log *s*; discard the rest |
| 9 (segments) | as 2, 5, 6 for the first run, with the count the frame's whole length; then the second and third `START`s at the dones (below) |

**Frame 9's three runs.** Order: block 2 for "before *s*", `START`, the count (the frame's length), once. At its
`done`: block 2 for segment *s* (`continue`, `tracer_copy`), `START`. At its `done`: block 2 for the rest
(`continue`), `START`, then the hand-over. Nothing is lost or taken twice between runs because: the port moves each
word once, and the count is written once; the Updater's sink takes words whether a run is on or not, and a `START`
does not empty its 4 words or, with `continue` 1, its decoder's bits and LRU table; and each run takes exactly
`WORD_COUNT` update words, its decoder filling only the bits the next word needs. Between runs the words wait in
the Updater's 4 and in Ingress's queue, and the link is held when that is full.

**The orders it relies on** (each tested with the timing varied, section 6):

| | the order | why it holds |
|---|---|---|
| O1 | the count against `START` | either order is safe (section 3: the sink takes words when idle and only a run decodes them); the program does `START` first so that the root is made while the first words arrive. A `continue` 0 run clears only the last frame's padding, which is in the decoder's bits, never a whole word in the buffer (HF: padding is under 128 bits) |
| O2 | a run's `done` against the next frame's flag | the next frame's flag can come first: `count` reaches 0 when the last word has entered the Updater, not when it is decoded. A frame for the Updater waits for that `done` before block 2 (a store during a run is UB); other frames (7, 8) go ahead, and the run overlaps them. The next frame's count is written after that `done`, so none of its words enter the Updater before the last run has taken its own |
| O3 | a copy against the next count | a count store waits for the copy's transfer to have ended (UB otherwise): the program waits for its landing, which follows the transfer's end |
| O4 | a count against the next copy | a background DMA starts only after a load of `count` reads 0 |
| O5 | a flag against a count store | the flag a transfer posts as it ends (words left) may still be pending when the program stores a count; the wake it causes finds `count` not 0 and sleeps again (the check above) |
| O6 | `BACKGROUND_WORDS` against a copy | loaded with `count` 0 and no transfer in flight, it is a lower bound on what is there, so a copy of at most that is defined |

**What goes:** the allowance and the pump, the thresholds and `avail`, the segment-queue credit, the scrap area
(`ML_SCRAP`; the advice area stays where it is so that no other address moves), `MS_H_THRESHOLD` (the word stays,
unused, so that `system_config`'s layout does not move). `MR_THRESHOLD_STORES` becomes `MR_COUNT_STORES`.

## 6. The tests

| what the prompt asks | where |
|---|---|
| **Ingress:** the count written before, during and after the words arrive; a count across many packets; send and discard; a store of 0 mid-frame; a store that replaces a running count; a load giving what remains | `test_ingress_link`, with a test sink on the port, each word's cycle by hand |
| the Updater's `ready` coming and going at random (seeded), with every word arriving once, in order | `test_ingress_link` |
| forwarding while a DMA transfer runs from the foreground queue, in the same cycles | `test_ingress_link`, a file with both streams (the tests' packet writer gains a stream option) |
| a full queue holding the link: 512 words, the room freed word by word by the port and by the discard | `test_ingress_link` (replaces the 1,024-word test) |
| every line of the service rule, with a case that would post one flag too many (words arriving while it already asks, or while the count runs) and one that would post one too few (a transfer ending with words left; `count` reaching 0 with words already there) | `test_ingress_link` |
| the three UB cases | death tests |
| WORKER2's vectors | `test_packet_vectors`, read out by the DMA as today |
| **the Updater:** the port's `ready` (4 words, idle or busy), the buffer kept across `START`s and emptied at the reset, part 1 now UB, `EVENTS` [2] 0, every existing test moved from the queue to the port | `test_updater` |
| **nothing the chip builds changes:** `test_master`, `proof-real`, `proof-matrix`, `proof-29`; every image equal to gold's, `PAGE_COUNT` 0 at every `done`, no error bit; the link at 1, 2, 4, 8 | here (`test_master`) and the runner |
| **the orders, with the timing varied:** every fixture with the Updater's memory slow (O2: the next frame's flag long before `done`) and fast; the link slow and as fast as taken; frames ending inside packets | `test_master` (WORKER4's variants kept, the threshold ones dropped) |
| **planted bugs**, replacing those on the allowance, the thresholds and the segment queue: a frame's count one short; one long; send for discard (frame 7's leading discard sent to the Updater); discard for send (frame 6); the wrong record kept (68 (*s* + 1) discarded); the wrong log kept; a count stored with the copy still in flight (the wait removed: UB, caught by the stop); a count stored without a load of 0 first (frame 8's rest written over the leading discard); the next frame's `START` without waiting for the last `done` | `fw_teeth.sh master:` |
| `./run_tests.sh all --check`, `make check` | here and the runner |

## 7. The figures

On WORKER4's runs (`proof-real` 3, `proof-matrix` 24, `proof-29` 10) at a byte a cycle, and `proof-29` and the
matrix at 2, 4 and 8, beside WORKER4's (Quest 206 and 208):

- the master's cycles awake to the hand-over, over the link's time to then (the owner's 10% is this ratio);
- the wakes a frame, and in all;
- the Updater's waits (data, pages, memory), the link's waits for room, the link's last byte;
- where the limit is at each rate (expected: the link at 1, the black box Updater from 2 or 4, as WORKER4 found,
  the firmware nowhere near).

## 8. Order of work, a commit and push each

1. Ingress's black box, the slot's wiring and `out_end`, `test_ingress_link` with a test sink; with the switch off,
   every v1.36 step unchanged. (The whole chip does not run between 1 and 3: the firmware still uses the
   thresholds.)
2. The Updater's input, `test_updater`, the benches and `updater_frames` on the port.
3. `host.h`, `master.c`, the image; `test_master`; the teeth.
4. The harness's figures; `all --check` here; the runner's tasks.
5. The figures, `RUNNING-v1.37.md`, `CHANGES-v1.37.md`, `REPORT.md` with the lists for II, UI and `word_port.h`.

## 9. As built, where it differs from the plan

- **The link's arrivals are taken at the end of the cycle** (`settle`), after the port's move, not in Ingress's step.
  With the move at the edge, a word sent in t made room only after the step of t, and the link started a cycle later
  than after a DMA's take. Now the next packet's bytes start in the cycle after the one that made room, whichever
  way out made it, as WORKER4 had it for the DMA. Every timing test of `test_ingress_link` is unchanged.
- **The firmware merges drops in a row into one count** (`skip`, then `drop_now` before the next copy or at the
  frame's end), which O3 needs: the only place two drops meet is frame 8 with log *s* empty. `divs` at *s* 60 has
  such a log, but no log after it; `test_master`'s `master_divs_empty_log` gives segments 61 to 63 a row each
  (values no ASIC at *s* 60 uses), and the planted "drops in a row as two counts" is caught there and nowhere else.
- **The master stamps its cycles awake at the first frame** (`MR_AWAKE_FIRST_FRAME`, word 123; it counts its own
  cycles in WFI), so that the figures can set the boot (`map_format`'s 128 KB and the LOADs, about 86,000 cycles)
  apart from the frames.
- **`test_master`'s timing variants:** `master_orders_with_the_timing_varied` (every fixture at five timings: the
  link from 4 cycles a byte to as fast as taken, 8 bytes a cycle, the Updater's look-up 6, 300 or 3,000 cycles)
  replaces WORKER4's threshold sweeps.
- **The runner's `faster` item** runs the matrix with the link as fast as taken in place of the thresholds.

## Choices

| | choice | why; the alternative |
|---|---|---|
| C1 | **a DMA transfer is in flight from a queue while the DMA is busy with a transfer whose `DMA_HDR.queue` names that queue, ending at `dma_done`** (the owner, 2026-11-16: `STATUS.dma_busy` is the signal the hardware has). The slot tells Ingress the end by a new optional hook `out_end(t, q)`, in the cycle `dma_done` is set | II's "when a DMA transfer ends" is `dma_done`'s "the transfer has ended". The alternative, until the queue's last word is taken, needed no slot change but posted the flag a cycle or more before `dma_done` |
| C2 | the service flag is posted on the rise of the asking level, evaluated at the end of each cycle | it is II's three lines exactly, with no special cases to get wrong |
| C3 | `discard` stays as written when `count` reaches 0; a load reads both fields | it is one register; the firmware masks [30:0]. Alternative: the whole register to 0 |
| C4 | a discarded word leaves only if it was in the queue at the start of the cycle | "every output a flip-flop": a word arriving at the end of t is the queue's from t + 1, for both ways out |
| C5 | the Updater black box keeps 4 words behind the port | section 3 |
| C6 | a DMA transfer of 0 words from a queue counts as a transfer: the level falls while it is in flight and rises at its end, so a flag is posted if words wait | II's line 3: "the next one comes only when the core has started something that takes words from the queue, and that has ended". Alternative: no flag for a count of 0. The firmware never starts one |
| C7 | a count store is presented to Ingress like any store (in effect at the end of its cycle), and so is a load's view of it | the slot's rule for every window word |
| C8 | the wiring is between peripherals 1 and 2 of a slot, whichever types have a source and a sink | II and UI put both on the master tile; the slot then cares only about the methods (section 4) |
| C9 | the firmware starts a run before writing its count (O1) | the root is made while the first words arrive; the other order works too |
| C10 | frames 7 and 8 have no scrap area: what is not the ASIC's is discarded | the brief; the scrap rows stay unused, so no address in the layout moves |
| C11 | the firmware treats a flag as a reason to look (O5) and acts on `count` 0 and words waiting | a flag posted by a transfer's end can be pending when a count is stored; masking it at the right moment is harder to get right than a load |

## Questions

None yet: every † is workable as written. `QUESTIONS.md` is opened if one is not.
