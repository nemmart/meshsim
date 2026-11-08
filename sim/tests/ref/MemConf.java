import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.ArrayDeque;
import java.util.Random;

/**
 * The memory path's conformance, ref's side (WORKER11; the C++ side is tests/test_memconf.cpp). ref's own tile, of
 * which only G3's components run: the SRAM read queue and the injection point (Tile's srqInj), the big SRAM, the write
 * engine, the table store and HIST, in Tile's order. The driver plays everything around them from a seeded script:
 * the GATHER engine's and the address generator's read holding registers (a refused put retried), the read port's
 * queue, the director's we-in FIFO, the controller's PTR and HIST window stores, the AG's STREAM and GROUP requests,
 * the core's send holding register (retried), and the router (it takes every injected packet and frees each loopback
 * token, one a cycle, 0-127 cycles later: the token window fills). Compiled into ref's default package (D5: nothing in sim/ref changes).
 *
 * <pre>
 *   java -cp REF:. MemConf gen TILE SEED CYCLES [comb]   a script on stdout (comb: write combining on)
 *   java -cp REF:. MemConf run SCRIPT                    ref's observations on stdout
 * </pre>
 *
 * Script: "# memconf v1", "# tile T", "# seed S cycles N", "# cfg DMA_CONFIG RECORD_BASE AREA_DEPTH COMB", then one
 * line per cycle t (from 1): {@code t [G:a,b] [A:a,b] [R:a,b] [W:a,b,c] [P:i,v] [H:i,v] [S:i] [Q:g] [X:a,b,c]}, hex.
 * Before cycle 1 the big SRAM's chunks c of areas 0-7, rows 0-255, hold init(c), and the tables hold their own
 * pattern (below).
 *
 * Observation line for cycle t, after the components ran t: {@code t [I:a,b,c] [SR:a,b,c] [GR:a,b,c] [F] U:h,h,h,h,h,h,h}:
 * the packet the injection point put at t, the STREAM and GROUP returns put at t, F if a loopback token was freed at t,
 * then StateFp's streams of srq, inj, sram, sram.data, we, ts and hist at the end of t, hashed.
 */
final class MemConf {
    public static void main(String[] a) throws IOException {
        switch (a[0]) {
            case "gen" -> gen(Integer.parseInt(a[1]), Long.parseLong(a[2]), Integer.parseInt(a[3]), a.length > 4 && a[4].equals("comb"));
            case "run" -> run(a[1]);
            default -> throw new IllegalArgumentException(a[0]);
        }
    }
    static final String[] UNITS = { "srq", "inj", "sram", "sram.data", "we", "ts", "hist" };
    static int init(int c) { int x = c * 0x9E3779B1; return x ^ (x >>> 15); }

