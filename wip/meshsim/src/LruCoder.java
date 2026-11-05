/**
 * The Tracer's LRU stream encoder (spec 4.21.6; 4.19.15; DECISIONS-v1.3 sections 3, 4; item 151), the exact inverse of the
 * replayer's decoder ({@code Replayer.Stream}): a 16-entry table, all zeros at a window's start ({@link #reset}).
 * <ul>
 * <li>code and read log: a true LRU of 16 entries; the write log: 15 LRU entries and E[15], <b>the last load</b>, set by
 *     every load word (tag 0) in row order ({@link #lastLoad}), as the decoder does;</li>
 * <li>a hit is coded 1, then its 4-bit index; a value matching several entries takes the <b>lowest matching index</b>,
 *     and in the write log a match with E[15] is coded as <b>index 15 first</b>;</li>
 * <li>a miss is coded 0, a size bit, then the value: <b>32 bits exactly when its top 32 bits are 0</b>, else 64 (item 151,
 *     gold's frame-5 rule). The table is then updated as the decoder updates it (move to front; a miss pushes at the front
 *     and drops the LRU end, E[15] untouched in the write log).</li>
 * </ul>
 * Bits are emitted least significant first ({@link Bits}). Set 1 (sizing) passes no sink and counts; set 2 emits.
 */
final class LruCoder {
    final boolean isWrite;
    private final long[] e = new long[16];
    long bits, hits, misses, values;

    LruCoder(boolean isWrite) { this.isWrite = isWrite; }
    void reset() { java.util.Arrays.fill(e, 0); bits = hits = misses = values = 0; }
    void lastLoad(long v) { if (isWrite) e[15] = v; }

    /** The bits v would take, without coding it (the scan's room check). */
    int peekBits(long v) {
        if (isWrite && e[15] == v) return 5;
        for (int i = 0; i < (isWrite ? 15 : 16); i++) if (e[i] == v) return 5;
        return (v >>> 32) != 0 ? 66 : 34;
    }
    /** Codes v; appends its bits to sink (null: count only); returns the bits it takes. */
    int encode(long v, Bits sink) {
        values++;
        int idx = -1;
        if (isWrite && e[15] == v) idx = 15;
        else for (int i = 0; i < (isWrite ? 15 : 16); i++) if (e[i] == v) { idx = i; break; }
        int n;
        if (idx >= 0) {
            hits++; n = 5;
            if (sink != null) { sink.put(1, 1); sink.put(idx, 4); }
            if (!(isWrite && idx == 15)) { System.arraycopy(e, 0, e, 1, idx); e[0] = v; }
        } else {
            misses++;
            boolean wide = (v >>> 32) != 0;
            n = wide ? 66 : 34;
            if (sink != null) { sink.put(0, 1); sink.put(wide ? 1 : 0, 1); sink.put(v, wide ? 64 : 32); }
            System.arraycopy(e, 0, e, 1, isWrite ? 14 : 15);
            e[0] = v;
        }
        bits += n;
        return n;
    }

    /** A packer's bit stream: least significant bit first into 128-bit words (4.19.15). */
    static final class Bits {
        long lo, hi; int fill;                         // the word being packed and its bits
        final java.util.function.BiConsumer<Long, Long> out;   // each full word (and the last, padded with zeros)
        long words;
        Bits(java.util.function.BiConsumer<Long, Long> out) { this.out = out; }
        void put(long v, int n) {
            while (n > 0) {
                int take = Math.min(n, 128 - fill);
                long m = take == 64 ? -1L : (1L << take) - 1, part = v & m;
                if (fill < 64) { lo |= part << fill; if (fill + take > 64) hi |= part >>> (64 - fill); }
                else hi |= part << (fill - 64);
                fill += take; n -= take; v = take == 64 ? 0 : v >>> take;
                if (fill == 128) { out.accept(lo, hi); words++; lo = hi = 0; fill = 0; }
            }
        }
        /** The stream's end: its last partial word committed (the rest of it padding, zeros here). */
        void finish() { if (fill > 0) { out.accept(lo, hi); words++; lo = hi = 0; fill = 0; } }
    }
}
