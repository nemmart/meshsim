/**
 * The SRAM read queue, the tokens and the return queues (spec 4.5, as of 2026-10-26n): one queue of big-SRAM read
 * requests from three clients at fixed priority -- 0 the GATHER engine (gather values), 1 the address generator, 2 the
 * controller read port (4.3) -- and their returns, turned into packets for the injection point.
 *
 * Client request words: a = source chunk address (21) | width (2) << 21 | mode (4) << 23 | dst (7) << 27,
 * b = destination address (21, the packet's addr) | reply address (12) << 24.
 * Width: the size code (3.1): 0 32 bits, 1 64 bits, 2 128 bits; 3 is undefined, and meshsim makes it a zero-width entry
 * (no memory access, payload 0). Every return is stamped with its width as the packet's size. A read-port return carries
 * the simulator-only {@link LinkWord#TAG_READPORT}.
 *
 * Tokens (4.5): one pool of `readTokens` (64), shared by the three clients. Every read the queue takes holds one. A token
 * is freed when its packet is injected ({@link #injected}), except a loopback packet's (dst = me or broadcast), which
 * holds it until it leaves the loopback FIFO for the eject selector ({@link #loopbackLeft}, from the router). The send
 * port's loopback packets take a token at injection too ({@link #takeForSend}). Packets that hold a token carry the
 * simulator-only {@link LinkWord#TAG_TOKEN}. Room is judged on the count at the start of the cycle: a token freed in a
 * cycle is free from the next (4.13's rule).
 *
 * Issue: one request per cycle, the highest-priority client with one ready, while a token is free and the area has
 * room. A request goes to the SRAM's read holding register; if the SRAM leaves the previous one there (its area's
 * queue is full) the put is refused and retried: the queue stalls at its head (no bypass). A request that does not fit
 * in one row goes as two parts on consecutive cycles (part 1 ahead of every client).
 *
 * Returns (4.1, 4.5, item 77): the big SRAM hands back each ordered client's reads in the order it issued them, so the
 * address generator and the GATHER engine each have an in-order return queue of their held reads' metadata; the read
 * port's returns are unordered (each lands at its own word). In a cycle when more than one client has a return ready,
 * one packet goes to the read-return holding register, by the request priority (gather value, address generator, read
 * port); a return that loses waits, and its client's later returns wait behind it. Within the read port, the earliest
 * ready goes first. A zero-width entry completes 2 + sramReadLatency cycles after issue, the latency of an unqueued read.
 */
final class SRAMReadQueue implements Component {
    static final int GATHER = 0, AG = 1, READPORT = 2;
    static final String[] CLIENT = { "gather", "address generator", "read port" };
    final int me;
    final Config cfg;
    // ---- bus ends ----
    final WordSource[] client = new WordSource[3];
    WordHold toSram;                                  // the SRAM's read holding register
    final WordSource[] fromSram = new WordSource[BigSRAM.AREAS];
    WordHold toInjection;                             // read returns as packets (read-port returns tagged)
    // ---- the token slots ----
    private final int cap;
    private final long[] reqA, reqB, issueT, readyT, lo, hi;
    private final int[] partsLeft, cl;
    private final java.util.ArrayDeque<Integer> free = new java.util.ArrayDeque<>();
    private final java.util.ArrayDeque<Integer>[] order;   // per ordered client: its held reads in issue order (GATHER, AG)
    private final java.util.TreeSet<long[]> rpReady = new java.util.TreeSet<>((x, y) -> x[0] != y[0] ? Long.compare(x[0], y[0]) : Long.compare(x[1], y[1]));
    private final boolean[] rpIsReady;
    private int reads;                                // reads held (issued, not yet injected)
    private int loopHeld;                             // tokens held by packets on their way through the loopback
    private long lastT = Long.MIN_VALUE; private int startHeld, takenNow, freesPending;
    // ---- issue state ----
    private int splitTok = -1, splitAddr, splitN;     // the second part of a request spanning two rows
    // ---- stats ----
    final long[] issued = new long[3];
    private final int[] inFlightBy = new int[3];
    final long[] firstIssueT = { -1, -1, -1 }, lastIssueT = { -1, -1, -1 };
    long spans;                                       // reads that cross a row boundary: two area accesses (4.1: allowed)
    long outOfRange;                                  // reads wholly out of range (a smaller device): no area access, payload 0
    long stallCycles, windowFullCycles, packets, sendTokenWaits;
    int peakHeld;
    final long[] latMax = new long[3], latSum = new long[3], latN = new long[3];

    @SuppressWarnings("unchecked")
    SRAMReadQueue(int me, Config cfg) {
        this.me = me; this.cfg = cfg;
        cap = cfg.readTokens;
        reqA = new long[cap]; reqB = new long[cap]; issueT = new long[cap]; readyT = new long[cap]; lo = new long[cap]; hi = new long[cap];
        partsLeft = new int[cap]; cl = new int[cap]; rpIsReady = new boolean[cap];
        for (int i = 0; i < cap; i++) free.add(i);
        order = new java.util.ArrayDeque[] { new java.util.ArrayDeque<Integer>(), new java.util.ArrayDeque<Integer>() };
    }

