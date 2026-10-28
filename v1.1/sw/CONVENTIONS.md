# Tile programs: mesh.h 1.1 and the control conventions

**meshsim v1.1, `mesh.h` version 1.1, spec 2026-10-26n.** The register reference is `sw/REGISTERS.md`; tested code for every common operation is `sw/RECIPES.md`.

Spec references are to `spec/machine-spec.md`.

## 1. Build

- **Build script:** `sw/build.sh` builds `sw/loader.S` and every `sw/tests/*.c`. The toolchain is `riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32`. A software divide fails the build unless `ALLOW_SOFTDIV=1`.
- **Outputs:** each program produces `<name>.code.bin` (from 0x0000), `<name>.data.bin` (from 0x4000) and `<name>.sym`.
- **Memory map** (`sw/app.ld`):

  | range | contents |
  |---|---|
  | 0x0000–0x1EFF | code |
  | 0x1F00–0x1FFF | the resident loader |
  | 0x4000–0x5BBF | `.rodata`, `.data`, `.bss`, and the stack, which grows down from 0x5BC0 (`STACK_TOP`) |
  | 0x5BC0–0x5FBF | the reply block: 256 words, two per source, written only by replies (mode 8) |
  | 0x5FC0–0x5FFF | the command block: 16 slots, written only by command writes (mode 7) |

- **`.bss` is not loaded.** `crt0.S` zeroes it, then calls `main()`. If `main` returns, the core executes `ebreak` with a0 = the return value; that is a simulator-only exit.
- **Registers:** every register is one `lw`/`sw`. A load from a register or the table window takes 2 cycles in meshsim (spec 4.11: 1 or 2, fixed per register; meshsim models the worst case); data-memory loads and every store take 1.

## 2. Addresses

- **Big SRAM chunk addresses are 21 bits** (up to 8 MB); the device's size is `sram_chunks()` (`TILE_STATUS` [31:27], in 256 KB units). Chunks past the end read 0 and writes there are dropped.
- **Packed controller-memory words** (mode-6 `addr`, `STREAM_DEST`'s word for mode 6, the read port's destination): words 0x000–0x7FF are instruction memory and 0x800–0xFFF data memory; convert a byte address with `CM_WORD(byte_addr)`. Mode 6 never writes the reply block (packed 0xEF0–0xFEF) or the command block (0xFF0–0xFFF) (invariant 11).
- **The command block:** `COMMAND[s]`, slot *s*; `COMMAND[0]` is the command word. **The reply block:** `REPLY[2 s]` and `REPLY[2 s + 1]` hold source *s*'s last reply (`REPLY_WORD(s)` is its byte address).
- **Table window** (4.10):
  - `HIST[256]` holds 17 bits and is read/write.
  - `PTR[256]` holds 21 bits; write-only.
  - `STREAM` is three write-only arrays, one word per stream each: `STREAM_SRC[i]` (the source), `STREAM_DEST[i]` (destination word and *d*), `STREAM_COUNT[i]` (count, mode, size, gap). `set_stream(i, src, dest, count)` writes all three; a new round usually rewrites only `STREAM_SRC` and `STREAM_COUNT`. `GROUP[2g]` is group *g*'s low word, `GROUP[2g+1]` its high word.
  - Touch a table only when its engine is not using it: the address generator not running (its lead-in included) and `ts_busy` = 0: `wait_tables()`. The simulator reports a window access that meets the engine.
  - 0x3600–0x3FFF is unmapped, and any access there is a bus error.

## 3. Boot (6.2)

