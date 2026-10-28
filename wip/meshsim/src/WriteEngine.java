import java.util.ArrayList;
import java.util.List;

/**
 * The DMA write engine (spec 4.9, v1.1 as renumbered): the big SRAM's only writer. It takes modes 2-5 from the traffic
 * director through its input FIFO (4) and queues each as one write at its area: modes 2 and 4 write the payload at its
 * size (32, 64 or 128 bits: 1, 2 or 4 chunks) at PTR[i], then PTR[i] += those chunks, with i = src (mode 2) or the
 * payload's bucket digit (mode 4); mode 5 (augmenting) writes the 64-bit record (the packet's low 32 bits, the ordinal
 * (target - DMA_RECORD_BASE_ADDRESS) / 2 in [63:32]) at PTR[src], then PTR[src] += 2, from a 32- or 64-bit packet;
 * mode 3 writes the payload at its explicit, size-aligned addr, with no PTR access (it never spans). PTR lives in the
 * table store: every PTR write sends a pointer RMW (old value returned, addend 1, 2 or 4) across ts_engine_latency each
 * way, and the packet waits in the pointer stage (2 * ts_engine_latency + 2 entries) until its old pointer returns, in
 * order; a mode-3 entry needs none but keeps its place in the stage. Count-on-arrival sends a counter increment for the
 * histogram digit (payload bits [31:0]) in the same cycle, for modes 2, 4 and 5 only (k.1.7).
 *
 * The write goes to the SRAM's write holding register with per-chunk enables (4.1): a 32-bit write is one chunk, a
 * 128-bit write at chunk 0 one full-row access. A write that does not fit in its row (o + n > 4 for a write of n chunks
 * at chunk o) spans two rows: part 0 (4 - o chunks) goes first, part 1 (the rest, at the next row) on the next cycle
 * ahead of any other write, and the engine's next write waits meanwhile. If the SRAM leaves the previous request in the
 * holding register (its area's queue is full) the put is refused and retried, the stage stops advancing, the engine
 * stops taking packets, and the input FIFO fills (its overflow is an error, spec 4.9).
 */
final class WriteEngine implements Component {
    final int me;
    final Config cfg;
    WordSource in;                    // the input FIFO
    WordSink tsReq; WordSource tsRet; // table store (pointer RMWs)
    WordSink histReq;                  // HIST store
    WordHold toSram;
    // ---- configuration (DMA_CONFIG, DMA_RECORD_BASE_ADDRESS; static during a run) ----
    int bucketDigit, histDigit, augmentBase;
    // ---- pointer stage ----
    private final int cap;
    private final long[] stD, stH, stC; private final int[] stPtr; private final boolean[] stHave;
    private int head, count;
    private final java.util.ArrayDeque<Integer> awaiting = new java.util.ArrayDeque<>();   // stage entries waiting for their PTR return, in order
    private boolean splitPending; private int splitAddr, splitN; private long splitLo, splitHi;
    long lastTsReq = Long.MIN_VALUE / 2;
    // ---- stats ----
    long taken, written, stallCycles, splits, augmented, explicit;
    final long[] writtenBySize = new long[3];   // writes of 32, 64, 128 bits (mode 4's records count as 64)
    int stageHighWater;
    List<long[]> log;

    WriteEngine(int me, Config cfg) {
        this.me = me; this.cfg = cfg;
        cap = 2 * cfg.tsEngineLatency + 2;
        stD = new long[cap]; stH = new long[cap]; stC = new long[cap]; stPtr = new int[cap]; stHave = new boolean[cap];
    }

    @Override public void runCycle(long t) {
        // pointer returns, in order
        if (tsRet.ready(t)) {
            int old = (int) tsRet.peekB(t); tsRet.take(t);
            Integer i = awaiting.poll();                             // null: a return for a request a test put on the bus directly
            if (i != null) { stPtr[i] = old & 0x1FFFFF; stHave[i] = true; }
        }
        // the write side
        if (toSram.refused(t)) { toSram.retry(t); stallCycles++; }
        else if (splitPending) { toSram.put(t, BigSRAM.writeReq(splitAddr, splitN), splitLo, splitHi); splitPending = false; }
        else if (count > 0 && stHave[head]) {
            long lo = stD[head], h = stH[head], hi = stC[head]; int a = stPtr[head];
            int n;
            if (LinkWord.mode(h) == LinkWord.M_AUG) { lo = (lo & 0xFFFFFFFFL) | (long) (((a - augmentBase) & 0x1FFFFF) >>> 1) << 32; hi = 0; n = 2; augmented++; }
            else n = LinkWord.chunks(LinkWord.size(h));
            writtenBySize[Integer.numberOfTrailingZeros(n)]++;
            int o = a & 3;
            if (o + n > 4) {                                        // spans two rows (4.1): part 0 now, part 1 next cycle
                int n0 = 4 - o;
                toSram.put(t, BigSRAM.writeReq(a, n0), lo, hi);
                splitPending = true; splitAddr = (a + n0) & 0x1FFFFF; splitN = n - n0;
                splitLo = shiftLo(lo, hi, n0); splitHi = shiftHi(hi, n0); splits++;
            } else toSram.put(t, BigSRAM.writeReq(a, n), lo, hi);
            written++;
            stHave[head] = false; head = (head + 1) % cap; count--;
        }
        // the input side: one packet a cycle while the pointer stage has room
        if (count < cap && in.ready(t)) {
            long d = in.peekA(t), h = in.peekB(t), c = in.peekC(t);
            in.take(t);
            taken++;
            if (log != null) log.add(new long[] { t, d, h, c });
            int mode = LinkWord.mode(h);
            int i = (head + count) % cap;
            stD[i] = d; stH[i] = h; stC[i] = c; count++;
            if (mode == LinkWord.M_EXPLICIT) { stPtr[i] = LinkWord.addr(h); stHave[i] = true; explicit++; }   // 4.9: at addr, no PTR access
            else {
                int idx = mode == LinkWord.M_BUCKET ? LinkWord.digit(d, bucketDigit) : LinkWord.src(h);   // modes 2 and 5: src
                int step = mode == LinkWord.M_AUG ? 2 : LinkWord.chunks(LinkWord.size(h));                  // 4.9: += 1, 2 or 4; mode 5: += 2
                tsReq.put(t, TableStore.OP_ADD_RET, TableStore.PTR + idx, step);
                lastTsReq = t;
                stHave[i] = false; awaiting.add(i);
                if (histDigit != 0) histReq.put(t, TableStore.OP_ADD_RET, LinkWord.digit(d, histDigit), 1);   // HIST: modes 2, 4, 5 (k.1.7)
            }
            if (count > stageHighWater) stageHighWater = count;
        }
    }

    /** The payload (lo = chunks 0, 1; hi = chunks 2, 3) shifted down by k chunks: part 1 of a spanning write. */
    static long shiftLo(long lo, long hi, int k) { return k == 0 ? lo : k == 1 ? lo >>> 32 | hi << 32 : k == 2 ? hi : hi >>> 32; }
    static long shiftHi(long hi, int k) { return k == 0 ? hi : k == 1 ? hi >>> 32 : 0; }

    /** Nothing in the engine: the pointer stage, a split half and the holding register are empty. */
    boolean idle(long t) { return count == 0 && !splitPending && toSram.isEmpty(t); }
    void record() { log = new ArrayList<>(); }
    void resetStats() { taken = written = stallCycles = splits = 0; java.util.Arrays.fill(writtenBySize, 0); stageHighWater = count; }
}
