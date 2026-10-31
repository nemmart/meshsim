import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * Stage A's standalone harness: one {@link Replayer} fed by an ideal controller, through the slot's interface only.
 * The controller loads part 0, the decode table and the ROM with mode-0 packets into region 0x04, starts a pass, and then
 * every cycle consumes every event it sees (a load of the replayer's EVENTS, every bit in the mask, v1.2's counted
 * events, 4.17.3) and answers: a queue's credit with min(half, entries left) packets of that stream, new expansion with
 * the template of the presented instruction from the next offset (min(128, words)), the expansion's credit with the
 * expansion's next words. (Internally the driver keeps v1.1's word addresses; the port takes byte offsets, x 4.)
 * The port takes one packet per cycle, as the traffic director hands them over; expansion words go first.
 * Only the replayer's inputs are used; the records come out through its sink.
 */
final class ReplayDriver {
    static final int SRC = 0;                              // the tile's own endpoint id (Replayer(0): only the tile itself refills, invariant 16)
    final Replayer r;
    final ReplayWindow w;
    long t;
    final List<long[]> rows = new ArrayList<>();
    final ArrayDeque<long[]> port = new ArrayDeque<>();     // {addr, lo, hi}
    final ArrayDeque<long[]> portExp = new ArrayDeque<>();  // expansion words: ahead of everything else
    int codeNext, rlogNext, wlogNext;                      // next entry of each stream to send
    long[] expWords; int expNext;                          // the current controller-fed expansion
    boolean done;
    long runAt, doneAt;
    long missingTemplates;
    List<String> log = new ArrayList<>();

    ReplayDriver(ReplayWindow w) { this(w, new Replayer(0, w.rows)); }
    ReplayDriver(ReplayWindow w, Replayer r) {
        this.w = w; this.r = r;
        r.rowListener = rows::add;
    }

    void step() {
        if (!port.isEmpty() || !portExp.isEmpty()) {
            long[] p = !portExp.isEmpty() ? portExp.poll() : port.poll();
            r.accept(t, SRC, (int) p[0] << 2, LinkWord.SIZE_128, p[1], p[2]);
        }
        r.runCycle(t);
        t++;
    }

    /** Region 0, the decode table and the ROM, by mode 9, with the replayer stopped. */
    void load() {
        if (w.region0 != null) {                       // a delivered record: bytes 16-1,071 unchanged
            for (int i = 0; i < 66; i++) port.add(new long[] { 4 * i, w.region0[2 * i], w.region0[2 * i + 1] });
        } else {
            port.add(new long[] { 0x0, w.stateLo(), w.stateHi() });
            port.add(new long[] { 0x4, w.streamLo(), w.streamHi() });
            for (int i = 0; i < 64; i++) port.add(new long[] { 0x8 + 4 * i, w.regs[2 * i], w.regs[2 * i + 1] });
        }
        for (int p = 0; p < 256; p++) {
            long lo = 0, hi = 0;
            for (int k = 0; k < 4; k++) lo |= (w.table[8 * p + k] & 0xFFFFL) << (16 * k);
            for (int k = 0; k < 4; k++) hi |= (w.table[8 * p + 4 + k] & 0xFFFFL) << (16 * k);
            port.add(new long[] { 0x400 + 4 * p, lo, hi });
        }
        for (int j = 0; j < 512; j++) port.add(new long[] { 0x800 + 4 * j, w.romLo[j], w.romHi[j] });
        while (!port.isEmpty()) step();
        for (int i = 0; i < Peripheral.RP_LATENCY + 1; i++) step();
    }

    /** One pass. Returns the rows; stops at done or after {@code maxCycles}. */
    List<long[]> pass(long maxCycles) {
        rows.clear(); done = false; codeNext = rlogNext = wlogNext = 0; expWords = null;
        r.run(t); runAt = t;
        step();
        long end = t + maxCycles;
        while (t < end) {
            r.sync(t);
            int l = r.events().value(t);
            if (l != 0) {
                int st0 = r.status(t, 0), st1 = r.status(t, 1);
                r.events().consume(l);
                if ((l & 1 << Replayer.EV_CODE) != 0) codeNext = refill(w.code, codeNext, 0x40000, 128);
                if ((l & 1 << Replayer.EV_RLOG) != 0) rlogNext = refill(w.rlog, rlogNext, 0x80000, 256);
                if ((l & 1 << Replayer.EV_WLOG) != 0) wlogNext = refill(w.wlog, wlogNext, 0xC0000, 256);
                if ((l & 1 << Replayer.EV_NEWEXP) != 0) {
                    long[] tpl = w.templates.get(st1);
                    if (tpl == null) { missingTemplates++; log.add("@" + t + " no template for 0x" + Integer.toHexString(st1)); tpl = new long[0]; }
                    int off = st0 >>> 16, rd0 = w.templateRd0.getOrDefault(st1, (int) (tpl.length > 0 ? (tpl[0] >>> 46) & 3 : 0));
                    if (((st1 >>> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) {
                        // N3: a NOP policy with rd = x0: the single NOP word (ROM word 1) carrying the template's rd0
                        expWords = new long[] { w.romLo[1] & ~(3L << 46) | (long) rd0 << 46, w.romHi[1] }; expNext = 0;
                    } else if (off > 0 && 2 * off < tpl.length) {
                        // N3: a mid-sequence start: the first word sent carries the template's rd0
                        expWords = tpl.clone(); expWords[2 * off] = expWords[2 * off] & ~(3L << 46) | (long) rd0 << 46; expNext = 2 * off;
                    } else { expWords = tpl; expNext = 2 * off; }
                    // the first chunk goes out at once, on an expansion credit: the one this load took, or one taken now (4.19.8)
                    if ((l & 1 << Replayer.EV_EXP) == 0) r.events().consume(1 << Replayer.EV_EXP);
                    l &= ~(1 << Replayer.EV_EXP);
                    sendExp();
                }
                if ((l & 1 << Replayer.EV_EXP) != 0) sendExp();
                if ((l & 1 << Replayer.EV_RUN_DONE) != 0) { done = true; doneAt = t; }
            }
            step();
            if (done && r.outQueueRows() == 0) break;
        }
        for (int i = 0; i < 4; i++) step();       // let the sink take the last rows
        return rows;
    }

    private int refill(long[] s, int next, int base, int half) {
        int n = Math.min(half, s.length / 2 - next);
        for (int i = 0; i < n; i++, next++) port.add(new long[] { base, s[2 * next], s[2 * next + 1] });
        return next;
    }
    private void sendExp() {
        if (expWords == null) return;
        int n = Math.min(128, (expWords.length - expNext) / 2);
        for (int i = 0; i < n; i++, expNext += 2) portExp.add(new long[] { 0x100000, expWords[expNext], expWords[expNext + 1] });
    }
}
