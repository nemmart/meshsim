import java.util.ArrayList;
import java.util.List;

/**
 * The peripheral slot (spec 4.17) from the tile's side, with a test peripheral (one line of each kind, {@link
 * TestPeripheral}) and with the replayer (type 1, the default on every mesh tile):
 *  - TILE_STATUS's type [15:12] and periph_running [3] (from the end of the run store's cycle);
 *  - claims: a consuming WAKE_EVENTS load claims exactly the lines it returned under WAKE_MASK, at its sample cycle;
 *    WAKE_PEEK and a load with a line's mask bit clear claim nothing; status reads have no side effects; each kind
 *    responds (an event clears, a queue condition gives a unit, an item FIFO pops into its status register);
 *  - mode 9 lands rp_latency after the director and DMA_PERIPH_REMAINING counts it then, from any source; nothing is
 *    counted in HALT (a boot load), and a mode-9 arrival with the counter at 0 is unexpected;
 *  - setHalted on HALT, on HALT -> RUN and on a halted controller, only on a change; runCycle every cycle, HALT included;
 *  - the replayer: type 1, its write port's boot load in HALT, counting on landing, a bad peripheral write (condition 11),
 *    a run while running ignored and flagged.
 * Usage: java PeripheralSlotTest [shuffle seed | -1]
 */
