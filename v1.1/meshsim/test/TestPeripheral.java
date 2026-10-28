import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * A test-only peripheral (type 15, reserved in the spec; meshsim's tests only) that exercises the tile's side of the slot
 * (4.17) and the sender (4.17.9): one line of each kind and two output queues.
 *  - line 1 (PERIPH_SERVICE_1, [13]): an event (sticky), raised by {@link #post}; a claim clears it;
 *  - line 2 ([14]): a queue condition (live), up while {@link #units} > 0; a claim takes one unit;
 *  - line 3 ([15]): an item FIFO (live), up while items remain; its presented item is PERIPH_STATUS_2; a claim pops it;
 *  - line 6 ([18]): done (an event), posted when a run of {@link #runLength} cycles ends;
 *  - PERIPH_STATUS_0: the number of claims taken; PERIPH_STATUS_1: runs started; the rest 0;
 *  - mode 9 lands rp_latency after the director; the packets are recorded; nothing is a bad write;
 *  - two output queues: after outStart(q, n) it makes n words, word i = q << 24 | the queue's running count, one per
 *    cycle from the next cycle (or all at once with {@link #burst}).
 * Every input takes effect at the end of its cycle; level reads at t see the state at the end of t - 1.
 */
final class TestPeripheral implements Peripheral {
    private record In(long t, int kind, int a, int b) {}
    private final List<In> pending = new ArrayList<>();
    private long settledTo = Long.MIN_VALUE;
    // state as at the end of the last settled cycle
    boolean running, event, done, halted;
    int units, claims, runs;
    final ArrayDeque<Integer> items = new ArrayDeque<>();
    long runEnds = -1;
    int runLength = 10;
    // records
    final List<long[]> accepts = new ArrayList<>();   // {t, src, addr, size, lo, hi}
    final List<long[]> claimLog = new ArrayList<>();  // {t, lines}
    final List<long[]> haltLog = new ArrayList<>();   // {t, 1/0}
    final List<Long> runLog = new ArrayList<>();
    // output
    final int[] taken = new int[2];
    @SuppressWarnings("unchecked") final List<Long>[] availAt = new List[] { new ArrayList<Long>(), new ArrayList<Long>() };   // per word: the first cycle it can be taken
    boolean burst;                                    // all of a send's words at once (else one per cycle)
    Host host;

    @Override public int type() { return 15; }
    @Override public void attach(Host h) { host = h; }
    /** Raise line 1 at the end of t (a test drives it). */
    void post(long t) { pending.add(new In(t, 4, 0, 0)); }
    @Override public void accept(long t, int src, int addr, int size, long lo, long hi) { accepts.add(new long[] { t, src, addr, size, lo, hi }); }
    @Override public void run(long t) { runCalls.add(t); pending.add(new In(t, 1, 0, 0)); }
    final List<Long> runCalls = new ArrayList<>();
    @Override public void claim(long t, int lines) { claimLog.add(new long[] { t, lines }); pending.add(new In(t, 2, lines, 0)); }
    @Override public void setHalted(long t, boolean h) { haltLog.add(new long[] { t, h ? 1 : 0 }); pending.add(new In(t, 3, h ? 1 : 0, 0)); }

    /** Apply every input made before t (end-of-cycle effects), and the run's end. */
    private void settle(long t) {
        if (t - 1 <= settledTo) return;
        List<In> now = new ArrayList<>();
        for (In i : pending) if (i.t < t) now.add(i);
        pending.removeAll(now);
        now.sort((x, y) -> Long.compare(x.t, y.t));
        for (In i : now) {
            if (running && runEnds <= i.t) endRun();
            switch (i.kind) {
                case 1 -> { if (running) host.violate(i.t, "test peripheral: a run while running, ignored"); else if (!halted) { running = true; runs++; runLog.add(i.t); runEnds = i.t + runLength; } }
                case 2 -> {
                    claims++;
                    if ((i.a & 1) != 0) event = false;
                    if ((i.a & 2) != 0 && units > 0) units--;
                    if ((i.a & 4) != 0 && !items.isEmpty()) items.poll();
                    if ((i.a & 32) != 0) done = false;
                }
                case 3 -> { halted = i.a == 1; if (halted) { running = false; event = false; done = false; } }
                case 4 -> event = true;
            }
        }
        if (running && runEnds <= t - 1) endRun();
        settledTo = t - 1;
    }
    private void endRun() { running = false; done = true; }
    @Override public void runCycle(long t) { settle(t); }
    @Override public boolean running(long t) { settle(t); return running; }
    @Override public int lines(long t) {
        settle(t);
        return (event ? 1 : 0) | (units > 0 ? 2 : 0) | (!items.isEmpty() ? 4 : 0) | (done ? 32 : 0);
    }
    @Override public int status(long t, int i) { settle(t); return i == 0 ? claims : i == 1 ? runs : i == 2 ? (items.isEmpty() ? 0 : items.peek()) : 0; }
    @Override public int landed(long t) { int n = 0; for (long[] a : accepts) if (a[0] == t - RP_LATENCY) n++; return n; }
    @Override public long conditionEvents(int k) { return 0; }
    @Override public int outputQueues() { return 2; }
    @Override public boolean outReady(long t, int q) { return q < 2 && taken[q] < availAt[q].size() && availAt[q].get(taken[q]) <= t; }
    @Override public int outTake(long t, int q) { if (!outReady(t, q)) throw new IllegalStateException("no word"); return q << 24 | taken[q]++; }
    @Override public void outStart(long t, int q, int words) {
        long from = Math.max(t + 1, availAt[q].isEmpty() ? t + 1 : availAt[q].get(availAt[q].size() - 1) + (burst ? 0 : 1));
        for (int i = 0; i < words; i++) availAt[q].add(burst ? from : from + i);
    }
}
