# DECISIONS-v1.26: v1.25 plus the replayer changes designed for v1.3

**v1.26 is v1.25 with exactly the replayer changes decided for v1.3** (owner, 2026-11-01), in the spec, meshsim and the
software, and nothing else: no Tracer, no new memory system, no file system. The changes come from `DECISIONS-v1.3.md` sections
12, 23 and 24 (cited as V3-12, V3-23, V3-24). The work is done in `wip/` and frozen as `v1.26/` like every version.

## 1. The changes (owner, 2026-11-01)

1. **The state record grows to 68 words, in a new order** (V3-23; owner, 2026-11-01): **[0] the state word, [1] the stream word,
   [2-65] the register image, [66] the files word** (the memory file's and the map file's roots and depths, carried and ignored),
   **[67] the template bitmap** (moved from word 0 to last). The whole record lands in part 0 at consecutive offsets from 0x000,
   the first three where v1.25 has them; one record format for the host, the Tracer and the replayer.
   **With the files word and the bitmap last, the replayer needs only words 0-65** (owner, 2026-11-01), exactly v1.25's part-0
   image: it ignores writes to words 66-67, and a controller may simply not send them (the tile's controller reads the bitmap from
   the record itself, to trim its templates). So in converted deliveries the files word's content doesn't matter (zero).
2. **A stream's size is two numbers: its 128-bit words and its values** (V3-24; owner, 2026-11-01: bits aren't useful to anyone):
   **the words** as sent, padding included (what the controller sends and the queue holds), and **the values out** (the 32- or
   64-bit values it decodes to, which is where a decoder stops, so padding never matters). The stream word (word 1; v1.25: three
   bit lengths, code [23:0], read log [55:32], write log [87:64]) carries both for all three streams, **and each stream's
   compression flag** (owner, 2026-11-01), moved from the state word's [112]-[114], since it describes the stream: 16 + 17 + 1
   bits per stream, 102 bits for the three; its layout is to propose (a stream is at most about 33,800 words and 65,536 values).
3. **The read-log and write-log queues shrink from 8 KB to 4 KB** (V3-12), so every queue is 4 KB. Justified by the measured
   densities (spec 4.19.15); **confirm by meshsim's Stage B stall counts on both workloads.**
