import java.util.ArrayDeque;

/**
 * A tile's control (spec 4.12, 4.13, 6, 8): the register file the controller reaches through its peripheral window,
 * the down-counters' controller side, `done` and "command landed", the READY edge, the GO and RUN/HALT lines as this
 * tile sees them, the local-wake latch, the error conditions (TILE_CONDITIONS) and TILE_STATUS.
 *
 * Timing (spec 4.11): a store at t updates the register at the end of t, and the owning unit acts from t+1; status
 * computed at t is readable at t+1. {@link #endCycle} runs after every unit of the tile has run cycle t, so the order
 * in which the units ran does not matter. Within it the order is:
 *   1. line edges seen at t: GO (clears done and command landed, a new disjoint-buffer phase), HALT -> RUN
 *      (clears done and command landed), RUN -> HALT (clears the conditions; the engines are held in reset, 13a);
 *   2. the controller's store of cycle t (DMA_PULSE: start before done; [3:2] reserved, flagged; a done pulse with a
 *      down-counter non-zero sets done early, and clears the local-wake latch: the done gate);
 *   3. a mode-6 write to mailbox slot 0 at t sets command landed; in HALT, done is forced to 1;
 *   4. new events set conditions; 5. READY on the rising edge of done AND command landed;
 *   6. the local wake: WFI's consumption, then a counter store, clear the latch; the rising edge of "every counter
 *      done" (all three 0, mem_idle, cm_idle) sets it, if some counter has been non-zero since it last set (an
 *      uncounted packet would otherwise make a spurious edge); while done is set (the done gate) an edge is ignored
 *      and consumes the qualifier; 7. TILE_STATUS and TILE_CONDITIONS for t+1.
 *
 * The done gate (spec 4.12): from the done pulse until GO only GO wakes the core. The local wake's latch cannot be set
 * in that window (an edge then is ignored), and the done pulse clears it, so wait_for_command() is woken by GO alone.
 * WFI wakes on GO or the local wake (v1.0: no wake timer; the core has its own).
 */
final class TileControl {
    final Tile tile;
    final Config cfg;
    final int idx;
    // ---- registers (write-only ones hold their value for the units; reads return 0) ----
    int setHdr, setHi, recordBase, rdDest;
    // ---- the controller's store this cycle ----
    private int pendIdx = -1, pendVal; private long pendT = Long.MIN_VALUE;
    // ---- state ----
    boolean done, landed, halted, wakeLatch;
    private boolean wakeCond = true, readyLevel;
    /** The local wake's qualifier (DEV): some counter has been non-zero since the latch last set. Without it, an
     *  uncounted packet (mode 6, or C&C's src-64 traffic) passing through port B or the write path drops cm_idle or
     *  mem_idle for a few cycles and makes a rising edge of "every counter done": a spurious wake. */
    private boolean waiting;
    long latchSets, spuriousEdges;
    private long consumeAt = Long.MIN_VALUE, counterStoreAt = Long.MIN_VALUE;
    int conditions, status, conditionsRead;                       // conditionsRead: TILE_CONDITIONS as a load at t+1 sees it
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
    long goWakes, localWakes;
    // ---- done early, the done gate, the reserved pulses ----
    long doneEarlyEvents, gatedEdges, wakeInDone, reservedPulses;
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