1. **Load.** With every tile halted, C&C broadcasts the loader, then the image's code and data, by mode 6, as 128-bit packets (an image padded to whole packets: up to three zero words land past its end). A halted tile counts nothing.
2. **The marker.** C&C writes the entry address (`_start`) as the first argument of its block (slot 1, or slot 8 for an odd command), then slot 0 (`CMD_JUMP << 24` with the parity bit), all as 32-bit command writes. Further arguments may follow it.
3. **RUN.** All 64 READY fire, since `done` is forced to 1 in HALT. C&C raises RUN, then waits run_latency + 1 cycles before sending anything else; a packet in flight across the line change breaks invariant 7.
4. **The loader** runs on every core from 0x1F00 (23 words). It reads slot 0 and its block's first slot first, then sets `PTR[64]` = top of big SRAM − `STAGE_CHUNKS` = `stage_base()` (C&C's mode-2 staging area: the 64 KB below the top 128 KB, which is sort scratch, spec 6.2; 0xF4000 on the 4 MB device), then jumps to the entry address.

**Staging after boot.** C&C stages images and overlays by mode 2 into `PTR[64]`, and mode 2 is counted (4.12): **anything C&C writes into a running tile is announced first.** C&C sends `CMD_STAGE` with the number of packets as its argument; the tile writes `DMA_SRAM_REMAINING` with it, `wfi()`s until they have landed, then calls `wait_for_command()`. C&C sends the first staged packet no sooner than `stage_delay` = 32 cycles after its GO store (spec 6.2, Proposed); the sort's tiles arm 14-15 cycles after it [meas: SortTest staging]. **An uncoordinated write needs the tile halted:** C&C halts it, writes (nothing is counted in HALT), and boots it again.

HALT → RUN also restarts a core halted by a fault (bus error, illegal instruction, `ecall`, `ebreak`). Entering HALT clears the conditions, the four down-counters and their done events (section 5).

## 4. Commands and phases (6.3)

**C&C's command convention:**
- **Argument blocks alternate.** Even commands put their arguments in slots 1–7, odd commands in slots 8–15: **seven arguments at most.**
- **The command word carries the parity.** Its layout is [31:24] type, [23] parity, [22:0] argument.
- **Slot 0 goes last.** C&C writes the arguments first, then the command word to slot 0, all as 32-bit command writes (mode 7).
- **Parity is counted per tile.** A broadcast command needs every tile at the same parity.
- **One kind per command.** All packets of one command to a tile are of one kind: all broadcast or all unicast.

**The tile's command loop:**
```c
for (;;) {
    uint32_t w = wait_for_command();           /* pulse done, then WFI (two back-to-back instructions); on waking (by GO
                                                  alone: the done gate) a load of WAKE_EVENTS consumes GO, then a load of
                                                  slot 0 returns the command word */
    volatile uint32_t *arg = CMD_ARGS(w);      /* this command's block: arg[0], arg[1], ... (7 at most) */
    ...
}
```

**Why the blocks.**
- C&C sends the next command right after GO. Its arguments go to the other block, so they never overwrite the block the woken tile is reading.
- **Every wake from a command goes through `wait_for_command()`,** which reads slot 0 on waking. A barrier is a command like any other: route it through `wait_for_command()`, even if it ignores the word.
- Only slot 0 is shared. The simulator flags any slot-0 landing while the previous command word there is still unread.
- **Keep GO (`WAKE_GO`) in `WAKE_MASK`** whenever you call `wait_for_command()` (the reset mask has it); meshsim flags a `done` pulse without it.

**C&C waits for GO to settle.** After a GO store, C&C sends nothing until GO has reached every tile: go_latency + 1 cycles. GO clears "command landed"; a command landing before its tile has seen GO would be overwritten unread. After a RUN/HALT store, C&C likewise waits run_latency + 1.

**A one-command phase** (a local pass, a gather): wake, read the command, run the phase, call `wait_for_command()`.

**A phase whose engines must start together** (an all-to-all) uses two commands:
1. **Configure:** write `STREAM`/`GROUP`/`PTR`/`HIST`, `DMA_CONFIG` and `DMA_RECORD_BASE_ADDRESS`; then `wait_for_command()`. Nothing starts yet.
   - **Do not arm the counters here.** `done` with a counter still expecting arrivals breaks invariant 4.
   - The run is static from here on: `DMA_CONFIG` and the tables stay as they are until the run ends.
