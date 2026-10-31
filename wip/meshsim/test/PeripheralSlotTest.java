import java.util.List;

/**
 * The peripheral framework (spec 4.17, v1.2) from a unit's side, with a test peripheral ({@link TestPeripheral}) and with
 * the replayer (type 4, every mesh tile's peripheral 1):
 *  - presence: PARAMS [2:0]; an absent peripheral's window is a bus error, a packet to its region a bad packet;
 *  - the standard window (4.17.4): PARAMS, STATUS (run, DMA), ERRORS, PULSE_RUN (a run while running ignored and
 *    flagged), reserved and undefined words (read 0, stores ignored, flagged), a defined read-only word (a bus error);
 *  - events (4.17.3): counted with their depths (a credit posted to a full count flagged), live bits, EVENTS_MASK 0 at
 *    reset, a consuming load takes one count from each set bit under the mask, EVENTS_PEEK takes nothing; the controller's
 *    EVENTS [13] live while (events & mask) != 0; its ERRORS [11] live while the peripheral's ERRORS != 0;
 *  - region writes (4.17.5): mode 0 at a byte offset, landing rp_latency after the director, counted at K;
 *  - the DMA (4.17.9): 128-bit packets, then 64 and 32 for the remainder, DMA_ADDR advancing, STATUS [1], DMA done, a
 *    count of 0, PULSE_DMA while busy and a missing output queue ignored and flagged;
 *  - the replayer through its window: type 4, its words 32-34, its credits after PULSE_RUN;
 *  - C&C 1's peripheral (8.14, 4.20): READY_LINES and READY_EXPECT, all tiles READY, PULSE_GO, ERROR_LINES and the
 *    unit-error bit, the watchdog and the toast pin, LED, the UART's FIFOs, timing, line errors and events.
 * Usage: java PeripheralSlotTest [shuffle seed | -1]
 */
