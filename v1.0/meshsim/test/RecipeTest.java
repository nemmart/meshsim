import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The recipe book's tests (sw/RECIPES.md, sw/tests/rcp_*.c and rcp.h). Every recipe runs on all 64 tiles and is
 * checked bit-exactly, with no invariant violations (or exactly the expected ones), and its cycles are reported. The
 * snippet check makes every code block in RECIPES.md equal to the RECIPE ... END region it names, so the book can't
 * drift from the code that runs. Usage: {@code java RecipeTest [shuffleSeed]}.
 */
public final class RecipeTest {
    static long shuffle = -1;
    static final List<String> table = new ArrayList<>();
    static void out(String s) { System.out.println(s); }
    static void meas(String recipe, String what, long cycles) { table.add(String.format("  %-14s %-72s %8d", recipe, what, cycles)); }
    static void meas(String recipe, String what, String cycles) { table.add(String.format("  %-14s %-72s %8s", recipe, what, cycles)); }

    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        snippets();
        cmdloop(); copy(); cmem(); bucket(); aug(); gather(); xchg(); xvec(); a2a(); reduce(); rdport(); status(); rules(); sync();
        blocks(); selftest(); reportRecipe();
        out("recipe cycles [meas: RecipeTest; tile 0 unless stated]:");
        for (String s : table) out(s);
        System.exit(T.done("RecipeTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    // ================================================================ helpers
    static Mach mach() { return new Mach(Mach.cfg(shuffle)); }
    static void noViolations(Chip c, String what) {
        T.check(c.violations == 0, what + ": no invariant violations" + (c.violations == 0 ? "" : "\n" + c.violationReport()));
    }
    static void finish(Mach m, String what, long max) {
        m.c.cc.command(LinkWord.BROADCAST, 0).go();
        boolean ok = m.run(m::allStopped, max);
        T.check(ok && m.allEbreak(), what + ": every tile ran to its exit: " + m.haltSummary());
    }
    /** Cycles from command k's GO (0 = the first command after boot) to all 64 READY after it. */
    static long goToReady(Mach m, int k) { List<long[]> ph = m.c.cc.phaseLog; return ph.get(k + 1)[0] - ph.get(k)[1]; }
    /** A synchronised start (spec 6.4): from C&C's GO store (phase k) to each tile's start store and first request.
     *  Returns {min, max} over tiles of the first request, and prints the breakdown. */
    static long[] goToFirstRequest(Mach m, int k, String recipe, String run) {
        long go = m.c.cc.phaseLog.get(k)[1];
        long fmin = Long.MAX_VALUE, fmax = 0, smin = Long.MAX_VALUE, smax = 0, lead = -1; boolean sameLead = true;
        for (Tile p : m.c.tiles) {
            long f = p.ag.firstRequestAt - go, st = p.ag.startedAt - 1 - go, l = p.ag.firstRequestAt - (p.ag.startedAt - 1);
            fmin = Math.min(fmin, f); fmax = Math.max(fmax, f); smin = Math.min(smin, st); smax = Math.max(smax, st);
            if (lead < 0) lead = l; else if (lead != l) sameLead = false;
        }
        T.check(sameLead, recipe + ": the lead-in is the same on every tile (" + lead + " cycles from the start store)");
        T.check(fmax - fmin <= 1, recipe + ": every tile's first request within the 0/+1 start skew (" + fmin + ".." + fmax + ")");
        meas(recipe, run + ": GO store -> start store, every tile", smin + (smax > smin ? "-" + smax : ""));
        meas(recipe, run + ": start store -> first request (the lead-in)", lead);
        meas(recipe, run + ": GO store -> first request, every tile", fmin + (fmax > fmin ? "-" + fmax : ""));
        return new long[] { fmin, fmax };
    }
    static boolean idle(Mach m) { return !m.c.cc.busy() && tilesIdle(m); }
    /** Every tile back in wait_for_command() since the last GO (C&C's own state aside: usable inside a C&C step). */
    static boolean tilesIdle(Mach m) {
        if (!m.allInWfi()) return false;
        for (Tile p : m.c.tiles) if (!p.ctl.done || p.ctl.lastDoneAt <= m.c.cc.lastGoAt) return false;
        return true;
    }
    static long rec(Tile p, int chunk) { return p.sram.peek64(chunk); }
    static long rec(int lo, int hi) { return (lo & 0xFFFFFFFFL) | (long) hi << 32; }
    static long sk(Mach m, Tile p) { return m.c.cfg.goLatency + m.c.startSkew[p.idx]; }

    // ================================================================ the snippet check
    static final Pattern MD = Pattern.compile("<!-- recipe: (\\S+) \\((\\S+)\\) -->\\s*```(?:c|java)\\n(.*?)```", Pattern.DOTALL);
    static final Pattern SRC = Pattern.compile("/\\* RECIPE (\\S+) \\*/\\n(.*?)\\n[ \\t]*/\\* END \\*/", Pattern.DOTALL);
    static String norm(String s) {
        String[] l = s.replaceAll("[ \\t]+\\n", "\n").stripTrailing().split("\n", -1);
        int ind = Integer.MAX_VALUE;
        for (String x : l) if (!x.isBlank()) ind = Math.min(ind, x.length() - x.stripLeading().length());
        StringBuilder sb = new StringBuilder();
        for (String x : l) sb.append(x.isBlank() ? "" : x.substring(ind)).append('\n');
        return sb.toString().strip();
    }
    static void snippets() {
        T.group("snippets");
        try {
            String md = Files.readString(Registers.doc("RECIPES.md"));
            Map<String, String> src = new LinkedHashMap<>();
            Map<String, String> file = new LinkedHashMap<>();
            try (var files = Files.list(Path.of("sw/tests"))) {
                List<Path> all = new ArrayList<>(files.sorted().toList());
                all.add(Path.of("src/CnC.java"));                  // The C&C side of REPORT
                for (Path f : all) {
                    String n = f.getFileName().toString();
                    if (!(n.startsWith("rcp") && (n.endsWith(".c") || n.endsWith(".h")) || n.equals("selftest.h") || n.equals("CnC.java"))) continue;
                    Matcher x = SRC.matcher(Files.readString(f));
                    while (x.find()) { T.check(!src.containsKey(x.group(1)), "recipe " + x.group(1) + " marked once"); src.put(x.group(1), norm(x.group(2))); file.put(x.group(1), n); }
                }
            }
            Matcher x = MD.matcher(md);
            int n = 0; java.util.Set<String> seen = new java.util.HashSet<>();
            while (x.find()) {
                n++;
                String name = x.group(1);
                T.check(src.containsKey(name), "RECIPES.md's block " + name + " has a RECIPE region in the sources");
                if (!src.containsKey(name)) continue;
                T.eqo(file.get(name), x.group(2), "block " + name + ": the file named is the one that holds it");
                T.eqo(src.get(name), norm(x.group(3)), "block " + name + ": RECIPES.md's text equals sw/tests/" + file.get(name));
                seen.add(name);
            }
            for (String s : src.keySet()) T.check(seen.contains(s), "RECIPE region " + s + " (" + file.get(s) + ") is quoted in RECIPES.md");
            out("snippets: " + n + " code blocks in RECIPES.md, " + src.size() + " RECIPE regions in sw/tests/rcp*.{c,h}, selftest.h and src/CnC.java");
        } catch (IOException e) { T.check(false, "snippets: " + e); }
    }

    // ================================================================ 1 boot and the command loop
    static void cmdloop() {
        T.group("cmdloop");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_cmdloop");
        int[] a31 = new int[31]; int s31 = 0; for (int i = 0; i < 31; i++) { a31[i] = 1000 + i; s31 += a31[i]; }
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 3, 10, 20, 30).go();
        for (int t = 0; t < 64; t++) m.c.cc.command(t, 1 << 24 | 2, t * 100, 5);
        m.c.cc.go().command(LinkWord.BROADCAST, 1 << 24 | 31, a31).go();
        m.c.cc.command(LinkWord.BROADCAST, 2 << 24).go();
        boolean ok = m.run(m::allStopped, 20000);
        T.check(ok && m.allEbreak(), "every tile ran the command loop to CMD_EXIT: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            int me = p.idx;
            T.eq(60 + me, m.word(me, im, "res", 0), "tile " + me + ": broadcast, even block, 3 arguments");
            T.eq(me * 100 + 5 + me, m.word(me, im, "res", 1), "tile " + me + ": unicast, odd block, its own arguments");
            T.eq(s31 + me, m.word(me, im, "res", 2), "tile " + me + ": broadcast, even block, 31 arguments");
            T.eq(3, m.word(me, im, "ncmd", 0), "tile " + me + ": three sums");
        }
        noViolations(m.c, "cmdloop");
        meas("cmdloop", "a 3-argument command: GO -> all 64 READY", goToReady(m, 0));
        meas("cmdloop", "a 31-argument command: GO -> all 64 READY", goToReady(m, 2));
    }

    // ================================================================ 2 a stream run: local copy
    static void copy() {
        T.group("copy");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_copy");
        for (Tile p : m.c.tiles) for (int i = 0; i < 100 + p.idx; i++) p.sram.poke64(2 * i, rec(p.idx << 20 | i, ~i));
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 100).go();
        finish(m, "copy", 30000);
        for (Tile p : m.c.tiles) {
            int bad = 0;
            for (int i = 0; i < 100 + p.idx; i++) {
                long e = rec(p.idx << 20 | i, ~i);
                if (rec(p, 0x40000 + 2 * i) != e || rec(p, 0x80000 + 2 * i) != e) bad++;
            }
            T.eq(0, bad, "tile " + p.idx + ": " + (100 + p.idx) + " records copied SRC -> DST -> DST2");
            T.eq(0, p.sram.peek64(0x40000 + 2 * (100 + p.idx)), "tile " + p.idx + ": nothing past the run");
        }
        noViolations(m.c, "copy");
        Tile p = m.c.tiles[0];
        List<long[]> w = p.core.wfiLog;
        meas("copy", "100 records (tile 0): start store -> first instruction after WFI", w.get(w.size() - 1)[1] - p.ag.startedAt + 1);
    }

