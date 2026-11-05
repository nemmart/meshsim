/**
 * The Tracer's black-box memory system (WORKER22, B1; spec 4.21.5, 7; DECISIONS-v1.3 sections 20-22; SPEC-QUESTIONS Q1,
 * Q2, Q12, Q13):
 *  - FileStore: depths, holes reading zero, cow_open and path copying, the four statuses (ok only with every entry on the
 *    path owned, Q1; beyond the depth's capacity, Q2), a dense file of depth 1;
 *  - the shared cache's timing: a cold depth-2 read (three misses), warm reads, an index line shared by 32 pages, the set
 *    mapping and LRU replacement, the free-or-clean victim policy with its early evictions, the write buffer draining one
 *    line per 80 cycles and a read miss waiting for it to empty, a write-allocate without an LPDDR read, cold().
 * Usage: java MemorySystemTest
 */
public final class MemorySystemTest {
    public static void main(String[] args) {
        statuses();
        timing();
        policy();
        System.exit(T.done("MemorySystemTest"));
    }

    static long[] page(long seed) { long[] p = new long[512]; for (int i = 0; i < 512; i++) p[i] = seed * 1000003L + i; return p; }

    static void statuses() {
        T.group("FileStore: statuses");
        FileStore fs = new FileStore();
        FileStore.File img = fs.create(2);
        // pages 0, 1 (same leaf index page), 5000 (another leaf: 5000 >> 10 = 4), and 1<<20 - 1 (the last of depth 2)
        long[] pgs = { 0, 1, 5000, (1L << 20) - 1 };
        for (long p : pgs) fs.putPage(img, p, page(p + 7));
        FileStore.Walk w = new FileStore.Walk();
        T.eq(FileStore.ST_OK, fs.walk(img.root(), 2, 0, w).status, "the image owns every path");
        T.eq(3, w.nLines, "a depth-2 walk reads two index lines and the data line");
        T.eq(2, w.indexLines, "two of them index lines");
        T.eq(FileStore.ST_HOLE, fs.walk(img.root(), 2, 2 * 32, w).status, "page 2 is a hole (an entry of the existing leaf)");
        T.eq(FileStore.ST_HOLE, fs.walk(img.root(), 2, 3000L * 32, w).status, "page 3000's leaf index page is a hole (stops a level up)");
        T.eq(1, w.nLines, "a hole at the root's entry: one index line read");
        T.eq(0, fs.load64(img, 2 * 4096 + 8), "a hole reads as zero");
        T.eq(page(5007)[17], fs.load64(img, 5000L * 4096 + 17 * 8), "the doubleword at an offset");
        T.eq(FileStore.ST_BEYOND, fs.walk(img.root(), 2, 1L << 25, w).status, "beyond the depth's capacity (Q2): status 3");
        T.eq(0, w.nLines, "status 3 reads nothing");
        T.eq(FileStore.ST_OK, fs.walk(img.root(), 2, (1L << 25) - 1, w).status, "the capacity's last line");

        T.group("FileStore: cow_open and path copying (Q1)");
        FileStore.File cp = fs.cowOpen(img);
        fs.makePrivate(cp, 1);                                          // the segment writes page 1
        T.eq(FileStore.ST_OK, fs.walk(cp.root(), 2, 1 * 32 + 5, w).status, "a pre-copied page: every entry on its path owned");
        T.eq(FileStore.ST_NOT_OWNED, fs.walk(cp.root(), 2, 0, w).status, "page 0, beside it in the now-private leaf: not owned");
        T.eq(FileStore.ST_NOT_OWNED, fs.walk(cp.root(), 2, 5000L * 32, w).status,
                "page 5000, under a shared leaf whose entry reads owned = 1 in the source: not owned (Q1: the path, not the leaf)");
        T.eq(FileStore.ST_HOLE, fs.walk(cp.root(), 2, 2 * 32, w).status, "a hole stays a hole in the copy");
        T.eq(page(8)[3], fs.load64(cp, 4096 + 24), "the copy reads the same data");
        long[] line = new long[16]; java.util.Arrays.fill(line, 0x5A5AL);
        fs.walk(cp.root(), 2, 32, w); fs.writeLine(w.dataLine, line);
        T.eq(0x5A5AL, fs.load64(cp, 4096), "a write to the copy's private page");
        T.eq(page(8)[0], fs.load64(img, 4096), "leaves the source unchanged");
        T.throwsISE(() -> fs.makePrivate(cp, 2), "pre-copying a hole is refused");

        T.group("FileStore: a dense depth-1 file and depth 0");
        FileStore.File map = fs.create(FileStore.depthFor(4L << 20));
        T.eq(1, map.depth(), "4 MB is depth 1");
        for (int p = 0; p < 1024; p++) fs.putPage(map, p, page(p));
        T.eq(FileStore.ST_OK, fs.walk(map.root(), 1, 1023L * 32 + 31, w).status, "the last line");
        T.eq(FileStore.ST_BEYOND, fs.walk(map.root(), 1, 1024L * 32, w).status, "one past it: beyond (Q2)");
        T.eq(2, w.nLines + 2, "nothing read beyond");
        FileStore.File d0 = fs.create(0); fs.putPage(d0, 0, page(9));
        T.eq(FileStore.ST_OK, fs.walk(d0.root(), 0, 31, w).status, "depth 0: the page itself");
        T.eq(1, w.nLines, "depth 0: the data line only");
        T.eq(FileStore.ST_BEYOND, fs.walk(d0.root(), 0, 32, w).status, "depth 0 holds 32 lines");
        T.eq(0, FileStore.depthFor(4096), "4 KB is depth 0");
        T.eq(2, FileStore.depthFor(4L << 30), "4 GB is depth 2");
        T.eq(3, FileStore.depthFor((4L << 30) + 1), "above 4 GB is depth 3");
    }

