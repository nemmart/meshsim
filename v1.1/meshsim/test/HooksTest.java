import java.util.ArrayList;
import java.util.List;

/**
 * The 26n register map and the tile hooks a Java test can drive without a tile image (the C side is CtrlTest's and
 * CondTest's):
 *  - the map (8.1): 40 offsets 0x800-0x89C, every family contiguous; 0x8A0-0xFFC and the window's 0x3600-0x3FFF are bus
 *    errors; the request port's shared offsets decode stores and loads separately; write-only registers read 0;
 *  - TILE_PULSE [2] (PERIPH_RUN) on a tile with no peripheral and [3] (GO, unwired) are ignored and flagged;
 *  - the send port (8.4): ROUTER_SET_W0-W2 below the sending word, ROUTER_SEND_32/_64/_128 supplying the top word, words
 *    beyond the size 0, W0-W2 cleared at every send, addr +1/+2/+4 for modes 3, 6 and 9 and +1 for mode 7;
 *  - the empty slot (4.17.7): mode 9 a counted bad peripheral write; the lines and PERIPH_STATUS_0-7 read 0;
 *  - the unwired UART (4.20): no-ops, TX printed to the console.
 * Usage: java HooksTest [shuffle seed | -1]
 */
public final class HooksTest {
    static long shuffle = -1;

    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        pulses();
        busErrors();
        sendPort();
        emptySlot();
        uart();
        loadLatency(2); loadLatency(1);
        System.exit(T.done("HooksTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    /** A specified machine (controllers on, TILE_STATUS computed) whose tiles all sit in a WFI loop at the loader. */
    static Net machine() { return machine(0); }
    /** periphType 0: an empty slot on every tile (these hooks test the tile's side); 1: the replayer. */
    static Net machine(int periphType) {
        Config k = Config.machine(); k.shuffleSeed = shuffle; k.periphType = periphType;
        Net n = new Net(k);
        for (Tile p : n.c.tiles) { p.core.pokeLocal(Registers.LOADER_BASE, Controller.WFI); p.core.pokeLocal(Registers.LOADER_BASE + 4, RiscvTest.jal(0, -4)); }
        n.c.cc.then((cc, t) -> { cc.storeRun(t, true); return true; });
        n.c.run(10);
        return n;
    }
    static Integer load(Net n, Tile p, int addr) { return p.core.load(n.c.cycle + 1, addr, 4); }
    static int status(Tile p, Net n) { return load(n, p, Registers.TILE_STATUS.addr()); }
    static void store(Net n, Tile p, int addr, int v) { String e = p.core.store(n.c.cycle + 1, addr, v, -1); if (e != null) throw new IllegalStateException(e); n.c.step(); }

    // ================================================================ TILE_PULSE [2] and [3]
    static void pulses() {
        T.group("TILE_PULSE");
        Net n = machine(); Tile p = n.c.tiles[7];
        T.eq(0xFFFFF808L, Registers.TILE_PULSE.addr() & 0xFFFFFFFFL, "TILE_PULSE at 0x808");
        long v0 = p.ctl.violations; boolean done0 = p.ctl.done;
        store(n, p, Registers.TILE_PULSE.addr(), 1 << 2);
        n.c.run(5);
        T.eq(v0 + 1, p.ctl.violations, "[2], PERIPH_RUN on a type-0 tile: flagged");
        T.check(p.ctl.lastViolation.contains("PERIPH_RUN"), "the flag names PERIPH_RUN: " + p.ctl.lastViolation);
        T.check(!p.ag.running && p.ag.startedAt < 0 && p.ctl.done == done0, "[2] does nothing else: no run started, done unchanged");
        T.eq(0, Registers.ST_PERIPH_RUNNING.get(status(p, n)), "periph_running stays 0");
        store(n, p, Registers.TILE_PULSE.addr(), 1 << 3);
        n.c.run(5);
        T.eq(v0 + 2, p.ctl.violations, "[3], GO on a tile whose pulse isn't wired: flagged");
        T.check(p.ctl.lastViolation.contains("GO"), "the flag names GO");
        // [1] with [2]: start still acts (an empty run), [2] still flagged
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 0, 0, 0, 0, 1));
        store(n, p, Registers.TILE_PULSE.addr(), (1 << 1) | (1 << 2));
        n.c.run(5);
        T.check(p.ag.startedAt >= 0 && p.ag.done, "start in the same store acts (a count-0 run ends at once)");
        T.eq(v0 + 3, p.ctl.violations, "and [2] is flagged again");
        T.eq(0, p.ctl.conditions, "an ignored pulse sets no condition (4.13)");
        T.eq(0, (int) load(n, p, Registers.TILE_PULSE.addr()), "TILE_PULSE reads 0");
    }

