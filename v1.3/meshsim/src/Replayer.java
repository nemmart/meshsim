import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;

/**
 * The replayer, peripheral type 4 (spec 4.19; 4.17.1, D46: type 1 in v1.1), cycle by cycle. It implements the peripheral interface ({@link Peripheral},
 * 4.17, v1.2) and nothing else reaches it. v1.2 (phase 3, as far as the replayer recipes need): its region in bytes
 * (offset [22:20] the part, 4.19.10; the v1.1 word address is offset / 4 inside); its events counted (4.17.3, 4.19.9):
 * [0] run done (depth 1), [2]-[5] the code, read-log, write-log and expansion credits (depth 2, every queue's set to 2
 * by PULSE_RUN and the expansion's again at each new expansion, one returned per half a queue popped), [6] new
 * expansion (depth 1); its ERRORS [0] bad write, [1] queue overrun, [2] invalid expansion, sticky; its words 32-34
 * (ROWS_RUN, EXPANSION_OFFSET, EXPANSION_OPCODE). v1.1's service lines and claims are gone. Row semantics are {@link ReplayerAlu}'s (spec/replayer-opcodes.md, normative).
 *
 * <p>v1.25 (WORKER11; jolt-cpp 821e1bd, DECISIONS-v1.25 s1, s4, s5): the renumbered opcode set with VirtualLoadB/H/W and
 * VirtualStoreB/H/W (class L: a read-log or write-log word each, as LD and SD; the address rounded down to its
 * doubleword); the expansion word's offset_imm [63] (a ShiftData row's shift_offset is the source immediate) and
 * sign_imm [62] (carried into the record, word 0 [86], Proposed); PEXT for any mask; the write log's last-load entry
 * fed by every load row; misaligned memory rows flagged (jolt-cpp aborts on them). The decode table's class 3 is an
 * invalid instruction (universal tables: encodings that aren't valid RV64IMAC). seq_remaining and the controller-fed
 * length count words.
 *
 * <p>v1.25 section 9 (DECISIONS-v1.25 s9): the expansion word's [61:55] (v1.25's untraced [55] and advice_reg [61:56])
 * are reserved; a word with one set is undefined behavior (meshsim stops). HALT is gone from the spec, and with it the
 * replayer's halt input (setHalted), its "PULSE_RUN in HALT" flag and its queue-port-in-HALT bad write.
 *
 * <p>v1.26 (WORKER20; DECISIONS-v1.26, V3-12/23/24): the state record is 68 words in a new order ([0] state word, [1]
 * stream word, [2-65] the register image, [66] the files word, [67] the template bitmap); the replayer holds words
 * 0-65 and ignores writes to 66 and 67. The stream word gives each stream's size as its 128-bit words (padding
 * included) and its values out, and its compression flag ({@link #parseStreamWord}, a provisional layout until
 * WORKER19's spec text lands). The state word keeps two per-pass flags: [115] use the store log and [116] output
 * rd_prev. Part 0 is the working state (item 12): the image is the register file and the state word's pc, instr_idx and
 * offset are the live ones, changed in place by a pass (no copy at PULSE_RUN), so after a pass part 0 holds its end state
 * and the controller reloads words 0-65 before every pass. The read-log and write-log queues are 4 KB ({@link #LOG_QUEUE_ENTRIES}). The write to word 0
 * flushes every queue (contents, decoders, credits to 2), in place of PULSE_RUN. Padding is never decoded. The pops are
 * checked against the sizes: ERRORS [3] code-stream, [4] read-log, [5] write-log underflow (a decode past the stream's
 * words, or a pop past its values; the pass stops), and [6] done with input left over.
 *
 * <p>v1.26.1 (WORKER25; DECISIONS-v1.26 items 13-17): RUN_CONFIG at window word 32 ([23:0] the rows a pass runs, [24] use
 * the store log, [25] output rd_prev; 0 at the reset, sticky, a write while running UB); words 33-35 ROWS_RUN (24 bits),
 * EXPANSION_OFFSET, EXPANSION_OPCODE, the same as the Tracer's. A pass runs RUN_CONFIG's rows, so it can be shorter (or
 * longer) than a window; a count of 0 runs nothing and is done the next cycle, with the leftover check. The state word's
 * [116:112] are reserved and ignored, so the record is loaded verbatim. The stream word is rearranged with the Tracer's
 * advice field, which the replayer ignores.
 *
 * <h3>What is modelled</h3>
 * The mode-9 write port (4.19.10: the map, the sizes, ownership, rp_latency, counting); the four queues as true FIFOs
 * with occupancy, pending and their low-water lines (4.19.8); the three decompressors with the write log's last-load
 * entry (4.19.15); the decode index, the decode table and the ROM (4.19.5, 4.19.6); the rd = x0 policy and the
 * temporary offset (4.19.4); the register file, the fixed 3- and 5-cycle row schedules (4.19.7); pc and instr_idx;
 * the 80-byte record (4.19.12) into a 32-row output queue drained by a sink (one row per cycle by default); the six
 * service lines and two status registers (4.19.9); run, done and conditions 11-13 (4.19.11).
 *
 * <h3>Model choices the spec leaves open</h3> (each is listed in REPLAYER-NOTES.md)
 * <ul>
 * <li>A row starts when its inputs are present: its expansion word, its log word (for a row with from_log), and for a
 *     row with next_from_value either a read-log word or the read log's end (a not-taken branch must not wait for a word
 *     that may never come); and a free output-queue slot. Its results are computed at the start; the register write,
 *     the record push and the pc/instr_idx update happen at the end of its last cycle (cycle 3, or 5 for class M).</li>
 * <li>The front end decodes the next instruction (taking its code value, so the code table updates then) in the
 *     cycle after the current instruction's last row starts, so decode overlaps that row. A controller-fed
 *     instruction raises new expansion at that decode.</li>
 * <li>An instruction's code entries leave the code queue when its last row completes (4.19.7); log entries leave
 *     when their last bit is taken; expansion words when their row starts.</li>
 * </ul>
 */
final class Replayer implements Peripheral {
    static final int ROWS_PER_PASS = 65536;
    /** v1.26 (DECISIONS-v1.26 item 12): no copy at a run's start, part 0 is the working state; v1.25 copied the image in 64. */
    static final int IMAGE_COPY_CYCLES = 0;
    static final int OUTQ_ROWS = 32;
    static final int REC_LONGS = 10;                 // 5 × 128 bits = 80 bytes
    /** Record word 0 bit [86]: the expansion word's sign_imm, so the consumer needs no list of signed-immediate opcodes
     *  (v1.25, DECISIONS-v1.25 s4; the bit's place in the record is WORKER11 Q1, Proposed). Bit 22 of the record's long 1. */
    static final int REC_SIGN_IMM = 22;

