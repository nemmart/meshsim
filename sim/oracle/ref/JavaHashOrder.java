/**
 * JavaHashOrder (WORKER21): java.util.HashMap's iteration order for Long keys, after a sequence of put (HashSet.add is
 * put) and computeIfAbsent calls: the reference for tracer_rig.cpp's JavaLongHashMap (tests/test_tracer_rig.cpp).
 * ref's TracerRig puts a guest's pages into its FileStore in this order (its HashMap of pages, filled by poke's
 * computeIfAbsent and by put; its HashSet of owned pages), which decides their addresses, and so the caches' sets and
 * every timing. The two calls differ: put appends a new key to its bin and grows the table after inserting;
 * computeIfAbsent prepends it and grows the table before; their treeify thresholds differ by one.
 * Each case is two lines: "ops x1 x2 ..." (pK: put(K), cK: computeIfAbsent(K), in order) and "order o1 o2 ..." (the
 * map's keySet), or "treeified" where a bin became a red-black tree (its order follows the tree's root; the C++ model
 * refuses those rather than guess). Usage: java --add-opens java.base/java.util=ALL-UNNAMED JavaHashOrder
 */
public final class JavaHashOrder {
    static long lcg = 12345;
    static long next() { lcg = lcg * 6364136223846793005L + 1442695040888963407L; return lcg >>> 33; }
    static void emit(String[] ops) {
        java.util.HashMap<Long, Long> m = new java.util.HashMap<>();
        StringBuilder a = new StringBuilder("ops"), b = new StringBuilder("order");
        for (String op : ops) {
            long k = Long.parseLong(op.substring(1));
            if (op.charAt(0) == 'p') m.put(k, 0L); else m.computeIfAbsent(k, x -> 0L);
            a.append(' ').append(op);
        }
        boolean tree = false;
        try {
            java.lang.reflect.Field f = java.util.HashMap.class.getDeclaredField("table");
            f.setAccessible(true);
            for (Object e : (Object[]) f.get(m)) tree |= e != null && e.getClass().getName().equals("java.util.HashMap$TreeNode");
        } catch (ReflectiveOperationException e) { throw new IllegalStateException(e); }
        if (tree) b = new StringBuilder("treeified");
        else for (long k : m.keySet()) b.append(' ').append(k);
        System.out.println(a); System.out.println(b);
    }
    static String[] ops(char kind, long[] keys) {
        String[] o = new String[keys.length];
        for (int i = 0; i < keys.length; i++) o[i] = (kind == 'm' ? (next() % 2 == 0 ? 'p' : 'c') : kind) + Long.toString(keys[i]);
        return o;
    }
    public static void main(String[] args) {
        for (char kind : new char[] { 'p', 'c', 'm' }) {
            for (int n : new int[] { 5, 12, 13, 14, 40, 90 }) { long[] k = new long[n]; for (int i = 0; i < n; i++) k[i] = (next() & 0xFFFFF) * 4096; emit(ops(kind, k)); }
            { long[] k = new long[21]; for (int i = 0; i < 21; i++) k[i] = 0x100000L + 4096L * i; emit(ops(kind, k)); }
            for (int n = 7; n <= 12; n++) { long[] k = new long[n]; for (int i = 0; i < n; i++) k[i] = 16L * (n - 1 - i); emit(ops(kind, k)); }  // one bin filling
            { long[] k = new long[30]; for (int i = 0; i < 30; i++) k[i] = (next() << 20) ^ -(long) next(); emit(ops(kind, k)); }
            emit(ops(kind, new long[] { 7, 3, 7, 1L << 40, -5, 3, 1L << 32, 1 }));
        }
        emit(new String[] { "c1048576", "c1052672" });                                              // TracerTest's rig()
        emit(new String[] { "c1048576", "p1052672", "p1056768", "p1060864", "p1064960", "p1069056" }); // writeBack, streamy
        emit(new String[] { "c0", "c4096" });                                                       // the unaligned loop
        emit(new String[] { "p1052672", "p1056768", "p1060864", "p1064960", "p1069056" });          // writeBack's owned set
    }
}
