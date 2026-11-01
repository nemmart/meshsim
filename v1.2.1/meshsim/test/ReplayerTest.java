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
        String vec = args.length > 0 ? args[0] : "../tools/replayer-opcodes/vec.tsv";
        vectors(Path.of(vec));
        ReplayerUnitTests.all();
        System.exit(T.done("ReplayerTest"));
    }

    // ================================================================ the ALU against vec.tsv
    static final long PC = 0x8000_0100L;
    static int vecPass, vecNA, vecFlagged;

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
            long log = name.equals("LD") ? 0x1122334455667788L : name.equals("SD") ? 0x99 : name.equals("VirtualAdvice") ? 0x42 : 0;
            // slot values as read_reg_vals gives them (x0 reads 0; rd = rs1 aliases x6)
            long c = rdImm ? rdImmV : rdReg == 0 ? 0 : rdReg == 6 ? a : rdInit;
            ReplayerAlu.exec(op, a, b, c, PC, inc, true, log, o);
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
                boolean expectFlag = note.contains("UB") || note.contains("mask 0") || note.contains("non-contiguous");
                T.check(expectFlag, what + ": flagged undefined (" + o.undefined + ") but the vector is defined");
                report.add("flag " + what + ": undefined for the replayer (" + o.undefined + "), flagged; tracer " + f[3] + ", model " + got);
                continue;
            }
            T.check(!(note.contains("UB") || note.contains("mask 0") || note.contains("non-contiguous")), what + ": an undefined input was not flagged");
            boolean ok = got.equals(f[3]);
            T.check(ok, what + ": tracer " + f[3] + ", model " + got);
            if (ok) vecPass++;
        }
        System.out.println("ALU vectors: " + lines.size() + " rows; " + vecPass + " match the tracer; " + vecFlagged
                + " undefined for the replayer and flagged (invariant 16); " + vecNA + " not applicable");
        for (String r : report) System.out.println("  " + r);
    }
    static String hx(long v) { return String.format("0x%016x", v); }
}
