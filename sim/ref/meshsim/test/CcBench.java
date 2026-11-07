/**
 * WORKER14's measurements: C&C 1-3 as cores (Config.ccCores) against the Java model (CnC).
 *  - the boot (6.2), reset release to all 64 tiles running their image, with rcp_boot (recipe 22's tile side: the
 *    RECIPES table's 120 cycles, 62 broadcast packets) and with cmd; the cores' boot broken down;
 *  - a phase: back-to-back broadcast commands (cmd's type 1, one argument) with READY/GO, GO to GO in the steady state.
 * Usage: java CcBench
 */
final class CcBench {
    public static void main(String[] a) {
        for (String img : new String[] { "rcp_boot", "cmd" }) boot(img);
        phase(32);
    }
    static long allRunning(Chip c) { long r = 0; for (Tile p : c.tiles) { if (p.core.firstImageAt < 0) return -1; r = Math.max(r, p.core.firstImageAt); } return r; }

    static void boot(String img) {
        Mach j = new Mach(Mach.cfg(-1));
        j.boot(img);
        j.run(() -> allRunning(j.c) >= 0, 20000);
        long model = allRunning(j.c);
        Mach m = new Mach(Mach.coresCfg(-1));
        m.bootCores(img);
        CcCores k = m.c.cores;
        long started = -1, overlaysDone = -1, allReady = -1, go = -1;
        long gos = m.c.cc1p.gos;
        while (allRunning(m.c) < 0 && m.c.cycle < 20000) {
            m.c.step();
            if (started < 0 && m.mbox(3) >= 1) started = m.c.cycle;
            if (allReady < 0 && m.c.cc1p.readyBits(m.c.cycle) == -1L) allReady = m.c.cycle;
            if (go < 0 && m.c.cc1p.gos > gos) go = m.c.cycle;
        }
        long cc1 = k.unit(0).core.firstImageAt, cc23 = Math.max(k.unit(1).core.firstImageAt, k.unit(2).core.firstImageAt), all = allRunning(m.c);
        Mach.Image t = new Mach.Image(img), c1 = m.cc1;
        System.out.printf("boot '%s' (%d + %d packets to each tile): the Java model %d cycles; cores %d cycles%n", img,
                (t.code.length + 3) / 4, (t.data.length + 3) / 4, model, all);
        System.out.printf("  the host lands C&C 1's image (%d packets) and CMD_JUMP: C&C 1 running at %d%n", (c1.code.length + 3) / 4 + (c1.data.length + 3) / 4 + 2, cc1);
        System.out.printf("  C&C 1 stages the commands and streams C&C 2/3's images: both running at %d (+%d)%n", cc23, cc23 - cc1);
        System.out.printf("  both \"started\" requests served by %d (+%d)%n", started, started - cc23);
        System.out.printf("  the tiles' image streamed, all READY at %d (+%d); GO at %d (+%d); all 64 running at %d (+%d)%n",
                allReady, allReady - started, go, go - allReady, all, all - go);
        System.out.printf("  from C&C 1 running: %d cycles; violations %d%n", all - cc1, m.c.violations);
    }

    static void phase(int n) {
        Mach j = new Mach(Mach.cfg(-1));
        j.boot("cmd");
        j.run(() -> allRunning(j.c) >= 0 && j.allInWfi(), 20000);
        for (int i = 0; i < n; i++) j.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 1, 1).go();
        long[] mg = gos(j, n);
        Mach m = new Mach(Mach.coresCfg(-1));
        m.bootCores("cmd");
        m.run(() -> m.coresBooted() && m.allInWfi(), 20000);
        m.cc1Command(Mach.CC_PHASE, n << 16 | 1, 1 << 24 | 1, 1);
        long[] cg = gos(m, n);
        System.out.printf("phase: %d back-to-back broadcast commands (cmd type 1, one argument), READY then GO each%n", n);
        System.out.printf("  GO to GO, steady state (the last %d): the Java model %s; cores %s%n", n / 2, stats(mg, n / 2), stats(cg, n / 2));
        System.out.printf("  violations: model %d, cores %d%n", j.c.violations, m.c.violations);
    }
    /** The cycles of the next n GO pulses. */
    static long[] gos(Mach m, int n) {
        long[] at = new long[n]; int k = 0; long g = m.c.cc1p.gos;
        while (k < n && m.c.cycle < 2_000_000) { m.c.step(); while (m.c.cc1p.gos > g && k < n) { g++; at[k++] = m.c.cycle; } }
        return at;
    }
    static String stats(long[] at, int last) {
        long min = Long.MAX_VALUE, max = 0, sum = 0;
        for (int i = at.length - last; i < at.length; i++) { long d = at[i] - at[i - 1]; min = Math.min(min, d); max = Math.max(max, d); sum += d; }
        return String.format("%.1f (%d-%d)", (double) sum / last, min, max);
    }
}
