import java.util.HashMap;

/**
 * The memory as files (DECISIONS-v1.3 sections 12, 20, 22; spec 4.21.5, 7): a simulated LPDDR of 4 KB pages addressed in
 * 128 B units, holding files as trees of 1,024-entry index pages whose depth follows the length (0: the data page itself;
 * 1: an index page of data pages; 2 and 3 more levels). An index entry is 32 bits: [30:0] the page's address in 128 B
 * units, [31] the owned bit; 0xFFFFFFFF is no page (a hole), at every level.
 *
 * <p>The black box's storage only (WORKER22; owner, 2026-11-05): the shared cache's timing is {@link MemorySystem}'s. It
 * builds an image as one file that owns everything, makes the Tracer's copy by {@code cow_open} (a new root, its entries'
 * owned bits cleared) and pre-copies the segment's written pages by path copying (section 20: a shared index page copied
 * keeps its entries' owned bits 0, since its children are still shared), so a shared index page below a copy holds
 * entries that read owned = 1 relative to the source: the case SPEC-QUESTIONS Q1 rules on (ok only if every entry on the
 * path from the root is owned).
 */
final class FileStore {
    static final int PAGE_UNITS = 32;                  // 4 KB in 128 B units
    static final int NONE = 0xFFFFFFFF;
    static final int OWNED = 0x80000000;
    static final int ST_OK = 0, ST_HOLE = 1, ST_NOT_OWNED = 2, ST_BEYOND = 3;

    private final HashMap<Integer, long[]> pages = new HashMap<>();      // page address (128 B units, a multiple of 32) -> 512 longs
    private int nextFree;

    /** first: the first page's address in 128 B units (address 0 is the superblock's, so tests start above it). */
    FileStore(int first) { if (first % PAGE_UNITS != 0) throw new IllegalArgumentException("unaligned"); nextFree = first; }
    FileStore() { this(PAGE_UNITS); }

    int pagesAllocated() { return pages.size(); }

    // ---------------------------------------------------------------- pages
    private int alloc(boolean index) {
        int a = nextFree; nextFree += PAGE_UNITS;
        if (nextFree < 0) throw new IllegalStateException("out of 31-bit addresses");
        long[] p = new long[512];
        if (index) java.util.Arrays.fill(p, -1L);      // every entry 0xFFFFFFFF: a zeroed entry would point at address 0
        pages.put(a, p);
        return a;
    }
    private long[] page(int a) {
        long[] p = pages.get(a);
        if (p == null) throw new IllegalStateException("no page at 0x" + Integer.toHexString(a));
        return p;
    }
    static int entryOf(long[] index, int i) { return (int) (index[i >>> 1] >>> (32 * (i & 1))); }
    static void setEntry(long[] index, int i, int e) {
        int s = 32 * (i & 1);
        index[i >>> 1] = index[i >>> 1] & ~(0xFFFFFFFFL << s) | (e & 0xFFFFFFFFL) << s;
    }
    static int addr(int e) { return e & 0x7FFFFFFF; }

    /** A file: its root (128 B units) and depth (0-3). */
    record File(int root, int depth) {
        /** The depth's capacity in 128 B lines (status 3 beyond it, SPEC-QUESTIONS Q2). */
        long capacityLines() { return 1L << (5 + 10 * depth); }
    }

    /** The depth a file of `bytes` needs (section 20): 0 to 4 KB, 1 to 4 MB, 2 to 4 GB, 3 to 4 TB. */
    static int depthFor(long bytes) {
        for (int d = 0; d <= 3; d++) if (bytes <= (4096L << (10 * d))) return d;
        throw new IllegalArgumentException("too long: " + bytes);
    }

    /** A new, empty file of the given depth that owns its root (depth 0: one data page). */
    File create(int depth) { return new File(alloc(depth > 0), depth); }

    /** The page-index digits of page number `pg` (offset / 4 KB), root level first. */
    private static int digit(long pg, int level, int depth) { return (int) (pg >>> (10 * (depth - 1 - level))) & 1023; }

    /**
     * Writes a whole 4 KB page into file f at page number pg as its creator (every entry on the path owned), allocating
     * index pages and the data page as needed. For building images, before any copy shares them.
     */
    void putPage(File f, long pg, long[] data) {
        if (data.length != 512) throw new IllegalArgumentException("a page is 512 longs");
        if (f.depth == 0) { if (pg != 0) throw new IllegalArgumentException("depth 0 has one page"); System.arraycopy(data, 0, page(f.root), 0, 512); return; }
        if (pg >= (1L << (10 * f.depth))) throw new IllegalArgumentException("page " + pg + " beyond depth " + f.depth);
        int a = f.root;
        for (int lv = 0; lv < f.depth; lv++) {
            long[] ix = page(a);
            int i = digit(pg, lv, f.depth), e = entryOf(ix, i);
            if (e == NONE) { e = alloc(lv < f.depth - 1) | OWNED; setEntry(ix, i, e); }
            else if ((e & OWNED) == 0) throw new IllegalStateException("putPage through a shared entry: use writePage on a copy");
            a = addr(e);
        }
        System.arraycopy(data, 0, page(a), 0, 512);
    }

