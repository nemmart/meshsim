import java.util.ArrayList;
import java.util.List;

/**
 * The v1.2 register map and the hooks a Java test can drive without a tile image (the C side is CtrlTest's):
 *  - the map (8.1): the block's offsets, the request port's shared offsets (stores and loads decode separately), reserved
 *    words (read 0, stores ignored, flagged), read-only stores and sub-word accesses (bus errors), write-only reads (0),
 *    the table window (only HIST loads), an absent peripheral's window and the rest of region 0xFF (bus errors);
 *  - the pulses (8.6): a store of 1 fires, 0 does nothing; PULSE_DMA clears EVENTS [10] and is ignored while a run runs;
 *  - the send port (8.4): ROUTER_HDR and ROUTER_ADDR, ROUTER_SET_W0-W2 below the sending word, words beyond the size 0,
 *    W0-W2 cleared at every send, ROUTER_ADDR + 4/8/16 at mode 0, fixed under no-increment and for modes 1-5;
 *  - a tile with no peripheral (PARAMS [2:0] = 0): mode 0 to region 0x04 is a bad packet; its window is a bus error;
 *  - the load latencies (4.11): 2 cycles for the block, 1 for the table window, and the sampling rule.
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
        loadLatency();
        System.exit(T.done("HooksTest" + (shuffle >= 0 ? " (shuffled, seed " + shuffle + ")" : "")));
    }

    /** A specified machine (controllers on, TILE_STATUS computed) whose tiles all sit in a WFI loop at the loader. */
    static Net machine() { return machine(0); }
    /** periphType 0: no peripheral on any tile (these hooks test the tile's side); 4: the replayer. */
    static Net machine(int periphType) {
        Config k = Config.machine(); k.shuffleSeed = shuffle; k.periphType = periphType;
        Net n = new Net(k);
        // v1.2: every core boots into the boot ROM and waits there (done pulsed, WFI for GO)
        settle(n);
        return n;
    }
    static Integer load(Net n, Tile p, int addr) { return p.core.load(n.c.cycle + 1, addr, 4); }
    static int status(Tile p, Net n) { return load(n, p, Registers.STATUS.addr()); }
    static void store(Net n, Tile p, int addr, int v) { String e = p.core.store(n.c.cycle + 1, addr, v, -1); if (e != null) throw new IllegalStateException(e); n.c.step(); }

    // ================================================================ the pulses (8.6)
    static void pulses() {
        T.group("pulses");
        Net n = machine(); Tile p = n.c.tiles[7];
        T.eq(0xFFFFF80CL, Registers.PULSE_DONE.addr() & 0xFFFFFFFFL, "PULSE_DONE at 0x80C"); T.eq(0xFFFFF810L, Registers.PULSE_DMA.addr() & 0xFFFFFFFFL, "PULSE_DMA at 0x810");
        long v0 = p.ctl.violations; long dones = p.ctl.dones;
        store(n, p, Registers.PULSE_DONE.addr(), 0); store(n, p, Registers.PULSE_DMA.addr(), 0);
        n.c.run(5);
        T.check(p.ctl.dones == dones && p.ag.startedAt < 0, "a store of 0 does nothing");
        T.eq(v0, p.ctl.violations, "and isn't flagged");
        // the old PULSE_GO word (0x814) is reserved: stores ignored, flagged
        store(n, p, Registers.REG_BASE + 0x14, 1);
        T.eq(v0 + 1, p.ctl.violations, "a store to 0x814 (reserved; PULSE_GO moved to C&C 1's peripheral) is flagged");
        T.eq(0, (int) load(n, p, Registers.REG_BASE + 0x14), "and the word reads 0");
        // PULSE_DMA starts the run and clears EVENTS [10]; a start while running is ignored (flagged)
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 0, 0, 0, 0, 1));
        store(n, p, Registers.PULSE_DMA.addr(), 1);
        n.c.run(10);
        T.check(p.ag.startedAt >= 0 && p.ag.done, "PULSE_DMA starts the run (a count-0 run ends at once)");
        int rd = 1 << Registers.EV_RUN_DONE;
        T.eq(rd, load(n, p, Registers.EVENTS_PEEK.addr()) & rd, "DMA run done ([10]) set when rd_done rose");
        p.setStream(0, Fields.streamSrc(0), 0, Fields.streamCount(400, Fields.SIZE_32, 1, LinkWord.M_COUNT, 0));
        p.setMasterConfig(Fields.masterConfig(Fields.RUN_STREAMS, 0, 1, 0, 0, 1));
        p.dir.remaining[1] = 400;
        store(n, p, Registers.PULSE_DMA.addr(), 1);
        T.eq(0, load(n, p, Registers.EVENTS_PEEK.addr()) & rd, "PULSE_DMA cleared [10]");
        n.c.run(10);
        long started = p.ag.startedAt; long v1 = n.c.violations;
        store(n, p, Registers.PULSE_DMA.addr(), 1); n.c.run(2);
        T.eq(started, p.ag.startedAt, "PULSE_DMA while the run runs: ignored");
        T.check(n.c.violations > v1, "and flagged");
        n.c.runUntil(() -> p.ag.done && p.dir.remaining[1] == 0, 5000);
        T.eq(0, p.ctl.conditions, "an ignored pulse sets no error (4.13)");
        T.eq(0, (int) load(n, p, Registers.PULSE_DMA.addr()), "PULSE_DMA reads 0");
    }

    // ================================================================ the map and the bus errors (8.1, 4.11)
    static void busErrors() {
        T.group("register map and window");
        Net n = machine(); Tile p = n.c.tiles[2]; Controller core = p.core;
        String[] names = { "PARAMS", "STATUS", "ERRORS", "PULSE_DONE", "PULSE_DMA", null, "EVENTS", "EVENTS_PEEK", "EVENTS_MASK", "EVENTS_REQUIRED",
                "ROUTER_HDR", "ROUTER_ADDR", "ROUTER_SET_W0", "ROUTER_SET_W1", "ROUTER_SET_W2", "ROUTER_SEND_32", "ROUTER_SEND_64", "ROUTER_SEND_128",
                "REQUEST_SET_W0", "REQUEST_SET_W1", "REQUEST_SET_W2", "REQUEST_SEND", "DMA_CONFIG", "DMA_RECORD_BASE_ADDRESS",
                "WRITE_COUNTER_1", "WRITE_COUNTER_2", "WRITE_COUNTER_3", "WRITE_COUNTER_4", "WRITE_COUNTER_5", "WRITE_COUNTER_6", "WRITE_COUNTER_7",
                "SRAM_RD_ADDR_QUEUE", "SRAM_RD_DEST_ADDR" };
        for (int i = 0; i < names.length; i++) {
            if (names[i] == null) continue;
            Registers.Reg r = Registers.byName(names[i]);
            T.check(r != null && r.addr() == 0xFFFFF800 + 4 * i, names[i] + " at offset 0x" + Integer.toHexString(0x800 + 4 * i));
        }
        String[] loads = { "REQUEST_GET_W0", "REQUEST_GET_W1", "REQUEST_GET_W2", "REQUEST_RECEIVE" };
        for (int i = 0; i < 4; i++) {
            int a = 0xFFFFF848 + 4 * i;
            T.check(Registers.loadAt(a) == Registers.byName(loads[i]) && Registers.storeAt(a) == Registers.byName(names[18 + i]),
                    "offset 0x" + Integer.toHexString(0x848 + 4 * i) + ": a store reaches " + names[18 + i] + ", a load " + loads[i]);
        }
        // reserved words: read 0, stores ignored, flagged (8.1)
        for (int off : new int[] { 0x814, 0x884, 0x900, 0x9F4 }) {
            int a = 0xFFFFF000 | off; long v0 = p.ctl.reservedAccesses;
            T.check(core.load(n.c.cycle + 1, a, 4) == 0, "offset 0x" + Integer.toHexString(off) + " reserved: reads 0");
            T.check(core.store(n.c.cycle + 1, a, 5, -1) == null, "a store there is ignored, not a bus error");
            T.eq(v0 + 2, p.ctl.reservedAccesses, "both flagged");
        }
        for (Registers.Reg r : new Registers.Reg[] { Registers.PARAMS, Registers.STATUS, Registers.ERRORS, Registers.EVENTS, Registers.EVENTS_PEEK })
            T.check(core.store(n.c.cycle + 1, r.addr(), 0, -1) != null, "a store to the read-only " + r.name + " is a bus error");
        for (Registers.Reg r : new Registers.Reg[] { Registers.PULSE_DONE, Registers.PULSE_DMA, Registers.ROUTER_SET_W0, Registers.ROUTER_SEND_128, Registers.SIM_EXIT, Registers.TRACE })
            T.eq(0, (int) load(n, p, r.addr()), "the write-only " + r.name + " reads 0");
        T.check(core.load(n.c.cycle + 1, Registers.STATUS.addr(), 2) == null, "a halfword load from a register is a bus error");
        T.check(core.store(n.c.cycle + 1, Registers.ROUTER_SET_W1.addr(), 1, 0xFF) != null, "a byte store to a register is a bus error");
        // region 0xFF outside the block, the windows and the table window; other regions
        for (int a : new int[] { 0xFFFF0000, 0xFFFF3600, 0xFFFFF7FC, 0x03000000, 0x05000000, 0x80000000 }) {
            T.check(core.load(n.c.cycle + 1, a, 4) == null, "a load from 0x" + Integer.toHexString(a) + " is a bus error");
            T.check(core.store(n.c.cycle + 1, a, 0, -1) != null, "a store to 0x" + Integer.toHexString(a) + " is a bus error");
        }
        T.check(core.store(n.c.cycle + 1, Registers.PMEM_BASE + 0x100, 0, -1) != null, "a store to program memory is a bus error");
        T.check(core.load(n.c.cycle + 1, Registers.PMEM_BASE + 0x100, 4) != null, "a load from program memory is not");
        // the windows (8.1): peripheral 1 present (the replayer), 2 and 3 absent
        Tile q = machine(4).c.tiles[2];
        T.check(q.core.load(0, Registers.windowBase(1), 4) != null, "peripheral 1's window: present");
        T.check(q.core.load(0, Registers.windowBase(2), 4) == null && q.core.store(0, Registers.windowBase(3) + 0x1FC, 0, -1) != null, "peripherals 2 and 3's windows: absent, bus errors");
        // the table window: only HIST loads; PTR, STREAM and GROUP are write-only
        for (int a : new int[] { Registers.PTR_BASE, Registers.STREAM_SRC_BASE, Registers.STREAM_DEST_BASE, Registers.STREAM_COUNT_BASE, Registers.GROUP_BASE })
            T.check(core.load(n.c.cycle + 1, a, 4) == null, "a load from 0x" + Integer.toHexString(a) + " (write-only table) is a bus error");
        store(n, p, Registers.STREAM_SRC_BASE + 4 * 255, 0x12345); store(n, p, Registers.STREAM_DEST_BASE + 4 * 3, 0x2ABCDE); store(n, p, Registers.STREAM_COUNT_BASE + 4 * 7, 0x7777);
        store(n, p, Registers.GROUP_BASE, 0x111); store(n, p, Registers.TABLE_END - 4, 0x222); store(n, p, Registers.HIST_BASE + 8, 0x1234);
        T.eq(0x12345, p.ts.peek(TableStore.STREAM_SRC + 255), "0xFFFF2BFC: STREAM_SRC[255]");
        T.eq(0x2ABCDE, p.ts.peek(TableStore.STREAM_DEST + 3), "0xFFFF2C0C: STREAM_DEST[3]");
        T.eq(0x7777, p.ts.peek(TableStore.STREAM_COUNT + 7), "0xFFFF301C: STREAM_COUNT[7]");
        T.eq(0x111, p.ts.peek(TableStore.GROUP), "0xFFFF3400: GROUP[0] low");
        T.eq(0x222, p.ts.peek(TableStore.GROUP + 127), "0xFFFF35FC: GROUP[63] high");
        n.c.run(10);
        T.eq(0x1234, (int) core.load(n.c.cycle + 1, Registers.HIST_BASE + 8, 4), "HIST[2] loads back");
        T.check(p.ts.peek(TableStore.STREAM_SRC) == 0 && p.ts.peek(TableStore.STREAM_COUNT + 255) == 0, "no stray writes");
        int par = load(n, p, Registers.PARAMS.addr());
        T.eq(2, Registers.PA_ID.get(par), "PARAMS [26:20]: the endpoint id, tile 2");
        T.eq(15, Registers.PA_SIZE.get(par), "PARAMS [31:27]: 4 MB = 16 units of 256 KB - 1");
        T.eq(0, Registers.PA_PERIPH.get(par), "PARAMS [2:0]: no peripheral on this machine");
        T.eq(1, Registers.PA_PERIPH.get(q.ctl.params()), "and 0b001 with the replayer (every mesh tile's, 4.17.1)");
        T.eq(0, par & 0x000FFFF8, "PARAMS [19:3] spare, read 0");
    }

    // ================================================================ the send port (8.4)
    static void sendPort() {
        T.group("send port");
        Net n = machine(); Tile s = n.c.tiles[6], r = n.c.tiles[7];
        r.dir.log = new ArrayList<>();
        int HDR = Registers.ROUTER_HDR.addr(), ADDR = Registers.ROUTER_ADDR.addr(), W0 = Registers.ROUTER_SET_W0.addr(), W1 = Registers.ROUTER_SET_W1.addr(), W2 = Registers.ROUTER_SET_W2.addr();
        int S32 = Registers.ROUTER_SEND_32.addr(), S64 = Registers.ROUTER_SEND_64.addr(), S128 = Registers.ROUTER_SEND_128.addr();
        int dmem = 0x400;
        r.dir.remaining[2] = 3; r.dir.remaining[3] = 2; r.dir.remaining[1] = 2; r.setPtr(6, 0x30000);
        // mode 0 into data memory: + 16, + 8, + 4
        store(n, s, HDR, Registers.HDR_DST.place(7) | Registers.HDR_MODE.place(LinkWord.M_WRITE) | Registers.HDR_CTR.place(2));
        store(n, s, ADDR, dmem);
        store(n, s, W0, 1); store(n, s, W1, 2); store(n, s, W2, 3); store(n, s, S128, 4);
        T.eq(dmem + 16, s.ctl.routerAddr, "mode 0, ROUTER_SEND_128: ROUTER_ADDR + 16");
        T.check(s.ctl.setW[0] == 0 && s.ctl.setW[1] == 0 && s.ctl.setW[2] == 0, "W0-W2 cleared by the send");
        store(n, s, W0, 5); store(n, s, W1, 0xEE); store(n, s, S64, 6);
        T.eq(dmem + 24, s.ctl.routerAddr, "ROUTER_SEND_64: + 8");
        store(n, s, W0, 0xEE); store(n, s, S32, 7);
        T.eq(dmem + 28, s.ctl.routerAddr, "ROUTER_SEND_32: + 4");
        T.eq(dmem + 28, (int) load(n, s, ADDR), "a load of ROUTER_ADDR returns the next packet's address");
        // no-increment: replies to the same address
        store(n, s, HDR, Registers.HDR_DST.place(7) | Registers.HDR_MODE.place(LinkWord.M_WRITE) | Registers.HDR_CTR.place(3) | Registers.RH_NOINC.place(1));
        store(n, s, ADDR, Registers.REPLY_BASE + 8 * 6);
        store(n, s, W0, 0x99); store(n, s, S64, 0x77);
        store(n, s, S32, 0x55);
        T.eq(Registers.REPLY_BASE + 8 * 6, s.ctl.routerAddr, "no-increment: ROUTER_ADDR stays put");
        T.eq(Registers.HDR_DST.place(7) | Registers.HDR_MODE.place(0) | Registers.HDR_CTR.place(3) | 1, (int) load(n, s, HDR), "ROUTER_HDR reads back, no-increment set until cleared");
        // modes 1-5 leave ROUTER_ADDR alone
        store(n, s, HDR, Registers.HDR_DST.place(7) | Registers.HDR_MODE.place(LinkWord.M_SRC) | Registers.HDR_CTR.place(1));
        store(n, s, ADDR, 0x0300ABC0);
        store(n, s, W0, 0x8); store(n, s, W1, 0x9); store(n, s, W2, 0xA); store(n, s, S128, 0xB);
        store(n, s, HDR, Registers.HDR_DST.place(7) | Registers.HDR_MODE.place(LinkWord.M_COUNT) | Registers.HDR_CTR.place(1));
        store(n, s, S32, 0);
        T.eq(0x0300ABC0, s.ctl.routerAddr, "modes 1 and 5: ROUTER_ADDR unchanged");
        n.c.run(60);
        List<long[]> l = r.dir.log;
        T.eq(7, l.size(), "seven packets at tile 7");
        if (l.size() == 7) {
            T.check(LinkWord.size(l.get(0)[2]) == LinkWord.SIZE_128 && l.get(0)[1] == (2L << 32 | 1) && l.get(0)[3] == (4L << 32 | 3) && LinkWord.addr(l.get(0)[2]) == dmem
                    && LinkWord.counter(l.get(0)[2]) == 2 && LinkWord.mode(l.get(0)[2]) == 0, "ROUTER_SEND_128: size 128, payload {data, W2, W1, W0}, its address, counter and mode");
            T.check(LinkWord.size(l.get(1)[2]) == LinkWord.SIZE_64 && l.get(1)[1] == (6L << 32 | 5) && l.get(1)[3] == 0 && LinkWord.addr(l.get(1)[2]) == dmem + 16,
                    "ROUTER_SEND_64: size 64, payload {data, W0}, W1 not sent");
            T.check(LinkWord.size(l.get(2)[2]) == LinkWord.SIZE_32 && l.get(2)[1] == 7 && l.get(2)[3] == 0 && LinkWord.addr(l.get(2)[2]) == dmem + 24,
                    "ROUTER_SEND_32: size 32, the data only");
            T.eq(Registers.REPLY_BASE + 48, LinkWord.addr(l.get(3)[2]), "the first reply at REPLY + 8 x 6"); T.eq(Registers.REPLY_BASE + 48, LinkWord.addr(l.get(4)[2]), "the second at the same address");
        }
        boolean words = true; for (int i = 0; i < 7; i++) if (r.cmem.dmem.peek(0x100 + i) != i + 1) words = false;
        T.check(words, "mode 0 at 128, 64, 32 bits: seven consecutive words at 0x400");
        T.eq(0x55, r.cmem.dmem.peek(ControllerMemories.REPLY_DWORD + 12), "the 32-bit reply over the 64-bit one: word 0");
        T.eq(0x77, r.cmem.dmem.peek(ControllerMemories.REPLY_DWORD + 13), "word 1 from the 64-bit reply");
        T.check(r.sram.peek(0x30000) == 8 && r.sram.peek(0x30001) == 9 && r.sram.peek(0x30002) == 0xA && r.sram.peek(0x30003) == 0xB, "mode 1 at 128 bits: one row at PTR[6]");
        T.eq(0x30004, r.ptr(6), "PTR[6] += 4");
        T.eq(2L, s.ctl.sendsBySize[LinkWord.SIZE_128], "two 128-bit sends (modes 0 and 1)");
        T.eq(0, r.ctl.conditions | s.ctl.conditions, "no errors");
        T.eq(0, r.dir.remaining[1] + r.dir.remaining[2] + r.dir.remaining[3], "every counted packet counted once");
        T.eq(0L, n.c.violations, "no violations " + n.c.violationReport());
    }

    // ================================================================ a tile with no peripheral
    static void emptySlot() {
        T.group("no peripheral");
        Net n = machine(); Tile p = n.c.tiles[12];
        T.eq(0, p.dir.periphPresent, "PARAMS [2:0] = 0");
        p.dir.remaining[4] = 2;
        n.at(3, n.c.cycle + 2, 12, LinkWord.M_WRITE, 4, 0x04000400, LinkWord.SIZE_128, 1, 2);
        n.c.cnc.send(n.c.cycle, 12, LinkWord.M_WRITE, 4, 0x04000410, LinkWord.SIZE_128, 3, 4);   // from C&C 1 too
        n.runAll(200);
        T.eq(2, p.dir.droppedBad, "two mode-0 packets to region 0x04: bad packets, dropped");
        T.eq(2, p.dir.remaining[4], "uncounted");
        T.check((p.ctl.conditions & 1 << Conditions.BAD_MODE_OR_SIZE) != 0, "ERRORS [1]");
        T.check(p.core.load(n.c.cycle + 1, Registers.windowBase(1), 4) == null, "peripheral 1's window: a bus error");
        p.dir.remaining[4] = 0;
    }

    // ================================================================ register-load latency and the sampling rule (4.11)
    static int lw(int rd, int off) { return (off & 0xFFF) << 20 | 0 << 15 | 2 << 12 | rd << 7 | 0x03; }
    static void loadLatency() {
        int L = 2;
        T.group("register-load latency");
        Config k = Config.machine(); k.shuffleSeed = shuffle; k.periphType = 0;
        Net n = new Net(k); Tile p = n.c.tiles[3];
        int B = Registers.PMEM_BASE;
        p.core.pokeLocal(B, 0x00000013);                                 // nop
        p.core.pokeLocal(B + 4, lw(10, 0x818));                          // lw a0, EVENTS  (issued at R + 1)
        p.core.pokeLocal(B + 8, lw(11, 0x81C));                          // lw a1, EVENTS_PEEK
        p.core.pokeLocal(B + 12, 0x0000006F);                            // j . (the end)
        n.c.cc1p.expect = 1L << 3;                                       // only tile 3 starts (6.2): its command, then GO
        n.c.cc.command(3, Registers.CMD_JUMP << 24, B).go();
        while (p.core.pc != B + 4) n.step();                             // cycle R: the ROM has jumped and the nop has run
        long s0 = p.core.stallRegLoad;
        long R = n.c.cycle, issue = R + 1, last = issue + L - 1;         // the load's sample point is the end of issue + L - 2
        int c9 = 1 << Registers.EV_COUNTERS_DONE, go = 1 << Registers.EV_GO;
        p.ctl.events |= c9;                                              // pending from the end of R: before the sample point
        p.ctl.scheduleGo(last);                                          // GO seen in the load's last cycle: after its sample point
        while (n.c.cycle < last) n.step();
        T.eq(c9, p.core.x[10] & (c9 | go), "L = " + L + ": the load returns counters done (pending at its sample point), not GO (set in its last cycle)");
        T.eq(go, p.ctl.events & (c9 | go), "L = " + L + ": its side effect at the end of its last cycle cleared counters done; GO, set in that cycle, survives");
        while (n.c.cycle < last + L) n.step();
        T.eq(go, p.core.x[11] & (c9 | go), "L = " + L + ": the next load (EVENTS_PEEK) sees GO, not counters done");
        T.eq(2L * (L - 1), p.core.stallRegLoad - s0, "L = " + L + ": each register-block load costs " + L + " cycles");
        T.eq(0L, n.c.violations, "no violations " + n.c.violationReport());
        // the table window: a HIST load takes 1 cycle (4.11)
        s0 = p.core.stallRegLoad;
        T.check(p.core.load(n.c.cycle + 1, Registers.HIST_BASE, 4) != null, "a HIST load");
        T.eq(s0, p.core.stallRegLoad, "a table-window load stalls nothing: 1 cycle");
    }

    /** Every core has reached the boot ROM's WFI (done pulsed): Java may now drive the registers without racing the ROM. */
    static void settle(Net n) {
        for (int i = 0; i < 200; i++) { boolean all = true; for (Tile p : n.c.tiles) if (n.c.cfg.coreless ? false : !p.core.inWfi) all = false; if (all) break; n.step(); }
    }
}
