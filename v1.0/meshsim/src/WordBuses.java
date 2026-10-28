/**
 * Primitive specialisations of the bus classes (brief 2.2, "hot-path implementations may be
 * specialised to primitives"): each value is three longs (a, b, c). Used for link words
 * (a = payload, b = address bits, c unused), SRAM queues (a = address, b = data lo, c = data hi)
 * and register writes. They obey exactly the rules of the generic classes and are tested against
 * the same suite (test/BusTest.java).
 */
interface WordSink   { boolean canPut(long t); void put(long t, long a, long b, long c); }
interface WordSource { boolean ready(long t); long peekA(long t); long peekB(long t); long peekC(long t); void take(long t); }

abstract class WordBus extends BusBase implements WordSink, WordSource {
    WordBus(String name) { super(name); }
}

/** Registered value; take() is non-destructive. */
final class WordLatch extends WordBus {
    private long a, b, c, pa, pb, pc;
    private boolean valid;

    WordLatch(String name) { super(name); }

    @Override protected void commit(long tPut) { a = pa; b = pb; c = pc; valid = true; }
    @Override public boolean canPut(long t) { enter(t); return true; }
    /** Putting the value that is already registered (and nothing pending) is a no-op. */
    @Override public void put(long t, long a, long b, long c) {
        enter(t);
        if (valid && !pendPut && a == this.a && b == this.b && c == this.c) return;
        notePut(t); pa = a; pb = b; pc = c;
    }
    @Override public boolean ready(long t) { enter(t); return valid; }
    private void check(long t) { enter(t); if (!valid) throw new IllegalStateException(name + ": no value visible at " + t); }
    @Override public long peekA(long t) { check(t); return a; }
    @Override public long peekB(long t) { check(t); return b; }
    @Override public long peekC(long t) { check(t); return c; }
    @Override public void take(long t) { check(t); }
}

/** Fixed latency L >= 1; put at t visible from t+L; bounded capacity (consumer must take). */
final class WordDelayLine extends WordBus {
    final int latency;
    private final long[] va, vb, vc, at;
    private int head, count;
    private long pa, pb, pc, pendAt;

    WordDelayLine(String name, int latency) { this(name, latency, latency + 2); }
    WordDelayLine(String name, int latency, int capacity) {
        super(name);
        if (latency < 1) throw new IllegalArgumentException("latency >= 1");
        this.latency = latency;
        va = new long[capacity]; vb = new long[capacity]; vc = new long[capacity]; at = new long[capacity];
    }

    @Override protected void commit(long tPut) {
        if (count == va.length) throw new IllegalStateException(name + ": delay line full (consumer did not take)");
        int i = head + count; if (i >= va.length) i -= va.length;
        va[i] = pa; vb[i] = pb; vc[i] = pc; at[i] = pendAt; count++;
    }
    @Override public boolean canPut(long t) { enter(t); return true; }
    @Override public void put(long t, long a, long b, long c) { notePut(t); pa = a; pb = b; pc = c; pendAt = t + latency; }
    @Override public boolean ready(long t) { enter(t); return count > 0 && at[head] <= t; }
    private void check(long t) { if (!ready(t)) throw new IllegalStateException(name + ": nothing visible at " + t); }
    @Override public long peekA(long t) { check(t); return va[head]; }
    @Override public long peekB(long t) { check(t); return vb[head]; }
    @Override public long peekC(long t) { check(t); return vc[head]; }
    @Override public void take(long t) {
        check(t);
        noteTake(t);
        head = head + 1 == va.length ? 0 : head + 1;
        count--;
    }
    int inFlight(long t) { enter(t); return count + (pendPut ? 1 : 0); }
}

/**
 * Overflow FIFO of words: capacity N, put at t visible from t+1, never backpressures; a put that
 * does not fit at the settle is dropped and sets the sticky overflow flag (identity = name,
 * cycle recorded). High-water mark kept for the reports.
 */
final class WordFIFO extends WordBus {
    final int capacity;
    private final long[] va, vb, vc;
    private int head, count;
    private long pa, pb, pc;
    int highWater;
    boolean overflow;
    long overflowCycle = -1;
    long overflowCount;

    WordFIFO(String name, int capacity) {
        super(name);
        this.capacity = capacity;
        va = new long[capacity]; vb = new long[capacity]; vc = new long[capacity];
    }

