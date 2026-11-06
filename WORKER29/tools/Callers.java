import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import jdk.jfr.consumer.RecordedEvent;
import jdk.jfr.consumer.RecordedFrame;
import jdk.jfr.consumer.RecordingFile;

/**
 * WORKER29 (P2, the profile): who calls a hot frame. For every sample of EVENT (jdk.ExecutionSample, or
 * jdk.ObjectAllocationSample weighted by bytes) whose stack has a frame containing TARGET ("" matches the top frame), the
 * chain of DEPTH frames from that one down, counted; the TOP chains as shares of all the event's samples.
 * Usage: java Callers FILE.jfr EVENT TARGET DEPTH [TOP=25]
 */
public final class Callers {
    public static void main(String[] a) throws Exception {
        String ev = a[1], tgt = a[2]; int d = Integer.parseInt(a[3]), top = a.length > 4 ? Integer.parseInt(a[4]) : 25;
        boolean alloc = ev.contains("Allocation");
        Map<String, Long> m = new HashMap<>(); long n = 0, w = 0;
        for (RecordedEvent e : RecordingFile.readAllEvents(Path.of(a[0]))) {
            if (!e.getEventType().getName().equals(ev) || e.getStackTrace() == null) continue;
            long wt = alloc ? e.getLong("weight") : 1; n += wt;
            List<RecordedFrame> f = e.getStackTrace().getFrames();
            for (int i = 0; i < f.size(); i++) {
                if (!name(f.get(i)).contains(tgt)) continue;
                StringBuilder b = new StringBuilder(alloc ? e.getClass("objectClass").getName() + " @ " : "");
                for (int j = i; j < Math.min(f.size(), i + d); j++) b.append(j > i ? " <- " : "").append(name(f.get(j))).append(':').append(f.get(j).getLineNumber());
                m.merge(b.toString(), wt, Long::sum); w += wt;
                break;
            }
        }
        System.out.printf("%s: %s %d, matching '%s' %d%n", a[0], ev, n, tgt, w);
        final long all = n;
        m.entrySet().stream().sorted((x, y) -> Long.compare(y.getValue(), x.getValue())).limit(top)
         .forEach(x -> System.out.printf("%7.2f%%  %s%n", 100.0 * x.getValue() / all, x.getKey()));
    }
    static String name(RecordedFrame f) { return f.getMethod().getType().getName() + "." + f.getMethod().getName(); }
}
