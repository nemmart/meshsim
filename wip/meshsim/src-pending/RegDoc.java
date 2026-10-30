import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The generated tables of REGISTERS.md, the programmer's register reference. Names, offsets, fields and access come
 * from {@link Registers} (which also generates mesh.h); {@link #PREFIX} maps each multi-field register to its field
 * macros' prefix.
 *
 * Everything else a reference needs and mesh.h doesn't carry (diagram labels, reset values, what a load returns, the
 * widths of the one-field registers, the packet modes) is here, and RegDocTest checks it against the simulator.
 * REGISTERS.md holds blocks {@code <!-- generated: ID -->} ... {@code <!-- end generated -->}; the prose around them is
 * hand-written. {@code java RegDoc [path]} rewrites every block in place (default: ../sw/REGISTERS.md, else sw/); {@code --check} reports blocks that
 * differ, are missing or are unknown, and exits 1 if any do.
 */
final class RegDoc {
    private RegDoc() {}

    // ================================================================ the registers
    /** Always true since mesh.h 1.0 (kept for the generator's branches that show mesh.h names). v1.1: mesh.h 1.1. */
    static final boolean DIRECT = true;

    /** A register as this document shows it: name, address, access, and the simulator register behind it. */
    record Reg(String name, int addr, int access, List<Registers.Reg> sim) {}
    static final List<Reg> REGS = new ArrayList<>();
    static {
        for (Registers.Reg r : Registers.ALL) REGS.add(new Reg(r.name, r.addr(), r.access, List.of(r)));
    }
    static Reg reg(String name) {
        for (Reg r : REGS) if (r.name.equals(name)) return r;
        throw new IllegalArgumentException("unknown register " + name);
    }
    /** Text from Registers (field meanings), as is. */
    static String v1(String s) { return s; }

    // ================================================================ the conditions
    /** The conditions in bit order (Conditions index of bit i). */
    static final List<Integer> COND = new ArrayList<>();
    static { for (int i = 0; i < Conditions.N; i++) COND.add(i); }
    /** Kinds (spec 4.13): P a program error, H a hardware-fault detector (Conditions.KIND). */
    static String condKind(String cname) { for (int i = 0; i < Conditions.N; i++) if (Conditions.CNAME[i].equals(cname)) return Conditions.KIND[i]; throw new IllegalArgumentException(cname); }
    /** When each condition is set (spec 4.13's table, shortened). */
    static final Map<String, String> COND_WHEN = Map.ofEntries(
        Map.entry("DRIFT", "in a group run, the SRAM read queue refused an address-generator request: the tile fell behind its timetable"),
        Map.entry("ROUTER_OVF", "a link, eject or fifth-port FIFO of this tile's router overflowed"),
        Map.entry("WE_IN_OVF", "a mode-2, 3, 4 or 5 packet found the write engine's input FIFO full"),
        Map.entry("READ_PORT", "a `SRAM_RD_ADDR_QUEUE` store found no free entry, or its address wasn't aligned to the read size; the read is dropped"),
        Map.entry("UNEXPECTED", "a counted packet arrived for a down-counter already at 0 (in RUN)"),
        Map.entry("TABLE_OVF", "the `PTR` or `HIST` request FIFO overflowed"),
        Map.entry("BUS_ERROR", "a load or store broke the access rules (section 1); the controller halts"),
        Map.entry("ILLEGAL", "a divide opcode or a CSR instruction; the controller halts"),
        Map.entry("GATHER_OVF", "a mode-1 packet found the GATHER engine's 16-entry request queue full"),
        Map.entry("BAD_MODE_OR_SIZE", "a bad packet arrived (mode 0 or 10-15, mode 5 at 128 bits, a misaligned mode 3, mode 7 above 32 bits, mode 8 at 128 bits); it was dropped, uncounted"),
        Map.entry("DONE_EARLY", "done was pulsed while a down-counter was non-zero"),
        Map.entry("BAD_PERIPH_WRITE", "the peripheral's write port dropped a mode-9 packet (an address, size or ownership it doesn't take); every mode-9 packet on a tile with no peripheral"),
        Map.entry("PERIPH_2", "defined by the peripheral; the replayer: queue overrun"),
        Map.entry("PERIPH_3", "defined by the peripheral; the replayer: invalid expansion (the pass stops)"),
        Map.entry("RECV_UNDERFLOW", "a `REQUEST_RECEIVE` load found the receive queue empty (on tiles 0-63, every one)"));

    // ================================================================ words and their fields
    /** One field of a 32-bit word. reset: the value at chip reset ("undef" = undefined, "—" = no state). */
    record DF(int lo, int width, String label, String reset, String macro, String doc) {
        int hi() { return lo + width - 1; }
    }
    /** A 32-bit word: fields, a name for its unused bits ("spare" or "reserved"), and ranges drawn as one box. */
    record Word(String id, List<DF> fields, String gapName, List<int[]> collapse, List<String> collapseLabel) {}

    static final String UNDEF = "undef", NONE = "—";
    /** This document's word ids -> the field prefixes in Registers (mesh.h). mesh.h 1.0 may rename the prefixes: update here. */
    static final Map<String, String> PREFIX = Map.ofEntries(Map.entry("DMA_CONFIG", "DMA_CFG"), Map.entry("TILE_PULSE", "TILE_PULSE"), Map.entry("TILE_STATUS", "TILE_ST"),
            Map.entry("ROUTER_SET_HDR", "ROUTER_HDR"), Map.entry("STREAM_SRC", "STREAM_SRC"), Map.entry("STREAM_DEST", "STREAM_DEST"), Map.entry("STREAM_COUNT", "STREAM_COUNT"),
            Map.entry("GROUP_LO", "GROUP_LO"), Map.entry("GROUP_HI", "GROUP_HI"), Map.entry("SRAM_RD_DEST_ADDR", "SRAM_RD_SETUP"), Map.entry("SENDER_SEND", "SENDER"),
            Map.entry("UART_RX", "UART_RX"), Map.entry("UART_STATUS", "UART_ST"));
    static final Map<String, Word> WORDS = new LinkedHashMap<>();

    // ---- one-field registers and table entries: widths not in mesh.h (RegDocTest checks them against the simulator)
    static final int BASE_WIDTH = 21, COUNTER_WIDTH = 20, RDQ_ADDR_WIDTH = 21, RD_DEST_WIDTH = 14, HIST_WIDTH = 17, PTR_WIDTH = 21, SENDER_ADDR_WIDTH = 21, DIVISOR_WIDTH = 16;