    /** Pushes that found the FIFO full at the start of the cycle but fitted after its pop (V1.0-CHANGES item 5 overflows them). */
    static long pushAtFullWithPop;
    static boolean countBeforePop = !Boolean.getBoolean("fifo.afterPop");   // V1.0 item 5; -Dfifo.afterPop=true: M15's rule (comparison only)
    @Override protected void commit(long tPut) {
        boolean fullAtStart = count + takes >= capacity;           // takes: this FIFO's pops in cycle tPut
        if (fullAtStart && count < capacity) pushAtFullWithPop++;
        if (count == capacity || countBeforePop && fullAtStart) {
            overflowCount++;
            if (!overflow) { overflow = true; overflowCycle = tPut; }
            return;
        }
        int i = head + count; if (i >= capacity) i -= capacity;
        va[i] = pa; vb[i] = pb; vc[i] = pc; count++;
        if (count > highWater) highWater = count;
    }
    @Override public boolean canPut(long t) { enter(t); return true; }
    @Override public void put(long t, long a, long b, long c) { notePut(t); pa = a; pb = b; pc = c; }
    @Override public boolean ready(long t) { enter(t); return count > 0; }
    private void check(long t) { enter(t); if (count == 0) throw new IllegalStateException(name + ": empty at " + t); }
    @Override public long peekA(long t) { check(t); return va[head]; }
    @Override public long peekB(long t) { check(t); return vb[head]; }
    @Override public long peekC(long t) { check(t); return vc[head]; }
    @Override public void take(long t) {
        check(t);
        noteTake(t);
        head = head + 1 == capacity ? 0 : head + 1;
        count--;
    }
    int size(long t) { enter(t); return count; }
    /** Payload word a of the k-th visible entry (0 = head), for a unit that shows more than its head (command FIFO, v3.4). */
    long peekAAt(long t, int k) { enter(t); if (k >= count) throw new IllegalStateException(name + ": no entry " + k + " at " + t); int i = head + k; if (i >= capacity) i -= capacity; return va[i]; }
    /** Occupancy at the start of cycle t (independent of takes made so far this cycle). */
    int sizeAtStart(long t) { enter(t); return count + takes; }
    /** Host-side (between cycles): visible plus pending entries, without touching the clock. */
    int occupancyNow() { return count + (pendPut ? 1 : 0); }
    /** True only if nothing is visible and nothing is pending (for quiescence checks). */
    boolean isEmpty(long t) { enter(t); return count == 0 && !pendPut; }
    void resetStats(long t) { enter(t); highWater = count; }
    void clearOverflow() { overflow = false; overflowCycle = -1; overflowCount = 0; }
    /** Empty the FIFO at the end of a cycle, pending put included (a unit held in reset, 13a). */
    void clear() { count = 0; head = 0; pendPut = false; }
    /** Restart the high-water mark from the current occupancy (per-phase peaks; between cycles). */
    void resetPeak() { highWater = occupancyNow(); }
}

/**
 * Holding register (brief 2.2, last paragraph): the places where the spec has one side wait
 * (injection-mux priority, SRAM read/write priority, the core's send register) are not
 * backpressure: the requester's word sits in its holding register until the owning unit takes
 * it. Semantics: put at t visible from t+1; take at t empties it; a put made at t while the
 * register is still occupied at the end-of-cycle settle (i.e. nobody took the old word at t)
 * is refused, and the producer learns that at t+1 through {@link #refused(long)} and presents
 * the word again. canPut(t) is start-of-cycle emptiness (order independent) and doubles as the
 * "not busy" status bit. Neither refusal nor busy is an error.
 */
final class WordHold extends WordBus {
    private long a, b, c, pa, pb, pc;
    private boolean occ;
    private long refusedAt = Long.MIN_VALUE;
    long refusals;

    WordHold(String name) { super(name); }

    @Override protected void commit(long tPut) {
        if (occ) { refusedAt = tPut; refusals++; return; }
        a = pa; b = pb; c = pc; occ = true;
    }
    /** True if the register was empty at the start of cycle t. */
    @Override public boolean canPut(long t) { enter(t); return !occ && takes == 0; }
    @Override public void put(long t, long a, long b, long c) { notePut(t); pa = a; pb = b; pc = c; }
    /** True at t if the put made at t-1 was refused (the old word was not taken at t-1). */
    boolean refused(long t) { enter(t); return refusedAt == t - 1; }
    /** Present the refused word again (the producer's retry; the register still holds it). */
    void retry(long t) {
        if (!refused(t)) throw new IllegalStateException(name + ": retry without a refusal at " + t);
        put(t, pa, pb, pc);
    }
    @Override public boolean ready(long t) { enter(t); return occ; }
    /** Start-of-cycle occupancy: true if a word was held at the start of t, whether or not it is taken during t
     *  (order independent, unlike ready(t) after a same-cycle take). v3.3. */
    boolean heldAtStart(long t) { enter(t); return occ || takes > 0; }
    boolean isEmpty(long t) { enter(t); return !occ && !pendPut; }
    private void check(long t) { enter(t); if (!occ) throw new IllegalStateException(name + ": empty at " + t); }
    @Override public long peekA(long t) { check(t); return a; }
    @Override public long peekB(long t) { check(t); return b; }
    @Override public long peekC(long t) { check(t); return c; }
    @Override public void take(long t) { check(t); noteTake(t); occ = false; }
}

