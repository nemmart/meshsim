/**
 * The address generator (spec 4.2; "DMA read engine" before 2 October): a state machine that walks STREAM (and GROUP)
 * and offers one request per element to the SRAM read queue, at the lowest priority, through its holding register (the
 * SRAM read queue's client 2). If the queue does not take a request, the next put is refused and retried: the address
 * generator waits its turn and nothing advances (a stall).
 *
 * Runs are described by DMA_CONFIG (kind, start, count) and started by TILE_PULSE's start (v1.0, decided 12 October: arm
 * merged into start). Start loads the run and begins the table read-ahead; rd_running is 1 from the start. The first
 * request goes out as soon as its entries are in: 4 ts_engine_latency + 2 cycles after the start's store for a group run
 * (the GROUP read, then the STREAM read), 2 ts_engine_latency + 1 for a stream run. That lead-in depends only on the run
 * kind, the tables and the latency, so a synchronised start stays in lockstep; it is not counted as a read-ahead wait.
 * A start while running (lead-in included) is ignored by the hardware and flagged here. One element per request, and
 * every visit takes one cycle whatever its read size (v1.1: a 128-bit element is one packet; v1.0 took two cycles).
 *
 * A run ends when the generator has walked its last entry, trailing count-0 streams and R = 0 groups included; the last
 * gap-after is not waited (spec 4.2, V1.0-CHANGES item 6). rd_done is cleared by the next start.
 *
 * NO PER-STREAM WORKING STATE. The two banks of 16 stream working
 * registers and the group prefetch are gone. The element of a visit is computed from one iteration counter and the
 * stream's STREAM entry (its three arrays, read in parallel), which is read on every visit: element e is at source +
 * e * (1 << size chunks), and for modes 5 and 8 the destination word is STREAM_DEST's field + e * (1, 2 or 4 words),
 * wrapping mod 2^20 (v1.1; v1.0's mode 5 stepped 2). e is the element counter j in a stream run and the rotation counter
 * r in a group run. STREAM is never written by the engine.
 *
 * Read-ahead: two queues of ag_lookahead entries (Config.agLookahead(), default 2 ts_engine_latency + 2), one per
 * table, each table with its own engine read path, one read per cycle (spec 4.10). The GROUP queue holds entries in
 * group order, from arm, and an entry is freed when its group ends. The STREAM queue holds entries in visit order: a
 * stream run's streams in order; a group run's group streams, R times over (an R = 0 group reads none). An entry occupies its
 * queue from the cycle its read is issued until its visit takes it. The STREAM reader walks the groups whose GROUP
 * entries are in, one group boundary per cycle; it never waits on data, only on the GROUP queue and on room.
 *
 * Stream runs: streams start .. start + count - 1 (mod 256) one after another, each stream's whole run then its gap-after
 * idle cycles; a count-0 stream takes only its gap (count-0, gap-0 streams pass in the same cycle while entries are in,
 * as M7).
 *
 * Group runs: groups start .. start + count - 1, each R rotations of period P = t (v1.1: every visit is one cycle, and a
 * group's streams may have different read sizes, proposed item 27, accepted; v1.0's 2t for 128-bit groups and its
 * one-size-per-group report are gone), then its gap. Group g starts at
 * S_g = S_{g-1} + max(R_{g-1} P_{g-1}, 1) + gap_{g-1} (M14: the 1 for R = 0, spec 4.2 of 10 October; was R P + gap, so an
 * R = 0 group with gap 0 was counted a cycle late), S_0 = the cycle the first group starts (the end of the lead-in); rotation r at S_g + r P. A group
 * starts when its GROUP entry is in and, if R > 0, its first STREAM entry; one group starts per cycle at most. A group
 * that cannot start on its scheduled cycle because its entries are not in yet starts later, and its timetable moves with
 * it (ruling A-3); so does a visit whose STREAM entry is not in yet (a read-ahead underrun). These read-ahead waits
 * depend only on the tables and ts_engine_latency, the same on every tile: they are TIMETABLE, counted separately
 * (groupsLatePrefetch, groupLateBy, underrunCycles), never as lateness. DRIFT (spec 4.13 condition 0): in a group run, the SRAM read queue refused a
 * request; the rotation it delays starts late, which is invariant 3 (rotationsLate).
 */
