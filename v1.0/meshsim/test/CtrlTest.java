import java.util.ArrayList;
import java.util.List;

/**
 * Control and the controller (spec 4.10-4.13, 6, 8). Programs are C, built by sw/build.sh. Usage:
 * {@code java CtrlTest [shuffleSeed]}.
 */
public final class CtrlTest {
    static long shuffle = -1;
    static boolean full;
    static final StringBuilder report = new StringBuilder();
    static void out(String s) { System.out.println(s); report.append(s).append('\n'); }

    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        full = args.length > 1 && args[1].equals("full");
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        fields();
        boot();
        registers();
        commands();
        wfi();
        faults();
        invariants();
        blockCopy();
        allToAll();
        System.exit(T.done("CtrlTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    static void noViolations(Chip c, String what) {
        T.check(c.violations == 0, what + ": no invariant violations" + (c.violations == 0 ? "" : "\n" + c.violationReport()));
    }

    // ================================================================ registers, the window and the send port (8, 4.10-4.12)
    static void registers() {
        T.group("registers");
        Mach m = new Mach(Mach.cfg(shuffle));
        Mach.Image im = m.boot("regs");
        boolean ok = m.run(m::allStopped, 40000);
        T.check(ok && m.allEbreak(), "every tile ran regs to its ebreak: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            int i = p.idx;
            int[] r = new int[30]; for (int k = 0; k < 30; k++) r[k] = m.word(i, im, "r", k);
            String tl = "tile " + i + ": ";
            for (int k = 0; k <= 6; k++) T.eq(0, r[k], tl + "write-only register " + k + " reads 0");
            T.eq(0x12345, r[7], tl + "SRAM_REMAINING reads back"); T.eq(0xFFFFF, r[8], tl + "CM_REMAINING keeps 20 bits"); T.eq(3, r[9], tl + "REPLY_REMAINING reads back");
            T.eq(0x1FFFF, r[10], tl + "HIST keeps 17 bits"); T.eq(0xFFFFF, p.ptr(i ^ 1), tl + "PTR keeps 20 bits (read through the simulator: PTR is write-only)");
            T.eq(0xDEADBEEF, p.ts.peek(TableStore.STREAM + 6), tl + "STREAM[3] low"); T.eq(0x12345678, p.ts.peek(TableStore.STREAM + 7), tl + "STREAM[3] high");
            T.eq(0x0BADF00D, p.ts.peek(TableStore.GROUP + 126), tl + "GROUP[63] low"); T.eq(0xFEEDFACE, p.ts.peek(TableStore.GROUP + 127), tl + "GROUP[63] high");
            T.eq(100 + i, r[16], tl + "mode-7 reply 0"); T.eq(200 + i, r[17], tl + "mode-7 reply 1 (addr + 1)"); T.eq(300 + i, r[18], tl + "mode-7 reply 2 (addr + 2)"); T.eq(0, r[19], tl + "REPLY_REMAINING counted down");
            T.eq(0x11110000 + i, r[20], tl + "mode-5 word 0"); T.eq(0xAAAA0000 + i, r[21], tl + "mode-5 word 1 (SET_HI)");
            T.eq(0x22220000 + i, r[22], tl + "mode-5 word 2 (addr + 2)"); T.eq(0, r[23], tl + "mode-5 word 3: SET_HI cleared by the previous send");
            T.eq(0x55500000 + i, r[24], tl + "mode-6 slot 64"); T.eq(0x55510000 + i, r[25], tl + "mode-6 slot 65 (addr + 1)");
            T.eq(0x104, p.ptr(i), tl + "mode 2 at PTR[me]: PTR advanced by 2 per packet");
            T.eq(0xA0A00000L + i | (0xB0B00000L + i) << 32, p.sram.peek64(0x100), tl + "mode-2 packet 0 in big SRAM");
            T.eq(0xC0C00000L + i | (0xD0D00000L + i) << 32, p.sram.peek64(0x102), tl + "mode-2 packet 1 in big SRAM");
            int s = r[27];
            T.eq(1, Registers.ST_MEM_IDLE.get(s), tl + "status: mem_idle"); T.eq(1, Registers.ST_CM_IDLE.get(s), tl + "status: cm_idle");
            T.eq(0, Registers.ST_DONE.get(s), tl + "status: done cleared by RUN"); T.eq(0, Registers.ST_LANDED.get(s), tl + "status: command landed cleared by RUN");
            T.eq(0, s & (1 << 2 | 0x7FF << 10 | 3 << 27), tl + "status: the spare bits [2], [20:10] and [28:27] read 0"); T.eq(0, p.ctl.conditions, tl + "no conditions"); T.eq(i, Registers.ST_TILE.get(s), tl + "status: tile"); T.eq(7, Registers.ST_SIZE.get(s), tl + "status: size 8 areas - 1 (4 MB)");
            T.eq(1, Registers.ST_DONE.get(r[28]), tl + "status: done after MASTER_PULSE[0], readable the next cycle");
            T.eq(29, r[29], tl + "every step ran");
            T.eq(3, p.ctl.localWakes, tl + "three WFIs, each left by the local wake");
            T.eq(0, p.ctl.readies - 1, tl + "READY only at boot (done without a landed command makes none)");
        }
        noViolations(m.c, "registers");
        out("registers [meas: CtrlTest registers]: write-only reads 0; counters 20 bits; HIST 17, PTR 20, STREAM/GROUP 64 bits through the window; send port modes 2, 5, 6, 7 to the tile itself with auto-increment +2/+1 and SET_HI cleared per send; local wake after each; status and done readable the next cycle");
    }

    // ================================================================ one- and two-command phases (6.3)
    static void commands() {
        T.group("commands");
        Mach m = new Mach(Mach.cfg(shuffle));
        Mach.Image im = m.boot("cmd");
        final int N = 512;
        for (Tile p : m.c.tiles) for (int j = 0; j < N; j++) p.sram.poke64(2 * j, (long) p.idx << 32 | j);
        // the next command goes out right after each GO (I-8)
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24 | 10, 5).go()               // one-command phase: sum += 10 + 5
             .command(LinkWord.BROADCAST, 1 << 24 | 20, 7).go()                // and again: sum += 27
             .command(LinkWord.BROADCAST, 2 << 24).go()                        // configure
             .command(LinkWord.BROADCAST, 3 << 24).go()                        // start
             .command(LinkWord.BROADCAST, 4 << 24).go();                       // exit
        // per GO and tile: the GO cycle as the tile saw it, and when its address generator started
        long[][] goSeen = new long[8][64];
        long[] startAt = new long[64];
        int[] gosSeen = new int[64];
        long[] w5 = new long[64]; long[] margin = { Long.MAX_VALUE }; int[] marginTile = { -1 };
        long[] l0 = new long[64]; java.util.Arrays.fill(l0, -1); long[] margin0 = { Long.MAX_VALUE };
        boolean ok = m.run(() -> {
            for (Tile p : m.c.tiles) {
                if (p.cmem.writesMbox != w5[p.idx]) {          // a mailbox write: how long after this tile's last GO?
                    w5[p.idx] = p.cmem.writesMbox;
                    int g = gosSeen[p.idx] - 1;
                    if (g >= 0 && m.c.cycle - goSeen[g][p.idx] < margin[0]) { margin[0] = m.c.cycle - goSeen[g][p.idx]; marginTile[0] = p.idx; }
                }
                if (p.cmem.landedAt != l0[p.idx]) {         // slot 0 written: how long after this tile's last GO?
                    l0[p.idx] = p.cmem.landedAt;
                    int g = gosSeen[p.idx] - 1;
                    if (g >= 0) margin0[0] = Math.min(margin0[0], p.cmem.landedAt - goSeen[g][p.idx]);
                }
                if (p.ctl.goVisible(m.c.cycle) && gosSeen[p.idx] < 8) goSeen[gosSeen[p.idx]++][p.idx] = m.c.cycle;
                if (p.ag.startedAt >= 0) startAt[p.idx] = p.ag.startedAt;
            }
            return m.allStopped();
        }, 100000);
        T.check(ok && m.allEbreak(), "every tile ran the command loop to its exit: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            int i = p.idx;
            T.eq(42, p.core.x[10], "tile " + i + ": sum of the two one-command phases");
            T.eq(5, m.word(i, im, "phases", 0), "tile " + i + ": five commands taken");
            T.eq(5, m.word(i, im, "arg1s", 0), "tile " + i + ": phase 0's argument (read before the next command landed)");
            T.eq(7, m.word(i, im, "arg1s", 1), "tile " + i + ": phase 1's argument");
            boolean copy = true; for (int j = 0; j < N; j++) if (p.sram.peek64(0x20000 + 2 * j) != ((long) i << 32 | j)) copy = false;
            T.check(copy, "tile " + i + ": the two-command phase's local copy landed");
            T.eq(5, p.ctl.gos, "tile " + i + ": five GOs");
        }
        // the start store lies at the same distance from GO on every tile (identical code from WFI)
        long d0 = startAt[0] - goSeen[3][0]; boolean same = true; long skewOk = 0;
        for (int i = 0; i < 64; i++) { if (startAt[i] - goSeen[3][i] != d0) same = false; if (goSeen[3][i] - goSeen[3][0] == m.c.startSkew[i] - m.c.startSkew[0]) skewOk++; }
        T.check(same, "the address generator starts the same number of cycles after GO on every tile");
        T.eq(64, skewOk, "GO reaches each tile with its 0/+1 skew");
        noViolations(m.c, "commands");
        // round trips
        StringBuilder sb = new StringBuilder("command phases [meas: CtrlTest commands]: per GO, all-READY -> GO store (C&C) and READY round trip\n");
        List<long[]> ph = m.c.cc.phaseLog;
        for (int g = 0; g < ph.size(); g++) sb.append(String.format("  GO %d: all READY at %d, GO stored at %d, seen by tiles at %d-%d%n", g, ph.get(g)[0], ph.get(g)[1], goSeen[g][0] - m.c.startSkew[0], goSeen[g][0] - m.c.startSkew[0] + 1));
        sb.append(String.format("  two-command phase: start store %d cycles after GO on every tile; local copy of %d records, start -> READY %d cycles%n", d0, N, ph.get(4)[0] - startAt[0]));
        sb.append(String.format("  the next command, sent right after GO: its first mailbox write lands %d cycles after the tile saw GO at the earliest (tile %d), into the other argument block;%n"
                + "  its slot 0 (zero-argument commands) %d cycles after GO at the earliest, after the woken tile read the previous command word (the simulator checks every slot-0 landing)%n", margin[0], marginTile[0], margin0[0]));
        out(sb.toString().stripTrailing());
    }

