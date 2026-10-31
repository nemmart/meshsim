import java.util.ArrayList;
import java.util.List;

/**
 * WORKER14: C&C 1-3 as cores (Config.ccCores). Usage: java CcCoresTest [shuffleSeed]
 *  - the switch off: the Java model, no controller tiles;
 *  - the boot ROM's branch for 64-66 (6.2 step 4): each controller waits in its ROM; the harness lands a program and its
 *    first command through the host port (W14-02); the ROM jumps; the program reads PARAMS' id and exits with it;
 *  - the link (W14-01): C&C 1's send port writes into a tile's data memory through tile 0's fifth port;
 *  - the request tree: C&C 2's request reaches target 0 stamped with requestor 65; C&C 1's request port reads it.
 */
final class CcCoresTest {
    static long shuffle = -1;

    public static void main(String[] a) {
        if (a.length > 0) shuffle = Long.parseLong(a[0]);
        switchOff();
        romBranch();
        System.exit(T.done("CcCoresTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    static Config cfg(boolean cores) { Config k = Config.machine(); k.shuffleSeed = shuffle; k.ccCores = cores; return k; }

    // ---------------------------------------------------------------- a tiny assembler (x0-relative register accesses)
    static int R(int off) { return off - 0x800; }                  // X0REL
    static int lw(int rd, int rs1, int imm) { return (imm & 0xFFF) << 20 | rs1 << 15 | 2 << 12 | rd << 7 | 0x03; }
    static int sw(int rs2, int rs1, int imm) { return ((imm >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | 2 << 12 | (imm & 0x1F) << 7 | 0x23; }
    static int addi(int rd, int rs1, int imm) { return (imm & 0xFFF) << 20 | rs1 << 15 | rd << 7 | 0x13; }
    static int andi(int rd, int rs1, int imm) { return (imm & 0xFFF) << 20 | rs1 << 15 | 7 << 12 | rd << 7 | 0x13; }
    static int srli(int rd, int rs1, int sh) { return sh << 20 | rs1 << 15 | 5 << 12 | rd << 7 | 0x13; }
    static int lui(int rd, int imm20) { return imm20 << 12 | rd << 7 | 0x37; }
    static int beq(int rs1, int rs2, int off) {
        return ((off >> 12) & 1) << 31 | ((off >> 5) & 0x3F) << 25 | rs2 << 20 | rs1 << 15 | ((off >> 1) & 0xF) << 8 | ((off >> 11) & 1) << 7 | 0x63;
    }
    static final int J_SELF = 0x0000006F, A0 = 10, A1 = 11, T0 = 5, T1 = 6, T2 = 7;
    static final int PARAMS = 0x000, EVENTS_PEEK = 0x01C, ROUTER_HDR = 0x028, ROUTER_ADDR = 0x02C, ROUTER_SEND_32 = 0x03C,
            REQ_W0 = 0x048, REQ_W1 = 0x04C, REQ_W2 = 0x050, REQ_SEND = 0x054, REQ_GET_W0 = 0x048, REQ_RECEIVE = 0x054, SIM_EXIT = 0x1F8;

    /** The program for controller cc: a0 = PARAMS' id; C&C 1 sends 0x5A5 to tile 5's data memory at 0x100 and then waits
     *  for a request and exits with its W3 + 0x1000 * its requestor; C&C 2 sends C&C 1 a "started" request carrying its id;
     *  every other exits with its id. */
    static int[] program(int cc) {
        List<Integer> p = new ArrayList<>();
        p.add(lw(A0, 0, R(PARAMS))); p.add(srli(A0, A0, 20)); p.add(andi(A0, A0, 0x7F));
        if (cc == 0) {
            p.add(lui(T0, 5 << 13));                               // ROUTER_HDR: dst 5 [31:25], mode 0, uncounted
            p.add(sw(T0, 0, R(ROUTER_HDR)));
            p.add(addi(T1, 0, 0x100)); p.add(sw(T1, 0, R(ROUTER_ADDR)));
            p.add(addi(T2, 0, 0x5A5)); p.add(sw(T2, 0, R(ROUTER_SEND_32)));
            int wait = p.size();                                   // wait: EVENTS_PEEK [12], request pending (live)
            p.add(lw(T0, 0, R(EVENTS_PEEK))); p.add(srli(T0, T0, 12)); p.add(andi(T0, T0, 1));
            p.add(beq(T0, 0, 4 * (wait - p.size())));
            p.add(lw(T1, 0, R(REQ_GET_W0))); p.add(andi(T1, T1, 0x7F));
            p.add(lw(A0, 0, R(REQ_RECEIVE)));                      // pops
            p.add(0x00C31313);                                     // slli t1, t1, 12
            p.add(0x00650533);                                     // add a0, a0, t1
        } else if (cc == 1) {
            p.add(lui(T0, 1)); p.add(addi(T0, T0, -2048));         // REQ_CC1(REQ_OP_STARTED, 0, 0) = 2 << 10
            p.add(sw(T0, 0, R(REQ_W0))); p.add(sw(0, 0, R(REQ_W1))); p.add(sw(0, 0, R(REQ_W2)));
            p.add(sw(A0, 0, R(REQ_SEND)));
        }
        p.add(sw(A0, 0, R(SIM_EXIT))); p.add(J_SELF);
        int[] w = new int[p.size()]; for (int i = 0; i < w.length; i++) w[i] = p.get(i); return w;
    }

    static void switchOff() {
        T.group("the switch off");
        Chip c = new Chip(cfg(false));
        T.check(c.cores == null, "no controller tiles: the Java model");
        T.check(c.tiles[0].router.fromCnc == c.cnc.send && c.tiles[0].router.toCnc == c.cnc.rx, "tile 0's fifth port on C&C 1's endpoint");
        c.run(300);
        T.eq(0, c.violations, "violations");
    }

    static void romBranch() {
        T.group("the boot ROM's branch for 64-66");
        Chip c = new Chip(cfg(true));
        CcCores k = c.cores;
        T.check(k != null, "controller tiles built");
        c.run(200);
        for (int cc = 0; cc < 3; cc++) {
            Tile u = k.unit(cc);
            T.check(u.core.inWfi && (u.core.pc & 0xFF000000) == Registers.BOOTROM_BASE, "C&C " + (cc + 1) + " waits in its ROM (WFI)");
            T.eq(cc == 0 ? 1 : cc + 1, u.slot.p[1] != null ? u.slot.p[1].type() : -1, "C&C " + (cc + 1) + "'s peripheral 1's type");
        }
        T.check(c.tiles[0].core.inWfi, "tiles wait in their ROM too");
        // C&C 2 and 3 first, then C&C 1 (whose program waits for C&C 2's request)
        for (int cc : new int[] { 1, 2, 0 }) { k.hostLoad(cc, Registers.PMEM_BASE, program(cc)); k.hostCommand(cc, Registers.CMD_JUMP << 24, Registers.PMEM_BASE); }
        boolean ok = c.runUntil(() -> { for (Tile u : k.units) if (u.core.halt != Controller.HALT_EXIT) return false; return true; }, 5000);
        T.check(ok, "every controller jumped to its program and exited");
        T.eq(65, k.unit(1).ctl.simExitValue, "C&C 2 read its id from PARAMS");
        T.eq(66, k.unit(2).ctl.simExitValue, "C&C 3 read its id from PARAMS");
        T.eq(65L * 0x1000 + 65, k.unit(0).ctl.simExitValue & 0xFFFFFFFFL, "C&C 1 received C&C 2's request: requestor 65, W3 = 65");
        T.eq(0x5A5, c.tiles[5].core.peekLocal(0x100), "C&C 1's packet reached tile 5's data memory through tile 0's fifth port");
        T.eq(1, c.reqBus.received[0], "target 0 popped once");
        T.check(c.tiles[0].core.inWfi && (c.tiles[0].core.pc & 0xFF000000) == Registers.BOOTROM_BASE, "tile 0 still in its ROM");
        T.eq(0, c.violations, "violations");
        if (c.violations != 0) System.out.print(c.violationReport());
    }
}
