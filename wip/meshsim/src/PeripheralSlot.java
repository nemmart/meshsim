import java.util.ArrayList;
import java.util.List;

/**
 * A unit's peripherals (spec 4.17, v1.2): up to three, peripheral n at region 0x03 + n and window 0xFFFFF800 + 0x200 n.
 * The unit's side of {@link Peripheral}, and the standard window's slot-owned words:
 *  - presence: PARAMS [2:0] ({@link #present}); an absent peripheral's window is a bus error, a packet to its region a
 *    bad packet (the director);
 *  - the window (4.17.4): words 0-9 standard, 10-31 reserved (read 0, stores ignored, flagged), 32-127 the
 *    peripheral's (undefined ones as reserved); a store to a defined read-only word is a bus error;
 *  - EVENTS_MASK (0 at reset), a consuming load of EVENTS (one count from each set bit under the mask), EVENTS_PEEK; the
 *    controller's live bits EVENTS [15:13] ((events & mask) != 0) and ERRORS [13:11] (ERRORS != 0);
 *  - the DMA (4.17.9): DMA_HDR, DMA_ADDR, PULSE_DMA, STATUS [1], DMA done (EVENTS [1]); one transfer at a time from one
 *    output queue: floor(n / 4) 128-bit packets, then a 64-bit one for 2, a 32-bit one for 1, a 64 and a 32 for 3; each
 *    packet at DMA_ADDR, which advances by its size; at most one packet a cycle into {@link #dmaOut}, the peripheral's
 *    own router port (on C&C 1-3; item 118 defers its details). A mesh tile's peripherals have no DMA (4.19).
 */
final class PeripheralSlot implements Peripheral.Host {
    final Tile tile;
    final Peripheral[] p = new Peripheral[4];             // [1]-[3]; null: absent
    final int[] mask = new int[4];
    final Dma[] dma = new Dma[4];
    long accepted, runsIgnored, reservedAccesses, violations; String lastViolation;

    PeripheralSlot(Tile tile, Peripheral... ps) {
        this.tile = tile;
        for (int n = 1; n <= 3 && n - 1 < ps.length; n++) if (ps[n - 1] != null) { p[n] = ps[n - 1]; p[n].attach(this); dma[n] = new Dma(n); }
    }
    @Override public void violate(long t, String m) { violations++; lastViolation = m; tile.ctl.violate(t, m); }
    /** PARAMS [2:0]. */
    int present() { int v = 0; for (int n = 1; n <= 3; n++) if (p[n] != null) v |= 1 << (n - 1); return v; }
    /** v1.1 compatibility for the replayer's tests: peripheral 1. */
    Peripheral first() { return p[1]; }

    // ---------------------------------------------------------------- the region (4.17.5)
    void accept(long t, int n, long lo, long h, long hi) {
        accepted++;
        if (p[n] == null) return;                         // the director drops packets for an absent peripheral (a bad packet)
        p[n].accept(t, LinkWord.src(h), LinkWord.offset(LinkWord.addr(h)), LinkWord.size(h), lo, hi);
    }

    // ---------------------------------------------------------------- the window (4.17.4); null: a bus error
    Integer read(long t, int n, int w) {
        Peripheral q = p[n];
        if (q == null) return null;
        switch (w) {
            case 0: return q.params();
            case 1: return (q.running(t) ? 1 : 0) | (dma[n].busy(t) ? 2 : 0) | (q.statusBits(t) & ~3);
            case 2: return q.errors(t);
            case 3: case 4: return 0;
            case 5: { q.sync(t); int v = q.events().value(t) | dma[n].doneBit(t), c = v & mask[n]; if (c != 0) { consumeT[n] = t; consumeBits[n] = c; } return v; }
            case 6: q.sync(t); return q.events().value(t) | dma[n].doneBit(t);
            case 7: return mask[n];
            case 8: return q.outputQueues() > 0 ? dma[n].hdr : reserved(t, n, w, false);
            case 9: return q.outputQueues() > 0 ? dma[n].addr : reserved(t, n, w, false);
            default:
                if (w < Registers.STD_RESERVED_END) return reserved(t, n, w, false);
                Integer v = q.word(t, w);
                return v != null ? v : reserved(t, n, w, false);
        }
    }
    /** false: a bus error (absent, or a store to a defined read-only word). */
    boolean write(long t, int n, int w, int v) {
        Peripheral q = p[n];
        if (q == null) return false;
        switch (w) {
            case 0: case 1: case 2: case 5: case 6: return false;
            case 3: if ((v & 1) != 0) q.run(t); return true;
            case 4: if (q.outputQueues() == 0) { reserved(t, n, w, true); return true; } if ((v & 1) != 0) dma[n].start(t); return true;
            case 7: pendMask[n] = v; pendMaskT[n] = t; return true;
            case 8: if (q.outputQueues() == 0) { reserved(t, n, w, true); return true; } dma[n].setHdr(t, v); return true;
            case 9: if (q.outputQueues() == 0) { reserved(t, n, w, true); return true; } dma[n].setAddr(t, v); return true;
            default:
                if (w < Registers.STD_RESERVED_END) { reserved(t, n, w, true); return true; }
                int r = q.writeWord(t, w, v);
                if (r < 0) { reserved(t, n, w, true); return true; }
                return r > 0;
        }
    }
    private int reserved(long t, int n, int w, boolean store) {
        // W4-06: the handler's fixed reports (4.13) read DMA_HDR and DMA_ADDR of every present peripheral, a DMA or not;
        // a handler's loads of words 8 and 9 aren't flagged
        if (!store && (w == 8 || w == 9) && tile.core.inHandler) return 0;
        reservedAccesses++;
        violate(t, (store ? "store to" : "load of") + " peripheral " + n + "'s reserved or undefined window word " + w + ": reads 0, stores ignored (4.17.4)");
        return 0;
    }
    private final long[] consumeT = { -1, -1, -1, -1 }, pendMaskT = { -1, -1, -1, -1 };
    private final int[] consumeBits = new int[4], pendMask = new int[4];