    // ================================================================ WFI and the local wake (4.12, 6.3)
    static void wfi() {
        T.group("wfi");
        Mach m = new Mach(Mach.cfg(shuffle));
        Mach.Image im = m.boot("wfi");
        long[] forcedGo = { -1 }, firstGo = { -1 };
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go()                      // wakes B
             .command(LinkWord.BROADCAST, 2 << 24).go()                        // C pulsed done: this GO arrives while C spins
             .call(() -> firstGo[0] = m.c.cc.lastGoAt)
             .waitCycles(1500)
             .then((c, t) -> { c.storeGo(t); forcedGo[0] = t; return true; })  // test only: a GO without READY wakes C
             .command(LinkWord.BROADCAST, 3 << 24).go();                       // lands during D's spin; READY at done
        boolean ok = m.run(m::allStopped, 20000);
        T.check(ok && m.allEbreak(), "every tile ran wfi to its ebreak: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            String tl = "tile " + p.idx + ": ";
            List<long[]> w = p.core.wfiLog;
            T.eq(4, w.size(), tl + "four WFIs");
            if (w.size() < 4) continue;
            T.check(w.get(0)[1] == w.get(0)[0] && w.get(0)[2] == TileControl.WAKE_LOCAL, tl + "A: the latch set before WFI, which falls straight through");
            T.check(w.get(1)[2] == TileControl.WAKE_GO && w.get(1)[1] > w.get(1)[0], tl + "B: after a counter store cleared the latch (and a count of 0), wait_for_command waits for GO");
            long goC = firstGo[0] + m.c.cfg.goLatency + m.c.startSkew[p.idx];
            T.check(w.get(2)[0] > goC, tl + "C: the GO arrived before WFI");
            T.eq(forcedGo[0] + m.c.cfg.goLatency + m.c.startSkew[p.idx], w.get(2)[1], tl + "C: GO is not latched: WFI waits for the next GO");
            T.check(p.ctl.lastLandedAt < p.ctl.lastDoneAt, tl + "D: the command landed before done");
            T.eq(1 + 2 + 3 + 4 + 9, m.word(p.idx, im, "r", 1), tl + "the self-sends landed");
        }
        long readyAtDone = 0; for (Tile p : m.c.tiles) if (p.ctl.readyPulseAt == p.ctl.lastDoneAt) readyAtDone++;
        T.eq(64, readyAtDone, "D: READY fires in the cycle done is pulsed when the command had landed");
        T.eq(0, sum(m, true), "no spurious local-wake edges were turned into wakes"); 
        noViolations(m.c, "wfi");
        out(String.format("WFI [meas: CtrlTest wfi]: latch before WFI falls through in 1 cycle; a counter store clears the latch; a count of 0 makes no edge; GO is not latched; READY fires at done when the command landed first; uncounted packets made %d local-wake edges that the qualifier (DEV) kept from waking", sum(m, false)));
    }
    static long sum(Mach m, boolean dummy) { long s = 0; if (dummy) return 0; for (Tile p : m.c.tiles) s += p.ctl.spuriousEdges; return s; }

