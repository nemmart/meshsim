/**
 * MachSelf (WORKER20, P2): v1.3's Mach harness driven through its machine-as-specified paths, printing what a suite
 * reads back, for tests/mach.h's C++ harness to reproduce line for line (tests/test_mach.cpp; fixture
 * tests/mach/machself.out, written by tests/ref/machself.sh). Run in a scratch directory laid out as ref/meshsim is
 * (hops.txt, sw/bootrom.hex, sw/build/ holding the C++ tree's committed images): machself.sh makes it.
 *
 *   java -cp REF_CLASSES:THIS MachSelf
 */
final class MachSelf {
    static String all(Mach m, Mach.Image im, String s, int i) {
        int v = m.word(0, im, s, i);
        for (Tile p : m.c.tiles) if (m.word(p.idx, im, s, i) != v) return "differs (tile " + p.idx + ")";
        return "0x" + Integer.toHexString(v);
    }

    static void report(String name, Mach m, boolean ok) {
        System.out.printf("%s: %s at cycle %d; bootStart %d runAt %d; allExited %b allInWfi %b; halts [%s]; exit(0) %d exit(63) %d%n",
                name, ok ? "stopped" : "NOT stopped", m.c.cycle, m.bootStart, m.runAt, m.allExited(), m.allInWfi(),
                m.haltSummary(), m.exitValue(0), m.exitValue(63));
    }

    static void a2a(long seed) {
        Mach m = new Mach(Mach.cfg(seed));
        A2A data = new A2A(7, 2000);
        for (Tile p : m.c.tiles) {
            int s = p.idx;
            for (int k = 0; k < data.rec[s].length; k++) p.sram.poke64(A2A.SRC + 2 * k, data.rec[s][k]);
            for (int d = 0; d < 64; d++) p.sram.poke(0xF0000 + d, data.cnt[s][d]);
            for (int src = 0; src < 64; src++) p.sram.poke(0xF0000 + 64 + src, data.cnt[src][s]);
            for (int g = 0; g < 16; g++) p.sram.poke(0xF0000 + 128 + g, data.R[g]);
            p.sram.poke(0xF0000 + 144, 0);
        }
        m.boot("a2a");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
        boolean ok = m.run(m::allStopped, 2_000_000);
        report("a2a seed " + seed, m, ok);
        System.out.printf("  misplaced %d; lastGoAt %d lastAllReadyAt %d; busy %b; sram t9 0x40000 %016x t50 0x40100 %016x%n", data.verify(m.c),
                m.c.cc.lastGoAt, m.c.cc.lastAllReadyAt, m.c.cc.busy(), m.c.tiles[9].sram.peek64(0x40000), m.c.tiles[50].sram.peek64(0x40100));
    }

    public static void main(String[] a) {
        {   // regs (CtrlTest's), run as M2Prog runs it
            Mach m = new Mach(Mach.cfg(-1));
            Mach.Image im = m.boot("regs");
            boolean ok = m.run(m::allStopped, 40000);
            report("regs", m, ok);
            System.out.println("  r[35] " + all(m, im, "r", 35) + "; tile 5 pc 0x" + Integer.toHexString(m.c.tiles[5].core.pc)
                    + " x10 " + m.c.tiles[5].core.x[10]);
        }
        {   // wfi with M2Prog's C&C sequence; the run split in two (run(n), then runUntil) to check the harness's time
            Mach m = new Mach(Mach.cfg(-1));
            Mach.Image im = m.boot("wfi");
            m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().waitCycles(700).command(LinkWord.BROADCAST, 3 << 24).go();
            m.c.run(1500);
            System.out.printf("wfi after run(1500): cycle %d runAt %d busy %b allInWfi %b%n", m.c.cycle, m.runAt, m.c.cc.busy(), m.allInWfi());
            boolean ok = m.run(m::allStopped, 20000);
            report("wfi", m, ok);
            System.out.println("  r[1] " + all(m, im, "r", 1));
        }
        a2a(-1);
        a2a(4);
        {   // C&C 1-3 as cores: the boot through the host port, until C&C 1 reports the tiles running
            Mach m = new Mach(Mach.coresCfg(-1));
            m.bootCores("wfi");
            boolean ok = m.c.runUntil(m::coresBooted, 400_000);
            System.out.printf("cores: booted %b at cycle %d; bootStart %d; mbox %d %d %d %d; tile 0 pc 0x%x, cc1 pc 0x%x%n", ok, m.c.cycle,
                    m.bootStart, m.mbox(0), m.mbox(1), m.mbox(2), m.mbox(3), m.c.tiles[0].core.pc, m.c.cores.unit(0).core.pc);
        }
    }
}
