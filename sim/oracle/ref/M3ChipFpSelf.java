import java.nio.file.Path;

/**
 * WORKER13: a local check of StateFpM3.write (sim/tests/AUDIT-M3.md 6.6-6.9) on the full chip with cores, the Tracer and
 * the replayers, no inputs needed: the boot of T2's machine (empty layouts: C&C 3's firmware finds no header and stops),
 * every unit fingerprinted every 997 cycles to cycle 30,000. Passes when the run's figures equal a run without
 * fingerprints (neutral) and a second fingerprinted run gives the same lines (deterministic).
 */
public final class M3ChipFpSelf {
    static String run(boolean fp, java.io.StringWriter sw) {
        Config k = Mach.coresCfg(-1); k.cc3Tracer = true;
        Mach m = new Mach(k);
        m.c.cores.tracer.mem = new MemorySystem(new FileStore());
        java.io.PrintWriter w = new java.io.PrintWriter(sw);
        m.bootCores(new Mach.Image(Path.of("sw/replay/build"), "replay_ctl"), new Mach.Image(Path.of("sw/tracer/build"), "tracer_ctl"));
        long[] booted = { -1 };
        m.run(() -> {
            long c = m.c.cycle;
            if (booted[0] < 0 && m.coresBooted()) booted[0] = c;
            if (fp && c > 0 && c % 997 == 0) StateFpM3.write(w, m.c, c, true, null);
            return c >= 30_000;
        }, 40_000);
        w.flush();
        StringBuilder b = new StringBuilder("booted " + booted[0] + "; ");
        for (int i = 0; i < 64; i++) b.append(m.c.tiles[i].core.instructions).append(',');
        for (int c = 0; c < 3; c++) b.append(" cc").append(c + 1).append(' ').append(m.c.cores.unit(c).core.instructions).append(" pc ").append(Integer.toHexString(m.c.cores.unit(c).core.pc));
        return b.toString();
    }
    public static void main(String[] a) {
        java.io.StringWriter s1 = new java.io.StringWriter(), s2 = new java.io.StringWriter(), s0 = new java.io.StringWriter();
        String plain = run(false, s0), f1 = run(true, s1), f2 = run(true, s2);
        long lines = s1.toString().lines().count(), distinct = s1.toString().lines().map(l -> l.substring(l.lastIndexOf(' ') + 1)).distinct().count();
        boolean neutral = plain.equals(f1), det = s1.toString().equals(s2.toString());
        System.out.println(plain);
        System.out.printf("lines %d, distinct hashes %d; neutral %s, deterministic %s%n", lines, distinct, neutral, det);
        System.out.println(neutral && det && lines > 1000 ? "M3ChipFpSelf: PASS" : "M3ChipFpSelf: FAIL");
        if (a.length > 0) System.out.print(s1.toString().lines().limit(Integer.parseInt(a[0])).collect(java.util.stream.Collectors.joining("\n", "", "\n")));
        System.exit(neutral && det ? 0 : 1);
    }
}
