/**
 * The big SRAM (spec 4.1): up to 16 contiguous areas of 128 bits x 32K rows (8 MB); meshsim's default is 8 (4 MB), and
 * the size is any multiple of 256 KB, the last area partial when it isn't a whole number of areas. In a 21-bit chunk
 * address, bits 20-17 are the area, 16-2 the row, 1-0 the chunk in the row. Each area does one read or one write of one row per cycle, served
 * from its own request queue in arrival order (depth `areaQueueDepth`, default 8). A read served at t returns its row
 * on the area's return line at t + `sramReadLatency` (default 2). Writes carry per-chunk enables, so a 64-bit write
 * is one access (spec 4.1): a 32-bit write is one chunk, a 128-bit write at chunk 0 the whole row.
 *
 * Clients: two holding registers, one per queue (spec 4.5). Each cycle the SRAM takes the SRAM write queue's request,
 * then the SRAM read queue's (the tie order: writes before reads), each only if its area's queue has room by its count
 * before the cycle's pop (13a: ready_w = count < depth, ready_r = count + push_w < depth); otherwise
 * the request stays in its holding register and the producer's next put is refused (it retries): the queue stalls.
 * An access that does not fit in one row is two requests, sent by the queue on consecutive cycles.
 *
 * Request words: read  a = chunk address | chunks (1, 2 or 4) << 21 | token << 24 | part << 40, b = c = 0;
 *                write a = chunk address | chunks (1, 2 or 4) << 21, b = chunks 0,1 (lo), c = chunks 2,3 (hi): the write's
 *                      first chunk is b's low word (a spanning write's part 1 arrives already shifted by the write engine).
 * A part covers chunks [addr & 3, (addr & 3) + chunks) of its row; the queue splits before sending.
 * Return words (per area): a = token | part << 16, b = row chunks 0,1, c = row chunks 2,3.
 *
 * Wrap and the small device: addresses are modulo 2^21; a chunk at or above `memChunks` reads 0 and a write to it is
 * dropped. An out-of-range read never reaches an area (13a: the SRAM read queue marks its token zero); an out-of-range
 * write still takes its turn in its area's queue. No condition.
 *
 * The disjoint-buffer checker (invariant 2): per phase, every chunk read and every chunk written is marked; a chunk
 * both read and written in one phase is a violation. Phases are marked by {@link #newPhase()}.
 */
final class BigSRAM implements Component {
    static final int AREAS = 16, ROWS = 32768, AREA_CHUNKS = ROWS * 4, CHUNKS = 1 << 21;
    final int me;
    final Config cfg;
    final int memChunks;
    private final int[] qStart = new int[AREAS];
    /** Accesses the M15 rule (room after the cycle's pop) took that 13a's rule (count before the pop) refuses. */
    long popRoomWrites, popRoomReads;
    private final int[][] data = new int[AREAS][];          // lazily allocated rows x 4 chunks
    // ---- bus ends ----
    WordSource readReq, writeReq;                           // holding registers of the two queues
    final WordDelayLine[] ret = new WordDelayLine[AREAS];   // per-area read returns, latency sramReadLatency
    // ---- area queues (internal to the unit) ----
    private final int depth;
    private final boolean[][] qWrite;
    private final int[][] qRow, qMask, qToken, qPart;
    private final int[][][] qData;
    private final int[] qHead = new int[AREAS], qCount = new int[AREAS], qWrites = new int[AREAS];
    // ---- stats ----
    final long[] reads = new long[AREAS], writes = new long[AREAS], combined = new long[AREAS];
    final int[] queuePeak = new int[AREAS];
    long readStallCycles, writeStallCycles, accesses;
    // ---- disjoint-buffer checker ----
    final long[] rdBits = new long[CHUNKS / 64], wrBits = new long[CHUNKS / 64];
    boolean checkDisjoint;
    long disjointViolations; String lastViolation; int phase;

    BigSRAM(int me, Config cfg) {
        this.me = me; this.cfg = cfg;
        memChunks = cfg.memChunks;
        checkDisjoint = cfg.checkDisjoint;
        if (memChunks < 1 << 16 || memChunks > CHUNKS || memChunks % (1 << 16) != 0) throw new IllegalArgumentException("memChunks: a multiple of 256 KB (65,536 chunks) up to 8 MB (4.1)");
        depth = cfg.areaQueueDepth;
        qWrite = new boolean[AREAS][depth]; qRow = new int[AREAS][depth]; qMask = new int[AREAS][depth];
        qToken = new int[AREAS][depth]; qPart = new int[AREAS][depth]; qData = new int[AREAS][depth][4];
        for (int a = 0; a < AREAS; a++) ret[a] = new WordDelayLine("t" + me + ".sram.ret" + a, cfg.sramReadLatency);
    }

