# WORKER3 review, round 2: registers, peripherals and the software impact

This round does two things. It checks how `wip/planning/DECISIONS-v1.2.md` answered round 1's findings
(W3-01 to W3-23), and it reviews the register and peripheral design as it now stands. It also brings the
migration inventory up to date.

How to read the citations:

- **D l.*n*** is a line of the decisions log at commit b8bd504, and **D*n*** is one of its sections.
- **S l.*n*** is a line of `wip/spec/machine-spec.md`.
- **W1-*nn*** is a finding in `WORKER1/REVIEW-2.md`.
- Code under `wip/sw`, `wip/sort` and `wip/meshsim` hasn't changed since round 1.
- Anything inferred rather than read is marked *(inferred)*.
- The C claims were checked by compiling a draft header with `riscv64-unknown-elf-gcc` 13.2 (`-march=rv32i_zmmul -mabi=ilp32 -O2`). The draft is described in W3-27.

## 1. Summary: the five points that matter most

1. **W3-24 (blocker): the replayer's credits leak.** A credit comes back only when a whole half-buffer has been consumed, but every push shorter than a half uses up a credit. Each controller-fed expansion shorter than 128 words therefore loses a credit, and the third such expansion in a pass deadlocks. Separately, credits nobody needs keep the tile's wake bit high.
2. **W3-25 (issue): counted events can't replace a queue of items.** v1.1's "pop the next item into a register" kind is gone. The RW engine's closed-tag return (D25) and Ingress/Egress's host events need it.
3. **W3-26 (issue): reset leaves control state undefined.** The reset values of `done`, the `done` gate, "command landed" and a pending GO are unstated. A GO sent while units sleep in the boot ROM stays pending and makes the image's first `wait_for_command()` return with the boot marker. This adds to W1-23.
4. **W3-27 (issue): the per-block C structs have four problems in practice.**
   - GCC holds the block's base address in a register, which adds one instruction to the GO-to-start path.
   - `TileRegs t = tile;` compiles silently and consumes events and pops requests.
   - "Extends `PeripheralStd`" needs a macro, because C has no inheritance.
   - A type named `Tile` would clash with the sort's planner.
5. **W3-33 (issue): the 16-bit stream count breaks the full-size sort.** At 64 × 65,536, the local sort's streams have count 65,536, one over the limit, and skewed transfers reach the same size.

## 2. Status of round 1's findings

| finding | status | evidence |
|---|---|---|
| W3-01 C&C 1's permanent errors | **partly closed** | UART line errors are status, not errors (D21 l.824-826). But D10 makes `STATUS` read-only and live (l.358, l.374), so a sticky line-error bit there can never be cleared (W3-28). |
| W3-02 peripheral events | **partly closed** | Counted and live events (D10 l.394-408) give the replayer's lines a home. Their credit rule leaks (W3-24), and v1.1's item-FIFO kind is gone (W3-25). |
| W3-03 `wait_tables()` | **closed** | `PULSE_DMA` clears DMA run done (D17 l.631), and helpers may poll `STATUS.rd_running` (l.634-636). Small slip: l.632 still says "[6]" for [7] (W3-31). |
| W3-04 `ROUTER_ADDR` | **closed** | The no-increment bit [0] (D7 l.290-298), and advance by plain byte addition (D26 l.975-978). Modes 1-4 ignore the address (D26 l.987), so whether they advance it hardly matters; the log doesn't say. |
| W3-05 mode 3 to C&C | **closed** | LPDDR writes go through tag windows in the RW engine's region at dst 65 (D25 l.928-936), and controllers are tile instances (l.921). |
| W3-06 stale live bit | **closed** | Spurious wakes are allowed; software re-checks; the hardware aims for "current by the next instruction" (D10 l.409-413). |
| W3-07 `STATUS` positions | **closed** | Renumbered compactly (D18 l.640-666). |
| W3-08 *d* | **closed** | D12 l.485-486. D10 l.420 still says `DMA_HDR` [31:17] is laid out "exactly" as `STREAM_COUNT`'s, *d* included (W3-31). |
| W3-09 NULL | **closed** | Data word 0 is reserved and meshsim flags writes to it (D27 l.1000-1002). |
| W3-10 reset PC, loader | **partly closed** | The boot ROM at 0x02000000 is the reset vector (D22). Still open: the ROM's contract (W1-23); the image handler's placement against overlays; C&C 1's own boot (W1-27). D5 l.222-224 still lists "the resident loader's address". |
| W3-11 program memory | **closed** | Load-only (D5 l.215-217). The summary (l.25-26) and l.218 still say the controller sees only 0x00, 0x01 and 0xFF, although it fetches and loads 0x02 (W3-31). |
| W3-12 `mem_idle` | **closed** | Gone from the hardware (D18 l.668-669, D17 l.625-628). |
| W3-13 bad packets | **closed** | D26's table (l.981-988), still partly *(proposed)*. |
| W3-14 standard fields | **closed** | D10 l.370-382. |
| W3-15 rules for the windows | **closed** | Spec-update items (D10 l.389-393). |
| W3-16 names | **closed in principle** | One struct per block (D14 l.523-529). Practical problems in W3-27. |
| W3-17 two address formats | **closed** | Bytes in packets, chunks in the engines (D5 l.225-230, D26 l.962-965, D29 l.1032-1033). |
| W3-18 C&C 1's registers | **reviewed** | D28 is reviewed in W3-28. |
| W3-19 three counters | **closed** | Four counters and a 3-bit field (D30). The 16-bit stream count it brings is W3-33. |
| W3-20 `UART_CONTROL` | **closed** | D29, D9 l.343. |
| W3-21 to-do 13 alias | **closed** | D29. |
| W3-22 simulator addresses | **partly closed** | Moved to 0xFFFFF9FC (D29), but that is a reserved word meshsim flags (W3-32). |
| W3-23 Q12 | **closed** | Log files through the file system (D23 l.897-899). |

