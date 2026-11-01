# Tile programs: the v1.2 control conventions (draft)

**v1.2 draft, from `planning/DECISIONS-v1.2.md` sections 1-44 (cited as D*n*), against `sw/mesh-v1.2-draft.h`.** The worked code for every operation is `sw/RECIPES.md`, whose recipes are cited here as R*n*. The spec references are to `spec/machine-spec.md`, which WORKER5 is rewriting to v1.2; where the two disagree the log is the authority until the spec is reviewed. Anything marked *(proposed)* is this draft's choice where the log leaves a number or a name open; WORKER4 settles it in the generated `mesh.h`.

## 1. Build

- **Toolchain:** `riscv64-unknown-elf-gcc -march=rv32i_zicsr_zmmul -mabi=ilp32 -std=gnu11`. v1.1 built `rv32i_zmmul`. CSR instructions are legal now for the CSRs the core implements (D1.2), and the ROM, the handler and boot part 2 use them, so `zicsr` is needed. `gnu11` (or `c11`) is needed for the register block's anonymous unions (W3-27). A software divide still fails the build unless `ALLOW_SOFTDIV=1`.
- **Register access:** every register and window word is one `lw`/`sw`. The controller block's addresses are D11's, with D40's seven counters (0x860-0x878) and the read port at 0x87C and 0x880. Through the header's pointers (`tile->EVENTS`) GCC 13 at `-O2` emits x0-relative accesses, as v1.1's constants did; checked in the disassembly of every recipe (R13).
- **The memory map** (decided: the regions and sizes, D5; the reserved bytes, D37.4; the command and reply blocks' addresses, W5-01, spec 4.11-4.12; the stack is a convention):

  | range | contents |
  |---|---|
  | 0x01000000-0x01007FFF | program memory, 32 KB: code, and `.rodata` if wanted (the controller fetches and loads it; a store is a bus error, D5) |
  | 0x00000000-0x0000000F | reserved: never placed; meshsim flags any write after boot (D27, D37.4) |
  | 0x00000010-0x00007BBF | `.data`, `.bss`, then the stack, growing down from 0x7BC0 (`STACK_TOP`) |
  | 0x00007BC0-0x00007FBF | the reply block: 256 words, two per source endpoint (D6) |
  | 0x00007FC0-0x00007FFF | the command block: 16 slots (D6) |
  | 0x02000000 | the boot ROM: the reset vector (D22) |

- **Images** are loaded by C&C 1 as uncounted 128-bit mode-0 overlays (R22), so each is padded to 16 bytes and starts 16-byte aligned: program memory at 0x01000000, data memory at 0x00000010.
- **`.bss` is not loaded.** `crt0` zeroes it and calls `main`, which never returns. A meshsim test ends by storing to the simulator-only exit word (`sim_exit()`, D36); `ebreak` now traps (D27).

## 2. Addresses

- **One 32-bit map for packets and the core** (D5): region [31:24], byte offset [23:0]. A C pointer to controller memory *is* the packet address; `CM_WORD()` is gone.
- **Big SRAM keeps 21-bit chunk addresses inside the tile's engines:** `PTR`, `STREAM_SRC`, `SRAM_RD_ADDR_QUEUE`, `DMA_RECORD_BASE_ADDRESS`. A packet to big SRAM uses `SRAM_ADDR(chunk)` = 0x03000000 | chunk << 2 (D29). `sram_chunks()` reads the size from `PARAMS` [31:27].
- **Natural alignment everywhere** (D26): every 64-bit access 8-byte aligned and every 128-bit access 16-byte aligned, packets and big-SRAM accesses alike, checked in hardware (`ERRORS` [1]). In C, a landing buffer for 64- or 128-bit packets is declared `__attribute__((aligned(8)))` or `aligned(16)`. A `PTR` is aligned to its packets' size, and one pointer takes one size. A stream's source and every element it reads are aligned to the read size.
- **Out of bounds is not checked in hardware** (D26): it aliases or is undefined. meshsim errors on it, and on any auto-advancing address that runs past the end of its memory.
- **The command block:** `COMMAND[s]`, slot *s*; `COMMAND[0]` is the command word. **The reply block:** `REPLY[2 s]` and `REPLY[2 s + 1]` hold source *s*'s last reply; `REPLY_WORD(s)` is its byte address. That only commands write the first and only a source's own replies write its pair is a software convention now, checked by meshsim (D6, D27).
- **The table window** (`tables->`): `HIST`, `PTR`, the three `STREAM` arrays and `GROUP` at 0xFFFF2000 *(proposed: v1.1's low 16 bits, D11)*. Only `HIST` can be loaded. **When the tables may be touched:** section 5.

## 3. Boot (D22, D31, D36, D37)

1. **The reset**, always the whole chip, from outside (D35), zeroes everything but the memories. Every controller starts in the boot ROM with interrupts off.
2. **C&C 1** is started by the outside world (its overlays and first command land through Ingress/Egress, or the meshsim harness). It boots C&C 2 and C&C 3 (each jumps when its command lands and sends C&C 1 "started"), then the tiles.
3. **A tile** sleeps in the ROM with `done` pulsed. C&C 1 streams its overlays from its big SRAM, uncounted, a packet per cycle, with *d* = 126 for all tiles (D41; only the controllers broadcast from streams), and its **first command, which is even: `CMD_JUMP` with the entry address in slot 1**, as one 128-bit packet at slot 0, the same run's last stream. One source and one kind (D36): the command lands last.
4. The landed command raises READY; at GO the ROM jumps **if a command has landed** (the event [9], which nothing in the reset mask consumes, so the ROM's tile branch mustn't either, D41); otherwise it pulses `done` again and keeps waiting (D37.1). So C&C 1 lands a tile's first command only when that tile is to start at the next GO.
5. **Boot part 2** is the image's first command, `CMD_STAGE` with the number of 128-bit packets of big-SRAM data. In its start path the tile arms counter 1, then sets `mtvec` to its own handler and enables `mie.MEIE` and `mstatus.MIE` (D37.2); C&C 1 streams the data counted, to region 0x03 by address, no sooner than `stage_delay` after its GO store (timed by `mcycle`, D41); the tile waits for the counter and pulses `done` (R22).
- **Uncounted traffic is commands and boot overlays only** (D17). Everything a running program receives is counted, and modes 1-3 are always counted. Data C&C 1 sends a running tile is announced first by a command, as in boot part 2 (v1.1's announced staging, 6.2). There is no `PTR[64]` staging area and no "halt the tile first": C&C 1 never sends a tile anything it hasn't armed for.

## 4. Commands and phases (6.3)

**C&C 1's command convention** (R27):
- **Argument blocks alternate.** Even commands put their arguments in slots 1-7, odd commands in slots 8-15: seven at most.
- **The command word carries the parity:** [31:24] type, [23] parity, [22:0] argument.
- **Slot 0 goes last.** Commands are uncounted mode-0 writes (D6): the arguments, then the word to slot 0. The slot-0 write sets "command landed" when it lands in port B, so the whole command has landed by then. A single 64- or 128-bit packet covering slot 0 does too.
- **Parity is counted per tile, and starts at 0 after every reset.** A broadcast needs every tile at the same parity.
- **One kind per command:** all of a command's packets to a tile are broadcast, or all unicast.

**The tile's command loop** (R1):
```c
for (;;) {
    uint32_t w = wait_for_command();          /* pulse done, then WFI (back to back); woken by GO alone (the done gate),
                                                 a load of EVENTS consumes GO, then a load of slot 0 */
    volatile uint32_t *arg = CMD_ARGS(w);     /* arg[0], arg[1], ... (7 at most) */
    ...
}
```
- **Every wake from a command goes through `wait_for_command()`,** barriers included.
- **Keep GO in `EVENTS_MASK`** whenever `done` is pulsed (the reset mask has it).
- **C&C 1 waits `go_latency` + 1 cycles after a GO store** before sending anything, then sends the next command at once.

**A phase whose engines must start together** (an all-to-all, R9) uses two commands:
1. **Configure:** the tables, `DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS`. **Arm nothing**: `done` with a counter armed is done early.
2. **Start:** wake, **arm the counters first, then `PULSE_DMA`**, then wait for the counters and DMA run done, then `done`.

**The code from `WFI` to the start store is identical on every tile.** Counter values may differ. The window between GO and arming is closed by this timing (D36): nothing arrives sooner than a sender's own start store plus the lead-in and a link. If it fails, unexpected arrival says so, and traps.

**The lead-in** (spec 4.2), with *L* = `ts_engine_latency`: a group run's first request 4*L* + 3 cycles after the start store, a stream run's 2*L* + 2. The cycles from GO to the start store are to measure on meshsim v1.2.

## 5. Waiting: counters and events (D3, D16, D17, spec 4.12)

**All wakes are events.** `EVENTS` collects them; `WFI` wakes on `EVENTS & EVENTS_MASK` (with `EVENTS_REQUIRED` for all-of waits) and consumes nothing. The helpers do: `wfi()` is `WFI` then a load of `EVENTS`, which consumes the pending events in the mask and returns them all; `wait_all(ev)` waits until every event in *ev* is pending (R0). `EVENTS_PEEK` looks without consuming.

| bit | event | kind |
|---|---|---|
| [0]-[6] | write counter 1-7 done | sticky |
| [7] | counters done: all seven at 0, one armed since it last fired | sticky |
| [8] | GO | sticky |
| [9] | command landed (nothing in the reset mask consumes it, D41) | sticky |
| [10] | DMA run done (the address generator's run); `PULSE_DMA` clears it | sticky |
| [11] | request FIFO no longer full | sticky |
| [12] | request pending (controllers only) | live |
| [13]-[15] | peripheral 1-3: its `EVENTS & EVENTS_MASK` non-zero | live |

Renumbered by D40. `EVENTS_MASK` resets to GO | counters done.

- **Seven counters.** The packet names its counter (D3, D40; modes 0-3 and 5 count, D43): 0 uncounted, 1-7 (`WRITE_COUNTER_1`-`7` at 0x860-0x878). **The convention** *(proposed; the hardware treats the seven alike)*: **1 big SRAM** (local runs, all-to-alls, staged data), **2 controller memory** (block copies, vectors, read-port results), **3 tile-to-tile replies, 4 peripheral writes** (the replayer's refills and state), **5-7 one per outstanding request** (`CTR_REQ(k)`, k = 0-2; section 6b). A fence takes any counter the phase isn't using; the draft's `CTR_FENCE` is 7, so no fence while request slot 2 is outstanding. Each thing a phase waits for separately has a counter of its own (D3's usage rule); a phase may reassign them as long as it keeps to that.
- **A counter decrements *K* cycles after the director accepts its packet** (D3, D32), by which time the write is readable, a `PTR` or `HIST` update included (D17 A).
- **A store adds:** *N* > 0 arms the counter, clearing its done event and counters done. **A count of 0 never fires,** so don't wait for an unarmed counter.
- **Unexpected arrival** (a decrement at 0) and **done early** (`done` with a counter non-zero) are errors, and trap.
- **The rule: a phase that arms a counter waits for it before `done`.**
- **The done gate:** from the `done` pulse until GO only GO wakes `WFI`, whatever the mask; counters done can't be set in that window, and the pulse clears it.
- **The tables** (`STREAM`, `GROUP`, `PTR`, `HIST`) are free **per memory** (D47): `STREAM` and `GROUP` once the tile's own run is over (DMA run done, [10]), `PTR` and `HIST` once the arrivals are in (their counter) (D17; R23). To touch both kinds, wait for both. **Every phase that runs the address generator waits for both before `done`,** so every command starts with the tables free and nothing waits before touching them. `wait_tables()` and `ts_busy` are gone. A helper that can't know what ran before may poll `STATUS.rd_running` (`wait_rd_idle()`), which falls in the same cycle as DMA run done (D17 B, D41); it says nothing about arrivals, so only when nothing counted can still be arriving.
- **Peripheral events** (D10): each peripheral bit is a small counter, an ordinary event of depth 1 or a queue's credits of depth 2, or a live bit. A load of the peripheral's `EVENTS` takes one count from every set bit in its `EVENTS_MASK` (0 at reset). The tile's bit for the peripheral is live. **A program acts on every bit a consuming load returns,** or narrows the mask around the load (R25, R26). Spurious wakes are allowed: **re-check after every wake.**
- **A plain `wfi()` on counters done** (D47): under the reset mask (GO | counters done), a bare `WFI` waits for [7], all seven counters at 0, and saves the four stores `wait_all()` costs. Sanctioned **only when no other counter is armed** (no request outstanding on 5-7, no other phase's counter), since [7] needs every counter at 0.
- **Polling a landed word** in data memory (D47) to learn that a sender's earlier packets are in: valid **only when those earlier packets also land in controller memory**. One unit's packets to one destination arrive in order (D44) and port B writes them in that order, so the marker lands last. Packets to big SRAM take the slower write path, and a marker in data memory can land before them: wait for their counter instead.
- **What a wait returns is what it consumed;** sticky events outside the mask stay pending. Counters done is left pending by a wait on one counter, and is cleared by the next arming or `done`. Command landed [9] stays pending in a tile, since nothing consumes it; the boot ROM relies on that (D41).

## 6. Sending (D6, D7)

- **The send port:** `ROUTER_HDR` (counter, mode, dst, and the no-increment bit [0]) and `ROUTER_ADDR` (the full 32-bit address), then one store per 32 bits of each packet in ascending order, the last store sending: `send32(w0)`; `send64(w1, w0)`; `send128(w3, w2, w1, w0)`. `send_to(hdr, addr)` stores both. `ROUTER_SET_W0`-`W2` clear at every send.
- **`ROUTER_ADDR` advances by each packet's size** (4, 8 or 16 bytes) for mode 0 only (D42; modes 1-4 ignore it), unless no-increment is set. A run that goes down in size and back up re-stores `ROUTER_ADDR`, or its larger packets are misaligned (W3-29).
- **The no-increment bit stays set until the program clears it** (D37.5). A program sending to one address in many tiles (replies, R7) sets it, and clears it when done.
- **Every header names its counter** as a required macro argument (`ROUTER_HDR_W(dst, mode, ctr)`), and `CTR_NONE` only for commands and boot overlays.
- **Writes to big SRAM:** mode 0 to region 0x03 at an address (R17), or mode 1 to the tile itself through `PTR[me]`.
- **One word to another tile** is a reply: by default (D40) a 32-bit mode-0 write to `REPLY_WORD(me)` in the receiver, with no-increment, counted on the counter the receiver armed (counter 3). A tile keeps at most one reply outstanding to each receiver until it has been read. **Several words** go to an address in the receiver's data memory, counted on counter 2.
- **Ordering holds per sending unit, not per tile** (D44). Packets from one unit (the address generator, the send port, the GATHER engine, the read port, each peripheral's DMA) to one destination, of one kind, arrive in the order that unit sent them; between units there is none, even in one tile. DMA run done doesn't mean a run's packets have left (D43). To make a send-port packet follow a run, put it in the run or **fence** the run: end it with a count-only packet to the tile itself on a spare counter and send once that counter is done (R28). Counted data needs neither.
- **Rate:** one packet per cycle when no read return is waiting; a send store stalls while the holding register is occupied (`STATUS` [3] lets a program avoid that). A packet to the tile itself or a broadcast also waits for a loopback token (S9).

## 6a. Packet modes (D6)

| mode | `mesh-v1.2-draft.h` | packet | lands | from a stream |
|---|---|---|---|---|
| 0 | `MODE_WRITE` | write at the address: any region a packet may reach (0x00, 0x01, 0x03, a present peripheral) | at the address | yes |
| 1 | `MODE_SRC` | big-SRAM write by source | `PTR[src]` | yes |
| 2 | `MODE_BUCKET` | bucket write | `PTR[digit]` | yes |
| 3 | `MODE_AUGMENT` | augmented write: the low word and the ordinal as a 64-bit record (32 or 64 bits, never 128) | `PTR[src]` | yes |
| 4 | `MODE_GATHER` | gather request, to the tile itself; never counted; its counter field is its values' | the GATHER engine | yes, *d* = 0 |
| 5 | `MODE_COUNT` | count only (D43): decrements its counter, writes nothing; any size, payload and address ignored | nothing | yes (the source is read, the payload discarded) |
| 6-7 | — | reserved: discarded with no error and no decrement (meshsim flags) | — | — |

- **The counter field counts modes 0-3 and 5**; mode 4 passes it to its values; 6-7 decrement nothing. A count-only packet with counter 0 does nothing, and meshsim flags it.
- **Count-only packets** (R28): a **fence** ends a run with one to the tile itself on a spare counter (`CTR_FENCE`); a **signal** tells another tile "done" for one packet, on a counter it armed.
- **Modes 1 and 3 may mix in one phase** (both through `PTR[src]`); mode 2 never shares a phase with them.
- **Bad packets** (`ERRORS` [1]): a misaligned 64- or 128-bit write; a region a packet can't reach (0x02, 0x07-0xFE, 0xFF, an absent peripheral); mode 3 at 128 bits. **An all-zero header is a valid uncounted write to address 0** (D6), which is why the first 16 bytes are reserved and meshsim flags writes there.

## 6b. Requests (spec 3.3, 4.18; D21, D25, D38)

- **A request is 128 bits:** `request_send(w3, w2, w1, w0)`, words 0-2 then the last store pushes, stalling only while the 128-entry request FIFO is full. Software writes the target in [9:7] and 0 in the requestor [6:0], which the tree stamps.
- **Targets:** 0 C&C 1, 1 C&C 2 (the file system), 2 C&C 3 (the pager) *(spec draft 3.3)*. The layout above [9:0] is the target's; `REQ_CC1(op, ctr, arg)` is a placeholder for C&C 1's.
- **Outstanding requests per tile** (D38): **at most 1 to C&C 1, 3 to C&C 2, 1 to C&C 3.** A tile issues its next request to a target only after the previous one's reply has come back. The receive queues are in the tree (128, 256 and 128 requests), so normal traffic never runs out of credits and no row stalls.
- **A request is outstanding until everything it brings back has landed** (D44, a software convention): its reply, if any, and its data. A read, which has no reply, is outstanding until its data has landed. The limits count requests that way.
- **Every request names its reply's counter and its own reply address**, anywhere in the requester's data memory (D25, D40), so no two outstanding replies share a buffer. **The convention: request slots** (R21). Slot *k* (0-2) owns counter `CTR_REQ(k)` = 5 + *k* and a reply buffer; `request_issue(k, …, data, reply)` arms the counter for everything the request brings back (its data packets, plus 1 if it has a reply) and sends; `request_wait(k)` waits. Three slots cover C&C 2's limit, so a tile has at most three requests outstanding in all by this convention.
- **Error reports are the exception:** the handler sends its 24 at once, with no reply (R24).
- **A target answers every tile request,** at the address and counter it names, or the tile waits for ever (R27's `drain_requests`).
- **Receiving** (controllers only): `REQUEST_GET_W0`-`W2` read the oldest request without popping, and `REQUEST_RECEIVE` reads its last word and pops it. Popping an empty queue is receive underflow, an error, so check `EVENTS_PEEK` [12] first.
- **Bounded waits** on a controller use `mcycle` (D41).

## 7. Parameters, status and errors (D18, D20, D27)

- **`PARAMS`** (constant): peripherals present [2:0], endpoint id [26:20] (`tile_index()`), big-SRAM size in 256 KB units - 1 [31:27] (`sram_chunks()`).
- **`STATUS`** (live, diagnostic; no program needs to poll it): `rd_running`, `rd_done`, GATHER busy, `send_busy`, `done`, command landed, `request_busy`.
- **`ERRORS`** (sticky until the reset; [13:11] live): any bit raises the hard interrupt and the error line (R12 has the table). **Once boot part 2 has enabled interrupts, a running program always reads 0 here**, since the first error traps. Before that an error raises the error line and the toast pin, but takes no trap.
- **The handler** (R24): terminal (no `mret`), reports the fixed set of 24 requests to C&C 1 (the spec defines it, D41), then parks. It reads `EVENTS_PEEK`, never `EVENTS`; it checks `PARAMS` [2:0] before reading a window; absent peripherals report zeros (D37.3). The image installs it in boot part 2, and the ROM holds the same code until then.
- **Traps without an `ERRORS` bit** (`ecall`, `ebreak`, a fetch outside program memory and the ROM, a misaligned jump, a trap in the handler) park the unit with no error line; meshsim stops loudly on each (D27).
- **Write-only registers read 0**, except the request port's shared offsets, where a load reads the receive side. Reserved words read 0 and ignore stores (meshsim flags them), except the simulator-only trace and exit words. **An absent peripheral's window is a bus error.**
- **Timing:** a register load returns the state at its sample point; a store at *t* acts from *t* + 1.

## 8. Block copy into controller memory (6.5; R3)

```c
set_stream(s, STREAM_SRC_W(src_chunk, 0), dst_addr, STREAM_COUNT_W(n, SIZE_128, CTR_CM, MODE_WRITE, 0));
tile->DMA_CONFIG = DMA_CFG_RUN(RUN_STREAMS, s, 1);
tile->WRITE_COUNTER[CTR_CM - 1] = n;              /* n 128-bit packets, 4n words */
start_run();
wait_all(EV_CTR(CTR_CM) | EV_RUN_DONE);           /* the last word is in memory; the tables are free */
```
The destination is any data- or program-memory address, 16-byte aligned (the source 4-chunk aligned). The code doing the copy must sit outside the region being overwritten.

## 9. Reading big SRAM: the read port (4.3; D13; R11)

- **`SRAM_RD_DEST_ADDR`** = `RD_DEST(byte_offset, size, counter)`: the data-memory offset of the next result (data memory only), its size, and its counter. It advances by the size per queued read, and a load returns it.
- **A store to `SRAM_RD_ADDR_QUEUE`** queues a read at a chunk address aligned to the size, keeping the counter `SRAM_RD_DEST_ADDR` holds then (each entry carries its own, D42). It never stalls. A misaligned read is not performed, but its packet still goes out with payload 0 and is counted, and `ERRORS` [1] is set (D36). A store with no free entry is a read-port error ([7]).
- **Results** are mode-0 writes to the tile itself, in any order, each at its own offset, counted on the named counter. Only the counter says they have landed; a free entry means only that its return has left.
- **Read-port results and incoming traffic:** as in v1.1, a tile does not issue read-port reads while packets are arriving for it, and doesn't start a phase that receives while results are outstanding (invariant 14). Whether S9's required protected loopback lets the spec retire the rule is open.