    private static String lower(String s) { return s.toLowerCase().replace('_', ' '); }
    private static List<DF> fromRegisters(String word, Map<String, String> labels, Map<String, String> resets, String defReset) {
        String prefix = PREFIX.get(word);
        List<DF> out = new ArrayList<>();
        for (Registers.Field f : Registers.FIELDS) {
            if (!f.word.equals(prefix)) continue;
            out.add(new DF(f.lo, f.width, labels.getOrDefault(f.name, lower(f.name)), resets.getOrDefault(f.name, defReset),
                    f.word + "_" + f.name, v1(f.doc)));
        }
        if (out.isEmpty()) throw new IllegalStateException("RegDoc: no fields with prefix " + prefix + " for " + word + " (update RegDoc.PREFIX for mesh.h 1.1)");
        return out;
    }
    private static void word(String id, List<DF> fields, String gap) { WORDS.put(id, new Word(id, fields, gap, List.of(), List.of())); }
    private static DF one(int width, String label, String reset, String doc) { return new DF(0, width, label, reset, NONE, doc); }

    static {
        List<DF> st = new ArrayList<>(fromRegisters("TILE_STATUS", Map.of("ID", "endpoint id", "SIZE", "size-1", "PERIPH_TYPE", "type"),
                Map.ofEntries(Map.entry("RD_RUNNING", "0"), Map.entry("RD_DONE", "0"), Map.entry("SEND_BUSY", "0"),
                        Map.entry("TS_BUSY", "0"), Map.entry("MEM_IDLE", "1"), Map.entry("CM_IDLE", "1"), Map.entry("DONE", "1"),
                        Map.entry("LANDED", "0"), Map.entry("GATHER_BUSY", "0"), Map.entry("PERIPH_RUNNING", "0"), Map.entry("SENDER_BUSY", "0"),
                        Map.entry("REQUEST_BUSY", "0"), Map.entry("PERIPH_TYPE", "1 (tiles 0-63)"), Map.entry("ID", "the tile's id"), Map.entry("SIZE", "device")), "0"));
        st.sort((a, b) -> Integer.compare(a.lo, b.lo));
        WORDS.put("TILE_STATUS", new Word("TILE_STATUS", st, "spare", List.<int[]>of(new int[] { Registers.ST_REQUEST_BUSY.lo, 0 }), List.of("live bits (table)")));
        List<DF> cond = new ArrayList<>();
        for (int b = 0; b < COND.size(); b++) {
            int i = COND.get(b); String c = Conditions.CNAME[i];
            cond.add(new DF(b, 1, Conditions.NAME[i], "0", "TILE_COND_" + c, "**" + condKind(c) + "** " + COND_WHEN.get(c)));
        }
        WORDS.put("TILE_CONDITIONS", new Word("TILE_CONDITIONS", cond, "reserved", List.<int[]>of(new int[] { COND.size() - 1, 0 }), List.of("conditions (table)")));
        word("ROUTER_SET_HDR", fromRegisters("ROUTER_SET_HDR", Map.of(), Map.of(), UNDEF), "spare");
        word("ROUTER_SET_W0", List.of(one(32, "payload [31:0]", UNDEF, "the next packet's payload bits 31-0 (64- and 128-bit sends); 0 after each send")), "spare");
        word("ROUTER_SET_W1", List.of(one(32, "payload [63:32]", UNDEF, "the next packet's payload bits 63-32 (128-bit sends); 0 after each send")), "spare");
        word("ROUTER_SET_W2", List.of(one(32, "payload [95:64]", UNDEF, "the next packet's payload bits 95-64 (128-bit sends); 0 after each send")), "spare");
        word("ROUTER_SEND_32", List.of(one(32, "payload [31:0]", NONE, "the packet's payload bits 31-0: the store sends a 32-bit packet")), "spare");
        word("ROUTER_SEND_64", List.of(one(32, "payload [63:32]", NONE, "the packet's payload bits 63-32: the store sends a 64-bit packet, `ROUTER_SET_W0` below")), "spare");
        word("ROUTER_SEND_128", List.of(one(32, "payload [127:96]", NONE, "the packet's payload bits 127-96: the store sends a 128-bit packet, `ROUTER_SET_W0`-`W2` below")), "spare");
        word("DMA_CONFIG", fromRegisters("DMA_CONFIG", Map.of("KIND", "kind", "START", "start", "COUNT", "count", "BUCKETDIG", "bucket digit",
                "HISTDIG", "hist digit", "KM1", "k-1"), Map.of(), UNDEF), "spare");
        word("TILE_PULSE", fromRegisters("TILE_PULSE", Map.of("PERIPH_RUN", "periph run", "GO", "GO"), Map.of(), NONE), "spare");
        word("COUNTER", List.of(one(COUNTER_WIDTH, "count", "0", "store: N is added to the count (0 adds nothing; N > 0 arms it); load: the count")), "spare");
        word("DMA_RECORD_BASE_ADDRESS", List.of(one(BASE_WIDTH, "chunk address", UNDEF, "the run's record buffer: augment's landing buffer or gather's value buffer")), "spare");
        word("RDQ_ST", List.of(one(RDQ_ADDR_WIDTH, "chunk address", NONE, "the big-SRAM chunk to read: 1, 2 or 4 words by the setup register's size, aligned to it")), "spare");
        word("RDQ_LD", List.of(one(32, "free entries", "depth", "free entries in the request queue (at least 64 when empty)")), "spare");
        word("SRAM_RD_DEST_ADDR", fromRegisters("SRAM_RD_DEST_ADDR", Map.of("DEST", "destination", "SIZE", "size"), Map.of(), UNDEF), "spare");
        List<DF> wake = new ArrayList<>();
        String[] evDoc = { "`DMA_SRAM_REMAINING` done (sticky)", "`DMA_CM_REMAINING` done (sticky)", "`DMA_REPLY_REMAINING` done (sticky)", "`DMA_PERIPH_REMAINING` done (sticky)" };
        String[] evLbl = { "sram", "cm", "reply", "periph" };
        for (int i = 0; i < 4; i++) wake.add(new DF(i, 1, evLbl[i], "0", "WAKE_" + Registers.EV_NAME[i], evDoc[i]));
        wake.add(new DF(4, 4, "reserved", "0", NONE, "reserved, read 0"));
        wake.add(new DF(Registers.EV_GO, 1, "GO", "0", "WAKE_GO", "GO (sticky): C&C's barrier release"));
        wake.add(new DF(Registers.EV_COUNTERS_DONE, 1, "ctrs", "0", "WAKE_COUNTERS_DONE", "counters done (sticky): every counter done, the writes landed"));
        wake.add(new DF(Registers.EV_COMMAND, 1, "cmd", "0", "WAKE_COMMAND", "command landed (sticky): a command write to slot 0"));
        wake.add(new DF(Registers.EV_RUN_DONE, 1, "run", "0", "WAKE_RUN_DONE", "engine run done (sticky): `rd_done` rose"));
        wake.add(new DF(Registers.EV_REQUEST_ROOM, 1, "req", "0", "WAKE_REQUEST_ROOM", "request FIFO no longer full (sticky): `request_busy` fell"));
        wake.add(new DF(Registers.EV_PERIPH_1, 6, "service lines", "0", "WAKE_PERIPH(n)", "`PERIPH_SERVICE_1`-`6`: the peripheral's lines; a consuming load claims one unit of service (2.8)"));
        wake.add(new DF(Registers.EV_REQUEST_PENDING, 1, "pend", "live", "WAKE_REQUEST_PENDING", "`REQUEST_PENDING` (live): a received request waits; consuming it changes nothing"));
        wake.add(new DF(Registers.EV_SENDER_IDLE, 1, "idle", "0", "WAKE_SENDER_IDLE", "sender idle (sticky): `sender_busy` fell"));
        wake.add(new DF(Registers.EV_UART_RX, 1, "rx", "live", "WAKE_UART_RX", "UART RX not empty (live); never on an unwired tile"));
        wake.add(new DF(Registers.EV_UART_TX, 1, "tx", "live", "WAKE_UART_TX", "UART TX not full (live); always on an unwired tile"));
        WORDS.put("WAKE", new Word("WAKE", wake, "reserved", List.of(), List.of()));
        word("REQ_W0", List.of(new DF(0, 7, "requestor", NONE, "REQ_REQUESTOR", "the sender's endpoint id: written 0, stamped by the tree"),
                new DF(7, 3, "target", NONE, "REQ_TARGET", "the receiver the root delivers to (8 targets)"), new DF(10, 22, "request [31:10]", NONE, NONE, "defined by the target")), "spare");
        word("REQ_W1", List.of(one(32, "request [63:32]", NONE, "the request's bits 63-32; store: sticky until the next send, then 0")), "spare");
        word("REQ_W2", List.of(one(32, "request [95:64]", NONE, "the request's bits 95-64; store: sticky until the next send, then 0")), "spare");
        word("REQ_W3", List.of(one(32, "request [127:96]", NONE, "the request's bits 127-96: a store pushes the request, a load pops the oldest received one")), "spare");
        word("SENDER_SEND", fromRegisters("SENDER_SEND", Map.of("DEST", "dest", "MODE", "mode", "QUEUE", "queue", "COUNT", "word count"), Map.of(), NONE), "spare");
        word("SENDER_ADDR", List.of(one(SENDER_ADDR_WIDTH, "word address", UNDEF, "the first packet's addr for modes 3, 6 and 9; advancing by each packet's words")), "spare");
        word("PERIPH_STATUS", List.of(one(32, "status", "0", "defined by the peripheral; 0 where it defines none (the replayer: 2.8.2)")), "spare");
        word("UART_TX", List.of(one(8, "byte", NONE, "pushed into the TX FIFO (stalls while it is full); on an unwired tile dropped, never stalls; meshsim prints it")), "spare");
        word("UART_RX", fromRegisters("UART_RX", Map.of("BYTE", "byte", "VALID", "valid"), Map.of(), NONE), "spare");
        word("UART_STATUS", fromRegisters("UART_STATUS", Map.of("TX_OCC", "tx occupancy", "RX_OCC", "rx occupancy", "OVERRUN", "overrun", "FRAMING", "framing"), Map.of(), "0"), "spare");
        word("UART_DIVISOR", List.of(one(DIVISOR_WIDTH, "divisor", "434 at 25 MHz", "reference-oscillator cycles per bit; reset round(f_ref / 57,600); on an unwired tile writes are ignored and it reads its reset value")), "spare");
        word("HIST", List.of(one(HIST_WIDTH, "count", UNDEF, "count-on-arrival's count for this digit value (modes 2, 4, 5)")), "spare");
        word("PTR", List.of(one(PTR_WIDTH, "chunk address", UNDEF, "where the next packet for this entry lands; + 1, 2 or 4 by the packet's size (modes 2, 4), + 2 (mode 5)")), "spare");
        word("STREAM_SRC", fromRegisters("STREAM_SRC", Map.of("ADDR", "source"), Map.of(), UNDEF), "spare");
        word("STREAM_DEST", fromRegisters("STREAM_DEST", Map.of("WORD", "dest word", "D", "d"), Map.of(), UNDEF), "spare");
        word("STREAM_COUNT", fromRegisters("STREAM_COUNT", Map.of("N", "count", "SIZE", "size", "GAP", "gap"), Map.of(), UNDEF), "spare");
        word("GROUP_LO", fromRegisters("GROUP_LO", Map.of("R", "R"), Map.of(), UNDEF), "spare");
        word("GROUP_HI", fromRegisters("GROUP_HI", Map.of("TM1", "t-1"), Map.of(), UNDEF), "spare");
        word("CMD", List.of(new DF(0, Registers.CMD_PARITY_BIT, "argument", NONE, "CMD_ARG(w)", "the command's own argument"),
                new DF(Registers.CMD_PARITY_BIT, 1, "parity", NONE, "CMD_PARITY(w)", "0: arguments in slots " + Registers.ARGS_EVEN + "-" + (Registers.ARGS_ODD - 1) + "; 1: slots " + Registers.ARGS_ODD + "-" + (Registers.COMMAND_SLOTS - 1)),
                new DF(24, 8, "type", NONE, "CMD_TYPE(w)", "the command (jump, report, stage, the program's own)")), "spare");
    }

