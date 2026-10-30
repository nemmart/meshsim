import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Generates sw/REGISTERS.md (v1.2), the programmer's register reference, from {@link Registers} and {@link LinkWord}:
 * the address map and the access rules, the controller's block (every register, its reset value and what a load returns,
 * the fields), the peripheral windows' standard words, the table window, the packet fields and modes, the command and
 * reply blocks. The spec (spec/machine-spec.md v1.2: 2.2, 3.1, 4.11-4.13, 8) is the source of truth; where the two
 * differ, this document is wrong. RegDocTest checks the file is this generator's output and that the simulator behaves
 * as each row says. {@code java RegDoc [path]} writes it (default ../sw/REGISTERS.md, else sw/REGISTERS.md).
 */
final class RegDoc {
    private RegDoc() {}

    /** One register as the document shows it: reset, a load's value, the function; null fields read "-". */
    record Row(Registers.Reg reg, String reset, String load, String function) {}
    static final List<Row> ROWS = new ArrayList<>();
    private static void row(Registers.Reg r, String reset, String load, String fn) { ROWS.add(new Row(r, reset, load, fn)); }
    static {
        row(Registers.PARAMS, "the unit's constant", "the constant", "peripherals present [2:0], endpoint id [26:20], big-SRAM size [31:27]");
        row(Registers.STATUS, "0 (the idle machine)", "the live bits", "diagnostic: rd_running, rd_done, GATHER busy, send_busy, done, command landed, request_busy");
        row(Registers.ERRORS, "0", "the errors", "sticky until the reset; [13:11] live (the peripherals')");
        row(Registers.PULSE_DONE, "-", "0", "store 1: the done pulse (wait_for_command())");
        row(Registers.PULSE_DMA, "-", "0", "store 1: start the run in DMA_CONFIG; clears EVENTS [10]; ignored while a run runs");
        row(Registers.EVENTS, "0 (sticky events)", "every pending event; consumes the sticky ones under EVENTS_MASK", "the events");
        row(Registers.EVENTS_PEEK, "-", "what EVENTS would return; consumes nothing", "the same, for looking");
        row(Registers.EVENTS_MASK, "[8] | [7] = 0x180", "the mask", "the events that wake WFI on their own and that a load of EVENTS consumes");
        row(Registers.EVENTS_REQUIRED, "0", "the value", "the events that wake WFI only all together");
        row(Registers.ROUTER_HDR, "undefined (meshsim 0)", "no-increment, counter, mode, dst; size and spare read 0", "the header of outgoing packets");
        row(Registers.ROUTER_ADDR, "undefined (meshsim 0)", "the address the next packet will use", "the next packet's 32-bit address; + 4, 8, 16 at each mode-0 send unless no-increment");
        row(Registers.ROUTER_SET_W0, "undefined", "0", "payload [31:0]; cleared by every send");
        row(Registers.ROUTER_SET_W1, "undefined", "0", "payload [63:32]; cleared by every send");
        row(Registers.ROUTER_SET_W2, "undefined", "0", "payload [95:64]; cleared by every send");
        row(Registers.ROUTER_SEND_32, "-", "0", "payload [31:0]; sends a 32-bit packet");
        row(Registers.ROUTER_SEND_64, "-", "0", "payload [63:32]; sends a 64-bit packet, ROUTER_SET_W0 below");
        row(Registers.ROUTER_SEND_128, "-", "0", "payload [127:96]; sends a 128-bit packet, ROUTER_SET_W0-W2 below");
        row(Registers.REQUEST_SET_W0, "undefined", "(the receive side: REQUEST_GET_W0)", "the request's [31:0]: target [9:7], requestor [6:0] written 0");
        row(Registers.REQUEST_SET_W1, "undefined", "(REQUEST_GET_W1)", "the request's [63:32]");
        row(Registers.REQUEST_SET_W2, "undefined", "(REQUEST_GET_W2)", "the request's [95:64]");
        row(Registers.REQUEST_SEND, "-", "(REQUEST_RECEIVE)", "the request's [127:96]; pushes it (stalls while the request FIFO is full)");
        row(Registers.REQUEST_GET_W0, "-", "the oldest received request's [31:0]; 0 when empty", "receive side; on tiles 0-63 always empty");
        row(Registers.REQUEST_GET_W1, "-", "its [63:32]; 0 when empty", "receive side");
        row(Registers.REQUEST_GET_W2, "-", "its [95:64]; 0 when empty", "receive side");
        row(Registers.REQUEST_RECEIVE, "-", "its [127:96], and pops it; empty: 0 and receive underflow (ERRORS [14])", "receive side");
        row(Registers.DMA_CONFIG, "undefined (meshsim 0)", "what was stored", "the run, the bucket and histogram digits, k");
        row(Registers.DMA_RECORD_BASE_ADDRESS, "undefined (meshsim 0)", "chunk address [20:0]", "the run's record buffer");
        for (int n = 1; n <= 7; n++) row(Registers.WRITE_COUNTER[n], "0", "the count [19:0]", "write counter " + n + ": a store adds N, N > 0 arms it");
        row(Registers.SRAM_RD_ADDR_QUEUE, "empty", "free entries (at least 64 when empty)", "store: queue a read at a chunk address [20:0], aligned to the size");
        row(Registers.SRAM_RD_DEST_ADDR, "undefined (meshsim 0)", "the next result's destination, as advanced, with the size and counter", "the read port's setup");
        row(Registers.SIM_EXIT, "-", "0", "simulator only: a store ends a meshsim test with the value");
        row(Registers.TRACE, "-", "0", "simulator only: a store is logged with the cycle and the unit");
    }
    static Row row(String name) { for (Row r : ROWS) if (r.reg.name.equals(name)) return r; throw new IllegalArgumentException(name); }

