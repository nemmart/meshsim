/**
 * WORKER29 (P2, A): primitive collections for the simulator's bookkeeping, so a cycle allocates nothing (S1's acceptance
 * times were boxed into ArrayDeque&lt;Long&gt; and HashMap&lt;Integer, Long&gt;). Same contents and order as the collections they
 * replace; no iteration order is exposed.
 */
final class Prim {
    private Prim() {}

    /** A FIFO of longs (ArrayDeque&lt;Long&gt;'s add, poll, isEmpty), growing by doubling. */
    static final class LongRing {
        private long[] v; private int head, n;
        LongRing() { this(8); }
        LongRing(int cap) { v = new long[Integer.highestOneBit(Math.max(2, cap - 1)) << 1]; }
        boolean isEmpty() { return n == 0; }
        int size() { return n; }
        void add(long x) {
            if (n == v.length) { long[] w = new long[2 * v.length]; for (int i = 0; i < n; i++) w[i] = v[(head + i) & (v.length - 1)]; v = w; head = 0; }
            v[(head + n++) & (v.length - 1)] = x;
        }
        /** The oldest value, removed; empty: throws (callers check isEmpty, as ArrayDeque.poll's null was checked). */
        long poll() { if (n == 0) throw new IllegalStateException("empty"); long x = v[head]; head = (head + 1) & (v.length - 1); n--; return x; }
        void clear() { head = 0; n = 0; }
    }

    /** A map from non-negative ints to longs (HashMap&lt;Integer, Long&gt;'s put, get, remove): open addressing, linear probing,
     *  backward-shift deletion. ABSENT marks a missing key. */
    static final class IntLongMap {
        static final long ABSENT = Long.MIN_VALUE;
        private int[] k; private long[] v; private int n, mask;
        IntLongMap() { this(64); }
        IntLongMap(int cap) { int c = Integer.highestOneBit(Math.max(4, cap - 1)) << 1; k = new int[c]; v = new long[c]; java.util.Arrays.fill(k, -1); mask = c - 1; }
        int size() { return n; }
        private static int h(int x) { x *= 0x9E3779B9; return x ^ (x >>> 16); }
        void put(int key, long val) {
            if (key < 0) throw new IllegalArgumentException("key " + key);
            int i = h(key) & mask;
            while (k[i] != -1) { if (k[i] == key) { v[i] = val; return; } i = (i + 1) & mask; }
            k[i] = key; v[i] = val;
            if (++n * 2 > k.length) grow();
        }
        long get(int key) {
            int i = h(key) & mask;
            while (k[i] != -1) { if (k[i] == key) return v[i]; i = (i + 1) & mask; }
            return ABSENT;
        }
        long remove(int key) {
            int i = h(key) & mask;
            while (k[i] != -1 && k[i] != key) i = (i + 1) & mask;
            if (k[i] == -1) return ABSENT;
            long out = v[i];
            // backward shift: move later entries of the probe run into the hole if their home is at or before it
            int j = i;
            while (true) {
                j = (j + 1) & mask;
                if (k[j] == -1) break;
                int home = h(k[j]) & mask;
                if (((j - home) & mask) >= ((j - i) & mask)) { k[i] = k[j]; v[i] = v[j]; i = j; }
            }
            k[i] = -1; n--;
            return out;
        }
        private void grow() {
            int[] ok = k; long[] ov = v; int c = ok.length * 2;
            k = new int[c]; v = new long[c]; java.util.Arrays.fill(k, -1); mask = c - 1; n = 0;
            for (int i = 0; i < ok.length; i++) if (ok[i] != -1) put(ok[i], ov[i]);
        }
    }
}