    // ================================================================ what a reference adds per register
    /** Per register: the word(s) a store and a load use, what a load returns, the reset value, the entry and its anchor. */
    record RD(String storeWord, String loadWord, String loads, String reset, String entry, String anchor) {}
    static final Map<String, RD> DOCS = new LinkedHashMap<>();
    static {
        String cfg = "undefined; kept across HALT → RUN";
        DOCS.put("TILE_STATUS", new RD(null, "TILE_STATUS", "live bits, peripheral type, endpoint id, size", "the idle machine", "2.1.1", "tile-status"));
        DOCS.put("TILE_CONDITIONS", new RD(null, "TILE_CONDITIONS", "the conditions", "0", "2.1.2", "tile-conditions"));
        DOCS.put("TILE_PULSE", new RD("TILE_PULSE", null, "0", "— (no state)", "2.1.3", "tile-pulse"));
        DOCS.put("ROUTER_SET_HDR", new RD("ROUTER_SET_HDR", null, "0", cfg, "2.2", "send-port"));
        for (String w : new String[] { "ROUTER_SET_W0", "ROUTER_SET_W1", "ROUTER_SET_W2" }) DOCS.put(w, new RD(w, null, "0", cfg, "2.2", "send-port"));
        for (String w : new String[] { "ROUTER_SEND_32", "ROUTER_SEND_64", "ROUTER_SEND_128" }) DOCS.put(w, new RD(w, null, "0", "— (no state)", "2.2", "send-port"));
        DOCS.put("DMA_CONFIG", new RD("DMA_CONFIG", null, "0", cfg, "2.3.1", "dma-config"));
        DOCS.put("DMA_RECORD_BASE_ADDRESS", new RD("DMA_RECORD_BASE_ADDRESS", null, "0", cfg, "2.3.3", "record-base"));
        for (String c : new String[] { "DMA_SRAM_REMAINING", "DMA_CM_REMAINING", "DMA_REPLY_REMAINING" }) DOCS.put(c, new RD("COUNTER", "COUNTER", "the count", "0", "2.3.2", "down-counters"));
        DOCS.put("DMA_PERIPH_REMAINING", new RD("COUNTER", "COUNTER", "the count", "0", "2.8", "replayer"));
        DOCS.put("SRAM_RD_ADDR_QUEUE", new RD("RDQ_ST", "RDQ_LD", "free entries", "queue empty", "2.4", "read-port"));
        DOCS.put("SRAM_RD_DEST_ADDR", new RD("SRAM_RD_DEST_ADDR", null, "0", cfg, "2.4", "read-port"));
        DOCS.put("WAKE_EVENTS", new RD(null, "WAKE", "every pending event; consumes those in `WAKE_MASK`", "0 (live events: their state)", "2.6", "wake-events"));
        DOCS.put("WAKE_PEEK", new RD(null, "WAKE", "every pending event; consumes nothing", "as `WAKE_EVENTS`", "2.6", "wake-events"));
        DOCS.put("WAKE_MASK", new RD("WAKE", "WAKE", "the mask", "[8] \\| [9]; again at HALT → RUN", "2.6", "wake-events"));
        DOCS.put("WAKE_REQUIRED", new RD("WAKE", "WAKE", "the required set", "0; again at HALT → RUN", "2.6", "wake-events"));
        DOCS.put("REQUEST_SET_W0", new RD("REQ_W0", null, "(a load: `REQUEST_GET_W0`)", cfg, "2.7", "request-port"));
        DOCS.put("REQUEST_SET_W1", new RD("REQ_W1", null, "(a load: `REQUEST_GET_W1`)", cfg, "2.7", "request-port"));
        DOCS.put("REQUEST_SET_W2", new RD("REQ_W2", null, "(a load: `REQUEST_GET_W2`)", cfg, "2.7", "request-port"));
        DOCS.put("REQUEST_SEND", new RD("REQ_W3", null, "(a load: `REQUEST_RECEIVE`)", "— (no state)", "2.7", "request-port"));
        DOCS.put("REQUEST_GET_W0", new RD(null, "REQ_W0", "the oldest received request's bits 31-0; 0 when empty", "queue empty", "2.7", "request-port"));
        DOCS.put("REQUEST_GET_W1", new RD(null, "REQ_W1", "its bits 63-32; 0 when empty", "queue empty", "2.7", "request-port"));
        DOCS.put("REQUEST_GET_W2", new RD(null, "REQ_W2", "its bits 95-64; 0 when empty", "queue empty", "2.7", "request-port"));
        DOCS.put("REQUEST_RECEIVE", new RD(null, "REQ_W3", "its bits 127-96, and pops it; empty: 0 and receive underflow", "queue empty", "2.7", "request-port"));
        DOCS.put("SENDER_SEND", new RD("SENDER_SEND", null, "0", "— (no state)", "2.8.1", "sender"));
        DOCS.put("SENDER_ADDR", new RD("SENDER_ADDR", null, "0", cfg, "2.8.1", "sender"));
        for (int i = 0; i < 8; i++) DOCS.put("PERIPH_STATUS_" + i, new RD(null, "PERIPH_STATUS", "the peripheral's status " + i + "; no side effects", "per peripheral; 0 when idle", "2.8", "replayer"));
        DOCS.put("UART_TX", new RD("UART_TX", null, "0", "— (no state)", "2.9", "uart"));
        DOCS.put("UART_RX", new RD(null, "UART_RX", "the oldest byte and valid; pops when valid", "FIFO empty", "2.9", "uart"));
        DOCS.put("UART_STATUS", new RD("UART_STATUS", "UART_STATUS", "occupancies and errors; no side effects", "0", "2.9", "uart"));
        DOCS.put("UART_DIVISOR", new RD("UART_DIVISOR", "UART_DIVISOR", "the divisor", "round(f_ref / 57,600)", "2.9", "uart"));
        for (Reg r : REGS) if (!DOCS.containsKey(r.name)) throw new IllegalStateException("RegDoc: no entry for " + r.name);
    }
    /** How a word's unused bits behave: by whether the program stores it, loads it, or both. */
    static final Map<String, String> DIRECTION = Map.ofEntries(
        Map.entry("DMA_CONFIG", "ignored on a store"), Map.entry("TILE_PULSE", "ignored on a store"), Map.entry("TILE_STATUS", "reads 0"),
        Map.entry("TILE_CONDITIONS", "reads 0"), Map.entry("ROUTER_SET_HDR", "ignored on a store"), Map.entry("DMA_RECORD_BASE_ADDRESS", "ignored on a store"),
        Map.entry("RDQ_ST", "ignored on a store"), Map.entry("SRAM_RD_DEST_ADDR", "ignored on a store"), Map.entry("PTR", "ignored on a store"),
        Map.entry("STREAM_SRC", "ignored on a store"), Map.entry("STREAM_DEST", "ignored on a store"), Map.entry("STREAM_COUNT", "ignored on a store"),
        Map.entry("GROUP_LO", "ignored on a store"), Map.entry("GROUP_HI", "ignored on a store"), Map.entry("SENDER_SEND", "ignored on a store"),
        Map.entry("SENDER_ADDR", "ignored on a store"), Map.entry("UART_TX", "ignored on a store"), Map.entry("UART_RX", "reads 0"),
        Map.entry("REQ_W0", "—"), Map.entry("WAKE", "ignored on a store, reads 0"));
    /** Meanings that say more than mesh.h's one-line comments (keyed word.label). */
    static final Map<String, String> DOC = Map.ofEntries(
        Map.entry("ROUTER_SET_HDR.addr", "the packet's addr: modes 3, 6, 7 and 9 (0 otherwise); after each send + 1, 2 or 4 by the size sent (modes 3, 6, 9) or + 1 (mode 7)"),
        Map.entry("ROUTER_SET_HDR.mode", "packet mode, 4 bits: 1-9 (0 and 10-15 are bad modes; mode 5 not at 128 bits, mode 7 only at 32, mode 8 not at 128, mode 3 aligned to its size)"),
        Map.entry("TILE_STATUS.done", "live: done pulsed since the last GO or HALT -> RUN (forced to 1 in HALT)"),
        Map.entry("TILE_STATUS.landed", "live: a command write to slot 0 since the last GO or HALT -> RUN"),
        Map.entry("TILE_STATUS.size-1", "big-SRAM size in 256 KB units, minus 1 (fixed per device; 15 at 4 MB)"),
        Map.entry("TILE_STATUS.endpoint id", "this tile's endpoint id: the tile index, 0-63 (fixed)"),
        Map.entry("TILE_STATUS.type", "the peripheral slot's type: 0 none, 1 the replayer (fixed; 1 on every mesh tile)"),
        Map.entry("TILE_PULSE.periph run", "peripheral run: start the peripheral's work; ignored while periph_running and on a type-0 tile (meshsim flags both)"),
        Map.entry("TILE_PULSE.GO", "GO: drives the GO net where wired (C&C's); on every mesh tile ignored, and meshsim flags a 1"),
        Map.entry("DMA_CONFIG.k-1", "gather value size k - 1, in records (k = 1-8)"),
        Map.entry("STREAM_SRC.source", "element 0's chunk address; element e at source + e × (1, 2 or 4 chunks by the read size), mod 2^21"),
        Map.entry("STREAM_DEST.dest word", "modes 3, 6 and 9: element 0's addr (mode 6: a packed word; mode 3: a chunk, aligned to the size); + 1, 2 or 4 per element by the read size, mod 2^21. Other modes: ignored"),
        Map.entry("STREAM_DEST.d", "destination: 0-63 a tile offset, dst = (me + d) mod 64; 64 C&C (mode 3 only); 65-127 errors"),
        Map.entry("STREAM_COUNT.count", "elements in the stream (0: the stream sends nothing)"),
        Map.entry("STREAM_COUNT.mode", "packet mode, 4 bits: 1-6 or 9 (0, 7, 8 and 10-15 are errors; mode 5 needs read size 32 or 64)"),
        Map.entry("STREAM_COUNT.size", "read size, stamped as every packet's size: 0 32 bits, 1 64 bits, 2 128 bits (3 undefined)"),
        Map.entry("STREAM_COUNT.gap", "stream runs: idle cycles after the stream (group runs ignore it)"),
        Map.entry("GROUP_LO.R", "rotations: the group's maximum count over all tiles"),
        Map.entry("GROUP_HI.start", "first stream of the group (0-255)"),
        Map.entry("GROUP_HI.t-1", "streams in the group, minus 1 (t = 1-16)"),
        Map.entry("GROUP_HI.gap", "idle cycles after the group"),
        Map.entry("SRAM_RD_DEST_ADDR.destination", "packed controller-memory word of the next result; + 1, 2 or 4 (the size) per queued read, a dropped one included, wrapping in 14 bits"),
        Map.entry("SRAM_RD_DEST_ADDR.size", "read size: 0 32 bits, 1 64, 2 128 (3 reserved); each result one mode-6 packet of that size"));

