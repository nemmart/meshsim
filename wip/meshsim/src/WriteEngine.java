import java.util.ArrayList;
import java.util.List;

/**
 * The DMA write engine (spec 4.9, v1.2): the big SRAM's only writer. It takes mode 0 to region 0x03 and modes 1-3 from
 * the traffic director through its input FIFO (4) and queues each as one write at its area. The packet's mode says where
 * every payload goes:
 *   mode 0: the packet's address, byte offset / 4 as the chunk address; no PTR access, no count-on-arrival;
 *   modes 1 and 2: the payload at its size (1, 2 or 4 chunks) at PTR[i], then PTR[i] += those chunks, i = src (mode 1)
 *           or the payload's bucket digit (mode 2);
 *   mode 3: the 64-bit record (the packet's low 32 bits, the ordinal (target - DMA_RECORD_BASE_ADDRESS) / 2 in [63:32])
 *           at PTR[src], then PTR[src] += 2, from a 32- or 64-bit packet.
 * PTR lives in the table store: every PTR write sends a pointer RMW (old value returned, addend 1, 2 or 4) across
 * ts_engine_latency each way, and the packet waits in the pointer stage (2 * ts_engine_latency + 2 entries) until its old
 * pointer returns, in order; a mode-0 entry needs none but keeps its place in the stage. Count-on-arrival sends a counter
 * increment for the histogram digit (payload bits [31:0]) in the same cycle, for modes 1-3 only.
 *
 * Alignment (4.1, 4.9, D32): every access is naturally aligned, so no write spans a row (v1.1's spanning write is gone).
 * A write through a PTR not aligned to the write's size (a record for mode 3) is dropped, though still counted (its
 * decrement was scheduled at the director), the pointer still advances, and ERRORS [1] is set ({@link #misalignedWrites},
 * the error's source); meshsim flags it. A write at or past the end of big SRAM is undefined in hardware (4.1): meshsim
 * drops it and flags it (invariant 19).
 *
 * The write goes to the SRAM's write holding register with per-chunk enables (4.1). If the SRAM leaves the previous
 * request in the holding register (its area's queue is full) the put is refused and retried, the stage stops advancing,
 * the engine stops taking packets, and the input FIFO fills (its overflow is an error, 4.9).
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
    private long[] stAcc;                                 // S1: each entry's acceptance at the director
    java.util.ArrayDeque<Long> acceptQ;                   // S1: fed by the director, in the input bus's order
    java.util.Map<Integer, Long> sramAcc;                 // S1: chunk -> acceptance, until the area stores it
    java.util.ArrayDeque<Long>[] histAcc, ptrAcc;         // S1: per table index, acceptances awaiting their write-back
    private int head, count;
    private final java.util.ArrayDeque<Integer> awaiting = new java.util.ArrayDeque<>();   // stage entries waiting for their PTR return, in order
    long lastTsReq = Long.MIN_VALUE / 2;
    // ---- stats ----
    long taken, written, stallCycles, augmented, explicit;
    long misalignedWrites, outOfRange;                // monotonic: ERRORS [1]'s source (4.9); out-of-range writes (invariant 19)
    long violations; String lastViolation;
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
        else if (count > 0 && stHave[head]) {
            long lo = stD[head], h = stH[head], hi = stC[head]; int a = stPtr[head];
            int n, mode = LinkWord.mode(h);
            if (mode == LinkWord.M_AUG) { lo = (lo & 0xFFFFFFFFL) | (long) (((a - augmentBase) & 0x1FFFFF) >>> 1) << 32; hi = 0; n = 2; augmented++; }
            else n = LinkWord.chunks(LinkWord.size(h));
            if ((a & (n - 1)) != 0) {                               // a misaligned PTR (mode 0 can't be: the director dropped it)
                misalignedWrites++; violations++;
                lastViolation = "a write through PTR[" + (mode == LinkWord.M_BUCKET ? "digit" : "src " + LinkWord.src(h)) + "] = chunk 0x" + Integer.toHexString(a)
                        + ", not aligned to its " + (32 * n) + "-bit write: dropped, still counted, ERRORS [1] (4.9; invariant 20): " + LinkWord.describe(stD[head], h, stC[head]);
            } else if (a + n > cfg.memChunks) {
                outOfRange++; violations++;
                lastViolation = "a big-SRAM write at chunk 0x" + Integer.toHexString(a) + ", at or past the end of big SRAM: undefined (4.1; invariant 19): " + LinkWord.describe(stD[head], h, stC[head]);
            } else {
                writtenBySize[Integer.numberOfTrailingZeros(n)]++;
                toSram.put(t, BigSRAM.writeReq(a, n), lo, hi);
                if (sramAcc != null && stAcc != null && stAcc[head] >= 0) for (int k = 0; k < n; k++) sramAcc.put(a + k, stAcc[head]);
                written++;
            }
            stHave[head] = false; head = (head + 1) % cap; count--;
        }
        // the input side: one packet a cycle while the pointer stage has room
        if (count < cap && in.ready(t)) {
            long d = in.peekA(t), h = in.peekB(t), c = in.peekC(t);
            in.take(t);
            taken++;
            long acc = acceptQ != null && !acceptQ.isEmpty() ? acceptQ.poll() : -1;
            if (stAcc == null) stAcc = new long[cap];
            if (log != null) log.add(new long[] { t, d, h, c });
            int mode = LinkWord.mode(h);
            int i = (head + count) % cap;
            stD[i] = d; stH[i] = h; stC[i] = c; count++; stAcc[i] = acc;
            if (mode == LinkWord.M_WRITE) { stPtr[i] = LinkWord.offset(LinkWord.addr(h)) >>> 2; stHave[i] = true; explicit++; }   // 4.9: at its address, no PTR access
            else {
                int idx = mode == LinkWord.M_BUCKET ? LinkWord.digit(d, bucketDigit) : LinkWord.src(h);   // modes 1 and 3: src
                int step = mode == LinkWord.M_AUG ? 2 : LinkWord.chunks(LinkWord.size(h));                  // 4.9: += 1, 2 or 4; mode 3: += 2
                tsReq.put(t, TableStore.OP_ADD_RET, TableStore.PTR + idx, step);
                if (ptrAcc != null && acc >= 0) ptrAcc[idx].add(acc);
                lastTsReq = t;
                stHave[i] = false; awaiting.add(i);
                if (histDigit != 0) { histReq.put(t, TableStore.OP_ADD_RET, LinkWord.digit(d, histDigit), 1); if (histAcc != null && acc >= 0) histAcc[LinkWord.digit(d, histDigit)].add(acc); }   // HIST: modes 1-3 (4.9)
            }
            if (count > stageHighWater) stageHighWater = count;
        }
    }

    /** Nothing in the engine: the pointer stage and the holding register are empty. */
    boolean idle(long t) { return count == 0 && toSram.isEmpty(t); }
    void record() { log = new ArrayList<>(); }
    void resetStats() { taken = written = stallCycles = 0; java.util.Arrays.fill(writtenBySize, 0); stageHighWater = count; }
    /** The reset (6.1): the pointer stage empties. */
    void reset() { head = 0; count = 0; java.util.Arrays.fill(stHave, false); awaiting.clear(); }
}
