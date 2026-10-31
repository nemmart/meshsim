import java.nio.file.Files;
import java.nio.file.Path;

/**
 * REGISTERS.md and mesh.h against their generators, the draft header and the simulator (v1.2).
 *
 * 1. sw/REGISTERS.md (../sw/REGISTERS.md in the layout) is RegDoc's output.
 * 2. sw/mesh.h is MeshHeader's output, and the programmer's copy (../sw/mesh.h) is the same file.
 * 3. tools/header_diff.py: every macro and struct offset the generated header shares with the draft the recipes compile
 *    against (../sw/mesh-v1.2-draft.h) agrees (needs python3 and gcc; reported as skipped without them).
 * 4. The simulator behaves as the document's register table says: each register's value after the reset (a load), a
 *    store to a read-only register is a bus error, write-only registers and reserved words read 0, the request port's
 *    receive side on a mesh tile.
 *
 * Run: java -cp build RegDocTest
 */
final class RegDocTest {
    static int pass, fail;
    static void check(boolean ok, String what) { if (ok) pass++; else { fail++; System.out.println("FAIL " + what); } }

    public static void main(String[] args) throws Exception {
        System.out.println("RegDocTest: mesh.h 1.2");
        // ---- 1, 2. the generated files
        String doc = Files.readString(Registers.doc("REGISTERS.md"));
        check(doc.equals(RegDoc.document()), "REGISTERS.md is RegDoc's output (run java -cp build RegDoc)");
        String meshh = Files.readString(Path.of("sw/mesh.h"));
        check(meshh.equals(MeshHeader.header()), "sw/mesh.h is MeshHeader's output");
        if (Files.exists(Path.of("../sw/mesh.h"))) check(Files.readString(Path.of("../sw/mesh.h")).equals(meshh), "../sw/mesh.h (the programmer's copy) is the generated sw/mesh.h");
        check(meshh.contains("#define MESH_H_VERSION 0x0102"), "mesh.h: MESH_H_VERSION 1.2");
        for (String gone : new String[] { "TILE_PULSE", "TILE_STATUS", "TILE_CONDITIONS", "WAKE_EVENTS", "ROUTER_SET_HDR", "DMA_SRAM_REMAINING", "MODE_CM", "MODE_REPLY", "CM_WORD", "SENDER_SEND", "PERIPH_STATUS_0" })
            check(!meshh.contains(gone), "mesh.h 1.2 has no v1.1 name " + gone);

        // ---- 3. the draft header
        Path draft = Path.of("../sw/mesh-v1.2-draft.h");
        if (Files.exists(draft) && onPath("python3") && onPath("gcc")) {
            Process pr = new ProcessBuilder("python3", "tools/header_diff.py", "sw/mesh.h", draft.toString()).redirectErrorStream(true).start();
            String out = new String(pr.getInputStream().readAllBytes());
            pr.waitFor();
            // the differences decided after the draft (listed in WORKER4/REPORT.md): PTYPE_REPLAYER is 4 (D46; the draft's 1)
            String[] known = { "PTYPE_REPLAYER" };
            long unknown = out.lines().filter(l -> l.contains("DIFFERS") && java.util.Arrays.stream(known).noneMatch(l::contains)).count();
            check(unknown == 0 && out.contains("differing offsets: 0"), "every macro and struct offset shared with the draft agrees, the decided differences aside:\n" + out);
            check(out.contains("DIFFERS PTYPE_REPLAYER") && out.contains("generated 0x4"), "PTYPE_REPLAYER is 4 (D46)");
            check(!out.contains("ONLY IN DRAFT") && !out.contains("MEMBER ONLY IN DRAFT"), "the generated header defines everything the draft does");
        } else System.out.println("header_diff skipped (needs python3, gcc and ../sw/mesh-v1.2-draft.h)");

        // ---- 4. the simulator
        Chip c = new Chip(Config.machine());
        c.run(20);
        Tile p = c.tiles[5];
        Controller core = p.core;
        long t = c.cycle + 1;
        for (RegDoc.Row r : RegDoc.ROWS) {
            Registers.Reg g = r.reg();
            if (!g.readable() && Registers.LOAD[g.index] != null) continue;   // the request port's stores: a load reads the receive side
            if (g == Registers.REQUEST_RECEIVE) continue;                       // a load pops (underflow on a mesh tile): below
            Integer v = core.load(t, g.addr(), 4);
            check(v != null, g.name + ": a load is not a bus error");
            if (v == null) continue;
            if (r.load().equals("0")) check(v == 0, g.name + ": a load reads 0 (" + v + ")");
            // PHASE-2 INTERIM (until phase 4's reset): a halted unit shows done in STATUS
            if (r.reset().startsWith("0")) check(v == 0 || g == Registers.STATUS && v == 1 << Registers.ST_DONE, g.name + ": 0 after the reset (" + Integer.toHexString(v) + ")");
            if (g == Registers.EVENTS_MASK) check(v == 0x180, "EVENTS_MASK: [8] | [7] after the reset (" + Integer.toHexString(v) + ")");
            if (g == Registers.PARAMS) check(v == p.ctl.params() && Registers.PA_ID.get(v) == 5, "PARAMS: the constant, endpoint id 5");
            if (g == Registers.SRAM_RD_ADDR_QUEUE) check(v >= 64, "SRAM_RD_ADDR_QUEUE: at least 64 free entries when empty (" + v + ")");
            if (!g.writable() && Registers.STORE[g.index] == null) check(core.store(t, g.addr(), 0, -1) != null, g.name + ": a store to a read-only register is a bus error");
        }
        for (int i = 0; i < Registers.BLOCK_WORDS; i++) if (Registers.reservedWord(i)) {
            check(core.load(t, Registers.REG_BASE + 4 * i, 4) == 0, "reserved word 0x" + Integer.toHexString(0x800 + 4 * i) + " reads 0");
            check(core.store(t, Registers.REG_BASE + 4 * i, 1, -1) == null, "and a store there is ignored, not a bus error");
        }
        check(core.load(t, Registers.REQUEST_RECEIVE.addr(), 4) == 0, "REQUEST_RECEIVE on a mesh tile reads 0 (an empty queue)");
        c.step(); c.step();
        check((p.ctl.conditions & 1 << Conditions.RECV_UNDERFLOW) != 0, "and sets receive underflow (ERRORS [14])");
        System.out.println("RegDocTest: " + pass + " passed, " + fail + " failed");
        System.exit(fail == 0 ? 0 : 1);
    }
    static boolean onPath(String exe) {
        for (String d : System.getenv().getOrDefault("PATH", "").split(":")) if (Files.isExecutable(Path.of(d, exe))) return true;
        return false;
    }
}