    static String access(int a) { return a == Registers.R ? "R" : a == Registers.W ? "W" : "R/W"; }
    static String hex8(int v) { return String.format("0x%08X", v); }
    static String hex(int v) { return String.format("0x%X", v); }
    static String bits(int hi, int lo) { return hi == lo ? "[" + hi + "]" : "[" + hi + ":" + lo + "]"; }

    // ================================================================ rendering a word
    record Seg(int hi, int lo, String label) {}

    /** The word's boxes, high bits first: fields, collapsed ranges and the unused bits between them. */
    static List<Seg> segs(Word w) {
        boolean[] used = new boolean[32];
        for (DF f : w.fields)
            for (int b = f.lo; b <= f.hi(); b++) { if (used[b]) throw new IllegalStateException(w.id + ": bit " + b + " twice"); used[b] = true; }
        List<Seg> s = new ArrayList<>();
        for (int b = 31; b >= 0; ) {
            final int bb = b;
            int ci = -1;
            for (int i = 0; i < w.collapse.size(); i++) if (w.collapse.get(i)[0] == b) ci = i;
            if (ci >= 0) { int[] c = w.collapse.get(ci); s.add(new Seg(c[0], c[1], w.collapseLabel.get(ci))); b = c[1] - 1; continue; }
            DF f = w.fields.stream().filter(x -> x.hi() == bb).findFirst().orElse(null);
            if (f != null) { s.add(new Seg(f.hi(), f.lo, f.label)); b = f.lo - 1; continue; }
            int lo = b;
            while (lo - 1 >= 0 && !used[lo - 1] && !startsCollapse(w, lo - 1)) lo--;
            s.add(new Seg(b, lo, w.gapName));
            b = lo - 1;
        }
        return s;
    }
    private static boolean startsCollapse(Word w, int b) { for (int[] c : w.collapse) if (c[0] == b) return true; return false; }

