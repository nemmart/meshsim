import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Stage B: the replay controller (sw/replay/replay_ctl.c) on a meshsim tile, feeding the tile's replayer through the
 * peripheral slot, against a tracer delivery (replay-inputs/1). For each window: the Java-side loader writes the window
 * into tile 0's big SRAM as sw/replay/replay_layout.h lays it out; C&C boots replay_ctl on tile 0 (the other tiles run
 * only the loader, which ebreaks); the program loads the tables and region 0, runs the pass with the 4.19.9 service loop
 * and returns. The rows the replayer emits are compared byte for byte with rows.bin, as Stage A does
 * (ReplayDelivery), and so are the streams' consumption and the end state.
 *
 * The figure that matters is cycles per row: the pass (the replayer's run to its end), against Stage A's ideal
 * controller (ReplayDriver: every line answered in the cycle it is seen, the port at one packet a cycle).
 *
 * Usage: java StageB <delivery dir> <variant: state | state-lru | state-pick> <program> [window ...]
 * (no windows: every delivered window of the program). Requires sw/replay/build (sw/replay/build.sh).
 */
public final class StageB {
    static final int HDR = 0x00000, TABLES = 0x00040, INDEX = 0x00C40, RECORD = 0x00E40, TEMPLATES = 0x01000,
            CODE = 0x08000, RLOG = 0x18000, WLOG = 0x38000;

    public static void main(String[] args) throws Exception {
        Path dir = Path.of(args[0]);
        String variant = args[1], prog = args[2];
        List<Integer> only = new ArrayList<>();
        for (int i = 3; i < args.length; i++) only.add(Integer.parseInt(args[i]));
        Map<String, Object> m = ReplayDelivery.Json.object(Files.readString(dir.resolve("manifest.json")));
        Path pd = dir.resolve(prog);
        Image im = new Image(Path.of("sw/replay/build"));
        int windows = 0, failed = 0;
        for (Object po : ReplayDelivery.Json.list(m.get("programs"))) {
            Map<String, Object> p = ReplayDelivery.Json.object(po);
            if (!prog.equals(p.get("name"))) continue;
            for (Object wo : ReplayDelivery.Json.list(p.get("window_entries"))) {
                Map<String, Object> we = ReplayDelivery.Json.object(wo);
                if (!we.containsKey("files")) continue;
                int index = ((Number) we.get("index")).intValue();
                if (!only.isEmpty() && !only.contains(index)) continue;
                windows++;
                if (!runWindow(im, pd, we, variant, prog)) failed++;
            }
        }
        System.out.println("StageB " + prog + " " + variant + ": " + windows + " windows, " + failed + " failed");
        System.exit(failed == 0 && windows > 0 ? 0 : 1);
    }

    static final class Image {
        final int[] code, data; final Map<String, Integer> sym = new HashMap<>();
        Image(Path b) throws IOException {
            code = words(b.resolve("replay_ctl.code.bin")); data = words(b.resolve("replay_ctl.data.bin"));
            for (String l : Files.readAllLines(b.resolve("replay_ctl.sym"))) { String[] f = l.trim().split("\\s+"); if (f.length == 3) sym.put(f[2], (int) Long.parseLong(f[0], 16)); }
        }
        static int[] words(Path p) throws IOException {
            byte[] b = Files.readAllBytes(p); ByteBuffer bb = ReplayDelivery.le(b);
            int[] w = new int[(b.length + 3) / 4]; for (int i = 0; i < b.length / 4; i++) w[i] = bb.getInt(4 * i); return w;
        }
    }

    /** Put bytes into tile p's big SRAM from chunk c, one 32-bit word per chunk; returns the chunks used, rounded up to 4. */
    static int put(Tile p, int c, byte[] b) {
        ByteBuffer bb = ReplayDelivery.le(java.util.Arrays.copyOf(b, (b.length + 15) & ~15));
        int n = bb.capacity() / 4;
        for (int i = 0; i < n; i++) p.sram.poke(c + i, bb.getInt(4 * i));
        return n;
    }

    static boolean runWindow(Image im, Path pd, Map<String, Object> we, String variant, String prog) throws IOException {
        int index = ((Number) we.get("index")).intValue(), rows = ((Number) we.get("rows")).intValue();
        String base = String.format("window_%05d.", index);
        Config k = Config.machine(); k.periphType = 0;
        Replayer[] rp = new Replayer[1];
        k.testPeripheral = tile -> tile == 0 ? (rp[0] = new Replayer(0, rows)) : null;
        Chip c = new Chip(k);
        Tile p = c.tiles[0];
        Replayer r = rp[0];
        List<long[]> got = new ArrayList<>();
        r.rowListener = got::add;

        // ---- the loader: the window into tile 0's big SRAM (replay_layout.h)
        byte[] table = Files.readAllBytes(pd.resolve("decode_table.bin")), rom = Files.readAllBytes(pd.resolve("rom.bin"));
        byte[] tables = new byte[table.length + rom.length];
        System.arraycopy(table, 0, tables, 0, table.length); System.arraycopy(rom, 0, tables, table.length, rom.length);
        put(p, TABLES, tables);
        byte[] ixb = Files.readAllBytes(pd.resolve("template_index.bin"));
        put(p, INDEX, ixb);
        put(p, TEMPLATES, Files.readAllBytes(pd.resolve("templates.bin")));
        byte[] rec = Files.readAllBytes(pd.resolve(base + variant + ".bin"));
        put(p, RECORD, rec);
        ByteBuffer rb = ReplayDelivery.le(rec);
        long sHi = rb.getLong(16 + 8), lLo = rb.getLong(16 + 16), lHi = rb.getLong(16 + 24);
        boolean cc = ((sHi >>> 48) & 1) != 0, rc = ((sHi >>> 49) & 1) != 0, wc = ((sHi >>> 50) & 1) != 0;
        byte[] code = Files.readAllBytes(pd.resolve(base + (cc ? "code-lru" : "code") + ".bin"));
        byte[] rlog = Files.readAllBytes(pd.resolve(base + (rc ? "rlog-lru" : "rlog") + ".bin"));
        byte[] wlog = Files.readAllBytes(pd.resolve(base + (wc ? "wlog-lru" : "wlog") + ".bin"));
        put(p, CODE, code); put(p, RLOG, rlog); put(p, WLOG, wlog);
        int[] h = new int[16];
        h[0] = 0x594C5052; h[1] = 1; h[2] = TABLES; h[3] = INDEX; h[4] = ixb.length / 16; h[5] = TEMPLATES; h[6] = RECORD;
        h[7] = CODE; h[8] = code.length / 16; h[9] = RLOG; h[10] = rlog.length / 16; h[11] = WLOG; h[12] = wlog.length / 16; h[13] = 1;
        for (int i = 0; i < 16; i++) p.sram.poke(HDR + i, h[i]);

        // ---- boot: the loader everywhere; replay_ctl and its boot marker on tile 0 only (the others' loaders ebreak)
        int[] loader = Image.words(Path.of("sw/build/loader.bin"));
        c.cc.loadWords(LinkWord.BROADCAST, Registers.LOADER_BASE, loader)
            .loadWords(0, Registers.IMEM_BASE, im.code);
        if (im.data.length > 0) c.cc.loadWords(0, Registers.DMEM_BASE, im.data);
        c.cc.command(0, Registers.CMD_JUMP << 24, im.sym.get("_start"))
            .waitCycles(200).then((cnc, t) -> { cnc.storeRun(t, true); return true; });
        // -Dstageb.profile: the controller's pc during the replayer's new-expansion stalls, by function
        final boolean prof = System.getProperty("stageb.profile") != null;
        final Map<Integer, Long> pcs = new HashMap<>(); final long[] last = { 0 };
        boolean ok = c.runUntil(() -> {
            if (prof) { long s2 = r.stalls[2]; if (s2 != last[0]) { pcs.merge(p.core.inWfi ? -1 : p.core.pc, 1L, Long::sum); last[0] = s2; } }
            return p.core.halt != Controller.RUN && p.core.halt != 0 || c.cycle > 40L * rows + 2_000_000;
        }, 60L * rows + 3_000_000);
        if (prof) {
            Map<String, Long> byFn = new java.util.TreeMap<>();
            for (var e : pcs.entrySet()) {
                String fn = "WFI"; if (e.getKey() >= 0) { int best = -1; for (var sy : im.sym.entrySet()) if (sy.getValue() <= e.getKey() && sy.getValue() > best && sy.getValue() < 0x2000) { best = sy.getValue(); fn = sy.getKey(); } }
                byFn.merge(fn, e.getValue(), Long::sum);
            }
            System.out.println("  new-expansion stall cycles by the controller's pc: " + byFn);
            pcs.entrySet().stream().sorted((x, y) -> Long.compare(y.getValue(), x.getValue())).limit(40)
               .forEach(e -> System.out.println(String.format("    pc 0x%x %d", e.getKey(), e.getValue())));
        }
        ok = p.core.halt == Controller.HALT_EBREAK;
        int res = im.sym.get("rp_results");
        int status = p.core.peekLocal(res), passes = p.core.peekLocal(res + 4), cond = p.core.peekLocal(res + 8),
            newexp = p.core.peekLocal(res + 12), refills = p.core.peekLocal(res + 16), missing = p.core.peekLocal(res + 20),
            slow = im.sym.containsKey("rp_results") ? p.core.peekLocal(res + 24) : 0;

        // ---- the rows, the streams, the end state (as Stage A)
        List<String> detail = new ArrayList<>();
        ByteBuffer ref = ReplayDelivery.le(Files.readAllBytes(pd.resolve(base + "rows.bin")));
        int nref = ref.capacity() / 80, same = Math.min(nref, got.size());
        if (got.size() != nref) detail.add("row count: tile " + got.size() + ", reference " + nref);
        for (int i = 0; i < same; i++) {
            long[] g = got.get(i); boolean eq = true;
            for (int f = 0; f < 10; f++) if (g[f] != ref.getLong(80 * i + 8 * f)) { eq = false; break; }
            if (!eq) { detail.add("first mismatching row " + i); break; }
        }
        long codeLen = lLo & 0xFFFFFF, rlogLen = (lLo >>> 32) & 0xFFFFFF, wlogLen = lHi & 0xFFFFFF;
        if (r.code.consumed != codeLen || r.rlog.consumed != rlogLen || r.wlog.consumed != wlogLen)
            detail.add("streams not consumed exactly: code " + r.code.consumed + "/" + codeLen + ", read log " + r.rlog.consumed + "/" + rlogLen + ", write log " + r.wlog.consumed + "/" + wlogLen);
        Path endp = pd.resolve(base + "end.bin");
        if (Files.exists(endp)) {
            ByteBuffer end = ReplayDelivery.le(Files.readAllBytes(endp));
            long epc = end.getLong(16), eHi = end.getLong(24);
            if (r.pc() != epc || (r.instrIdx() & 0xFFFFFFFFL) != (eHi & 0xFFFFFFFFL)) detail.add("end state differs");
            for (int i = 1; i < 128; i++) if (r.reg(i) != end.getLong(48 + 8 * i)) { detail.add("end state: register " + i); break; }
        }
        if (!r.violations.isEmpty() || !r.invalids.isEmpty() || !r.overruns.isEmpty() || !r.badWrites.isEmpty())
            detail.add("replayer: violations " + r.violations + ", invalid " + r.invalids + ", overruns " + r.overruns + ", bad writes " + r.badWrites);
        if (c.violations > 0) detail.add("meshsim: " + c.violationReport().replace("\n", "; "));
        if (!ok) detail.add("tile 0 did not reach its ebreak: " + p.core.halt + " pc 0x" + Integer.toHexString(p.core.pc));
        if (status != 1) detail.add("rp_results: status " + status + " conditions 0x" + Integer.toHexString(cond) + " missing 0x" + Integer.toHexString(missing));
        boolean pass = detail.isEmpty();
        long cyc = r.passEndAt - r.passStartAt;
        System.out.println(String.format("%-12s window %5d  %-10s %s  %6d rows  pass %8d cycles  %.3f cycles/row  newexp %d (served %d, slow path %d), refills %d, stalls %s  [meas: StageB]",
                prog, index, variant, pass ? "OK  " : "FAIL", same, cyc, (double) cyc / Math.max(1, same), r.newExpansions, newexp, slow, refills,
                java.util.Arrays.toString(r.stalls)));
        for (String d : detail) System.out.println("    " + d);
        return pass;
    }
}