2. **Start:** wake, then in this order:
   1. **write the counters first** (`DMA_SRAM_REMAINING`, `DMA_CM_REMAINING`, `DMA_REPLY_REMAINING`, `DMA_PERIPH_REMAINING`, whichever the phase receives; a write adds, and at a phase start the count is 0);
   2. **then pulse start** with `start_run()`;
   3. then `wfi()` for counters done;
   4. then `wait_for_command()`.

   The earliest packet arrives a few cycles after any tile's start pulse, so no arrival can beat its counter.

**The lead-in** (spec 4.2, 8.6), with *L* = `ts_engine_latency`:
- **a group run:** the first request 4*L* + 3 cycles after the start store; 7 at the default;
- **a stream run:** 2*L* + 2 cycles after it; 4 at the default.

`rd_running` = 1 (and `rd_done` = 0) from the end of the start store's cycle; a start while running is ignored (meshsim flags it). The rise of `rd_done` posts engine run done (`WAKE_RUN_DONE`).

**The code from `WFI` to the start store must be identical on every tile.** Counter values may differ per tile; a store costs one cycle whatever its value, and a register load is fixed-latency. Every address generator then starts the same number of cycles after GO, plus each tile's 0/+1 skew. In the shortest start path (the counter write, then the start store) the start store lands 8–9 cycles after C&C's GO store, the first request 12–13 cycles after it for a stream run (20–21 for a group run on the all-to-all recipe's longer path) [meas: RecipeTest sync, a2a; 2-cycle register loads].

**A tile that only sends** in a phase still waits for `mem_idle` before `done`, because its own loopback writes count.

## 5. Waiting: the counters and the wake events (4.12)

**All wakes are events.** `WAKE_EVENTS` collects them; `WFI` wakes on `WAKE_EVENTS & WAKE_MASK` (with `WAKE_REQUIRED` for all-of waits) and **consumes nothing**. The helpers do: `wfi()` is `WFI` then a load of `WAKE_EVENTS`, which consumes the pending events that are in `WAKE_MASK` and returns them all. At the reset mask (GO and counters done) a program that uses the helpers behaves as in v1.0. `wake_peek()` looks without consuming.

- **The counters.**
  - `DMA_SRAM_REMAINING` counts modes 2–5 (big-SRAM writes, gather values included); `DMA_CM_REMAINING` mode 6 (read-port results included); `DMA_REPLY_REMAINING` mode 8 (replies); `DMA_PERIPH_REMAINING` mode 9, when it lands. Each is 20 bits, and each packet counts 1 whatever its size.
  - **Every mode is counted from any source** (other tiles, C&C, the memory system), except command writes (mode 7) and gather requests (mode 1). **A halted tile counts nothing.**
  - **A write adds** its value, atomically with arrivals; a write of 0 does nothing; a sum past 2²⁰ − 1 wraps (meshsim flags it). **Nothing overwrites a count:** it returns to 0 by counting down, or on entering HALT. HALT is the recovery for a count that can never finish.
  - An arrival at a counter already at 0 (in RUN) sets the unexpected-arrival condition; it is also invariant 9.
- **Counters done** (`WAKE_COUNTERS_DONE`, [9]) is set on the rising edge of "every counter is done": all four at 0, with `mem_idle` and `cm_idle`, if some counter was non-zero since it was last set or cleared.
  - It is sticky: an edge before `WFI` is not lost, and the `WFI` falls straight through.
  - A non-zero counter write clears it, so every new wait starts clean; so do `done` and HALT → RUN.
  - **A count of 0 never makes an edge.** Don't `wfi()` in a phase that expects nothing.
- **Per-counter done events** ([0]–[3]: `WAKE_SRAM_DONE`, `WAKE_CM_DONE`, `WAKE_REPLY_DONE`, `WAKE_PERIPH_DONE`) wait for one class of arrivals while another is outstanding: put the bit in `WAKE_MASK`. They are not in the reset mask, so they stay pending until a load with them in the mask consumes them.
- **The rule: a phase that arms counters ends with a `WFI` for counters done before it pulses `done`:** the `WFI` proves every arrival is in. `done` with a counter still non-zero is the **done-early** condition (spec 4.13) and invariant 4.
- **The done gate** (spec 4.12). From the `done` pulse until GO only a pending GO wakes the core, whatever `WAKE_MASK` holds; counters done cannot be set in that window, and the `done` pulse clears it. So `wait_for_command()` is woken by GO and nothing else. meshsim flags any other wake with `done` set (`Config.doneGate` off, test only, shows it).
- **Other events:** command landed ([10]), engine run done ([11]), request FIFO no longer full ([12]), the peripheral's service lines ([18:13]), a received request ([19], live), sender idle ([20]), the UART ([21], [22], live). Sticky events stay until consumed; live ones follow their condition.

## 6. Sending (4.11)

- **The send sequence:** `ROUTER_SET_HDR = ROUTER_HDR(dst, mode, addr)` once per run of consecutive words. Then send each packet with its words in ascending order, the last store sending: `send32(w0)`; `send64(w1, w0)` (`ROUTER_SET_W0` = w0, then `ROUTER_SEND_64` = w1); `send128(w3, w2, w1, w0)` (`ROUTER_SET_W0`–`W2`, then `ROUTER_SEND_128` = w3).
- **Auto-increment:** after each send, addr advances by 1, 2 or 4 (the size) for modes 3, 6 and 9, and by 1 for mode 7. It stays put for the other modes.
- **`ROUTER_SET_W0`–`W2` are cleared to 0 by every send.**
- **Rate:** one packet per cycle when no read return is waiting. A send store stalls while the holding register is occupied; `TILE_ST_SEND_BUSY` lets a program avoid that stall. A packet to the tile itself or a broadcast also waits for a loopback token.
- **Writes to big SRAM** are mode 2 to the tile itself (landing at `PTR[me]`) or mode 3 at an explicit address aligned to the size.
- **One word to another tile** is a reply (mode 8): it lands by source at `REPLY[2 me]` there, so a sender keeps at most one reply outstanding to each receiver until it has been read. **Several words** to another tile's data memory are mode 6.
- **Replies to C&C** use dst 64; C&C's port ignores mode and addr (except mode 3, a write stream for the memory system).

## 6a. Packet modes (3.1)

| mode | `mesh.h` | packet | lands | counted in | from a stream |
|---|---|---|---|---|---|
| 0 | — | invalid | dropped, bad-mode-or-size condition | — | no (an error) |
| 1 | `MODE_GATHER` | gather request, to the tile itself | GATHER engine | — (never) | yes, *d* = 0 |
| 2 | `MODE_SRC` | big-SRAM write at `PTR[src]` | big SRAM | `DMA_SRAM_REMAINING` | yes |
| 3 | `MODE_EXPLICIT` | big-SRAM write at addr, aligned to its size | big SRAM | `DMA_SRAM_REMAINING` | yes |
| 4 | `MODE_BUCKET` | bucket write at `PTR[bucket digit]` | big SRAM | `DMA_SRAM_REMAINING` | yes |
| 5 | `MODE_AUGMENT` | augmented write: the low 32 bits and the ordinal as a 64-bit record at `PTR[src]` (not 128 bits) | big SRAM | `DMA_SRAM_REMAINING` | yes |
| 6 | `MODE_CM` | 1, 2 or 4 words into controller memory at addr | instruction or data memory | `DMA_CM_REMAINING` | yes |
| 7 | `MODE_COMMAND` | command write, 32 bits, to slot addr[3:0] | the command block | — (never) | no (an error) |
| 8 | `MODE_REPLY` | reply, 32 or 64 bits, by source | the reply block at word 2·src | `DMA_REPLY_REMAINING` | no (an error) |
| 9 | `MODE_PERIPH` | peripheral write | the peripheral (the replayer on every mesh tile) | `DMA_PERIPH_REMAINING`, on landing | yes |
| 10–15 | — | reserved | dropped, bad-mode-or-size condition | — | — |

- **Augmenting is the sender's choice, per stream.** The receiver sets `DMA_RECORD_BASE_ADDRESS`; a mode-5 payload landing at chunk *a* becomes the record (key, (*a* − base) / 2). Modes 2 and 5 may mix in one phase, both advancing `PTR[src]`; mode 4 never shares a phase with them.
- **The sort:** step 1's input pass and the keys all-to-all's key streams send `MODE_AUGMENT` with 128-bit values and `MODE_SRC` with 32-bit values (`KEY_MODE(k128)` in `sort.h`); on the two-hop paths hop 1 is mode 2 and hop 2's key streams, built on the intermediate tile, are mode 5 (the planner's `hop2_mode()`).
- **Bad packets** (dropped uncounted, with the bad-mode-or-size condition): modes 0 and 10–15, mode 5 at 128 bits, a misaligned mode 3, mode 7 above 32 bits, mode 8 at 128 bits. Size 11 is undefined (meshsim flags it). A program never sends one (invariant 5).