final class AddressGenerator implements Component {
    static final int MAXSTREAMS = 16, MAXGROUPS = 64;
    final int me;
    final Config cfg;
    final int lookahead;
    WordSink streamReq, groupReq; WordSource streamRet, groupRet;   // the two engine read paths (spec 4.10)
    WordHold out;
    // ---- configuration (DMA_CONFIG) and the start pulse (TILE_PULSE), seen the cycle after the store ----
    int kind, runStart, runCount;
    private boolean pStart;
    // ---- status ----
    boolean running, done;
    private boolean leadIn;                               // from the start until the run's first group starts / first stream is loaded
    long startedAt = -1, doneAt = -1, firstRequestAt = -1;
    long startsWhileRunning;                              // a start while running (lead-in included): ignored, flagged (v1.0 item 8)
    long tableWraps;                                      // runs past the table's end (they wrap), flagged
    long lastTsReq = Long.MIN_VALUE / 2;
    // ---- stats (per run) ----
    long requests, stallCycles, rotations, rotationsLate, maxRotLate, groupsLatePrefetch, maxGroupLate;
    long underrunCycles;                                 // visits waiting for their STREAM entry inside a group (timetable moves)
    long streamReads, groupReads;                         // table reads this run
    int streamQPeak, groupQPeak;                          // queue occupancy peaks this run (reads in flight + entries in)
    final long[] groupStart = new long[MAXGROUPS];       // actual start of each group, relative to S_0
    final long[] groupLateBy = new long[MAXGROUPS];      // read-ahead wait of each group (start + underruns): timetable, not lateness
    long groupStallCycles;                               // refused requests in a group run, this run (drift)
    long zeroGroups, zeroGroupsWithGap;                  // R = 0 groups issued, this run; those with a gap (their cost changed in M14)
    long cumZeroGroups, cumZeroGroupsWithGap, cumGroupStall;   // earlier runs' (accounting)
    long totZeroGroups() { return cumZeroGroups + zeroGroups; }
    long totZeroGroupsWithGap() { return cumZeroGroupsWithGap + zeroGroupsWithGap; }
    long totGroupStall() { return cumGroupStall + groupStallCycles; }
    /** Reserved STREAM encodings and stream errors (4.2, v1.2), errors wherever they appear, count 0 included, checked on
     *  every entry the generator takes (a stream run: once per stream; a group run: on each visit). None has a hardware
     *  check at the sender: the element still issues as written. Read size 11 is undefined (3.1): meshsim reads nothing
     *  and sends a zero payload stamped 11, which the receiver's director flags and drops (meshsim's choice). Errors: read
     *  size 11; mode 6-7; d = 67-125 or 127; a mode-4 stream with d != 0; a mode-3 stream at read size 128; a mode-0 stream
     *  whose destination address isn't aligned to its read size; a source not aligned to its read size (caught when read,
     *  4.1, and flagged here too); a mode-5 stream on counter 0 (invariant 21); and d = 126 from a mesh tile (only the
     *  controllers broadcast from streams, D41 F14: meshsim errors loudly). */
    long sizeViolations, modeViolations, aug128Violations, destViolations, alignViolations; String lastViolation;
    private void checkEntry(long e0, long e1) {
        int m = Fields.sMode(e1), z = Fields.sSize(e1), d = Fields.sD(e1), ctr = Fields.sCounter(e1);
        if (LinkWord.reservedMode(m)) { modeViolations++; lastViolation = "stream with the reserved mode " + m + " (3.1, 4.2: modes 6-7; invariant 21)"; }
        if (z == Fields.SIZE_UNDEF) { sizeViolations++; lastViolation = "stream with read size 11, undefined (3.1, 4.2)"; }
        if (m == LinkWord.M_AUG && z == Fields.SIZE_128) { aug128Violations++; lastViolation = "mode-3 stream with read size 128: its packets are bad packets (3.1, 4.2)"; }
        if (m == LinkWord.M_WRITE && !LinkWord.aligned(Fields.sDest(e0), z)) { alignViolations++; lastViolation = "mode-0 stream whose destination address 0x" + Integer.toHexString(Fields.sDest(e0)) + " is not aligned to its read size: its packets are bad packets (3.1, 4.2; invariant 20)"; }
        if (!LinkWord.chunkAligned(Fields.sSrc(e0), z)) { alignViolations++; lastViolation = "stream whose source chunk 0x" + Integer.toHexString(Fields.sSrc(e0)) + " is not aligned to its read size: its reads are not performed (4.1, 4.2; invariant 20)"; }
        if (d > LinkWord.CC3 && d != LinkWord.BROADCAST) { destViolations++; lastViolation = "stream with d = " + d + " (4.2: 67-125 name no endpoint, 127 is invalid; invariant 17)"; }
        else if (d == LinkWord.BROADCAST && me < 64) { destViolations++; lastViolation = "stream with d = 126 (the broadcast) on mesh tile " + me + " (4.2, D41 F14: only the controllers broadcast from streams; invariant 17)"; }
        if (m == LinkWord.M_GATHER && d != 0) { destViolations++; lastViolation = "mode-4 stream with d = " + d + " (4.2: a gather stream goes to the tile itself, d = 0; invariant 10)"; }
        if (m == LinkWord.M_COUNT && ctr == 0) { modeViolations++; lastViolation = "count-only stream on counter 0: it does nothing (3.1; invariant 21)"; }
    }
    long driftEvents;                                    // monotonic: refused requests in group runs, the drift condition's source
    long cumRequests, cumStall, cumRotLate, cumGrpLate, runs;   // earlier runs' counters (accounting: totals = cum + this run's)
    long totRequests() { return cumRequests + requests; }
    long totStall() { return cumStall + stallCycles; }
    /** Rotations late (a stall in a group run delayed them: drift). Read-ahead waits are timetable and no longer count. */
    long totLate() { return cumRotLate + rotationsLate; }
    /** Read-ahead waits at group starts (timetable; counted as late until M13). */
    long totReadAheadWait() { return cumGrpLate + groupsLatePrefetch; }
    /** rd_running as TILE_STATUS shows it: 1 from the start store's own cycle (the store sets it at the end of that cycle,
     *  so a load right after the store already sees it; spec 8.6: rd_running = 1 from the start, lead-in included). */
    boolean runningForStatus() { return running || pStart; }
    /** rd_done as TILE_STATUS shows it: the start store clears it at the end of its cycle. */
    boolean doneForStatus() { return done && !pStart; }
    /** Cycles from the cycle the start pulse took effect to the run's first request (-1 if none yet). */
    long startToFirstRequest() { return firstRequestAt < 0 || startedAt < 0 ? -1 : firstRequestAt - startedAt; }

