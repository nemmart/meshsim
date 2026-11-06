import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * The replayer (spec 4.19), from Java: the ALU against the tracer's test vectors, and the unit's parts in isolation.
 * Usage: java ReplayerTest [path to tools/replayer-opcodes/vec.tsv]
 */
public final class ReplayerTest {
    public static void main(String[] args) throws Exception {
        // v1.25: WORKER9's regenerated vectors at jolt-cpp 821e1bd (wip/tools/replayer-opcodes/out-821e1bd/vec.tsv, copied)
        String vec = args.length > 0 ? args[0] : "data/vec-821e1bd.tsv";
        String opt = args.length > 1 ? args[1] : "../tables/opcode_table.txt";
        opcodeTable(Path.of(opt));
        vectors(Path.of(vec));
        ReplayerUnitTests.all();
        System.exit(T.done("ReplayerTest"));
    }

    // ================================================================ the ALU against vec.tsv
    static final long PC = 0x8000_0100L;
    static int vecPass, vecNA, vecFlagged;

    /** ReplayerAlu's opcode set against the tables' opcode_table.txt (jolt-cpp 821e1bd): every name, number and class. */
    static void opcodeTable(Path p) throws Exception {
        T.group("opcode table (" + p + ")");
        int rows = 0, executed = 0;
        for (String line : Files.readAllLines(p)) {
            if (line.isBlank()) continue;
            String[] f = line.trim().split("\\s+");
            int code = Integer.parseInt(f[0]); rows++;
            if (f[2].equals("-")) { T.check(!ReplayerAlu.executes(code), f[1] + " (" + code + ") is not executed by the replayer"); continue; }
            executed++;
            T.check(f[1].equals(ReplayerAlu.NAME[code]), "opcode " + code + ": table " + f[1] + ", model " + ReplayerAlu.NAME[code]);
            T.check(ReplayerAlu.CLASS[code] == f[2].charAt(0), f[1] + ": class " + f[2] + ", model " + ReplayerAlu.CLASS[code]);
        }
        int model = 0; for (int i = 0; i < 256; i++) if (ReplayerAlu.executes(i)) model++;
        T.eq(executed, model, "the model executes exactly the table's row opcodes");
        System.out.println("opcode table: " + rows + " opcodes, " + executed + " row opcodes, all matching the model");
    }

    /** vec.cpp (821e1bd) sets ShiftData's shift_offset per vector without printing it: its S(...) calls, in order. */
    static final long[] SHIFT_OFFSETS = { 0, 7, -2, 0, 2, -4, 0, 4, -4 };
    static int shiftK;

