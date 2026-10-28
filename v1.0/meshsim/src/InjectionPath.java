/**
 * The injection point (spec 4.5): at most one packet per cycle onto the router's loopback route block, from
 * (1) the SRAM read queue's returns, then (2) the controller's send port, whose packet waits in its holding register
 * (priority proposed, as implemented). Each supplies dst, mode, addr and payload; the router adds src.
 *
 * A read-port return (tagged {@link ReadPort#TAG} by the SRAM read queue, simulator-only) frees its read-port entry when
 * it is injected (spec 4.3); the tag travels with the packet to the director.
 *
 * Checks (spec 9): invariant 5 (no packet with a bad mode; 0 or 8-15; is sent) and invariant 10 (a mode-1 packet goes only to the
 * tile itself). A violating packet is still injected, as the hardware would.
 */
final class InjectionPath implements Component {
    final int me;
    WordSource readReturn;     // the SRAM read queue's returns as packets; a test-fed holding register in M1
    WordSource send;           // the controller's send holding register (ROUTER_SEND_LO)
    WordSink toRouter;         // delay line into the router's loopback route block
    ReadPort readPort;         // told when a read-port return is injected

    long injected, fromRead, fromSend, sendWaitCycles;
    long violations; String lastViolation;

    InjectionPath(int me) { this.me = me; }

    @Override public void runCycle(long t) {
        boolean r = readReturn.ready(t), s = send.ready(t);
        WordSource src = r ? readReturn : s ? send : null;
        if (src == null) return;
        if (r && s) sendWaitCycles++;
        long d = src.peekA(t), h = src.peekB(t), c = src.peekC(t);
        src.take(t);
        if (c == ReadPort.TAG && src == readReturn && readPort != null) readPort.injectedAt(t);
        int mode = LinkWord.mode(h), dst = LinkWord.dst(h);
        if (LinkWord.badMode(mode)) violate("mode-" + mode + " packet sent (invariant 5): " + LinkWord.describe(d, h));
        if (mode == LinkWord.M_GATHER && dst != me) violate("mode-1 packet to another tile (invariant 10): " + LinkWord.describe(d, h));
        toRouter.put(t, d, h, c);
        injected++;
        if (src == readReturn) fromRead++; else fromSend++;
    }

    private void violate(String m) { violations++; lastViolation = m; }
    void resetStats() { injected = fromRead = fromSend = sendWaitCycles = 0; }
}
