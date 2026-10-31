/**
 * Latch: one registered value. A put at t is visible from t+1 and stays visible until the next
 * put replaces it. take() is non-destructive (a latch may have many readers, e.g. the GO line):
 * it returns the visible value. ready() is false until the first value has become visible.
 */
final class Latch<T> extends AbstractBus<T> {
    @Override void flush() { super.flush(); valid = false; pending = null; }
    private T value, pending;
    private boolean valid;

    Latch(String name) { super(name); }

    @Override protected void commit(long tPut) { value = pending; valid = true; pending = null; }
    @Override public boolean canPut(long t) { enter(t); return true; }
    @Override public void put(long t, T v) { notePut(t); pending = v; }
    @Override public boolean ready(long t) { enter(t); return valid; }
    @Override public T peek(long t) {
        enter(t);
        if (!valid) throw new IllegalStateException(name + ": no value visible at " + t);
        return value;
    }
    @Override public T take(long t) { return peek(t); }
}

/**
 * DelayLine: fixed latency L >= 1; a put at t is visible from t+L, values stay in order and are
 * removed by take(). Capacity is bounded (default L+2): the consumer is expected to take each value
 * when it becomes visible (a link delivers one word per cycle); exceeding it is a simulator bug.
 */
final class DelayLine<T> extends AbstractBus<T> {
    @Override void flush() { super.flush(); head = 0; count = 0; pending = null; }
    final int latency;
    private final Object[] vals;
    private final long[] at;
    private int head, count;
    private T pending;
    private long pendAt;

    DelayLine(String name, int latency) { this(name, latency, latency + 2); }
    DelayLine(String name, int latency, int capacity) {
        super(name);
        if (latency < 1) throw new IllegalArgumentException("latency >= 1");
        this.latency = latency;
        vals = new Object[capacity];
        at = new long[capacity];
    }

    @Override protected void commit(long tPut) {
        if (count == vals.length) throw new IllegalStateException(name + ": delay line full (consumer did not take)");
        int i = head + count; if (i >= vals.length) i -= vals.length;
        vals[i] = pending; at[i] = pendAt; count++;
        pending = null;
    }
    @Override public boolean canPut(long t) { enter(t); return true; }
    @Override public void put(long t, T v) { notePut(t); pending = v; pendAt = t + latency; }
    @Override public boolean ready(long t) { enter(t); return count > 0 && at[head] <= t; }
    @SuppressWarnings("unchecked")
    @Override public T peek(long t) {
        if (!ready(t)) throw new IllegalStateException(name + ": nothing visible at " + t);
        return (T) vals[head];
    }
    @Override public T take(long t) {
        T v = peek(t);
        noteTake(t);
        vals[head] = null;
        head = head + 1 == vals.length ? 0 : head + 1;
        count--;
        return v;
    }
    int inFlight(long t) { enter(t); return count + (pendPut ? 1 : 0); }
}

/**
 * TimedFIFO: capacity N; a put at t is visible from t+1; the capacity check is made at the
 * end-of-cycle settle against occupancy(t) - takes(t), so a FIFO of any depth sustains one write
 * and one read per cycle whatever the call order. Statistics: high-water mark, overflow flag
 * with the cycle of the first overflow.
 */
abstract class TimedFIFO<T> extends AbstractBus<T> {
    @Override void flush() { super.flush(); head = 0; count = 0; pending = null; overflow = false; overflowCycle = -1; }
    final int capacity;
    private final Object[] vals;
    private int head, count;          // visible entries
    private T pending;
    int highWater;
    boolean overflow;                 // sticky
    long overflowCycle = -1;
    long overflowCount;

    TimedFIFO(String name, int capacity) {
        super(name);
        this.capacity = capacity;
        vals = new Object[capacity];
    }

    @Override protected void commit(long tPut) {
        if (count == capacity) { onOverflow(tPut); pending = null; return; }
        int i = head + count; if (i >= capacity) i -= capacity;
        vals[i] = pending; count++; pending = null;
        if (count > highWater) highWater = count;
    }

    /** Called when the put at tPut does not fit. */
    protected void onOverflow(long tPut) {
        overflowCount++;
        if (!overflow) { overflow = true; overflowCycle = tPut; }
    }

    @Override public void put(long t, T v) { notePut(t); pending = v; }
    @Override public boolean ready(long t) { enter(t); return count > 0; }
    @SuppressWarnings("unchecked")
    @Override public T peek(long t) {
        enter(t);
        if (count == 0) throw new IllegalStateException(name + ": empty at " + t);
        return (T) vals[head];
    }
    @Override public T take(long t) {
        T v = peek(t);
        noteTake(t);
        vals[head] = null;
        head = head + 1 == capacity ? 0 : head + 1;
        count--;
        return v;
    }
    /** Visible occupancy at t (start-of-cycle occupancy minus takes so far this cycle). */
    int size(long t) { enter(t); return count; }
    void resetStats(long t) { enter(t); highWater = count; }
    void clearOverflow() { overflow = false; overflowCycle = -1; overflowCount = 0; }
}

/** Never backpressures: canPut is always true; a put that does not fit is dropped and flagged. */
final class OverflowFIFO<T> extends TimedFIFO<T> {
    OverflowFIFO(String name, int capacity) { super(name, capacity); }
    @Override public boolean canPut(long t) { enter(t); return true; }
}

/**
 * canPut(t) is computed from start-of-cycle occupancy (so the answer does not depend on call
 * order); a put when canPut(t) was false is a simulator bug and throws. Not used by the design.
 */
final class BackpressureFIFO<T> extends TimedFIFO<T> {
    BackpressureFIFO(String name, int capacity) { super(name, capacity); }

    /** Start-of-cycle occupancy = visible entries + takes already made this cycle. */
    @Override public boolean canPut(long t) { return size(t) + takes < capacity; }
    @Override public void put(long t, T v) {
        if (!canPut(t)) throw new IllegalStateException(name + ": put when canPut(" + t + ") is false");
        super.put(t, v);
    }
    @Override protected void onOverflow(long tPut) {
        throw new IllegalStateException(name + ": backpressure FIFO overflowed at " + tPut);
    }
}