    static final int MAX_LINE = 80;

    /** The ASCII diagram (bit 31 on the left): bit numbers, boxes, labels, widths; split at bit 16 if wider than MAX_LINE. */
    static String diagram(Word w) {
        List<Seg> all = segs(w);
        String one = draw(all);
        if (one.lines().mapToInt(String::length).max().orElse(0) <= MAX_LINE) return one;
        List<Seg> hi = new ArrayList<>(), lo = new ArrayList<>();
        for (Seg g : all) {
            if (g.lo >= 16) hi.add(g);
            else if (g.hi < 16) lo.add(g);
            else { hi.add(new Seg(g.hi, 16, g.label)); lo.add(new Seg(15, g.lo, g.label)); }
        }
        return draw(hi) + "\n" + draw(lo);
    }
    private static String draw(List<Seg> segs) {
        StringBuilder num = new StringBuilder(" "), bord = new StringBuilder("+"), lab = new StringBuilder("|"), wid = new StringBuilder(" ");
        for (Seg g : segs) {
            String h = Integer.toString(g.hi), l = Integer.toString(g.lo), n = Integer.toString(g.hi - g.lo + 1);
            int w = Math.max(g.label.length(), g.hi == g.lo ? h.length() : h.length() + l.length() + 1) + 2;
            w = Math.max(w, n.length() + 2);
            num.append(g.hi == g.lo ? center(h, w) : h + " ".repeat(w - h.length() - l.length()) + l).append(' ');
            bord.append("-".repeat(w)).append('+');
            lab.append(center(g.label, w)).append('|');
            wid.append(center(n, w)).append(' ');
        }
        return rtrim(num) + "\n" + bord + "\n" + lab + "\n" + bord + "\n" + rtrim(wid) + "\n";
    }
    private static String center(String s, int w) { int l = (w - s.length()) / 2; return " ".repeat(l) + s + " ".repeat(w - s.length() - l); }
    private static String rtrim(StringBuilder sb) { return sb.toString().replaceAll("\\s+$", ""); }

