import java.util.ArrayDeque;

/**
 * A tile's control (spec 4.12, 4.13, 6, 8; v1.1 at 2026-10-26n): the register block the controller reaches through its
 * peripheral window, the down-counters' controller side, `done` and "command landed", the READY edge, the GO and RUN/HALT
 * lines as this tile sees them, the wake events (WAKE_EVENTS, WAKE_MASK, WAKE_REQUIRED, WAKE_PEEK), the error
 * conditions (TILE_CONDITIONS) and TILE_STATUS.
 *
 * Timing (spec 4.11): a store at t updates its register at the end of t, and the owning unit acts from t + 1. A load
 * reaches {@link #read} in the cycle it samples; the controller models the register-load latency L (Config.regLoadLatency)
 * by holding a register or table-window load for L - 1 cycles and then reading, so a load issued at t returns the state
 * at the end of t + L - 2 and takes its side effect (a consuming WAKE_EVENTS load, a REQUEST_RECEIVE or UART_RX pop) at
 * the end of t + L - 1. {@link #endCycle} runs after every unit of the tile has run cycle t, so the order in which the
 * units ran does not matter. Within it the order is:
 *   1. line edges seen at t: GO (clears done and command landed, ends the done gate, sets GO's event), HALT -> RUN (clears
 *      done, command landed, the gate, the sticky events, their qualifiers and WAKE_REQUIRED; WAKE_MASK = [8] | [9]),
 *      RUN -> HALT (clears the conditions, the four counters, their done events and qualifiers; the engines, the
 *      peripheral and the sender are held in reset);
 *   2. the controller's store of cycle t (TILE_PULSE: start and run before done; a done pulse with a down-counter non-zero
 *      sets done early, closes the done gate and clears the counters' events; a counter write adds);
 *   3. a command write to slot 0 at t sets command landed and its event; in HALT, done is forced to 1;
 *   4. a consuming load's clear (only the bits it returned, under WAKE_MASK); then the edge events of t ([10], [11],
 *      [12], [20], the per-counter done events [3:0] and counters done [9]); a clear by a counter write, the done pulse or
 *      HALT -> RUN beats a set of [9] and [3:0] in the same cycle;
 *   5. new events set conditions; READY on the rising edge of done AND command landed; TILE_STATUS for t + 1.
 *
 * The wake term (4.12): wake = gate ? GO pending : ((events & mask & ~required) != 0) | (required != 0 && (events &
 * required) == required). A wake consumes nothing; the mesh.h helpers load WAKE_EVENTS after every WFI.
 */
final class TileControl {
    final Tile tile;
    final Config cfg;
    final int idx;
    // ---- configuration registers (write-only ones hold their value for the units; reads return 0) ----
    int setHdr, recordBase, rdSetup;
    final int[] setW = new int[3];                                // ROUTER_SET_W0..W2
    // ---- the controller's store this cycle ----
    private int pendIdx = -1, pendVal; private long pendT = Long.MIN_VALUE;
    // ---- state ----
    boolean done, landed, halted, gate;
    private boolean readyLevel;
    int events;                                                   // WAKE_EVENTS' sticky events, as at the end of the last cycle
    int wakeMask = Registers.WAKE_MASK_RESET, wakeRequired;
    // qualifiers (4.12): counters done [9] and each counter's done event [0]-[3]
    private boolean qAll; private final boolean[] qOne = new boolean[TrafficDirector.N_COUNTERS + 1];
    private boolean condAllPrev = true; private final boolean[] condOnePrev = { true, true, true, true, true, true, true, true };
    private boolean rdDonePrev, requestBusyPrev, senderBusyPrev;
    private int consumeBits; private long consumeT = Long.MIN_VALUE; // a consuming load's clear, at the end of its cycle
    long latchSets, spuriousEdges, gatedEdges;
    int conditions, status;
    private final long[] evPrev = new long[Conditions.N];
    long errorSince = -1;                                         // the cycle the conditions last became non-zero (the error line, 6.6)
    // ---- lines from C&C, as this tile sees them (scheduled by Chip; each already includes this tile's skew) ----
    private final ArrayDeque<Long> goQ = new ArrayDeque<>();
    private final ArrayDeque<long[]> runQ = new ArrayDeque<>();   // {t, 1 run / 0 halt}
    private long goAt = Long.MIN_VALUE;
    long runEdgeAt = Long.MIN_VALUE, haltEdgeAt = Long.MIN_VALUE;
    long readyPulseAt = Long.MIN_VALUE;                           // Chip collects it after the cycle
    // ---- stats and checks ----
    long dones, readies, gos, runEdges, sends, lastSendAt = -1;
    final long[] sendsBySize = new long[3];                       // send stores by size (32, 64, 128)
    long goWakes, eventWakes;
    long doneEarlyEvents, wakeInDone, counterWraps, goPulses;
    /** The cycle of the last write to each down-counter (its store's cycle), -1 if none: measurements only. */
    final long[] counterWriteAt = { -1, -1, -1, -1, -1, -1, -1, -1 };
    long violations; String lastViolation;