## 3. Register map check

The complete map holds together:

| block | check |
|---|---|
| **Controller block** (D11 with D30) | 0x800-0x874 has no overlaps; 0x814, `PULSE_GO`'s old word, is now a hole, and 0x878-0x9FC is reserved. |
| **The four windows** (D9) | Tile 0xFFFFF800-0xFFFFFFFF exactly, and every word is `x0`-reachable. |
| **Table window** | 0xFFFF2000-0xFFFF35FF; the rest of region 0xFF is unmapped. |
| **Boot ROM** | Region 0x02 is fetch and load for the controller, and a bad packet for the network (D26). |
| **The 40 v1.1 registers** | All accounted for as in round 1, with `PULSE_GO` now in C&C 1's window (D28). |

**The common header block** (D12, D30) is the same in every register that carries one:

| register | [16:0] | size [18:17] | counter [21:19] | mode [24:22] | [31:25] |
|---|---|---|---|---|---|
| `STREAM_COUNT` | count [15:0], spare [16] | yes | yes | yes | *d* (translated, not copied) |
| `ROUTER_HDR` | no-increment [0], spare | reserved | yes | yes | dst |
| `DMA_HDR` | count [11:0], queue [13:12], spare | reserved | yes | yes | dst |
| `SRAM_RD_DEST_ADDR` | byte offset [16:0] | yes | yes | reserved (0) | reserved (self) |

The link word's [167:160] is the [24:17] block, 8 bits (D2 l.121-124).

Two details don't line up:

- D4 l.179 still says 88 bits are used; with the 16-bit count it is 87.
- D13 l.495 still says "units per Q10"; Q10 is now bytes.

Both are listed in W3-31.

## 4. New findings

### W3-24 — The replayer's credits leak, and unneeded credits keep the tile awake (blocker)

*D10 (l.394-408), D16 [10]; S 4.19.8 (l.1457-1465), 4.19.9 (l.1475-1481), 4.19.11 (l.1561).*

D10's rule has three parts:

- a low-water bit holds up to 2 credits;
- "a queue starts with 2 after reset";
- the peripheral returns one credit "each time it has consumed half a buffer", and the controller may split a credit into several moves, as long as the total stays within it.

Three consequences follow, and they have to be specified before the replayer can be written.

**Credits leak.** A credit returns per *half consumed*, while one is spent per refill.

Take the expansion queue (half = 128 words). An expansion of 3 words spends a credit and returns none, because the replayer never consumes 128 words of it. v1.1 avoided this by resetting the expansion queue's pending count at each new expansion (S l.1464-1465, l.1469). A pass has about 13 controller-fed expansions per window (S l.1518), so with two credits the third short expansion finds none, and the pass deadlocks.

The code and log queues leak the same way whenever a refill is shorter than a half before the stream's end. That happens, for instance, if the controller sends only what is resident before a staged LPDDR block arrives.

**When credits are set.** "After reset" must mean at every `PULSE_RUN`, since a run empties the queues (S l.1561). The new-expansion event should also reset the expansion queue to two credits, replacing v1.1's "the first 128 words are credited" (S l.1465).

**Unneeded credits.** Some credits have nothing behind them:

- at a stream's end;
- after an expansion's last word;
- the write log in a pass without the store log (S l.1457).

These credits still count, so the tile's live bit [10] stays high. If the loop leaves those bits in the peripheral's `EVENTS_MASK`, every `WFI` falls straight through and the loop spins. If it consumes them without sending, it loses them for good, the same leak as above. v1.1 gated these lines off at the source: the write log only with the store log, and the expansion line only while words remain (S l.1457-1461).

*Suggested resolution:*

- Reset each queue's credits to 2 at `PULSE_RUN`, and the expansion queue's at each new expansion.
- Return a credit per refill landed, or require every refill except a stream's last to be exactly a half.
- Let a bit be gated by a live condition (count > 0 **and** more to send), as v1.1's lines were.
- Write the 4.19.9 loop out in the spec against these rules.

### W3-25 — Counted events lose v1.1's item FIFO (issue)

*D10 (l.394-408), D25 (l.934-936), D28 (l.1024); S 4.17.3 (l.1022-1029).*

v1.1 had three kinds of line: sticky event, queue credit, and **item FIFO**. In an item FIFO, a consuming load pops the next item into the line's designated status register. Counted events cover the first two kinds.

