import java.util.Random;

/**
 * The peripheral slot's conformance, ref's side (WORKER15; the C++ side is tests/slotconf.h, units::SlotCore).
 *
 * ref's PeripheralSlot taken from a Chip built as PeripheralSlotTest builds its machine (Config.defaults, a
 * TestPeripheral as tile 5's peripheral 1; tile 6 keeps its replayer), and driven directly, the Chip never stepped: a
 * seeded script of calls on the two slots (the test peripheral's posts, credits, live bit, faults and burst; region
 * accepts; window loads and stores, PULSE_RUN, EVENTS_MASK, consuming loads, DMA_HDR / DMA_ADDR / PULSE_DMA; the
 * replayer's RUN_CONFIG and PULSE_RUN; runCycle and endCycle). Every call's result and, after each cycle, the slots'
 * observations (live bits, counters, DMA packets, the test peripheral's state) are written.
 *
 *   java -cp REF:. SlotConf gen SEED CYCLES OUT     OUT.script and OUT.obs
 *   java -cp REF:. SlotConf run SCRIPT              the script replayed on ref: the observations on stdout
 *
 * Script: per cycle "t op op ...": slot index s (0: tile 5, the test peripheral; 1: tile 6, the replayer) in each op:
 *   P  post, C credit, L1 / L0 the live bit, F fault, B1 / B0 burst (test peripheral);
 *   A:s:off:lo:hi  a region-0x04 accept (src 7, 128 bits) at byte offset off;
 *   R:s:w  a window load (its value, "b" a bus error);  W:s:w:v  a window store (1 accepted, 0 a bus error);
 *   T:s:w  storeStalls;  S  runCycle and endCycle of both slots, then the observations.
 */
public final class SlotConf {
    public static void main(String[] a) throws Exception {
        switch (a[0]) {
            case "gen" -> gen(Long.parseLong(a[1]), Integer.parseInt(a[2]), a[3]);
            case "run" -> System.out.print(run(java.nio.file.Files.readAllLines(java.nio.file.Path.of(a[1]))));
            default -> { System.err.println("usage: SlotConf gen SEED CYCLES OUT | run SCRIPT"); System.exit(2); }
        }
    }