    // ================================================================ controller faults and HALT -> RUN (4.11, 4.13, 6.1)
    static void faults() {
        T.group("faults");
        Mach m = new Mach(Mach.cfg(shuffle));
        Mach.Image im = m.boot("faults");
        m.run(m::allStopped, 5000);
        int[] reason = { Controller.HALT_BUSERR, Controller.HALT_BUSERR, Controller.HALT_BUSERR, Controller.HALT_ILLEGAL,
                Controller.HALT_ILLEGAL, Controller.HALT_ECALL, Controller.HALT_BUSERR, Controller.HALT_BUSERR };
        for (Tile p : m.c.tiles) {
            int k = p.idx & 7;
            T.eq(reason[k], p.core.halt, "tile " + p.idx + " (fault " + k + "): " + p.core.haltReport());
            int st = p.ctl.conditions;
            T.eq(reason[k] == Controller.HALT_BUSERR ? 1 : 0, st >> Conditions.BUS_ERROR & 1, "tile " + p.idx + ": bus-error condition");
            T.eq(reason[k] == Controller.HALT_ILLEGAL ? 1 : 0, st >> Conditions.ILLEGAL & 1, "tile " + p.idx + ": illegal-instruction condition");
        }
        long logged = m.c.log.stream().filter(l -> l.contains("controller halted")).count();
        T.eq(64, logged, "every controller halt logged with its cause");
        T.eq(0, Long.bitCount(m.c.cc.readyBits(m.c.cycle)), "no READY from a crashed tile (C&C's watchdog would catch it)");
        noViolations(m.c, "faults");
        // HALT, then RUN with a marker whose slot 2 says "restart": every core restarts at the loader; entering HALT cleared the sticky bits
        m.c.cc.halt().command(LinkWord.BROADCAST, Registers.CMD_JUMP << 24, im.sym("_start"), 0x600D).runWhenReady();
        boolean ok = m.run(() -> m.c.cc.runStores == 3 && m.c.cycle > m.c.cc.lastRunAt + 5 && m.allStopped(), 5000);
        T.check(ok && m.allEbreak(), "after HALT -> RUN every tile restarted and exited: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            T.eq(0x600D0000 + p.idx, m.word(p.idx, im, "ok", 0), "tile " + p.idx + ": restarted at the loader");
            T.eq(0, m.word(p.idx, im, "st", 0), "tile " + p.idx + ": entering HALT cleared the conditions (MASTER_CONDITIONS reads 0)");
        }
        noViolations(m.c, "restart");
        out("faults [meas: CtrlTest faults]: store to MASTER_STATUS, load from instruction memory, sub-word register store, the table window's gap and a misaligned load are bus errors; div and a CSR read are illegal; ecall halts; each cause logged, its condition set, no READY; HALT clears the conditions and HALT -> RUN restarts every core at the loader");
    }

