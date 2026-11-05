import java.util.ArrayDeque;

/**
 * A unit's control (spec v1.2: 4.12, 4.13, 6, 8): the controller's register block (8.1) and its side of the write
 * counters, `done` and "command landed", the READY edge, the GO line as this unit sees it, the events (EVENTS,
 * EVENTS_PEEK, EVENTS_MASK, EVENTS_REQUIRED) and the done gate, ERRORS, PARAMS and STATUS, the send port (ROUTER_HDR
 * with no-increment, ROUTER_ADDR, the W words), the read port's setup and the simulator-only words (SIM_EXIT, TRACE).
 * The reset ({@link #reset}, 6.1) is the only way to stop anything; there is no HALT. The peripheral windows decode in {@link PeripheralSlot} (4.17.4); EVENTS [15:13] and
 * ERRORS [13:11] are its live bits.
 *
 * Timing (4.11): a store at t updates its register at the end of t, and the owning unit acts from t + 1. A load reaches
 * {@link #read} in the cycle it samples (the controller holds a block or window load for one cycle, latency 2), so a load
 * issued at t returns the state at the end of t + L - 2 and takes its side effect (a consuming EVENTS load, a
 * REQUEST_RECEIVE pop) at the end of t + L - 1. {@link #endCycle} runs after every unit of the tile has run cycle t:
 *   1. GO seen at t (clears done and command landed, ends the done gate, sets [8]);
 *   2. the controller's store of cycle t (a done pulse with a counter non-zero sets done early, closes the done gate and
 *      clears the counters' events; a counter write adds and arms; PULSE_DMA clears [10]);
 *   3. a director write covering slot 0 at t sets command landed and [9];
 *   4. a consuming load's clear (only the sticky bits it returned, under EVENTS_MASK); then the edge events of t ([10],
 *      [11], the per-counter done events [6:0] and counters done [7]); a clear in the same cycle beats a set;
 *   5. new errors; READY on the rising edge of done AND command landed; STATUS for t + 1.
 *
 * The wake term (4.12): wake = gate ? GO pending : ((events & mask & ~required) != 0) | (required != 0 && (events &
 * required) == required). A wake consumes nothing; the mesh.h helpers load EVENTS after every WFI.
 */
final class TileControl {
    final Tile tile;
    final Config cfg;
    final int idx;
    // ---- configuration registers ----
    int routerHdr, routerAddr, dmaConfig, recordBase, rdSetup;
    final int[] setW = new int[3];                                // ROUTER_SET_W0..W2
    // ---- the controller's store this cycle ----
    private int pendIdx = -1, pendVal; private long pendT = Long.MIN_VALUE;
    // ---- state ----
    boolean done, landed, gate;
    private boolean readyLevel;
    int events;                                                   // EVENTS' sticky events, as at the end of the last cycle
    int wakeMask = Registers.EVENTS_MASK_RESET, wakeRequired;
    // qualifiers (4.12): counters done [7] and each counter's done event [6:0]
    private boolean qAll; private final boolean[] qOne = new boolean[TrafficDirector.N_COUNTERS + 1];
    private boolean condAllPrev = true; private final boolean[] condOnePrev = { true, true, true, true, true, true, true, true };
    private boolean rdDonePrev, requestBusyPrev;
    long reservedAccesses;                                        // loads and stores of reserved words (8.1: meshsim flags them)
    long simExitAt = -1; int simExitValue;                        // SIM_EXIT (8.15)
    long traceStores; int lastTrace;
    /** Simulator-only: called with each TRACE store (8.15), at its cycle (tests and benchmarks). */
    interface TraceHook { void onTrace(long t, int v); }
    TraceHook traceHook;
    private int consumeBits; private long consumeT = Long.MIN_VALUE; // a consuming load's clear, at the end of its cycle
    long latchSets, spuriousEdges, gatedEdges;
    int conditions, status;
    private final long[] evPrev = new long[Conditions.N];
    long errorSince = -1;                                         // the cycle the conditions last became non-zero (the error line, 6.6)
    // ---- lines from C&C, as this tile sees them (scheduled by Chip; each already includes this tile's skew) ----
    private final ArrayDeque<Long> goQ = new ArrayDeque<>();
    private long goAt = Long.MIN_VALUE;
    long readyPulseAt = Long.MIN_VALUE;                           // Chip collects it after the cycle
    // ---- stats and checks ----
    long dones, readies, gos, runEdges, sends, lastSendAt = -1;
    final long[] sendsBySize = new long[3];                       // send stores by size (32, 64, 128)
    long goWakes, eventWakes;
    long doneEarlyEvents, wakeInDone, counterWraps;
    /** The cycle of the last write to each down-counter (its store's cycle), -1 if none: measurements only. */
    final long[] counterWriteAt = { -1, -1, -1, -1, -1, -1, -1, -1 };
    long resetAt = Long.MIN_VALUE;