    // ================================================================ v1.26: the state record (DECISIONS-v1.26 items 1, 2, 8)
    /** The record's 128-bit words: [0] state, [1] stream, [2-65] the register image, [66] files, [67] template bitmap. */
    static final int REC_WORDS = 68, PART0_WORDS = 66, REC_FILES = 66, REC_BITMAP = 67;
    /** v1.26.1 (DECISIONS-v1.26 items 13-15): RUN_CONFIG, window word 32 (R/W): [23:0] the rows a pass runs, [24] use the
     *  store log, [25] output rd_prev, [31:26] reserved. 0 at the reset and sticky (owner, 2026-11-05). The state word's
     *  [116:112] are reserved: written 0 and ignored, not checked (owner, 2026-11-05); v1.26 had the flags at [115], [116]. */
    static final int RC_ROWS = 0xFFFFFF, RC_STORE_LOG = 1 << 24, RC_RD_PREV = 1 << 25;
    /** [28:26]: the Tracer's per-stream compression enables (code, read log, write log; v1.3; owner, 2026-11-05): held and
     *  read back, ignored by the replayer (never flagged). [31:29] reserved: read 0, stores ignored (WORKER25). */
    static final int RC_TRACER_COMP = 7 << 26, RC_DEFINED = 0x1FFFFFFF;
    /** Window words (4.19.9, 8.11; v1.26.1, item 14: the same on the Tracer). */
    static final int W_RUN_CONFIG = 32, W_ROWS_RUN = 33, W_EXP_OFFSET = 34, W_EXP_OPCODE = 35;
    static int runConfig(int rows, boolean storeLog, boolean rdPrev) {
        if (rows < 0 || rows > RC_ROWS) throw new IllegalArgumentException("rows " + rows + ": 0 to 16,777,215");
        return rows | (storeLog ? RC_STORE_LOG : 0) | (rdPrev ? RC_RD_PREV : 0);
    }
    /** One stream's size and form, as the stream word gives it: its 128-bit words as sent (padding included), its
     *  values out (32-bit code words, 64-bit log words) and whether it is LRU-compressed. */
    record StreamSize(int words, int values, boolean compressed) {
        StreamSize { if (words < 0 || words > 0xFFFF || values < 0 || values > 0x1FFFF) throw new IllegalArgumentException("stream size " + words + " words, " + values + " values"); }
    }
    /** The stream word's layout (v1.26.1, DECISIONS-v1.26 item 16), stream s = 0 code, 1 read log, 2 write log: words
     *  [16s+15:16s] ([15:0], [31:16], [47:32]); values out [48+17s+16:48+17s] ([64:48], [81:65], [98:82]); compressed
     *  [99+s]; the advice values [124:102] (the Tracer's: a replayer ignores them); [127:125] spare. Only these functions
     *  know it. (v1.26: compressed [50:48], values out [80:64], [97:81], [114:98].) */
    static int swWords(int s) { return 16 * s; }
    static int swFlag(int s) { return 99 + s; }
    static int swValues(int s) { return 48 + 17 * s; }
    static final int SW_ADVICE = 102, SW_ADVICE_BITS = 23;
    /** The advice field (the Tracer's; tests and the converters only: the replayer never reads it). */
    static int streamWordAdvice(long lo, long hi) { return (int) bits128(lo, hi, SW_ADVICE, SW_ADVICE_BITS); }
    static StreamSize[] parseStreamWord(long lo, long hi) {
        StreamSize[] r = new StreamSize[3];
        for (int s = 0; s < 3; s++)
            r[s] = new StreamSize((int) bits128(lo, hi, swWords(s), 16), (int) bits128(lo, hi, swValues(s), 17), bits128(lo, hi, swFlag(s), 1) != 0);
        return r;
    }
    /** The stream word for three sizes: {lo, hi} (the inverse of {@link #parseStreamWord}). */
    static long[] streamWord(StreamSize code, StreamSize rlog, StreamSize wlog) { return streamWord(code, rlog, wlog, 0); }
    /** The same with the advice field (0 to 2^22), which replayers ignore. */
    static long[] streamWord(StreamSize code, StreamSize rlog, StreamSize wlog, int adviceValues) {
        if (adviceValues < 0 || adviceValues > 1 << 22) throw new IllegalArgumentException("advice values " + adviceValues + ": 0 to 2^22");
        long[] w = new long[2];
        put128(w, SW_ADVICE, SW_ADVICE_BITS, adviceValues);
        StreamSize[] ss = { code, rlog, wlog };
        for (int s = 0; s < 3; s++) {
            put128(w, swWords(s), 16, ss[s].words()); put128(w, swValues(s), 17, ss[s].values()); put128(w, swFlag(s), 1, ss[s].compressed() ? 1 : 0);
        }
        return w;
    }
    /** n ≤ 64 bits of a 128-bit word {lo, hi} from bit pos. */
    static long bits128(long lo, long hi, int pos, int n) {
        long v = 0;
        for (int k = 0; k < n; k++) { int b = pos + k; long bit = b < 64 ? lo >>> b : hi >>> (b - 64); v |= (bit & 1) << k; }
        return v;
    }
    static void put128(long[] w, int pos, int n, long v) {
        for (int k = 0; k < n; k++) { int b = pos + k; long bit = (v >>> k) & 1; if (b < 64) w[0] = w[0] & ~(1L << b) | bit << b; else w[1] = w[1] & ~(1L << (b - 64)) | bit << (b - 64); }
    }
    /** The state word's hi long for instr_idx and the next offset ([127:112] 0: v1.26.1 has no flags here). */
    static long stateHi(int instrIdx, int nextOffset) {
        return (instrIdx & 0xFFFFFFFFL) | (long) (nextOffset & 0xFFFF) << 32;
    }
    /** The read-log and write-log queues' entries: 256 (4 KB) at v1.26 (V3-12), 512 at v1.25. -Dreplayer.logq=512 gives
     *  v1.25's 8 KB for a control run; set before the replayer is made. */
    static int LOG_QUEUE_ENTRIES = Integer.getInteger("replayer.logq", 256);

    // events (4.19.9): bit numbers
    static final int EV_RUN_DONE = 0, EV_CODE = 2, EV_RLOG = 3, EV_WLOG = 4, EV_EXP = 5, EV_NEWEXP = 6;
    final EventCounts ev = new EventCounts(new int[] { 1, 0, 2, 2, 2, 2, 1 }, new boolean[] { false, false, true, true, true, true, false });
    /** ERRORS (4.19.11): [0] reserved (v1.2's bad write), [1] queue overrun, [2] invalid expansion; v1.26: [3] code-stream,
     *  [4] read-log and [5] write-log underflow, [6] done with input left over; sticky until the reset. */
    static final int ERR_BAD_WRITE = 0, ERR_OVERRUN = 1, ERR_INVALID = 2, ERR_CODE_UNDER = 3, ERR_RLOG_UNDER = 4, ERR_WLOG_UNDER = 5, ERR_LEFTOVER = 6, N_ERR = 7;

    final int me;
    /** RUN_CONFIG (window word 32; v1.26.1): written by a store while stopped, read by PULSE_RUN; 0 at the reset, sticky. */
    private int runConfig;
    /** The pass's rows, RUN_CONFIG [23:0] taken at PULSE_RUN (it can't change while running). v1.26: a fixed 65,536, with a
     *  testing parameter for debug windows, which RUN_CONFIG replaces. */
    private int rowsPerPass;
    private Host host;
    @Override public void attach(Host h) { host = h; }
    private void violate(long t, String m) { violations.add("t" + me + " @" + t + ": " + m); if (host != null) host.violate(t, m); }
    final List<String> violations = new ArrayList<>();

    // ================================================================ memories (mode 9, 4.19.10)
    final long[] region0 = new long[PART0_WORDS * 2];  // state word, stream word, the 64-word register image: lo, hi (record words 66-67 are ignored)
    final short[] decodeTable = new short[2048];
    final long[] romLo = new long[512], romHi = new long[512];

    // ================================================================ queues (4.19.8)
    final Stream code = new Stream("code", 256, 32, false);
    final Stream rlog = new Stream("read log", LOG_QUEUE_ENTRIES, 64, false);
    final Stream wlog = new Stream("write log", LOG_QUEUE_ENTRIES, 64, true);
    { code.credit = EV_CODE; rlog.credit = EV_RLOG; wlog.credit = EV_WLOG; }
    final ExpQueue exp = new ExpQueue();

