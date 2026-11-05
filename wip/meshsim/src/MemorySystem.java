/**
 * The memory system as the Tracer sees it (spec 4.21.5, 7; DECISIONS-v1.3 sections 12, 20, 21; the owner's model for B1,
 * 2026-11-05): a black box behind the fill bus, which reads and writes one 128 B line of a file named by (root, depth,
 * line). Its storage is {@link FileStore}'s trees; this class is the timing.
 *
 * <h3>The model (owner, 2026-11-05; SPEC-QUESTIONS Q12, Q13)</h3>
 * <ul>
 * <li><b>No TLB anywhere</b> (section 21 (b)): translation is the walk through the file's tree, each index read an access of
 *     the index line holding the entry, then the data line: a depth-2 file costs three accesses.</li>
 * <li><b>The shared cache:</b> 512 KB of 128 B lines, 8 ways × 512 sets (the set is the LPDDR line address's low 9 bits),
 *     LRU, write-back, addressed after translation, holding index and data lines alike. A hit takes {@link #hit} = 6
 *     cycles, a miss {@link #miss} = 80 from LPDDR. One request outstanding. Cold at each segment's start ({@link #cold}).</li>
 * <li><b>The next victim in every set is kept free or clean:</b> when a full set's LRU line is dirty, it is evicted at once,
 *     in the background, into the write buffer, its way left free; a clean LRU line stays and is dropped at no cost when a
 *     fill needs its way, so a fill never waits for a write-back.</li>
 * <li><b>The write buffer:</b> {@link #wbDepth} = 4 lines, draining to LPDDR at one line per {@link #wbDrain} = 80 cycles.
 *     <b>A read that misses waits until the buffer is empty</b> (the buffer is never searched).</li>
 * <li><b>A Tracer write-back</b> walks like a read; its data line, written whole, hits in place or <b>allocates without an
 *     LPDDR read</b> into a free or clean way, dirty (Q13), at {@link #hit}.</li>
 * </ul>
 * Model simplifications, stated in the report: only the Tracer's traffic is modelled, so hit rates are optimistic; an early
 * eviction that finds the buffer full frees its way at once although its line enters the buffer only when a slot drains
 * (its LPDDR write is timed correctly; only a write-allocate into that very set in that window could differ); a request's
 * accesses are timed serially at issue, which is exact with one request outstanding.
 *
 * <p>The data is functional: reads return FileStore's lines and writes update them at once (nothing else writes the
 * Tracer's files during a segment, section 21), so the cache keeps tags and state only.
 */
final class MemorySystem {
    int hit = 6, miss = 80, wbDepth = 4, wbDrain = 80;
    static final int SETS = 512, WAYS = 8;

    final FileStore store;
    private final int[] tag = new int[SETS * WAYS];
    private final boolean[] dirty = new boolean[SETS * WAYS];
    private final long[] stamp = new long[SETS * WAYS];
    private long clock;
    private final long[] wbDone = new long[64];          // the write buffer's lines' drain completions, a ring (FIFO)
    private int wbHead, wbCount;
    private long busyUntil = Long.MIN_VALUE;             // one request outstanding: the bus is free from this cycle
    private final FileStore.Walk walk = new FileStore.Walk();

    // ---- statistics (per segment: cleared by cold())
    long reads, writes, beyond;
    long indexAccesses, indexHits, dataAccesses, dataHits, lpddrReads;
    long walkCycles, dataCycles;                         // cycles spent on index lines and on data lines
    long earlyEvictions, readWaits, readWaitCycles, writeAllocates;

    MemorySystem(FileStore store) { this.store = store; cold(); }

    /** Cold cache, empty write buffer, idle bus, statistics cleared: a segment's start. */
    void cold() {
        java.util.Arrays.fill(tag, -1); java.util.Arrays.fill(dirty, false); java.util.Arrays.fill(stamp, 0);
        clock = 0; wbHead = 0; wbCount = 0; busyUntil = Long.MIN_VALUE;
        reads = writes = beyond = 0;
        indexAccesses = indexHits = dataAccesses = dataHits = lpddrReads = 0;
        walkCycles = dataCycles = 0;
        earlyEvictions = readWaits = readWaitCycles = writeAllocates = 0;
    }

    /** The bus is free at t (one request outstanding). */
    boolean idle(long t) { return t >= busyUntil; }

    /** The result of a read: its line, status and the cycle its data arrives. */
    static final class Response { final long[] data = new long[16]; int status; long readyAt; }

    /**
     * A fill-bus read issued at t (the bus idle): walks (root, depth, line), times every access, and returns the line and
     * its status (4.21.5): 0 ok, 1 hole (zeros), 2 not owned, 3 beyond the depth's capacity (no access).
     */
    Response read(long t, int root, int depth, long line, Response r) {
        if (!idle(t)) throw new IllegalStateException("a fill-bus read at " + t + " with a request outstanding until " + busyUntil);
        reads++;
        store.walk(root, depth, line, walk);
        long now = t;
        for (int k = 0; k < walk.nLines; k++) {
            boolean idx = k < walk.indexLines;
            long s = now;
            now = access(now, walk.lines[k], false, idx);
            if (idx) walkCycles += now - s; else dataCycles += now - s;
        }
        if (walk.status == FileStore.ST_BEYOND) { beyond++; now = t + 1; }
        r.status = walk.status;
        if (walk.dataLine >= 0) store.readLine(walk.dataLine, r.data); else java.util.Arrays.fill(r.data, 0);
        r.readyAt = now;
        busyUntil = now;
        return r;
    }