    // ------------------------------------------------------------------------------------------------ the generator
    /** Sources in phases of 1-48 cycles (idle to every cycle), so holds are refused, areas fill, tokens run out. */
    static void gen(int tile, long seed, int cycles, boolean comb) {
        Random r = new Random(seed);
        int bucketSel = 1 + r.nextInt(8), histSel = 1 + r.nextInt(8);
        int dma = bucketSel << 18 | histSel << 21, base = r.nextInt(1 << 20) & ~1, depth = 1 + r.nextInt(8);
        StringBuilder sb = new StringBuilder();
        sb.append("# memconf v1\n# tile ").append(tile).append("\n# seed ").append(seed).append(" cycles ").append(cycles)
          .append("\n# cfg ").append(Integer.toHexString(dma)).append(' ').append(Integer.toHexString(base)).append(' ')
          .append(depth).append(' ').append(comb ? 1 : 0).append('\n');
        final int N = 9;
        double[] rate = new double[N]; int[] left = new int[N];
        String[] tag = { "G", "A", "R", "W", "P", "H", "S", "Q", "X" };
        double[] top = { 1, 1, 0.5, 1, 0.1, 0.1, 0.5, 0.5, 0.3 };
        // Combining bursts (write combining, 13a): we-in writes W0-W3 into the four chunks of one row of area a at c..c+3
        // and read-port reads of other rows of area a at c and c + 1. Through the write engine and the read queue they
        // reach area a as R0, then W0 with R1 (the write first), then W1, W2, W3 one a cycle: W1 queues behind R1 and
        // W2 (W3) merges into it. A read delayed (higher clients, a full token window) spoils a burst; one in 12 cycles
        // starts one. The bursts' cycles carry no other we-in word or read.
        java.util.TreeMap<Integer, String[]> burst = new java.util.TreeMap<>();
        int quiet = 0;
        for (int t = 1; t <= cycles; t++) {
            sb.append(Integer.toHexString(t));
            if (burst.isEmpty() && t + 4 <= cycles && r.nextInt(12) == 0) {
                int area = r.nextInt(8), row = r.nextInt(256), other = (row + 1 + r.nextInt(200)) & 255;
                for (int j = 0; j < 4; j++) {
                    int chunk = area << 17 | row << 2 | j;
                    long hd = LinkWord.hdr(r.nextInt(64), 0, LinkWord.M_WRITE, 0, 3 << 24 | chunk << 2, LinkWord.SIZE_32);
                    burst.put(t + j, new String[] { "W:" + h(r.nextLong()) + "," + h(hd) + "," + h(r.nextLong()), j <= 1 ? "R:" + h((area << 17 | (other + j) % 256 << 2) | (long) tile << 29) + "," + h(0x2000 + 4 * t + 4 * j) : null });
                }
                quiet = t + 4;
            }
            String[] b = burst.remove(t);
            if (b != null) for (String x : b) if (x != null) sb.append(' ').append(x);
            for (int s = 0; s < N; s++) {
                if (left[s]-- <= 0) { left[s] = 1 + r.nextInt(48); int k = r.nextInt(4); rate[s] = k == 0 ? 0 : k == 1 ? 0.15 * top[s] : k == 2 ? 0.6 * top[s] : top[s]; }
                if (r.nextDouble() >= rate[s]) continue;
                if (t <= quiet && s <= 3) continue;                   // a burst's cycles: no other read or we-in word
                sb.append(' ').append(tag[s]).append(':').append(switch (s) {
                    case 0, 1, 2 -> read(r, tile);
                    case 3 -> weWord(r, bucketSel);
                    case 4 -> { int i = r.nextInt(16); yield h(i) + "," + h(ptrValue(r, i)); }
                    case 5 -> h(r.nextInt(16)) + "," + h(r.nextInt(1 << 17));
                    case 6 -> h(r.nextInt(256));
                    case 7 -> h(r.nextInt(64));
                    default -> sendWord(r, tile, t);
                });
            }
            sb.append('\n');
            if (sb.length() > 1 << 16) { System.out.print(sb); sb.setLength(0); }
        }
        System.out.print(sb);
    }
    static String h(long x) { return Long.toHexString(x); }
    /** A read: chunk in areas 0-7 (rows 0-255, where init() is) or now and then past the end (areas 8-15: zero-width),
     *  aligned to its size (3: the undefined size, zero-width); to the tile itself (a loopback token) or elsewhere. */
    static String read(Random r, int tile) {
        int w = r.nextInt(16) == 0 ? 3 : r.nextInt(3);
        int area = r.nextInt(12) == 0 ? 8 + r.nextInt(8) : r.nextInt(8);
        int chunk = area << 17 | r.nextInt(1024);
        if (w != 3) chunk &= ~((1 << w) - 1);
        int dst = r.nextInt(3) == 0 ? r.nextInt(64) : tile;
        long a = (chunk & 0x1FFFFF) | (long) w << 21 | (long) r.nextInt(8) << 23 | (long) r.nextInt(8) << 26 | (long) dst << 29;
        return h(a) + "," + h(r.nextInt() & 0xFFFFFFFFL);
    }
    /** PTR[i] for i < 16 is used at one size (i % 3: 32, 64, 128 bits; 1-bit-aligned values for mode 3 at i % 3 == 1). */
    static int ptrValue(Random r, int i) { int n = 1 << (i % 3); return (r.nextInt(1 << 20) + (r.nextInt(8) == 0 ? 1 << 20 : 0)) & ~(n - 1); }
    /** A we-in word (the director's): mode 0 into region 3 (aligned), mode 1 (PTR[src]), 2 (PTR[bucket digit]) or 3
     *  (augmented, at a PTR with i % 3 == 1); indices < 16 at their size; payloads random. */
    static String weWord(Random r, int bucketSel) {
        int mode = r.nextInt(4);
        long d = r.nextLong(), c = r.nextLong();
        int size, src = r.nextInt(64), addr = 0;
        if (mode == 0) { size = r.nextInt(3); int chunk = (r.nextInt(8) << 17 | r.nextInt(4096)) & ~((1 << size) - 1); addr = 3 << 24 | chunk << 2; }
        else if (mode == 1) { int i = r.nextInt(16); src = i; size = i % 3; }
        else if (mode == 2) { int i = r.nextInt(16); d = (d & ~(0xFFL << (8 * (bucketSel - 1)))) | (long) i << (8 * (bucketSel - 1)); size = i % 3; }
        else { int i = 1 + 3 * r.nextInt(5); src = i; size = r.nextInt(2); }
        long hd = LinkWord.hdr(src, 0, mode, r.nextInt(8), addr, size);
        return h(d) + "," + h(hd) + "," + h(c);
    }
    static String sendWord(Random r, int tile, int t) {
        int k = r.nextInt(100), dst = k < 50 ? tile : k < 70 ? LinkWord.BROADCAST : r.nextInt(64);
        return h(t) + "," + h(LinkWord.hdr(0, dst, 0, 0, r.nextInt(), r.nextInt(3))) + "," + h(r.nextInt(1 << 12));
    }