    // ================================================================ core state
    private boolean running;
    // v1.26 (item 12): the register file is part 0's image (region0[4 + r]); pc, instr_idx and the offset are the state word's
    private long pc; private int instrIdx;
    private boolean fStore, fRdPrev;                   // RUN_CONFIG [24], [25] at PULSE_RUN: use the store log, output rd_prev (v1.26.1)
    private boolean stateWritten;                      // word 0 written (the flush) since the last pass started
    private int startOffset;                           // the state word's next offset, latched
    private long copyDoneAt;                           // first cycle a row may start
    private int rowsStarted, rowsDone;
    private Instr cur, nxt;
    private boolean firstInstr;                        // the next decode is the pass's first instruction
    private long busyUntil = Long.MIN_VALUE;           // last cycle of the row in flight
    private Pending inflight;                          // the row in flight
    private int presented;                             // PERIPH_STATUS_1
    // sticky events: the cycle each was set (Long.MIN_VALUE: clear)
    private long newExpSetAt = Long.MIN_VALUE, doneSetAt = Long.MIN_VALUE;

    // ================================================================ the output queue and its sink (4.19.12)
    private final ArrayDeque<long[]> outQ = new ArrayDeque<>();
    private final ArrayDeque<Long> outAt = new ArrayDeque<>();
    /** The consumer takes one row every {@code sinkInterval} cycles (1: meshsim's default sink, one row per cycle). */
    int sinkInterval = 1;
    private long lastSink = Long.MIN_VALUE / 2;
    /** Every record, in order, as the sink takes it (10 longs: word w's bits [63:0] at 2w, [127:64] at 2w + 1). */
    Consumer<long[]> rowListener;

    // ================================================================ statistics
    enum Stall { IMAGE, CODE, NEWEXP, EXP, RLOG, WLOG, OUTQ }
    final long[] stalls = new long[Stall.values().length];
    long passCycles, rowsTotal, rowsM, rowsCtrl, newExpansions, passes;
    /** PEXT rows whose mask isn't one contiguous run (v1.25: computed and timed like any row; a statistic, 4.19.7). */
    long pextNonContiguous;
    long passStartAt, passEndAt;

    Replayer(int me) { this.me = me; }
    @Override public int type() { return 4; }
    @Override public int params() { return 4; }                    // type 4 (4.17.1, D46), version 0
    @Override public EventCounts events() { return ev; }
    // invariant 16's refill bookkeeping: entries pushed and entries the taken credits allow, per queue
    private final int[] pushed = new int[4], allowance = new int[4];
    private int half(int q) { return q == 0 ? code.half : q == 1 ? rlog.half : q == 2 ? wlog.half : ExpQueue.HALF; }
    { ev.onConsume = b -> { int q = b == EV_CODE ? 0 : b == EV_RLOG ? 1 : b == EV_WLOG ? 2 : b == EV_EXP ? 3 : -1; if (q >= 0) allowance[q] += half(q); }; }
    @Override public int errors(long t) { settle(t); int e = 0; for (int k = 0; k < N_ERR; k++) if (condEvents[k] > 0) e |= 1 << k; return e; }
    @Override public Integer word(long t, int w) {
        settle(t);
        return switch (w) { case W_RUN_CONFIG -> sRunConfig; case W_ROWS_RUN -> sRows & RC_ROWS; case W_EXP_OFFSET -> sStatus0 >>> 16; case W_EXP_OPCODE -> sStatus1; default -> null; };
    }
    /** A store to word 32, RUN_CONFIG (taking effect at the end of t, like every input); words 33-35 are read-only. */
    @Override public int writeWord(long t, int w, int v) {
        if (w == W_RUN_CONFIG) { noteInput(t); inputs.add(new Input(t, IN_CONFIG, v, 0, 0, 0, 0)); return 1; }
        return word(t, w) == null ? -1 : 0;
    }

    // ================================================================ inputs, deferred to the end of their cycle
    private record Input(long t, int kind, int a, int b, int c, long lo, long hi) {}
    private static final int IN_CLAIM = 1, IN_RUN = 2, IN_CONFIG = 3;
    private final ArrayDeque<Input> inputs = new ArrayDeque<>();
    private final ArrayDeque<Input> landing = new ArrayDeque<>();      // accepted packets; kind = src, a = addr, b = size
    private final long[] acceptTimes = new long[8];
    private int acceptN;
    private long settled = Long.MIN_VALUE;
    private long lastInputT = Long.MIN_VALUE;

    private void noteInput(long t) {
        if (t < lastInputT) throw new IllegalStateException("replayer " + me + ": time went backwards " + lastInputT + " -> " + t);
        if (t < settled) throw new IllegalStateException("replayer " + me + ": an input at " + t + " after cycle " + settled + " was settled");
        lastInputT = t;
    }

    /** A mode-0 write into region 0x04 at its byte offset (4.19.10): offset / 4 is v1.1's word address, which the
     *  write port below decodes (offset [22:20] the part, so word address [20:18]; v1.25 CL1: one 1 MB part per queue,
     *  1 code, 2 read log, 3 write log, 4 expansion; parts 5-7, and any offset from 0x800000, unmapped). */
    @Override public void accept(long t, int src, int offset, int size, long lo, long hi) {
        int addr = offset >>> 2;
        noteInput(t);
        if (acceptN > 0 && acceptTimes[(acceptN - 1) & 7] == t) throw new IllegalStateException("replayer " + me + ": two mode-9 packets in cycle " + t);
        acceptTimes[acceptN++ & 7] = t;
        landing.add(new Input(t + RP_LATENCY, src, addr, size, 0, lo, hi));
    }
    @Override public void run(long t) { noteInput(t); inputs.add(new Input(t, IN_RUN, 0, 0, 0, 0, 0)); }
    /** The reset (6.1): idle, events and ERRORS 0, the queues empty; region 0, the tables and the ROM keep. */
    @Override public void reset(long t) {
        inputs.clear(); landing.clear(); settled = Long.MIN_VALUE;
        stopCore(t, false); expDiscard = 0; newExpSetAt = Long.MIN_VALUE; doneSetAt = Long.MIN_VALUE; ev.clearAll();
        code.clear(); rlog.clear(); wlog.clear(); exp.reset(); java.util.Arrays.fill(condEvents, 0); outQ.clear(); outAt.clear();
        runConfig = 0;                                                  // v1.26.1: RUN_CONFIG is 0 after the reset
    }

    int landed(long t) {
        for (int i = 0; i < Math.min(acceptN, 8); i++) if (acceptTimes[(acceptN - 1 - i) & 7] == t - RP_LATENCY) return 1;
        return 0;
    }

    // ================================================================ level reads: the snapshot at the end of t − 1
    private boolean sRunning;
    private int sLines, sStatus0, sStatus1, sRows, sRunConfig;

    @Override public boolean running(long t) { settle(t); return sRunning; }
    @Override public boolean idle(long t) { return !running && inputs.isEmpty() && landing.isEmpty() && outQ.isEmpty(); }
    /** Bring the model to the start of t (the slot calls it before reading the events). */
    @Override public void sync(long t) { settle(t); }
    /** v1.1's PERIPH_STATUS_0/1, for the replayer's own tests: rows done [15:0] | the next offset [31:16]; the presented item. */
    int status(long t, int i) { settle(t); return i == 0 ? sStatus0 : i == 1 ? sStatus1 : 0; }

    // ================================================================ conditions
    final long[] condEvents = new long[N_ERR];
    long conditionEvents(int k) { return condEvents[k]; }

    // ================================================================ no output (4.17.9)