    /** The request words (above). */
    static long readReq(int addr, int n, int tok, int part) { return (addr & 0x1FFFFF) | (long) n << 21 | (long) tok << 24 | (long) part << 40; }
    static long writeReq(int addr, int n) { return (addr & 0x1FFFFF) | (long) n << 21; }
    static int area(int chunk) { return (chunk >>> 17) & 15; }
    static int row(int chunk) { return (chunk >>> 2) & (ROWS - 1); }

    @Override public void runCycle(long t) {
        // 1. every area serves the head of its queue (as of the start of the cycle)
        if (cfg.fifoCountBeforePop) System.arraycopy(qCount, 0, qStart, 0, AREAS);   // 13a: room is judged by the count before the pop
        for (int a = 0; a < AREAS; a++) if (qCount[a] > 0) serve(a, t);
        // 2. arrivals: the SRAM write queue first, then the SRAM read queue (tie order, lead's ruling of 2 October: writes cannot
        //    wait, nothing behind them has flow control; reads can, the address generator just waits)
        if (writeReq.ready(t)) {
            long w = writeReq.peekA(t);
            int addr = (int) (w & 0x1FFFFF), a = area(addr);
            int n = (int) ((w >>> 21) & 7), mask = chunkMask(addr, n);
            boolean merge = cfg.writeCombining && canCombine(a, row(addr), mask);
            boolean room = qCount[a] < depth;
            if (cfg.fifoCountBeforePop) { boolean r0 = qStart[a] < depth; if (room && !r0 && !merge) popRoomWrites++; room = r0; }
            if (merge || room) {
                if (cfg.fifoCountBeforePop && !merge) qStart[a]++;          // the write goes in first (13a): a read to the same area sees it
                long lo = writeReq.peekB(t), hi = writeReq.peekC(t);
                writeReq.take(t);
                if (merge) combine(a, mask, addr & 3, lo, hi); else enqueue(a, true, row(addr), mask, 0, 0, lo, hi);
                if (s1Acc != null) for (int k = 0; k < n; k++) { Long acc = s1Acc.get(addr + k); if (acc != null) s1Ordered(t, t - acc, "big SRAM chunk 0x" + Integer.toHexString(addr + k)); }
            } else writeStallCycles++;
        }
        if (readReq.ready(t)) {
            long r = readReq.peekA(t);
            int addr = (int) (r & 0x1FFFFF), a = area(addr);
            boolean room = qCount[a] < depth;
            if (cfg.fifoCountBeforePop) { boolean r0 = qStart[a] < depth; if (room && !r0) popRoomReads++; room = r0; }
            if (room) {
                readReq.take(t);
                int n = (int) ((r >>> 21) & 7);
                enqueue(a, false, row(addr), chunkMask(addr, n), (int) ((r >>> 24) & 0xFFFF), (int) ((r >>> 40) & 1), 0, 0);
            } else readStallCycles++;
        }
    }

    static int chunkMask(int addr, int n) { int o = addr & 3; return ((1 << n) - 1) << o & 0xF; }

    /** Write data for chunk j of the row, from a write whose first chunk is at offset o: the payload's chunk j - o. */
    private static int chunkOf(long lo, long hi, int k) { return (int) ((k < 2 ? lo : hi) >>> (32 * (k & 1))); }

    private void enqueue(int a, boolean w, int row, int mask, int token, int part, long lo, long hi) {
        int i = (qHead[a] + qCount[a]) % depth;
        qWrite[a][i] = w; qRow[a][i] = row; qMask[a][i] = mask; qToken[a][i] = token; qPart[a][i] = part;
        if (w) { int o = Integer.numberOfTrailingZeros(mask); for (int j = 0; j < 4; j++) qData[a][i][j] = ((mask >>> j) & 1) != 0 ? chunkOf(lo, hi, j - o) : 0; qWrites[a]++; }
        qCount[a]++;
        if (qCount[a] > queuePeak[a]) queuePeak[a] = qCount[a];
    }
    private boolean canCombine(int a, int row, int mask) {
        if (qCount[a] < 1) return false;                     // queued entries are served from the next cycle on
        int i = (qHead[a] + qCount[a] - 1) % depth;
        return qWrite[a][i] && qRow[a][i] == row && (qMask[a][i] & mask) == 0;
    }
    private void combine(int a, int mask, int o, long lo, long hi) {
        int i = (qHead[a] + qCount[a] - 1) % depth;
        for (int j = 0; j < 4; j++) if (((mask >>> j) & 1) != 0) qData[a][i][j] = chunkOf(lo, hi, j - o);
        qMask[a][i] |= mask; combined[a]++;
    }