The third is what D25 suggests for the RW engine: "an event in the engine's `EVENTS` and a register presenting the closed tag". Several tags can close before the file system services them, so it needs:

- a depth of the tag pool, not 1 or 2;
- a presented register that advances on each consume.

Without that pairing, a count of 5 closed tags says nothing about *which* tags. Ingress/Egress's host events (D28 l.1024) are likely the same. The "one load consumes one from every masked bit" rule (l.406-407) makes a load that claims a tag also claim credits on other bits. That is acceptable if the program services all of them.

*Suggested resolution:* restore 4.17.3's item kind under counted events: a depth chosen per bit, and a designated register that the consuming load advances. Keep "at most one presented bit per peripheral".

### W3-26 — Reset values of the control state, and a GO sent into the boot ROM (issue)

*D16 (l.579-580, l.593), D22 (l.863-875), D23 (l.887), D28 (l.1018); S 6.3 (l.1761-1767), 4.12 (l.890); W1-23.*

W1-23 covers the boot ROM's mask, "command landed" after the jump, and `done`'s reset value. Three register-level points remain:

- **The `done` gate.** Its reset value must be "open". If reset left it closed, as `done` = 1 would imply under v1.1's "forced to 1 in HALT", only GO could wake the ROM's `WFI`, and boot would hang.
- **A GO sent while units are in their boot ROMs.** D28's `PULSE_GO` both clears READY and drives GO. C&C 1 can't clear stale READY bits after a reset without also pulsing GO into units sleeping in ROM. GO is sticky in every tile's `EVENTS` [5], so the pending GO survives the jump. The image's first `wait_for_command()` then wakes at once on it (the gate admits a pending GO), consumes it, and returns slot 0: the boot marker, read as a command.
- **What reset clears.** D23 l.887 says "`EVENTS_MASK` resets at reset". The sticky events, `EVENTS_REQUIRED`, the counters and their qualifiers, `done`, the gate and "command landed" need the same list. That is v1.1's HALT → RUN list (S l.1729), now attached to the reset.

*Suggested resolution:*

- State every reset value (all 0, mask GO | counters done).
- Have the ROM clear the stale GO and "command landed" before jumping: consume [5], and either a store that clears "command landed" or define the jump to clear it.
- Give C&C 1 a READY clear without GO (W3-28).

### W3-27 — The struct-per-block header in practice (issue)

*D14 (l.523-529), D10 (l.355-368), D11; S 4.11 (l.751), 6.3-6.4 (l.1795, l.1801), 8.1 (l.1956); `sw/mesh.h` l.341, l.429-433.*

A draft for the tile block, `PeripheralStd` and a replayer type compiles. Its offsets are checked with `_Static_assert`, and the shared request-port offsets are anonymous unions:

```c
#define R  const volatile uint32_t     /* read-only: a store doesn't compile */
#define W  volatile uint32_t           /* write-only */
#define RW volatile uint32_t
#define PERIPHERAL_STD_WORDS  R PARAMS; R STATUS; R ERRORS; W PULSE_RUN; W PULSE_DMA; \
    R EVENTS; R EVENTS_PEEK; RW EVENTS_MASK; RW DMA_HDR; RW DMA_ADDR; uint32_t _reserved_std[22]
typedef struct { PERIPHERAL_STD_WORDS; uint32_t _defined[96]; } PeripheralStd;
typedef struct { PERIPHERAL_STD_WORDS; R ROWS; R PRESENTED; uint32_t _defined[94]; } ReplayerPeripheral;
typedef struct {
    R PARAMS; R STATUS; R ERRORS; W PULSE_DONE; W PULSE_DMA; uint32_t _was_pulse_go;
    R EVENTS; R EVENTS_PEEK; RW EVENTS_MASK; RW EVENTS_REQUIRED; RW ROUTER_HDR; RW ROUTER_ADDR;
    W ROUTER_SET_W0, ROUTER_SET_W1, ROUTER_SET_W2, ROUTER_SEND_32, ROUTER_SEND_64, ROUTER_SEND_128;
    union { R REQUEST_GET_W0; W REQUEST_SET_W0; };   /* ... W1, W2 likewise */
    union { R REQUEST_RECEIVE; W REQUEST_SEND; };
    RW DMA_CONFIG; RW DMA_RECORD_BASE_ADDRESS; RW WRITE_COUNTER[4];
    RW SRAM_RD_ADDR_QUEUE; RW SRAM_RD_DEST_ADDR; uint32_t _reserved[98];
} TileRegs;
#define tile        (*(TileRegs *)0xFFFFF800u)
#define peripheral1 (*(ReplayerPeripheral *)0xFFFFFA00u)
```

What the compiler showed:

1. **One instruction per access is lost on hot paths.** A lone access is a single `x0`-relative `lw`, as in v1.1. With two or more accesses in a function, GCC loads the base once (`li a5,-2048`) and uses offsets. v1.1's `MESH_REG` constants stay `x0`-relative throughout. On the synchronised start path (the load of `EVENTS` after `WFI`, the counter store, `PULSE_DMA`), that is one extra instruction. It is identical on every tile, so correctness holds, but 6.4's measured 12-13 cycles move, and 8.1's rule that every register access is one `lw` or `sw` with `rs1 = x0` stops holding.
   - *Fix:* have the generator also emit per-register `MESH_REG` macros for the start path, or accept the cycle and re-measure.
