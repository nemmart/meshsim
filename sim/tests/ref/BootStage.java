import java.nio.file.Path;
import java.util.ArrayDeque;

/**
 * ref's side of t2_harness's boot (WORKER16): Mach.bootCores(replay_ctl, tracer_ctl) on ref's chip with C&C as cores,
 * between cycles before the boot, then what it left: the host queue of C&C 1 (count and an FNV-1a hash over every
 * packet's lo, header, hi) and C&C 1's big SRAM (count and hash over every non-zero (chunk, value)). The C++ harness's
 * boot_cores must leave the same. D5: nothing in sim/ref changes.
 *
 *   java -cp REF_CLASSES:. BootStage IMAGE_DIR      (tests/m3-images: cc1, cc23, replay_ctl, tracer_ctl), run in a
 *                                                   directory whose sw/build holds cc1 and cc23 (bootstage.sh makes it)
 */
final class BootStage {
    static long h = 0xcbf29ce484222325L;
    static void f(long v) { for (int i = 0; i < 8; i++) { h ^= (v >>> (8 * i)) & 0xFF; h *= 0x100000001b3L; } }
    @SuppressWarnings("unchecked")
    public static void main(String[] a) throws Exception {
        Path d = Path.of(a[0]);
        Mach m = new Mach(Mach.coresCfg(-1));
        Mach.Image tile = new Mach.Image(d, "replay_ctl"), cc3 = new Mach.Image(d, "tracer_ctl");
        m.bootCores(tile, cc3);                     // cc1 and cc23 from Mach.SW (sw/build): run where it holds them
        ArrayDeque<long[]>[] q = (ArrayDeque<long[]>[]) StateFp.o(m.c.cores, "hostQ");
        h = 0xcbf29ce484222325L; for (long[] p : q[0]) { f(p[0]); f(p[1]); f(p[2]); }
        System.out.printf("hostq %d %016x%n", q[0].size(), h);
        int[][] data = (int[][]) StateFp.o(m.c.cores.unit(0).sram, "data");
        h = 0xcbf29ce484222325L; long n = 0;
        for (int ar = 0; ar < data.length; ar++) if (data[ar] != null) for (int k = 0; k < data[ar].length; k++) if (data[ar][k] != 0) { f((ar << 17) | k); f(data[ar][k] & 0xFFFFFFFFL); n++; }
        System.out.printf("sram %d %016x%n", n, h);
    }
}