## 7. Status and conditions (8.2, 8.3, 4.13)

**`TILE_STATUS`** holds what a program steers by:

| bits | field |
|---|---|
| [11:0] | live bits: `rd_running`, `rd_done`, GATHER busy, `periph_running`, `sender_busy`, `send_busy`, `ts_busy`, `mem_idle`, `cm_idle`, `done`, command landed, `request_busy` |
| [15:12] | peripheral type: 0 none, 1 the replayer (every mesh tile) |
| [19:16] | spare (reads 0) |
| [26:20] | endpoint id: the tile index (`tile_index()`) |
| [31:27] | big-SRAM size in 256 KB units − 1 (`sram_chunks()` = (size + 1) << `SIZE_UNIT_SHIFT`) |

**`TILE_CONDITIONS`** (read-only; `conditions()`) holds the errors, bit *n* = condition *n* (`TILE_COND_*`, [14:0]): router overflow, bad mode or size, unexpected arrival, drift, write-engine input overflow, table request overflow, GATHER queue overflow, read-port error, done early, bus error, illegal instruction, bad peripheral write, the peripheral's two, receive underflow.
- **A correct program on correct hardware reads 0 here for ever.** The test is `conditions() == 0`; no mask.
- **Read-only, cleared only on entering HALT** (the RUN → HALT edge, and chip reset). Nothing in RUN clears them.
- **A phase sees what it caused** by reading `conditions()` at its start and comparing at its end (RECIPES 12).
- **Any non-zero condition raises the tile's error line**, ORed at C&C into one sticky error bit that wakes C&C; C&C's REPORT (spec 6.6, RECIPES 20) then collects every tile's conditions. Tile programs answer `CMD_REPORT` with `report()` (`sw/tests/rcp.h`), first in the command.
- **Drift** is set only when, in a group run, the SRAM read queue refuses an address-generator request (an element spanning two rows does this: keep group runs row-aligned). Zero-length groups and group starts are timetable.
- **Program errors and hardware-fault detectors.** Table request overflow and GATHER queue overflow can't be caused by a program in meshsim: they mean a hardware fault.
- **Timing:** a register load returns the state at its sample point (meshsim: the cycle before the load completes). A store at *t* acts from *t* + 1.
- **There is no "controller halted" bit.** A crashed tile shows up at C&C as a missing READY, and the simulator logs the cause.
- **Write-only registers read 0**, except the request port's four offsets, where a load reads the receive side (`REQUEST_GET_W0`–`W2`, `REQUEST_RECEIVE`).
- **Ignored and flagged:** `TILE_PULSE` [3] (GO; wired only on C&C), `PERIPH_RUN` while the peripheral runs, a `SENDER_SEND` on a tile whose peripheral has no output (every mesh tile).
- **Bus errors** (the core halts): a store to a read-only register; a load from `PTR`, a `STREAM` array or `GROUP`; a sub-word access to a register or the table window; a misaligned access; an access to instruction memory; register offsets 0x8A0–0xFFC and any other unmapped address.
- **Illegal instructions** (the core halts): divide opcodes and CSR instructions. **`ecall` and `ebreak`** halt the core.

