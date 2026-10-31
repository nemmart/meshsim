/**
 * A peripheral (spec 4.17, v1.2): what every peripheral offers its unit, and the only way the unit reaches it. A unit has
 * up to three ({@link PeripheralSlot} holds them); the unit's control logic is one design whatever it holds.
 *
 * The standard window (4.17.4) is split between the slot and the peripheral: the slot owns EVENTS_MASK, the DMA's
 * registers and engine (DMA_HDR, DMA_ADDR, PULSE_DMA, STATUS [1], EVENTS [1]) and the access rules; the peripheral owns
 * PARAMS, STATUS's other bits, ERRORS, PULSE_RUN, its events ({@link #events}, counted with their depths) and its words
 * 32-127.
 *
 * <h3>Timing (meshsim's bus rule: what happens at t is visible at t + 1)</h3>
 * Inputs ({@link #accept}, {@link #run}, the consuming load through {@link #events}) may be called in any order during
 * cycle t and take effect at the end of t. Level reads called with t return the state at the end of t - 1 (a load
 * issued at t with latency L reads them at its sample point). A region write accepted at t lands, or is dropped by the
 * peripheral's write port, at t + {@link #RP_LATENCY}; it was counted at the director either way (4.17.5).
 */
interface Peripheral extends Component {
    /** rp_latency (4.17.5): a write handed over at t lands at t + 2, with no queue on the path. */
    int RP_LATENCY = 2;

    /** PARAMS (window word 0): [7:0] type, [15:8] version, the rest peripheral-defined. */
    int params();
    int type();

    // ---------------------------------------------------------------- inputs (take effect at the end of t)
    /**
     * The traffic director hands over one mode-0 write into the peripheral's region at t (4.17.5), from any source; at
     * most one per cycle. The peripheral's write port judges it at t + RP_LATENCY.
     * @param src    the sending endpoint (invariant 18's one-source-per-port check)
     * @param offset the packet address's 24-bit byte offset in the region
     * @param size   the link word's size code: 0 = 32, 1 = 64, 2 = 128 bits
     */
    void accept(long t, int src, int offset, int size, long lo, long hi);
    /** PULSE_RUN stored with 1 at t (4.17.2). A run while running is ignored; the peripheral flags it. */
    void run(long t);

    // ---------------------------------------------------------------- level reads (state at the end of t - 1)
    /** STATUS [0], run in progress: the ownership state (4.17.2). */
    boolean running(long t);
    /** STATUS's peripheral-defined bits, [31:2] in place ([1:0] are the slot's and the run bit). */
    default int statusBits(long t) { return 0; }
    /** ERRORS (window word 2), sticky until the reset (4.17.6). */
    int errors(long t);
    /** The peripheral's events (4.17.3): bit [1], DMA done, is posted by the slot. */
    EventCounts events();
    /** Bring the model to the start of t before the slot reads its events (a lazily settled model). */
    default void sync(long t) {}
    /** A load of window word w, 32-127, sampling at t; null: an undefined word (reads 0, flagged by the slot). */
    default Integer word(long t, int w) { return null; }
    /** A store to window word w, 32-127, at t: 1 written, 0 read-only (a bus error), -1 undefined (ignored, flagged). */
    default int writeWord(long t, int w, int v) { return word(t, w) == null ? -1 : 0; }

    // ---------------------------------------------------------------- output (4.17.9): the DMA's source
    /** The number of output queues (0-4); 0 for a peripheral without a DMA, such as the replayer. */
    default int outputQueues() { return 0; }
    /** Output queue q holds a 32-bit word for the DMA at t (state at the end of t - 1). */
    default boolean outReady(long t, int q) { return false; }
    /** The DMA takes queue q's next word at t; only when {@link #outReady} is true. */
    default int outTake(long t, int q) { throw new IllegalStateException("no output queue"); }
    /** PULSE_DMA for queue q at t: lets the peripheral produce {@code words} for it (no unsolicited output). */
    default void outStart(long t, int q, int words) {}

    // ---------------------------------------------------------------- the host
    /** Flags and invariant violations go to the unit (meshsim's {@code violate}); set by the slot. */
    interface Host { void violate(long t, String msg); }
    void attach(Host host);
    /** A halted controller or HALT (PHASE-3 INTERIM until phase 4's reset): hold the peripheral idle; its memories keep. */
    default void setHalted(long t, boolean halted) {}
}