    /**
     * Bring the state to the start of cycle T: apply every input and landing of earlier cycles (end-of-cycle effects,
     * in the order landings, claims, runs within one cycle), then take the snapshot that reads at T return.
     */
    private void settle(long T) {
        if (T <= settled) return;
        while (true) {
            long tl = landing.isEmpty() ? Long.MAX_VALUE : landing.peek().t();
            long ti = inputs.isEmpty() ? Long.MAX_VALUE : inputs.peek().t();
            long tm = Math.min(tl, ti);
            if (tm >= T) break;
            if (tl == tm) { Input p = landing.poll(); land(p.t(), p.kind(), p.a(), p.b(), p.lo(), p.hi()); continue; }
            Input in = inputs.poll();
            switch (in.kind()) {
                case IN_RUN -> doRun(in.t());
                case IN_CONFIG -> {
                    if (running) throw new UndefinedBehavior(in.t(), me, "replayer", "a RUN_CONFIG write while a pass runs (4.19.9; DECISIONS-v1.26 item 13)");
                    runConfig = in.a() & RC_DEFINED; configWrites++;   // [28:26] held for the Tracer's sake, never read
                }
                default -> throw new IllegalStateException();
            }
        }
        settled = T;
        sRunning = running;
        sStatus0 = (rowsDone & 0xFFFF) | (nextOffset() & 0xFFFF) << 16;
        sStatus1 = presented;
        sRows = rowsDone; sRunConfig = runConfig;
    }
    long configWrites;

    private int nextOffset() {
        if (cur != null && !cur.lastStarted) return cur.nextRow;
        if (nxt != null) return nxt.nextRow;
        if (firstInstr) return startOffset;
        return running ? 0 : stoppedOffset;
    }
    private int stoppedOffset;                         // the next offset when the pass ended (a window ending mid-expansion)

    // ================================================================ run (end of its cycle); v1.25: HALT is gone from the spec
    private void doRun(long t) {
        if (running) throw new UndefinedBehavior(t, me, "replayer", "PULSE_RUN while a pass runs (4.17.2; v1.2 ignored it; DECISIONS-v1.25 s8e)");
        // v1.26 (DECISIONS-v1.26 item 4): the queues were flushed by the state word's write, not here; a pass with no
        // write to word 0 since the last one would start on whatever the last pass left (meshsim flags it)
        if (!stateWritten && passes > 0)
            violate(t, "a pass started without a write to the state word since the last pass: its queues weren't flushed (DECISIONS-v1.26 item 4)");
        stateWritten = false;
        // v1.26 (items 10, 12): PULSE_RUN only starts the pass: STATUS [0], rows run cleared, and it runs from part 0 as it is
        // (the image is the register file; the state word's pc, instr_idx and offset are the live ones), the first row at
        // once. The flags, the sizes, the counters, the credits and the decoders were set by the state word's write; the
        // queues hold what landed since (item 11: preloading allowed).
        long sLo = region0[0], sHi = region0[1];
        pc = sLo; instrIdx = (int) sHi; startOffset = (int) (sHi >>> 32) & 0xFFFF;
        // v1.26.1 (items 13, 15): the row count and the flags are RUN_CONFIG's, which can't change while the pass runs
        rowsPerPass = runConfig & RC_ROWS; fStore = (runConfig & RC_STORE_LOG) != 0; fRdPrev = (runConfig & RC_RD_PREV) != 0;
        running = true;
        copyDoneAt = t + 1 + IMAGE_COPY_CYCLES;
        rowsStarted = 0; rowsDone = 0; cur = null; nxt = null; firstInstr = true; inflight = null; busyUntil = Long.MIN_VALUE;
        codeEnded = false; lastStartAt = Long.MIN_VALUE; expDiscard = 0; stoppedOffset = 0;
        srcOf[0] = srcOf[1] = srcOf[2] = srcOf[3] = -1;
        passStartAt = t; passes++;
    }


    /** The write to the state word (word 0) lands: setting the state resets everything (DECISIONS-v1.26 items 4, 10, in
     *  place of PULSE_RUN): the input queues flushed (contents and occupancy; not the output queue), the stream word's
     *  sizes latched (v1.26.1: the state word has no flags), the size (pop) counters cleared, every queue's credits set to 2 (4.19.8) and the
     *  decompressors reset. Pushes that land after it are kept (item 11: preloading). */
    private void flush(long t) {
        code.flush(); rlog.flush(); wlog.flush(); exp.reset();
        latchSizes();
        for (int b = EV_CODE; b <= EV_EXP; b++) ev.set(t, b, 2);
        java.util.Arrays.fill(pushed, 0); java.util.Arrays.fill(allowance, 0);
        stateWritten = true; flushes++;
    }
    /** The stream word's sizes and flags into the three decoders (their counters cleared). At the state word's write (item
     *  10), and again at a write to the stream word itself: state-first loading writes word 1 after word 0, so latching only
     *  at word 0 would take the previous window's sizes. Part 0 is written only while stopped, so this is the same as reading
     *  the stream word at the pass's start (model choice, WORKER20; for WORKER19's text). */
    private void latchSizes() {
        StreamSize[] sz = parseStreamWord(region0[2], region0[3]);
        code.latch(sz[0]); rlog.latch(sz[1]); wlog.latch(sz[2]);
    }
    long flushes;

    /** The pass ends: periph_running falls; done is posted (4.19.11, 4.17.7). */
    private void stopCore(long t, boolean postDone) {
        if (running) {
            passEndAt = t; stoppedOffset = nextOffset();             // PERIPH_STATUS_0 keeps the next offset after the pass
            region0[1] = region0[1] & ~(0xFFFFL << 32) | (long) (stoppedOffset & 0xFFFF) << 32;   // item 12: part 0 holds the end state
        }
        running = false; inflight = null; cur = null; nxt = null;
        if (postDone) { doneSetAt = t; ev.post(t, EV_RUN_DONE); }
    }

    // ================================================================ the write port (4.19.10)
    private final int[] srcOf = { -1, -1, -1, -1 };

    /** Which queue a packet's address names, or null (part 0 or unmapped): one part per queue (v1.25, CL1). */
    private Object queueOf(int addr) {
        int w = addr >>> 18;
        if (w == 1) return code;
        if (w == 2) return rlog;
        if (w == 3) return wlog;
        if (w == 4) return exp;
        return null;
    }

    /** v1.25 (DECISIONS-v1.25 s8e): a bad write (an unmapped offset, a wrong size, a single-ported memory written during a
     *  pass) is undefined behavior: meshsim stops. v1.2 dropped it, counted it and set ERRORS [0], now reserved. */
    private void bad(long t, String why) { throw new UndefinedBehavior(t, me, "replayer", "a bad write: " + why); }
    final List<String> badWrites = new ArrayList<>();

