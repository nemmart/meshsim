import java.util.*;
import java.util.function.*;

/**
 * Measurements of the address generator's read-ahead (PROPOSAL-slot-iteration-counter.md section 7).
 * Written against the API M7 and M8 share (M8-only fields by reflection), so the same file runs on both trees.
 *   lookahead  the smallest ag_lookahead with no cycle lost to the read-ahead, at ts_engine_latency 1-4, without and
 *              with read-queue stalls (each element's write lands in its source area: writes win ties, spec 4.1);
 *              a cycle lost = run cycles (start to rd_done) above the same run at ag_lookahead 64
 *   zero       runs of k zero-length groups (R = 0, gap 0) before a group: how late it starts
 *   ready      arm to rd_ready, and arm + start in one store: how late group 0 starts
 * One tile (0) runs; everything goes to the tile itself in mode 2 (d = 0).
 * Usage: java AgBench [all|lookahead|zero|ready]
 */
public final class AgBench {
    static final boolean M8 = has(Config.class, "agLookahead");

    public static void main(String[] a) {
        String what = a.length > 0 ? a[0] : "all";
        System.out.println("AgBench on " + (M8 ? "M8 (read-ahead)" : "M7 (banks + prefetch)"));
        if ((what.equals("all") || what.equals("lookahead")) && M8) lookahead();
        if (what.equals("all") || what.equals("zero")) zero();
        if (what.equals("all") || what.equals("ready")) ready();
    }

    // ---------------------------------------------------------------- reflection (M7 / M8)
    static boolean has(Class<?> c, String f) { try { c.getDeclaredField(f); return true; } catch (NoSuchFieldException e) { return false; } }
    static void setLa(Config k, int la) { try { Config.class.getDeclaredField("agLookahead").setInt(k, la); } catch (Exception e) { throw new RuntimeException(e); } }
    static long get(Object o, String f) { try { return ((Number) o.getClass().getDeclaredField(f).get(o)).longValue(); } catch (Exception e) { return -1; } }

    // ---------------------------------------------------------------- one run
    /** Tables for tile p; returns {kind, start, count, elements}. */
    interface Setup { int[] write(Tile p, int srcBase); }
    static final int SRC = 0x1000, DST_OTHER = 0x80000, DST_SAME = 0x10000;   // areas 0 and 4; DST_SAME is area 0, with the sources

    static final class Res { long cycles, late, underrun, tableWait, startToFirst, stalls, group0Late; long[] groupStart; }

    /** Arm; once ready wait `idle` cycles (the sort arms at configure time) then start; or arm + start in one store. */
    static Res run(int L, int la, boolean stalls, boolean oneStore, int idle, Setup s) {
        Config k = Config.defaults(); k.checkUnexpected = false; k.tsEngineLatency = L;
        if (la > 0) setLa(k, la);
        Chip c = new Chip(k); Tile p = c.tiles[0];
        int[] r = s.write(p, SRC);
        p.setPtr(0, stalls ? DST_SAME : DST_OTHER); p.dir.remaining[0] = r[3];
        p.setMasterConfig(Fields.masterConfig(r[0], r[1], r[2], 0, 0, 1));
        long storeAt = c.cycle;                          // the host pulse stands for a DMA_PULSE store in this cycle
        Res out = new Res();
        p.pulse(Fields.PULSE_START);
        if (!c.runUntil(() -> p.ag.done, 2_000_000)) throw new IllegalStateException("never done");
        out.startToFirst = p.ag.startToFirstRequest();   // from the cycle the start takes effect (the store's cycle + 1)
        if (p.ag.startedAt != storeAt + 1) throw new IllegalStateException("the start took effect at " + p.ag.startedAt + ", store at " + storeAt);
        out.cycles = p.ag.doneAt - p.ag.startedAt;
        out.late = p.ag.groupsLatePrefetch; out.stalls = p.ag.stallCycles; out.tableWait = p.ag.tableWaitCycles;
        out.underrun = Math.max(0, get(p.ag, "underrunCycles"));
        out.group0Late = p.ag.groupLateBy[0];
        out.groupStart = p.ag.groupStart.clone();
        boolean other = c.violationMessages.stream().anyMatch(m -> !(stalls && m.contains("rotation started late")));
        if (other) throw new IllegalStateException("violations:\n" + c.violationReport());   // under stalls, late rotations are expected
        return out;
    }

