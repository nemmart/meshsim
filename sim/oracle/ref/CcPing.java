import java.nio.file.Path;

/**
 * ref's side of test_cc_units (WORKER16): C&C 1 as a core (Config.ccCores, Mach.coresCfg) booted through the host port
 * with tests/m3-images/cc_ping (Mach.hostBoot: its program, its data, CMD_JUMP to _start), run until it exits.
 * Prints SIM_EXIT's cycle and value, cc_ping's stamp[0..5] (mcycle at each send and when STATUS's send-busy cleared),
 * and the words its sends wrote (tile 5, C&C 1 itself, every tile's broadcast word, C&C 1's own broadcast word).
 * Compiled into ref's default package with ref's test classes (Mach); D5: nothing in sim/ref changes.
 *
 *   java -cp REF_CLASSES:. CcPing IMAGE_DIR
 */
final class CcPing {
    public static void main(String[] a) {
        Mach m = new Mach(Mach.coresCfg(-1));
        Mach.Image im = new Mach.Image(Path.of(a[0]), "cc_ping");
        m.hostBoot(im, im.data);
        Tile cc1 = m.c.cores.unit(0);
        if (!m.c.runUntil(() -> cc1.ctl.simExitAt >= 0, 100000)) throw new IllegalStateException("C&C 1 did not exit");
        StringBuilder sb = new StringBuilder();
        sb.append("exit ").append(cc1.ctl.simExitAt).append(' ').append(Integer.toHexString(cc1.ctl.simExitValue)).append('\n');
        int st = im.sym("stamp");
        sb.append("stamp");
        for (int i = 0; i < 6; i++) sb.append(' ').append(Integer.toUnsignedString(cc1.core.peekLocal(st + 4 * i)));
        sb.append('\n');
        int bmin = -1, bmax = 0;
        for (int k = 0; k < 64; k++) { int w = m.c.tiles[k].core.peekLocal(0x6004); if (k == 0 || Integer.compareUnsigned(w, bmin) < 0) bmin = w; if (Integer.compareUnsigned(w, bmax) > 0) bmax = w; }
        sb.append("words ").append(Integer.toHexString(m.c.tiles[5].core.peekLocal(0x6000))).append(' ')
          .append(Integer.toHexString(cc1.core.peekLocal(0x6000))).append(' ').append(Integer.toHexString(bmin)).append(' ')
          .append(Integer.toHexString(bmax)).append(' ').append(Integer.toHexString(cc1.core.peekLocal(0x6004))).append('\n');
        System.out.print(sb);
    }
}