    static final class M {
        final TestPeripheral[] tp = new TestPeripheral[1];
        final Chip chip;
        final PeripheralSlot[] slot = new PeripheralSlot[2];
        int[] dmaSeen = new int[2];
        long[] last = new long[64];
        boolean first = true;
        final StringBuilder obs = new StringBuilder();
        StringBuilder line = new StringBuilder();
        M() {
            Config k = Config.defaults();
            k.testPeripheral = tile -> tile == 5 ? (tp[0] = new TestPeripheral()) : null;
            chip = new Chip(k);
            slot[0] = chip.tiles[5].slot; slot[1] = chip.tiles[6].slot;
        }
        void op(long t, String o) {
            String[] f = o.split(":");
            TestPeripheral x = tp[0];
            switch (f[0]) {
                case "P" -> x.post(t);
                case "C" -> x.credit(t);
                case "L1" -> x.level(true);
                case "L0" -> x.level(false);
                case "F" -> x.fault();
                case "B1" -> x.burst = true;
                case "B0" -> x.burst = false;
                case "A" -> {
                    int s = Integer.parseInt(f[1]), off = Integer.parseInt(f[2], 16);
                    long h = LinkWord.hdr(7, 5 + s, 0, 0, 0x04 << 24 | off, LinkWord.SIZE_128);
                    slot[s].accept(t, 1, Long.parseUnsignedLong(f[3], 16), h, Long.parseUnsignedLong(f[4], 16));
                }
                case "R" -> { Integer v = slot[Integer.parseInt(f[1])].read(t, 1, Integer.parseInt(f[2])); line.append(' ').append(v == null ? "b" : Integer.toHexString(v)); }
                case "W" -> { boolean ok = slot[Integer.parseInt(f[1])].write(t, 1, Integer.parseInt(f[2]), (int) Long.parseLong(f[3], 16)); line.append(' ').append(ok ? '1' : '0'); }
                case "T" -> line.append(' ').append(slot[Integer.parseInt(f[1])].storeStalls(t, 1, Integer.parseInt(f[2])) ? 's' : '-');
                case "S" -> {
                    for (PeripheralSlot p : slot) p.runCycle(t);
                    for (PeripheralSlot p : slot) p.endCycle(t);
                    observe(t);
                }
                default -> throw new IllegalArgumentException(o);
            }
        }
        void observe(long t) {
            long u = t + 1;
            long[] v = new long[64];
            int k = 0;
            for (PeripheralSlot p : slot) {
                v[k++] = p.liveEvents(u); v[k++] = p.liveErrors(u);
                v[k++] = p.accepted; v[k++] = p.reservedAccesses; v[k++] = p.violations;
                v[k++] = p.dma[1] == null ? 0 : (p.dma[1].busy(u) ? 1 : 0) | p.dma[1].doneBit(u) | (long) p.dma[1].transfers << 8 | (long) p.dma[1].packets << 24;
            }
            TestPeripheral x = tp[0];
            v[k++] = x.runs; v[k++] = x.scratch; v[k++] = x.errors; v[k++] = x.running ? 1 : 0; v[k++] = x.accepts.size();
            for (int b = 0; b < 5; b++) v[k++] = x.ev.count(b);
            v[k++] = x.ev.fullPosts;
            Replayer r = (Replayer) slot[1].p[1];
            v[k++] = r.running(u) ? 1 : 0; v[k++] = r.errors(u); r.sync(u); v[k++] = r.events().value(u);
            v[k++] = r.code.occ | r.rlog.occ << 9 | r.wlog.occ << 18 | (long) r.exp.occ << 27;
            v[k++] = r.rowsTotal;
            StringBuilder b = new StringBuilder();
            for (int i = 0; i < k; i++) if (first || v[i] != last[i]) { b.append(' ').append(i).append('=').append(Long.toHexString(v[i])); last[i] = v[i]; }
            for (int s = 0; s < 2; s++)
                for (; dmaSeen[s] < slot[s].dmaOut.size(); dmaSeen[s]++) {
                    long[] d = slot[s].dmaOut.get(dmaSeen[s]);   // {t, dst, mode, counter, addr, size, lo, hi}
                    b.append(" d").append(s).append('=').append(Long.toHexString(d[0])).append(',').append(Long.toHexString(d[1])).append(',')
                     .append(Long.toHexString(d[2])).append(',').append(Long.toHexString(d[3])).append(',').append(Long.toHexString(d[4])).append(',')
                     .append(Long.toHexString(d[6])).append(',').append(Long.toHexString(d[7]));
                }
            first = false;
            if (line.length() > 0 || b.length() > 0) obs.append(t).append(line).append(" |").append(b).append('\n');
            line = new StringBuilder();
        }
    }

    static String run(java.util.List<String> lines) {
        M m = new M();
        for (String l : lines) {
            if (l.isEmpty() || l.startsWith("#")) continue;
            String[] f = l.split(" ");
            long t = Long.parseLong(f[0]);
            for (int i = 1; i < f.length; i++) m.op(t, f[i]);
        }
        return "# slotconf observations v1\n" + m.obs;
    }

