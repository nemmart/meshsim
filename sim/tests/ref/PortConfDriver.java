import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.Writer;

/**
 * Port conformance, ref's side (WORKER3; sim/src/PORTS.md section 10): runs a PortConfGen script on one of ref's bus
 * classes and writes one observation line per cycle. The C++ ports run the same script and must write the same lines.
 * Compiled into ref's default package (DECISIONS D5: ref's classes are package-private); ref is not changed. Usage:
 * <pre>  java -cp REF_BUILD:. PortConfDriver SCRIPT &gt; observations</pre>
 *
 * <p>Each script line is a cycle t (from 1, consecutive) and its ops, performed in the line's order at t:
 * <pre>
 *   P:a,b,c   put (fifo, hold's plain put, reg)                         -> P
 *   G:a,b,c   put if the guard holds, else nothing                       -> G | G:-
 *             link: in flight + visible below the capacity (L + 2); credit, rlink: canPut(t)
 *   R:a,b,c   hold: if the put of t - 1 was refused, retry it (the same words), else put a,b,c   -> R | P
 *   T         take if something is visible (reg: read, non-destructive)  -> T:a,b,c | T:-
 *   K         peek if something is visible                               -> K:a,b,c | K:-
 * </pre>
 * After the ops, the cycle's queries, always (Q), fields in this order:
 * <pre>
 *   link    ready, inFlight
 *   fifo    ready, size, sizeAtStart, overflowCount, overflowCycle (-1: none)
 *   credit  ready, size, canPut
 *   hold    ready, canPut, refused, heldAtStart, refusals
 *   reg     ready
 *   rlink   ready, canPut, inFlight
 * </pre>
 * An observation line is {@code t tok... Q:f1,f2,...}; words are lowercase hex without leading zeros, booleans 0 or 1.
 * A bus's own exception (a simulator-bug check in ref) ends the run with exit status 1: a legal script never raises one.
 */
final class PortConfDriver {
    public static void main(String[] a) throws IOException {
        if (a.length != 1) { System.err.println("usage: java PortConfDriver SCRIPT"); System.exit(2); }
        try (BufferedReader in = new BufferedReader(new FileReader(a[0]), 1 << 16);
             Writer out = new BufferedWriter(new OutputStreamWriter(System.out), 1 << 16)) {
            run(in, out);
        } catch (IllegalStateException e) {
            System.out.flush();
            System.err.println("PortConfDriver: ref raised: " + e.getMessage());
            System.exit(1);
        }
    }

    // The port under test, one of these.
    String kind;
    WordDelayLine link; WordFIFO fifo; WordBackpressureFIFO credit; WordHold hold; WordLatch reg; WordBackpressureDelayLine rlink;
    int linkCapacity;

    static void run(BufferedReader in, Writer out) throws IOException {
        PortConfDriver d = new PortConfDriver();
        long expect = 1;
        StringBuilder o = new StringBuilder();
        for (String line; (line = in.readLine()) != null; ) {
            if (line.startsWith("#")) { if (line.startsWith("# kind ")) d.make(line.substring(7).trim().split(" ")); continue; }
            if (d.kind == null) throw new IllegalArgumentException("script: no '# kind' line before the first cycle");
            String[] f = line.split(" ");
            long t = Long.parseLong(f[0]);
            if (t != expect) throw new IllegalArgumentException("script: cycle " + t + ", expected " + expect);
            expect++;
            o.setLength(0);
            o.append(t);
            for (int i = 1; i < f.length; i++) o.append(' ').append(d.op(t, f[i]));
            o.append(" Q:").append(d.query(t)).append('\n');
            out.write(o.toString());
        }
    }

    void make(String[] k) {
        kind = k[0];
        switch (kind) {
            case "link" -> { int l = Integer.parseInt(k[1]); link = new WordDelayLine("link", l); linkCapacity = l + 2; }
            case "fifo" -> fifo = new WordFIFO("fifo", Integer.parseInt(k[1]));
            case "credit" -> credit = new WordBackpressureFIFO("credit", Integer.parseInt(k[1]));
            case "hold" -> hold = new WordHold("hold");
            case "reg" -> reg = new WordLatch("reg");
            case "rlink" -> rlink = new WordBackpressureDelayLine("rlink", Integer.parseInt(k[1]), Integer.parseInt(k[2]));
            default -> throw new IllegalArgumentException("script: unknown kind " + kind);
        }
    }

    WordBus bus() { return link != null ? link : fifo != null ? fifo : credit != null ? credit : hold != null ? hold : reg != null ? reg : rlink; }

    String op(long t, String tok) {
        char c = tok.charAt(0);
        WordBus b = bus();
        switch (c) {
            case 'T', 'K' -> {
                if (!b.ready(t)) return c + ":-";
                String v = c + ":" + h(b.peekA(t)) + "," + h(b.peekB(t)) + "," + h(b.peekC(t));
                if (c == 'T') b.take(t);
                return v;
            }
            case 'P', 'G', 'R' -> {
                String[] w = tok.substring(2).split(",");
                long x = Long.parseUnsignedLong(w[0], 16), y = Long.parseUnsignedLong(w[1], 16), z = Long.parseUnsignedLong(w[2], 16);
                if (c == 'G' && !guard(t)) return "G:-";
                if (c == 'R' && hold.refused(t)) { hold.retry(t); return "R"; }      // WordHold.retry: its own copy of t - 1's words
                b.put(t, x, y, z);
                return c == 'R' ? "P" : String.valueOf(c);
            }
            default -> throw new IllegalArgumentException("script: unknown op " + tok + " at " + t);
        }
    }

    boolean guard(long t) {
        if (link != null) return link.inFlight(t) < linkCapacity;
        if (credit != null) return credit.canPut(t);
        if (rlink != null) return rlink.canPut(t);
        throw new IllegalArgumentException("script: G on a " + kind);
    }

    String query(long t) {
        return switch (kind) {
            case "link" -> b(link.ready(t)) + "," + link.inFlight(t);
            case "fifo" -> b(fifo.ready(t)) + "," + fifo.size(t) + "," + fifo.sizeAtStart(t) + "," + fifo.overflowCount + "," + fifo.overflowCycle;
            case "credit" -> b(credit.ready(t)) + "," + credit.size(t) + "," + b(credit.canPut(t));
            case "hold" -> b(hold.ready(t)) + "," + b(hold.canPut(t)) + "," + b(hold.refused(t)) + "," + b(hold.heldAtStart(t)) + "," + hold.refusals;
            case "reg" -> b(reg.ready(t));
            case "rlink" -> b(rlink.ready(t)) + "," + b(rlink.canPut(t)) + "," + rlink.inFlight(t);
            default -> throw new IllegalStateException(kind);
        };
    }

    static String h(long v) { return Long.toHexString(v); }
    static String b(boolean v) { return v ? "1" : "0"; }
}