public final class PeripheralSlotTest {
    static long shuffle = -1;
    public static void main(String[] args) {
        if (args.length > 0) shuffle = Long.parseLong(args[0]);
        if (shuffle >= 0) System.out.println("shuffled child order, seed " + shuffle);
        claims();
        landing();
        halt();
        replayer();
        System.exit(T.done("PeripheralSlotTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    /** A machine (tiles in a WFI loop at the loader) whose tile `tp` holds a test peripheral. */
    static Net machine(int tp, TestPeripheral[] out) {
        Config k = Config.machine(); k.shuffleSeed = shuffle;
        k.testPeripheral = tile -> tile == tp ? (out[0] = new TestPeripheral()) : null;
        Net n = new Net(k);
        for (Tile p : n.c.tiles) { p.core.pokeLocal(Registers.LOADER_BASE, Controller.WFI); p.core.pokeLocal(Registers.LOADER_BASE + 4, RiscvTest.jal(0, -4)); }
        n.c.cc.then((cc, t) -> { cc.storeRun(t, true); return true; });
        for (int i_ = 0; i_ < 10; i_++) n.step();
        return n;
    }
    static Integer load(Net n, Tile p, int addr) { Integer v = p.core.load(n.c.cycle + 1, addr, 4); n.step(); return v; }
    static void store(Net n, Tile p, int addr, int v) { String e = p.core.store(n.c.cycle + 1, addr, v, -1); if (e != null) throw new IllegalStateException(e); n.step(); }
    static final int LINES = Registers.WAKE_PERIPH_LINES, L1 = 1 << 13, L2 = 1 << 14, L3 = 1 << 15, L6 = 1 << 18;

    static void claims() {
        T.group("claims");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = machine(5, tp); Tile p = n.c.tiles[5]; TestPeripheral x = tp[0];
        T.eq(15, Registers.ST_PERIPH_TYPE.get(load(n, p, Registers.TILE_STATUS.addr())), "TILE_STATUS [15:12]: the slot's type (15, the test peripheral)");
        x.post(n.c.cycle); x.units = 2; x.items.add(0x111); x.items.add(0x222);
        n.step();
        int peek = load(n, p, Registers.WAKE_PEEK.addr());
        T.eq(L1 | L2 | L3, peek & LINES, "WAKE_PEEK: lines 1-3 up (an event, a queue condition, an item FIFO)");
        T.eq(0, x.claimLog.size(), "WAKE_PEEK claims nothing");
        T.eq(0x111, (int) load(n, p, Registers.PERIPH_STATUS[2].addr()), "PERIPH_STATUS_2 presents the first item");
        T.eq(0x111, (int) load(n, p, Registers.PERIPH_STATUS[2].addr()), "a status read has no side effect");
        int v = load(n, p, Registers.WAKE_EVENTS.addr());       // the reset mask ([8] | [9]) covers no line
        T.eq(L1 | L2 | L3, v & LINES, "WAKE_EVENTS returns every pending event, the lines included");
        T.eq(0, x.claimLog.size(), "a load with the lines' mask bits clear claims nothing");
        store(n, p, Registers.WAKE_MASK.addr(), L1 | L2 | L3 | Registers.WAKE_MASK_RESET);
        long t = n.c.cycle + 1;
        v = load(n, p, Registers.WAKE_EVENTS.addr());
        T.eq(L1 | L2 | L3, v & LINES, "a consuming load returns the lines");
        T.check(x.claimLog.size() == 1 && x.claimLog.get(0)[0] == t && x.claimLog.get(0)[1] == 7, "it claims exactly lines 1-3, at its sample cycle (L = " + n.c.cfg.regLoadLatency + ")");
        v = load(n, p, Registers.WAKE_PEEK.addr());
        T.eq(L2 | L3, v & LINES, "after one claim: the event is clear, the queue condition still up (one unit left), the FIFO still holds an item");
        T.eq(0x222, (int) load(n, p, Registers.PERIPH_STATUS[2].addr()), "the item FIFO popped into PERIPH_STATUS_2");
        load(n, p, Registers.WAKE_EVENTS.addr());
        T.eq(0, load(n, p, Registers.WAKE_PEEK.addr()) & LINES, "after a second claim every line is down");
        T.eq(2, (int) load(n, p, Registers.PERIPH_STATUS[0].addr()), "two claims taken");
        // PERIPH_RUN: periph_running from the end of the store's cycle; done is line 6 (an event)
        long ts = n.c.cycle + 1;
        store(n, p, Registers.TILE_PULSE.addr(), Registers.P_PERIPH_RUN.place(1));
        T.check(x.runCalls.size() == 1 && x.runCalls.get(0) == ts, "PERIPH_RUN calls run() at the store's cycle");
        T.eq(1, Registers.ST_PERIPH_RUNNING.get(load(n, p, Registers.TILE_STATUS.addr())), "periph_running reads 1 the next cycle");
        for (int i_ = 0; i_ < 20; i_++) n.step();
        T.eq(0, Registers.ST_PERIPH_RUNNING.get(load(n, p, Registers.TILE_STATUS.addr())), "and 0 after the run");
        T.eq(L6, load(n, p, Registers.WAKE_PEEK.addr()) & LINES, "the run's end on line 6");
        T.eq(0, p.ctl.conditions, "no conditions");
        T.eq(0L, n.c.violations, "no violations " + n.c.violationReport());
    }

    static void landing() {
        T.group("landing and counting");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = machine(9, tp); Tile p = n.c.tiles[9]; TestPeripheral x = tp[0];
        p.dir.log = new ArrayList<>();
        store(n, p, Registers.DMA_PERIPH_REMAINING.addr(), 3);
        n.at(3, n.c.cycle + 2, 9, LinkWord.M_PERIPH, 0x100, LinkWord.SIZE_128, 1, 2);     // from another tile
        n.c.cc.send(9, LinkWord.M_PERIPH, 0x104, LinkWord.SIZE_32, 3, 0);                     // from C&C
        long[] at = new long[3]; int k = 0; int prev = 3;
        for (int i = 0; i < 200 && k < 2; i++) { n.step(); int r = p.dir.remaining[TrafficDirector.PERIPH]; if (r < prev) { at[k++] = n.c.cycle; prev = r; } }
        T.eq(2, x.accepts.size(), "two mode-9 packets handed over, from tile 3 and from C&C");
        if (x.accepts.size() == 2 && k == 2) {
            T.eq(x.accepts.get(0)[0] + Peripheral.RP_LATENCY, at[0], "the first counted rp_latency (2) after the director handed it over");
            T.eq(x.accepts.get(1)[0] + Peripheral.RP_LATENCY, at[1], "the second likewise");
            T.eq(3L + 64L, x.accepts.get(0)[1] + x.accepts.get(1)[1], "from src 3 and src 64");
        }
        // the director's own count: none at acceptance
        n.at(4, n.c.cycle + 2, 9, LinkWord.M_PERIPH, 0x108, LinkWord.SIZE_64, 5, 0);
        n.runAll(100); for (int i_ = 0; i_ < 5; i_++) n.step();
        T.eq(0, p.dir.remaining[TrafficDirector.PERIPH], "three landed, three counted");
        T.eq(1 << Registers.EV_PERIPH_DONE, load(n, p, Registers.WAKE_PEEK.addr()) & (1 << Registers.EV_PERIPH_DONE), "DMA_PERIPH_REMAINING done ([3]) posted");
        n.at(4, n.c.cycle + 2, 9, LinkWord.M_PERIPH, 0x10C, LinkWord.SIZE_64, 6, 0);
        n.runAll(100); for (int i_ = 0; i_ < 5; i_++) n.step();
        T.check((p.ctl.conditions & 1 << Conditions.UNEXPECTED) != 0, "a fourth with the counter at 0: unexpected arrival");
    }

    static void halt() {
        T.group("HALT and a halted controller");
        TestPeripheral[] tp = new TestPeripheral[1];
        Net n = machine(12, tp); Tile p = n.c.tiles[12]; TestPeripheral x = tp[0];
        T.check(x.haltLog.size() == 2 && x.haltLog.get(0)[1] == 1 && x.haltLog.get(1)[1] == 0, "chip reset holds it (HALT), HALT -> RUN releases it: " + x.haltLog.size() + " calls");
        n.c.cc.halt(); for (int i_ = 0; i_ < 10; i_++) n.step();
        T.check(x.haltLog.size() == 3 && x.haltLog.get(2)[1] == 1, "entering HALT holds it");
        // a boot load in HALT lands but isn't counted
        n.c.cc.send(12, LinkWord.M_PERIPH, 0x200, LinkWord.SIZE_128, 7, 8); for (int i_ = 0; i_ < 40; i_++) n.step();
        T.eq(1, x.accepts.size(), "a mode-9 packet in HALT reaches the peripheral");
        T.eq(0, p.ctl.conditions & 1 << Conditions.UNEXPECTED, "and isn't counted (a halted tile counts nothing): no unexpected arrival");
        long rc0 = n.c.cycle; long[] seen = { 0 }; for (int i_ = 0; i_ < 5; i_++) n.step();
        n.c.cc.then((cc, t) -> { cc.storeRun(t, true); return true; }); for (int i_ = 0; i_ < 10; i_++) n.step();
        T.check(x.haltLog.size() == 4 && x.haltLog.get(3)[1] == 0, "HALT -> RUN releases it");
        // a halted controller stops it at once
        p.core.pokeLocal(Registers.LOADER_BASE + 4, 0x80002023);    // sw x0, -2048(x0): a store to TILE_STATUS, a bus error
        p.ctl.scheduleGo(n.c.cycle + 1);                            // wake the WFI (test only)
        for (int i_ = 0; i_ < 5; i_++) n.step();
        T.eq(Controller.HALT_BUSERR, p.core.halt, "the core halted on the bus error");
        T.check(x.haltLog.size() == 5 && x.haltLog.get(4)[1] == 1, "a halted controller (bus error) holds it");
        n.c.cc.halt(); for (int i_ = 0; i_ < 10; i_++) n.step();
        T.eq(5, x.haltLog.size(), "entering HALT afterwards changes nothing (calls only on a change)");
    }

    static void replayer() {
        T.group("the replayer in the slot");
        Config k = Config.machine(); k.shuffleSeed = shuffle;
        Chip c = new Chip(k);
        Tile p = c.tiles[20];
        T.check(p.slot.p instanceof Replayer, "every mesh tile holds a replayer (type 1) by default");
        // a boot load in HALT: one decode-table word, from C&C, uncounted
        c.cc.send(20, LinkWord.M_PERIPH, 0x400, LinkWord.SIZE_128, 0x0004000300020001L, 0x0008000700060005L);
        c.run(50);
        Replayer r = (Replayer) p.slot.p;
        T.check(r.decodeTable[0] == 1 && r.decodeTable[7] == 8, "the decode table took the boot load in HALT");
        T.eq(0, p.ctl.conditions, "no condition");
        // RUN; a ROM write from tile 3, counted on landing; a 32-bit write: a bad peripheral write, counted
        for (Tile q : c.tiles) { q.core.pokeLocal(Registers.LOADER_BASE, Controller.WFI); q.core.pokeLocal(Registers.LOADER_BASE + 4, RiscvTest.jal(0, -4)); }
        c.cc.then((cc, t) -> { cc.storeRun(t, true); return true; });
        c.run(10);
        T.eq(1, Registers.ST_PERIPH_TYPE.get(p.ctl.statusAt(c.cycle + 1)), "TILE_STATUS [15:12] = 1");
        p.dir.remaining[TrafficDirector.PERIPH] = 2;
        c.tiles[3].sendHold.put(c.cycle, 0x1111L, LinkWord.hdr(0, 20, LinkWord.M_PERIPH, 0x800, LinkWord.SIZE_128), 0x2222L);
        c.run(40);
        T.check(r.romLo[0] == 0x1111L && r.romHi[0] == 0x2222L, "a ROM word from tile 3 landed (mode 9 from any source)");
        c.tiles[3].sendHold.put(c.cycle, 0x3333L, LinkWord.hdr(0, 20, LinkWord.M_PERIPH, 0x804, LinkWord.SIZE_32), 0);
        c.run(40);
        T.eq(0, p.dir.remaining[TrafficDirector.PERIPH], "both counted (landed, and dropped)");
        T.eq(1 << Conditions.BAD_PERIPH_WRITE, p.ctl.conditions, "the 32-bit write is a bad peripheral write (condition 11), and only it");
        // PERIPH_RUN twice: the second while running is ignored and flagged by the replayer
        long v0 = p.ctl.violations;
        p.core.store(c.cycle + 1, Registers.TILE_PULSE.addr(), Registers.P_PERIPH_RUN.place(1), -1); c.step();
        T.check(p.slot.running(c.cycle + 1), "periph_running from the end of the run store's cycle");
        p.core.store(c.cycle + 1, Registers.TILE_PULSE.addr(), Registers.P_PERIPH_RUN.place(1), -1); c.step(); c.run(3);
        T.check(p.ctl.violations > v0, "a run while running: flagged (" + p.ctl.lastViolation + ")");
    }
}