    /** The field table, bit 0 first (the diagram above it has bit 31 on the left), with the unused bits. */
    static String fieldTable(Word w, boolean showMacro, boolean showReset) {
        StringBuilder sb = new StringBuilder("| bits | field | width |" + (showReset ? " reset |" : "") + (showMacro ? " `mesh.h` |" : "") + " meaning |\n|---|---|---|"
                + (showReset ? "---|" : "") + (showMacro ? "---|" : "") + "---|\n");
        List<Seg> rows = new ArrayList<>(segs(new Word(w.id, w.fields, w.gapName, List.of(), List.of())));
        Collections.reverse(rows);
        String dir = DIRECTION.getOrDefault(w.id, "ignored on a store, reads 0");
        for (Seg g : rows) {
            DF f = w.fields.stream().filter(x -> x.hi() == g.hi && x.lo == g.lo).findFirst().orElse(null);
            String label = f != null ? f.label : g.label, reset = f != null ? f.reset : (dir.equals("ignored on a store") ? NONE : "0");
            String doc = f != null ? DOC.getOrDefault(w.id + "." + f.label, f.doc) : (w.gapName.equals("reserved") ? "reserved: " : "") + dir;
            String macro = f == null || f.macro.equals(NONE) ? NONE : "`" + f.macro + "`";
            sb.append("| ").append(bits(g.hi, g.lo)).append(" | ").append(label).append(" | ").append(g.hi - g.lo + 1);
            if (showReset) sb.append(" | ").append(reset);
            if (showMacro) sb.append(" | ").append(macro);
            sb.append(" | ").append(doc).append(" |\n");
        }
        return sb.toString();
    }
    static String wordBlock(String id) {
        Word w = WORDS.get(id);
        boolean macros = DIRECT && w.fields.stream().anyMatch(f -> !f.macro.equals(NONE));
        boolean resets = w.fields.stream().anyMatch(f -> !f.reset.equals(NONE));
        return "```text\n" + diagram(w) + "```\n\n" + fieldTable(w, macros, resets);
    }

    // ================================================================ the blocks
    static String render(String id) {
        String[] p = id.trim().split("\\s+");
        return switch (p[0]) {
            case "addressmap" -> addressMap();
            case "summary" -> summary();
            case "reg" -> regs(Arrays.copyOfRange(p, 1, p.length));
            case "window" -> window();
            case "entry" -> entry(p[1]);
            case "modes" -> modes();
            case "linkword" -> linkWord();
            case "cmword" -> cmWord();
            case "cmwrites" -> cmWrites();
            case "command" -> command();
            default -> throw new IllegalArgumentException("unknown block: " + id);
        };
    }

    static int regEnd() { return Registers.REG_BASE + 4 * Registers.N_OFFSETS; }

    static String addressMap() {
        return "| address | size | contents | data access |\n|---|---|---|---|\n"
            + row(Registers.IMEM_BASE, Registers.LOADER_BASE - 1, "instruction memory: code (fetched; written only by mode 6)", "bus error")
            + row(Registers.LOADER_BASE, Registers.LOADER_BASE + Registers.LOADER_SIZE - 1, "instruction memory: the resident loader", "bus error")
            + row(Registers.HIST_BASE, Registers.PTR_BASE - 1, "table window: `HIST`", "`lw`, `sw`")
            + row(Registers.PTR_BASE, Registers.STREAM_SRC_BASE - 1, "table window: `PTR`", "`sw` only")
            + row(Registers.STREAM_SRC_BASE, Registers.STREAM_DEST_BASE - 1, "table window: `STREAM_SRC`", "`sw` only")
            + row(Registers.STREAM_DEST_BASE, Registers.STREAM_COUNT_BASE - 1, "table window: `STREAM_DEST`", "`sw` only")
            + row(Registers.STREAM_COUNT_BASE, Registers.GROUP_BASE - 1, "table window: `STREAM_COUNT`", "`sw` only")
            + row(Registers.GROUP_BASE, Registers.TABLES_END - 1, "table window: `GROUP`", "`sw` only")
            + row(Registers.TABLES_END, Registers.TABLE_END - 1, "unmapped", "bus error")
            + row(Registers.DMEM_BASE, Registers.REPLY_BASE - 1, "data memory: `.rodata`, `.data`, `.bss`, stack", "any width")
            + row(Registers.REPLY_BASE, Registers.COMMAND_BASE - 1, "data memory: the reply block, 256 words (2 per source)", "any width")
            + row(Registers.COMMAND_BASE, Registers.DMEM_BASE + Registers.DMEM_SIZE - 1, "data memory: the command block, 16 slots", "any width")
            + row(Registers.DMEM_BASE + Registers.DMEM_SIZE, Registers.REG_BASE - 1, "unmapped", "bus error")
            + row(Registers.REG_BASE, regEnd() - 1, "registers: " + Registers.N_OFFSETS + " words (section 2)", "`lw`, `sw` per register")
            + row(regEnd(), -1, "unmapped", "bus error");
    }
    private static String row(int a, int b, String what, String access) {
        long size = Integer.toUnsignedLong(b) - Integer.toUnsignedLong(a) + 1;
        String sz = size >= 1L << 30 ? "≈\u00A04\u00A0GB" : size % 1024 == 0 ? (size / 1024) + "\u00A0KB" : size + "\u00A0B";   // no-break spaces
        return "| `" + hex8(a) + "`–`" + hex8(b) + "` | " + sz + " | " + what + " | " + access + " |\n";
    }

    static String summary() {
        StringBuilder sb = new StringBuilder("| offset | register | access | a load returns | reset | entry |\n|---|---|---|---|---|---|\n");
        for (Reg r : REGS) {
            if (r.name.startsWith("REQUEST_GET_") || r.name.equals("REQUEST_RECEIVE")) continue;   // shown with its store register
            RD d = DOCS.get(r.name);
            String nm = r.name.startsWith("REQUEST_") ? "`" + r.name + "` / `" + (r.name.equals("REQUEST_SEND") ? "REQUEST_RECEIVE" : r.name.replace("SET", "GET")) + "`" : "`" + r.name + "`";
            String acc = r.name.startsWith("REQUEST_") ? "W / R" : access(r.access);
            sb.append("| `").append(hex(r.addr & 0xFFF)).append("` | ").append(nm).append(" | ").append(acc).append(" | ")
              .append(d.loads).append(" | ").append(d.reset).append(" | [").append(d.entry).append("](#").append(d.anchor).append(") |\n");
        }
        return sb.toString();
    }

