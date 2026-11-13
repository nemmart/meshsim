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

    /** SortTest's cfg.<field>=value (Options.setField), ref's own. */
    static void setField(Config k, String f, String v) throws Exception {
        var fld = Config.class.getDeclaredField(f); fld.setAccessible(true);
        Class<?> t = fld.getType();
        if (t == int.class) fld.setInt(k, Integer.parseInt(v));
        else if (t == long.class) fld.setLong(k, Long.parseLong(v));
        else if (t == boolean.class) fld.setBoolean(k, Boolean.parseBoolean(v));
    }
    static final String[] KIT = { "cfg.linkLatency=4", "cfg.cmLatency=4", "cfg.goLatency=4", "cfg.runLatency=8", "cfg.areaQueueDepth=4",
            "cfg.sramReadLatency=3", "cfg.tsEngineLatency=2", "cfg.skewSeed=5", "cfg.checkDisjoint=false", "cfg.skew=true" };
    static void a2a(long seed) { a2a(seed, false); }
    static void a2a(long seed, boolean kit) { a2a(seed, kit, true); }
    static void a2a(long seed, boolean kit, boolean s1) {
        Config cfg = Mach.cfg(seed);
        cfg.measureS1 = s1;
        if (kit) for (String a : KIT) { int eq = a.indexOf('='); try { setField(cfg, a.substring(4, eq), a.substring(eq + 1)); } catch (Exception e) { throw new RuntimeException(e); } }
        Mach m = new Mach(cfg);
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
        report("a2a seed " + seed + (kit ? " kit" : "") + (s1 ? "" : " no S1"), m, ok);
        {   // the canonical state fingerprint of the chip at its last cycle (StateFp, as M2Prog prints it): B25's
            StateFp.Out o; long all = 0x6A09E667F3BCC908L; int nn = 0;
            for (Tile p : m.c.tiles) for (String u : StateFp.TILE_UNITS) { o = new StateFp.Out(); StateFp.unit(o, m.c, p, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); nn++; }
            for (String u : StateFp.CHIP_UNITS) { o = new StateFp.Out(); StateFp.chipUnit(o, m.c, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); nn++; }
            System.out.printf("  fp %016x%n", StateFp.fmix(all ^ nn));
        }
        System.out.print(m.c.agReport());
        System.out.println("  " + m.c.s1.report(m.c.cfg.K));
        StringBuilder pl = new StringBuilder("  phaseLog");
        for (long[] e : m.c.cc.phaseLog) pl.append(' ').append(java.util.Arrays.toString(e));
        System.out.println(pl);
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

    /** H6: the TRACE stores of search.c (SearchBench's mark(), through TileControl.traceHook), N = 4096, one run. */
    static void trace() throws Exception { trace(false); }
    /** with rdpeak: at SRAM read latency 4, variant 1, PhaseLog's CSV (the read port's peak: a store and an injection in one cycle). */
    static void trace(boolean rdpeak) throws Exception {
        int N = 4096;
        Config cfg = Mach.cfg(-1);
        if (rdpeak) cfg.sramReadLatency = 4;
        Mach m = new Mach(cfg);
        java.util.Random r = new java.util.Random(N);
        for (Tile p : m.c.tiles) {
            long[] k = new long[N]; for (int i = 0; i < N; i++) k[i] = r.nextInt() & 0xFFFFFFFFL;
            java.util.Arrays.sort(k);
            for (int i = 0; i < N; i++) p.sram.poke(i, (int) k[i]);
            p.sram.poke(0xF0000, N); p.sram.poke(0xF0001, 0);
            for (int i = 0; i < 64; i++) p.sram.poke(0xF0002 + i, i == 0 ? 0 : i == 1 ? -1 : r.nextInt());
        }
        m.boot("search");
        java.util.List<java.util.List<long[]>> marks = new java.util.ArrayList<>(); for (int i = 0; i < 64; i++) marks.add(new java.util.ArrayList<>());
        for (Tile p : m.c.tiles) { final int i = p.idx; p.ctl.traceHook = (t, v) -> marks.get(i).add(new long[] { t, v }); }
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | (rdpeak ? 1 : 0) << 8 | 4).go();
        m.run(() -> !m.c.cc.busy(), 5_000_000);
        long[] d0 = new long[64]; for (Tile p : m.c.tiles) d0[p.idx] = p.ctl.dones;
        boolean ok = m.run(() -> { for (Tile p : m.c.tiles) if (p.ctl.dones <= d0[p.idx]) return false; return true; }, 5_000_000);
        if (rdpeak) {
            m.c.phases.close();
            java.nio.file.Path f = java.nio.file.Files.createTempFile("phases", ".csv");
            m.c.phases.csv(f);
            System.out.println("rdpeak: " + (ok ? "finished" : "NOT finished") + " at cycle " + m.c.cycle);
            System.out.print(java.nio.file.Files.readString(f));
            java.nio.file.Files.delete(f);
            return;
        }
        System.out.println("trace: " + (ok ? "finished" : "NOT finished") + " at cycle " + m.c.cycle);
        for (int t : new int[] { 0, 7, 63 }) {
            StringBuilder sb = new StringBuilder("  tile " + t + ": " + marks.get(t).size() + " TRACE stores");
            for (long[] e : marks.get(t)) sb.append(" ").append(e[0]).append(":").append(Long.toHexString(e[1]));
            System.out.println(sb);
        }
    }

    static String fp(Mach m) {
        StateFp.Out o; long all = 0x6A09E667F3BCC908L; int nn = 0;
        for (Tile p : m.c.tiles) for (String u : StateFp.TILE_UNITS) { o = new StateFp.Out(); StateFp.unit(o, m.c, p, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); nn++; }
        for (String u : StateFp.CHIP_UNITS) { o = new StateFp.Out(); StateFp.chipUnit(o, m.c, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); nn++; }
        return String.format("%016x", StateFp.fmix(all ^ nn));
    }
    /** The chip's StateFp with C&C's model's error-line words left out (D49: ref feeds C&C's model, after a reset, an error
     *  line computed before it, so its error bit latches about the error latency later; v1.35 doesn't). */
    static final java.util.Set<String> D49 = java.util.Set.of("errorBit", "errLineSeen", "errLineLast", "errLine.n", "errLine");
    static String fpD49(Mach m) {
        StateFp.Out o; long all = 0x6A09E667F3BCC908L; int nn = 0;
        for (Tile p : m.c.tiles) for (String u : StateFp.TILE_UNITS) { o = new StateFp.Out(); StateFp.unit(o, m.c, p, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); nn++; }
        for (String u : StateFp.CHIP_UNITS) {
            o = u.equals("cnc") ? new StateFp.Out() { @Override void w(String name, long v) { if (!D49.contains(name)) super.w(name, v); } } : new StateFp.Out();
            StateFp.chipUnit(o, m.c, u, m.c.cycle); all = StateFp.fmix(all ^ o.done()); nn++;
        }
        return String.format("%016x", StateFp.fmix(all ^ nn));
    }
    /** B20: the a2a's run cut by C&C's queued soft reset mid-phase, the state across it (StateFp), then the a2a again. */
    static void resetCase(long seed) {
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
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go();
        m.run(() -> m.c.cycle >= 5200, 100000);                          // the a2a phase under way
        m.c.cc.reset(false);
        m.run(() -> m.c.resets == 1, 100000);
        StringBuilder sb = new StringBuilder("reset seed " + seed + ": at " + m.c.lastResetAt + " fp " + fp(m));
        m.c.step(); sb.append("; next fp ").append(fp(m));
        m.run(() -> !m.c.cc.busy(), 100000);
        sb.append("; queue done at ").append(m.c.cycle).append(" fp ").append(fp(m));
        m.boot("a2a");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
        boolean ok = m.run(m::allStopped, 20_000);
        sb.append("; rerun ").append(ok ? "stopped" : "NOT stopped").append(" at ").append(m.c.cycle).append(" fp ").append(fp(m)).append(" violations ").append(m.c.violations);
        System.out.println(sb);
        for (String l : m.c.log) if (l.contains("reset")) System.out.println("  " + l);
    }

    /** CtrlTest faults' reads: the request ports' and the request bus's idle(), each cycle while C&C 1's model drains
     *  the handlers' reports; C&C 1's toast latch, and the soft reset clearing it. */
    static boolean track(Mach m, long[] k, boolean[] was) {
        boolean pi = true;
        for (Tile p : m.c.tiles) if (!p.req.idle()) { pi = false; k[2]++; }
        boolean bi = m.c.reqBus.idle();
        if (bi) k[0]++;
        if (pi) k[1]++;
        if (bi != was[0]) { k[3] = m.c.cycle; k[4]++; was[0] = bi; }
        if (pi != was[1]) { k[5] = m.c.cycle; was[1] = pi; }
        return pi;
    }
    static void faultsReads() {
        Mach m = new Mach(Mach.cfg(-1));
        m.c.cfg.checkErrors = false;
        Mach.Image im = m.boot("faults");
        long[] k = new long[6];                                           // bus idle, ports idle, port-busy tile-cycles, last changes
        boolean[] was = { false, false };
        m.run(() -> { track(m, k, was); return false; }, 5000);         // the faults, the handlers' reports, nothing drained
        boolean ok = m.run(() -> {
            long now = m.c.cycle + 1;
            while (m.c.reqBus.pending(0, now)) m.c.reqBus.receive(0, now);
            boolean pi = track(m, k, was), bi = m.c.reqBus.idle();
            for (Tile p : m.c.tiles) if (!(p.core.parked || p.core.halt != Controller.RUN)) return false;
            return pi && bi && !m.c.reqBus.pending(0, now);
        }, 200000);
        System.out.printf("faults reads: %s at %d; bus idle %d cycles (%d changes, last at %d), ports idle %d cycles (last change at %d), port busy %d tile-cycles; toast %b at %d%n",
                ok ? "drained" : "NOT drained", m.c.cycle, k[0], k[4], k[3], k[1], k[5], k[2], m.c.cc1p.toast, m.c.cc1p.toastAt);
        // the pops of one cycle and their credits on their way, with the parked cores' park loops
        System.out.printf("  received %d; fp %s", m.c.reqBus.received[0], fp(m));
        m.c.step(); System.out.printf("; next %s", fp(m));
        m.c.step(); System.out.printf("; next %s; credits %d%n", fp(m), m.c.reqBus.credits[0]);
        m.c.cc.reset(false).command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start"), 0x600D).runWhenReady();
        ok = m.run(() -> m.c.resets == 1, 20000);
        System.out.printf("  the reset: %s at %d; toast %b at %d%n", ok ? "made" : "NOT made", m.c.lastResetAt, m.c.cc1p.toast, m.c.cc1p.toastAt);
    }

    static long routerOverflows(Tile p) { long r = 0; for (WordFIFO[] fs : p.router.q) for (WordFIFO f : fs) r += f.overflowCount; return r; }
    static int errorsOr(Mach m) { int e = 0; for (Tile q : m.c.tiles) e |= q.ctl.conditions; return e; }
    /** The reset with a router FIFO overflowing in the reset's own cycle: cond.c's error 0 (tiles 8, 10, 1 and 17 flood
     *  tile 9), C&C 1 draining the reports until tile 9 parks, then the soft reset and the boot again, as CondTest does. */
    static void floodReset(long seed) {
        Mach m = new Mach(Mach.cfg(seed));
        m.c.cfg.maxViolationMessages = 1000;
        Mach.Image im = m.boot("cond");
        m.run(() -> m.c.cc.lastRunAt >= 0 && !m.c.cc.busy(), 20000);
        Tile p = m.c.tiles[9];
        m.c.cc.command(LinkWord.BROADCAST, 0, (1 << 8) | (1 << 10) | (1 << 1) | (1 << 17) | (1 << 9), 0).go();
        int[] got = { 0 };
        m.run(() -> {
            long now = m.c.cycle + 1;
            while (m.c.reqBus.pending(0, now)) { long[] r = m.c.reqBus.peekRequest(0, now); m.c.reqBus.receive(0, now); if ((r[0] & 0x7F) == 9) got[0]++; }
            return p.core.parked && got[0] >= 2;
        }, 200000);
        m.c.run(10);
        StringBuilder sb = new StringBuilder("flood reset seed " + seed + ": parked at " + m.c.cycle + " reports " + got[0] + " tile 9 overflows " + routerOverflows(p)
                + " ERRORS 0x" + Integer.toHexString(p.ctl.conditions) + " fp " + fp(m));
        m.c.cc.reset(false).command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start")).runWhenReady();
        m.run(() -> m.c.resets == 1, 20000);
        sb.append("; reset at ").append(m.c.lastResetAt).append(" overflows ").append(routerOverflows(p)).append(" fp(D49) ").append(fpD49(m));
        m.c.step();
        sb.append("; next ERRORS 0x").append(Integer.toHexString(errorsOr(m))).append(" fp(D49) ").append(fpD49(m));
        boolean ok = m.run(() -> !m.c.cc.busy(), 20000);
        m.c.run(50);
        sb.append("; booted ").append(ok).append(" at ").append(m.c.cycle).append(" ERRORS 0x").append(Integer.toHexString(errorsOr(m)))
          .append(" unit-error ").append(m.c.cc1p.read(m.c.cycle, 2) & 1).append(" toast ").append(m.c.cc1p.toast)
          .append(" line 9 ").append(m.c.cc1p.read(m.c.cycle, Cnc1Peripheral.ERROR_LINES) >>> 9 & 1)
          .append(" overflows ").append(routerOverflows(p)).append(" fp(D49) ").append(fpD49(m)).append("; violations ").append(m.c.violations);
        System.out.println(sb);
        for (String v : m.c.violationMessages) if (v.contains("invariant 1)")) System.out.println("  " + v);
    }

    public static void main(String[] a) throws Exception {
        resetCase(-1);
        resetCase(4);
        faultsReads();
        floodReset(-1);
        floodReset(4);
        trace();
        trace(true);
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
        a2a(-1, true);
        a2a(4, false, false);
        {   // C&C 1-3 as cores: the boot through the host port, until C&C 1 reports the tiles running
            Mach m = new Mach(Mach.coresCfg(-1));
            m.bootCores("wfi");
            boolean ok = m.c.runUntil(m::coresBooted, 400_000);
            System.out.printf("cores: booted %b at cycle %d; bootStart %d; mbox %d %d %d %d; tile 0 pc 0x%x, cc1 pc 0x%x%n", ok, m.c.cycle,
                    m.bootStart, m.mbox(0), m.mbox(1), m.mbox(2), m.mbox(3), m.c.tiles[0].core.pc, m.c.cores.unit(0).core.pc);
        }
    }
}
