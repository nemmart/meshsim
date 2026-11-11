import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.Random;

/**
 * The engines' conformance, ref's side (WORKER12; the C++ side is tests/test_engconf.cpp). ref's AddressGenerator,
 * TableStore (its STREAM and GROUP paths) and GatherEngine, wired with ref's own buses as Tile wires them, driven by a
 * seeded script; one observation line per cycle. Compiled into ref's default package (D5: ref's classes are
 * package-private; nothing in sim/ref is changed).
 *
 * <pre>
 *   java -cp REF:. EngConf gen TILE SEED CYCLES      a script on stdout
 *   java -cp REF:. EngConf run SCRIPT                ref's observations on stdout
 * </pre>
 *
 * Script: "# engconf v1", "# tile T", "# seed S cycles N L lat", the tables ("# s i src dest count" for STREAM 0-255,
 * "# g i entry" for GROUP 0-63, "# ptr p" for PTR[me], "# base b" for the record base; hex), then one line per cycle t
 * (from 1, consecutive): {@code t [R:cfg] [B:base] [G:d,h] [T]}:
 *   R: at endCycle(t), if the AG is not running (runningForStatus, before this endCycle's own actions) and no start is
 *      pending, DMA_CONFIG := cfg (AG.config, gather.k), and the start pulse at endCycle(t + 1) (a core's two stores);
 *   B: at endCycle(t), DMA_RECORD_BASE_ADDRESS := base (the gather's base);
 *   G: the director puts a mode-4 packet (d, h) in the gather queue at t;
 *   T: the read queue may take one client's held request at t, by priority: the gather's, else the AG's.
 * The units run in Tile's order: the read queue, the AG, the table store, the gather engine; then endCycle.
 *
 * Observation line for cycle t, after endCycle(t):
 * {@code t [A:a,b] [g:a,b] S:running,done a:aghash q:gatherhash b:busy d:drift v:violations}: the AG's put at t on its
 * hold (a new request or a retry) and the gather's; rd_running and rd_done as STATUS has them (runningForStatus,
 * doneForStatus); the AG's own state as StateFp.ag writes it, without its two return lines (the table store's returns,
 * determined by the requests); the gather's own state as StateFp.gather writes it, without its queue; GatherEngine.busy's
 * three local terms (!gatherIn.isEmpty(t) || left > 0 || !out.isEmpty(t): the read queue's in-flight count is G3's); the
 * AG's drift events so far; its checkEntry counts so far.
 */
final class EngConf {
    public static void main(String[] a) throws IOException {
        switch (a[0]) {
            case "gen" -> gen(Integer.parseInt(a[1]), Long.parseLong(a[2]), Integer.parseInt(a[3]));
            case "run" -> run(a[1]);
            default -> throw new IllegalArgumentException(a[0]);
        }
    }

    // ------------------------------------------------------------------------------------------------ the generator
    static int pick(Random r, int[] w) { int s = 0; for (int x : w) s += x; int k = r.nextInt(s); for (int i = 0; ; i++) { k -= w[i]; if (k < 0) return i; } }