    // ================================================================ bus errors and the decode
    static void busErrors() {
        T.group("register map and window");
        Net n = machine(); Tile p = n.c.tiles[2]; Controller core = p.core;
        String[] names = { "TILE_STATUS", "TILE_CONDITIONS", "TILE_PULSE", "ROUTER_SET_HDR", "ROUTER_SET_W0", "ROUTER_SET_W1", "ROUTER_SET_W2",
                "ROUTER_SEND_32", "ROUTER_SEND_64", "ROUTER_SEND_128", "DMA_CONFIG", "DMA_RECORD_BASE_ADDRESS", "DMA_SRAM_REMAINING", "DMA_CM_REMAINING",
                "DMA_REPLY_REMAINING", "DMA_PERIPH_REMAINING", "SRAM_RD_ADDR_QUEUE", "SRAM_RD_DEST_ADDR", "WAKE_EVENTS", "WAKE_PEEK", "WAKE_MASK",
                "WAKE_REQUIRED", "REQUEST_SET_W0", "REQUEST_SET_W1", "REQUEST_SET_W2", "REQUEST_SEND", "SENDER_SEND", "SENDER_ADDR",
                "PERIPH_STATUS_0", "PERIPH_STATUS_1", "PERIPH_STATUS_2", "PERIPH_STATUS_3", "PERIPH_STATUS_4", "PERIPH_STATUS_5", "PERIPH_STATUS_6",
                "PERIPH_STATUS_7", "UART_TX", "UART_RX", "UART_STATUS", "UART_DIVISOR" };
        T.eq(40, names.length, "40 offsets (8.1, part E)");
        for (int i = 0; i < names.length; i++) {
            Registers.Reg r = Registers.byName(names[i]);
            T.check(r != null && r.addr() == 0xFFFFF800 + 4 * i, names[i] + " at offset 0x" + Integer.toHexString(0x800 + 4 * i));
        }
        String[] loads = { "REQUEST_GET_W0", "REQUEST_GET_W1", "REQUEST_GET_W2", "REQUEST_RECEIVE" };
        for (int i = 0; i < 4; i++) {
            int a = 0xFFFFF858 + 4 * i;
            T.check(Registers.loadAt(a) == Registers.byName(loads[i]) && Registers.storeAt(a) == Registers.byName(names[22 + i]),
                    "offset 0x" + Integer.toHexString(0x858 + 4 * i) + ": a store reaches " + names[22 + i] + ", a load " + loads[i]);
        }
        for (int off : new int[] { 0x8A0, 0x8A4, 0x900, 0xFFC }) {
            int a = 0xFFFFF000 | off;
            T.check(Registers.at(a) == null, "offset 0x" + Integer.toHexString(off) + " unmapped (8.1: no holes, unmapped from 0x8A0)");
            T.check(core.load(n.c.cycle + 1, a, 4) == null, "a load from 0x" + Integer.toHexString(off) + " is a bus error");
            T.check(core.store(n.c.cycle + 1, a, 0, -1) != null, "a store to 0x" + Integer.toHexString(off) + " is a bus error");
        }
        for (Registers.Reg r : new Registers.Reg[] { Registers.TILE_STATUS, Registers.WAKE_EVENTS, Registers.WAKE_PEEK, Registers.PERIPH_STATUS[3], Registers.UART_RX })
            T.check(core.store(n.c.cycle + 1, r.addr(), 0, -1) != null, "a store to the read-only " + r.name + " is a bus error");
        for (Registers.Reg r : new Registers.Reg[] { Registers.SENDER_ADDR, Registers.SENDER_SEND, Registers.UART_TX, Registers.DMA_CONFIG, Registers.ROUTER_SET_W2 })
            T.eq(0, (int) load(n, p, r.addr()), "the write-only " + r.name + " reads 0");
        for (int a : new int[] { 0x3600, 0x37FC, 0x3FFC }) {
            T.check(core.load(n.c.cycle + 1, a, 4) == null, "a load from the window's 0x" + Integer.toHexString(a) + " is a bus error");
            T.check(core.store(n.c.cycle + 1, a, 0, -1) != null, "a store to the window's 0x" + Integer.toHexString(a) + " is a bus error");
        }
        for (int a : new int[] { 0x2800, 0x2C00, 0x3000, 0x3400 })
            T.check(core.load(n.c.cycle + 1, a, 4) == null, "a load from 0x" + Integer.toHexString(a) + " (write-only table) is a bus error");
        store(n, p, 0x2800 + 4 * 255, 0x12345); store(n, p, 0x2C00 + 4 * 3, 0x2ABCDE); store(n, p, 0x3000 + 4 * 7, 0x7777);
        store(n, p, 0x3400, 0x111); store(n, p, 0x35FC, 0x222);
        T.eq(0x12345, p.ts.peek(TableStore.STREAM_SRC + 255), "0x2BFC: STREAM_SRC[255]");
        T.eq(0x2ABCDE, p.ts.peek(TableStore.STREAM_DEST + 3), "0x2C0C: STREAM_DEST[3]");
        T.eq(0x7777, p.ts.peek(TableStore.STREAM_COUNT + 7), "0x301C: STREAM_COUNT[7]");
        T.eq(0x111, p.ts.peek(TableStore.GROUP), "0x3400: GROUP[0] low");
        T.eq(0x222, p.ts.peek(TableStore.GROUP + 127), "0x35FC: GROUP[63] high");
        T.check(p.ts.peek(TableStore.STREAM_SRC) == 0 && p.ts.peek(TableStore.STREAM_COUNT + 255) == 0, "no stray writes");
        int st = status(p, n);
        T.eq(2, Registers.ST_ID.get(st), "TILE_STATUS [26:20]: the endpoint id, tile 2");
        T.eq(15, Registers.ST_SIZE.get(st), "TILE_STATUS [31:27]: 4 MB = 16 units of 256 KB - 1");
        T.eq(0, Registers.ST_PERIPH_TYPE.get(st), "TILE_STATUS [15:12]: peripheral type 0 (this machine's slots are empty)");
        T.eq(1, Registers.ST_PERIPH_TYPE.get(status(machine(1).c.tiles[2], machine(1))), "and 1 with the replayer (the default on every mesh tile, 4.17.1)");
        T.eq(0, st & (0xF << 16), "TILE_STATUS [19:16] spare, read 0");
    }