public final class PeripheralSlotTest {
    static long shuffle = -1;
    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        window();
        events();
        landing();
        dma();
        replayer();
        cc1();
        System.exit(T.done("PeripheralSlotTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    /** A machine (tiles in a WFI loop at the loader) whose tile `tp` holds a test peripheral as peripheral 1. */
    static Net machine(int tp, TestPeripheral[] out) {
        Config k = Config.machine(); k.shuffleSeed = shuffle;
        k.testPeripheral = tile -> tile == tp ? (out[0] = new TestPeripheral()) : null;
        Net n = new Net(k);
        // v1.2: every core boots into the boot ROM and waits there (done pulsed, WFI for GO)
        settle(n);
        return n;
    }
    static Integer load(Net n, Tile p, int addr) { Integer v = p.core.load(n.c.cycle + 1, addr, 4); n.step(); return v; }
    static void store(Net n, Tile p, int addr, int v) { String e = p.core.store(n.c.cycle + 1, addr, v, -1); if (e != null) throw new IllegalStateException(e); n.step(); }
    static int w(int word) { return Registers.windowBase(1) + 4 * word; }
    static final int PARAMS = 0, STATUS = 1, ERRORS = 2, PULSE_RUN = 3, PULSE_DMA = 4, EVENTS = 5, EVENTS_PEEK = 6, EVENTS_MASK = 7, DMA_HDR = 8, DMA_ADDR = 9;

    static void window() {
        T.group("the standard window");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = machine(5, tp); Tile p = n.c.tiles[5]; TestPeripheral x = tp[0];
        T.eq(1, Registers.PA_PERIPH.get(load(n, p, Registers.PARAMS.addr())), "the unit's PARAMS [2:0]: peripheral 1 present");
        T.eq(15 | 3 << 8, (int) load(n, p, w(PARAMS)), "the window's PARAMS: type 15, version 3");
        T.check(p.core.load(n.c.cycle + 1, Registers.windowBase(2), 4) == null, "peripheral 2's window: absent, a bus error");
        T.eq(0, (int) load(n, p, w(STATUS)), "STATUS: idle");
        store(n, p, w(PULSE_RUN), 0);
        T.eq(0, x.runs, "PULSE_RUN 0 does nothing");
        store(n, p, w(PULSE_RUN), 1);
        T.eq(1, (int) load(n, p, w(STATUS)) & 1, "STATUS [0]: run in progress from the end of the run store's cycle");
        long v0 = n.c.violations;
        store(n, p, w(PULSE_RUN), 1);
        T.eq(1, x.runs, "a run while running is ignored");
        T.check(n.c.violations > v0, "and flagged");
        T.eq(1, (int) load(n, p, w(32)), "word 32: runs started");
        store(n, p, w(33), 0xABC);
        T.eq(0xABC, (int) load(n, p, w(33)), "word 33 is read/write");
        T.check(p.core.store(n.c.cycle + 1, w(34), 1, -1) != null, "a store to the read-only word 34 is a bus error");
        T.check(p.core.store(n.c.cycle + 1, w(ERRORS), 1, -1) != null, "and to ERRORS");
        long r0 = p.slot.reservedAccesses;
        T.eq(0, (int) load(n, p, w(12)), "reserved standard word 12 reads 0");
        T.eq(0, (int) load(n, p, w(100)), "an undefined word reads 0");
        store(n, p, w(31), 5);
        T.eq(r0 + 3, p.slot.reservedAccesses, "each access flagged");
        T.eq(0, (int) load(n, p, w(PULSE_RUN)), "PULSE_RUN reads 0");
        for (int i = 0; i < 20; i++) n.step();
        T.eq(0, (int) load(n, p, w(STATUS)) & 1, "the run ended");
        T.eq(0, (int) load(n, p, w(ERRORS)), "ERRORS: 0");
        x.fault(); n.step(); n.step();
        T.eq(1, (int) load(n, p, w(ERRORS)), "the peripheral's ERRORS [0]");
        T.eq(1 << 11, p.ctl.errorsAt(n.c.cycle) & 7 << 11, "the unit's ERRORS [11]: live while peripheral 1's ERRORS is non-zero");
        T.check(p.ctl.errorLine(), "and the error line is up");
    }

    static void events() {
        T.group("events");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = machine(6, tp); Tile p = n.c.tiles[6]; TestPeripheral x = tp[0];
        int ev13 = 1 << Registers.EV_PERIPH_1;
        T.eq(0, (int) load(n, p, w(EVENTS_MASK)), "EVENTS_MASK: 0 at reset");
        x.post(n.c.cycle); x.post(n.c.cycle); x.credit(n.c.cycle); x.credit(n.c.cycle); x.level(true);
        n.step();
        T.eq(0b11100, (int) load(n, p, w(EVENTS_PEEK)), "EVENTS_PEEK: the event, the credit and the live bit");
        T.eq(1, x.ev.count(2), "an ordinary event has depth 1: posted twice, counted once");
        T.eq(2, x.ev.count(3), "a credit has depth 2");
        x.credit(n.c.cycle); n.step();
        T.eq(1L, x.ev.fullPosts, "a credit posted to a full count is flagged");
        T.eq(0, load(n, p, Registers.EVENTS_PEEK.addr()) & ev13, "the unit's EVENTS [13]: down while the mask is 0");
        store(n, p, w(EVENTS_MASK), 0b11100);
        n.step();
        T.eq(ev13, load(n, p, Registers.EVENTS_PEEK.addr()) & ev13, "EVENTS [13] up: (events & mask) != 0");
        T.eq(0b11100, (int) load(n, p, w(EVENTS)), "a load of EVENTS returns every bit");
        n.step();
        T.eq(0, x.ev.count(2), "and consumes the event");
        T.eq(1, x.ev.count(3), "and one credit of two");
        T.eq(0b11000, (int) load(n, p, w(EVENTS_PEEK)), "the live bit is never consumed");
        x.level(false); load(n, p, w(EVENTS)); n.step();
        T.eq(0, (int) load(n, p, w(EVENTS_PEEK)), "the second credit taken");
        T.eq(0, load(n, p, Registers.EVENTS_PEEK.addr()) & ev13, "EVENTS [13] falls");
        T.eq(0, p.ctl.conditions, "no unit errors");
    }

    static void landing() {
        T.group("region writes");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = machine(7, tp); Tile p = n.c.tiles[7]; TestPeripheral x = tp[0];
        p.dir.remaining[4] = 2; p.dir.log = new java.util.ArrayList<>();
        n.at(3, n.c.cycle + 2, 7, LinkWord.M_WRITE, 4, 0x04000400, LinkWord.SIZE_128, 1, 2);
        n.c.cnc.send(n.c.cycle, 7, LinkWord.M_WRITE, 4, 0x04123450, LinkWord.SIZE_64, 3, 0);   // from C&C 1 too: any source
        n.runAll(300);
        T.eq(2, x.accepts.size(), "both handed to the peripheral");
        boolean timed = true; for (int i = 0; i < 2; i++) if (x.accepts.get(i)[0] != p.dir.log.get(i)[0] + Peripheral.RP_LATENCY) timed = false;
        T.check(timed, "each lands rp_latency after the director accepts it");
        long[] a = x.accepts.get(0)[1] == 3 ? x.accepts.get(0) : x.accepts.get(1), c = x.accepts.get(0)[1] == 3 ? x.accepts.get(1) : x.accepts.get(0);
        T.eq(0x400, (int) a[2], "tile 3's at its byte offset in the region, 0x400");
        T.eq(0x123450, (int) c[2], "C&C 1's at 0x123450");
        T.eq(64, (int) c[1], "its src 64");
        T.eq(0, p.dir.remaining[4], "both counted at K");
        Net m = machine(8, tp); Tile q = m.c.tiles[9];
        m.at(3, m.c.cycle + 2, 9, LinkWord.M_WRITE, 0, 0x05000000, LinkWord.SIZE_32, 1, 0);
        m.runAll(300);
        T.eq(1, q.dir.droppedBad, "a packet for absent peripheral 2's region: a bad packet");
    }

    static void dma() {
        T.group("the DMA");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = machine(10, tp); Tile p = n.c.tiles[10];
        List<long[]> out = p.slot.dmaOut;
        int hdr = Registers.DH_COUNT.place(7) | Registers.DH_QUEUE.place(1) | Registers.HDR_DST.place(65) | Registers.HDR_MODE.place(0) | Registers.HDR_CTR.place(5);
        store(n, p, w(DMA_HDR), hdr); store(n, p, w(DMA_ADDR), 0x400);
        store(n, p, w(EVENTS_MASK), 2);
        store(n, p, w(PULSE_DMA), 1);
        T.eq(2, (int) load(n, p, w(STATUS)) & 2, "STATUS [1]: DMA in progress");
        long v0 = n.c.violations;
        store(n, p, w(PULSE_DMA), 1);
        T.check(n.c.violations > v0 && p.slot.dma[1].ignored == 1, "PULSE_DMA while busy: ignored and flagged");
        for (int i = 0; i < 20; i++) n.step();
        T.eq(3, out.size(), "7 words: a 128-bit, a 64-bit and a 32-bit packet");
        if (out.size() == 3) {
            long[] a = out.get(0), b = out.get(1), c = out.get(2);
            T.check(a[5] == LinkWord.SIZE_128 && a[4] == 0x400 && a[6] == ((1L << 24 | 1) << 32 | (1 << 24)) && a[7] == ((1L << 24 | 3) << 32 | (1 << 24 | 2)), "128 bits at 0x400, queue 1's words 0-3, the first in [31:0]");
            T.check(b[5] == LinkWord.SIZE_64 && b[4] == 0x410, "then 64 bits at 0x410");
            T.check(c[5] == LinkWord.SIZE_32 && c[4] == 0x418, "then 32 bits at 0x418");
            T.check(a[1] == 65 && a[2] == 0 && a[3] == 5, "each stamped with dst, mode and counter");
        }
        T.eq(0x41C, (int) load(n, p, w(DMA_ADDR)), "DMA_ADDR advanced past the transfer");
        T.eq(0, (int) load(n, p, w(STATUS)) & 2, "STATUS [1] fell");
        T.eq(2, (int) load(n, p, w(EVENTS)) & 2, "DMA done, EVENTS [1]");
        n.step();
        T.eq(0, (int) load(n, p, w(EVENTS_PEEK)) & 2, "consumed by the load");
        store(n, p, w(DMA_HDR), Registers.DH_COUNT.place(0)); store(n, p, w(PULSE_DMA), 1);
        for (int i = 0; i < 4; i++) n.step();
        T.eq(2, (int) load(n, p, w(EVENTS_PEEK)) & 2, "a count of 0: DMA done still posted");
        T.eq(3, out.size(), "and nothing sent");
        store(n, p, w(DMA_HDR), Registers.DH_COUNT.place(4) | Registers.DH_QUEUE.place(3)); store(n, p, w(PULSE_DMA), 1);
        T.eq(2L, p.slot.dma[1].ignored, "a missing output queue: ignored (flagged)");
    }

    static void replayer() {
        T.group("the replayer's window");
        Config k = Config.machine(); k.shuffleSeed = shuffle;
        Net n = new Net(k); Tile p = n.c.tiles[3];
        // v1.2: every core boots into the boot ROM and waits there (done pulsed, WFI for GO)
        for (int i = 0; i < 10; i++) n.step();
        T.eq(4, (int) load(n, p, w(PARAMS)), "PARAMS: type 4, the replayer (4.17.1, D46)");
        T.eq(0, (int) load(n, p, w(STATUS)), "STATUS: idle");
        T.eq(0, (int) load(n, p, w(EVENTS_PEEK)), "no event before a pass");
        store(n, p, w(PULSE_RUN), 1);
        n.step();
        T.eq(0b111100, (int) load(n, p, w(EVENTS_PEEK)) & 0b111100, "PULSE_RUN: every queue's credit up");
        T.eq(0, (int) load(n, p, w(4)), "word 4 (no DMA): reserved, reads 0");
        T.eq(0, (int) load(n, p, w(32)), "ROWS_RUN");
    }

    static void cc1() {
        T.group("C&C 1's peripheral");
        Config k = Config.machine(); k.shuffleSeed = shuffle; k.refCycle = 1;
        Net n = new Net(k);
        Cnc1Peripheral c = n.c.cc1p;
        long t = n.c.cycle;
        // READY and GO
        c.write(t, Cnc1Peripheral.READY_EXPECT, 0b1010); c.write(t, Cnc1Peripheral.READY_EXPECT + 1, 0);
        c.readyPulse(1, t); c.readyPulse(3, t); n.step(); n.step();
        T.eq(0b1010, c.read(n.c.cycle, Cnc1Peripheral.READY_LINES), "READY_LINES_0: tiles 1 and 3 (sticky)");
        T.eq(1 << Cnc1Peripheral.EV_ALL_READY, c.read(n.c.cycle, 6) & 1 << Cnc1Peripheral.EV_ALL_READY, "all tiles READY: (READY & EXPECT) == EXPECT (live)");
        T.eq(1, c.read(t, 0), "PARAMS: type 1, Ingress/Egress with the UART (4.17.1, D46)");
        long go0 = n.c.cc.gos;
        c.write(n.c.cycle, Cnc1Peripheral.PULSE_GO, 1); n.step(); n.step();
        T.eq(go0 + 1, n.c.cc.gos, "PULSE_GO drives GO");
        T.eq(0, c.read(n.c.cycle, Cnc1Peripheral.READY_LINES), "and clears READY 0-63");
        T.eq(0, c.read(n.c.cycle, 6) & 1 << Cnc1Peripheral.EV_ALL_READY, "all tiles READY falls");
        // the error lines
        n.c.tiles[33].ctl.conditions = 1 << Conditions.DRIFT;
        for (int i = 0; i < 5; i++) n.step();
        T.eq(2, c.read(n.c.cycle, Cnc1Peripheral.ERROR_LINES + 1), "ERROR_LINES_1 bit 1: tile 33's ERRORS non-zero (live)");
        T.eq(1, c.read(n.c.cycle, 2), "ERRORS [0], the unit-error bit");
        T.check(c.toast, "the toast pin latches on a C&C 1 error");
        n.c.tiles[33].ctl.conditions = 0;
        for (int i = 0; i < 5; i++) n.step();
        T.eq(0, c.read(n.c.cycle, Cnc1Peripheral.ERROR_LINES + 1), "the line follows its unit");
        T.check(c.toast, "the toast pin stays latched (until the reset)");
        // the watchdog
        Net m = new Net(k); Cnc1Peripheral d = m.c.cc1p;
        d.write(m.c.cycle, Cnc1Peripheral.WATCHDOG, 20);
        for (int i = 0; i < 10; i++) m.step();
        d.write(m.c.cycle, Cnc1Peripheral.PULSE_GO, 1);                    // restarts it from 20
        for (int i = 0; i < 15; i++) m.step();
        T.check(!d.toast && d.read(m.c.cycle, Cnc1Peripheral.WATCHDOG) > 0, "PULSE_GO restarted the watchdog");
        for (int i = 0; i < 10; i++) m.step();
        T.eq(1 << Cnc1Peripheral.EV_WATCHDOG, d.read(m.c.cycle, 6) & 1 << Cnc1Peripheral.EV_WATCHDOG, "watchdog expired: its event");
        T.check(d.toast, "and the toast pin");
        d.write(m.c.cycle, Cnc1Peripheral.LED, 3);
        T.eq(1, d.read(m.c.cycle, Cnc1Peripheral.LED), "LED [0]");
        // the UART
        Net u = new Net(k); Cnc1Peripheral x = u.c.cc1p;
        T.eq(Cnc1Peripheral.RESET_DIVISOR, x.read(u.c.cycle, Cnc1Peripheral.UART_DIVISOR), "UART_DIVISOR resets to 434 (57,600 baud at 25 MHz)");
        x.write(u.c.cycle, Cnc1Peripheral.UART_DIVISOR, 3);              // 30 cycles a frame (refCycle 1)
        int pushed = 0; for (char ch : "hello, world!!!!!".toCharArray()) if (x.write(u.c.cycle, Cnc1Peripheral.UART_TX, ch) == 1) pushed++;
        T.eq(16, pushed, "UART_TX: a 16-byte FIFO");
        T.eq(-2, x.write(u.c.cycle, Cnc1Peripheral.UART_TX, '?'), "a store to a full TX FIFO stalls");
        T.eq(0, u.c.cc1p.events().value(u.c.cycle + 1) & 1 << Cnc1Peripheral.EV_UART_TX, "TX not full: down");
        long t0 = u.c.cycle;
        for (int i = 0; i < 16 * 30 + 5; i++) u.step();
        T.eqo("hello, world!!!!", x.serialOut.toString(), "the bytes went out in order");
        T.eq(30L, x.serialLog.get(1)[0] - x.serialLog.get(0)[0], "a frame every 10 x divisor reference cycles");
        x.rxByte(u.c.cycle, 'A', false); x.rxByte(u.c.cycle, 'B', false); x.rxByte(u.c.cycle, 'C', true);
        u.step();
        int us = x.read(u.c.cycle, Cnc1Peripheral.UART_STATUS);
        T.eq(2, us >> 8 & 0x1F, "UART_STATUS: RX occupancy 2"); T.eq(1, us >> 17 & 1, "and the framing error (the bad stop bit dropped)");
        T.eq(1 << Cnc1Peripheral.EV_UART_RX, x.read(u.c.cycle, 6) & 1 << Cnc1Peripheral.EV_UART_RX, "RX not empty (live)");
        T.eq('A' | 0x100, x.read(u.c.cycle, Cnc1Peripheral.UART_RX), "UART_RX: the oldest byte, valid");
        T.eq('B' | 0x100, x.read(u.c.cycle, Cnc1Peripheral.UART_RX), "popped: the next");
        T.eq(0, x.read(u.c.cycle, Cnc1Peripheral.UART_RX), "empty: valid = 0, nothing popped");
        x.write(u.c.cycle, Cnc1Peripheral.UART_STATUS, 1 << 17);
        T.eq(0, x.read(u.c.cycle, Cnc1Peripheral.UART_STATUS) & 1 << 17, "framing error cleared by writing 1");
        for (int i = 0; i < 20; i++) x.rxByte(u.c.cycle, i, false);
        T.eq(1 << 16, x.read(u.c.cycle, Cnc1Peripheral.UART_STATUS) & 1 << 16, "a 17th byte: RX overrun (status, not an error)");
        T.eq(0, x.read(u.c.cycle, 2) & ~1, "line errors never reach ERRORS");
    }

    /** Every core has reached the boot ROM's WFI (done pulsed): Java may now drive the registers without racing the ROM. */
    static void settle(Net n) {
        for (int i = 0; i < 200; i++) { boolean all = true; for (Tile p : n.c.tiles) if (n.c.cfg.coreless ? false : !p.core.inWfi) all = false; if (all) break; n.step(); }
    }
}