    // ---------------------------------------------------------------- table shapes
    /** g groups of t streams, R rotations each (R[] cycles through), gap 0, the given size. */
    static Setup groups(int g, int t, int[] Rs, int size) {
        return (p, base) -> {
            int stream = 0, src = base, el = 0, cpe = SRAMReadQueue.chunksOf(size);
            for (int i = 0; i < g; i++) {
                int R = Rs[i % Rs.length];
                p.setGroup(i, Fields.groupLo(R), Fields.groupHi(stream & 255, t, 0));
                for (int s = 0; s < t; s++, stream++) { p.setStream(stream & 255, Fields.streamLo(src, 0), Fields.streamHi(R, 0, LinkWord.M_SRC, size, 0)); src += R * cpe; el += R * (size == Fields.SIZE_128 ? 2 : 1); }
            }
            return new int[] { Fields.RUN_GROUPS, 0, g, el };
        };
    }
    /** A stream run of n streams of count c, gap after gap. */
    static Setup streams(int n, int cnt, int gap) {
        return (p, base) -> {
            for (int s = 0; s < n; s++) p.setStream(s, Fields.streamLo(base + 2 * cnt * s, 0), Fields.streamHi(cnt, 0, LinkWord.M_SRC, Fields.SIZE_64, gap));
            return new int[] { Fields.RUN_STREAMS, 0, n, n * cnt };
        };
    }
    /** Group A (t = 4, R = 1), k zero-length groups (R = 0, gap 0), group B (t = 4, R = 2), group C (t = 4, R = 1). */
    static Setup zeros(int k) {
        return (p, base) -> {
            int src = base, stream = 0, g = 0;
            int[] R = new int[k + 3]; R[0] = 1; R[k + 1] = 2; R[k + 2] = 1;
            for (int i = 0; i < k + 3; i++) {
                p.setGroup(g++, Fields.groupLo(R[i]), Fields.groupHi(stream, 4, 0));
                for (int s = 0; s < 4; s++, stream++) { p.setStream(stream, Fields.streamLo(src, 0), Fields.streamHi(R[i], 0, LinkWord.M_SRC, Fields.SIZE_64, 0)); src += 2 * R[i]; }
            }
            return new int[] { Fields.RUN_GROUPS, 0, k + 3, 4 * 4 };
        };
    }

    // ---------------------------------------------------------------- lookahead
    static void lookahead() {
        System.out.println("\n== ag_lookahead: cycles lost to the read-ahead (run cycles above ag_lookahead 64), by ag_lookahead 1..12 (searched to 20); min = the smallest from which no more groups start late and no more visits wait for an entry than at ag_lookahead 64 (group starts late + underrun cycles + stream-run table waits) [meas: AgBench lookahead]");
        Object[][] shapes = {
            { "16 groups t=4 R=1 (the sort's shortest)", groups(16, 4, new int[] { 1 }, Fields.SIZE_64) },
            { "64 groups t=1 R=1 (one visit per group)", groups(64, 1, new int[] { 1 }, Fields.SIZE_64) },
            { "64 groups t=2 R=1", groups(64, 2, new int[] { 1 }, Fields.SIZE_64) },
            { "16 groups t=16 R=1", groups(16, 16, new int[] { 1 }, Fields.SIZE_64) },
            { "16 groups t=4 R=1, 128-bit", groups(16, 4, new int[] { 1 }, Fields.SIZE_128) },
            { "16 groups t=4 R=3,1,0,2", groups(16, 4, new int[] { 3, 1, 0, 2 }, Fields.SIZE_64) },
            { "stream run 63 x count 1, gap 0", streams(63, 1, 0) },
            { "stream run 63 x count 1, gap 4", streams(63, 1, 4) },
            { "stream run 32 x count 3, gap 0", streams(32, 3, 0) },
        };
        for (boolean stalls : new boolean[] { false, true }) {
            System.out.println(stalls ? "\n-- with read-queue stalls (writes land in the source area and win ties)" : "\n-- no stalls");
            System.out.printf("  %-42s %3s %6s  %-44s %4s %5s%n", "shape", "L", "cycles", "lost at ag_lookahead 1..12", "min", "2L+2");
            if (stalls) System.out.println("  (cycles: start to rd_done at ag_lookahead 64, stalls included)");
            for (Object[] sh : shapes) for (int L = 1; L <= 4; L++) {
                Setup s = (Setup) sh[1];
                Res ref = run(L, 64, stalls, false, 64, s);
                StringBuilder sb = new StringBuilder(), se = new StringBuilder(); int min = -1;
                for (int la = 1; la <= 20; la++) {
                    Res x = run(L, la, stalls, false, 64, s);
                    long lost = x.cycles - ref.cycles, ev = (x.late + x.underrun + x.tableWait) - (ref.late + ref.underrun + ref.tableWait);
                    if (la <= 12) { sb.append(lost).append(la < 12 ? " " : ""); se.append(ev).append(la < 12 ? " " : ""); }
                    if (ev == 0 && min < 0) min = la;
                    if (ev != 0) min = -1;
                    if (lost < 0 && !stalls) throw new IllegalStateException("faster than the reference");
                }
                System.out.printf("  %-42s %3d %6d  %-44s %4s %5d%s%n", sh[0], L, ref.cycles, sb, min < 0 ? ">20" : "" + min, 2 * L + 2,
                        stalls ? "  (stall cycles " + ref.stalls + ")" : "");
                if (stalls) System.out.printf("  %-42s %3s %6s  %-44s%n", "  read-ahead events above ag_lookahead 64", "", "", se);
            }
        }
    }