    // ================================================================ gen: a seeded script, run as it is made
    static void gen(long seed, int cycles, String out) throws Exception {
        Random g = new Random(seed);
        M m = new M();
        StringBuilder sc = new StringBuilder("# slotconf script v1: seed " + seed + " cycles " + cycles + "\n");
        TestPeripheral x = m.tp[0];
        Replayer r = (Replayer) m.slot[1].p[1];
        for (long t = 0; t < cycles; t++) {
            StringBuilder l = new StringBuilder().append(t);
            java.util.List<String> ops = new java.util.ArrayList<>();
            int n = g.nextInt(4);
            for (int i = 0; i < n; i++) {
                int k = g.nextInt(100);
                String o;
                if (k < 6) o = "P";
                else if (k < 12) o = "C";
                else if (k < 15) o = g.nextBoolean() ? "L1" : "L0";
                else if (k < 16) o = "F";
                else if (k < 18) o = g.nextBoolean() ? "B1" : "B0";
                else if (k < 24) o = "A:0:" + Integer.toHexString(16 * g.nextInt(4096)) + ":" + Long.toHexString(g.nextLong()) + ":" + Long.toHexString(g.nextLong());
                else if (k < 30) {
                    // the replayer's queues only (part 0 while running is undefined): parts 1-4, a 128-bit word each
                    o = "A:1:" + Integer.toHexString((1 + g.nextInt(4)) << 20 | 16 * g.nextInt(64)) + ":" + Long.toHexString(g.nextLong()) + ":" + Long.toHexString(g.nextLong());
                } else if (k < 60) {
                    int s = g.nextInt(4) == 0 ? 1 : 0;
                    int w = g.nextInt(3) == 0 ? g.nextInt(128) : new int[] { 0, 1, 2, 5, 5, 5, 6, 7, 8, 9, 32, 33, 34, 35, 36 }[g.nextInt(15)];
                    o = "R:" + s + ":" + w;
                } else if (k < 95) {
                    int s = g.nextInt(4) == 0 ? 1 : 0;
                    int w, v = g.nextInt(4) == 0 ? g.nextInt() : g.nextInt(8);
                    int c = g.nextInt(12);
                    if (s == 0) {
                        w = c < 3 ? 7 : c < 5 ? 3 : c < 6 ? 33 : c < 7 ? 8 : c < 8 ? 9 : c < 9 ? 4 : g.nextInt(128);
                        if (w == 7) v = g.nextInt(32);
                        if (w == 8) v = g.nextInt(2) << 12 | g.nextInt(6) | (g.nextInt(4) << 25 | g.nextInt(8) << 22 | g.nextInt(8) << 19);
                        if (w == 4) { v = 1; if (m.slot[0].dma[1].busy(Long.MAX_VALUE)) w = 33; }                // PULSE_DMA while busy: UB in ref
                        if (w == 4 && (m.slot[0].dma[1].hdr >>> 12 & 3) >= 2) w = 33;            // a queue it hasn't: UB
                        if (w == 3) v = g.nextInt(2);
                    } else {
                        w = c < 4 ? 7 : c < 6 ? 32 : c < 8 ? 3 : g.nextInt(128);
                        if (w == 7) v = g.nextInt(128);
                        if (w == 32) v = g.nextInt(4) | (g.nextBoolean() ? 1 << 24 : 0) | (g.nextBoolean() ? 1 << 25 : 0);
                        if (w == 3) v = 1;
                        if ((w == 32 || w == 3) && r.running(t)) w = 7;     // RUN_CONFIG / PULSE_RUN while running: UB
                    }
                    o = "W:" + s + ":" + w + ":" + Integer.toHexString(v);
                } else o = "T:" + (g.nextInt(2)) + ":" + (32 + g.nextInt(96));
                ops.add(o);
            }
            // at most one PULSE_RUN to the replayer per cycle, and none after one in flight (run applies at t's end)
            boolean runSeen = false;
            boolean[] accSeen = new boolean[2];
            for (String o : ops) {
                if (o.startsWith("A:")) { int s = o.charAt(2) - '0'; if (accSeen[s]) continue; accSeen[s] = true; }   // one per cycle (the director)
                // PULSE_DMA while busy, or naming a queue the peripheral hasn't: UB in ref; judged as the store applies
                if (o.startsWith("W:0:4:") && (m.slot[0].dma[1].busy(Long.MAX_VALUE) || (m.slot[0].dma[1].hdr >>> 12 & 3) >= 2)) o = "W:0:33:" + o.substring(6);
                if (o.startsWith("W:1:3:")) { if (runSeen) continue; runSeen = true; }
                if (o.startsWith("W:1:32:") && runSeen) continue;
                l.append(' ').append(o);
                m.op(t, o);
            }
            l.append(" S");
            m.op(t, "S");
            sc.append(l).append('\n');
        }
        java.nio.file.Files.writeString(java.nio.file.Path.of(out + ".script"), sc.toString());
        java.nio.file.Files.writeString(java.nio.file.Path.of(out + ".obs"), "# slotconf observations v1\n" + m.obs);
    }
}
