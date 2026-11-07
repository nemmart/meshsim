import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.Writer;

/**
 * The retire-trace observer: sim/ref's one permitted change (sim/planning/DECISIONS.md D5; sim/ref/README.md). Off unless
 * the system property meshsim.observe is set; then one tile's controller writes a line per retired instruction and per
 * test-bench event, in the format of sim/tests/TRACE-FORMAT.md. -Dmeshsim.observe=FILE,TILE writes to FILE from the
 * start; any other value (e.g. "on") turns the hooks on with no output until a driver calls {@link #start}.
 * <p>{@link #ON} is read from the property once, at class load. It is not a compile-time constant, so javac keeps the
 * guarded calls; the JIT folds them away when it is false. Nothing here changes the simulation: every hook only reads.
 */
final class Observer {
    static final boolean ON;
    private static PrintWriter out;
    private static int tile = -1;
    private static String pendingHalt;               // a halt raised inside execute (SIM_EXIT) follows its store's retire line
    static {
        String p = System.getProperty("meshsim.observe");
        ON = p != null && !p.isEmpty() && !"false".equalsIgnoreCase(p);
        int k = p == null ? -1 : p.lastIndexOf(',');
        if (ON && k > 0) {
            try { start(new FileWriter(p.substring(0, k)), Integer.parseInt(p.substring(k + 1).trim())); }
            catch (IOException e) { throw new RuntimeException("meshsim.observe: " + e.getMessage(), e); }
            Runtime.getRuntime().addShutdownHook(new Thread(Observer::stop));
        }
    }
    private Observer() { }

    /** Drivers: trace tile t's controller into w from now (any earlier output is closed). */
    static synchronized void start(Writer w, int t) { stop(); out = new PrintWriter(w); tile = t; pendingHalt = null; }
    /** Drivers: write a line of their own (the header, the closing block, an UndefinedBehavior stop). */
    static synchronized void line(String s) { if (out != null) { flushHalt(); out.println(s); } }
    /** Drivers: end the trace (a pending halt is written first). */
    static synchronized void stop() { if (out != null) { flushHalt(); out.flush(); out.close(); out = null; } tile = -1; }

    private static boolean mine(Controller c) { return out != null && c.tile.idx == tile; }
    private static void emit(String s) { flushHalt(); out.println(s); }
    private static void flushHalt() { if (pendingHalt != null) { String h = pendingHalt; pendingHalt = null; out.println(h); } }
    private static String h(int v) { return String.format("%08x", v); }

    /** Controller.execute, an instruction completed at t: a, b are rs1 and rs2 as read; res is rd's value if wr. */
    static synchronized void retire(Controller c, long t, int pc, int ins, int a, int b, boolean wr, int res) {
        if (!mine(c)) return;
        StringBuilder s = new StringBuilder(64).append(t).append(' ').append(h(pc)).append(' ').append(h(ins));
        int op = ins & 0x7F, rd = (ins >>> 7) & 31, f3 = (ins >>> 12) & 7;
        if (wr) s.append(" x").append(rd).append('=').append(h(res));
        if (op == 0x03) {
            int size = 1 << (f3 & 3), ea = a + (ins >> 20);
            s.append(" ld ").append(h(ea)).append(' ').append(size).append(' ').append(h(size == 4 ? res : res & ((1 << 8 * size) - 1)));
        } else if (op == 0x23) {
            int size = 1 << (f3 & 3), ea = a + (((ins >> 25) << 5) | rd);
            s.append(" st ").append(h(ea)).append(' ').append(size).append(' ').append(h(size == 4 ? b : b & ((1 << 8 * size) - 1)));
        }
        out.println(s);                              // then a halt this instruction raised (only a halt ends a core, so
        flushHalt();                                 // none is pending from before)
    }
    /** Controller.trap: taken at t (mepc, mcause, mtval as just set; pc the handler's first). */
    static synchronized void trap(Controller c, long t) {
        if (mine(c)) emit(t + " TRAP mcause=" + h(c.mcause) + " mepc=" + h(c.mepc) + " mtval=" + h(c.mtval) + " pc=" + h(c.pc));
    }
    /** Controller.haltWith: the core stops at t. Raised inside an instruction (SIM_EXIT), it is written after that
     *  instruction's retire line. */
    static synchronized void halt(Controller c, long t, int reason, int at) {
        if (mine(c)) { flushHalt(); pendingHalt = t + " HALT " + reason + " pc=" + h(at); }
    }
    /** Controller.runCycle: a WFI that slept is left at t (cause 1 GO, 2 an event: TileControl.WAKE_*). */
    static synchronized void wake(Controller c, long t, int cause) {
        if (mine(c)) emit(t + " WAKE " + (cause == TileControl.WAKE_GO ? "go" : "event"));
    }
    /** Controller.execute: a REQUEST_SEND store refused at t (the request FIFO full); the core retries at t + 1. */
    static synchronized void refused(Controller c, long t) {
        if (mine(c)) emit(t + " REQ-REFUSED");
    }
    /** TileControl.endCycle at t: GO, and a command landing, applied at the end of t; the controller sees them from t + 1. */
    static synchronized void endCycle(TileControl ctl, long t, boolean go, boolean landed) {
        if (out == null || ctl.idx != tile || !(go || landed)) return;
        if (go) emit((t + 1) + " GO");
        if (landed) emit((t + 1) + " CMD");
    }
}