    TileControl(Tile tile, Config cfg) {
        this.tile = tile; this.cfg = cfg; this.idx = tile.idx;
        halted = !cfg.coreless;                    // chip reset: HALT (coreless test machines start in RUN)
        done = halted;
    }

    // ================================================================ lines
    void scheduleGo(long t) { goQ.add(t); }
    void scheduleRun(long t, boolean run) { runQ.add(new long[] { t, run ? 1 : 0 }); }
    /** Apply line changes due at or before t (idempotent; every caller in cycle t sees the same result). */
    private void advance(long t) {
        while (!goQ.isEmpty() && goQ.peek() <= t) goAt = goQ.poll();
        while (!runQ.isEmpty() && runQ.peek()[0] <= t) {
            long[] c = runQ.poll();
            boolean run = c[1] == 1;
            if (run && halted) { halted = false; runEdgeAt = c[0]; runEdges++; }
            else if (!run && !halted) { halted = true; haltEdgeAt = c[0]; }
        }
    }
    boolean running(long t) { advance(t); return !halted; }
    boolean goVisible(long t) { advance(t); return goAt == t; }
    boolean runEdge(long t) { advance(t); return runEdgeAt == t; }
    boolean lineChange(long t) { advance(t); return runEdgeAt == t || haltEdgeAt == t; }
    boolean pendingLines() { return !goQ.isEmpty() || !runQ.isEmpty(); }

