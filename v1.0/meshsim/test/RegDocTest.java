import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * REGISTERS.md against the generator, mesh.h and the simulator.
 *
 * 1. Every generated block of REGISTERS.md matches RegDoc, and every register and table entry has one.
 * 2. mesh.h is what the generator writes; its 13 registers sit at 0x800 + 4 i in order; its conditions are the
 *    document's; the programmer's copy (../sw/mesh.h in the v1.0 layout) is the generated one.
 * 3. The simulator behaves as the document says: reset values, what loads return, read-only stores, the widths of the
 *    one-field registers and table entries, ROUTER_SET_HDR's auto-increment per mode, ROUTER_SET_HI cleared by a send,
 *    window loads from PTR, STREAM and GROUP and sub-word accesses as bus errors, nothing past the 13th register; and
 *    the split's own register header agrees with mesh.h.
 *
 * Run: java -cp build RegDocTest
 */
final class RegDocTest {
    static int pass, fail;
    static void check(boolean ok, String what) {
        if (ok) pass++; else { fail++; System.out.println("FAIL " + what); }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("RegDocTest: mesh.h 1.0");
        String doc = Files.readString(Registers.doc("REGISTERS.md"));
        String meshh = Files.readString(Path.of("sw/mesh.h"));
        if (Files.exists(Path.of("../sw/mesh.h")))
            check(Files.readString(Path.of("../sw/mesh.h")).equals(meshh), "../sw/mesh.h (the programmer's copy) is the generated sw/mesh.h");

        // ---- 1. the generated blocks
        List<String> bad = RegDoc.check(doc);
        for (String b : bad) check(false, "REGISTERS.md: " + b);
        check(bad.isEmpty(), "REGISTERS.md's generated blocks");

        // ---- 2. mesh.h
        check(meshh.equals(Registers.header()), "sw/mesh.h is what Registers generates");
        Map<Integer, String> regs = new TreeMap<>(Integer::compareUnsigned);
        Matcher a = Pattern.compile("#define (\\w+)_ADDR\\s+(0xFFFFF[0-9A-F]{3})").matcher(meshh);
        while (a.find()) regs.put((int) Long.parseLong(a.group(2).substring(2), 16), a.group(1));
        Map<Integer, String> conds = new TreeMap<>();
        Matcher cm = Pattern.compile("#define (?:TILE_)?COND_(\\w+)\\s+(0x[0-9A-Fa-f]+|\\d+)").matcher(meshh);
        while (cm.find()) if (!cm.group(1).equals("MASK")) conds.put(Integer.numberOfTrailingZeros(Integer.decode(cm.group(2))), cm.group(1));
        List<String> docConds = new ArrayList<>();
        for (int i : RegDoc.COND) docConds.add(Conditions.CNAME[i]);
        { List<String> want = new ArrayList<>(), got = new ArrayList<>(regs.values());
          for (Registers.Reg r : Registers.ALL) want.add(r.name);
          check(got.equals(want), "mesh.h 1.0's registers, in address order: " + got); }
        int ri = 0;
        for (Map.Entry<Integer, String> e : regs.entrySet()) check((e.getKey() & 0xFFF) == 0x800 + 4 * ri++, "mesh.h: " + e.getValue() + " at " + Integer.toHexString(e.getKey()));
        check(regs.size() == 13 && conds.size() == 11, "mesh.h 1.0: 13 registers, 11 conditions");
        List<String> meshConds = new ArrayList<>(conds.values());
        check(meshConds.equals(docConds), "the document's conditions in bit order are mesh.h's: " + docConds);

        // ---- 3. the simulator
        Chip c = new Chip(Config.machine());
        c.run(20);
        Tile p = c.tiles[5];
        Controller core = p.core;
        long t = c.cycle + 1;   // the next cycle: a store at t is applied by the cycle that step() runs
        // reset values and loads
        for (RegDoc.Reg r : RegDoc.REGS) {
            RegDoc.RD d = RegDoc.DOCS.get(r.name());
            Integer v = core.load(t, r.sim().get(0).addr(), 4);
            check(v != null, r.name() + ": a load is not a bus error");
            if (v == null) continue;
            if (r.access() == Registers.W) check(v == 0, r.name() + ": write-only, a load returns 0 (got " + v + ")");
            switch (r.name()) {
                case "TILE_STATUS" -> {
                    for (RegDoc.DF f : RegDoc.WORDS.get("TILE_STATUS").fields()) {
                        int got = (v >>> f.lo()) & (f.width() == 32 ? -1 : (1 << f.width()) - 1);
                        if (f.reset().equals("index")) check(got == 5, "TILE_STATUS.tile index at reset = 5 (got " + got + ")");
                        else if (!f.reset().equals("device")) check(got == Integer.parseInt(f.reset()), "TILE_STATUS." + f.label() + " at reset = " + f.reset() + " (got " + got + ")");
                    }
                }
                case "TILE_CONDITIONS", "DMA_SRAM_REMAINING", "DMA_CM_REMAINING", "DMA_REPLY_REMAINING" -> check(v == 0 && d.reset().equals("0"), r.name() + " at reset = 0 (got " + v + ")");
                case "SRAM_RD_ADDR_QUEUE" -> check(v == p.readPort.depth && v >= 64, "SRAM_RD_ADDR_QUEUE at reset = the depth, at least 64 (got " + v + ")");
                default -> { }
            }
        }
        // read-only stores
        for (RegDoc.Reg r : RegDoc.REGS) if (r.access() == Registers.R)
            check(core.store(t, r.sim().get(0).addr(), 0, -1) != null, r.name() + ": a store is a bus error");
        // widths: store all ones
        for (RegDoc.Reg r : RegDoc.REGS) if (r.name().endsWith("_REMAINING") || r.name().equals("SRAM_RD_DEST_ADDR") || r.name().equals("ROUTER_SET_HI"))
            for (Registers.Reg s : r.sim()) { check(core.store(c.cycle + 1, s.addr(), -1, -1) == null, r.name() + ": store"); c.step(); }
        for (Registers.Reg s : RegDoc.reg("DMA_RECORD_BASE_ADDRESS").sim()) { core.store(c.cycle + 1, s.addr(), -1, -1); c.step(); }
        core.store(c.cycle + 1, Registers.HIST_BASE, -1, -1); c.step();
        core.store(c.cycle + 1, Registers.PTR_BASE, -1, -1); c.run(8);
        int cw = (1 << RegDoc.COUNTER_WIDTH) - 1;
        for (RegDoc.Reg r : RegDoc.REGS) if (r.name().endsWith("_REMAINING")) {
            Integer v = core.load(c.cycle + 1, r.sim().get(0).addr(), 4);
            check(v != null && v == cw, r.name() + ": " + RegDoc.COUNTER_WIDTH + " bits (all ones reads " + v + ")");
            core.store(c.cycle + 1, r.sim().get(0).addr(), 0, -1); c.step();
        }
        int bw = (1 << RegDoc.BASE_WIDTH) - 1;
        check(p.writer.augmentBase == bw && p.gather.base == bw, "DMA_RECORD_BASE_ADDRESS: " + RegDoc.BASE_WIDTH + " bits, used by augment and gather (" + p.writer.augmentBase + ", " + p.gather.base + ")");
        check(p.ctl.rdDest == (1 << RegDoc.RD_DEST_WIDTH) - 1, "SRAM_RD_DEST_ADDR: " + RegDoc.RD_DEST_WIDTH + " bits (" + p.ctl.rdDest + ")");
        check(p.ctl.setHi == -1, "ROUTER_SET_HI: 32 bits");
        check(p.hist.pipe.ctrlRead(c.cycle + 1, 0) == (1 << RegDoc.HIST_WIDTH) - 1, "HIST entry: " + RegDoc.HIST_WIDTH + " bits");
        check(p.ts.pipe.ctrlRead(c.cycle + 1, 0) == (1 << RegDoc.PTR_WIDTH) - 1, "PTR entry: " + RegDoc.PTR_WIDTH + " bits");
        // ROUTER_SET_HDR's addr after a send, per mode (the modes table), and ROUTER_SET_HI cleared
        int hdr = RegDoc.reg("ROUTER_SET_HDR").sim().get(0).addr(), hi = RegDoc.reg("ROUTER_SET_HI").sim().get(0).addr(), lo = RegDoc.reg("ROUTER_SEND_LO").sim().get(0).addr();
        Map<Integer, String> step = new LinkedHashMap<>();
        for (String[] m : RegDoc.MODES)
            if (m[0].contains("–")) { String[] ab = m[0].split("–"); for (int k = Integer.parseInt(ab[0]); k <= Integer.parseInt(ab[1]); k++) step.put(k, m[5]); }
            else step.put(Integer.parseInt(m[0]), m[5]);
        for (Map.Entry<Integer, String> e : step.entrySet()) {
            int mode = e.getKey();
            core.store(c.cycle + 1, hdr, Registers.H_DST.place(1) | Registers.H_MODE.place(mode) | Registers.H_ADDR.place(0x100), -1); c.step();
            core.store(c.cycle + 1, hi, 0x1234, -1); c.step();
            core.store(c.cycle + 1, lo, 0x5678, -1); c.run(4);
            int inc = Registers.H_ADDR.get(p.ctl.setHdr) - 0x100;
            int want = switch (e.getValue()) { case "+2" -> 2; case "+1" -> 1; default -> 0; };
            check(inc == want, "ROUTER_SET_HDR addr after a mode-" + mode + " send: " + e.getValue() + " (got +" + inc + ")");
            check(p.ctl.setHi == 0, "ROUTER_SET_HI is 0 after a mode-" + mode + " send");
            c.run(20);
        }
        // window loads, sub-word accesses, the end of the register block
        int[] tables = { Registers.PTR_BASE, Registers.STREAM_BASE, Registers.GROUP_BASE };
        String[] tnames = { "PTR", "STREAM", "GROUP" };
        for (int i = 0; i < 3; i++) {
            boolean busErr = core.load(c.cycle + 1, tables[i], 4) == null;
            check(busErr, "a load from " + tnames[i] + " is a bus error");
        }
        check(core.load(c.cycle + 1, Registers.TILE_STATUS.addr(), 2) == null, "a halfword load from a register is a bus error");
        check(core.load(c.cycle + 1, Registers.TILE_STATUS.addr(), 1) == null, "a byte load from a register is a bus error");
        check(core.load(c.cycle + 1, Registers.HIST_BASE, 2) == null, "a halfword load from the table window is a bus error");
        check(core.load(c.cycle + 1, Registers.HIST_BASE, 4) != null, "a word load from HIST is not");
        check(core.store(c.cycle + 1, Registers.HIST_BASE, 0, 0xFFFF) != null, "a halfword store to the table window is a bus error");
        // the split keeps its own register header (it builds on its host ISS too): every address in it is mesh.h 1.0's
        try {
            int n = 0;
            for (String line : java.nio.file.Files.readAllLines(java.nio.file.Path.of("sw/ext/split-bisect/split_regs.h"))) {
                java.util.regex.Matcher mm = java.util.regex.Pattern.compile("#define\\s+(\\w+)\\s+SR_REG\\(0x([0-9A-Fa-f]+)u\\)").matcher(line);
                if (!mm.find() || mm.group(1).equals("TRACE_REG")) continue;
                Registers.Reg r = null; for (Registers.Reg x : Registers.ALL) if (x.name.equals(mm.group(1))) r = x;
                check(r != null && (r.addr() & 0xFFFFFFFFL) == Long.parseLong(mm.group(2), 16), "split_regs.h: " + mm.group(1) + " at 0x" + mm.group(2) + " is mesh.h's");
                n++;
            }
            check(n == 9, "split_regs.h: nine registers checked (got " + n + ")");
        } catch (java.io.IOException e) { check(false, "split_regs.h readable: " + e); }
        boolean pastLast = core.load(c.cycle + 1, RegDoc.regEnd(), 4) == null && core.store(c.cycle + 1, RegDoc.regEnd(), 0, -1) != null;
        check(pastLast, "the offset after the last register is unmapped");

        System.out.println("RegDocTest: " + pass + " passed, " + fail + " failed");
        System.exit(fail == 0 ? 0 : 1);
    }
}
