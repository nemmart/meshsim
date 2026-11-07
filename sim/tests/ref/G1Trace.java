import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/**
 * What crosses one tile's G1 boundary in ref (WORKER8), so G1's units can be checked against golden-m2 before the
 * network exists in C++: runs regs or wfi exactly as M2Prog does (its setup, copied) and writes, for one tile, every
 * packet the eject bus delivers to the director (with its arrival cycle), every GO the register block schedules (the
 * cycle it is scheduled and the cycle it acts), and every cycle the injection point takes the send holding register.
 * The C++ replay (tests/test_g1_replay.cpp) feeds these to G1's units through a stand-in network. Reflection only.
 *
 *   java -cp REF_CLASSES:THIS G1Trace regs|wfi TILE OUT
 * OUT lines: "E at a b c" (an eject arrival), "G s e" (GO scheduled at s, acting at endCycle(e)), "T t" (the send hold
 * taken at t), "END c" (the run's last cycle).
 */
final class G1Trace {
    public static void main(String[] a) throws Exception {
        String prog = a[0]; int tile = Integer.parseInt(a[1]);
        Map<String, String> kv = new LinkedHashMap<>();
        Mach m = new Mach(Mach.cfg(Long.parseLong("-1")));
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
        Tile p = m.c.tiles[tile];
        try (PrintWriter w = new PrintWriter(a[2])) {
            w.println("# G1 boundary of tile " + tile + " in M2Prog " + prog + " (sim/tests/ref/G1Trace.java)");
            long[] lastAt = { 0 }; Set<Long> gos = new HashSet<>(); boolean[] occPrev = { false };
            Runnable look = () -> {
                long c = m.c.cycle;
                WordDelayLine d = p.ejectBus;
                long[] va = (long[]) StateFp.o(d, "va"), vb = (long[]) StateFp.o(d, "vb"), vc = (long[]) StateFp.o(d, "vc"), at = (long[]) StateFp.o(d, "at");
                int head = StateFp.i(d, "head"), count = StateFp.i(d, "count");
                for (int k = 0; k < count; k++) { int j = (head + k) % va.length; if (at[j] > lastAt[0]) { w.printf("E %d %x %x %x%n", at[j], va[j], vb[j], vc[j]); lastAt[0] = at[j]; } }
                if (StateFp.z(d, "pendPut")) { long pa = StateFp.l(d, "pendAt"); if (pa > lastAt[0]) { w.printf("E %d %x %x %x%n", pa, StateFp.l(d, "pa"), StateFp.l(d, "pb"), StateFp.l(d, "pc")); lastAt[0] = pa; } }
                @SuppressWarnings("unchecked") ArrayDeque<Long> goQ = (ArrayDeque<Long>) StateFp.o(p.ctl, "goQ");
                for (long g : goQ) if (gos.add(g)) w.printf("G %d %d%n", c, g);
                WordHold h = p.sendHold;
                boolean occ = StateFp.z(h, "occ"), pend = StateFp.z(h, "pendPut"); long lastT = StateFp.l(h, "lastT");
                if (pend && lastT < c && !occ) occ = true;
                if (occPrev[0] && !occ) w.printf("T %d%n", c);
                occPrev[0] = occ || (pend && lastT == c);
            };
            look.run();
            boolean ok = m.run(() -> { look.run(); return m.allStopped(); }, max);
            w.println("END " + m.c.cycle + (ok ? "" : " (not stopped)"));
        }
    }
}