    // ================================================================ the wake events
    /** The live events and the peripheral's lines as a load sampling at t reads them (4.12, 4.17.3). */
    private int liveEvents(long t) {
        int v = tile.slot.lines(t) << Registers.EV_PERIPH_1;
        if (tile.req.pending(t)) v |= 1 << Registers.EV_REQUEST_PENDING;
        if (tile.uart.rxNotEmpty()) v |= 1 << Registers.EV_UART_RX;
        if (tile.uart.txNotFull()) v |= 1 << Registers.EV_UART_TX;
        return v;
    }
    /** WAKE_EVENTS as a load sampling at t returns it: the sticky events as at the end of t - 1, the live events and the
     *  service lines at t. GO seen at t is set at the end of t (4.12's timing): a load or a WFI sees it from t + 1. */
    int eventsAt(long t) { advance(t); return (events | liveEvents(t)) & Registers.WAKE_VALID; }
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
    /** A load sampling at t (null = bus error). Write-only registers read 0; status is the value computed at t - 1. A
     *  consuming load (WAKE_EVENTS, REQUEST_RECEIVE, UART_RX) takes its side effect at the end of t. */
    Integer read(long t, int addr) {
        Registers.Reg r = Registers.loadAt(addr);
        if (r == null) return null;
        if (r == Registers.TILE_STATUS) return statusAt(t);
        if (r == Registers.TILE_CONDITIONS) return conditions;
        if (Registers.isCounter(r)) return tile.dir.counterAtStart(Registers.counterIndex(r) + 1, t);   // PHASE-1 INTERIM: v1.1's four registers are counters 1-4
        if (r == Registers.SRAM_RD_ADDR_QUEUE) return tile.readPort.free(t);
        if (r == Registers.WAKE_EVENTS) {
            int v = eventsAt(t), c = v & wakeMask;
            if (c != 0) { consumeBits = c; consumeT = t; int lines = (c & Registers.WAKE_PERIPH_LINES) >>> Registers.EV_PERIPH_1; if (lines != 0) tile.slot.claim(t, lines); }
            return v;
        }
        if (r == Registers.WAKE_PEEK) return eventsAt(t);
        if (r == Registers.WAKE_MASK) return wakeMask;
        if (r == Registers.WAKE_REQUIRED) return wakeRequired;
        if (r == Registers.REQUEST_GET_W0) return tile.req.get(t, 0);
        if (r == Registers.REQUEST_GET_W1) return tile.req.get(t, 1);
        if (r == Registers.REQUEST_GET_W2) return tile.req.get(t, 2);
        if (r == Registers.REQUEST_RECEIVE) return tile.req.receive(t);
        if (r.index >= Registers.PERIPH_STATUS[0].index && r.index <= Registers.PERIPH_STATUS[7].index) return tile.slot.status(t, Registers.periphStatusIndex(r));
        if (r == Registers.UART_RX) return tile.uart.rx(t);
        if (r == Registers.UART_STATUS) return tile.uart.status();
        if (r == Registers.UART_DIVISOR) return tile.uart.divisor();
        return 0;
    }
    /** A store at t; false = bus error (unmapped or read-only). A send store (ROUTER_SEND_32, _64, _128) is the controller's
     *  own (it fills the holding register in the store's cycle); this records it for the auto-increment and the clears. */
    boolean write(long t, int addr, int v) {
        Registers.Reg r = Registers.storeAt(addr);
        if (r == null || !r.writable()) return false;
        pendIdx = r.index; pendVal = v; pendT = t;
        return true;
    }
    /** A counter write of N at t adds N at the end of t: the director judges a same-cycle arrival against count + N. */
    int pendingAdd(int c, long t) {
        return pendT == t && Registers.isCounter(Registers.STORE[pendIdx]) && Registers.counterIndex(Registers.STORE[pendIdx]) + 1 == c ? pendVal & TrafficDirector.COUNTER_MASK : 0;
    }
    /** The packet a send store of word v at t sends (8.4), from the registers as they stand at t: the store supplies the
     *  packet's top word, ROUTER_SET_W0-W2 the words below it; words beyond the size are 0. */
    long sendLo(int v, int size) {
        return size == LinkWord.SIZE_32 ? v & 0xFFFFFFFFL : size == LinkWord.SIZE_64 ? (long) v << 32 | (setW[0] & 0xFFFFFFFFL)
                : (long) setW[1] << 32 | (setW[0] & 0xFFFFFFFFL);
    }
    long sendHi(int v, int size) { return size == LinkWord.SIZE_128 ? (long) v << 32 | (setW[2] & 0xFFFFFFFFL) : 0; }
    /** PHASE-1 INTERIM: v1.1's ROUTER_SET_HDR read as a v1.2 header (mode [2:0] of v1.1's field, uncounted, the 21-bit
     *  address as is). Phase 2 replaces it with ROUTER_HDR and ROUTER_ADDR. */
    long sendHeader(int size) {
        return LinkWord.hdr(0, Registers.H_DST.get(setHdr), Registers.H_MODE.get(setHdr) & 7, 0, Registers.H_ADDR.get(setHdr), size);
    }
    /** PHASE-1 INTERIM: the read port's setup (8.8) from v1.1's word: the next result's byte offset in data memory, the
     *  size, counter 2. Phase 2 replaces it with v1.2's SRAM_RD_DEST_ADDR. */
    int rdDest() { return (Registers.RD_DEST.get(rdSetup) & 0x7FF) << 2; }
    int rdSize() { return Registers.RD_SIZE.get(rdSetup); }
    int rdCounter() { return 2; }

