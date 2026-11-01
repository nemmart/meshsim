import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Stage A's loader and byte-for-byte comparison against a tracer delivery (replay-inputs/1, its FORMAT.md).
 * For each delivered window: load the program's decode table, ROM and controller-fed templates, take one of the
 * window's state records and the streams its flags name, run one pass of the replayer model through the slot's
 * interface only (ReplayDriver), and compare every record with rows.bin. Also checks that every stream was consumed
 * exactly and that the end state equals end.bin.
 *
 * Usage: java ReplayDelivery <delivery dir> <variant: state | state-lru | state-pick> [program ...] [--keep-going]
 * Stops at the first failing window (prints its first mismatching row and field) unless --keep-going.
 */
public final class ReplayDelivery {
    static final String[] FIELDS = { "instr_idx", "opcode", "rd_reg", "rs1_reg", "rs2_reg", "seq_remaining",
            "is_compressed", "is_virtual", "is_first", "rd_is_imm", "rs1_is_imm", "rs2_is_imm", "spare",
            "pc", "imm", "rs1", "rs2", "rd_prev", "rd", "mem_addr", "mem_value" };

    public static void main(String[] args) throws Exception {
        Path dir = Path.of(args[0]);
        String variant = args[1];
        boolean keepGoing = false;
        List<String> only = new ArrayList<>();
        for (int i = 2; i < args.length; i++) { if (args[i].equals("--keep-going")) keepGoing = true; else only.add(args[i]); }
        Map<String, Object> m = Json.object(Files.readString(dir.resolve("manifest.json")));
        int windows = 0, failed = 0;
        long rowsCompared = 0;
        for (Object po : Json.list(m.get("programs"))) {
            Map<String, Object> p = Json.object(po);
            String name = (String) p.get("name");
            if (!only.isEmpty() && !only.contains(name)) continue;
            Path pd = dir.resolve(name);
            Program prog = Program.load(pd);
            for (Object wo : Json.list(p.get("window_entries"))) {
                Map<String, Object> we = Json.object(wo);
                if (!we.containsKey("files")) continue;
                windows++;
                Result r = runWindow(prog, pd, we, variant);
                rowsCompared += r.rows;
                System.out.println(r.line(name, variant));
                if (!r.ok) {
                    failed++;
                    for (String d : r.detail) System.out.println("    " + d);
                    if (!keepGoing) { System.out.println("STOPPED at the first failing window"); System.exit(1); }
                }
            }
        }
        System.out.println(variant + ": " + windows + " windows, " + rowsCompared + " rows compared, " + failed + " failed");
        System.exit(failed == 0 ? 0 : 1);
    }

    // ================================================================ a program's files
    static final class Program {
        final short[] table = new short[2048];
        final long[] romLo = new long[512], romHi = new long[512];
        final Map<Integer, long[]> templates = new LinkedHashMap<>();
        final Map<Integer, Integer> rd0 = new LinkedHashMap<>();
        static Program load(Path pd) throws IOException {
            Program p = new Program();
            ByteBuffer t = le(Files.readAllBytes(pd.resolve("decode_table.bin")));
            for (int e = 0; e < 2048; e++) p.table[e] = t.getShort(2 * e);
            ByteBuffer r = le(Files.readAllBytes(pd.resolve("rom.bin")));
            for (int j = 0; j < 512; j++) { p.romLo[j] = r.getLong(16 * j); p.romHi[j] = r.getLong(16 * j + 8); }
            ByteBuffer words = le(Files.readAllBytes(pd.resolve("templates.bin")));
            byte[] ixb = Files.readAllBytes(pd.resolve("template_index.bin"));
            ByteBuffer ix = le(ixb);
            for (int k = 0; k < ixb.length / 16; k++) {                // count = file size / 16
                int w = ix.getInt(16 * k), off = ix.getInt(16 * k + 4), len = ix.getInt(16 * k + 8), rd0 = ix.get(16 * k + 12) & 0xFF;
                long[] tw = new long[2 * len];
                for (int i = 0; i < len; i++) { tw[2 * i] = words.getLong(16 * (off + i)); tw[2 * i + 1] = words.getLong(16 * (off + i) + 8); }
                p.templates.put(w, tw); p.rd0.put(w, rd0);
            }
            return p;
        }
    }

    // ================================================================ one window
    static final class Result {
        boolean ok = true; long rows; String summary = ""; final List<String> detail = new ArrayList<>(); int index; long cycles;
        String line(String prog, String variant) {
            return String.format("%-12s window %5d  %-10s %s  %6d rows  %8d cycles  %s", prog, index, variant, ok ? "OK  " : "FAIL", rows, cycles, summary);
        }
    }

