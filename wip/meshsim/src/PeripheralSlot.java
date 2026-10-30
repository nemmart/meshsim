/**
 * The tile's peripheral slot (spec 4.17): the tile's side of {@link Peripheral}. The tile's control logic is one design
 * whatever the slot holds; the slot of a type-0 tile holds nothing ({@link #p} null) and behaves as 4.17.7 says:
 * PERIPH_RUN ignored and flagged, periph_running 0, the lines and PERIPH_STATUS_0-7 0, every mode-9 packet a bad
 * peripheral write (condition 11), counted in DMA_PERIPH_REMAINING at once (in RUN).
 *
 * With a peripheral (type 1, the replayer, on every mesh tile by default: Config.periphType):
 *  - the director hands it each mode-9 packet ({@link Peripheral#accept}); it lands, or is dropped by the peripheral's
 *    write port, rp_latency later, and DMA_PERIPH_REMAINING counts it then ({@link #endCycle}: landed(t)), in RUN only;
 *  - PERIPH_RUN (TILE_PULSE [2]) calls {@link Peripheral#run} at the store's cycle; a run while running is the
 *    peripheral's to ignore and flag;
 *  - the controller's register loads read running, lines and status at the cycle they sample (TileControl: a load
 *    issued at t with latency L samples at t + L - 1), and a consuming WAKE_EVENTS load claims the lines it returned
 *    under WAKE_MASK at that cycle;
 *  - HALT and a halted controller call setHalted(true), HALT -> RUN setHalted(false), only on a change;
 *  - runCycle is called every cycle, in HALT too (the write port lands boot loads in HALT);
 *  - conditions 11-13 are the peripheral's monotonic event counts.
 */
final class PeripheralSlot implements Peripheral.Host {
    final Tile tile;
    final Peripheral p;                              // null: type 0, an empty slot
    private boolean held;                            // the peripheral is held in reset (HALT, or a halted controller)
    long badWrites;                                  // monotonic: condition 11 on a type-0 tile
    long accepted, runsIgnored, landedCounted;

    PeripheralSlot(Tile tile, int type) {
        this.tile = tile;
        p = switch (type) {
            case 0 -> null;
            case 1 -> new Replayer(tile.idx);
            default -> throw new IllegalArgumentException("peripheral type " + type + ": only 0 (none) and 1 (the replayer) exist");
        };
        if (p != null) p.attach(this);
    }
    /** A peripheral built elsewhere (tests: a peripheral with output for the sender). */
    PeripheralSlot(Tile tile, Peripheral q) { this.tile = tile; p = q; if (p != null) p.attach(this); }

    int type() { return p == null ? 0 : p.type(); }
    @Override public void violate(long t, String msg) { tile.ctl.violate(t, "peripheral: " + msg); }

    /** The director hands over a mode-9 packet at t (4.17.5). */
    private long lastAcceptT = Long.MIN_VALUE / 2;
    /** A mode-9 packet handed over and not yet landed (in the rp_latency path). */
    boolean inFlight(long t) { return lastAcceptT + Peripheral.RP_LATENCY >= t; }
    /** v1.2 (4.17.5): a mode-0 write into peripheral n's region; counted at the director by its counter field (K), not
     *  here. PHASE-1 INTERIM: the peripheral still takes v1.1's word address (the byte offset / 4) until phase 3 moves the
     *  replayer onto its v1.2 map. */
    void accept(long t, int n, long lo, long h, long hi) {
        accepted++; lastAcceptT = t;
        if (p == null || n != 1) { badWrites++; return; }   // the director drops packets to an absent peripheral; defensive
        p.accept(t, LinkWord.src(h), LinkWord.offset(LinkWord.addr(h)) >>> 2, LinkWord.size(h), lo, hi);
    }
    /** PERIPH_RUN stored at t (4.17.2). */
    void run(long t) {
        if (p == null) { runsIgnored++; tile.ctl.violate(t, "PERIPH_RUN (TILE_PULSE [2]) on a tile with no peripheral (type 0): ignored (4.17.7)"); return; }
        p.run(t);
    }
    boolean running(long t) { return p != null && p.running(t); }
    int lines(long t) { return p == null ? 0 : p.lines(t) & 0x3F; }
    int status(long t, int i) { return p == null ? 0 : p.status(t, i); }
    void claim(long t, int lines) { if (p != null && lines != 0) p.claim(t, lines); }
    /** HALT or a halted controller (true), HALT -> RUN (false): passed on only on a change. */
    void setHalted(long t, boolean h) { if (h != held) { held = h; if (p != null) p.setHalted(t, h); } }
    boolean held() { return held; }
    /** Monotonic counts for TILE_CONDITIONS [11], [12], [13]. */
    long conditionEvents(int k) { return (k == 0 ? badWrites : 0) + (p == null ? 0 : p.conditionEvents(k)); }
    void runCycle(long t) { if (p != null) p.runCycle(t); }
    /** After every child has run cycle t: a write that landed or was dropped at t (v1.2 counts at the director). */
    void endCycle(long t) {
        if (p != null && p.landed(t) != 0) landedCounted++;
    }
}