    AddressGenerator(int me, Config cfg) {
        this.me = me; this.cfg = cfg; lookahead = cfg.agLookahead();
        if (lookahead < 1) throw new IllegalArgumentException("ag_lookahead >= 1");
        sq = new long[lookahead]; sq1 = new long[lookahead]; cq = new long[lookahead];
    }

    /** A store to TILE_PULSE at the current cycle (or the host side, coreless tests). */
    void pulse(int bits) { if ((bits & Fields.PULSE_START) != 0) pStart = true; }
    void config(int mc) { kind = Fields.mcKind(mc); runStart = Fields.mcStart(mc); runCount = Fields.mcCount(mc); }

    // ================================================================ cycle
    @Override public void runCycle(long t) {
        if (pStart) {
            pStart = false;
            if (running) { startsWhileRunning++; lastViolation = "a start while the address generator is running (its lead-in included): ignored (spec 8.2; V1.0-CHANGES item 8)"; }
            else {
                load(t); begin(t);
                int size = kind == Fields.RUN_STREAMS ? 256 : 64;
                if (runStart + runCount > size) {             // past the table's end: the numbers wrap (spec 8.5); meshsim flags it
                    tableWraps++;
                    lastViolation = "a " + (kind == Fields.RUN_STREAMS ? "stream" : "group") + " run past the table's end (start " + runStart + ", count " + runCount + " > " + size + "): the numbers wrap (spec 8.5)";
                }
                if (runCount == 0) { running = false; leadIn = false; done = true; doneAt = t; finishedIssuing = true; }   // count 0: ends at once, no requests (spec 8.5)
            }
        }
        tableReturn(t);
        tableRequest(t);
        if (running) {
            if (out.refused(t)) {
                out.retry(t); stallCycles++; refusedNow = true; stalledSinceGroup = true;
                if (kind == Fields.RUN_GROUPS) { groupStallCycles++; driftEvents++; }   // drift (4.13): a refusal in a group run
            }
            else { refusedNow = false; if (kind == Fields.RUN_STREAMS) streamIssue(t); else groupIssue(t); }
            // rd_done: the last request has been taken (the holding register was empty at the start of this cycle)
            if (finishedIssuing && !refusedNow && !out.heldAtStart(t) && !putThisCycle) { running = false; done = true; doneAt = t; }
            putThisCycle = false;
        }
        streamQPeak = Math.max(streamQPeak, sqBuf + sqOut); groupQPeak = Math.max(groupQPeak, cqBuf + cqOut);
    }
    private boolean refusedNow, finishedIssuing, putThisCycle;
    long tableWaitCycles;                                // stream runs: a stream's entry not back from STREAM yet

