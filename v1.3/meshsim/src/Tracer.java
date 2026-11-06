import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The Tracer, C&C 3's peripheral 1, type 3 (spec 4.21; 8.16; DECISIONS-v1.3 sections 1-25; WORKER22, B1): the RV64 row
 * machine that executes the guest as the rows of Jolt's expanded trace and writes each window's run log. Its own core
 * (section 25), sharing with the replayer formats and tables, not the design: the 68-word record, the region map, the
 * universal decode table, ROM and controller-fed templates, and the expansion rules ({@link RowCore}, {@link ReplayerAlu}).
 *
 * <h3>Milestone 1 (this file's state)</h3>
 * Modelled: the region and the record (4.21.2); control: the word-0 write, PULSE_RUN, RUN_CONFIG, words 32-43, the events
 * and the two input credits (4.21.3); execution: fetch through the instruction cache (straddling lines), the RVC
 * decompressor, the decode table, ROM and controller-fed templates with the rd = x0 policy and the temporary offset, the
 * rows of 4.21.4's table (loads, stores, advice, control transfers, the program's end, assertions, padding), the template
 * latch and bitmap (4.21.4); the jump LRU and the map probe; memory: the two 128-line caches, the one-entry write-back
 * buffer, the fill bus to a {@link MemorySystem}, the range comparator and the misalignment rule (4.21.5); the code FIFO
 * and the 66-bit log FIFO, set 1's sizing and the stream word's fields (4.21.6); every ERRORS bit with item 152's stopping
 * rule (4.21.8); 4.21.9's timing; and (milestone 2) the output path of 4.21.6: set 2, the packers, output queues 0-3
 * with their credits and live service flags, queue 0 read in place by a pointer, the drain, done last, and the DMA's
 * side (outStart, outReady, outTake; 128-bit words, 4.17.9).
 *
 * <h3>Model choices (for the report and SPEC-QUESTIONS)</h3>
 * <ul>
 * <li>A row's memory traffic is timed at its start, serially: one read outstanding, in the core's order (fetch halves,
 *     data line, map lines); a dirty data victim's write-back is issued when the read that displaced it completes (no
 *     read is then outstanding), and the next read waits for the bus. So the one-entry buffer never holds a line while a
 *     read is wanted, and the take-back case of 4.21.5 never arises.</li>
 * <li>The row's result is committed at its start; it is visible only to later rows, which start after it (rows are
 *     strictly sequential), and the window words show completed rows only.</li>
 * <li>A row's stall cycles are counted by cause; a row occupies its tr_row_cycles (or tr_mul_cycles) after its inputs are
 *     present (4.21.9: stalls add to the schedule).</li>
 * </ul>
 */
final class Tracer implements Peripheral {
    // ================================================================ the window (8.16)
    static final int TYPE = 3;
    static final int W_RUN_CONFIG = 32, W_ROWS_RUN = 33, W_EXP_OFFSET = 34, W_EXP_OPCODE = 35, W_STATE_LENGTH = 36,
            W_CODE_LENGTH = 37, W_RLOG_LENGTH = 38, W_WLOG_LENGTH = 39, W_HASH_MUL = 40, W_HASH_STEP = 41, W_HASH_LINES = 42,
            W_EXP_TEMPLATE = 43, W_STAT = 44, STAT_N = 8;
    /** The statistics counters, words 44-51 (8.16, item 157), in word order. */
    static final int ST_CYCLES = 0, ST_STALL_FETCH = 1, ST_STALL_DATA = 2, ST_STALL_MAP = 3, ST_STALL_OUTPUT = 4, ST_MISS_I = 5,
            ST_MISS_D = 6, ST_PROBES = 7;
    static final int RC_ROWS = 0xFFFFFF, RC_COMP_CODE = 1 << 26, RC_COMP_RLOG = 1 << 27, RC_COMP_WLOG = 1 << 28, RC_DEFINED = 0x1FFFFFFF;
    static final int MAX_ROWS = 65536;
    // events (4.21.3, item 144)
    static final int EV_RUN_DONE = 0, EV_ADVICE = 3, EV_EXP = 5, EV_NEWEXP = 6, EV_SVC_STATE = 7, EV_SVC_CODE = 8, EV_SVC_RLOG = 9, EV_SVC_WLOG = 10;
    // ERRORS (4.21.8)
    static final int ERR_OVERRUN = 1, ERR_INVALID = 2, ERR_ADVICE_UNDER = 4, ERR_MAP = 7, ERR_STORE = 8, ERR_PAGE = 9,
            ERR_RANGE = 10, ERR_ASSERT = 11, ERR_MISALIGNED = 12;
    // the record (4.19.2, 4.21.2)
    static final int REC_WORDS = 68, REC_FILES = 66, REC_BITMAP = 67;
    // tags in the log FIFO (4.21.6)
    static final int TAG_LOAD = 0, TAG_OTHER = 1, TAG_WRITE = 2;

    final EventCounts ev = new EventCounts(new int[] { 1, 0, 0, 2, 0, 2, 1, 0, 0, 0, 0 },
            new boolean[] { false, false, false, true, false, true, false, false, false, false, false });

    // ================================================================ parameters (4.21.9)
    int rowCycles = 4, mulCycles = 6, probeCompare = 2, probeLines = 3;

    final int me;
    /** The fill bus's other side (4.21.5): set by the unit or the harness before the first run. */
    MemorySystem mem;
    /** The map probe's memory system: {@link #mem} unless a harness separates them (the map's effect on the data, B1). */
    MemorySystem mapMem;
    /** Called when the word-0 write lands (a segment's start): the harness makes the memory system cold (owner, 2026-11-05). */
    Runnable onStateWrite;
    private Host host;

    Tracer(int me) { this.me = me; }
    @Override public int type() { return TYPE; }
    @Override public int params() { return TYPE; }
    @Override public EventCounts events() { return ev; }
    @Override public void attach(Host h) { host = h; }
    final List<String> violations = new ArrayList<>();
    private void violate(long t, String m) { violations.add("@" + t + ": " + m); if (host != null) host.violate(t, m); }
    private UndefinedBehavior ub(long t, String what) { return new UndefinedBehavior(t, me, "Tracer", what); }

    // ================================================================ part 0 (4.21.2)
    /** The record, words 0-67, lo and hi: [0] state word, [1] stream word, [2-65] the register image (the working register
     *  file: register r at rec[4 + r]), [66] the files word, [67] the template bitmap. */
    final long[] rec = new long[REC_WORDS * 2];
    final short[] decodeTable = new short[2048];
    final long[] romLo = new long[512], romHi = new long[512];