    // ================================================================ the send port (8.4)
    static void sendPort() {
        T.group("send port");
        Net n = machine(); Tile s = n.c.tiles[6], r = n.c.tiles[7];
        r.dir.log = new ArrayList<>();
        int HDR = Registers.ROUTER_SET_HDR.addr(), W0 = Registers.ROUTER_SET_W0.addr(), W1 = Registers.ROUTER_SET_W1.addr(), W2 = Registers.ROUTER_SET_W2.addr();
        int S32 = Registers.ROUTER_SEND_32.addr(), S64 = Registers.ROUTER_SEND_64.addr(), S128 = Registers.ROUTER_SEND_128.addr();
        // mode 9: +4, +2, +1 by size (on a type-0 tile the packets are bad peripheral writes; this checks what is sent)
        store(n, s, HDR, Registers.H_DST.place(7) | Registers.H_MODE.place(LinkWord.M_PERIPH) | Registers.H_ADDR.place(0x10));
        store(n, s, W0, 0xA0); store(n, s, W1, 0xA1); store(n, s, W2, 0xA2); store(n, s, S128, 0xA3);
        T.eq(0x14, Registers.H_ADDR.get(s.ctl.setHdr), "mode 9, ROUTER_SEND_128: addr + 4");
        T.check(s.ctl.setW[0] == 0 && s.ctl.setW[1] == 0 && s.ctl.setW[2] == 0, "W0-W2 cleared by the send");
        store(n, s, W0, 0xB0); store(n, s, W1, 0xB1); store(n, s, S64, 0xB2);
        T.eq(0x16, Registers.H_ADDR.get(s.ctl.setHdr), "mode 9, ROUTER_SEND_64: addr + 2");
        T.check(s.ctl.setW[1] == 0, "W1 cleared by a 64-bit send too");
        store(n, s, W0, 0xC0); store(n, s, W2, 0xC2); store(n, s, S32, 0xC3);
        T.eq(0x17, Registers.H_ADDR.get(s.ctl.setHdr), "mode 9, ROUTER_SEND_32: addr + 1");
        n.c.run(40);
        List<long[]> l = r.dir.log;
        T.eq(3, l.size(), "three mode-9 packets at tile 7");
        if (l.size() == 3) {
            T.check(LinkWord.size(l.get(0)[2]) == LinkWord.SIZE_128 && l.get(0)[1] == (0xA1L << 32 | 0xA0) && l.get(0)[3] == (0xA3L << 32 | 0xA2) && LinkWord.addr(l.get(0)[2]) == 0x10,
                    "ROUTER_SEND_128: size 128, payload {data, W2, W1, W0}, addr 0x10");
            T.check(LinkWord.size(l.get(1)[2]) == LinkWord.SIZE_64 && l.get(1)[1] == (0xB2L << 32 | 0xB0) && l.get(1)[3] == 0 && LinkWord.addr(l.get(1)[2]) == 0x14,
                    "ROUTER_SEND_64: size 64, payload {data, W0}, W1 not sent, addr 0x14");
            T.check(LinkWord.size(l.get(2)[2]) == LinkWord.SIZE_32 && l.get(2)[1] == 0xC3 && l.get(2)[3] == 0 && LinkWord.addr(l.get(2)[2]) == 0x16,
                    "ROUTER_SEND_32: size 32, payload the data only (W0, W2 below the sending word go unused), addr 0x16");
        }
        T.eq(3L, r.slot.badWrites, "on a type-0 tile they are bad peripheral writes (4.17.7)");
        HooksTest.halt(n);
        n = machine(); s = n.c.tiles[6]; r = n.c.tiles[7];
        // mode 6: +4/+2/+1 and the words land; mode 7: +1; mode 3: +4/+2/+1; mode 8 and mode 2: unchanged
        r.dir.remaining[TrafficDirector.CM] = 3; r.dir.remaining[TrafficDirector.REPLY] = 1; r.dir.remaining[TrafficDirector.SRAM] = 2; r.setPtr(6, 0x30000);
        store(n, s, HDR, Registers.H_DST.place(7) | Registers.H_MODE.place(LinkWord.M_CMEM) | Registers.H_ADDR.place(0x900));
        store(n, s, W0, 1); store(n, s, W1, 2); store(n, s, W2, 3); store(n, s, S128, 4);
        store(n, s, W0, 5); store(n, s, S64, 6);
        store(n, s, S32, 7);
        T.eq(0x907, Registers.H_ADDR.get(s.ctl.setHdr), "mode 6: + 4, + 2, + 1");
        store(n, s, HDR, Registers.H_DST.place(7) | Registers.H_MODE.place(LinkWord.M_CMD) | Registers.H_ADDR.place(5));
        store(n, s, S32, 0x55);
        T.eq(6, Registers.H_ADDR.get(s.ctl.setHdr), "mode 7: + 1 (the next command slot)");
        store(n, s, HDR, Registers.H_DST.place(7) | Registers.H_MODE.place(LinkWord.M_REPLY) | Registers.H_ADDR.place(0));
        store(n, s, W0, 0x99); store(n, s, S64, 0x77);
        T.eq(0, Registers.H_ADDR.get(s.ctl.setHdr), "mode 8: addr unused and unchanged");
        store(n, s, HDR, Registers.H_DST.place(7) | Registers.H_MODE.place(LinkWord.M_EXPLICIT) | Registers.H_ADDR.place(0x20000));
        store(n, s, W0, 0x31); store(n, s, S64, 0x32);
        T.eq(0x20002, Registers.H_ADDR.get(s.ctl.setHdr), "mode 3: + 2 after a 64-bit send");
        store(n, s, HDR, Registers.H_DST.place(7) | Registers.H_MODE.place(LinkWord.M_SRC) | Registers.H_ADDR.place(0));
        store(n, s, W0, 0x8); store(n, s, W1, 0x9); store(n, s, W2, 0xA); store(n, s, S128, 0xB);
        T.eq(0, Registers.H_ADDR.get(s.ctl.setHdr), "mode 2: addr unchanged");
        n.c.run(60);
        boolean words = true; for (int i = 0; i < 7; i++) if (r.cmem.dmem.peek(0x100 + i) != i + 1) words = false;
        T.check(words, "mode 6 at 128, 64, 32 bits: seven consecutive words at 0x900 (" + r.cmem.dmem.peek(0x100) + ", ...)");
        T.eq(0x55, r.cmem.dmem.peek(0x7F5), "mode 7: command slot 5");
        T.eq(0x99, r.cmem.dmem.peek(0x6F0 + 12), "mode 8 from src 6: reply word 12 = W0");
        T.eq(0x77, r.cmem.dmem.peek(0x6F0 + 13), "mode 8 from src 6: reply word 13 = the sending word");
        T.check(r.sram.peek(0x20000) == 0x31 && r.sram.peek(0x20001) == 0x32, "mode 3: 64 bits at its explicit addr");
        T.check(r.sram.peek(0x30000) == 8 && r.sram.peek(0x30001) == 9 && r.sram.peek(0x30002) == 0xA && r.sram.peek(0x30003) == 0xB, "mode 2 at 128 bits: one row at PTR[6]");
        T.eq(0x30004, r.ptr(6), "PTR[6] += 4");
        T.eq(2L, s.ctl.sendsBySize[LinkWord.SIZE_128], "two 128-bit sends (modes 6 and 2)");
        T.eq(0, r.ctl.conditions | s.ctl.conditions, "no conditions");
        T.eq(0, r.dir.remaining[0] + r.dir.remaining[1] + r.dir.remaining[2], "every counted packet counted once");
    }
    static void halt(Net n) {}

