/**
 * Timed buses (brief 2.2) — the only way components communicate.
 *
 * Interfaces: {@link Component}, {@link Sink}, {@link Source}; base class {@link AbstractBus}.
 * Every bus tracks the current cycle from the {@code t} of each call: calls within one cycle
 * may come in any order, {@code t} never goes backwards, and the bus settles (commits the
 * cycle's put) the first time it is touched at a later cycle. That is equivalent to settling
 * at the end of the cycle, because nothing can observe the bus in between.
 *
 * Rules enforced (throw = simulator bug):
 *  1. a value put at t is never visible before t+1 (t+L on a delay line);
 *  2. put and take in the same cycle are both allowed; occupancy(t+1) = occupancy(t) - takes + puts;
 *  3. at most one put per bus per cycle, at most one take per cycle; reading a value that is not
 *     visible is an error.
 */
interface Component { void runCycle(long t); }

interface Sink<T>   { boolean canPut(long t); void put(long t, T v); }
interface Source<T> { boolean ready(long t); T peek(long t); T take(long t); }

/** Cycle bookkeeping shared by all buses (generic and primitive). */
abstract class BusBase {
    final String name;
    protected long lastT = Long.MIN_VALUE;
    protected boolean pendPut;      // a put happened at lastT and is not yet committed
    protected int takes;            // takes at lastT

    BusBase(String name) { this.name = name; java.util.List<BusBase> r = REGISTRY.get(); if (r != null) r.add(this); }
    /** The buses a chip under construction creates register here (Chip sets it), so the reset can empty them all. */
    static final ThreadLocal<java.util.List<BusBase>> REGISTRY = new ThreadLocal<>();
    /** The reset (6.1): empty the bus, a pending put included. */
    void flush() { pendPut = false; takes = 0; }

    /** Enter cycle t: commit the previous cycle's put, reset per-cycle counters. */
    protected final void enter(long t) {
        if (t != lastT) enterSlow(t);
    }
    private void enterSlow(long t) {
        if (t < lastT) throw new IllegalStateException(name + ": time went backwards " + lastT + " -> " + t);
        if (pendPut) { commit(lastT); pendPut = false; }
        takes = 0;
        lastT = t;
    }

    /** Commit the put made at cycle tPut (it becomes visible at tPut+1 or later). */
    protected abstract void commit(long tPut);

    protected final void notePut(long t) {
        enter(t);
        if (pendPut) throw new IllegalStateException(name + ": two puts in cycle " + t);
        pendPut = true;
    }

    protected final void noteTake(long t) {
        enter(t);
        if (takes > 0) throw new IllegalStateException(name + ": two takes in cycle " + t);
        takes++;
    }
}

abstract class AbstractBus<T> extends BusBase implements Sink<T>, Source<T> {
    AbstractBus(String name) { super(name); }
}
