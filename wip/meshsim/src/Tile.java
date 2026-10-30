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

    Tile(int idx, Config cfg) {
        this.idx = idx; this.cfg = cfg;
        router = new Router(idx, cfg.hops, cfg.queueDepth, cfg.loopbackDepth > 0 ? cfg.loopbackDepth : cfg.readTokens, idx < 3);
        inj = new InjectionPath(idx);
        dir = new TrafficDirector(idx, cfg);
        Peripheral tp = cfg.testPeripheral != null ? cfg.testPeripheral.apply(idx) : null;
        Peripheral p1 = tp != null ? tp : cfg.periphTypeOf(idx) == 1 ? new Replayer(idx) : null;   // every mesh tile's peripheral 1: the replayer (4.17.1)
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
        for (WordFIFO[] fs : router.q) for (WordFIFO f : fs) hardwareFifos.add(f);
        hardwareFifos.add(weIn); hardwareFifos.add(gatherIn);
        hardwareFifos.add(ts.qStream); hardwareFifos.add(ts.qGroup); hardwareFifos.add(ts.qWe); hardwareFifos.add(hist.q);
        ctl = new TileControl(this, cfg);
        core = new Controller(this, ctl, cmem);
        core.enabled = !cfg.coreless;
        if (ctl.halted) slot.setHalted(0, true);           // chip reset: every tile halted, its peripheral held in reset
        dir.ctl = ctl;
        // the SRAM read queue and the injection point share the token pool: one unit, run in that order (4.5)
        Component srqInj = t -> { srq.runCycle(t); inj.runCycle(t); };
        children = new Component[] { router, srqInj, dir, cmem, sram, writer, ag, ts, hist, readPort, gat, core };
    }

    @Override public void runCycle(long t) {
        if (shuffle != null) {
            for (int i = children.length - 1; i > 0; i--) { int j = shuffle.nextInt(i + 1); Component x = children[i]; children[i] = children[j]; children[j] = x; }
        }
        boolean held = !ctl.running(t);               // HALT holds the address generator idle (6.1)
        for (Component c : children) { if (held && c == ag) continue; c.runCycle(t); }
        slot.runCycle(t);
        slot.endCycle(t);
        ctl.endCycle(t);
    }

    /** Monotonic event counts per condition (TileControl sets a condition when its count grows; spec 4.13). */
    private final long[] evc = new long[Conditions.N];
    long[] eventCounts() {
        long[] c = evc;
        c[Conditions.DRIFT] = ag.driftEvents;
        long r = 0; for (WordFIFO[] fs : router.q) for (WordFIFO f : fs) r += f.overflowCount;
        c[Conditions.ROUTER_OVF] = r;
        c[Conditions.WE_IN_OVF] = weIn.overflowCount;
        c[Conditions.READ_PORT] = readPort.errors;
        c[Conditions.UNEXPECTED] = dir.unexpectedTotal;
        c[Conditions.TABLE_OVF] = ts.qStream.overflowCount + ts.qGroup.overflowCount + ts.qWe.overflowCount + hist.q.overflowCount;
        c[Conditions.BUS_ERROR] = core.busErrors;
        c[Conditions.ILLEGAL] = core.illegals;
        c[Conditions.GATHER_OVF] = gatherIn.overflowCount;
        c[Conditions.BAD_MODE_OR_SIZE] = dir.badTotal + srq.misalignedReads + writer.misalignedWrites;   // ERRORS [1] (4.13)
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
        if (bd > 4 || hd > 4) {                       // selectors 5-7 are reserved, treated as 0 (spec 4.15); meshsim flags them
            reservedDigits++;
            ctl.violate(-1, "DMA_CONFIG with a reserved digit selector (bucket " + bd + ", hist " + hd + "; 5-7 are reserved and treated as 0: spec 4.15)");
        }
        writer.bucketDigit = bd > 4 ? 0 : bd; writer.histDigit = hd > 4 ? 0 : hd;
        gather.k = Fields.mcK(v);
    }
    /** TILE_PULSE's start from Java (coreless tests), acting from the next cycle. */
    void pulse(int bits) { ag.pulse(bits); }
    /** DMA_RECORD_BASE_ADDRESS (v1.0: DMA_RECORD_BASE_ADDRESS and DMA_RECORD_BASE_ADDRESS merged): augment and gather use one base. */
    void setRecordBase(int chunk) { writer.augmentBase = chunk & 0x1FFFFF; gather.base = chunk & 0x1FFFFF; }
    /** Entering HALT (13a, 6.1): the address generator to IDLE; the GATHER engine's queue and active register, the read
     *  port's queue and in-flight count, and the return queues clear; the peripheral and the sender are held in reset. Nothing is in flight when RUN/HALT changes
     *  (invariant 7), so in a correct program this changes nothing. */
    void haltReset(long t) { ag.haltReset(); gather.haltReset(); readPort.haltReset(); srq.haltReset(); slot.setHalted(t, true); }
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