    // ---------------------------------------------------------------- the token pool
    /** Enter cycle t: frees of the previous cycle take effect; the count at the start of t is fixed. */
    private void advance(long t) {
        if (t == lastT) return;
        loopHeld -= freesPending; freesPending = 0;
        startHeld = reads + loopHeld; takenNow = 0; lastT = t;
    }
    private boolean tokenFree(long t) { advance(t); return startHeld + takenNow < cap; }
    /** Tokens held now (reads and loopback packets). */
    int held() { return reads + loopHeld; }
    /** The injection point takes a send-port packet for the tile itself (or a broadcast) at t if a token is free; the
     *  packet then holds it until it leaves the loopback FIFO. */
    boolean takeForSend(long t) {
        if (!tokenFree(t)) { sendTokenWaits++; return false; }
        takenNow++; loopHeld++; peakHeld = Math.max(peakHeld, held());
        return true;
    }
    /** The router's eject selector took a token-holding packet out of the loopback FIFO at t: its token is free from t + 1. */
    void loopbackLeft(long t) { advance(t); freesPending++; }
    /** The injection point injected a read return at t (its header): the token is freed, or held through the loopback. */
    void injected(long t, long h) {
        advance(t);
        int tok = (int) ((h >>> 41) & 0x3F);
        reads--; inFlightBy[cl[tok]]--;
        long lat = t - issueT[tok]; int c = cl[tok];
        latSum[c] += lat; latN[c]++; if (lat > latMax[c]) latMax[c] = lat;
        if (tokensOn && loops(LinkWord.dst(h))) loopHeld++;           // the token moves to the loopback packet: still held
        free.add(tok);
    }
    boolean loops(int dst) { return dst == me || dst == LinkWord.BROADCAST; }
    boolean tokensOn = true;                          // false: an experiment (Config.loopbackTokens)

    /** A client's request: (a, b) words. */
    static long reqA(int srcChunk, int width, int mode, int dst) { return (srcChunk & 0x1FFFFF) | (long) (width & 3) << 21 | (long) (mode & 15) << 23 | (long) (dst & 0x7F) << 27; }
    static int aAddr(long a) { return (int) (a & 0x1FFFFF); }
    static int aWidth(long a) { return (int) ((a >>> 21) & 3); }
    static int aMode(long a) { return (int) ((a >>> 23) & 15); }
    static int aDst(long a) { return (int) ((a >>> 27) & 0x7F); }
    static long reqB(int dstAddr, int replyAddr) { return (dstAddr & 0x1FFFFF) | (long) (replyAddr & 0xFFF) << 24; }
    /** Chunks read for a size code: 1, 2 or 4; 0 for the undefined code 11 (meshsim reads nothing: a zero-width entry). */
    static int chunksOf(int size) { return size == LinkWord.SIZE_UNDEF ? 0 : 1 << size; }

    @Override public void runCycle(long t) {
        advance(t);
        // 1. returns from the areas
        for (int a = 0; a < BigSRAM.AREAS; a++) {
            WordSource s = fromSram[a];
            if (s == null || !s.ready(t)) continue;
            long w = s.peekA(t), rlo = s.peekB(t), rhi = s.peekC(t);
            s.take(t);
            int tok = (int) (w & 0xFFFF), part = (int) ((w >>> 16) & 1);
            place(tok, part, rlo, rhi);
            if (--partsLeft[tok] == 0) ready(tok, t);
        }
        // 2. one return a cycle, by request priority among the clients with a return ready
        release(t);
        // 3. issue
        if (toSram.refused(t)) { toSram.retry(t); stallCycles++; return; }
        if (splitTok >= 0) { putPart(t, splitTok, splitAddr, splitN, 1); splitTok = -1; return; }
        if (!tokenFree(t)) { for (WordSource c : client) if (c != null && c.ready(t)) { windowFullCycles++; break; } return; }
        for (int c = 0; c < 3; c++) {
            WordSource s = client[c];
            if (s == null || !s.ready(t)) continue;
            long a = s.peekA(t), b = s.peekB(t);
            s.take(t);
            issue(t, c, a, b);
            return;
        }
    }

    private void ready(int tok, long t) { readyT[tok] = t; if (cl[tok] == READPORT) { rpIsReady[tok] = true; rpReady.add(new long[] { t, tok }); } }

