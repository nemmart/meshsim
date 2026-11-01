/**
 * A unit's registers as one bus (brief v3.1 item 1): like an SRAM, reads return start-of-cycle
 * values and writes commit at the end of the cycle, so a core store at t is seen by loads and by
 * the owning unit at t+1, and a unit's status written at t is readable at t+1 (one cycle old).
 *
 * Two sides. Core side: {@link #read} and {@link #write} (one core write per block per cycle:
 * a core does at most one store per cycle). Unit side: {@link #unitWrite} with a bit mask,
 * {@link #written} / {@link #writtenValue} (a core write committed at the end of the previous
 * cycle, so command registers such as MEM_RD_ADDR are seen as events, not just values).
 *
 * Contract exceptions, documented in DEVIATIONS: several unit-side writes may land on one
 * register in one cycle when their masks do not overlap (STATUS gathers bits from several
 * units), and on an OR-merge register (EVENTS) they may even overlap. Commit order for one
 * register: unit write(s), then the core write (core wins); on a W1C register the core's clear
 * is applied first so a bit set in the same cycle stays set; then the commit rules attached by
 * the register table (auto-increment, count-adjust, clear-on-write), which is how a counter a
 * program polls is right from t+1.
 */
final class RegisterBlock extends BusBase {
    static final int RULE_INC = 1, RULE_SUB_VALUE = 2, RULE_CLEAR = 3, RULE_SET = 4, RULE_DEC_POS = 5, RULE_COPY = 6, RULE_ADD_FIELD = 7, RULE_SET_IF_FIELD = 8;

    final int n;
    private final int[] val;
    private final boolean[] corePend; private final int[] coreVal; private int coreWrites;
    private final boolean[] unitPend; private final int[] unitVal, unitMask;
    private final boolean[] orMerge, w1c;
    private final long[] writtenT; private final int[] writtenVal;
    private int[] ruleSrc = new int[0], ruleDst = new int[0], ruleKind = new int[0], ruleArg = new int[0];

    RegisterBlock(String name, int n) {
        super(name);
        this.n = n;
        val = new int[n]; corePend = new boolean[n]; coreVal = new int[n];
        unitPend = new boolean[n]; unitVal = new int[n]; unitMask = new int[n];
        orMerge = new boolean[n]; w1c = new boolean[n];
        writtenT = new long[n]; java.util.Arrays.fill(writtenT, Long.MIN_VALUE);
        writtenVal = new int[n];
    }

    void setOrMerge(int i) { orMerge[i] = true; }
    void setW1C(int i) { w1c[i] = true; }
    /** On a core write to src committed this cycle: dst += 1 (INC), dst -= written value (SUB_VALUE), dst = 0 (CLEAR), dst |= arg (SET),
     *  dst -= 1 if dst > 0 (DEC_POS), dst = register arg (COPY; register arg's value after this cycle's unit writes). */
    void addRule(int src, int dst, int kind, int arg) {
        int k = ruleSrc.length;
        ruleSrc = java.util.Arrays.copyOf(ruleSrc, k + 1); ruleDst = java.util.Arrays.copyOf(ruleDst, k + 1);
        ruleKind = java.util.Arrays.copyOf(ruleKind, k + 1); ruleArg = java.util.Arrays.copyOf(ruleArg, k + 1);
        ruleSrc[k] = src; ruleDst[k] = dst; ruleKind[k] = kind; ruleArg[k] = arg;
    }
    void init(int i, int v) { val[i] = v; }

    @Override protected void commit(long tPut) {
        for (int i = 0; i < n; i++) {
            if (corePend[i] && w1c[i]) val[i] &= ~coreVal[i];                    // W1C clear, then this cycle's sets
            if (unitPend[i]) {
                val[i] = orMerge[i] ? val[i] | unitVal[i] : (val[i] & ~unitMask[i]) | unitVal[i];
                unitPend[i] = false; unitVal[i] = 0; unitMask[i] = 0;
            }
            if (corePend[i]) {
                if (!w1c[i]) val[i] = coreVal[i];                                   // core wins
                writtenT[i] = tPut; writtenVal[i] = coreVal[i];
                corePend[i] = false;
            }
        }
        int[] before = null;                                                     // COPY reads the values before any rule of this cycle
        for (int k = 0; k < ruleSrc.length; k++) if (ruleKind[k] == RULE_COPY && writtenT[ruleSrc[k]] == tPut) { before = val.clone(); break; }
        for (int k = 0; k < ruleSrc.length; k++) {
            if (writtenT[ruleSrc[k]] != tPut) continue;
            int d = ruleDst[k];
            switch (ruleKind[k]) {
                case RULE_INC -> val[d]++;
                case RULE_SUB_VALUE -> val[d] -= writtenVal[ruleSrc[k]];
                case RULE_CLEAR -> val[d] = 0;
                case RULE_DEC_POS -> { if (val[d] > 0) val[d]--; }
                case RULE_COPY -> val[d] = before[ruleArg[k]];
                case RULE_ADD_FIELD -> val[d] += fieldOf(writtenVal[ruleSrc[k]], ruleArg[k]);            // v3.6: += a field of the stored value
                case RULE_SET_IF_FIELD -> { if (fieldOf(writtenVal[ruleSrc[k]], ruleArg[k]) != 0) val[d] |= 1 << (ruleArg[k] >>> 16); }
                default -> val[d] |= ruleArg[k];
            }
        }
        coreWrites = 0;
    }

    /** Rule argument for the field kinds: lo | width << 8 (| bit << 16 for SET_IF_FIELD). */
    static int fieldOf(int v, int arg) { int lo = arg & 0xFF, w = (arg >>> 8) & 0xFF; return (int) ((v & 0xFFFFFFFFL) >>> lo) & (int) ((1L << w) - 1); }
    static int fieldArg(int lo, int width) { return lo | width << 8; }

    // ---- core side ----
    int read(long t, int i) { enter(t); return val[i]; }
    void write(long t, int i, int v) {
        enter(t);
        if (coreWrites++ > 0) throw new IllegalStateException(name + ": two core writes in cycle " + t);
        pendPut = true; corePend[i] = true; coreVal[i] = v;
    }

    // ---- unit side ----
    void unitWrite(long t, int i, int v, int mask) {
        enter(t);
        if (!orMerge[i] && (unitMask[i] & mask) != 0) throw new IllegalStateException(name + ": overlapping unit writes to register " + i + " in cycle " + t);
        pendPut = true; unitPend[i] = true; unitVal[i] |= v & mask; unitMask[i] |= mask;
    }
    void unitWrite(long t, int i, int v) { unitWrite(t, i, v, -1); }
    /** True at t if the core wrote register i at t-1 (the write committed at the end of t-1). */
    boolean written(long t, int i) { enter(t); return writtenT[i] == t - 1; }
    int writtenValue(long t, int i) { enter(t); return writtenVal[i]; }
}
