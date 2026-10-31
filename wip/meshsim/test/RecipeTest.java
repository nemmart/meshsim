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
        cmdloop(); copy(); cmem(); bucket(); aug(); gather(); xchg(); xvec(); a2a(); rdport(); status(); rules(); sync();
        blocks(); selftest(); boot(); signal(); tables(); replay();
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
        T.check(ok && m.allExited(), what + ": every tile ran to its exit: " + m.haltSummary());
    }
    /** Cycles from command k's GO (0 = the first command after boot) to all 64 READY after it. */
    static long goToReady(Mach m, int k) { List<long[]> ph = m.c.cc.phaseLog; return ph.get(k + 2)[0] - ph.get(k + 1)[1]; }   // v1.2: entry 0 is the boot's GO
    /** A synchronised start (spec 6.4): from C&C's GO store (phase k) to each tile's start store and first request.
     *  Returns {min, max} over tiles of the first request, and prints the breakdown. */
    static long[] goToFirstRequest(Mach m, int k, String recipe, String run) {
        long go = m.c.cc.phaseLog.get(k + 1)[1];                     // v1.2: entry 0 is the boot's GO
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
    static final Pattern MD = Pattern.compile("<!-- recipe: (\\S+) \\((\\S+)\\) -->\\s*```(?:c|java|asm)\\n(.*?)```", Pattern.DOTALL);
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
            try (var files = Files.list(Path.of("../sw/recipes"))) {          // v1.2: the recipes' sources (wip/sw/recipes/)
                List<Path> all = new ArrayList<>(files.sorted().toList());
                for (Path f : all) {
                    String n = f.getFileName().toString();
                    if (!(n.endsWith(".c") || n.endsWith(".h") || n.endsWith(".S"))) continue;
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
                T.eqo(src.get(name), norm(x.group(3)), "block " + name + ": RECIPES.md's text equals sw/recipes/" + file.get(name));
                seen.add(name);
            }
            for (String s : src.keySet()) T.check(seen.contains(s), "RECIPE region " + s + " (" + file.get(s) + ") is quoted in RECIPES.md");
            out("snippets: " + n + " code blocks in RECIPES.md, " + src.size() + " RECIPE regions in wip/sw/recipes/");
        } catch (IOException e) { T.check(false, "snippets: " + e); }
    }

    // ================================================================ 1 boot and the command loop
    static void cmdloop() {
        T.group("cmdloop");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_cmdloop");
        int[] a31 = new int[Registers.ARGS_PER_BLOCK]; int s31 = 0; for (int i = 0; i < a31.length; i++) { a31[i] = 1000 + i; s31 += a31[i]; }   // a full block: 7 arguments
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 3, 10, 20, 30).go();
        for (int t = 0; t < 64; t++) m.c.cc.command(t, 1 << 24 | 2, t * 100, 5);
        m.c.cc.go().command(LinkWord.BROADCAST, 1 << 24 | a31.length, a31).go();
        m.c.cc.command(LinkWord.BROADCAST, 2 << 24).go();
        boolean ok = m.run(m::allStopped, 20000);
        T.check(ok && m.allExited(), "every tile ran the command loop to CMD_EXIT: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            int me = p.idx;
            T.eq(60 + me, m.word(me, im, "res", 0), "tile " + me + ": broadcast, even block, 3 arguments");
            T.eq(me * 100 + 5 + me, m.word(me, im, "res", 1), "tile " + me + ": unicast, odd block, its own arguments");
            T.eq(s31 + me, m.word(me, im, "res", 2), "tile " + me + ": broadcast, even block, 7 arguments (a full block)");
            T.eq(3, m.word(me, im, "ncmd", 0), "tile " + me + ": three sums");
        }
        noViolations(m.c, "cmdloop");
        meas("cmdloop", "a 3-argument command: GO -> all 64 READY", goToReady(m, 0));
        meas("cmdloop", "a 7-argument command: GO -> all 64 READY", goToReady(m, 2));
    }

    // ================================================================ 2 a stream run: local copy
    static void copy() {
        T.group("copy");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_copy");
        for (Tile p : m.c.tiles) for (int i = 0; i < 120; i++) p.sram.poke64(2 * i, rec(p.idx << 20 | i, ~i));
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 100).go();
        finish(m, "copy", 30000);
        for (Tile p : m.c.tiles) {
            int bad = 0;
            for (int i = 0; i < 100; i++) if (rec(p, 0x40000 + 2 * i) != rec(p.idx << 20 | i, ~i)) bad++;
            T.eq(0, bad, "tile " + p.idx + ": 100 records copied SRC -> DST");
            T.eq(0, p.sram.peek64(0x40000 + 2 * 100), "tile " + p.idx + ": nothing past the run");
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
        final int OVL = Registers.PMEM_BASE + 0x6000;              // v1.2: the recipe's overlay address (program memory)
        T.check(im.code.length * 4 <= 0x6000, "the program's code lies below the overlay at 0x01006000");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();
        finish(m, "cmem", 20000);
        for (Tile p : m.c.tiles) {
            int me = p.idx, s = 0; for (int i = 0; i < 16; i++) s += me * 1000 + i;
            int got = 0; for (int i = 0; i < 16; i++) got += m.word(me, im, "params", i);
            T.eq(s, got, "tile " + me + ": 16 parameter words in data memory (params[])");
            boolean match = true; for (int i = 0; i < 256; i++) if (p.core.peekLocal(OVL + 4 * i) != ov[i]) match = false;
            T.check(match, "tile " + me + ": 256 overlay words in program memory");
            T.eq(2 * (me * 1000) + 0x123, m.word(me, im, "res", 0), "tile " + me + ": the overlay ran on params[0]");
        }
        noViolations(m.c, "cmem");
        List<long[]> w = m.c.tiles[0].core.wfiLog;
        meas("cmem", "256 words into program memory: start store -> wake", w.get(w.size() - 1)[1] - m.c.tiles[0].ag.startedAt + 1);
    }

    // ================================================================ 4 histogram digit, prefix sum, bucket passes
    static void bucket() {
        T.group("bucket");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_bucket");
        long[][] in = new long[64][];
        for (Tile p : m.c.tiles) {
            Random r = new Random(1000 + p.idx);
            int n = 200;                                          // v1.2: the command's n on every tile
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
            int h = 0; for (int t = 0; t < 256; t++) h += p.hist.peek(t);
            T.eq(0, h, "tile " + p.idx + ": HIST clear at the end");
        }
        noViolations(m.c, "bucket");
        meas("bucket", "200 records: count copy + 4 bucket passes: GO -> all READY", goToReady(m, 0));
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
            for (int i = 0; i < 64; i++) if (rec(p, 0x40000 + 2 * i) != rec(0x7000_0000 + me * 0x1000 + i * 3, i)) bad++;
            T.eq(0, bad, "tile " + me + ": local form: 64 raw keys -> (key, j), j = 0, 1, 2, ... (v1.2: 64 on every tile)");
            int l = (me + 63) & 63, r = (me + 1) & 63, nl = nAug(l), nr = nAug(r); bad = 0;
            for (int j = 0; j < nl; j++) if (rec(p, 0x80000 + 2 * j) != rec(l << 16 | j, j)) bad++;
            for (int j = 0; j < nr; j++) if (rec(p, 0x80000 + 2 * (nl + j)) != rec(r << 16 | j, nl + j)) bad++;
            T.eq(0, bad, "tile " + me + ": remote form: the left's " + nl + " then the right's " + nr + " records, high word = position in the buffer");
        }
        noViolations(m.c, "aug");
        meas("aug", "local form, 64 keys: GO -> all READY", goToReady(m, 0));
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
                for (int i = 0; i < n; i++) p.sram.poke64(0x00000 + 2 * i, rec(i * 11, perm[p.idx][i]));
                for (int j = 0; j < n; j++) for (int q = 0; q < k; q++) p.sram.poke64(0x40000 + 2 * k * j + 2 * q, rec(p.idx << 24 | j << 4 | q, 0xBEEF0000 | k));
            }
            m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | k).go();
            T.check(m.run(() -> idle(m), 40000), "k = " + k + ": every tile gathered");
            for (Tile p : m.c.tiles) {
                int bad = 0, n = 50 + p.idx;
                for (int i = 0; i < n; i++) for (int q = 0; q < k; q++)
                    if (rec(p, 0x80000 + 2 * k * i + 2 * q) != rec(p.idx << 24 | perm[p.idx][i] << 4 | q, 0xBEEF0000 | k)) bad++;
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
            for (int s = 0; s < 64; s++) if (m.word(me, im, "in", s) != s * 64 + me) bad++;
            T.eq(0, bad, "tile " + me + ": in[s] = tile s's word to this tile (s x 64 + me), all 64");
        }
        int bad4 = 0; for (int i = 0; i < 4; i++) if (m.word(0, im, "box", i) != 64 + i) bad4++;
        T.eq(0, bad4, "tile 0: tile 1's four words in box, ROUTER_ADDR auto-incremented");
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
            for (int k = 0; k < 4; k++) for (int i = 0; i < 6; i++) if (m.word(me, base + 4 * (6 * k + i)) != from[k] * 16 + i) bad++;
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
        int[] q = { 0, 50, 777, 1234, 2099, 2100, 0x7FFFFFFF };          // 7: a command's argument block (v1.0 had 8)
        // v1.2 (recipe 11): command 1 reads key 0; command 2 searches the first n = 300 keys for the seven arguments
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24 | 300, q).go();
        finish(m, "rdport", 30000);
        for (Tile p : m.c.tiles) {
            T.eq(keys[p.idx][0], m.word(p.idx, im, "first", 0), "tile " + p.idx + ": one read");
            int bad = 0;
            for (int i = 0; i < q.length; i++) {
                int lo = 0; while (lo < 300 && keys[p.idx][lo] < q[i]) lo++;
                if (m.word(p.idx, im, "lb", i) != lo) bad++;
            }
            T.eq(0, bad, "tile " + p.idx + ": seven lower bounds");
        }
        noViolations(m.c, "rdport");
        meas("rdport", "one read: GO -> all READY", goToReady(m, 0));
        meas("rdport", "7 searches over 300 keys: GO -> all READY", goToReady(m, 1));
    }

    // ================================================================ 12 status and errors
    static void status() {
        T.group("status");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_status");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();
        finish(m, "status", 20000);
        for (Tile p : m.c.tiles) {
            int me = p.idx;
            T.eq(me, m.word(me, im, "me_out", 0), "tile " + me + ": PARAMS' endpoint id");
            T.eq(0x100000, m.word(me, im, "chunks_out", 0), "tile " + me + ": sram_chunks() = 4 MB");
            T.eq(1, m.word(me, im, "present_out", 0), "tile " + me + ": peripheral 1 present (the replayer)");
            T.eq(0, m.word(me, im, "live_out", 0) & ~(1 << Registers.ST_LANDED), "tile " + me + ": STATUS: nothing running (the idle machine)");
        }
        noViolations(m.c, "status");
        out("status: PARAMS' endpoint id, size and peripherals, and STATUS, read on every tile");
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
        int blk = im.sym("blk");                                                                            // v1.2: the program's block in data memory
        cc.waitUntil(() -> tilesIdle(m)).call(() -> {                                                       // after crt0 has zeroed .bss
            for (Tile p : m.c.tiles) for (int i = 0; i < W2; i++) { p.core.pokeLocal(blk + 8 * i, p.idx << 16 | i); p.core.pokeLocal(blk + 8 * i + 4, 0xB10C0000 | i); } });
        cc.command(LinkWord.BROADCAST, 7 << 24).go();                                                       // phase 0: write the block (256 records)
        for (int n : new int[] { F1, F2 }) {                                                               // phases 2, 3: fill, C&C's seeds
            for (int i = 0; i < 64; i++) cc.command(i, 1 << 24, (int) seeds[i], (int) (seeds[i] >>> 32), n);
            cc.go();
        }
        cc.command(LinkWord.BROADCAST, 5 << 24, B_SRC, F1).go().command(LinkWord.BROADCAST, 5 << 24, B_SRC, F2).go();   // phases 4, 5: hash
        final long BASE = 0x0123456789ABCDEFL;
        cc.command(LinkWord.BROADCAST, 2 << 24, (int) BASE, (int) (BASE >>> 32), F2).go();                  // phase 6: fill, index seed + base
        cc.command(LinkWord.BROADCAST, 2 << 24, 0, 0, F1).go();                                             // phase 7: fill, index seed alone
        long[][] wrote = new long[64][], filled = new long[64][], sig = new long[64][2];
        boolean ok = m.run(() -> cc.phaseLog.size() >= 6 && tilesIdle(m), 400000);   // the boot's GO, then five phases
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
        long w2 = goToReady(m, 0), f1 = goToReady(m, 1), f2 = goToReady(m, 2), h1 = goToReady(m, 3), h2 = goToReady(m, 4);   // v1.2: one write phase
        if (System.getenv("RCP_DEBUG") != null) for (long[] e : m.c.cc.phaseLog) System.out.println("  phaseLog " + Arrays.toString(e));
        meas("write_block", W2 + " records: GO -> all READY", w2);
        meas("fill", F1 + " records, C&C's seed: GO -> all READY", f1);
        meas("fill", F2 + " records: GO -> all READY", f2);
        meas("hash", F1 + " records (" + 2 * F1 + " words): GO -> all READY", h1);
        meas("hash", F2 + " records: GO -> all READY", h2);
        out("blocks [meas: RecipeTest blocks]: cycles per 64-bit record, marginal (the difference of two sizes over their difference): write_block "
                + String.format("%.2f (256 records, not marginal)", w2 / 256.0) + ", fill " + perRec(f1, f2, F1, F2) + ", hash " + perRec(h1, h2, F1, F2)
                + " (" + String.format("%.2f", (double) (h2 - h1) / (2 * (F2 - F1))) + " per 32-bit word read)");
        perRecord.put("write_block", String.format("%.1f", w2 / 256.0)); perRecord.put("fill", perRec(f1, f2, F1, F2)); perRecord.put("hash", perRec(h1, h2, F1, F2));
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
            cc.drainReports = true;                                        // v1.2: the signatures are requests to C&C 1, which answers each
            List<long[]> sigs = cc.results;
            final int CORRUPT = 17, REC = 22 * M + 3;                      // tile 17's block for tile 22: a record of it
            cc.command(LinkWord.BROADCAST, 2 << 24, (int) BASE, (int) (BASE >>> 32), N).go();
            if (round == 1) cc.then((c, t) -> {                          // after the fill, before configure: the block is at rest
                if (!tilesIdle(m)) return false;
                m.c.tiles[CORRUPT].sram.poke64(B_SRC + 2 * REC, 0xBADL); return true; });
            cc.command(LinkWord.BROADCAST, 3 << 24).go()
              .command(LinkWord.BROADCAST, 4 << 24).go()
              .command(LinkWord.BROADCAST, 5 << 24, B_DST, N).go()
              .command(LinkWord.BROADCAST, 6 << 24).go();
            finish(m, "selftest round " + round, 400000);
            long[] got = new long[64]; Arrays.fill(got, -1);
            for (long[] e : sigs) got[(int) e[1]] = e[3] << 32 | (e[2] & 0xFFFFFFFFL);
            int match = 0, miss = 0;
            for (int t = 0; t < 64; t++) if (got[t] == expect[t]) match++; else miss++;
            if (round == 0) {
                T.eq(64, sigs.size(), "round 0: 64 signatures reached C&C 1 as requests (and each was answered)");
                T.eq(64, match, "round 0: every tile's signature equals the one the simulator computed");
                long first = sigs.get(0)[0], last = sigs.get(sigs.size() - 1)[0];
                noViolations(m.c, "selftest round 0");
                meas("selftest", "fill (" + N + " records, index seed): GO -> all READY", goToReady(m, 0));
                meas("selftest", "configure: GO -> all READY", goToReady(m, 1));
                meas("selftest", "start (timetable 16 x 4 x " + M + " = " + 64 * M + "): GO -> all READY", goToReady(m, 2));
                meas("selftest", "hash of the received " + N + " records: GO -> all READY", goToReady(m, 3));
                meas("selftest", "signatures to C&C 1 over the request bus, answered: GO -> all READY", goToReady(m, 4));
                meas("selftest", "signatures at C&C 1: first to last", last - first);
            } else {
                T.eq(63, match, "round 1 (tile " + CORRUPT + "'s record " + REC + " corrupted): 63 signatures still match");
                T.check(got[22] != expect[22], "round 1: the one that differs is tile 22's, which received the record");
                noViolations(m.c, "selftest round 1");
            }
        }
        out("selftest [meas: RecipeTest selftest]: fill from the index seed, a uniform all-to-all (" + M + " records per pair), hash, signatures to C&C 1 as requests (answered): all 64 equal the simulator's; one corrupted record changes exactly its receiver's");
    }

    /** REPORT (6.6) end to end: tiles 0, 5, 10, ... provoke an unexpected arrival; C&C, with stopOnError, holds the next
     *  GO, waits for all READY, discards the pending command, broadcasts REPORT, collects 64 replies, halts. */
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
        // v1.2 (recipe 15): the right forms only, each run on every tile; the wrong forms are described in the book
        Mach m = mach();
        Mach.Image im = m.boot("rcp_rules");
        for (Tile p : m.c.tiles) {
            for (int i = 0; i < 64; i++) p.sram.poke64(2 * i, rec(p.idx << 16 | i, 0xA0000 + i));
            for (int i = 0; i < 64; i++) p.sram.poke(0x40000 + 3 + i, 0xB0000 + i);
        }
        // rule_wait: a self-reply, waited for before done
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();
        T.check(m.run(() -> idle(m), 40000), "rule_wait ran");
        for (Tile p : m.c.tiles) T.eq(7, m.word(p.idx, im, "rbox", 0), "tile " + p.idx + ": rule_wait: the reply was in before done");
        // rule_disjoint: shift 64 records up by 8, through B
        m.c.cc.command(LinkWord.BROADCAST, 2 << 24).go();
        T.check(m.run(() -> idle(m), 80000), "rule_disjoint ran");
        int bad = 0;
        for (Tile p : m.c.tiles) for (int i = 0; i < 64; i++) if (rec(p, 2 * (8 + i)) != rec(p.idx << 16 | i, 0xA0000 + i)) bad++;
        T.eq(0, bad, "rule_disjoint: every tile's 64 records moved up by 8, through a disjoint buffer");
        for (Tile p : m.c.tiles) for (int i = 0; i < 64; i++) p.sram.poke64(2 * i, rec(p.idx << 16 | i, 0xA0000 + i));
        // rule_start: configure (3), then the start by identical code (4): 20 + (me & 7) records to the right-hand tile
        m.c.cc.command(LinkWord.BROADCAST, 3 << 24).go().command(LinkWord.BROADCAST, 4 << 24).go();
        T.check(m.run(() -> idle(m), 80000), "rule_start ran");
        bad = 0;
        for (Tile p : m.c.tiles) { int l = (p.idx + 63) & 63; for (int i = 0; i < 20 + (l & 7); i++) if (rec(p, 0x80000 + 2 * i) != rec(l << 16 | i, 0xA0000 + i)) bad++; }
        T.eq(0, bad, "rule_start: every tile received its left neighbour's 20 + (l & 7) records");
        // rule_align: a 64-bit stream to itself and a 32-bit stream to the right-hand tile in one group (B was rule_disjoint's
        // scratch buffer: its words are written again)
        for (Tile p : m.c.tiles) for (int i = 0; i < 64; i++) p.sram.poke(0x40000 + 3 + i, 0xB0000 + i);
        m.c.cc.command(LinkWord.BROADCAST, 5 << 24).go();
        T.check(m.run(() -> idle(m), 80000), "rule_align ran");
        bad = 0;
        for (Tile p : m.c.tiles) {
            for (int i = 0; i < 32; i++) if (rec(p, 0x80000 + 2 * i) != rec(p.idx << 16 | i, 0xA0000 + i)) bad++;
            for (int i = 0; i < 32; i++) if (p.sram.peek(0x88000 + i) != 0xB0000 + i) bad++;
        }
        T.eq(0, bad, "rule_align: the records to itself and the words from the left, in one group");
        finish(m, "rules", 20000);
        noViolations(m.c, "rules");
        out("rules: the right form of each rule (rule_wait, rule_disjoint, rule_start, rule_align) runs clean on every tile");
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
        { long lo = Long.MAX_VALUE, hi = 0;
          for (Tile p : m.c.tiles) for (long w : p.ctl.counterWriteAt) if (w >= goAt) { lo = Math.min(lo, w - goAt); hi = Math.max(hi, w - goAt); }
          out(String.format("  sync: the shortest start path's counter store, from C&C's GO store: cycle +%d..+%d over the 64 tiles [meas: RecipeTest sync]", lo, hi)); }
        meas("sync", "start command: GO -> all READY", goToReady(m, 1));
        goToFirstRequest(m, 1, "sync", "start (a stream run)");
    }

    // ================================================================ 22 boot part 2: announced staging
    static void boot() {
        T.group("boot");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_boot");
        final int P = 16, AT = 0xE0000;                                 // 16 rows into area 7 (6.2's staging area)
        List<long[]> ps = new ArrayList<>();
        for (int i = 0; i < P; i++) ps.add(new long[] { LinkWord.BROADCAST, LinkWord.M_WRITE, 0x03000000 | (AT + 4 * i) << 2, 0x5EED0000L | i, LinkWord.SIZE_128, (long) i << 32, LinkWord.counter(0) + 1 });
        m.c.cc.command(LinkWord.BROADCAST, 0x53 << 24 | P).go().waitCycles(32).send(ps);   // CMD_STAGE; the data no sooner than stage_delay after GO
        T.check(m.run(() -> idle(m), 40000), "every tile armed, took the staged data and pulsed done");
        int bad = 0, irq = 0;
        for (Tile p : m.c.tiles) {
            for (int i = 0; i < P; i++) if (p.sram.peek64(AT + 4 * i) != (0x5EED0000L | i) || p.sram.peek64(AT + 4 * i + 2) != (long) i << 32) bad++;
            if (p.core.mtvec == im.sym("trap_handler") && (p.core.mstatus & 8) != 0 && (p.core.mie & 1 << 11) != 0) irq++;
        }
        T.eq(0, bad, "the 16 staged rows are in every tile's big SRAM (counted, broadcast)");
        T.eq(64, irq, "boot part 2 set mtvec to the image's handler and enabled the hard interrupt on every tile");
        long running = 0; for (Tile p : m.c.tiles) running = Math.max(running, p.core.firstImageAt);
        meas("boot", "reset release -> all 64 tiles running their image (overlays " + (im.code.length + im.data.length + 3) / 4 + " packets, broadcast)", running);
        // the handler (recipe 24): an error on tile 9 with the hard interrupt enabled -> its 24 reports in C&C 1's queue
        m.c.cfg.checkErrors = false;
        m.c.cc.drainReports = true;
        m.c.tiles[9].ctl.conditions |= 1 << Conditions.DRIFT;
        Tile t9 = m.c.tiles[9];
        T.check(m.run(() -> m.c.cc.reportsOf(9).size() >= 24, 40000), "tile 9's handler sent its 24 reports to C&C 1");
        T.check(t9.core.trapCause == Controller.CAUSE_EXTERNAL && t9.core.mtvec == im.sym("trap_handler"), "through the image's handler (the hard interrupt)");
        meas("handler", "trap -> the last of 24 reports in C&C 1's receive queue (tile 9)", m.c.cc.reportsOf(9).get(23)[0] - t9.core.trapAt);
        m.c.cfg.checkErrors = true; m.c.violations = 0; m.c.violationMessages.clear();
        m.c.cc.reset(false).command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start")).runWhenReady();
        T.check(m.run(() -> m.c.resets == 1 && idle(m), 40000), "a reset and the boot again");
        finish(m, "boot", 20000);
        noViolations(m.c, "boot");
        meas("boot", "CMD_STAGE with 16 broadcast rows: GO -> all READY", goToReady(m, 0));
    }

    // ================================================================ 28 a signal between tiles: the ring pipeline
    static void signal() {
        T.group("signal");
        Mach m = mach();
        Mach.Image im = m.boot("rcp_signal");
        int vecs = im.sym("vecs"), gots = im.sym("gots");
        m.c.cc.waitUntil(() -> tilesIdle(m)).call(() -> {                // after crt0 has zeroed .bss
            for (Tile p : m.c.tiles) for (int st = 0; st < 4; st++) for (int i = 0; i < 8; i++) p.core.pokeLocal(vecs + 4 * (8 * st + i), p.idx << 16 | st << 8 | i); });
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();
        finish(m, "signal", 40000);
        int bad = 0;
        for (Tile p : m.c.tiles) { int l = (p.idx + 63) & 63; for (int st = 0; st < 4; st++) for (int i = 0; i < 8; i++) if (m.word(p.idx, gots + 4 * (8 * st + i)) != (l << 16 | st << 8 | i)) bad++; }
        T.eq(0, bad, "four steps of the ring: every tile got its left neighbour's vectors, each send released by a count-only signal");
        noViolations(m.c, "signal");
        meas("signal", "a 4-step ring of 8-word vectors, released by signals: GO -> all READY", goToReady(m, 0));
    }

    // ================================================================ 23 waiting for the tables by events
    static void tables() {
        T.group("tables");
        // the program's `expected` is 0 (never set: gcc folds it), so command 1 is a run that writes nothing here and
        // a2a_finish waits for DMA run done alone; the run (a 40-record stream to the right-hand tile's... none: count 0
        // streams would end at once) is set up from Java as 8 streams of 0 records, whose run still reads the tables
        Mach m = mach();
        m.boot("rcp_tables");
        m.c.cc.waitUntil(() -> tilesIdle(m)).call(() -> {
            for (Tile p : m.c.tiles) {
                for (int i = 0; i < 8; i++) p.setStream(i, Fields.streamSrc(0), 0, Fields.streamCount(0, Fields.SIZE_64, 1, LinkWord.M_SRC, 0));
                p.ctl.dmaConfig = Fields.masterConfig(Fields.RUN_STREAMS, 0, 8, 0, 0, 1); p.setMasterConfig(p.ctl.dmaConfig);
            } });
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go();
        finish(m, "tables", 40000);
        int woke = 0;
        for (Tile p : m.c.tiles) if (p.ag.doneAt > 0 && p.ctl.lastDoneAt > p.ag.doneAt) woke++;
        T.eq(64, woke, "a2a_finish: done followed the run's end (DMA run done), and tables_free_unknown returned, on every tile");
        noViolations(m.c, "tables");
    }

    // ================================================================ 26 the replayer's refill loop
    static void replay() {
        T.group("replay");
        // rcp_replay.c's state[] is never written (gcc folds it to zeros), so the pass it starts has an empty code stream:
        // the loop's shape runs (the state load into part 0, PULSE_RUN, the credits, the masks, run done) but no rows. The
        // replayer's real check is StageB synthetic (sw/replay/replay_ctl.c, row for row against Stage A).
        Mach m = mach();
        m.c.cfg.checkErrors = false;                                       // the empty code stream's invalid expansion, checked below
        m.boot("rcp_replay");
        Tile p = m.c.tiles[0];
        Replayer r = (Replayer) p.slot.p[1];
        m.c.cc.waitUntil(() -> tilesIdle(m)).call(() -> m.c.cc1p.expect = 1);
        m.c.cc.command(0, 1 << 24).go();                                   // tile 0 only
        T.check(m.run(() -> !m.c.cc.busy() && p.ctl.lastDoneAt > m.c.cc.lastGoAt && p.core.inWfi, 400000), "tile 0 ran recipe 26's pass and came back to wait_for_command()");
        T.eq(1L, r.passes, "one pass");
        T.check(!r.isRunning() && r.region0[0] == 0 && r.region0[1] == 0, "it ended; part 0 holds the recipe's (zero) state word");
        T.eq(0, p.slot.mask[1], "the loop cleared the replayer's EVENTS_MASK at the end");
        T.eq(Registers.EVENTS_MASK_RESET, p.ctl.wakeMask, "and restored the tile's");
        T.eq(1 << Replayer.ERR_INVALID, r.errors(m.c.cycle), "the replayer's only error: invalid expansion, a pop past the (empty) code stream, as the zero state makes it");
        T.check(r.overruns.isEmpty() && r.badWrites.isEmpty(), "no overrun and no bad write: the loop's refills kept to their credits");
        T.eq(1 << 11, p.ctl.conditions | p.ctl.errorsAt(m.c.cycle) & ~0x7FF, "the tile's ERRORS [11] (peripheral 1), and nothing else");
        noViolations(m.c, "replay");
    }
}
