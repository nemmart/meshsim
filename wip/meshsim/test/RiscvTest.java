import java.nio.file.*;
import java.util.*;

/**
 * The RISC-V suite on the controller (v1.2): riscv-tests rv32ui and rv32um (test/riscv/build.sh; code at 0x01000000,
 * data at 0x00000010) must pass, ending with a store of 1 to the exit word (8.15); div/divu/rem/remu must halt as
 * illegal instructions with ERRORS [10]. Each program runs on tile 0 of a specified machine (Config.machine()), started
 * by a HALT -> RUN (PHASE-2 INTERIM): the loader word at LOADER_BASE jumps to the program (the other tiles' loaders
 * WFI forever). WFI itself is tested in CtrlTest. The binaries come from riscv-tests 3df703c.
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
            Chip c = new Chip(Config.machine());
            Controller core = c.tiles[0].core;
            int[] code = Mach.words(f), data = Mach.words(dir.resolve(name + ".data.bin"));
            for (int i = 0; i < code.length; i++) core.pokeLocal(Registers.PMEM_BASE + 4 * i, code[i]);
            for (int i = 0; i < data.length; i++) core.pokeLocal(Registers.DMEM_FIRST + 4 * i, data[i]);
            core.pokeLocal(Registers.LOADER_BASE, jal(0, Registers.PMEM_BASE - Registers.LOADER_BASE));
            for (int t = 1; t < 64; t++) { c.tiles[t].core.pokeLocal(Registers.LOADER_BASE, Controller.WFI); c.tiles[t].core.pokeLocal(Registers.LOADER_BASE + 4, jal(0, -4)); }
            c.cc.then((cc, t) -> { cc.storeRun(t, true); return true; });
            boolean halted = c.runUntil(() -> c.cycle > 5 && core.halt != Controller.RUN, 200_000);
            boolean divTest = name.startsWith("div") || name.startsWith("rem");
            int st = c.tiles[0].ctl.conditions;
            c.run(2); st = c.tiles[0].ctl.conditions;
            boolean ok = divTest ? halted && core.halt == Controller.HALT_ILLEGAL && (st >> Conditions.ILLEGAL & 1) == 1
                                 : halted && core.halt == Controller.HALT_EXIT && c.tiles[0].ctl.simExitValue == 1;
            T.check(ok, name + ": " + (halted ? core.haltReport() + " a0=" + core.x[10] + " testnum=" + core.x[11] : "did not halt"));
            T.check(c.violations == 0, name + ": no violations " + c.violationReport());
            instr += core.instructions;
        }
        System.out.println(files.size() + " programs run, " + instr + " instructions");
        System.exit(T.done("RiscvTest"));
    }
}