2. **A whole-block copy compiles.** `TileRegs t = tile;` loads all 128 words with no warning. That consumes `EVENTS`, pops `REQUEST_RECEIVE` (on a tile with an empty receive queue, a receive underflow, so an error and a trap), and reads reserved words. Plain assignment (`snap = tile`) fails only because the read-only members are `const`. D14's "never copied whole" can't be enforced by the type system; `const` members are the best available guard, and the generator should use them.
3. **"A type per peripheral that extends it" has no C form.** C has no inheritance. Embedding a `PeripheralStd` member gives `peripheral1.std.EVENTS`, not `peripheral1.EVENTS`, unless every build uses `-fms-extensions`. The generator should emit the standard words as a member-list macro, as above.
4. **Names.**
   - The sort's planner already has `typedef struct {...} Tile;` (`sort/skew/planner/skew.h` l.154, used as `Tile *T = &SK` throughout), so the register type can't be `Tile`.
   - A macro named `tile` would also capture any identifier `tile` in user code; there are none today *(checked by grep)*.
   - Bit names need the same scoping as registers, e.g. `EV_GO` versus the replayer's `RP_EV_CODE_LOW`.
5. **Unions are needed for the request port.** The shared offsets need anonymous unions, a C11 feature (or a GNU extension). The build flags should say `-std=c11` or `gnu11`.
6. **The table window is a separate block** at 0xFFFF2000: its own struct of arrays (`HIST[256]`, `PTR[256]`, the three `STREAM` arrays, `GROUP[64][2]`).

### W3-28 — C&C 1's register set (D28): what to change (issue)

*D28 (l.1011-1028), D21 (l.779-783, l.824-826, l.837-842), D23 (l.890), D24 (l.913); W1-21, W1-29.*

D28's structure is right. The controller block is the same on every unit, all chip-level functions sit in C&C 1's peripheral, and reports come through the ordinary request port. These are the changes I would make:

