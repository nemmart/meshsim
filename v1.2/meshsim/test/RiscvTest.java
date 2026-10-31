import java.nio.file.*;
import java.util.*;

/**
 * The RISC-V suite on the controller (v1.2): riscv-tests rv32ui and rv32um (test/riscv/build.sh; code at 0x01000000,
 * data at 0x00000010) must pass, ending with a store of 1 to the exit word (8.15); div/divu/rem/remu must halt as
 * illegal instructions: ERRORS [10], a trap to the boot ROM's handler, which reports and parks. Each program runs on
 * tile 0 of a specified machine (Config.machine()), booted through the boot ROM (6.2): its command lands, GO, the ROM
 * jumps (the other tiles wait in their ROMs). WFI itself is tested in CtrlTest. The binaries come from riscv-tests 3df703c.
 */
public final class RiscvTest {
    static int jal(int rd, int off) {
        return ((off >> 20) & 1) << 31 | ((off >> 1) & 0x3FF) << 21 | ((off >> 11) & 1) << 20 | ((off >> 12) & 0xFF) << 12 | rd << 7 | 0x6F;
    }
    public static void main(String[] args) throws Exception {
        Path dir = Path.of(args.length > 0 ? args[0] : "test/riscv/bin");
        List<Path> files = new ArrayList<>();
        try (DirectoryStream<Path> ds = Files.newDirectoryStream(dir, "*.code.bin")) { ds.forEach(files::add); }
        Collections.sort(files);
        long instr = 0;
        for (Path f : files) {
            String name = f.getFileName().toString().replace(".code.bin", "");
            Config k = Config.machine(); k.checkErrors = !(name.startsWith("div") || name.startsWith("rem"));   // div's ERRORS [10] is the point
            Chip c = new Chip(k);
            Controller core = c.tiles[0].core;
            int[] code = Mach.words(f), data = Mach.words(dir.resolve(name + ".data.bin"));
            for (int i = 0; i < code.length; i++) core.pokeLocal(Registers.PMEM_BASE + 4 * i, code[i]);
            for (int i = 0; i < data.length; i++) core.pokeLocal(Registers.DMEM_FIRST + 4 * i, data[i]);
            c.cc1p.expect = 1;                                    // only tile 0 starts (6.2): its command, then GO
            c.cc.command(0, Registers.CMD_JUMP << 24, Registers.PMEM_BASE).go();
            boolean halted = c.runUntil(() -> core.halt != Controller.RUN || core.parked, 200_000);
            boolean divTest = name.startsWith("div") || name.startsWith("rem");
            c.run(2); int st = c.tiles[0].ctl.conditions;
            boolean ok = divTest ? halted && core.trapCause == Controller.CAUSE_ILLEGAL && core.parked && (st >> Conditions.ILLEGAL & 1) == 1
                                 : halted && core.halt == Controller.HALT_EXIT && c.tiles[0].ctl.simExitValue == 1;
            T.check(ok, name + ": " + (halted ? core.haltReport() + " a0=" + core.x[10] + " testnum=" + core.x[11] : "did not halt"));
            T.check(c.violations == 0, name + ": no violations " + c.violationReport());
            instr += core.instructions;
        }
        System.out.println(files.size() + " programs run, " + instr + " instructions");
        System.exit(T.done("RiscvTest"));
    }
}