## 8. Block copy into controller memory (6.5)

```c
set_stream(s, STREAM_SRC_ADDR(src_chunk), STREAM_DEST_ENTRY(CM_WORD(dst_byte), 0), STREAM_COUNT_ENTRY(n, MODE_CM, SIZE_128, 0));
DMA_CM_REMAINING = n;                              /* n 128-bit packets, 4n words; the count is 0 here, the write arms it */
DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, s, 1);
TILE_PULSE = TILE_PULSE_START(1);
wfi();                                         /* counters done: the last word is in memory */
```
A row-aligned source at read size 128 writes four words a cycle. The code doing the copy must sit outside the region being overwritten. 1,024 words into instruction memory take about 270 cycles from the start store to the first instruction after `WFI` [meas: CtrlTest block copy]. `sw/tests/a2a.c` fetches its parameters this way (at 64 bits).

## 9. Reading big SRAM: the controller read port (4.3)

**The registers.**
- **`SRAM_RD_DEST_ADDR`** (write-only) is the setup register: the packed controller-memory word of the next result and the read size, `SRAM_RD_DEST(CM_WORD(&results[0]), SRAM_RD_SIZE_32)` (or `_64`, `_128`).
- **A store to `SRAM_RD_ADDR_QUEUE`** queues a read of 1, 2 or 4 words (the size) at a 21-bit chunk address aligned to the size, and advances the destination by the size in words. It never stalls. A misaligned read, or a store with no free entry, is dropped with the read-port-error condition.
- **Reading `SRAM_RD_ADDR_QUEUE`** returns the free entries. The queue holds `READ_PORT_DEPTH` = 64, guaranteed.

