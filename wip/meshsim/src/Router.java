
/**
 * Router (design v2 spec 4.6, 4.7, 4.17; from meshsim v3.10, widened to the 101-bit packet): the route blocks
 * (four link inputs, the loopback input from the injection point, and on tile 0 a sixth, "from C&C"), the output
 * queue sets (one overflow FIFO of queue_depth packets per feeder) with round-robin selectors, and the link
 * outputs (delay lines owned by Chip).
 *
 * The injection point (InjectionPath) puts at most one packet per cycle on {@link #fromInjection} (dst, mode,
 * addr, payload; this router stamps src = me). The eject selector puts at most one packet per cycle on
 * {@link #toDirector}, the whole header kept: the traffic director switches on its mode.
 *
 * Tile 0 only (spec 4.17): input "from C&C" is a route block routed as an injection at tile 0; every one of
 * tile 0's five outputs gains a "from C&C" feeder FIFO (appended as the last slot), and a sixth output "to C&C"
 * has FIFOs from the loopback (tile 0's own packets), left, right, up and down with its own round-robin
 * selector. Every packet for dst 64 or 65 reaching tile 0 goes to "to C&C". Memory subsystem (65), as v3.10's
 * proposal (spec 7 open): it shares the port -- the from-C&C input takes C&C's packet, else the memory
 * subsystem's; after the to-C&C selector, dst 64 goes to C&C and dst 65 to the memory subsystem.
 *
 * Every bus here carries the 168-bit link word as three longs (payload [63:0], header, payload [127:64]); the header's
 * simulator-only bit 63 ({@link LinkWord#TAG_READPORT}) marks a read-port return and changes no routing decision.
 * (The eject hold for read-port results, M6-M9, is gone: results now arrive as packets through the loopback.)
 */
final class Router implements Component {
    static final int FROM_LEFT = 0, FROM_RIGHT = 1, FROM_UP = 2, FROM_DOWN = 3, LOOPBACK = 4, FROM_CNC = 5;
    static final int TO_RIGHT = 0, TO_LEFT = 1, TO_DOWN = 2, TO_UP = 3, EJECT = 4, TO_CNC = 5;
    static final int RIGHT = 1, LEFT = 2, DOWN = 4, UP = 8, EJ = 16, CNC = 32;
    static final String[] OUT_NAME = { "to-right", "to-left", "to-down", "to-up", "eject", "to-cnc" };
    static final String[] PORT_NAME = { "from-left", "from-right", "from-up", "from-down", "loopback", "from-cnc" };
    private static final int[][] FEEDERS_TILE = {
        { LOOPBACK, FROM_LEFT }, { LOOPBACK, FROM_RIGHT },
        { LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP }, { LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_DOWN },
        { LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_DOWN } };
    private static final int[][] FEEDERS_TILE0 = {
        { LOOPBACK, FROM_LEFT, FROM_CNC }, { LOOPBACK, FROM_RIGHT, FROM_CNC },
        { LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_CNC }, { LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_DOWN, FROM_CNC },
        { LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_DOWN, FROM_CNC },
        { LOOPBACK, FROM_LEFT, FROM_RIGHT, FROM_UP, FROM_DOWN } };
    /** The feeders of each output: every tile's router has the fifth port (4.16, one tile design); it is connected only
     *  where something is attached (tile 0's, to C&C), and an unconnected port never receives and is never chosen. */
    static int[][] feeders(int me) { return FEEDERS_TILE0; }

    final int me;
    final Hops hops;
    final int[][] feeders;
    final int nOut, nPort;
    final WordFIFO[][] q;
    private final int[][] slotOf;
    private final int[] rr, maybe;              // round-robin pointer; hint: feeder FIFOs that may hold a word (truth is ready(t))
    private final byte[] uniTable = new byte[128 * 128];   // route decision is a pure function of (me, src, dst): tabulated
    private final byte[] bcTable = new byte[6 * 128];

    // ---- bus ends (wired by Tile / Chip) ----
    final WordSource[] linkIn = new WordSource[4];    // delay lines, indexed by arrival port
    final WordSink[] linkOut = new WordSink[4];       // delay lines, indexed by output direction
    WordSource fromInjection;                         // injection point -> loopback route block (src stamped here)
    WordSink toDirector;                              // eject selector -> traffic director
    WordSource fromCnc, fromMemsub;                   // tile 0: holding registers into the from-C&C route block (C&C first)
    WordSink toCnc, toMemsub;                         // tile 0: after the to-C&C selector, by dst