    /** A fill-bus write issued at t (the bus idle) of a line the Tracer may write (status ok); returns its acknowledgement's cycle. */
    long write(long t, int root, int depth, long line, long[] data) {
        if (!idle(t)) throw new IllegalStateException("a fill-bus write at " + t + " with a request outstanding until " + busyUntil);
        writes++;
        store.walk(root, depth, line, walk);
        if (walk.status != FileStore.ST_OK)
            throw new IllegalStateException("a write-back of a line that isn't writable (status " + walk.status + "): only writable lines are dirty (4.21.5)");
        long now = t;
        for (int k = 0; k < walk.nLines; k++) {
            boolean idx = k < walk.indexLines;
            long s = now;
            now = access(now, walk.lines[k], !idx, idx);
            if (idx) walkCycles += now - s; else dataCycles += now - s;
        }
        store.writeLine(walk.dataLine, data);
        busyUntil = now;
        return now;
    }

    // ---------------------------------------------------------------- the shared cache
    /** One access of LPDDR line a at t; returns the cycle it completes. write: a whole-line write (Q13). */
    private long access(long t, int a, boolean write, boolean index) {
        int set = a & (SETS - 1), base = set * WAYS, way = -1;
        if (index) indexAccesses++; else dataAccesses++;
        for (int w = 0; w < WAYS; w++) if (tag[base + w] == a) { way = w; break; }
        long done;
        if (way >= 0) {
            if (index) indexHits++; else dataHits++;
            done = t + hit;
        } else {
            way = victim(base);
            if (write) { done = t + hit; writeAllocates++; }               // allocated without an LPDDR read (Q13)
            else {
                long e = wbEmptyAt(t);
                if (e > t) { readWaits++; readWaitCycles += e - t; t = e; }   // a read miss waits for an empty buffer (Q12)
                done = t + miss; lpddrReads++;
            }
            tag[base + way] = a; dirty[base + way] = false;
        }
        stamp[base + way] = ++clock;
        if (write) dirty[base + way] = true;
        keepVictimClean(base, done);
        return done;
    }

    /** A free way, else the LRU way (clean, by the policy). */
    private int victim(int base) {
        int v = -1;
        for (int w = 0; w < WAYS; w++) {
            if (tag[base + w] < 0) return w;
            if (v < 0 || stamp[base + w] < stamp[base + v]) v = w;
        }
        if (dirty[base + v]) throw new IllegalStateException("a dirty victim: the policy keeps the next victim free or clean");
        return v;
    }

    /** The policy (Q12): a full set whose LRU line is dirty evicts it at once, at t, into the write buffer. */
    private void keepVictimClean(int base, long t) {
        int v = -1;
        for (int w = 0; w < WAYS; w++) {
            if (tag[base + w] < 0) return;                                 // a free way is the next victim
            if (v < 0 || stamp[base + w] < stamp[base + v]) v = w;
        }
        if (!dirty[base + v]) return;
        earlyEvictions++;
        tag[base + v] = -1; dirty[base + v] = false;
        wbPush(t);
    }

    // ---------------------------------------------------------------- the write buffer: a FIFO draining one line per wbDrain
    private void wbRetire(long t) { while (wbCount > 0 && wbDone[wbHead] <= t) { wbHead = (wbHead + 1) % wbDone.length; wbCount--; } }
    private void wbPush(long t) {
        wbRetire(t);
        long last = wbCount == 0 ? Long.MIN_VALUE : wbDone[(wbHead + wbCount - 1) % wbDone.length];
        long enter = t;
        if (wbCount >= wbDepth) enter = Math.max(t, wbDone[(wbHead + wbCount - wbDepth) % wbDone.length]);
        long done = Math.max(enter, last) + wbDrain;
        if (wbCount == wbDone.length) throw new IllegalStateException("write-buffer ring overflow");
        wbDone[(wbHead + wbCount) % wbDone.length] = done; wbCount++;
    }
    private long wbEmptyAt(long t) {
        wbRetire(t);
        return wbCount == 0 ? t : wbDone[(wbHead + wbCount - 1) % wbDone.length];
    }

    /** A one-line summary of the segment's statistics. */
    String summary() {
        return String.format("reads %d, writes %d; index %d (%.2f%% hit), data %d (%.2f%% hit), LPDDR reads %d; walk %d cycles, data %d; " +
                        "early evictions %d, write-allocates %d, read waits %d (%d cycles)",
                reads, writes, indexAccesses, pct(indexHits, indexAccesses), dataAccesses, pct(dataHits, dataAccesses), lpddrReads,
                walkCycles, dataCycles, earlyEvictions, writeAllocates, readWaits, readWaitCycles);
    }
    private static double pct(long a, long b) { return b == 0 ? 0 : 100.0 * a / b; }
}