    static void gen(int tile, long seed, int cycles) {
        Random r = new Random(seed);
        int L = 1 + (int) (seed % 3);
        StringBuilder sb = new StringBuilder();
        sb.append("# engconf v1\n# tile ").append(tile).append("\n# seed ").append(seed).append(" cycles ").append(cycles).append(" L ").append(L).append('\n');
        for (int i = 0; i < 256; i++) {
            int size = pick(r, new int[] { 30, 40, 28, 2 });
            int mode = pick(r, new int[] { 25, 30, 15, 10, 5, 15 });
            int n = size == 3 ? 1 : 1 << size;
            int src = r.nextInt(1 << 16) & ~(n - 1);
            if (r.nextInt(50) == 0) src |= 1;                                   // a source not aligned to its size (counted)
            int dest = r.nextInt();
            if (mode == 0 && r.nextInt(20) != 0) dest &= ~(4 * n - 1);
            int dsel = r.nextInt(100);
            int d = dsel < 80 ? r.nextInt(64) : dsel < 95 ? 64 + r.nextInt(3) : new int[] { 70, 126, 127 }[r.nextInt(3)];
            if (mode == 4 && r.nextInt(10) != 0) d = 0;
            int count = r.nextInt(100) < 15 ? 0 : r.nextInt(100) < 90 ? 1 + r.nextInt(8) : 1 + r.nextInt(40);
            int gap = r.nextInt(3) == 0 ? r.nextInt(8) : 0;
            sb.append("# s ").append(i).append(' ').append(Integer.toHexString(Fields.streamSrc(src, gap))).append(' ')
              .append(Integer.toHexString(dest)).append(' ').append(Integer.toHexString(Fields.streamCount(count, size, r.nextInt(8), mode, d))).append('\n');
        }
        for (int i = 0; i < 64; i++) {
            int T = 1 + r.nextInt(4), R = r.nextInt(100) < 15 ? 0 : 1 + r.nextInt(4), start = r.nextInt(257 - T), gap = r.nextInt(3) == 0 ? r.nextInt(4) : 0;
            long e = (R & 0x3FFFFL) | (long) start << 32 | (long) (T - 1) << 40 | (long) gap << 44;
            sb.append("# g ").append(i).append(' ').append(Long.toHexString(e)).append('\n');
        }
        sb.append("# ptr ").append(Integer.toHexString(r.nextInt(1 << 18) << 2)).append('\n');
        sb.append("# base ").append(Integer.toHexString(r.nextInt(1 << 16) << 2)).append('\n');
        double[] rate = new double[3]; int[] left = new int[3];
        for (int t = 1; t <= cycles; t++) {
            sb.append(t);
            for (int s = 0; s < 3; s++) if (left[s]-- <= 0) { left[s] = 1 + r.nextInt(64); int k = r.nextInt(4); rate[s] = k == 0 ? 0 : k == 1 ? 0.15 : k == 2 ? 0.6 : 1.0; }
            if (r.nextInt(25) == 0) {                                            // a run: kind, start, count, k
                int kind = r.nextInt(2), size = kind == 0 ? 256 : 64;
                int count = kind == 0 ? r.nextInt(13) : r.nextInt(7), start = r.nextInt(size - count + 1);
                int cfg = kind | start << 1 | count << 9 | r.nextInt(8) << 24;
                sb.append(" R:").append(Integer.toHexString(cfg));
            }
            if (r.nextInt(500) == 0) sb.append(" B:").append(Integer.toHexString(r.nextInt(1 << 16) << 2));
            if (r.nextDouble() < rate[0] * 0.06) {                                 // k reads each: the gather must not starve the AG
                long h = LinkWord.hdr(r.nextInt(64), tile, LinkWord.M_GATHER, r.nextInt(8), 0, LinkWord.SIZE_64);
                long d = (long) r.nextInt(1 << 20) << 32 | r.nextInt(1 << 16);
                sb.append(" G:").append(Long.toHexString(d)).append(',').append(Long.toHexString(h));
            }
            if (r.nextDouble() < rate[1]) sb.append(" T");
            sb.append('\n');
            if (sb.length() > 1 << 16) { System.out.print(sb); sb.setLength(0); }
        }
        System.out.print(sb);
    }

    // ------------------------------------------------------------------------------------------------ the driver
    static long hex(String s) { return Long.parseUnsignedLong(s, 16); }
    static final String[] AG_FIELDS = { "kind", "runStart", "runCount", "pStart", "running", "done", "leadIn", "finishedIssuing", "gatherChecked",
            "sqOut", "cqOut", "streamDiscard", "groupDiscard", "sReads", "cReads", "rg", "rr", "rv",
            "slRun", "cur", "cSrc", "cDest", "cCount", "cJ", "cD", "cMode", "cSize", "cGap", "cCtr", "gapLeft",
            "g", "r", "v", "gR", "gT", "gGap", "groupS", "rotSched", "inRot", "stalledSinceGroup" };