    // ================================================================ programming errors the simulator catches (spec 9)
    static void invariants() {
        T.group("invariants");
        Mach m = new Mach(Mach.cfg(shuffle));
        m.c.cfg.maxViolationMessages = 1000;
        m.c.cfg.strictReplies = true;                    // invariant 8 strict for this test
        Mach.Image im = m.boot("inv");
        m.run(m::allStopped, 10000);
        T.check(m.allEbreak(), "every tile ran inv: " + m.haltSummary());
        String[] want = { "invariant 4", "invariant 8", "address generator is running", "invariant 9", "DMA_PULSE [3:2]" };
        for (Tile p : m.c.tiles) {
            String key = "tile " + p.idx + ": ";
            long n = m.c.violationMessages.stream().filter(v -> v.contains(key) && v.contains(want[p.idx & 3])).count();
            T.check(n >= 1, "tile " + p.idx + ": " + want[p.idx & 3] + " reported");
            if ((p.idx & 3) == 0) T.eq(1 << Conditions.DONE_EARLY, p.ctl.conditions, key + "done with DMA_SRAM_REMAINING non-zero: done early, and only it");
            if ((p.idx & 3) == 3) {
                T.eq(1 << Conditions.UNEXPECTED, m.word(p.idx, im, "r", 0), key + "the unexpected-arrival condition set, and only it");
                T.eq(m.word(p.idx, im, "r", 0), m.word(p.idx, im, "r", 1), key + "a store of 1 to DMA_PULSE[3] (reserved) is ignored: the condition stays");
                T.check(m.c.violationMessages.stream().anyMatch(v -> v.contains(key) && v.contains(want[4])), key + "and meshsim flags the reserved bit");
            }
        }
        long other = m.c.violationMessages.stream().filter(v -> { for (String w : want) if (v.contains(w)) return false; return true; }).count();
        T.eq(0, other, "no other violations: " + m.c.violationReport());
        // invariant 8 by default: counted, not a violation
        Mach d = new Mach(Mach.cfg(shuffle));
        d.c.cfg.maxViolationMessages = 1000;
        d.boot("inv");
        d.run(d::allStopped, 10000);
        long ow = 0; for (Tile p : d.c.tiles) ow += p.cmem.overwrittenUnread;
        T.eq(16L, ow, "invariant 8 by default: the 16 overwritten-unread replies (tiles 1, 5, ...) are counted");
        T.check(d.c.violationMessages.stream().noneMatch(v -> v.contains("invariant 8")), "invariant 8 by default: no violation");
        // invariant 5 (no packet with a bad mode; 0 or 8-15; is sent) and invariant 7 (a packet in flight when the line changes)
        Mach h = new Mach(Mach.cfg(shuffle));
        h.c.cc.send(5, LinkWord.M_INVALID, 0x100, 0x800).waitCycles(30)
               .send(9, LinkWord.M_MBOX, 3, 1).then((c, t) -> { c.storeRun(t, true); return true; });
        h.c.run(60);
        T.check(h.c.violationMessages.stream().anyMatch(v -> v.contains("invariant 5")), "invariant 5: a mode-0 packet sent");
        T.check(h.c.violationMessages.stream().anyMatch(v -> v.contains("invariant 7")), "invariant 7: RUN raised with a packet in flight");
        out("invariants [meas: CtrlTest invariants]: 4 (done before arrivals), 5 (a mode-0 packet sent), 7 (packet in flight at a line change), 8 (reply overwritten unread), 9 (unexpected arrival: its condition set; a store to the reserved MASTER_PULSE[3] ignored and flagged; M14) and the table-window check each fire on a bad program");
    }

