# Binding the split into the sort program

*For worker 6.* The split is design v1's bisection (Sorter v1.0), ported to the controller read port (spec 4.3). It replaces the oracle with no change to the post-XA state.

## The call

```c
#include "split.h"
void split_get_cuts(uint32_t run_chunk, uint32_t n_me, uint32_t shaped,
                    uint32_t c[65], uint32_t n[64]);
```

| argument | meaning |
|---|---|
| `run_chunk` | big-SRAM chunk address of this tile's sorted run (sort 1's output): `n_me` 64-bit (key, index) records, key in the low word, so record i's key is chunk `run_chunk + 2i` |
| `n_me` | the run's length |
| `shaped` | 0 for the sort (R_q = ⌊q·N/64⌋, balanced); 1 only for the two shaped test sets (R_q = n_0 + … + n_{q−1}) |
| `c[65]` | out: the post-XA state. c[q] is the number of this tile's records whose global rank in (key, tile, position) order is below R_q. c[0] = 0, c[64] = `n_me`. |
| `n[64]` | out: every tile's run length |

**Address rule.** `c` and `n` receive mode-7 words from every tile at base + sender, so they must sit at the **same data-memory address on every tile**. That holds automatically for static arrays in one image, as in `driver.c`.

## Where it runs and what it uses

**Entry and exit.** Call it at the start of a command, right after GO, on every tile, with nothing outstanding: all three counters at 0 and the read-port queue empty. It returns after XA's last word has landed; the caller then signals `done` as usual.

**It owns its barriers.** Internally it stores `MASTER_PULSE` = done and executes `WFI` **66 times** (65 with `shaped` = 1). C&C must answer each with GO, exactly as for a run of 66 one-line commands. The sequence, which is the same on every tile:
- the n exchange (no barrier before it; it runs in the calling command);
- a barrier, then the seed exchange (mode 7; not with `shaped`), after one read-port batch of 64 reads;
- 32 rounds, each with:
  - a barrier, then the probe exchange (mode 7), then counting through the read port;
  - a barrier, then the counts exchange (mode 5), then the owner update;
- a barrier, then XA (mode 7).

It contains no data-dependent barrier, and its round count is fixed at 32 (lead, 3 October).
- **Early exit inside the fixed sequence.** Once every owner's done marker has been seen (the same round on every tile), a tile skips the remaining exchanges and searches. It still takes each remaining barrier (≈ 30 cycles), so C&C answers the same 66 barriers either way.

**Registers:** `MASTER_STATUS` (tile index), `MASTER_PULSE`, `CM_REMAINING`, `REPLY_REMAINING`, `SET_HDR`, `SET_HI`, `SEND_LO`, and the read port's `MEM_RD_ADDR_QUEUE` (0xFFFFF82C) and `MEM_RD_DEST_ADDR` (0xFFFFF830).
- `split_regs.h` holds these until mesh.h v4 appears; switch the include then.
- The function leaves the counters at 0 and the read-port queue empty.

**Traffic it relies on:** every exchange is 64 sends in the 16-group schedule order, one per destination, so each is a permutation started at GO.
- Mode 6 goes to a data-memory array at base + sender (n, seeds, probes, XA's cuts).
- Mode 4 goes to its own 128-word buffer, one 64-bit packet per sender.
- Nothing is written into the mailbox.

**Read-port use** follows spec 4.3:
- one initial wait for 64 free entries;
- then per step, `REPLY_REMAINING` = number of reads, `MEM_RD_DEST_ADDR`, the reads, then `WFI`;
- never between a `WFI` and a start store.

## Memory

| item | bytes |
|---|---|
| the split's own data (`.bss`) | 5,588 |
| the caller's `c[65]` and `n[64]` | 516 |
| stack, deepest | 192 (split_get_cuts 144 + step_loop 48) |
| code | 5,004 (gcc -O2, RV32I + Zmmul; no divide) |

The split's data is free again once it returns: nothing in it is needed after XA. If the sort's image is tight, the split can be an overlay.

## Files

- `split.h`, `split.c`, `split_regs.h`: the split.
- `driver.c`: the ISS harness's caller.

`SPLIT_TRACE` adds ISS-only trace marks; leave it undefined in the sort.