    private long reg(int r) { return r == 0 ? 0 : rec[4 + r]; }
    private long pc() { return rec[0]; }
    private int instrIdx() { return (int) rec[1]; }
    private int stateOffset() { return (int) (rec[1] >>> 32) & 0xFFFF; }
    private void setPcIdx(long pc, int idx) { rec[0] = pc; rec[1] = rec[1] & ~0xFFFFFFFFL | (idx & 0xFFFFFFFFL); }
    private void setStateOffset(int off) { rec[1] = rec[1] & ~(0xFFFFL << 32) | (long) (off & 0xFFFF) << 32; }
    // the stream word's advice field [124:102]: rec[3] bits [60:38]
    int adviceLeft() { return (int) ((rec[3] >>> 38) & 0x7FFFFF); }
    private void setAdviceLeft(int n) { rec[3] = rec[3] & ~(0x7FFFFFL << 38) | (long) (n & 0x7FFFFF) << 38; }
    // the files word (4.21.2, 66f399e): memory root [30:0], depth [32:31], map root [63:33], depth [65:64], log2 K [71:66],
    // the I/O region's size [102:72] (0x8000_0000 - lowest_address, exact in bytes), [127:103] spare
    int memRoot() { return (int) (rec[2 * REC_FILES] & 0x7FFFFFFF); }
    int memDepth() { return (int) (rec[2 * REC_FILES] >>> 31) & 3; }
    int mapRoot() { return (int) (rec[2 * REC_FILES] >>> 33) & 0x7FFFFFFF; }
    int mapDepth() { return (int) (rec[2 * REC_FILES + 1] & 3); }
    int log2K() { return (int) (rec[2 * REC_FILES + 1] >>> 2) & 63; }
    long lowest() { return 0x80000000L - ((rec[2 * REC_FILES + 1] >>> 8) & 0x7FFFFFFFL); }   // [102:72] = hi [38:8]
    /** The files word for (memory root, depth, map root, depth, log2 K, lowest_address): a tool's helper. */
    static long[] filesWord(int memRoot, int memDepth, int mapRoot, int mapDepth, int log2K, long lowest) {
        long io = 0x80000000L - lowest;
        if (io < 0 || io >= 1L << 31) throw new IllegalArgumentException(String.format("lowest_address 0x%x: the I/O region must be under 2 GiB below 0x8000_0000", lowest));
        long lo = (memRoot & 0x7FFFFFFFL) | (long) (memDepth & 3) << 31 | (mapRoot & 0x7FFFFFFFL) << 33;
        long hi = (mapDepth & 3) | (long) (log2K & 63) << 2 | io << 8;
        return new long[] { lo, hi };
    }

    // ================================================================ registers (4.21.3)
    private int runConfig, hashMul, hashStep, hashLines;
    private int lengthCode, lengthRlog, lengthWlog;   // words 37-39: 0 from PULSE_RUN until the rows end

    // ================================================================ the input queues (4.21.3)
    static final int QCAP = 256, QHALF = 128;
    private final long[] rqLo = new long[QCAP], rqHi = new long[QCAP];
    private int rqHead, rqOcc, rqHalf, rqPopped;      // rqHalf: the head entry's next value (0 low, 1 high)
    private final long[] eqLo = new long[QCAP], eqHi = new long[QCAP];
    private int eqHead, eqOcc, eqPopped;

    // ================================================================ the caches (4.21.5)
    static final int CACHE_LINES = 128;
    /** The instruction cache's lines: 4.21.5's 128; a parameter for B1's capacity sweep only. */
    int icacheLines = CACHE_LINES;
    static final class Line { final long[] d = new long[16]; boolean dirty, writable; }
    private final LinkedHashMap<Long, Line> icache = new LinkedHashMap<>(256, 0.75f, true);
    private final LinkedHashMap<Long, Line> dcache = new LinkedHashMap<>(256, 0.75f, true);
    static final int LRU_ENTRIES = 64;
    private final LinkedHashMap<Long, Integer> jumpLru = new LinkedHashMap<>(128, 0.75f, true);

    // ================================================================ the FIFOs and set 1 (4.21.6)
    final int[] codeFifo = new int[MAX_ROWS];
    final long[] logFifo = new long[MAX_ROWS];
    final byte[] logTag = new byte[MAX_ROWS];
    int codeN, logN;
    private final LruCoder set1Code = new LruCoder(false), set1Rlog = new LruCoder(false), set1Wlog = new LruCoder(true);
    private int rlogValues, wlogValues;

    // ================================================================ the core
    private boolean running, stopped, rowsEnded, drained;
    private int rowsTarget, rowsDone;
    private long startAt, busyUntil = Long.MIN_VALUE, waitUntil = Long.MIN_VALUE;
    private Stall waitCause;
    private int presented, expansionOffset;           // words 35 and 34
    private int latch = -1;                           // the template latch: -1 none
    private boolean templateStored;                   // an EXPANSION_TEMPLATE store since the last new expansion (item 145)
    private final long[] bitmap = new long[2];

    /** The instruction in progress (it persists across runs: a run boundary inside an expansion doesn't present it again). */
    static final class Instr {
        int w, kind, start, rd0, offset, cls, nextRow;
        boolean ctrlFirstSeen, fetched, presented;
        long codeRun = -1;                            // the run whose code FIFO holds its value
    }
    private Instr cur;
    private int expDiscard;                           // words of a NOP-substituted controller-fed expansion still to drop
    private long runId;
    private final RowCore.Decoded dec = new RowCore.Decoded();
    private final ReplayerAlu.Out alu = new ReplayerAlu.Out();
    private final MemorySystem.Response resp = new MemorySystem.Response();

    // ================================================================ statistics
    enum Stall { IFETCH, DMISS, BUS, PROBE, ADVICE, NEWEXP, EXP }
    final long[] stalls = new long[Stall.values().length];
    long rows, rowsM, rowsPad, instrs, runCycles, runs;
    long iAccess, iMiss, dAccess, dMiss, writebacks, lruHits, lruMisses, probeReads;
    /** The instruction-fetch stalls broken down (B1's report): each instruction-cache miss is cold (the line's first fetch
     *  since the word-0 write) or capacity (fetched before, since evicted), and a first half or a straddling second half
     *  (a 32-bit instruction's second half in the next line); with its stall cycles, and whether its fill reached LPDDR. */
    final long[] iMissBy = new long[4], iCycBy = new long[4];      // [cold first, cold second, capacity first, capacity second]
    long iMissLpddr, straddles;
    private final java.util.HashSet<Long> iSeen = new java.util.HashSet<>();
    long runStartAt, rowsEndAt, drainedAt;
    /** The window's statistics counters, words 44-51 (4.21.3, item 157): 32 bits, wrapping, cleared by the word-0 write and
     *  the reset, counting while STATUS [0] is 1. Each stall cycle is counted in its own cycle (stat, statUntil below), so a
     *  load mid-stall reads exactly the cycles so far. Advice and expansion waits are in none of them. */
    final int[] stat = new int[STAT_N];
    /** Stage 1: the counters tick only while STATUS [0] is 1 (running) or a stall started then runs out; the tile sleeps
     *  only with its peripheral idle, and the Tracer reports idle never (Peripheral.idle's default), so C&C 3's tile never
     *  sleeps and there is nothing to catch up. Should it ever be skipped while counting, fail rather than drift. */
    @Override public void skipCycles(long n) {
        if (n > 0 && (running || statUntil > settled)) throw new IllegalStateException("the Tracer's counters (words 44-51) would tick while its tile sleeps");
    }
    @Override public long[] digestWords() { long[] w = new long[STAT_N]; for (int i = 0; i < STAT_N; i++) w[i] = stat[i]; return w; }
    private long statUntil = Long.MIN_VALUE;          // a data or map stall in progress: its cycles up to (excluding) this
    private int statCause;                            // ST_STALL_DATA or ST_STALL_MAP

