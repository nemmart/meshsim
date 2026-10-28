# Tile programs: mesh.h 1.0 and the control conventions

**meshsim v1.0, `mesh.h` version 1.0.** The register reference is `sw/REGISTERS.md`; tested code for every common operation is `sw/RECIPES.md`.

Spec references are to `spec/machine-spec.md`.

## 1. Build

- **Build script:** `sw/build.sh` builds `sw/loader.S` and every `sw/tests/*.c`. The toolchain is `riscv64-unknown-elf-gcc -march=rv32i_zmmul -mabi=ilp32`. A software divide fails the build unless `ALLOW_SOFTDIV=1`.
- **Outputs:** each program produces `<name>.code.bin` (from 0x0000), `<name>.data.bin` (from 0x4000) and `<name>.sym`.
- **Memory map** (`sw/app.ld`):

  | range | contents |
  |---|---|
  | 0x0000–0x1EFF | code |
  | 0x1F00–0x1FFF | the resident loader |
  | 0x4000–0x5BFF | `.rodata`, `.data`, `.bss`, and the stack, which grows down from 0x5C00 |
  | 0x5C00–0x5FFF | the mailbox: 256 slots, written by mode 6 only |

- **`.bss` is not loaded.** `crt0.S` zeroes it, then calls `main()`. If `main` returns, the core executes `ebreak` with a0 = the return value; that is a simulator-only exit.
- **Registers:** every register is one `lw`/`sw`. GCC may hoist the address into a register; that costs the same.

## 2. Addresses

- **Packed controller-memory words.** Mode-5 and mode-7 `addr` and `STREAM_LO.CM` use the packed map. Words 0x000–0x7FF are instruction memory and words 0x800–0xFFF are data memory. Convert a byte address with `CM_WORD(byte_addr)`.
- **Mailbox:** stream *s* is `MBOX[s]`, which is packed word `MBOX_WORD + s`.
- **Table window** (4.10):
  - `HIST[256]` holds 17 bits and is read/write.
  - `PTR[256]` holds 20 bits.
  - `STREAM[2i]` is entry *i*'s low word and `STREAM[2i+1]` its high word; `GROUP` is laid out the same way.
  - Touch a table only when its engine is not using it: after `rd_done` and with `ts_busy` = 0. The simulator reports a window access that meets the engine (the address generator running, its lead-in included, for `STREAM`/`GROUP`; `PTR` or `HIST` busy).
  - 0x3200–0x3FFF is unmapped, and any access there is a bus error.

## 3. Boot (6.2)

1. **Load.** With every tile halted, C&C broadcasts the loader, then the image's code and data, all by mode 5.
2. **The marker.** C&C writes the entry address (`_start`) as the first argument of its block (slot 1, or slot 32 for an odd command), then slot 0 (`CMD_JUMP << 24` with the parity bit). Further arguments may follow it.
3. **RUN.** All 64 READY fire, since `done` is forced to 1 in HALT. C&C raises RUN, then waits run_latency + 1 cycles before sending anything else; a packet in flight across the line change breaks invariant 7.
4. **The loader** runs on every core from 0x1F00 (23 words). It reads slot 0 and its block's first slot first, then sets `PTR[64]` = top of big SRAM − `STAGE_CHUNKS` = `stage_base()` (C&C's mode-2 staging area: the 64 KB below the top 128 KB, which is sort scratch, spec 6.2; 0xF4000 on the 4 MB device), then jumps to the entry address.

HALT → RUN also restarts a core halted by a fault (bus error, illegal instruction, `ecall`, `ebreak`). Entering HALT clears the conditions (`TILE_CONDITIONS`, section 7).

## 4. Commands and phases (6.3)

**C&C's command convention:**
- **Argument blocks alternate.** Even commands put their arguments in streams 1–31, odd commands in streams 32–63.
- **The command word carries the parity.** Its layout is [31:24] type, [23] parity, [22:0] argument.
- **Stream 0 goes last.** C&C writes the arguments first, then the command word to slot 0.
- **Parity is counted per tile.** A broadcast command needs every tile at the same parity.
- **One kind per command.** All packets of one command to a tile are of one kind: all broadcast or all unicast.

