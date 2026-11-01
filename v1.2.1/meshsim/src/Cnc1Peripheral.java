import java.util.ArrayDeque;

/**
 * C&C 1's peripheral 1, Ingress/Egress and the UART (spec 6.6, 8.14, 4.20; v1.2): every chip-level register. C&C 1
 * is a behavioural Java model ({@link CnC}), so nothing loads this window through a core: the model and the tests use
 * {@link #read} and {@link #write} at the window's word numbers, with the standard words' rules (4.17.4), and the
 * outside world drives {@link #readyPulse}, {@link #errorLine} and {@link #rxByte}.
 *
 * Words 32-45: ERROR_LINES_0-2 (live: unit id's ERRORS non-zero, error_latency behind), READY_LINES_0-1 (sticky,
 * set by READY pulses, cleared by PULSE_GO; a pulse in the clearing cycle is kept), READY_EXPECT_0-1, PULSE_GO (clears
 * READY 0-63, drives GO, restarts the watchdog), WATCHDOG (0 off; N > 0 starts it from N; a write or PULSE_GO restarts
 * it from the last N; reaching 0 posts its event and latches the toast pin; a load returns the count), LED [0], and the
 * UART: UART_TX (pushes a byte; the store stalls while the TX FIFO is full: {@link #write} returns -2), UART_RX (the
 * oldest byte [7:0] and valid [8], popped only when valid), UART_DIVISOR (reference-oscillator cycles per bit; a new
 * value takes effect at each direction's next idle point), UART_STATUS (TX occupancy [4:0], RX occupancy [12:8], RX
 * overrun [16], framing error [17]; write 1 to clear).
 * ERRORS: [0] the unit-error bit, OR(ERROR_LINES), live. EVENTS: [0] run done, [1] DMA done (never: no run, no DMA in
 * meshsim), [2] all tiles READY (live), [3] watchdog expired (depth 1), [4] UART RX not empty, [5] UART TX not full
 * (live). PARAMS: type 1, Ingress/Egress with the UART and the chip lines (4.17.1, D46; W4-05).
 * UART timing: one frame (start, 8 data, stop) takes 10 x divisor reference cycles, Config.refCycle core cycles each.
 */
final class Cnc1Peripheral implements Peripheral {
    static final int ERROR_LINES = 32, READY_LINES = 35, READY_EXPECT = 37, PULSE_GO = 39, WATCHDOG = 40, LED = 41,
            UART_TX = 42, UART_RX = 43, UART_DIVISOR = 44, UART_STATUS = 45;
    static final int EV_ALL_READY = 2, EV_WATCHDOG = 3, EV_UART_RX = 4, EV_UART_TX = 5;
    static final int FIFO = 16, RESET_DIVISOR = Uart.RESET_DIVISOR;
    final Config cfg;
    final EventCounts ev = new EventCounts(new int[] { 1, 1, 0, 1, 0, 0 }, new boolean[6]);
    Host host;
    /** GO's net (6.3): called with the PULSE_GO store's cycle. */
    java.util.function.LongConsumer goNet;

    // the lines
    private final ArrayDeque<long[]> errChanges = new ArrayDeque<>();   // {visible at, id, 0/1}
    final boolean[] errLine = new boolean[128];
    private final long[] readyAt = new long[64];                       // latest READY arrival (Long.MIN_VALUE: none)
    private long clearedAt = Long.MIN_VALUE;
    long expect;                                                        // READY_EXPECT_0-1
    // the watchdog, the pins
    private long wdCount, wdN;
    boolean toast; long toastAt = -1;
    int led;
    // the UART
    private final ArrayDeque<Integer> tx = new ArrayDeque<>(), rx = new ArrayDeque<>();
    private int divisor = RESET_DIVISOR, txDivisor = RESET_DIVISOR, rxDivisor = RESET_DIVISOR;
    private long txBusyUntil = Long.MIN_VALUE;
    boolean rxOverrun, framing;
    final StringBuilder serialOut = new StringBuilder();                // every byte the TX shifter sent
    final java.util.List<long[]> serialLog = new java.util.ArrayList<>();   // {t, byte}
    long gos;