    // ---------------------------------------------------------------- zero-length groups
    static void zero() {
        System.out.println("\n== runs of k zero-length groups (R = 0, gap 0) between a t=4 R=1 group and a t=4 R=2 group: cycles group B starts late (actual - scheduled) [meas: AgBench zero]");
        int[] ks = { 0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
        StringBuilder h = new StringBuilder(); for (int k : ks) h.append(String.format("%5d", k));
        System.out.printf("  %-28s%s%n", "k =", h);
        for (int L = 1; L <= 4; L++) {
            StringBuilder sb = new StringBuilder(), sbc = new StringBuilder();
            for (int k : ks) {
                Res x = run(L, -1, false, false, 64, zeros(k));
                long late = x.groupStart[k + 1] - 4;                  // B is scheduled at S_0 + 4 (group A: R 1 x P 4; the R = 0 groups take 0)
                sb.append(String.format("%5d", late));
                sbc.append(String.format("%5d", x.groupStart[k + 2] - (4 + 8)));
            }
            System.out.printf("  %-28s%s%n", "L " + L + ": B late by", sb);
            System.out.printf("  %-28s%s%n", "L " + L + ": C late by (B's + its own)", sbc);
        }
        System.out.println("  stream run: a count-4 stream, k count-0 gap-0 streams, a count-4 stream: cycles above k = 0");
        for (int L = 1; L <= 4; L++) {
            StringBuilder sb = new StringBuilder(); long base = run(L, -1, false, false, 64, zeroStreams(0)).cycles;
            for (int k : ks) sb.append(String.format("%5d", run(L, -1, false, false, 64, zeroStreams(k)).cycles - base));
            System.out.printf("  %-28s%s%n", "L " + L + ": stream run", sb);
        }
    }

    /** A stream run: a count-4 stream, k count-0 gap-0 streams, a count-4 stream (the cycles of the run, start to rd_done). */
    static Setup zeroStreams(int k) {
        return (p, base) -> {
            p.setStream(0, Fields.streamLo(base, 0), Fields.streamHi(4, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
            for (int i = 1; i <= k; i++) p.setStream(i, Fields.streamLo(base, 0), Fields.streamHi(0, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
            p.setStream(k + 1, Fields.streamLo(base + 8, 0), Fields.streamHi(4, 0, LinkWord.M_SRC, Fields.SIZE_64, 0));
            return new int[] { Fields.RUN_STREAMS, 0, k + 2, 8 };
        };
    }

    // ---------------------------------------------------------------- arm to ready
    static void ready() {
        System.out.println("\n== start to the first request [meas: AgBench start]: cycles from the start taking effect (the cycle after the DMA_PULSE");
        System.out.println("   store) to the address generator's first request; spec 8.2 (v1.0): 4 L + 2 for a group run, 2 L + 1 for a stream run");
        System.out.printf("  %-34s %s%n", "", "L=1  L=2  L=3  L=4");
        for (int t : new int[] { 1, 4, 8, 16 }) {
            StringBuilder a = new StringBuilder();
            for (int L = 1; L <= 4; L++) a.append(String.format("%4d ", run(L, -1, false, false, 0, groups(2, t, new int[] { 2 }, Fields.SIZE_64)).startToFirst));
            System.out.printf("  %-34s %s%n", "group run, group 0 t=" + t, a);
        }
        { StringBuilder a = new StringBuilder(), b = new StringBuilder();
          for (int L = 1; L <= 4; L++) { a.append(String.format("%4d ", run(L, -1, false, false, 0, streams(8, 2, 0)).startToFirst)); b.append(String.format("%4d ", 2 * L + 1)); }
          System.out.printf("  %-34s %s%n", "stream run", a);
          System.out.printf("  %-34s %s%n", "(2 L + 1 for reference)", b); }
        { StringBuilder a = new StringBuilder();
          for (int L = 1; L <= 4; L++) a.append(String.format("%4d ", run(L, -1, false, true, 0, streams(8, 2, 0)).cycles));
          System.out.printf("  %-34s %s%n", "stream run 8 x 2: start to rd_done", a); }
    }
}