    /** The reset (6.1), at t: every error, counter and event 0, the reset mask, the gate, done, command landed and any
     *  pending GO cleared; the configuration registers (undefined) to 0. */
    void reset(long t) {
        resetAt = t;
        done = false; landed = false; gate = false; events = 0; qAll = false; java.util.Arrays.fill(qOne, false);
        condAllPrev = true; java.util.Arrays.fill(condOnePrev, true); readyLevel = false; rdDonePrev = false; requestBusyPrev = false;
        wakeMask = Registers.EVENTS_MASK_RESET; wakeRequired = 0; conditions = 0; errorSince = -1;
        long[] c = tile.eventCounts(); System.arraycopy(c, 0, evPrev, 0, evPrev.length);
        goQ.clear(); goAt = Long.MIN_VALUE; consumeT = Long.MIN_VALUE; pendT = Long.MIN_VALUE;
        routerHdr = routerAddr = dmaConfig = recordBase = rdSetup = 0; setW[0] = setW[1] = setW[2] = 0; simExitAt = -1;
        tile.dir.resetCounters();
    }
    long violations; String lastViolation;

    TileControl(Tile tile, Config cfg) {
        this.tile = tile; this.cfg = cfg; this.idx = tile.idx;
        // power-on is a reset (6.1): done 0, the reset mask; the core starts in the boot ROM
    }

    // ================================================================ lines
    void scheduleGo(long t) { goQ.add(t); }

    /** Apply line changes due at or before t (idempotent; every caller in cycle t sees the same result). */
    private void advance(long t) {
        while (!goQ.isEmpty() && goQ.peek() <= t) goAt = goQ.poll();
    }

    boolean goVisible(long t) { advance(t); return goAt == t; }
    boolean pendingLines() { return !goQ.isEmpty(); }

    // ================================================================ the wake events
    /** The live events as a load sampling at t reads them (4.12): request pending, and the peripherals' bits.
     *  PHASE-2 INTERIM: peripheral 1's bit is the v1.1 slot's service lines (phase 3: its EVENTS & EVENTS_MASK). */
    private int liveEvents(long t) {
        int v = tile.slot.liveEvents(t) << Registers.EV_PERIPH_1;
        if (tile.req.pending(t)) v |= 1 << Registers.EV_REQUEST_PENDING;
        return v;
    }
    /** EVENTS as a load sampling at t returns it: the sticky events as at the end of t - 1 and the live events at t. GO
     *  seen at t is set at the end of t (4.12's timing): a load or a WFI sees it from t + 1. */
    int eventsAt(long t) { advance(t); return (events | liveEvents(t)) & Registers.EVENTS_VALID; }
    /** The wake term at t (4.12): the done gate overrides it to GO pending. */
    boolean wakeTerm(long t) {
        int e = eventsAt(t);
        if (gate) return (e & 1 << Registers.EV_GO) != 0;
        int notReq = e & wakeMask & ~wakeRequired;
        return notReq != 0 || wakeRequired != 0 && (e & wakeRequired) == wakeRequired;
    }
    /** WFI: true if the wake term holds at t (a wake consumes nothing). */
    boolean takeWake(long t, boolean atWfi) {
        if (!wakeTerm(t)) return false;
        if ((events & 1 << Registers.EV_GO) != 0) { goWakes++; lastWake = WAKE_GO; } else { eventWakes++; lastWake = WAKE_EVENT; }
        if (done && !gate && lastWake != WAKE_GO) {               // only GO wakes wait_for_command() (the gate makes this impossible unless Config.doneGate is off)
            wakeInDone++;
            violate(t, "a WFI woken by an event other than GO with done set (wait_for_command()): only GO wakes the core between done and GO (the done gate, spec 4.12)");
        }
        return true;
    }
    static final int WAKE_GO = 1, WAKE_EVENT = 2;
    int lastWake;
    long lastDoneAt = -1, lastLandedAt = -1;

