import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * One window's replayer inputs, as the tracer delivers them (spec 4.19.2, 4.19.10, 4.19.13): the checkpoint (state
 * word, stream word, register image), the decode table and the ROM, the three streams as 128-bit entries, the
 * controller-fed templates with their index, and optionally the reference rows. Also the builders the unit tests use
 * to make windows by hand, and the stream encoders of 4.19.15 (the tracer's encoder rule).
 */
final class ReplayWindow {
    String name = "window";
    /** The window's length in rows: 65,536, or the manifest's debug `window_rows` (REPLAY-INPUT-REQUIREMENTS 4). */
    int rows = Replayer.ROWS_PER_PASS;
    // the checkpoint
    long pc; int instrIdx, nextOffset;
    boolean codeComp, rlogComp, wlogComp, storeLog = true;
    long codeLen, rlogLen, wlogLen;               // exact lengths in bits
    final long[] regs = new long[128];
    // the tables
    final short[] table = new short[2048];
    final long[] romLo = new long[512], romHi = new long[512];
    // the streams: entry i is {lo[2i], hi[2i + 1]}
    long[] code = new long[0], rlog = new long[0], wlog = new long[0];
    // controller-fed templates by exact instruction word (the stored opcode-stream word), and each one's rd0 (index byte 12)
    final Map<Integer, long[]> templates = new HashMap<>();
    final Map<Integer, Integer> templateRd0 = new HashMap<>();
    /** A delivered state record's words 1-66 (region 0), sent unchanged; null: built from the fields above. */
    long[] region0;
    // reference rows (10 longs each), if any
    List<long[]> ref;

    ReplayWindow() { Arrays.fill(table, (short) 1); }     // unused entries are controller class (4.19.5's builder rule)

    long stateLo() { return pc; }
    long stateHi() {
        return (instrIdx & 0xFFFFFFFFL) | (long) (nextOffset & 0xFFFF) << 32
                | (codeComp ? 1L : 0) << 48 | (rlogComp ? 1L : 0) << 49 | (wlogComp ? 1L : 0) << 50 | (storeLog ? 1L : 0) << 51;
    }
    long streamLo() { return (codeLen & 0xFFFFFF) | (rlogLen & 0xFFFFFF) << 32; }
    long streamHi() { return wlogLen & 0xFFFFFF; }

    // ================================================================ expansion words (4.19.3)
    /** A 128-bit expansion word: {lo, hi}. */
    static final class W {
        long lo, hi;
        static W op(int op) { W w = new W(); w.lo = op & 0xFF; w.rd(128).rs1(128).rs2(128); return w; }
        W rd(int c) { lo = lo & ~(0xFFL << 8) | (long) (c & 0xFF) << 8; return this; }
        W rs1(int c) { lo = lo & ~(0xFFL << 16) | (long) (c & 0xFF) << 16; return this; }
        W rs2(int c) { lo = lo & ~(0xFFL << 24) | (long) (c & 0xFF) << 24; return this; }
        W seq(int n) { lo = lo & ~(0xFFFL << 32) | (long) (n & 0xFFF) << 32; return this; }
        W nfv() { lo |= 1L << 44; return this; }
        W hold() { lo |= 1L << 45; return this; }
        W rd0(int p) { lo = lo & ~(3L << 46) | (long) (p & 3) << 46; return this; }
        W fmt(int f) { lo = lo & ~(7L << 48) | (long) (f & 7) << 48; return this; }
        W virt() { lo |= 1L << 51; return this; }
        W imm0pc0() { lo |= 1L << 52; return this; }
        W reportC() { lo |= 1L << 53; return this; }
        W fromLog() { lo |= 1L << 54; return this; }
        W untraced() { lo |= 1L << 55; return this; }
        W cnst(long c) { hi = c; return this; }
        int seqRem() { return (int) (lo >>> 32) & 0xFFF; }
    }

    /** Put a template into the ROM at {@code start}; seq_remaining is set from the template's length. */
    void rom(int start, W... words) {
        for (int i = 0; i < words.length; i++) { words[i].seq(words.length - 1 - i); romLo[start + i] = words[i].lo; romHi[start + i] = words[i].hi; }
    }
    /** ROM words 0 (padding) and 1 (the shared NOP), 4.19.6. */
    void fixedRom() {
        W pad = W.op(0).rd(0).rs1(0).rs2(0).hold(); romLo[0] = pad.lo; romHi[0] = pad.hi;
        W nop = W.op(ReplayerAlu.ADDI).rd(0).rs1(0).rs2(128).cnst(0).reportC(); romLo[1] = nop.lo; romHi[1] = nop.hi;
    }
    /** Map an instruction to a ROM template (class 0) at its exact decode index. */
    void decodeRom(int w, int start) { table[Replayer.decodeIndex(w)] = (short) (start << 2); }
    void decodeClass(int w, int cls) { table[Replayer.decodeIndex(w)] = (short) cls; }
    /** A controller-fed template for one instruction word (seq_remaining set from its length). */
    void template(int w, W... words) {
        long[] t = new long[2 * words.length];
        for (int i = 0; i < words.length; i++) { words[i].seq(words.length - 1 - i); t[2 * i] = words[i].lo; t[2 * i + 1] = words[i].hi; }
        templates.put(w, t);
    }

    // ================================================================ the streams (4.19.15)
    /** A bit writer: LSB first within each 128-bit entry, entries in order. */
    static final class Bits {
        final List<Long> words = new ArrayList<>();   // 64-bit chunks
        long n;                                       // bits written
        void put(long v, int bits) {
            for (int k = 0; k < bits; k++) {
                long pos = n + k; int wi = (int) (pos >>> 6);
                while (words.size() <= wi) words.add(0L);
                if (((v >>> k) & 1) != 0) words.set(wi, words.get(wi) | 1L << (pos & 63));
            }
            n += bits;
        }
        /** Padded with zero bits to a whole 128-bit entry: lo, hi per entry. */
        long[] entries() {
            int e = (int) ((n + 127) >>> 7);
            long[] r = new long[2 * e];
            for (int i = 0; i < 2 * e && i < words.size(); i++) r[i] = words.get(i);
            return r;
        }
    }

    /** The table side of the encoder: true LRU over 16 entries (or 15 plus the last-load entry for the write log). */
    static final class Lru {
        final long[] E = new long[16]; final boolean write;
        Lru(boolean write) { this.write = write; }
        /** Encode v: the hit index, or −1 for a miss; and update the table exactly as the decoder will. */
        int code(long v) {
            if (write && E[15] == v) return 15;                       // the encoder rule: index 15 first
            int top = write ? 15 : 16;
            for (int i = 0; i < top; i++) if (E[i] == v) { System.arraycopy(E, 0, E, 1, i); E[0] = v; return i; }
            System.arraycopy(E, 0, E, 1, top - 1); E[0] = v; return -1;
        }
    }
    static void putValue(Bits b, int hit, long v) {
        if (hit >= 0) { b.put(1, 1); b.put(hit, 4); }
        else if ((v >>> 32) == 0) { b.put(0, 1); b.put(0, 1); b.put(v, 32); }
        else { b.put(0, 1); b.put(1, 1); b.put(v, 64); }
    }

    /** The code stream from its opcode words. */
    void setCode(int[] words, boolean compressed) {
        Bits b = new Bits(); Lru l = new Lru(false);
        for (int w : words) { long v = w & 0xFFFFFFFFL; if (compressed) putValue(b, l.code(v), v); else b.put(v, 32); }
        code = b.entries(); codeLen = b.n; codeComp = compressed;
    }

    /** One row's log traffic, in row order: a read-log value (an LD's if {@code load}) and/or a write-log value. */
    record LogEvent(boolean hasRead, long read, boolean load, boolean hasWrite, long write) {}

    /** The two logs from the row-ordered log events: E[15] of the write log follows every LD's read value. */
    void setLogs(List<LogEvent> ev, boolean rComp, boolean wComp) {
        Bits rb = new Bits(), wb = new Bits(); Lru rl = new Lru(false), wl = new Lru(true);
        for (LogEvent e : ev) {
            if (e.hasRead()) {
                if (rComp) putValue(rb, rl.code(e.read()), e.read()); else rb.put(e.read(), 64);
                if (e.load()) wl.E[15] = e.read();
            }
            if (e.hasWrite()) { if (wComp) putValue(wb, wl.code(e.write()), e.write()); else wb.put(e.write(), 64); }
        }
        rlog = rb.entries(); rlogLen = rb.n; rlogComp = rComp;
        wlog = wb.entries(); wlogLen = wb.n; wlogComp = wComp;
    }
}
