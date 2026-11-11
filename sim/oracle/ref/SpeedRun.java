import java.nio.file.Files;
import java.nio.file.Path;

/**
 * WORKER5: ref's speed on one program, booted exactly as TraceRun boots it (sim/oracle/ref/TraceRun.java: the image poked
 * into the tile's program memory, C&C 1 expecting that tile alone, the boot command with entry 0x01000000, then GO; the
 * boot ROM jumps), with the observer OFF (run without -Dmeshsim.observe). For the framework-against-ref comparison of
 * sim/tests/bench_controller.cpp's loop. Compiled into sim/ref's default package (D5), run from sim/ref/meshsim:
 *
 *   java -cp REF_BUILD:THIS_BUILD SpeedRun TILE CODE_BIN REPS [MAX_CYCLES]
 *
 * Each repetition builds a fresh Chip and runs it until the core halts (or parks), timing Chip.runUntil alone. Prints the
 * instructions retired, the cycles, the halt reason, data memory's word 0x100 (the loop's count), and the rates.
 * -Dmeshsim.skip=false runs it with idle-skip off (every unit of all 64 tiles stepped every cycle).
 */
public final class SpeedRun {
    public static void main(String[] a) throws Exception {
        if (a.length < 3) { System.err.println("usage: SpeedRun TILE CODE_BIN REPS [MAX_CYCLES]"); System.exit(2); }
        if (Observer.ON) { System.err.println("SpeedRun: run without -Dmeshsim.observe (the observer must be off)"); System.exit(2); }
        int tile = Integer.parseInt(a[0]);
        int[] code = Mach.words(Path.of(a[1]));
        int reps = Integer.parseInt(a[2]);
        long max = a.length > 3 ? Long.parseLong(a[3]) : 2_000_000_000L;
        for (int r = 0; r < reps; r++) {
            Config k = Config.machine();
            k.checkErrors = false;
            Chip c = new Chip(k);
            Controller core = c.tiles[tile].core;
            for (int i = 0; i < code.length; i++) core.pokeLocal(Registers.PMEM_BASE + 4 * i, code[i]);
            c.cc1p.expect = 1L << tile;
            c.cc.command(tile, Registers.CMD_JUMP << 24, new int[] { Registers.PMEM_BASE }).go();
            long t0 = System.nanoTime();
            boolean stopped = c.runUntil(() -> core.halt != Controller.RUN || core.parked, max);
            double s = (System.nanoTime() - t0) / 1e9;
            System.out.printf("ref%s rep %d: %d instructions, %d cycles in %.3f s: %.2f M instructions/s, %.2f M cycles/s, "
                    + "%.1f ns per instruction; halt %d, count %d%s%n", c.skip ? "" : " (idle-skip off)", r + 1,
                    core.instructions, c.cycle, s, core.instructions / s / 1e6, c.cycle / s / 1e6, s / core.instructions * 1e9,
                    core.halt, core.peekLocal(0x100), stopped ? "" : " (DID NOT STOP)");
        }
    }
}
