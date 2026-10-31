import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeSet;

/**
 * The hardware change (4-bit modes, augmenting as mode 4) moves no packet and no cycle,
 * but the tile software that drives it changed (the key streams choose mode 4 where the receiver used to set attach), so
 * a run's totals move by a few controller instructions. This compares two runs of the same job PHASE BY PHASE on that
 * basis, instead of demanding identical totals:
 *
 *  1. Data plane: every phase has the same name and command word, and every engine and network column is equal --
 *     address-generator requests, stalls, late rotations and runs; gather requests; read-port reads; injected and
 *     ejected packets; read-queue latencies; send stalls; invariant 8; every peak and maximum latency.
 *     One exception, the OVERLAY RULE: an overlay image that grew or shrank by k 64-bit packets is copied by one more or
 *     fewer packet per tile, so in the phase that loads it agReq, injected, ejected and the AG latency count (latNA)
 *     all move by exactly 64 k (and latSumA with them), where k is a sum of the overlays' size changes (from the two
 *     images' sort.sym, __load_start/stop_ov_*). Nothing else may differ.
 *  2. Cycles: a phase ends when its slowest tile is READY, so its cycle change is traced to that tile's controller.
 *     When both runs have PhaseLog's slowest-tile columns (M11 with m11-phaselog.patch) and the slowest tile is
 *     the same tile, r = dCycles - dBusy - k, where dBusy is the change in that tile's busy cycles (instructions plus
 *     stall cycles: taken branches, multiplies, send retries) and k the overlay packets (one cycle each, spent in WFI);
 *     r is then 0 unless a wait moved. Otherwise (older CSVs, or another tile became the slowest), the fallback
 *     r = dCycles - dInstr / 64 - k, with dInstr the change summed over the 64 controllers, i.e. the mean per tile.
 *     A phase is explained when |r| <= RESIDUAL_TOL, or, when the slowest tile waited on others (an exchange, a
 *     token: its own busy change is then not the whole story), when dCycles - k lies within the range of every tile's
 *     busy-cycle change in the phase (PhaseLog's tile_busy), +-RESIDUAL_TOL: the critical path ran through other tiles'
 *     code, and no data-plane difference exists to explain anything else. r is reported, not hidden, with the basis.
 *  3. Steps: the step CSVs have the same steps, path, stage-2 and BT decisions and checks; each step's cycles change by
 *     the sum of its phases' changes (C&C's turnaround is unchanged), and its overlay_load column by k.
 *
 * Usage: java PhaseNeutral DIR_A DIR_B SYM_A SYM_B [out.md]   (every <id>.csv / <id>.phases.csv present in both)
 */
public final class PhaseNeutral {
    static final double RESIDUAL_TOL = 2.0;
    /** The v1.0 start rule (meshsim v1.0 against M15): arm is merged into start. (1) A run is counted where it starts, not
     *  where M15 armed it, so agRuns and peak_agTable may move between phases if their run totals agree (sum, max).
     *  (2) A phase's cycle change may exceed the controllers' change by up to one lead-in (4 ts_engine_latency + 2) per
     *  run it starts: M15 pre-armed a run in an earlier command, so its first request went out at the start; in v1.0
     *  the read-ahead follows the start. */
    static boolean START_RULE = false;
    static final Set<String> MOVABLE = Set.of("agRuns", "peak_agTable");
    static final Set<String> TIMING = Set.of("phase", "go_at", "end_at", "cycles", "ready_spread", "slowest_tile", "cnc_reaction", "instr", "wfiCycles", "slow_instr", "slow_busy", "tile_busy");
    static final List<String> OVERLAY = List.of("agReq", "injected", "ejected", "latNA");
    static final Set<String> STEP_TIMING = Set.of("cycles", "go_ready", "overlay_load");

