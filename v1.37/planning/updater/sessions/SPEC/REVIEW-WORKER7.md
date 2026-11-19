# Review request: WORKER7, Ingress/Egress and the master tile's firmware

*From the specification session (`sessions/SPEC/`), branch `v137-spec`, at the commit that adds this file. You built
the forward count, the word port, the service rule and the firmware's discipline with Ingress, in the black box and
in `fw/cc/master.c`.*

**17.6 follows the design, not the black box** (the owner, 2026-11-18: for Ingress/Egress, the UART, Egress and the
frames, the design documents lead). The black box is knowingly behind it: its register numbers, the old stream
words, v1.36's UART, queues of 8 KiB, a bad link packet being fatal, no Egress, no chain. **Every such difference
belongs in your second list,** not the first; `sessions/SPEC/FOR-THE-SIMULATOR.md` already has the ones I found.

## What to read

All in `v1.37_wip/spec/`:

| Section | What it states |
| --- | --- |
| `machine-spec.md` 17.6, 17.6.1–17.6.15 | Ingress/Egress: its words and bits (packed), the control lines, the incoming queues, the DMA, **the forward count (17.6.5)**, **the word port (17.6.6)**, **the service flags (17.6.7)**, Egress, the UART, its registers, errors, what software keeps, reset, the simulator's checks |
| `master-firmware.md`, 30.1–30.9 | the master tile's firmware as a contract: the boot, **the frame loop (30.3)**, the Updater's runs and the free page queue, the hand-over, the segment, the reply, an error, the cost |
| `host-frames.md` 29.2, 29.14 | every frame's rules; the reply |
| `machine-spec.md` 20 (invariant 35), 21.5 (the forward count's, the service flag's and Egress's rows) | the gathered invariant and timing |

## What I derived and did not run, or could not fully check

1. **The service rule as one level** (17.6.7), "asks" posting a counted flag in the cycle it rises, with "in flight"
   as `STATUS.dma_busy` with `DMA_HDR.queue` naming the queue, and a transfer of 0 words counting.
2. **The forward count's cycle** (17.6.5): a cycle works from the registers at its start; a word arriving at a
   cycle's end is the queue's from the next for both ways out; `discard` stays as written when `count` reaches 0;
   a store takes effect as any window store does (6.4).
3. `valid` on the word port as exactly `count` ≠ 0, `discard` 0 and a word at the head of the queue (17.6.6).
4. "A count beyond the words in the queue at `DMA_START` is UB" (17.6.4): judged at `DMA_START`.
5. That `FOREGROUND_WORDS` and `BACKGROUND_WORDS` are 11 bits, 0 to 1,024, and a word counts as it lands.
6. **30.3's frame loop:** the dispatch table; that frames 7 and 8 go on while frame 6's run does; and **"any other
   type: dropped with one discard count",** which is `INGRESS-INTERFACE.md`'s and not `master.c`'s (it ends in its
   handler, `MR_FRAME`).
7. **30.5's hand-over:** what the tracer tile's load carries, and that the load rides the line between the special
   tiles (16.1).
8. **30.6:** the tracer tile's result carrying its last record to the master tile (the owner's ruling MS-2);
   `tracer_ctl.c` does not do this today.
9. **30.7's reply:** 72 words, `EGRESS_COUNT` 72, written into part 1 of region 0x04 from one sending unit, within
   one credit. Egress is not built.
10. 30.9's figures are quoted from your Quest 213 table in `INGRESS-INTERFACE.md`, not re-measured.

## What I ask of you

Two lists, each item with the section, and the line of the black box or the firmware where it applies:

1. **Where the specification misdescribes what the design, or the parts you built to it, do:** the forward count,
   the word port, the service rule, the firmware's discipline; a rule stated wrongly, a case left out that a
   builder needs.
2. **Where the specification is right and the black box or the firmware is wrong, behind or silent:** every
   difference from the design, the known ones included where `FOR-THE-SIMULATOR.md` misses or misstates them.

Please do not edit `spec/`: put both lists in `sessions/SPEC/REVIEW-WORKER7-ANSWER.md`, and the specification session
applies them.