    static void run(String script) throws IOException {
        try (BufferedReader in = new BufferedReader(new FileReader(script), 1 << 16);
             Writer out = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            int tile = 0; AddressGenerator ag = null; TableStore ts = null; GatherEngine ga = null;
            WordHold agOut = null, gaOut = null; WordFIFO gIn = null;
            int ptr = 0, base = 0;
            int[] src = new int[256], dest = new int[256], cnt = new int[256]; long[] grp = new long[64];
            boolean built = false, pulseNext = false;
            long lastAg = 0, lastGa = 0;
            for (String line; (line = in.readLine()) != null; ) {
                if (line.startsWith("#")) {
                    String[] f = line.split(" ");
                    switch (f[1]) {
                        case "tile" -> tile = Integer.parseInt(f[2]);
                        case "seed" -> {
                            Config c = new Config(); c.tsEngineLatency = Integer.parseInt(f[6]);
                            int L = c.tsEngineLatency;
                            ag = new AddressGenerator(tile, c); ts = new TableStore(tile); ga = new GatherEngine(tile);
                            WordDelayLine sReq = new WordDelayLine("ag>stream", L), sRet = new WordDelayLine("stream>ag", L);
                            WordDelayLine gReq = new WordDelayLine("ag>group", L), gRet = new WordDelayLine("group>ag", L);
                            ag.streamReq = sReq; ag.streamRet = sRet; ag.groupReq = gReq; ag.groupRet = gRet;
                            ts.reqStream = sReq; ts.retStream = sRet; ts.reqGroup = gReq; ts.retGroup = gRet;
                            ts.reqWe = new WordDelayLine("we>ts", L); ts.retWe = new WordDelayLine("ts>we", L);
                            agOut = new WordHold("ag.out"); ag.out = agOut;
                            gIn = new WordFIFO("gather.in", 16); gaOut = new WordHold("gather.out");
                            ga.in = gIn; ga.queue = gIn; ga.out = gaOut;
                        }
                        case "s" -> { int i = Integer.parseInt(f[2]); src[i] = (int) hex(f[3]); dest[i] = (int) hex(f[4]); cnt[i] = (int) hex(f[5]); }
                        case "g" -> grp[Integer.parseInt(f[2])] = hex(f[3]);
                        case "ptr" -> ptr = (int) hex(f[2]);
                        case "base" -> base = (int) hex(f[2]);
                        default -> { }
                    }
                    continue;
                }
                if (!built) {
                    for (int i = 0; i < 256; i++) { ts.poke(TableStore.STREAM_SRC + i, src[i]); ts.poke(TableStore.STREAM_DEST + i, dest[i]); ts.poke(TableStore.STREAM_COUNT + i, cnt[i]); }
                    for (int i = 0; i < 64; i++) { ts.poke(TableStore.GROUP + 2 * i, (int) grp[i]); ts.poke(TableStore.GROUP + 2 * i + 1, (int) (grp[i] >>> 32)); }
                    ts.poke(TableStore.PTR + tile, ptr);
                    ga.base = base;
                    final TableStore fts = ts; final GatherEngine fga = ga; final int me = tile;
                    ag.gatherAlign = tt -> {
                        int p = fts.peek(TableStore.PTR + me), b = fga.base;
                        if ((p & 3) != 0 || (b & 3) != 0) throw new IllegalStateException("gather buffers not 128-bit aligned at " + tt);
                    };
                    built = true;
                }
                String[] f = line.split(" ");
                long t = Long.parseLong(f[0]);
                Integer rcfg = null, nbase = null; boolean take = false;
                for (int i = 1; i < f.length; i++) {
                    char k = f[i].charAt(0);
                    if (k == 'R') rcfg = (int) hex(f[i].substring(2));
                    else if (k == 'B') nbase = (int) hex(f[i].substring(2));
                    else if (k == 'G') { String[] w = f[i].substring(2).split(","); gIn.put(t, hex(w[0]), hex(w[1]), 0); }
                    else if (k == 'T') take = true;
                }
                // the read queue (client 0 before client 1, one a cycle), the AG, the table store, the gather engine
                if (take) { if (gaOut.ready(t)) gaOut.take(t); else if (agOut.ready(t)) agOut.take(t); }
                ag.runCycle(t); ts.runCycle(t); ga.runCycle(t);
                // endCycle(t): a pending start, then this cycle's stores
                boolean wasRunning = ag.runningForStatus();
                if (pulseNext) { ag.pulse(Fields.PULSE_START); pulseNext = false; }
                else if (rcfg != null && !wasRunning) { ag.config(rcfg); ga.k = Fields.mcK(rcfg); pulseNext = true; }
                if (nbase != null) ga.base = nbase & 0x1FFFFF;
                // the observation
                StringBuilder sb = new StringBuilder().append(t);
                long na = ag.requests + ag.stallCycles + ag.cumRequests + ag.cumStall;
                if (na != lastAg) { sb.append(" A:").append(Long.toHexString(StateFp.l(agOut, "pa"))).append(',').append(Long.toHexString(StateFp.l(agOut, "pb"))); lastAg = na; }
                long ng = ga.reads + ga.stallCycles;
                if (ng != lastGa) { sb.append(" g:").append(Long.toHexString(StateFp.l(gaOut, "pa"))).append(',').append(Long.toHexString(StateFp.l(gaOut, "pb"))); lastGa = ng; }
                sb.append(" S:").append(ag.runningForStatus() ? 1 : 0).append(',').append(ag.doneForStatus() ? 1 : 0);
                StateFp.Out o = new StateFp.Out();
                for (String fl : AG_FIELDS) o.w(fl, StateFp.l(ag, fl));
                long[] sq = (long[]) StateFp.o(ag, "sq"), sq1 = (long[]) StateFp.o(ag, "sq1"), cq = (long[]) StateFp.o(ag, "cq"); int n = sq.length;
                int sbuf = StateFp.i(ag, "sqBuf"), sh = StateFp.i(ag, "sqHead"), cb = StateFp.i(ag, "cqBuf"), chd = StateFp.i(ag, "cqHead");
                o.w("sq.n", sbuf); for (int k = 0; k < sbuf; k++) { int j = (sh + k) % n; o.w("sq.e0", sq[j]); o.w("sq.e1", sq1[j]); }
                o.w("cq.n", cb); for (int k = 0; k < cb; k++) o.w("cq.e", cq[(chd + k) % n]);
                sb.append(" a:").append(Long.toHexString(o.done()));
                StateFp.Out q = new StateFp.Out();
                q.w("k", ga.k); q.w("base", ga.base);
                for (String fl : new String[] { "left", "ord", "i", "ctr", "wide" }) q.w(fl, StateFp.l(ga, fl));
                sb.append(" q:").append(Long.toHexString(q.done()));
                boolean busy = !gIn.isEmpty(t) || StateFp.l(ga, "left") > 0 || !gaOut.isEmpty(t);
                sb.append(" b:").append(busy ? 1 : 0).append(" d:").append(ag.driftEvents);
                sb.append(" v:").append(ag.sizeViolations + ag.modeViolations + ag.aug128Violations + ag.destViolations + ag.alignViolations).append('\n');
                out.write(sb.toString());
            }
        }
    }
}