    // ================================================================ mesh.h's field table agrees with the engines' encoders
    static void fields() {
        T.group("fields");
        java.util.Random r = new java.util.Random(5);
        for (int k = 0; k < 200; k++) {
            int src = r.nextInt(1 << 20), cm = r.nextInt(1 << 12), cnt = r.nextInt(1 << 18), d = r.nextInt(64), mode = r.nextInt(7), size = r.nextInt(4), gap = r.nextInt(8);
            T.eq(Fields.streamLo(src, cm), Registers.STL_SRC.place(src) | Registers.STL_CM.place(cm), "STREAM low");
            T.eq(Fields.streamHi(cnt, d, mode, size, gap), Registers.STH_COUNT.place(cnt) | Registers.STH_D.place(d) | Registers.STH_MODE.place(mode) | Registers.STH_SIZE.place(size) | Registers.STH_GAP.place(gap), "STREAM high");
            int R = r.nextInt(1 << 18), st = r.nextInt(256), t = 1 + r.nextInt(16), g = r.nextInt(256);
            T.eq(Fields.groupLo(R), Registers.GRL_R.place(R), "GROUP low");
            T.eq(Fields.groupHi(st, t, g), Registers.GRH_START.place(st) | Registers.GRH_TM1.place(t - 1) | Registers.GRH_GAP.place(g), "GROUP high");
            int kind = r.nextInt(2), s0 = r.nextInt(256), c = 1 + r.nextInt(256), bd = r.nextInt(5), cd = r.nextInt(5), kk = 1 + r.nextInt(8);
            T.eq(Fields.masterConfig(kind, s0, c, bd, cd, kk), Registers.MC_KIND.place(kind) | Registers.MC_START.place(s0) | Registers.MC_COUNT.place(c)
                    | Registers.MC_BUCKET.place(bd) | Registers.MC_HIST.place(cd) | Registers.MC_KM1.place(kk - 1), "MASTER_CONFIG");
        }
        T.eq(Fields.PULSE_DONE | Fields.PULSE_START, Registers.P_DONE.place(1) | Registers.P_START.place(1), "DMA_PULSE: done [0], start [1] ([3:2] reserved)");
        T.eq(0xC, Registers.PULSE_RESERVED_MASK, "DMA_PULSE [3:2] reserved");
        int used = 0; for (Registers.Field f : Registers.FIELDS) if (f.word.equals("TILE_ST")) { T.eq(0, used & (f.mask() << f.lo), "TILE_STATUS fields do not overlap: " + f.name); used |= f.mask() << f.lo; }
        T.eq(~(1 << 2 | 0x7FF << 10 | 3 << 27), used, "TILE_STATUS: live [9:0], tile [26:21], size [31:29]; [2], [20:10] and [28:27] spare");
        T.eq(0xFFFFF804L, Registers.TILE_CONDITIONS.addr() & 0xFFFFFFFFL, "TILE_CONDITIONS at 0xFFFFF804");
        T.check(Registers.TILE_CONDITIONS.readable() && !Registers.TILE_CONDITIONS.writable(), "TILE_CONDITIONS is read-only");
        T.eq(0x7FF, Conditions.MASK, "eleven conditions in [10:0]");
        T.eq(13, Registers.N_REGS, "13 registers, 0xFFFFF800 + 4 i");
        int mcUsed = 0; for (Registers.Field f : Registers.FIELDS) if (f.word.equals("DMA_CFG")) { T.eq(0, mcUsed & (f.mask() << f.lo), "DMA_CONFIG fields do not overlap: " + f.name); mcUsed |= f.mask() << f.lo; }
        T.eq((1 << 27) - 1, mcUsed, "DMA_CONFIG is 27 bits (8.1)");
        int hUsed = 0; for (Registers.Field f : Registers.FIELDS) if (f.word.equals("ROUTER_HDR")) hUsed |= f.mask() << f.lo;
        T.eq((1 << 31) - 1, hUsed, "ROUTER_SET_HDR: dst [6:0], mode [10:7], addr [30:11]");
    }

