import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * TracerPlay (WORKER14): ref's Tracer replaying a call trace (bench/w14/tracer_play.cpp's format, converted from a
 * TracerConf log), timed: per repetition a fresh store, memory system and Tracer, then every call in order; per segment
 * (a segment starts at its ownership edit, not timed) and in all; then the harness alone (the decoding and dispatch, no
 * Tracer call). Each pass must end on the log's last row count and record hash. Compiled into ref's default package.
 *
 * Usage: java -Xmx4g TracerPlay TRACE [REPS]
 */
public final class TracerPlay {
    static final class Op { int op; long d, a, b, c, lo, hi; long[] pages; }
    static final class In {
        final ByteBuffer p;
        In(ByteBuffer p) { this.p = p; }
        long v() { long x = 0; int s = 0; while (true) { int b = p.get() & 0xFF; x |= (long) (b & 0x7F) << s; if ((b & 0x80) == 0) return x; s += 7; } }
        long z() { long u = v(); return (u >>> 1) ^ -(u & 1); }
        long raw() { return p.getLong(); }
    }

    static List<Op[]> defs = new ArrayList<>();
    static byte[] groups;
    static int first, nextFree, icLines = 128;
    static Map<Integer, long[]> pages0 = new java.util.HashMap<>();
    static long endRows, endRecHash;

    static Op[] readDef(In in) {
        Op[] ops = new Op[(int) in.v()];
        for (int i = 0; i < ops.length; i++) {
            Op o = new Op(); ops[i] = o;
            o.op = in.p.get() & 0xFF; o.d = in.z();
            switch (o.op) {
                case 4 -> { o.a = in.v(); o.b = in.v(); o.c = in.v(); o.lo = in.raw(); o.hi = in.raw(); }
                case 5, 13 -> { o.a = in.v(); o.b = in.v(); }
                case 6, 10, 14, 15 -> o.a = in.v();
                case 16 -> { o.a = in.v(); o.b = in.v(); int k = (int) in.v(); o.pages = new long[k]; for (int j = 0; j < k; j++) o.pages[j] = in.v(); }
                default -> { }
            }
        }
        return ops;
    }