    static String hex(int v) { return String.format("0x%08X", v); }
    static String off(Registers.Reg r) { return String.format("0x%03X", r.off()); }
    static String acc(int a) { return a == Registers.R ? "R" : a == Registers.W ? "W" : "R/W"; }

    static String document() {
        StringBuilder b = new StringBuilder();
        b.append("""
# Unit registers: the programmer's reference (v1.2)

**v1.2 (`mesh.h` 1.2, meshsim v1.2).** Everything a controller program reaches with loads and stores: the address map,
the register block, the peripheral windows, the table window, and the packets and command words programs build.
`spec/machine-spec.md` v1.2 is the source of truth (2.2, 3.1, 4.11-4.13, 8); where the two differ, this document is
wrong. **This file is generated** by meshsim (`src/RegDoc.java`, from the register table in `src/Registers.java`,
which also generates `sw/mesh.h`); edit the generator, not the file. `test/RegDocTest.java` checks the simulator
behaves as every row says.

## 1. The address map

An address is an 8-bit region [31:24] and a 24-bit byte offset [23:0] (2.2). A C pointer is the packet address.

| address | contents | the controller | packets (mode 0) |
|---|---|---|---|
""");
        b.append(String.format("| `%s`-`%s` | data memory, 32 KB; the first 16 bytes reserved; the reply block at `0x%08X`, the command block at `0x%08X` | loads, stores (1 cycle) | yes |%n",
                hex(Registers.DMEM_BASE), hex(Registers.DMEM_BASE + Registers.DMEM_SIZE - 1), Registers.REPLY_BASE, Registers.COMMAND_BASE));
        b.append(String.format("| `%s`-`%s` | program memory, 32 KB | fetch, loads; a store is a bus error | yes |%n", hex(Registers.PMEM_BASE), hex(Registers.PMEM_BASE + Registers.PMEM_SIZE - 1)));
        b.append(String.format("| `%s`- | the boot ROM | fetch, loads | no (a bad packet) |%n", hex(Registers.BOOTROM_BASE)));
        b.append(String.format("| `%s`- | big SRAM: chunk *c* at `0x03000000 + 4c` | bus error (no port) | yes |%n", hex(Registers.REGION_SRAM)));
        b.append("| `0x04000000`-`0x06FFFFFF` | peripherals 1-3's regions | bus error | yes, if present |\n");
        b.append("| `0x07000000`-`0xFEFFFFFF` | unmapped | bus error | no |\n");
        b.append(String.format("| `%s`-`%s` | the table window (section 4) | word accesses; only `HIST` loads (1 cycle) | no |%n", hex(Registers.TABLE_BASE), hex(Registers.TABLE_END - 1)));
        b.append(String.format("| `%s`-`%s` | the controller's register block (section 2) | word accesses (loads 2 cycles) | no |%n", hex(Registers.REG_BASE), hex(Registers.REG_BASE + 4 * Registers.BLOCK_WORDS - 1)));
        for (int n = 1; n <= 3; n++) b.append(String.format("| `%s`-`%s` | peripheral %d's window (section 3) | word accesses (2 cycles); absent: bus error | no |%n", hex(Registers.windowBase(n)), hex(Registers.windowBase(n) + 0x1FF), n));
        b.append("| the rest of region 0xFF | unmapped | bus error | no |\n");
        b.append("""

**Access rules** (4.11, 8.1):
- **Bus errors** (`ERRORS` [9]): a store to program memory or the boot ROM; a misaligned access; an unmapped address,
  an absent peripheral's window included; a sub-word access to the block, a window or the table window; a store to a
  read-only register; a load from `PTR`, the `STREAM` arrays or `GROUP`.
- **Write-only registers read 0.** The request port's four words are the exception: a load reads the receive side.
- **Reserved words** in the block and the windows read 0 and ignore stores (meshsim flags the access). Reserved and
  spare bits are ignored on a store and read 0.
- **An offset past the end of data or program memory is not checked**: the high bits are ignored and the access
  aliases (meshsim flags it). meshsim also flags any write into data memory's first 16 bytes.
- **Timing:** a load from the block or a window takes 2 cycles and returns the state at the end of its first cycle,
  taking any side effect (a consuming load of `EVENTS`, a pop of `REQUEST_RECEIVE`) at the end of its second; a load from
  the memories or the table window takes 1. Stores are single-cycle and posted, taking effect at the end of their
  cycle. Only a send store on an occupied send holding register and a `REQUEST_SEND` on a full request FIFO stall.
- **Pulse registers:** a store of 1 fires the action for one cycle, a store of 0 does nothing; a load reads 0.

## 2. The controller's register block

One per unit, identical on every unit, at `0xFFFFF800`; register *i* at `0xFFFFF800 + 4i`, reached from `x0` (the
offset below is the address's low 12 bits). In C: `tile->NAME` (`TileRegs`, `mesh.h`).

| offset | register | access | reset | a load returns | function |
|---|---|---|---|---|---|
""");
        for (Row r : ROWS) {
            String o = r.reg.name.startsWith("REQUEST_GET") || r.reg.name.equals("REQUEST_RECEIVE") ? off(r.reg) + " (load)" : off(r.reg);
            b.append(String.format("| `%s` | `%s` | %s | %s | %s | %s |%n", o, r.reg.name, acc(r.reg.access), r.reset, r.load, r.function));
            if (r.reg == Registers.PULSE_DMA) b.append("| `0x814` | reserved (was `PULSE_GO`, now C&C 1's peripheral) | | | 0 | |\n");
            if (r.reg == Registers.SRAM_RD_DEST_ADDR) b.append("| `0x884`-`0x9F4` | reserved | | | 0 | stores ignored (meshsim flags them) |\n");
        }
        b.append("\n### 2.1 `PARAMS` and `STATUS`\n\n| register | bits | field |\n|---|---|---|\n");
        for (Registers.Field f : Registers.FIELDS) if (f.word.equals("PARAMS")) b.append(fieldRow("PARAMS", f));
        for (int i = 0; i < Registers.ST_NAME.length; i++) b.append(String.format("| `STATUS` | [%d] | `%s` (`ST_%s`) |%n", i, Registers.ST_NAME[i].toLowerCase(), Registers.ST_NAME[i]));
        b.append("""

`PARAMS` [2:0] is 0b001 on tiles 0-63 (the replayer) and on C&C 1-3; the endpoint id is 0-63 on a mesh tile and 64-66
on C&C 1-3; the size is 15 at the default 4 MB (`sram_chunks()` = (size + 1) << 16). `STATUS` is diagnostic only: no
program needs to poll it; programs wait by events.

### 2.2 `ERRORS`

Any non-zero bit raises the unit's hard interrupt and its error line (4.13). In a correct program on correct hardware
`ERRORS` reads 0 forever.

| bit | `mesh.h` | error |
|---|---|---|
""");
        String[] en = { "ERR_ROUTER_OVF", "ERR_BAD_PACKET", "ERR_UNEXPECTED", "ERR_DRIFT", "ERR_WE_IN_OVF", "ERR_TABLE_OVF", "ERR_GATHER_OVF",
                "ERR_READ_PORT", "ERR_DONE_EARLY", "ERR_BUS", "ERR_ILLEGAL", "ERR_PERIPH(1)", "ERR_PERIPH(2)", "ERR_PERIPH(3)", "ERR_RECV_UNDERFLOW" };
        String[] ed = { "router overflow: a link, eject or fifth-port FIFO overflowed", "bad packet or misaligned access: a bad packet dropped, a write through a misaligned `PTR` dropped (still counted), or a misaligned read not performed (payload 0)",
                "unexpected arrival: a decrement found its counter at 0", "drift: in a group run the SRAM read queue refused the address generator",
                "write-engine input overflow", "table request overflow (a hardware fault)", "GATHER queue overflow (a hardware fault)",
                "read-port error: a `SRAM_RD_ADDR_QUEUE` store with no free entry (the read is dropped)", "done early: `done` with a write counter non-zero",
                "bus error", "illegal instruction", "peripheral 1 (live: its `ERRORS` non-zero)", "peripheral 2 (live)", "peripheral 3 (live)",
                "receive underflow: `REQUEST_RECEIVE` on an empty queue" };
        for (int i = 0; i < en.length; i++) b.append(String.format("| %d | `%s` | %s |%n", i, en[i], ed[i]));
        b.append("""

Bits 0-10 and 14 are sticky until the reset; 11-13 follow the peripherals. [31:15] read 0. A store is a bus error.

### 2.3 The events: `EVENTS`, `EVENTS_PEEK`, `EVENTS_MASK`, `EVENTS_REQUIRED`

| bit | `mesh.h` | event | kind |
|---|---|---|---|
| [6:0] | `EV_CTR(n)` | write counter *n* done (bit *n* - 1): its count reached 0, armed since the event last set or cleared | sticky |
| [7] | `EV_CTRS_DONE` | counters done: all seven at 0, one armed since it last fired | sticky |
| [8] | `EV_GO` | GO, C&C 1's barrier release (mesh tiles only) | sticky |
| [9] | `EV_COMMAND` | command landed: a director write covering command slot 0 | sticky |
| [10] | `EV_RUN_DONE` | DMA run done: `rd_done` rose; cleared by `PULSE_DMA` | sticky |
| [11] | `EV_REQ_ROOM` | request FIFO no longer full: `request_busy` fell | sticky |
| [12] | `EV_REQ_PENDING` | request pending: the unit's receive queue holds a request (C&C 1-3 only) | live |
| [15:13] | `EV_PERIPH(n)` | peripheral *n*: its `EVENTS & EVENTS_MASK` non-zero | live |

- **A load of `EVENTS`** returns every pending event and clears exactly the returned sticky bits that are in
  `EVENTS_MASK`; an event set after its sample point survives. `EVENTS_PEEK` returns the same and clears nothing.
- **The wake term:** `wake = gate ? EVENTS[8] : ((EVENTS & EVENTS_MASK & ~EVENTS_REQUIRED) != 0) OR (EVENTS_REQUIRED != 0
  AND (EVENTS & EVENTS_REQUIRED) == EVENTS_REQUIRED)`. A wake consumes nothing: `wfi()` loads `EVENTS` after `WFI`.
- **The done gate:** from the `done` pulse until GO only GO wakes `WFI`, and the counters' done events can't be set.
  `done` clears [7] and [6:0]. Keep [8] in `EVENTS_MASK` whenever `wait_for_command()` runs.
- **A counter write** of *N* > 0 clears [7] and that counter's own done event and arms both.

### 2.4 The common header layout

Every register that carries a packet's header fields lays them out in [31:17], in the link word's order:

| bits | field | `mesh.h` |
|---|---|---|
""");
        for (Registers.Field f : Registers.FIELDS) if (f.word.equals("HDR")) b.append(String.format("| [%d:%d] | %s | `HDR_%s()` |%n", f.lo + f.width - 1, f.lo, f.doc, f.name));
        b.append("""

Used by `ROUTER_HDR` (with no-increment [0]), `STREAM_COUNT` (count [16:0]; [31:25] is the relative *d*),
`SRAM_RD_DEST_ADDR` (destination [16:0]; size and counter) and a peripheral's `DMA_HDR` (count [11:0], queue [13:12]).

### 2.5 The send port

`ROUTER_HDR` (no-increment [0], counter, mode, dst) and `ROUTER_ADDR` set the next packets' header and address; then one
store per 32 bits of each packet, in ascending word order, the last store sending: `ROUTER_SEND_32` alone,
`ROUTER_SET_W0` + `ROUTER_SEND_64`, or `ROUTER_SET_W0`-`W2` + `ROUTER_SEND_128`. At each send `ROUTER_ADDR` advances by
4, 8 or 16 for mode 0 unless no-increment is set (modes 1-5 leave it); `ROUTER_SET_W0`-`W2` clear. The router stamps src.
A send store stalls only if the holding register is full and not being injected that cycle. The send port checks
nothing: the receiver and meshsim do.

### 2.6 `DMA_CONFIG` and the write counters

| bits | field |
|---|---|
""");
        for (Registers.Field f : Registers.FIELDS) if (f.word.equals("DMA_CFG")) b.append(String.format("| [%d:%d] | %s |%n", f.lo + f.width - 1, f.lo, f.doc));
        b.append("""

`DMA_CONFIG` is written once per run, before the start, and holds while the run's arrivals land. The write counters
(`WRITE_COUNTER_1`-`7`, `tile->WRITE_COUNTER[n - 1]`) are 20 bits; a store of *N* adds *N* atomically with that cycle's
decrement; a sum past 2^20 - 1 wraps (meshsim flags it); the count returns to 0 only by counting down or at the reset.
A packet's counter field names one; the traffic director decrements it K cycles after accepting the packet, so a
counter at 0 means everything it counted is in place.

### 2.7 The read port

A store to `SRAM_RD_ADDR_QUEUE` queues a read of 32, 64 or 128 bits at a chunk address aligned to the size, with
`SRAM_RD_DEST_ADDR`'s destination (a data-memory byte offset [16:0]), size [18:17] and counter [21:19]; the
destination then advances by the size in bytes. Each result lands as a mode-0 packet to the tile itself, counted on
that counter. A misaligned read isn't performed (its result lands as 0, `ERRORS` [1]); a store with no free entry is
dropped (`ERRORS` [7]). `RD_DEST(byte_off, size, ctr)` builds the setup word.

### 2.8 The request port

Software stores the request's words 0-2 (`REQUEST_SET_W0`-`W2`), then `REQUEST_SEND` (bits [127:96]) pushes the
128-bit request into the 128-entry request FIFO. Word 0 carries the target [9:7] (0 C&C 1, 1 C&C 2, 2 C&C 3; 3-7
dropped at the root) and 0 in the requestor [6:0], which the tree stamps. The receive queues are in the request tree
(128, 256 and 128 entries); C&C 1-3 read theirs through `REQUEST_GET_W0`-`W2` and `REQUEST_RECEIVE`, which pops. On
tiles 0-63 the receive side reads an empty queue: `REQUEST_RECEIVE` returns 0 and sets receive underflow.

### 2.9 The simulator-only words

`SIM_EXIT` (`0x9F8`): a store ends a meshsim test with the stored value (`sim_exit(v)`; `crt0` stores `main`'s return
value). `TRACE` (`0x9FC`): a store is logged with the cycle and the unit (`sim_trace(v)`). On hardware both are reserved
words.

## 3. The peripheral windows

Peripheral *n* (1-3) has a 128-word window at `0xFFFFF800 + 0x200n`, if present (`PARAMS` [*n* - 1]); an absent
peripheral's window is a bus error. Words 0-9 are standard, 10-31 reserved standard words, 32-127 the peripheral's own.
In C: `peripheral(n)->NAME` (`PeripheralStd`), `replayer->NAME`, `cc1->NAME`.

| word | name | access | function |
|---|---|---|---|
""");
        String[] sd = { "[7:0] type, [15:8] version", "[0] run in progress, [1] DMA in progress", "sticky until the reset", "store 1: start a run; ignored while one runs",
                "store 1: start the DMA set up in DMA_HDR and DMA_ADDR", "a load returns the events and consumes one count from each set bit in EVENTS_MASK",
                "what a load of EVENTS would return", "the events that count: wake the unit's EVENTS bit, are consumed; 0 at reset",
                "the DMA: count [11:0], queue [13:12], counter, mode, dst", "the DMA's 32-bit destination, advancing per packet" };
        for (int i = 0; i < Registers.STD_WORDS; i++) b.append(String.format("| %d | `%s` | %s | %s |%n", i, Registers.STD_WORD[i], acc(Registers.STD_ACCESS[i]), sd[i]));
        b.append("""

The replayer (type 1, every mesh tile's peripheral 1): `ROWS_RUN` (32), `EXPANSION_OFFSET` (33), `EXPANSION_OPCODE`
(34). C&C 1's peripheral: `ERROR_LINES[3]` (32-34), `READY_LINES[2]` (35-36), `READY_EXPECT[2]` (37-38), `PULSE_GO`
(39), `WATCHDOG` (40), `LED` (41), and the UART: `UART_TX` (42), `UART_RX` (43), `UART_DIVISOR` (44), `UART_STATUS`
(45). Spec 4.17, 4.19, 8.11-8.14 give their meanings.

## 4. The table window

""");
        b.append(String.format("""
| table | addresses | entries | load | store |
|---|---|---|---|---|
| `HIST` | `%s`-`%s` | 256 words; count [16:0] | the count | the count |
| `PTR` | `%s`-`%s` | 256 words; chunk address [20:0] | bus error | the pointer |
| `STREAM_SRC` | `%s`-`%s` | 256 words; source chunk [20:0], gap after [23:21] | bus error | the word |
| `STREAM_DEST` | `%s`-`%s` | 256 words; the first element's 32-bit address (mode 0 only) | bus error | the word |
| `STREAM_COUNT` | `%s`-`%s` | 256 words; count [16:0], read size, counter, mode, *d* [31:25] | bus error | the word |
| `GROUP` | `%s`-`%s` | 64 entries, low word then high word | bus error | one word |
""", hex(Registers.HIST_BASE), hex(Registers.PTR_BASE - 1), hex(Registers.PTR_BASE), hex(Registers.STREAM_SRC_BASE - 1),
                hex(Registers.STREAM_SRC_BASE), hex(Registers.STREAM_DEST_BASE - 1), hex(Registers.STREAM_DEST_BASE), hex(Registers.STREAM_COUNT_BASE - 1),
                hex(Registers.STREAM_COUNT_BASE), hex(Registers.GROUP_BASE - 1), hex(Registers.GROUP_BASE), hex(Registers.TABLE_END - 1)));
        b.append("""

`GROUP[g]` low: R [17:0]; high: start stream [7:0], *t* - 1 [11:8], gap [19:12]. In C: `tables->HIST[i]`,
`set_stream(i, STREAM_SRC_W(chunk, gap), dest, STREAM_COUNT_W(n, size, ctr, mode, d))`, `set_group(g, lo, hi)`.
**When the controller may touch them** (4.10): `STREAM` and `GROUP` only while the address generator isn't running (its
lead-in included); `PTR` and `HIST` only while no arrivals use them. *d*: 0-63 a relative tile, (me + *d*) mod 64;
64-66 C&C 1-3; 126 the broadcast (controllers only); mode 4 needs *d* = 0.

## 5. Packets

The 256-bit link word: payload [127:0], address [159:128], size [161:160], counter [164:162], mode [167:165], src
[174:168], dst [181:175]; [255:182] reserved. Sizes: 0 32 bits, 1 64, 2 128 (3 undefined); a payload's bits above its
size are 0. Every access is naturally aligned to its size.

| mode | `mesh.h` | what the receiver does | counted |
|---|---|---|---|
| 0 | `MODE_WRITE` | writes the payload at the address: data or program memory, big SRAM, a present peripheral's region | by its counter field |
| 1 | `MODE_SRC` | big SRAM at `PTR[src]`, then `PTR[src]` += 1, 2 or 4 chunks | by its counter field |
| 2 | `MODE_BUCKET` | big SRAM at `PTR[digit]` (`DMA_CONFIG`'s bucket digit) | by its counter field |
| 3 | `MODE_AUGMENT` | a 64-bit record at `PTR[src]`: the payload's low word and the ordinal (32 or 64 bits only) | by its counter field |
| 4 | `MODE_GATHER` | a gather request, to the tile itself only; its counter field passes to the values | never |
| 5 | `MODE_COUNT` | nothing written | by its counter field |
| 6-7 | | reserved: discarded (meshsim flags them) | no |

**Bad packets** are dropped, uncounted, and set `ERRORS` [1]: a mode-0 packet not aligned to its size or for a region
a packet can't reach (the boot ROM, 0x07-0xFF, an absent peripheral's), and a 128-bit mode 3. Counter field 0 is
uncounted: only commands and boot overlays, from C&C 1-3, travel uncounted.

## 6. The command block, the reply block and the command word

- **The command block:** 16 words at `0x00007FC0`; slot 0 the command word, slots 1-15 its arguments: even commands
  use slots 1-7, odd ones 8-15. C&C 1-3 write the arguments, then the command word, as uncounted mode-0 writes; a write
  covering slot 0 sets command landed (`EVENTS` [9]). `COMMAND[s]` in C.
- **The command word:** [31:24] type, [23] parity (the argument block), [22:0] argument: `CMD_WORD(type, parity, arg)`,
  `CMD_TYPE(w)`, `CMD_ARG(w)`, `CMD_ARGS(w)`. `CMD_JUMP` (0x4A): jump to the address in the argument block's first slot.
- **The reply block:** 256 words below it, at `0x00007BC0`: two words per source, `REPLY_WORD(src)` = `0x00007BC0 + 8 src`.
  A reply is a 32- or 64-bit mode-0 write at the sender's own pair; a sender sets `ROUTER_ADDR` once with no-increment
  and changes only the dst. Every request may name its own reply address instead.
- **The stack** grows down from `0x00007BC0`; images place data from `0x00000010`.

## 7. Common sequences (`mesh.h`'s helpers)

- `wait_for_command()`: the done pulse and `WFI`, back to back; GO wakes it; it consumes GO and returns slot 0.
- A local run: `set_stream`/`set_group`, `tile->DMA_CONFIG`, arm the counter (`tile->WRITE_COUNTER[c - 1] = N`), then
  `start_run()`, then `wfi()` for counters done.
- A block copy into controller memory: a mode-0 stream to the tile itself (*d* = 0) at the destination address, counted;
  `wfi()`.
- Several events at once: `wait_all(EV_CTR(2) | EV_RUN_DONE)`.
- A reply: `send_to(ROUTER_HDR_W(dst, MODE_WRITE, ctr) | ROUTER_NOINC, REPLY_WORD(me))`, then `send32(v)` per receiver
  (changing `tile->ROUTER_HDR` for each).
""");
        return b.toString();
    }
    static String fieldRow(String word, Registers.Field f) {
        return String.format("| `%s` | [%d:%d] | %s (`%s_%s`) |%n", word, f.lo + f.width - 1, f.lo, f.doc, word, f.name);
    }

    public static void main(String[] args) throws IOException {
        Path out = args.length > 0 ? Path.of(args[0]) : Registers.doc("REGISTERS.md");
        String d = document();
        if (Files.exists(out) && Files.readString(out).equals(d)) { System.out.println(out + " unchanged"); return; }
        Files.writeString(out, d);
        System.out.println("wrote " + out);
    }
}
