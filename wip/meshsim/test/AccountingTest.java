import java.nio.file.Path;
import java.util.List;

/**
 * M6 step 2: per-phase accounting (PhaseLog). Runs the all-to-all from C (configure, start, exit) and checks the
 * phase table against C&C's own record and the run's known counts, then prints the tables. Usage:
 * {@code java AccountingTest [shuffleSeed] [full]}; writes results/phases-*.csv.
 */
public final class AccountingTest {
    public static void main(String[] args) throws Exception {
        long shuffle = args.length > 0 ? Long.parseLong(args[0]) : -1;
        boolean full = args.length > 1 && args[1].equals("full");
        int n = full ? 65536 : 2000;
        T.group("all-to-all phases");
        A2A a = new A2A(7, n);
        Mach m = new Mach(Mach.cfg(shuffle));
        m.c.phases.namer = w -> switch (w >>> 24) { case 1 -> "configure"; case 2 -> "start (a2a)"; case 3 -> "exit"; case Registers.CMD_JUMP -> "boot"; default -> "?"; };
        for (Tile p : m.c.tiles) {
            int s = p.idx;
            for (int k = 0; k < a.rec[s].length; k++) p.sram.poke64(A2A.SRC + 2 * k, a.rec[s][k]);
            for (int d = 0; d < 64; d++) p.sram.poke(0xF0000 + d, a.cnt[s][d]);
            for (int src = 0; src < 64; src++) p.sram.poke(0xF0000 + 64 + src, a.cnt[src][s]);
            for (int g = 0; g < 16; g++) p.sram.poke(0xF0000 + 128 + g, a.R[g]);
            p.sram.poke(0xF0000 + 144, 0);
        }
        m.boot("a2a");
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
        T.check(m.run(m::allStopped, 3_000_000) && m.allExited(), "the run finished: " + m.haltSummary());
        m.c.phases.close();
        T.eq(0, a.verify(m.c), "every record landed");
        List<PhaseLog.Phase> ph = m.c.phases.phases;
        List<long[]> cl = m.c.cc.phaseLog;
        T.eq(4, ph.size(), "four phases: the boot (the ROM's GO, 6.2), configure, start, exit");
        for (int i = 0; i + 1 < ph.size(); i++) T.eq(cl.get(i + 1)[0] - cl.get(i)[1], ph.get(i).cycles(), "phase " + i + ": GO -> all READY as C&C saw it");
        long sent = 0; for (int s = 0; s < 64; s++) for (int d = 0; d < 64; d++) sent += a.cnt[s][d];
        T.eq(sent, (long) ph.get(2).n.get("agReq"), "start phase: address-generator requests = records sent");
        T.eq(sent + 64, (long) ph.get(2).n.get("ejected"), "start phase: ejected packets = records + the exit command (one broadcast packet per tile, sent right after GO)");
        T.eq(64L, (long) ph.get(1).n.get("ejected"), "configure phase: the start command, one packet per tile");
        T.eq(sent, (long) ph.get(2).n.get("injected"), "start phase: injected packets = records");
        T.eq(0, (long) ph.get(2).n.get("agLate"), "start phase: never late");
        T.check(ph.get(2).peak.get("link") > ph.get(1).peak.get("link"), "peaks restart per phase (start " + ph.get(2).peak.get("link") + " > configure " + ph.get(1).peak.get("link") + ")");
        String rep = m.c.phases.report();
        System.out.println("phase accounting, 16-group all-to-all from C (Data(7, " + n + ")) [meas: AccountingTest]:\n" + rep);
        m.c.phases.csv(Path.of("results/phases-a2a" + (full ? "-full" : "") + ".csv"));
        System.exit(T.done("AccountingTest" + (full ? " full" : "")));
    }
}