    /** Packets (8 B) per overlay from a sort.sym: name -> packets. */
    static Map<String, Long> overlays(Path sym) throws IOException {
        Map<String, Long> start = new LinkedHashMap<>(), stop = new LinkedHashMap<>(), out = new LinkedHashMap<>();
        for (String l : Files.readAllLines(sym)) {
            String[] f = l.trim().split("\\s+");
            if (f.length < 3) continue;
            String n = f[f.length - 1];
            if (n.startsWith("__load_start_ov_")) start.put(n.substring(16), Long.parseLong(f[0], 16));
            if (n.startsWith("__load_stop_ov_")) stop.put(n.substring(15), Long.parseLong(f[0], 16));
        }
        for (String n : start.keySet()) if (stop.containsKey(n)) out.put(n, (stop.get(n) - start.get(n)) / 8);
        return out;
    }
    /** The allowed per-tile packet changes: every sum of a subset of the overlays' size changes (0 included). */
    static Set<Long> allowedK(Path symA, Path symB) throws IOException {
        Map<String, Long> a = overlays(symA), b = overlays(symB);
        List<Long> d = new ArrayList<>();
        for (String n : a.keySet()) if (b.containsKey(n) && !b.get(n).equals(a.get(n))) d.add(b.get(n) - a.get(n));
        Set<Long> s = new TreeSet<>();
        for (int m = 0; m < (1 << d.size()); m++) { long t = 0; for (int i = 0; i < d.size(); i++) if ((m >> i & 1) != 0) t += d.get(i); s.add(t); }
        return s;
    }
    static String describeOverlays(Path symA, Path symB) throws IOException {
        Map<String, Long> a = overlays(symA), b = overlays(symB);
        List<String> l = new ArrayList<>();
        for (String n : a.keySet()) l.add(n + " " + a.get(n) + (Objects.equals(a.get(n), b.get(n)) ? "" : " -> " + b.get(n)));
        return String.join(", ", l);
    }

    static final class Result {
        boolean ok = true; int phases, overlayPhases, residualPhases, slowBasis, meanBasis, rangePhases, leadPhases; long dCycles, dOverlay, dTotal; double dInstr, dBusy, residual, maxAbsResidual;
        String pathA = "?", pathB = "?";
        final List<String> problems = new ArrayList<>();
        final List<String> notes = new ArrayList<>();   // phases whose timing changed: name dCyc = dInstr/64 + k + r
        void bad(String s) { ok = false; if (problems.size() < 12) problems.add(s); }
    }
    static long num(String s) { return s == null || s.isEmpty() ? 0 : Long.parseLong(s.replace(",", "")); }

