# meshsim v1.1 → spec 2026-10-26n (checked against 26r and part M): the simulator session of 2026-10-27

This session brought meshsim, its generated files (`sw/mesh.h`, REGISTERS.md's generated blocks), its tile test
programs, the recipes (`sw/tests/rcp_*`, `sw/RECIPES.md`, `sw/CONVENTIONS.md`) and the sort (the sort program, the
split, the planner) to spec 2026-10-26n, and put the replayer (replayer-cp6 → cp12) and Stage B (`sw/replay`) on the
tile. 26r changes nothing meshsim models beyond what this covers (`deliverables/SPEC-OPEN-26n.md`). The commit-by-commit
record, with every old → new expectation, is `deliverables/NOTES-sim-26n.md` (commits A-L). Deviations: `DEVIATIONS.md`.

Toolchain: OpenJDK 21, riscv64-unknown-elf-gcc 13.2, picolibc 1.8.6.

## a. What changed in the model

- **Link word** 169 bits: 21-bit addr, 4-bit mode at [38:35], size at [40:39]. Modes 1-9 as 26n numbers them; bad
  packets: modes 0 and 10-15, mode 5 at 128, a misaligned mode 3, mode 7 above 32 bits, mode 8 at 128.
- **Counting** (26n policy): every mode from any source except 1 and 7; a halted tile counts nothing; four counters
  (SRAM, CM, REPLY, PERIPH), PERIPH on landing. Counter writes add; entering HALT clears the counters, their done events
  and the conditions.
- **Register map**, part E: 40 offsets 0x800-0x89C, no holes; the request port's four offsets carry a store and a load
  register each. TILE_STATUS: peripheral type, endpoint id [26:20], size in 256 KB units [31:27].
- **Wake events**: WAKE_EVENTS (sticky and live events), WAKE_MASK, WAKE_REQUIRED, WAKE_PEEK; a consuming load at its
  sample point; the done gate; GO's event visible from the cycle after GO is seen.
- **Register and table-window loads take 2 cycles** (the reference), sampling in their last cycle.
- **Port B**: mode 6 to either memory (1, 2 or 4 words), mode 7 to the command block's slot addr[3:0], mode 8 to the
  reply block by source (32 bits: word 0; 64: both).
- **Read port**: the setup register (destination word, size), alignment checked, results as mode-6 packets counted in
  DMA_CM_REMAINING, in any order.
- **SRAM read queue**: one pool of 64 tokens shared by the three clients and the loopback; in-order return queues for
  the address generator and the GATHER engine, read-port returns unordered; tokens freed at injection, a loopback
  packet's when it leaves the loopback FIFO.
- **Router**: the fifth port in every tile (connected on tile 0), a 64-deep loopback FIFO served only when every other
  eject FIFO is empty.
- **Big SRAM** up to 8 MB (16 areas), any multiple of 256 KB.
- **New units**: the peripheral slot (the replayer, type 1, on every tile; a test peripheral for the slot's tests), the
  sender (a test-only recording sink at its port), the request port and the request bus (8-ary tree, 16-bit beats,
  credits per target; targets strapped to receivers only in tests), the UART (unwired: TX printed per tile).
- **RegDoc** regenerated REGISTERS.md's blocks and `mesh.h` 1.1.

## b. Software

- **Recipes:** ported; the one-word exchange as replies by source; rule 4 is now row-aligned elements in group runs
  (v1.1 allows mixed read sizes in a group); rule 5's staging goes to halted tiles; commands carry 7 arguments.
- **The sort:** `xchg` and BT's half-words as replies, copied out; BT's descriptors (up to 5 words per sender per
  receiver) as mode-6 writes with a per-sender marker, polled; one packet per 128-bit value; `CMD_STAGE` before C&C's
  overlay staging; data memory below the reply block.
- **The split:** every exchange as replies; the counts (lb, ub) as one 64-bit reply per owner; the read port's setup
  register and DMA_CM_REMAINING; `split_regs.h` at mesh.h 1.1's addresses (RegDocTest checks it).
- **The planner:** the three STREAM arrays in the tile build, one packet per value; its host harness keeps v1.0's
  encoding (its expected results stand).
- **Stage B:** `test/StageB.java` loads a delivery window into tile 0's big SRAM as `replay_layout.h` lays it out, boots
  `replay_ctl`, and checks the rows byte for byte against `rows.bin`, the streams' consumption and the end state.

## c. Suites (results/)

Every suite passes: BusTest 154, NetTest 30,602, MemTest 173, EngineTest 356, CtrlTest 7,158, CondTest 396,
ReadPortGatherTest 499 (full 504), AgTest 222, HooksTest 153, PeripheralSlotTest 43, SenderTest 48, RequestBusTest 49,
ReplayerTest 1,183, RiscvTest 96, AccountingTest 11 (full 11), SearchBench 56, RecipeTest 2,325, RegDocTest 300,
SortTest p1 54, sort 124, forced BT 12 and 13; every suite with a shuffled-order run also passes it, with identical
results. AgBench and the quick sweep ran. **On the runner machine** (`RESULTS-full-size.md`): CtrlTest full 7,167,
EngineTest full 383, the skew table and the full sweep (task 067; task 066 failed only for a packaging gap, now fixed);
the 43 full-size P4 runs all pass (task 065). The kit's 92 runs at 64 × 2,000 all pass (`RESULTS-n2000.md`).

## d. Re-measured numbers (old → new)

| what | v1.0 (or 26n at L = 1) | 26n | why |
|---|---:|---:|---|
| GO store → first request, stream run [RecipeTest sync] | 9-10 | 12-13 | GO's event visible next cycle; two 2-cycle loads in `wait_for_command()` |
| GO store → first request, group run [RecipeTest a2a] | 17-18 | 20-21 | as above |
| read port during an all-to-all, packets lost [ReadPortGatherTest] | 561 | 0 | the token-protected loopback, served last |
| read-port tolerance, 32 results against a full-rate stream | overflow | none | as above |
| gather 1 from C, 64 × 65,536, k = 2 [rdport-full] | 196,652 (L = 1) | 196,655 | 2-cycle loads |
| full-size all-to-all from C [accounting-full] | 74,409 (L = 1) | 74,416 | 2-cycle loads |
| 63 searches, state packed [SearchBench] | 28,240 | 28,274 | 2-cycle loads (step overhead 30.4 → 32.4) |
| xchg recipe (64 words each way) [RecipeTest] | 987 | 1,303 | replies by source and the copy-out |
| bucket recipe [RecipeTest] | 10,894 | 11,950 | 2-cycle HIST loads in the prefix sums |
| request bus, unloaded [RequestBusTest] | — | 17 | new (spec estimate 15-20) |
| forced BT, 64 × 16,384, trace hot 10 / 10 %, 32-bit values | 701,028 | 709,658 | exact; 2-cycle loads |
| the same, 128-bit values | 998,040 | 927,893 | exact; a 128-bit value is one packet |
| the sort's built-in sets, 64 × ≤ 2,000 | NOTES, commit J | -8,896 to +4,873 | exact; same paths |
| the 43 full-size P4 runs, 128-bit values [runner task 065] | v1.0 kit | 4.3 % to 13.4 % faster (median 7.5 %) | a 128-bit value is one packet: the V steps halve |
| the 43 full-size P4 runs, 32-bit values | v1.0 kit | 0.4 % to 0.7 % slower (median 0.55 %) | 2-cycle loads; same paths on all 43 |
| Stage B, EVM Zone/29 window 53, cycles/row (Stage A 3.270) | — | 3.814 (cp9) → 3.479 (cp12) | new-expansion service |
| Stage B, Cyclone/25 window 78 (Stage A 3.006) | — | 3.169 (cp9) → 3.090 (cp12) | as above |

## e. Spec items

`deliverables/SPEC-OPEN-26n.md`: the six problems logged early are fixed in 26r; the session's four further items
(6.4's start path, 4.3's overlap rule, 6.2's staging order, `reqbus_latency`) are in part M. Part M's handoff, answered
in the merge: the counter store lands +7..+8 cycles after C&C's GO store on the shortest start path and +14..+15 on the
sort's `CMD_STAGE` path; with `stage_delay` = 32 the sort passes; v1.0's 561 lost packets went with the 64-deep
loopback FIFO (with v1.0's 16-deep FIFO meshsim loses exactly 561 again).

## f. The milestone merge (`deliverables/MERGE-2026-10-27.md`)

The spec session's archive through part M (its spec, REGISTERS.md's prose, CHANGES parts s-v) and replayer-cp14 merged
into this tree; every conflict is listed there. Checks on the merged tree: the quick suite passes, 35 suites and
shuffled runs with 0 failures, including RegDocTest (300), the recipes (2,325), P1 (54) and the sort with its shuffled
run (124 each, every set exact, staging at `stage_delay` = 32); Stage B with cp14's `replay_ctl` matches byte for byte on
EVM Zone/29 window 53 (3.479 cycles/row) and Cyclone/25 window 78 (3.090); 1,191 section references in the spec resolve
to its headings and its 24 figure numbers to `diagrams/README.md`. The kit (`kit/`, `runner/p4/kit`) is the one task
065 ran (its jar waits 64 cycles before staging, which satisfies `stage_delay`); `runner/suites` carries the merged
classes.
