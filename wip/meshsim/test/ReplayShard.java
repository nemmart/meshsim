import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.List;
import java.util.Map;

/**
 * The acceptance kit's replayer (acceptance/README.md; DECISIONS-v1.25 s2, s7): every window of a delivery (or every
 * k-th, for parallel JVMs), Stage A (the model fed by the ideal controller, ReplayDelivery) and/or Stage B (replay_ctl.c
 * on a meshsim tile, StageB), each compared row for row with the reference rows, with the streams' consumption and the
 * end state. Before replaying, the window's rows.bin is checked against the manifest's rows_sha256 (a corrupt
 * delivery is reported as such, not as a mismatch).
 *
 * <p>Usage: java ReplayShard DELIVERY PROGRAM CSV [--stages A|B|AB] [--variant V] [--stride N --phase K]
 * <br>V: the state-record variant (default state-pick if the window has one, else state). Appends one CSV line per
 * window and stage: program,window,first_row,stage,verdict,rows,cycles,wall_ms,detail. Exit 0 if every line is OK.
 */
public final class ReplayShard {
    public static void main(String[] a) throws Exception {
        Path dir = Path.of(a[0]); String prog = a[1]; Path csv = Path.of(a[2]);
        String stages = "AB", variant = null; int stride = 1, phase = 0;
        for (int i = 3; i < a.length; i++) switch (a[i]) {
            case "--stages" -> stages = a[++i];
            case "--variant" -> variant = a[++i];
            case "--stride" -> stride = Integer.parseInt(a[++i]);
            case "--phase" -> phase = Integer.parseInt(a[++i]);
            default -> throw new IllegalArgumentException(a[i]);
        }
        Map<String, Object> m = ReplayDelivery.Json.object(Files.readString(dir.resolve("manifest.json")));
        Path pd = dir.resolve(prog);
        ReplayTables tb = ReplayDelivery.tablesFor(dir, pd);
        List<String> bad = ReplayTables.checkManifest(tb.dir, ReplayTables.defaultDir().resolve("../manifest.json"), "replayer/");
        StageB.Image im = stages.contains("B") ? new StageB.Image(Path.of(System.getProperty("replay.build", "sw/replay/build"))) : null;
        int n = 0, failed = 0;
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(csv, StandardOpenOption.CREATE, StandardOpenOption.APPEND))) {
            if (!bad.isEmpty()) { out.println(prog + ",-,-,tables,FAIL,0,0,0," + q("tables differ from wip/tables/manifest.json: " + bad)); failed++; }
            for (Object po : ReplayDelivery.Json.list(m.get("programs"))) {
                Map<String, Object> p = ReplayDelivery.Json.object(po);
                if (!prog.equals(p.get("name"))) continue;
                int k = 0;
                for (Object wo : ReplayDelivery.Json.list(p.get("window_entries"))) {
                    Map<String, Object> we = ReplayDelivery.Json.object(wo);
                    if (!we.containsKey("files") || k++ % stride != phase) continue;
                    int index = ((Number) we.get("index")).intValue();
                    long firstRow = ((Number) we.get("first_row")).longValue();
                    String base = String.format("window_%05d.", index);
                    String v = variant != null ? variant : Files.exists(pd.resolve(base + "state-pick.bin")) ? "state-pick" : "state";
                    // the delivery's own integrity first
                    Object want = we.get("rows_sha256");
                    if (want != null && !want.equals(ReplayTables.sha256(Files.readAllBytes(pd.resolve(base + "rows.bin"))))) {
                        out.println(String.join(",", prog, "" + index, "" + firstRow, "-", "CORRUPT", "0", "0", "0", q("rows.bin differs from the manifest's rows_sha256")));
                        failed++; n++; out.flush(); continue;
                    }
                    for (char st : stages.toCharArray()) {
                        long t0 = System.nanoTime();
                        ReplayDelivery.Result r = st == 'A' ? ReplayDelivery.runWindow(tb, pd, we, v) : StageB.runWindow(im, tb, pd, we, v, prog);
                        long ms = (System.nanoTime() - t0) / 1_000_000;
                        if (st == 'A') System.out.println(r.line(prog, v));
                        out.println(String.join(",", prog, "" + index, "" + firstRow, st + ":" + v, r.ok ? "OK" : "FAIL", "" + r.rows, "" + r.cycles, "" + ms,
                                q(r.ok ? r.summary : String.join(" | ", r.detail))));
                        out.flush();
                        n++; if (!r.ok) failed++;
                    }
                }
            }
        }
        System.out.println("ReplayShard " + prog + " " + stages + " phase " + phase + "/" + stride + ": " + n + " runs, " + failed + " failed");
        System.exit(failed == 0 && n > 0 ? 0 : 1);
    }
    static String q(String s) { return '"' + s.replace("\"", "'").replace("\n", " ") + '"'; }
}