    // ================================================================ the empty slot (4.17.7)
    static void emptySlot() {
        T.group("empty slot");
        Net n = machine(); Tile p = n.c.tiles[12];
        for (int i = 0; i < 8; i++) T.eq(0, (int) load(n, p, Registers.PERIPH_STATUS[i].addr()), "PERIPH_STATUS_" + i + " reads 0");
        T.eq(0, load(n, p, Registers.WAKE_PEEK.addr()) & Registers.WAKE_PERIPH_LINES, "the service lines read 0");
        p.dir.remaining[TrafficDirector.PERIPH] = 2;
        n.at(3, n.c.cycle + 2, 12, LinkWord.M_PERIPH, 0x400, LinkWord.SIZE_128, 1, 2);
        n.c.cc.send(12, LinkWord.M_PERIPH, 0x404, LinkWord.SIZE_128, 3, 4);           // from C&C too (4.17.5: any source)
        n.runAll(200);
        T.eq(2L, p.slot.badWrites, "two mode-9 packets: bad peripheral writes");
        T.eq(0, p.dir.remaining[TrafficDirector.PERIPH], "both counted in DMA_PERIPH_REMAINING at once");
        T.check((p.ctl.conditions & 1 << Conditions.BAD_PERIPH_WRITE) != 0, "condition 11");
        T.eq(0, (int) load(n, p, Registers.DMA_PERIPH_REMAINING.addr()), "DMA_PERIPH_REMAINING reads 0");
    }

