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
    /** Always true since mesh.h 1.0 (kept for the generator's branches that show mesh.h names). */
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
    /** Kinds (spec 4.13): P a program error, H a hardware-fault detector. Keyed by CNAME. */
    static final Map<String, String> COND_KIND = Map.ofEntries(
        Map.entry("DRIFT", "P"), Map.entry("ROUTER_OVF", "P"), Map.entry("WE_IN_OVF", "P"), Map.entry("RDPORT_OVF", "P"),
        Map.entry("UNEXPECTED", "P"), Map.entry("TABLE_OVF", "H"), Map.entry("BUS_ERROR", "P (or H)"), Map.entry("ILLEGAL", "P (or H)"),
        Map.entry("GATHER_OVF", "H"), Map.entry("BAD_MODE", "P"), Map.entry("DONE_EARLY", "P"));
    /** When each condition is set (spec 4.13's table, shortened). */
    static final Map<String, String> COND_WHEN = Map.ofEntries(
        Map.entry("DRIFT", "in a group run, the SRAM read queue refused an address-generator request: the tile fell behind its timetable"),
        Map.entry("ROUTER_OVF", "a link, eject or C&C FIFO of this tile's router overflowed"),
        Map.entry("WE_IN_OVF", "a mode-2, 3 or 4 packet found the write engine's input FIFO full"),
        Map.entry("RDPORT_OVF", "a `SRAM_RD_ADDR_QUEUE` store found no free entry"),
        Map.entry("UNEXPECTED", "a packet arrived for a down-counter already at 0"),
        Map.entry("TABLE_OVF", "the `PTR` or `HIST` request FIFO overflowed"),
        Map.entry("BUS_ERROR", "a load or store broke the access rules (section 1); the controller halts"),
        Map.entry("ILLEGAL", "a divide opcode or a CSR instruction; the controller halts"),
        Map.entry("GATHER_OVF", "a mode-1 packet found the GATHER engine's 16-entry request queue full"),
        Map.entry("BAD_MODE", "a packet with mode 0 or 8-15 arrived; it was dropped"),
        Map.entry("DONE_EARLY", "done was pulsed while a down-counter was non-zero"));

    // ================================================================ words and their fields
    /** One field of a 32-bit word. reset: the value at chip reset ("undef" = undefined, "—" = no state). */
    record DF(int lo, int width, String label, String reset, String macro, String doc) {
        int hi() { return lo + width - 1; }
    }
    /** A 32-bit word: fields, a name for its unused bits ("spare" or "reserved"), and ranges drawn as one box. */
    record Word(String id, List<DF> fields, String gapName, List<int[]> collapse, List<String> collapseLabel) {}

    static final String UNDEF = "undef", NONE = "—";
    /** This document's word ids -> the field prefixes in Registers (mesh.h). mesh.h 1.0 may rename the prefixes: update here. */
    static final Map<String, String> PREFIX = Map.of("DMA_CONFIG", "DMA_CFG", "DMA_PULSE", "DMA_PULSE", "TILE_STATUS", "TILE_ST", "ROUTER_SET_HDR", "ROUTER_HDR",
            "STREAM_LO", "STREAM_LO", "STREAM_HI", "STREAM_HI", "GROUP_LO", "GROUP_LO", "GROUP_HI", "GROUP_HI");
    static final Map<String, Word> WORDS = new LinkedHashMap<>();

    // ---- one-field registers and table entries: widths not in mesh.h (RegDocTest checks them against the simulator)
    static final int BASE_WIDTH = 20, COUNTER_WIDTH = 20, RDQ_ADDR_WIDTH = 20, RD_DEST_WIDTH = 12, HIST_WIDTH = 17, PTR_WIDTH = 20;

    private static String lower(String s) { return s.toLowerCase().replace('_', ' '); }
    private static List<DF> fromRegisters(String word, Map<String, String> labels, Map<String, String> resets, String defReset) {
        String prefix = PREFIX.get(word);
        List<DF> out = new ArrayList<>();
        for (Registers.Field f : Registers.FIELDS) {
            if (!f.word.equals(prefix)) continue;
            out.add(new DF(f.lo, f.width, labels.getOrDefault(f.name, lower(f.name)), resets.getOrDefault(f.name, defReset),
                    f.word + "_" + f.name, v1(f.doc)));
        }
        if (out.isEmpty()) throw new IllegalStateException("RegDoc: no fields with prefix " + prefix + " for " + word + " (update RegDoc.PREFIX for mesh.h 1.0)");
        return out;
    }
    private static void word(String id, List<DF> fields, String gap) { WORDS.put(id, new Word(id, fields, gap, List.of(), List.of())); }
    private static DF one(int width, String label, String reset, String doc) { return new DF(0, width, label, reset, NONE, doc); }

    static {
        WORDS.put("TILE_STATUS", new Word("TILE_STATUS", fromRegisters("TILE_STATUS", Map.of("TILE", "tile index", "SIZE", "size-1"),
                Map.ofEntries(Map.entry("RD_RUNNING", "0"), Map.entry("RD_DONE", "0"), Map.entry("SEND_BUSY", "0"),
                        Map.entry("TS_BUSY", "0"), Map.entry("MEM_IDLE", "1"), Map.entry("CM_IDLE", "1"), Map.entry("DONE", "1"),
                        Map.entry("LANDED", "0"), Map.entry("GATHER_BUSY", "0"), Map.entry("TILE", "index"), Map.entry("SIZE", "device")), "0"),
                "spare", List.<int[]>of(new int[] { 9, 0 }), List.of("live bits (table)")));
        List<DF> cond = new ArrayList<>();
        for (int b = 0; b < COND.size(); b++) {
            int i = COND.get(b); String c = Conditions.CNAME[i];
            cond.add(new DF(b, 1, Conditions.NAME[i], "0", "TILE_COND_" + c, "**" + COND_KIND.get(c) + "** " + COND_WHEN.get(c)));
        }
        WORDS.put("TILE_CONDITIONS", new Word("TILE_CONDITIONS", cond, "reserved", List.<int[]>of(new int[] { COND.size() - 1, 0 }), List.of("conditions (table)")));
        word("ROUTER_SET_HDR", fromRegisters("ROUTER_SET_HDR", Map.of(), Map.of(), UNDEF), "spare");
        word("ROUTER_SET_HI", List.of(one(32, "payload [63:32]", UNDEF, "the next packet's payload bits 63-32; 0 after each send")), "spare");
        word("ROUTER_SEND_LO", List.of(one(32, "payload [31:0]", NONE, "the packet's payload bits 31-0; the store sends")), "spare");
        word("DMA_CONFIG", fromRegisters("DMA_CONFIG", Map.of("KIND", "kind", "START", "start", "COUNT", "count", "BUCKETDIG", "bucket digit",
                "HISTDIG", "hist digit", "KM1", "k-1"), Map.of(), UNDEF), "spare");
        List<DF> pulse = new ArrayList<>(fromRegisters("DMA_PULSE", Map.of(), Map.of(), NONE));
        pulse.add(new DF(Integer.numberOfTrailingZeros(Registers.PULSE_RESERVED_MASK), Integer.bitCount(Registers.PULSE_RESERVED_MASK), "reserved", NONE, NONE, "reserved: ignored (meshsim flags a 1)"));
        word("DMA_PULSE", pulse, "spare");
        word("COUNTER", List.of(one(COUNTER_WIDTH, "count", "0", "arrivals still expected")), "spare");
        word("DMA_RECORD_BASE_ADDRESS", List.of(one(BASE_WIDTH, "chunk address", UNDEF, "the run's record buffer: augment's landing buffer or gather's value buffer")), "spare");
        word("RDQ_ST", List.of(one(RDQ_ADDR_WIDTH, "chunk address", NONE, "the big-SRAM chunk to read (32 bits)")), "spare");
        word("RDQ_LD", List.of(one(32, "free entries", "depth", "free entries in the request queue (at least 64 when empty)")), "spare");
        word("SRAM_RD_DEST_ADDR", List.of(one(RD_DEST_WIDTH, "packed word", UNDEF, "packed data-memory word (0x800-0xEFF) for the next result; +1 per queued read, wrapping in 12 bits")), "spare");
        word("HIST", List.of(one(HIST_WIDTH, "count", UNDEF, "count-on-arrival's count for this digit value")), "spare");
        word("PTR", List.of(one(PTR_WIDTH, "chunk address", UNDEF, "where the next packet for this entry lands; +2 per packet")), "spare");
        word("STREAM_LO", fromRegisters("STREAM_LO", Map.of("SRC", "source", "CM", "cm word"), Map.of(), UNDEF), "spare");
        word("STREAM_HI", fromRegisters("STREAM_HI", Map.of("D", "d", "SIZE", "size", "GAP", "gap"), Map.of(), UNDEF), "spare");
        word("GROUP_LO", fromRegisters("GROUP_LO", Map.of("R", "R"), Map.of(), UNDEF), "spare");
        word("GROUP_HI", fromRegisters("GROUP_HI", Map.of("TM1", "t-1"), Map.of(), UNDEF), "spare");
        word("CMD", List.of(new DF(0, Registers.CMD_PARITY_BIT, "argument", NONE, "CMD_ARG(w)", "the command's own argument"),
                new DF(Registers.CMD_PARITY_BIT, 1, "parity", NONE, "CMD_PARITY(w)", "0: arguments in slots 1-31; 1: slots 32-63"),
                new DF(24, 8, "type", NONE, "CMD_TYPE(w)", "the command (jump, report, the program's own)")), "spare");
    }

    // ================================================================ what a reference adds per register
    /** Per register: the word(s) a store and a load use, what a load returns, the reset value, the entry and its anchor. */
    record RD(String storeWord, String loadWord, String loads, String reset, String entry, String anchor) {}
    static final Map<String, RD> DOCS = new LinkedHashMap<>();
    static {
        String cfg = "undefined; kept across HALT → RUN";
        DOCS.put("TILE_STATUS", new RD(null, "TILE_STATUS", "live bits, tile index, size", "the idle machine", "2.1.1", "tile-status"));
        DOCS.put("TILE_CONDITIONS", new RD(null, "TILE_CONDITIONS", "the conditions", "0", "2.1.2", "tile-conditions"));
        DOCS.put("ROUTER_SET_HDR", new RD("ROUTER_SET_HDR", null, "0", cfg, "2.2", "send-port"));
        DOCS.put("ROUTER_SET_HI", new RD("ROUTER_SET_HI", null, "0", cfg, "2.2", "send-port"));
        DOCS.put("ROUTER_SEND_LO", new RD("ROUTER_SEND_LO", null, "0", "— (no state)", "2.2", "send-port"));
        DOCS.put("DMA_CONFIG", new RD("DMA_CONFIG", null, "0", cfg, "2.3.1", "dma-config"));
        DOCS.put("DMA_PULSE", new RD("DMA_PULSE", null, "0", "— (no state)", "2.3.2", "dma-pulse"));
        DOCS.put("DMA_SRAM_REMAINING", new RD("COUNTER", "COUNTER", "the count", "0", "2.3.3", "down-counters"));
        DOCS.put("DMA_CM_REMAINING", new RD("COUNTER", "COUNTER", "the count", "0", "2.3.3", "down-counters"));
        DOCS.put("DMA_REPLY_REMAINING", new RD("COUNTER", "COUNTER", "the count", "0", "2.3.3", "down-counters"));
        DOCS.put("DMA_RECORD_BASE_ADDRESS", new RD("DMA_RECORD_BASE_ADDRESS", null, "0", cfg, "2.3.4", "record-base"));
        DOCS.put("SRAM_RD_ADDR_QUEUE", new RD("RDQ_ST", "RDQ_LD", "free entries", "queue empty", "2.4", "read-port"));
        DOCS.put("SRAM_RD_DEST_ADDR", new RD("SRAM_RD_DEST_ADDR", null, "0", cfg, "2.4", "read-port"));
        for (Reg r : REGS) if (!DOCS.containsKey(r.name)) throw new IllegalStateException("RegDoc: no entry for " + r.name);
    }
    /** How a word's unused bits behave: by whether the program stores it, loads it, or both. */
    static final Map<String, String> DIRECTION = Map.ofEntries(
        Map.entry("DMA_CONFIG", "ignored on a store"), Map.entry("DMA_PULSE", "ignored on a store"), Map.entry("TILE_STATUS", "reads 0"),
        Map.entry("TILE_CONDITIONS", "reads 0"), Map.entry("ROUTER_SET_HDR", "ignored on a store"), Map.entry("DMA_RECORD_BASE_ADDRESS", "ignored on a store"),
        Map.entry("RDQ_ST", "ignored on a store"), Map.entry("SRAM_RD_DEST_ADDR", "ignored on a store"), Map.entry("PTR", "ignored on a store"),
        Map.entry("STREAM_LO", "ignored on a store"), Map.entry("STREAM_HI", "ignored on a store"), Map.entry("GROUP_LO", "ignored on a store"),
        Map.entry("GROUP_HI", "ignored on a store"));
    /** Meanings that say more than mesh.h's one-line comments (keyed word.label). */
    static final Map<String, String> DOC = Map.ofEntries(
        Map.entry("ROUTER_SET_HDR.addr", "the packet's addr: modes 5-7 only (0 otherwise); after each send +2 (mode 5) or +1 (modes 6, 7)"),
        Map.entry("ROUTER_SET_HDR.mode", "packet mode, 4 bits: 1-7 (0 and 8-15 are bad modes)"),
        Map.entry("TILE_STATUS.done", "live: done pulsed since the last GO or HALT -> RUN (forced to 1 in HALT)"),
        Map.entry("TILE_STATUS.landed", "live: a mode-6 write to mailbox slot 0 since the last GO or HALT -> RUN"),
        Map.entry("TILE_STATUS.size-1", "big-SRAM size in 512 KB areas, minus 1 (fixed per device)"),
        Map.entry("TILE_STATUS.tile index", "this tile's index, 0-63 (fixed)"),
        Map.entry("DMA_CONFIG.k-1", "gather value size k - 1, in records (k = 1-8)"),
        Map.entry("STREAM_LO.cm word", "mode 5: the first controller-memory word, packed map; +2 per packet. Other modes: ignored"),
        Map.entry("STREAM_HI.gap", "stream runs: idle cycles after the stream (group runs ignore it)"),
        Map.entry("STREAM_HI.count", "elements in the stream (0: the stream sends nothing)"),
        Map.entry("GROUP_LO.R", "rotations: the group's maximum count over all tiles"),
        Map.entry("GROUP_HI.start", "first stream of the group (0-255)"),
        Map.entry("GROUP_HI.t-1", "streams in the group, minus 1 (t = 1-16)"),
        Map.entry("GROUP_HI.gap", "idle cycles after the group"));

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

    static int regEnd() { return Registers.REG_BASE + 4 * REGS.size(); }

    static String addressMap() {
        return "| address | size | contents | data access |\n|---|---|---|---|\n"
            + row(Registers.IMEM_BASE, Registers.LOADER_BASE - 1, "instruction memory: code (fetched; written only by mode 5)", "bus error")
            + row(Registers.LOADER_BASE, Registers.LOADER_BASE + Registers.LOADER_SIZE - 1, "instruction memory: the resident loader", "bus error")
            + row(Registers.HIST_BASE, Registers.PTR_BASE - 1, "table window: `HIST`", "`lw`, `sw`")
            + row(Registers.PTR_BASE, Registers.STREAM_BASE - 1, "table window: `PTR`", "`sw` only")
            + row(Registers.STREAM_BASE, Registers.GROUP_BASE - 1, "table window: `STREAM`", "`sw` only")
            + row(Registers.GROUP_BASE, Registers.TABLES_END - 1, "table window: `GROUP`", "`sw` only")
            + row(Registers.TABLES_END, Registers.TABLE_END - 1, "unmapped", "bus error")
            + row(Registers.DMEM_BASE, Registers.MAILBOX_BASE - 1, "data memory: `.rodata`, `.data`, `.bss`, stack", "any width")
            + row(Registers.MAILBOX_BASE, Registers.DMEM_BASE + Registers.DMEM_SIZE - 1, "data memory: the mailbox, 256 slots", "any width")
            + row(Registers.DMEM_BASE + Registers.DMEM_SIZE, Registers.REG_BASE - 1, "unmapped", "bus error")
            + row(Registers.REG_BASE, regEnd() - 1, "registers: " + REGS.size() + " words (section 2)", "`lw`, `sw` per register")
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
            RD d = DOCS.get(r.name);
            sb.append("| `").append(hex(r.addr & 0xFFF)).append("` | `").append(r.name).append("` | ").append(access(r.access)).append(" | ")
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
            sb.append("\n");
            if (distinct.size() > 1) sb.append("**").append(pt[0]).append(".**\n\n");
            sb.append(wordBlock(pt[1]));
        }
        return sb.toString();
    }

    static String window() {
        return "| table | window | entries | entry | index from the address | load | store |\n|---|---|---|---|---|---|---|\n"
            + "| `HIST` | `" + hex(Registers.HIST_BASE) + "`–`" + hex(Registers.PTR_BASE - 1) + "` | 256 | one word, " + HIST_WIDTH + " bits | *i* = addr[9:2] | the count | the count |\n"
            + "| `PTR` | `" + hex(Registers.PTR_BASE) + "`–`" + hex(Registers.STREAM_BASE - 1) + "` | 256 | one word, " + PTR_WIDTH + " bits | *i* = addr[9:2] | bus error | the pointer |\n"
            + "| `STREAM` | `" + hex(Registers.STREAM_BASE) + "`–`" + hex(Registers.GROUP_BASE - 1) + "` | 256 | two words: low, high | *i* = addr[10:3]; addr[2]: 0 low, 1 high | bus error | one word |\n"
            + "| `GROUP` | `" + hex(Registers.GROUP_BASE) + "`–`" + hex(Registers.TABLES_END - 1) + "` | 64 | two words: low, high | *g* = addr[8:3]; addr[2]: 0 low, 1 high | bus error | one word |\n"
            + "| — | `" + hex(Registers.TABLES_END) + "`–`" + hex(Registers.TABLE_END - 1) + "` | — | unmapped | — | bus error | bus error |\n";
    }

    static String entry(String id) {
        if (!List.of("HIST", "PTR", "STREAM_LO", "STREAM_HI", "GROUP_LO", "GROUP_HI").contains(id)) throw new IllegalArgumentException("unknown entry " + id);
        return wordBlock(id);
    }

    // ---- packets
    /** Per mode: mesh.h name (DIRECT only), packet, lands, uses addr, ROUTER_SET_HDR's addr after a send, counter, STREAM. */
    static final String[][] MODES = {
        { "0", "", "invalid", "dropped; bad mode", "no", "unchanged", "—", "no" },
        { "1", "MODE_GATHER", "gather request (to the tile itself)", "the GATHER engine", "no", "unchanged", "— (its values count)", "yes, *d* = 0" },
        { "2", "MODE_SRC", "write by source", "big SRAM at `PTR[src]`", "no", "unchanged", "`DMA_SRAM_REMAINING`", "yes" },
        { "3", "MODE_BUCKET", "bucket write", "big SRAM at `PTR[bucket digit]`", "no", "unchanged", "`DMA_SRAM_REMAINING`", "yes" },
        { "4", "MODE_AUG", "augmented write", "big SRAM at `PTR[src]`, ordinal in [63:32]", "no", "unchanged", "`DMA_SRAM_REMAINING`", "yes" },
        { "5", "MODE_CMEM", "controller-memory write, two words", "instruction or data memory at addr", "yes", "+2", "`DMA_CM_REMAINING`", "yes" },
        { "6", "MODE_MBOX", "mailbox write, low word", "mailbox slot addr[7:0]", "yes", "+1", "— (slot 0: command landed)", "no" },
        { "7", "MODE_REPLY", "reply, low word", "data memory at addr (outside the mailbox)", "yes", "+1", "`DMA_REPLY_REMAINING`", "no" },
        { "8–15", "", "reserved", "dropped; bad mode", "no", "unchanged", "—", "no" },
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
        { "dst", 7, "the sender: tile 0–63, 64 C&C, 126 broadcast", "`ROUTER_SET_HDR`.dst; `STREAM`: (`me` + *d*) mod 64" },
        { "mode", 4, "the sender", "`ROUTER_SET_HDR`.mode; `STREAM`.mode, zero-extended" },
        { "addr", 20, "the sender, for modes 5–7; 0 otherwise", "`ROUTER_SET_HDR`.addr; `STREAM`: the cm word for mode 5, else 0; the read port: `SRAM_RD_DEST_ADDR`" },
        { "payload", 64, "the sender", "`ROUTER_SET_HI`:`ROUTER_SEND_LO`; `STREAM`: the element; the read port: the chunk read" },
    };
    static String linkWord() {
        StringBuilder sb = new StringBuilder("| field | width | set by | from a program |\n|---|---|---|---|\n");
        int total = 0;
        for (Object[] f : LINK) { sb.append("| ").append(f[0]).append(" | ").append(f[1]).append(" | ").append(f[2]).append(" | ").append(f[3]).append(" |\n"); total += (Integer) f[1]; }
        sb.append("| **total** | **").append(total).append("** | | |\n");
        return sb.toString();
    }
    static String cmWord() {
        int mb = 0x800 + ((Registers.MAILBOX_BASE - Registers.DMEM_BASE) >> 2);
        return "| packed word | byte address | memory |\n|---|---|---|\n"
            + "| `0x000`–`0x7FF` | `" + hex(Registers.IMEM_BASE) + "`–`" + hex(Registers.IMEM_BASE + Registers.IMEM_SIZE - 4) + "` | instruction memory (mode 5 only) |\n"
            + "| `0x800`–`" + hex(mb - 1) + "` | `" + hex(Registers.DMEM_BASE) + "`–`" + hex(Registers.MAILBOX_BASE - 4) + "` | data memory below the mailbox (modes 5 and 7) |\n"
            + "| `" + hex(mb) + "`–`0xFFF` | `" + hex(Registers.MAILBOX_BASE) + "`–`" + hex(Registers.DMEM_BASE + Registers.DMEM_SIZE - 4) + "` | the mailbox (mode 6 writes it by slot; modes 5 and 7 must not) |\n";
    }
    /** The three producers of controller-memory writes and their address sources (section 2.5). */
    static String cmWrites() {
        return "| producer | packets | typical use | the address comes from | advances | counted in |\n|---|---|---|---|---|---|\n"
            + "| the address generator | mode 5 (from a `STREAM` entry) | block copies and vector exchanges | the `STREAM` entry's cm word | +2 per packet | `DMA_CM_REMAINING` |\n"
            + "| the send port | mode 5 or 7 | words or vectors sent by a program | `ROUTER_SET_HDR`'s addr | +2 (mode 5) or +1 (mode 7) per packet | `DMA_CM_REMAINING` or `DMA_REPLY_REMAINING` |\n"
            + "| the read port | mode 7, to the tile itself | scattered reads: searches, hashing | `SRAM_RD_DEST_ADDR` | +1 per read | `DMA_REPLY_REMAINING` |\n";
    }
    /** Mailbox slots 0-63 are C&C's (spec 4.12). */
    static final int CNC_SLOTS = 64;
    static String command() {
        int ae = Registers.ARGS_EVEN, ao = Registers.ARGS_ODD;
        return wordBlock("CMD") + "\n| mailbox slots | use |\n|---|---|\n"
            + "| 0 | the command word, written last |\n"
            + "| " + ae + "–" + (ao - 1) + " | arguments of an even command |\n"
            + "| " + ao + "–" + (CNC_SLOTS - 1) + " | arguments of an odd command |\n"
            + "| " + CNC_SLOTS + "–" + (Registers.MAILBOX_SLOTS - 1) + " | free for other mode-6 uses |\n";
    }

    // ================================================================ the document
    static final Pattern BLOCK = Pattern.compile("<!-- generated: ([^>]*?) -->\\n(.*?)<!-- end generated -->", Pattern.DOTALL);
    static final List<String> REQUIRED = List.of("addressmap", "summary", "window", "entry HIST", "entry PTR", "entry STREAM_LO",
            "entry STREAM_HI", "entry GROUP_LO", "entry GROUP_HI", "modes", "linkword", "cmword", "cmwrites", "command");

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
        System.out.println("RegDoc: mesh.h 1.0");
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
