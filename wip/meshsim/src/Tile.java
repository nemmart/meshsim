import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * One tile (spec 4). Owns its units and every intra-tile bus; units hold bus ends only. M1 built the network side:
 * the router, the injection point, the traffic director and port B of the controller memories. M2 adds the big SRAM
 * with its area queues and the SRAM read queue. M3 adds the table store and HIST store, the address generator (the
 * SRAM read queue's client 1) and the write engine. On M1-M3 test machines the GATHER engine is a stand-in drain at one packet
 * per cycle and their read-queue inputs FIFOs a test fills; the send port is a holding register a test fills
 * (the controller is M4). DMA_CONFIG, TILE_PULSE, DMA_RECORD_BASE_ADDRESS (ATTACH_BASE_ADDRESS until M12) and PTR are set from Java in M3.
 *
 * Stages (intra_tile_latency each): injection point -> router loopback route block; eject selector -> director.
 */
final class Tile implements Component {
    final int idx;
    final Config cfg;
    final Router router;
    final InjectionPath inj;
    final TrafficDirector dir;
    final ControllerMemories cmem;
    final BigSRAM sram;
    final SRAMReadQueue srq;
    final WriteEngine writer;
    final AddressGenerator ag;
    final TableStore ts;
    final HistStore hist;
    final WordHold agOut;                          // the address generator's holding register (read-queue client 2)
    final WordDelayLine agStreamReq, agStreamRet, agGroupReq, agGroupRet, weTsReq, weTsRet, weHistReq;
    int masterConfig;
    final WordHold sramRd, sramWr;                 // the two queues' holding registers into the SRAM
    final WordFIFO[] srqClient = new WordFIFO[3];   // read-queue clients' request FIFOs (test-fed until M3/M5)
    // buses
    final WordHold readReturn, sendHold;
    final WordDelayLine injBus, ejectBus, portB;
    final WordFIFO weIn, gatherIn;
    // stand-ins for later milestones
    final Drain gatherStub;                       // stand-in (M1-M3 test machines: Config.realResponder off)
    final GatherEngine gather;
    final WordHold gatherOut;
    final ReadPort readPort;                      // the controller read port (4.3)
    final PeripheralSlot slot;                    // the peripheral slot (4.17)
    final RequestPort req;                        // the request port (4.18)
    // control
    final TileControl ctl;
    final Controller core;
    Chip chip;
    final List<WordFIFO> hardwareFifos = new ArrayList<>();
    private final Component[] children;
    Random shuffle;

    Tile(int idx, Config cfg) { this(idx, cfg, meshPeripheral(idx, cfg)); }
    private static Peripheral meshPeripheral(int idx, Config cfg) {
        Peripheral tp = cfg.testPeripheral != null ? cfg.testPeripheral.apply(idx) : null;
        return tp != null ? tp : cfg.periphTypeOf(idx) == 4 ? new Replayer(idx) : null;   // every mesh tile's peripheral 1: the replayer, type 4 (4.17.1)
    }
    /** A unit with peripheral 1 p1: a mesh tile (0-63), or a controller (64-66, Config.ccCores: CcCores builds it with its engine). */
    Tile(int idx, Config cfg, Peripheral p1) {
        this.idx = idx; this.cfg = cfg;
        java.util.List<BusBase> reg = BusBase.REGISTRY.get(); int reg0 = reg == null ? 0 : reg.size();
        router = new Router(idx, cfg.hops, cfg.queueDepth, cfg.loopbackDepth > 0 ? cfg.loopbackDepth : cfg.readTokens, idx < 3);
        inj = new InjectionPath(idx);
        dir = new TrafficDirector(idx, cfg);
        slot = new PeripheralSlot(this, p1); dir.periph = slot; req = new RequestPort(this, cfg);
        cmem = new ControllerMemories(cfg);
        String p = "t" + idx + ".";
        readReturn = new WordHold(p + "readReturn");
        sendHold = new WordHold(p + "send");
        injBus = new WordDelayLine(p + "inj>router", cfg.intraTileLatency);
        ejectBus = new WordDelayLine(p + "eject>director", cfg.intraTileLatency);
        portB = new WordDelayLine(p + "director>portB", cfg.cmLatency);
        weIn = new WordFIFO(p + "we-in", cfg.weInDepth);
        gatherIn = new WordFIFO(p + "gather-q", cfg.gatherDepth);
        inj.readReturn = readReturn; inj.send = sendHold; inj.toRouter = injBus;
        router.fromInjection = injBus; router.toDirector = ejectBus;
        dir.periphPresent = slot.present();
        dir.in = ejectBus; dir.toGather = gatherIn; dir.toWriteEngine = weIn; dir.toPortB = portB;
        cmem.portB = portB;
        sram = new BigSRAM(idx, cfg); srq = new SRAMReadQueue(idx, cfg); writer = new WriteEngine(idx, cfg);
        ag = new AddressGenerator(idx, cfg); ts = new TableStore(idx); hist = new HistStore(idx);
        int L = cfg.tsEngineLatency;
        agStreamReq = new WordDelayLine(p + "ag>stream", L); agStreamRet = new WordDelayLine(p + "stream>ag", L);
        agGroupReq = new WordDelayLine(p + "ag>group", L); agGroupRet = new WordDelayLine(p + "group>ag", L);
        weTsReq = new WordDelayLine(p + "we>ts", L); weTsRet = new WordDelayLine(p + "ts>we", L); weHistReq = new WordDelayLine(p + "we>hist", L);
        agOut = new WordHold(p + "ag.out");
        ag.streamReq = agStreamReq; ag.streamRet = agStreamRet; ag.groupReq = agGroupReq; ag.groupRet = agGroupRet; ag.out = agOut;
        ts.reqStream = agStreamReq; ts.retStream = agStreamRet; ts.reqGroup = agGroupReq; ts.retGroup = agGroupRet; ts.reqWe = weTsReq; ts.retWe = weTsRet;
        hist.req = weHistReq;
        writer.tsReq = weTsReq; writer.tsRet = weTsRet; writer.histReq = weHistReq;
        sramRd = new WordHold(p + "sram.rd"); sramWr = new WordHold(p + "sram.wr");
        sram.readReq = sramRd; sram.writeReq = sramWr;
        srq.toSram = sramRd; srq.toInjection = readReturn; inj.srq = srq; router.tokens = srq; srq.tokensOn = cfg.loopbackTokens; router.loopbackLast = cfg.loopbackLast;
        for (int a = 0; a < BigSRAM.AREAS; a++) srq.fromSram[a] = sram.ret[a];
        for (int c = 0; c < 3; c++) { srqClient[c] = new WordFIFO(p + "srq.client" + c, 64); srq.client[c] = srqClient[c]; }
        if (!cfg.testReadClients) srq.client[SRAMReadQueue.AG] = agOut;
        gather = new GatherEngine(idx); gather.srq = srq; gather.queue = gatherIn;
        ag.gatherAlign = this::checkGatherAlignment;
        gatherOut = new WordHold(p + "gather.out"); gather.out = gatherOut;
        if (cfg.realResponder && !cfg.testReadClients) srq.client[SRAMReadQueue.GATHER] = gatherOut;
        readPort = new ReadPort(idx, cfg);
        if (!cfg.testReadClients) {               // test-fed read clients (M2 tests) bypass the read port's queue and accounting
            srq.client[SRAMReadQueue.READPORT] = readPort.q;
            inj.readPort = readPort;              // results return as packets through the loopback (4.3)
            dir.readPort = readPort;
        }
        writer.in = weIn; writer.toSram = sramWr;
        gatherStub = new Drain(p + "gather");
        if (cfg.realResponder && !cfg.testReadClients) gather.in = gatherIn; else gatherStub.in = gatherIn;
        Component gat = gather.in != null ? gather : gatherStub;
        for (WordFIFO[] fs : router.q) for (WordFIFO f : fs) { hardwareFifos.add(f); f.sinkOverflows(ovf, Conditions.ROUTER_OVF); }
        hardwareFifos.add(weIn); hardwareFifos.add(gatherIn); weIn.sinkOverflows(ovf, Conditions.WE_IN_OVF); gatherIn.sinkOverflows(ovf, Conditions.GATHER_OVF);
        for (WordFIFO f : new WordFIFO[] { ts.qStream, ts.qGroup, ts.qWe, hist.q }) f.sinkOverflows(ovf, Conditions.TABLE_OVF);
        hardwareFifos.add(ts.qStream); hardwareFifos.add(ts.qGroup); hardwareFifos.add(ts.qWe); hardwareFifos.add(hist.q);
        ctl = new TileControl(this, cfg);
        core = new Controller(this, ctl, cmem);
        core.enabled = !cfg.coreless;
        dir.ctl = ctl;
        // the SRAM read queue and the injection point share the token pool: one unit, run in that order (4.5)
        Component srqInj = t -> { srq.runCycle(t); inj.runCycle(t); };
        children = new Component[] { router, srqInj, dir, cmem, sram, writer, ag, ts, hist, readPort, gat, core };
        gatherReal = gather.in != null;
        ownBuses = reg == null ? java.util.List.of() : new ArrayList<>(reg.subList(reg0, reg.size()));
    }
    /** The buses this unit built (its internal ones): stage 1 wakes the unit on a put to any of them, so a put from
     *  outside its step (a test feeding the send port, a harness) wakes it; from inside, the unit is awake anyway. */
    private final java.util.List<BusBase> ownBuses;
    void wakeOnOwnBuses(Chip c) { for (BusBase b : ownBuses) if (b.wakeChip == null) { b.wakeChip = c; b.wakeUnit = idx; } }

    /** C (WORKER29): the children stepped by direct calls, in the order of {@link #order} (their indices in
     *  {@link #children}), so the JIT can inline each unit's step instead of one megamorphic interface call; the shuffle
     *  permutes the indices with the same draws as it permuted the children. */
    private final int[] order = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    private boolean gatherReal;
    @Override public void runCycle(long t) {
        if (shuffle != null) {
            for (int i = order.length - 1; i > 0; i--) { int j = shuffle.nextInt(i + 1); int x = order[i]; order[i] = order[j]; order[j] = x; }
            for (int i = 0; i < order.length; i++) step(order[i], t);
        } else {
            router.runCycle(t); srq.runCycle(t); inj.runCycle(t); dir.runCycle(t); cmem.runCycle(t); sram.runCycle(t);
            writer.runCycle(t); ag.runCycle(t); ts.runCycle(t); hist.runCycle(t); readPort.runCycle(t);
            if (gatherReal) gather.runCycle(t); else gatherStub.runCycle(t);
            core.runCycle(t);
        }
        slot.runCycle(t);
        slot.endCycle(t);
        ctl.endCycle(t);
    }

    private void step(int k, long t) {
        switch (k) {
            case 0 -> router.runCycle(t);
            case 1 -> { srq.runCycle(t); inj.runCycle(t); }       // one unit: they share the token pool (4.5)
            case 2 -> dir.runCycle(t);
            case 3 -> cmem.runCycle(t);
            case 4 -> sram.runCycle(t);
            case 5 -> writer.runCycle(t);
            case 6 -> ag.runCycle(t);
            case 7 -> ts.runCycle(t);
            case 8 -> hist.runCycle(t);
            case 9 -> readPort.runCycle(t);
            case 10 -> { if (gatherReal) gather.runCycle(t); else gatherStub.runCycle(t); }
            default -> core.runCycle(t);
        }
    }

    /** Monotonic event counts per condition (TileControl sets a condition when its count grows; spec 4.13). */
    private final long[] evc = new long[Conditions.N];
    /** C (WORKER29): the hardware FIFOs' overflow counts by condition, summed as they happen (WordFIFO.sinkOverflows). */
    private final long[] ovf = new long[Conditions.N];
    long[] eventCounts() {
        long[] c = evc;
        c[Conditions.DRIFT] = ag.driftEvents;
        c[Conditions.ROUTER_OVF] = ovf[Conditions.ROUTER_OVF];          // C (WORKER29): the FIFOs' overflows as they happen
        c[Conditions.WE_IN_OVF] = ovf[Conditions.WE_IN_OVF];
        c[Conditions.READ_PORT] = readPort.errors;
        c[Conditions.UNEXPECTED] = dir.unexpectedTotal;
        c[Conditions.TABLE_OVF] = ovf[Conditions.TABLE_OVF];
        c[Conditions.BUS_ERROR] = core.busErrors;
        c[Conditions.ILLEGAL] = core.illegals;
        c[Conditions.GATHER_OVF] = ovf[Conditions.GATHER_OVF];
        c[Conditions.BAD_MODE_OR_SIZE] = dir.badTotal;                    // ERRORS [1] (4.13): bad packets (v1.25: misalignment is UB)
        c[Conditions.DONE_EARLY] = ctl.doneEarlyEvents;
        // ERRORS [13:11] are live (the peripherals' ERRORS != 0), not counted events: TileControl.errorsAt
        c[Conditions.RECV_UNDERFLOW] = req.underflows;
        return c;
    }

    /** TILE_CONDITIONS (spec 4.13, 8.3a) as the tile holds it. */
    int conditions() { return ctl.conditions; }

    /** mem_idle (spec 4.1): the big-SRAM write path is empty -- the write engine's input FIFO, its holding register
     *  into the SRAM and every area's queue hold no write (DEV: reads queued at an area do not hold mem_idle low). */
    boolean memIdle(long t) { return weIn.isEmpty(t) && writer.idle(t) && sram.noWritesQueued(); }

    // ---- the controller's view, set from Java in M3 (registers in M4) ----
    /** DMA_CONFIG (spec 8.1): the run, the digits and k. */
    long reservedDigits;
    void setMasterConfig(int v) {
        masterConfig = v; ag.config(v);
        int bd = Fields.mcBucketDigit(v), hd = Fields.mcHistDigit(v);
        if (bd > 4 || hd > 4)                         // v1.25 (s8e): selectors 5-7 are UB (v1.2: treated as 0)
            throw new UndefinedBehavior(chip != null ? chip.cycle : -1, idx, "DMA_CONFIG", "a reserved digit selector (bucket " + bd + ", hist " + hd + "; 5-7 are reserved: spec 4.15)");
        writer.bucketDigit = bd > 4 ? 0 : bd; writer.histDigit = hd > 4 ? 0 : hd;
        gather.k = Fields.mcK(v);
    }
    /** TILE_PULSE's start from Java (coreless tests), acting from the next cycle. */
    void pulse(int bits) { if (chip != null && chip.skip) chip.wakeNow(idx); ag.pulse(bits); }
    /** DMA_RECORD_BASE_ADDRESS (v1.0: DMA_RECORD_BASE_ADDRESS and DMA_RECORD_BASE_ADDRESS merged): augment and gather use one base. */
    /** v1.25 (DECISIONS-v1.25 s8c): gather buffers are 128-bit aligned whatever k is, the source (DMA_RECORD_BASE_ADDRESS)
     *  and the destination (PTR[me]); checked when a run with mode-4 streams starts. Misalignment is UB (s8b). */
    void checkGatherAlignment(long t) {
        int p = ptr(idx), b = gather.base;
        if ((p & 3) != 0 || (b & 3) != 0)
            throw new UndefinedBehavior(t, idx, "gather (a run with mode-4 streams starts)", "gather buffers not 128-bit aligned: "
                    + ((b & 3) != 0 ? "DMA_RECORD_BASE_ADDRESS = chunk 0x" + Integer.toHexString(b) : "") + ((b & 3) != 0 && (p & 3) != 0 ? ", " : "")
                    + ((p & 3) != 0 ? "PTR[me] = chunk 0x" + Integer.toHexString(p) : ""));
    }
    void setRecordBase(int chunk) { writer.augmentBase = chunk & 0x1FFFFF; gather.base = chunk & 0x1FFFFF; }
    /** The reset (6.1), at the end of t, with Chip.reset (which empties every bus): the engines idle, every queue empty,
     *  the core in the boot ROM, the control state as at power-on; the memories, the tables and big SRAM keep. */
    /** Item S1's measurement (simulator only): every counted write's acceptance -> completion, against K. */
    void s1Attach(S1 s1) {
        Prim.LongRing q = new Prim.LongRing(); dir.acceptQ = q; writer.acceptQ = q;      // WORKER29 (A): primitive, no boxing
        Prim.IntLongMap m = new Prim.IntLongMap(); writer.sramAcc = m; sram.s1Acc = m; sram.s1 = s1; sram.s1K = cfg.K;
        writer.ptrAcc = new Prim.LongRing[256]; writer.histAcc = new Prim.LongRing[256];
        for (int i = 0; i < 256; i++) { writer.ptrAcc[i] = new Prim.LongRing(4); writer.histAcc[i] = new Prim.LongRing(4); }
        ts.s1Acc = writer.ptrAcc; ts.s1 = s1; ts.s1K = cfg.K; ts.s1Tile = idx;
        hist.s1Acc = writer.histAcc; hist.s1 = s1; hist.s1K = cfg.K; hist.s1Tile = idx;
    }
    void reset(long t) {
        ag.haltReset(); gather.haltReset(); readPort.haltReset(); srq.haltReset(); writer.reset(); router.reset(); sram.reset();
        ts.resetPipe(); hist.pipe.reset(); slot.reset(t); req.reset(); cmem.reset(); ctl.reset(t); core.reset();
    }
    void setPtr(int i, int chunk) { ts.poke(TableStore.PTR + i, chunk & 0x1FFFFF); }
    int ptr(int i) { return ts.peek(TableStore.PTR + i); }
    /** STREAM[i] from Java (coreless tests): its three words (Fields.streamSrc, streamDest, streamCount). */
    void setStream(int i, int src, int dest, int count) {
        ts.poke(TableStore.STREAM_SRC + i, src); ts.poke(TableStore.STREAM_DEST + i, dest); ts.poke(TableStore.STREAM_COUNT + i, count);
    }
    void setGroup(int g, int lo, int hi) { ts.poke(TableStore.GROUP + 2 * g, lo); ts.poke(TableStore.GROUP + 2 * g + 1, hi); }
    /** ts_busy (spec 4.15): an engine request within the last ts_engine_latency + 1 cycles, or either store busy. */
    boolean tsBusy(long t) {
        int L = cfg.tsEngineLatency;
        return ts.busy || hist.busy || t <= ag.lastTsReq + L + 1 || t <= writer.lastTsReq + L + 1
                || agStreamReq.inFlight(t) > 0 || agStreamRet.inFlight(t) > 0 || agGroupReq.inFlight(t) > 0 || agGroupRet.inFlight(t) > 0 || weTsReq.inFlight(t) > 0 || weTsRet.inFlight(t) > 0 || weHistReq.inFlight(t) > 0;
    }

    /**
     * Stage 1 (WORKER28): at the end of cycle t, nothing in this unit can change until something wakes it: its core did
     * nothing at t (halted, or in a WFI it didn't leave) and won't wake at t + 1, its peripherals, DMAs and request port
     * are idle, every queue, bus and engine is empty, and nothing is in flight towards it on a link or its fifth port.
     * Its steps until a wake would then change only the per-cycle counters (Controller.skipCycles).
     */
    boolean canSleep(long t) {
        Controller c = core;
        if (!c.enabled || c.idleAt != t) return false;
        if (!slot.idle(t) || !req.idle() || !writer.idle(t) || ag.runningForStatus() || gather.in != null && gather.busy(t)) return false;
        if (!quiescent(t)) return false;
        for (WordSource s : router.linkIn) if (s != null && pendingIn(s, t)) return false;
        if (router.fromCnc != null && pendingIn(router.fromCnc, t)) return false;
        if (c.halt == Controller.RUN && (!c.inWfi || c.interruptPending(t + 1) || ctl.wakeTerm(t + 1))) return false;
        return true;
    }
    /** Stage 1: the cycle after t a sleeping tile must step at though nothing reaches it (a GO; a peripheral's line). */
    long sleepWakeAt(long t) { return Math.min(ctl.nextGoAt(t), slot.nextWakeAt(t)); }
    static boolean pendingIn(WordSource s, long t) {
        if (s instanceof WordDelayLine d) return d.inFlight(t) > 0;
        if (s instanceof WordFIFO f) return !f.isEmpty(t);
        if (s instanceof WordHold h) return !h.isEmpty(t);
        return true;
    }

    boolean quiescent(long t) {
        if (!ag.idle() || tsBusy(t) || !agOut.isEmpty(t)) return false;
        if (!srq.idle() || !sram.idle() || !sramRd.isEmpty(t) || !sramWr.isEmpty(t)) return false;
        for (WordFIFO f : srqClient) if (!f.isEmpty(t)) return false;
        for (WordDelayLine l : sram.ret) if (l.inFlight(t) > 0) return false;
        if (!gatherOut.isEmpty(t) || !readPort.idle(t)) return false;
        if (!readReturn.isEmpty(t) || !sendHold.isEmpty(t) || injBus.inFlight(t) > 0 || ejectBus.inFlight(t) > 0 || portB.inFlight(t) > 0 || slot.inFlight(t)) return false;
        if (dir.inFlight() > 0) return false;                     // the K delay line: decrements still to come (4.8)
        for (WordFIFO f : hardwareFifos) if (!f.isEmpty(t)) return false;
        return true;
    }

    void resetStats(long t) {
        router.resetStats(t); inj.resetStats(); dir.resetStats(); sram.resetStats(); srq.resetStats(); writer.resetStats(); ts.resetStats();
        weIn.resetStats(t); gatherIn.resetStats(t); core.resetStats();
    }
}