    static void timing() {
        T.group("the shared cache: timing");
        FileStore fs = new FileStore();
        FileStore.File img = fs.create(2);
        for (long p = 0; p < 64; p++) fs.putPage(img, p, page(p));
        MemorySystem ms = new MemorySystem(fs);
        MemorySystem.Response r = new MemorySystem.Response();
        ms.read(100, img.root(), 2, 0, r);
        T.eq(100 + 3 * 80, r.readyAt, "cold: two index lines and the data line, each from LPDDR");
        T.eq(page(0)[0], r.data[0], "the line's data");
        T.eq(FileStore.ST_OK, r.status, "status ok");
        T.check(!ms.idle(r.readyAt - 1), "one request outstanding: busy until its data");
        T.throwsISE(() -> ms.read(r.readyAt - 1, img.root(), 2, 1, new MemorySystem.Response()), "a second request while busy is refused");
        long t = r.readyAt;
        ms.read(t, img.root(), 2, 0, r);
        T.eq(t + 3 * 6, r.readyAt, "warm: three hits");
        t = r.readyAt; ms.read(t, img.root(), 2, 1, r);
        T.eq(t + 6 + 6 + 80, r.readyAt, "the next line of the page: index lines hit, the data line misses");
        t = r.readyAt; ms.read(t, img.root(), 2, 31L * 32, r);
        T.eq(t + 6 + 6 + 80, r.readyAt, "page 31: the same leaf index line as page 0 (32 entries a line)");
        t = r.readyAt; ms.read(t, img.root(), 2, 40L * 32, r);
        T.eq(t + 6 + 80 + 80, r.readyAt, "page 40: the next leaf index line (pages 32-63), a miss");
        T.eq(160 + 12 + 12 + 12 + 86, ms.walkCycles, "the walk's cycles");
        ms.read(r.readyAt, img.root(), 2, 3L * 32, r);
        T.eq(FileStore.ST_OK, r.status, "page 3 ok");
        ms.read(r.readyAt, img.root(), 2, 70L * 32, r);
        T.eq(FileStore.ST_HOLE, r.status, "page 70: a hole");
        T.eq(0, r.data[5], "reads zeros");
        long b0 = r.readyAt; ms.read(b0, img.root(), 2, 1L << 25, r);
        T.eq(FileStore.ST_BEYOND, r.status, "beyond: status 3");
        T.eq(b0 + 1, r.readyAt, "answered at once");
        ms.cold();
        ms.read(1000, img.root(), 2, 0, r);
        T.eq(1000 + 240, r.readyAt, "cold() empties the cache");
        T.eq(1, ms.reads, "and clears the statistics");
    }

