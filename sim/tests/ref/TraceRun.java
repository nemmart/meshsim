import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The trace runner (sim/planning/PLAN-M1.md, W2): boots one program on one tile alone, as RiscvTest boots tile 0 (code
 * and data poked into the tile's memories; C&C 1 expects that tile's READY alone; the boot command, then GO; the boot
 * ROM jumps), lands the further commands and GOs of the program's test-bench schedule, and writes the retire trace
 * through sim/ref's Observer (sim/tests/TRACE-FORMAT.md). Compiled into sim/ref's default package (its classes are
 * package-private) and run with -Dmeshsim.observe=on.
 *
 *   java -Dmeshsim.observe=on -cp REF_BUILD:THIS_BUILD TraceRun OUT TILE IMAGE_DIR NAME [SCHEDULE]
 *
 * IMAGE_DIR/NAME.code.bin goes to program memory from 0x01000000, NAME.data.bin to data memory: at NAME.sym's
 * _data_start with the boot command's entry _start, or, with no NAME.sym (the riscv-tests), at 0x10 with the entry
 * 0x01000000. The schedule (sim/fw/NAME.tb) has one step per line, run in order after the boot's GO:
 *   at N              wait until cycle N (C&C's step runs at N)
 *   cmd WORD [ARG..]  C&C sends a command to the tile (WORD and ARGs hex): the arguments, then slot 0, through the mesh
 *   go                C&C clears READY and stores GO now, without waiting for READY
 *   readygo           C&C waits for the tile's READY, then GO (as at the boot)
 *   max N             run at most N cycles (default 200000)
 *   boot ARG..        the boot command's arguments after the entry (slots 2-7; hex), wherever the line stands
 * The run ends when the core halts, the boot ROM's handler parks, or an UndefinedBehavior stop; then the closing block.
 */
public final class TraceRun {
    public static void main(String[] args) throws Exception {
        if (args.length < 4) { System.err.println("usage: TraceRun OUT TILE IMAGE_DIR NAME [SCHEDULE]"); System.exit(2); }
        if (!Observer.ON) { System.err.println("TraceRun: run with -Dmeshsim.observe=on"); System.exit(2); }
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

        try (FileWriter w = new FileWriter(out.toFile())) {
            Observer.start(w, tile);
            header(k, c, tile, name, codeF, dataF, dataAt, entry, steps);
            String end;
            try {
                boolean stopped = c.runUntil(() -> core.halt != Controller.RUN || core.parked, max);
                end = !stopped ? "timeout" : core.halt != Controller.RUN ? "halt " + core.halt : "parked";
            } catch (UndefinedBehavior u) {
                String m = u.getMessage(); int i = m.indexOf(u.unit + ": "), j = m.indexOf(" (DECISIONS");
                Observer.line(u.cycle + " UB " + u.unit + ": " + m.substring(i + u.unit.length() + 2, j));
                end = "ub";
            }
            closing(c, core, end);
            Observer.stop();
        }
    }

    /** The header (D12): the configuration the trace was made under. */
    static void header(Config k, Chip c, int tile, String name, Path codeF, Path dataF, int dataAt, int entry, List<String> steps) throws Exception {
        Observer.line("# meshsim retire trace v1 (sim/tests/TRACE-FORMAT.md)");
        Observer.line("# program " + name);
        Observer.line("# tile " + tile);
        Observer.line("# image code " + sha256(Files.readAllBytes(codeF)) + " data " + (Files.exists(dataF) ? sha256(Files.readAllBytes(dataF)) : "none")
                + String.format(" data_at %08x entry %08x", dataAt, entry));
        Observer.line("# bootrom " + sha256(words(Controller.bootRom)));
        Controller core = c.tiles[tile].core;
        String[][] cfg = {
            { "reg_load_latency", "" + k.regLoadLatency }, { "branch_penalty", "" + core.branchPenalty }, { "mul_latency", "" + core.mulLatency },
            { "ref_cycle", "" + k.refCycle }, { "go_latency", "" + k.goLatency }, { "ready_latency", "" + k.readyLatency },
            { "error_latency", "" + k.errorLatency() }, { "start_skew", "" + c.startSkew[tile] }, { "stage_delay", "" + k.stageDelay },
            { "cnc_reaction", "" + k.cncReaction }, { "cnc_drain_reports", "" + c.cc.drainReports }, { "cnc_go_settle", "" + k.cncGoSettle }, { "done_gate", "" + k.doneGate },
            { "request_fifo_depth", "" + k.requestFifoDepth }, { "req_hop_latency", "" + k.reqHopLatency },
            { "link_latency", "" + k.linkLatency }, { "intra_tile_latency", "" + k.intraTileLatency }, { "cm_latency", "" + k.cmLatency },
            { "queue_depth", "" + k.queueDepth }, { "K", "" + k.K }, { "mem_chunks", "" + k.memChunks }, { "periph_type", "" + k.periphTypeOf(tile) },
            { "cc_cores", "" + k.ccCores }, { "idle_skip", "" + k.idleSkip }, { "shuffle_seed", "" + k.shuffleSeed },
        };
        for (String[] kv : cfg) Observer.line("# config " + kv[0] + " " + kv[1]);
        for (String s : steps) Observer.line("# tb " + s);
    }

    /** The closing block: how the run ended, the registers, the CSRs, mcycle and the data memory's digest. */
    static void closing(Chip c, Controller core, String end) throws Exception {
        Observer.line("END " + end + " cycle " + c.cycle);
        StringBuilder r = new StringBuilder("REGS");
        for (int i = 1; i < 32; i++) r.append(String.format(" %08x", core.x[i]));
        Observer.line(r.toString());
        Observer.line(String.format("CSRS pc %08x mstatus %08x mie %08x mtvec %08x mscratch %08x mepc %08x mcause %08x mtval %08x mcycle %016x",
                core.pc, core.mstatus, core.mie, core.mtvec, core.mscratch, core.mepc, core.mcause, core.mtval, core.mcycle));
        int[] d = new int[ControllerMemories.BYTES / 4];
        for (int i = 0; i < d.length; i++) d[i] = core.peekLocal(Registers.DMEM_BASE + 4 * i);
        Observer.line("DMEM " + sha256(words(d)));
    }

    static byte[] words(int[] w) {
        byte[] b = new byte[4 * w.length];
        for (int i = 0; i < w.length; i++) for (int k = 0; k < 4; k++) b[4 * i + k] = (byte) (w[i] >>> 8 * k);
        return b;
    }
    static String sha256(byte[] b) throws Exception {
        StringBuilder s = new StringBuilder();
        for (byte x : MessageDigest.getInstance("SHA-256").digest(b)) s.append(String.format("%02x", x));
        return s.toString();
    }
}