4. **Writing the state flushes every queue** (V3-24), in place of `PULSE_RUN` emptying them (spec 4.19.8). **The state stays a
   memory**, address-mapped in part 0 as at v1.25, not a queue (owner, 2026-11-01: the same on the Tracer and the replayer; the
   hardware is simplest, one-word updates and readback stay, and the DMA writes consecutive offsets anyway), and **the flushing
   write is the write to word 0, the state word, at part 0 offset 0x000** (WORKER20's proposal, owner): state-first loading writes it
   first, and a per-pass flag change resends only it, so every pass starts with clean queues.
5. **A stream's padding can be anything**, compressed or not (V3-24): streams arrive as whole 128-bit words and the decoder stops
   at the exact length.
6. **The replayer checks its pops against the sizes it was given** (V3-24): reading past a stream's words, or popping past its
   values, is an underflow error at once; reaching done with input left over is an error.
7. **New `ERRORS` bits** (V3-24), after v1.25's [0] reserved, [1] queue overrun, [2] invalid expansion: **[3] code-stream
   underflow, [4] read-log underflow, [5] write-log underflow, [6] done with input left over.**

8. **The state word keeps the per-pass flags, now two** (owner, 2026-11-01), the stream's compression flags having moved to the
   stream word: **use the store log** ([115], as at v1.25: 0 means the pass doesn't use stores' pre-values, the write log is never
   fed, and the write log's sizes are ignored, neither underflow [5] nor leftover [6]) and **output rd_prev** (new: 0 means the
   pass doesn't need rd's value before the row, so the output row's `rd_prev` field, word 3 [63:0] of 4.19.12's row, is 0). Both
   are a consumer's choice per pass, so they stay in word 0, whose write also flushes the queues for the new pass. **Only the flag
   changes** (owner): the record's other words, the stream word's write-log sizes included, stay exactly as the Tracer produced
   them; nothing is zeroed or edited for a pass. Bit positions to propose.
   *Superseded in v1.26.1 (item 15): the two flags move to `RUN_CONFIG`, and [115], [116] become reserved.*

9. **WORKER19's choices within the rulings, confirmed** (owner, 2026-11-01): words 66-67 are written only while the replayer is
   stopped, like the rest of part 0; a stream's word count must be the fewest 128-bit words that hold its values, so a whole
   extra word of padding sets [6].
10. **Setting the state resets everything; `PULSE_RUN` only starts running** (owner, 2026-11-01), the same on the Tracer and
   the replayer: **the write to word 0, the state word, flushes the input queues, latches the sizes, clears the size counters,
   sets the credits and resets the decompressors**; `PULSE_RUN` starts a pass on the replayer, the next `RUN_CYCLES` rows on the
   Tracer, and does nothing else. The replayer writes its state before every pass (item 12), so each pass starts fresh;
   the Tracer writes its state once per segment, so its counters and credits run across the 64 runs. (Replaces `PULSE_RUN`
   setting the credits, resetting the decompressors, latching the sizes and clearing the counters, as first specified.)

11. **Preloading is allowed** (owner, 2026-11-01): after the state write, software may push the first chunks into the queues and
   then pulse `PULSE_RUN`, or pulse first and push after; either works, since the state write has already flushed the queues and
   set the credits, and it is all our software. (Drops "the queues must be empty at `PULSE_RUN`", invariant 16 as first written.)
   **Writes to words 66-67 while a pass runs are flagged**, like any part-0 write while running (item 9), not silently ignored.

12. **Part 0's state is the working state** (owner, 2026-11-01; **no hidden memories**: a hidden copy of state is complexity, so
   what the controller writes is what the unit runs on): no separate set of registers, no copy at a run's start. Writing
   the record's words 0-65 writes the live register file, pc, `instr_idx` and the offset directly; running changes them in place;
   the word-0 write still does item 10's resets. So **the replayer reloads the whole record, words 0-65, before every pass**, from
   the tile's copy (part 0 then holds the window's end state after a pass), and **the Tracer loads once per segment**, its 64 runs
   carrying on from wherever the last stopped. The cost is unchanged: loading 66 words takes about 66 cycles wherever they come
   from, as v1.25's internal copy took 64. (Replaces part 0 as a stored copy, copied into the working state at `PULSE_RUN`, and
   a per-pass resend of word 0 alone.) The checks read the sizes and flags from part 0, which can't be written while running.

**Out of scope** (they belong to the Tracer or to v1.3): the peripheral DMA in 128-bit words (spec 4.17.9), the broadcast of
controller-fed templates, everything about files, slots and caches.

## 2. Definition of done

- **The spec** (`wip/spec/machine-spec.md`) states all seven changes, with `CHANGES-v1.26.md` (the delta from v1.25, each item
  citing this file) and the change guide `CHANGES-to-v1.25.md` extended, or a short guide from v1.25 to v1.26 for the RTL.
- **meshsim's replayer model**, its controller firmware (recipe 26, `replay_ctl.c`), the acceptance kit, `ReplayDriver` and the
  synthetic deliveries follow the spec; tests for each change, including each new error bit and the flush.
- **The real deliveries** (WORKER9's 2²⁹ windows of EVM Zone and Cyclone, 67-word records) are converted or regenerated to 68
  words with value counts, and **every window of both workloads at 2²⁹ matches in stages A, B and F**, as v1.25's acceptance did
  (Quest 083, 084), with the 4 KB queues; the stall counts reported.
- The quick and full suites pass; the full-size sort is unchanged (p4/uniform-k128: 1,229,008 cycles).

**Done: frozen as `v1.26/`, 2026-11-04.** The spec, `CHANGES-v1.26.md` and the RTL guide (WORKER19); meshsim, the firmware, the
kit and the converted deliveries (WORKER20, merged at 5b03600). **Every window of both workloads at 2²⁹ matched with the 4 KB
queues**: Quest 098 (EVM Zone) and 099 (Cyclone), stages A and B on 8,192 of 8,192 windows each, F on 128 of 128; the 8 KB
controls 100 and 101 on 8,192 each. The log waits are only at a pass's start, 0.066 % and 0.063 % of a pass at 4 KB (0.102 % and
0.097 % at 8 KB; item 143). Suites 36 of 36 and 11 of 11, the full-size sort 1,229,008 cycles (102b). The deliveries were
regenerated into the runner's persistent data and match WORKER9's (103, 104).