    // ================================================================ 3 block copy into controller memory
    static void cmem() {
        T.group("cmem");
        Mach m = mach();
        int[] ov = new int[256];
        ov[0] = 0x00a50533; ov[1] = 0x12350513; ov[2] = 0x00008067;          // add a0, a0, a0; addi a0, a0, 0x123; ret
        for (int i = 3; i < 256; i++) ov[i] = 0x5A5A0000 + i;
        for (Tile p : m.c.tiles) {
            for (int i = 0; i < 16; i++) p.sram.poke(0x20000 + i, p.idx * 1000 + i);
            for (int i = 0; i < 256; i++) p.sram.poke(0x21000 + i, ov[i]);
        }
        Mach.Image im = m.boot("rcp_cmem");
        T.check(im.code.length * 4 <= 0x1800, "the program's code lies below the overlay at 0x1800");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();
        finish(m, "cmem", 20000);
        for (Tile p : m.c.tiles) {
            int me = p.idx, s = 0; for (int i = 0; i < 16; i++) s += me * 1000 + i;
            T.eq(s, m.word(me, im, "par_sum", 0), "tile " + me + ": 16 parameter words in data memory");
            boolean match = true; for (int i = 0; i < 256; i++) if (p.core.peekLocal(0x1800 + 4 * i) != ov[i]) match = false;
            T.check(match, "tile " + me + ": 256 overlay words in instruction memory");
            T.eq(2 * (me * 1000) + 0x123, m.word(me, im, "res", 0), "tile " + me + ": the overlay ran on params[0]");
        }
        noViolations(m.c, "cmem");
        List<long[]> w = m.c.tiles[0].core.wfiLog;
        meas("cmem", "256 words into instruction memory: start store -> wake", w.get(w.size() - 1)[1] - m.c.tiles[0].ag.startedAt + 1);
    }

