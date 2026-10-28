import java.util.ArrayList;
import java.util.List;

/**
 * The DMA write engine (spec 4.9): the big SRAM's only writer. It takes modes 2, 3 and 4 from the traffic director
 * through its input FIFO (4) and writes each 64-bit payload at PTR[i], then PTR[i] += 2, with i = src (modes 2 and 4) or
 * the payload's bucket digit (mode 3). PTR lives in the table store: every write sends a pointer RMW (old value returned) across
 * ts_engine_latency each way, and the packet waits in the pointer stage (2 * ts_engine_latency + 2 entries) until its
 * old pointer returns, in order. Count-on-arrival sends a counter increment for the histogram digit in the same cycle.
 * Augmenting (mode 4; until then the DMA_CONFIG attach flag on mode 2): the
 * payload's high 32 bits become the ordinal, (target - DMA_RECORD_BASE_ADDRESS) / 2.
 *
 * The write goes to the SRAM's write holding register; if the SRAM leaves the previous one there (its area's queue is
 * full) the put is refused and retried, the stage stops advancing, the engine stops taking packets, and the input FIFO
 * fills (its overflow is an error, spec 4.9). A write at a chunk address = 3 mod 4 spans two rows: two 32-bit writes on
 * consecutive cycles.
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
    private final long[] stD, stH; private final int[] stPtr; private final boolean[] stHave;
    private int head, count, nextRet;     // nextRet: the stage entry the next pointer return belongs to (offset from head)
    private boolean splitPending; private int splitAddr; private long splitData;
    long lastTsReq = Long.MIN_VALUE / 2;
    // ---- stats ----
    long taken, written, stallCycles, splits, augmented;
    int stageHighWater;
    List<long[]> log;

    WriteEngine(int me, Config cfg) {
        this.me = me; this.cfg = cfg;
        cap = 2 * cfg.tsEngineLatency + 2;
        stD = new long[cap]; stH = new long[cap]; stPtr = new int[cap]; stHave = new boolean[cap];
    }

    @Override public void runCycle(long t) {
        // pointer returns, in order
        if (tsRet.ready(t)) {
            int old = (int) tsRet.peekB(t); tsRet.take(t);
            int i = (head + nextRet) % cap;
            stPtr[i] = old & 0xFFFFF; stHave[i] = true; nextRet++;
        }
        // the write side
        if (toSram.refused(t)) { toSram.retry(t); stallCycles++; }
        else if (splitPending) { toSram.put(t, splitAddr | 1L << 20, splitData, 0); splitPending = false; }
        else if (count > 0 && stHave[head]) {
            long d = stD[head], h = stH[head]; int a = stPtr[head];
            if (LinkWord.mode(h) == LinkWord.M_AUG) { d = (d & 0xFFFFFFFFL) | (long) (((a - augmentBase) & 0xFFFFF) >>> 1) << 32; augmented++; }
            if ((a & 3) == 3) {
                toSram.put(t, a | 1L << 20, d & 0xFFFFFFFFL, 0);
                splitPending = true; splitAddr = (a + 1) & 0xFFFFF; splitData = d >>> 32; splits++;
            } else toSram.put(t, a | 2L << 20, d, 0);
            written++;
            stHave[head] = false; head = (head + 1) % cap; count--; nextRet--;
        }
        // the input side: one packet a cycle while the pointer stage has room
        if (count < cap && in.ready(t)) {
            long d = in.peekA(t), h = in.peekB(t);
            in.take(t);
            taken++;
            if (log != null) log.add(new long[] { t, d, h });
            int mode = LinkWord.mode(h);
            int idx = mode == LinkWord.M_BUCKET ? LinkWord.digit(d, bucketDigit) : LinkWord.src(h);   // modes 2 and 4: src
            tsReq.put(t, TableStore.OP_ADD_RET, TableStore.PTR + idx, 2);
            lastTsReq = t;
            if (histDigit != 0) histReq.put(t, TableStore.OP_ADD_RET, LinkWord.digit(d, histDigit), 1);
            int i = (head + count) % cap;
            stD[i] = d; stH[i] = h; stHave[i] = false; count++;
            if (count > stageHighWater) stageHighWater = count;
        }
    }

    /** Nothing in the engine: the pointer stage, a split half and the holding register are empty. */
    boolean idle(long t) { return count == 0 && !splitPending && toSram.isEmpty(t); }
    void record() { log = new ArrayList<>(); }
    void resetStats() { taken = written = stallCycles = splits = 0; stageHighWater = count; }
}
