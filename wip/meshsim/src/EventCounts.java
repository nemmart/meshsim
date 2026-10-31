/**
 * A peripheral's counted events (spec 4.17.3): every EVENTS bit is backed by a small counter of a given depth, and reads
 * 1 while its count is above 0. The peripheral adds 1 when the event happens ({@link #post}; adding to a full count
 * leaves it full, and meshsim flags a credit returned to a full counter); a consuming load of EVENTS subtracts 1 from
 * each bit it returned set under EVENTS_MASK ({@link #consume}). A bit of depth 0 is live: the peripheral supplies its
 * level ({@link #live}) and nothing consumes it.
 *
 * Timing (4.11): a post at t is visible from t + 1; a consuming load sampling at s takes its effect at the end of s + 1
 * and removes exactly what it returned, so an event posted after the sample point survives.
 */
final class EventCounts {
    final int[] depth;                       // per bit: 0 live, 1 an ordinary event, 2 a double-buffered queue's credits
    final boolean[] flagFull;                // per bit: a post to a full counter is a violation (credits)
    private final int[] count = new int[32];
    private final long[] lastPostAt = new long[32];
    private int liveBits;
    long fullPosts; String lastFull;

    EventCounts(int[] depth, boolean[] flagFull) { this.depth = depth; this.flagFull = flagFull; java.util.Arrays.fill(lastPostAt, Long.MIN_VALUE); }

    /** The event on bit b happens at t (visible from t + 1). */
    void post(long t, int b) {
        if (count[b] >= depth[b]) { if (flagFull[b]) { fullPosts++; lastFull = "an event posted to bit " + b + " at its full count " + depth[b] + " (4.17.3)"; } return; }
        count[b]++; lastPostAt[b] = t;
    }
    /** Set bit b's count (the replayer's credits at a start). */
    void set(long t, int b, int n) { count[b] = Math.min(n, depth[b]); lastPostAt[b] = t; }
    void clear(int b) { count[b] = 0; }
    void clearAll() { java.util.Arrays.fill(count, 0); }
    /** A live bit's level. */
    void live(int b, boolean on) { if (on) liveBits |= 1 << b; else liveBits &= ~(1 << b); }

    /** EVENTS as a load sampling at t reads it: counts as at the end of t - 1 (a post at t not yet), the live bits. */
    int value(long t) {
        int v = liveBits;
        for (int b = 0; b < depth.length; b++) if (depth[b] > 0 && count[b] - (lastPostAt[b] == t ? 1 : 0) > 0) v |= 1 << b;
        return v;
    }
    /** A consuming load's effect: one count from each counted bit in bits (they were returned set). */
    void consume(int bits) { for (int b = 0; b < depth.length; b++) if ((bits >> b & 1) != 0 && depth[b] > 0 && count[b] > 0) count[b]--; }
    int count(int b) { return count[b]; }
}