    // ================================================================ the controller's register accesses
    /** A load of the block or a window sampling at t; null = bus error (an absent peripheral's window). Write-only and
     *  reserved words read 0 (8.1); a reserved word is flagged. A consuming load (EVENTS, REQUEST_RECEIVE) takes its
     *  side effect at the end of t. */
    Integer read(long t, int addr) {
        int n = Registers.windowOf(addr);
        if (n != 0) return windowRead(t, n, ((addr - Registers.windowBase(n)) >> 2));
        Registers.Reg r = Registers.loadAt(addr);
        if (r == null) {
            int i = (addr - Registers.REG_BASE) >> 2;
            if (Registers.STORE[i] == null) reserved(t, "load of the reserved register-block word 0x" + Integer.toHexString(0x800 + 4 * i));
            return 0;                                              // a write-only register, or reserved: reads 0
        }
        if (r == Registers.PARAMS) return params();
        if (r == Registers.STATUS) return statusAt(t);
        if (r == Registers.ERRORS) return errorsAt(t);
        if (Registers.isCounter(r)) return tile.dir.counterAtStart(Registers.counterOf(r), t);
        if (r == Registers.SRAM_RD_ADDR_QUEUE) return tile.readPort.free(t);
        if (r == Registers.SRAM_RD_DEST_ADDR) return rdSetup;
        if (r == Registers.EVENTS) {
            int v = eventsAt(t), c = v & wakeMask & ~Registers.EVENTS_LIVE;
            if (c != 0) { consumeBits = c; consumeT = t; }
            return v;
        }
        if (r == Registers.EVENTS_PEEK) return eventsAt(t);
        if (r == Registers.EVENTS_MASK) return wakeMask;
        if (r == Registers.EVENTS_REQUIRED) return wakeRequired;
        if (r == Registers.ROUTER_HDR) return routerHdr;
        if (r == Registers.ROUTER_ADDR) return routerAddr;
        if (r == Registers.DMA_CONFIG) return dmaConfig;
        if (r == Registers.DMA_RECORD_BASE_ADDRESS) return recordBase;
        if (r == Registers.REQUEST_GET_W0) return tile.req.get(t, 0);
        if (r == Registers.REQUEST_GET_W1) return tile.req.get(t, 1);
        if (r == Registers.REQUEST_GET_W2) return tile.req.get(t, 2);
        if (r == Registers.REQUEST_RECEIVE) return tile.req.receive(t);
        return 0;
    }
    /** A store at t to the block or a window; false = bus error (a read-only register, an absent peripheral's window). A
     *  reserved word ignores the store (flagged). A send store (ROUTER_SEND_32, _64, _128) is the controller's own (it
     *  fills the holding register in the store's cycle); this records it for ROUTER_ADDR's advance and the clears. */
    boolean write(long t, int addr, int v) {
        int n = Registers.windowOf(addr);
        if (n != 0) return windowWrite(t, n, (addr - Registers.windowBase(n)) >> 2, v);
        Registers.Reg r = Registers.storeAt(addr);
        if (r == null) {
            int i = (addr - Registers.REG_BASE) >> 2;
            if (Registers.LOAD[i] != null) return false;           // a read-only register: bus error
            reserved(t, "store to the reserved register-block word 0x" + Integer.toHexString(0x800 + 4 * i));
            return true;
        }
        int defined = definedBits(r);
        if ((v & ~defined) != 0) violate(t, r.name + " stored with reserved bits set (0x" + Integer.toHexString(v & ~defined) + "): ignored, read 0 (8.1; invariant 21)");
        if (r == Registers.DMA_RECORD_BASE_ADDRESS && (v & 1) != 0) violate(t, "DMA_RECORD_BASE_ADDRESS 0x" + Integer.toHexString(v) + " not aligned to a record (two chunks; 8.7; invariant 20)");
        if (r == Registers.SRAM_RD_DEST_ADDR && Registers.HDR_SIZE.get(v) == LinkWord.SIZE_UNDEF) violate(t, "SRAM_RD_DEST_ADDR with the reserved size 11 (8.8; invariant 21)");
        pendIdx = r.index; pendVal = v; pendT = t;
        return true;
    }
    /** A store's defined bits per register (8.1-8.10); the rest are reserved or spare (invariant 21). */
    static int definedBits(Registers.Reg r) {
        if (r == Registers.PULSE_DONE || r == Registers.PULSE_DMA) return 1;
        if (r == Registers.EVENTS_MASK || r == Registers.EVENTS_REQUIRED) return Registers.EVENTS_VALID;
        if (r == Registers.ROUTER_HDR) return 1 | 0xFFF80000;
        if (r == Registers.DMA_CONFIG) return (1 << 27) - 1;
        if (r == Registers.DMA_RECORD_BASE_ADDRESS || r == Registers.SRAM_RD_ADDR_QUEUE) return 0x1FFFFF;
        if (Registers.isCounter(r)) return TrafficDirector.COUNTER_MASK;
        if (r == Registers.SRAM_RD_DEST_ADDR) return 0x3FFFFF;
        return -1;
    }
    private void reserved(long t, String m) { reservedAccesses++; violate(t, m + ": reads 0, stores ignored (8.1)"); }

