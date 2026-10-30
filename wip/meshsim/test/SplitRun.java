import java.io.DataInputStream;
import java.io.FileInputStream;
import java.io.BufferedInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

/**
 * Worker 4's split (design_v2/sort/split-bisect, split.c unchanged; sw/ext/split-bisect/splitdrv.c calls it) on the
 * machine as specified: C&C boots it, then answers its 66 barriers with one-line commands and GO. Data sets are
 * tools/export_sets.py's .bin files (worker 4's format). Checks every tile's c[] against the exact cuts, and reports
 * S (GO of the split's command -> all READY after XA) and the step loop's cycles per search step, measured as worker
 * 4's ISS does: all cycles inside step_loop (waits included) over all tiles / all search steps (read-port stores
 * made inside step_loop). Usage: {@code java SplitRun FILE.bin ...}.
 * Also the read-port results looped back and the overlap check's count, and one tab-separated RESULT line per set
 * (the verification kit's summary reads it; M9's SplitRun has the same human-readable lines without it).
 */
public final class SplitRun {
    public static void main(String[] args) throws IOException {
        for (String f : args) run(Path.of(f));
        System.exit(T.done("SplitRun"));
    }
    static void run(Path f) throws IOException {
        ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(f)).order(ByteOrder.LITTLE_ENDIAN);
        byte[] magic = new byte[4]; b.get(magic);
        byte[] nm = new byte[64]; b.get(nm); String name = new String(nm).trim().replace("\0", "");
        int mode = b.getInt();
        int[] ns = new int[64]; for (int i = 0; i < 64; i++) ns[i] = b.getInt();
        int[][] keys = new int[64][]; for (int i = 0; i < 64; i++) { keys[i] = new int[ns[i]]; for (int k = 0; k < ns[i]; k++) keys[i][k] = b.getInt(); }
        int[] R = new int[65]; for (int i = 0; i < 65; i++) R[i] = b.getInt();
        int[][] exp = new int[64][64]; for (int q = 1; q < 64; q++) for (int i = 0; i < 64; i++) exp[q][i] = b.getInt();

        Config k = Config.machine();     // checkCommandRead on: the split's barriers are to go through wait_for_command()
        Mach m = new Mach(k);
        for (Tile p : m.c.tiles) {
            int i = p.idx;
            for (int r = 0; r < ns[i]; r++) p.sram.poke64(0x60000 + 2 * r, (long) r << 32 | (keys[i][r] & 0xFFFFFFFFL));
            p.sram.poke(0xF0000, ns[i]);
        }
        Mach.Image im = m.boot("split");
        int lo = im.sym("step_loop.constprop.0"), hi = lo + 0x3c0;
        for (Tile p : m.c.tiles) { p.core.profLo = lo; p.core.profHi = hi; p.core.traceReg = true; }
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go();
        for (int j = 0; j < 66; j++) m.c.cc.command(LinkWord.BROADCAST, 2 << 24).go();
        m.c.cc.command(LinkWord.BROADCAST, 3 << 24).go();
        boolean ok = m.run(m::allStopped, 50_000_000);
        T.check(ok && m.allExited(), name + ": every tile finished: " + m.haltSummary());
        int bad = 0;
        for (Tile p : m.c.tiles) for (int q = 1; q < 64; q++) if (m.word(p.idx, im, "c", q) != exp[q][p.idx]) bad++;
        T.eq(0, bad, name + ": every tile's c[1..63] exact");
        List<long[]> pl = m.c.cc.phaseLog;
        long S = pl.get(67)[0] - pl.get(0)[1];
        long cyc = 0, st = 0, unread = 0, lc = 0, ls = 0; for (Tile p : m.c.tiles) { cyc += p.core.profCycles; st += p.core.profStores; unread += p.cmem.unreadCommands; lc += p.core.loopCycles; ls += p.core.loopStores; }
        System.out.printf("  as worker 4's ISS measures it (TRACE 0x600 -> 0x601, step_loop and the final key read): %,d search steps, %.2f cycles per step; step_loop alone: %,d steps, %.2f%n", ls, (double) lc / ls, st, (double) cyc / st);
        long stArr = 0; for (Tile p : m.c.tiles) for (int r = 0; r <= 32; r++) stArr += m.word(p.idx, im, "steps", r);
        long ow = 0, ovl = 0, rpr = 0; for (Tile p : m.c.tiles) { ow += p.cmem.overwrittenUnread; ovl += p.dir.readPortOverlaps; rpr += p.dir.readPortResults; }
        System.out.println("  split_steps[] summed over tiles: " + stArr + "; mode-7 words overwritten unread (invariant 8, a statistic): " + ow + "; violations " + m.c.violations + (m.c.violationMessages.isEmpty() ? "" : ", first: " + m.c.violationMessages.get(0)));
        System.out.printf("%-26s S %,9d cycles (GO of the split's command -> all READY after XA, 66 barriers); step loop %,d search steps, %.2f cycles per step; %d barrier commands per tile left unread in slot 0%n",
                name, S, st, (double) cyc / st, unread / 64);
        // Read-port results through the loopback (spec 4.3) and the overlap check
        System.out.println("  read-port results looped back: " + rpr + "; packets from outside accepted with results outstanding (4.3's rule): " + ovl);
        System.out.printf("RESULT\t%s\texact=%s\tS=%d\tsteps=%d\tcps=%.4f\tiss_steps=%d\tiss_cps=%.4f\tinv8=%d\tviolations=%d\toverlaps=%d\tunread_cmd=%d%n",
                name, bad == 0, S, st, (double) cyc / st, ls, (double) lc / ls, ow, m.c.violations, ovl, unread / 64);
    }
}