    // ================================================================ the errors (4.21.8)
    private int errors;
    final List<String> errorLog = new ArrayList<>();
    /** Sets ERRORS bits; every error but queue overrun stops the Tracer where it is (item 152). */
    private void error(long t, int bits, String why) {
        errors |= bits;
        errorLog.add("@" + t + " ERRORS 0x" + Integer.toHexString(bits) + ": " + why);
        if ((bits & ~(1 << ERR_OVERRUN)) != 0) stopped = true;
    }

    // ================================================================ inputs: deferred to the end of their cycle
    private record Input(long t, int kind, int a, int b, long lo, long hi) {}
    private static final int IN_RUN = 1, IN_WORD = 2;
    private final java.util.ArrayDeque<Input> inputs = new java.util.ArrayDeque<>();
    private final java.util.ArrayDeque<Input> landing = new java.util.ArrayDeque<>();
    private long settled = Long.MIN_VALUE, lastAccept = Long.MIN_VALUE;

    @Override public void accept(long t, int src, int offset, int size, long lo, long hi) {
        if (t == lastAccept) throw new IllegalStateException("Tracer: two region packets in cycle " + t);
        lastAccept = t;
        landing.add(new Input(t + RP_LATENCY, src, offset, size, lo, hi));
    }
    @Override public void run(long t) { inputs.add(new Input(t, IN_RUN, 0, 0, 0, 0)); }

    // ---- level reads: the snapshot at the end of t - 1
    private final int[] sStat = new int[STAT_N];
    private boolean sRunning; private int sErrors, sRows, sExpOffset, sExpOpcode, sRunConfig, sCodeL, sRlogL, sWlogL, sMul, sStep, sLines;

    @Override public boolean running(long t) { settle(t); return sRunning; }
    @Override public void sync(long t) { settle(t); }
    @Override public int errors(long t) { settle(t); return sErrors; }
    @Override public Integer word(long t, int w) {
        settle(t);
        return switch (w) {
            case W_RUN_CONFIG -> sRunConfig; case W_ROWS_RUN -> sRows; case W_EXP_OFFSET -> sExpOffset; case W_EXP_OPCODE -> sExpOpcode;
            case W_STATE_LENGTH -> REC_WORDS; case W_CODE_LENGTH -> sCodeL; case W_RLOG_LENGTH -> sRlogL; case W_WLOG_LENGTH -> sWlogL;
            case W_HASH_MUL -> sMul; case W_HASH_STEP -> sStep; case W_HASH_LINES -> sLines; case W_EXP_TEMPLATE -> 0;
            default -> w >= W_STAT && w < W_STAT + STAT_N ? sStat[w - W_STAT] : null;
        };
    }
    @Override public int writeWord(long t, int w, int v) {
        switch (w) {
            case W_RUN_CONFIG, W_HASH_MUL, W_HASH_STEP, W_HASH_LINES, W_EXP_TEMPLATE -> { inputs.add(new Input(t, IN_WORD, w, v, 0, 0)); return 1; }
            case W_ROWS_RUN, W_EXP_OFFSET, W_EXP_OPCODE, W_STATE_LENGTH, W_CODE_LENGTH, W_RLOG_LENGTH, W_WLOG_LENGTH -> { return 0; }   // a bus error (8.16)
            default -> { return w >= W_STAT && w < W_STAT + STAT_N ? 0 : -1; }    // the counters: read-only, a bus error
        }
    }

    @Override public void reset(long t) {
        inputs.clear(); landing.clear(); settled = Long.MIN_VALUE;
        running = stopped = rowsEnded = drained = false; cur = null; errors = 0; errorLog.clear();
        runConfig = hashMul = hashStep = hashLines = 0; lengthCode = lengthRlog = lengthWlog = 0;
        rqHead = rqOcc = rqHalf = rqPopped = 0; eqHead = eqOcc = eqPopped = 0;
        icache.clear(); dcache.clear(); jumpLru.clear(); wbLine = -1;
        codeN = logN = 0; ev.clearAll(); presented = expansionOffset = 0; latch = -1;
        java.util.Arrays.fill(stat, 0); statUntil = Long.MIN_VALUE;
    }

    /** Applies every input and landing of cycles before T, then takes the snapshot that reads at T return. */
    private void settle(long T) {
        if (T <= settled) return;
        while (true) {
            long tl = landing.isEmpty() ? Long.MAX_VALUE : landing.peek().t();
            long ti = inputs.isEmpty() ? Long.MAX_VALUE : inputs.peek().t();
            long tm = Math.min(tl, ti);
            if (tm >= T) break;
            if (tl == tm) { Input p = landing.poll(); land(p.t(), p.kind(), p.a(), p.b(), p.lo(), p.hi()); continue; }
            Input in = inputs.poll();
            if (in.kind() == IN_RUN) doRun(in.t()); else storeWord(in.t(), in.a(), in.b());
        }
        settled = T;
        sRunning = running; sErrors = errors; sRows = rowsDone; sRunConfig = runConfig;
        sExpOffset = expansionOffset; sExpOpcode = presented;
        sCodeL = lengthCode; sRlogL = lengthRlog; sWlogL = lengthWlog;
        sMul = hashMul; sStep = hashStep; sLines = hashLines;
        System.arraycopy(stat, 0, sStat, 0, STAT_N);
        ev.live(EV_SVC_STATE, rowsEnded && !stopped && q0ptr < REC_WORDS);
        for (int q = 1; q <= 3; q++) ev.live(EV_SVC_STATE + q, rowsEnded && !stopped && oq[q].flag());
    }

    private void storeWord(long t, int w, int v) {
        switch (w) {
            case W_RUN_CONFIG -> {
                if (running) throw ub(t, "a RUN_CONFIG write while running (4.21.3)");
                runConfig = v & RC_DEFINED;
            }
            case W_HASH_MUL -> { if (running) throw ub(t, "a HASH_MUL write while running (4.21.3)"); hashMul = v; }
            case W_HASH_STEP -> { if (running) throw ub(t, "a HASH_STEP write while running (4.21.3)"); hashStep = v & 0xFFFFFF; }
            case W_HASH_LINES -> { if (running) throw ub(t, "a HASH_LINES write while running (4.21.3)"); hashLines = v & 31; }
            case W_EXP_TEMPLATE -> {
                if ((v & 0x80000000) != 0) {
                    int tn = v & 0x7F;
                    bitmap[tn >>> 6] |= 1L << (tn & 63); latch = tn; templateStored = true;
                } else latch = -1;
            }
            default -> throw new IllegalStateException();
        }
    }

