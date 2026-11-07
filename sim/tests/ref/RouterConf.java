import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.file.Path;
import java.util.Random;

/**
 * Router conformance, ref's side (WORKER9; the C++ side is tests/test_router.cpp). One of ref's routers, alone, driven
 * by a seeded script of arrivals; one observation line per cycle. Compiled into ref's default package (D5: ref's classes
 * are package-private; nothing in sim/ref is changed).
 *
 * <pre>
 *   java -cp REF:. RouterConf gen TILE SEED CYCLES      a script on stdout
 *   java -cp REF:. RouterConf run HOPS SCRIPT           ref's observations on stdout
 *   java -cp REF:. RouterConf table HOPS                every router's routing decisions, hashed per tile
 * </pre>
 *
 * Script: "# routerconf v1", "# tile T", "# seed S cycles N", then one line per cycle t (from 1, consecutive):
 * {@code t [Lp:a,b,c]... [I:a,b,c] [C:a,b,c]}: a word put at t on link input p (0-3: from-left, from-right, from-up,
 * from-down), on the injection link, or (tiles 0-2) in C&C's send holding register; each is visible to the router at
 * t + 1 (links and the injection of latency 1; the hold's put of t). Words in lowercase hex.
 *
 * Observation line for cycle t, after the router ran t: {@code t [Od:a,b,c]... [E:a,b,c] [X:a,b,c] [F] Q:fp,ovf,rerr,eerr}:
 * the words the router put at t on link output d (to-right, to-left, to-down, to-up), on the eject bus, on the
 * to-controller output; F if a token-holding packet left the loopback FIFO at t (srq.loopbackLeft(t)); then the router's
 * own canonical stream (StateFp.router's rr and FIFOs, AUDIT-M2 6.2) hashed, the overflow total (every put through t
 * judged: see observe), route and eject errors.
 */
final class RouterConf {
    public static void main(String[] a) throws IOException {
        switch (a[0]) {
            case "gen" -> gen(Integer.parseInt(a[1]), Long.parseLong(a[2]), Integer.parseInt(a[3]));
            case "run" -> run(Hops.load(Path.of(a[1])), a[2]);
            case "table" -> table(Hops.load(Path.of(a[1])));
            default -> throw new IllegalArgumentException(a[0]);
        }
    }

    // ------------------------------------------------------------------------------------------------ the generator
    /** Rates vary in phases of 1-64 cycles per source (idle to every cycle), so FIFOs fill, overflow and drain. */
    static void gen(int tile, long seed, int cycles) {
        Random r = new Random(seed);
        StringBuilder sb = new StringBuilder();
        sb.append("# routerconf v1\n# tile ").append(tile).append("\n# seed ").append(seed).append(" cycles ").append(cycles).append('\n');
        int nsrc = tile < 3 ? 6 : 5;
        double[] rate = new double[nsrc]; int[] left = new int[nsrc];
        for (int t = 1; t <= cycles; t++) {
            sb.append(t);
            for (int s = 0; s < nsrc; s++) {
                if (left[s]-- <= 0) { left[s] = 1 + r.nextInt(64); int k = r.nextInt(4); rate[s] = k == 0 ? 0 : k == 1 ? 0.15 : k == 2 ? 0.6 : 1.0; }
                if (r.nextDouble() >= rate[s]) continue;
                String tag = s < 4 ? "L" + s : s == 4 ? "I" : "C";
                sb.append(' ').append(tag).append(':').append(word(r, tile, s, (long) t << 3 | s));
            }
            sb.append('\n');
            if (sb.length() > 1 << 16) { System.out.print(sb); sb.setLength(0); }
        }
        System.out.print(sb);
    }
    /** A packet: any source and destination (the route functions are pure: every input is legal), any mode, counter,
     *  size and address, and the simulator-only bits at random (the eject selector reads TAG_TOKEN); payload [63:0] is a
     *  sequence number (cycle, source) so a misrouted word is easy to read, payload [127:64] a small random tag. */
    static String word(Random r, int tile, int s, long seq) {
        int dst, src;
        int k = r.nextInt(100);
        if (k < 30) dst = tile; else if (k < 45) dst = LinkWord.BROADCAST; else if (k < 50) dst = 64 + r.nextInt(3);
        else if (k < 52) dst = r.nextInt(128); else dst = r.nextInt(64);
        int j = r.nextInt(100);
        src = j < 90 ? r.nextInt(64) : j < 95 ? 64 + r.nextInt(3) : j < 97 ? 72 + r.nextInt(3) : r.nextInt(128);
        if (s == 4) src = 0;                                         // the router stamps the injection's src
        long h = LinkWord.hdr(src, dst, r.nextInt(8), r.nextInt(8), r.nextInt(), r.nextInt(4));
        h |= (long) r.nextInt(64) << LinkWord.TOKEN_SHIFT;
        if (r.nextInt(3) == 0) h |= LinkWord.TAG_TOKEN;
        if (r.nextInt(8) == 0) h |= LinkWord.TAG_FROM_SRQ;
        if (r.nextInt(8) == 0) h |= LinkWord.TAG_READPORT;
        return Long.toHexString(seq) + "," + Long.toHexString(h) + "," + Integer.toHexString(r.nextInt(1 << 12));   // payloads: a sequence number and a small tag
    }

    // ------------------------------------------------------------------------------------------------ the driver
    static long hex(String s) { return Long.parseUnsignedLong(s, 16); }