## Work list

| # | work | worker |
|---|---|---|
| V1 | the spec and the change notes | WORKER19, done |
| V2 | meshsim, the firmware, the kit, the deliveries' conversion, the acceptance runs | WORKER20, done |

## 3. v1.26.1: the point release (owner, 2026-11-04)

Made with the Tracer's design (DECISIONS-v1.3 sections 23-24) so that SP1 (the Tracer's chapter) and Tony start from the final
layouts. Four changes, on both units:

13. **`RUN_CONFIG`, one 32-bit register at word 32 on both units** (the replayer and the Tracer; on the Tracer it replaces
    `RUN_CYCLES`): **[23:0] rows to run** (up to 16,777,215, so a whole 2²² segment fits; a replayer pass is typically 65,536);
    **[24] use the store log**; **[25] output rd_prev**; [31:26] reserved. **Both units honour the row count:** the replayer runs
    exactly that many rows and then raises done, so a pass can be shorter than a window (useful for debugging), instead of a
    fixed 65,536; the Tracer runs exactly that many, as `RUN_CYCLES` did, and **ignores the two flags**. **The rule:** written
    while stopped, used by the next `PULSE_RUN`; a write while running is UB, like a part-0 write. **A row count of 0** runs nothing and
    raises done at once (owner, 2026-11-04: `STATUS` [0] rises and falls and run done is posted, as a DMA with count 0, 4.17.9).
    **`RUN_CONFIG` is sticky:** an ordinary R/W register, untouched by `PULSE_RUN`, the state write and done, 0 only after a reset;
    firmware writes it once before its passes. **[6] stays unconditional** (owner): a short pass is for a short trace, a record
    sized to it.
14. **Words 32-35 are the same on both units:** 32 `RUN_CONFIG` (R/W), 33 `ROWS_RUN` (R), 34 `EXPANSION_OFFSET` (R),
    35 `EXPANSION_OPCODE` (R); so **the replayer's three status words move up one** (from 32-34). **`ROWS_RUN` widens to 24 bits**
    on both units ([23:0]; [31:24] 0), the rows of this run completed, so a long run's progress reads exactly (it counted modulo
    2¹⁶). It is a diagnostic (rows left = the row count − rows run while `STATUS` [0] is 1); no firmware reads it, and it stays.
15. **The state word's [115] and [116] become reserved** (superseding item 8): **the record describes only where the program
    is**, so every replayer pass loads the stored words 0-65 verbatim, with nothing edited per pass (item 12 unchanged). **The
    done-with-input-left check [6] and the write-log exemption follow `RUN_CONFIG` [24]**: with the store log off, the write
    log's sizes are ignored, with neither [5] nor [6].
16. **The stream word is rearranged, contiguous, and carries a fourth stream, advice** (DECISIONS-v1.3 section 23): word counts
    [15:0] code, [31:16] read log, [47:32] write log; values out [64:48] code, [81:65] read log, [98:82] write log; compression
    flags [99] code, [100] read log, [101] write log; **advice values [124:102]** (23 bits, 0 to 2²² inclusive; uncompressed 64-bit
    values, so its word count is ceil(values / 2), and no flag); [127:125] spare. **Replayers ignore the advice field**; their
    advice still comes in the read log. On the Tracer it is the segment's advice at load, counting down live.