    /**
     * Each row of vec.tsv is one call of the tracer's execute_instruction (vec.cpp): registers rs1 = x6, rs2 = x7,
     * rd = x5 initialised to 0xdead, pc 0x8000_0100, pc_increment 4, unless the row's form or note says otherwise.
     * The settings vec.tsv doesn't print (pc_increment 2 or 0, rd's register, the log word, HostIO's rd) are taken
     * from vec.cpp by note, below.
     */
    static void vectors(Path p) throws Exception {
        T.group("ALU vectors (vec.tsv)");
        List<String> lines = Files.readAllLines(p);
        List<String> report = new ArrayList<>();
        ReplayerAlu.Out o = new ReplayerAlu.Out();
        boolean[] covered = new boolean[256];
        for (String line : lines) {
            if (line.isBlank()) continue;
            String[] f = line.split("\t", -1);
            String name = f[0], note = f.length > 4 ? f[4] : "", rdImmS = f.length > 5 ? f[5] : "";
            String[] res = f[3].split("\\|");
            int op = ReplayerAlu.code(name);
            String what = name + " " + f[1] + " " + f[2] + (note.isEmpty() ? "" : " (" + note + ")");
            if (res[0].equals("ABORT")) { vecNA++; report.add("n/a  " + what + ": the tracer aborts (a failing assertion never reaches a replayer)"); continue; }
            if (!ReplayerAlu.executes(op)) { vecNA++; report.add("n/a  " + what + ": not a replayer opcode (advice is written as VirtualAdvice and taken from the log, 4.19.7)"); continue; }
            boolean s1imm = f[1].startsWith("imm "), s2imm = f[2].startsWith("imm ");
            long a = Long.parseUnsignedLong(f[1].substring(f[1].indexOf("0x") + 2), 16);
            long b = Long.parseUnsignedLong(f[2].substring(f[2].indexOf("0x") + 2), 16);
            boolean rdImm = !rdImmS.isEmpty();
            long rdImmV = rdImm ? Long.parseUnsignedLong(rdImmS.substring(rdImmS.indexOf("0x") + 2), 16) : 0;
            // vec.cpp's per-row settings
            int inc = note.contains("not the last row") ? 0 : note.contains("compressed") ? 2 : 4;
            int rdReg = note.contains("rd = x0") ? 0 : note.contains("rd = rs1") ? 6 : note.contains("EBREAK") ? 40 : 5;
            long rdInit = name.equals("VirtualHostIO") ? 0x1234 : name.equals("JAL") && rdReg == 40 ? 0 : 0xdeadL;
            int opc = ReplayerAlu.code(name);
            long log = ReplayerAlu.isLoad(opc) ? (note.contains("rd = x0") ? 0x99 : 0x1122334455667788L) : ReplayerAlu.isStore(opc) ? 0x99
                    : name.equals("VirtualAdvice") ? 0x42 : 0;
            long so = ReplayerAlu.isShiftData(opc) ? SHIFT_OFFSETS[shiftK++] : 0;
            // slot values as read_reg_vals gives them (x0 reads 0; rd = rs1 aliases x6)
            long c = rdImm ? rdImmV : rdReg == 0 ? 0 : rdReg == 6 ? a : rdInit;
            ReplayerAlu.exec(op, a, b, c, PC, inc, true, log, so, o);
            boolean writes = !rdImm;
            long rdAfter = !writes ? 0 : rdReg == 0 ? 0 : o.rd;
            long finalPc; String next;
            if (o.newPc == 0) { finalPc = 0; next = "end: pc 0, instr_idx 0"; }
            else if (o.newPc != PC) { finalPc = o.newPc; next = "transfer"; }
            else { finalPc = PC + inc; next = "sequential"; }
            int logs = ReplayerAlu.CLASS[op] == 'L' ? 1 : 0;
            String got = (rdImm ? "—" : hx(rdAfter)) + "|" + hx(finalPc) + "|" + next + "|" + logs;
            if (o.undefined != null) {
                vecFlagged++;
                boolean expectFlag = note.contains("UB");         // v1.25: PEXT is defined for every mask, mask 0 included
                T.check(expectFlag, what + ": flagged undefined (" + o.undefined + ") but the vector is defined");
                report.add("flag " + what + ": undefined for the replayer (" + o.undefined + "), flagged; tracer " + f[3] + ", model " + got);
                continue;
            }
            T.check(!note.contains("UB"), what + ": an undefined input was not flagged");
            boolean ok = got.equals(f[3]);
            T.check(ok, what + ": tracer " + f[3] + ", model " + got);
            if (ok) { vecPass++; covered[op] = true; }
        }
        StringBuilder missing = new StringBuilder();
        for (int op = 1; op < 256; op++) if (ReplayerAlu.executes(op) && !covered[op])   // NOP (0): the padding row, in the unit tests missing.append(' ').append(ReplayerAlu.name(op));
        T.check(missing.length() == 0, "every row opcode the replayer executes has a matching vector; missing:" + missing);
        System.out.println("ALU vectors: " + lines.size() + " rows; " + vecPass + " match the tracer; " + vecFlagged
                + " undefined for the replayer and flagged (invariant 16); " + vecNA + " not applicable");
        for (String r : report) System.out.println("  " + r);
    }
    static String hx(long v) { return String.format("0x%016x", v); }
}
