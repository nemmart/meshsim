/**
 * The read-modify-write pipeline of PTR and HIST (spec 4.10; figure 12): port A is the engine's read port only;
 * port B is a read/write port shared by the increment's write-back and the controller, with the increment first. An
 * RMW reads the entry on port A at t and writes the new value on port B at t + 1, with a bypass so updates to the same
 * entry at any spacing are not lost (the newest write-back in flight supersedes the macro, including one committing
 * this cycle). The pipeline is inside the memory: ts_engine_latency does not lengthen it (lead, 3 October; M7 wrote
 * back at t + ts_engine_latency, DEV 92).
 * A controller access on a cycle with a write-back loses port B: a controller write is lost, and a controller load
 * returns the value the engine writes, at the engine's address (figure 12). The window check still reports it.
 */
final class RmwPipe {
    static final int WB_DELAY = 1;   // figure 12: read on t, write back on t + 1
    private final SRAMMacro mem;
    private final int rdPort, wrPort, latency, ringN;
    private final int[] wbIdx, wbVal;
    private final long[] wbAt;
    private final boolean[] wbValid;
    private int wbCount, lastWbIdx, lastWbVal;
    private long lastWbT = -1, lastTake = Long.MIN_VALUE / 2;
    long ctrlWritesLost, ctrlLoadsEngine;   // controller accesses that met a write-back
    RmwPipe(SRAMMacro mem, int rdPort, int wrPort) {
        this.mem = mem; this.rdPort = rdPort; this.wrPort = wrPort; this.latency = WB_DELAY; ringN = latency + 2;
        wbIdx = new int[ringN]; wbVal = new int[ringN]; wbAt = new long[ringN]; wbValid = new boolean[ringN];
    }
    void writeBack(long t) {
        int slot = (int) (t % ringN);
        if (wbValid[slot]) {
            wbValid[slot] = false; wbCount--;
            mem.write32(wrPort, t, wbIdx[slot], wbVal[slot]);
            lastWbIdx = wbIdx[slot]; lastWbVal = wbVal[slot]; lastWbT = t;
        }
    }
    int current(long t, int idx) {
        int old = mem.read32(rdPort, t, idx);
        long best = Long.MIN_VALUE;
        if (lastWbT == t && lastWbIdx == idx) { best = t - latency; old = lastWbVal; }
        for (int i = 0; i < ringN; i++) if (wbValid[i] && wbIdx[i] == idx && wbAt[i] > best) { best = wbAt[i]; old = wbVal[i]; }
        return old;
    }
    void schedule(long t, int idx, int nv) {
        int s = (int) ((t + latency) % ringN);
        if (wbValid[s]) throw new IllegalStateException("RMW write-back ring collision");
        wbValid[s] = true; wbIdx[s] = idx; wbVal[s] = nv; wbAt[s] = t; wbCount++; lastTake = t;
    }
    /** Port B's write-back at t, whether or not the memory has run this cycle yet (it was scheduled at t - 1). */
    private int wbSlotAt(long t) {
        if (lastWbT == t) return -2;
        int s = (int) (t % ringN);
        return wbValid[s] && wbAt[s] + latency == t ? s : -1;
    }
    /** A controller store through port B (the window, one cycle): lost if the increment writes back this cycle. */
    void ctrlWrite(long t, int idx, int v) {
        if (wbSlotAt(t) != -1) { ctrlWritesLost++; return; }
        mem.write32(wrPort, t, idx, v);
    }
    /** A controller load through port B: the engine's written value (at its own address) if it writes back this cycle. */
    int ctrlRead(long t, int idx) {
        int s = wbSlotAt(t);
        if (s == -1) return mem.read32(wrPort, t, idx);
        ctrlLoadsEngine++;
        return s == -2 ? lastWbVal : wbVal[s];
    }
    boolean busy(long t) { return t <= lastTake + latency; }
    boolean empty() { return wbCount == 0; }
}