    /** The controller's live bits: EVENTS [15:13] (events & mask non-zero) and ERRORS [13:11], as at the end of t - 1. */
    int liveEvents(long t) { int v = 0; for (int n = 1; n <= 3; n++) if (p[n] != null && mask[n] != 0 && (sync(n, t) | (p[n].events().value(t) | dma[n].doneBit(t)) & mask[n]) != 0) v |= 1 << (n - 1); return v; }
    private int sync(int n, long t) { p[n].sync(t); return 0; }
    int liveErrors(long t) { int v = 0; for (int n = 1; n <= 3; n++) if (p[n] != null && p[n].errors(t) != 0) v |= 1 << (n - 1); return v; }

    void runCycle(long t) {
        for (int n = 1; n <= 3; n++) if (p[n] != null) { p[n].runCycle(t); dma[n].runCycle(t); }
    }
    /** After every child has run t: the consuming loads and EVENTS_MASK stores of t take effect. */
    void endCycle(long t) {
        for (int n = 1; n <= 3; n++) {
            if (p[n] == null) continue;
            if (consumeT[n] == t) { p[n].events().consume(consumeBits[n] & ~2); if ((consumeBits[n] & 2) != 0) dma[n].done = 0; }
            if (pendMaskT[n] == t) mask[n] = pendMask[n];
        }
    }
    /** The reset (6.1): every peripheral idle, its EVENTS_MASK 0, its DMA idle. */
    void reset(long t) {
        for (int n = 1; n <= 3; n++) if (p[n] != null) { p[n].reset(t); mask[n] = 0; dma[n] = new Dma(n); consumeT[n] = -1; pendMaskT[n] = -1; }
    }
    boolean inFlight(long t) { return false; }

    /** The output port every DMA packet leaves by (4.17.9): {t, dst, mode, counter, addr, size, lo, hi}; tests read it, and
     *  a controller's unit wires it to its router port ({@link #dmaPort}). */
    final List<long[]> dmaOut = new ArrayList<>();
    WordSink dmaPort;

    /** A peripheral's DMA (4.17.9). */
    final class Dma {
        final int n;
        int hdr, addr, done;                          // done: DMA done's count (depth 1)
        private long doneAt = Long.MIN_VALUE, startedAt = Long.MIN_VALUE, busyFrom = Long.MAX_VALUE, busyTo = Long.MIN_VALUE;
        private boolean busy;
        private int left, q, have;
        private final int[] buf = new int[4];
        long transfers, packets, ignored;
        Dma(int n) { this.n = n; }
        boolean busy(long t) { return busy && t > startedAt; }
        int doneBit(long t) { return done > 0 && doneAt < t ? 2 : 0; }
        void setHdr(long t, int v) { if (busy) violate(t, "peripheral " + n + "'s DMA_HDR changed mid-transfer: undefined (4.17.9)"); hdr = v; }
        void setAddr(long t, int v) { if (busy) violate(t, "peripheral " + n + "'s DMA_ADDR changed mid-transfer: undefined (4.17.9)"); addr = v; }
        void start(long t) {
            Peripheral pp = p[n];
            int qq = Registers.DH_QUEUE.get(hdr);
            if (busy) { ignored++; violate(t, "PULSE_DMA while peripheral " + n + "'s DMA is in progress: ignored (4.17.9)"); return; }
            if (qq >= pp.outputQueues()) { ignored++; violate(t, "PULSE_DMA naming output queue " + qq + ", which peripheral " + n + " doesn't have: ignored (4.17.9)"); return; }
            q = qq; left = Registers.DH_COUNT.get(hdr); have = 0; busy = true; startedAt = t; transfers++;
            if (left > 0) pp.outStart(t, q, left);
        }
        void runCycle(long t) {
            if (!busy || t <= startedAt) return;
            if (left == 0) { finish(t); return; }
            int need = left >= 4 ? 4 : left >= 2 ? 2 : 1;
            while (have < need && p[n].outReady(t, q)) buf[have++] = p[n].outTake(t, q);
            if (have < need) return;
            if (dmaPort != null && !dmaPort.canPut(t)) return;
            int size = need == 4 ? LinkWord.SIZE_128 : need == 2 ? LinkWord.SIZE_64 : LinkWord.SIZE_32;
            long lo = buf[0] & 0xFFFFFFFFL, hi = 0;
            if (need >= 2) lo |= (long) buf[1] << 32;
            if (need == 4) hi = (buf[2] & 0xFFFFFFFFL) | (long) buf[3] << 32;
            int dst = Registers.HDR_DST.get(hdr), mode = Registers.HDR_MODE.get(hdr), ctr = Registers.HDR_CTR.get(hdr);
            dmaOut.add(new long[] { t, dst, mode, ctr, addr & 0xFFFFFFFFL, size, lo, hi });
            if (dmaPort != null) dmaPort.put(t, lo, LinkWord.hdr(0, dst, mode, ctr, addr, size), hi);
            packets++;
            addr += 4 * need; left -= need; have = 0;
            if (left == 0) finish(t);
        }
        private void finish(long t) { busy = false; if (done < 1) done = 1; doneAt = t; }
    }
}