    /** A mode-9 packet lands (or is dropped) at the end of cycle t. */
    private void land(long t, int src, int addr, int size, long lo, long hi) {
        int w = addr >>> 18;                                         // the part, offset [22:20] (and [23] above it)
        if (w >= 5) { bad(t, "unmapped address 0x" + Integer.toHexString(addr)); return; }
        if (size != LinkWord.SIZE_128) { bad(t, "size " + (32 << size) + " at 0x" + Integer.toHexString(addr) + ": only 128-bit packets"); return; }
        if (w == 0) {
            if ((addr & 3) != 0) throw new UndefinedBehavior(t, me, "replayer", "a 128-bit write to part 0 off a 128-bit boundary: word address 0x" + Integer.toHexString(addr) + " (item 47)");
            int wd = addr >>> 2;                                     // 128-bit word index in window 0
            if (wd < PART0_WORDS) {
                if (running) { bad(t, "region 0 written while a pass runs"); return; }
                region0[2 * wd] = lo; region0[2 * wd + 1] = hi;
                if (wd == 0) flush(t);                               // v1.26: the state word's write resets everything (item 10)
                else if (wd == 1) latchSizes();                      // the stream word, landing after word 0 (latchSizes)
            } else if (wd == REC_FILES || wd == REC_BITMAP) {
                if (running) { bad(t, "record word " + wd + " written while a pass runs (part 0; DECISIONS-v1.26 items 9, 11)"); return; }
                ignoredWrites++;                                     // v1.26: the files word and the bitmap, carried and ignored
            } else if (addr >= 0x400 && addr < 0x800) {
                if (running) { bad(t, "decode table written while a pass runs"); return; }
                int e0 = (wd - 0x100) * 8;
                for (int k = 0; k < 8; k++) decodeTable[e0 + k] = (short) ((k < 4 ? lo >>> (16 * k) : hi >>> (16 * (k - 4))) & 0xFFFF);
            } else if (addr >= 0x800 && addr < 0x1000) {
                int j = wd - 0x200; romLo[j] = lo; romHi[j] = hi;    // the ROM's second port: written even while running
            } else bad(t, "unmapped address 0x" + Integer.toHexString(addr));
            return;
        }
        int qi = w - 1;                                              // parts 1-4: code, read log, write log, expansion
        if (running) {
            if (srcOf[qi] >= 0 && srcOf[qi] != src)
                violate(t, "two sources feed one queue port in a pass (invariant 18): " + srcOf[qi] + " and " + src);
            srcOf[qi] = src;
            if (src != me) violate(t, "unit " + src + " wrote the replayer's queue " + qi + " during a pass: only the tile itself refills it (4.19.13; invariant 16)");
            // invariant 16 (4.19.8): no refill exceeds the credit taken for it: pushes since the start (or, for the expansion
            // queue, the new expansion) stay within half a queue per credit the controller has taken
            if (++pushed[qi] > allowance[qi]) violate(t, "queue " + qi + " pushed past the credits taken for it: " + pushed[qi] + " entries, " + allowance[qi] / half(qi) + " credits (4.19.8; invariant 16)");
        }
        boolean ok = qi == 3 ? exp.push(t, lo, hi) : (qi == 0 ? code : qi == 1 ? rlog : wlog).push(t, lo, hi);
        if (!ok) { condEvents[ERR_OVERRUN]++; overruns.add("@" + t + " queue " + qi); }
    }
    final List<String> overruns = new ArrayList<>();
    long ignoredWrites;

    // ================================================================ the cycle
    @Override public void runCycle(long T) {
        settle(T);
        drain(T);
        code.cycle = rlog.cycle = wlog.cycle = exp.cycle = T;
        if (!running) return;
        passCycles++;
        if (rowsPerPass == 0) { checkLeftover(T); stopCore(T, true); return; }   // v1.26.1: a count of 0, done the next cycle (O)
        if (inflight != null && T == busyUntil) finishRow(T);
        if (!running) return;
        if (expDiscard > 0 && exp.occ > 0) { exp.pop(); expDiscard--; }     // one dropped word per cycle (N3)
        frontEnd(T);
        if (!running) return;
        if (T > busyUntil) {
            if (T < copyDoneAt) { stalls[Stall.IMAGE.ordinal()]++; return; }
            if (rowsStarted < rowsPerPass) startRow(T);
        }
    }

    private void drain(long T) {
        if (outAt.isEmpty() || outAt.peek() > T || T - lastSink < sinkInterval) return;
        outAt.poll(); long[] r = outQ.poll(); lastSink = T;
        if (rowListener != null) rowListener.accept(r);
    }

    // ---------------------------------------------------------------- the front end: the next instruction
    /** One native instruction's expansion as the replayer walks it. */
    static final class Instr {
        int w;                      // the opcode-stream word (bit 0 clear: compressed)
        int kind;                   // K_PAD, K_ROM, K_NOP, K_CTRL
        int start;                  // first ROM word (K_ROM)
        int offset;                 // the temporary offset bit (4.19.4)
        int rd0;                    // the template's rd = x0 policy (K_NOP: 1 or 3)
        int nextRow;                // index within the expansion of the next row to start
        boolean lastStarted;        // its last row has started
        int firstRow;               // the row it starts at: the pass's next offset for its first instruction, else 0
        boolean ctrlFirstSeen;      // controller-fed: its first word (row 0) has been seen
        long decodedAt;
        long codeEnd;               // the code stream bit position after its value (entries up to it leave at completion)
    }
    static final int K_PAD = RowCore.K_PAD, K_ROM = RowCore.K_ROM, K_NOP = RowCore.K_NOP, K_CTRL = RowCore.K_CTRL, K_BAD = RowCore.K_BAD;

    private void frontEnd(long T) {
        if (nxt != null || (cur != null && !cur.lastStarted)) return;
        if (rowsStarted >= rowsPerPass || codeEnded) return;
        if (cur != null && lastStartAt == T) return;                  // decode the cycle after the last row starts
        int st = code.peek();
        if (st == Stream.WAIT) return;
        if (st == Stream.END) { codeEnded = true; return; }            // its values used up: an error only when a row needs one (startRow)
        if (st == Stream.OVER) { underflow(T, code, "the code stream's next value runs past its " + code.words + " words"); return; }
        int w = (int) code.peekValue;
        code.commit();
        Instr in = new Instr();
        in.w = w; in.decodedAt = T; in.codeEnd = code.consumed;
        in.nextRow = in.firstRow = firstInstr ? startOffset : 0;
        firstInstr = false;
        if ((w & 3) == 0) { in.kind = K_PAD; }
        else {
            RowCore.decode(w, decodeTable, romLo, dec);                 // 4.19.4, 4.19.5 (RowCore, shared with the Tracer)
            in.kind = dec.kind; in.start = dec.start; in.rd0 = dec.rd0; in.offset = dec.offset;
            // K_BAD: an error when its row would start (a row in flight completes)
            if (in.kind == K_NOP) { in.nextRow = in.firstRow = 0; }
            else if (in.kind == K_CTRL) { in.rd0 = 0; presented = w; newExpSetAt = T; exp.newExpansion(); ev.post(T, EV_NEWEXP); newExpansions++; }
        }
        nxt = in;
    }
    private final RowCore.Decoded dec = new RowCore.Decoded();
    private long lastStartAt = Long.MIN_VALUE;
    private boolean codeEnded;
    private int expDiscard;                            // words of a NOP-substituted controller-fed expansion still to drop

    /** The decode table's index and the source immediate: {@link RowCore}'s (kept here for the tests and tools). */
    static int decodeIndex(int w) { return RowCore.decodeIndex(w); }
    static long sourceImm(int format, int w) { return RowCore.sourceImm(format, w); }

    // ---------------------------------------------------------------- starting a row
    /** A row in flight: what its last cycle commits. */
    private static final class Pending {
        int rdReg = -1;            // the register written, or -1
        long rdVal;
        long newPc; int newIdx; boolean hold;
        long[] rec; boolean lastOfInstr; Instr instr;
    }
    private final ReplayerAlu.Out aluOut = new ReplayerAlu.Out();