17. **The record is the superset of both units' state, plus the window's descriptor** (owner, 2026-11-04): one 68-word format,
    each unit using what it needs and ignoring the rest, so gold, the host, the segment files and the DMA write one layout. **The
    test for state is resuming:** what a unit needs to continue execution elsewhere (as RISC-V's `satp` is saved with a hart's
    context though running never changes it). The row count and the two flags fail it, so they are in `RUN_CONFIG` (item 13); the
    files word passes, being the Tracer's context.

    | words | Tracer | replayer | what it is |
    |---|---|---|---|
    | 0, 2-65 | the program's state | the program's state | state |
    | 1, the advice field | the live input count | ignored | state (the Tracer's) |
    | 1, the three stream fields | outputs, written at done | inputs, the pass's size checks | the window's descriptor |
    | 66, the files word | its context: the files, K, `lowest_address` | ignored | state (the Tracer's) |
    | 67, the template bitmap | an output, written at done | ignored (the tile's controller trims by it) | the window's descriptor |

    **The window's descriptor is in the record for pragmatism, not principle:** word 1's stream fields and word 67 describe one
    window rather than state (reset at each `PULSE_RUN`, produced at done, delivered to tile *n* while the state goes to tile
    *n* + 1, DECISIONS-v1.3 section 24). They travel with the window because the DMA delivers them with it for free; the
    alternative for the bitmap is four read-only status registers, which C&C 3 would read at every done and then deliver to the
    tile itself, a second path. The stream fields have the stronger case, since the replayer's checks read them.

18. **`RUN_CONFIG` [28:26] are the Tracer's per-stream compression enables** (owner, 2026-11-05, from the Tracer's chapter,
    DECISIONS-v1.3 section 24): code, read log, write log; [31:29] reserved. **The replayer ignores them** (never flags them) and
    holds them like the other bits, reading them back, so one register block serves both units; [31:29] read 0 (WORKER25's
    choice, accepted).
19. **The release's rulings** (owner, 2026-11-04/05, on WORKER25's plan): `RUN_CONFIG` is sticky, 0 only after a reset, so
    firmware writes it once before its passes; a row count of 0 posts done at once ([6] if the record has input); [6] stays
    unconditional (short passes are for short traces); reserved [115]/[116] are written 0 and ignored, not checked; the
    deliveries are converted in scratch per task (v1.25 to v1.26.1 directly, refusing [115] = 0); the 8 KB controls are
    skipped (the queue sizes don't change).

**To carry, in the same release:** the spec (4.17, 4.19.2, 4.19.8-4.19.10, 8.11, section 13), `CHANGES-v1.26.1.md` and Tony's
change notes; meshsim's replayer model, `mesh.h`, the register reference, `replay_ctl.c` and the recipes, the tests, the
acceptance kit and `ReplayDriver`; everything that writes a record or a stream word: gold (`wip/gold/`), WORKER16's segment
tools, and the converted deliveries in the runner's data. SP1 writes the Tracer's chapter on these layouts.

**The release** (owner, 2026-11-04): made as **`v1.26.1/`**, the point-release rule (as `v1.2.1/`). Tony has not started on
v1.26 and is waiting for it. **Once `v1.26.1/` is frozen, `v1.26/` is deleted** (owner): git keeps it, so the README's version
table names v1.26 as superseded, with the last commit holding the folder, and the references to `v1.26/` (README, this file,
the handover) point there or to `v1.26.1/`.

**Done: frozen as `v1.26.1/`, 2026-11-05** (WORKER25, merged at f9e20f6). The spec, `CHANGES-v1.26.1.md` and the RTL guide
from v1.25 straight to v1.26.1 (`CHANGES-v1.25-to-v1.26.1.md`); meshsim, `mesh.h`, the register reference, `replay_ctl.c`
(layout 4) and recipe 26, the kit and the converted deliveries. **Every window of both workloads at 2²⁹ identical to v1.26,
cycle for cycle** (Quest 106, 107 against 098, 099: verdicts, rows, cycles, stalls and refills on 16,512 of 16,512 lines per
workload); the smoke run 105; the suites 36 of 36 and 11 of 11 and the full-size sort at 1,229,008 cycles (108).
ReplayerTest 2182/0. **`v1.26/` deleted** after the freeze (owner); it is kept in git history.
