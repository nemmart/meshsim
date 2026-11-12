/**
 * MachSelf (WORKER20, P2): v1.3's Mach harness driven through its machine-as-specified paths, printing what a suite
 * reads back, for tests/mach.h's C++ harness to reproduce line for line (tests/test_mach.cpp; fixture
 * tests/mach/machself.out, written by oracle/ref/machself.sh). Run in a scratch directory laid out as ref/meshsim is
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

    /** StageB's machine (W25): the replayer on tile 0 only (testPeripheral, periphType 0), C&C waiting for tile 0's READY
     *  only (cc1p.expect = 1), tiles 1-63 parked; an image booted on tile 0 alone. */
    static void stageb(boolean lone) throws Exception {
        Config k = Config.machine(); k.periphType = 0;
        k.testPeripheral = tile -> tile == 0 ? new Replayer(0) : null;
        Chip c = new Chip(k);
        if (lone) { c.parked = new boolean[64]; java.util.Arrays.fill(c.parked, 1, 64, true); }
        Mach.Image im = new Mach.Image(java.nio.file.Path.of("sw/build"), "regs");
        c.cc1p.expect = 1;
        c.cc.loadWords(0, Registers.PMEM_BASE, im.code);
        c.cc.loadWords(0, im.sym("_data_start"), im.data);
        c.cc.command(0, Registers.CMD_JUMP << 24, im.sym("_start")).go();
        Tile p = c.tiles[0];
        boolean ok = c.runUntil(() -> p.core.halt != Controller.RUN && p.core.halt != 0, 100000);
        System.out.printf("stageb lone %b: %s at cycle %d; tile 0 halt %d pc 0x%x exit %d; PARAMS t0 %x t1 %x; parkedUntouched %b; violations %d%n",
                lone, ok ? "stopped" : "NOT stopped", c.cycle, p.core.halt, p.core.pc, p.ctl.simExitValue,
                p.core.load(c.cycle + 1, 0xFFFFF800, 4), c.tiles[1].core.load(c.cycle + 1, 0xFFFFF800, 4), c.parkedUntouched(), c.violations);
        System.out.print(c.violationReport());
    }

    /** PhaseLog (H3): the a2a's phases (AccountingTest's namer), its report and CSV, plain and shuffled. */
    static void phases(long seed) {
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
        m.c.phases.namer = w -> switch (w >>> 24) { case 1 -> "configure"; case 2 -> "start (a2a)"; case 3 -> "exit"; case Registers.CMD_JUMP -> "boot"; default -> "?"; };
        m.boot("a2a");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
        boolean ok = m.run(m::allStopped, 2_000_000);
        m.c.phases.close();
        System.out.println("phases seed " + seed + ": " + (ok ? "stopped" : "NOT stopped") + " at cycle " + m.c.cycle);
        System.out.print(m.c.phases.report());
        try {
            java.nio.file.Path f = java.nio.file.Files.createTempFile("phases", ".csv");
            m.c.phases.csv(f);
            System.out.print(java.nio.file.Files.readString(f));
            java.nio.file.Files.delete(f);
        } catch (java.io.IOException e) { throw new RuntimeException(e); }
    }

    public static void main(String[] a) throws Exception {
        phases(-1);
        phases(4);
        stageb(true);
        stageb(false);
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