    // ------------------------------------------------------------------------------------------------ the driver
    static long hex(String s) { return Long.parseUnsignedLong(s, 16); }

    static void run(String script) throws IOException {
        try (BufferedReader in = new BufferedReader(new FileReader(script), 1 << 16);
             Writer out = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            Tile tl = null; int tile = 0;
            long[] gRetry = null, aRetry = null, xRetry = null;
            ArrayDeque<Long> frees = new ArrayDeque<>();          // loopback frees due (cycle), one a cycle
            for (String line; (line = in.readLine()) != null; ) {
                if (line.startsWith("# tile ")) tile = Integer.parseInt(line.substring(7).trim());
                if (line.startsWith("# cfg ")) {
                    String[] f = line.substring(6).trim().split(" ");
                    Config cfg = Config.machine();
                    cfg.areaQueueDepth = Integer.parseInt(f[2]); cfg.writeCombining = f[3].equals("1");
                    tl = new Tile(tile, cfg);
                    int dma = (int) hex(f[0]), base = (int) hex(f[1]);
                    tl.writer.bucketDigit = (dma >>> 18) & 7; tl.writer.histDigit = (dma >>> 21) & 7; tl.writer.augmentBase = base & 0x1FFFFF;
                    for (int c = 0; c < 8 << 17; c += 4) if ((c & 0x1FFFF) < 1024) for (int j = 0; j < 4; j++) tl.sram.poke(c + j, init(c + j));
                    for (int i = 0; i < 256; i++) {
                        tl.ts.poke(TableStore.STREAM_SRC + i, init(0x40000 + i)); tl.ts.poke(TableStore.STREAM_DEST + i, init(0x50000 + i));
                        tl.ts.poke(TableStore.STREAM_COUNT + i, init(0x60000 + i));
                    }
                    for (int w = 0; w < 128; w++) tl.ts.poke(TableStore.GROUP + w, init(0x70000 + w));
                    continue;
                }
                if (line.startsWith("#")) continue;
                String[] f = line.split(" ");
                long t = hex(f[0]);
                // the router (runs first in the tile): one loopback free a cycle
                StringBuilder sb = new StringBuilder().append(h(t));
                boolean freed = false;
                if (!frees.isEmpty() && frees.peek() <= t) { frees.poll(); tl.srq.loopbackLeft(t); freed = true; }
                tl.srq.runCycle(t); tl.inj.runCycle(t);
                tl.sram.runCycle(t); tl.writer.runCycle(t); tl.ts.runCycle(t); tl.hist.runCycle(t);
                // the others' puts of t (each visible from t + 1; the core's window stores after the stores ran, as
                // ref's core runs last); a refused hold is presented again and the producer's word of t dropped
                long[] g = null, a = null, x = null;
                for (int i = 1; i < f.length; i++) {
                    int c = f[i].indexOf(':');
                    String k = f[i].substring(0, c); String[] w = f[i].substring(c + 1).split(",");
                    switch (k) {
                        case "G" -> g = new long[] { hex(w[0]), hex(w[1]) };
                        case "A" -> a = new long[] { hex(w[0]), hex(w[1]) };
                        case "R" -> tl.readPort.q.put(t, hex(w[0]), hex(w[1]), 0);
                        case "W" -> tl.weIn.put(t, hex(w[0]), hex(w[1]), hex(w[2]));
                        case "P" -> tl.ts.pipe.ctrlWrite(t, (int) hex(w[0]), (int) hex(w[1]) & 0x1FFFFF);
                        case "H" -> tl.hist.pipe.ctrlWrite(t, (int) hex(w[0]), (int) hex(w[1]) & 0x1FFFF);
                        case "S" -> tl.agStreamReq.put(t, 1, TableStore.STREAM + hex(w[0]), 0);
                        case "Q" -> tl.agGroupReq.put(t, 1, TableStore.GROUP + 2 * hex(w[0]), 0);
                        case "X" -> x = new long[] { hex(w[0]), hex(w[1]), hex(w[2]) };
                        default -> throw new IllegalArgumentException(k);
                    }
                }
                gRetry = hold(tl.gatherOut, t, gRetry, g); aRetry = hold(tl.agOut, t, aRetry, a); xRetry = hold(tl.sendHold, t, xRetry, x);
                long u = t + 1;
                if (tl.injBus.ready(u)) {
                    long d = tl.injBus.peekA(u), hd = tl.injBus.peekB(u), c = tl.injBus.peekC(u); tl.injBus.take(u);
                    sb.append(" I:").append(h(d)).append(',').append(h(hd)).append(',').append(h(c));
                    if ((hd & LinkWord.TAG_TOKEN) != 0) frees.add(u + (hd >>> 22 & 127));   // held 0-127 cycles: the window fills
                }
                if (tl.agStreamRet.ready(u)) { sb.append(" SR:").append(h(tl.agStreamRet.peekA(u))).append(',').append(h(tl.agStreamRet.peekB(u))).append(',').append(h(tl.agStreamRet.peekC(u))); tl.agStreamRet.take(u); }
                if (tl.agGroupRet.ready(u)) { sb.append(" GR:").append(h(tl.agGroupRet.peekA(u))).append(',').append(h(tl.agGroupRet.peekB(u))).append(',').append(h(tl.agGroupRet.peekC(u))); tl.agGroupRet.take(u); }
                if (freed) sb.append(" F");
                sb.append(" U:");
                for (int k = 0; k < UNITS.length; k++) {
                    StateFp.Out o = new StateFp.Out();
                    StateFp.unit(o, null, tl, UNITS[k], t);
                    sb.append(k > 0 ? "," : "").append(h(o.done()));
                }
                out.write(sb.append('\n').toString());
            }
        }
    }
    /** A producer's holding register at t: a refused put is retried (the producer stalls, its word of t dropped). */
    static long[] hold(WordHold hd, long t, long[] last, long[] now) {
        if (hd.refused(t)) { hd.retry(t); return last; }
        if (now == null) return last;
        hd.put(t, now[0], now[1], now.length > 2 ? now[2] : 0);
        return now;
    }
}