    Cnc1Peripheral(Config cfg) { this.cfg = cfg; java.util.Arrays.fill(readyAt, Long.MIN_VALUE); }
    @Override public int type() { return 1; }
    @Override public int params() { return 1; }                    // type 1, Ingress/Egress with the UART (4.17.1, D46), version 0
    @Override public void attach(Host h) { host = h; }
    @Override public EventCounts events() { return ev; }
    @Override public void accept(long t, int src, int offset, int size, long lo, long hi) { violate(t, "a write into C&C 1's peripheral region: its map is Ingress/Egress's, deferred"); }
    @Override public void run(long t) {}
    /** The reset (6.1): READY, READY_EXPECT, the watchdog (off), LED, the toast latch, the UART's FIFOs and status, the
     *  divisor to its reset value; the error lines in flight are reset with everything else. */
    @Override public void reset(long t) {
        errChanges.clear(); java.util.Arrays.fill(errLine, false); java.util.Arrays.fill(readyAt, Long.MIN_VALUE); clearedAt = Long.MIN_VALUE;
        expect = 0; wdCount = 0; wdN = 0; toast = false; toastAt = -1; led = 0;
        tx.clear(); rx.clear(); divisor = txDivisor = rxDivisor = RESET_DIVISOR; txBusyUntil = Long.MIN_VALUE; rxOverrun = false; framing = false;
        ev.clearAll(); mask = 0;
    }
    @Override public boolean running(long t) { return false; }
    private void violate(long t, String m) { if (host != null) host.violate(t, m); }

    // ---------------------------------------------------------------- the lines (the outside world and the units)
    /** A unit's error line (its ERRORS != 0) as it stands at the end of t; visible error_latency later. */
    void errorLine(long t, int id, boolean high) { errChanges.add(new long[] { t + Math.max(1, cfg.errorLatency()), id, high ? 1 : 0 }); }
    /** A tile's READY pulse at tEdge, visible ready_latency later. */
    void readyPulse(int tile, long tEdge) { readyAt[tile] = Math.max(readyAt[tile], tEdge + cfg.readyLatency); }
    boolean ready(int tile, long t) { return readyAt[tile] != Long.MIN_VALUE && readyAt[tile] >= clearedAt && readyAt[tile] <= t; }
    long readyBits(long t) { long b = 0; for (int i = 0; i < 64; i++) if (ready(i, t)) b |= 1L << i; return b; }
    boolean allReady(long t) { return expect != 0 && (readyBits(t) & expect) == expect; }
    /** Clears every READY bit visible at t (a pulse arriving during t is kept). */
    void clearReady(long t) { clearedAt = t + 1; }
    /** PULSE_GO at t: clear READY 0-63, drive GO, restart the watchdog (6.1, 6.3). */
    void pulseGo(long t) { clearReady(t); gos++; if (goNet != null) goNet.accept(t); if (wdN > 0) wdCount = wdN; }
    /** A byte arriving on the serial line at t (framing: a bad stop bit, dropped). */
    void rxByte(long t, int b, boolean badStop) {
        if (badStop) { framing = true; return; }
        if (rx.size() >= FIFO) { rxOverrun = true; return; }
        rx.add(b & 0xFF);
    }
    boolean errorLineOf(int id) { return errLine[id]; }