    /** PHASE-2 INTERIM (phase 3 builds the peripheral framework): peripheral n's window. Absent: a bus error. Peripheral 1
     *  (the v1.1 slot): PARAMS reads its type; the other words read 0. */
    private Integer windowRead(long t, int n, int word) { return tile.slot.read(t, n, word); }
    private boolean windowWrite(long t, int n, int word, int v) { return tile.slot.write(t, n, word, v); }
    /** A counter write of N at t adds N at the end of t: the director judges a same-cycle arrival against count + N. */
    int pendingAdd(int c, long t) {
        return pendT == t && Registers.isCounter(Registers.STORE[pendIdx]) && Registers.counterOf(Registers.STORE[pendIdx]) == c ? pendVal & TrafficDirector.COUNTER_MASK : 0;
    }
    /** The packet a send store of word v at t sends (8.4), from the registers as they stand at t: the store supplies the
     *  packet's top word, ROUTER_SET_W0-W2 the words below it; words beyond the size are 0. */
    long sendLo(int v, int size) {
        return size == LinkWord.SIZE_32 ? v & 0xFFFFFFFFL : size == LinkWord.SIZE_64 ? (long) v << 32 | (setW[0] & 0xFFFFFFFFL)
                : (long) setW[1] << 32 | (setW[0] & 0xFFFFFFFFL);
    }
    long sendHi(int v, int size) { return size == LinkWord.SIZE_128 ? (long) v << 32 | (setW[2] & 0xFFFFFFFFL) : 0; }
    /** The header a send store at t captures (8.4): ROUTER_HDR's counter, mode and dst, ROUTER_ADDR, the store's size. */
    long sendHeader(int size) {
        return LinkWord.hdr(0, Registers.HDR_DST.get(routerHdr), Registers.HDR_MODE.get(routerHdr), Registers.HDR_CTR.get(routerHdr), routerAddr, size);
    }
    /** The read port's setup (8.8): the next result's byte offset in data memory, the size and the counter. */
    int rdDest() { return Registers.RD_DEST.get(rdSetup); }
    int rdSize() { return Registers.HDR_SIZE.get(rdSetup); }
    int rdCounter() { return Registers.HDR_CTR.get(rdSetup); }