    static String regs(String[] names) {
        List<Reg> rs = new ArrayList<>();
        for (String n : names) rs.add(reg(n));
        StringBuilder sb = new StringBuilder("| register | address | access | a load returns | reset |\n|---|---|---|---|---|\n");
        for (Reg r : rs) {
            RD d = DOCS.get(r.name);
            sb.append("| `").append(r.name).append("` | `").append(hex8(r.addr)).append("` | ").append(access(r.access))
              .append(" | ").append(d.loads).append(" | ").append(d.reset).append(" |\n");
        }
        List<String[]> parts = new ArrayList<>();          // {label, word id}
        for (Reg r : rs) {
            RD d = DOCS.get(r.name);
            if (d.storeWord != null && d.storeWord.equals(d.loadWord)) parts.add(new String[] { "`" + r.name + "`, store and load", d.storeWord });
            else {
                if (d.storeWord != null) parts.add(new String[] { "`" + r.name + "`, store", d.storeWord });
                if (d.loadWord != null) parts.add(new String[] { "`" + r.name + "`, load", d.loadWord });
            }
        }
        Set<String> distinct = new LinkedHashSet<>();
        for (String[] pt : parts) distinct.add(pt[1]);
        Set<String> done = new LinkedHashSet<>();
        for (String[] pt : parts) {
            if (!done.add(pt[1])) continue;
            // registers sharing one word (the three send registers) are named together: `A`, `B` and `C`, store
            List<String> nm = new ArrayList<>();
            for (String[] q : parts) if (q[1].equals(pt[1])) nm.add(q[0].substring(0, q[0].lastIndexOf(',')));
            String label = nm.size() == 1 ? pt[0] : String.join(", ", nm.subList(0, nm.size() - 1)) + " and " + nm.get(nm.size() - 1) + pt[0].substring(pt[0].lastIndexOf(','));
            sb.append("\n");
            if (distinct.size() > 1) sb.append("**").append(label).append(".**\n\n");
            sb.append(wordBlock(pt[1]));
        }
        return sb.toString();
    }

    static String window() {
        return "| table | window | entries | entry | index from the address | load | store |\n|---|---|---|---|---|---|---|\n"
            + "| `HIST` | `" + hex(Registers.HIST_BASE) + "`–`" + hex(Registers.PTR_BASE - 1) + "` | 256 | one word, " + HIST_WIDTH + " bits | *i* = addr[9:2] | the count | the count |\n"
            + "| `PTR` | `" + hex(Registers.PTR_BASE) + "`–`" + hex(Registers.STREAM_SRC_BASE - 1) + "` | 256 | one word, " + PTR_WIDTH + " bits | *i* = addr[9:2] | bus error | the pointer |\n"
            + "| `STREAM_SRC` | `" + hex(Registers.STREAM_SRC_BASE) + "`–`" + hex(Registers.STREAM_DEST_BASE - 1) + "` | 256 | one word | *i* = addr[9:2] | bus error | the word |\n"
            + "| `STREAM_DEST` | `" + hex(Registers.STREAM_DEST_BASE) + "`–`" + hex(Registers.STREAM_COUNT_BASE - 1) + "` | 256 | one word | *i* = addr[9:2] | bus error | the word |\n"
            + "| `STREAM_COUNT` | `" + hex(Registers.STREAM_COUNT_BASE) + "`–`" + hex(Registers.GROUP_BASE - 1) + "` | 256 | one word | *i* = addr[9:2] | bus error | the word |\n"
            + "| `GROUP` | `" + hex(Registers.GROUP_BASE) + "`–`" + hex(Registers.TABLES_END - 1) + "` | 64 | two words: low, high | *g* = addr[8:3]; addr[2]: 0 low, 1 high | bus error | one word |\n"
            + "| — | `" + hex(Registers.TABLES_END) + "`–`" + hex(Registers.TABLE_END - 1) + "` | — | unmapped | — | bus error | bus error |\n";
    }

    static String entry(String id) {
        if (!List.of("HIST", "PTR", "STREAM_SRC", "STREAM_DEST", "STREAM_COUNT", "GROUP_LO", "GROUP_HI").contains(id)) throw new IllegalArgumentException("unknown entry " + id);
        return wordBlock(id);
    }