    // ---------------------------------------------------------------- the window (C&C 1's own accesses, as a core's would be)
    @Override public int errors(long t) { for (boolean b : errLine) if (b) return 1; return 0; }
    @Override public Integer word(long t, int w) {
        switch (w) {
            case 32: case 33: case 34: { int v = 0; for (int b = 0; b < 32; b++) if (errLine[32 * (w - 32) + b]) v |= 1 << b; return w == 34 ? v & 6 : v; }
            case 35: return (int) readyBits(t);
            case 36: return (int) (readyBits(t) >>> 32);
            case 37: return (int) expect;
            case 38: return (int) (expect >>> 32);
            case 39: return 0;
            case 40: return (int) wdCount;
            case 41: return led;
            case 42: return 0;
            case 43: { Integer b = rx.poll(); return b == null ? 0 : b | 0x100; }          // pops when valid
            case 44: return divisor;
            case 45: return tx.size() | rx.size() << 8 | (rxOverrun ? 1 << 16 : 0) | (framing ? 1 << 17 : 0);
            default: return null;
        }
    }
    /** 1 written, 0 read-only (bus error), -1 undefined, -2 a UART_TX store that must stall (the TX FIFO is full). */
    @Override public int writeWord(long t, int w, int v) {
        switch (w) {
            case 37: expect = (expect & ~0xFFFFFFFFL) | (v & 0xFFFFFFFFL); return 1;
            case 38: expect = (expect & 0xFFFFFFFFL) | (long) v << 32; return 1;
            case 39: if ((v & 1) != 0) pulseGo(t); return 1;
            case 40: wdN = v & 0xFFFFFFFFL; wdCount = wdN; return 1;
            case 41: led = v & 1; return 1;
            case 42: if (tx.size() >= FIFO) return -2; tx.add(v & 0xFF); return 1;
            case 44: divisor = v & 0xFFFF; return 1;
            case 45: if ((v >> 16 & 1) != 0) rxOverrun = false; if ((v >> 17 & 1) != 0) framing = false; return 1;
            case 32: case 33: case 34: case 35: case 36: case 43: return 0;
            default: return -1;
        }
    }
    /** C&C 1's model reads a word (the standard ones included): the 4.17.4 rules; null = a bus error never arises here. */
    int read(long t, int w) {
        if (w == 0) return params();
        if (w == 1) return 0;
        if (w == 2) return errors(t);
        if (w == 5 || w == 6) { int v = ev.value(t); if (w == 5) ev.consume(v & mask); return v; }
        if (w == 7) return mask;
        Integer v = word(t, w);
        return v == null ? 0 : v;
    }
    int mask;
    /** C&C 1's model stores a word; returns writeWord's code (or 1 for the standard words it takes). */
    int write(long t, int w, int v) { if (w == 7) { mask = v; return 1; } if (w < 32) return w == 3 || w == 4 ? 1 : 0; return writeWord(t, w, v); }

    // ---------------------------------------------------------------- the cycle
    @Override public void runCycle(long t) {
        while (!errChanges.isEmpty() && errChanges.peek()[0] <= t) { long[] c = errChanges.poll(); errLine[(int) c[1]] = c[2] == 1; }
        if (wdCount > 0 && --wdCount == 0) { ev.post(t, EV_WATCHDOG); if (!toast) { toast = true; toastAt = t; } }
        if (errors(t) != 0 && !toast) { toast = true; toastAt = t; }                  // any C&C 1 error raises the toast pin (6.1)
        // the TX shifter: a frame is 10 bit times; a new divisor takes effect between frames
        if (t >= txBusyUntil && !tx.isEmpty()) {
            txDivisor = divisor;                                         // a new divisor takes effect between frames
            int b = tx.poll(); serialOut.append((char) b); serialLog.add(new long[] { t, b });
            txBusyUntil = t + 10L * txDivisor * Math.max(1, cfg.refCycle);
        }
        ev.live(EV_ALL_READY, allReady(t));
        ev.live(EV_UART_RX, !rx.isEmpty());
        ev.live(EV_UART_TX, tx.size() < FIFO);
    }
    /** One frame's time in core cycles at the current divisor. */
    long frameCycles() { return 10L * divisor * Math.max(1, cfg.refCycle); }
}