    // ================================================================ end of cycle
    void endCycle(long t) {
        advance(t);
        if (goAt == t) {
            done = false; landed = false; gate = false; gos++;
            events |= 1 << Registers.EV_GO;
            // the boot ROM takes the boot command at this GO; its own load of slot 0 is incidental (it jumps by slot 1), so
            // the next command may land before it without the command-read check flagging it (6.2, 6.3)
            if (LinkWord.region(tile.core.pc) == LinkWord.R_BOOTROM) tile.cmem.slot0Unread = false;
            tile.sram.newPhase();
        }
        int clearBits = 0;                                        // clears that beat a same-cycle set of [7] and [6:0]
        if (pendT == t) clearBits = apply(t, pendIdx, pendVal);
        if (tile.cmem.landedAt == t) { landed = true; lastLandedAt = t; events |= 1 << Registers.EV_COMMAND; }
        conditionEvents(t);
        boolean rd = done && landed;
        if (rd && !readyLevel) { readyPulseAt = t; readies++; }
        readyLevel = rd;
        // a consuming load's clear: only the sticky bits it returned (an event set after its sample point survives)
        if (consumeT == t) events &= ~(consumeBits & ~Registers.EVENTS_LIVE);
        // edge events
        boolean rdDone = tile.ag.doneForStatus();
        if (rdDone && !rdDonePrev) events |= 1 << Registers.EV_RUN_DONE;
        rdDonePrev = rdDone;
        boolean rb = tile.req.busy(t);
        if (!rb && requestBusyPrev) events |= 1 << Registers.EV_REQUEST_ROOM;
        requestBusyPrev = rb;
        counterEvents(t, clearBits);
        if (!cfg.coreless) status = computeStatus(t);              // only a controller can read it (simulator speed)
    }

    static final int COUNTER_EVENTS = 0x7F | 1 << Registers.EV_COUNTERS_DONE;
    /** The per-counter done events [6:0] and counters done [7] (4.12): set on the rising edge of each one's done
     *  condition when its qualifier holds, not while the done gate is closed (an edge there is ignored and consumes the
     *  qualifier); a clear in the same cycle wins. */
    private void counterEvents(long t, int clearBits) {
        int[] r = tile.dir.remaining;
        boolean any = false, gated = gate && cfg.doneGate;
        for (int c = 1; c <= TrafficDirector.N_COUNTERS; c++) {     // v1.2 (4.12): done means landed, by K; mem_idle and cm_idle take no part
            if (r[c] != 0) { qOne[c] = true; any = true; }
            boolean cond = r[c] == 0;
            int bit = Registers.evCounter(c);
            if (cond && !condOnePrev[c] && qOne[c]) {
                qOne[c] = false;
                if (!gated && (clearBits & 1 << bit) == 0) events |= 1 << bit;
            }
            condOnePrev[c] = cond;
        }
        if (any) qAll = true;
        boolean cond = countersZero();
        if (cond && !condAllPrev && qAll && gated) { qAll = false; gatedEdges++; }
        else if (cond && !condAllPrev && qAll) {
            qAll = false;
            if ((clearBits & 1 << Registers.EV_COUNTERS_DONE) == 0) { events |= 1 << Registers.EV_COUNTERS_DONE; latchSets++; tile.sram.newPhase(); }
        } else if (cond && !condAllPrev) spuriousEdges++;
        condAllPrev = cond;
    }

    boolean everyCounterDone(long t) { return countersZero(); }
    boolean countersZero() { int[] r = tile.dir.remaining; for (int c = 1; c <= TrafficDirector.N_COUNTERS; c++) if (r[c] != 0) return false; return true; }
    /** Counters done ([7]) is pending (tests). */
    boolean countersDonePending() { return (events & 1 << Registers.EV_COUNTERS_DONE) != 0; }