/**
 * The table memories PTR, STREAM and GROUP (spec 4.10, v1.1: separate macros, each with a controller side and an
 * engine side; HIST is {@link HistStore}). Nothing is shared, so the address generator's table reads and the write
 * engine's pointer updates never wait for each other (M3's Q6).
 *   STREAM_SRC, STREAM_DEST, STREAM_COUNT   three 256 x 32 macros, read in parallel at the same index as one STREAM
 *            read (one entry per cycle): controller writes (one word each); the address generator reads (port 0)
 *   GROUP  64 x 64 bits (128 words): controller writes; the address generator reads (port 0)
 *   PTR    256 x 20 bits: controller writes; the write engine's read-modify-write (ports 0 and 1)
 * The controller's window uses each macro's port CTRL (STREAM, GROUP) or PTR's port B (figure 12, {@link RmwPipe}).
 * The engines pay the distance: requests arrive over delay lines of ts_engine_latency into 16-entry request FIFOs.
 * STREAM and GROUP each have their own engine read path (request line, FIFO, return line), one read per cycle each
 * (spec 4.10: four separate memories; M7 served both through one FIFO at one read per cycle). The address
 * generator is their only engine reader. The write engine's FIFO feeds one PTR RMW per cycle, returning the old value.
 * Request words: a = op, b = index (AG: STREAM + i or GROUP + 2g; WE: PTR + i), c = addend.
 * Returns: STREAM a = STREAM_SRC[i] | STREAM_DEST[i] << 32, b = STREAM_COUNT[i], c = index; GROUP a = index,
 * b = the 64-bit entry; PTR a = index, b = the old pointer.
 */
final class TableStore implements Component {
    static final int OP_READ64 = 1, OP_ADD_RET = 3;
    /** Request index spaces (not addresses: each table is its own memory). */
    static final int PTR = 0x100, STREAM = 0x200, GROUP = 0x400;
    /** Host index spaces of the three STREAM arrays (peek, poke). */
    static final int STREAM_SRC = 0x1000, STREAM_DEST = 0x1100, STREAM_COUNT = 0x1200;
    static final int RD_PORT = 0, WR_PORT = 1, CTRL = 1;   // PTR, HIST: port A (engine read), port B (write-back, controller)
    final SRAMMacro streamSrc = new SRAMMacro("STREAM_SRC", 256, 2, false);
    final SRAMMacro streamDest = new SRAMMacro("STREAM_DEST", 256, 2, false);
    final SRAMMacro streamCount = new SRAMMacro("STREAM_COUNT", 256, 2, false);
    final SRAMMacro group = new SRAMMacro("GROUP", 128, 2, false);
    final SRAMMacro ptr = new SRAMMacro("PTR", 256, 2, false);
    final RmwPipe pipe;
    WordSource reqStream, reqGroup, reqWe;   // delay lines of ts_engine_latency
    WordSink retStream, retGroup, retWe;     // delay lines of ts_engine_latency
    final WordFIFO qStream, qGroup, qWe;     // the request FIFOs (16)
    long tableReads, rmws;
    boolean busy;                 // this cycle: a request arriving or queued, or an RMW not yet written back
    boolean ptrBusy;              // the same, for PTR alone (the window check)

    TableStore(int tile) {
        qStream = new WordFIFO("t" + tile + ".stream.q", 16); qGroup = new WordFIFO("t" + tile + ".group.q", 16); qWe = new WordFIFO("t" + tile + ".ptr.q", 16);
        pipe = new RmwPipe(ptr, RD_PORT, WR_PORT);
    }