    static void run(Hops hops, String script) throws IOException {
        int tile = -1;
        try (BufferedReader in = new BufferedReader(new FileReader(script), 1 << 16);
             Writer out = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            Router rt = null; WordDelayLine[] lin = new WordDelayLine[4], lout = new WordDelayLine[4];
            WordDelayLine inj = null, ej = null; WordHold cnc = null; WordFIFO toCnc = null; SRAMReadQueue tok = null;
            long t = 0;
            for (String line; (line = in.readLine()) != null; ) {
                if (line.startsWith("#")) {
                    if (line.startsWith("# tile ")) {
                        tile = Integer.parseInt(line.substring(7).trim());
                        rt = new Router(tile, hops, 16, 64, tile < 3);
                        for (int d = 0; d < 4; d++) { lin[d] = new WordDelayLine("in" + d, 1); lout[d] = new WordDelayLine("out" + d, 1); rt.linkIn[d] = lin[d]; rt.linkOut[d] = lout[d]; }
                        inj = new WordDelayLine("inj", 1); ej = new WordDelayLine("eject", 1); rt.fromInjection = inj; rt.toDirector = ej;
                        if (tile < 3) { cnc = new WordHold("cnc"); toCnc = new WordFIFO("tocnc", 16); rt.fromCnc = cnc; rt.toCnc = toCnc; }
                        Config c = new Config();
                        tok = new SRAMReadQueue(tile, c); rt.tokens = tok;
                    }
                    continue;
                }
                String[] f = line.split(" ");
                t = Long.parseLong(f[0]);
                if (t > 1) observe(out, rt, t - 1, lout, ej, toCnc, tok);     // the outputs of t - 1, visible at t
                for (int i = 1; i < f.length; i++) {
                    String[] w = f[i].substring(f[i].indexOf(':') + 1).split(",");
                    long x = hex(w[0]), y = hex(w[1]), z = hex(w[2]);
                    char k = f[i].charAt(0);
                    if (k == 'L') lin[f[i].charAt(1) - '0'].put(t, x, y, z);
                    else if (k == 'I') inj.put(t, x, y, z);
                    else cnc.put(t, x, y, z);
                }
                rt.runCycle(t);
                fp[0] = own(rt, t);
            }
            observe(out, rt, t, lout, ej, toCnc, tok);
        }
    }
    static final long[] fp = new long[1];
    /** The router's own stream at the end of t, as StateFp.router writes it (without the buses delivering into it). */
    static long own(Router r, long t) {
        StateFp.Out o = new StateFp.Out();
        int[] rr = (int[]) StateFp.o(r, "rr"); for (int k = 0; k < rr.length; k++) o.w("rr" + k, rr[k]);
        for (int q = 0; q < r.q.length; q++) for (int f = 0; f < r.q[q].length; f++) StateFp.fifo(o, "q" + q + "." + f, r.q[q][f], t);
        return o.done();
    }
    static void observe(Writer out, Router r, long t, WordDelayLine[] lout, WordDelayLine ej, WordFIFO toCnc, SRAMReadQueue tok) throws IOException {
        StringBuilder sb = new StringBuilder().append(t);
        long u = t + 1;
        for (int d = 0; d < 4; d++) if (lout[d].ready(u)) { sb.append(" O").append(d).append(':').append(w3(lout[d].peekA(u), lout[d].peekB(u), lout[d].peekC(u))); lout[d].take(u); }
        if (ej.ready(u)) { sb.append(" E:").append(w3(ej.peekA(u), ej.peekB(u), ej.peekC(u))); ej.take(u); }
        if (toCnc != null && toCnc.ready(u)) { sb.append(" X:").append(w3(toCnc.peekA(u), toCnc.peekB(u), toCnc.peekC(u))); toCnc.take(u); }
        if (StateFp.l(tok, "lastT") == t && StateFp.l(tok, "freesPending") > 0) sb.append(" F");
        // ref counts an overflow when the FIFO next settles (its first touch after the put: lazily, when a selector
        // reaches it, a put lands in it, or idle-skip's quiescence scan reads it). The framework judges every put at
        // t + 1 (PORTS.md 3): settle every FIFO here, at t + 1, so the count is the overflows of every put through t.
        long ovf = 0; for (WordFIFO[] fs : r.q) for (WordFIFO f : fs) { f.sizeAtStart(u); ovf += f.overflowCount; }
        sb.append(" Q:").append(Long.toHexString(fp[0])).append(',').append(ovf).append(',').append(r.routeErrors).append(',').append(r.ejectErrors).append('\n');
        out.write(sb.toString());
    }
    static String w3(long a, long b, long c) { return Long.toHexString(a) + "," + Long.toHexString(b) + "," + Long.toHexString(c); }

    // ------------------------------------------------------------------------------------------------ the tables
    /** Per tile: FNV-1a over unicast(src, dst) for src, dst in 0..127, then broadcast(port, src) for port 0-5, src 0..127. */
    static void table(Hops hops) {
        for (int me = 0; me < 64; me++) {
            Router r = new Router(me, hops, 16, 64, me < 3);
            long h = 0xcbf29ce484222325L;
            for (int s = 0; s < 128; s++) for (int d = 0; d < 128; d++) { h ^= r.unicast(s, d); h *= 0x100000001b3L; }
            for (int p = 0; p < 6; p++) for (int s = 0; s < 128; s++) { h ^= r.broadcast(p, s); h *= 0x100000001b3L; }
            System.out.printf("%d %016x%n", me, h);
        }
    }
}
