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
 * end state. Before replaying, <b>every file the window's replay reads</b> (its state record, the streams that record
 * names, rows.bin, end.bin) is checked against the manifest's SHA-256 (a corrupt delivery is reported as CORRUPT, not
 * as a mismatch). The delivery is only read (DECISIONS-v1.25 s7 revised: it stays on the runner box for all shards).
 *
 * <p>Usage: java ReplayShard DELIVERY PROGRAM CSV [--stages ABF] [--variant V] [--first F --count N]
 * [--stride S --phase K]: windows with index F..F+N-1 (default all), every S-th of them from the K-th
 * <br>V: the state-record variant (default state-pick if the window has one, else state). Appends one CSV line per
 * window and stage: program,window,first_row,stage,verdict,rows,cycles,wall_ms,detail. Exit 0 if every line is OK.
 */
public final class ReplayShard {
    public static void main(String[] a) throws Exception {
        Path dir = Path.of(a[0]); String prog = a[1]; Path csv = Path.of(a[2]);
        String stages = "AB", variant = null; int stride = 1, phase = 0; long firstW = 0, countW = Long.MAX_VALUE;
        for (int i = 3; i < a.length; i++) switch (a[i]) {
            case "--stages" -> stages = a[++i];
            case "--variant" -> variant = a[++i];
            case "--stride" -> stride = Integer.parseInt(a[++i]);
            case "--phase" -> phase = Integer.parseInt(a[++i]);
            case "--first" -> firstW = Long.parseLong(a[++i]);
            case "--count" -> countW = Long.parseLong(a[++i]);
            default -> throw new IllegalArgumentException(a[i]);
        }
        Map<String, Object> m = ReplayDelivery.Json.object(Files.readString(dir.resolve("manifest.json")));
        Path pd = dir.resolve(prog);
        ReplayTables tb = ReplayDelivery.tablesFor(dir, pd);
        List<String> bad = ReplayTables.checkManifest(tb.dir, ReplayTables.defaultDir().resolve("../manifest.json"), "replayer/");
        StageB.Image im = stages.contains("B") || stages.contains("F") ? new StageB.Image(Path.of(System.getProperty("replay.build", "sw/replay/build"))) : null;
        int n = 0, failed = 0;
        try (PrintWriter out = new PrintWriter(Files.newBufferedWriter(csv, StandardOpenOption.CREATE, StandardOpenOption.APPEND))) {
            if (!bad.isEmpty()) { out.println(prog + ",-,-,tables,FAIL,0,0,0," + q("tables differ from wip/tables/manifest.json: " + bad)); failed++; }
            for (Object po : ReplayDelivery.Json.list(m.get("programs"))) {
                Map<String, Object> p = ReplayDelivery.Json.object(po);
                if (!prog.equals(p.get("name"))) continue;
                int k = 0;
                for (Object wo : ReplayDelivery.Json.list(p.get("window_entries"))) {
                    Map<String, Object> we = ReplayDelivery.Json.object(wo);
                    if (!we.containsKey("files")) continue;
                    int index = ((Number) we.get("index")).intValue();
                    if (index < firstW || index - firstW >= countW || k++ % stride != phase) continue;
                    long firstRow = ((Number) we.get("first_row")).longValue();
                    String base = String.format("window_%05d.", index);
                    String v = variant != null ? variant : Files.exists(pd.resolve(base + "state-pick.bin")) ? "state-pick" : "state";
                    // the delivery's own integrity first: every file this replay reads, against the manifest
                    String corrupt = checkFiles(pd, base, v, we);
                    if (corrupt != null) {
                        out.println(String.join(",", prog, "" + index, "" + firstRow, "-", "CORRUPT", "0", "0", "0", q(corrupt)));
                        failed++; n++; out.flush(); continue;
                    }
                    for (char st : stages.toCharArray()) {
                        long t0 = System.nanoTime();
                        // B: Stage B on tile 0 alone (the 63 idle tiles parked, checked untouched); F: Stage B on the full chip
                        StageB.LONE = st == 'B';
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
    /**
     * The window's files a replay of variant v reads (the record, the three streams its flags name, rows.bin, end.bin
     * if delivered), each against the manifest's "files" entry ({"sha256": ...} or the hash itself) and rows.bin also
     * against rows_sha256. Returns null if all match, else what didn't (a file with no manifest entry is an error).
     */
    @SuppressWarnings("unchecked")
    static String checkFiles(Path pd, String base, String v, Map<String, Object> we) throws Exception {
        Map<String, Object> files = (Map<String, Object>) we.get("files");
        java.util.List<String> names = new java.util.ArrayList<>(List.of(base + v + ".bin", base + "rows.bin"));
        if (Files.exists(pd.resolve(base + "end.bin"))) names.add(base + "end.bin");
        byte[] rec = Files.readAllBytes(pd.resolve(base + v + ".bin"));
        long sHi = ReplayTables.le(rec).getLong(24);
        names.add(base + (((sHi >>> 48) & 1) != 0 ? "code-lru" : "code") + ".bin");
        names.add(base + (((sHi >>> 49) & 1) != 0 ? "rlog-lru" : "rlog") + ".bin");
        names.add(base + (((sHi >>> 50) & 1) != 0 ? "wlog-lru" : "wlog") + ".bin");
        StringBuilder bad = new StringBuilder();
        for (String f : names) {
            Object e = files.get(f);
            String want = e instanceof String str ? str : e instanceof Map<?, ?> m ? (String) m.get("sha256") : null;
            if (want == null) { bad.append(f).append(": no SHA-256 in the manifest; "); continue; }
            Path p = pd.resolve(f);
            if (!Files.exists(p)) { bad.append(f).append(": missing; "); continue; }
            String got = ReplayTables.sha256(Files.readAllBytes(p));
            if (!want.equals(got)) bad.append(f).append(": sha256 ").append(got, 0, 12).append(", manifest ").append(want, 0, Math.min(12, want.length())).append("; ");
            if (f.endsWith("rows.bin") && we.get("rows_sha256") != null && !we.get("rows_sha256").equals(got)) bad.append(f).append(": differs from rows_sha256; ");
        }
        return bad.length() == 0 ? null : bad.toString();
    }

    static String q(String s) { return '"' + s.replace("\"", "'").replace("\n", " ") + '"'; }
}