    static Result compare(Path phA, Path phB, Path stA, Path stB, Set<Long> allowedK) throws IOException {
        Result r = new Result();
        if (!Files.exists(phA) || !Files.exists(phB)) { r.bad("phase CSV missing"); return r; }
        List<Map<String, String>> A = SortCompare.read(phA), B = SortCompare.read(phB);
        int L = 1;
        if (Files.exists(stB)) { var st = SortCompare.read(stB); if (!st.isEmpty() && st.get(0).get("params") != null) {
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("tsEngineLatency=(\\d+)").matcher(st.get(0).get("params")); if (m.find()) L = Integer.parseInt(m.group(1)); } }
        final long lead = 4L * L + 2;
        long runsA = 0, runsB = 0, peakA = 0, peakB = 0;
        if (A.size() != B.size()) r.bad("phase count " + A.size() + " -> " + B.size());
        for (int i = 0; i < Math.min(A.size(), B.size()); i++) {
            Map<String, String> x = A.get(i), y = B.get(i);
            r.phases++;
            String nm = x.get("phase") + " " + x.get("name");
            List<String> diff = new ArrayList<>();
            for (String c : x.keySet()) if (!TIMING.contains(c) && !OVERLAY.contains(c) && !c.equals("latSumA") && !(START_RULE && MOVABLE.contains(c)) && !Objects.equals(x.get(c), y.get(c))) diff.add(c + " " + x.get(c) + " -> " + y.get(c));
            runsA += num(x.get("agRuns")); runsB += num(y.get("agRuns"));
            peakA = Math.max(peakA, num(x.get("peak_agTable"))); peakB = Math.max(peakB, num(y.get("peak_agTable")));
            long started = (num(y.get("agRuns")) + 63) / 64;           // runs started in this phase (v1.0 counts a run at its start)
            long k = 0;
            long d0 = num(y.get("agReq")) - num(x.get("agReq"));
            boolean ovEq = true;
            for (String c : OVERLAY) if (num(y.get(c)) - num(x.get(c)) != d0) ovEq = false;
            if (!ovEq || d0 % 64 != 0 || !allowedK.contains(d0 / 64)) {
                for (String c : OVERLAY) if (!Objects.equals(x.get(c), y.get(c))) diff.add(c + " " + x.get(c) + " -> " + y.get(c));
                if (!Objects.equals(x.get("latSumA"), y.get("latSumA"))) diff.add("latSumA " + x.get("latSumA") + " -> " + y.get("latSumA"));
            } else if (d0 != 0) { k = d0 / 64; r.overlayPhases++; }
            else if (!Objects.equals(x.get("latSumA"), y.get("latSumA"))) diff.add("latSumA " + x.get("latSumA") + " -> " + y.get("latSumA"));
            if (!diff.isEmpty()) r.bad("phase " + nm + ": " + String.join("; ", diff));
            long dc = num(y.get("cycles")) - num(x.get("cycles"));
            double di = (num(y.get("instr")) - num(x.get("instr"))) / 64.0;
            boolean slow = x.get("slow_busy") != null && y.get("slow_busy") != null && !x.get("slow_busy").isEmpty() && !y.get("slow_busy").isEmpty()
                    && num(x.get("slow_busy")) >= 0 && Objects.equals(x.get("slowest_tile"), y.get("slowest_tile"));
            double db = slow ? num(y.get("slow_busy")) - num(x.get("slow_busy")) : di;
            if (slow) r.slowBasis++; else r.meanBasis++;
            double res = dc - db - k;
            r.dCycles += dc; r.dInstr += di; r.dBusy += db; r.dOverlay += k; r.residual += res;
            r.maxAbsResidual = Math.max(r.maxAbsResidual, Math.abs(res));
            if (Math.abs(res) > 1e-9) r.residualPhases++;
            String range = "";
            if (Math.abs(res) > RESIDUAL_TOL && x.get("tile_busy") != null && y.get("tile_busy") != null && !x.get("tile_busy").isEmpty() && !y.get("tile_busy").isEmpty()) {
                String[] ta = x.get("tile_busy").split(";"), tb = y.get("tile_busy").split(";");
                long lo = Long.MAX_VALUE, hi = Long.MIN_VALUE;
                for (int t = 0; t < Math.min(ta.length, tb.length); t++) { long d = num(tb[t]) - num(ta[t]); lo = Math.min(lo, d); hi = Math.max(hi, d); }
                if (dc - k >= lo - RESIDUAL_TOL && dc - k <= hi + RESIDUAL_TOL) { range = String.format(" [tiles %+d..%+d]", lo, hi); r.rangePhases++; }
            }
            String leadNote = "";
            if (START_RULE && Math.abs(res) > RESIDUAL_TOL && range.isEmpty() && res >= -RESIDUAL_TOL && res <= started * lead + RESIDUAL_TOL) {
                leadNote = String.format(" [lead-in: %d run%s x %d]", started, started == 1 ? "" : "s", lead); r.leadPhases++;
            }
            if (Math.abs(res) > RESIDUAL_TOL && range.isEmpty() && leadNote.isEmpty()) r.bad(String.format("phase %s: cycles %+d not explained: %s %+.2f, overlay packets %+d, residual %+.2f", nm, dc, slow ? "slowest tile's busy cycles" : "instructions/tile", db, k, res));
            if (dc != 0 || di != 0 || k != 0) r.notes.add(String.format("%s %+d = %+.2f %s %+d ov%s%s%s", x.get("name"), dc, db, slow ? "busy(t" + x.get("slowest_tile") + ")" : "instr/64", k, Math.abs(res) > 1e-9 ? String.format(" %+.2f", res) : "", range, leadNote));
        }
        if (START_RULE && runsA != runsB) r.bad("agRuns over the run " + runsA + " -> " + runsB);
        if (START_RULE && peakA != peakB) r.bad("peak_agTable over the run " + peakA + " -> " + peakB);
        // steps
        if (!Files.exists(stA) || !Files.exists(stB)) { r.bad("step CSV missing"); return r; }
        List<Map<String, String>> SA = SortCompare.read(stA), SB = SortCompare.read(stB);
        if (SA.size() != SB.size()) r.bad("step count " + SA.size() + " -> " + SB.size());
        long stepSum = 0;
        for (int i = 0; i < Math.min(SA.size(), SB.size()); i++) {
            Map<String, String> x = SA.get(i), y = SB.get(i);
            if (i == 0) { r.pathA = x.get("path"); r.pathB = y.get("path"); }
            for (String c : x.keySet()) if (!STEP_TIMING.contains(c) && !Objects.equals(x.get(c), y.get(c))) r.bad("step " + x.get("step") + ": " + c + " " + x.get(c) + " -> " + y.get(c));
            long d = num(y.get("cycles")) - num(x.get("cycles"));
            if ("TOTAL".equals(x.get("step"))) r.dTotal = d; else stepSum += d;
        }
        if (stepSum != r.dTotal) r.bad("steps sum to " + stepSum + ", TOTAL changed by " + r.dTotal);
        if (r.dCycles != r.dTotal) r.bad("phases sum to " + r.dCycles + ", TOTAL changed by " + r.dTotal + " (C&C turnaround changed?)");
        return r;
    }

