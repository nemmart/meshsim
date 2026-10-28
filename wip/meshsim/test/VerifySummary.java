import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The v1.0 verification kit's summary: meshsim v1.0 against M15, from the results directory of the kit's run.sh.
 *   OUT/m15/split/NN.log, OUT/v1.0/split/NN.log   SplitRun on the split's 25 sets (one set per log)
 *   OUT/m15/kit/, OUT/v1.0/kit/                   the sort kit's 92 runs at 64 x 2,000 (SortTest CSVs, phase CSVs, logs)
 *   OUT/m15/bt/, OUT/v1.0/bt/                     the two full-size BT runs
 * The split's image is instruction-identical on both (only the register addresses differ), so the split must be
 * identical: S, steps, cycles per step, invariant 8 and checks. The sort is judged at the PHASE level ({@link
 * PhaseNeutral} with its v1.0 start rule): the data plane identical in every phase, except that a run is counted where it
 * starts (agRuns, peak_agTable may move between phases, their run totals equal) and an overlay that changed size moves
 * its phase's packet counts by exactly 64 per packet; every phase's cycle change explained by the controllers' change,
 * the overlay packets, and at most one lead-in (4 ts_engine_latency + 2) per run the phase starts.
 * Writes OUT/VERIFY.md, PHASES-kit.md, PHASES-bt.md.
 *   java -cp v1.0/split/split.jar VerifySummary OUT [HASHES-SPLIT]     (run from the kit's root: reads m15/ and v1.0/image/sort.sym)
 */
final class VerifySummary {
    static final String A = "m15", B = "v1.0";
    static final double S_BAND = 0.0;            // percent: the split's image is instruction-identical on M15 and v1.0: S identical

    static final class Split {
        String label = "?", inputs; boolean done, exact; long S = -1, steps = -1, inv8 = -1, viol = -1, overlaps = -1, rpResults = -1; double cps = Double.NaN;
        int passed = -1, failed = -1;
    }
    static long num(String s) { return Long.parseLong(s.replace(",", "")); }

    static Split readSplit(Path f) throws IOException {
        Split r = new Split();
        if (!Files.exists(f)) return r;
        Pattern pS = Pattern.compile("^(\\S.*?)\\s+S\\s+([\\d,]+) cycles .*step loop ([\\d,]+) search steps, ([\\d.]+) cycles per step");
        Pattern p8 = Pattern.compile("overwritten unread \\(invariant 8, a statistic\\): (\\d+); violations (\\d+)");
        Pattern pO = Pattern.compile("read-port results looped back: (\\d+); .*outstanding \\(4\\.3's rule\\): (\\d+)");
        Pattern pD = Pattern.compile("^SplitRun: (\\d+) passed, (\\d+) failed");
        boolean exactFail = false;
        for (String l : Files.readAllLines(f)) {
            Matcher m;
            if (l.startsWith("inputs: ")) r.inputs = l.substring(8).trim();
            if ((m = pS.matcher(l)).find()) { r.label = m.group(1).trim(); r.S = num(m.group(2)); r.steps = num(m.group(3)); r.cps = Double.parseDouble(m.group(4)); }
            if ((m = p8.matcher(l)).find()) { r.inv8 = num(m.group(1)); r.viol = num(m.group(2)); }
            if ((m = pO.matcher(l)).find()) { r.rpResults = num(m.group(1)); r.overlaps = num(m.group(2)); }
            if ((m = pD.matcher(l)).find()) { r.done = true; r.passed = Integer.parseInt(m.group(1)); r.failed = Integer.parseInt(m.group(2)); }
            if (l.startsWith("FAIL") && l.contains("exact")) exactFail = true;
        }
        r.exact = r.done && !exactFail && r.S >= 0;
        return r;
    }

    /** Kit run: TOTAL and SPLIT cycles, path, checks failed (CSV); invariant 8 summed over phases; the log's check line. */
    static final class Kit { long total = -1, split = -1, inv8 = -1, rpReads = -1; String path = "?", checks = "no log"; boolean csv; }
    static Map<String, Kit> readKit(Path dir) throws IOException {
        Map<String, Kit> m = new TreeMap<>();
        if (!Files.isDirectory(dir)) return m;
        List<Path> fs = new ArrayList<>();
        try (var s = Files.list(dir)) { s.forEach(fs::add); }
        for (Path f : fs) {
            String n = f.getFileName().toString();
            if (n.endsWith(".log")) {
                Kit k = m.computeIfAbsent(n.substring(0, n.length() - 4), x -> new Kit());
                k.checks = "unfinished";
                for (String l : Files.readAllLines(f)) if (l.matches("^SortTest.*passed.*")) k.checks = l.replaceAll("^SortTest[^:]*: ", "");
            } else if (n.endsWith(".csv") && !n.endsWith(".phases.csv") && !n.equals("all.csv")) {
                Kit k = m.computeIfAbsent(n.substring(0, n.length() - 4), x -> new Kit());
                k.csv = true;
                List<String> L = Files.readAllLines(f);
                List<String> h = List.of(L.get(0).split(","));
                int iStep = h.indexOf("step"), iCyc = h.indexOf("cycles"), iPath = h.indexOf("path");
                for (String l : L.subList(1, L.size())) {
                    String[] c = l.split(",", -1);
                    if (c[iStep].equals("TOTAL")) k.total = num(c[iCyc]);
                    if (c[iStep].equals("SPLIT")) k.split = num(c[iCyc]);
                    if (iPath >= 0 && k.path.equals("?")) k.path = c[iPath];
                }
            } else if (n.endsWith(".phases.csv")) {
                Kit k = m.computeIfAbsent(n.substring(0, n.length() - 11), x -> new Kit());
                List<String> L = Files.readAllLines(f);
                List<String> h = List.of(L.get(0).split(","));
                int iU = h.indexOf("unreadOverwrites"), iR = h.indexOf("rpReads");
                long u = 0, r = 0;
                for (String l : L.subList(1, L.size())) { String[] c = l.split(",", -1); if (iU >= 0) u += num(c[iU]); if (iR >= 0) r += num(c[iR]); }
                k.inv8 = u; k.rpReads = r;
            }
        }
        return m;
    }

    static Map<String, String> hashes(Path f) throws IOException {
        Map<String, String> m = new LinkedHashMap<>();
        if (f == null || !Files.exists(f)) return m;
        for (String l : Files.readAllLines(f)) {
            if (l.isBlank() || l.startsWith("#")) continue;
            String[] p = l.trim().split("\\s+", 2);
            if (p.length == 2) m.put(p[1].trim(), p[0]);
        }
        return m;
    }
    static Map<String, String> inputsLine(String s) {
        Map<String, String> m = new LinkedHashMap<>();
        if (s == null) return m;
        for (String t : s.split("\\s+")) { int e = t.lastIndexOf('='); if (e > 0) m.put(t.substring(0, e), t.substring(e + 1)); }
        return m;
    }

    public static void main(String[] a) throws IOException {
        PhaseNeutral.START_RULE = true;
        Path out = Path.of(a.length > 0 ? a[0] : "results");
        Map<String, String> ref = hashes(a.length > 1 ? Path.of(a[1]) : Path.of("HASHES-SPLIT"));
        StringBuilder md = new StringBuilder();
        List<String> verdict = new ArrayList<>();
        md.append("# v1.0 verification: meshsim v1.0 against M15\n\n");
        md.append("Made by `VerifySummary` from `").append(out).append("`. Detail: `PHASES-kit.md` and `PHASES-bt.md` (phase by phase), `COMPARE-kit.md` and `COMPARE-bt.md` (step by step), `m15/kit/SUMMARY.md` and `v1.0/kit/SUMMARY.md` (each kit's runs and its inputs check).\n\n");

        // ------------------------------------------------------------------ the split
        md.append("## The split: SplitRun on the 25 sets\n\n");
        md.append(String.format("Expected: exact on every balanced set on both (the two shaped sets, 21 and 23, are run as balanced splits by SplitRun and are not exact on either version); S, search steps and cycles per step identical (band %.1f %%); invariant 8's count unchanged; no violations, and no read-port overlaps on v1.0.%n%n", S_BAND));
        md.append("| set | label | exact M15 / v1.0 | S M15 | S v1.0 | change | % | cyc/step M15 | v1.0 | inv. 8 M15 | v1.0 | violations M15 / v1.0 | overlaps v1.0 | flags |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
        int nShaped = 0, nBoth = 0, nExact = 0, nBand = 0, nInv8 = 0, nClean = 0, missing = 0; double maxPct = -1e9, minPct = 1e9, sumPct = 0;
        long sumS9 = 0, sumS10 = 0;
        List<String> inputFlags = new ArrayList<>();
        Map<String, String> setHash = new TreeMap<>();
        for (int i = 0; i < 25; i++) {
            String nn = String.format("%02d", i);
            Split p = readSplit(out.resolve(A + "/split/" + nn + ".log")), q = readSplit(out.resolve(B + "/split/" + nn + ".log"));
            List<String> fl = new ArrayList<>();
            if (!p.done || !q.done) { missing++; md.append(String.format("| %s | %s | %s / %s | | | | | | | | | | | **not finished** |%n", nn, q.done ? q.label : p.label, p.done ? "done" : "missing", q.done ? "done" : "missing")); continue; }
            nBoth++;
            double pct = 100.0 * (q.S - p.S) / p.S;
            boolean shaped = q.label.contains("(shaped)") || p.label.contains("(shaped)");   // SplitRun runs every set balanced
            if (p.exact && q.exact) nExact++;
            else if (shaped && p.exact == q.exact && p.S == q.S) { nExact++; nShaped++; fl.add("shaped set run balanced: not exact on either"); }
            else fl.add("NOT EXACT");
            if (Math.abs(pct) <= S_BAND && p.steps == q.steps && p.cps == q.cps) nBand++; else fl.add("S, steps or cyc/step differ");
            if (p.inv8 == q.inv8) nInv8++; else fl.add("inv. 8 differs");
            if (q.viol == 0 && q.overlaps == 0) nClean++; else fl.add("v1.0 violations/overlaps");
            if (p.viol != 0) fl.add("M15 violations");
            maxPct = Math.max(maxPct, pct); minPct = Math.min(minPct, pct); sumPct += pct; sumS9 += p.S; sumS10 += q.S;
            md.append(String.format("| %s | %s | %s / %s | %,d | %,d | %+,d | %+.3f | %.2f | %.2f | %,d | %,d | %d / %d | %s | %s |%n", nn, q.label,
                    p.exact ? "yes" : "**no**", q.exact ? "yes" : "**no**", p.S, q.S, q.S - p.S, pct, p.cps, q.cps, p.inv8, q.inv8, p.viol, q.viol,
                    q.overlaps < 0 ? "?" : String.valueOf(q.overlaps), fl.isEmpty() ? "" : "**" + String.join(", ", fl) + "**"));
            // inputs: each log's files against HASHES-SPLIT, and the set the same file on both
            for (Split s : new Split[] { p, q }) {
                String who = (s == p ? A : B) + "/split/" + nn;
                Map<String, String> in = inputsLine(s.inputs);
                if (in.isEmpty()) { inputFlags.add(who + ": no inputs line"); continue; }
                for (var e : in.entrySet()) {
                    String r = ref.get(e.getKey());
                    if (r == null) inputFlags.add(who + ": " + e.getKey() + " not in HASHES-SPLIT");
                    else if (!r.equals(e.getValue())) inputFlags.add(who + ": " + e.getKey() + " differs from HASHES-SPLIT");
                    if (e.getKey().startsWith("sets-")) { String o = setHash.putIfAbsent(e.getKey(), e.getValue()); if (o != null && !o.equals(e.getValue())) inputFlags.add(nn + ": M15 and v1.0 read different set files"); }
                }
            }
        }
        if (nBoth > 0) md.append(String.format("%n%d of 25 sets on both: exact %d; S within %.1f %% %d (change %+.3f %% to %+.3f %%, mean %+.3f %%; summed S %,d -> %,d, %+,d); invariant 8 unchanged %d; v1.0 clean (no violation, no overlap) %d.%n",
                nBoth, nExact, S_BAND, nBand, minPct, maxPct, sumPct / nBoth, sumS9, sumS10, sumS10 - sumS9, nInv8, nClean));
        if (missing > 0) md.append(String.format("%n**%d sets not finished on both.**%n", missing));
        md.append("\n**Split inputs** (each log's `inputs:` line against `HASHES-SPLIT`: the split jar and image of each version, `hops.txt`, the set): ");
        md.append(inputFlags.isEmpty() ? (nBoth > 0 ? "every log matches; M15 and v1.0 read the same set files.\n" : "nothing to check.\n") : "\n\n" + String.join("\n", inputFlags.stream().map(x -> "- " + x).toList()) + "\n");
        boolean splitOk = nBoth == 25 && nExact == 25 && nBand == 25 && nInv8 == 25 && nClean == 25 && inputFlags.isEmpty();
        verdict.add(String.format("Split: %s (%d of 25 on both; exact %d, within band %d, inv. 8 unchanged %d, v1.0 clean %d; inputs %s).", splitOk ? "**as expected**" : "**check the table**", nBoth, nExact, nBand, nInv8, nClean, inputFlags.isEmpty() ? "ok" : inputFlags.size() + " flags"));

        // ------------------------------------------------------------------ the kit (phase level, PhaseNeutral)
        Path symA = Path.of(A + "/image/sort.sym"), symB = Path.of(B + "/image/sort.sym");
        java.util.Set<Long> allowedK = Files.exists(symA) && Files.exists(symB) ? PhaseNeutral.allowedK(symA, symB) : java.util.Set.of(0L);
        String ovDesc = Files.exists(symA) && Files.exists(symB) ? PhaseNeutral.describeOverlays(symA, symB) : "(" + A + "/ or " + B + "/image/sort.sym missing: no overlay change allowed)";
        md.append("\n## The sort kit's runs at 64 × 2,000: v1.0 against M15, phase by phase\n\n");
        md.append("Expected: every check passing on both; the same path; the **data plane identical in every phase** (address-generator requests, stalls, gather requests, read-port reads, injected and ejected packets, read latencies, send stalls, invariant 8, every peak), except that a run is counted in the phase that starts it (v1.0: start loads the run; M15 counted it where it was armed), so `agRuns` and `peak_agTable` may move between phases with equal run totals, and the one phase loading an overlay whose size changed moves agReq, injected, ejected and latNA together by exactly 64 per packet; and **every phase's cycle change explained**: dCycles = the change in the slowest tile's busy controller cycles + k (overlay packets) + r, with |r| <= " + (int) PhaseNeutral.RESIDUAL_TOL + ", or 0 <= r <= one lead-in (4 ts_engine_latency + 2) per run the phase starts (M15 pre-armed such runs, so their first request went out at the start). TOTALs move by tens of cycles and are reported, not required equal. Overlays (packets, M15 -> v1.0): " + ovDesc + ". Phase detail: `PHASES-kit.md`; step detail: `COMPARE-kit.md`.\n\n");
        Map<String, Kit> K9 = readKit(out.resolve(A + "/kit")), K10 = readKit(out.resolve(B + "/kit"));
        TreeSet<String> ids = new TreeSet<>(); ids.addAll(K9.keySet()); ids.addAll(K10.keySet());
        md.append("| run | path M15 / v1.0 | checks M15 | checks v1.0 | TOTAL M15 | TOTAL v1.0 | change | = controller cycles | + overlay | + residual (max) | inv. 8 M15 / v1.0 | flags |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
        StringBuilder phk = new StringBuilder("# v1.0 against M15, the kit's runs phase by phase (PhaseNeutral, v1.0 start rule)\n\n| run | changed phases: name dCycles = the slowest tile's busy-cycle change (or instr/64) + k overlay packets (residual) |\n|---|---|\n");
        int kBoth = 0, kPass = 0, kPath = 0, kInv8 = 0, kNeutral = 0, kMissing = 0; long dMin = Long.MAX_VALUE, dMax = Long.MIN_VALUE, dSum = 0; double rMax = 0;
        for (String id : ids) {
            Kit p = K9.get(id), q = K10.get(id);
            if (p == null || q == null || !p.csv || !q.csv) { kMissing++; md.append(String.format("| %s | | %s | %s | | | | | | | | **not finished on both** |%n", id, p == null ? "missing" : p.checks, q == null ? "missing" : q.checks)); continue; }
            kBoth++;
            List<String> fl = new ArrayList<>();
            boolean pass = p.checks.contains(" 0 failed") && q.checks.contains(" 0 failed");
            if (pass) kPass++; else fl.add("CHECKS");
            if (p.path.equals(q.path)) kPath++; else fl.add("PATH");
            if (p.inv8 == q.inv8) kInv8++; else fl.add("inv. 8");
            PhaseNeutral.Result r = PhaseNeutral.compare(out.resolve(A + "/kit/" + id + ".phases.csv"), out.resolve(B + "/kit/" + id + ".phases.csv"),
                    out.resolve(A + "/kit/" + id + ".csv"), out.resolve(B + "/kit/" + id + ".csv"), allowedK);
            if (r.ok) kNeutral++; else fl.add(String.join("; ", r.problems));
            long d = q.total - p.total;
            dMin = Math.min(dMin, d); dMax = Math.max(dMax, d); dSum += d; rMax = Math.max(rMax, r.maxAbsResidual);
            md.append(String.format("| %s | %s | %s | %s | %,d | %,d | %+,d | %+.2f | %+d | %+.2f (%.2f) | %,d / %,d | %s |%n", id, p.path.equals(q.path) ? p.path : p.path + " / " + q.path,
                    p.checks, q.checks, p.total, q.total, d, r.dBusy, r.dOverlay, r.residual, r.maxAbsResidual, p.inv8, q.inv8, fl.isEmpty() ? "" : "**" + String.join(", ", fl) + "**"));
            phk.append("| ").append(id).append(" | ").append(String.join("; ", r.notes)).append(" |\n");
        }
        if (kBoth > 0) md.append(String.format("%n%d runs on both: checks passing on both %d; same path %d; data plane identical and every phase explained %d (largest residual %.2f cycles); TOTAL change %+,d to %+,d (mean %+.1f); invariant 8 unchanged %d.%n",
                kBoth, kPass, kPath, kNeutral, rMax, dMin, dMax, (double) dSum / kBoth, kInv8));
        if (kMissing > 0) md.append(String.format("%n**%d runs not finished on both.**%n", kMissing));
        if (kBoth > 0) Files.writeString(out.resolve("PHASES-kit.md"), phk.toString());
        boolean kitOk = kBoth == ids.size() && kBoth > 0 && kPass == kBoth && kPath == kBoth && kInv8 == kBoth && kNeutral == kBoth;
        verdict.add(String.format("Kit: %s (%d runs on both; checks %d, path %d, inv. 8 %d; phase-level neutral %d, largest residual %.2f; TOTAL %+,d to %+,d).", kitOk ? "**as expected**" : "**check the table**", kBoth, kPass, kPath, kInv8, kNeutral, rMax, kBoth > 0 ? dMin : 0, kBoth > 0 ? dMax : 0));

        // ------------------------------------------------------------------ the full-size BT runs
        md.append("\n## The full-size BT runs (64 × 65,536, trace hot=10 frac=0.10): v1.0 against M15, phase by phase\n\n");
        md.append("BT is infeasible at 64 × 2,000, so these two runs are the only ones that execute it. Expected: path BT on both, checks passing, and the same phase-level test as the kit. Detail: `PHASES-bt.md`, `COMPARE-bt.md`.\n\n");
        Map<String, Kit> B10 = readKit(out.resolve(A + "/bt")), B11 = readKit(out.resolve(B + "/bt"));
        TreeSet<String> bids = new TreeSet<>(); bids.addAll(B10.keySet()); bids.addAll(B11.keySet());
        md.append("| run | path M15 / v1.0 | checks M15 | checks v1.0 | TOTAL M15 | TOTAL v1.0 | change | = controller cycles | + overlay | + residual (max) | flags |\n|---|---|---|---|---|---|---|---|---|---|---|\n");
        StringBuilder phb = new StringBuilder("# v1.0 against M15, the full-size BT runs phase by phase (PhaseNeutral, v1.0 start rule)\n\n| run | changed phases: name dCycles = the slowest tile's busy-cycle change (or instr/64) + k overlay packets (residual) |\n|---|---|\n");
        int bBoth = 0, bOk = 0;
        for (String id : bids) {
            Kit p = B10.get(id), q = B11.get(id);
            if (p == null || q == null || !p.csv || !q.csv) { md.append(String.format("| %s | | %s | %s | | | | | | | **not finished on both** |%n", id, p == null ? "missing" : p.checks, q == null ? "missing" : q.checks)); continue; }
            bBoth++;
            PhaseNeutral.Result r = PhaseNeutral.compare(out.resolve(A + "/bt/" + id + ".phases.csv"), out.resolve(B + "/bt/" + id + ".phases.csv"),
                    out.resolve(A + "/bt/" + id + ".csv"), out.resolve(B + "/bt/" + id + ".csv"), allowedK);
            List<String> fl = new ArrayList<>();
            if (!(p.checks.contains(" 0 failed") && q.checks.contains(" 0 failed"))) fl.add("CHECKS");
            if (!p.path.equals("BT") || !q.path.equals("BT")) fl.add("NOT BT");
            if (!r.ok) fl.add(String.join("; ", r.problems));
            if (fl.isEmpty()) bOk++;
            md.append(String.format("| %s | %s / %s | %s | %s | %,d | %,d | %+,d | %+.2f | %+d | %+.2f (%.2f) | %s |%n", id, p.path, q.path, p.checks, q.checks, p.total, q.total, q.total - p.total,
                    r.dBusy, r.dOverlay, r.residual, r.maxAbsResidual, fl.isEmpty() ? "" : "**" + String.join(", ", fl) + "**"));
            phb.append("| ").append(id).append(" | ").append(String.join("; ", r.notes)).append(" |\n");
        }
        if (bBoth > 0) Files.writeString(out.resolve("PHASES-bt.md"), phb.toString());
        verdict.add(String.format("BT at full size: %s (%d of 2 on both; %d as expected).", bBoth == 2 && bOk == 2 ? "**as expected**" : bBoth == 0 ? "not run (`-g bt`)" : "**check the table**", bBoth, bOk));

        StringBuilder top = new StringBuilder();
        top.append(md, 0, md.indexOf("## The split"));
        top.append("## Verdict\n\n");
        for (String v : verdict) top.append("- ").append(v).append('\n');
        top.append("- Inputs of the kit runs: see the Inputs section of `m15/kit/SUMMARY.md` and `v1.0/kit/SUMMARY.md`.\n\n");
        top.append(md.substring(md.indexOf("## The split")));
        Files.writeString(out.resolve("VERIFY.md"), top.toString());
        System.out.println("== " + out.resolve("VERIFY.md") + " written");
        for (String v : verdict) System.out.println("   " + v.replace("**", ""));
    }
}
