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
 *   java -cp REF:. RouterConf gen TILE SEED CYCLES [send]   a script on stdout (send: the injection point behind the
 *                                                           router, fed by a core's send holding register)
 *   java -cp REF:. RouterConf run HOPS SCRIPT           ref's observations on stdout
 *   java -cp REF:. RouterConf table HOPS                every router's routing decisions, hashed per tile
 * </pre>
 *
 * Script: "# routerconf v1", "# tile T", "# seed S cycles N", then one line per cycle t (from 1, consecutive):
 * {@code t [Lp:a,b,c]... [I:a,b,c] [C:a,b,c]}: a word put at t on link input p (0-3: from-left, from-right, from-up,
 * from-down), on the injection link, or (tiles 0-2) in C&C's send holding register; each is visible to the router at
 * t + 1 (links and the injection of latency 1; the hold's put of t). Words in lowercase hex.
 *
 * With "# send" (stage 2: the injection point, InjectionPath with SRAMReadQueue's tokens, as Tile runs them: the router,
 * then the read queue and the injection point), source 4 is {@code S:a,b,c}: the core's send store at t into the send
 * holding register (the core runs after them); if the put of t - 1 was refused, the core retries it at t instead and the
 * script's word of t is dropped (the core stalls).
 *
 * Observation line for cycle t, after the router ran t: {@code t [Od:a,b,c]... [E:a,b,c] [X:a,b,c] [F] Q:fp,ovf,rerr,eerr}:
 * the words the router put at t on link output d (to-right, to-left, to-down, to-up), on the eject bus, on the
 * to-controller output; F if a token-holding packet left the loopback FIFO at t (srq.loopbackLeft(t)); then the router's
 * own canonical stream (StateFp.router's rr and FIFOs, AUDIT-M2 6.2) hashed, the overflow total (every put through t
 * judged: see observe), route and eject errors. With "# send", then {@code [B] P:fp,held,waits,refusals,violations}: B if
 * STATUS's send-busy is set at the end of t (!sendHold.isEmpty(t)); the injection unit's stream (StateFp's "inj": the
 * read-return and send holds) hashed; the tokens held as at the start of t + 1; token-wait cycles; the send hold's
 * refusals; the injection point's violations.
 *
 * M3 (WORKER16): controllers' routers and the C&C links (Config.ccCores; AUDIT-M3 3.4).
 * <pre>
 *   java -cp REF:. RouterConf gen TILE SEED CYCLES link      tiles 0-2: the fifth port as the C&C links (WordDelayLine 1
 *                                                           each way, as CcCores wires them) in place of the endpoint
 *   java -cp REF:. RouterConf gen TILE SEED CYCLES [dma]     TILE 64-66: a controller's router (FEEDERS_CC, ccRoute)
 * </pre>
 * The script's extra header line {@code # fifth link} (tiles 0-2) or {@code # cc} / {@code # cc dma} (64-66). For a
 * controller: L0 the C&C link into CC_LINK_IN (WordDelayLine 1), L1 the host port (WordFIFO 4, CC_HOST_IN), L2 the DMA
 * (WordFIFO 4, CC_DMA_IN; with dma only), I the injection; outputs O0 (CC_LINK_OUT) and E. Lines as above.
 */
final class RouterConf {
    public static void main(String[] a) throws IOException {
        switch (a[0]) {
            case "gen" -> gen(Integer.parseInt(a[1]), Long.parseLong(a[2]), Integer.parseInt(a[3]), a.length > 4 ? a[4] : "");
            case "run" -> run(Hops.load(Path.of(a[1])), a[2]);
            case "table" -> table(Hops.load(Path.of(a[1])));
            default -> throw new IllegalArgumentException(a[0]);
        }
    }

    // ------------------------------------------------------------------------------------------------ the generator
    /** Rates vary in phases of 1-64 cycles per source (idle to every cycle), so FIFOs fill, overflow and drain. */
    static void gen(int tile, long seed, int cycles, String mode) {
        boolean send = mode.equals("send");
        if (tile >= 64) { genCc(tile, seed, cycles, mode.equals("dma")); return; }
        Random r = new Random(seed);
        StringBuilder sb = new StringBuilder();
        sb.append("# routerconf v1\n# tile ").append(tile).append("\n# seed ").append(seed).append(" cycles ").append(cycles).append(send ? " send" : "").append('\n');
        if (mode.equals("link")) sb.append("# fifth link\n");
        int nsrc = tile < 3 ? 6 : 5;
        double[] rate = new double[nsrc]; int[] left = new int[nsrc];
        for (int t = 1; t <= cycles; t++) {
            sb.append(t);
            for (int s = 0; s < nsrc; s++) {
                if (left[s]-- <= 0) { left[s] = 1 + r.nextInt(64); int k = r.nextInt(4); rate[s] = k == 0 ? 0 : k == 1 ? 0.15 : k == 2 ? 0.6 : 1.0; }
                if (r.nextDouble() >= rate[s]) continue;
                String tag = s < 4 ? "L" + s : s == 4 ? (send ? "S" : "I") : "C";
                sb.append(' ').append(tag).append(':').append(send && s == 4 ? sendWord(r, tile, (long) t << 3 | s) : word(r, tile, s, (long) t << 3 | s));
            }
            sb.append('\n');
            if (sb.length() > 1 << 16) { System.out.print(sb); sb.setLength(0); }
        }
        System.out.print(sb);
    }
    /** A controller's router: the link, the host port, the DMA (with dma) and the injection, at rates varying in phases
     *  as above. The injection's packets go mostly to the controller itself or elsewhere (the link); the others' are
     *  any packet (every input but the loopback and the DMA is ejected, so a dst not this controller is an eject error). */
    static void genCc(int tile, long seed, int cycles, boolean dma) {
        Random r = new Random(seed);
        StringBuilder sb = new StringBuilder();
        sb.append("# routerconf v1\n# tile ").append(tile).append("\n# seed ").append(seed).append(" cycles ").append(cycles).append('\n');
        sb.append(dma ? "# cc dma\n" : "# cc\n");
        int[] srcs = dma ? new int[] { 0, 1, 2, 4 } : new int[] { 0, 1, 4 };
        double[] rate = new double[5]; int[] left = new int[5];
        for (int t = 1; t <= cycles; t++) {
            sb.append(t);
            for (int s : srcs) {
                if (left[s]-- <= 0) { left[s] = 1 + r.nextInt(64); int k = r.nextInt(4); rate[s] = k == 0 ? 0 : k == 1 ? 0.15 : k == 2 ? 0.6 : 1.0; }
                if (r.nextDouble() >= rate[s]) continue;
                String tag = s == 4 ? "I" : "L" + s;
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
        if (k < 30) dst = tile; else if (tile >= 64 && k < 40) dst = 64 + r.nextInt(3); else if (k < 45) dst = LinkWord.BROADCAST; else if (k < 50) dst = 64 + r.nextInt(3);
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

    /** A core's send: mostly to the tile itself or a broadcast (they take loopback tokens), the rest anywhere. The
     *  simulator-only bits are 0, as the register block makes them (TileControl.sendHeader). */
    static String sendWord(Random r, int tile, long seq) {
        int k = r.nextInt(100);
        int dst = k < 40 ? tile : k < 70 ? LinkWord.BROADCAST : k < 72 ? 64 + r.nextInt(3) : k < 73 ? r.nextInt(128) : r.nextInt(64);
        long h = LinkWord.hdr(0, dst, r.nextInt(8), r.nextInt(8), r.nextInt(), r.nextInt(4));
        return Long.toHexString(seq) + "," + Long.toHexString(h) + "," + Integer.toHexString(r.nextInt(1 << 12));
    }

    // ------------------------------------------------------------------------------------------------ the driver
    static long hex(String s) { return Long.parseUnsignedLong(s, 16); }

    static void run(Hops hops, String script) throws IOException {
        int tile = -1;
        try (BufferedReader in = new BufferedReader(new FileReader(script), 1 << 16);
             Writer out = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            Router rt = null; WordDelayLine[] lin = new WordDelayLine[4], lout = new WordDelayLine[4];
            WordDelayLine inj = null, ej = null; WordHold cnc = null; WordBus toCnc = null; SRAMReadQueue tok = null;
            WordDelayLine cncLink = null; WordSource[] lsrc = new WordSource[4];
            InjectionPath ip = null; WordHold send = null, rret = null;
            long t = 0;
            for (String line; (line = in.readLine()) != null; ) {
                if (line.startsWith("#")) {
                    if (line.startsWith("# tile ")) {
                        tile = Integer.parseInt(line.substring(7).trim());
                        rt = new Router(tile, hops, 16, 64, tile < 3);
                        for (int d = 0; d < 4; d++) {
                            if (tile >= 64 && d > 0) continue;
                            lin[d] = new WordDelayLine("in" + d, 1); lout[d] = new WordDelayLine("out" + d, 1); rt.linkIn[d] = lin[d]; rt.linkOut[d] = lout[d]; lsrc[d] = lin[d];
                        }
                        if (tile >= 64) { WordFIFO h = new WordFIFO("host", 4); rt.linkIn[Router.CC_HOST_IN] = h; lsrc[1] = h; }
                        inj = new WordDelayLine("inj", 1); ej = new WordDelayLine("eject", 1); rt.fromInjection = inj; rt.toDirector = ej;
                        if (tile < 3) { cnc = new WordHold("cnc"); WordFIFO f = new WordFIFO("tocnc", 16); toCnc = f; rt.fromCnc = cnc; rt.toCnc = f; }
                        Config c = new Config();
                        tok = new SRAMReadQueue(tile, c); rt.tokens = tok;
                    }
                    if (line.equals("# fifth link")) {                         // CcCores: WordDelayLine(ccLinkLatency 1) each way
                        cnc = null; cncLink = new WordDelayLine("from-cc", 1); WordDelayLine tl = new WordDelayLine("to-cc", 1);
                        rt.fromCnc = cncLink; rt.toCnc = tl; toCnc = tl;
                    }
                    if (line.equals("# cc dma")) { WordFIFO d = new WordFIFO("dma", 4); rt.linkIn[Router.CC_DMA_IN] = d; lsrc[2] = d; }
                    if (line.startsWith("# seed ") && line.endsWith(" send")) {
                        send = new WordHold("send"); rret = new WordHold("readReturn");
                        ip = new InjectionPath(tile); ip.readReturn = rret; ip.send = send; ip.toRouter = inj; ip.srq = tok;
                        tok.toSram = new WordHold("sram.rd"); tok.toInjection = rret;
                    }
                    continue;
                }
                String[] f = line.split(" ");
                t = Long.parseLong(f[0]);
                if (t > 1) observe(out, rt, t - 1, lout, ej, toCnc, tok);     // the outputs of t - 1, visible at t
                long[] core = null;                                            // the core's send store of t (runs last)
                for (int i = 1; i < f.length; i++) {
                    String[] w = f[i].substring(f[i].indexOf(':') + 1).split(",");
                    long x = hex(w[0]), y = hex(w[1]), z = hex(w[2]);
                    char k = f[i].charAt(0);
                    if (k == 'L') ((WordSink) lsrc[f[i].charAt(1) - '0']).put(t, x, y, z);
                    else if (k == 'I') inj.put(t, x, y, z);
                    else if (k == 'S') core = new long[] { x, y, z };
                    else if (cncLink != null) cncLink.put(t, x, y, z);
                    else cnc.put(t, x, y, z);
                }
                rt.runCycle(t);
                if (ip != null) {
                    tok.runCycle(t); ip.runCycle(t);                           // Tile's srqInj
                    if (send.refused(t)) send.retry(t);                        // the core stalls: the script's word is dropped
                    else if (core != null) send.put(t, core[0], core[1], core[2]);
                    StateFp.Out o = new StateFp.Out(); StateFp.hold(o, "readReturn", rret, t); StateFp.hold(o, "send", send, t);
                    sendFp[0] = (send.isEmpty(t) ? "" : " B") + " P:" + Long.toHexString(o.done()) + ","
                            + (StateFp.l(tok, "loopHeld") - StateFp.l(tok, "freesPending")) + "," + ip.tokenWaitCycles + "," + send.refusals + "," + ip.violations;
                }
                fp[0] = own(rt, t);
            }
            observe(out, rt, t, lout, ej, toCnc, tok);
        }
    }
    static final long[] fp = new long[1];
    static final String[] sendFp = { "" };
    /** The router's own stream at the end of t, as StateFp.router writes it (without the buses delivering into it). */
    static long own(Router r, long t) {
        StateFp.Out o = new StateFp.Out();
        int[] rr = (int[]) StateFp.o(r, "rr"); for (int k = 0; k < rr.length; k++) o.w("rr" + k, rr[k]);
        for (int q = 0; q < r.q.length; q++) for (int f = 0; f < r.q[q].length; f++) StateFp.fifo(o, "q" + q + "." + f, r.q[q][f], t);
        return o.done();
    }
    static void observe(Writer out, Router r, long t, WordDelayLine[] lout, WordDelayLine ej, WordBus toCnc, SRAMReadQueue tok) throws IOException {
        StringBuilder sb = new StringBuilder().append(t);
        long u = t + 1;
        for (int d = 0; d < 4; d++) if (lout[d] != null && lout[d].ready(u)) { sb.append(" O").append(d).append(':').append(w3(lout[d].peekA(u), lout[d].peekB(u), lout[d].peekC(u))); lout[d].take(u); }
        if (ej.ready(u)) { sb.append(" E:").append(w3(ej.peekA(u), ej.peekB(u), ej.peekC(u))); ej.take(u); }
        if (toCnc != null && toCnc.ready(u)) { sb.append(" X:").append(w3(toCnc.peekA(u), toCnc.peekB(u), toCnc.peekC(u))); toCnc.take(u); }
        if (StateFp.l(tok, "lastT") == t && StateFp.l(tok, "freesPending") > 0) sb.append(" F");
        // ref counts an overflow when the FIFO next settles (its first touch after the put: lazily, when a selector
        // reaches it, a put lands in it, or idle-skip's quiescence scan reads it). The framework judges every put at
        // t + 1 (PORTS.md 3): settle every FIFO here, at t + 1, so the count is the overflows of every put through t.
        long ovf = 0; for (WordFIFO[] fs : r.q) for (WordFIFO f : fs) { f.sizeAtStart(u); ovf += f.overflowCount; }
        sb.append(" Q:").append(Long.toHexString(fp[0])).append(',').append(ovf).append(',').append(r.routeErrors).append(',').append(r.ejectErrors).append(sendFp[0]).append('\n');
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