    static Result runWindow(Program prog, Path pd, Map<String, Object> we, String variant) throws IOException {
        Result res = new Result();
        int index = ((Number) we.get("index")).intValue(), rows = ((Number) we.get("rows")).intValue();
        res.index = index;
        String base = String.format("window_%05d.", index);
        ByteBuffer rec = le(Files.readAllBytes(pd.resolve(base + variant + ".bin")));
        ReplayWindow w = new ReplayWindow();
        w.rows = rows;
        System.arraycopy(prog.table, 0, w.table, 0, 2048);
        System.arraycopy(prog.romLo, 0, w.romLo, 0, 512); System.arraycopy(prog.romHi, 0, w.romHi, 0, 512);
        w.templates.putAll(prog.templates); w.templateRd0.putAll(prog.rd0);
        w.region0 = new long[132];
        for (int i = 0; i < 132; i++) w.region0[i] = rec.getLong(16 + 8 * i);
        long sHi = w.region0[1], lLo = w.region0[2], lHi = w.region0[3];
        boolean cc = ((sHi >>> 48) & 1) != 0, rc = ((sHi >>> 49) & 1) != 0, wc = ((sHi >>> 50) & 1) != 0;
        w.code = words(pd.resolve(base + (cc ? "code-lru" : "code") + ".bin"));
        w.rlog = words(pd.resolve(base + (rc ? "rlog-lru" : "rlog") + ".bin"));
        w.wlog = words(pd.resolve(base + (wc ? "wlog-lru" : "wlog") + ".bin"));
        long codeLen = lLo & 0xFFFFFF, rlogLen = (lLo >>> 32) & 0xFFFFFF, wlogLen = lHi & 0xFFFFFF;
        res.summary = "streams " + (cc ? "C" : "r") + (rc ? "C" : "r") + (wc ? "C" : "r");

        ReplayDriver d = new ReplayDriver(w);
        d.load();
        d.pass(40L * rows + 100_000);
        res.cycles = d.r.passEndAt - d.r.passStartAt;

        // ---- the rows, byte for byte
        ByteBuffer ref = le(Files.readAllBytes(pd.resolve(base + "rows.bin")));
        int nref = ref.capacity() / 80;
        res.rows = Math.min(nref, d.rows.size());
        if (d.rows.size() != nref) { res.ok = false; res.detail.add("row count: model " + d.rows.size() + ", reference " + nref); }
        ByteBuffer diag = Files.exists(pd.resolve(base + "diag.bin")) ? le(Files.readAllBytes(pd.resolve(base + "diag.bin"))) : null;
        for (int r = 0; r < res.rows; r++) {
            long[] g = d.rows.get(r);
            boolean same = true;
            for (int k = 0; k < 10; k++) if (g[k] != ref.getLong(80 * r + 8 * k)) { same = false; break; }
            if (same) continue;
            long[] e = new long[10];
            for (int k = 0; k < 10; k++) e[k] = ref.getLong(80 * r + 8 * k);
            res.ok = false;
            res.detail.add("first mismatching row " + r + " (trace row " + (((Number) we.get("first_row")).longValue() + r) + ")");
            long[] gf = fields(g), ef = fields(e);
            for (int f = 0; f < FIELDS.length; f++)
                if (gf[f] != ef[f]) res.detail.add(String.format("  %-14s model 0x%x, reference 0x%x", FIELDS[f], gf[f], ef[f]));
            res.detail.add("  reference row: " + describe(ef));
            res.detail.add("  model row:     " + describe(gf));
            if (diag != null) {
                int c = diag.getInt(16 * r), lg = diag.getInt(16 * r + 4), de = diag.getInt(16 * r + 8), fl = diag.getInt(16 * r + 12);
                res.detail.add(String.format("  diag: code word %d, log %s %d, decode entry %d, controller number %s, flags 0x%x (log kind %d)",
                        c, new String[] { "none", "read", "write", "?" }[(lg >>> 30) & 3], lg & 0x3FFFFFFF, de & 0xFFFF,
                        (de >>> 16) == 0xFFFF ? "-" : String.valueOf(de >>> 16), fl & 0xFF, (fl >>> 8) & 0xFF));
            }
            for (int q = Math.max(0, r - 2); q < r; q++) res.detail.add("  row " + q + " (matched): " + describe(fields(d.rows.get(q))));
            break;
        }
        // ---- the streams consumed exactly, the end state, the replayer clean
        if (res.ok) {
            if (d.r.code.consumed != codeLen || d.r.rlog.consumed != rlogLen || d.r.wlog.consumed != wlogLen) {
                res.ok = false;
                res.detail.add("streams not consumed exactly: code " + d.r.code.consumed + "/" + codeLen + ", read log " + d.r.rlog.consumed + "/" + rlogLen + ", write log " + d.r.wlog.consumed + "/" + wlogLen + " bits");
            }
            Path endp = pd.resolve(base + "end.bin");
            if (Files.exists(endp)) {
                ByteBuffer end = le(Files.readAllBytes(endp));
                long epc = end.getLong(16), eHi = end.getLong(24);
                if (d.r.pc() != epc || (d.r.instrIdx() & 0xFFFFFFFFL) != (eHi & 0xFFFFFFFFL) || d.r.nextOffsetNow() != ((eHi >>> 32) & 0xFFFF)) {
                    res.ok = false;
                    res.detail.add(String.format("end state: model pc 0x%x idx %d offset %d; end.bin pc 0x%x idx %d offset %d", d.r.pc(), d.r.instrIdx(),
                            d.r.nextOffsetNow(), epc, eHi & 0xFFFFFFFFL, (eHi >>> 32) & 0xFFFF));
                }
                for (int i = 1; i < 128; i++) {
                    long ev = end.getLong(48 + 8 * i);
                    if (d.r.reg(i) != ev) { res.ok = false; res.detail.add(String.format("end state: register %d model 0x%x, end.bin 0x%x", i, d.r.reg(i), ev)); break; }
                }
            }
            if (!d.r.violations.isEmpty() || !d.r.invalids.isEmpty() || !d.r.overruns.isEmpty() || !d.r.badWrites.isEmpty() || d.missingTemplates > 0) {
                res.ok = false;
                res.detail.add("replayer: violations " + d.r.violations + ", invalid " + d.r.invalids + ", overruns " + d.r.overruns + ", bad writes " + d.r.badWrites + ", missing templates " + d.log);
            }
        }
        res.summary += "  newexp " + d.r.newExpansions + ", stalls " + java.util.Arrays.toString(d.r.stalls);
        return res;
    }