    // ================================================================ the controller's peripheral accesses
    /** A load at t (null = bus error). Write-only registers read 0; status is the value computed at t-1. */
    Integer read(long t, int addr) {
        Registers.Reg r = Registers.at(addr);
        if (r == null) return null;
        if (r == Registers.TILE_STATUS) return status;
        if (r == Registers.DMA_SRAM_REMAINING) return tile.dir.counterAtStart(TrafficDirector.SRAM, t);
        if (r == Registers.DMA_CM_REMAINING) return tile.dir.counterAtStart(TrafficDirector.CM, t);
        if (r == Registers.DMA_REPLY_REMAINING) return tile.dir.counterAtStart(TrafficDirector.REPLY, t);
        if (r == Registers.SRAM_RD_ADDR_QUEUE) return tile.readPort.free(t);
        if (r == Registers.TILE_CONDITIONS) return conditionsRead;
        return 0;
    }
    /** A store at t; false = bus error (unmapped or read-only). ROUTER_SEND_LO is the controller's own (it fills the holding
     *  register in the store's cycle); this records it for the auto-increment. */
    boolean write(long t, int addr, int v) {
        Registers.Reg r = Registers.at(addr);
        if (r == null || !r.writable()) return false;
        pendIdx = r.index; pendVal = v; pendT = t;
        return true;
    }
    /** The packet a ROUTER_SEND_LO store of lo at t sends: {payload, header} from the registers as they stand at t. */
    long sendPayload(int lo) { return (long) setHi << 32 | (lo & 0xFFFFFFFFL); }
    long sendHeader() {
        return LinkWord.hdr(0, Registers.H_DST.get(setHdr), Registers.H_MODE.get(setHdr), Registers.H_ADDR.get(setHdr));
    }
    /** WFI: true if a wake is available at t (GO now, or the local-wake latch), consuming the latch. */
    boolean takeWake(long t, boolean atWfi) {
        if (goVisible(t)) { goWakes++; lastWake = WAKE_GO; if (wakeLatch) consumeAt = t; return true; }
        boolean local = wakeLatch && consumeAt != t;
        if (!local) return false;
        if (done) {                                                // the done gate makes this impossible (Config.doneGate off: test only)
            wakeInDone++;
            violate(t, "a WFI woken by the local wake with done set (wait_for_command()): only GO wakes the core between done and GO (the done gate, spec 4.12)");
        }
        localWakes++; consumeAt = t; lastWake = WAKE_LOCAL;
        return true;
    }
    static final int WAKE_GO = 1, WAKE_LOCAL = 2;
    int lastWake;
    long lastDoneAt = -1, lastLandedAt = -1;

    // ================================================================ end of cycle
    void endCycle(long t) {
        advance(t);
        if (goAt == t) {
            done = false; landed = false; gos++;
            tile.sram.newPhase();
        }
        if (runEdgeAt == t) { done = false; landed = false; }
        if (haltEdgeAt == t) { conditions = 0; errorSince = -1; tile.haltReset(); }   // entering HALT clears every condition (4.13) and resets the engines (13a)
        if (pendT == t) apply(t, pendIdx, pendVal);
        if (tile.cmem.landedAt == t) { landed = true; lastLandedAt = t; }
        if (halted) done = true;
        events(t);
        boolean rd = done && landed;
        if (rd && !readyLevel) { readyPulseAt = t; readies++; }
        readyLevel = rd;
        if (consumeAt == t) wakeLatch = false;
        if (counterStoreAt == t && cfg.counterStoreClearsLatch) wakeLatch = false;   // spec 4.12; false only in DEV 89's test
        boolean cond = everyCounterDone(t);
        int[] r = tile.dir.remaining;
        if (r[0] != 0 || r[1] != 0 || r[2] != 0) waiting = true;
        if (cond && !wakeCond && waiting && done && cfg.doneGate) { waiting = false; gatedEdges++; }   // the done gate: ignored, qualifier consumed
        else if (cond && !wakeCond && waiting) { wakeLatch = true; waiting = false; latchSets++; tile.sram.newPhase(); }   // an engine run ended: a new disjoint-buffer phase (4.1)
        else if (cond && !wakeCond) spuriousEdges++;
        wakeCond = cond;
        if (!cfg.coreless) { status = computeStatus(t); conditionsRead = conditions; }   // only a controller can read them (simulator speed)
    }

    boolean everyCounterDone(long t) {
        int[] r = tile.dir.remaining;
        return r[0] == 0 && r[1] == 0 && r[2] == 0 && tile.memIdle(t) && tile.dir.cmIdle(t);
    }

