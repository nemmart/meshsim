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
        T.check(ok && m.allExited(), "every tile ran regs to its exit: " + m.haltSummary());
        int buf = im.sym("buf");
        for (Tile p : m.c.tiles) {
            int i = p.idx;
            int[] r = new int[36]; for (int k = 0; k < 36; k++) r[k] = m.word(i, im, "r", k);
            String tl = "tile " + i + ": ";
            T.eq(0xBCDEF, r[0], tl + "DMA_CONFIG reads back (v1.2: readable)"); T.eq(0, r[1], tl + "PULSE_DMA reads 0");
            T.eq(0x54321, r[2], tl + "DMA_RECORD_BASE_ADDRESS reads back"); T.eq(0, r[3], tl + "SRAM_RD_DEST_ADDR: 0 at reset");
            T.eq(Registers.HDR_DST.place(i) | Registers.HDR_MODE.place(LinkWord.M_SRC) | Registers.HDR_CTR.place(1), r[4], tl + "ROUTER_HDR reads back without the size and the spare bits (stores ignore them)");
            for (int k = 5; k <= 8; k++) T.eq(0, r[k], tl + "write-only register " + k + " (ROUTER_SET_W0-W2, ROUTER_SEND_32) reads 0");
            T.eq(0x12345678, r[9], tl + "ROUTER_ADDR reads back"); T.eq(0, r[10], tl + "PULSE_DONE reads 0");
            T.eq(0x12345, r[11], tl + "WRITE_COUNTER_1 reads back"); T.eq(0xFFFFF, r[12], tl + "WRITE_COUNTER_2 keeps 20 bits"); T.eq(3, r[13], tl + "WRITE_COUNTER_3: a write of 0 adds nothing");
            T.eq(3L, p.ctl.counterWraps, tl + "three writes past 2^20 - 1 wrapped to 0 (flagged)");
            T.eq(0x1FFFF, r[14], tl + "HIST keeps 17 bits"); T.eq(0x1FFFFF, p.ptr(i ^ 1), tl + "PTR keeps 21 bits (read through the simulator: PTR is write-only)");
            T.eq(0xDEADBEEF, p.ts.peek(TableStore.STREAM_SRC + 3), tl + "STREAM_SRC[3]"); T.eq(0x12345678, p.ts.peek(TableStore.STREAM_DEST + 3), tl + "STREAM_DEST[3]");
            T.eq(0x0BADBEEF, p.ts.peek(TableStore.STREAM_COUNT + 3), tl + "STREAM_COUNT[3]");
            T.eq(0x0BADF00D, p.ts.peek(TableStore.GROUP + 126), tl + "GROUP[63] low"); T.eq(0xFEEDFACE, p.ts.peek(TableStore.GROUP + 127), tl + "GROUP[63] high");
            T.eq(0xAAAA0000 + i, r[15], tl + "64-bit reply, word 0 (W0) at REPLY + 8 me"); T.eq(0xBBBB0000 + i, r[16], tl + "word 1 (the sending word)");
            T.eq(0xCCCC0000 + i, r[17], tl + "32-bit reply: word 0"); T.eq(0xBBBB0000 + i, r[18], tl + "a 32-bit reply leaves word 1 alone");
            T.eq(Registers.REPLY_BASE + 8 * i, r[19], tl + "ROUTER_ADDR stays put under no-increment");
            T.eq(0x11110000 + i, r[20], tl + "mode-0 128-bit word 0 (W0)"); T.eq(0xAAAA0000 + i, r[21], tl + "word 1 (W1)");
            T.eq(0xBBBB0000 + i, r[22], tl + "word 2 (W2)"); T.eq(0xCCCC0000 + i, r[23], tl + "word 3 (the sending word)");
            T.eq(0x22220000 + i, r[24], tl + "mode-0 32-bit packet at ROUTER_ADDR + 16: one word, W0 not sent");
            T.eq(0, r[25], tl + "buf[5] untouched");
            T.eq(0, r[26], tl + "the 64-bit packet's word 0 is 0 (W0 cleared by the 32-bit send)"); T.eq(0x33330000 + i, r[27], tl + "its word 1 the sending word");
            T.eq(buf + 32, r[28], tl + "ROUTER_ADDR advanced by 8 past the 64-bit packet");
            T.eq(0x107, p.ptr(i), tl + "mode 1 at PTR[me]: PTR advanced by 4, 2 and 1 (128, 64, 32 bits)");
            T.eq(0xD0D00000L + i | (0xD1D10000L + i) << 32, p.sram.peek64(0x100), tl + "mode-1 128-bit packet, chunks 0-1");
            T.eq(0xD2D20000L + i | (0xD3D30000L + i) << 32, p.sram.peek64(0x102), tl + "mode-1 128-bit packet, chunks 2-3");
            T.eq(0xA0A00000L + i | (0xB0B00000L + i) << 32, p.sram.peek64(0x104), tl + "mode-1 64-bit packet");
            T.eq(0xC0C00000 + i, p.sram.peek(0x106), tl + "mode-1 32-bit packet: one chunk");
            T.eq(0, r[29], tl + "STATUS: the idle machine (no run, done and command landed cleared by RUN)"); T.eq(0, p.ctl.conditions, tl + "no errors");
            T.eq(Registers.EVENTS_MASK_RESET, r[30], tl + "EVENTS_MASK = [8] | [7] after HALT -> RUN"); T.eq(0, r[31], tl + "EVENTS_REQUIRED = 0");
            T.eq(7, r[32], tl + "EVENTS_PEEK: counters 1-3's done events [2:0] still pending (not in the reset mask, so wfi() didn't consume them); counters done consumed");
            T.eq(buf + 32, r[33], tl + "modes 1-5 leave ROUTER_ADDR alone");
            T.eq(1 << Registers.ST_DONE, r[34], tl + "STATUS: done after PULSE_DONE, readable the next cycle");
            T.eq(35, r[35], tl + "every step ran");
            T.eq(4, p.ctl.eventWakes, tl + "four WFIs, each left by an event (counters done)");
            T.eq(0, p.ctl.readies - 1, tl + "READY only at boot (done without a landed command makes none)");
            int par = p.ctl.params();
            T.eq(1, Registers.PA_PERIPH.get(par), tl + "PARAMS: peripheral 1 present"); T.eq(i, Registers.PA_ID.get(par), tl + "PARAMS: endpoint id");
            T.eq(15, Registers.PA_SIZE.get(par), tl + "PARAMS: size 16 units of 256 KB - 1 (4 MB)");
        }
        T.eq(64 * 3L, m.c.violations, "the 192 violations are the three counter wraps on every tile");
        m.c.violations = 0; m.c.violationMessages.clear();
        noViolations(m.c, "registers");
        out("registers [meas: CtrlTest registers]: the configuration registers read back, write-only ones read 0; counters 20 bits, writes add and wrap (flagged); HIST 17, PTR 21, the STREAM arrays and GROUP 32 bits a word through the window; the send port in modes 0 and 1 to the tile itself at 32, 64 and 128 bits, ROUTER_ADDR + 4/8/16 at mode 0 and fixed under no-increment, W0-W2 cleared per send; counters done after each; the events' registers at reset; STATUS and done readable the next cycle");
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
                if (p.cmem.writesCmd != w5[p.idx]) {           // a command write: how long after this tile's last GO?
                    w5[p.idx] = p.cmem.writesCmd;
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
        T.check(ok && m.allExited(), "every tile ran the command loop to its exit: " + m.haltSummary());
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
        long[] firstGo = { -1 };
        m.c.cc.command(LinkWord.BROADCAST, 1 << 24).go()                      // wakes B
             .command(LinkWord.BROADCAST, 2 << 24).go()                        // C pulsed done: this GO arrives while C spins
             .call(() -> firstGo[0] = m.c.cc.lastGoAt)
             .waitCycles(700)
             .command(LinkWord.BROADCAST, 3 << 24).go();                       // lands during D's spin; READY at done
        boolean ok = m.run(m::allStopped, 20000);
        T.check(ok && m.allExited(), "every tile ran wfi to its exit: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            String tl = "tile " + p.idx + ": ";
            List<long[]> w = p.core.wfiLog;
            T.eq(4, w.size(), tl + "four WFIs");
            if (w.size() < 4) continue;
            T.check(w.get(0)[1] == w.get(0)[0] && w.get(0)[2] == TileControl.WAKE_EVENT, tl + "A: counters done set before WFI, which falls straight through");
            int a = m.word(p.idx, im, "r", 2);
            T.eq(1 << Registers.EV_COUNTERS_DONE | 1 << Registers.evCounter(2), a & ~(1 << Registers.EV_COMMAND), tl + "A: wfi() returns EVENTS: counters done and counter 2 done (and command landed if command 1 is in)");
            T.check(w.get(1)[2] == TileControl.WAKE_GO && w.get(1)[1] > w.get(1)[0], tl + "B: done cleared counters done, so wait_for_command waits for GO");
            long goC = firstGo[0] + m.c.cfg.goLatency + m.c.startSkew[p.idx];
            T.check(w.get(2)[0] > goC, tl + "C: the GO arrived before WFI");
            T.check(w.get(2)[1] == w.get(2)[0] && w.get(2)[2] == TileControl.WAKE_GO, tl + "C: GO is sticky: WFI falls straight through on it");
            T.check(p.ctl.lastLandedAt < p.ctl.lastDoneAt, tl + "D: the command landed before done");
            T.eq(1 + 2 + 3 + 4 + 9, m.word(p.idx, im, "r", 1), tl + "the self-sends landed");
            T.eq(2 << 24, m.word(p.idx, im, "cmds", 1), tl + "C read command 2 (the third command after the boot marker: parity 0)");
        }
        long readyAtDone = 0; for (Tile p : m.c.tiles) if (p.ctl.readyPulseAt == p.ctl.lastDoneAt) readyAtDone++;
        T.eq(64, readyAtDone, "D: READY fires in the cycle done is pulsed when the command had landed");
        T.eq(0, sum(m, true), "no spurious local-wake edges were turned into wakes");
        noViolations(m.c, "wfi");
        out(String.format("WFI [meas: CtrlTest wfi]: counters done before WFI falls through in 1 cycle and wfi()'s load returns it; done clears it; GO is sticky (a WFI after a missed GO falls through); READY fires at done when the command landed first; uncounted packets made %d counters-done edges that the qualifier kept from setting it", sum(m, false)));
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
        T.check(ok && m.allExited(), "after HALT -> RUN every tile restarted and exited: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            T.eq(0x600D0000 + p.idx, m.word(p.idx, im, "ok", 0), "tile " + p.idx + ": restarted at the loader");
            T.eq(0, m.word(p.idx, im, "st", 0), "tile " + p.idx + ": entering HALT cleared the errors (ERRORS reads 0; PHASE-2 INTERIM)");
        }
        noViolations(m.c, "restart");
        out("faults [meas: CtrlTest faults] (PHASE-2 INTERIM: a fault halts; phase 4 traps): a store to STATUS, a store to program memory, a sub-word register store, a load from PTR and a misaligned load are bus errors; div and a CSR read are illegal; ecall halts; each cause logged, its error set, no READY; HALT clears the errors and HALT -> RUN restarts every core at the loader");
    }

    // ================================================================ programming errors the simulator catches (spec 9)
    static void invariants() {
        T.group("invariants");
        Mach m = new Mach(Mach.cfg(shuffle));
        m.c.cfg.maxViolationMessages = 1000;
        m.c.cfg.strictReplies = true;                    // invariant 8 strict for this test
        Mach.Image im = m.boot("inv");
        m.run(m::allStopped, 10000);
        T.check(m.allExited(), "every tile ran inv: " + m.haltSummary());
        String[] want = { "invariant 4", "invariant 8", "address generator is running", "invariant 9", "reserved register-block word" };
        for (Tile p : m.c.tiles) {
            String key = "tile " + p.idx + ": ";
            long n = m.c.violationMessages.stream().filter(v -> v.contains(key) && v.contains(want[p.idx & 3])).count();
            T.check(n >= 1, "tile " + p.idx + ": " + want[p.idx & 3] + " reported");
            if ((p.idx & 3) == 0) T.eq(1 << Conditions.DONE_EARLY, p.ctl.conditions, key + "done with WRITE_COUNTER_1 non-zero: done early, and only it");
            if ((p.idx & 3) == 3) {
                T.eq(1 << Conditions.UNEXPECTED, m.word(p.idx, im, "r", 0), key + "the unexpected-arrival error set, and only it");
                T.eq(0, m.word(p.idx, im, "r", 1), key + "a reserved register-block word reads 0");
                T.eq(m.word(p.idx, im, "r", 0), m.word(p.idx, im, "r", 2), key + "and its load changes nothing");
                T.check(m.c.violationMessages.stream().anyMatch(v -> v.contains(key) && v.contains(want[4])), key + "and meshsim flags the access");
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
        // invariant 5 (no bad packet is sent: a misaligned mode 0) and invariant 7 (a packet in flight when the line changes)
        Mach h = new Mach(Mach.cfg(shuffle));
        h.c.cc.send(5, LinkWord.M_WRITE, 0x104, 0x800).waitCycles(30)
               .send(9, LinkWord.M_WRITE, Registers.COMMAND_BASE + 12, LinkWord.SIZE_32, 1, 0).then((c, t) -> { c.storeRun(t, true); return true; });
        h.c.run(60);
        T.check(h.c.violationMessages.stream().anyMatch(v -> v.contains("invariant 5")), "invariant 5: a misaligned mode-0 packet sent");
        T.check(h.c.violationMessages.stream().anyMatch(v -> v.contains("invariant 7")), "invariant 7: RUN raised with a packet in flight");
        out("invariants [meas: CtrlTest invariants]: 4 (done before arrivals), 5 (a bad packet sent), 7 (packet in flight at a line change), 8 (reply overwritten unread), 9 (unexpected arrival: its error set), a reserved register-block word (reads 0, flagged) and the table-window check each fire on a bad program");
    }

    // ================================================================ mesh.h's field table agrees with the engines' encoders
    static void fields() {
        T.group("fields");
        java.util.Random r = new java.util.Random(5);
        for (int k = 0; k < 200; k++) {
            int src = r.nextInt(1 << 21), dest = r.nextInt(), cnt = r.nextInt(1 << 17), d = r.nextInt(128), mode = r.nextInt(8), size = r.nextInt(4), gap = r.nextInt(8), ctr = r.nextInt(8);
            T.eq(Fields.streamSrc(src, gap), Registers.SS_CHUNK.place(src) | Registers.SS_GAP.place(gap), "STREAM_SRC");
            T.eq(Fields.streamCount(cnt, size, ctr, mode, d), Registers.SC_N.place(cnt) | Registers.HDR_SIZE.place(size) | Registers.HDR_CTR.place(ctr)
                    | Registers.HDR_MODE.place(mode) | Registers.HDR_DST.place(d), "STREAM_COUNT (the common header layout, 8.1)");
            long e0 = Fields.streamE0(Fields.streamSrc(src, gap), dest), e1 = Fields.streamCount(cnt, size, ctr, mode, d) & 0xFFFFFFFFL;
            T.check(Fields.sSrc(e0) == src && Fields.sGap(e0) == gap && Fields.sDest(e0) == dest && Fields.sD(e1) == d && Fields.sCount(e1) == cnt
                    && Fields.sMode(e1) == mode && Fields.sSize(e1) == size && Fields.sCounter(e1) == ctr, "STREAM entry decodes as encoded");
            int R = r.nextInt(1 << 18), st = r.nextInt(256), t = 1 + r.nextInt(16), g = r.nextInt(256);
            T.eq(Fields.groupLo(R), Registers.GRL_R.place(R), "GROUP low");
            T.eq(Fields.groupHi(st, t, g), Registers.GRH_START.place(st) | Registers.GRH_TM1.place(t - 1) | Registers.GRH_GAP.place(g), "GROUP high");
            int kind = r.nextInt(2), s0 = r.nextInt(256), c = 1 + r.nextInt(256), bd = r.nextInt(5), cd = r.nextInt(5), kk = 1 + r.nextInt(8);
            T.eq(Fields.masterConfig(kind, s0, c, bd, cd, kk), Registers.MC_KIND.place(kind) | Registers.MC_START.place(s0) | Registers.MC_COUNT.place(c)
                    | Registers.MC_BUCKET.place(bd) | Registers.MC_HIST.place(cd) | Registers.MC_KM1.place(kk - 1), "DMA_CONFIG");
        }
        // the common header layout (8.1): size [18:17], counter [21:19], mode [24:22], dst [31:25]
        int hUsed = 0; for (Registers.Field f : Registers.FIELDS) if (f.word.equals("HDR")) { T.eq(0, hUsed & (f.mask() << f.lo), "header fields do not overlap: " + f.name); hUsed |= f.mask() << f.lo; }
        T.eq(0xFFFE0000, hUsed, "the common header block is [31:17]");
        T.eq(LinkWord.hdr(0, 5, 3, 6, 0, 1) >>> 7 & 0x7F, 5, "LinkWord: dst"); 
        T.eq(Fields.hdrBits(1, 6, 3, 5), Registers.HDR_SIZE.place(1) | Registers.HDR_CTR.place(6) | Registers.HDR_MODE.place(3) | Registers.HDR_DST.place(5), "Fields.hdrBits agrees");
        // the block (8.1): offsets
        int[][] off = { { Registers.PARAMS.addr(), 0xFFFFF800 }, { Registers.ERRORS.addr(), 0xFFFFF808 }, { Registers.PULSE_DONE.addr(), 0xFFFFF80C },
            { Registers.PULSE_DMA.addr(), 0xFFFFF810 }, { Registers.EVENTS.addr(), 0xFFFFF818 }, { Registers.EVENTS_REQUIRED.addr(), 0xFFFFF824 },
            { Registers.ROUTER_HDR.addr(), 0xFFFFF828 }, { Registers.ROUTER_ADDR.addr(), 0xFFFFF82C }, { Registers.ROUTER_SEND_128.addr(), 0xFFFFF844 },
            { Registers.REQUEST_SEND.addr(), 0xFFFFF854 }, { Registers.DMA_CONFIG.addr(), 0xFFFFF858 }, { Registers.WRITE_COUNTER[1].addr(), 0xFFFFF860 },
            { Registers.WRITE_COUNTER[7].addr(), 0xFFFFF878 }, { Registers.SRAM_RD_ADDR_QUEUE.addr(), 0xFFFFF87C }, { Registers.SRAM_RD_DEST_ADDR.addr(), 0xFFFFF880 },
            { Registers.SIM_EXIT.addr(), 0xFFFFF9F8 }, { Registers.TRACE.addr(), 0xFFFFF9FC } };
        for (int[] o : off) T.eq(o[1], o[0], "register at 0x" + Integer.toHexString(o[1]));
        T.check(Registers.ERRORS.readable() && !Registers.ERRORS.writable(), "ERRORS is read-only");
        T.eq(0x7FFF, Conditions.MASK, "fifteen errors in [14:0]");
        int mcUsed = 0; for (Registers.Field f : Registers.FIELDS) if (f.word.equals("DMA_CFG")) { T.eq(0, mcUsed & (f.mask() << f.lo), "DMA_CONFIG fields do not overlap: " + f.name); mcUsed |= f.mask() << f.lo; }
        T.eq((1 << 27) - 1, mcUsed, "DMA_CONFIG is 27 bits (8.5)");
        T.check(Registers.reservedWord(5) && Registers.reservedWord(33) && Registers.reservedWord(125) && !Registers.reservedWord(126), "reserved words: 0x814, 0x884-0x9F4");
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
        T.check(ok && m.allExited(), "every tile copied, woke and called the overlay: " + m.haltSummary());
        long cyc = -1; boolean same = true;
        for (Tile p : m.c.tiles) {
            boolean match = true; for (int i = 0; i < N; i++) if (p.core.peekLocal(DST + 4 * i) != ov[i]) match = false;
            T.check(match, "tile " + p.idx + ": 1,024 words landed in instruction memory");
            T.eq(2L * p.idx + 0x123, m.word(p.idx, im, "res", 0), "tile " + p.idx + ": the copied code ran");
            T.eq(1, p.core.wfiLog.size(), "tile " + p.idx + ": one WFI");
            if (p.core.wfiLog.size() == 1) {
                T.eq(TileControl.WAKE_EVENT, p.core.wfiLog.get(0)[2], "tile " + p.idx + ": woken by counters done");
                long c = p.core.wfiLog.get(0)[1] - p.ag.startedAt + 1;
                if (cyc < 0) cyc = c; else if (c != cyc) same = false;
            }
        }
        T.check(same, "the same copy time on every tile");
        noViolations(m.c, "block copy");
        out(String.format("block copy [meas: CtrlTest block copy]: 1,024 words (256 128-bit mode-6 packets) big SRAM -> instruction memory by the 6.5 sequence, from the start store to the first instruction after WFI: %d cycles (spec 4.2: about 256 plus the stream's start; v1.0's 512 64-bit packets took 527)", cyc));
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
            T.check(ok && m.allExited(), set[0] + ": every tile ran a2a to its exit: " + m.haltSummary());
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
        T.check(ok && m.allExited(), "every tile ran hello to its exit: " + m.haltSummary());
        for (Tile p : m.c.tiles) {
            int i = p.idx;
            T.eq(i, m.word(i, im, "result", 0), "tile " + i + ": tile index from PARAMS");
            T.eq(0xC0FFEE00 + i, m.word(i, im, "result", 1), "tile " + i + ": .rodata constant + index");
            T.eq(1 << 20, m.word(i, im, "result", 2), "tile " + i + ": big-SRAM size from PARAMS");
            T.eq(1, m.word(i, im, "result", 3), "tile " + i + ": PARAMS: peripheral 1 present");
            T.eq(7, m.exitValue(i), "tile " + i + ": main's return value through SIM_EXIT");
        }
        noViolations(m.c, "boot");
        long last = 0; for (Tile p : m.c.tiles) last = Math.max(last, p.core.haltPc >= 0 ? 0 : 0);
        int words = Mach.loader().length + im.code.length + im.data.length;
        out(String.format("boot [meas: CtrlTest boot] (PHASE-2 INTERIM: the resident loader): loader %d words + hello %d code + %d data words = %d 128-bit mode-0 packets and 2 command writes from C&C; all 64 READY at cycle %d, RUN raised at %d, every tile at main's exit by %d",
                Mach.loader().length, im.code.length, im.data.length, (Mach.loader().length + 3) / 4 + (im.code.length + 3) / 4 + (im.data.length + 3) / 4, m.c.cc.lastAllReadyAt, m.runAt, m.c.cycle));
    }
}
