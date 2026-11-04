# WORKER20 notes for WORKER19 (v1.26)

> **Item 143, the stall counts with 4 KB queues (2026-11-04).** Stage B (`replay_ctl.c` on tile 0), every window of both
> workloads at 2^29, the v1.26 model with item 12; cycles per window, average (max). Quest 098/099 (4 KB), 100/101 (the 8 KB
> control: the same model and controller with v1.25's 512-entry logs), v1.25's from WORKER11's 083/084.
>
> | per window | EVM Zone 4 KB | 8 KB | v1.25 | Cyclone 4 KB | 8 KB | v1.25 |
> |---|---|---|---|---|---|---|
> | read-log wait | **39.2** (127) | 39.2 (127) | 59 | **43.5** (127) | 43.5 (127) | 63 |
> | write-log wait | **100.0** (255) | 176.2 (383) | 267 | **84.5** (255) | 153.6 (383) | 222 |
> | log waits, share of a pass | **0.066 %** | 0.102 % | ~0.15 % | **0.063 %** | 0.097 % | ~0.15 % |
> | Stage B cycles per row | 3.2116 | 3.2128 | 3.213 | 3.1105 | 3.1120 | |
>
> **For 4.19.2 / 4.19.8:** with 4 KB queues the logs cost 0.06-0.07 % of a pass on both workloads, less than at 8 KB. The
> waits are all at a pass's start, not from the logs' density: the first refill run sends the code half, then the read log's,
> then the write log's, a packet a cycle, so no read-log wait exceeds 127 cycles and no write-log wait 255 (383 at 8 KB) in
> any window; window by window the 8 KB write-log wait exceeds the 4 KB one by 0-128 cycles (exactly 128 in 4,753 and 4,259
> windows) and the read-log waits are identical. Cyclone's densest stretches (0.83 words per row) never stall beyond the
> start. After the start a 128-entry refill always lands in time. Details: `WORKER20/REPORT.md` section 5.
>
> The acceptance with 4 KB queues: every window of both workloads matched in stages A and B, and every 64th in F
> (098, 099: PASS).

## What meshsim does where your text leaves room (REPLAYER-NOTES.md, "v1.26")

- **The stream word's sizes are latched at the state word's write and again at the stream word's own write.** State-first
  loading writes word 1 after word 0, so a latch at word 0 alone would take the previous window's sizes. Part 0 can't be
  written while running, so this equals reading the sizes from part 0 during the pass (item 12's wording).
- **A pass started without a write to word 0 since the previous pass is flagged** (a violation): its queues weren't reset and
  it would start from the previous pass's end state.
- **A push "in flight at the state write"** can't be seen from inside the replayer: every packet reaches it through one
  in-order landing path with a fixed latency, so a push accepted before the state write always lands before it. The
  controllers keep the rule by waiting for their refill counters before writing part 0.
- **An underflow stops the pass** (done posted), as for an invalid expansion; [6] is evaluated as the 65,536th row finishes.
- **The stream word's layout and [116] are yours** (`Replayer.parseStreamWord`, `stateHi`).