    // ================================================================ the write port (4.21.2)
    private void land(long t, int src, int offset, int size, long lo, long hi) {
        int part = offset >>> 20;
        if (size != LinkWord.SIZE_128 || (offset & 15) != 0)
            throw ub(t, "a " + (32 << size) + "-bit or misaligned packet at offset 0x" + Integer.toHexString(offset) + " (4.21.2: 128-bit packets only)");
        switch (part) {
            case 0 -> {
                int wd = offset >>> 4;
                if (offset < 0x440) {
                    if (running) throw ub(t, "a part-0 write while running (4.21.3): record word " + wd);
                    if (wd == REC_BITMAP) return;                        // an output: accepted and ignored
                    rec[2 * wd] = lo; rec[2 * wd + 1] = hi;
                    if (wd == 0) stateWrite(t);
                } else if (offset >= 0x1000 && offset < 0x2000) {
                    if (running) throw ub(t, "a decode-table write while running (4.21.2)");
                    int e0 = ((offset - 0x1000) >>> 4) * 8;
                    for (int k = 0; k < 8; k++) decodeTable[e0 + k] = (short) ((k < 4 ? lo >>> (16 * k) : hi >>> (16 * (k - 4))) & 0xFFFF);
                } else if (offset >= 0x2000 && offset < 0x4000) {
                    if (running) throw ub(t, "a ROM write while running (item 156: single-ported)");
                    int j = (offset - 0x2000) >>> 4; romLo[j] = lo; romHi[j] = hi;
                } else throw ub(t, "a write to an unmapped part-0 offset 0x" + Integer.toHexString(offset));
            }
            case 2 -> {
                if (rqOcc >= QCAP) { error(t, 1 << ERR_OVERRUN, "a push into the full read queue (dropped)"); return; }
                int i = (rqHead + rqOcc) % QCAP; rqLo[i] = lo; rqHi[i] = hi; rqOcc++;
            }
            case 4 -> {
                if (eqOcc >= QCAP) { error(t, 1 << ERR_OVERRUN, "a push into the full expansion queue (dropped)"); return; }
                int i = (eqHead + eqOcc) % QCAP; eqLo[i] = lo; eqHi[i] = hi; eqOcc++;
            }
            default -> throw ub(t, "a write to " + (part == 1 || part == 3 ? "unused" : "unmapped") + " part " + part + " (4.21.2)");
        }
    }

    /** The write to word 0 lands (4.21.3): every queue emptied, the credits set, the counters cleared, the caches and the
     *  jump LRU invalidated without writing back dirty lines, the template latch and bitmap cleared. */
    private void stateWrite(long t) {
        // word 0 lands before word 1 (state first), so the advice field is still the last segment's leftover here
        if (adviceLeft() > 0)
            violate(t, "a word-0 write with advice still unconsumed (" + adviceLeft() + " values): a mid-segment rewrite is UB (4.21.3)");
        rqHead = rqOcc = rqHalf = rqPopped = 0; eqHead = eqOcc = eqPopped = 0; expDiscard = 0;
        codeN = logN = 0; lengthCode = lengthRlog = lengthWlog = 0; rowsEnded = false; drained = false;
        q0ptr = REC_WORDS; for (int q = 1; q <= 3; q++) oq[q].reset();
        ev.set(t, EV_ADVICE, 2); ev.set(t, EV_EXP, 2);
        icache.clear(); dcache.clear(); jumpLru.clear(); wbLine = -1; iSeen.clear();
        latch = -1; bitmap[0] = bitmap[1] = 0; cur = null;
        java.util.Arrays.fill(stat, 0); statUntil = Long.MIN_VALUE;
        if (onStateWrite != null) onStateWrite.run();
        stateWrites++;
    }
    long stateWrites;

    // ================================================================ PULSE_RUN (4.21.3)
    private void doRun(long t) {
        if (running) throw ub(t, "PULSE_RUN while a run is in progress (4.17.2)");
        int n = runConfig & RC_ROWS;
        if (n > MAX_ROWS) throw ub(t, "RUN_CONFIG [23:0] = " + n + " rows: more than 65,536 is UB on the Tracer (4.21.3)");
        if (codeN != 0 || logN != 0) throw new IllegalStateException("PULSE_RUN with the last window's FIFOs not drained");
        running = true; rowsEnded = false; drained = false; done = false; q0ptr = REC_WORDS; xferLeft = 0;
        for (int q = 1; q <= 3; q++) oq[q].reset();
        rowsTarget = n; rowsDone = 0; startAt = t + 1; runId++; runs++; runStartAt = t;
        set1Code.reset(); set1Rlog.reset(); set1Wlog.reset(); rlogValues = wlogValues = 0;
        lengthCode = lengthRlog = lengthWlog = 0;
        bitmap[0] = bitmap[1] = 0;
        if (cur != null && cur.kind == RowCore.K_CTRL && cur.nextRow > 0 && latch >= 0) bitmap[latch >>> 6] |= 1L << (latch & 63);
    }

    // ================================================================ the cycle
    @Override public void runCycle(long T) {
        settle(T);
        if (expDiscard > 0 && eqOcc > 0) { eqPop(T); expDiscard--; }      // a NOP-substituted expansion's words, dropped one a cycle (N3)
        if (running) stat[ST_CYCLES]++;                   // STATUS [0] is 1: the drain included, and after a stopping error
        if (!running || stopped) return;
        if (rowsEnded) { drainCycle(T); return; }
        runCycles++;
        if (T < startAt) return;
        if (T < statUntil) stat[statCause]++;
        if (busyUntil >= T) {
            if (T == busyUntil) { rowsDone++; if (rowsDone == rowsTarget) endRows(T); }
            return;
        }
        if (rowsTarget == 0) { endRows(T); return; }
        if (T < waitUntil) { stalls[waitCause.ordinal()]++; if (waitCause == Stall.IFETCH) stat[ST_STALL_FETCH]++; return; }
        step(T);
    }