    // ================================================================ end of cycle
    void endCycle(long t) {
        advance(t);
        if (goAt == t) {
            done = false; landed = false; gate = false; gos++;
            events |= 1 << Registers.EV_GO;
            tile.sram.newPhase();
        }
        if (runEdgeAt == t) {                                     // HALT -> RUN (6.1)
            done = false; landed = false; gate = false; events = 0; qAll = false; java.util.Arrays.fill(qOne, false);
            wakeRequired = 0; wakeMask = Registers.WAKE_MASK_RESET;
            tile.slot.setHalted(t, false);
        }
        if (haltEdgeAt == t) {                                    // RUN -> HALT: conditions, counters, their events and qualifiers (6.1)
            conditions = 0; errorSince = -1; tile.haltReset(t);
            tile.dir.resetCounters();
            events &= ~(0xF | 1 << Registers.EV_COUNTERS_DONE); qAll = false; java.util.Arrays.fill(qOne, false);
        }
        int clearBits = 0;                                        // clears that beat a same-cycle set of [9] and [3:0]
        if (pendT == t) clearBits = apply(t, pendIdx, pendVal);
        if (tile.cmem.landedAt == t) { landed = true; lastLandedAt = t; events |= 1 << Registers.EV_COMMAND; }
        if (halted) done = true;
        conditionEvents(t);
        boolean rd = done && landed;
        if (rd && !readyLevel) { readyPulseAt = t; readies++; }
        readyLevel = rd;
        // a consuming load's clear: only the sticky bits it returned (an event set after its sample point survives)
        if (consumeT == t) events &= ~(consumeBits & ~Registers.WAKE_LIVE & ~Registers.WAKE_PERIPH_LINES);
        // edge events
        boolean rdDone = tile.ag.doneForStatus();
        if (rdDone && !rdDonePrev) events |= 1 << Registers.EV_RUN_DONE;
        rdDonePrev = rdDone;
        boolean rb = tile.req.busy(t);
        if (!rb && requestBusyPrev) events |= 1 << Registers.EV_REQUEST_ROOM;
        requestBusyPrev = rb;
        boolean sb = tile.sender.busy();
        if (!sb && senderBusyPrev && !tile.sender.abandonedAt(t)) events |= 1 << Registers.EV_SENDER_IDLE;
        senderBusyPrev = sb;
        counterEvents(t, clearBits);
        if (halted) events &= ~(0xF | 1 << Registers.EV_COUNTERS_DONE);   // the counters are 0 and their events clear while halted
        if (!cfg.coreless) status = computeStatus(t);              // only a controller can read it (simulator speed)
    }

