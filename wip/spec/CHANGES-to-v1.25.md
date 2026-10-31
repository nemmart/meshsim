# The change guide to v1.25, from v1.1 and from v1.2

For the RTL engineer of the mesh. This guide covers two upgrade paths to **v1.25**:

- from **v1.1**, which the RTL implements today, straight to v1.25;
- from **v1.2**, the frozen waypoint, to v1.25.

It is organized **by hardware unit**. Each unit's section lists everything that changed in that unit since v1.1, and where v1.25 revised a v1.2 change, it gives the final state.

**Sources.**
- **The authority is `spec/machine-spec.md` v1.25** (with `spec/replayer-opcodes.md`). This guide matches **spec commit `0b4ba20`** *(to be re-stamped when the guide is finished)*.
- It folds together `CHANGES-v1.2.md` (v1.1 → v1.2) and `CHANGES-v1.25.md` (v1.2 → v1.25).
- It was checked against all three specs (`v1.1/`, `v1.2/`, `wip/`).
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
- **From v1.2:** read only the entries marked **v1.25** or **both**. Section 1.2 lists them. Almost all are in the replayer (section 2.7).

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
| `VirtualAlignAddr`, the two alignment assertions; the contiguous-mask narrowing of PEXT; per-program tables; "unused entries are controller class"; the exact-word template key; `VirtualHostIO`'s remap; EBREAK, ECALL and MRET as controller-fed | 4.19 | **step v1.25** (2.7) |

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