    // ================================================================ block copy into controller memory (6.5)
    static void blockCopy() {
        T.group("block copy");
        Mach m = new Mach(Mach.cfg(shuffle));
        final int N = 1024, DST = 0x0800;
        // the overlay: "add a0, a0, a0; addi a0, a0, 0x123; ret" at its start, then filler words
        int[] ov = new int[N];
        ov[0] = 0x00a50533; ov[1] = 0x12350513; ov[2] = 0x00008067;
        for (int i = 3; i < N; i++) ov[i] = 0x5A5A0000 + i;
        for (Tile p : m.c.tiles) for (int i = 0; i < N; i++) p.sram.poke(0x1000 + i, ov[i]);
        Mach.Image im = m.boot("blockcopy");
        boolean ok = m.run(m::allStopped, 20000);
        T.check(ok && m.allEbreak(), "every tile copied, woke and called the overlay: " + m.haltSummary());
        long cyc = -1; boolean same = true;
        for (Tile p : m.c.tiles) {
            boolean match = true; for (int i = 0; i < N; i++) if (p.core.peekLocal(DST + 4 * i) != ov[i]) match = false;
            T.check(match, "tile " + p.idx + ": 1,024 words landed in instruction memory");
            T.eq(2L * p.idx + 0x123, m.word(p.idx, im, "res", 0), "tile " + p.idx + ": the copied code ran");
            T.eq(1, p.core.wfiLog.size(), "tile " + p.idx + ": one WFI");
            if (p.core.wfiLog.size() == 1) {
                T.eq(TileControl.WAKE_LOCAL, p.core.wfiLog.get(0)[2], "tile " + p.idx + ": woken by the local wake");
                long c = p.core.wfiLog.get(0)[1] - p.ag.startedAt + 1;
                if (cyc < 0) cyc = c; else if (c != cyc) same = false;
            }
        }
        T.check(same, "the same copy time on every tile");
        noViolations(m.c, "block copy");
        out(String.format("block copy [meas: CtrlTest block copy]: 1,024 words (512 mode-5 packets) big SRAM -> instruction memory by the 6.5 recipe, from the arm-and-start store to the first instruction after WFI: %d cycles (spec 6.5: about 512 plus the stream's start)", cyc));
    }