    private void serve(int a, long t) {
        int i = qHead[a];
        int row = qRow[a][i], mask = qMask[a][i];
        int base = a * AREA_CHUNKS + row * 4;               // the chunk address of the row's chunk 0
        accesses++;
        if (qWrite[a][i]) {
            writes[a]++; qWrites[a]--;
            for (int j = 0; j < 4; j++) if (((mask >>> j) & 1) != 0) {
                mark(wrBits, rdBits, base + j, "written", "read"); store(base + j, qData[a][i][j]);
                Long acc = s1Acc == null ? null : s1Acc.remove(base + j);
                if (acc != null && s1 != null) s1.sram(t - acc);                 // S1's report: acceptance -> stored
            }
        } else {
            reads[a]++;
            for (int j = 0; j < 4; j++) if (((mask >>> j) & 1) != 0) mark(rdBits, wrBits, base + j, "read", "written");
            long lo = (load(base) & 0xFFFFFFFFL) | ((long) load(base + 1) << 32), hi = (load(base + 2) & 0xFFFFFFFFL) | ((long) load(base + 3) << 32);
            ret[a].put(t, qToken[a][i] | (long) qPart[a][i] << 16, lo, hi);
        }
        qHead[a] = (i + 1) % depth; qCount[a]--;
    }

    private void mark(long[] mine, long[] other, int c, String did, String also) {
        if (!checkDisjoint) return;
        mine[c >>> 6] |= 1L << c;
        if ((other[c >>> 6] & (1L << c)) != 0) {
            disjointViolations++;
            if (lastViolation == null || disjointViolations == 1) lastViolation = "chunk 0x" + Integer.toHexString(c) + " " + did + " after being " + also + " in the same phase (invariant 2, phase " + phase + ")";
        }
    }
    /** A phase boundary for the disjoint-buffer check (the test, or GO from M4). */
    void newPhase() { java.util.Arrays.fill(rdBits, 0); java.util.Arrays.fill(wrBits, 0); phase++; lastViolation = null; }

    private int load(int c) { if (c >= memChunks) return 0; int[] d = data[c >>> 17]; return d == null ? 0 : d[c & (AREA_CHUNKS - 1)]; }
    private void store(int c, int v) { if (c >= memChunks) return; int a = c >>> 17; if (data[a] == null) data[a] = new int[AREA_CHUNKS]; data[a][c & (AREA_CHUNKS - 1)] = v; }

    // ---- host access (no timing; between cycles) ----
    int peek(int c) { return load(c & 0x1FFFFF); }
    long peek64(int c) { return (peek(c) & 0xFFFFFFFFL) | ((long) peek(c + 1) << 32); }
    void poke(int c, int v) { store(c & 0x1FFFFF, v); }
    void poke64(int c, long v) { poke(c, (int) v); poke(c + 1, (int) (v >>> 32)); }

    /** No write pending in any area's queue. */
    boolean noWritesQueued() { for (int a = 0; a < AREAS; a++) if (qWrites[a] > 0) return false; return true; }
    boolean idle() { for (int a = 0; a < AREAS; a++) if (qCount[a] > 0) return false; return true; }
    int queued(int a) { return qCount[a]; }
    // S1 (simulator only): the acceptance of each counted write awaiting its store; the measurement; K
    java.util.Map<Integer, Long> s1Acc; S1 s1; int s1K;
    private int lastS1Chunk = -1;
    /** S1's check (invariant 24): a write is ordered ahead of any later read of its area once it is in the area's queue
     *  (the queue serves in order), so the hazard is a write entering its queue later than K + the controller's reaction. */
    void s1Ordered(long t, long lat, String what) {
        if (s1 == null) return;
        s1.sramOrder(lat);
        if (lat > s1K + S1.REACTION) { s1.late++; s1.lastLate = "tile " + me + ": " + what + " in its area's queue " + lat + " cycles after acceptance, past K + 4 = " + (s1K + S1.REACTION) + " (invariant 24)"; }
    }
    void resetQueuePeaks() { for (int a = 0; a < AREAS; a++) queuePeak[a] = qCount[a]; }
    void resetStats() {
        java.util.Arrays.fill(reads, 0); java.util.Arrays.fill(writes, 0); java.util.Arrays.fill(combined, 0);
        for (int a = 0; a < AREAS; a++) queuePeak[a] = qCount[a];
        readStallCycles = writeStallCycles = accesses = 0;
    }
    /** The reset (6.1): the area queues empty (a queued write is lost; the memory keeps what it holds). */
    void reset() { java.util.Arrays.fill(qHead, 0); java.util.Arrays.fill(qCount, 0); java.util.Arrays.fill(qWrites, 0); }
}