**The tile's command loop:**
```c
for (;;) {
    uint32_t w = wait_for_command();           /* pulse done, then WFI (two back-to-back instructions); on waking it
                                                  reads slot 0 (one load) and returns the command word */
    volatile uint32_t *arg = CMD_ARGS(w);      /* this command's block: arg[0], arg[1], ... */
    ...
}
```

**Why the blocks.**
- C&C sends the next command right after GO. Its arguments go to the other block, so they never overwrite the block the woken tile is reading.
- **Every wake from a command goes through `wait_for_command()`,** which reads slot 0 on waking. A barrier is a command like any other: route it through `wait_for_command()`, even if it ignores the word.
- Only slot 0 is shared. The next command word reaches tile 0 **4 cycles** after tile 0 sees GO [meas: CtrlTest commands], and the woken tile reads slot 0 first.
- The simulator flags any slot-0 landing while the previous command word there is still unread.

**C&C waits for GO to settle.** After a GO store, C&C sends nothing until GO has reached every tile: go_latency + 1 cycles.
- GO clears "command landed". A command landing before its tile has seen GO would therefore be overwritten unread, and its READY lost.
- Without the wait, the commands test hangs at go_latency ≥ 6.
- After a RUN/HALT store, C&C likewise waits run_latency + 1.

**A one-command phase** (a local pass, a gather): wake, read the command, run the phase, call `wait_for_command()`.

**A phase whose engines must start together** (an all-to-all) uses two commands:
1. **Configure:** write `STREAM`/`GROUP`/`PTR`/`HIST`, `DMA_CONFIG` and `DMA_RECORD_BASE_ADDRESS`; then `wait_for_command()`. Nothing starts yet: the start (step 2) loads the run.
   - **Do not set the counters here.** `done` with a counter still expecting arrivals breaks invariant 4.
   - The run is static from here on: DMA_CONFIG and the tables stay as they are until the run ends.
2. **Start:** wake, then in this order:
   1. **store the counters first** (`DMA_SRAM_REMAINING`, `DMA_CM_REMAINING`, `DMA_REPLY_REMAINING`, whichever the phase receives);
   2. **then pulse start** with `start_run()`;
   3. then `wfi()` for the local wake;
   4. then `wait_for_command()`.

   The earliest packet arrives a few cycles after any tile's start pulse, so no arrival can beat its counter.

**The lead-in** (spec 4.2, 8.6). The start loads the run in `DMA_CONFIG` and begins the table read-ahead; the first request is offered as soon as its table entries are in, with *L* = `ts_engine_latency`:
- **a group run:** 4*L* + 3 cycles after the start store (4*L* + 2 after the cycle the start takes effect): the `GROUP` read, then the `STREAM` read; 7 at the default;
- **a stream run:** 2*L* + 2 cycles after the start store (2*L* + 1 after the cycle it takes effect): one `STREAM` read; 4 at the default.

A store at *t* takes effect at *t* + 1. S₀, the start of a group run's timetable, is the first-request cycle. The lead-in depends only on the run kind, the tables and *L*, so it is the same on every tile. `rd_running` = 1 (and `rd_done` = 0) from the end of the start store's cycle, so a load right after the store sees it, through the lead-in; a start while running is ignored (meshsim flags it).

**The code from `WFI` to the start store must be identical on every tile.** Counter values may differ per tile, because a store costs one cycle whatever its value. With that, every address generator starts the same number of cycles after GO, plus each tile's 0/+1 skew. In the shortest start path (the counter store, then the start store) the start store lands 5–6 cycles after C&C's GO store, and the first request 9–10 cycles after it for a stream run (12–13 for a group run) [meas: RecipeTest sync].

**A tile that only sends** in a phase still waits for `mem_idle` before `done`, because its own loopback writes count.

## 5. Waiting for arrivals: the down-counters and the local wake (4.12)

`WFI` has two wake sources: GO and the local wake (this section). **A `WFI` consumes the local-wake latch**; GO consumes it too.

- **The counters.**
  - `DMA_SRAM_REMAINING` counts modes 2, 3 and 4; `DMA_CM_REMAINING` counts mode 5; `DMA_REPLY_REMAINING` counts mode 7. Each is 20 bits.
  - Each packet counts 1, including a 64-bit mode-5 packet.
  - **Packets from C&C (src 64) are never counted**: boot loads and staging.
  - An arrival at a counter already at 0 sets the unexpected-arrival condition; it is also invariant 9.