    /** The start: load the run (the old arm's work), counters per run, the read-ahead from empty. */
    private void load(long t) {
        done = false; finishedIssuing = false; firstRequestAt = -1;
        cumRequests += requests; cumStall += stallCycles; cumRotLate += rotationsLate; cumGrpLate += groupsLatePrefetch; runs++;
        cumZeroGroups += zeroGroups; cumZeroGroupsWithGap += zeroGroupsWithGap; cumGroupStall += groupStallCycles;
        requests = stallCycles = rotations = rotationsLate = maxRotLate = groupsLatePrefetch = maxGroupLate = 0;
        groupStallCycles = zeroGroups = zeroGroupsWithGap = 0;
        underrunCycles = streamReads = groupReads = 0; streamQPeak = groupQPeak = 0; tableWaitCycles = 0;
        java.util.Arrays.fill(groupStart, -1); java.util.Arrays.fill(groupLateBy, 0);
        // reads still in flight from an earlier run are dropped when they return (none: a run ends after its reads)
        streamDiscard += sqOut; groupDiscard += cqOut;
        sqBuf = sqHead = sqOut = 0; cqBuf = cqHead = cqOut = 0;
        sReads = 0; cReads = 0; rg = 0; rr = 0; rv = 0;
        slRun = 0; cur = false; g = 0;
    }

    private void begin(long t) {
        running = true; startedAt = t; leadIn = true;
        // S_0 is the cycle the run's first group starts (its entries in): the lead-in, not a read-ahead wait
        if (kind == Fields.RUN_GROUPS) { g = 0; r = 0; v = 0; inRot = false; groupS = t; rotSched = groupS; stalledSinceGroup = false; }
        else gapLeft = 0;
    }

    // ================================================================ the read-ahead queues
    private final long[] sq, sq1, cq;            // rings of `lookahead` entries (STREAM: e0 = SRC | DEST << 32, e1 = COUNT)
    private int sqBuf, sqHead, sqOut;            // STREAM: entries in, ring head, reads in flight
    private int cqBuf, cqHead, cqOut;            // GROUP: the same (the head is the issuer's group g)
    private int streamDiscard, groupDiscard;      // returns of reads from an earlier run, dropped
    private int sReads;                          // stream runs: streams read
    private int cReads;                          // group runs: groups read
    private int rg, rr, rv;                      // group runs: the STREAM reader's group, rotation and visit

    private void tableReturn(long t) {
        if (streamRet.ready(t)) {
            long e0 = streamRet.peekA(t), e1 = streamRet.peekB(t); streamRet.take(t);
            if (streamDiscard > 0) streamDiscard--;
            else { int k = (sqHead + sqBuf) % lookahead; sq[k] = e0; sq1[k] = e1; sqBuf++; sqOut--; }
        }
        if (groupRet.ready(t)) {
            long e = groupRet.peekB(t); groupRet.take(t);
            if (groupDiscard > 0) groupDiscard--;
            else { cq[(cqHead + cqBuf) % lookahead] = e; cqBuf++; cqOut--; }
        }
    }
    /** One STREAM read: its three arrays at index i, in parallel (4.10), one engine read path. */
    private void readStream(long t, int i) { streamReq.put(t, TableStore.OP_READ64, TableStore.STREAM + (i & 255), 0); lastTsReq = t; sqOut++; streamReads++; }
    private void readGroup(long t, int i) { groupReq.put(t, TableStore.OP_READ64, TableStore.GROUP + 2 * (i & 63), 0); lastTsReq = t; cqOut++; groupReads++; }