    static long[] fields(long[] g) {
        return new long[] { g[0] & 0xFFFFFFFFL, (g[0] >>> 32) & 0xFF, (g[0] >>> 40) & 0xFF, (g[0] >>> 48) & 0xFF, g[0] >>> 56,
                g[1] & 0xFFFF, (g[1] >>> 16) & 1, (g[1] >>> 17) & 1, (g[1] >>> 18) & 1, (g[1] >>> 19) & 1, (g[1] >>> 20) & 1, (g[1] >>> 21) & 1,
                g[1] >>> 22, g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9] };
    }
    static String describe(long[] f) {
        return String.format("idx %d %s rd %d rs1 %d rs2 %d seq %d c%d v%d f%d imm%d%d%d pc 0x%x imm 0x%x rs1 0x%x rs2 0x%x rd_prev 0x%x rd 0x%x mem 0x%x/0x%x",
                f[0], ReplayerAlu.name((int) f[1]), f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[13], f[14], f[15], f[16], f[17], f[18], f[19], f[20]);
    }

    static ByteBuffer le(byte[] b) { return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN); }
    static long[] words(Path p) throws IOException {
        ByteBuffer b = le(Files.readAllBytes(p));
        long[] r = new long[b.capacity() / 8];
        for (int i = 0; i < r.length; i++) r[i] = b.getLong(8 * i);
        return r;
    }

    // ================================================================ a minimal JSON reader (objects, arrays, strings, numbers, literals)
    static final class Json {
        final String s; int i;
        Json(String s) { this.s = s; }
        @SuppressWarnings("unchecked") static Map<String, Object> object(Object o) { return (Map<String, Object>) (o instanceof String ? new Json((String) o).value() : o); }
        @SuppressWarnings("unchecked") static List<Object> list(Object o) { return (List<Object>) o; }
        void ws() { while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++; }
        Object value() {
            ws();
            char c = s.charAt(i);
            if (c == '{') {
                Map<String, Object> m = new LinkedHashMap<>(); i++; ws();
                if (s.charAt(i) == '}') { i++; return m; }
                while (true) { ws(); String k = (String) value(); ws(); i++; m.put(k, value()); ws(); if (s.charAt(i++) == '}') return m; }
            }
            if (c == '[') {
                List<Object> l = new ArrayList<>(); i++; ws();
                if (s.charAt(i) == ']') { i++; return l; }
                while (true) { l.add(value()); ws(); if (s.charAt(i++) == ']') return l; }
            }
            if (c == '"') {
                StringBuilder b = new StringBuilder(); i++;
                while (true) {
                    char ch = s.charAt(i++);
                    if (ch == '"') return b.toString();
                    if (ch == '\\') {
                        char e = s.charAt(i++);
                        switch (e) {
                            case 'n' -> b.append('\n'); case 't' -> b.append('\t'); case 'r' -> b.append('\r'); case 'b' -> b.append('\b'); case 'f' -> b.append('\f');
                            case 'u' -> { b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; }
                            default -> b.append(e);
                        }
                    } else b.append(ch);
                }
            }
            if (s.startsWith("true", i)) { i += 4; return Boolean.TRUE; }
            if (s.startsWith("false", i)) { i += 5; return Boolean.FALSE; }
            if (s.startsWith("null", i)) { i += 4; return null; }
            int j = i;
            while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
            String n = s.substring(j, i);
            if (n.contains(".") || n.contains("e") || n.contains("E")) return Double.parseDouble(n);
            return Long.parseLong(n);
        }
    }
}
