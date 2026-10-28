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
 * 2. mesh.h is what the generator writes (version 1.1, 26n); its 40 offsets 0x800-0x89C carry 44 names (the request
 *    port's four offsets a store name and a load name each); its 15 conditions are the document's; the programmer's
 *    copy (../sw/mesh.h in the layout) is the generated one; none of the withdrawn names survives.
 * 3. The simulator behaves as the document says: reset values, what loads return, read-only stores, the widths of the
 *    one-field registers and table entries, ROUTER_SET_HDR's auto-increment per mode and send register (the modes
 *    table), ROUTER_SET_W0-W2 cleared by a send, window loads from PTR, the three STREAM arrays and GROUP and sub-word
 *    accesses as bus errors, the window's 0x3600-0x3FFF and every offset from 0x8A0 unmapped.
 * 4. The split's own register header (sw/ext/split-bisect/split_regs.h) agrees with mesh.h 1.1 (addresses and
 *    constants).
 *
 * Run: java -cp build RegDocTest
 */
final class RegDocTest {
    static int pass, fail;
    static void check(boolean ok, String what) {
        if (ok) pass++; else { fail++; System.out.println("FAIL " + what); }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("RegDocTest: mesh.h 1.1");
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
        Map<String, Integer> regs = new LinkedHashMap<>();
        Matcher a = Pattern.compile("#define (\\w+)_ADDR\\s+(0xFFFFF[0-9A-F]{3})").matcher(meshh);
        while (a.find()) regs.put(a.group(1), (int) Long.parseLong(a.group(2).substring(2), 16));
        Map<Integer, String> conds = new TreeMap<>();
        Matcher cm = Pattern.compile("#define (?:TILE_)?COND_(\\w+)\\s+(0x[0-9A-Fa-f]+|\\d+)").matcher(meshh);
        while (cm.find()) if (!cm.group(1).equals("MASK")) conds.put(Integer.numberOfTrailingZeros(Integer.decode(cm.group(2))), cm.group(1));
        List<String> docConds = new ArrayList<>();
        for (int i : RegDoc.COND) docConds.add(Conditions.CNAME[i]);
        { List<String> want = new ArrayList<>(), got = new ArrayList<>(regs.keySet());
          for (Registers.Reg r : Registers.ALL) want.add(r.name);
          check(got.equals(want), "mesh.h 1.1's registers, in the table's order: " + got); }
        for (Map.Entry<String, Integer> e : regs.entrySet()) { Registers.Reg r = Registers.byName(e.getKey()); check(r != null && r.addr() == e.getValue(), "mesh.h: " + e.getKey() + " at " + Integer.toHexString(e.getValue())); }
        { boolean[] seen = new boolean[Registers.N_OFFSETS]; for (int v : regs.values()) seen[((v & 0xFFF) - 0x800) >> 2] = true; boolean full = true; for (boolean b : seen) full &= b;
          check(full, "every offset 0x800-0x89C carries a register: no holes (8.1, part E)"); }
        check(regs.size() == 44 && conds.size() == 15, "mesh.h 1.1: 44 register names at 40 offsets, 15 conditions (got " + regs.size() + ", " + conds.size() + ")");
        check(meshh.contains("#define MESH_H_VERSION 0x0101"), "mesh.h: MESH_H_VERSION 1.1");
        check(!Pattern.compile("#define\\s+(DMA_PULSE\\w*|ROUTER_SET_HI\\w*|ROUTER_SEND_LO\\w*|STREAM_LO\\w*|STREAM_HI\\w*|STREAM|ROUTER_SET_W3\\w*|MAILBOX\\w*|MBOX\\w*|MODE_MBOX|MODE_CMEM|MODE_AUG|MODE_REPLAY|MODE_WAKE|MODE_EVENT|REQUEST_SEND_(?:32|64|128)|REPLAYER_\\w*|DMA_\\w+_ADD|WAKE_TAG|TILE_ST_TILE\\w*|TILE_COND_RDPORT\\w*|AREA_CHUNKS_SHIFT)[\\s(]").matcher(meshh).find(),
                "mesh.h 1.1 defines none of the withdrawn names (v1.0's DMA_PULSE*, ROUTER_SET_HI*, ROUTER_SEND_LO*, STREAM_LO*/_HI*; v1.1's ROUTER_SET_W3, the mailbox, the old mode names, REQUEST_SEND_32/_64/_128, REPLAYER_*, the add aliases, WAKE_TAG, TILE_ST_TILE, TILE_COND_RDPORT_OVF, AREA_CHUNKS_SHIFT)");
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
            if (r.name().equals("REQUEST_SEND") || r.name().equals("REQUEST_RECEIVE")) continue;   // a load there pops (underflow on an empty queue): 2.7
            Integer v = core.load(t, r.sim().get(0).addr(), 4);
            check(v != null, r.name() + ": a load is not a bus error");
            if (v == null) continue;
            if (r.access() == Registers.W) check(v == 0, r.name() + ": write-only, a load returns 0 (got " + v + ")");
            switch (r.name()) {
                case "TILE_STATUS" -> {
                    for (RegDoc.DF f : RegDoc.WORDS.get("TILE_STATUS").fields()) {
                        int got = (v >>> f.lo()) & (f.width() == 32 ? -1 : (1 << f.width()) - 1);
                        if (f.reset().equals("the tile's id")) check(got == 5, "TILE_STATUS.endpoint id at reset = 5 (got " + got + ")");
                        else if (f.reset().startsWith("1 (")) check(got == 1, "TILE_STATUS.type at reset = 1, the replayer (got " + got + ")");
                        else if (f.reset().equals("device")) check(got == 15, "TILE_STATUS.size-1 = 15 at 4 MB (got " + got + ")");
                        else check(got == Integer.parseInt(f.reset()), "TILE_STATUS." + f.label() + " at reset = " + f.reset() + " (got " + got + ")");
                    }
                }
                case "TILE_CONDITIONS", "DMA_SRAM_REMAINING", "DMA_CM_REMAINING", "DMA_REPLY_REMAINING", "DMA_PERIPH_REMAINING", "WAKE_REQUIRED" -> check(v == 0 && d.reset().startsWith("0"), r.name() + " at reset = 0 (got " + v + ")");
                case "WAKE_MASK" -> check(v == Registers.WAKE_MASK_RESET, "WAKE_MASK at reset = [8] | [9] (got " + v + ")");
                case "UART_DIVISOR" -> check(v == Uart.RESET_DIVISOR, "UART_DIVISOR on an unwired tile reads its reset value (got " + v + ")");
                case "UART_RX", "UART_STATUS" -> check(v == 0, r.name() + " on an unwired tile reads 0 (got " + v + ")");
                case "SRAM_RD_ADDR_QUEUE" -> check(v == p.readPort.depth && v >= 64, "SRAM_RD_ADDR_QUEUE at reset = the depth, at least 64 (got " + v + ")");
                default -> { }
            }
        }
        // read-only stores
        for (RegDoc.Reg r : RegDoc.REGS) if (r.access() == Registers.R && !r.name().startsWith("REQUEST_"))   // the request port's load registers share their offsets with stores
            check(core.store(t, r.sim().get(0).addr(), 0, -1) != null, r.name() + ": a store is a bus error");
        // widths: store all ones
        for (RegDoc.Reg r : RegDoc.REGS) if (r.name().endsWith("_REMAINING") || r.name().equals("SRAM_RD_DEST_ADDR") || r.name().startsWith("ROUTER_SET_W"))
            for (Registers.Reg s : r.sim()) { check(core.store(c.cycle + 1, s.addr(), -1, -1) == null, r.name() + ": store"); c.step(); }
        for (Registers.Reg s : RegDoc.reg("DMA_RECORD_BASE_ADDRESS").sim()) { core.store(c.cycle + 1, s.addr(), -1, -1); c.step(); }
        core.store(c.cycle + 1, Registers.HIST_BASE, -1, -1); c.step();
        core.store(c.cycle + 1, Registers.PTR_BASE, -1, -1); c.run(8);
        int cw = (1 << RegDoc.COUNTER_WIDTH) - 1;
        for (RegDoc.Reg r : RegDoc.REGS) if (r.name().endsWith("_REMAINING")) {
            Integer v = core.load(c.cycle + 1, r.sim().get(0).addr(), 4);
            check(v != null && v == cw, r.name() + ": " + RegDoc.COUNTER_WIDTH + " bits (all ones added to 0 reads " + v + ")");
            core.store(c.cycle + 1, r.sim().get(0).addr(), 1, -1); c.step();             // a write adds: + 1 wraps the count to 0 (flagged)
            check(core.load(c.cycle + 1, r.sim().get(0).addr(), 4) == 0, r.name() + ": a write adds (+ 1 wrapped it to 0)");
        }
        int bw = (1 << RegDoc.BASE_WIDTH) - 1;
        check(p.writer.augmentBase == bw && p.gather.base == bw, "DMA_RECORD_BASE_ADDRESS: " + RegDoc.BASE_WIDTH + " bits, used by augment and gather (" + p.writer.augmentBase + ", " + p.gather.base + ")");
        check(p.ctl.rdDest() == (1 << RegDoc.RD_DEST_WIDTH) - 1, "SRAM_RD_DEST_ADDR: " + RegDoc.RD_DEST_WIDTH + " bits (" + p.ctl.rdDest() + ")");
        check(p.ctl.setW[0] == -1 && p.ctl.setW[1] == -1 && p.ctl.setW[2] == -1, "ROUTER_SET_W0-W2: 32 bits each");
        check(p.hist.pipe.ctrlRead(c.cycle + 1, 0) == (1 << RegDoc.HIST_WIDTH) - 1, "HIST entry: " + RegDoc.HIST_WIDTH + " bits");
        check(p.ts.pipe.ctrlRead(c.cycle + 1, 0) == (1 << RegDoc.PTR_WIDTH) - 1, "PTR entry: " + RegDoc.PTR_WIDTH + " bits");
        // ROUTER_SET_HDR's addr after a send, per mode (the modes table) and per send register; W1-W3 cleared by every send
        int hdr = RegDoc.reg("ROUTER_SET_HDR").sim().get(0).addr();
        int[] wr = { RegDoc.reg("ROUTER_SET_W0").sim().get(0).addr(), RegDoc.reg("ROUTER_SET_W1").sim().get(0).addr(), RegDoc.reg("ROUTER_SET_W2").sim().get(0).addr() };
        String[] sends = { "ROUTER_SEND_32", "ROUTER_SEND_64", "ROUTER_SEND_128" };
        Map<Integer, String> step = new LinkedHashMap<>();
        for (String[] m : RegDoc.MODES)
            if (m[0].contains("–")) { String[] ab = m[0].split("–"); for (int k = Integer.parseInt(ab[0]); k <= Integer.parseInt(ab[1]); k++) step.put(k, m[5]); }
            else step.put(Integer.parseInt(m[0]), m[5]);
        for (Map.Entry<Integer, String> e : step.entrySet()) for (int sz = 0; sz < 3; sz++) {
            int mode = e.getKey();
            core.store(c.cycle + 1, hdr, Registers.H_DST.place(1) | Registers.H_MODE.place(mode) | Registers.H_ADDR.place(0x100), -1); c.step();
            for (int w : wr) { core.store(c.cycle + 1, w, 0x1234, -1); c.step(); }
            core.store(c.cycle + 1, RegDoc.reg(sends[sz]).sim().get(0).addr(), 0x5678, -1); c.run(4);
            int inc = Registers.H_ADDR.get(p.ctl.setHdr) - 0x100;
            int want = switch (e.getValue()) { case "+1, +2 or +4 by size" -> 1 << sz; case "+1" -> 1; case "unchanged" -> 0; default -> -1; };
            check(inc == want, "ROUTER_SET_HDR addr after a mode-" + mode + " " + sends[sz] + ": " + e.getValue() + " (got +" + inc + ")");
            check(p.ctl.setW[0] == 0 && p.ctl.setW[1] == 0 && p.ctl.setW[2] == 0, "ROUTER_SET_W0-W2 are 0 after a mode-" + mode + " " + sends[sz]);
            c.run(20);
        }
        // window loads, sub-word accesses, the end of the register block
        int[] tables = { Registers.PTR_BASE, Registers.STREAM_SRC_BASE, Registers.STREAM_DEST_BASE, Registers.STREAM_COUNT_BASE, Registers.GROUP_BASE };
        String[] tnames = { "PTR", "STREAM_SRC", "STREAM_DEST", "STREAM_COUNT", "GROUP" };
        for (int i = 0; i < tables.length; i++) {
            boolean busErr = core.load(c.cycle + 1, tables[i], 4) == null;
            check(busErr, "a load from " + tnames[i] + " is a bus error");
        }
        check(core.load(c.cycle + 1, Registers.TILE_STATUS.addr(), 2) == null, "a halfword load from a register is a bus error");
        check(core.load(c.cycle + 1, Registers.TILE_STATUS.addr(), 1) == null, "a byte load from a register is a bus error");
        check(core.load(c.cycle + 1, Registers.HIST_BASE, 2) == null, "a halfword load from the table window is a bus error");
        check(core.load(c.cycle + 1, Registers.HIST_BASE, 4) != null, "a word load from HIST is not");
        check(core.store(c.cycle + 1, Registers.HIST_BASE, 0, 0xFFFF) != null, "a halfword store to the table window is a bus error");
        // the window's unmapped tail and the replayer's reserved register offsets (4.17)
        for (int wa : new int[] { Registers.TABLES_END, Registers.TABLE_END - 4 })
            check(core.load(c.cycle + 1, wa, 4) == null && core.store(c.cycle + 1, wa, 0, -1) != null, "window 0x" + Integer.toHexString(wa) + ": unmapped, a bus error");
        boolean pastLast = core.load(c.cycle + 1, RegDoc.regEnd(), 4) == null && core.store(c.cycle + 1, RegDoc.regEnd(), 0, -1) != null;
        check(pastLast, "the offset after the last register (0x" + Integer.toHexString(RegDoc.regEnd() & 0xFFF) + ") is unmapped");
        check(RegDoc.regEnd() == (Registers.REG_BASE | Registers.UNMAPPED_FROM), "the registers end at 0x8A0, where the unmapped offsets begin");
        boolean all = true;
        for (int off = Registers.UNMAPPED_FROM; off <= 0xFFC; off += 4) if (Registers.at(0xFFFFF000 | off) != null) all = false;
        check(all, "every offset 0x8A0-0xFFC is unmapped (a bus error)");
        // ---- 4. the split keeps its own register header (it builds on its host ISS too): every address in it, and its
        // constants (the endpoint id's shift, the two modes, the reply block), must be mesh.h's.
        try {
            int n = 0, wrong = 0;
            List<String> lines = Files.readAllLines(Path.of("sw/ext/split-bisect/split_regs.h"));
            boolean claims11 = lines.stream().anyMatch(l -> l.contains("mesh.h 1.1"));
            StringBuilder mis = new StringBuilder();
            for (String line : lines) {
                Matcher mm = Pattern.compile("#define\\s+(\\w+)\\s+SR_REG\\(0x([0-9A-Fa-f]+)u\\)").matcher(line);
                if (!mm.find() || mm.group(1).equals("TRACE_REG")) continue;
                Registers.Reg r = null; for (Registers.Reg x : Registers.ALL) if (x.name.equals(mm.group(1))) r = x;
                boolean ok = r != null && (r.addr() & 0xFFFFFFFFL) == Long.parseLong(mm.group(2), 16);
                if (!ok) { wrong++; mis.append(' ').append(mm.group(1)).append("@0x").append(mm.group(2)); }
                if (claims11) check(ok, "split_regs.h: " + mm.group(1) + " at 0x" + mm.group(2) + " is mesh.h's");
                n++;
            }
            if (claims11) check(wrong == 0, "split_regs.h agrees with mesh.h 1.1");
            String txt = String.join("\n", lines);
            check(txt.contains("TILE_STATUS >> " + Registers.ST_ID.lo + ")"), "split_regs.h: ME() takes the endpoint id at [" + (Registers.ST_ID.lo + 6) + ":" + Registers.ST_ID.lo + "]");
            check(txt.contains("SR_MODE_CM    " + LinkWord.M_CMEM + "u") && txt.contains("SR_MODE_REPLY " + LinkWord.M_REPLY + "u"), "split_regs.h: modes 6 (controller memory) and 8 (reply)");
            check(txt.contains("0x" + Integer.toHexString(Registers.REPLY_BASE).toUpperCase() + "u"), "split_regs.h: the reply block at REPLY_BASE");
            check(txt.contains("(void)WAKE_EVENTS"), "split_regs.h: its wfi() loads WAKE_EVENTS (consumes counters done)");
            check(claims11, "split_regs.h says it is at mesh.h 1.1's addresses" + (claims11 ? "" : " (" + wrong + " of " + n + " differ:" + mis + ")"));
        } catch (java.io.IOException e) { check(false, "split_regs.h readable: " + e); }

        System.out.println("RegDocTest: " + pass + " passed, " + fail + " failed");
        System.exit(fail == 0 ? 0 : 1);
    }
}