    long injected, routed, ejected, ejectErrors, routeErrors, fromCncWords, toCncWords;

    final boolean attached;                     // the fifth port is connected (tile 0: C&C)
    final int loopSlot;                         // the eject output's loopback FIFO
    SRAMReadQueue tokens;                       // freed as a token-holding packet leaves the loopback FIFO (4.5)
    Router(int me, Hops hops, int depth) { this(me, hops, depth, depth, me == 0); }
    Router(int me, Hops hops, int depth, int loopDepth, boolean attached) {
        this.me = me; this.hops = hops; this.attached = attached;
        feeders = feeders(me);
        nOut = feeders.length; nPort = 6;
        q = new WordFIFO[nOut][];
        slotOf = new int[nOut][6];
        rr = new int[nOut]; maybe = new int[nOut];
        for (int o = 0; o < nOut; o++) {
            java.util.Arrays.fill(slotOf[o], -1);
            q[o] = new WordFIFO[feeders[o].length];
            for (int f = 0; f < feeders[o].length; f++) {
                q[o][f] = new WordFIFO("t" + me + "." + OUT_NAME[o] + "<" + PORT_NAME[feeders[o][f]], o == EJECT && feeders[o][f] == LOOPBACK ? loopDepth : depth);
                slotOf[o][feeders[o][f]] = f;
            }
            rr[o] = feeders[o].length - 1;
        }
        loopSlot = slotOf[EJECT][LOOPBACK];
        for (int src = 0; src < 128; src++) {
            for (int dst = 0; dst < 128; dst++) uniTable[src * 128 + dst] = (byte) unicast(src, dst);
            for (int port = 0; port < 6; port++) bcTable[port * 128 + src] = (byte) broadcast(port, src);
        }
    }
    /** Feeder slot of an input port at an output, -1 if none. */
    int slotOfPort(int o, int port) { return slotOf[o][port]; }

    @Override public void runCycle(long t) {
        // selectors: one word per output
        for (int o = 0; o < 4; o++) if (select(o, t)) linkOut[o].put(t, selData, selAddr, selTag);
        if (selectEject(t)) {
            int dst = LinkWord.dst(selAddr);
            if (selSlot == loopSlot && (selAddr & LinkWord.TAG_TOKEN) != 0 && tokens != null) tokens.loopbackLeft(t);
            ejected++;
            if (dst == me || dst == LinkWord.BROADCAST) toDirector.put(t, selData, selAddr, selTag);
            else ejectErrors++;
        }
        if (attached && select(TO_CNC, t)) {
            toCncWords++;
            (LinkWord.dst(selAddr) == LinkWord.MEMSUB ? toMemsub : toCnc).put(t, selData, selAddr, selTag);
        }
        // route blocks: link inputs
        for (int p = 0; p < 4; p++) {
            WordSource in = linkIn[p];
            if (in.ready(t)) { long d = in.peekA(t), a = in.peekB(t), c = in.peekC(t); in.take(t); route(t, p, d, a, c); }
        }
        // loopback route block: the injection point's one packet per cycle, source stamped here
        if (fromInjection.ready(t)) {
            long d = fromInjection.peekA(t), a = fromInjection.peekB(t), c = fromInjection.peekC(t);
            fromInjection.take(t);
            injected++;
            route(t, LOOPBACK, d, LinkWord.withSrc(a, me), c);
        }
        // tile 0: the from-C&C route block (C&C's holding register first, then the memory subsystem's)
        if (fromCnc != null) {
            WordSource src = fromCnc.ready(t) ? fromCnc : fromMemsub != null && fromMemsub.ready(t) ? fromMemsub : null;
            if (src != null) { long d = src.peekA(t), a = src.peekB(t), c = src.peekC(t); src.take(t); fromCncWords++; route(t, FROM_CNC, d, a, c); }
        }
    }

