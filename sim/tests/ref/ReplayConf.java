/**
 * The replayer's conformance, ref's side (WORKER15; the C++ side is tests/test_replayer_alu.cpp and test_replayconf.cpp).
 * Compiled into ref's default package (D5: ref's classes are package-private; nothing in sim/ref is changed).
 *
 * <pre>
 *   java -cp REF:. ReplayConf alu SEED N      every executed opcode on N seeded inputs: one line per opcode with a digest
 *                                             of exec's (rd, new pc, undefined) over them; then the RowCore sweep
 * </pre>
 *
 * The inputs come from splitmix64 (the same generator as tests/harness.h's Rng), so the C++ side regenerates them and
 * only the digests are committed. The digest: h = mix(h ^ x) for each output word x, mix splitmix64's finaliser.
 */
public final class ReplayConf {
    public static void main(String[] a) {
        switch (a.length > 0 ? a[0] : "") {
            case "alu" -> alu(Long.parseLong(a[1]), Integer.parseInt(a[2]));
            default -> { System.err.println("usage: ReplayConf alu SEED N"); System.exit(2); }
        }
    }

    // ================================================================ the generator and the digest (mirrored in C++)
    static final class Rng {
        long s;
        Rng(long seed) { s = seed; }
        long next() {
            long z = (s += 0x9e3779b97f4a7c15L);
            z = (z ^ (z >>> 30)) * 0xbf58476d1ce4e5b9L;
            z = (z ^ (z >>> 27)) * 0x94d049bb133111ebL;
            return z ^ (z >>> 31);
        }
        /** A value biased to the ALU's edges: small, negative, powers of two, masks, sign bits, words. */
        long value() {
            long r = next();
            int k = (int) ((r >>> 8) & 63), j = (int) ((r >>> 16) & 63);
            switch ((int) (r & 7)) {
                case 0: return next();
                case 1: return (r >>> 24) % 71;
                case 2: return -((r >>> 24) % 71);
                case 3: return 1L << k;
                case 4: return (k == 0 ? 0 : (-1L >>> (64 - k))) << j;
                case 5: return 0;
                case 6: return Long.MIN_VALUE | ((r >>> 24) % 71);
                default: return next() & 0xFFFF_FFFFL;
            }
        }
    }
    static long mix(long z) {
        z = (z ^ (z >>> 30)) * 0xbf58476d1ce4e5b9L;
        z = (z ^ (z >>> 27)) * 0x94d049bb133111ebL;
        return z ^ (z >>> 31);
    }

    // ================================================================ alu
    static void alu(long seed, int n) {
        StringBuilder out = new StringBuilder();
        out.append("# replayconf alu v1: seed ").append(seed).append(" n ").append(n).append('\n');
        ReplayerAlu.Out o = new ReplayerAlu.Out();
        for (int op = 0; op < 256; op++) {
            if (!ReplayerAlu.executes(op)) continue;
            Rng g = new Rng(seed * 1000 + op);
            long h = 0; int undef = 0;
            for (int i = 0; i < n; i++) {
                long va = g.value(), vb = g.value(), vc = g.value(), pc = g.value(), log = g.value(), so = g.value();
                long r = g.next();
                int inc = new int[] { 0, 2, 4 }[(int) Long.remainderUnsigned(r, 3)];
                boolean imm0pc0 = ((r >>> 8) & 1) != 0;
                ReplayerAlu.exec(op, va, vb, vc, pc, inc, imm0pc0, log, so, o);
                h = mix(h ^ o.rd); h = mix(h ^ o.newPc); h = mix(h ^ (o.undefined != null ? 1 : 0));
                if (o.undefined != null) undef++;
            }
            out.append(op).append(' ').append(ReplayerAlu.name(op)).append(' ').append(Long.toHexString(h)).append(' ').append(undef).append('\n');
        }
        // RowCore: the decode index, the six source immediates, reg() for every code and both offsets, imm_val for the
        // immediate codes, invalidWord's verdict and needsSourceImm on seeded words
        Rng g = new Rng(seed);
        long h = 0;
        for (int i = 0; i < n * 16; i++) {
            int w = (int) g.value();
            long lo = g.value(), cnst = g.value(), imm = g.value();
            h = mix(h ^ RowCore.decodeIndex(w));
            for (int f = 0; f < 6; f++) h = mix(h ^ RowCore.sourceImm(f, w));
            for (int c = 0; c < 146; c++) { h = mix(h ^ RowCore.reg(c, w, 0)); h = mix(h ^ RowCore.reg(c, w, 1)); }
            for (int c : new int[] { 128, 132, 133, 134, 135, 136, 145 }) h = mix(h ^ RowCore.immVal(c, cnst, imm));
            String inv = RowCore.invalidWord(lo);
            int verdict = inv == null ? 0 : inv.startsWith("opcode") ? 1 : inv.startsWith("a reserved") ? 2 : 3;
            h = mix(h ^ verdict); h = mix(h ^ (RowCore.needsSourceImm(lo) ? 1 : 0));
        }
        out.append("rowcore ").append(Long.toHexString(h)).append('\n');
        System.out.print(out);
    }
}
