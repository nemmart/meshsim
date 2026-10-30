import java.nio.file.Path;

/** Simulator parameters (spec 10). Defaults are the spec's; tests override fields before building a Chip. */
final class Config {
    Hops hops;
    // ---- network (spec 3.2, 4.7) ----
    int linkLatency = 3;                 // every link direction (spec 3.2)
    int[][] linkLatencyOverride;         // [tile][direction], tests only (null = uniform)
    int queueDepth = 16;                 // router FIFOs, including tile 0's C&C FIFOs
    int intraTileLatency = 1;            // each registered stage between units (spec 4)
    // ---- start skew (spec 3.2): each tile +0 or +1 on GO and RUN ----
    boolean skew = true;
    long skewSeed = 1;
    int[] startSkew;                     // override per tile (null = drawn from skewSeed when skew is on)
    // ---- receive side (spec 4.3, 4.4, 4.8, 4.9) ----
    int weInDepth = 4;                   // write engine input FIFO
    int readPortDepth = 64;              // the controller read port's request queue (4.3: at least 64, guaranteed)
    int gatherDepth = 16;                // GATHER request queue
    int cmLatency = 2;                   // director to port B (built, M1 of v3.11)
    int rpLatency = 2;                   // director to a peripheral's write landing (4.17.5: required of every peripheral; Proposed value, item 48)
    int K = Integer.getInteger("meshsim.K", 8);   // the write counters' delay line (4.8): at least max(2 L + 3, L + 4, cm_latency, rp_latency)
    // ---- big SRAM (spec 4.1) ----
    int memChunks = 1 << 20;             // 4 MB; any multiple of 256 KB (65,536 chunks) up to 8 MB (2^21); the last area may be partial (4.1)
    int areaQueueDepth = 8;              // per-area request queue (open in the spec; agreed default)
    int sramReadLatency = 2;             // array read latency (open; agreed default)
    boolean writeCombining = false;      // merge a 32- or 64-bit write into the queued write to the same row (open item 3; off)
    boolean fifoCountBeforePop = true;   // 13a / V1.0 item 5: every FIFO is full by its count before the cycle's pop (the area queues, the
                                         // token FIFO, the overflow FIFOs). false: M15's rule (room after the pop), for comparison only
    int readTokens = 64;                 // the token pool (4.5): reads in flight plus packets in the loopback
    // Experiments only (item 116: what removed v1.0's read-port loss): the eject selector's loopback-last priority (4.7)
    // and the loopback's tokens (4.5). -Dmeshsim.loopbackLast=false / -Dmeshsim.loopbackTokens=false.
    boolean loopbackLast = !"false".equals(System.getProperty("meshsim.loopbackLast"));
    boolean loopbackTokens = !"false".equals(System.getProperty("meshsim.loopbackTokens"));
    int loopbackDepth = Integer.getInteger("meshsim.loopbackDepth", -1);              // the eject output's loopback FIFO (4.7: at least the token count); -1 = readTokens
    // ---- engines and the table store (spec 4.2, 4.9, 4.10) ----
    int tsEngineLatency = 1;             // engine <-> table store, each way
    int agLookahead = -1;                // entries in each of the address generator's two read-ahead queues (STREAM, GROUP;
                                         // M8, PROPOSAL-slot-iteration-counter.md); <= 0: the default 2 ts_engine_latency + 2
    @Deprecated int streamLookahead = 4;   // M7's stream-run lookahead. Dead since Not read; agLookahead replaces it
    /** ag_lookahead as used: the field if set, else 2 ts_engine_latency + 2. */
    int agLookahead() { return agLookahead > 0 ? agLookahead : 2 * tsEngineLatency + 2; }
    boolean testReadClients = false;     // tests: the SRAM read queue's AG input is a FIFO the test fills (no AG)
    // ---- the controller (spec 4.11) ----
    int regLoadLatency = 2;              // register and table-window load latency, cycles (4.11: 1 or 2, fixed per register; meshsim models 2, the worst case: its reference setting)
    // ---- C&C (spec 6.2, section 10) ----
    int stageDelay = 32;                 // stage_delay: C&C's first staged packet no sooner than this after its GO store (Proposed)
    // ---- the request bus (spec 3.3, 4.18) ----
    int requestFifoDepth = 128;          // the request FIFO: 128 x 128 bits (4.18)
    int receiveQueueDepth = 16;          // the receive queue in every tile, and each target's credits at the root (3.3)
    int reqHopLatency = 3;               // cycles a beat takes on each hop's pipelined wires (meshsim's choice: reqbus_latency is an estimate, 3.3)
    /** Test only: target k's receiver is tile targetReceiver[k] (-1 or null: no receiver; the endpoint map is deferred, item 82). */
    int[] targetReceiver;
    // ---- the peripheral slot (spec 4.17): the type on every tile (4.17.1: 1, the replayer, on tiles 0-63), or per tile ----
    int periphType = 1;
    int[] periphTypes;                   // per-tile override (null: periphType everywhere)
    int periphTypeOf(int tile) { return periphTypes != null ? periphTypes[tile] : periphType; }
    /** Tests only: a peripheral built by the test for a tile (null: the tile's type). */
    java.util.function.IntFunction<Peripheral> testPeripheral;              // register and table-window load latency, cycles (4.11: 1 or 2; meshsim's reference 2)
    // ---- control (spec 6, 10) ----
    boolean realResponder = false;       // M1-M3 test machines: the GATHER engine is a drain; machine() = real (name kept from M5)
    boolean coreless = true;             // M1-M3 test machines: tiles start in RUN with no controller; Config.machine() = the spec's
    int goLatency = 1;                   // C&C's GO store to every tile (balanced net)
    int runLatency = 1;                  // as GO
    int readyLatency = 1;                // the READY lines (1 in simulation; 8 used for estimates)
    boolean cncGoSettle = true;          // C&C sends nothing until GO has reached every tile (go_latency + 1 after the store)
    int cncReaction = 1;                 // cycles from C&C seeing all 64 READY bits to its clear-and-GO store (Java sequencer)
    // ---- simulator ----
    boolean strictReplies = false;       // invariant 8 strict: a mode-6 word overwritten unread is a violation (default: counted)
    boolean counterStoreClearsLatch = true; // spec 4.12 (clarified): the latch persists until a WFI consumes it or a store
                                         // to a counter clears it. false: TEST ONLY, the latch survives counter stores
    boolean checkReadPortOverlap = true; // spec 4.3's rule: an outside packet arriving while read-port results are outstanding
                                         // is a violation (C&C's mode-5 command words exempt); measurements of the tolerance turn it off
    boolean checkCommandRead = true;     // flag a command word overwritten before the core read it
    boolean checkDisjoint = true;        // invariant 2's buffer check (a program property: sweeps may turn it off for speed)
    long shuffleSeed = -1;               // >= 0: child order shuffled every cycle
    int maxViolationMessages = 50;
    boolean checkUnexpected = true;      // invariant 9 (a pure network test with no counters set turns it off)
    // ---- Conditions, the done gate, the error line, C&C's receive FIFO ----
    boolean doneGate = true;             // spec 4.12: from done until GO only GO wakes the core; done clears the latch. false: TEST ONLY,
                                         // to show the wake-in-done check fires and what the gate prevents
    int errorLatency = -1;               // the error line's OR tree to C&C's sticky error bit (6.6; no equal-latency requirement); -1 = ready_latency
    int errorLatency() { return errorLatency >= 0 ? errorLatency : readyLatency; }
    int cncRxDepth = 64;                 // C&C's receive FIFO (6.6: at least 64 entries of 128-bit payload, size and src; item 29); a packet arriving full is lost

    /** The machine as specified: chip reset puts every tile in HALT with its controller on. */
    static Config machine() { Config c = defaults(); c.coreless = false; c.realResponder = true; return c; }

    static Config defaults() {
        Config c = new Config();
        try { c.hops = Hops.load(Path.of("hops.txt")); } catch (java.io.IOException e) { throw new RuntimeException(e); }
        return c;
    }
}