    /** Starts the next row at T if its inputs are present; else counts the stall. */
    private void step(long T) {
        // ---- the instruction
        if (cur == null) {
            cur = new Instr();
            cur.nextRow = stateOffset();                  // a segment's start may be inside an expansion (4.21.4)
        }
        Instr in = cur;
        if (!in.fetched) {
            if (pc() == 0) { in.w = 0; in.kind = RowCore.K_PAD; in.fetched = true; in.nextRow = 0; }
            else {
                long ready = fetch(T, in);
                if (stopped) return;
                in.fetched = true;
                if (ready > T) { waitUntil = ready; waitCause = Stall.IFETCH; stalls[Stall.IFETCH.ordinal()]++; stat[ST_STALL_FETCH]++; return; }
            }
        }
        if (in.kind == RowCore.K_CTRL && !in.presented) {
            in.presented = true;
            presented = in.w; expansionOffset = in.nextRow;
            ev.set(T, EV_EXP, 2); eqPopped = 0;
            ev.post(T, EV_NEWEXP);
            latch = -1; templateStored = false;
            newExpansions++;
        }
        if (in.kind == RowCore.K_BAD) { error(T, 1 << ERR_INVALID, in.cls == 3 ? "decode-table class 3 for 0x" + Integer.toHexString(in.w) : "a 16-bit encoding with no 32-bit form"); return; }

        // ---- the row's expansion word
        long lo, hi;
        int rowIdx = in.nextRow;
        switch (in.kind) {
            case RowCore.K_PAD -> { lo = romLo[0]; hi = romHi[0]; }
            case RowCore.K_NOP -> { lo = romLo[1]; hi = romHi[1]; }
            case RowCore.K_ROM -> {
                int a = in.start + rowIdx;
                if (a > 511) { error(T, 1 << ERR_INVALID, "a template past ROM word 511"); return; }
                lo = romLo[a]; hi = romHi[a];
            }
            default -> {
                if (expDiscard > 0) { stalls[Stall.EXP.ordinal()]++; return; }     // the previous expansion's dropped words still arriving
                if (eqOcc == 0) { stalls[(in.ctrlFirstSeen ? Stall.EXP : Stall.NEWEXP).ordinal()]++; return; }
                lo = eqLo[eqHead]; hi = eqHi[eqHead];
            }
        }
        if (RowCore.reserved(lo) != 0) throw ub(T, String.format("an expansion word with reserved bits [61:55] set: 0x%016x", lo));
        { String why = RowCore.invalidWord(lo); if (why != null) { error(T, 1 << ERR_INVALID, why); return; } }
        int op = RowCore.op(lo), cRd = RowCore.cRd(lo), cRs1 = RowCore.cRs1(lo), cRs2 = RowCore.cRs2(lo);
        int seqRem = RowCore.seqRemaining(lo), format = RowCore.format(lo), rd0w = RowCore.rd0(lo);
        boolean nfv = RowCore.nextFromValue(lo), hold = RowCore.hold(lo), imm0pc0 = RowCore.imm0pc0(lo), fromLog = RowCore.fromLog(lo);
        boolean offsetImm = RowCore.offsetImm(lo);
        // the rd = x0 policy for a controller-fed expansion, read from the first word delivered (4.19.4, N3)
        if (in.kind == RowCore.K_CTRL && !in.ctrlFirstSeen) {
            in.ctrlFirstSeen = true;
            int rdField = (in.w >>> 7) & 31;
            in.offset = rdField == 0 && rd0w == 2 ? 1 : 0;
            if (rdField == 0 && (rd0w == 1 || rd0w == 3) && rowIdx == 0) {
                eqPop(T); expDiscard = seqRem; in.kind = RowCore.K_NOP; in.rd0 = rd0w;
                stalls[Stall.EXP.ordinal()]++;                // the NOP row starts next cycle
                return;
            }
        }
        // ---- operands (RowCore, shared with the replayer)
        int w = in.w;
        long srcImm = RowCore.needsSourceImm(lo) ? RowCore.sourceImm(format, w) : 0, so = offsetImm ? srcImm : 0;
        int rRd = RowCore.reg(cRd, w, in.offset), rRs1 = RowCore.reg(cRs1, w, in.offset), rRs2 = RowCore.reg(cRs2, w, in.offset);
        long vRd = rRd >= 0 ? reg(rRd) : RowCore.immVal(cRd, hi, srcImm);
        long vRs1 = rRs1 >= 0 ? reg(rRs1) : RowCore.immVal(cRs1, hi, srcImm);
        long vRs2 = rRs2 >= 0 ? reg(rRs2) : RowCore.immVal(cRs2, hi, srcImm);
        long immSum = (rRd < 0 ? vRd : 0) + (rRs1 < 0 ? vRs1 : 0) + (rRs2 < 0 ? vRs2 : 0);

        boolean isMem = fromLog && (ReplayerAlu.isLoad(op) || ReplayerAlu.isStore(op));
        boolean isAdvice = fromLog && op == ReplayerAlu.ADVICE;
        // ---- polled inputs first: advice (4.21.4)
        long adviceVal = 0;
        if (isAdvice) {
            if (adviceLeft() == 0) { error(T, 1 << ERR_ADVICE_UNDER, "an advice row with the advice field at 0"); return; }
            if (rqOcc == 0) { stalls[Stall.ADVICE.ordinal()]++; return; }
            adviceVal = rqHalf == 0 ? rqLo[rqHead] : rqHi[rqHead];
        }
        boolean last = seqRem == 0 || in.kind == RowCore.K_PAD || in.kind == RowCore.K_NOP;

        // ---- the row starts: everything below commits
        long ready = T; busWait = 0;
        if (in.kind == RowCore.K_CTRL) eqPop(T);
        if (isAdvice) popAdvice(T);
        long logWord = 0; int tag = -1;
        long memA = 0; Line memLine = null; int memWord = 0;
        if (isMem) {
            long ea = vRs1 + immSum;
            memA = ReplayerAlu.memAddr(op, vRs1, immSum);
            long o = memA - lowest();
            int bad = 0;
            if (Long.compareUnsigned(o, 8L << log2K()) >= 0) bad |= 1 << ERR_RANGE;      // the range comparator (8 K, Q9)
            if ((memA & 7) != 0) bad |= 1 << ERR_MISALIGNED;                             // the floor address (item 150)
            if (bad != 0) { error(T, bad, String.format("%s at 0x%x (ea 0x%x)%s%s", ReplayerAlu.name(op), memA, ea,
                    (bad & 1 << ERR_RANGE) != 0 ? ": outside the provable range" : "", (bad & 1 << ERR_MISALIGNED) != 0 ? ": misaligned" : "")); return; }
            ready = dataLine(T, o >>> 7);
            if (stopped) return;
            memLine = dcache.get(o >>> 7); memWord = (int) ((o >>> 3) & 15);
            if (ReplayerAlu.isStore(op) && !memLine.writable) {
                error(T, 1 << ERR_STORE, String.format("a store to 0x%x, whose line isn't writable (a hole or a page not owned)", memA)); return;
            }
            logWord = memLine.d[memWord];                                                // the loaded doubleword, or the pre-value
            tag = ReplayerAlu.isLoad(op) ? TAG_LOAD : TAG_WRITE;
        } else if (isAdvice) { logWord = adviceVal; tag = TAG_OTHER; }

        boolean compressed = (w & 1) == 0;
        int pcInc = !last ? 0 : compressed ? 2 : 4;
        long pc = pc();
        ReplayerAlu.exec(op, vRs1, vRs2, vRd, pc, pcInc, imm0pc0, logWord, so, alu);
        if (alu.undefined != null) violate(T, "undefined result: " + alu.undefined + " in " + ReplayerAlu.name(op));
        // assertions (4.21.4: evaluated, appendix 2 H)
        if (!assertionHolds(op, vRs1, vRs2)) { error(T, 1 << ERR_ASSERT, ReplayerAlu.name(op) + " failed: rs1 0x" + Long.toHexString(vRs1) + ", rs2 0x" + Long.toHexString(vRs2)); return; }
        // pc and instr_idx (4.19.7, 4.21.4)
        long newPc; int newIdx;
        if (hold) { newPc = pc; newIdx = instrIdx(); }
        else {
            long np = alu.newPc;
            if (np == 0 && op == ReplayerAlu.JAL && imm0pc0 && vRs1 == 0) {               // the program's end (any JAL with immediate 0)
                if (!nfv) violate(T, "the program's end on a JAL row without next_from_value");
                newPc = 0; newIdx = 0; logWord = 0; tag = TAG_OTHER;
            } else if (np != pc) {
                if (np == 0 && op != ReplayerAlu.JAL) { error(T, 1 << ERR_MAP, ReplayerAlu.name(op) + " to pc 0 (4.21.4, item 146)"); return; }
                if (!nfv) violate(T, "a control transfer from a row without next_from_value (" + ReplayerAlu.name(op) + ")");
                long base = lookup(T, np);
                if (stopped) return;
                ready = Math.max(ready, lookupReady);
                newPc = np; newIdx = (int) base; logWord = base & 0xFFFFFFFFL; tag = TAG_OTHER;
            } else { newPc = pc + pcInc; newIdx = instrIdx() + 1; }
        }
        // commit: memory, registers, the FIFOs
        if (memLine != null && ReplayerAlu.isStore(op)) { memLine.d[memWord] = vRs2; memLine.dirty = true; }
        boolean writes = rRd >= 0 && op != ReplayerAlu.NOP;
        if (writes && rRd != 0) rec[4 + rRd] = alu.rd;
        if (in.codeRun != runId) { pushCode(in.w); in.codeRun = runId; }
        if (tag >= 0) pushLog(tag, logWord);
        if (in.kind == RowCore.K_CTRL && in.cls == 1 && !templateStored)
            violate(T, "an inline expansion completed a row with no EXPANSION_TEMPLATE store since its new expansion (item 145)");
        setPcIdx(newPc, newIdx);
        in.nextRow++;
        rows++; if (ReplayerAlu.CLASS[op] == 'M') rowsM++; if (in.kind == RowCore.K_PAD) rowsPad++;
        if (last) { cur = null; instrs++; setStateOffset(0); }
        else { expansionOffset = in.nextRow; setStateOffset(in.nextRow); }    // the state word is live (4.21.2)
        if (ready > T) {
            stalls[(isMem ? Stall.DMISS : Stall.PROBE).ordinal()] += ready - T - busWait;
            // the counters: the whole wait, the write-back buffer's included (STAT_STALL_DATA), one cycle at a time from T
            statCause = isMem ? ST_STALL_DATA : ST_STALL_MAP; statUntil = ready; stat[statCause]++;
        }
        busyUntil = ready + (ReplayerAlu.CLASS[op] == 'M' ? mulCycles : rowCycles) - 1;
    }
    long newExpansions;