    /** The controller's store of cycle t; returns the event bits it clears this cycle (beating a same-cycle set). */
    private int apply(long t, int i, int v) {
        Registers.Reg r = Registers.STORE[i];
        int clears = 0;
        if (r == Registers.PULSE_DONE) {
            if ((v & 1) != 0) {
                checkInvariant4(t);
                if (!countersZero()) doneEarlyEvents++;            // done early (4.13): a write counter non-zero
                if ((wakeMask & 1 << Registers.EV_GO) == 0)
                    violate(t, "done pulsed (wait_for_command()) with GO clear in EVENTS_MASK: the GO it waits for would not be consumed (4.12)");
                done = true; dones++; lastDoneAt = t;
                if (cfg.doneGate) gate = true;                    // the done gate (4.12)
                clears |= COUNTER_EVENTS;                         // done clears counters done and the per-counter events, with their qualifiers
                events &= ~clears; qAll = false; java.util.Arrays.fill(qOne, false);
            }
        }
        else if (r == Registers.PULSE_DMA) {
            if ((v & 1) != 0) {
                if (tile.ag.running) { /* ignored, flagged by the address generator (8.6) */ }
                else { clears |= 1 << Registers.EV_RUN_DONE; events &= ~clears; }
                tile.ag.pulse(Fields.PULSE_START);
            }
        }
        else if (r == Registers.DMA_CONFIG) { dmaConfig = v; tile.setMasterConfig(v); }
        else if (r == Registers.DMA_RECORD_BASE_ADDRESS) { recordBase = v & Registers.RB_CHUNK.mask(); tile.setRecordBase(v); }
        else if (Registers.isCounter(r)) {                         // 4.12: a write adds; N > 0 arms; 0 does nothing
            int k = Registers.counterOf(r), n = v & TrafficDirector.COUNTER_MASK;
            counterWriteAt[k] = t;                                 // the store's cycle: armed from the end of t
            if (n != 0) {
                long sum = (long) tile.dir.remaining[k] + n;
                if (sum > TrafficDirector.COUNTER_MASK) { counterWraps++; violate(t, TrafficDirector.counterName(k) + " written past 2^20 - 1: the count wraps (4.12)"); }
                tile.dir.remaining[k] = (int) (sum & TrafficDirector.COUNTER_MASK);
                clears |= 1 << Registers.evCounter(k) | 1 << Registers.EV_COUNTERS_DONE;
                events &= ~clears; qAll = true; qOne[k] = true;
            }
        }
        else if (r == Registers.ROUTER_HDR) routerHdr = v & ~(Registers.HDR_SIZE.mask() << Registers.HDR_SIZE.lo) & ~0x1FFFE;   // size and spare: stores ignored, read 0
        else if (r == Registers.ROUTER_ADDR) routerAddr = v;
        else if (r == Registers.ROUTER_SET_W0) setW[0] = v;
        else if (r == Registers.ROUTER_SET_W1) setW[1] = v;
        else if (r == Registers.ROUTER_SET_W2) setW[2] = v;
        else if (Registers.isSend(r)) {                            // captured at t (8.4): ROUTER_ADDR advances (mode 0, no no-increment); W0-W2 clear
            sends++; lastSendAt = t; sendsBySize[Registers.sendSize(r)]++;
            if (Registers.HDR_MODE.get(routerHdr) == LinkWord.M_WRITE && Registers.RH_NOINC.get(routerHdr) == 0) routerAddr += LinkWord.bytes(Registers.sendSize(r));
            setW[0] = setW[1] = setW[2] = 0;
        }
        else if (r == Registers.SRAM_RD_ADDR_QUEUE) {              // the read was queued (or dropped) at t: the next result's offset (8.8)
            int n = rdSize() == LinkWord.SIZE_UNDEF ? 0 : LinkWord.bytes(rdSize());
            rdSetup = (rdSetup & ~Registers.RD_DEST.mask()) | Registers.RD_DEST.place(rdDest() + n);
        }
        else if (r == Registers.SRAM_RD_DEST_ADDR) rdSetup = v & 0x3FFFFF;                       // reserved [31:22]
        else if (r == Registers.EVENTS_MASK) wakeMask = v & Registers.EVENTS_VALID;
        else if (r == Registers.EVENTS_REQUIRED) wakeRequired = v & Registers.EVENTS_VALID;
        else if (r == Registers.REQUEST_SET_W0) tile.req.setW(0, v);
        else if (r == Registers.REQUEST_SET_W1) tile.req.setW(1, v);
        else if (r == Registers.REQUEST_SET_W2) tile.req.setW(2, v);
        else if (r == Registers.REQUEST_SEND) tile.req.send(t, v);
        else if (r == Registers.SIM_EXIT) { simExitAt = t; simExitValue = v; tile.chip.log(t, tile.idx, "SIM_EXIT 0x" + Integer.toHexString(v)); }
        else if (r == Registers.TRACE) { traceStores++; lastTrace = v; if (traceHook != null) traceHook.onTrace(t, v); if (cfg.logTrace) tile.chip.log(t, tile.idx, "TRACE 0x" + Integer.toHexString(v)); }
        return clears;
    }