    private void tableRequest(long t) {
        if (!running) return;
        if (kind == Fields.RUN_STREAMS) {                          // from the start: the first entries read ahead
            if (sReads < runCount && sqBuf + sqOut < lookahead) { readStream(t, runStart + sReads); sReads++; }
            return;
        }
        // GROUP entries in group order, from the start
        if (cReads < runCount && cqBuf + cqOut < lookahead) { readGroup(t, runStart + cReads); cReads++; }
        // STREAM entries in visit order: the reader's group must be in the GROUP queue (an issued group's entry is freed,
        // and an R = 0 group the issuer has passed needs no reads: the reader catches up to the issuer)
        if (rg < g) { rg = g; rr = 0; rv = 0; }
        if (rg >= runCount) return;
        int k = rg - g;
        if (k >= cqBuf) return;                                  // the reader's GROUP entry is not in yet
        long ce = cq[(cqHead + k) % lookahead];
        int R = Fields.cR(ce), tt = Fields.cT(ce);
        if (R == 0) { rg++; rr = 0; rv = 0; return; }            // nothing to read: one group boundary per cycle
        if (sqBuf + sqOut >= lookahead) return;
        readStream(t, Fields.cStart(ce) + rv);
        if (++rv == tt) { rv = 0; if (++rr == R) { rr = 0; rg++; } }
    }
    private long popE1;                                           // the popped entry's STREAM_COUNT word
    private long popStream() { long e = sq[sqHead]; popE1 = sq1[sqHead]; sqHead = (sqHead + 1) % lookahead; sqBuf--; return e; }

    // ================================================================ stream runs
    private int slRun;                                            // streams run
    private boolean cur;                                          // a stream is loaded
    private int cSrc, cDest, cCount, cJ, cD, cMode, cSize, cGap, cCtr;  // the current stream's entry and its element counter j
    private int gapLeft;

    private void streamIssue(long t) {
        if (gapLeft > 0) { gapLeft--; if (gapLeft == 0) endStream(); return; }
        while (!cur) {
            if (slRun >= runCount) return;
            if (sqBuf == 0) { if (!leadIn) tableWaitCycles++; return; }   // the entry is not back yet (table latency; the lead-in is not counted)
            leadIn = false;
            long pe0 = popStream(); loadStream(pe0, popE1); cur = true; checkEntry(pe0, popE1);
            boolean last = slRun == runCount - 1;
            if (cCount == 0) {                                    // count 0: exactly its gap-after cycles, this one the first
                if (cGap == 0 || last) { endStream(); continue; }  // the last entry's gap is not waited (4.2)
                gapLeft = cGap - 1; if (gapLeft == 0) endStream();
                return;
            }
        }
        issueElement(t, cJ);
        cJ++;
        if (cJ == cCount) { if (cGap > 0 && slRun < runCount - 1) gapLeft = cGap; else endStream(); }   // the last gap is not waited (4.2)
    }
    private void endStream() { cur = false; slRun++; if (slRun >= runCount) finishedIssuing = true; }
    private void loadStream(long e0, long e1) {
        cSrc = Fields.sSrc(e0); cDest = Fields.sDest(e0); cGap = Fields.sGap(e0); cD = Fields.sD(e1);
        cCount = Fields.sCount(e1); cMode = Fields.sMode(e1); cSize = Fields.sSize(e1); cCtr = Fields.sCounter(e1); cJ = 0;
    }