    private long selData, selAddr, selTag;
    private int selSlot;
    /** The eject selector (4.7): the loopback FIFO only in a cycle when every other eject FIFO is empty; the others
     *  round-robin among themselves. */
    boolean loopbackLast = true;
    private boolean selectEject(long t) {
        int m = maybe[EJECT];
        if (m == 0) return false;
        if (!loopbackLast) { boolean r = select(EJECT, t, m); if (r && selSlot == loopSlot) rr[EJECT] = loopSlot; return r; }
        if (select(EJECT, t, m & ~(1 << loopSlot))) return true;
        return (m & (1 << loopSlot)) != 0 && select(EJECT, t, 1 << loopSlot);
    }
    private boolean select(int o, long t) { return select(o, t, maybe[o]); }
    private boolean select(int o, long t, int m) {
        if (m == 0) return false;
        WordFIFO[] fs = q[o];
        int n = fs.length, f = rr[o];
        for (int i = 0; i < n; i++) {
            f = f + 1 == n ? 0 : f + 1;
            if ((m & (1 << f)) == 0) continue;
            WordFIFO q = fs[f];
            if (q.ready(t)) {
                selData = q.peekA(t); selAddr = q.peekB(t); selTag = q.peekC(t); q.take(t); selSlot = f; if (o != EJECT || f != loopSlot) rr[o] = f;
                if (q.isEmpty(t)) maybe[o] &= ~(1 << f);
                return true;
            }
            maybe[o] &= ~(1 << f);                  // hint was stale (dropped put)
        }
        return false;
    }

    private void route(long t, int port, long d, long a, long c) {
        routed++;
        int src = LinkWord.src(a), dst = LinkWord.dst(a);
        int mask = dst == LinkWord.BROADCAST ? bcTable[port * 128 + src] : uniTable[src * 128 + dst];
        if ((mask & RIGHT) != 0) push(t, TO_RIGHT, port, d, a, c);
        if ((mask & LEFT) != 0) push(t, TO_LEFT, port, d, a, c);
        if ((mask & DOWN) != 0) push(t, TO_DOWN, port, d, a, c);
        if ((mask & UP) != 0) push(t, TO_UP, port, d, a, c);
        if ((mask & EJ) != 0) push(t, EJECT, port, d, a, c);
        if ((mask & CNC) != 0) push(t, TO_CNC, port, d, a, c);
    }

    private void push(long t, int o, int port, long d, long a, long c) {
        int f = o < nOut ? slotOf[o][port] : -1;
        if (f < 0) { routeErrors++; return; }
        q[o][f].put(t, d, a, c);
        maybe[o] |= 1 << f;
    }

    int unicast(int src, int dst) {
        boolean toCncPort = dst == LinkWord.CNC || dst == LinkWord.MEMSUB;
        int s = src >= 64 ? 0 : src;
        int d = toCncPort ? 0 : dst;
        int k = (d - s) & 63;
        int b = hops.b[k], a = hops.a[k];
        int cs = s & 7, cm = me & 7;
        int hb = b >= 0 ? (cm - cs) & 7 : -((cs - cm) & 7);
        if (hb != b) return b > 0 ? RIGHT : LEFT;
        if (me != d) return a > 0 ? DOWN : UP;
        return toCncPort ? CNC : EJ;               // tile 0: words for C&C / the memory subsystem leave through to-C&C (spec 4.17)
    }

    int broadcast(int port, int src) {
        int s = src >= 64 ? 0 : src;
        int cs = s & 7, cm = me & 7, rs = s >> 3, rm = me >> 3;
        switch (port) {
            case LOOPBACK: case FROM_CNC: return EJ | RIGHT | LEFT | DOWN | UP;     // a broadcast rooted here (C&C's: at tile 0); never delivered to C&C
            case FROM_LEFT: { int m = EJ | DOWN | UP; if (((cm - cs) & 7) != 4) m |= RIGHT; return m; }
            case FROM_RIGHT: { int m = EJ | DOWN | UP; if (((cs - cm) & 7) != 3) m |= LEFT; return m; }
            case FROM_UP: { int m = EJ; if (((rm - rs) & 7) != 4) m |= DOWN; return m; }
            default: { int m = EJ; if (((rs - rm) & 7) != 3) m |= UP; return m; }
        }
    }

    void resetStats(long t) {
        for (WordFIFO[] fs : q) for (WordFIFO f : fs) f.resetStats(t);
        injected = routed = ejected = ejectErrors = routeErrors = fromCncWords = toCncWords = 0;
    }
}