/**
 * Backpressure FIFO of words: canPut(t) is computed from start-of-cycle occupancy (visible entries
 * plus takes already made this cycle), so the answer does not depend on call order; a put when
 * canPut(t) is false is a simulator bug and throws. Used for intra-tile handshakes where the
 * consumer pulls only when it has room (none in the v3.4 design; kept, tested).
 */
final class WordBackpressureFIFO extends WordBus {
    final int capacity;
    private final long[] va, vb, vc;
    private int head, count;
    private long pa, pb, pc;
    int highWater;

    WordBackpressureFIFO(String name, int capacity) {
        super(name);
        this.capacity = capacity;
        va = new long[capacity]; vb = new long[capacity]; vc = new long[capacity];
    }

    @Override protected void commit(long tPut) {
        if (count == capacity) throw new IllegalStateException(name + ": backpressure FIFO overflowed at " + tPut);
        int i = head + count; if (i >= capacity) i -= capacity;
        va[i] = pa; vb[i] = pb; vc[i] = pc; count++;
        if (count > highWater) highWater = count;
    }
    /** Start-of-cycle occupancy below capacity. */
    @Override public boolean canPut(long t) { enter(t); return count + takes < capacity; }
    @Override public void put(long t, long a, long b, long c) {
        if (!canPut(t)) throw new IllegalStateException(name + ": put when canPut(" + t + ") is false");
        notePut(t); pa = a; pb = b; pc = c;
    }
    @Override public boolean ready(long t) { enter(t); return count > 0; }
    private void check(long t) { enter(t); if (count == 0) throw new IllegalStateException(name + ": empty at " + t); }
    @Override public long peekA(long t) { check(t); return va[head]; }
    @Override public long peekB(long t) { check(t); return vb[head]; }
    @Override public long peekC(long t) { check(t); return vc[head]; }
    @Override public void take(long t) { check(t); noteTake(t); head = head + 1 == capacity ? 0 : head + 1; count--; }
    int size(long t) { enter(t); return count; }
    boolean isEmpty(long t) { enter(t); return count == 0 && !pendPut; }
    void resetStats(long t) { enter(t); highWater = count; }
}

/**
 * Delay line with reservation: a put at t is visible from t+L, and canPut(t) is true only while
 * the words in flight plus the words visible and not yet taken (as of the start of t) are below
 * the capacity. The producer reserves the consumer's space at the moment it issues (the memory
 * port reserves a landing slot before it issues a core read); a put when canPut is false throws.
 */
final class WordBackpressureDelayLine extends WordBus {
    final int latency, capacity;
    private final long[] va, vb, vc, at;
    private int head, count;                // committed entries (in flight or visible)
    private long pa, pb, pc, pendAt;

    WordBackpressureDelayLine(String name, int latency, int capacity) {
        super(name);
        if (latency < 1) throw new IllegalArgumentException("latency >= 1");
        this.latency = latency; this.capacity = capacity;
        va = new long[capacity]; vb = new long[capacity]; vc = new long[capacity]; at = new long[capacity];
    }

    @Override protected void commit(long tPut) {
        if (count == capacity) throw new IllegalStateException(name + ": reservation exceeded at " + tPut);
        int i = head + count; if (i >= capacity) i -= capacity;
        va[i] = pa; vb[i] = pb; vc[i] = pc; at[i] = pendAt; count++;
    }
    /** Start-of-cycle: entries committed (in flight or waiting) plus takes so far, below capacity. */
    @Override public boolean canPut(long t) { enter(t); return count + takes < capacity; }
    @Override public void put(long t, long a, long b, long c) {
        if (!canPut(t)) throw new IllegalStateException(name + ": put when canPut(" + t + ") is false");
        notePut(t); pa = a; pb = b; pc = c; pendAt = t + latency;
    }
    @Override public boolean ready(long t) { enter(t); return count > 0 && at[head] <= t; }
    private void check(long t) { if (!ready(t)) throw new IllegalStateException(name + ": nothing visible at " + t); }
    @Override public long peekA(long t) { check(t); return va[head]; }
    @Override public long peekB(long t) { check(t); return vb[head]; }
    @Override public long peekC(long t) { check(t); return vc[head]; }
    @Override public void take(long t) { check(t); noteTake(t); head = head + 1 == capacity ? 0 : head + 1; count--; }
    int inFlight(long t) { enter(t); return count + (pendPut ? 1 : 0); }
}
