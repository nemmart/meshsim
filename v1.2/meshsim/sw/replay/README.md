# The Stage B replay controller

**v1.2 (WORKER4, 2026-10-31).** Ported to mesh.h 1.2 and v1.2's replayer: byte addresses (the replayer's region
0x04, `RP_Q_*`, `RP_STATE`, the decode table at offset 0x1000; data memory by address), copies on counter 2 and
replayer writes on counter 4, the 4.19.9 loop on counted credits (the replayer's `EVENTS`, its `EVENTS_MASK`, queues
with nothing left out of the mask, a new expansion's first chunk on an explicitly taken credit), the squelch on
counter 4's done and DMA run done together, results through the exit word. `replay_regs.h`, `crt0.S` and `replay.ld`
are gone: it builds with meshsim's `mesh.h`, `crt0.S` and `app.ld`. `java StageB synthetic` runs it on a tile against
Stage A's synthetic windows (every one matches row for row); `DELIVERY=<replay-inputs dir>` runs the guest windows as
before. **The text below describes the v1.1 program** and is kept for its history; where it names v1.1's registers
(service lines, `DMA_PERIPH_REMAINING`, mode 9) read the v1.2 equivalents above.


`replay_ctl.c` is one tile's controller program for replayer passes, against spec 4.19 at mesh-sorter-v1.1-26p (26n's 4.19 is unchanged in 26p). **Final for this milestone (cp14):** it runs on meshsim's v1.1 tile (interim 2026-10-27, `test/StageB.java`), and its rows match byte for byte. Measured: EVM Zone 2²⁹ 3.479 and 3.492 cycles per row, Cyclone input/25 3.090. Software tuning is closed. The next step is proposal P1, in REPLAYER-NOTES.

| file | what |
|---|---|
| `replay_regs.h` | **local register header**: every constant from the spec's section 8. It stands in for `mesh.h`, whose v1.1 offsets are stale. Delete it when meshsim generates the new `mesh.h`. |
| `replay_layout.h` | the 2 MB big-SRAM layout: the contract with the Java-side loader |
| `replay_ctl.c` | the program |
| `crt0.S`, `replay.ld` | start-up and image layout (stack below the reply block at 0x5BC0) |
| `build.sh` | builds `build/replay_ctl.{code,data}.bin` and `.sym` with meshsim's toolchain flags (`rv32i_zmmul`, no software divide) |

Toolchain: `apt-get install gcc-riscv64-unknown-elf picolibc-riscv64-unknown-elf` (gcc 13.2).

## What the Java-side loader writes before the tile runs

These go into big SRAM, laid out as in `replay_layout.h`:
- **The header:** 16 words, with every region's chunk address and size and the pass count.
- **The tables:** the decode table (256 × 128-bit words) followed by the ROM (512 words), in mode-9 order.
- **The template index:** as delivered by the tracer (`REPLAY-INPUT-REQUIREMENTS.md` 2), 16 B per template number, with **`rd0` at byte 12 and the decode class at byte 13**. The count is the file size / 16, and the loader writes it into the header. The controller reads `rd0` for ruling N3.
- **The state record**, as delivered (1,072 B).
- **The template words and the three streams**, as delivered.

## The program

1. **Copies into data memory** the header, the index, ROM word 1 (the NOP word) and the state record. Each is a mode-6 run to the tile itself, counted in `DMA_CM_REMAINING`, waited for on `WAKE_EVENTS` [1].
2. **Builds a hash** of the index keyed by the exact instruction word (item 114).
3. **Loads the decode table and the ROM** with one mode-9 run: 768 words to 0x00400.
4. **Loads region 0 through the send port** from byte 16 of the record: 66 `SEND_128`s.
5. **Runs passes with the 4.19.9 loop.** One load of `WAKE_EVENTS` claims every line. Then:
   - the new expansion is served first;
   - each claimed queue gets one refill stream of min(half, entries left);
   - one address-generator run sends them all, squelched to `DMA_PERIPH_REMAINING` done ([3]);
   - then the mask is restored.
6. **Ruling N3:**
   - **A mid-sequence start:** the word at the next offset is fetched, its `rd0` is set to the template's, and it is sent through the send port.
   - **A NOP policy with rd = x0:** the single NOP word (ROM word 1) is sent through the send port, carrying the template's `rd0`.

   Either way the send-port word is waited for before an address-generator run feeds the expansion queue (one source at a time, invariant 18).
7. **Results** go to `rp_results[]` in data memory: the status, passes, `TILE_CONDITIONS`, new expansions served, refill runs, and a missing instruction word if any. Then `main` returns (ebreak).

Every counter write adds to the count (8.7 and 8.11; 26n part J). The program arms each counter only when it is at 0.

## The new-expansion fast path (cp12)

Measured before this change, in meshsim's tile: about 157 cycles per new expansion. About 123 of them were the program's own code between claiming the line and starting the template's stream (about 100 instructions); about 33 were the template fetch. The path is now:

1. **Preloaded streams.** At start-up each template gets its own STREAM entry (4 + its number) sending its first min(128, length) words to the expansion port. Serving an expansion is one `DMA_CONFIG` write naming that entry and the start pulse. Entries 0–3 stay for refills.
2. **A direct-mapped table.** 128 slots × 16 B: the word, the prebuilt `DMA_CONFIG`, the first chunk's word count, the template number. The slot is `(w × m) >> 25`, where the multiplier *m* is searched at start-up until the program's words land in distinct slots: 1 try for EVM Zone's 32 words, 8 for Cyclone's 23. A single shift-and-mask had 11 and 7 collisions there. A word missing from the table takes the slow path.
3. **N3 off the common path, by construction.**
   - **Mid-sequence start:** only a pass's first instruction can start mid-expansion, so only the pass's first expansion takes the slow path, which reads `PERIPH_STATUS_0`.
   - **NOP policy:** whether it applies is a property of the exact word (a NOP-policy template, and the word's rd field is 0), so such words are left out of the table at start-up and always take the slow path.
4. **Start first, bookkeeping after.** The fast path arms `DMA_PERIPH_REMAINING`, writes `DMA_CONFIG` and pulses start; only then does it record the active expansion and wait for the arrivals. Refills claimed by the same load follow as a second run.
   - **The active expansion** is the chunk address of its next word and the words left. Each expansion low-water claim sends the next min(128, left) words from it, with no lookup.
   - **No `wait_ag_idle` on this path:** every run is waited for before the next claim, so the address generator is idle here.

**The fast path, wake to start:** 18 instructions, about 22–25 cycles. The table's 2 KB shares space with the state record's buffer, which is used only at start-up. Counters in `rp_results[]`: slow-path expansions, the multiplier, the tries, collisions.

**`-DREPLAY_LAST_WORD`** adds a "same word as last time?" check before the lookup. It is off by default. In the timing windows it hits 100% on Cyclone (one BLAKE3 word, 52 times per window) and 0% on EVM Zone (4 distinct words that never repeat back to back). A hit saves about 8 cycles and a miss costs about 1. That's about 0.006 cycles per row gained on Cyclone and 0.003 lost on EVM Zone: not measurable, so it stays off unless StageB.java shows otherwise.