| change | why |
|---|---|
| **Clear READY without GO.** Add a `READY_CLEAR` store, or have asserting `RESET` clear the READY bits. Keep `PULSE_GO` as clear-and-GO. | After a reset, stale READY bits (W1-21) can't be cleared without pulsing GO into units in ROM (W3-26). |
| **An expected-units mask** (`READY_EXPECT_0`-`1`): the "all tiles READY" event fires when `READY & EXPECT == EXPECT`. | D23's one-at-a-time bring-up; W1-29 asks the same. |
| **UART line errors in a peripheral-defined `UART_STATUS` word, write-1-to-clear**, as v1.1's `UART_STATUS`. | D21 l.825 puts them in the standard `STATUS`, which D10 makes read-only and live (l.358, l.374), so a sticky line error there can never be cleared. |
| **Watchdog semantics:** a write of the limit (re)starts the count, and `PULSE_GO` restarts it, so one store per phase does both. Its expiry is a depth-1 event. | D28 lists only "its limit and count". |
| **State `RESET`'s power-on value: 0 (asserted).** | So the domain stays in reset until C&C 1's firmware releases it (D21 l.778-780). |
| **Fix the order text.** D21 l.779 places `RESET` "between the error words and the run words", but the run words are gone (D23). | Lay out words 32 and up in one table: `ERROR_LINES_0`-`2`, `RESET`, `READY_LINES_0`-`1`, `READY_EXPECT_0`-`1`, `READY_CLEAR`, `PULSE_GO`, `WATCHDOG`, `LED`, then the UART and host words. |
| **Say that tiles' `EVENTS` [5] GO keeps its meaning** with GO now driven from C&C 1's peripheral. | A unit not wired to GO (C&C 2, C&C 3) never sees it. |
| **The unit-error bit at `ERRORS` [0]** (D15's proposal) and the error pin's exclusion of it (D24 l.905-907) should name the bit. | So the pin logic and the handler agree. |

### W3-29 — Alignment of auto-advancing runs (issue)

*D26 (l.949-961, l.975-978), D7, D10 (l.365-366, l.421-422); `meshsim/sw/tests/regs.c` l.40-42.*

D26 requires every 64- or 128-bit write to be naturally aligned, and says runs "must start aligned to their size". A run that *continues* can still misalign:

- **The send port.** `ROUTER_ADDR` advances by each packet's size, so a 32-bit send followed by a 64-bit send at the advanced address is misaligned. `regs.c` does exactly this (a 32-bit send to word 4, then a 64-bit send to word 5).
- **A peripheral's DMA.** A transfer sends 128-bit packets, then a 64-bit and/or 32-bit remainder, and "consecutive sends continue where the last stopped" (v1.1, S l.1093). A transfer whose word count isn't a multiple of 4 leaves `DMA_ADDR` misaligned for the next transfer's 128-bit packets.

*Suggested resolution:* add the software rule "every packet of a run is aligned to its own size", with its two consequences: send-port runs don't go down in size and back up without re-storing `ROUTER_ADDR`; a DMA transfer's count is a multiple of 4 unless it is the last into that buffer. meshsim already errors on the result.

### W3-30 — A misaligned read-port read: `ERRORS` [1] or [7]? (nit)

*D18 (l.676, l.682), D26 (l.957).*

D18 keeps [7] "read-port error", which in v1.1 includes a read not aligned to its size (S 8.3). D26 sends every misaligned big-SRAM access to [1]. A misaligned `SRAM_RD_ADDR_QUEUE` store fits both. Pick one; [7] keeps the read port's errors together.

### W3-31 — Stale text in the log (nit)

The register-area remnants. W1-28 lists the reset- and HALT-related ones.

| where | says | should say |
|---|---|---|
| l.17-18 (summary) | "a 2-bit counter" | 3-bit (D30) |
| l.25-26, l.218 | "the controller reaches only 0x00, 0x01 and 0xFF" | the controller also fetches and loads 0x02 (the boot ROM) |
| l.179 | "96 bits, 88 used" | 87 used |
| l.209-210 | the italic renumbering note splits D5's table | move the note below the table |
| l.222-224 | "the resident loader's address" | the boot ROM's role |
| l.359 | `ERRORS` "sticky until HALT … (one exception: 15)" | sticky until reset; the unit-error bit is live |
| l.362 | `EVENTS` "sticky events" | counted events (l.394-408) |
| l.420 | `DMA_HDR` laid out "exactly" as `STREAM_COUNT`, *d* included | the [24:17] block only; [31:25] is *d* in one and dst in the other (D12) |
| l.495 | "units per Q10" | bytes |
| l.543 | `ERROR_LINES` "leave 0x874-0x87C" | 0x874 is now `SRAM_RD_DEST_ADDR` |
| l.593 | "at chip reset and HALT → RUN" | at reset |
| l.632 | "[6] always refers to the run" | [7] |
| l.671 | "sticky until HALT, except [13:11]" | sticky until reset; [13:11] live |
| D11 0x814 | a hole left by `PULSE_GO` | fine as reserved; or close it up, since the addresses are still proposed |

### W3-32 — The trace word is a reserved word meshsim flags (nit)

*D29 (l.1037-1039), D10 (l.381).*

D29 moves the split's ISS-only `TRACE_REG` to 0xFFFFF9FC. D10 makes every reserved word "read 0, ignore stores; meshsim flags the access". Exempt that word in meshsim, or give it a name in the register table as a simulator-only word. On hardware a store there is then harmless.

### W3-33 — The 16-bit stream count against the full-size sort (issue)

*D30 (l.1051-1054), D4 (l.185); `sort/program/sw/local.c` l.33, l.63-64, l.79; `transfer.c` l.76-77; `config.c` l.22-26; `sort/skew/planner/skew.h` l.197; PN 1.1 (43 full-size runs, 64 × 65,536).*

At full size each tile holds n = 65,536 records: `BUF_X` is one 512 KB area of 64-bit records (`sort.h` l.9, l.12). Several streams use a count of n:

- the local sort's three passes (bucket from X and from Y, and the input pass);
- its gather stream;
- the direct, LB and planner streams' per-destination count `cnt`, which reaches n on skewed sets (the `all_equal` data set sends every record to one tile).

All of them exceed 65,535 by one or more. D30's answer, "longer moves are broken into smaller transfers", is right for the local passes, which can use two `STREAM` entries.

For the all-to-all streams it interacts with the group structure: a group holds at most 16 streams (`GROUP_HI_TM1`, 4 bits), and the direct schedule's tables are identical on every tile. The planner (`skew.h`) would have to cap each stream at 65,535 and add entries *(inferred: affects the TH and BT paths only on the most skewed sets)*.

*Suggested resolution:* confirm splitting as the rule; add a meshsim check for count overflow; and size the planner's change before the migration. Alternatively, keep the count at 17 bits: [16] is spare (D30 l.1051).

## 5. The replayer as peripheral 1

Under the standard window, with counted events and no DMA, the replayer would look like this. The bit and word assignments are my proposal:

| word or bit | v1.2 | v1.1 |
|---|---|---|
| `PARAMS` [7:0] | type 1 | `TILE_STATUS` [15:12] |
| `STATUS` [0] | run in progress | `periph_running` |
| `STATUS` [1] | always 0 (no DMA) | — |
| `ERRORS` (peripheral-defined bits) | bad write, queue overrun, invalid expansion | conditions 11-13 |
| `PULSE_RUN` | a pass | `TILE_PULSE` [2] |
| words 4, 8, 9 | reserved (no DMA) | sender, unattached |
| `EVENTS` [0] | run done, depth 1 | line 6, done |
| `EVENTS` [2]-[5] | code, read-log, write-log and expansion credits, depth 2 (W3-24) | lines 1-4 |
| `EVENTS` [6] | new expansion, depth 1, presented in word 33 | line 5, `PERIPH_STATUS_1` |
| words 32, 33 | rows run and next offset; the presented instruction | `PERIPH_STATUS_0`, `_1` |
| region 0x04 | its write map in byte offsets: windows at offset [22:20], 128-bit ports 16-byte aligned | mode 9, word addresses, addr[20:18] |

Its writes are counted by a write counter at K. A dropped write (overrun, bad write) still decrements, as v1.1 counted drops.

What it needs beyond W3-24:

- **Ownership rules restated for "idle"** in place of HALT (W1-30).
- **What happens to the pass on a trap.** An invalid expansion or queue overrun now traps the tile terminally, so "the pass stops as if it had ended" (S l.1577) only matters for meshsim.
- **The 4.19.9 loop rewritten:**
  - wake on the tile's [10];
  - claim with one load of `peripheral1.EVENTS`;
  - squelch by clearing the tile's `EVENTS_MASK` [10] while a refill streams;
  - wait on the refill's write counter;
  - re-check after every wake.

The draft struct for its window is in W3-27.

## 6. Migration inventory (updated)

**Sizes:** S small, M medium, L large. Copies are not listed again: `meshsim/sw/mesh.h` = `sw/mesh.h` and `meshsim/sw/ext/sort/` = `sort/program/sw/`, kept equal by `check_layout.sh`.

**Flags:**

| flag | meaning |
|---|---|
| **A** | relies on unaligned or spanning access (26) |
| **B** | relies on `ebreak` or `ecall` (20) |
| **H** | relies on HALT, the RUN/HALT nets or the loader-at-HALT boot (23) |
| **U** | uncounted traffic, or a counter choice now needed (17) |
| **P** | `PTR[64]` staging (17) |
| **C** | chunk- or word-unit packet addresses (5) |
| **N** | stream counts above 65,535 (30) |

The biggest mechanical risk is **U**: every send site now names a counter. A header macro that defaults the field to 0 turns sends into silent uncounted writes. Make the counter a required macro argument, and start the migration with a fixed 1:1 mapping of v1.1's four counters (1 big SRAM, 2 controller memory, 3 replies, 4 peripheral), which keeps every receiver's arming as it is.

### `wip/sw/`

| file | change | size | flags |
|---|---|---|---|
| `mesh.h` (generated) | The block structs (W3-27) and optionally per-register macros for the start path. New addresses, regions and byte addresses (`CM_WORD`, `ROUTER_HDR_ADDR` and `STREAM_DEST_WORD` go). Counter arguments. `PULSE_*`, `EVENTS_*` (new bits), `WRITE_COUNTER_1`-`4`. `wait_tables()` becomes an event helper plus a `rd_running` poll. `tile_index()` and `sram_chunks()` read `PARAMS`. `stage_base()`, `STAGE_CHUNKS` and `CMD_REPORT` go, and so do the UART helpers. | L | C, P, U |
| `REGISTERS.md` | Generated tables and the prose of sections 1-7. The 2.8.2 replayer example and the boot section are rewritten. | L | A, C, H, P |
| `CONVENTIONS.md` | s.2 addresses (bytes, regions), s.3 boot (ROM and marker), s.4 commands as mode 0, s.5 waiting (four counters, events), s.6/6a sending and modes, s.7 errors, s.8 block copy, s.9 read port. | M | C, H, U |
| `RECIPES.md` | Every recipe that sends or waits. s.15 rule 4 (row-aligned elements) becomes an alignment error, not a timing rule; rule 5 (`PTR[64]`) goes; s.20 REPORT goes; s.21 self-test. Synced with `rcp_*.c`. | L | A, P, U |
| `regdoc/src/*.py`, `regdoc/svg/*` | Five worked examples, regenerated. | M | C, U |

### `wip/sort/`

| file | change | size | flags |
|---|---|---|---|
| `program/sw/sort.ld` | Origins 0x01000000 and 0x00000000. Leave data word 0 free (start `.data` at 8). Drop the overlays (they fit in 32 KB). `.rodata` may move to program memory, now load-only. | M | — |
| `program/sw/loader.S` | **Goes**, or shrinks to the image's entry: the boot ROM jumps to the image (D22), though D20 l.755-756 still mentions the loader. Its `PTR[64]` set-up and `ebreak` go either way. | S | B, H, P |
| `program/sw/main.c` | `ov_load` goes with the overlays. `CMD_STAGE` becomes "staging lands uncounted by address, then the next command" (D17). `CMD_EXIT`'s return to crt0's `ebreak` needs meshsim's new exit. Counter choice. | M | B, P, U |
| `program/sw/local.c` | **Streams of count n = 65,536** (l.33, l.63-64, l.79) split in two. `PTR[64]` restore (l.44-47) goes. `wait_tables()` ×5. `TILE_PULSE` start. | M | N, P, U |
| `program/sw/exchange.c` | `xchg`: mode 0 to `REPLY` + 8·`me`, with `ROUTER_ADDR` stored once and no-increment set. `send_vector`: byte address instead of `CM_WORD`. Counters. | S | C, U |
| `program/sw/bt_plan.c` | Tokens and credits by byte address. `bt_descs` with no-increment. Counters. | M | C, U |
| `program/sw/bt_run.c` | Reply loop with no-increment; `wait_tables()` ×3; counters. | M | U |
| `program/sw/transfer.c`, `config.c` | `cnt` can reach 65,536 on skewed sets (W3-33). `wait_tables()`. Counters. | M | N, U |
| `program/sw/skew.c` | `wait_tables()` ×2, counters. | S | U |
| `program/sw/sort.h` | `KEY_MODE` still works with the new constants (1, 3). The staging comments (l.17-19) go. `CMD_EXIT`. | S | B, P |
| `program/build.sh`, `program/patches/*` | Single image; regenerate the patches. | S | — |
| `split/split.c` | Reply sends with no-increment (l.265, 288, 308, 345-363, 408). Counters. `TILE_PULSE` done in its 66 barriers. | M | C, U |
| `split/split_regs.h` | Every v1.1 address and mode; `CM_WORD` against 0x4000; `SR_REPLY` 0x5BC0; `ME()` from `TILE_STATUS`; `TRACE_REG` moves to 0xFFFFF9FC (W3-32). Better: include the generated header. | L | C |
| `skew/planner/skew.h`, `skew_*.c` | Stream counts capped at 65,535 (W3-33). The `Tile` type name clashes with the register type if that is called `Tile` (W3-27). | M | N |
| `skew/harness/*` | Host ISS: the sentinel address only. | S | — |

### `wip/meshsim/` software

| file | change | size | flags |
|---|---|---|---|
| `sw/crt0.S`, `sw/replay/crt0.S` | Stack at the top of the 32 KB data memory, below the reply block. The `ebreak` exit needs meshsim's replacement (D20 l.755-757). | S | B |
| `sw/loader.S`, `sw/loader.ld` | Go (boot ROM). meshsim needs a model of the ROM instead. | S | B, H, P |
| `sw/app.ld`, `sw/build.sh`, `sw/replay/replay.ld`, `build.sh` | New origins; data word 0 free. | S | — |
| `sw/replay/replay_regs.h` | Replace with the generated header (`ReplayerPeripheral`). | L | C |
| `sw/replay/replay_ctl.c` | The 4.19.9 loop on counted credits (W3-24, section 5); `PULSE_RUN`; region 0x04 byte addresses; `wait_ag_idle()` keeps polling `rd_running` (now allowed, D17); counters; `TILE_CONDITIONS` polls go (errors trap). | L | C, U |
| `sw/tests/regs.c` | The register map. **A 64-bit send to word 5 after a 32-bit send to word 4** (l.40-42), now misaligned. UART checks go. | M | A, C |
| `sw/tests/rcp_rules.c` | Rule 4's wrong form (`B + 3` at 64 bits, l.96) is now an alignment error. Rule 5 (`PTR[64]`, l.116-122) goes. `wait_tables()` ×9. | M | A, P |
| `sw/tests/rcp_bucket.c` | `PTR[64]` restore (l.51). | S | P |
| `sw/tests/cond.c` | Conditions are now trapping errors. REPORT. Polls of `mem_idle` and `rd_done` (l.48-49, 78, 93): `mem_idle` is gone. | M | U |
| `sw/tests/rcp_selftest.c`, `rcp.h` | REPORT; mode 8 to C&C (`rcp.h` l.99); counters. | M | C, U |
| `sw/tests/faults.c`, `search.c`, `hello.c`, `cmd.c`, `rcp_cmdloop.c` | `ecall`/`ebreak` faults and markers, the `CMD_EXIT` exit. | S each | B |
| `sw/tests/a2a.c`, `search.c`, `rcp_sync.c`, `rcp_cmem.c` | Copies into `&P` by `CM_WORD` at 64 bits: byte addresses, with the landing variable 8-byte aligned. | S each | A, C |
| `sw/tests/rdport.c` | Packed-word read-port setup; `STATUS` polls. | S | C |
| `sw/tests/` others (about 15) | Counter names, header stores, `wait_tables()`. | S each | U |

### `wip/meshsim/` simulator and tests

| file | change | size | flags |
|---|---|---|---|
| `src/Registers.java`, `src/RegDoc.java` | The register table and its generators: structs, header block, bit names (W3-27). | L | — |
| `src/Controller.java` | Reset vector 0x02000000 and a boot-ROM model. Traps, CSRs, `mtvec`. Load-only program memory. Strict alignment and out-of-bounds checks. `HALT_NAME`/halt states go; a new exit mechanism. | L | B, H |
| `src/CnC.java` | Becomes C&C 1 as a tile with its peripheral (D28): error, READY and reset words, `PULSE_GO`. The REPORT flow goes. Boot: images uncounted by address, then the marker. RUN/HALT goes. Staging by address. | L | C, H, P |
| `src/TileControl.java` | Register decode, pulses, `EVENTS` (D16), errors; UART and sender go. | L | H |
| `src/TrafficDirector.java` | Region decode, counter field and K line, bad-packet table (D26), "command landed" at port B; the halted paths (l.58-89) go. | L | H, U |
| `src/PeripheralSlot.java`, `Peripheral.java` | Up to three standard windows; counted and live events; live tile bits; landing within K. | L | H |
| `src/Replayer.java` | Its window (section 5), credits (W3-24), region-0x04 map in bytes. | M | C, H |
| `src/Sender.java` | Becomes the peripheral DMA with its own router port (D10 l.423-428). | M | C, H |
| `src/BigSRAM.java`, `src/WriteEngine.java` | **Spanning writes go** (l.17; l.18, 68, 99); alignment errors. | M | A |
| `src/AddressGenerator.java`, `src/SRAMReadQueue.java` | `STREAM` layouts and the 16-bit count; alignment of sources and `GROUP` sources; [7]'s timing and its clear on `PULSE_DMA`. | M | A, N |
| `src/ControllerMemories.java`, `src/LinkWord.java`, `src/ReadPort.java`, `src/GatherEngine.java` | 32 KB and byte addressing; 256-bit word and 3-bit counter; read-port destination; gather counter pass-through. | M each | C |
| `src/Uart.java` | Into C&C 1's peripheral. The per-tile console becomes the file-system logs (D23); meshsim may keep a print path. | S | — |
| `src/Tile.java`, `Chip.java`, `RequestBus.java`, `Fields.java`, `Conditions.java`, `Config.java` | Global reset in place of HALT; names. | S each | H |
| `test/MemTest.java`, `EngineTest.java` | Spanning-write tests are deleted; alignment-error tests added. | M | A |
| `test/SortTest.java`, `StageB.java` | Staging at `PTR[64]` (`SortTest` l.148, 305-330, 480); exit by `ebreak` (l.228); full-size counts (W3-33). | M | B, N, P |
| `test/RecipeTest.java` | Rule 5 (`PTR[64]`, l.215-220, 690-705); REPORT; `allEbreak`. | M | B, P |
| `test/CtrlTest.java` | Boot by loader (`PTR[64]` l.372); `allEbreak`; pulses; faults. | M | B, H, P |
| `test/RiscvTest.java`, `test/riscv/*`, `bin/` (144) | Relink at the new map. PASS/FAIL by `ebreak` needs another end (D20). CSR tests change (Q1). | M | B |
| `test/HooksTest.java`, `PeripheralSlotTest.java`, `SenderTest.java`, `TestPeripheral.java` | UART, the slot, the sender, the HALT ownership rules. | L | H |
| `test/CondTest.java`, `RequestBusTest.java`, `Mach.java` | REPORT, halts, `allEbreak`. | M each | B, H |
| `test/NetTest.java`, `AgTest.java`, `AgBench.java`, `ReadPortGatherTest.java`, `A2A.java`, `RegDocTest.java` | Mode numbers, counters, chunk addresses in `LinkWord.hdr`. | M each | C, U |
| `kit/`, `verify/`, `tools/` | Rebuild only *(inferred)*. | S | — |

**What the flags found:**

| flag | files |
|---|---|
| **A** | `regs.c` l.40-42; `rcp_rules.c` rule 4; meshsim's spanning model and its tests. No production code relies on misalignment; the sort's landing buffers are already 8-byte aligned (`exchange.c` l.26-27, `bt_plan.c` l.42). |
| **B** | crt0's exit; the loader's unknown-command `ebreak`; `faults.c`'s deliberate `ecall`; `search.c`'s `ebreak` markers; every Java test that checks `allEbreak`. |
| **H** | meshsim's boot and halted paths; the loader concept. |
| **U** | None in the code today, since v1.1 counted everything but commands. The risk is the default counter (above). |
| **P** | `loader.S`, `local.c`, `sort.h`, `mesh.h`, `rcp_bucket.c`, `rcp_rules.c`, `SortTest`, `RecipeTest`, `CtrlTest`, `Registers.java`. |
| **C** | Every `CM_WORD`, `PACKED_WORD` and `cmWord` site; `SRAM_RD_DEST` packed words; the replayer's word map. |
| **N** | The sort at full size (W3-33). |

## 7. Spec sections to update, and questions for the owner

### Spec sections

Additions to round 1's list:

| section | change |
|---|---|
| 4.12 | Reset values (W3-26). Counted and live events at the peripheral level. Stale-GO handling. |
| 4.17 | Counted events with depths, the item kind (W3-25), the gated credit (W3-24), the peripheral DMA's router port (D10 l.423). |
| 4.19.8-4.19.11 | Credits reset at `PULSE_RUN` and at each new expansion, the return rule, gating (W3-24); ownership for "idle" (W1-30). |
| 4.11, 8.1 | The struct header and the one-instruction rule (W3-27); alignment of runs (W3-29). |
| 4.3, 8.3 | Which bit reports a misaligned read-port read (W3-30). |
| 6.3-6.4 | The start path's cycle count if the struct header adds an instruction (W3-27). |
| 6.6 | C&C 1's window as revised (W3-28). |
| 4.2, 8.9 | The 16-bit count and splitting (W3-33). |

### Questions for the owner

1. Is a credit returned per half consumed or per refill landed? Should credit bits be gated by "more to send" (W3-24)?
2. Should counted events get v1.1's item kind, a depth per bit with a presented register that advances on consume (W3-25)?
3. Should the boot ROM consume a stale GO and clear "command landed" before jumping? And should C&C 1 get a READY clear without GO (W3-26, W3-28, W1-23)?
4. Accept one extra instruction on the start path, or have the generator keep per-register macros for it (W3-27)?
5. For D28: an expected-units mask, a W1C `UART_STATUS`, `PULSE_GO` restarting the watchdog, and `RESET` = 0 at power-on (W3-28)?
6. Split long streams, or keep a 17-bit count using the spare bit [16] (W3-33)?