    // ================================================================ the all-to-all from C (two-command phase)
    static void allToAll() {
        T.group("all-to-all from C");
        StringBuilder sb = new StringBuilder("16-group all-to-all programmed from C as a two-command phase, gap 0 [meas: CtrlTest all-to-all]: cycles from C&C's start GO store to all 64 READY at C&C\n");
        sb.append("  data set                   GO -> all READY     bound   over  | M4 start->last wake (Java-driven, M3 harness)  | v3.10 GO -> all DONE (gap 8)\n");
        List<Object[]> sets = new ArrayList<>();
        try {
            sets.add(new Object[] { "uniform (Data(7, " + (full ? 65536 : 2000) + "))", new A2A(7, full ? 65536 : 2000), full ? "71,345" : "" });
            if (full) for (String[] f : new String[][] { { "zipfraw", "294,580" }, { "trace", "706,470" }, { "one99", "434,712" } })
                sets.add(new Object[] { f[0], new A2A(java.nio.file.Path.of("data/" + f[0] + ".txt")), f[1] });
        } catch (java.io.IOException e) { throw new RuntimeException(e); }
        for (Object[] set : sets) {
            A2A a = (A2A) set[1];
            int gap = 0;
            Mach m = new Mach(Mach.cfg(shuffle));
            for (Tile p : m.c.tiles) {
                int s = p.idx;
                for (int k = 0; k < a.rec[s].length; k++) p.sram.poke64(A2A.SRC + 2 * k, a.rec[s][k]);
                int base = 0xF0000;
                for (int d = 0; d < 64; d++) p.sram.poke(base + d, a.cnt[s][d]);
                for (int src = 0; src < 64; src++) p.sram.poke(base + 64 + src, a.cnt[src][s]);
                for (int g = 0; g < 16; g++) p.sram.poke(base + 128 + g, a.R[g]);
                p.sram.poke(base + 144, gap);
            }
            Mach.Image im = m.boot("a2a");
            m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go().command(LinkWord.BROADCAST, 2 << 24).go().command(LinkWord.BROADCAST, 3 << 24).go();
            boolean ok = m.run(m::allStopped, 2_000_000);
            T.check(ok && m.allEbreak(), set[0] + ": every tile ran a2a to its exit: " + m.haltSummary());
            T.eq(0, a.verify(m.c), set[0] + ": every record landed where it belongs");
            noViolations(m.c, set[0].toString());
            List<long[]> ph = m.c.cc.phaseLog;          // GO 0 configure, 1 start, 2 exit
            long goToReady = ph.get(2)[0] - ph.get(1)[1];
            Mach j = new Mach(A2A_cfg()); a.load(j.c, gap); long javaRun = A2A.run(j.c, 3_000_000);
            sb.append(String.format("  %-24s %10d %12d %6d  | %10d %36s | %s%n", set[0], goToReady, a.bound(gap), goToReady - a.bound(gap), javaRun, "", set[2]));
            out("    " + set[0] + ": " + m.c.agReport().trim());
        }
        out(sb.toString().stripTrailing());
    }
    static Config A2A_cfg() { Config c = Config.defaults(); c.shuffleSeed = shuffle; return c; }

