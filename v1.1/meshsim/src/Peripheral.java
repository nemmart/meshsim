/**
 * The tile's peripheral slot (spec 4.17): what every peripheral offers its tile, and the only way the tile reaches it.
 * The tile's control logic is one design whatever the slot holds; the slot of a type-0 tile is simply null (4.17.7
 * is then the tile's own behaviour: PERIPH_RUN ignored and flagged, lines and status 0, every mode-9 packet a bad
 * peripheral write, counted at once).
 *
 * <h3>Timing (meshsim's bus rule: what happens at t is visible at t + 1)</h3>
 * <ul>
 * <li><b>Inputs</b> ({@link #accept}, {@link #run}, {@link #claim}, {@link #setHalted}) may be called in any order
 *     during cycle t, before or after {@link #runCycle}(t). Each takes effect at the end of t, so the peripheral's
 *     work in cycle t + 1 sees it and nothing in cycle t does. Calls never go back in time.</li>
 * <li><b>Level reads</b> ({@link #running}, {@link #lines}, {@link #status}) called with t return the peripheral's
 *     state at the end of cycle t − 1, including every input made at t − 1, whatever the call order within t.
 *     So a register load issued at t with latency L, which samples at the end of t + L − 2 (4.11), reads
 *     {@code status(t + L − 1, i)} and {@code lines(t + L − 1)}; and {@code running(t + 1)} is 1 after a run
 *     stored at t ("1 from the end of the run store's cycle", 4.17.2).</li>
 * <li><b>Counts and conditions</b> are monotonic event counters, read by the tile in its end-of-cycle step after
 *     every child has run (as {@code Tile.eventCounts} reads the other units'). {@link #landed} is the exception:
 *     it is a pure function of the accept log, because landing is a fixed {@link #RP_LATENCY} after the director.</li>
 * </ul>
 *
 * <h3>Who does what</h3>
 * The tile owns WAKE_EVENTS, WAKE_MASK, WAKE_PEEK, the four down-counters, TILE_STATUS and TILE_CONDITIONS. It merges
 * {@link #lines} into WAKE_EVENTS [18:13]; a consuming load of WAKE_EVENTS calls {@link #claim} with exactly the
 * service-line bits it returned under WAKE_MASK, at the cycle its side effect takes place (t + L − 1); WAKE_PEEK and
 * loads with a line's mask bit clear never call it. It decrements DMA_PERIPH_REMAINING by {@link #landed}(t) at each
 * cycle (nothing while the tile is halted, 4.12), and raises TILE_CONDITIONS [11], [12], [13] when the matching
 * counter of {@link #conditionEvents} grows (nothing is counted while halted; conditions follow 4.13).
 */
interface Peripheral extends Component {
    /** rp_latency (4.17.5): a mode-9 packet handed over at t lands at t + 2, with no queue on the path. */
    int RP_LATENCY = 2;
    /** Indexes into {@link #conditionEvents}: TILE_CONDITIONS bits 11, 12 and 13 (4.17.6). */
    int COND_BAD_WRITE = 0, COND_2 = 1, COND_3 = 2;

    /** TILE_STATUS [15:12] (4.17.1): a constant of the tile. 1 is the replayer. */
    int type();

    // ---------------------------------------------------------------- inputs (take effect at the end of t)
    /**
     * The traffic director hands over one mode-9 packet at t (4.17.5), from any source; at most one per cycle. It
     * lands, or is dropped by the peripheral's write port, at t + {@link #RP_LATENCY}; either way it counts once.
     * @param src   the sending endpoint (for invariant 18's one-source-per-port check)
     * @param addr  the packet's 21-bit word address
     * @param size  the link word's size code: 0 = 32, 1 = 64, 2 = 128 bits
     * @param lo    payload bits [63:0]
     * @param hi    payload bits [127:64] (0 for 32- and 64-bit packets)
     */
    void accept(long t, int src, int addr, int size, long lo, long hi);

    /** PERIPH_RUN (TILE_PULSE [2]) stored at t (4.17.2). A run while running is ignored; the peripheral flags it. */
    void run(long t);

    /**
     * A consuming load of WAKE_EVENTS claims service lines (4.17.3). {@code lines} is a 6-bit mask, bit 0 =
     * PERIPH_SERVICE_1 (WAKE_EVENTS [13]) … bit 5 = PERIPH_SERVICE_6 ([18]): exactly the lines the load returned
     * and its WAKE_MASK covered. Called at the cycle the load takes its side effect. A sticky line raised after the
     * load's sample point survives.
     */
    void claim(long t, int lines);

    /**
     * HALT (or a halted controller, 4.17.7) at t: {@code true} holds the peripheral in reset from the end of t,
     * dropping running, lines and sticky events; its memories keep their contents and the write port follows the
     * peripheral's HALT rules. {@code false} (HALT → RUN) releases it, idle. Calls only on a change.
     */
    void setHalted(long t, boolean halted);

    // ---------------------------------------------------------------- level reads (state at the end of t − 1)
    /** periph_running, TILE_STATUS [3] (4.17.2). */
    boolean running(long t);

    /** The six service lines, bit 0 = PERIPH_SERVICE_1 … bit 5 = PERIPH_SERVICE_6 (WAKE_EVENTS [18:13]). */
    int lines(long t);

    /** PERIPH_STATUS_i, i in 0..7 (4.17.4): no side effects; undefined registers read 0. */
    int status(long t, int i);

    // ---------------------------------------------------------------- counts
    /** 1 if a mode-9 packet lands or is dropped at t (it was accepted at t − RP_LATENCY), else 0. */
    int landed(long t);

    /** Monotonic event counts for TILE_CONDITIONS [11], [12], [13]: index COND_BAD_WRITE, COND_2, COND_3. */
    long conditionEvents(int k);

    // ---------------------------------------------------------------- the sender (4.17.9)
    /** The number of output queues (0–4); 0 for a peripheral with no output, such as the replayer. */
    int outputQueues();
    /** Output queue q holds a 32-bit word for the sender at t (state at the end of t − 1). */
    boolean outReady(long t, int q);
    /** The sender takes queue q's next word at t; only when {@link #outReady} is true. */
    int outTake(long t, int q);
    /** The sender's SENDER_SEND for queue q at t: lets the peripheral produce {@code words} more for it (no unsolicited output). */
    void outStart(long t, int q, int words);

    // ---------------------------------------------------------------- the host
    /** Flags and invariant violations go to the tile (meshsim's {@code violate}); set by the tile. */
    interface Host { void violate(long t, String msg); }
    void attach(Host host);
}