    private void issue(long t, int c, long a, long b) {
        int tok = free.poll();
        takenNow++; reads++; peakHeld = Math.max(peakHeld, held());
        reqA[tok] = a; reqB[tok] = b; issueT[tok] = t; cl[tok] = c; lo[tok] = hi[tok] = 0; readyT[tok] = Long.MAX_VALUE; rpIsReady[tok] = false;
        if (c != READPORT) order[c].add(tok);
        issued[c]++; inFlightBy[c]++;
        if (firstIssueT[c] < 0) firstIssueT[c] = t; lastIssueT[c] = t;
        int width = aWidth(a), n = chunksOf(width), addr = aAddr(a);
        if (n == 0) { partsLeft[tok] = 0; ready(tok, t + 2 + cfg.sramReadLatency); return; }
        int o = addr & 3;
        if (o + n > 4) spans++;
        // an out-of-range part (a smaller device) makes no area access: its chunks read 0, and it counts as returned
        boolean out0 = addr >= cfg.memChunks;
        if (o + n <= 4) { partsLeft[tok] = out0 ? 0 : 1; if (!out0) putPart(t, tok, addr, n, 0); }
        else {
            int a1 = (addr + 4 - o) & 0x1FFFFF;
            boolean out1 = a1 >= cfg.memChunks;
            partsLeft[tok] = (out0 ? 0 : 1) + (out1 ? 0 : 1);
            if (!out0) putPart(t, tok, addr, 4 - o, 0);
            if (!out1) { if (out0) putPart(t, tok, a1, n - (4 - o), 1); else { splitTok = tok; splitAddr = a1; splitN = n - (4 - o); } }
        }
        if (partsLeft[tok] == 0) { ready(tok, t); outOfRange++; }
    }
    private void putPart(long t, int tok, int addr, int n, int part) {
        toSram.put(t, BigSRAM.readReq(addr, n, tok, part), 0, 0);
    }

    /** Store a returned row's chunks into the entry: part 0 holds chunks from the request's offset, part 1 the rest. */
    private void place(int tok, int part, long rlo, long rhi) {
        int addr = aAddr(reqA[tok]), n = chunksOf(aWidth(reqA[tok])), o = addr & 3;
        int first = part == 0 ? o : 0, cnt = part == 0 ? Math.min(n, 4 - o) : n - (4 - o), dstFirst = part == 0 ? 0 : 4 - o;
        for (int j = 0; j < cnt; j++) {
            int k = first + j;
            long v = ((k < 2 ? rlo : rhi) >>> (32 * (k & 1))) & 0xFFFFFFFFL;
            int d = dstFirst + j;
            if (d < 2) lo[tok] |= v << (32 * d); else hi[tok] |= v << (32 * (d - 2));
        }
    }

    private void release(long t) {
        int tok = -1;
        for (int c = 0; c < 2 && tok < 0; c++) {          // GATHER, then AG: the head of each in-order queue
            Integer h = order[c].peek();
            if (h != null && partsLeft[h] == 0 && readyT[h] <= t) { tok = h; order[c].poll(); }
        }
        if (tok < 0 && !rpReady.isEmpty() && rpReady.first()[0] <= t) { tok = (int) rpReady.pollFirst()[1]; rpIsReady[tok] = false; }
        if (tok < 0) return;
        emit(t, tok);
    }
    /** The return as one packet (4.5): size = the request's width; payload the chunks read (bits above the size 0). The
     *  token travels in the simulator-only header bits [46:41] until the injection point frees it. */
    private void emit(long t, int tok) {
        long a = reqA[tok], b = reqB[tok];
        int mode = aMode(a), dst = aDst(a), dstAddr = (int) (b & 0x1FFFFF);
        int size = aWidth(a);
        long pl = chunksOf(size) == 0 ? 0 : lo[tok], ph = chunksOf(size) == 0 ? 0 : hi[tok];
        long h = LinkWord.hdr(0, dst, mode, dstAddr, size) | (long) tok << 41 | LinkWord.TAG_FROM_SRQ
                | (cl[tok] == READPORT ? LinkWord.TAG_READPORT : 0) | (tokensOn && loops(dst) ? LinkWord.TAG_TOKEN : 0);
        toInjection.put(t, LinkWord.maskLo(pl, size), h, LinkWord.maskHi(ph, size));
        packets++;
    }
    int inFlightOf(int c) { return inFlightBy[c]; }

    boolean idle() { return reads == 0 && splitTok < 0; }
    /** HALT (6.1): the return queues clear and every read's token is free (nothing is in flight when RUN/HALT changes:
     *  invariant 7). Loopback packets keep theirs until they leave the loopback FIFO. */
    void haltReset() {
        order[0].clear(); order[1].clear(); rpReady.clear(); splitTok = -1;
        free.clear(); for (int i = 0; i < cap; i++) free.add(i);
        reads = 0; java.util.Arrays.fill(inFlightBy, 0);
    }
    int inFlight() { return held(); }
    void resetStats() { java.util.Arrays.fill(issued, 0); java.util.Arrays.fill(latMax, 0); java.util.Arrays.fill(latSum, 0); java.util.Arrays.fill(latN, 0); stallCycles = windowFullCycles = packets = 0; peakHeld = held(); }
}
