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
 * invalid instruction (universal tables: encodings that aren't valid RV64IMAC). seq_remaining counts traced words, and a
 * replayer only ever sees traced words, so the model needs no change for it.
 *
 * <h3>What is modelled</h3>
 * The mode-9 write port (4.19.10: the map, the sizes, ownership, rp_latency, counting); the four queues as true FIFOs
 * with occupancy, pending and their low-water lines (4.19.8); the three decompressors with the write log's last-load
 * entry (4.19.15); the decode index, the decode table and the ROM (4.19.5, 4.19.6); the rd = x0 policy and the
 * temporary offset (4.19.4); the register file, the fixed 3- and 5-cycle row schedules (4.19.7); pc and instr_idx;
 * the 80-byte record (4.19.12) into a 32-row output queue drained by a sink (one row per cycle by default); the six
 * service lines and two status registers (4.19.9); run, done, halt and conditions 11-13 (4.19.11).
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
    static final int IMAGE_COPY_CYCLES = 64;
    static final int OUTQ_ROWS = 32;
    static final int REC_LONGS = 10;                 // 5 × 128 bits = 80 bytes
    /** Record word 0 bit [86]: the expansion word's sign_imm, so the consumer needs no list of signed-immediate opcodes
     *  (v1.25, DECISIONS-v1.25 s4; the bit's place in the record is WORKER11 Q1, Proposed). Bit 22 of the record's long 1. */
    static final int REC_SIGN_IMM = 22;

    // events (4.19.9): bit numbers
    static final int EV_RUN_DONE = 0, EV_CODE = 2, EV_RLOG = 3, EV_WLOG = 4, EV_EXP = 5, EV_NEWEXP = 6;
    final EventCounts ev = new EventCounts(new int[] { 1, 0, 2, 2, 2, 2, 1 }, new boolean[] { false, false, true, true, true, true, false });
    /** ERRORS (4.19.11): [0] bad write, [1] queue overrun, [2] invalid expansion; sticky until the reset. */
    static final int ERR_BAD_WRITE = 0, ERR_OVERRUN = 1, ERR_INVALID = 2;

    final int me;
    /**
     * Rows per pass: 65,536, as the hardware runs (4.19.1). A testing parameter only: a debug window (the tracer's
     * manifest gives its length as window_rows, e.g. 2^8) or a unit test runs a shorter pass. Set before a run; 1 to 65,536.
     */
    int rowsPerPass = ROWS_PER_PASS;
    private Host host;
    @Override public void attach(Host h) { host = h; }
    private void violate(long t, String m) { violations.add("t" + me + " @" + t + ": " + m); if (host != null) host.violate(t, m); }
    final List<String> violations = new ArrayList<>();

    // ================================================================ memories (mode 9, 4.19.10)
    final long[] region0 = new long[66 * 2];           // state word, stream word, the 64-word register image: lo, hi
    final short[] decodeTable = new short[2048];
    final long[] romLo = new long[512], romHi = new long[512];

    // ================================================================ queues (4.19.8)
    final Stream code = new Stream("code", 256, 32, false);
    final Stream rlog = new Stream("read log", 512, 64, false);
    final Stream wlog = new Stream("write log", 512, 64, true);
    { code.credit = EV_CODE; rlog.credit = EV_RLOG; wlog.credit = EV_WLOG; }
    final ExpQueue exp = new ExpQueue();

    // ================================================================ core state
    private boolean running, halted;
    private long[] regs = new long[128];
    private long pc; private int instrIdx;
    private boolean fCode, fRlog, fWlog, fStore;       // latched per run: compressed flags, includes store log
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
    long passStartAt, passEndAt;

    Replayer(int me) { this.me = me; }
    /** A replayer whose passes are {@code rowsPerPass} rows long (testing; the hardware's is 65,536). */
    Replayer(int me, int rowsPerPass) {
        this(me);
        if (rowsPerPass < 1 || rowsPerPass > ROWS_PER_PASS) throw new IllegalArgumentException("rows per pass " + rowsPerPass + ": 1 to 65,536");
        this.rowsPerPass = rowsPerPass;
    }
    @Override public int type() { return 4; }
    @Override public int params() { return 4; }                    // type 4 (4.17.1, D46), version 0
    @Override public EventCounts events() { return ev; }
    // invariant 16's refill bookkeeping: entries pushed and entries the taken credits allow, per queue
    private final int[] pushed = new int[4], allowance = new int[4];
    private static int half(int q) { return q == 0 ? 128 : q == 3 ? 128 : 256; }
    { ev.onConsume = b -> { int q = b == EV_CODE ? 0 : b == EV_RLOG ? 1 : b == EV_WLOG ? 2 : b == EV_EXP ? 3 : -1; if (q >= 0) allowance[q] += half(q); }; }
    @Override public int errors(long t) { settle(t); int e = 0; for (int k = 0; k < 3; k++) if (condEvents[k] > 0) e |= 1 << k; return e; }
    @Override public Integer word(long t, int w) {
        settle(t);
        return switch (w) { case 32 -> sStatus0 & 0xFFFF; case 33 -> sStatus0 >>> 16; case 34 -> sStatus1; default -> null; };
    }

    // ================================================================ inputs, deferred to the end of their cycle
    private record Input(long t, int kind, int a, int b, int c, long lo, long hi) {}
    private static final int IN_CLAIM = 1, IN_RUN = 2, IN_HALT = 3;
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
     *  write port below decodes (offset [22:20] the part). Offsets from 0x400000 are unmapped. */
    @Override public void accept(long t, int src, int offset, int size, long lo, long hi) {
        int addr = offset >= 0x400000 ? 0x100000 | offset >>> 2 : offset >>> 2;   // past part 3: an unmapped word address
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
    }
    void setHalted(long t, boolean h) { noteInput(t); inputs.add(new Input(t, IN_HALT, h ? 1 : 0, 0, 0, 0, 0)); }

    int landed(long t) {
        for (int i = 0; i < Math.min(acceptN, 8); i++) if (acceptTimes[(acceptN - 1 - i) & 7] == t - RP_LATENCY) return 1;
        return 0;
    }

    // ================================================================ level reads: the snapshot at the end of t − 1
    private boolean sRunning;
    private int sLines, sStatus0, sStatus1;

    @Override public boolean running(long t) { settle(t); return sRunning; }
    /** Bring the model to the start of t (the slot calls it before reading the events). */
    @Override public void sync(long t) { settle(t); }
    /** v1.1's PERIPH_STATUS_0/1, for the replayer's own tests: rows done [15:0] | the next offset [31:16]; the presented item. */
    int status(long t, int i) { settle(t); return i == 0 ? sStatus0 : i == 1 ? sStatus1 : 0; }

    // ================================================================ conditions
    final long[] condEvents = new long[3];
    long conditionEvents(int k) { return condEvents[k]; }

    // ================================================================ no output (4.17.9)


    /**
     * Bring the state to the start of cycle T: apply every input and landing of earlier cycles (end-of-cycle effects,
     * in the order landings, claims, runs, halts within one cycle), then take the snapshot that reads at T return.
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
                case IN_HALT -> doHalt(in.t(), in.a() != 0);
                default -> throw new IllegalStateException();
            }
        }
        settled = T;
        sRunning = running;
        sStatus0 = (rowsDone & 0xFFFF) | (nextOffset() & 0xFFFF) << 16;
        sStatus1 = presented;
    }

    private int nextOffset() {
        if (cur != null && !cur.lastStarted) return cur.nextRow;
        if (nxt != null) return nxt.nextRow;
        if (firstInstr) return startOffset;
        return running ? 0 : stoppedOffset;
    }
    private int stoppedOffset;                         // the next offset when the pass ended (a window ending mid-expansion)

    // ================================================================ run, halt (end of their cycle)
    private void doRun(long t) {
        if (halted) { violate(t, "PERIPH_RUN in HALT: ignored"); return; }
        if (running) { violate(t, "PERIPH_RUN while periph_running: ignored (4.17.2, 4.13)"); return; }
        for (Input p : landing) if (p.t() >= t && queueOf(p.a()) != null)
            violate(t, "a pass started with a queue push still in flight (4.19.8's software rule; invariant 16): packet landing at " + p.t());
        // the pass: empty the queues and the decompressors' tables, latch the state word and the stream word
        long sLo = region0[0], sHi = region0[1], lLo = region0[2], lHi = region0[3];
        pc = sLo; instrIdx = (int) sHi; startOffset = (int) (sHi >>> 32) & 0xFFFF;
        fCode = ((sHi >>> 48) & 1) != 0; fRlog = ((sHi >>> 49) & 1) != 0; fWlog = ((sHi >>> 50) & 1) != 0; fStore = ((sHi >>> 51) & 1) != 0;
        code.reset(fCode, (int) (lLo & 0xFFFFFF)); rlog.reset(fRlog, (int) ((lLo >>> 32) & 0xFFFFFF)); wlog.reset(fWlog, (int) (lHi & 0xFFFFFF));
        exp.reset();
        for (int b = EV_CODE; b <= EV_EXP; b++) ev.set(t, b, 2);   // every queue's credits to 2 (4.19.8)
        java.util.Arrays.fill(pushed, 0); java.util.Arrays.fill(allowance, 0);
        for (int i = 0; i < 64; i++) { regs[2 * i] = region0[4 + 2 * i]; regs[2 * i + 1] = region0[4 + 2 * i + 1]; }
        regs[0] = 0;
        running = true;
        copyDoneAt = t + 1 + IMAGE_COPY_CYCLES;
        rowsStarted = 0; rowsDone = 0; cur = null; nxt = null; firstInstr = true; inflight = null; busyUntil = Long.MIN_VALUE;
        codeEnded = false; lastStartAt = Long.MIN_VALUE; expDiscard = 0; stoppedOffset = 0;
        srcOf[0] = srcOf[1] = srcOf[2] = srcOf[3] = -1;
        passStartAt = t; passes++;
    }

    private void doHalt(long t, boolean h) {
        if (h == halted) return;
        halted = h;
        if (h) { stopCore(t, false); expDiscard = 0; }
        newExpSetAt = Long.MIN_VALUE; doneSetAt = Long.MIN_VALUE; ev.clearAll();
        code.clear(); rlog.clear(); wlog.clear(); exp.reset();
    }

    /** The pass ends: periph_running falls; done is posted unless a halt stopped it (4.19.11, 4.17.7). */
    private void stopCore(long t, boolean postDone) {
        if (running) { passEndAt = t; stoppedOffset = nextOffset(); }   // PERIPH_STATUS_0 keeps the next offset after the pass
        running = false; inflight = null; cur = null; nxt = null;
        if (postDone) { doneSetAt = t; ev.post(t, EV_RUN_DONE); }
    }

    // ================================================================ the write port (4.19.10)
    private final int[] srcOf = { -1, -1, -1, -1 };

    /** Which queue a packet's address names, or null (window 0 or unmapped). */
    private Object queueOf(int addr) {
        int w = (addr >>> 18) & 7;
        if (w == 1) return code;
        if (w == 2) return (addr & 0x20000) == 0 ? rlog : wlog;
        if (w == 3) return exp;
        return null;
    }

    private void bad(long t, String why) { condEvents[ERR_BAD_WRITE]++; badWrites.add("@" + t + " " + why); }
    final List<String> badWrites = new ArrayList<>();

    /** A mode-9 packet lands (or is dropped) at the end of cycle t. */
    private void land(long t, int src, int addr, int size, long lo, long hi) {
        addr &= 0x1FFFFF;
        int w = addr >>> 18;
        if (w >= 4) { bad(t, "unmapped address 0x" + Integer.toHexString(addr)); return; }
        if (size != LinkWord.SIZE_128) { bad(t, "size " + (32 << size) + " at 0x" + Integer.toHexString(addr) + ": only 128-bit packets"); return; }
        if (w == 0) {
            if ((addr & 3) != 0) { bad(t, "128-bit write in window 0 off a multiple of 4: 0x" + Integer.toHexString(addr)); return; }
            int wd = addr >>> 2;                                     // 128-bit word index in window 0
            if (wd < 66) {
                if (running) { bad(t, "region 0 written while a pass runs"); return; }
                region0[2 * wd] = lo; region0[2 * wd + 1] = hi;
            } else if (addr >= 0x400 && addr < 0x800) {
                if (running) { bad(t, "decode table written while a pass runs"); return; }
                int e0 = (wd - 0x100) * 8;
                for (int k = 0; k < 8; k++) decodeTable[e0 + k] = (short) ((k < 4 ? lo >>> (16 * k) : hi >>> (16 * (k - 4))) & 0xFFFF);
            } else if (addr >= 0x800 && addr < 0x1000) {
                int j = wd - 0x200; romLo[j] = lo; romHi[j] = hi;    // the ROM's second port: written even while running
            } else bad(t, "unmapped address 0x" + Integer.toHexString(addr));
            return;
        }
        if (halted) { bad(t, "queue port written in HALT"); return; }
        int qi = w == 1 ? 0 : w == 3 ? 3 : (addr & 0x20000) == 0 ? 1 : 2;
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

    // ================================================================ the cycle
    @Override public void runCycle(long T) {
        settle(T);
        drain(T);
        code.cycle = rlog.cycle = wlog.cycle = exp.cycle = T;
        if (!running) return;
        passCycles++;
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
    static final int K_PAD = 0, K_ROM = 1, K_NOP = 2, K_CTRL = 3, K_BAD = 4;

    private void frontEnd(long T) {
        if (nxt != null || (cur != null && !cur.lastStarted)) return;
        if (rowsStarted >= rowsPerPass || codeEnded) return;
        if (cur != null && lastStartAt == T) return;                  // decode the cycle after the last row starts
        int st = code.peek();
        if (st == Stream.WAIT) return;
        if (st == Stream.END) { codeEnded = true; return; }            // an error only when a row needs it (startRow)
        int w = (int) code.peekValue;
        code.commit();
        Instr in = new Instr();
        in.w = w; in.decodedAt = T; in.codeEnd = code.consumed;
        in.nextRow = in.firstRow = firstInstr ? startOffset : 0;
        firstInstr = false;
        if ((w & 3) == 0) { in.kind = K_PAD; }
        else {
            int e = decodeTable[decodeIndex(w)] & 0xFFFF, cls = e & 3;
            int rdField = (w >>> 7) & 31;
            if (cls == 3) in.kind = K_BAD;                              // an error when its row would start (a row in flight completes)
            else if (cls == 0) {
                in.start = (e >>> 2) & 0x1FF;
                in.rd0 = (int) ((romLo[in.start] >>> 46) & 3);
                if (rdField == 0 && (in.rd0 == 1 || in.rd0 == 3)) { in.kind = K_NOP; in.nextRow = in.firstRow = 0; }
                else { in.kind = K_ROM; in.offset = rdField == 0 && in.rd0 == 2 ? 1 : 0; }
            } else {
                in.kind = K_CTRL;
                presented = w; newExpSetAt = T; exp.newExpansion(); ev.post(T, EV_NEWEXP); newExpansions++;
            }
        }
        nxt = in;
    }
    private long lastStartAt = Long.MIN_VALUE;
    private boolean codeEnded;
    private int expDiscard;                            // words of a NOP-substituted controller-fed expansion still to drop

    /** The decode table's 11-bit index (4.19.5); bit 0 of w (the compressed flag) plays no part. */
    static int decodeIndex(int w) {
        w |= 1;
        int op = w & 0x7F, f3 = (w >>> 12) & 7;
        if (op == 0x2F) return 1 << 10 | ((w >>> 12) & 1) << 5 | (w >>> 27) & 31;                        // AMO
        if (op == 0x0B) return 1 << 10 | 1 << 8 | ((w >>> 25) & 31) << 3 | f3;                          // custom-0
        if (op == 0x73) {                                                                                // SYSTEM
            int sel = f3 != 0 ? f3 : w == 0x00000073 ? 8 : w == 0x00100073 ? 9 : w == 0x30200073 ? 10 : 11;
            return 1 <<10 | 2 << 8 | sel;
        }
        return ((w >>> 2) & 31) << 5 | f3 << 2 | ((w >>> 30) & 1) << 1 | (w >>> 25) & 1;
    }

    /** The source instruction's immediate by format (4.19.3, code 132): 0 R, 1 I, 2 S, 3 B, 4 U, 5 J. */
    static long sourceImm(int format, int w) {
        w |= 1;
        return switch (format) {
            case 0 -> 0;
            case 1 -> (long) (w >> 20);
            case 2 -> (long) ((w >> 25) << 5 | (w >>> 7) & 31);
            case 3 -> (long) ((w >> 31) << 12 | ((w >>> 7) & 1) << 11 | ((w >>> 25) & 63) << 5 | ((w >>> 8) & 15) << 1);
            case 4 -> (long) (w & 0xFFFFF000);
            case 5 -> (long) ((w >> 31) << 20 | ((w >>> 12) & 0xFF) << 12 | ((w >>> 20) & 1) << 11 | ((w >>> 21) & 0x3FF) << 1);
            default -> throw new IllegalArgumentException("format " + format);
        };
    }

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
            if (nxt == null && codeEnded) { invalid(T, "a pop past the code stream's length"); return; }
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
        int op = (int) (lo & 0xFF), cRd = (int) (lo >>> 8) & 0xFF, cRs1 = (int) (lo >>> 16) & 0xFF, cRs2 = (int) (lo >>> 24) & 0xFF;
        int seqRem = (int) (lo >>> 32) & 0xFFF;
        boolean nfv = ((lo >>> 44) & 1) != 0, hold = ((lo >>> 45) & 1) != 0, virt = ((lo >>> 51) & 1) != 0;
        boolean imm0pc0 = ((lo >>> 52) & 1) != 0, reportC = ((lo >>> 53) & 1) != 0, fromLog = ((lo >>> 54) & 1) != 0;
        boolean untraced = ((lo >>> 55) & 1) != 0;
        boolean signImm = ((lo >>> 62) & 1) != 0, offsetImm = ((lo >>> 63) & 1) != 0;   // v1.25 (DECISIONS-v1.25 s4, s5)
        int format = (int) (lo >>> 48) & 7, rd0w = (int) (lo >>> 46) & 3;
        // checks (4.19.11, condition 13)
        if (untraced) { invalid(T, "an untraced expansion word reached the replayer"); return; }
        if (!ReplayerAlu.executes(op)) { invalid(T, "opcode " + op + " (" + ReplayerAlu.name(op) + ") is not executed by the replayer"); return; }
        if (cRd >= 146 || cRs1 >= 146 || cRs2 >= 146) { invalid(T, "a reserved operand code (146-255)"); return; }
        if (format >= 6) { invalid(T, "format " + format); return; }
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
        long srcImm = (cRs1 >= 132 && cRs1 <= 136) || (cRs2 >= 132 && cRs2 <= 136) || (cRd >= 132 && cRd <= 136) || offsetImm ? sourceImm(format, w) : 0;
        // shift_offset (v1.25, offset_imm [63]): the source instruction's immediate when set, else 0. Only ShiftData reads it.
        long so = offsetImm ? srcImm : 0;
        if (offsetImm && !ReplayerAlu.isShiftData(op))
            violate(T, "offset_imm set on a " + ReplayerAlu.name(op) + " row (only ShiftData reads shift_offset; WORKER11 Q3)");
        int rRd = reg(cRd, in), rRs1 = reg(cRs1, in), rRs2 = reg(cRs2, in);
        long vRd = rRd >= 0 ? rv(rRd) : immVal(cRd, hi, srcImm);
        long vRs1 = rRs1 >= 0 ? rv(rRs1) : immVal(cRs1, hi, srcImm);
        long vRs2 = rRs2 >= 0 ? rv(rRs2) : immVal(cRs2, hi, srcImm);
        long immSum = (rRd < 0 ? vRd : 0) + (rRs1 < 0 ? vRs1 : 0) + (rRs2 < 0 ? vRs2 : 0);
        // log readiness
        Stream logS = null;
        if (fromLog) logS = ReplayerAlu.isStore(op) ? (fStore ? wlog : null) : rlog;   // SD and VirtualStoreB/H/W: the write log
        if (logS != null) {
            int s = logS.peek();
            if (s == Stream.WAIT) { stall(logS == wlog ? Stall.WLOG : Stall.RLOG); return; }
            if (s == Stream.END) { invalid(T, "a pop past the " + logS.name + "'s length"); return; }
        }
        if (nfv) {
            if (logS == rlog) violate(T, "a row with both from_log and next_from_value (two read-log words)");
            else if (rlog.peek() == Stream.WAIT) { stall(Stall.RLOG); return; }   // a word, or the log's end
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
            r[6] = rRd >= 0 ? vRd : 0; r[7] = rdAfter;
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
        if (s != Stream.AVAIL) { invalid(T, "a pop past the read log's length (a control transfer's next instr_idx)"); return 0; }
        long v = rlog.peekValue; rlog.commit(); rlog.releaseConsumed();
        return v;
    }

    private long rv(int r) { return r == 0 ? 0 : regs[r]; }

    /** A register operand code's register, or −1 for an immediate code (4.19.3, 4.19.4). */
    private static int reg(int c, Instr in) {
        if (c < 128) return c;
        if (c == 129) return in.offset == 1 ? 40 : (in.w >>> 7) & 31;
        if (c == 130) return (in.w >>> 15) & 31;
        if (c == 131) return (in.w >>> 20) & 31;
        if (c >= 137 && c <= 144) return 40 + (c - 137) + in.offset;
        return -1;
    }

    /** An immediate operand code's value (4.19.3). */
    private static long immVal(int c, long cnst, long imm) {
        return switch (c) {
            case 128 -> cnst;
            case 145 -> 0;                                 // immediate 0 (26q): a zero slot beside a non-zero constant
            case 132 -> imm;
            case 133 -> -(1L << (imm & 63));
            case 134 -> 1L << (imm & 63);
            case 135 -> 1L << (imm & 31);
            case 136 -> (1L << 32) - (1L << (imm & 31));
            default -> throw new IllegalStateException("operand code " + c);
        };
    }

    // ---------------------------------------------------------------- finishing a row
    private void finishRow(long T) {
        Pending p = inflight; inflight = null;
        if (p.rdReg > 0) regs[p.rdReg] = p.rdVal;
        pc = p.newPc; instrIdx = p.newIdx;
        outQ.add(p.rec); outAt.add(T + 1);
        if (p.lastOfInstr) code.releaseTo(p.instr.codeEnd);
        rowsDone++;
        if (rowsDone == rowsPerPass) stopCore(T, true);
    }

    private void invalid(long T, String why) {
        condEvents[ERR_INVALID]++;
        invalids.add("@" + T + " " + why);
        stopCore(T, true);
    }
    final List<String> invalids = new ArrayList<>();

    private void stall(Stall s) { stalls[s.ordinal()]++; }

    // ---------------------------------------------------------------- state for tests and the harness
    long reg(int r) { return regs[r]; }
    long pc() { return pc; }
    int instrIdx() { return instrIdx; }
    int rowsDone() { return rowsDone; }
    int nextOffsetNow() { return nextOffset(); }
    boolean isRunning() { return running; }
    int outQueueRows() { return outQ.size(); }

    // ================================================================ a code or log queue and its decompressor (4.19.15)
    final class Stream {
        static final int AVAIL = 0, WAIT = 1, END = 2;
        final String name; final int cap, half, valueBits; final boolean isWrite;
        final long[] lo, hi;
        int head, occ, pend, poppedSince;
        int credit = -1;      // its credit bit (4.19.8)
        long base;            // stream bit position of the head entry's bit 0
        long consumed;        // stream bit position of the next unread bit
        long len;             // the exact length in bits (latched at the run)
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
        void clear() { head = 0; occ = 0; pend = 0; base = 0; consumed = 0; poppedSince = 0; }
        void reset(boolean compressed, int lenBits) { clear(); comp = compressed; len = lenBits; java.util.Arrays.fill(E, 0); }

        boolean lowWater() { return cap - occ - pend >= half; }

        /** A push lands at the end of cycle t: dropped if the queue was full before the cycle's pop (4.19.8, 4.13). */
        boolean push(long t, long l, long h) {
            int popsAtT = popCycle == t ? pops : 0;
            if (occ + popsAtT >= cap) return false;
            int i = (head + occ) % cap; lo[i] = l; hi[i] = h; occ++;
            if (pend > 0) pend--;
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
        private int need(long n) { if (consumed + n > len) return END; if (consumed + n > landedBits()) return WAIT; return AVAIL; }

        /** Decode the next value without taking it: AVAIL (peekValue set), WAIT (not landed yet) or END (past the length). */
        int peek() {
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
            while (occ > 0 && (base + 128 <= pos || (pos >= len && base < len))) {
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
