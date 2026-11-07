import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The core's canonical fingerprint on M1's goldens (WORKER8; sim/tests/AUDIT-M2.md 6, StateFp.core): boots one program
 * on one tile exactly as TraceRun does (the same setup, copied), and after every cycle writes the tile's core stream and
 * its memories (StateFp's cmem without its port-B bus), so the C++ core's fingerprint writer is checked, cycle by cycle,
 * against ref on every M1 program. Reads ref by reflection only (StateFp), the observer off.
 *
 *   java -cp REF_BUILD:THIS_BUILD CoreFp OUT TILE IMAGE_DIR NAME [SCHEDULE]
 * OUT: "cycle t<k>.core hash", "cycle t<k>.mem hash" and "cycle t<k>.ctlx hash" (StateFp.ctl without its GO queue,
 * go.n and go.at: M1's bench delivers GO at its cycle and keeps no queue) per cycle, from 0 to the run's end (the core halts, parks, or
 * a UB stop).
 */
public final class CoreFp {
    public static void main(String[] args) throws Exception {
        Path out = Path.of(args[0]); int tile = Integer.parseInt(args[1]); Path dir = Path.of(args[2]); String name = args[3];
        Path tb = args.length > 4 ? Path.of(args[4]) : null;

        Path codeF = dir.resolve(name + ".code.bin"), dataF = dir.resolve(name + ".data.bin"), symF = dir.resolve(name + ".sym");
        int[] code = Mach.words(codeF), data = Files.exists(dataF) ? Mach.words(dataF) : new int[0];
        int dataAt = Registers.DMEM_FIRST, entry = Registers.PMEM_BASE;
        if (Files.exists(symF)) {
            Map<String, Integer> sym = new HashMap<>();
            for (String l : Files.readAllLines(symF)) { String[] f = l.trim().split("\\s+"); if (f.length == 3) sym.put(f[2], (int) Long.parseLong(f[0], 16)); }
            dataAt = sym.getOrDefault("_data_start", Registers.DMEM_FIRST); entry = sym.get("_start");
        }
        List<String> steps = new ArrayList<>();
        if (tb != null) for (String l : Files.readAllLines(tb)) { String s = l.replaceAll("#.*", "").trim(); if (!s.isEmpty()) steps.add(s); }

        List<Integer> bootArgs = new ArrayList<>(List.of(entry));
        for (String s : steps) if (s.startsWith("boot ")) for (String a : s.substring(5).trim().split("\\s+")) bootArgs.add((int) Long.parseLong(a, 16));

        Config k = Config.machine();
        k.checkErrors = false;                                   // faults, irq and the div tests raise ERRORS on purpose
        Chip c = new Chip(k);
        Controller core = c.tiles[tile].core;
        for (int i = 0; i < code.length; i++) core.pokeLocal(Registers.PMEM_BASE + 4 * i, code[i]);
        for (int i = 0; i < data.length; i++) core.pokeLocal(dataAt + 4 * i, data[i]);
        c.cc1p.expect = 1L << tile;                              // only this tile starts (6.2): its command, then GO
        c.cc.command(tile, Registers.CMD_JUMP << 24, bootArgs.stream().mapToInt(Integer::intValue).toArray()).go();
        long max = 200_000;
        for (String s : steps) {
            String[] f = s.split("\\s+");
            switch (f[0]) {
                case "at" -> { long n = Long.parseLong(f[1]); c.cc.then((cc, t) -> t >= n); }
                case "cmd" -> { int[] a = new int[f.length - 2]; for (int i = 0; i < a.length; i++) a[i] = (int) Long.parseLong(f[i + 2], 16); c.cc.command(tile, (int) Long.parseLong(f[1], 16), a); }
                case "go" -> c.cc.then((cc, t) -> { cc.clearReady(t); cc.storeGo(t); return true; });
                case "readygo" -> c.cc.go();
                case "max" -> max = Long.parseLong(f[1]);
                case "boot" -> { }
                default -> throw new IllegalArgumentException(tb + ": unknown step: " + s);
            }
        }

        try (PrintWriter w = new PrintWriter(out.toFile())) {
            w.println("# core fingerprint v1 (StateFp.core; mem = StateFp's cmem without portB) " + name + " tile " + tile);
            final Chip cc = c; final Controller co = core; final int tl = tile;
            try {
                cc.runUntil(() -> { line(w, cc, tl); return co.halt != Controller.RUN || co.parked; }, max);
            } catch (UndefinedBehavior u) { w.println("# UB at " + u.cycle); }
        }
    }
    static void line(PrintWriter w, Chip c, int tile) {
        Tile p = c.tiles[tile];
        StateFp.Out o = new StateFp.Out(); StateFp.core(o, c, p, c.cycle);
        w.printf("%d t%d.core %016x%n", c.cycle, tile, o.done());
        StateFp.Out m = new StateFp.Out();
        m.w("dmem", StateFp.banked(StateFp.R_DMEM, p.cmem.dmem)); m.w("pmem", StateFp.banked(StateFp.R_PMEM, p.cmem.imem));
        w.printf("%d t%d.mem %016x%n", c.cycle, tile, m.done());
        StateFp.Out x = new StateFp.Out() { @Override void w(String n, long v) { if (!n.startsWith("go.")) super.w(n, v); } };
        StateFp.ctl(x, p.ctl, c.cycle);
        w.printf("%d t%d.ctlx %016x%n", c.cycle, tile, x.done());
    }
}