    static void load(Path path) throws Exception {
        ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(path)).order(ByteOrder.LITTLE_ENDIAN);
        byte[] m = new byte[4]; b.get(m);
        if (!new String(m).equals("W14T")) throw new IllegalArgumentException("not a trace");
        In in = new In(b);
        if (in.v() != 1) throw new IllegalArgumentException("version");
        java.io.ByteArrayOutputStream g = new java.io.ByteArrayOutputStream(1 << 24);
        while (b.hasRemaining()) {
            int k = b.get() & 0xFF;
            if (k == 'S') {
                first = (int) in.v(); nextFree = (int) in.v(); long n = in.v();
                for (long i = 0; i < n; i++) {
                    int a = (int) in.v(); long[] pg = new long[512];
                    if ((b.get() & 0xFF) == 1) for (int w = 0; w < 512; w++) pg[w] = in.raw();
                    pages0.put(a, pg);
                }
            } else if (k == 'N') icLines = (int) in.v();
            else if (k == 'D') defs.add(readDef(in));
            else if (k == 'G') { int s = b.position(); in.v(); in.z(); in.v(); g.write(b.array(), s, b.position() - s); }
            else if (k == 'E') { endRows = in.v(); endRecHash = in.raw(); break; }
            else throw new IllegalArgumentException("record " + k);
        }
        groups = g.toByteArray();
    }

    @SuppressWarnings("unchecked")
    static FileStore store() throws Exception {
        FileStore fs = new FileStore(first);
        Field pf = FileStore.class.getDeclaredField("pages"); pf.setAccessible(true);
        Field nf = FileStore.class.getDeclaredField("nextFree"); nf.setAccessible(true);
        Map<Integer, long[]> p = (Map<Integer, long[]>) pf.get(fs);
        for (var e : pages0.entrySet()) p.put(e.getKey(), e.getValue().clone());
        nf.setInt(fs, nextFree);
        return fs;
    }

    static long sink;
    static double[] pass(boolean calls, List<double[]> segs) throws Exception {
        FileStore fs = store();
        MemorySystem ms = new MemorySystem(fs);
        Tracer t = new Tracer(66);
        t.icacheLines = icLines; t.mem = ms; t.onStateWrite = ms::cold;
        In in = new In(ByteBuffer.wrap(groups).order(ByteOrder.LITTLE_ENDIAN));
        long nextT = 0, cycles = 0, ncalls = 0, segCycles = 0;
        long[] w2 = new long[2];
        long t0 = System.nanoTime(), segT0 = t0, edits = 0;
        while (in.p.hasRemaining()) {
            Op[] ops = defs.get((int) in.v());
            long g0 = nextT + in.z(), n = in.v();
            nextT = g0 + n;
            for (long i = 0; i < n; i++) {
                long tc = g0 + i;
                for (Op o : ops) {
                    long tt = tc + o.d;
                    if (!calls) { sink += tt ^ o.a; continue; }
                    switch (o.op) {
                        case 1 -> t.runCycle(tt);
                        case 2 -> t.run(tt);
                        case 3 -> t.sync(tt);
                        case 4 -> t.accept(tt, (int) o.a, (int) o.b, (int) o.c, o.lo, o.hi);
                        case 5 -> sink += t.writeWord(tt, (int) o.a, (int) o.b);
                        case 6 -> { Integer v = t.word(tt, (int) o.a); sink += v == null ? 0 : v; }
                        case 7 -> sink += t.running(tt) ? 1 : 0;
                        case 8 -> sink += t.errors(tt);
                        case 9 -> sink += t.events().value(tt);
                        case 10 -> t.events().consume((int) o.a);
                        case 11 -> sink += t.rowsEnded(tt) ? 1 : 0;
                        case 12 -> sink += t.writeBackDirtyUntimed();
                        case 13 -> t.outStart(tt, (int) o.a, (int) o.b);
                        case 14 -> sink += t.outReady(tt, (int) o.a) ? 1 : 0;
                        case 15 -> { t.outTake(tt, (int) o.a, w2); sink += w2[0]; }
                        case 16 -> {
                            long x = System.nanoTime();
                            if (segCycles > 0) segs.add(new double[] { segCycles, (x - segT0) / 1e9 });
                            List<Long> pg = new ArrayList<>(); for (long q : o.pages) pg.add(q);
                            fs.setOwnership(new FileStore.File((int) o.a, (int) o.b), pg);
                            segT0 = System.nanoTime(); edits += segT0 - x; segCycles = 0;
                        }
                        case 17 -> ms.cold();
                        case 18 -> Arrays.fill(t.stalls, 0);
                        default -> { }
                    }
                    ncalls++;
                }
                cycles++; segCycles++;
            }
        }
        long t1 = System.nanoTime();
        if (segCycles > 0) segs.add(new double[] { segCycles, (t1 - segT0) / 1e9 });
        if (calls) {
            long h = 17; for (long x : t.rec) h = h * 31 + x;
            if (t.rows != endRows || h != endRecHash) throw new IllegalStateException("the pass did not end where the log did (rows " + t.rows + ")");
        }
        return new double[] { (t1 - t0 - edits) / 1e9, cycles, ncalls };
    }

    public static void main(String[] a) throws Exception {
        long l0 = System.nanoTime();
        load(Path.of(a[0]));
        int reps = a.length > 1 ? Integer.parseInt(a[1]) : 3;
        System.out.printf("trace %s: groups %.1f MB, %d cycle kinds, store %d pages, loaded in %.2f s%n", a[0], groups.length / 1e6, defs.size(), pages0.size(), (System.nanoTime() - l0) / 1e9);
        double[] per = new double[reps];
        for (int k = 0; k < reps; k++) {
            List<double[]> segs = new ArrayList<>();
            double[] r = pass(true, segs);
            per[k] = 1e9 * r[0] / r[1];
            StringBuilder sb = new StringBuilder();
            for (double[] s : segs) sb.append(String.format("%.2f,", 1e9 * s[1] / s[0]));
            System.out.printf("rep %d cycles=%d calls=%d seconds=%.3f ns_per_cycle=%.2f segs=%s%n", k, (long) r[1], (long) r[2], r[0], per[k], sb);
        }
        double[] e = pass(false, new ArrayList<>());
        System.out.printf("harness cycles=%d seconds=%.3f ns_per_cycle=%.2f (decoding and dispatch, no Tracer call)%n", (long) e[1], e[0], 1e9 * e[0] / e[1]);
        double[] s = per.clone(); Arrays.sort(s);
        System.out.printf("TracerPlay median ns_per_cycle=%.2f of %d reps (sink %x)%n", s[s.length / 2], reps, sink);
    }
}