    private void startRow(long T) {
        Instr in = cur;
        if (in == null || in.lastStarted) {
            if (nxt == null && codeEnded) { underflow(T, code, "an instruction past the code stream's " + code.valuesOut + " values"); return; }
            if (nxt == null || nxt.decodedAt >= T) { stall(Stall.CODE); return; }
            cur = in = nxt; nxt = null;
        }
        if (in.kind == K_BAD) { invalid(T, "decode-table class 3 for instruction 0x" + Integer.toHexString(in.w)); return; }
        // the row's expansion word
        long lo, hi;
        int rowIdx = in.nextRow;
        switch (in.kind) {
            case K_PAD -> { lo = romLo[0]; hi = romHi[0]; }
            case K_NOP -> { lo = romLo[1]; hi = romHi[1]; }
            case K_ROM -> {
                int a = in.start + rowIdx;
                if (a > 511) { invalid(T, "a template runs past ROM word 511 (start " + in.start + ", row " + rowIdx + ")"); return; }
                lo = romLo[a]; hi = romHi[a];
            }
            default -> {
                if (expDiscard > 0) { stall(Stall.EXP); return; }      // the previous expansion's dropped words are still arriving
                if (exp.occ == 0) { stall(rowIdx == in.firstRow ? Stall.NEWEXP : Stall.EXP); return; }
                lo = exp.headLo(); hi = exp.headHi();
            }
        }
        int op = RowCore.op(lo), cRd = RowCore.cRd(lo), cRs1 = RowCore.cRs1(lo), cRs2 = RowCore.cRs2(lo);
        int seqRem = RowCore.seqRemaining(lo);
        boolean nfv = RowCore.nextFromValue(lo), hold = RowCore.hold(lo), virt = RowCore.virt(lo);
        boolean imm0pc0 = RowCore.imm0pc0(lo), reportC = RowCore.reportC(lo), fromLog = RowCore.fromLog(lo);
        boolean signImm = RowCore.signImm(lo), offsetImm = RowCore.offsetImm(lo);   // v1.25 (DECISIONS-v1.25 s4, s5)
        int format = RowCore.format(lo), rd0w = RowCore.rd0(lo);
        // checks (4.19.11, condition 13)
        // v1.25 (DECISIONS-v1.25 s9): [61:55] are reserved (untraced and advice_reg are gone: the Tracer's templates are the
        // replayer's). A word with one set comes from our own table builder, so it is undefined behavior: meshsim stops.
        if (RowCore.reserved(lo) != 0)
            throw new UndefinedBehavior(T, me, "replayer", String.format("an expansion word with reserved bits [61:55] = 0x%x (%s): lo 0x%016x",
                    RowCore.reserved(lo), in.kind == K_CTRL ? "controller-fed" : "ROM", lo));
        { String why = RowCore.invalidWord(lo); if (why != null) { invalid(T, why); return; } }
        // N3 (owner, 2026-10-27): the rd = x0 policy applies to every word, ROM or controller-fed. For a controller-fed
        // expansion it is read from the first word delivered: word 0, or the word at the pass's next offset (N3a).
        if (in.kind == K_CTRL && !in.ctrlFirstSeen) {
            in.ctrlFirstSeen = true;
            int rdField = (in.w >>> 7) & 31;
            in.offset = rdField == 0 && rd0w == 2 ? 1 : 0;
            if (rdField == 0 && (rd0w == 1 || rd0w == 3) && rowIdx == 0) {
                // NOP policy: the instruction becomes the shared NOP row; this word and the rest of the expansion are
                // dropped as they arrive (seq_remaining says how many follow)
                exp.pop();
                expDiscard = seqRem;
                in.kind = K_NOP; in.rd0 = rd0w;
                startRow(T);
                return;
            }
        }
        // operands
        int w = in.w;
        long srcImm = RowCore.needsSourceImm(lo) ? RowCore.sourceImm(format, w) : 0;
        // shift_offset (v1.25, offset_imm [63]): the source instruction's immediate when set, else 0. Only ShiftData reads it.
        long so = offsetImm ? srcImm : 0;
        if (offsetImm && !ReplayerAlu.isShiftData(op))
            violate(T, "offset_imm set on a " + ReplayerAlu.name(op) + " row (only ShiftData reads shift_offset; WORKER11 Q3)");
        int rRd = reg(cRd, in), rRs1 = reg(cRs1, in), rRs2 = reg(cRs2, in);
        long vRd = rRd >= 0 ? rv(rRd) : RowCore.immVal(cRd, hi, srcImm);
        long vRs1 = rRs1 >= 0 ? rv(rRs1) : RowCore.immVal(cRs1, hi, srcImm);
        long vRs2 = rRs2 >= 0 ? rv(rRs2) : RowCore.immVal(cRs2, hi, srcImm);
        long immSum = (rRd < 0 ? vRd : 0) + (rRs1 < 0 ? vRs1 : 0) + (rRs2 < 0 ? vRs2 : 0);
        // log readiness
        Stream logS = null;
        if (fromLog) logS = ReplayerAlu.isStore(op) ? (fStore ? wlog : null) : rlog;   // SD and VirtualStoreB/H/W: the write log
        if (logS != null) {
            int s = logS.peek();
            if (s == Stream.WAIT) { stall(logS == wlog ? Stall.WLOG : Stall.RLOG); return; }
            if (s == Stream.END) { underflow(T, logS, "a pop past the " + logS.name + "'s " + logS.valuesOut + " values"); return; }
            if (s == Stream.OVER) { underflow(T, logS, "the " + logS.name + "'s next value runs past its " + logS.words + " words"); return; }
        }
        if (nfv) {
            if (logS == rlog) violate(T, "a row with both from_log and next_from_value (two read-log words)");
            else {
                int s = rlog.peek();                                     // a word, or the log's end (its values used up)
                if (s == Stream.WAIT) { stall(Stall.RLOG); return; }
                if (s == Stream.OVER) { underflow(T, rlog, "the read log's next value runs past its " + rlog.words + " words"); return; }
            }
        }
        if (outQ.size() >= OUTQ_ROWS) { stall(Stall.OUTQ); return; }

        // ---- the row starts
        if (in.kind == K_CTRL) exp.pop();
        long logWord = 0;
        if (logS != null) {
            logWord = logS.peekValue; logS.commit(); logS.releaseConsumed();
            if (ReplayerAlu.isLoad(op)) wlog.lastLoad(logWord);            // write-log E[15] (4.19.15): every load row (WORKER11 Q2)
        }
        boolean last = seqRem == 0 || in.kind == K_PAD || in.kind == K_NOP;
        boolean compressed = (w & 1) == 0;
        int pcInc = !last ? 0 : compressed ? 2 : 4;
        ReplayerAlu.exec(op, vRs1, vRs2, vRd, pc, pcInc, imm0pc0, logWord, so, aluOut);
        if ((op == ReplayerAlu.PEXT || op == ReplayerAlu.PEXT_S) && vRs2 != 0 && ((vRs2 >>> Long.numberOfTrailingZeros(vRs2)) & ((vRs2 >>> Long.numberOfTrailingZeros(vRs2)) + 1)) != 0)
            pextNonContiguous++;
        if (aluOut.undefined != null) violate(T, "undefined result (invariant 16): " + aluOut.undefined + " in " + ReplayerAlu.name(op));
        Pending p = new Pending();
        p.instr = in; p.lastOfInstr = last;
        boolean writes = rRd >= 0 && op != ReplayerAlu.NOP;
        if (writes && rRd != 0) { p.rdReg = rRd; p.rdVal = aluOut.rd; }
        long rdAfter = rRd < 0 ? 0 : rRd == 0 ? 0 : writes ? aluOut.rd : vRd;
        // pc and instr_idx (4.19.7, appendix 1.4)
        if (hold) { p.hold = true; p.newPc = pc; p.newIdx = instrIdx; }
        else {
            long np = aluOut.newPc;
            if (np == 0) {
                p.newPc = 0; p.newIdx = 0;
                if (nfv) { long v = popTransfer(T); if (v != 0 && running) violate(T, "the program-end row's log word is " + v + ", not 0"); }
                if (!running) return;
            } else if (np != pc) {
                if (!nfv) violate(T, "a control transfer from a row without next_from_value (" + ReplayerAlu.name(op) + "): the read log is popped anyway");
                p.newPc = np; p.newIdx = (int) popTransfer(T);
                if (!running) return;
            } else { p.newPc = pc + pcInc; p.newIdx = instrIdx + 1; }
        }
        // the record (4.19.12)
        long[] r = new long[REC_LONGS];
        if (in.kind != K_PAD) {
            boolean isVirtual = in.kind == K_NOP ? in.rd0 == 1 : (virt || seqRem > 0 || rowIdx > 0);
            boolean isFirst = isVirtual && (in.kind == K_NOP || rowIdx == 0);
            boolean isComp = compressed && reportC;
            r[0] = (instrIdx & 0xFFFFFFFFL) | (long) op << 32 | (long) Math.max(rRd, 0) << 40 | (long) Math.max(rRs1, 0) << 48 | (long) Math.max(rRs2, 0) << 56;
            r[1] = (seqRem & 0xFFFFL) | (isComp ? 1L : 0) << 16 | (isVirtual ? 1L : 0) << 17 | (isFirst ? 1L : 0) << 18
                    | (rRd < 0 ? 1L : 0) << 19 | (rRs1 < 0 ? 1L : 0) << 20 | (rRs2 < 0 ? 1L : 0) << 21
                    | (signImm ? 1L : 0) << REC_SIGN_IMM;
            // the row's immediate: the immediate slots' sum; for ShiftData its shift_offset (jolt-cpp imm_raw, 821e1bd)
            r[2] = pc; r[3] = ReplayerAlu.isShiftData(op) ? so : immSum;
            r[4] = rRs1 >= 0 ? vRs1 : 0; r[5] = rRs2 >= 0 ? vRs2 : 0;
            r[6] = rRd >= 0 && fRdPrev ? vRd : 0; r[7] = rdAfter;       // v1.26: rd_prev only when the pass outputs it
            // memory rows: LD, SD and (v1.25) the narrow VirtualLoad/StoreB/H/W, whose address is rounded down to its doubleword
            if (ReplayerAlu.isLoad(op)) { r[8] = ReplayerAlu.memAddr(op, vRs1, immSum); r[9] = rdAfter; }
            else if (ReplayerAlu.isStore(op)) { r[8] = ReplayerAlu.memAddr(op, vRs1, immSum); r[9] = logS != null ? logWord : 0; }
            if ((ReplayerAlu.isLoad(op) || ReplayerAlu.isStore(op)) && ((vRs1 + immSum) & 7 & ~ReplayerAlu.floorMask(op)) != 0)
                violate(T, "a misaligned " + ReplayerAlu.name(op) + " at 0x" + Long.toHexString(vRs1 + immSum) + " (jolt-cpp aborts the trace; checked_memory_address)");
        }
        p.rec = r;
        in.nextRow++;
        if (last) { in.lastStarted = true; lastStartAt = T; }
        rowsStarted++; rowsTotal++;
        if (ReplayerAlu.CLASS[op] == 'M') rowsM++;
        if (in.kind == K_CTRL) rowsCtrl++;
        inflight = p;
        busyUntil = T + ReplayerAlu.cycles(op) - 1;
    }