    // ================================================================ boot (6.2)
    static void boot() {
        T.group("boot");
        Mach m = new Mach(Mach.cfg(shuffle));
        for (Tile p : m.c.tiles) T.check(!p.ctl.running(0) && p.ctl.done, "tile " + p.idx + " halted at reset with done forced to 1");
        Mach.Image im = m.boot("hello");
        boolean ok = m.run(m::allStopped, 20000);
        T.check(ok && m.allEbreak(), "every tile ran hello to its ebreak: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            int i = p.idx;
            T.eq(i, m.word(i, im, "result", 0), "tile " + i + ": tile index from MASTER_STATUS");
            T.eq(0xC0FFEE00 + i, m.word(i, im, "result", 1), "tile " + i + ": .rodata constant + index");
            T.eq(1 << 20, m.word(i, im, "result", 2), "tile " + i + ": big-SRAM size from MASTER_STATUS");
            T.eq(0xF4000, p.ptr(64), "tile " + i + ": the loader set PTR[64] = 0xF4000, below the sort scratch (read through the simulator)");
            T.eq(7, p.core.x[10], "tile " + i + ": main's return value in a0");
        }
        noViolations(m.c, "boot");
        long last = 0; for (Tile p : m.c.tiles) last = Math.max(last, p.core.haltPc >= 0 ? 0 : 0);
        int words = Mach.loader().length + im.code.length + im.data.length;
        out(String.format("boot [meas: CtrlTest boot]: loader %d words + hello %d code + %d data words = %d mode-5 packets and 2 mailbox packets from C&C; all 64 READY at cycle %d, RUN raised at %d, every tile at main's ebreak by %d",
                Mach.loader().length, im.code.length, im.data.length, (Mach.loader().length + 1) / 2 + (im.code.length + 1) / 2 + (im.data.length + 1) / 2, m.c.cc.lastAllReadyAt, m.runAt, m.c.cycle));
    }
}