    /** Invariant 4: done ends a phase only when every counter is done (mem_idle; cm_idle as far as counted packets go:
     *  a command crossing the mesh mid-phase may be in the port-B pipe). A run still issuing is not flagged
     *  (its arrivals elsewhere are counted there). */
    private void checkInvariant4(long t) {
        int[] r = tile.dir.remaining;
        if (!countersZero()) {
            StringBuilder b = new StringBuilder();
            for (int c = 1; c <= TrafficDirector.N_COUNTERS; c++) if (r[c] != 0) b.append(' ').append(TrafficDirector.counterName(c)).append(' ').append(r[c]);
            violate(t, "done pulsed before every expected arrival landed (invariant 4; done early, ERRORS [8]):" + b);
        }
    }

    void violate(long t, String m) { violations++; lastViolation = m; }

    // ================================================================ conditions and status
    private void conditionEvents(long t) {
        long[] c = tile.eventCounts();
        int before = conditions;
        boolean entering = resetAt == t;                          // an error raised in the reset's cycle is lost (the reset wins, 4.13)
        for (int i = 0; i < Conditions.N; i++) {
            if (c[i] > evPrev[i] && !entering) conditions |= 1 << i;
            evPrev[i] = c[i];
        }
        if (before == 0 && conditions != 0) errorSince = t;       // the error line rises (6.6)
    }
    /** The tile's error line (6.6): TILE_CONDITIONS non-zero, as at the end of the last cycle. */
    boolean errorLine() { return conditions != 0 || tile.slot.liveErrors(tile.chip.cycle) != 0; }

    private int computeStatus(long t) {
        AddressGenerator a = tile.ag;
        return (a.runningForStatus() ? 1 << Registers.ST_RD_RUNNING : 0) | (a.doneForStatus() ? 1 << Registers.ST_RD_DONE : 0)
                | (tile.gather.busy(t) ? 1 << Registers.ST_GATHER_BUSY : 0) | (tile.sendHold.isEmpty(t) ? 0 : 1 << Registers.ST_SEND_BUSY)
                | (done ? 1 << Registers.ST_DONE : 0) | (landed ? 1 << Registers.ST_LANDED : 0) | (tile.req.busy(t) ? 1 << Registers.ST_REQUEST_BUSY : 0);
    }
    /** STATUS as a load sampling at t reads it (computed at the end of t - 1). */
    int statusAt(long t) { return status; }
    /** PARAMS (8.2): constant. */
    int params() {
        return Registers.PA_PERIPH.place(tile.dir.periphPresent) | Registers.PA_ID.place(idx) | Registers.PA_SIZE.place((cfg.memChunks >>> Registers.SIZE_UNIT_SHIFT) - 1);
    }
    /** ERRORS as a load sampling at t reads it (4.13): the sticky bits; [13:11] live (PHASE-2 INTERIM: v1.1's sticky bad
     *  peripheral write stays at [11] until phase 3). */
    int errorsAt(long t) { return conditions | tile.slot.liveErrors(t) << 11; }
}
