import java.io.BufferedWriter;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * The Tracer's pure pieces against ref (WORKER14): ref's own Rvc, LruCoder, FileStore and MemorySystem, driven by a seeded
 * stimulus, every input and every result written as one event a line. The C++ side (tests/test_tracer_parts.cpp) reads
 * the inputs back, runs its ports and must reproduce every result. Compiled into ref's default package (D5); nothing in
 * sim/ref changes.
 *
 * Usage: java TracerParts rvc | lru SEED N | mem SEED N
 *
 * rvc: every 16-bit encoding: "h uncompress codeWord", hex.
 * lru: three coders (code, read log, write log) with a packer each; a seeded stream of values drawn so that hits, lowest-
 *   index duplicates (only after a reset to zeros), E[15] hits, 32- and 64-bit misses and packer boundaries all occur:
 *     e C V PEEK BITS       coder C (0 code, 1 rlog, 2 wlog) encodes V: peekBits before, the bits it took
 *     l V                   the write log's lastLoad(V)
 *     w C LO HI             a word coder C's packer emitted (after the e or f line that produced it)
 *     f C                   coder C's packer finishes (its w line follows if it had bits)
 *     r                     every coder and packer reset (a window's start)
 *     s C BITS HITS MISSES VALUES WORDS   coder C's statistics and its packer's word count
 * mem: an image built by FileStore's operations, then a seeded stream of fill-bus reads and writes:
 *     n FIRST               a FileStore(FIRST)
 *     c K DEPTH -> ROOT     file K = create(DEPTH)
 *     p K PG SEED           putPage(file K, PG, page(SEED))
 *     o K SRC -> ROOT       file K = cowOpen(file SRC)
 *     v K PG                makePrivate(file K, PG)
 *     O K -> CREATED PG...  setOwnership(file K, pages in the order given)
 *     m ROOT                the memory system made, mapRootForStats = ROOT
 *     R T K LINE -> ST READY H   a read of file K's LINE at T: status, readyAt, the data's hash
 *     W T K LINE SEED -> ACK      a write of line(SEED) at T: the acknowledgement's cycle
 *     C                     cold()
 *     S <counters>          every statistic, in MemorySystem's field order
 *     L K OFF -> V          load64(file K, OFF)
 */
public final class TracerParts {
    static PrintWriter out = new PrintWriter(new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16));

    public static void main(String[] a) {
        switch (a[0]) {
            case "rvc" -> rvc();
            case "lru" -> lru(Long.parseLong(a[1]), Integer.parseInt(a[2]));
            case "mem" -> mem(Long.parseLong(a[1]), Integer.parseInt(a[2]));
            default -> throw new IllegalArgumentException(a[0]);
        }
        out.flush();
    }

    static String hx(long v) { return Long.toHexString(v); }

    // ================================================================ rvc
    static void rvc() {
        for (int h = 0; h < 65536; h++) out.println(Integer.toHexString(h) + " " + Integer.toHexString(Rvc.uncompress(h)) + " " + Integer.toHexString(Rvc.codeWord(h)));
    }

    // ================================================================ lru
    static void lru(long seed, int n) {
        Random r = new Random(seed);
        LruCoder[] c = { new LruCoder(false), new LruCoder(false), new LruCoder(true) };
        List<String> held = new ArrayList<>();       // a packer's words, printed after the line that produced them
        LruCoder.Bits[] pk = new LruCoder.Bits[3];
        for (int q = 0; q < 3; q++) pk[q] = bits(q, held);
        long[] pool = new long[24];
        for (int i = 0; i < pool.length; i++) pool[i] = i < 8 ? r.nextInt() & 0xFFFFFFFFL : r.nextLong();
        pool[0] = 0; pool[1] = 0xFFFFFFFFL; pool[2] = 1L << 32;
        for (int i = 0; i < n; i++) {
            int k = r.nextInt(1000);
            if (k < 5) {
                for (int q = 0; q < 3; q++) { out.println("f " + q); pk[q].finish(); flush(held); stats(c[q], pk[q], q); }
                out.println("r");
                for (int q = 0; q < 3; q++) { c[q].reset(); pk[q] = bits(q, held); }
                continue;
            }
            if (k < 100) { long v = pick(r, pool); out.println("l " + hx(v)); c[2].lastLoad(v); continue; }
            int q = r.nextInt(3);
            long v = pick(r, pool);
            int peek = c[q].peekBits(v);
            int b = c[q].encode(v, pk[q]);
            out.println("e " + q + " " + hx(v) + " " + peek + " " + b);
            flush(held);
        }
        for (int q = 0; q < 3; q++) { out.println("f " + q); pk[q].finish(); flush(held); stats(c[q], pk[q], q); }
    }
    static LruCoder.Bits bits(int q, List<String> held) { return new LruCoder.Bits((lo, hi) -> held.add("w " + q + " " + hx(lo) + " " + hx(hi))); }
    static void flush(List<String> held) { for (String s : held) out.println(s); held.clear(); }

    static long pick(Random r, long[] pool) {
        int k = r.nextInt(10);
        if (k < 6) return pool[r.nextInt(pool.length)];
        if (k < 8) return r.nextInt() & 0xFFFFFFFFL;
        return r.nextLong();
    }
    static void stats(LruCoder c, LruCoder.Bits pk, int q) {
        out.println("s " + q + " " + c.bits + " " + c.hits + " " + c.misses + " " + c.values + " " + pk.words);
    }

    // ================================================================ mem
    static long[] page(long seed) { long[] p = new long[512]; for (int i = 0; i < 512; i++) p[i] = seed * 1000003L + i; return p; }
    static long[] line(long seed) { long[] p = new long[16]; for (int i = 0; i < 16; i++) p[i] = seed * 31L + i * 0x9E3779B97F4A7C15L; return p; }
    static long hash(long[] d) { long h = 17; for (long x : d) h = h * 31 + x; return h; }

    static void mem(long seed, int n) {
        Random r = new Random(seed);
        int first = 32 * (1 + r.nextInt(64));
        FileStore fs = new FileStore(first);
        out.println("n " + first);
        List<FileStore.File> f = new ArrayList<>();
        // file 0: the image, depth 2, pages in a few clusters (so leaves are shared and sets collide)
        FileStore.File img = fs.create(2); f.add(img); out.println("c 0 2 -> " + img.root());
        List<Long> pages = new ArrayList<>();
        int clusters = 3 + r.nextInt(3);
        for (int k = 0; k < clusters; k++) {
            long base = r.nextInt(1 << 20) & ~63L, len = 32 + r.nextInt(128);
            for (long p = base; p < base + len && p < (1L << 20); p++) if (r.nextInt(5) != 0) pages.add(p);
        }
        for (long p : pages) { long s = r.nextInt(1 << 20); fs.putPage(img, p, page(s)); out.println("p 0 " + p + " " + s); }
        // file 1: the jump map, depth 1, dense
        FileStore.File map = fs.create(1); f.add(map); out.println("c 1 1 -> " + map.root());
        int mapPages = 1 + r.nextInt(8);
        for (long p = 0; p < mapPages; p++) { long s = r.nextInt(1 << 20); fs.putPage(map, p, page(s)); out.println("p 1 " + p + " " + s); }
        // file 2: the Tracer's copy; some pages made private (written by the segment)
        FileStore.File cp = fs.cowOpen(img); f.add(cp); out.println("o 2 0 -> " + cp.root());
        for (long p : pages) if (r.nextInt(3) == 0) { fs.makePrivate(cp, p); out.println("v 2 " + p); }
        // file 3: a second copy, its ownership set by the one-memory rule (some written pages absent: created)
        FileStore.File cp2 = fs.cowOpen(img); f.add(cp2); out.println("o 3 0 -> " + cp2.root());
        List<Long> written = new ArrayList<>();
        for (long p : pages) if (r.nextInt(4) == 0) written.add(p);
        for (int k = 0; k < 3; k++) written.add((long) r.nextInt(1 << 20));
        int created = fs.setOwnership(cp2, written);
        StringBuilder sb = new StringBuilder("O 3 -> " + created);
        for (long p : written) sb.append(' ').append(p);
        out.println(sb);
        // file 4: depth 0
        FileStore.File d0 = fs.create(0); f.add(d0); out.println("c 4 0 -> " + d0.root());
        { long s = r.nextInt(1 << 20); fs.putPage(d0, 0, page(s)); out.println("p 4 0 " + s); }

        MemorySystem ms = new MemorySystem(fs);
        ms.mapRootForStats = map.root();
        out.println("m " + map.root());
        MemorySystem.Response resp = new MemorySystem.Response();
        FileStore.Walk w = new FileStore.Walk();
        long t = r.nextInt(1000);
        for (int i = 0; i < n; i++) {
            int k = r.nextInt(1000);
            if (k < 1) { ms.cold(); out.println("C"); continue; }
            if (k < 30) { stats(ms); continue; }
            int fi = r.nextInt(100) < 50 ? 2 : r.nextInt(100) < 60 ? 1 : r.nextInt(5);
            FileStore.File file = f.get(fi);
            long ln = pickLine(r, file, pages, mapPages);
            if (k < 60) { long off = ln * 128 + 8 * r.nextInt(16); out.println("L " + fi + " " + off + " -> " + hx(fs.load64(file, off))); continue; }
            // issue at the bus's free cycle or later
            long ft = ms.freeAt();
            long gap = r.nextInt(4) == 0 ? 0 : r.nextInt(120);
            t = Math.max(t + gap, ft) + (r.nextInt(3) == 0 ? 0 : r.nextInt(40));
            fs.walk(file.root(), file.depth(), ln, w);
            if (k < 450 && w.status == FileStore.ST_OK) {
                long s = r.nextInt(1 << 20);
                long ack = ms.write(t, file.root(), file.depth(), ln, line(s));
                out.println("W " + t + " " + fi + " " + ln + " " + s + " -> " + ack);
            } else {
                ms.read(t, file.root(), file.depth(), ln, resp);
                out.println("R " + t + " " + fi + " " + ln + " -> " + resp.status + " " + resp.readyAt + " " + hx(hash(resp.data)));
            }
        }
        stats(ms);
    }

    static long pickLine(Random r, FileStore.File file, List<Long> pages, int mapPages) {
        long cap = 1L << (5 + 10 * file.depth());
        int k = r.nextInt(100);
        if (k < 3) return cap + r.nextInt(100);                      // beyond
        if (k < 5) return cap - 1;
        if (file.depth() == 2 && k < 50) return pages.get(r.nextInt(pages.size())) * 32 + 3 + 4 * r.nextInt(3);   // a few sets: full, dirty victims
        if (file.depth() == 2 && k < 85) return pages.get(r.nextInt(pages.size())) * 32 + r.nextInt(32);
        if (file.depth() == 1 && k < 90) return (long) r.nextInt(mapPages) * 32 + r.nextInt(32);
        return (long) (r.nextDouble() * cap);
    }

    static void stats(MemorySystem m) {
        StringBuilder sb = new StringBuilder("S");
        for (long v : new long[] { m.reads, m.writes, m.beyond, m.indexAccesses, m.indexHits, m.dataAccesses, m.dataHits, m.lpddrReads,
                m.walkCycles, m.dataCycles, m.earlyEvictions, m.readWaits, m.readWaitCycles, m.writeAllocates }) sb.append(' ').append(v);
        for (long[] arr : new long[][] { m.acc, m.hits, m.cycles }) for (long v : arr) sb.append(' ').append(v);
        out.println(sb);
    }
}