    @Override public void runCycle(long t) {
        pipe.writeBack(t);
        boolean arrAg = false, arrWe = false;
        if (reqStream.ready(t)) { qStream.put(t, reqStream.peekA(t), reqStream.peekB(t), reqStream.peekC(t)); reqStream.take(t); arrAg = true; }
        if (reqGroup.ready(t)) { qGroup.put(t, reqGroup.peekA(t), reqGroup.peekB(t), reqGroup.peekC(t)); reqGroup.take(t); arrAg = true; }
        if (reqWe.ready(t)) { qWe.put(t, reqWe.peekA(t), reqWe.peekB(t), reqWe.peekC(t)); reqWe.take(t); arrWe = true; }
        boolean qdAg = qStream.sizeAtStart(t) > 0 || qGroup.sizeAtStart(t) > 0, qdWe = qWe.sizeAtStart(t) > 0;
        if (qStream.ready(t)) {
            int idx = (int) qStream.peekB(t);
            qStream.take(t);
            tableReads++;
            int i = (idx - STREAM) & 255;                       // the three arrays read in parallel at one index (4.10)
            int src = streamSrc.read32(RD_PORT, t, i), dest = streamDest.read32(RD_PORT, t, i), cnt = streamCount.read32(RD_PORT, t, i);
            retStream.put(t, Fields.streamE0(src, dest), cnt & 0xFFFFFFFFL, idx);
        }
        if (qGroup.ready(t)) {
            int idx = (int) qGroup.peekB(t);
            qGroup.take(t);
            tableReads++;
            retGroup.put(t, idx, group.read64(RD_PORT, t, idx - GROUP), 0);
        }
        if (qWe.ready(t)) {
            int idx = (int) qWe.peekB(t) - PTR, add = (int) qWe.peekC(t);
            qWe.take(t);
            rmws++;
            int old = pipe.current(t, idx);
            pipe.schedule(t, idx, (old + add) & 0x1FFFFF);
            retWe.put(t, idx, old & 0xFFFFFFFFL, 0);
        }
        ptrBusy = arrWe || qdWe || pipe.busy(t);
        busy = arrAg || qdAg || ptrBusy;
    }
    boolean drained() { return pipe.empty() && qStream.occupancyNow() == 0 && qGroup.occupancyNow() == 0 && qWe.occupancyNow() == 0; }

    // ---- host access by the old index spaces (tests) ----
    private SRAMMacro host(int idx) {
        return idx >= STREAM_COUNT ? streamCount : idx >= STREAM_DEST ? streamDest : idx >= STREAM_SRC ? streamSrc : idx >= GROUP ? group : ptr;
    }
    private static int hostBase(int idx) { return idx >= STREAM_COUNT ? STREAM_COUNT : idx >= STREAM_DEST ? STREAM_DEST : idx >= STREAM_SRC ? STREAM_SRC : idx >= GROUP ? GROUP : PTR; }
    int peek(int idx) { return host(idx).peek(idx - hostBase(idx)); }
    void poke(int idx, int v) { host(idx).poke(idx - hostBase(idx), v); }
    void resetStats() { tableReads = rmws = 0; }
}

/**
 * The HIST store (spec 4.10): HIST[256], 17 bits each, in its own 1R1W macro with its own 16-entry request FIFO,
 * pipeline and bypass, so a counter update and a pointer update can be taken in the same cycle. Fed by the write
 * engine's count-on-arrival. A count past 17 bits wraps and is flagged (invariant 6).
 */
final class HistStore implements Component {
    final SRAMMacro mem = new SRAMMacro("HIST", 256, 2, false);   // ports: A (RMW read), B (write-back and the controller, figure 12)
    final RmwPipe pipe;
    WordSource req;
    final WordFIFO q;
    long rmws;
    boolean histOverflow, busy;
    HistStore(int tile) { q = new WordFIFO("t" + tile + ".hist.q", 16); pipe = new RmwPipe(mem, TableStore.RD_PORT, TableStore.WR_PORT); }

    @Override public void runCycle(long t) {
        pipe.writeBack(t);
        boolean arriving = false;
        if (req.ready(t)) { q.put(t, req.peekA(t), req.peekB(t), req.peekC(t)); req.take(t); arriving = true; }
        boolean queued = q.sizeAtStart(t) > 0;
        if (q.ready(t)) {
            int idx = (int) q.peekB(t) & 0xFF, add = (int) q.peekC(t);
            q.take(t);
            rmws++;
            int nv = pipe.current(t, idx) + add;
            if ((nv & ~0x1FFFF) != 0) histOverflow = true;
            pipe.schedule(t, idx, nv & 0x1FFFF);
        }
        busy = arriving || queued || pipe.busy(t);
    }
    boolean drained() { return pipe.empty() && q.occupancyNow() == 0; }
    int peek(int i) { return mem.peek(i); }
    void poke(int i, int v) { mem.poke(i, v); }
}