    /** The per-counter done events [3:0] and counters done [9] (4.12): set on the rising edge of each one's done
     *  condition when its qualifier holds, not while the done gate is closed (an edge there is ignored and consumes the
     *  qualifier); a clear in the same cycle wins. */
    private void counterEvents(long t, int clearBits) {
        int[] r = tile.dir.remaining;
        boolean any = false, gated = gate && cfg.doneGate;
        for (int c = 1; c <= TrafficDirector.N_COUNTERS; c++) {     // v1.2 (4.12): done means landed, by K; mem_idle and cm_idle take no part
            if (r[c] != 0) { qOne[c] = true; any = true; }
            boolean cond = r[c] == 0;
            int bit = evBitOf(c);
            if (cond && !condOnePrev[c] && qOne[c]) {
                qOne[c] = false;
                if (!gated && bit >= 0 && (clearBits & 1 << bit) == 0) events |= 1 << bit;
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
    /** PHASE-1 INTERIM: counter c's done event bit in v1.1's WAKE_EVENTS ([3:0] for counters 1-4; none for 5-7). Phase 2
     *  moves to v1.2's EVENTS ([6:0]). */
    static int evBitOf(int c) { return c <= 4 ? c - 1 : -1; }
    /** Counters done ([9]) is pending (tests; the v1.0 local-wake latch). */
    boolean countersDonePending() { return (events & 1 << Registers.EV_COUNTERS_DONE) != 0; }

    /** The controller's store of cycle t; returns the event bits it clears this cycle (beating a same-cycle set). */
    private int apply(long t, int i, int v) {
        Registers.Reg r = Registers.STORE[i];
        int clears = 0;
        if (r == Registers.DMA_CONFIG) tile.setMasterConfig(v);
        else if (r == Registers.TILE_PULSE) {
            if (Registers.P_GO.get(v) != 0) {                      // [3] GO: wired only on C&C (6.3, 8.6); no mesh tile drives the net
                goPulses++;
                violate(t, "a store of 1 to TILE_PULSE [3], GO, on a tile whose GO pulse isn't wired to the GO net (8.6): ignored");
            }
            if (Registers.P_START.get(v) != 0) tile.ag.pulse(Fields.PULSE_START);   // start and run act before done (8.6)
            if (Registers.P_PERIPH_RUN.get(v) != 0) tile.slot.run(t);
            if (Registers.P_DONE.get(v) != 0) {
                checkInvariant4(t);
                if (!countersZero()) doneEarlyEvents++;            // done early (4.13): a down-counter non-zero
                if ((wakeMask & 1 << Registers.EV_GO) == 0)
                    violate(t, "done pulsed (wait_for_command()) with GO clear in WAKE_MASK: the GO it waits for would not be consumed (item 79, 4.12)");
                done = true; dones++; lastDoneAt = t;
                if (cfg.doneGate) gate = true;                    // the done gate (4.12)
                clears |= 0xF | 1 << Registers.EV_COUNTERS_DONE;  // done clears counters done and the per-counter events, with their qualifiers
                events &= ~clears; qAll = false; java.util.Arrays.fill(qOne, false);
            }
        }
        else if (r == Registers.DMA_RECORD_BASE_ADDRESS) { recordBase = v; tile.setRecordBase(v); }
        else if (Registers.isCounter(r)) {                         // 4.12: a write adds; N > 0 arms; 0 does nothing
            int k = Registers.counterIndex(r) + 1, n = v & TrafficDirector.COUNTER_MASK;
            counterWriteAt[k] = t;                                 // the store's cycle: armed from the end of t
            if (n != 0) {
                long sum = (long) tile.dir.remaining[k] + n;
                if (sum > TrafficDirector.COUNTER_MASK) { counterWraps++; violate(t, TrafficDirector.counterName(k) + " written past 2^20 - 1: the count wraps (4.12)"); }
                tile.dir.remaining[k] = (int) (sum & TrafficDirector.COUNTER_MASK);
                clears |= (evBitOf(k) >= 0 ? 1 << evBitOf(k) : 0) | 1 << Registers.EV_COUNTERS_DONE;
                events &= ~clears; qAll = true; qOne[k] = true;
            }
        }
        else if (r == Registers.ROUTER_SET_HDR) setHdr = v;
        else if (r == Registers.ROUTER_SET_W0) setW[0] = v;
        else if (r == Registers.ROUTER_SET_W1) setW[1] = v;
        else if (r == Registers.ROUTER_SET_W2) setW[2] = v;
        else if (Registers.isSend(r)) {                            // captured at t (8.4): auto-increment; W0-W2 all clear
            sends++; lastSendAt = t; sendsBySize[Registers.sendSize(r)]++;
            int mode = Registers.H_MODE.get(setHdr), a = Registers.H_ADDR.get(setHdr);
            int inc = (mode & 7) == LinkWord.M_WRITE ? LinkWord.bytes(Registers.sendSize(r)) : 0;   // PHASE-1 INTERIM
            if (inc != 0) setHdr = (setHdr & ~(Registers.H_ADDR.mask() << Registers.H_ADDR.lo)) | Registers.H_ADDR.place(a + inc);
            setW[0] = setW[1] = setW[2] = 0;
        }
        else if (r == Registers.SRAM_RD_ADDR_QUEUE) {              // the read was queued (or dropped) at t: the next result's word (8.8)
            int n = rdSize() == LinkWord.SIZE_UNDEF ? 0 : LinkWord.chunks(rdSize());
            rdSetup = (rdSetup & ~Registers.RD_DEST.mask()) | Registers.RD_DEST.place(rdDest() + n);
        }
        else if (r == Registers.SRAM_RD_DEST_ADDR) rdSetup = v;
        else if (r == Registers.WAKE_MASK) wakeMask = v & Registers.WAKE_VALID;
        else if (r == Registers.WAKE_REQUIRED) wakeRequired = v & Registers.WAKE_VALID;
        else if (r == Registers.REQUEST_SET_W0) tile.req.setW(0, v);
        else if (r == Registers.REQUEST_SET_W1) tile.req.setW(1, v);
        else if (r == Registers.REQUEST_SET_W2) tile.req.setW(2, v);
        else if (r == Registers.REQUEST_SEND) tile.req.send(t, v);
        else if (r == Registers.SENDER_SEND) tile.sender.start(t, v);
        else if (r == Registers.SENDER_ADDR) tile.sender.addr = v & 0x1FFFFF;
        else if (r == Registers.UART_TX) tile.uart.tx(t, v);
        else if (r == Registers.UART_STATUS) tile.uart.writeStatus(v);
        else if (r == Registers.UART_DIVISOR) tile.uart.writeDivisor(v);
        return clears;
    }

    /** Invariant 4: done ends a phase only when every counter is done (mem_idle; cm_idle as far as counted packets go:
     *  a command crossing the mesh mid-phase may be in the port-B pipe). A run still issuing is not flagged
     *  (its arrivals elsewhere are counted there). */
    private void checkInvariant4(long t) {
        if (halted) return;
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
        boolean entering = haltEdgeAt == t;                       // a condition set as HALT is entered is cleared (the clear wins, 4.13)
        for (int i = 0; i < Conditions.N; i++) {
            if (c[i] > evPrev[i] && !entering) conditions |= 1 << i;
            evPrev[i] = c[i];
        }
        if (before == 0 && conditions != 0) errorSince = t;       // the error line rises (6.6)
    }
    /** The tile's error line (6.6): TILE_CONDITIONS non-zero, as at the end of the last cycle. */
    boolean errorLine() { return conditions != 0; }

    private int computeStatus(long t) {
        AddressGenerator a = tile.ag;
        int s = Registers.ST_RD_RUNNING.place(a.runningForStatus() ? 1 : 0) | Registers.ST_RD_DONE.place(a.doneForStatus() ? 1 : 0)
                | Registers.ST_GATHER_BUSY.place(tile.gather.busy(t) ? 1 : 0)
                | Registers.ST_SENDER_BUSY.place(tile.sender.busy() ? 1 : 0)
                | Registers.ST_SEND_BUSY.place(tile.sendHold.isEmpty(t) ? 0 : 1)
                | Registers.ST_TS_BUSY.place(tile.tsBusy(t) ? 1 : 0) | Registers.ST_MEM_IDLE.place(tile.memIdle(t) ? 1 : 0)
                | Registers.ST_CM_IDLE.place(tile.dir.cmIdle(t) ? 1 : 0) | Registers.ST_DONE.place(done ? 1 : 0)
                | Registers.ST_LANDED.place(landed ? 1 : 0) | Registers.ST_REQUEST_BUSY.place(tile.req.busy(t) ? 1 : 0);
        s |= Registers.ST_PERIPH_TYPE.place(tile.slot.type()) | Registers.ST_ID.place(idx)
                | Registers.ST_SIZE.place((cfg.memChunks >>> Registers.SIZE_UNIT_SHIFT) - 1);
        return s;
    }
    /** TILE_STATUS as a load sampling at t reads it: periph_running from the peripheral at t. */
    int statusAt(long t) { return status | Registers.ST_PERIPH_RUNNING.place(tile.slot.running(t) ? 1 : 0); }
}