    // ---- packets
    /** Per mode: mesh.h name (DIRECT only), packet, lands, uses addr, ROUTER_SET_HDR's addr after a send, counter, STREAM. */
    static final String[][] MODES = {
        { "0", "", "invalid", "dropped; bad mode or size", "no", "unchanged", "—", "no" },
        { "1", "MODE_GATHER", "gather request (to the tile itself)", "the GATHER engine", "no", "unchanged", "— (never; its values count)", "yes, *d* = 0" },
        { "2", "MODE_SRC", "big-SRAM write by source", "big SRAM at `PTR[src]`", "no", "unchanged", "`DMA_SRAM_REMAINING`", "yes" },
        { "3", "MODE_EXPLICIT", "big-SRAM write at an explicit address, aligned to its size (misaligned: a bad packet)", "big SRAM at addr; never spans", "yes", "+1, +2 or +4 by size", "`DMA_SRAM_REMAINING`", "yes; *d* = 64 a write stream to C&C" },
        { "4", "MODE_BUCKET", "bucket write", "big SRAM at `PTR[bucket digit]`", "no", "unchanged", "`DMA_SRAM_REMAINING`", "yes" },
        { "5", "MODE_AUGMENT", "augmented write: a 32- or 64-bit packet's low word as a 64-bit record (128 bits: a bad packet)", "big SRAM at `PTR[src]`, ordinal in [63:32]", "no", "unchanged", "`DMA_SRAM_REMAINING`", "yes, not at 128 bits" },
        { "6", "MODE_CM", "controller-memory write, 1, 2 or 4 words by size", "instruction or data memory at addr, outside both blocks", "yes", "+1, +2 or +4 by size", "`DMA_CM_REMAINING`", "yes" },
        { "7", "MODE_COMMAND", "command write: 32 bits only", "command slot addr[3:0]", "yes", "+1", "— (never; slot 0: command landed)", "no" },
        { "8", "MODE_REPLY", "reply: 32 or 64 bits (128: a bad packet)", "the reply block at word 2·src (32 bits: word 0 only)", "no", "unchanged", "`DMA_REPLY_REMAINING`", "no" },
        { "9", "MODE_PERIPH", "peripheral write", "the peripheral's address space at addr (2.8)", "yes", "+1, +2 or +4 by size", "`DMA_PERIPH_REMAINING`, on landing or drop", "yes" },
        { "10–15", "", "reserved", "dropped; bad mode or size", "no", "unchanged", "—", "no" },
    };
    static String modes() {
        StringBuilder sb = new StringBuilder("| mode |" + (DIRECT ? " `mesh.h` |" : "") + " packet | lands | uses addr | `ROUTER_SET_HDR` addr after a send | counted in | in `STREAM` |\n|---|"
                + (DIRECT ? "---|" : "") + "---|---|---|---|---|---|\n");
        for (String[] m : MODES) {
            sb.append("| ").append(m[0]);
            if (DIRECT) sb.append(" | ").append(m[1].isEmpty() ? NONE : "`" + m[1] + "`");
            for (int i = 2; i < m.length; i++) sb.append(" | ").append(m[i]);
            sb.append(" |\n");
        }
        return sb.toString();
    }
    /** The link word's fields and widths (spec 3.1); bit positions on the wire are not visible to a program. */
    static final Object[][] LINK = {
        { "src", 7, "the injecting tile's router (127: idle packet)", "—" },
        { "dst", 7, "the sender: tile 0–63, 64 C&C, 126 broadcast", "`ROUTER_SET_HDR`.dst; `STREAM`: (`me` + *d*) mod 64, or 64; the read port: `me`" },
        { "mode", 4, "the sender", "`ROUTER_SET_HDR`.mode; `STREAM_COUNT`.mode; the read port: 6" },
        { "addr", 21, "the sender, for modes 3, 6, 7 and 9; 0 otherwise", "`ROUTER_SET_HDR`.addr; `STREAM_DEST`'s word for modes 3, 6 and 9, else 0; the read port: its setup register's destination" },
        { "size", 2, "the sender: 00 32, 01 64, 10 128 bits (11 undefined)", "the send register (`ROUTER_SEND_32`, `_64`, `_128`); `STREAM_COUNT`.size; the read port: its setup register's size" },
        { "payload", 128, "the sender; bits above the size are 0", "`ROUTER_SET_W0`–`W2` and the send store's data; `STREAM`: the element; the read port: the words read" },
    };
    static String linkWord() {
        StringBuilder sb = new StringBuilder("| field | width | set by | from a program |\n|---|---|---|---|\n");
        int total = 0;
        for (Object[] f : LINK) { sb.append("| ").append(f[0]).append(" | ").append(f[1]).append(" | ").append(f[2]).append(" | ").append(f[3]).append(" |\n"); total += (Integer) f[1]; }
        sb.append("| **total** | **").append(total).append("** | | |\n");
        return sb.toString();
    }
    static String cmWord() {
        int rb = 0x800 + ((Registers.REPLY_BASE - Registers.DMEM_BASE) >> 2), cb = 0x800 + ((Registers.COMMAND_BASE - Registers.DMEM_BASE) >> 2);
        return "| packed word | byte address | memory |\n|---|---|---|\n"
            + "| `0x000`–`0x7FF` | `" + hex(Registers.IMEM_BASE) + "`–`" + hex(Registers.IMEM_BASE + Registers.IMEM_SIZE - 4) + "` | instruction memory (mode 6) |\n"
            + "| `0x800`–`" + hex(rb - 1) + "` | `" + hex(Registers.DMEM_BASE) + "`–`" + hex(Registers.REPLY_BASE - 4) + "` | data memory below the blocks (mode 6) |\n"
            + "| `" + hex(rb) + "`–`" + hex(cb - 1) + "` | `" + hex(Registers.REPLY_BASE) + "`–`" + hex(Registers.COMMAND_BASE - 4) + "` | the reply block (mode 8 writes it by source; mode 6 must not) |\n"
            + "| `" + hex(cb) + "`–`0xFFF` | `" + hex(Registers.COMMAND_BASE) + "`–`" + hex(Registers.DMEM_BASE + Registers.DMEM_SIZE - 4) + "` | the command block (mode 7 writes it by slot; mode 6 must not) |\n";
    }
    /** The three producers of controller-memory writes and their address sources (section 2.5). */
    static String cmWrites() {
        return "| producer | packets | typical use | the address comes from | advances | counted in |\n|---|---|---|---|---|---|\n"
            + "| the address generator | mode 6 (from a `STREAM` entry) | block copies and vector exchanges | `STREAM_DEST`'s destination word | +1, +2 or +4 per packet (its size) | `DMA_CM_REMAINING` |\n"
            + "| the send port | mode 6 | words or vectors sent by a program | `ROUTER_SET_HDR`'s addr | +1, +2 or +4 per packet (its size) | `DMA_CM_REMAINING` |\n"
            + "| the read port | mode 6, to the tile itself | scattered reads: searches, hashing | `SRAM_RD_DEST_ADDR`'s destination | +1, +2 or +4 per read (its size) | `DMA_CM_REMAINING` |\n";
    }
    static String command() {
        int ae = Registers.ARGS_EVEN, ao = Registers.ARGS_ODD;
        return wordBlock("CMD") + "\n| command slots | use |\n|---|---|\n"
            + "| 0 | the command word, written last |\n"
            + "| " + ae + "–" + (ao - 1) + " | arguments of an even command |\n"
            + "| " + ao + "–" + (Registers.COMMAND_SLOTS - 1) + " | arguments of an odd command |\n";
    }
    // ================================================================ the document
    static final Pattern BLOCK = Pattern.compile("<!-- generated: ([^>]*?) -->\\n(.*?)<!-- end generated -->", Pattern.DOTALL);
    static final List<String> REQUIRED = List.of("addressmap", "summary", "window", "entry HIST", "entry PTR", "entry STREAM_SRC",
            "entry STREAM_DEST", "entry STREAM_COUNT", "entry GROUP_LO", "entry GROUP_HI", "modes", "linkword", "cmword", "cmwrites", "command");

    /** The document with every block regenerated. */
    static String sync(String doc) {
        Matcher m = BLOCK.matcher(doc);
        StringBuilder sb = new StringBuilder();
        while (m.find()) m.appendReplacement(sb, Matcher.quoteReplacement("<!-- generated: " + m.group(1) + " -->\n" + render(m.group(1)) + "<!-- end generated -->"));
        m.appendTail(sb);
        return sb.toString();
    }
    /** Problems: blocks that differ from the generator, and registers, entries or blocks the document lacks. */
    static List<String> check(String doc) {
        List<String> bad = new ArrayList<>();
        Matcher m = BLOCK.matcher(doc);
        Set<String> regs = new LinkedHashSet<>(), ids = new LinkedHashSet<>();
        while (m.find()) {
            String id = m.group(1).trim();
            if (!ids.add(id)) bad.add("block '" + id + "' appears twice");
            String want;
            try { want = render(id); } catch (RuntimeException e) { bad.add("block '" + id + "': " + e.getMessage()); continue; }
            if (!want.equals(m.group(2))) bad.add("block '" + id + "' differs from the generator (java RegDoc sw/REGISTERS.md)");
            if (id.startsWith("reg ")) regs.addAll(Arrays.asList(id.substring(4).trim().split("\\s+")));
        }
        for (Reg r : REGS) if (!regs.contains(r.name)) bad.add("register " + r.name + " has no generated entry");
        for (String need : REQUIRED) if (!ids.contains(need)) bad.add("block '" + need + "' is missing");
        return bad;
    }

    public static void main(String[] args) throws IOException {
        boolean checkOnly = args.length > 0 && args[0].equals("--check");
        Path p = Path.of(args.length > (checkOnly ? 1 : 0) ? args[checkOnly ? 1 : 0] : Registers.doc("REGISTERS.md").toString());
        String doc = Files.readString(p);
        System.out.println("RegDoc: mesh.h 1.1");
        if (checkOnly) {
            List<String> bad = check(doc);
            bad.forEach(b -> System.out.println("REGISTERS.md: " + b));
            System.out.println(bad.isEmpty() ? p + ": every generated block matches" : p + ": " + bad.size() + " problem(s)");
            System.exit(bad.isEmpty() ? 0 : 1);
        }
        String out = sync(doc);
        if (out.equals(doc)) { System.out.println(p + " unchanged"); return; }
        Files.writeString(p, out);
        System.out.println("wrote " + p);
    }
}
