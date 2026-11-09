import java.nio.file.Path;

/**
 * ref's side of test_cc_req (WORKER14): the request bus end to end. C&C 1 as a core (Mach.coresCfg) booted through the
 * host port with tests/m3-images/cc_req; tile 5's program memory holding tile_req (put there directly: C&C 1's
 * CMD_JUMP starts it). Runs until C&C 1 exits; prints SIM_EXIT's cycle and value, tile 5's send stamps, C&C 1's received
 * words and stamps, its last REQUEST_RECEIVE (on an empty queue) and ERRORS, and the bus's beats at the root.
 * Compiled into ref's default package with ref's test classes (Mach); D5: nothing in sim/ref changes.
 *
 *   java -cp REF_CLASSES:. CcReq IMAGE_DIR
 */
final class CcReq {
    static final int N = 300;
    public static void main(String[] a) {
        Mach m = new Mach(Mach.coresCfg(-1));
        Mach.Image cc = new Mach.Image(Path.of(a[0]), "cc_req"), tl = new Mach.Image(Path.of(a[0]), "tile_req");
        for (int i = 0; i < tl.code.length; i++) m.c.tiles[5].core.pokeLocal(Registers.PMEM_BASE + 4 * i, tl.code[i]);
        m.hostBoot(cc, cc.data);
        Tile cc1 = m.c.cores.unit(0);
        if (!m.c.runUntil(() -> cc1.ctl.simExitAt >= 0, 400000))   // the state, to localise a run that never ends
            throw new IllegalStateException("C&C 1 did not exit: tile 5 pc " + Integer.toHexString(m.c.tiles[5].core.pc) + ", C&C 1 pc " + Integer.toHexString(cc1.core.pc)
                + ", beats at the root " + m.c.reqBus.beatsAtRoot + ", tile 5 FIFO " + m.c.tiles[5].req.fifo.size() + ", tile 5 stamp[0] " + m.c.tiles[5].core.peekLocal(tl.sym("stamp")));
        StringBuilder sb = new StringBuilder();
        sb.append("exit ").append(cc1.ctl.simExitAt).append(' ').append(Integer.toHexString(cc1.ctl.simExitValue)).append('\n');
        int ts = tl.sym("stamp"), cs = cc.sym("stamp"), cw = cc.sym("words"), ce = cc.sym("extra");
        for (int i = 0; i < N; i++) {
            sb.append("r ").append(i).append(' ').append(Integer.toUnsignedString(m.c.tiles[5].core.peekLocal(ts + 4 * i)));
            for (int k = 0; k < 4; k++) sb.append(' ').append(Integer.toHexString(cc1.core.peekLocal(cw + 16 * i + 4 * k)));
            sb.append(' ').append(Integer.toUnsignedString(cc1.core.peekLocal(cs + 4 * i))).append('\n');
        }
        sb.append("extra ").append(Integer.toHexString(cc1.core.peekLocal(ce))).append(' ').append(Integer.toHexString(cc1.core.peekLocal(ce + 4))).append('\n');
        sb.append("beats ").append(m.c.reqBus.beatsAtRoot).append('\n');
        System.out.print(sb);
    }
}