**Results** return as mode-6 packets to the tile itself, through its injection point and loopback (spec 4.3), each landing at its own destination word, **in any order**, counting `DMA_CM_REMAINING`. Wait for a batch with the counter and `wfi()`.

**An entry is freed when its return is injected,** not when it lands. So 64 free entries mean every return has left, not that every result is in data memory. Only `DMA_CM_REMAINING` reaching 0 says they have landed. The search loop's initial poll never stalls because of section 5's rule: any earlier read-port reads were in a phase that armed `DMA_CM_REMAINING` for them and ended with a `WFI` before `done`.

**The search loop** (the spec's, measured in `sw/tests/search.c`):
```c
while (SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;      /* once: never stalls (earlier phases ended with a WFI) */
while (n_active) {
    DMA_CM_REMAINING = n_active;                        /* at most 63; the count is 0 here */
    SRAM_RD_DEST_ADDR = SRAM_RD_DEST(CM_WORD((uint32_t)&results[0]), SRAM_RD_SIZE_32);
    for each active search: SRAM_RD_ADDR_QUEUE = KEYS + mid;
    wfi();
    for each active search: compare results[i], update, drop it if converged;
}
```

**Rules.**
- **Invariant 8 is a statistic.** A result or reply written twice before the program reads it is counted per phase (`PhaseLog`), not flagged. A test can make it strict (`Config.strictReplies`).
- **Priority:** the read port has the lowest priority in the SRAM read queue. During an address-generator run it waits; queued reads never delay the run [meas: ReadPortGatherTest].
- **Read-port results and incoming traffic** (spec 4.3). **A tile does not issue read-port reads while packets are arriving for it, and does not start a phase that receives packets while read-port results are outstanding:** read-port reads run in steps of their own; before a search step every expected arrival has landed; before the next exchange every result has landed. C&C's next command word (mode 7), sent right after each GO, may land during a search; that is allowed. meshsim checks the rule (invariant 14, `Config.checkReadPortOverlap`). With the token-protected loopback served last (4.5, 4.7) the v1.0 overflow hazard is gone in meshsim [meas: ReadPortGatherTest: no loss under an all-to-all or a full-rate stream]; the rule stands until the spec revisits it.
- **Not between `WFI` and a start store.** Read-port traffic belongs before `done` or after the start.
