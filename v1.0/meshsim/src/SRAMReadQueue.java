/**
 * The SRAM read queue (spec 4.5): one queue of big-SRAM read requests from three clients at fixed priority --
 * 0 the GATHER engine (gather value), 1 the address generator, 2 the controller read port (4.3) -- and its returns,
 * turned into packets for the injection point. A read-port read's return is a mode-7 packet to the tile itself like
 * any other (spec 4.3): mode 7 (reply), dst = me; it carries the simulator-only {@link ReadPort#TAG} in the
 * bus's third word so the injection point and the director can account for it.
 *
 * Client request words: a = source chunk address (20) | width (2) << 20 | mode (3) << 22 | dst (7) << 25,
 * b = destination address (20, the packet's addr) | reply address (12) << 20.
 * Width: 0 none (a zero-width entry: no memory access; the packet's payload is the reply address), 1 32 bits,
 * 2 64 bits, 3 128 bits (two packets, low then high, on consecutive cycles).
 *
 * Issue: one request per cycle, the highest-priority client with one ready, while fewer than `readTokens` are in
 * flight. A request goes to the SRAM's read holding register; if the SRAM leaves the previous one there (its area's
 * queue is full) the put is refused and retried: the queue stalls at its head. A request that does not fit in one
 * row goes as two parts on consecutive cycles.
 *
 * Returns: released strictly in request order (required: mode-2 packets and gather values land at PTR in arrival
 * order), one packet per cycle into the read-return holding register, which the injection point takes first.
 * A zero-width entry completes 2 + sramReadLatency cycles after issue, the latency of an unqueued read (put at t,
 * taken by the SRAM at t + 1, served at t + 2, returned at t + 2 + sramReadLatency).
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
    // ---- the in-order window ----
    private final int cap;
    private final long[] reqA, reqB, issueT, readyT, lo, hi;
    private final int[] partsLeft, cl;
    private int head, count;
    private boolean hiPending;                        // the second packet of a 128-bit return is due
    // ---- issue state ----
    private int splitTok = -1, splitAddr, splitN;     // the second part of a request spanning two rows
    // ---- stats ----
    final long[] issued = new long[3];
    private final int[] inFlightBy = new int[3];
    final long[] firstIssueT = { -1, -1, -1 }, lastIssueT = { -1, -1, -1 };        // requests in the window per client
    long popRoomIssues;                               // requests the M15 rule (a token freed this cycle) took that 13a's refuses
    long spans;                                       // reads that cross a row boundary: two area accesses (4.1: allowed)
    long outOfRange;                                  // reads wholly out of range (a smaller device): no area access, payload 0 (13a)
    long stallCycles, windowFullCycles, packets, latencySum, latencyMax, latencyN;
    final long[] latMax = new long[3], latSum = new long[3], latN = new long[3];

    SRAMReadQueue(int me, Config cfg) {
        this.me = me; this.cfg = cfg;
        cap = cfg.readTokens;
        reqA = new long[cap]; reqB = new long[cap]; issueT = new long[cap]; readyT = new long[cap]; lo = new long[cap]; hi = new long[cap];
        partsLeft = new int[cap]; cl = new int[cap];
    }

    /** A client's request: (a, b) words. */
    static long reqA(int srcChunk, int width, int mode, int dst) { return (srcChunk & 0xFFFFF) | (long) (width & 3) << 20 | (long) (mode & 15) << 22 | (long) (dst & 0x7F) << 26; }   // M12: 4-bit mode
    static long reqB(int dstAddr, int replyAddr) { return (dstAddr & 0xFFFFF) | (long) (replyAddr & 0xFFF) << 20; }
    static int chunksOf(int width) { return width == 0 ? 0 : width == 1 ? 1 : width == 2 ? 2 : 4; }

    @Override public void runCycle(long t) {
        // 1. returns from the areas
        for (int a = 0; a < BigSRAM.AREAS; a++) {
            WordSource s = fromSram[a];
            if (!s.ready(t)) continue;
            long w = s.peekA(t), rlo = s.peekB(t), rhi = s.peekC(t);
            s.take(t);
            int tok = (int) (w & 0xFFFF), part = (int) ((w >>> 16) & 1);
            place(tok, part, rlo, rhi);
            if (--partsLeft[tok] == 0) readyT[tok] = t;
        }
        // 2. release in request order, one packet per cycle
        int countAtStart = count;
        release(t);
        // 3. issue
        if (toSram.refused(t)) { toSram.retry(t); stallCycles++; return; }
        if (splitTok >= 0) { putPart(t, splitTok, splitAddr, splitN, 1); splitTok = -1; return; }
        boolean full = count == cap;
        if (cfg.fifoCountBeforePop && countAtStart == cap && !full) { for (WordSource c : client) if (c != null && c.ready(t)) { popRoomIssues++; break; } full = true; }
        if (full) { for (WordSource c : client) if (c != null && c.ready(t)) { windowFullCycles++; break; } return; }
        for (int c = 0; c < 3; c++) {
            WordSource s = client[c];
            if (s == null || !s.ready(t)) continue;
            long a = s.peekA(t), b = s.peekB(t);
            s.take(t);
            issue(t, c, a, b);
            return;
        }
    }

    private void issue(long t, int c, long a, long b) {
        int tok = (head + count) % cap;
        count++;
        reqA[tok] = a; reqB[tok] = b; issueT[tok] = t; cl[tok] = c; lo[tok] = hi[tok] = 0;
        issued[c]++; inFlightBy[c]++;
        if (firstIssueT[c] < 0) firstIssueT[c] = t; lastIssueT[c] = t;
        int width = (int) ((a >>> 20) & 3), n = chunksOf(width), addr = (int) (a & 0xFFFFF);
        if (n == 0) { partsLeft[tok] = 0; readyT[tok] = t + 2 + cfg.sramReadLatency; return; }
        int o = addr & 3;
        if (o + n > 4) spans++;
        // 13a: an out-of-range part (a smaller device) makes no area access: its chunks read 0, and it counts as returned
        boolean out0 = addr >= cfg.memChunks;
        if (o + n <= 4) { partsLeft[tok] = out0 ? 0 : 1; if (!out0) putPart(t, tok, addr, n, 0); }
        else {
            int a1 = (addr + 4 - o) & 0xFFFFF;
            boolean out1 = a1 >= cfg.memChunks;
            partsLeft[tok] = (out0 ? 0 : 1) + (out1 ? 0 : 1);
            if (!out0) putPart(t, tok, addr, 4 - o, 0);
            if (!out1) { if (out0) putPart(t, tok, a1, n - (4 - o), 1); else { splitTok = tok; splitAddr = a1; splitN = n - (4 - o); } }
        }
        if (partsLeft[tok] == 0) { readyT[tok] = t; outOfRange++; }
    }
    private void putPart(long t, int tok, int addr, int n, int part) {
        toSram.put(t, addr | (long) n << 20 | (long) tok << 24 | (long) part << 40, 0, 0);
    }

    /** Store a returned row's chunks into the entry: part 0 holds chunks from the request's offset, part 1 the rest. */
    private void place(int tok, int part, long rlo, long rhi) {
        int addr = (int) (reqA[tok] & 0xFFFFF), n = chunksOf((int) ((reqA[tok] >>> 20) & 3)), o = addr & 3;
        int first = part == 0 ? o : 0, cnt = part == 0 ? Math.min(n, 4 - o) : n - (4 - o), dstFirst = part == 0 ? 0 : 4 - o;
        for (int j = 0; j < cnt; j++) {
            int k = first + j;
            long v = ((k < 2 ? rlo : rhi) >>> (32 * (k & 1))) & 0xFFFFFFFFL;
            int d = dstFirst + j;
            if (d < 2) lo[tok] |= v << (32 * d); else hi[tok] |= v << (32 * (d - 2));
        }
    }

    private void release(long t) {
        if (hiPending) {
            int tok = head;
            emit(t, tok, hi[tok], 1);
            hiPending = false; retire(t, tok);
            return;
        }
        if (count == 0) return;
        int tok = head;
        if (partsLeft[tok] > 0 || readyT[tok] > t) return;
        int width = (int) ((reqA[tok] >>> 20) & 3);
        long payload = width == 0 ? (reqB[tok] >>> 20) & 0xFFF : lo[tok];
        if (cl[tok] == READPORT) payload = lo[tok] & 0xFFFFFFFFL;      // a 32-bit result (4.3)
        emit(t, tok, payload, 0);
        if (width == 3) hiPending = true; else retire(t, tok);
    }
    private void emit(long t, int tok, long payload, int second) {
        long a = reqA[tok], b = reqB[tok];
        int mode = (int) ((a >>> 22) & 15), dst = (int) ((a >>> 26) & 0x7F), dstAddr = (int) (b & 0xFFFFF);
        if (second == 1 && mode == LinkWord.M_CMEM) dstAddr += 2;
        toInjection.put(t, payload, LinkWord.hdr(0, dst, mode, dstAddr), cl[tok] == READPORT ? ReadPort.TAG : 0);
        packets++;
    }
    private void retire(long t, int tok) {
        long lat = t - issueT[tok]; int c = cl[tok];
        latSum[c] += lat; latN[c]++; if (lat > latMax[c]) latMax[c] = lat;
        head = (head + 1) % cap; count--; inFlightBy[c]--;
    }
    int inFlightOf(int c) { return inFlightBy[c]; }

    boolean idle() { return count == 0 && splitTok < 0 && !hiPending; }
    /** HALT (13a): the token FIFO clears (nothing is in flight when RUN/HALT changes: invariant 7). */
    void haltReset() { count = 0; head = 0; splitTok = -1; hiPending = false; java.util.Arrays.fill(inFlightBy, 0); }
    int inFlight() { return count; }
    void resetStats() { java.util.Arrays.fill(issued, 0); java.util.Arrays.fill(latMax, 0); java.util.Arrays.fill(latSum, 0); java.util.Arrays.fill(latN, 0); stallCycles = windowFullCycles = packets = 0; }
}
