import java.nio.file.Path;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import jdk.jfr.consumer.RecordedEvent;
import jdk.jfr.consumer.RecordedFrame;
import jdk.jfr.consumer.RecordingFile;

/**
 * WORKER28, step 1 (branch sim-idle-profile): where a simulation's wall time goes, from a JFR recording's execution
 * samples (java -XX:StartFlightRecording=filename=X.jfr,settings=profile). Four tables, as shares of the samples taken
 * inside Chip.runCycle (the simulation proper; the rest, loading and checking, is reported as one line):
 *   unit       the innermost runCycle/endCycle/pre/post frame's class (the unit being stepped; Chip itself when the
 *              sample is in Chip's own loops: checkInvariants, the error lines, scanOverflows);
 *   self       the top frame's class;
 *   method     the top frame's class.method;
 *   inclusive  every class on the stack, counted once per sample (the memory system under the Tracer, for example);
 *   inclusive methods  the same by class.method (Controller.step: the RV32 interpreter executing an instruction).
 * Usage: java JfrFold FILE.jfr [TOP]
 */
public final class JfrFold {
    public static void main(String[] a) throws Exception {
        int top = a.length > 1 ? Integer.parseInt(a[1]) : 30;
        Map<String, Long> imeth = new HashMap<>(), unit = new HashMap<>(), self = new HashMap<>(), method = new HashMap<>(), incl = new HashMap<>(), threads = new HashMap<>();
        long all = 0, sim = 0;
        for (RecordedEvent e : RecordingFile.readAllEvents(Path.of(a[0]))) {
            if (!e.getEventType().getName().equals("jdk.ExecutionSample") || e.getStackTrace() == null) continue;
            all++;
            List<RecordedFrame> fs = e.getStackTrace().getFrames();
            boolean inSim = false;
            for (RecordedFrame f : fs) if (f.isJavaFrame() && cls(f).equals("Chip") && f.getMethod().getName().equals("runCycle")) { inSim = true; break; }
            String th = e.getThread("sampledThread") != null ? e.getThread("sampledThread").getJavaName() : "?";
            threads.merge(th + (inSim ? " (sim)" : ""), 1L, Long::sum);
            if (!inSim) continue;
            sim++;
            String u = null;
            for (RecordedFrame f : fs) {
                if (!f.isJavaFrame()) continue;
                String m = f.getMethod().getName(), c = cls(f);
                if (c.equals("Chip") && !m.equals("runCycle")) { u = "Chip." + m; break; }
                if (m.equals("runCycle") || m.equals("endCycle") || c.equals("Probe")) { u = c.equals("Chip") ? "Chip (own loop)" : c + (m.equals("endCycle") ? ".endCycle" : ""); break; }
            }
            unit.merge(u == null ? "?" : u, 1L, Long::sum);
            RecordedFrame t0 = fs.isEmpty() ? null : fs.get(0);
            if (t0 != null) { self.merge(cls(t0), 1L, Long::sum); method.merge(cls(t0) + "." + t0.getMethod().getName(), 1L, Long::sum); }
            Set<String> seen = new HashSet<>();
            for (RecordedFrame f : fs) if (f.isJavaFrame() && seen.add(cls(f))) incl.merge(cls(f), 1L, Long::sum);
            Set<String> seenM = new HashSet<>();
            for (RecordedFrame f : fs) { if (!f.isJavaFrame()) continue; String k = cls(f) + "." + f.getMethod().getName(); if (seenM.add(k)) imeth.merge(k, 1L, Long::sum); }
        }
        System.out.printf("%s: %d execution samples, %d (%.1f%%) inside Chip.runCycle%n", a[0], all, sim, 100.0 * sim / Math.max(1, all));
        print("threads (all samples)", threads, all, top);
        print("unit (innermost runCycle/endCycle; share of the simulation's samples)", unit, sim, top);
        print("self (top frame's class)", self, sim, top);
        print("method (top frame)", method, sim, top);
        print("inclusive (class anywhere on the stack)", incl, sim, top);
        print("inclusive methods (Controller.step: the interpreter executing; anywhere on the stack)", imeth, sim, top + 20);
    }
    static String cls(RecordedFrame f) {
        String n = f.getMethod().getType().getName();
        int d = n.indexOf("$$Lambda"); if (d >= 0) n = n.substring(0, d) + "$lambda";
        return n;
    }
    static void print(String title, Map<String, Long> m, long tot, int top) {
        System.out.println("== " + title);
        m.entrySet().stream().sorted((x, y) -> Long.compare(y.getValue(), x.getValue())).limit(top)
                .forEach(e -> System.out.printf("  %6.2f%%  %8d  %s%n", 100.0 * e.getValue() / Math.max(1, tot), e.getValue(), e.getKey()));
    }
}
