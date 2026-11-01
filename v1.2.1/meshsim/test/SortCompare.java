import java.io.IOException;
import java.nio.file.*;
import java.util.*;

/**
 * Compare two kit results directories run by run (meshsim M8 against M7's results-p4, for example).
 * For every <id>.csv present in both: the steps (every column), then every C&C phase in <id>.phases.csv (every column
 * except go_at and end_at, which move with any earlier difference; cycles and the rest say where it happened).
 * Prints one line per run (identical, or the TOTAL difference and the differing steps), then the details.
 *
 * Reads both naming schemes (OLD_COLUMNS: M10's peak_cntReq is M11's peak_histReq).
 *
 * Usage: java SortCompare NEW_DIR REF_DIR [out.md]
 */
public final class SortCompare {
    public static void main(String[] a) throws IOException {
        Path n = Path.of(a[0]), r = Path.of(a[1]);
        StringBuilder sum = new StringBuilder(), det = new StringBuilder();
        sum.append("# ").append(n).append(" against ").append(r).append("\n\n");
        sum.append("| run | path | TOTAL new | TOTAL ref | diff | steps that differ |\n|---|---|---|---|---|---|\n");
        List<Path> files = new ArrayList<>();
        try (var s = Files.list(n)) { s.filter(p -> p.toString().endsWith(".csv") && !p.toString().endsWith(".phases.csv") && !p.getFileName().toString().equals("all.csv")).sorted().forEach(files::add); }
        int same = 0, diff = 0, missing = 0;
        for (Path f : files) {
            Path g = r.resolve(f.getFileName());
            String id = f.getFileName().toString().replace(".csv", "");
            if (!Files.exists(g)) { missing++; continue; }
            List<Map<String, String>> A = read(f), B = read(g);
            Map<String, Map<String, String>> bs = new LinkedHashMap<>(); for (var m : B) bs.put(m.get("step"), m);
            List<String> steps = new ArrayList<>(); String tn = "", tr = "", path = "";
            for (var m : A) {
                String st = m.get("step"); var o = bs.get(st);
                if (path.isEmpty() && m.get("path") != null) path = m.get("path");
                if (st.equals("TOTAL")) { tn = m.get("cycles"); tr = o == null ? "?" : o.get("cycles"); }
                if (o == null) { steps.add(st + " (missing in ref)"); continue; }
                List<String> cols = new ArrayList<>();
                for (String c : m.keySet()) if (!Objects.equals(m.get(c), o.get(c))) cols.add(c + " " + o.get(c) + " -> " + m.get(c));
                if (!cols.isEmpty()) { steps.add(st); det.append("- ").append(id).append(" ").append(st).append(": ").append(String.join("; ", cols)).append("\n"); }
            }
            Path pn = n.resolve(id + ".phases.csv"), pr = r.resolve(id + ".phases.csv");
            if (Files.exists(pn) && Files.exists(pr)) {
                List<Map<String, String>> P = read(pn), Q = read(pr);
                if (P.size() != Q.size()) det.append("- ").append(id).append(": ").append(Q.size()).append(" phases -> ").append(P.size()).append("\n");
                for (int i = 0; i < Math.min(P.size(), Q.size()); i++) {
                    var x = P.get(i); var y = Q.get(i); List<String> cols = new ArrayList<>();
                    for (String c : x.keySet()) if (!c.equals("go_at") && !c.equals("end_at") && !Objects.equals(x.get(c), y.get(c))) cols.add(c + " " + y.get(c) + " -> " + x.get(c));
                    if (!cols.isEmpty()) det.append("  - phase ").append(x.get("phase")).append(" ").append(x.get("name")).append(": ").append(String.join("; ", cols)).append("\n");
                }
            }
            long d = 0; try { d = Long.parseLong(tn) - Long.parseLong(tr); } catch (NumberFormatException e) { }
            if (steps.isEmpty()) same++; else diff++;
            sum.append("| ").append(id).append(" | ").append(path).append(" | ").append(tn).append(" | ").append(tr).append(" | ").append(d)
               .append(" | ").append(steps.isEmpty() ? "identical" : String.join(", ", steps)).append(" |\n");
        }
        sum.append("\n").append(same).append(" identical, ").append(diff).append(" differ, ").append(missing).append(" not in the reference.\n\n## Differences\n\n").append(det);
        System.out.print(sum);
        if (a.length > 2) Files.writeString(Path.of(a[2]), sum);
    }

    /** Column names before the 8 October rename (spec 1) read as their new names, so M10 results compare with M11's. */
    static final Map<String, String> OLD_COLUMNS = Map.of("peak_cntReq", "peak_histReq");
    static String canon(String col) { return OLD_COLUMNS.getOrDefault(col, col); }

    static List<Map<String, String>> read(Path p) throws IOException {
        List<String> l = Files.readAllLines(p); List<Map<String, String>> out = new ArrayList<>();
        if (l.isEmpty()) return out;
        String[] h = l.get(0).split(",", -1);
        for (int i = 1; i < l.size(); i++) {
            if (l.get(i).isBlank()) continue;
            String[] v = l.get(i).split(",", -1); Map<String, String> m = new LinkedHashMap<>();
            for (int k = 0; k < h.length; k++) m.put(canon(h[k]), k < v.length ? v[k] : "");
            out.add(m);
        }
        return out;
    }
}