- **The local wake** fires when every counter is done: all three at 0, with `mem_idle` and `cm_idle`.
  - It is a latched rising edge, so an edge before `WFI` is not lost: `WFI` falls straight through.
  - **The latch persists until a `WFI` consumes it or a store to a counter clears it** (spec 4.12):
    a counter store starts a new wait.
  - **A count of 0 never makes an edge.** Don't `WFI` for a local wake in a phase that expects nothing.
  - **The edge counts only if some counter was non-zero since the latch last set.** Without that, a mailbox command or C&C staging passing through port B or the write path makes a spurious wake.
- **Polling one counter and waiting for the rest.** Within a phase, a program may poll one
  counter (to 0, then `cm_idle` or `mem_idle` for its writes to have landed) and `WFI` for the others, as suits it.
  This is how a tile waits for one class of arrivals while another is still expected (there is no per-counter wake mask).
- **The rule: a phase that sets counters ends with a `WFI` before it pulses `done`:** the `WFI` proves every arrival
  is in. `done` with a counter still non-zero is the **done-early** condition (spec 4.13) and invariant 4. Don't
  end a phase on a poll of one counter while others are outstanding.
- **The done gate** (spec 4.12). From the `done` pulse until GO only GO wakes the core: the
  `done` pulse clears the local-wake latch, and an edge while `done` is set is ignored (it consumes the
  local wake's qualifier). So `wait_for_command()` is woken by GO and nothing else, whatever a program gets wrong; an
  arrival after `done` either completes an outstanding counter (the phase was done early) or finds its counter at 0
  (unexpected arrival). meshsim flags any wake but GO with `done` set
  (`Config.doneGate` off, test only, shows it).
  - Example (the sort's BT DESC): it stores `DMA_REPLY_REMAINING` (descriptors) and `DMA_CM_REMAINING` (the children's
    reduction packets), polls `DMA_REPLY_REMAINING`, works, then `WFI`s in the reduction for `DMA_CM_REMAINING`: that
    `WFI` consumes the edge, whichever counter reached 0 last. A tile with no children (`DMA_CM_REMAINING` = 0) `WFI`s for
    the descriptors instead of polling, so every edge it makes is consumed by a `WFI`.

## 6. Sending (4.11)

- **The send sequence:** `ROUTER_SET_HDR = ROUTER_HDR(dst, mode, addr)` once per run of consecutive words. Then send each packet:
  - `send64(hi, lo)` (`ROUTER_SET_HI` then `ROUTER_SEND_LO`) for a 64-bit packet;
  - `send32(lo)` for a 32-bit one.
- **Auto-increment:** after each send, addr advances by 2 for mode 5 and by 1 for modes 6 and 7. It stays put for the other modes.
- **`ROUTER_SET_HI` is cleared to 0 by every send**, so a `send32` after a `send64` carries a high word of 0.
- **Rate:** one packet per cycle when no read return is waiting.
  - A `ROUTER_SEND_LO` store stalls while the holding register is occupied.
  - `TILE_ST_SEND_BUSY` lets a program avoid that stall.
- **Writes to big SRAM** are mode 2 to the tile itself, landing at `PTR[me]`.
- **Replies to C&C** use dst 64; C&C's port ignores mode and addr.

## 6a. Packet modes (3.1)

| mode | `mesh.h` | packet | lands | counted in | from a stream |
|---|---|---|---|---|---|
| 0 | — | invalid | dropped, bad-mode condition | — | no (code 0 is an error) |
| 1 | `MODE_GATHER` | gather request, to the tile itself | GATHER engine | — | yes, d = 0 |
| 2 | `MODE_SRC` | write at `PTR[src]` | big SRAM | `DMA_SRAM_REMAINING` | yes |
| 3 | `MODE_BUCKET` | bucket write at `PTR[bucket digit]` | big SRAM | `DMA_SRAM_REMAINING` | yes |
| 4 | `MODE_AUG` | augmented write: as mode 2, bits 63–32 replaced by the ordinal | big SRAM | `DMA_SRAM_REMAINING` | yes |
| 5 | `MODE_CMEM` | two words into controller memory | instruction or data memory | `DMA_CM_REMAINING` | yes |
| 6 | `MODE_MBOX` | one word into a mailbox slot | data memory | — | no (code 6 is an error) |
| 7 | `MODE_REPLY` | one word into data memory | data memory | `DMA_REPLY_REMAINING` | no (code 7 is an error) |
| 8–15 | — | reserved | dropped, bad-mode condition | — | — |

- **Augmenting is the sender's choice, per stream.** The
  receiver sets `DMA_RECORD_BASE_ADDRESS`; a mode-4 payload landing at chunk *a* gets the ordinal (*a* − base) / 2 in its
  high word. Modes 2 and 4 may mix in one phase, both advancing `PTR[src]`; mode 3 never shares a phase with them.
- **The sort:** step 1's input pass and the keys all-to-all's key streams send `MODE_AUG` with 128-bit values and
  `MODE_SRC` with 32-bit values (`KEY_MODE(k128)` in `sort.h`); on the two-hop paths hop 1 is mode 2 and hop 2's key
  streams, built on the intermediate tile, are mode 4 (the planner's `hop2_mode()`).
- **`STREAM`'s mode field stays 3 bits**, zero-extended to the packet's 4: codes 1–5. Codes 0, 6 and 7 are an error even
  at count 0, like read size 00 (meshsim reports them). `ROUTER_SET_HDR`'s mode field is 4 bits; a program must never send 0
  or 8–15 (invariant 5).

## 7. Status and conditions (8.2, 8.3, 4.13)

**`TILE_STATUS`** holds what a program steers by:

| bits | field |
|---|---|
| [9:0] | live bits: `rd_running`, `rd_done`, [2] spare, `send_busy`, `ts_busy`, `mem_idle`, `cm_idle`, `done`, command landed, GATHER busy |
| [20:10] | spare (reads 0) |
| [26:21] | tile index |
| [28:27] | spare (reads 0) |
| [31:29] | big-SRAM size in 512 KB areas − 1 (`sram_chunks()` = (size + 1) << `AREA_CHUNKS_SHIFT`) |

**`TILE_CONDITIONS`** (0xFFFFF804, read-only; `conditions()`) holds the errors, bit *n* = condition *n* (`TILE_COND_*`):
drift, router overflow, write-engine input overflow, read-port overflow, unexpected arrival, table request overflow,
bus error, illegal instruction, GATHER queue overflow, bad mode, done early ([10:0]; [31:11] read 0).
- **A correct program on correct hardware reads 0 here for ever.** The test is `conditions() == 0`; no mask.
- **Read-only, cleared only on entering HALT** (the RUN → HALT edge, and chip reset). Nothing in RUN clears them; a
  store of 1 to `DMA_PULSE` [3:2] (reserved) is ignored, and meshsim flags it.
- **A phase sees what it caused** by reading `conditions()` at its start and comparing at its end (RECIPES 12).
- **Any non-zero condition raises the tile's error line**, ORed at C&C into one sticky error bit that wakes C&C; C&C's
  REPORT (spec 6.6, RECIPES 20) then collects every tile's conditions. Tile programs answer `CMD_REPORT` with
  `report()` (`sw/tests/rcp.h`), first in the command.
- **Drift** is set only when, in a group run, the SRAM read queue refuses an address-generator request. Zero-length
  groups (1 cycle each, plus the one-time read-ahead cost on long runs of them) and group starts are timetable.
- **Program errors and hardware-fault detectors.** Table request overflow and GATHER queue overflow can't be caused by
  a program in meshsim: they mean a hardware fault.
- **Timing:** status and conditions computed at *t* are readable at *t* + 1. A store at *t* acts from *t* + 1.
- **There is no "controller halted" bit.** A crashed tile shows up at C&C as a missing READY, and the simulator logs the cause.
- **Write-only registers read 0:** `DMA_CONFIG`, `DMA_PULSE`, `DMA_RECORD_BASE_ADDRESS`, `ROUTER_SET_HDR`, `ROUTER_SET_HI`, `ROUTER_SEND_LO` and `SRAM_RD_DEST_ADDR`.
- **Bus errors** (the core halts):
  - a store to `TILE_STATUS` or `TILE_CONDITIONS`;
  - a load from `PTR`, `STREAM` or `GROUP` (write-only; `HIST` is the window's only readable table);
  - a sub-word load or store to a register or to the table window;
  - a misaligned access;
  - an access to instruction memory;
  - an unmapped address.
- **Illegal instructions** (the core halts): divide opcodes and CSR instructions.
- **`ecall` and `ebreak`** halt the core.

## 8. Block copy into controller memory (6.5)

```c
set_stream(s, STREAM_LO(src_chunk, CM_WORD(dst_byte)), STREAM_HI(n, 0, MODE_CMEM, SIZE_64, 0));
DMA_CM_REMAINING = n;                              /* also clears the local-wake latch (5) */
DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, s, 1);
DMA_PULSE = PULSE_ARM(1) | DMA_PULSE_START(1);
wfi();                                         /* wakes when the last word is in memory */
```
The code doing the copy must sit outside the region being overwritten. 1,024 words into instruction memory take 527 cycles from the start store to the first instruction after `WFI` [meas: CtrlTest block copy]. `sw/tests/a2a.c` fetches its parameters this way.

## 9. Reading big SRAM: the controller read port (4.3)

**The registers.**
- **`SRAM_RD_DEST_ADDR`** (write-only) is the packed data-memory word for the next result: `CM_WORD(&results[0])`.
- **A store to `SRAM_RD_ADDR_QUEUE`** queues a 32-bit read of a 20-bit chunk address and advances `SRAM_RD_DEST_ADDR` by 1. It never stalls.
- **Reading `SRAM_RD_ADDR_QUEUE`** returns the free entries. The queue holds `READ_PORT_DEPTH` = 64, guaranteed.

**Results** return as mode-7 packets to the tile itself, through its injection point and loopback (spec 4.3), and land in issue order at the destination words, counting `DMA_REPLY_REMAINING`. Wait for them with the counter and `WFI`.

**An entry is freed when its return is injected,** not when it lands. So 64 free entries mean every return has left, not that every result is in data memory: the last few can still be in the loopback for several cycles. Only `DMA_REPLY_REMAINING` reaching 0 says they have landed. The search loop's initial poll below never stalls because of section 5's rule, not because of the free count: any earlier read-port reads were in a phase that set `DMA_REPLY_REMAINING` for them, and a phase that sets counters ends with a `WFI` before `done`, so their results have all landed (spec 4.3, lead, 4 October).

**The search loop** (the spec's, measured in `sw/tests/search.c`):
```c
while (SRAM_RD_ADDR_QUEUE < READ_PORT_DEPTH) ;      /* once: never stalls (earlier phases ended with a WFI) */
while (n_active) {
    DMA_REPLY_REMAINING = n_active;                     /* at most 63 */
    SRAM_RD_DEST_ADDR = CM_WORD((uint32_t)&results[0]);
    for each active search: SRAM_RD_ADDR_QUEUE = KEYS + mid;
    wfi();
    for each active search: compare results[i], update, drop it if converged;
}
```

**Rules.**
- **Invariant 8 is a statistic.** A result or mode-7 word written twice before the program reads it is counted per phase (`PhaseLog`), not flagged. A test can make it strict (`Config.strictReplies`). Exchanges that land words the program deliberately ignores are legitimate.
- **Priority:** the read port has the lowest priority in the SRAM read queue. During an address-generator run it waits; queued reads never delay the run [meas: ReadPortGatherTest].
- **Read-port results and incoming traffic** (spec 4.3). Results share the tile's loopback and eject with every packet arriving from the links, and the network has no flow control. **A tile does not issue read-port reads while packets are arriving for it, and does not start a phase that receives packets while read-port results are outstanding.** In practice:
  - read-port reads run in steps of their own;
  - before a search step, every expected arrival has landed (its counter is done);
  - before the next exchange, every read-port result has landed (`DMA_REPLY_REMAINING` is done, after a `WFI`).

  The split works this way. C&C's next command word (mode 6), sent right after each GO, may land during a search; that is harmless and allowed. The hardware's tolerance is small: against an incoming stream at one packet per cycle, 15 results over the whole stream (each result that takes an eject stream leaves one incoming packet waiting until the stream ends; the 16th overflows); against a half-rate stream, 32 in one burst [meas: ReadPortGatherTest read-port tolerance]. Programs must not rely on it. meshsim checks the rule: an outside packet (other than a C&C command word) accepted while read-port results are outstanding is a violation (`Config.checkReadPortOverlap`, on by default).
- **Not between `WFI` and a start store.** Read-port traffic belongs before `done` or after the start.