    // ================================================================ the unwired UART (4.20)
    static void uart() {
        T.group("UART (unwired)");
        Net n = machine(); Tile p = n.c.tiles[5];
        for (char ch : "hi\n".toCharArray()) store(n, p, Registers.UART_TX.addr(), ch);
        T.eqo("hi\n", p.uart.console.toString(), "UART_TX bytes printed to the tile's console");
        T.eq(0, (int) load(n, p, Registers.UART_RX.addr()), "UART_RX: valid = 0");
        T.eq(0, (int) load(n, p, Registers.UART_STATUS.addr()), "UART_STATUS: empty FIFOs, no errors");
        store(n, p, Registers.UART_DIVISOR.addr(), 100);
        T.eq(Uart.RESET_DIVISOR, (int) load(n, p, Registers.UART_DIVISOR.addr()), "UART_DIVISOR: writes ignored, reads its reset value (434 at 25 MHz)");
        int ev = load(n, p, Registers.WAKE_PEEK.addr());
        T.eq(1 << Registers.EV_UART_TX, ev & (3 << Registers.EV_UART_RX), "TX not full always, RX not empty never");
        T.eq(0, p.ctl.conditions, "no conditions");
    }

    // ================================================================ register-load latency and the sampling rule (4.11)
    static int lw(int rd, int off) { return (off & 0xFFF) << 20 | 0 << 15 | 2 << 12 | rd << 7 | 0x03; }
    static void loadLatency(int L) {
        T.group("register-load latency " + L);
        Config k = Config.machine(); k.shuffleSeed = shuffle; k.periphType = 0; k.regLoadLatency = L;
        Net n = new Net(k); Tile p = n.c.tiles[3];
        for (Tile q : n.c.tiles) { q.core.pokeLocal(Registers.LOADER_BASE, Controller.WFI); q.core.pokeLocal(Registers.LOADER_BASE + 4, RiscvTest.jal(0, -4)); }
        p.core.pokeLocal(Registers.LOADER_BASE, 0x00000013);              // nop
        p.core.pokeLocal(Registers.LOADER_BASE + 4, lw(10, 0x848));       // lw a0, WAKE_EVENTS  (issued at R + 1)
        p.core.pokeLocal(Registers.LOADER_BASE + 8, lw(11, 0x84C));       // lw a1, WAKE_PEEK
        p.core.pokeLocal(Registers.LOADER_BASE + 12, 0x00100073);         // ebreak
        n.c.cc.then((cc, t) -> { cc.storeRun(t, true); return true; });
        while (p.ctl.runEdges == 0) n.step();                            // cycle R: the run edge; the nop
        long R = n.c.cycle, issue = R + 1, last = issue + L - 1;         // the load's sample point is the end of issue + L - 2
        int c9 = 1 << Registers.EV_COUNTERS_DONE, go = 1 << Registers.EV_GO;
        p.ctl.events |= c9;                                              // pending from the end of R: before the sample point
        p.ctl.scheduleGo(last);                                          // GO seen in the load's last cycle: after its sample point
        while (n.c.cycle < last) n.step();
        T.eq(c9, p.core.x[10] & (c9 | go), "L = " + L + ": the load returns counters done (pending at its sample point), not GO (set in its last cycle)");
        T.eq(go, p.ctl.events & (c9 | go), "L = " + L + ": its side effect at the end of its last cycle cleared counters done; GO, set in that cycle, survives");
        while (n.c.cycle < last + L) n.step();
        T.eq(go, p.core.x[11] & (c9 | go), "L = " + L + ": the next load (WAKE_PEEK) sees GO, not counters done");
        T.eq(2L * (L - 1), p.core.stallRegLoad, "L = " + L + ": each register load costs " + L + " cycle(s)");
        T.eq(0L, n.c.violations, "no violations " + n.c.violationReport());
    }
}