    // ================================================================ 4 histogram digit, prefix sum, bucket passes
    static void bucket() {
        T.group("bucket");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_bucket");
        long[][] in = new long[64][];
        for (Tile p : m.c.tiles) {
            Random r = new Random(1000 + p.idx);
            int n = 200 + p.idx;
            in[p.idx] = new long[n];
            for (int i = 0; i < n; i++) { int key = p.idx % 8 == 0 ? r.nextInt(16) * 0x01010101 : r.nextInt(); in[p.idx][i] = rec(key, i); p.sram.poke64(2 * i, in[p.idx][i]); }
        }
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 200).go();
        finish(m, "bucket", 200000);
        for (Tile p : m.c.tiles) {
            long[] e = in[p.idx].clone();
            Long[] b = Arrays.stream(e).boxed().toArray(Long[]::new);
            Arrays.sort(b, (x, y) -> Integer.compareUnsigned((int) (long) x, (int) (long) y));   // stable: ties keep input order
            int bad = 0;
            for (int i = 0; i < b.length; i++) if (rec(p, 0x40000 + 2 * i) != b[i]) bad++;
            T.eq(0, bad, "tile " + p.idx + ": " + b.length + " records sorted by the 32-bit key, stable (values are input positions)");
            T.eq(Registers.STAGE_CHUNKS == 0 ? 0 : 0x100000 - Registers.STAGE_CHUNKS, p.ts.peek(TableStore.PTR + 64), "tile " + p.idx + ": PTR[64] restored to stage_base()");
            int h = 0; for (int t = 0; t < 256; t++) h += p.hist.peek(t);
            T.eq(0, h, "tile " + p.idx + ": HIST clear at the end");
        }
        noViolations(m.c, "bucket");
        meas("bucket", "200-263 records: count copy + 4 bucket passes + PTR[64]: GO -> all READY", goToReady(m, 0));
    }

    // ================================================================ 5 augmented transfers
    static int nAug(int t) { return 20 + ((t * 3) & 15); }
    static void aug() {
        T.group("aug");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_aug");
        for (Tile p : m.c.tiles) {
            for (int i = 0; i < 64 + p.idx; i++) p.sram.poke(i, 0x7000_0000 + p.idx * 0x1000 + i * 3);
            for (int i = 0; i < nAug(p.idx); i++) p.sram.poke64(0x20000 + 2 * i, rec(p.idx << 16 | i, 0xDEAD0000 | i));
        }
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 64).go()
              .command(LinkWord.BROADCAST, 2 << 24).go()
              .command(LinkWord.BROADCAST, 3 << 24).go();
        finish(m, "aug", 30000);
        for (Tile p : m.c.tiles) {
            int me = p.idx, bad = 0;
            for (int i = 0; i < 64 + me; i++) if (rec(p, 0x40000 + 2 * i) != rec(0x7000_0000 + me * 0x1000 + i * 3, i)) bad++;
            T.eq(0, bad, "tile " + me + ": local form: " + (64 + me) + " raw keys -> (key, j), j = 0, 1, 2, ...");
            int l = (me + 63) & 63, r = (me + 1) & 63, nl = nAug(l), nr = nAug(r); bad = 0;
            for (int j = 0; j < nl; j++) if (rec(p, 0x80000 + 2 * j) != rec(l << 16 | j, j)) bad++;
            for (int j = 0; j < nr; j++) if (rec(p, 0x80000 + 2 * (nl + j)) != rec(r << 16 | j, nl + j)) bad++;
            T.eq(0, bad, "tile " + me + ": remote form: the left's " + nl + " then the right's " + nr + " records, high word = position in the buffer");
        }
        noViolations(m.c, "aug");
        meas("aug", "local form, 64 keys (size 32, mode 4): GO -> all READY", goToReady(m, 0));
        meas("aug", "remote form, start command (two neighbours, 20-35 records each): GO -> all READY", goToReady(m, 2));
    }

    // ================================================================ 6 gather
    static void gather() {
        T.group("gather");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_gather");
        int[][] perm = new int[64][];
        for (int k = 1; k <= 2; k++) {
            for (Tile p : m.c.tiles) {
                int n = 50 + p.idx;
                Random r = new Random(77 * k + p.idx);
                perm[p.idx] = new int[n];
                for (int i = 0; i < n; i++) perm[p.idx][i] = i;
                for (int i = n - 1; i > 0; i--) { int j = r.nextInt(i + 1), x = perm[p.idx][i]; perm[p.idx][i] = perm[p.idx][j]; perm[p.idx][j] = x; }
                for (int i = 0; i < n; i++) p.sram.poke64(0x60000 + 2 * i, rec(i * 11, perm[p.idx][i]));
                for (int j = 0; j < n; j++) for (int q = 0; q < k; q++) p.sram.poke64(0x20000 + 2 * k * j + 2 * q, rec(p.idx << 24 | j << 4 | q, 0xBEEF0000 | k));
            }
            m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | k).go();
            T.check(m.run(() -> idle(m), 40000), "k = " + k + ": every tile gathered");
            for (Tile p : m.c.tiles) {
                int bad = 0, n = 50 + p.idx;
                for (int i = 0; i < n; i++) for (int q = 0; q < k; q++)
                    if (rec(p, 0xA0000 + 2 * k * i + 2 * q) != rec(p.idx << 24 | perm[p.idx][i] << 4 | q, 0xBEEF0000 | k)) bad++;
                T.eq(0, bad, "tile " + p.idx + ", k = " + k + ": " + n + " values in index order");
            }
        }
        finish(m, "gather", 10000);
        noViolations(m.c, "gather");
        meas("gather", "k = 1, 50-113 values per tile: GO -> all READY", goToReady(m, 0));
        meas("gather", "k = 2, 50-113 values per tile: GO -> all READY", goToReady(m, 1));
    }

    // ================================================================ 7 one-word exchange, words to one tile
    static void xchg() {
        T.group("xchg");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_xchg");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go();
        finish(m, "xchg", 20000);
        for (Tile p : m.c.tiles) {
            int me = p.idx, bad = 0;
            for (int s = 0; s < 64; s++) if (m.word(me, im, "in", s) != (s << 8 | me)) bad++;
            T.eq(0, bad, "tile " + me + ": in[s] = tile s's word to this tile, all 64");
            int l = (me + 63) & 63; bad = 0;
            for (int i = 0; i < 4; i++) if (m.word(me, im, "in4", i) != (l << 8 | 0xF0 | i)) bad++;
            T.eq(0, bad, "tile " + me + ": the left neighbour's four words, auto-incremented");
        }
        noViolations(m.c, "xchg");
        meas("xchg", "one-word exchange, 64 words per tile: GO -> all READY", goToReady(m, 0));
        meas("xchg", "four words to one tile: GO -> all READY", goToReady(m, 1));
    }

    // ================================================================ 8 vector exchange with the neighbours
    static void xvec() {
        T.group("xvec");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_xvec");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();
        finish(m, "xvec", 20000);
        int base = im.sym("nb");
        for (Tile p : m.c.tiles) {
            int me = p.idx, bad = 0;
            int[] from = { (me + 63) & 63, (me + 1) & 63, (me + 56) & 63, (me + 8) & 63 };   // left, right, above, below
            for (int k = 0; k < 4; k++) for (int i = 0; i < 6; i++) if (m.word(me, base + 4 * (6 * k + i)) != (from[k] << 8 | i)) bad++;
            T.eq(0, bad, "tile " + me + ": nb[0..3] from left, right, above, below");
        }
        noViolations(m.c, "xvec");
        meas("xvec", "6 words to each of 4 neighbours: GO -> all READY", goToReady(m, 0));
    }

    // ================================================================ 9 all-to-all
    static int cntA(int s, int d) { return (s * 7 + d * 3) & 7; }
    static final int[][] SCHED = { { 1, 29, 35, 63 }, { 2, 30, 34, 62 }, { 3, 31, 33, 61 }, { 5, 23, 41, 59 }, { 6, 22, 42, 58 },
        { 7, 21, 43, 57 }, { 8, 28, 36, 56 }, { 9, 27, 37, 55 }, { 10, 26, 38, 54 }, { 11, 25, 39, 53 }, { 12, 24, 40, 52 },
        { 13, 15, 49, 51 }, { 14, 18, 46, 50 }, { 16, 20, 44, 48 }, { 17, 19, 45, 47 }, { 0, 4, 32, 60 } };
    static void a2a() {
        T.group("a2a");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_a2a");
        for (Tile p : m.c.tiles) {
            int at = 0;
            for (int d = 0; d < 64; d++) for (int j = 0; j < cntA(p.idx, d); j++) p.sram.poke64(2 * at++, rec(p.idx << 24 | d << 16 | j, ~j));
        }
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
        finish(m, "a2a", 50000);
        long bound = 0;
        int[] R = new int[16];
        for (int g = 0; g < 16; g++) { for (int s = 0; s < 64; s++) for (int off : SCHED[g]) R[g] = Math.max(R[g], cntA(s, (s + off) & 63)); bound += 4L * R[g]; }
        for (Tile p : m.c.tiles) {
            int me = p.idx, at = 0, bad = 0;
            for (int s = 0; s < 64; s++) for (int j = 0; j < cntA(s, me); j++) if (rec(p, 0x40000 + 2 * at++) != rec(s << 24 | me << 16 | j, ~j)) bad++;
            T.eq(0, bad, "tile " + me + ": " + at + " records, each source's run in source order");
            int rb = 0; for (int g = 0; g < 16; g++) if (m.word(me, im, "R", g) != R[g]) rb++;
            T.eq(0, rb, "tile " + me + ": R[g] from the reduction = the global group maxima");
        }
        noViolations(m.c, "a2a");
        meas("a2a", "counts exchange + group maxima: GO -> all READY", goToReady(m, 0));
        meas("a2a", "reduction of R + configure: GO -> all READY", goToReady(m, 1));
        meas("a2a", "start: GO -> all READY (timetable sum R_g * 4 = " + bound + ")", goToReady(m, 2));
        goToFirstRequest(m, 2, "a2a", "start (a group run)");
    }

    // ================================================================ 10 reduction
    static void reduce() {
        T.group("reduce");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_reduce");
        int[] as = { 0, 977 };
        for (int a : as) m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | a).go();
        finish(m, "reduce", 40000);
        for (Tile p : m.c.tiles) {
            int bad = 0;
            for (int i = 0; i < 16; i++) {
                int e = 0; for (int t = 0; t < 64; t++) e = Math.max(e, (t * 37 + i * 11 + as[1]) & 0xFFF);
                if (m.word(p.idx, im, "res", i) != e) bad++;
            }
            T.eq(0, bad, "tile " + p.idx + ": the max over all 64 tiles, all 16 words");
        }
        noViolations(m.c, "reduce");
        meas("reduce", "16 words, max over 64 tiles, broadcast back: GO -> all READY", goToReady(m, 1));
    }

    // ================================================================ 11 the read port
    static void rdport() {
        T.group("rdport");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_rdport");
        int[][] keys = new int[64][];
        for (Tile p : m.c.tiles) {
            Random r = new Random(500 + p.idx);
            int n = 300 + p.idx;
            keys[p.idx] = new int[n];
            for (int i = 0; i < n; i++) keys[p.idx][i] = 100 + r.nextInt(2000);
            Arrays.sort(keys[p.idx]);
            for (int i = 0; i < n; i++) p.sram.poke(0x60000 + i, keys[p.idx][i]);
        }
        int[] q = { 0, 50, 100, 777, 1234, 2099, 2100, 0x7FFFFFFF };
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 5).go().command(LinkWord.BROADCAST, 2 << 24, q).go();
        finish(m, "rdport", 30000);
        for (Tile p : m.c.tiles) {
            T.eq(keys[p.idx][5], m.word(p.idx, im, "first", 0), "tile " + p.idx + ": one read");
            int bad = 0;
            for (int i = 0; i < q.length; i++) {
                int lo = 0; while (lo < keys[p.idx].length && keys[p.idx][lo] < q[i]) lo++;
                if (m.word(p.idx, im, "lb", i) != lo) bad++;
            }
            T.eq(0, bad, "tile " + p.idx + ": eight lower bounds");
        }
        noViolations(m.c, "rdport");
        meas("rdport", "one read: GO -> all READY", goToReady(m, 0));
        meas("rdport", "8 searches over 300-363 keys: GO -> all READY", goToReady(m, 1));
    }

    // ================================================================ 12 status and errors
    static void status() {
        T.group("status");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_status");
        for (int k = 1; k <= 3; k++) m.c.cc.command(LinkWord.BROADCAST, k << 24).go();
        finish(m, "status", 20000);
        for (Tile p : m.c.tiles) {
            int me = p.idx;
            T.eq(me, m.word(me, im, "info", 0), "tile " + me + ": tile index");
            T.eq(0x100000, m.word(me, im, "info", 1), "tile " + me + ": sram_chunks() = 4 MB");
            T.eq(0, m.word(me, im, "ev", 1), "tile " + me + ": case 1: no condition");
            T.eq(1 << Conditions.UNEXPECTED, m.word(me, im, "ev", 2), "tile " + me + ": case 2: unexpected arrival, and only it");
            T.eq(1 << Conditions.BAD_MODE, m.word(me, im, "ev", 3), "tile " + me + ": case 3: bad mode, the one new condition");
            T.eq(1 << Conditions.UNEXPECTED | 1 << Conditions.BAD_MODE, m.word(me, im, "cond", 3), "tile " + me + ": the conditions accumulate until HALT");
            T.eq(m.word(me, im, "cond", 3), p.ctl.conditions, "tile " + me + ": TILE_CONDITIONS as the tile holds it");
        }
        long v9 = m.c.violationMessages.stream().filter(s -> s.contains("invariant 9")).count();
        T.check(m.c.violations >= 64 && v9 > 0, "the unexpected arrivals are flagged (invariant 9): " + m.c.violations + " violations");
        T.check(m.c.violationMessages.stream().allMatch(s -> s.contains("invariant 9") || s.contains("invariant 5") || s.toLowerCase().contains("mode")),
                "and only the provoked ones: " + m.c.violationReport());
        T.check(m.c.cc.error(), "C&C's error bit is set (6.6)");
        out("status: provoked unexpected arrival and bad mode each set their condition, and only it; they accumulate until HALT; C&C's error bit set; " + m.c.violations + " violations, all provoked");
    }

    // ================================================================ 17-20: write a block, fill, hash, the self-test, REPORT
    static final SelfTest ST = SelfTest.load();
    static final int B_SRC = 0x00000, B_DST = 0x40000, B_WB = 0x80000;   // rcp_selftest.c's SRC, DST, WB
    static long[] sram(Tile p, int chunk, int n) { long[] v = new long[n]; for (int i = 0; i < n; i++) v[i] = p.sram.peek64(chunk + 2 * i); return v; }
    static String perRec(long c1, long c2, int n1, int n2) { return String.format("%.2f", (double) (c2 - c1) / (n2 - n1)); }
    static void blocks() {
        T.group("blocks");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_selftest");
        final int W1 = 64, W2 = 256, F1 = 256, F2 = 1024;
        long[] seeds = new long[64];
        Random rnd = new Random(14);
        for (int i = 0; i < 64; i++) seeds[i] = i == 5 ? 0 : rnd.nextLong();          // tile 5: a zero seed (st_seed makes it 1)
        CnC cc = m.c.cc;
        cc.command(LinkWord.BROADCAST, 7 << 24, W1).go().command(LinkWord.BROADCAST, 7 << 24, W2).go();      // phases 0, 1: write a block
        for (int n : new int[] { F1, F2 }) {                                                               // phases 2, 3: fill, C&C's seeds
            for (int i = 0; i < 64; i++) cc.command(i, 1 << 24, (int) seeds[i], (int) (seeds[i] >>> 32), n);
            cc.go();
        }
        cc.command(LinkWord.BROADCAST, 5 << 24, B_SRC, F1).go().command(LinkWord.BROADCAST, 5 << 24, B_SRC, F2).go();   // phases 4, 5: hash
        final long BASE = 0x0123456789ABCDEFL;
        cc.command(LinkWord.BROADCAST, 2 << 24, (int) BASE, (int) (BASE >>> 32), F2).go();                  // phase 6: fill, index seed + base
        cc.command(LinkWord.BROADCAST, 2 << 24, 0, 0, F1).go();                                             // phase 7: fill, index seed alone
        long[][] wrote = new long[64][], filled = new long[64][], sig = new long[64][2];
        boolean ok = m.run(() -> cc.phaseLog.size() >= 6 && tilesIdle(m), 400000);
        T.check(ok, "blocks: write, fill and hash phases ran");
        for (Tile p : m.c.tiles) {
            int me = p.idx;
            wrote[me] = sram(p, B_WB, W2);
            filled[me] = sram(p, B_SRC, F2);
            sig[me][0] = (m.word(me, im, "sig", 0) & 0xFFFFFFFFL) | (long) m.word(me, im, "sig", 1) << 32;
        }
        finish(m, "blocks", 400000);
        int badW = 0, badF = 0, badH = 0, badS = 0, badL = 0, badZ = 0;
        for (Tile p : m.c.tiles) {
            int me = p.idx;
            for (int i = 0; i < W2; i++) if (wrote[me][i] != rec(me << 16 | i, 0xB10C0000 | i)) badW++;
            long[] want = ST.fill(ST.seed(seeds[me]), F2);
            if (!Arrays.equals(want, filled[me])) badF++;
            if (sig[me][0] != ST.hash(want, 0, F2)) badH++;
            if (ST.hash(filled[me], 0, F2) != sig[me][0]) badS++;
            long[] idx = ST.fill(ST.tileSeed(me, BASE), F2);                                 // phase 6 (phase 7 overwrote the first F1)
            long[] idx0 = ST.fill(ST.tileSeed(me, 0), F1);
            long[] now = sram(p, B_SRC, F2);
            for (int i = 0; i < F2; i++) if (now[i] != (i < F1 ? idx0[i] : idx[i])) { badL++; break; }
            if (me == 5 && filled[me][0] != ST.fill(ST.seed(1), 1)[0]) badZ++;
        }
        T.eq(0, badW, "write_block: every tile's " + W2 + " records from data memory are in big SRAM");
        T.eq(0, badF, "fill from C&C's seeds: every tile's " + F2 + " records equal the Java generator's (selftest.h's constants)");
        T.eq(0, badZ, "fill: a zero seed is taken as 1 (st_seed)");
        T.eq(0, badH, "hash: every tile's hash of its block equals Java's hash of the Java-generated block");
        T.eq(0, badS, "hash: ... and Java's hash of what big SRAM holds");
        T.eq(0, badL, "fill from the tile index, with a base seed and without: every tile's records equal Java's");
        java.util.Set<Long> firsts = new java.util.HashSet<>(); for (Tile p : m.c.tiles) firsts.add(sram(p, B_SRC, 1)[0]);
        T.eq(64, firsts.size(), "index seeds: 64 different sequences");
        noViolations(m.c, "blocks");
        long w1 = goToReady(m, 0), w2 = goToReady(m, 1), f1 = goToReady(m, 2), f2 = goToReady(m, 3), h1 = goToReady(m, 4), h2 = goToReady(m, 5);
        meas("write_block", W1 + " records: GO -> all READY", w1);
        meas("write_block", W2 + " records: GO -> all READY", w2);
        meas("fill", F1 + " records, C&C's seed: GO -> all READY", f1);
        meas("fill", F2 + " records: GO -> all READY", f2);
        meas("hash", F1 + " records (" + 2 * F1 + " words): GO -> all READY", h1);
        meas("hash", F2 + " records: GO -> all READY", h2);
        out("blocks [meas: RecipeTest blocks]: cycles per 64-bit record, marginal (the difference of two sizes over their difference): write_block "
                + perRec(w1, w2, W1, W2) + ", fill " + perRec(f1, f2, F1, F2) + ", hash " + perRec(h1, h2, F1, F2)
                + " (" + String.format("%.2f", (double) (h2 - h1) / (2 * (F2 - F1))) + " per 32-bit word read)");
        perRecord.put("write_block", perRec(w1, w2, W1, W2)); perRecord.put("fill", perRec(f1, f2, F1, F2)); perRecord.put("hash", perRec(h1, h2, F1, F2));
    }
    static final Map<String, String> perRecord = new LinkedHashMap<>();

    /** The self-test: seed and fill, a uniform all-to-all, hash, and the signature to C&C; the simulator computes every
     *  tile's expected signature from the generator and the all-to-all's permutation alone. Round 1 corrupts one record
     *  of tile 17's block after the fill: exactly the tile that receives it reports a different signature. */
    static void selftest() {
        T.group("selftest");
        final int N = 1024, M = N / 64;
        final long BASE = 0x5EED5EED00C0FFEEL;
        long[][] want = new long[64][];
        for (int me = 0; me < 64; me++) want[me] = ST.fill(ST.tileSeed(me, BASE), N);
        long[] expect = new long[64];
        for (int t = 0; t < 64; t++) {
            long[] in = new long[N];
            for (int s = 0; s < 64; s++) System.arraycopy(want[s], t * M, in, s * M, M);   // s's block t lands in t's slot s
            expect[t] = ST.hash(in, 0, N);
        }
        for (int round = 0; round < 2; round++) {
            Mach m = mach();
            m.boot("rcp_selftest");
            CnC cc = m.c.cc;
            List<long[]> sigs = new ArrayList<>();
            final int CORRUPT = 17, REC = 22 * M + 3;                      // tile 17's block for tile 22: a record of it
            cc.command(LinkWord.BROADCAST, 2 << 24, (int) BASE, (int) (BASE >>> 32), N).go();
            if (round == 1) cc.then((c, t) -> {                          // after the fill, before configure: the block is at rest
                if (!tilesIdle(m)) return false;
                m.c.tiles[CORRUPT].sram.poke64(B_SRC + 2 * REC, 0xBADL); return true; });
            cc.command(LinkWord.BROADCAST, 3 << 24).go()
              .command(LinkWord.BROADCAST, 4 << 24).go()
              .command(LinkWord.BROADCAST, 5 << 24, B_DST, N).go()
              .command(LinkWord.BROADCAST, 6 << 24).go()
              .collect(64, 2000, sigs);
            finish(m, "selftest round " + round, 400000);
            long[] got = new long[64]; Arrays.fill(got, -1);
            for (long[] e : sigs) got[(int) e[1]] = e[2];
            int match = 0, miss = 0;
            for (int t = 0; t < 64; t++) if (got[t] == expect[t]) match++; else miss++;
            if (round == 0) {
                T.eq(64, sigs.size(), "round 0: 64 signatures reached C&C's receive FIFO");
                T.eq(64, match, "round 0: every tile's signature equals the one the simulator computed");
                long first = sigs.get(0)[0], last = sigs.get(sigs.size() - 1)[0];
                int paced = 0; long base = Long.MIN_VALUE;
                for (Tile p : m.c.tiles) {                                 // tile k sends 4 k cycles after tile 0 (after its skew): the delay loop
                    long at = p.ctl.lastSendAt - m.c.startSkew[p.idx] - 4L * p.idx;
                    if (base == Long.MIN_VALUE) base = at;
                    if (at == base) paced++;
                }
                T.eq(64, paced, "round 0: tile k's send 4 k cycles after tile 0's (after its skew): the (me + 1) x 4 delay loop is exact");
                T.check(m.c.cc.rxPeak <= 2, "round 0: C&C's receive FIFO never held more than 2 (" + m.c.cc.rxPeak + ")");
                noViolations(m.c, "selftest round 0");
                meas("selftest", "fill (" + N + " records, index seed): GO -> all READY", goToReady(m, 0));
                meas("selftest", "configure: GO -> all READY", goToReady(m, 1));
                meas("selftest", "start (timetable 16 x 4 x " + M + " = " + 64 * M + "): GO -> all READY", goToReady(m, 2));
                meas("selftest", "hash of the received " + N + " records: GO -> all READY", goToReady(m, 3));
                meas("selftest", "signatures to C&C: GO -> all READY", goToReady(m, 4));
                meas("selftest", "signatures to C&C: first to last arrival", last - first);
            } else {
                T.eq(63, match, "round 1 (tile " + CORRUPT + "'s record " + REC + " corrupted): 63 signatures still match");
                T.check(got[22] != expect[22], "round 1: the one that differs is tile 22's, which received the record");
                noViolations(m.c, "selftest round 1");
            }
        }
        out("selftest [meas: RecipeTest selftest]: fill from the index seed, a uniform all-to-all (" + M + " records per pair), hash, signatures paced to C&C: all 64 equal the simulator's; one corrupted record changes exactly its receiver's");
    }

    /** REPORT (6.6) end to end: tiles 0, 5, 10, ... provoke an unexpected arrival; C&C, with stopOnError, holds the next
     *  GO, waits for all READY, discards the pending command, broadcasts REPORT, collects 64 replies, halts. */
    static void reportRecipe() {
        T.group("report");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_selftest");
        CnC cc = m.c.cc;
        cc.stopOnError = true;
        long mask = 0; for (int t = 0; t < 64; t += 5) mask |= 1L << t;
        cc.command(LinkWord.BROADCAST, 8 << 24, (int) mask, (int) (mask >>> 32)).go().command(LinkWord.BROADCAST, 0).go();
        boolean ok = m.run(() -> cc.reportDoneAt >= 0 && !cc.busy(), 200000);
        T.check(ok, "report: the flow ran to the end (C&C idle)");
        long[] r = cc.reportBySrc();
        int good = 0;
        for (int t = 0; t < 64; t++) {
            int want = t % 5 == 0 ? 1 << Conditions.UNEXPECTED : 0;
            if (r[t] != -1 && (int) (r[t] >>> 32) == t && (int) r[t] == want) good++;
        }
        T.eq(64, cc.reports.size(), "report: 64 replies");
        T.eq(64, good, "report: each reply is (its tile index, its MASTER_CONDITIONS): unexpected arrival on tiles 0, 5, 10, ..., 0 elsewhere");
        long disc = 0; for (Tile p : m.c.tiles) disc += p.cmem.discardedCommands;
        T.eq(64, disc, "report: REPORT replaced the pending (exit) command on every tile, which never ran: discarded, not a violation");
        int stillCond = 0; for (Tile p : m.c.tiles) if (p.ctl.conditions != 0) stillCond++;
        T.eq(0, stillCond, "report: C&C halted the machine, which cleared every condition");
        T.check(!cc.error(), "report: and C&C cleared its error bit after the line fell");
        boolean halted = true; for (Tile p : m.c.tiles) if (!p.ctl.halted) halted = false;
        T.check(halted, "report: every tile is in HALT");
        long v9 = m.c.violationMessages.stream().filter(v -> v.contains("invariant 9")).count();
        T.check(m.c.violations == v9 && v9 > 0, "report: the only violations are the provoked unexpected arrivals: " + m.c.violationReport());
        long first = cc.reports.get(0)[0], last = cc.reports.get(cc.reports.size() - 1)[0];
        meas("report", "error bit set -> REPORT sent (C&C waits for all READY)", cc.reportStartedAt - cc.errorFirstAt);
        meas("report", "REPORT sent -> GO (the settle, 2 x " + cc.broadcastBound() + ")", cc.reportGoAt - cc.reportStartedAt);
        meas("report", "GO -> last reply at C&C", last - cc.reportGoAt);
        meas("report", "first to last reply", last - first);
        out("report [meas: RecipeTest report]: settle " + cc.reportSettle() + " cycles (2 x the broadcast bound " + cc.broadcastBound() + "); 64 replies in " + (last - first) + " cycles");
    }

    // ================================================================ 13 the rules that bite
    static Mach ruleMach(String what, int rule, int wrong, java.util.function.Consumer<Mach> prep, java.util.function.Consumer<Mach> extra) {
        Mach m = mach();
        if (prep != null) prep.accept(m);
        m.boot("rcp_rules");
        if (rule == 3) m.c.cc.command(LinkWord.BROADCAST, 3 << 24).go().command(LinkWord.BROADCAST, 6 << 24 | wrong).go();
        else m.c.cc.command(LinkWord.BROADCAST, rule << 24 | wrong).go();
        if (extra != null) extra.accept(m);
        finish(m, what, 40000);
        return m;
    }
    static void rules() {
        T.group("rules");
        Mach.Image im = new Mach.Image("rcp_rules");
        // 1: the end-of-phase WFI (M14: the wrong form is done with an arrival outstanding: done early; the done gate)
        for (int wrong = 0; wrong < 2; wrong++) {
            Mach m = ruleMach("rule 1", 1, wrong, null, null);
            long fell = 0, local = 0, early = 0, gated = 0, doneEarly = 0, landed = 0;
            long exitGo = m.c.cc.phaseLog.get(1)[1];
            for (Tile p : m.c.tiles) {
                List<long[]> wl = p.core.wfiLog;
                for (long[] w : wl) if (w[2] == TileControl.WAKE_LOCAL) local++;
                long[] last = wl.get(wl.size() - 1);                 // the wait_for_command() after the phase
                if (last[2] != TileControl.WAKE_GO) fell++;
                if (last[1] < exitGo) early++;
                gated += p.ctl.gatedEdges;
                if (p.ctl.conditions == 1 << Conditions.DONE_EARLY) doneEarly++;
                if (m.word(p.idx, im, "rbox", 0) == 7) landed++;
            }
            T.eq(64, landed, "1 " + (wrong == 0 ? "right" : "wrong") + ": every reply landed");
            T.eq(0, fell, "1 " + (wrong == 0 ? "right" : "wrong") + ": every wait_for_command() woken by GO");
            T.eq(0, early, "1 " + (wrong == 0 ? "right" : "wrong") + ": no tile acted on the next command before its GO");
            if (wrong == 0) { T.eq(64, local, "1 right: the phase's own WFI took the edge"); T.eq(0, doneEarly, "1 right: no condition"); noViolations(m.c, "rule 1 right"); }
            else {
                T.eq(64, doneEarly, "1 wrong: done with the reply outstanding sets done early, and only it, on every tile");
                T.eq(64, gated, "1 wrong: the reply's edge came after done and was ignored (the done gate)");
                T.check(m.c.cc.error(), "1 wrong: C&C's error bit is set");
                long v4 = m.c.violationMessages.stream().filter(s -> s.contains("invariant 4")).count();
                T.check(v4 > 0 && v4 == m.c.violationMessages.size(), "1 wrong: flagged as invariant 4, and nothing else: " + v4);
            }
        }
        // 2: disjoint buffers per run
        java.util.function.Consumer<Mach> poke2 = m -> { for (Tile p : m.c.tiles) for (int i = 0; i < 48; i++) p.sram.poke64(2 * i, rec(p.idx << 16 | i, i)); };
        for (int wrong = 0; wrong < 2; wrong++) {
            Mach m = ruleMach("rule 2", 2, wrong, poke2, null);
            if (wrong == 0) {
                int bad = 0;
                for (Tile p : m.c.tiles) for (int i = 0; i < 40; i++) if (rec(p, 2 * (8 + i)) != rec(p.idx << 16 | i, i)) bad++;
                T.eq(0, bad, "2 right: shifted by 8 records through a second buffer");
                noViolations(m.c, "rule 2 right");
            } else {
                long n = m.c.violationMessages.stream().filter(s -> s.contains("invariant 2")).count();
                T.check(n > 0 && n == m.c.violationMessages.size(), "2 wrong: reading and writing one buffer in a run is flagged (invariant 2): " + n);
            }
        }
        // 3: identical code from WFI to the start store
        java.util.function.Consumer<Mach> poke3 = m -> { for (Tile p : m.c.tiles) for (int i = 0; i < 20 + (p.idx & 7); i++) p.sram.poke64(2 * i, rec(p.idx << 16 | i, i)); };
        for (int wrong = 0; wrong < 2; wrong++) {
            Mach m = ruleMach("rule 3", 3, wrong, poke3, null);
            java.util.Set<Long> starts = new java.util.TreeSet<>();
            int bad = 0;
            for (Tile p : m.c.tiles) {
                starts.add(p.ag.startedAt - m.c.startSkew[p.idx]);
                int l = (p.idx + 63) & 63;
                for (int i = 0; i < 20 + (l & 7); i++) if (rec(p, 0x80000 + 2 * i) != rec(l << 16 | i, i)) bad++;
            }
            T.eq(0, bad, "3 " + (wrong == 0 ? "right" : "wrong") + ": the records arrived");
            if (wrong == 0) { T.eq(1, starts.size(), "3 right: every tile started on the same cycle (after its skew)"); noViolations(m.c, "rule 3 right"); }
            else {
                T.check(starts.size() > 1, "3 wrong: a per-tile delay before the start store: the tiles start on " + starts.size() + " different cycles");
                meas("rules", "3 wrong: spread of the start cycles across tiles (right: 0)", ((java.util.TreeSet<Long>) starts).last() - ((java.util.TreeSet<Long>) starts).first());
            }
        }
        // 4: one read size per group
        java.util.function.Consumer<Mach> poke4 = m -> { for (Tile p : m.c.tiles) for (int i = 0; i < 16 + p.idx; i++) { p.sram.poke64(2 * i, rec(0xA0000 + i, p.idx)); p.sram.poke64(0x40000 + 2 * i, rec(0xB0000 + i, p.idx)); } };
        for (int wrong = 0; wrong < 2; wrong++) {
            Mach m = ruleMach("rule 4", 4, wrong, poke4, null);
            if (wrong == 0) {
                int bad = 0;
                for (Tile p : m.c.tiles) for (int r = 0; r < 16 + p.idx; r++) {
                    if (rec(p, 0x80000 + 4 * r) != rec(0xA0000 + r, p.idx)) bad++;
                    if (rec(p, 0x80000 + 4 * r + 2) != rec(0xB0000 + r, p.idx)) bad++;
                }
                T.eq(0, bad, "4 right: the group's two streams interleaved, one element each per rotation");
                noViolations(m.c, "rule 4 right");
            } else {
                long n = m.c.violationMessages.stream().filter(s -> s.contains("differs from its group")).count();
                T.check(n > 0 && n == m.c.violationMessages.size(), "4 wrong: a stream whose read size differs from its group's is flagged: " + n);
            }
        }
        // 5: PTR[64] restored after mode 3; C&C stages afterwards
        java.util.function.Consumer<Mach> poke5 = m -> { for (Tile p : m.c.tiles) for (int i = 0; i < 30 + p.idx; i++) p.sram.poke64(2 * i, rec(i * 37 & 0xFF, i)); };
        for (int wrong = 0; wrong < 2; wrong++) {
            final int w = wrong;
            Mach m = ruleMach("rule 5", 5, wrong, poke5, mm -> {
                mm.c.cc.waitUntil(() -> tilesIdle(mm));
                List<long[]> ps = new ArrayList<>();
                for (int t = 0; t < 64; t++) for (int i = 0; i < 4; i++) ps.add(new long[] { t, LinkWord.M_SRC, 0, 0x5717_0000_0000_0000L | t << 8 | i });
                mm.c.cc.send(ps).waitCycles(50);
            });
            int at = 0x100000 - Registers.STAGE_CHUNKS, ok = 0;
            for (Tile p : m.c.tiles) { boolean here = true; for (int i = 0; i < 4; i++) if (rec(p, at + 2 * i) != (0x5717_0000_0000_0000L | p.idx << 8 | i)) here = false; if (here) ok++; }
            if (w == 0) { T.eq(64, ok, "5 right: C&C's staged words land at stage_base()"); noViolations(m.c, "rule 5 right"); }
            else T.eq(0, ok, "5 wrong: without the restore, C&C's staged words land where the bucket pass left PTR[64], not at stage_base()");
        }
        out("rules: each right form passes; each wrong form fails as described (1 a fall-through wait_for_command(), 2 invariant 2, 3 unaligned starts, 4 the group size check, 5 staging lands elsewhere)");
    }

    // ================================================================ 14 the synchronised start by identical code
    static int nSync(int t) { return 16 + ((t * 5) & 15); }
    static void sync() {
        T.group("sync");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_sync");
        for (Tile p : m.c.tiles) {
            for (int i = 0; i < nSync(p.idx); i++) p.sram.poke64(2 * i, rec(p.idx << 16 | i, 0xC0DE0000 | i));
            p.sram.poke(0x20000, nSync(p.idx)); p.sram.poke(0x20001, nSync((p.idx + 63) & 63));
        }
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go();
        T.check(m.run(() -> idle(m), 40000), "every tile ran the set-up and the start");
        java.util.Set<Long> starts = new java.util.TreeSet<>();
        long goAt = m.c.cc.lastGoAt;
        for (Tile p : m.c.tiles) {
            starts.add(p.ag.startedAt - m.c.startSkew[p.idx]);
            int l = (p.idx + 63) & 63, bad = 0;
            for (int i = 0; i < nSync(l); i++) if (rec(p, 0x40000 + 2 * i) != rec(l << 16 | i, 0xC0DE0000 | i)) bad++;
            T.eq(0, bad, "tile " + p.idx + ": the left neighbour's " + nSync(l) + " records arrived");
        }
        T.eq(1, starts.size(), "all 64 tiles started on the same cycle (after their 0/+1 skew)");
        long s0 = starts.iterator().next();
        finish(m, "sync", 10000);
        noViolations(m.c, "sync");
        meas("sync", "set-up command (per-tile lengths): GO -> all READY", goToReady(m, 0));
        meas("sync", "start command: GO -> the start taking effect (tile 0, after its skew)", s0 - goAt);
        meas("sync", "start command: GO -> all READY", goToReady(m, 1));
        goToFirstRequest(m, 1, "sync", "start (a stream run)");
    }
}