    public static void main(String[] a) throws IOException {
        if (a.length > 0 && a[0].equals("--v1.0")) { START_RULE = true; a = java.util.Arrays.copyOfRange(a, 1, a.length); }
        Path A = Path.of(a[0]), B = Path.of(a[1]);
        Set<Long> K = allowedK(Path.of(a[2]), Path.of(a[3]));
        StringBuilder sb = new StringBuilder("# Phase-level comparison: " + B + " against " + A + "\n\n");
        sb.append("Overlays (packets): ").append(describeOverlays(Path.of(a[2]), Path.of(a[3]))).append("; allowed per-tile packet changes ").append(K).append(".\n\n");
        sb.append("| run | path | phases (slowest-tile basis / mean basis / within the tiles' range) | data plane | overlay phases | TOTAL change | = controller cycles | + overlay packets | + residual (max per phase) | instr/tile | changed phases: dCycles = d(basis) + k ov (r) |\n|---|---|---|---|---|---|---|---|---|---|---|\n");
        List<Path> fs = new ArrayList<>();
        try (var s = Files.list(A)) { s.filter(p -> p.toString().endsWith(".phases.csv")).sorted().forEach(fs::add); }
        int n = 0, ok = 0;
        for (Path f : fs) {
            String id = f.getFileName().toString().replace(".phases.csv", "");
            if (!Files.exists(B.resolve(id + ".phases.csv"))) continue;
            Result r = compare(f, B.resolve(id + ".phases.csv"), A.resolve(id + ".csv"), B.resolve(id + ".csv"), K);
            n++; if (r.ok) ok++;
            sb.append(String.format("| %s | %s | %d (%d / %d / %d) | %s | %d | %+d | %+.2f | %+d | %+.2f (%.2f) | %+.2f | %s |%n", id, r.pathA.equals(r.pathB) ? r.pathA : r.pathA + " / " + r.pathB, r.phases, r.slowBasis, r.meanBasis, r.rangePhases,
                    r.ok ? "identical, explained" : "**" + String.join("; ", r.problems) + "**", r.overlayPhases, r.dTotal, r.dBusy, r.dOverlay, r.residual, r.maxAbsResidual, r.dInstr, String.join("; ", r.notes)));
        }
        sb.append(String.format("%n%d runs compared, %d explained (data plane identical; every phase's cycle change explained by the slowest tile's controller within %.0f cycles, or within the range of all tiles' changes).%n", n, ok, RESIDUAL_TOL));
        System.out.print(sb);
        if (a.length > 4) Files.writeString(Path.of(a[4]), sb);
    }
}
