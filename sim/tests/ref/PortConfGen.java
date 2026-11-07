import java.io.BufferedWriter;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.SplittableRandom;

/**
 * Port conformance, the script (WORKER3; sim/src/PORTS.md section 10): a seeded script of puts, takes and peeks for one
 * port kind, read by both sides (PortConfDriver over ref's bus classes, and the C++ ports). Usage:
 * <pre>  java PortConfGen KIND P1 [P2] SEED CYCLES &gt; script</pre>
 * KIND and its parameters: {@code link L} (WordDelayLine, latency L), {@code fifo N} (WordFIFO, depth N), {@code credit N}
 * (WordBackpressureFIFO), {@code hold} (WordHold), {@code reg} (WordLatch), {@code rlink L N}
 * (WordBackpressureDelayLine, latency L, capacity N).
 * <p>The script is stateless: every op whose legality depends on the port's state is either always legal for the kind
 * (a Fifo's put; a Hold's put; a take or a peek, which the driver performs only if something is visible) or guarded
 * (G: put only if the kind's guard holds, judged by the driver at that point of the cycle). So any seed gives a legal
 * script and nothing here models the port. The generator varies the producer's and the consumer's rates in phases of 1-64
 * cycles (from idle to every cycle), so FIFOs fill, overflow, drain and sit empty, and links back up to their guard.
 * At most one put-like op and one take per cycle (Bus.java: one put and one take per bus per cycle).
 */
final class PortConfGen {
    static final double[] RATES = { 0.0, 0.1, 0.5, 0.9, 1.0 };

    public static void main(String[] a) throws IOException {
        if (a.length < 3) usage();
        String kind = a[0];
        int nParams = switch (kind) { case "link", "fifo", "credit" -> 1; case "rlink" -> 2; case "hold", "reg" -> 0; default -> -1; };
        if (nParams < 0 || a.length != 3 + nParams) usage();
        long seed = Long.parseLong(a[1 + nParams]);
        long cycles = Long.parseLong(a[2 + nParams]);
        StringBuilder params = new StringBuilder();
        for (int i = 0; i < nParams; i++) params.append(' ').append(Integer.parseInt(a[1 + i]));
        try (Writer w = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            write(w, kind, params.toString(), seed, cycles);
        }
    }

    static void usage() {
        System.err.println("usage: java PortConfGen link L | fifo N | credit N | hold | reg | rlink L N  SEED CYCLES");
        System.exit(2);
    }

    static void write(Writer w, String kind, String params, long seed, long cycles) throws IOException {
        SplittableRandom r = new SplittableRandom(seed);
        w.write("# portconf v1\n# kind " + kind + params + "\n# seed " + seed + " cycles " + cycles + "\n");
        // the put op: P always legal; G guarded (link, credit, rlink); R a Hold's put that retries a refused word instead
        String putOp = switch (kind) { case "link", "credit", "rlink" -> "G"; case "hold" -> "R"; default -> "P"; };
        long phaseEnd = 0; double p = 0, q = 0;
        long[] regVals = { 0, 1, r.nextLong() };       // a Reg's values: few, so a put often repeats the registered one
        StringBuilder line = new StringBuilder();
        String[] ops = new String[3];
        for (long t = 1; t <= cycles; t++) {
            if (t > phaseEnd) { phaseEnd = t + 1 + r.nextInt(64); p = RATES[r.nextInt(RATES.length)]; q = RATES[r.nextInt(RATES.length)]; }
            int n = 0;
            if (r.nextDouble() < p) {
                long x, y, z;
                if (kind.equals("reg")) { int k = r.nextInt(4); x = k < 3 ? regVals[k] : r.nextLong(); y = r.nextInt(2); z = 0; }
                else { x = r.nextLong(); y = r.nextLong(); z = r.nextLong(); }
                ops[n++] = putOp + ":" + hex(x) + "," + hex(y) + "," + hex(z);
            }
            if (r.nextDouble() < q) ops[n++] = "T";
            if (r.nextDouble() < 0.2) ops[n++] = "K";
            for (int i = n - 1; i > 0; i--) { int j = r.nextInt(i + 1); String s = ops[i]; ops[i] = ops[j]; ops[j] = s; }
            line.setLength(0);
            line.append(t);
            for (int i = 0; i < n; i++) line.append(' ').append(ops[i]);
            w.write(line.append('\n').toString());
        }
    }

    static String hex(long v) { return Long.toHexString(v); }
}