    private long popTransfer(long T) {
        int s = rlog.peek();
        if (s != Stream.AVAIL) { underflow(T, rlog, "a pop past the read log's " + (s == Stream.END ? rlog.valuesOut + " values" : rlog.words + " words") + " (a control transfer's next instr_idx)"); return 0; }
        long v = rlog.peekValue; rlog.commit(); rlog.releaseConsumed();
        return v;
    }

    private long rv(int r) { return r == 0 ? 0 : region0[4 + r]; }

    /** A register operand code's register, or −1 for an immediate code (RowCore, 4.19.3, 4.19.4). */
    private static int reg(int c, Instr in) { return RowCore.reg(c, in.w, in.offset); }

    // ---------------------------------------------------------------- finishing a row
    private void finishRow(long T) {
        Pending p = inflight; inflight = null;
        if (p.rdReg > 0) region0[4 + p.rdReg] = p.rdVal;        // item 12: the image is the register file, written in place
        pc = p.newPc; instrIdx = p.newIdx;
        region0[0] = pc; region0[1] = region0[1] & ~0xFFFFFFFFL | (instrIdx & 0xFFFFFFFFL);   // and the state word's pc, instr_idx
        outQ.add(p.rec); outAt.add(T + 1);
        if (p.lastOfInstr) code.releaseTo(p.instr.codeEnd);
        rowsDone++;
        if (rowsDone == rowsPerPass) { checkLeftover(T); stopCore(T, true); }
    }

    /** v1.26 (DECISIONS-v1.26 items 6, 7): done with input left over, ERRORS [6]: a stream (the write log only when the pass
     *  uses the store log) with values not popped, words not reached, or more entries pushed than its words. One bit; the
     *  detail names the stream. The expansion queue, ended by seq_remaining, isn't checked. */
    private void checkLeftover(long T) {
        StringBuilder b = new StringBuilder();
        for (Stream s : fStore ? new Stream[] { code, rlog, wlog } : new Stream[] { code, rlog }) { String l = s.leftover(); if (l != null) b.append(b.length() > 0 ? "; " : "").append(l); }
        if (b.length() > 0) { condEvents[ERR_LEFTOVER]++; leftovers.add("@" + T + " " + b); }
    }
    final List<String> leftovers = new ArrayList<>();

    /** An underflow (ERRORS [3]-[5], the stream's bit): set at once, and the pass stops as for an invalid expansion. */
    private void underflow(long T, Stream s, String why) {
        condEvents[s == code ? ERR_CODE_UNDER : s == rlog ? ERR_RLOG_UNDER : ERR_WLOG_UNDER]++;
        underflows.add("@" + T + " " + why);
        stopCore(T, true);
    }
    final List<String> underflows = new ArrayList<>();
    /** Every error list, for the harnesses' "clean" checks. */
    boolean clean() { return violations.isEmpty() && invalids.isEmpty() && overruns.isEmpty() && badWrites.isEmpty() && underflows.isEmpty() && leftovers.isEmpty(); }
    String errorReport() { return "violations " + violations + ", invalid " + invalids + ", overruns " + overruns + ", underflows " + underflows + ", leftover " + leftovers; }

    private void invalid(long T, String why) {
        condEvents[ERR_INVALID]++;
        invalids.add("@" + T + " " + why);
        stopCore(T, true);
    }
    final List<String> invalids = new ArrayList<>();

    private void stall(Stall s) { stalls[s.ordinal()]++; }

    // ---------------------------------------------------------------- state for tests and the harness
    long reg(int r) { return r == 0 ? 0 : region0[4 + r]; }
    long pc() { return pc; }
    int instrIdx() { return instrIdx; }
    int rowsDone() { return rowsDone; }
    int nextOffsetNow() { return nextOffset(); }
    boolean isRunning() { return running; }
    boolean storeLogLatched() { return fStore; }
    int runConfigNow() { return runConfig; }
    int rowsPerPassNow() { return rowsPerPass; }
    int outQueueRows() { return outQ.size(); }

