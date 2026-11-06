import java.util.ArrayList;
import java.util.List;

/**
 * A stand-in consumer for a unit not built yet (M1: the write engine, the responder, the GATHER engine): takes one
 * packet per cycle from its input FIFO, the rate of the unit it stands for, and optionally records it.
 */
final class Drain implements Component {
    final String name;
    WordSource in;
    boolean enabled = true;               // a test can stop it to fill the FIFO
    List<long[]> log;                     // {t, payload, hdr} when non-null
    long taken;

    Drain(String name) { this.name = name; }

    @Override public void runCycle(long t) {
        if (!enabled || !in.ready(t)) return;
        long a = in.peekA(t), b = in.peekB(t);
        in.take(t);
        taken++;
        if (log != null) log.add(new long[] { t, a, b });
    }
    void record() { log = new ArrayList<>(); }
}