    private void apply(long t, int i, int v) {
        Registers.Reg r = Registers.ALL[i];
        if (r == Registers.DMA_CONFIG) tile.setMasterConfig(v);
        else if (r == Registers.DMA_PULSE) {
            if ((v & Registers.PULSE_RESERVED_MASK) != 0) {          // reserved: the hardware ignores them
                reservedPulses++;
                violate(t, "a store of 1 to DMA_PULSE [3:2], reserved ([2] was start before v1.0, [3] the clear pulse: spec 8.2)");
            }
            if (Registers.P_START.get(v) != 0) tile.ag.pulse(Fields.PULSE_START);   // start acts before done (8.2)
            if (Registers.P_DONE.get(v) != 0) {
                checkInvariant4(t);
                int[] r3 = tile.dir.remaining;
                if (r3[0] != 0 || r3[1] != 0 || r3[2] != 0) doneEarlyEvents++;   // done early (4.13): a down-counter non-zero
                done = true; dones++; lastDoneAt = t;
                if (cfg.doneGate) wakeLatch = false;               // the done gate (4.12): done clears the latch
            }
        }
        else if (r == Registers.DMA_RECORD_BASE_ADDRESS) tile.setRecordBase(v);
        else if (r == Registers.DMA_SRAM_REMAINING || r == Registers.DMA_CM_REMAINING || r == Registers.DMA_REPLY_REMAINING) {
            int k = r == Registers.DMA_SRAM_REMAINING ? 0 : r == Registers.DMA_CM_REMAINING ? 1 : 2;
            tile.dir.remaining[k] = v & TrafficDirector.COUNTER_MASK; counterStoreAt = t;
        }
        else if (r == Registers.ROUTER_SET_HDR) setHdr = v;
        else if (r == Registers.ROUTER_SET_HI) setHi = v;
        else if (r == Registers.ROUTER_SEND_LO) {                 // captured at t: auto-increment, ROUTER_SET_HI cleared (13a)
            sends++; lastSendAt = t;
            if (Registers.H_DST.get(setHdr) == LinkWord.MEMSUB)
                violate(t, "a packet sent to dst 65, which is unused (the memory subsystem hangs off C&C: spec 7)");
            int mode = Registers.H_MODE.get(setHdr), a = Registers.H_ADDR.get(setHdr);
            int inc = mode == LinkWord.M_CMEM ? 2 : mode == LinkWord.M_MBOX || mode == LinkWord.M_REPLY ? 1 : 0;
            if (inc != 0) setHdr = (setHdr & ~(Registers.H_ADDR.mask() << Registers.H_ADDR.lo)) | Registers.H_ADDR.place(a + inc);
            setHi = 0;
        }
        else if (r == Registers.SRAM_RD_ADDR_QUEUE) rdDest = (rdDest + 1) & 0xFFF;   // the read was queued (or dropped) at t: the next result's word
        else if (r == Registers.SRAM_RD_DEST_ADDR) rdDest = v & 0xFFF;
    }

    /** Invariant 4: done ends a phase only when every counter is done (mem_idle; cm_idle as far as counted packets go:
     *  a mailbox command crossing the mesh mid-phase may be in the port-B pipe). A run still issuing is not flagged
     *  (its arrivals elsewhere are counted there). */
    private void checkInvariant4(long t) {
        if (halted) return;
        int[] r = tile.dir.remaining;
        if (r[0] != 0 || r[1] != 0 || r[2] != 0 || !tile.memIdle(t) || !tile.dir.countedCmIdle(t))
            violate(t, "done pulsed before every expected arrival landed (invariant 4): SRAM_REMAINING " + r[0] + ", CM_REMAINING " + r[1]
                    + ", REPLY_REMAINING " + r[2] + ", mem_idle " + tile.memIdle(t) + ", cm_idle " + tile.dir.cmIdle(t));
    }

    void violate(long t, String m) { violations++; lastViolation = m; }

    // ================================================================ conditions and status
    private void events(long t) {
        long[] c = tile.eventCounts();
        int before = conditions;
        for (int i = 0; i < Conditions.N; i++) {
            if (c[i] > evPrev[i]) conditions |= 1 << i;
            evPrev[i] = c[i];
        }
        if (before == 0 && conditions != 0) errorSince = t;      // the error line rises (6.6)
    }
    /** The tile's error line (6.6): TILE_CONDITIONS non-zero, as at the end of the last cycle. */
    boolean errorLine() { return conditions != 0; }

    private int computeStatus(long t) {
        AddressGenerator a = tile.ag;
        int s = Registers.ST_RD_RUNNING.place(a.runningForStatus() ? 1 : 0) | Registers.ST_RD_DONE.place(a.doneForStatus() ? 1 : 0)
                | Registers.ST_SEND_BUSY.place(tile.sendHold.isEmpty(t) ? 0 : 1)
                | Registers.ST_TS_BUSY.place(tile.tsBusy(t) ? 1 : 0) | Registers.ST_MEM_IDLE.place(tile.memIdle(t) ? 1 : 0)
                | Registers.ST_CM_IDLE.place(tile.dir.cmIdle(t) ? 1 : 0) | Registers.ST_DONE.place(done ? 1 : 0)
                | Registers.ST_LANDED.place(landed ? 1 : 0) | Registers.ST_GATHER_BUSY.place(tile.gather.busy(t) ? 1 : 0);
        s |= Registers.ST_TILE.place(idx) | Registers.ST_SIZE.place((cfg.memChunks >>> Registers.AREA_CHUNKS_SHIFT) - 1);
        return s;
    }
}