    private static boolean assertionHolds(int op, long a, long b) {
        return switch (op) {
            case ReplayerAlu.ASSERT_EQ -> a == b;
            case ReplayerAlu.ASSERT_LTE -> Long.compareUnsigned(a, b) <= 0;
            case ReplayerAlu.ASSERT_DIV0 -> a != 0 || b == -1L;
            case ReplayerAlu.ASSERT_UREM -> b == 0 || Long.compareUnsigned(a, b) < 0;
            case ReplayerAlu.ASSERT_MULU -> ReplayerAlu.mulhu(a, b) == 0;
            default -> true;
        };
    }

    private void eqPop(long T) {
        eqHead = (eqHead + 1) % QCAP; eqOcc--;
        if (++eqPopped == QHALF) { eqPopped = 0; ev.post(T, EV_EXP); }
    }
    private void popAdvice(long T) {
        setAdviceLeft(adviceLeft() - 1);
        if (rqHalf == 0) rqHalf = 1;
        else { rqHalf = 0; rqHead = (rqHead + 1) % QCAP; rqOcc--; if (++rqPopped == QHALF) { rqPopped = 0; ev.post(T, EV_ADVICE); } }
    }

    private void pushCode(int w) {
        codeFifo[codeN++] = w;
        set1Code.encode(w & 0xFFFFFFFFL, null);
    }
    private void pushLog(int tag, long v) {
        logFifo[logN] = v; logTag[logN] = (byte) tag; logN++;
        if (tag == TAG_WRITE) { set1Wlog.encode(v, null); wlogValues++; }
        else { set1Rlog.encode(v, null); rlogValues++; if (tag == TAG_LOAD) set1Wlog.lastLoad(v); }
    }

    // ================================================================ fetch (4.21.4 step 1, 4.21.5)
    /** Fetches and decodes the instruction at pc into in; returns the cycle its halves are present. */
    private long fetch(long T, Instr in) {
        long o = pc() - lowest();
        long t = T;
        long[] h = new long[1];
        t = fetch16(t, o, h, false); if (stopped) return t;
        int h0 = (int) h[0];
        int word;
        if ((h0 & 3) == 3) {
            boolean straddle = ((o + 2) >>> 7) != (o >>> 7);
            if (straddle) straddles++;
            t = fetch16(t, o + 2, h, straddle); if (stopped) return t;
            word = h0 | (int) h[0] << 16;
        } else {
            word = Rvc.codeWord(h0);
            if (word == 0) { in.w = Rvc.uncompress(h0) & ~1; in.kind = RowCore.K_BAD; in.cls = -1; return t; }
        }
        in.w = word;
        RowCore.decode(word, decodeTable, romLo, dec);
        in.kind = dec.kind; in.start = dec.start; in.rd0 = dec.rd0; in.offset = dec.offset; in.cls = dec.cls;
        if (in.kind == RowCore.K_NOP) in.nextRow = 0;
        return t;
    }
    /** 16 bits at .pages offset o through the instruction cache; returns when they are present. */
    private long fetch16(long t, long o, long[] out, boolean second) {
        long line = o >>> 7;
        iAccess++;
        Line l = icache.get(line);
        if (l == null) {
            iMiss++; stat[ST_MISS_I]++;
            int cls = (iSeen.add(line) ? 0 : 2) + (second ? 1 : 0);
            long lp = mem.lpddrReads;
            long t0 = Math.max(t, mem.freeAt());
            mem.read(t0, memRoot(), memDepth(), line, resp);
            iMissBy[cls]++; iCycBy[cls] += resp.readyAt - t; if (mem.lpddrReads != lp) iMissLpddr++;
            if (resp.status == FileStore.ST_BEYOND) { error(t, 1 << ERR_PAGE, String.format("an instruction fetch at offset 0x%x, beyond the file's capacity", o)); return t; }
            l = new Line(); System.arraycopy(resp.data, 0, l.d, 0, 16);
            icache.put(line, l);
            if (icache.size() > icacheLines) { var it = icache.entrySet().iterator(); it.next(); it.remove(); }
            t = resp.readyAt;
        }
        int b = (int) (o & 127);
        out[0] = (l.d[b >>> 3] >>> (8 * (b & 7))) & 0xFFFF;
        return t;
    }