    /** section 20's cow_open: a new file sharing everything with `src` but its root, copied with the owned bits cleared. */
    File cowOpen(File src) {
        if (src.depth == 0) throw new IllegalArgumentException("cow_open of a depth-0 file: nothing to share");
        int r = alloc(true);
        long[] from = page(src.root), to = page(r);
        for (int i = 0; i < 1024; i++) { int e = entryOf(from, i); setEntry(to, i, e == NONE ? NONE : e & ~OWNED); }
        return new File(r, src.depth);
    }

    /**
     * Makes page pg of copy f private by path copying (section 20's write to a file that isn't frozen): every entry on
     * the path with owned 0 has its page copied into a new page, the entry pointing at the copy with owned 1; a copied
     * index page's entries are written with owned 0 (its children are still shared). A hole on the path is an error:
     * the Tracer's copy pre-copies only pages that exist (frame 1's counts, section 22). Returns nothing; the page's
     * content is unchanged (the pre-copy of section 22).
     */
    void makePrivate(File f, long pg) {
        int a = f.root;
        for (int lv = 0; lv < f.depth; lv++) {
            long[] ix = page(a);
            int i = digit(pg, lv, f.depth), e = entryOf(ix, i);
            if (e == NONE) throw new IllegalStateException("pre-copying a hole at page " + pg);
            if ((e & OWNED) == 0) {
                boolean index = lv < f.depth - 1;
                int c = alloc(index);
                long[] src = page(addr(e)), dst = page(c);
                if (index) for (int k = 0; k < 1024; k++) { int x = entryOf(src, k); setEntry(dst, k, x == NONE ? NONE : x & ~OWNED); }
                else System.arraycopy(src, 0, dst, 0, 512);
                e = c | OWNED; setEntry(ix, i, e);
            }
            a = addr(e);
        }
    }

    // ---------------------------------------------------------------- the walk (the translator's, spec 7, 4.21.5)
    /** One walk of (root, depth, line): the LPDDR lines it reads (index lines, then the data line), its status, the line. */
    static final class Walk {
        final int[] lines = new int[4]; int nLines;     // LPDDR line addresses (128 B units), in access order
        int indexLines;                                 // how many of them are index lines
        int status; int dataLine = -1;                  // the data line's address, or -1 (a hole, or beyond)
    }

    /** Walks f's tree to `line` (4.21.5's statuses): ok only if every entry on the path is owned (Q1); a hole at any
     *  level stops the walk (the line reads as zero); beyond the depth's capacity is status 3 at once (Q2). */
    Walk walk(int root, int depth, long line, Walk w) {
        w.nLines = 0; w.indexLines = 0; w.dataLine = -1;
        if (line < 0 || line >= (1L << (5 + 10 * depth))) { w.status = ST_BEYOND; return w; }
        long pg = line >>> 5;
        int a = root; boolean owned = true;
        for (int lv = 0; lv < depth; lv++) {
            int i = digit(pg, lv, depth);
            w.lines[w.nLines++] = a + (i >>> 5); w.indexLines++;        // the index line holding entry i (32 entries a line)
            int e = entryOf(page(a), i);
            if (e == NONE) { w.status = ST_HOLE; return w; }
            if ((e & OWNED) == 0) owned = false;
            a = addr(e);
        }
        w.dataLine = a + (int) (line & 31);
        w.lines[w.nLines++] = w.dataLine;
        w.status = owned ? ST_OK : ST_NOT_OWNED;
        return w;
    }

    /** The 16 longs of an LPDDR line (a data line found by a walk). */
    void readLine(int lineAddr, long[] out) { System.arraycopy(page(lineAddr & ~31), 16 * (lineAddr & 31), out, 0, 16); }
    void writeLine(int lineAddr, long[] in) { System.arraycopy(in, 0, page(lineAddr & ~31), 16 * (lineAddr & 31), 16); }

    /** Convenience for tools and tests: the doubleword at byte offset `off` of file f (0 in a hole). */
    long load64(File f, long off) {
        Walk w = walk(f.root, f.depth, off >>> 7, new Walk());
        if (w.dataLine < 0) return 0;
        return page(w.dataLine & ~31)[16 * (w.dataLine & 31) + (int) ((off >>> 3) & 15)];
    }
}