    /** One request for element e of the current entry: computed from the entry, nothing stored. */
    private void issueElement(long t, int e) {
        int dst = cD < 64 ? (LinkWord.tileOf(me) + cD) & 63 : cD;                                         // d: relative 0-63, absolute 64- (4.2)
        int dstAddr = 0, n = SRAMReadQueue.chunksOf(cSize);                                                // 1, 2 or 4 (0: size 11)
        if (cMode == LinkWord.M_WRITE) dstAddr = cDest + e * (4 * n);                                     // + 4, 8 or 16 bytes per element, plain addition (4.2)
        int src = (cSrc + e * n) & 0x1FFFFF;                                                               // source + (e << size)
        out.put(t, SRAMReadQueue.reqA(src, cSize, cMode, cCtr, dst), SRAMReadQueue.reqB(dstAddr), 0);
        if (firstRequestAt < 0) firstRequestAt = t;
        requests++; putThisCycle = true;
    }

    // ================================================================ group runs
    private int g, r, v;                          // the issuer's group, rotation counter and visit
    private int gR, gT, gGap;                     // the current group's GROUP entry (the queue head)
    private long groupS, rotSched;
    private boolean inRot;
    private boolean stalledSinceGroup;

    private void groupIssue(long t) {
        if (g >= runCount) { finishedIssuing = true; return; }
        if (!inRot) {
            if (t < rotSched) return;
            if (r == 0) {
                if (cqBuf == 0) return;                               // the group's GROUP entry is not in: it starts late
                long ce = cq[cqHead];
                gR = Fields.cR(ce); gT = Fields.cT(ce); gGap = Fields.cGap(ce);
                if (gR > 0 && sqBuf == 0) return;                     // nor its first STREAM entry
                long lateBy = t - rotSched;
                if (leadIn) { leadIn = false; groupS = t; rotSched = t; lateBy = 0; }   // the lead-in: S_0 is now (not a wait)
                if (lateBy > 0 && !stalledSinceGroup) {               // a read-ahead wait: timetable, the timetable moves with it
                    groupsLatePrefetch++; maxGroupLate = Math.max(maxGroupLate, lateBy); groupLateBy[g] = lateBy;
                    groupS = t; rotSched = t;
                }
                groupStart[g] = t - startedAt;
                stalledSinceGroup = false;
                if (gR == 0) { zeroGroups++; if (gGap > 0) zeroGroupsWithGap++; nextGroup(); return; }
            }
            long lateBy = t - rotSched;
            if (lateBy > 0) { rotationsLate++; maxRotLate = Math.max(maxRotLate, lateBy); }
            inRot = true; v = 0; rotations++;
        }
        // visit stream v of rotation r: its STREAM entry from the queue (one cycle, whatever its read size)
        if (sqBuf == 0) {                                             // underrun: the visit waits, the timetable moves with it
            underrunCycles++; groupS++; groupLateBy[g]++; maxGroupLate = Math.max(maxGroupLate, groupLateBy[g]);
            return;
        }
        long e0 = popStream(), e1 = popE1;
        checkEntry(e0, e1);                                           // every visit, idle or not
        if (r < Fields.sCount(e1)) { loadStream(e0, e1); issueElement(t, r); }
        endVisit();
    }
    private void endVisit() {
        v++;
        if (v < gT) return;
        inRot = false; r++;
        long P = gT;                                                  // period t (v1.1: every visit one cycle)
        if (r < gR) rotSched = groupS + r * P; else nextGroup();
    }
    private void nextGroup() {
        long P = gT;
        groupS = groupS + Math.max((long) gR * P, 1) + gGap; rotSched = groupS;   // R = 0: 1 cycle plus its gap (spec 4.2)
        cqHead = (cqHead + 1) % lookahead; cqBuf--;                  // the group's GROUP entry is freed
        g++; r = 0; inRot = false;
        if (g >= runCount) finishedIssuing = true;
    }

    boolean idle() { return !running; }

    /** HALT holds the engine in reset (13a): IDLE; rd_done keeps its value; reads in flight are dropped on return. */
    void haltReset() {
        if (!running) { pStart = false; return; }
        running = false; leadIn = false; pStart = false; finishedIssuing = false;
        streamDiscard += sqOut; groupDiscard += cqOut; sqBuf = sqHead = sqOut = 0; cqBuf = cqHead = cqOut = 0;
    }
}