    /** The data line (its .pages line number) into the data cache; returns when it is present. A store's write-allocate
     *  fills first (4.21.5); a dirty victim is written back as soon as the fill completes. */
    private long dataLine(long t, long line) {
        dAccess++;
        if (dcache.get(line) != null) return t;
        dMiss++; stat[ST_MISS_D]++;
        long t0 = Math.max(t, mem.freeAt());
        if (t0 > t) { stalls[Stall.BUS.ordinal()] += t0 - t; busWait = t0 - t; }
        mem.read(t0, memRoot(), memDepth(), line, resp);
        if (resp.status == FileStore.ST_BEYOND) { error(t, 1 << ERR_PAGE, "a data fill beyond the file's capacity"); return t; }
        Line l = new Line(); System.arraycopy(resp.data, 0, l.d, 0, 16);
        l.writable = resp.status == FileStore.ST_OK;
        long ready = resp.readyAt;
        dcache.put(line, l);
        if (dcache.size() > CACHE_LINES) {
            var it = dcache.entrySet().iterator(); Map.Entry<Long, Line> v = it.next(); it.remove();
            if (v.getValue().dirty) { mem.write(ready, memRoot(), memDepth(), v.getKey(), v.getValue().d); writebacks++; }
        }
        return ready;
    }
    private long busWait;                             // this row's wait for the bus (a write-back in flight)
    private long wbLine = -1;                         // (the one-entry buffer: never occupied across a read, see the class doc)

    // ================================================================ jump targets (4.21.4)
    private long lookupReady;
    /** The base instr_idx of the jump target np: the LRU, else the map probe. Errors stop (lookupReady: when known). */
    private long lookup(long T, long np) {
        lookupReady = T;
        Integer hit = jumpLru.get(np);
        if (hit != null) { lruHits++; return hit & 0xFFFFFFFFL; }
        lruMisses++; stat[ST_PROBES]++;
        long o = np - lowest();
        if (Long.compareUnsigned(o, 1L << 32) >= 0) { error(T, 1 << ERR_MAP, String.format("a jump to 0x%x, whose offset from lowest_address doesn't fit 32 bits", np)); return 0; }
        int key = (int) o, k = hashLines;
        long h = k == 0 ? 0 : ((key & 0xFFFFFFFFL) * (hashMul & 0xFFFFFFFFL) & 0xFFFFFFFFL) >>> (32 - k);
        long t = T;
        MemorySystem mm = mapMem != null ? mapMem : mem;
        for (int n = 0; n < probeLines; n++) {
            long t0 = Math.max(t, Math.max(mem.freeAt(), mm.freeAt()));
            mm.read(t0, mapRoot(), mapDepth(), h, resp);
            probeReads++;
            t = resp.readyAt + probeCompare;
            if (resp.status == FileStore.ST_HOLE || resp.status == FileStore.ST_BEYOND) {
                error(T, 1 << ERR_PAGE, "the jump map's line " + h + (resp.status == FileStore.ST_HOLE ? " is a hole (a dense file: corrupt)" : " is beyond its capacity")); return 0;
            }
            boolean free = false;
            for (int j = 0; j < 16; j++) {
                long e = resp.data[j];
                int ek = (int) e;
                if (ek == key) {
                    long base = e >>> 32;
                    jumpLru.put(np, (int) base);
                    if (jumpLru.size() > LRU_ENTRIES) { var it = jumpLru.entrySet().iterator(); it.next(); it.remove(); }
                    lookupReady = t;
                    return base;
                }
                if (ek == 0xFFFFFFFF) free = true;
            }
            if (free) { error(T, 1 << ERR_MAP, String.format("jump target 0x%x not in the map (a free entry in line %d)", np, h)); return 0; }
            h = (h + (hashStep & 0xFFFFFFL)) & ((1L << k) - 1);
        }
        error(T, 1 << ERR_MAP, String.format("jump target 0x%x not in the map (%d full lines)", np, probeLines));
        return 0;
    }

    // ================================================================ the rows' end (4.21.6)
    private void endRows(long T) {
        rowsEnded = true; rowsEndAt = T;
        boolean cc = (runConfig & RC_COMP_CODE) != 0, cr = (runConfig & RC_COMP_RLOG) != 0, cw = (runConfig & RC_COMP_WLOG) != 0;
        Replayer.StreamSize sc = choose(codeN, (codeN + 3) / 4, set1Code.bits, cc);
        Replayer.StreamSize sr = choose(rlogValues, (rlogValues + 1) / 2, set1Rlog.bits, cr);
        Replayer.StreamSize sw = choose(wlogValues, (wlogValues + 1) / 2, set1Wlog.bits, cw);
        long[] s = Replayer.streamWord(sc, sr, sw, adviceLeft());
        rec[2] = s[0]; rec[3] = s[1];
        rec[2 * REC_BITMAP] = bitmap[0]; rec[2 * REC_BITMAP + 1] = bitmap[1];
        lengthCode = sc.words(); lengthRlog = sr.words(); lengthWlog = sw.words();
        startDrain(T);
    }
    /** 4.21.6's choice: LRU-coded when enabled and fewer 128-bit words than raw, raw on a tie. */
    static Replayer.StreamSize choose(int values, int rawWords, long codedBits, boolean enabled) {
        int coded = (int) ((codedBits + 127) / 128);
        boolean c = enabled && coded < rawWords;
        return new Replayer.StreamSize(c ? coded : rawWords, values, c);
    }

    /** A window's output (for harnesses): the record as output queue 0 delivers it (68 words, lo/hi), and the three streams. */
    static final class WindowOut { long[] record; long[] code, rlog, wlog; Replayer.StreamSize[] sizes; }

    // ================================================================ the output path (4.21.6; milestone 2)
    /** Output queues 1-3: 4 KB each, 256 entries of 128 bits; credits per 2 KB chunk (128 entries) and for a stream's last
     *  part (4.21.6). Entries pushed at t are readable from t + 1; the scan's room is judged before the cycle's pops. */
    final class OutQueue {
        final long[] lo = new long[QCAP], hi = new long[QCAP]; final long[] at = new long[QCAP];
        int head, occ; long pushed, read;
        final long[] bound = new long[1 << 10]; int granted;            // each credit's last entry (cumulative)
        boolean ended; long lastPopT = Long.MIN_VALUE; int popsAt; long popsCycle = Long.MIN_VALUE;
        long stallCycles, maxOcc;
        void reset() { head = occ = 0; pushed = read = 0; granted = 0; ended = false; lastPopT = Long.MIN_VALUE; popsCycle = Long.MIN_VALUE; }
        boolean room(long t) { return occ + (popsCycle == t ? popsAt : 0) < QCAP; }
        void push(long t, long l, long h) {
            int k = (head + occ) % QCAP; lo[k] = l; hi[k] = h; at[k] = t; occ++; pushed++; maxOcc = Math.max(maxOcc, occ);
            if (pushed % QHALF == 0) bound[granted++] = pushed;
        }
        void end() { ended = true; if (pushed % QHALF != 0) bound[granted++] = pushed; }
        int taken() { int n = 0; while (n < granted && bound[n] <= read) n++; return n; }
        boolean flag() { return granted > taken(); }
        /** The entries the credits granted so far cover beyond what was read: the most a transfer may ask (4.21.6). */
        long covered() { return granted == 0 ? 0 : bound[granted - 1] - read; }
        boolean ready(long t) { return occ > 0 && at[head] < t; }
        void take(long t, long[] w) {
            w[0] = lo[head]; w[1] = hi[head]; head = (head + 1) % QCAP; occ--; read++;
            if (popsCycle != t) { popsCycle = t; popsAt = 0; } popsAt++; lastPopT = t;
        }
    }
    final OutQueue[] oq = { null, new OutQueue(), new OutQueue(), new OutQueue() };
    private int q0ptr = REC_WORDS;                    // output queue 0's read pointer: 68 = nothing to read
    private long q0LastT = Long.MIN_VALUE, scanStart;
    private boolean scansDone, done;
    private int codeScan, logScan;
    private final LruCoder set2Code = new LruCoder(false), set2Rlog = new LruCoder(false), set2Wlog = new LruCoder(true);
    private LruCoder.Bits pkCode, pkRlog, pkWlog;
    private boolean comp0, comp1, comp2, codeEnded, logEnded;
    private long xferQueue = -1, xferLeft;
    long drainCycles, scanStalls;

