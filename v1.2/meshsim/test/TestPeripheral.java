import java.util.ArrayList;
import java.util.List;

/**
 * A test-only peripheral (type 15; meshsim's tests only) that exercises a unit's side of the peripheral framework (4.17,
 * v1.2) and the DMA (4.17.9):
 *  - events: [0] run done (depth 1), [1] DMA done (the slot's), [2] an ordinary event (depth 1, {@link #post}), [3] a
 *    credit (depth 2, {@link #credit}; a post to a full count is flagged), [4] live ({@link #level});
 *  - a run lasts {@link #runLength} cycles; STATUS [0] while it runs; ERRORS [0] when a test calls {@link #fault};
 *  - words: 32 the runs started (R), 33 a scratch word (R/W), 34 a constant (R), the rest undefined;
 *  - region writes are recorded when they land (rp_latency after the director), at their byte offset;
 *  - two output queues: after outStart(q, n) it makes n words, word i = q << 24 | the queue's running count, one per
 *    cycle from the next cycle (or all at once with {@link #burst}).
 */
final class TestPeripheral implements Peripheral {
    final EventCounts ev = new EventCounts(new int[] { 1, 0, 1, 2, 0 }, new boolean[] { false, false, false, true, false });
    boolean running; long runEnds; int runLength = 10, runs, scratch, errors;
    final List<long[]> accepts = new ArrayList<>();   // {landing t, src, offset, size, lo, hi}
    final List<Long> runCalls = new ArrayList<>();
    final int[] taken = new int[2];
    @SuppressWarnings("unchecked") final List<Long>[] availAt = new List[] { new ArrayList<Long>(), new ArrayList<Long>() };
    boolean burst;
    Host host;

    @Override public int type() { return 15; }
    @Override public int params() { return 15 | 3 << 8; }          // type 15, version 3
    @Override public void attach(Host h) { host = h; }
    @Override public EventCounts events() { return ev; }
    void post(long t) { ev.post(t, 2); }
    void credit(long t) { ev.post(t, 3); }
    void level(boolean on) { ev.live(4, on); }
    void fault() { errors |= 1; }
    @Override public void accept(long t, int src, int offset, int size, long lo, long hi) { accepts.add(new long[] { t + RP_LATENCY, src, offset, size, lo, hi }); }
    @Override public void reset(long t) { running = false; ev.clearAll(); errors = 0; }
    @Override public void run(long t) {
        runCalls.add(t);
        if (running) { host.violate(t, "test peripheral: a run while running, ignored"); return; }
        running = true; runs++; runEnds = t + runLength;
    }
    @Override public void runCycle(long t) { if (running && t >= runEnds) { running = false; ev.post(t, 0); } }
    @Override public boolean running(long t) { return running; }
    @Override public int errors(long t) { return errors; }
    @Override public Integer word(long t, int w) { return w == 32 ? Integer.valueOf(runs) : w == 33 ? Integer.valueOf(scratch) : w == 34 ? Integer.valueOf(0x7E57) : null; }
    @Override public int writeWord(long t, int w, int v) { if (w == 33) { scratch = v; return 1; } return w == 32 || w == 34 ? 0 : -1; }
    @Override public int outputQueues() { return 2; }
    @Override public boolean outReady(long t, int q) { return q < 2 && taken[q] < availAt[q].size() && availAt[q].get(taken[q]) <= t; }
    @Override public int outTake(long t, int q) { if (!outReady(t, q)) throw new IllegalStateException("no word"); return q << 24 | taken[q]++; }
    @Override public void outStart(long t, int q, int words) {
        long from = Math.max(t + 1, availAt[q].isEmpty() ? t + 1 : availAt[q].get(availAt[q].size() - 1) + (burst ? 0 : 1));
        for (int i = 0; i < words; i++) availAt[q].add(burst ? from : from + i);
    }
}
