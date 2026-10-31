# The change guide to v1.25, from v1.1 and from v1.2

For the RTL engineer of the mesh. This guide covers two upgrade paths to **v1.25**:

- from **v1.1**, which the RTL implements today, straight to v1.25;
- from **v1.2**, the frozen waypoint, to v1.25.

It is organized **by hardware unit**. Each unit's section lists everything that changed in that unit since v1.1, and where v1.25 revised a v1.2 change, it gives the final state.

**Sources.**
- **The authority is `spec/machine-spec.md` v1.25** (with `spec/replayer-opcodes.md`). This guide matches **spec commit `0b4ba20`** *(to be re-stamped when the guide is finished)*.
- It folds together `CHANGES-v1.2.md` (v1.1 → v1.2) and `CHANGES-v1.25.md` (v1.2 → v1.25).
- It was checked against all three specs (`v1.1/`, `v1.2/`, `wip/`).
- It cites `v1.2/ERRATA.md` (**E*n***) where an entry corrects frozen v1.2's text.
- It adds the differences neither change document lists. These are marked **[not in CHANGES]**, and section 5 collects them.
- Decisions are cited as **D*n*** (`planning/DECISIONS-v1.2.md` section *n*), **V*n*** (`planning/DECISIONS-v1.25.md` section *n*), **O** (an owner's ruling) and **W*n*-*nn*** (a review finding).

---

## 0. How to read it

**Two paths.** Every entry carries a **step**:

| step | meaning | reader from v1.1 | reader from v1.2 |
|---|---|---|---|
| **v1.2** | changed between v1.1 and v1.2, unchanged since | applies | **skip**: v1.2 already has it |
| **v1.25** | changed only between v1.2 and v1.25 | applies | applies |
| **both** | changed in v1.2, then changed again in v1.25 | applies (take the final state) | applies (from the v1.2 state shown) |

- **From v1.1:** read everything. Implement the **v1.25** column, and ignore the v1.2 state shown in "both" entries, which is there for the other reader.
- **From v1.2:** read only the entries marked **v1.25** or **both**. Section 1.2 lists them. Almost all are in the replayer (section 2.6).

**Each entry** gives:
- what v1.1 said or built;
- what v1.25 says, with its spec section;
- the **kind**: new, changed, removed or renamed;
- the **step**;
- **why**, citing the decision.

For a **both** entry, the v1.25 column gives the final state first, then "*v1.2:* …", the state the v1.2 reader starts from.

**Names.** Registers are written with their v1.25 names, and their v1.1 names alongside wherever they differ. Bit positions are [*hi*:*lo*]. Addresses are v1.25's 32-bit byte addresses unless marked "v1.1".

**Who checks what** (v1.25 front matter; D27). The hardware checks only what is cheap and catches a bug far from its cause:
- overflows;
- unexpected arrival;
- done early;
- drift;
- alignment;
- the region decode.

Every other rule in the spec is a **meshsim** check, which the hardware ignores. v1.1 mixed the two. This guide marks hardware checks **(hardware)**.

---

## 1. Overview

### 1.1 From v1.1: the machine then and now

**At v1.1:**
- a 169-bit link word with a 21-bit chunk address and a 4-bit mode, modes 0–15;
- four mode-named down-counters, decremented as the director accepted a packet;
- 8 KB + 8 KB controller memories with packed 14-bit word addresses and a resident loader;
- a RUN/HALT net, so a tile could be halted, loaded while halted, and resumed;
- one C&C controller of its own design at endpoint 64, with the memory system served at its port;
- one peripheral slot per tile, with six service lines, eight status registers, mode 9 and a sender;
- a UART on every tile;
- spanning (unaligned) big-SRAM accesses;
- errors as "conditions" that halted nothing but the controller.

**At v1.25:**
1. **The link word is 256 bits** (3.1): a 128-bit payload, a **32-bit byte address**, size, a **3-bit counter field**, a **3-bit mode**, src and dst. 74 reserved bits are stored in full.
2. **One 32-bit address map** (2.2) serves packets and the controller's own loads and stores. Its regions are data memory, program memory, the boot ROM, big SRAM, three peripheral regions and the register region 0xFF. The engines' big-SRAM pointers stay 21-bit chunk addresses.
3. **Six modes:**
   - 0, write at an address (commands, replies, controller memory, peripherals and explicit big-SRAM writes);
   - 1, by source;
   - 2, bucket;
   - 3, augmented;
   - 4, gather request;
   - **5, count only**, which writes nothing.
   Modes 6–7 are discarded silently.
4. **Seven write counters, named by the packet**, decremented **K cycles after the traffic director accepts it** (K a parameter, default 8, set by the implementation). Counting no longer depends on the mode or the source.
5. **Natural alignment everywhere, checked in hardware.** Spanning accesses are gone. A misaligned packet is dropped, and a misaligned read sends a payload of 0. Both set `ERRORS` [1].
6. **The controller block is new**: every offset moved. It starts as every peripheral's window does: `PARAMS`, `STATUS`, `ERRORS`, the pulses (one register per action) and the events (`EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, `EVENTS_REQUIRED`). The send port takes a 32-bit `ROUTER_ADDR` and a `ROUTER_HDR` with a no-increment bit.
7. **The core takes traps.** It has the CSR instructions, the machine-mode trap CSRs, `mcycle` and `mtime`. Every error raises a level hard interrupt, and every trap is terminal: the handler sends 24 fixed reports to C&C 1 and parks.
8. **There is no HALT state.** Each unit has an **error line** into C&C 1. Any error latches the chip's **toast pin**, and the outside world applies a **hard or soft reset**. Every unit then starts in its **boot ROM** at 0x02000000.
9. **Controller memories are 32 KB data + 32 KB program.** Program memory is loadable but not storable. The command block sits at 0x00007FC0 and the reply block at 0x00007BC0. Port B takes one 128-bit write per cycle.
10. **Peripherals:** up to three per unit. Each has a region (0x04–0x06), a 0x200-byte standard window, counted (depth 1 or 2) or live events, its own `ERRORS`, and optionally a DMA on a router port of its own. The replayer is peripheral 1, **type 4** (was 1). Its queue low-water lines are now **counted credits**.
11. **Three controllers, C&C 1–3, at endpoints 64–66**, on tiles 0–2's fifth ports. Each is a tile instance with 2 MB of big SRAM, and its engine is its peripheral. Every chip-level register (READY, GO, error lines, watchdog, LED, UART) is in **C&C 1's peripheral**.
12. **The request bus** keeps its tree, but the receive queues are at the root (128/256/128 entries for C&C 1/2/3). Tiles have no receive queue, and every request names its own reply address and its counters.
13. **The replayer matches jolt-cpp `821e1bd`** (v1.25's step):
    - six new memory row opcodes;
    - opcodes renumbered (61 change);
    - `offset_imm` and `sign_imm` in the expansion word;
    - `imm_signed` in the output row;
    - PEXT correct for any mask;
    - universal tables, with unused decode entries invalid and controller-fed templates found by a normalized key.

### 1.2 From v1.2: what v1.25 changed

Only the replayer (spec 4.19) and what follows from it: the front matter, invariant 16, the replayer's parameter row and section 13. Everything else is v1.2's. The **v1.25** and **both** entries, in brief:
- **The reference is jolt-cpp `821e1bd`** (4.19.1).
- **The row opcodes are renumbered:** v1.2's 0–109 are unchanged, 110–111 move by +6, 114–153 by +4 and 155–167 by +3. 61 row opcodes change, and the ALU decodes the new numbers (4.19.7; appendix 2.0).
- **Six new opcodes,** `VirtualLoadB/H/W` (110–112) and `VirtualStoreB/H/W` (113–115). These are class L memory rows: load rows take the read log, store rows the write log. Their address is rounded to the doubleword.
- **Three opcodes removed:** `VirtualAlignAddr`, `VirtualAssertHalfwordAlignment` and `VirtualAssertWordAlignment`.
- **`VirtualShiftData…` changes:** rs2 is now the base register, the lane comes from rs2 + `shift_offset`, and the row's immediate is `shift_offset`.
- **`VirtualHostIO` has no operands** and is never remapped.
- **The expansion word gains bits:** `offset_imm` [63] and `sign_imm` [62]. `advice_reg` narrows to [61:56], stored as register − 146.
- **The output row** gains `imm_signed` (word 0 [86]). `mem_addr` and `mem_value` are formed for all eight memory opcodes.
- **`VirtualPext` / `VirtualPextSigned`** are correct for **any** mask, and a zero mask gives 0. Full speed is promised for contiguous masks only.
- **The replayer executes 88 opcodes** (was 85).
- **Write-log E[15] is updated by every load row** (4.19.15).
- **The tables are universal.** Decode class 2 means CSRRW/CSRRS only, and an unused entry is class 3 (invalid). The ROM holds 388 of 512 words, with ECALL, EBREAK and MRET now in the ROM. Controller-fed templates are found by a normalized key (software).

### 1.3 Removed outright (plan the deletions)

All removed at step **v1.2** unless marked otherwise.

| removed | v1.1 | replaced by |
|---|---|---|
| HALT, the RUN/HALT net, `run_latency` | 6.1, 2 | the reset; terminal traps; toast pin (6.1) |
| per-unit halt on bus error, illegal instruction, `ecall`, `ebreak` | 4.11 | traps to `mtvec` (4.11, 4.13) |
| the resident loader at 0x1F00 | 4.11 | the boot ROM (6.2) |
| REPORT and C&C's receive FIFO and its error flag | 6.6 | the handlers' 24 reports over the request bus (4.13) |
| the C&C controller's own design, its RUN/HALT and GO registers | 6.6 | C&C 1–3 as tile instances; `PULSE_GO` in C&C 1's peripheral |
| the error bit ORed from all tiles at C&C | 2, 6.6 | one error line per unit into `ERROR_LINES_0`–`2` |
| the four named counters `DMA_SRAM_`, `DMA_CM_`, `DMA_REPLY_`, `DMA_PERIPH_REMAINING` | 4.12, 8.7, 8.11 | `WRITE_COUNTER_1`–`7`, named by the packet |
| modes 0 (invalid), 3 (explicit big-SRAM), 6 (CM write), 7 (command), 8 (reply), 9 (peripheral write), 10–15 and their bad-packet rules | 3.1 | mode 0 at an address, by region; new bad-packet table |
| spanning big-SRAM accesses (two area accesses, part 1 ahead of every client) | 4.1, 4.5, 4.9 | natural alignment |
| big-SRAM out-of-range rule (wrap mod 2²¹; reads 0, writes dropped above the device) | 4.1 | undefined (2.2) |
| `mem_idle`, `cm_idle` | 4.1, 4.12, 8.2 | K (meshsim diagnostics only) |
| `ts_busy`, `wait_tables()` | 4.10, 8.2 | waiting by events; guarantees A–C |
| packed 14-bit controller-memory word addresses | 4.11 | byte addresses |
| `TILE_STATUS`, `TILE_CONDITIONS`, `TILE_PULSE`, `ROUTER_SET_HDR` | 8 | `PARAMS`/`STATUS`, `ERRORS`, pulse registers, `ROUTER_HDR` + `ROUTER_ADDR` |
| `WAKE_*` names and layout | 8.10 | `EVENTS_*` (renumbered) |
| the peripheral slot: `PERIPH_RUN`, `periph_running`, six service lines `WAKE_EVENTS` [18:13], `PERIPH_STATUS_0`–`7` | 4.17 | the standard window (4.17.4) |
| the sender: `SENDER_SEND`, `SENDER_ADDR`, `sender_busy`, sender idle | 4.17.9, 8.12 | a peripheral's DMA (4.17.9) |
| per-tile UART (`UART_*` at 0x890–0x89C, events [21], [22]) | 4.20, 8.13 | the UART in C&C 1's peripheral only |
| per-tile 16-entry request receive queue | 3.3, 4.18 | receive queues at the root (C&C 1–3 only) |
| `PTR[64]` as C&C's staging pointer | 4.9, 6.2 | staging by address into region 0x03 |
| write tokens and mode-3 write streams to C&C | 6.7 | writes by tags *(Deferred, item 119)* |
| the replayer's occupancy/pending low-water lines and claim rule (items 50, 51) | 4.19.8 | counted credits |
| `VirtualAlignAddr`, the two alignment assertions; the contiguous-mask narrowing of PEXT; per-program tables; "unused entries are controller class"; the exact-word template key; `VirtualHostIO`'s remap; EBREAK, ECALL and MRET as controller-fed | 4.19 | **step v1.25** (2.6) |

### 1.4 Entirely new (plan the additions)

All new at step **v1.2** unless marked otherwise.

| new | v1.25 section |
|---|---|
| the 32-bit address map and region decode (data, program, boot ROM, big SRAM, peripherals 1–3, register region 0xFF) | 2.2, 4.8 |
| the boot ROM in every controller, reset vector 0x02000000 | 6.2 |
| terminal traps; the hard-interrupt and wakeup inputs; `mcycle`, `mtime`; the 24-report handler convention | 4.11, 4.13 |
| per-unit error lines, `error_latency` | 2, 6.1 |
| the toast pin, the `LED` pin, the hard and soft reset inputs | 2, 6.1 |
| the keep-alive `WATCHDOG` | 6.1, 8.14 |
| count-only packets (mode 5) and the fence | 3.1 |
| the counter field and the K-stage delay line | 3.1, 4.8 |
| seven write counters and their per-counter done events | 4.12 |
| `ROUTER_ADDR` and no-increment | 4.11 |
| the standard peripheral window (ten words), counted events with depth, per-peripheral `ERRORS` | 4.17 |
| a peripheral's DMA on its own router port | 4.17.9 |
| C&C 1's peripheral: `ERROR_LINES`, `READY_LINES`, `READY_EXPECT`, `PULSE_GO`, `WATCHDOG`, `LED`, UART words | 8.14 |
| C&C 2 and C&C 3 (endpoints 65, 66, tiles 1–2's fifth ports) | 2, 6.6 |
| receive queues at the root of the request tree; per-tile request limits | 3.3 |
| broadcast streams (*d* = 126) | 4.2 |
| the simulator-only exit and trace words | 8.15 |
| `offset_imm`, `sign_imm`, `imm_signed`; `VirtualLoadB/H/W`, `VirtualStoreB/H/W`; the normalized template key | **step v1.25**: 4.19.3, 4.19.7, 4.19.9, 4.19.12 |

---

## 2. By hardware unit

Each table lists the unit's changes in v1.25 spec order. The columns are: **v1.1**, what the RTL has; **v1.25**, with its spec section; **kind**; **step**; and **why**. An ID (L1, A1, …) lets later sections refer back.

### 2.1 The link word and the network (spec 3.1–3.3, 4.5–4.8, 4.16)

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| L1 | **169-bit** link word: src 7, dst 7, mode 4, addr 21 (a chunk address), size 2, payload 128; no bit positions fixed | **256 bits** (3.1): payload [127:0], address [159:128] (32-bit, region [31:24] + byte offset [23:0]), size [161:160], **counter [164:162]**, mode [167:165] (3 bits), src [174:168], dst [181:175], reserved [255:182] sent as 0 and ignored. **All 256 bits are stored** in every FIFO and stage *(item 117: trimming to be revisited)* | changed | v1.2 | a fixed width so later field changes don't resize the links again; D2 (O, T) |
| L2 | — | **link [167:160] = header register [24:17]**: size, counter and mode copied as one 8-bit block from `STREAM_COUNT`, `ROUTER_HDR`, `DMA_HDR` (3.1, 8.1) | new | v1.2 | one header layout; D12 |
| L3 | modes: 0 invalid, 1 gather request, 2 by source, 3 explicit big-SRAM address, 4 bucket, 5 augmented, 6 CM write, 7 command, 8 reply, 9 peripheral write, 10–15 reserved | **0 write at address** (any reachable region: data/program memory via port B, big SRAM, a peripheral), **1 by source** (was 2), **2 bucket** (was 4), **3 augmented** (was 5), **4 gather request** (was 1), **5 count only**, 6–7 reserved (3.1). There is no invalid code: an all-zero header is a valid uncounted mode-0 write to 0x00000000 | changed | v1.2 | the region says where a write lands and the counter field does the counting, so the mode keeps only what the receiver does differently; D6; count only D43 |
| L4 | — | **mode 5, count only**: writes nothing; size, payload and address ignored; decrements its counter at the destination like a write; from a stream the source is read and the payload discarded; to the tile itself it takes a read token; with counter 0 it does nothing (3.1, 4.5, 4.8) | new | v1.2 | the fence (a count-only packet to self ends a run) and "done" signals between tiles; D43 |
| L5 | modes 10–15 and 0: bad packets (dropped, condition) | **modes 6–7 are discarded at the destination with no write, no decrement and no error**, whatever their counter field (3.1, 4.8) | changed | v1.2 | D6, D32 |
| L6 | counting by mode into four named counters; command and gather request never counted; in HALT nothing counted | **the packet names its counter**: counter field 1–7 = `WRITE_COUNTER_1`–`7`, 0 uncounted; modes 0–3 and 5 counted by the field; a gather request never (its field is passed to its gather values); decrement **K cycles after the director accepts the packet** (3.1, 4.8, 4.12) | changed | v1.2 | counting independent of mode and source; D3, D30, D40 (seven) |
| L7 | bad packets: mode 0, 10–15; mode 5 at 128; mode 3 misaligned; mode 7 at 64/128; mode 8 at 128; condition "bad mode or size" | **bad packet** (hardware: dropped uncounted, `ERRORS` [1]): a 64/128-bit mode-0 packet not naturally aligned, in any region; mode 0 to region 0x02, 0x07–0xFE, 0xFF or an absent peripheral's region; mode 3 at 128 bits. Modes 1–5 ignore the address, region included. Size 11 and out-of-bounds offsets are undefined, not checked (3.1) | changed | v1.2 | only the cheap checks: alignment and region decode; D26, D32 |
| L8 | write to the absent peripheral (type 0): a bad peripheral write, **counted** in `DMA_PERIPH_REMAINING` | a bad packet, **uncounted** (3.1, 4.17.1) **[not in CHANGES]** | changed | v1.2 | follows from L7: the region decode drops it at the director; D9, D32 |
| L9 | mode-1 ordinal: the mode table says [63:32], the fixed-payload paragraph and 4.4 say [51:32] (v1.1 inconsistent) | gather request's ordinal is **[51:32]**, sent at 64 bits (3.1, 4.4) **[not in CHANGES]** | changed | v1.2 | v1.1's two statements reconciled |
| L10 | padding stages 169 flops per stage per link direction | 256 (3.2) | changed | v1.2 | L1; D2 |
| L11 | in-order delivery per sender (source → destination), one kind | **per sending unit** in one tile: the address generator, the send port, the GATHER engine, the read port, each sending peripheral's DMA. No order between units: end a run with a **fence** (L4) or put the packet in the run (3.2) | changed | v1.2 | D44 (round 3) |
| L12 | request bus: 16-entry receive queue **in every tile**, fed only on receivers; root has no queues; credits start at 16 | **receive queues at the root, in the tree**: 128, 256, 128 × 128 bits for C&C 1, 2, 3; credits = depth; tiles have none, and their receive registers read an empty queue (3.3, 4.18) | changed | v1.2 | sized by each target's traffic, not by the tile; D38 |
| L13 | — | **per-tile limits** of 1, 3 and 1 outstanding requests to C&C 1, 2 and 3, a software rule (meshsim invariant 17); error reports exempt; "outstanding" is a software convention (3.3) | new | v1.2 | so normal traffic never exhausts a target's credits; D38, D44 (W5-23) |
| L14 | target 3: the Updater/Binder/RW engine at endpoint 73 | C&C 2's peripheral *(Deferred, item 119)* (3.3) | changed | v1.2 | D25, D33 |
| L15 | read-queue request and injection fields: dst, mode 4, addr 21, size 2, payload (7+4+21+2+128) | dst 7, **mode 3, counter 3, address 32**, size 2, payload 128; the router adds src and the reserved zeros (4.5) | changed | v1.2 | L1 |
| L16 | 64 tokens *(Proposed, accepted)*; loopback hold *(Proposed)* | 64 tokens; loopback packets hold their token until they leave the loopback FIFO, **required**; also a **broadcast stream's read** keeps its token until its own copy leaves, and a **count-only packet to self** takes a token (4.5) | changed | v1.2 | D42 (S9), D41 (F14), D43 |
| L17 | the injection point: read returns and the send port; C&C enters through tile 0's fifth port | the same two sources; controllers through tiles 0–2's fifth ports; **a sending peripheral's DMA through a router port of its own** (4.5, 4.17.9) | changed | v1.2 | D10 |
| L18 | sixth route block on tile 0, for C&C; dst 64 treated as tile 0 | sixth route block on **tiles 0, 1 and 2**, for C&C 1–3; ids 64–66 route as tile id − 64; a packet with dst = 64 + me leaves through the fifth-port output (4.6) | changed | v1.2 | D25 |
| L19 | FIFO entries 169 bits; loopback FIFO ≥ token count *(Proposed)* | 256 bits; loopback FIFO ≥ 64, **required** (4.7) | changed | v1.2 | D2, D42 |
| L20 | traffic director switches on **mode** alone | on **mode, and for mode 0 on the region** (4.8): 0x00/0x01 → port B; 0x03 → write engine; 0x04–0x06 → that peripheral's write port; modes 1–3 → write engine; 4 → GATHER queue; 5 → count only; 6–7 → discard; bad → drop + `ERRORS` [1] | changed | v1.2 | D6, D9 |
| L21 | the director decrements a counter **on acceptance** (RUN only); `DMA_PERIPH_REMAINING` on landing or drop | the director puts the counter id into a **K-stage delay line**; K cycles later that counter decrements; at most one decrement per cycle; **unexpected arrival is judged at the decrement** (4.8) | changed | v1.2 | so "done means landed" for every destination; D3 |
| L22 | — | **K**: a parameter, default 8. **In hardware K is the implementation's**: at least the slowest write path (controller memory, peripheral, big SRAM up to its area queue, the `PTR`/`HIST` update). meshsim's formula is max(2L + 4, L + 4, `cm_latency`, `rp_latency`), L = `ts_engine_latency`. The real K comes back in the RTL's delta (4.8, 10) **[not in CHANGES: D49; v1.2 errata E4, E6]** | new | v1.2 | D3, D32; **D49** (2026-11-01; meshsim's 2L + 3 → 2L + 4, W4-08) |
| L23 | controller-memory and peripheral writes counted at landing (≈ 2 cycles) | **one K for every destination**: CM and peripheral writes also wait K, about 6 cycles more per counter round trip than v1.1; a separate 2-cycle delay is not adopted (4.8) **[not in CHANGES]** | changed | v1.2 | O, 2026-10-31 |
| L24 | "command landed" set by a mode-7 write to slot 0 | set when **a write that comes through the director and covers command slot 0 lands in port B** (any size starting at slot 0); the controller's own store to slot 0 doesn't set it (4.8, 4.12) | changed | v1.2 | so READY and the boot ROM follow the write; D36 |
| L25 | HALT: the director lands everything, counts nothing | no state changes routing or counting (4.8) | removed | v1.2 | no HALT; D23 |
| L26 | src used by modes 2/5 (`PTR[src]`) and mode 8 (reply block) | src used only by modes 1 and 3 (`PTR[src]`) and routing (4.8) | changed | v1.2 | replies are mode 0; D6 |
| L27 | `cm_idle`, `mem_idle` kept by the director/write path | gone from the hardware (meshsim diagnostics) (4.8) | removed | v1.2 | K covers both paths; D18 |
| L28 | fifth port connected at tile 0 to C&C; ten 16 × 169-bit FIFOs; "to C&C" into C&C's receive FIFO | connected at tiles 0–2 to C&C 1–3; ten 16 × 256-bit FIFOs; the controller's router port for the link *(Deferred, item 118)*; the memory system's traffic at tile 1 (4.16) | changed | v1.2 | D2, D25 |

### 2.2 The address map (spec 1, 2, 2.1, 2.2)

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| A1 | 21-bit chunk addresses in packets and engines; controller map: instruction 0x0000–0x1FFF, tables 0x2000–0x3FFF, data 0x4000–0x5FFF, registers 0xFFFFF800– | **one 32-bit map** for packets and the core (2.2): region [31:24], byte offset [23:0]; 0x00 data memory (32 KB), 0x01 program memory (32 KB), 0x02 boot ROM, 0x03 big SRAM, 0x04–0x06 peripherals 1–3, 0x07–0xFE reserved, 0xFF registers, windows and table window. A C pointer is the packet address | changed | v1.2 | D5 |
| A2 | — | **who reaches what** (2.2): packets reach 0x00, 0x01, 0x03 and a present peripheral's region; the core reaches 0x00, 0x01 (fetch and load; a store is a bus error), 0x02 (fetch, load) and 0xFF. The core never loads or stores big SRAM or a peripheral directly | new | v1.2 | D5, D9 |
| A3 | engines' big-SRAM addresses 21-bit chunks | **unchanged inside the engines** (`STREAM_SRC`, `PTR`, `SRAM_RD_ADDR_QUEUE`, `DMA_RECORD_BASE_ADDRESS`); the packet address of chunk *c* is `0x03000000 \| c << 2` (1) | — | — | D5 *(item 129: whether `STREAM_SRC` goes to 32 bits, open)* |
| A4 | no alignment required | **natural alignment**: 64-bit at a multiple of 8 bytes, 128-bit at 16, in every region (1) | new | v1.2 | D26 |
| A5 | out of range: wrap mod 2²¹; above the device reads 0, writes dropped | **out of bounds is undefined** (no check): the 32 KB memories alias (high offset bits ignored), big SRAM promises nothing; auto-advancing past the end of a memory is a software bug (2.2) | changed | v1.2 | Tony, 2026-10-28; D26 |
| A6 | — | **data memory's first 16 bytes reserved** (0x00–0x0F): a null store and an all-zero header land harmlessly; overlays start at 0x10 (2.2) | new | v1.2 | D37.4 |
| A7 | endpoints: 0–63 tiles, 64 C&C, 65 memory system (served at C&C's port), 66–125 reserved, 126 broadcast, 127 none | 64 C&C 1, **65 C&C 2**, **66 C&C 3**, 67–125 reserved, 126 broadcast **to tiles 0–63**, 127 none (2.1) | changed | v1.2 | D25 |
| A8 | one C&C (separate design) at tile 0's fifth port | **C&C 1–3** at tiles 0–2's fifth ports, each **a tile instance with 2 MB big SRAM**, its engine as peripheral 1 (2) | changed | v1.2 | D25 |
| A9 | global nets: RUN/HALT, GO, READY (64), one error line per tile ORed at C&C | **GO** from C&C 1's peripheral (`PULSE_GO`) to tiles 0–63 (not C&C 2/3); **READY** 64 lines into `READY_LINES`; **error lines**, one per unit by endpoint id into `ERROR_LINES_0`–`2`, live, `error_latency`; **hard and soft reset** inputs; **toast** and **`LED`** output pins; no RUN/HALT (2) | changed | v1.2 | D23, D24, D28, D34, D35 |

### 2.3 The tile's engines (spec 4.1–4.5, 4.9, 4.10, 4.14, 4.15, 4.18)

**Big SRAM (4.1)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T1 | any chunk alignment; a 64/128-bit access crossing a row **spans** two rows: one request and one token, but two area accesses (part 1 ahead of every client); the client's next request waits; group runs "should" be row-aligned | **every 64/128-bit access naturally aligned**; no access straddles a row; every access is one request, one token, one area access (4.1) | changed | v1.2 | D26 |
| T2 | (n/a) | **misaligned access (hardware):** a mode-0 packet → bad packet; a write through a misaligned `PTR` → dropped by the write engine, **still counted**, `ERRORS` [1]; a misaligned read by the address generator, GATHER engine or read port → **not performed, its packet sent as usual with payload 0** (so the receiver still counts it), `ERRORS` [1] at the issuing tile (4.1) | new | v1.2 | nothing hangs and the error names the tile; D32 |
| T3 | wrap mod 2²¹; above the device: reads 0, writes dropped; no condition | undefined; no error bit (4.1) | changed | v1.2 | A5 |
| T4 | `mem_idle` status bit; phase ordering by `mem_idle` + counters | `mem_idle` gone; ordering by K: when a write's counter decrements it has entered its area's queue, so a later read queues behind it (4.1) | removed / changed | v1.2 | D18, D3 |
| T5 | write-engine input overflow: condition 4 | `ERRORS` [4] (4.1) | renamed | v1.2 | D18 |

**Address generator (4.2)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T6 | start = `TILE_PULSE` [1] | **`PULSE_DMA`** (0x810); also **clears DMA run done, `EVENTS` [10]** (4.2) | changed | v1.2 | D8, D12 |
| T7 | run ends when the last entry is walked: `rd_running` ↓, `rd_done` ↑, engine run done `WAKE_EVENTS` [11] | `rd_running` ↓ and `rd_done` ↑ **once the run's last table read has completed** (guarantee B), **in the same cycle as DMA run done, `EVENTS` [10]** (4.2, 4.10) | changed | v1.2 | polling `rd_running` means what the event means; D41 (F6) |
| T8 | `STREAM_SRC`: source [20:0], spare [31:21] | source [20:0] aligned to the read size; **gap after [23:21]** (moved from `STREAM_COUNT`); spare [31:24] (4.2, 8.9) | changed | v1.2 | D4 |
| T9 | `STREAM_DEST`: destination word [20:0] (modes 3, 6, 9), *d* [27:21] | **destination byte address [31:0], mode 0 only**, aligned to the read size, advancing by 4, 8 or 16 bytes per element (4.2) | changed | v1.2 | D4 |
| T10 | `STREAM_COUNT`: count [17:0] (18 bits), mode [21:18], read size [23:22], gap after [26:24] | **count [16:0] (17 bits, ≤ 131,071)**, read size [18:17], **counter [21:19]**, mode [24:22], ***d* [31:25]** (the common header layout, relative *d*) (4.2, 8.1) | changed | v1.2 | D4, D12, D30 |
| T11 | *d*: 0–63 relative, 64 C&C (mode 3 only), 65–127 invalid; never broadcast | *d* 0–63 relative, **64–66 the controllers**, **126 the broadcast** (hardware: no special case; its own copy takes a token), 67–125 and 127 invalid. By convention only the controllers broadcast from streams; meshsim errors on a mesh tile (4.2) | changed | v1.2 | boot overlays at a packet per cycle; D25, D41 (F14) |
| T12 | element and destination addresses wrap mod 2²¹; destination word +1, +2 or +4 for modes 3, 6, 9 | plain addition, no wrap (past the end is a software bug); a misaligned element is not read, payload 0, `ERRORS` [1] (4.2) | changed | v1.2 | D26, D32 |
| T13 | packet fields: dst, mode, addr (0 or destination word) | dst (*d* = 64–66 or 126 absolute), **mode and counter** from the entry, address = destination for mode 0, else 0 (4.2) | changed | v1.2 | L1 |
| T14 | drift: condition 3 | `ERRORS` [3] (4.2) | renamed | v1.2 | D18 |

**Controller read port (4.3)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T15 | `SRAM_RD_DEST_ADDR` **write-only**: destination [13:0] (a packed controller-memory word, any CM, program memory included), size [15:14] | **R/W** (0x880): destination [16:0], **a byte offset in data memory only** (region 0x00); size [18:17]; **counter [21:19]**; [31:22] reserved; a load returns the next destination as advanced, with size and counter (4.3, 8.8) | changed | v1.2 | D13 |
| T16 | results as mode-6 packets to self, counted in `DMA_CM_REMAINING` | **mode-0 packets to self** at 0x00000000 + destination, counted on the entry's counter at K (4.3) | changed | v1.2 | D6, D13 |
| T17 | request-queue entry 37 bits (21 + 14 + 2) | **43 bits** (21-bit address, 17-bit destination, 2-bit size, 3-bit counter) (4.3, 10) **[not in CHANGES]** | changed | v1.2 | follows from T15 |
| T18 | misaligned read: dropped, read-port error [7] | **performed as a payload-0 packet**, `ERRORS` [1]; [7] is now queue overflow only. A misaligned *destination* makes a misaligned mode-0 packet, a bad packet at the director (4.3) | changed | v1.2 | D32 |

**GATHER engine (4.4)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T19 | gather request mode 1; gather values mode 2; queue entry = 20-bit ordinal | request **mode 4**, values **mode 1**; **the request's counter field is stored with the ordinal** (entry = 20-bit ordinal + 3-bit counter) and stamped on its gather values, which are counted at the director (guarantee C) (4.4) | changed | v1.2 | D6, D17 |
| T20 | `DMA_RECORD_BASE_ADDRESS` any chunk; reads wrap mod 2²¹ | **aligned to a record** (even); an odd base makes each read payload 0 and sets `ERRORS` [1] (4.4) | changed | v1.2 | D32 |
| T21 | GATHER busy `TILE_STATUS` [2]; overflow condition 6 | `STATUS` [2]; `ERRORS` [6] (4.4) | renamed | v1.2 | D18 |

**SRAM read queue, return path and injection (4.5)** — see also L15–L17.

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T22 | chunk selection from one or two rows (spanning) | from the one returned row (4.5) | changed | v1.2 | T1 |
| T23 | out-of-range read returns payload 0 | misaligned read returns payload 0; out of range undefined (4.5) | changed | v1.2 | T2, T3 |
| T24 | SRAM write queue: modes 2–5 | mode 0 to region 0x03 and modes 1–3 (4.5) | changed | v1.2 | L3 |

**DMA write engine (4.9)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T25 | modes 2 (`PTR[src]`), 4 (`PTR[digit]`), 5 (augmented) and 3 (explicit chunk address, never counted) | **mode 1** (`PTR[src]`), **2** (`PTR[digit]`), **3** (augmented) and **mode 0** at byte offset / 4 (counted by its field) (4.9) | changed | v1.2 | D6 |
| T26 | `PTR[src]` index 0–64 | 0–66 (controllers' ids) (4.9) | changed | v1.2 | D25 |
| T27 | `PTR[64]`: C&C's staging pointer, re-set after mode-4 phases | not special (4.9) | removed | v1.2 | staging by address; D17 |
| T28 | spanning writes split into two area accesses | a `PTR` must be aligned to the size of the writes through it (an augmented one to a record); **a write through a misaligned `PTR` is dropped but counted, sets `ERRORS` [1], and the pointer still advances by the size** (4.9) | changed | v1.2 | D32; the advance: item 133 (accepted 2026-10-29) **[advance not in CHANGES]** |
| T29 | explicit-address write misaligned: bad packet | alignment checked at the director (L7) (4.9) | changed | v1.2 | D32 |
| T30 | input FIFO overflow condition 4 | `ERRORS` [4] (4.9) | renamed | v1.2 | D18 |

**Table memories (4.10)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T31 | window 0x2000–0x3FFF (0x3600–0x3FFF unmapped) | **0xFFFF2000–0xFFFF35FF**, same low 16 bits *(Proposed, item 120)*; the rest of region 0xFF outside the block and windows is unmapped (4.10, 8.9) | changed | v1.2 | D11 |
| T32 | access when `rd_running` = 0 and **`ts_busy`** = 0; `wait_tables()` | **`ts_busy` and `wait_tables()` gone**; software waits by events (a counter's done + DMA run done), may poll `STATUS.rd_running`; guarantees **A** (K covers the `PTR`/`HIST` update), **B** (T7), **C** (T19) (4.10) | removed / changed | v1.2 | D11, D17 |
| T33 | table-window load 1 or 2 cycles, implementation's choice | **1 cycle** (`HIST`, the only loadable table) (4.11, 8.1) **[not in CHANGES-v1.2: D45; v1.2 errata E2, E4]** | changed | v1.2 | D45 (Tony, 2026-10-29) |
| T34 | table request overflow: condition 5 | `ERRORS` [5] (4.10) | renamed | v1.2 | D18 |

**Commit ordering and digits (4.14, 4.15)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T35 | committed when counters are done with `mem_idle`/`cm_idle`; `ts_busy` for the tables | committed **when the write's counter has decremented** (K): CM written, peripheral landed, big SRAM in its area queue, `PTR`/`HIST` readable (4.14) | changed | v1.2 | D3 |
| T36 | bucket write is mode 4 (`DMA_CONFIG` [20:18]) | mode 2; same field (4.15) | renamed | v1.2 | D6 |

**Request port (4.18)** — the tree is L12–L14.

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| T37 | offsets 0x858–0x864 | **0x848–0x854** (`REQUEST_SET_W0`–`W2`/`GET_W0`–`W2`, `SEND`/`RECEIVE`) (8.1, 8.10) | changed | v1.2 | D11 |
| T38 | receive side reads the tile's own 16-entry queue | reads its **target's queue in the tree** (C&C 1–3 only); on tiles 0–63 the `GET` words read 0, request pending never rises and every `REQUEST_RECEIVE` load is a receive underflow (4.18) | changed | v1.2 | D38 |
| T39 | `request_busy` `TILE_STATUS` [11]; FIFO-not-full `WAKE_EVENTS` [12]; `REQUEST_PENDING` [19]; underflow condition 14 | `STATUS` [6]; `EVENTS` [11]; request pending `EVENTS` [12] (live); `ERRORS` [14] (4.18) | renamed | v1.2 | D16, D18, D40 |
| T40 | HALT keeps the receive queue; requests arrive while credits last | the reset empties every FIFO, the tree and the queues together and restores the credits; after an error the port keeps running (the handler's reports use it) (4.18) | changed | v1.2 | D23 |

### 2.4 The controller and its registers (spec 4.11–4.13, 8.1–8.10, 8.15)

Register offsets, bit by bit, are in section 3. This section covers the behaviour.

**The core and its memories (4.11)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| C1 | RV32I + Zmmul + `WFI`; no CSRs, traps, interrupts, `mtvec`, `mepc`, `mret` | RV32I + Zmmul + `WFI` + **the CSR instructions, the machine-mode trap CSRs, `mcycle` and `mtime`**; machine mode only; the same core in C&C 1–3. `mtime`'s rate is implementation-defined (4.11) *(item 121: the full CSR list and reset values, with Tony)* | changed | v1.2 | D1, D20, D41 (F12), D48 (W4-07) |
| C2 | — | **two core inputs** (4.11): the **hard interrupt** (a level, from `ERRORS` ≠ 0; traps to `mtvec` even in `WFI`; gated by `mstatus.MIE` and `mie`) and the **wakeup** (a level from the wake term; resumes `WFI` without a trap; if high at `WFI`, the core doesn't sleep) | new | v1.2 | T, 2026-10-28; D1 |
| C3 | illegal instruction (divide, CSR) → condition + **halt**; `ecall`, `ebreak` → halt | divide and CSRs the core lacks → `ERRORS` [10] + trap; **bus error, illegal, `ecall`, `ebreak`, a fetch outside program memory and the boot ROM, a misaligned jump target** all trap to `mtvec`; **every trap is terminal**; `ecall`, `ebreak`, the fetch and the jump set no `ERRORS` bit (4.11, 4.13) | changed | v1.2 | D20 |
| C4 | 8 KB instruction (0x0000–0x1FFF, fetch only) + 8 KB data (0x4000–0x5FFF); each four word-interleaved 512 × 32 banks | **32 KB data (0x00000000–0x00007FFF) + 32 KB program (0x01000000–0x01007FFF)**; program memory is **fetch and load**, a store is a bus error; banking is the implementer's choice (4.11) | changed | v1.2 | D5, D42 |
| C5 | port B writes 1, 2 or 4 consecutive words per cycle at any word address | **one 128-bit (or 32/64-bit) write per cycle, naturally aligned** (4.11) | changed | v1.2 | D42 (W5-05) |
| C6 | memory loads 1 cycle; register and table-window loads **1 or 2 cycles, the implementation's choice** (meshsim 2) | memories and boot ROM **1**; the table window **1**; **the register block and every peripheral window word 2**, fixed on every access and unit; the 32 KB memories meet one-cycle access at 1 GHz (4.11, 5, 8.1, 10) **[not in CHANGES-v1.2: D45; v1.2 errata E2–E5]** | changed | v1.2 | D45 (Tony, 2026-10-29); item 134 closed |
| C7 | sampling rule: `WAKE_EVENTS`, `REQUEST_RECEIVE`, `UART_RX` side effects at *t* + *L* − 1 | the same rule for every word of every window; adds a peripheral's `EVENTS` (subtracts one count per masked set bit) (4.11) | changed | v1.2 | D14 |
| C8 | stalling stores: the send port, the request FIFO, `UART_TX` on a wired tile | the send port, the request FIFO, the UART's TX word (C&C 1 only) (4.11) | changed | v1.2 | D28 |
| C9 | bus errors: stores to instruction memory, misaligned, unmapped (0x8A0–0xFFC), sub-word to registers or tables, store to read-only, loads from `PTR`/`STREAM`/`GROUP`; **halt** | `ERRORS` [9] + trap: store to program memory or boot ROM; misaligned; unmapped (regions 0x03–0xFE, the unused parts of 0xFF, **any word of an absent peripheral's window**); sub-word to registers, windows or tables; store to read-only; loads from `PTR`/`STREAM`/`GROUP`. **Reserved words in the block and windows read 0 and ignore stores** (not bus errors). Out-of-bounds offsets in CM are not checked (they alias) (4.11, 8.1) | changed | v1.2 | D5, D9, D27 |
| C10 | packed 14-bit CM word addresses | byte addresses; port B decodes the region (4.11) | changed | v1.2 | D5 |
| C11 | send port: `ROUTER_SET_HDR` (W): dst [6:0], mode [10:7], addr [31:11]; addr +1/+2/+4 for modes 3, 6, 9, +1 for mode 7 | **`ROUTER_HDR`** (R/W): **no-increment [0]**, spare [16:1], size [18:17] reserved (reads 0), **counter [21:19]**, mode [24:22], dst [31:25] (0–66, 126); **`ROUTER_ADDR`** (R/W, 32-bit), advancing by 4/8/16 bytes per mode-0 send unless no-increment; modes 1–5 leave it; **no-increment stays set until cleared** (4.11, 8.4) | changed | v1.2 | D7, D12, D37.5 |
| C12 | resident loader at 0x1F00, where HALT → RUN starts | gone: the **boot ROM** (6.2) | removed | v1.2 | D22 |

**The command and reply blocks, counters and events (4.12)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| C13 | command block 0x5FC0–0x5FFF; mode-7 32-bit writes only | **0x00007FC0–0x00007FFF**, a silicon constant (the boot ROM reads it); commands are **mode-0 writes, counter 0** (4.12) | changed | v1.2 | D6, D37 |
| C14 | command landed posts `WAKE_EVENTS` [10] | posts `EVENTS` [9]; **the event stays pending after a unit's first command** (the reset mask doesn't include it, the helpers don't consume it; the boot ROM relies on it) (4.12) | changed | v1.2 | D37.1, D41 (F13) |
| C15 | reply block 0x5BC0–0x5FBF; mode 8 lands by source at word 2·src; `DMA_REPLY_REMAINING` | **0x00007BC0–0x00007FBF**; a reply is a **mode-0 write** to `REPLY` + 8·src, counted on any counter; **a default, not a rule**: every request names its own reply address; writing only one's own pair is a software convention (4.12) | changed | v1.2 | D6, D40 |
| C16 | four down-counters by mode (0x830–0x83C), done = 0 **and** `mem_idle`/`cm_idle` | **seven write counters** `WRITE_COUNTER_1`–`7` (0x860–0x878), 20 bits, decremented at K; done = 0 (4.12, 8.7) | changed | v1.2 | D14, D30, D40 |
| C17 | additive write: `next = count + N − arrive`, judged against arrivals at acceptance; N > 0 clears counters done [9] and its own done [0]–[3] | the same rule against **that cycle's decrement**; N > 0 clears counters done [7] and its own done (bit *n* − 1) and sets their qualifiers (4.12) | changed | v1.2 | D30 |
| C18 | a counter returns to 0 by counting down, at chip reset or **on entering HALT** | counting down or **the reset**, which also clears the delay line (4.12) | changed | v1.2 | D23 |
| C19 | unexpected arrival at acceptance | **at the decrement**, K later; a stray accepted at 0 whose decrement lands after an arm uses up one of the *N* (accepted trade-off; meshsim also reports the stray at acceptance) (4.12) | changed | v1.2 | D3, W5-01 |
| C20 | `WAKE_EVENTS`: [3:0] counter done (SRAM, CM, REPLY, PERIPH), [7:4] reserved, [8] GO, [9] counters done, [10] command landed, [11] engine run done, [12] request FIFO not full, [18:13] service lines, [19] request pending, [20] sender idle, [21] UART RX not empty, [22] UART TX not full | `EVENTS`: **[6:0] write counter 1–7 done**, [7] counters done, [8] GO, [9] command landed, [10] **DMA run done**, [11] request FIFO not full, [12] request pending (live), **[15:13] peripherals 1–3 (live)**, [31:16] reserved (4.12, 8.10) | changed | v1.2 | D16, D40 |
| C21 | `WAKE_MASK` resets to [8] \| [9] (and at HALT → RUN) | `EVENTS_MASK` resets to **[8] \| [7]** (4.12) | changed | v1.2 | D40 |
| C22 | `WAKE_REQUIRED` | `EVENTS_REQUIRED`, **the controller's only** (no copy in a window) (4.12) | renamed | v1.2 | D14 |
| C23 | the `done` pulse clears counters done and the four done events | clears counters done and the seven done events, **and no other event** (DMA run done is cleared only by `PULSE_DMA` and a consuming load) (4.12) | changed | v1.2 | round 2 (recipe check) |
| C24 | the `done` gate: only GO wakes `WFI` | the same; **an error during the gate still takes the hard interrupt** (4.12) | changed | v1.2 | D21 |
| C25 | — | **spurious wakes allowed**: a peripheral's live bit may lag a consuming load; software re-checks after a wake; the hardware aims to make the bit current by the next instruction (4.12) | new | v1.2 | D16 |
| C26 | a live service line's consumption claims one unit of service (credit, nothing, or a pop) | live events are never consumed by a load of `EVENTS`; a peripheral's events are consumed only by a load of **its own** `EVENTS` (4.12, 4.17.3) | changed | v1.2 | D14, D16 |

**Status, errors and traps (4.13)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| C27 | `TILE_STATUS`: live bits, peripheral type, endpoint id, SRAM size | split: **`PARAMS`** (constants: peripherals present [2:0], endpoint id [26:20] (0–66), SRAM size [31:27], 7 on C&C 1–3) and **`STATUS`** (live, diagnostic only: [0] `rd_running`, [1] `rd_done`, [2] GATHER busy, [3] `send_busy`, [4] `done`, [5] command landed, [6] `request_busy`) (4.13, 8.2) | changed | v1.2 | D18 |
| C28 | `done` resets to 1 (forced to 1 in HALT) | **`done` resets to 0** (8.2) **[not in CHANGES]** | changed | v1.2 | no HALT; D23 |
| C29 | `TILE_CONDITIONS` [14:0] | **`ERRORS`**, same numbers except: [1] "bad packet **or misaligned access**" (was bad mode or size); [7] read-port **overflow only** (misaligned now [1]); **[11]–[13] peripherals 1–3, live** (the OR of each window's `ERRORS`; were bad peripheral write and the peripheral's conditions 2 and 3) (4.13, 8.3) | changed | v1.2 | D18, D21 |
| C30 | conditions cleared on entering HALT | **sticky until the reset** ([10:0], [14]); a store to `ERRORS` is a bus error (4.13) | changed | v1.2 | D21, D23 |
| C31 | a condition raises the tile's error line | `ERRORS` ≠ 0 raises the **hard interrupt** (once enabled) and the **error line** (hardware, `error_latency`, no software) (4.13) | changed | v1.2 | D21, D24 |
| C32 | — | **terminal traps**: `MIE` cleared on entry and never set again; a trap in the handler restarts it; engines and peripherals carry on unserviced until the reset (4.13) | new | v1.2 | D20 |
| C33 | — | **the handler's convention** (software, but sized by hardware): 24 reports to C&C 1 in fixed order; reads `EVENTS_PEEK`, never `EVENTS`; checks `PARAMS` [2:0] before a window; parks in a jump to itself; the boot ROM's handler until the image installs its own; **interrupts off until the image's boot part 2 enables them** (4.13) | new | v1.2 | D20, D37.2, D37.3, D44 (W5-24) |

**The register block (8.1, 8.15)** — offsets in section 3.1.

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| C34 | 0x800–0x89C contiguous; 0x8A0– unmapped (bus error) | 0x800–0x880 defined; **0x884–0x9F4 reserved (read 0, stores ignored)**; peripheral windows at 0xA00, 0xC00, 0xE00; **every offset moved** (8.1) | changed | v1.2 | D11, D30, D40 |
| C35 | `TILE_PULSE` bits: [0] done, [1] start, [2] peripheral run, [3] GO; one store could fire several | **one register per action**: `PULSE_DONE`, `PULSE_DMA` (block), `PULSE_RUN` (window), `PULSE_GO` (C&C 1's peripheral); a store of 1 fires, 0 does nothing, a load reads 0 (8.1, 8.6) | changed | v1.2 | D8 |
| C36 | `DMA_CONFIG`, `DMA_RECORD_BASE_ADDRESS`, `SRAM_RD_DEST_ADDR` write-only | **R/W** (8.5, 8.7, 8.8) | changed | v1.2 | D11, D13 |
| C37 | configuration registers undefined at chip reset, kept across HALT → RUN | undefined at the reset; adds `ROUTER_ADDR` and each window's `DMA_HDR`, `DMA_ADDR`; drops `SENDER_ADDR` (8.1) | changed | v1.2 | D23 |
| C38 | `mesh.h` 1.1, flat registers; trace mark at 0xFFFFFFF0; `CMD_EXIT` and `ebreak` end a test | `mesh.h` 1.2, structs (`TileRegs`, `PeripheralStd`, `<Name>Regs`); **simulator-only exit word 0xFFFFF9F8 and trace word 0xFFFFF9FC**, reserved words on hardware (8.1, 8.15) | new | v1.2 | D29, D32, D36 |

### 2.5 Peripherals (spec 4.17, 4.20)

v1.1's peripheral slot is replaced by **up to three peripherals per unit, all connected the same way**. On a mesh tile there is one, the replayer (2.6).

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| P1 | one slot per tile; type `TILE_STATUS` [15:12], 4 bits | **up to three per unit**, fixed at build: peripheral *n* has region 0x03 + *n* and its window at 0xFFFFF800 + 0x200·*n*; **presence** in the controller's `PARAMS` [2:0]; **type** in the peripheral's own `PARAMS` [7:0], version [15:8] (4.17.1) | changed | v1.2 | D9, D10 |
| P2 | types: 0 none, **1 the replayer**, 2–15 reserved | **0 none, 1 Ingress/Egress (C&C 1's), 2 Updater/Binder/RW engine (C&C 2's), 3 the Tracer (C&C 3's), 4 the replayer**; other values reserved (4.17.1, 4.17.4). **The replayer's `PARAMS` value changes from 1 to 4.** **[not in CHANGES-v1.2: D46; v1.2 errata E1, E4]** | changed | v1.2 | D46 (O, 2026-10-31; W4-05) |
| P3 | an absent peripheral (type 0): `PERIPH_RUN` ignored, status reads 0, every mode-9 write a bad peripheral write (counted) | **neither region nor window**: any word of its window is a **bus error**; a packet to its region is a **bad packet** (uncounted, L8); its bits in the controller's `EVENTS` and `ERRORS` read 0 (4.17.1) | changed | v1.2 | D9 |
| P4 | `PERIPH_RUN` = `TILE_PULSE` [2]; `periph_running` `TILE_STATUS` [3] | **`PULSE_RUN`** (window word 3); **`STATUS` [0] run in progress** (the ownership state); the end posts **run done, `EVENTS` [0]** (depth 1) (4.17.2) | changed | v1.2 | D10 |
| P5 | six service lines in `WAKE_EVENTS` [18:13]: sticky events, live queue conditions or item FIFOs; consuming a line claims a unit of service | the peripheral's own **`EVENTS` (word 5), `EVENTS_PEEK` (6), `EVENTS_MASK` (7, 0 at reset)**; **every bit backed by a small counter** with a stated **depth** (1 ordinary, 2 for double-buffered credits), or **live**; a consuming load subtracts one count per masked set bit; standard bits [0] run done, [1] DMA done; the controller's `EVENTS` [12 + *n*] is live while `EVENTS & EVENTS_MASK` ≠ 0; no item-FIFO kind *(Deferred, item 119)* (4.17.3) | changed | v1.2 | D14, D16 |
| P6 | `PERIPH_STATUS_0`–`7` (0x870–0x88C), read-only, peripheral-defined | **the standard window** (0x200 bytes): words 0 `PARAMS`, 1 `STATUS` ([0] run, [1] DMA), 2 `ERRORS`, 3 `PULSE_RUN`, 4 `PULSE_DMA`, 5 `EVENTS`, 6 `EVENTS_PEEK`, 7 `EVENTS_MASK`, 8 `DMA_HDR`, 9 `DMA_ADDR`; 10–31 reserved; 32–127 peripheral-defined (4.17.4, 8.11) | changed | v1.2 | D10 |
| P7 | — | access rules: reserved/undefined words read 0 and ignore stores; a store to a read-only word is a bus error; every word 32-bit, one at a time, **2-cycle loads** (4.17.4, 4.11). **A peripheral without a DMA reads 0 at words 4, 8 and 9**; a load there is not flagged, only a store (D48, W4-06) **[D48 not in CHANGES; v1.2 erratum E4]** | new | v1.2 | D10, D45, D48 |
| P8 | mode 9 at a 21-bit word address; counted in `DMA_PERIPH_REMAINING` on landing or drop; nothing counted in HALT | **mode-0 writes to the peripheral's region** at a byte offset, naturally aligned; counted by the packet's counter field at K; **a write the peripheral drops is still counted** and sets its own error; every peripheral lands writes within `rp_latency` = 2 *(Proposed, item 48)* and at most K (4.17.5) | changed | v1.2 | D6, D10 |
| P9 | conditions [13:11]: bad peripheral write (generic), peripheral conditions 2 and 3 | **the peripheral's own `ERRORS`** (word 2), sticky until the reset (C&C 1's unit-error bit excepted: live); the controller's `ERRORS` [11]–[13] are live ORs; errors interrupt, events wake (4.17.6) | changed | v1.2 | D10, D21 |
| P10 | HALT holds the peripheral and the sender in reset; a halted controller stops them | **resets with its unit at the reset, and is stopped by nothing else**: a controller's trap leaves it running, unserviced (4.17.8) | changed | v1.2 | D20, D23 |
| P11 | the sender (`SENDER_SEND` 0x868: dest [6:0], mode [10:7], queue [12:11], **count [28:13], 16 bits**; `SENDER_ADDR` 0x86C, 21-bit word, +1/+2/+4); `sender_busy` `TILE_STATUS` [4]; sender idle `WAKE_EVENTS` [20]; output through "the sender's output port" | **the peripheral's DMA** (4.17.9): **`DMA_HDR`** (R/W): **count [11:0] (12 bits, ≤ 4,095 words)**, output queue [13:12], spare [16:14], size [18:17] reserved, counter [21:19], mode [24:22], dst [31:25]; **`DMA_ADDR`** (R/W, 32-bit, advancing by 16/8/4 bytes); **`PULSE_DMA`** starts it; `STATUS` [1] DMA in progress; **DMA done `EVENTS` [1]** (depth 1). Packet split as before (⌊*n*/4⌋ × 128, then 64, 32 or 64 + 32). `PULSE_DMA` while busy is ignored; count 0 still posts DMA done; mid-transfer header changes undefined | changed | v1.2 | D10 |
| P12 | the sender's output port (unattached on tiles) | **a router port of its own**, paired with a link the unit doesn't otherwise use; its packets to its own unit reach the ejector through the router and take no read token. Only C&C 1–3's engines send *(Deferred, item 118: the port's src, routing inside the controllers' routers, flow control)* (4.17.9) | changed | v1.2 | D10 |
| P13 | UART on every tile at 0x890–0x89C; unwired tiles' registers are no-ops; events [21], [22]; line errors sticky in `UART_STATUS` (W1C); keeps running in HALT | **only in C&C 1's peripheral**, words 42 `UART_TX`, 43 `UART_RX`, **44 `UART_DIVISOR`, 45 `UART_STATUS`** (order changed); line errors are status (W1C), **never `ERRORS`** (so line noise can't raise the toast pin); RX not empty and TX not full are **live** bits in the peripheral's `EVENTS` ([4], [5]); reset empties the FIFOs and loads the reset divisor (4.20, 8.13, 8.14) | changed | v1.2 | D9, D21, D28 |
| P14 | meshsim prints every tile's `UART_TX` | debug output from other units goes to log files through the file system *(Deferred, item 119)* (4.20, 6.7) | changed | v1.2 | D23 (Q12) |

### 2.6 The replayer (spec 4.19, `spec/replayer-opcodes.md`)

**How this section splits.** v1.1 → v1.2 changed **only the replayer's interface**: its window, events, credits, region map, errors and type. 4.19.3–4.19.7, 4.19.12 and 4.19.14 are identical to v1.1's apart from names, and so is the opcode appendix. v1.2 → v1.25 changed **only its rows and tables**, to match jolt-cpp `821e1bd`. So the interface entries (R1–R14) are step **v1.2**, the row and table entries (R20–R41) are step **v1.25**, and a few (R15–R19) are **both**.

**Interface (step v1.2: a reader from v1.2 skips R1–R14)**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| R1 | peripheral type 1 in the slot | **peripheral 1, type 4** (`PARAMS` [7:0] = 4) on tiles 0–63 (4.19, 8.11) | changed | v1.2 | D46 (P2) |
| R2 | run = `TILE_PULSE` [2] `PERIPH_RUN`; running = `TILE_STATUS` [3] | `PULSE_RUN` (window word 3); `STATUS` [0]; `STATUS` [1] always 0 (no DMA) (4.19.11) | changed | v1.2 | D10 |
| R3 | written by **mode 9** at a 21-bit **word** address; addr[20:18] selects a window | written by **mode-0 packets to region 0x04** at a **byte offset**; **offset[22:20] selects a part** (4.19.10). Map, in bytes (v1.1 word address in brackets): state word 0x000000 [0x00000]; stream word 0x000010 [0x00004]; register image 0x000020–0x00041F, image word *i* at 0x20 + 16*i* [0x00008 + 4*i*]; decode table 0x001000–0x001FFF, entry *e* at 0x1000 + 16·(*e* >> 3) [0x400 + 4·(*e* >> 3)]; ROM 0x002000–0x003FFF, word *j* at 0x2000 + 16*j* [0x800 + 4*j*]; code queue 0x100000–0x1FFFFF [0x40000–0x7FFFF]; read-log 0x200000–0x27FFFF [0x80000–0x9FFFF]; write-log 0x280000–0x2FFFFF [0xA0000–0xBFFFF]; expansion 0x300000–0x3FFFFF [0xC0000–0xFFFFF] *(Proposed, items 44–46)* | changed | v1.2 | D6, D9, D10 |
| R4 | a bad peripheral write: unmapped word, wrong size, a 128-bit write in window 0 off a multiple of 4, a single-ported memory written while running, a queue port written **in HALT** | **a bad write**, the replayer's `ERRORS` [0], **still counted**: unmapped offset, wrong size, a single-ported memory written while running *(Proposed, item 47)*. A misaligned write is now a bad packet at the director (L7); the HALT case is gone (4.19.10) | changed | v1.2 | D10, D32 |
| R5 | ownership: stopped / running / HALT (item 54) | stopped / running by `STATUS` [0]; boot loads find the replayer stopped (4.19.10) | changed | v1.2 | W1-30, D23 |
| R6 | counted in `DMA_PERIPH_REMAINING` on landing or drop; boot loads uncounted because in HALT | counted by the packet's counter field at K, landed or dropped; boot loads are uncounted mode-0 writes (4.19.10) | changed | v1.2 | D6 |
| R7 | **low-water lines** (live): capacity − occupancy − pending ≥ half (and, for the expansion queue, words still undelivered); a claim (consuming `WAKE_EVENTS`) adds half to pending; a new expansion credits 128 words | **counted credits, depth 2, each half a queue** (4.19.8): `PULSE_RUN` sets every queue's credits to 2; **each new expansion sets the expansion queue's to 2**; the replayer **returns one credit per half queue popped**; a consuming load of the replayer's `EVENTS` takes one from each masked set queue bit; a credit permits up to half a queue, possibly split. No pending/occupancy test, no gating (the loop masks out streams with nothing left) | changed | v1.2 | D10, W3-24 |
| R8 | events: `WAKE_EVENTS` [13] code, [14] read-log, [15] write-log, [16] expansion low-water (live); [17] new expansion, [18] done (sticky) | the replayer's own `EVENTS`: **[0] run done (d1), [1] DMA done (never), [2] code credit, [3] read-log credit, [4] write-log credit, [5] expansion credit (d2 each), [6] new expansion (d1)**; the tile sees them as `EVENTS` [13] *(Proposed, item 123: the bit positions)* (4.19.9, 8.11) | changed | v1.2 | D10, D14 |
| R9 | `PERIPH_STATUS_0`: rows run [15:0], next offset [31:16]; `PERIPH_STATUS_1`: the awaiting instruction | **word 32 `ROWS_RUN`** [15:0], **33 `EXPANSION_OFFSET`** [15:0], **34 `EXPANSION_OPCODE`** [31:0]; one field per word; a store is a bus error (4.19.9, 8.11) | changed | v1.2 | D39 |
| R10 | queue overrun: tile condition 12 | the replayer's `ERRORS` [1]; the push still counted (4.19.8) | changed | v1.2 | D10 |
| R11 | invalid expansion: tile condition 13 | the replayer's `ERRORS` [2]; any of [0]–[2] sets the tile's `ERRORS` [11] and traps the controller *(Proposed, item 123)* (4.19.11) | changed | v1.2 | D10, D21 |
| R12 | the queues clear at a run, in HALT and when the controller halts | at a run (credits set to 2) and at the reset; a controller trap leaves the replayer running, unserviced, until the reset (4.19.8, 4.19.11) | changed | v1.2 | D20, D23 |
| R13 | a halt (C&C's HALT or the controller halting) stops the replayer at once | removed (4.19.11) | removed | v1.2 | D23 |
| R14 | stream-length bound ≈ 8.6 Mbit (two log words a row) | ≈ 4.3 Mbit (one log value a row); the field stays 24 bits; a pop past the end is `ERRORS` [2] (4.19.15) | changed | v1.2 | integrator, 2026-10-29 (correction) |

**Changed in both steps**

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| R15 | the controller loop: wake on service lines; refill on `DMA_PERIPH_REMAINING` done ([3]); decode `PERIPH_STATUS_1` (inlines and system instructions) | wake on the tile's `EVENTS` [13]; take credits from the replayer's `EVENTS`; refill on **its refill counter's done and DMA run done together** (`EVENTS_REQUIRED`); for a new expansion, **look up key(`EXPANSION_OPCODE`)**, the inlines and CSRRW/CSRRS only (4.19.9). *v1.2:* the same loop, but decoding `EXPANSION_OPCODE` by exact word, for inlines and the system instructions (ECALL, EBREAK, MRET, CSRRW, CSRRS) | changed | both | v1.2: D10, W3-24, W3-27; v1.25: V5, V6 |
| R16 | expansion queue: the inlines and system instructions | **the inlines and the CSR instructions** (4.19.1). *v1.2:* inlines and system instructions | changed | both | V6 |
| R17 | includes-store-log [115] = 0: SD rows pop nothing; the write-log low-water line stays quiet | **all store rows** (SD, `VirtualStoreB/H/W`) pop nothing and report pre-value 0; the loop leaves the write-log credit out of the mask (4.19.2). *v1.2:* SD rows only; the credit masked | changed | both | v1.2: D10; v1.25: V1 |
| R18 | the NOP word for a controller-fed rd = x0 remap: the controller knows the word from `PERIPH_STATUS_1` | from `EXPANSION_OPCODE`; **`VirtualHostIO` is no longer remapped** (no operands, `rd0` = 0) (4.19.4). *v1.2:* `EXPANSION_OPCODE`; `VirtualHostIO` remapped to v40 | changed | both | v1.2: D39; v1.25: V1 |
| R19 | write-log E[15] updated on every traced LD | updated on **every load row** (LD, `VirtualLoadB/H/W`) (4.19.15). *v1.2:* on LD | changed | v1.25 | V7 (Q2) |

R19's behaviour didn't change in v1.2 (only its wording), so it is step **v1.25**. It sits here beside the other log rules.

**Rows and tables (step v1.25: both readers apply R20–R41)**

| ID | v1.1 and v1.2 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| R20 | reference: jolt-asic-planning PR #1, `4f36b3d` | **jolt-cpp `821e1bd`** (PR #2, `tracer-standalone-main`); workloads EVM Zone `evm-zone-4e3451c1d8c5` and Cyclone, both 2²⁹ rows (4.19.1) | changed | v1.25 | V1, V2 |
| R21 | expansion word [63] spare | **[63] `offset_imm`**: when set, a `VirtualShiftData…` row's `shift_offset` is the source instruction's immediate (code 132's value), else 0; set on no other row; the replayer ignores it elsewhere (4.19.3) | new | v1.25 | V4, V7 (Q3) |
| R22 | [62:56] `advice_reg` (Tracer only) | **[62] `sign_imm`**: the row's immediate is signed (Jolt's 128-bit `imm()` is the 64-bit immediate sign- or zero-extended); the replayer computes nothing from it and reports it in the row. **[61:56] `advice_reg`**, now register − 146 (bank-1 registers 146–209) (4.19.3) | changed | v1.25 | V4, V5 (Q5) |
| R23 | `opcode` [7:0] 0–167 | **0–170** (171 opcodes) (4.19.3) | changed | v1.25 | V1 |
| R24 | the row's immediate = the sum of its immediate slots | the same, **except a `VirtualShiftData…` row, whose immediate is its `shift_offset`** (4.19.3) | changed | v1.25 | V1, V4 |
| R25 | operand codes 146–255 reserved (invalid expansion) | still an invalid expansion in a replayer (the Tracer uses them for bank-1 registers) (4.19.3) | — | — | wording only |
| R26 | **row opcode codes** (v1.1 = v1.2) | **renumbered** (4.19.7; appendix section 2.0): 0–109 unchanged; v1.2's 110–111 → +6; **112–113 removed**; 114–153 → +4; **154 removed**; 155–167 → +3; **110–115 new**. **61 row opcodes change** (52 renumbered, 3 removed, 6 new). The ALU decodes these numbers: **every hard-wired opcode compare above 109 must be redone**. Full list in section 3.9 | changed | v1.25 | V1 (fixed in spec `0b4ba20`) |
| R27 | — | **`VirtualLoadB/H/W` (110–112)**: class L load rows, rd = the read-log word (the whole doubleword). **`VirtualStoreB/H/W` (113–115)**: class L store rows, the write-log word is the pre-value, rd is an immediate slot holding the access's immediate. Address = rs1 + imm with its low bits cleared for the width: *a* − (*a* & (8 − *width*)) (4.19.7, 4.19.12) | new | v1.25 | V1 |
| R28 | `VirtualAlignAddr` (154), `VirtualAssertHalfwordAlignment` (112), `VirtualAssertWordAlignment` (113) | removed: jolt-cpp aborts on a misaligned access, so no trace has a misaligned memory row; the replayer doesn't check (meshsim flags one) (4.19.7) | removed | v1.25 | V1, V7 (Q4) |
| R29 | `VirtualShiftDataB/H/W rd, rs1, rs2`: rs2 the computed address | **rs2 is the access's base register**; the lane comes from rs2 + `shift_offset` (4.19.7) | changed | v1.25 | V1 |
| R30 | `VirtualHostIO` writes rd's own value back | **no operands: reads and writes nothing** (4.19.7) | changed | v1.25 | V1 |
| R31 | `VirtualPext`/`VirtualPextSigned` take only contiguous non-zero masks; other masks undefined | **correct for any mask**; **a zero mask gives 0** **[zero-mask clause not in CHANGES]**; full speed promised for contiguous masks only, others may take extra implementation-defined cycles; the method is the implementation's *(item 140, with Tony)* (4.19.7, 4.19.14) | changed | v1.25 | V1 (from v1.3 section 9), O |
| R32 | the replayer executes 85 opcodes (S, M, LD, SD, `VirtualAdvice`) | **88**: S, M and nine class L (the eight memory opcodes and `VirtualAdvice`); any other opcode is an invalid expansion (4.19.7) | changed | v1.25 | V1 |
| R33 | memory fields formed for LD and SD only | for the **eight memory opcodes**; a load's value from rd after the row (0 for LD to x0); the narrow rows' address rounded to the doubleword (4.19.7, 4.19.12) | changed | v1.25 | V1 |
| R34 | log select: SD → write log, other log rows → read log | **store rows** (SD, `VirtualStoreB/H/W`) → write log; others → read log (4.19.2). `from_log` still decides *whether* a row takes a word | changed | v1.25 | V1 |
| R35 | `next_from_value` set in the controller's words for MRET's, ECALL's and trap returns' JALR rows and EBREAK's JAL | all in the ROM at `821e1bd` (4.19.7) | changed | v1.25 | V6 |
| R36 | output row word 0 [127:86] spare | **[86] `imm_signed`** = the word's `sign_imm`; spare [127:87] (4.19.12) | new | v1.25 | V7 (Q1) |
| R37 | word 0 `opcode` `VirtualAdvice` = 122 | **126** (4.19.12) | changed | v1.25 | V1 (R26) |
| R38 | word 1 `imm` = the sum of the immediate slots | the row's immediate (R24), a `ShiftData…` row's `shift_offset` sign-extended; 0 for an advice row; consumers extend it to 128 bits by `imm_signed` (4.19.12) | changed | v1.25 | V1, V7 |
| R39 | decode class 2 = controller (system: ECALL, EBREAK, MRET, CSRRW, CSRRS); class 3 reserved; **unused entries are class 1/2 (controller)**; SYSTEM funct3 = 0 others → sel 11 (controller) | class 2 = **CSRRW, CSRRS only**; **class 3 = invalid**, and **every unused entry is class 3** (an invalid expansion, `ERRORS` [2]; no new error bit); SYSTEM sel 11 is invalid (4.19.5, 4.19.11) | changed | v1.25 | V5, O |
| R40 | per-program tables; ROM ≈ 508 of 512, atomics partly controller-fed | **universal tables**, one set per jolt-cpp version (`tables/`): 306 ROM, 17 controller and 1,725 invalid decode entries; **ROM 388 of 512 words**, with every atomic, ECALL, EBREAK and MRET in it (4.19.5, 4.19.6). Hardware sizes are unchanged | changed | v1.25 | V5, V6 |
| R41 | controller-fed templates keyed by exact instruction word, ≤ 128 per program | keyed by a **normalized key** (inlines: register fields cleared; CSRRW/CSRRS: rd/rs1 renamed by first appearance), the index sorted by key, a number its position; 75 numbers at `821e1bd`; a key with no entry is an error. **Software only** (`replay_ctl.c`), no hardware change (4.19.9) | changed | v1.25 | V5 |

Unchanged in all three versions: the queues' sizes; the ROM (512 × 128) and decode table (2K × 16); the register file (128 × 64, true dual port); the output queue (32 × 640 bits); row timing (3 or 5 cycles); the 64-cycle start copy; the decompressors; the state and stream words' layouts; the run log's format.

### 2.7 Control: reset, errors, boot, `done`/READY/GO (spec 5, 6.1–6.5)

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| K1 | **RUN/HALT**: one net from C&C, `run_latency`; HALT lands packets uncounted, holds peripherals and the sender in reset, clears counters and conditions; HALT → RUN starts at the resident loader | **no HALT state.** The reset is the only stop, and coming out of reset means running in the boot ROM (6.1) | removed | v1.2 | D23 (O, T) |
| K2 | a condition raises the tile's error line into one sticky bit at C&C | **an error end to end** (6.1): the unit's `ERRORS` bit → hard interrupt → handler's 24 reports to C&C 1 → park; its **error line** sets its bit in C&C 1's `ERROR_LINES_0`–`2` `error_latency` later (live); the **unit-error bit** (C&C 1's peripheral `ERRORS` [0], OR of the lines, live) interrupts C&C 1; every error is terminal for the chip | changed | v1.2 | D21, D24, D34 |
| K3 | — | **the toast pin**, raised by Ingress/Egress as soon as C&C 1's `ERRORS` ≠ 0 or the watchdog reaches 0, **latched until the reset**; `LED` stays a software pin (6.1, 8.14) | new | v1.2 | D24, D35 |
| K4 | — | **the keep-alive watchdog**, `WATCHDOG` in C&C 1's peripheral: 0 (reset) off; writing *N* > 0 starts it; a write or `PULSE_GO` restarts it from the last *N*; reaching 0 posts the watchdog event (depth 1) and latches the toast pin; **counts core cycles**; the only watchdog (6.1) | new | v1.2 | D34, D36 |
| K5 | chip reset (power-on) | **two inputs, the chip never resets itself**: **hard** (everything, the AXI side included; LPDDR contents lost; power-on) and **soft** (everything except the LVDS bus, the AXI bus and beyond, the LPDDR controller and PHY, so LPDDR keeps the flushed logs) *(item 124: the exact keep list, with Tony)* (6.1) | new | v1.2 | D35 |
| K6 | reset values: the down-counters, conditions, qualifier, gate, sticky wake events, `WAKE_REQUIRED`, read-port queue and request FIFO 0; `WAKE_MASK` [8] \| [9]; `done` 1 | **what the reset does** (6.1): every unit as at power-on except the memories. The core starts at **0x02000000** with `MIE` = 0. **Empty, credits returned:** router FIFOs and stages, return queues, area queues, write-engine FIFO and pointer stage, **the K delay line**, the send holding register, the read-port queue, the GATHER queue, request FIFOs, **the request tree and receive queues**; all read tokens free. **Zero:** every `ERRORS` (block and windows), write counters, done events and qualifiers, `done`, the gate, command landed, pending GO, sticky events, `EVENTS_REQUIRED`, `rd_running`, `rd_done`, round-robin pointers; `EVENTS_MASK` = [8] \| [7]; peripherals idle, their events 0 and `EVENTS_MASK` 0; in C&C 1's peripheral the READY bits, `READY_EXPECT`, `WATCHDOG`, `LED` and the toast latch. **Undefined:** configuration registers. **Kept:** controller memories, tables, big SRAM, peripherals' memories. **No reset hold** for the error lines (6.1) | changed | v1.2 | D21, D35, D36 |
| K7 | the resident loader at 0x1F00, loaded by C&C while halted | **the boot ROM** in every controller, region 0x02, reset vector 0x02000000, **exactly** (6.2): (1) set `mtvec` to the ROM's report-and-park handler; interrupts stay off; (2) branch on `PARAMS`' endpoint id; (3) **tiles:** `PULSE_DONE`; loop `WFI` + load `EVENTS` until GO is seen; **if that load's value has command landed (`EVENTS` [9]) go to 5, else repeat 3**; (4) **C&C 1–3:** `EVENTS_MASK` = [9]; loop `WFI` + load `EVENTS` until [9]; restore `EVENTS_MASK` to [8] \| [7]; (5) load slot 0, then the jump address in slot 1, and jump. The ROM never writes data memory. *A few dozen bytes per controller* | new | v1.2 | D22, D31, D37.1, D37.2, D41 (F13) |
| K8 | boot: C&C loads halted tiles (modes 6, 7), stages big SRAM with mode 2 through `PTR[64]`, then RUN | **the boot sequence** (6.2): reset → every controller waits in its ROM → **C&C 1 booted from outside** (overlays and first command from the host or harness) → C&C 2 and C&C 3: overlays uncounted, then the first command; each sends C&C 1 a "started" request → tiles: overlays uncounted, then the first command; READY; GO; the ROM jumps → **boot part 2** loads big SRAM by announced, **counted** staging (no sooner than `stage_delay` after GO) and enables interrupts. Overlays and the first command are of one kind; boot order is the order of the first commands | changed | v1.2 | D22, D31, D44 (W5-26) |
| K9 | staging by source through `PTR[64]`, uncounted while halted | **by address** (mode 0 to region 0x03), in area A7 below its top 128 KB, 128-bit packets, **always counted** (6.2) | changed | v1.2 | D17, W5-15 |
| K10 | `done` set by `TILE_PULSE` [0], forced to 1 in HALT; READY into C&C | `PULSE_DONE`; cleared by GO and the reset; READY is a one-cycle pulse on the rising edge of `done` & command landed, into C&C 1's `READY_LINES` (sticky); **tiles 0–63 only** (6.3) | changed | v1.2 | D23, D28 |
| K11 | GO from C&C (`TILE_PULSE` [3] where wired) | **`PULSE_GO` in C&C 1's peripheral**: clears READY bits 0–63 and drives GO in one store (a READY pulse in the same cycle is kept) and restarts the watchdog; GO reaches tiles 0–63 `go_latency` later; **C&C 2/3 are not wired to GO**; the controller block has no GO register (6.3) | changed | v1.2 | D28 |
| K12 | — | **"all tiles READY"**, a live event in C&C 1's peripheral: (`READY` & `READY_EXPECT`) == `READY_EXPECT` ≠ 0 (6.3, 6.6) | new | v1.2 | D28 |
| K13 | command convention: mode-7 words, slot 0 last | uncounted **mode-0** writes, slot 0 last, all of one kind; alternating argument blocks unchanged (6.3) | changed | v1.2 | D6 |
| K14 | a tile that only sends waits for `mem_idle` before `done` | counts its own loopback writes, if any, and waits for them (6.4) | changed | v1.2 | D18 |
| K15 | block copy: mode-6 stream, `DMA_CM_REMAINING`, start `TILE_PULSE` [1]; the swap code sits in the resident loader or untouched code | mode-0 stream to region 0x00/0x01 on a write counter, `PULSE_DMA`; overlays from C&C 1 are uncounted mode-0 writes followed by the command (6.5) | changed | v1.2 | D6 |
| K16 | timing rows: counters done, CM path modes 6–8, register loads 1 or 2, RUN/HALT | write counters at K; CM path mode 0 to 0x00/0x01; loads 2 (block, windows) or 1 (tables); **GO `go_latency`**; **error lines `error_latency`**; reset → 0x02000000 (5) | changed | v1.2 | D3, D23, D45 |

### 2.8 The controllers C&C 1–3 and the memory system (spec 2, 6.6, 6.7, 7, 8.14)

| ID | v1.1 | v1.25 (section) | kind | step | why |
|---|---|---|---|---|---|
| X1 | one C&C controller of its own design, with its own memories, RUN/HALT and GO-pulse registers, a receive FIFO (≥ 64) with an error flag, an error bit ORed from all tiles, REPORT | **C&C 1, 2, 3 are tile instances**: the tile's controller, 32 KB + 32 KB, the boot ROM, **2 MB big SRAM**, the same register block and `mesh.h`; each on its tile's fifth port; **one peripheral each, its engine** (Ingress/Egress and the UART; the Updater/Binder/RW engine; the Tracer); an engine's error is its controller's peripheral error (6.6) | changed | v1.2 | D1, D15, D25 |
| X2 | — | **every chip-level function in C&C 1's peripheral** (type 1), from word 32 (8.14): `ERROR_LINES_0`–`2` (words 32–34; bit *b* of word *w* = endpoint 32*w* + *b*; word 2 [0] unused, [1] C&C 2, [2] C&C 3), `READY_LINES_0`–`1` (35–36), `READY_EXPECT_0`–`1` (37–38), `PULSE_GO` (39), `WATCHDOG` (40), `LED` (41), the UART (42–45), reserved (46–63), host interface (64–127) *(Deferred)*. `ERRORS` [0] = the unit-error bit (live); `EVENTS` [2] all tiles READY (live), [3] watchdog expired (d1), [4] UART RX not empty (live), [5] UART TX not full (live) | new | v1.2 | D15, D24, D28, D36 |
| X3 | C&C receives errors and results through its receive FIFO | C&C 1 receives the units' reports and C&C 2/3's "started" through its request port (target 0's 128-entry queue); packets for it land in its memories, counted (6.6) | changed | v1.2 | D38 |
| X4 | C&C waits `go_latency` + 1 after each GO | **after each `PULSE_GO` store** (6.6) | changed | v1.2 | D28 |
| X5 | the memory system hangs off C&C, served at its port (src 64), answers with modes 3, 6, 9 and mode-8 replies | **C&C 2's peripheral**, the Updater/Binder/RW engine, the only bulk LPDDR client, at dst 65 through tile 1's fifth port, from its own router port *(Deferred, item 118: its src)*; everything it sends is counted mode-0 writes (6.7, 7) | changed | v1.2 | D25 |
| X6 | a request names a destination kind and a 128-byte-unit address | names a **full 32-bit destination address**, **the data's counter and the reply's counter**, and **a reply address** anywhere in the requester's data memory; the exact layout comes with the file system *(item 119)* (6.7) | changed | v1.2 | D40, W5-17 |
| X7 | a value answer is a mode-8 reply | a 32/64-bit **mode-0 write to the named reply address**, on the named counter (6.7) | changed | v1.2 | D6, D40 |
| X8 | writes: token-gated mode-3 write streams to C&C | **by tags**: the file system owns a pool, each tag a 2–4 KB window in the RW engine's region, written by plain mode-0 packets to dst 65; fan-in is the file system's to pace *(Deferred, item 119)* (6.7) | changed | v1.2 | D25, D33 |
| X9 | — | LPDDR controller and PHY behind the engine's AXI bus, kept across a soft reset (7) | new | v1.2 | D35 |

---

## 3. Register and field tables

**Conventions.**
- Every table has a **v1.1**, a **v1.2** and a **v1.25** column. Outside the replayer's words, v1.2 and v1.25 are identical; "=" in the v1.2 column means "as v1.25".
- Addresses are the low 12 bits of 0xFFFFF8xx–0xFFFFFFxx unless marked.
- Access: R, W, R/W, R/W1C.

### 3.1 The controller's register block (8.1)

Every offset moved at **v1.2**, so every image is rebuilt against `mesh.h` 1.2. Rows are in v1.25 order. v1.1 registers with no v1.25 counterpart are at the end.

| v1.25 offset | v1.25 register (access) | v1.2 | v1.1 offset, register (access) | note |
|---|---|---|---|---|
| 0x800 | `PARAMS` (R) | = | 0x800 `TILE_STATUS` (R), its constant fields | 3.2 |
| 0x804 | `STATUS` (R) | = | 0x800 `TILE_STATUS`, its live bits | 3.2; bits renumbered |
| 0x808 | `ERRORS` (R) | = | 0x804 `TILE_CONDITIONS` (R) | 3.3 |
| 0x80C | `PULSE_DONE` (W) | = | 0x808 `TILE_PULSE` [0] | C35 |
| 0x810 | `PULSE_DMA` (W) | = | 0x808 `TILE_PULSE` [1] | T6 |
| 0x814 | reserved | = | (`TILE_PULSE` [3] GO → C&C 1's `PULSE_GO`; [2] → a window's `PULSE_RUN`) | |
| 0x818 | `EVENTS` (R) | = | 0x848 `WAKE_EVENTS` (R) | 3.4 |
| 0x81C | `EVENTS_PEEK` (R) | = | 0x84C `WAKE_PEEK` (R) | |
| 0x820 | `EVENTS_MASK` (R/W) | = | 0x850 `WAKE_MASK` (R/W) | [15:0] (was [22:0]) |
| 0x824 | `EVENTS_REQUIRED` (R/W) | = | 0x854 `WAKE_REQUIRED` (R/W) | [15:0] |
| 0x828 | `ROUTER_HDR` (R/W) | = | 0x80C `ROUTER_SET_HDR` (W) | 3.5 |
| 0x82C | `ROUTER_ADDR` (R/W) | = | (in `ROUTER_SET_HDR` [31:11]) | 32-bit |
| 0x830–0x838 | `ROUTER_SET_W0`–`W2` (W) | = | 0x810–0x818 | unchanged behaviour |
| 0x83C–0x844 | `ROUTER_SEND_32`, `_64`, `_128` (W) | = | 0x81C–0x824 | unchanged behaviour |
| 0x848–0x854 | `REQUEST_SET_W0`–`W2`, `REQUEST_SEND` (W) / `REQUEST_GET_W0`–`W2`, `REQUEST_RECEIVE` (R) | = | 0x858–0x864 | T37–T39 |
| 0x858 | `DMA_CONFIG` (R/W) | = | 0x828 `DMA_CONFIG` (W) | fields unchanged |
| 0x85C | `DMA_RECORD_BASE_ADDRESS` (R/W) | = | 0x82C (W) | record-aligned (T20) |
| 0x860–0x878 | `WRITE_COUNTER_1`–`7` (R/W), 20 bits, a write adds | = | 0x830 `DMA_SRAM_REMAINING`, 0x834 `DMA_CM_REMAINING`, 0x838 `DMA_REPLY_REMAINING`, 0x83C `DMA_PERIPH_REMAINING` | C16 |
| 0x87C | `SRAM_RD_ADDR_QUEUE` (R/W) | = | 0x840 (R/W) | |
| 0x880 | `SRAM_RD_DEST_ADDR` (R/W) | = | 0x844 (W) | 3.5 |
| 0x884–0x9F4 | reserved: read 0, stores ignored | = | (0x8A0– unmapped: bus error) | C9, C34 |
| 0x9F8 | simulator-only exit word (W) | = | — | reserved on hardware |
| 0x9FC | simulator-only trace word (W) | = | — (trace at 0xFFFFFFF0) | reserved on hardware |
| 0xA00–0xBFC | peripheral 1's window | = | (0xA00– unmapped) | 3.6 |
| 0xC00–0xDFC | peripheral 2's window | = | — | |
| 0xE00–0xFFC | peripheral 3's window | = | — | |
| — | gone | — | 0x868 `SENDER_SEND`, 0x86C `SENDER_ADDR` | P11 |
| — | gone | — | 0x870–0x88C `PERIPH_STATUS_0`–`7` | R9 |
| — | gone (→ C&C 1's peripheral, 3.7) | — | 0x890 `UART_TX`, 0x894 `UART_RX`, 0x898 `UART_STATUS`, 0x89C `UART_DIVISOR` | P13 |
| table window | 0xFFFF2000–0xFFFF35FF *(Proposed, item 120)* | = | 0x00002000–0x00003FFF | T31 |

**Load latency** (C6, T33): **2 cycles** for every word of the block and the windows, **1** for the table window. v1.1 allowed 1 or 2, at the implementation's choice.

### 3.2 `PARAMS` and `STATUS` (8.2)

| field | v1.1 `TILE_STATUS` | v1.2 | v1.25 |
|---|---|---|---|
| `rd_running` | [0] | = | `STATUS` [0]; falls with DMA run done (T7) |
| `rd_done` | [1] | = | `STATUS` [1] |
| GATHER busy | [2] | = | `STATUS` [2] |
| `periph_running` | [3] | = | → the peripheral's `STATUS` [0] |
| `sender_busy` | [4] | = | → the peripheral's `STATUS` [1] (DMA in progress) |
| `send_busy` | [5] | = | `STATUS` [3] |
| `ts_busy` | [6] | = | gone |
| `mem_idle` (reset 1) | [7] | = | gone |
| `cm_idle` (reset 1) | [8] | = | gone |
| `done` | [9], **reset 1**, forced 1 in HALT | = | `STATUS` [4], **reset 0** (C28) |
| command landed | [10] | = | `STATUS` [5] |
| `request_busy` | [11] | = | `STATUS` [6] |
| peripheral type | [15:12] (1 = replayer) | = | → each peripheral's `PARAMS` [7:0] (replayer = **4**) |
| peripherals present | — | = | `PARAMS` [2:0], bit *n* − 1 for peripheral *n*; 0b001 on tiles and C&C 1–3 |
| endpoint id | [26:20], 0–63 | = | `PARAMS` [26:20], 0–66 |
| big-SRAM size (256 KB units − 1) | [31:27] | = | `PARAMS` [31:27]; 7 on C&C 1–3 (2 MB) |
| other bits | [19:16] spare | = | `PARAMS` [19:3] spare; `STATUS` [31:7] reserved |

### 3.3 `ERRORS` (8.3; v1.1 `TILE_CONDITIONS`)

| bit | v1.1 | v1.2 | v1.25 | kind |
|---|---|---|---|---|
| 0 | router overflow | = | router overflow (link, eject, fifth-port FIFOs) | P |
| 1 | bad mode or size | = | **bad packet or misaligned access** (L7, T2) | P |
| 2 | unexpected arrival (at acceptance) | = | unexpected arrival (**at the decrement**) | P |
| 3 | drift | = | drift | P |
| 4 | write-engine input overflow | = | = | P |
| 5 | table request overflow | = | = | H |
| 6 | GATHER queue overflow | = | = | H |
| 7 | read-port error (overflow or misaligned) | = | read-port **overflow only** | P |
| 8 | done early | = | = | P |
| 9 | bus error | = | bus error (now traps; wider set, C9) | P (H) |
| 10 | illegal instruction | = | illegal instruction (traps) | P (H) |
| 11 | bad peripheral write | = | **peripheral 1, live** (its `ERRORS` ≠ 0) | per peripheral |
| 12 | peripheral condition 2 (replayer: queue overrun) | = | **peripheral 2, live** | per peripheral |
| 13 | peripheral condition 3 (replayer: invalid expansion) | = | **peripheral 3, live** | per peripheral |
| 14 | receive underflow | = | = (every `REQUEST_RECEIVE` load on a tile) | P |
| clearing | entering HALT and chip reset | = | **sticky until the reset** ([13:11] follow their peripheral) | |

### 3.4 `EVENTS` (8.10; v1.1 `WAKE_EVENTS`)

| bit | v1.1 | v1.2 | v1.25 |
|---|---|---|---|
| 0 | `DMA_SRAM_REMAINING` done | = | write counter 1 done |
| 1 | `DMA_CM_REMAINING` done | = | write counter 2 done |
| 2 | `DMA_REPLY_REMAINING` done | = | write counter 3 done |
| 3 | `DMA_PERIPH_REMAINING` done | = | write counter 4 done |
| 4–6 | reserved | = | write counters 5–7 done |
| 7 | reserved | = | **counters done** |
| 8 | GO | = | GO (mesh tiles only) |
| 9 | counters done | = | **command landed** |
| 10 | command landed | = | **DMA run done** (cleared by `PULSE_DMA`) |
| 11 | engine run done | = | request FIFO no longer full |
| 12 | request FIFO no longer full | = | request pending (live) |
| 13 | service line 1 (replayer: code low-water) | = | **peripheral 1** (live) |
| 14 | service line 2 (read-log low-water) | = | **peripheral 2** (live) |
| 15 | service line 3 (write-log low-water) | = | **peripheral 3** (live) |
| 16 | service line 4 (expansion low-water) | = | reserved |
| 17 | service line 5 (new expansion) | = | reserved |
| 18 | service line 6 (done) | = | reserved |
| 19 | `REQUEST_PENDING` (live) | = | reserved |
| 20 | sender idle | = | reserved |
| 21 | UART RX not empty (live) | = | reserved (→ C&C 1's peripheral) |
| 22 | UART TX not full (live) | = | reserved (→ C&C 1's peripheral) |
| mask reset | [8] \| [9] | = | **[8] \| [7]** |

### 3.5 Header registers and the common layout (8.1)

All four share [31:17] = dst/*d* [31:25], mode [24:22], counter [21:19], size [18:17], copied as [24:17] → link [167:160].

| register | v1.1 | v1.2 | v1.25 |
|---|---|---|---|
| `STREAM_SRC` | source [20:0]; spare [31:21] | = | source [20:0]; **gap after [23:21]**; spare [31:24] |
| `STREAM_DEST` | dest word [20:0]; *d* [27:21]; spare [31:28] | = | **destination address [31:0]** (mode 0 only) |
| `STREAM_COUNT` | count [17:0]; mode [21:18]; read size [23:22]; gap after [26:24]; spare [31:27] | = | count [16:0]; read size [18:17]; **counter [21:19]**; mode [24:22]; ***d* [31:25]** |
| `ROUTER_HDR` (v1.1 `ROUTER_SET_HDR`, W) | dst [6:0]; mode [10:7]; addr [31:11] | = | R/W: **no-increment [0]**; spare [16:1]; size [18:17] reserved (reads 0); counter [21:19]; mode [24:22]; dst [31:25] |
| `ROUTER_ADDR` | — | = | R/W, [31:0] |
| `SRAM_RD_DEST_ADDR` (W in v1.1) | destination [13:0] (packed CM word); size [15:14]; spare [31:16] | = | R/W: destination [16:0] (data-memory byte offset); size [18:17]; counter [21:19]; reserved [31:22] |
| `SRAM_RD_ADDR_QUEUE` | store: chunk [20:0] (1, 2 or 4 words); load: free entries | = | store: chunk [20:0] (32, 64 or 128 bits); load: free entries |
| a window's `DMA_HDR` (v1.1 `SENDER_SEND`) | dest [6:0]; mode [10:7]; queue [12:11]; count [28:13]; spare [31:29] | = | count [11:0]; queue [13:12]; spare [16:14]; size [18:17] reserved; counter [21:19]; mode [24:22]; dst [31:25] |
| a window's `DMA_ADDR` (v1.1 `SENDER_ADDR`) | word address [20:0] | = | [31:0] byte address |
| `DMA_CONFIG` | kind [0]; start [8:1]; count [17:9]; bucket digit [20:18]; histogram digit [23:21]; *k* − 1 [26:24] (W) | = | same fields, **R/W** |
| `DMA_RECORD_BASE_ADDRESS` | chunk [20:0] (W) | = | chunk [20:0], record-aligned (R/W) |
| write counters | count [19:0] | = | count [19:0] |

### 3.6 A peripheral's standard window (4.17.4, 8.11)

Peripheral *n*'s word *w* is at 0xFFFFF800 + 0x200·*n* + 4*w*. All of it is **new at v1.2**. v1.1's equivalents are noted.

| word | register (access) | bits | v1.1 equivalent |
|---|---|---|---|
| 0 | `PARAMS` (R) | [7:0] type, [15:8] version, rest peripheral-defined | `TILE_STATUS` [15:12] |
| 1 | `STATUS` (R) | [0] run in progress, [1] DMA in progress | `periph_running`, `sender_busy` |
| 2 | `ERRORS` (R) | peripheral-defined, sticky | `TILE_CONDITIONS` [13:11] |
| 3 | `PULSE_RUN` (W) | store 1 | `TILE_PULSE` [2] |
| 4 | `PULSE_DMA` (W) | store 1 | `SENDER_SEND` (as a pulse) |
| 5 | `EVENTS` (R) | [0] run done, [1] DMA done, rest defined; counted per bit | service lines `WAKE_EVENTS` [18:13], sender idle [20] |
| 6 | `EVENTS_PEEK` (R) | as `EVENTS` | `WAKE_PEEK` |
| 7 | `EVENTS_MASK` (R/W) | 0 at reset | `WAKE_MASK`'s line bits |
| 8 | `DMA_HDR` (R/W) | 3.5 | `SENDER_SEND` |
| 9 | `DMA_ADDR` (R/W) | [31:0] | `SENDER_ADDR` |
| 10–31 | reserved | | |
| 32–127 | peripheral-defined | | `PERIPH_STATUS_0`–`7` |

### 3.7 C&C 1's peripheral, peripheral-defined words (8.13, 8.14)

All of it is **new at v1.2**. Base 0xFFFFFA00.

| word (offset) | register (access) | v1.1 equivalent |
|---|---|---|
| 32–34 (0xA80–0xA88) | `ERROR_LINES_0`–`2` (R, live) | C&C's single ORed error bit |
| 35–36 (0xA8C–0xA90) | `READY_LINES_0`–`1` (R, sticky) | C&C's READY bits |
| 37–38 (0xA94–0xA98) | `READY_EXPECT_0`–`1` (R/W) | — |
| 39 (0xA9C) | `PULSE_GO` (W) | `TILE_PULSE` [3] on C&C |
| 40 (0xAA0) | `WATCHDOG` (R/W) | C&C's watchdog (software) |
| 41 (0xAA4) | `LED` (R/W) [0] | — |
| 42 (0xAA8) | `UART_TX` (W) | 0x890 |
| 43 (0xAAC) | `UART_RX` (R) | 0x894 |
| 44 (0xAB0) | `UART_DIVISOR` (R/W) | **0x89C** |
| 45 (0xAB4) | `UART_STATUS` (R/W1C) | **0x898** |
| 46–63 | reserved | |
| 64–127 | host interface *(Deferred)* | |
| `ERRORS` [0] | unit-error bit (live) | |
| `EVENTS` [2]–[5] | all tiles READY (live), watchdog (d1), UART RX not empty (live), UART TX not full (live) | `WAKE_EVENTS` [21], [22] |

### 3.8 The replayer (4.19.3, 4.19.9, 4.19.12, 8.11)

**Its window's words.**

| word | v1.1 | v1.2 | v1.25 |
|---|---|---|---|
| `PARAMS` [7:0] | type 1 in `TILE_STATUS` [15:12] | **4** | 4 |
| `STATUS` [0] | `periph_running` | run in progress | = v1.2 |
| `ERRORS` | tile [11] bad write, [12] overrun, [13] invalid expansion | [0] bad write, [1] overrun, [2] invalid expansion | = v1.2 |
| `EVENTS` | service lines [18:13] | [0] run done, [2]–[5] code, read-log, write-log, expansion credits (depth 2), [6] new expansion | = v1.2 |
| 32 `ROWS_RUN` | `PERIPH_STATUS_0` [15:0] | [15:0] | = v1.2 |
| 33 `EXPANSION_OFFSET` | `PERIPH_STATUS_0` [31:16] | [15:0] | = v1.2 |
| 34 `EXPANSION_OPCODE` | `PERIPH_STATUS_1` | [31:0] | = v1.2 |

Its region map is R3.

**The expansion word (128 bits; only [63:0] defined).**

| bits | v1.1 | v1.2 | v1.25 |
|---|---|---|---|
| [7:0] `opcode` | 0–167 | = v1.1 | **0–170, renumbered** (3.9) |
| [15:8], [23:16], [31:24] `rd`, `rs1`, `rs2` operand codes | | = | = (146–255 invalid in a replayer) |
| [43:32] `seq_remaining` | | = | = |
| [44] `next_from_value`, [45] `hold`, [47:46] `rd0`, [50:48] `format`, [51] `virt`, [52] `imm0_pc0`, [53] `report_c`, [54] `from_log`, [55] `untraced` | | = | = |
| [62:56] | `advice_reg` (tracer only) | = | **[61:56] `advice_reg`** (register − 146) |
| [62] | (part of `advice_reg`) | = | **`sign_imm`** |
| [63] | spare | = | **`offset_imm`** |

**The output row (5 × 128 bits).**

| word, bits | v1.1 | v1.2 | v1.25 |
|---|---|---|---|
| 0 [39:32] `opcode` | `VirtualAdvice` = 122 | = | **126**, and every renumbered opcode |
| 0 [86] | spare | = | **`imm_signed`** |
| 0 [127:87] | spare | = | spare |
| 1 [127:64] `imm` | sum of immediate slots | = | the row's immediate (`ShiftData…`: `shift_offset`); 0 for advice |
| 4 [63:0] `mem_addr` | rs1 + imm for LD, SD | = | the eight memory opcodes; narrow rows rounded down |
| 4 [127:64] `mem_value` | LD: rd after; SD: pre-value | = | load rows: rd after; store rows: pre-value |
| everything else | | = | = |

### 3.9 The row-opcode renumbering

`spec/replayer-opcodes.md` **section 2.0** lists all 61 changes. In brief:

| v1.1 = v1.2 codes | v1.25 codes | shift |
|---|---|---|
| 0–109 | 0–109 | none |
| — | 110–115 | new: `VirtualLoadB/H/W`, `VirtualStoreB/H/W` |
| 110–111 | 116–117 | +6 |
| 112–113 | — | removed: the alignment assertions |
| 114–153 | 118–157 | +4 |
| 154 | — | removed: `VirtualAlignAddr` |
| 155–167 | 158–170 | +3 |

The complete map, generated from the two appendices, is also in `WORKER13/opcode-map.txt`.