    // ================================================================ a code or log queue and its decompressor (4.19.15)
    final class Stream {
        /** peek(): AVAIL a value decoded; WAIT its bits haven't landed; END its values are used up (a pop now is an
         *  underflow); OVER (v1.26) the value's bits run past the stream's words (an underflow at once). */
        static final int AVAIL = 0, WAIT = 1, END = 2, OVER = 3;
        final String name; final int cap, half, valueBits; final boolean isWrite;
        final long[] lo, hi;
        int head, occ, pend, poppedSince;
        int credit = -1;      // its credit bit (4.19.8)
        long base;            // stream bit position of the head entry's bit 0
        long consumed;        // stream bit position of the next unread bit
        int words, valuesOut; // v1.26: the size, latched at the run: 128-bit words (padding included) and values out
        long limit;           // words x 128: no value's bit may lie past it
        int taken;            // values popped this pass
        long endBits = -1;    // the bit position after the last value, once it is popped (0 for an empty stream)
        int landedEntries;    // entries pushed since the flush
        boolean comp;
        final long[] E = new long[16];
        long cycle; int pops; long popCycle = Long.MIN_VALUE;
        // the value peek() decoded, and how to commit it
        long peekValue; int peekBits, peekHit;          // peekHit: −1 a miss, else the index
        long values, hits, misses;

        Stream(String name, int cap, int valueBits, boolean isWrite) {
            this.name = name; this.cap = cap; this.half = cap / 2; this.valueBits = valueBits; this.isWrite = isWrite;
            lo = new long[cap]; hi = new long[cap];
        }
        void clear() { head = 0; occ = 0; pend = 0; base = 0; consumed = 0; poppedSince = 0; landedEntries = 0; taken = 0; endBits = -1; }
        /** v1.26: the state word's write: the queue and its decoder emptied (its table too). */
        void flush() { clear(); java.util.Arrays.fill(E, 0); }
        /** v1.26: a pass's start: the stream word's size and form for this stream. */
        void latch(StreamSize s) { words = s.words(); valuesOut = s.values(); comp = s.compressed(); limit = 128L * words; taken = 0; endBits = valuesOut == 0 ? 0 : -1; }
        /** For tests: flush and latch in one. */
        void reset(boolean compressed, int words, int values) { flush(); latch(new StreamSize(words, values, compressed)); }
        /** Input left over at done, or null: values not popped, words beyond the last value's, or entries pushed past its words. */
        String leftover() {
            if (taken < valuesOut) return name + ": " + (valuesOut - taken) + " of " + valuesOut + " values not popped";
            long used = (endBits + 127) >>> 7;
            if (used < words) return name + ": " + (words - used) + " of " + words + " words after its last value";
            if (landedEntries > words) return name + ": " + (landedEntries - words) + " entries pushed past its " + words + " words";
            return null;
        }

        boolean lowWater() { return cap - occ - pend >= half; }

        /** A push lands at the end of cycle t: dropped if the queue was full before the cycle's pop (4.19.8, 4.13). */
        boolean push(long t, long l, long h) {
            int popsAtT = popCycle == t ? pops : 0;
            if (occ + popsAtT >= cap) return false;
            int i = (head + occ) % cap; lo[i] = l; hi[i] = h; occ++;
            if (pend > 0) pend--;
            landedEntries++;
            return true;
        }

        private long landedBits() { return base + 128L * occ; }
        private long bits(long pos, int n) {          // n ≤ 64 bits from stream position pos, LSB first
            long v = 0;
            for (int k = 0; k < n; ) {
                long p = pos + k; int e = (int) ((p - base) >>> 7), b = (int) ((p - base) & 127);
                int slot = (head + e) % cap;
                long word = b < 64 ? lo[slot] >>> b : hi[slot] >>> (b - 64);
                int take = Math.min(n - k, 64 - (b & 63));
                long m = take == 64 ? -1L : (1L << take) - 1;
                v |= (word & m) << k;
                k += take;
            }
            return v;
        }
        private int need(long n) { if (consumed + n > limit) return OVER; if (consumed + n > landedBits()) return WAIT; return AVAIL; }

        /** Decode the next value without taking it: AVAIL (peekValue set), WAIT (not landed yet), END (its values used up)
         *  or OVER (its bits past the words). The decoder stops at the values out, so padding is never read (v1.26). */
        int peek() {
            if (taken >= valuesOut) return END;
            if (!comp) {
                int s = need(valueBits); if (s != AVAIL) return s;
                peekValue = bits(consumed, valueBits); peekBits = valueBits; peekHit = -1; return AVAIL;
            }
            int s = need(1); if (s != AVAIL) return s;
            if (bits(consumed, 1) == 1) {
                s = need(5); if (s != AVAIL) return s;
                peekHit = (int) bits(consumed + 1, 4); peekValue = E[peekHit]; peekBits = 5; return AVAIL;
            }
            s = need(2); if (s != AVAIL) return s;
            int n = bits(consumed + 1, 1) == 1 ? 64 : 32;
            s = need(2 + n); if (s != AVAIL) return s;
            peekValue = bits(consumed + 2, n); peekBits = 2 + n; peekHit = -1; return AVAIL;
        }

        /** Take the value peek() decoded: advance, and update the table (true LRU, move to front; 4.19.15). */
        void commit() {
            consumed += peekBits; values++;
            if (++taken == valuesOut) endBits = consumed;
            if (!comp) return;
            int lruTop = isWrite ? 14 : 15;
            if (peekHit >= 0) {
                hits++;
                if (isWrite && peekHit == 15) return;                     // the last-load entry: nothing reordered
                long v = E[peekHit];
                System.arraycopy(E, 0, E, 1, peekHit);
                E[0] = v;
            } else {
                misses++;
                System.arraycopy(E, 0, E, 1, lruTop);                     // the old E[lruTop] is dropped
                E[0] = peekValue;
            }
        }
        /** Write log only: an LD row's loaded value goes into E[15]. */
        void lastLoad(long v) { E[15] = v; }

        /** Release the entries wholly consumed, and the one the stream ends inside once it has ended (logs). */
        void releaseConsumed() { releaseTo(consumed); }
        void releaseTo(long pos) {
            while (occ > 0 && (base + 128 <= pos || (endBits >= 0 && pos >= endBits && base < endBits))) {
                head = (head + 1) % cap; occ--; base += 128;
                if (popCycle != cycle) { popCycle = cycle; pops = 0; }
                pops++;
                if (++poppedSince == half) { poppedSince = 0; if (credit >= 0) ev.post(cycle, credit); }   // a credit back per half popped (4.19.8)
            }
        }
    }

    // ================================================================ the expansion queue (4.19.8)
    final class ExpQueue {
        static final int CAP = 256, HALF = 128;
        final long[] lo = new long[CAP], hi = new long[CAP];
        int head, occ, pend, poppedSince;
        boolean newestValid; int newestSeq;
        long cycle; int pops; long popCycle = Long.MIN_VALUE;

        void reset() { head = 0; occ = 0; pend = 0; newestValid = false; poppedSince = 0; }
        /** A new expansion is presented: its credits to 2 again (4.19.8), and the newest-word judgement. */
        void newExpansion() { pend = 0; newestValid = false; poppedSince = 0; ev.set(cycle, EV_EXP, 2); pushed[3] = 0; allowance[3] = 0; }
        boolean lowWater() { return CAP - occ - pend >= HALF && newestValid && newestSeq > pend; }
        boolean push(long t, long l, long h) {
            int popsAtT = popCycle == t ? pops : 0;
            if (occ + popsAtT >= CAP) return false;
            int i = (head + occ) % CAP; lo[i] = l; hi[i] = h; occ++;
            if (pend > 0) pend--;
            newestValid = true; newestSeq = (int) (l >>> 32) & 0xFFF;
            return true;
        }
        long headLo() { return lo[head]; }
        long headHi() { return hi[head]; }
        void pop() {
            head = (head + 1) % CAP; occ--;
            if (popCycle != cycle) { popCycle = cycle; pops = 0; }
            pops++;
            if (++poppedSince == HALF) { poppedSince = 0; ev.post(cycle, EV_EXP); }
        }
    }
}
