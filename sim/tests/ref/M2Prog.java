import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * WORKER7's driver for M2's smaller references (sim/tests/AUDIT-M2.md section 7): CtrlTest's `regs`, `wfi` and
 * all-to-all from C (`a2a`, uniform Data(7, 2000)), each exactly as CtrlTest runs it (Mach.cfg(-1), Mach.boot, the same
 * C&C sequence and stop condition), with FpHooks between cycles. Prints the run's facts: the end cycle, exits,
 * violations, the program's own result check, the end-of-run canonical fingerprint, ref's digest.
 *
 *   java -cp REF_CLASSES:THIS M2Prog regs|wfi|a2a [seed=-1] [trace=TILE,FILE] [fp=FILE fpevery=N fptiles=.. ...]
 */
final class M2Prog {
    public static void main(String[] a) throws Exception {
        String prog = a[0];
        Map<String, String> kv = new LinkedHashMap<>();
        for (int i = 1; i < a.length; i++) { int e = a[i].indexOf('='); kv.put(a[i].substring(0, e), a[i].substring(e + 1)); }
        Mach m = new Mach(Mach.cfg(Long.parseLong(kv.getOrDefault("seed", "-1"))));
        m.c.cfg.maxViolationMessages = 1000;
        A2A data = null;
        if (prog.equals("a2a")) {                                // CtrlTest.allToAll's first set, gap 0
            data = new A2A(7, 2000);
            for (Tile p : m.c.tiles) {
                int s = p.idx;
                for (int k = 0; k < data.rec[s].length; k++) p.sram.poke64(A2A.SRC + 2 * k, data.rec[s][k]);
                int base = 0xF0000;
                for (int d = 0; d < 64; d++) p.sram.poke(base + d, data.cnt[s][d]);
                for (int src = 0; src < 64; src++) p.sram.poke(base + 64 + src, data.cnt[src][s]);
                for (int g = 0; g < 16; g++) p.sram.poke(base + 128 + g, data.R[g]);
                p.sram.poke(base + 144, 0);
            }
        }
        Mach.Image im = m.boot(prog);
        long max;
        switch (prog) {
            case "regs" -> max = 40000;
            case "wfi" -> {
                m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().waitCycles(700).command(LinkWord.BROADCAST, 3 << 24).go();
                max = 20000;
            }
            case "a2a" -> { m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go(); max = 2_000_000; }
            default -> throw new IllegalArgumentException(prog);
        }
        PrintWriter traceW = null; int traceTile = -1;
        if (kv.containsKey("trace")) {
            String[] t = kv.get("trace").split(",", 2); traceTile = Integer.parseInt(t[0]);
            traceW = new PrintWriter(new FileWriter(t[1]));
            Observer.start(traceW, traceTile);
            TraceRun.header(m.c.cfg, m.c, traceTile, prog, Mach.SW.resolve(prog + ".code.bin"), Mach.SW.resolve(prog + ".data.bin"),
                    im.sym("_data_start"), im.sym("_start"), List.of("M2Prog " + String.join(" ", a)));
        }
        FpHooks h = new FpHooks(m.c, kv);
        boolean ok = m.run(() -> { h.cycle(); return m.allStopped(); }, max);
        h.end();
        if (traceW != null) { TraceRun.closing(m.c, m.c.tiles[traceTile].core, "run end"); Observer.stop(); }
        String result = switch (prog) {
            case "regs" -> "r[35] (steps run) on every tile = " + allWord(m, im, "r", 35) + ", counter wraps " + sum(m, true);
            case "wfi" -> "WFIs per tile = " + wfis(m) + ", r[1] (self-sends) on every tile = " + allWord(m, im, "r", 1);
            default -> "records misplaced " + data.verify(m.c) + "; phases (all READY, GO): " + phases(m);
        };
        System.out.printf("%s: %s at cycle %d; every tile exited %b (%s); violations %d; %s%n", prog, ok ? "stopped" : "NOT stopped", m.c.cycle,
                m.allExited(), m.haltSummary().isEmpty() ? "all 0" : m.haltSummary(), m.c.violations, result);
        StateFp.Out o = new StateFp.Out(); long all = 0x6A09E667F3BCC908L; int n = 0;
        for (Tile p : m.c.tiles) for (String u : StateFp.TILE_UNITS) { o = new StateFp.Out(); StateFp.unit(o, m.c, p, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); n++; }
        for (String u : StateFp.CHIP_UNITS) { o = new StateFp.Out(); StateFp.chipUnit(o, m.c, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); n++; }
        System.out.printf("state fingerprint at the end (chip): %016x%n", StateFp.fmix(all ^ n));
        System.out.println("digest " + m.c.digest());
        System.exit(ok && m.allExited() ? 0 : 1);
    }
    static String allWord(Mach m, Mach.Image im, String s, int i) {
        int v = m.word(0, im, s, i); for (Tile p : m.c.tiles) if (m.word(p.idx, im, s, i) != v) return "differs (tile " + p.idx + ")";
        return "0x" + Integer.toHexString(v);
    }
    static long sum(Mach m, boolean wraps) { long s = 0; for (Tile p : m.c.tiles) s += p.ctl.counterWraps; return s; }
    static String wfis(Mach m) { int v = m.c.tiles[0].core.wfiLog.size(); for (Tile p : m.c.tiles) if (p.core.wfiLog.size() != v) return "differs"; return "" + v; }
    static String phases(Mach m) { StringBuilder b = new StringBuilder(); for (long[] x : m.c.cc.phaseLog) b.append(x[0]).append('/').append(x[1]).append(' '); return b.toString().trim(); }
}
