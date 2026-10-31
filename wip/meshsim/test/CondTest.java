import java.util.ArrayList;
import java.util.List;

/**
 * The errors end to end (spec 4.13, 6.1; v1.2), provoked from C (sw/tests/cond.c) on a booted machine: each case sets
 * its ERRORS bit on the tiles that act; the image has enabled interrupts, so the hard interrupt (or, for a bus error or
 * an illegal instruction, the synchronous trap) takes the tile to the boot ROM's handler, which sends its fixed set of
 * 24 reports to C&C 1 over the request bus and parks; the tile's error line reaches C&C 1's peripheral (ERROR_LINES,
 * the unit-error bit) and the toast pin rises. Then the outside world resets the chip and C&C 1 boots the tiles again
 * (6.1, 6.2): every ERRORS, line and the toast pin clear. Errors 4, 5 and 6, which a program can't provoke in the model
 * (5 and 6 are hardware-fault detectors; v1.2's write path has no half-rate spanning write to overflow the write
 * engine), are injected into ERRORS from Java to check the rest of the path.
 * Usage: java CondTest [shuffle seed | -1]
 */
public final class CondTest {
    static long shuffle = -1;
    static void out(String s) { System.out.println(s); }

    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        Mach m = new Mach(Mach.cfg(shuffle));
        m.c.cfg.maxViolationMessages = 1000;
        Mach.Image im = m.boot("cond");
        T.check(m.run(() -> m.c.cc.lastRunAt >= 0 && !m.c.cc.busy(), 20000), "booted");
        // {case, tiles that act..., the tile whose ERRORS rises, its mcause}
        int ext = Controller.CAUSE_EXTERNAL;
        int[][] cases = { { 1, 5, 5, ext }, { 2, 6, 6, ext }, { 3, 7, 7, ext }, { 7, 12, 12, ext }, { 8, 13, 13, ext },
                          { 9, 20, 20, Controller.CAUSE_STORE_FAULT }, { 10, 21, 21, Controller.CAUSE_ILLEGAL }, { 14, 22, 22, ext } };
        for (int[] c : cases) runCase(m, im, c[0], new int[] { c[1] }, c[2], c[3], 1 << c[0]);
        runCase(m, im, 0, new int[] { 8, 10, 1, 17, 9 }, 9, ext, 1 << 0);
        for (int bit : new int[] { 4, 5, 6 }) injected(m, im, bit);
        // meshsim's own flags for the deliberate bugs: each names its invariant (5, 9, 3, 8, 4, 1), nothing else
        String[] want = { "invariant 5", "invariant 9", "invariant 3", "invariant 8", "invariant 4", "invariant 1" };
        boolean named = m.c.violationMessages.stream().allMatch(v -> java.util.Arrays.stream(want).anyMatch(v::contains));
        T.check(named && m.c.violations == m.c.violationMessages.size(), "every violation flags a deliberate bug: " + m.c.violationReport());
        for (String w : want) T.check(m.c.violationMessages.stream().anyMatch(v -> v.contains(w)), "meshsim flagged " + w);
        out("errors [meas: CondTest]: errors 0, 1, 2, 3, 7, 8, 9, 10 and 14 provoked from C, 4, 5 and 6 injected: each sets its ERRORS bit and takes the tile to the boot ROM's handler (the hard interrupt, or the trap for 9 and 10), which sends its 24 reports to C&C 1 in order and parks; the error line reaches ERROR_LINES, the unit-error bit and the toast pin rise; a soft reset clears them all and the tiles boot again");
        System.exit(T.done("CondTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    /** One case: the command to the acting tiles; drain C&C 1's reports until the erring tile has parked; check; reset and boot again. */
    static void runCase(Mach m, Mach.Image im, int k, int[] acting, int erring, int cause, int bits) {
        T.group("error " + k);
        long lo = 0, hi = 0; for (int a : acting) if (a < 32) lo |= 1L << a; else hi |= 1L << (a - 32);
        Tile p = m.c.tiles[erring];
        m.c.cc.command(LinkWord.BROADCAST, k << 24, (int) lo, (int) hi).go();
        List<long[]> reports = drainUntilParked(m, p);
        check(m, p, k, cause, bits, reports);
        resetAndBoot(m, im);
    }
    static void injected(Mach m, Mach.Image im, int bit) {
        T.group("error " + bit + " (injected)");
        Tile p = m.c.tiles[30];
        p.ctl.conditions |= 1 << bit;                     // as if the unit's FIFO had overflowed
        List<long[]> reports = drainUntilParked(m, p);
        check(m, p, bit, Controller.CAUSE_EXTERNAL, 1 << bit, reports);
        resetAndBoot(m, im);
    }
    /** C&C 1's model drains target 0's receive queue (its handler would, 6.1) until tile p has parked and its 24 reports are in. */
    static List<long[]> drainUntilParked(Mach m, Tile p) {
        List<long[]> got = new ArrayList<>();
        m.run(() -> {
            long now = m.c.cycle + 1;
            while (m.c.reqBus.pending(0, now)) { long[] r = m.c.reqBus.peekRequest(0, now); m.c.reqBus.receive(0, now); if ((r[0] & 0x7F) == p.idx) got.add(r); }
            return p.core.parked && got.size() >= 24;
        }, 200000);
        m.c.run(10);
        return got;
    }
    static void check(Mach m, Tile p, int k, int cause, int bits, List<long[]> reports) {
        String tl = "error " + k + ", tile " + p.idx + ": ";
        T.eq(bits, p.ctl.conditions & bits, tl + "its ERRORS bit");
        T.check(p.core.parked && p.core.inHandler, tl + "the handler ran and parked");
        T.eq(cause, p.core.trapCause, tl + "mcause");
        T.eq(24, reports.size(), tl + "the fixed set of 24 reports reached C&C 1");
        boolean inOrder = true; for (int i = 0; i < reports.size(); i++) if (((reports.get(i)[0] >>> 21) & 0x7FF) != i) inOrder = false;
        T.check(inOrder, tl + "reports 0-23, in order");
        if (reports.size() > 12) T.eq(p.ctl.conditions, (int) reports.get(12)[1], tl + "report 12 carries ERRORS");
        T.eq(1, (m.c.cc1p.read(m.c.cycle, Cnc1Peripheral.ERROR_LINES + p.idx / 32) >>> (p.idx & 31)) & 1, tl + "its error line at C&C 1's ERROR_LINES");
        T.eq(1, m.c.cc1p.read(m.c.cycle, 2) & 1, tl + "C&C 1's unit-error bit");
        T.check(m.c.cc1p.toast, tl + "the toast pin");
    }
    /** The outside world's soft reset, then C&C 1 boots the tiles again (the image is still in memory): 6.1, 6.2. */
    static void resetAndBoot(Mach m, Mach.Image im) {
        long r0 = m.c.resets;
        m.c.cc.reset(false).command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start")).runWhenReady();
        boolean ok = m.run(() -> m.c.resets > r0 && !m.c.cc.busy(), 20000);
        m.c.run(50);
        int e = 0; for (Tile q : m.c.tiles) e |= q.ctl.conditions;
        T.check(ok && e == 0 && !m.c.cc1p.toast && (m.c.cc1p.read(m.c.cycle, 2) & 1) == 0, "the reset cleared every ERRORS, line and the toast pin, and the tiles booted again");
        boolean waiting = true; for (Tile q : m.c.tiles) if (q.core.inHandler || q.core.halt != Controller.RUN) waiting = false;
        T.check(waiting, "every tile back in its command loop");
    }
}