    static void policy() {
        T.group("the shared cache: the free-or-clean victim policy and the write buffer (Q12, Q13)");
        // a depth-0 file per line address is awkward; use one depth-2 file and pick data lines in one set: line addresses
        // a ≡ s (mod 512). Pages are allocated 32 units apart, so data lines of page p at unit P+l; choose lines of
        // pages whose units are 512 apart (16 pages), same l.
        FileStore fs = new FileStore(32 * 1024);                         // pages from unit 32768 (set 0 aligned)
        FileStore.File img = fs.create(2);                               // root at 32768, the leaf next, then data pages
        int pagesN = 16 * 12;
        for (long p = 0; p < pagesN; p++) fs.putPage(img, p, page(p));
        MemorySystem ms = new MemorySystem(fs);
        MemorySystem.Response r = new MemorySystem.Response();
        FileStore.Walk w = new FileStore.Walk();
        // the data line 3 of page p: its set
        int set0 = fs.walk(img.root(), 2, 3, w).dataLine & 511;
        int[] inSet = new int[12]; int n = 0;
        for (long p = 0; p < pagesN && n < 12; p++) if ((fs.walk(img.root(), 2, p * 32 + 3, w).dataLine & 511) == set0) inSet[n++] = (int) p;
        T.eq(12, n, "twelve data lines in one set");
        long t = 0;
        long[] line = new long[16];
        // write 8 lines into the set (write-allocates: their index lines read first)
        for (int k = 0; k < 8; k++) { t = ms.read(t, img.root(), 2, inSet[k] * 32L + 3, r).readyAt; t = ms.write(t, img.root(), 2, inSet[k] * 32L + 3, line); }
        T.eq(0, ms.writeAllocates, "a write after a read of the line hits it in place");
        T.eq(1, ms.earlyEvictions, "the set full with its LRU line dirty: evicted at once (the 8th write ages line 0)");
        long before = ms.dataAccesses - ms.dataHits;
        t = ms.read(t, img.root(), 2, inSet[8] * 32L + 3, r).readyAt;
        T.eq(before + 1, ms.dataAccesses - ms.dataHits, "a ninth line misses and fills the free way (no dirty victim)");
        T.eq(2, ms.earlyEvictions, "the set full again with its LRU line dirty: that one evicted too");
        T.check(ms.readWaits >= 1, "the read miss waited for the write buffer to empty");
        // a write-allocate: a line never read, written whole
        long wa = ms.writeAllocates;
        long t0 = t; t = ms.write(t, img.root(), 2, inSet[9] * 32L + 3, line);
        T.eq(wa + 1, ms.writeAllocates, "a Tracer write-back missing the cache allocates without an LPDDR read (Q13)");
        T.eq(t0 + 6 + 6 + 6, t, "its index lines hit and its data line costs a hit");
        // the buffer drains one line per 80 cycles; a read miss waits only until it is empty
        MemorySystem m2 = new MemorySystem(fs);
        long u = 0;
        for (int k = 0; k < 8; k++) { u = m2.read(u, img.root(), 2, inSet[k] * 32L + 3, r).readyAt; u = m2.write(u, img.root(), 2, inSet[k] * 32L + 3, line); }
        long wait0 = m2.readWaitCycles;
        long u1 = m2.read(u, img.root(), 2, inSet[10] * 32L + 3, r).readyAt;
        T.check(m2.readWaitCycles - wait0 <= 80, "one buffered line: the wait is at most one drain (80)");
        T.eq(u1, r.readyAt, "the response's cycle");
    }
}