    private void startDrain(long T) {
        q0ptr = 0; scanStart = T + 2;                 // [7] in the cycle after the last row, the scans in the cycle after that
        Replayer.StreamSize[] sz = Replayer.parseStreamWord(rec[2], rec[3]);
        comp0 = sz[0].compressed(); comp1 = sz[1].compressed(); comp2 = sz[2].compressed();
        set2Code.reset(); set2Rlog.reset(); set2Wlog.reset();
        for (int q = 1; q <= 3; q++) oq[q].reset();
        pkCode = new LruCoder.Bits((a, b) -> oq[1].push(pushT, a, b));
        pkRlog = new LruCoder.Bits((a, b) -> oq[2].push(pushT, a, b));
        pkWlog = new LruCoder.Bits((a, b) -> oq[3].push(pushT, a, b));
        codeScan = logScan = 0; codeEnded = logEnded = scansDone = done = false;
    }
    private long pushT;

    /** One cycle of the drain: each scan moves one FIFO entry into its encoder and packer while its queue has room. */
    private void drainCycle(long T) {
        pushT = T;
        long st0 = scanStalls;
        if (!scansDone && T >= scanStart) {
            // the code scan
            if (!codeEnded) {
                if (codeScan < codeN) {
                    long v = codeFifo[codeScan] & 0xFFFFFFFFL;
                    int bits = comp0 ? set2Code.peekBits(v) : 32;
                    if (pkCode.fill + bits >= 128 && !oq[1].room(T)) { oq[1].stallCycles++; scanStalls++; }
                    else { if (comp0) set2Code.encode(v, pkCode); else pkCode.put(v, 32); codeScan++; }
                } else if (pkCode.fill == 0 || oq[1].room(T)) { pkCode.finish(); oq[1].end(); codeEnded = true; }
            }
            // the log scan: tags 0 and 1 to the read log, 2 to the write log; every load passes set 2's write-log table
            if (!logEnded) {
                if (logScan < logN) {
                    long v = logFifo[logScan]; int tag = logTag[logScan];
                    boolean w = tag == TAG_WRITE;
                    LruCoder c = w ? set2Wlog : set2Rlog; LruCoder.Bits pk = w ? pkWlog : pkRlog; boolean cmp = w ? comp2 : comp1;
                    OutQueue q = oq[w ? 3 : 2];
                    int bits = cmp ? c.peekBits(v) : 64;
                    if (pk.fill + bits >= 128 && !q.room(T)) { q.stallCycles++; scanStalls++; }
                    else {
                        if (cmp) c.encode(v, pk); else pk.put(v, 64);
                        if (tag == TAG_LOAD) set2Wlog.lastLoad(v);
                        logScan++;
                    }
                } else {
                    boolean r2 = pkRlog.fill == 0 || oq[2].room(T), r3 = pkWlog.fill == 0 || oq[3].room(T);
                    if (r2 && r3) { pkRlog.finish(); pkWlog.finish(); oq[2].end(); oq[3].end(); logEnded = true; }
                }
            }
            if (codeEnded && logEnded) {
                scansDone = true;
                Replayer.StreamSize[] sz = Replayer.parseStreamWord(rec[2], rec[3]);
                if (pkCode.words != sz[0].words() || pkRlog.words != sz[1].words() || pkWlog.words != sz[2].words())
                    throw new IllegalStateException("set 2's words differ from set 1's: " + pkCode.words + "/" + pkRlog.words + "/" + pkWlog.words + " against " + java.util.Arrays.toString(sz));
                codeN = logN = 0;
            }
        }
        if (scanStalls != st0) stat[ST_STALL_OUTPUT]++;   // either scan stalled on a full output queue (item 157)
        // done comes last (4.21.6): the rows, both scans, every queue empty and the last transfer finished, all as of
        // the end of T - 1 (so the cycle the DMA took the last word doesn't matter)
        if (scansDone && q0ptr == REC_WORDS && q0LastT < T && xferLeft == 0) {
            boolean empty = true;
            for (int q = 1; q <= 3; q++) empty &= oq[q].occ == 0 && oq[q].lastPopT < T;
            if (empty) { done = true; running = false; drainedAt = T; drainCycles += T - rowsEndAt; ev.post(T, EV_RUN_DONE); }
        }
    }

    // ---- the DMA's side (4.17.9): four output queues; queue 0 is part 0 read in place
    @Override public int outputQueues() { return 4; }
    @Override public void outStart(long t, int q, int words) {
        if (xferLeft > 0) throw new IllegalStateException("Tracer: a transfer started with one in progress");
        if (q == 0) {
            if (q0ptr + words > REC_WORDS) throw ub(t, "a transfer from output queue 0 reading past word 67 (" + words + " words from word " + q0ptr + ", 4.21.6)");
        } else if (words > oq[q].covered()) {
            throw ub(t, "a transfer of " + words + " words from output queue " + q + ", more than its credits cover (" + oq[q].covered() + "; 4.21.6)");
        }
        xferQueue = q; xferLeft = words;
    }
    @Override public boolean outReady(long t, int q) {
        if (q != xferQueue || xferLeft == 0) return false;
        if (q == 0) return rowsEnded && q0ptr < REC_WORDS && rowsEndAt < t;
        return oq[q].ready(t);
    }
    @Override public void outTake(long t, int q, long[] w) {
        if (!outReady(t, q)) throw new IllegalStateException("Tracer: outTake with nothing ready");
        if (q == 0) { w[0] = rec[2 * q0ptr]; w[1] = rec[2 * q0ptr + 1]; q0ptr++; q0LastT = t; }
        else oq[q].take(t, w);
        xferLeft--;
    }

    /** meshsim only, for the one-memory acceptance (owner, 2026-11-05): at a segment's end the harness writes the data
     *  cache's dirty lines back into the memory, untimed, so the next segment starts where this one left it (the hardware
     *  drops them at the word-0 write; the next proof's images come from the updates). Returns the lines written. */
    int writeBackDirtyUntimed() {
        int n = 0; FileStore.Walk w = new FileStore.Walk();
        for (var e : dcache.entrySet()) if (e.getValue().dirty) {
            mem.store.walk(memRoot(), memDepth(), e.getKey(), w);
            mem.store.writeLine(w.dataLine, e.getValue().d); e.getValue().dirty = false; n++;
        }
        return n;
    }
    boolean rowsEnded(long t) { settle(t); return rowsEnded; }
    boolean stopped() { return stopped; }
    int cacheLines(boolean data) { return (data ? dcache : icache).size(); }
}
