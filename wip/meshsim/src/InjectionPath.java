/**
 * The injection point (spec 4.5): at most one packet per cycle onto the router's loopback route block, from
 * (1) the SRAM read queue's returns, then (2) the controller's send port, whose packet waits in its holding register
 * (priority proposed, as implemented). Each supplies dst, mode, addr and payload; the router adds src.
 *
 * A read-port return (tagged {@link LinkWord#TAG_READPORT} by the SRAM read queue, simulator-only) frees its read-port entry when
 * it is injected (spec 4.3); the tag travels with the packet to the director.
 *
 * Checks (spec 9): invariant 5 (no bad packet, 3.1, and no size 11, is sent), invariant 10 (a mode-1 packet goes only to the
 * tile itself) and 2.1 (no packet for 65-125, which have no endpoint; none for 127). A violating packet is still injected, as the hardware would.
 */
final class InjectionPath implements Component {
    final int me;
    WordSource readReturn;     // the SRAM read queue's returns as packets; a test-fed holding register in M1
    WordSource send;           // the controller's send holding register (ROUTER_SEND_32, _64, _128)
    WordSink toRouter;         // delay line into the router's loopback route block
    ReadPort readPort;         // told when a read-port return is injected

    long injected, fromRead, fromSend, sendWaitCycles, tokenWaitCycles;
    long violations; String lastViolation;

    InjectionPath(int me) { this.me = me; }

    SRAMReadQueue srq;         // the token pool (4.5)

    @Override public void runCycle(long t) {
        boolean r = readReturn.ready(t), s = send.ready(t);
        if (!r && !s) return;
        if (r && s) sendWaitCycles++;
        long h;
        WordSource src;
        if (r) { src = readReturn; h = src.peekB(t); }
        else {
            src = send; h = src.peekB(t);
            int sd = LinkWord.dst(h);
            if (srq != null && srq.tokensOn && srq.loops(sd)) {                    // a send-port packet for the tile itself or a broadcast waits for a token (4.5)
                if (!srq.takeForSend(t)) { tokenWaitCycles++; return; }
                h |= LinkWord.TAG_TOKEN;
            }
        }
        long d = src.peekA(t), c = src.peekC(t);
        src.take(t);
        if (src == readReturn && (h & LinkWord.TAG_FROM_SRQ) != 0 && srq != null) srq.injected(t, h);
        if (LinkWord.readPortTag(h) && src == readReturn && readPort != null) readPort.injectedAt(t);
        int mode = LinkWord.mode(h), dst = LinkWord.dst(h);
        if (LinkWord.badPacket(h)) violate("bad packet sent (invariant 5: mode 0 or 10-15, mode 5 at 128, a misaligned mode 3, mode 7 above 32 or mode 8 at 128 bits): " + LinkWord.describe(d, h, c));
        if (LinkWord.size(h) == LinkWord.SIZE_UNDEF) violate("packet with size 11, undefined, sent (3.1, invariant 5): " + LinkWord.describe(d, h, c));
        if (dst > LinkWord.CNC && dst < LinkWord.BROADCAST || dst == LinkWord.NONE) violate("a packet sent to dst " + dst + ", which has no endpoint (2.1: only 64 exists of 64-125; 127 is not a destination): " + LinkWord.describe(d, h, c));
        if (mode == LinkWord.M_GATHER && dst != me) violate("mode-1 packet to another tile (invariant 10): " + LinkWord.describe(d, h));
        toRouter.put(t, d, h, c);
        injected++;
        if (src == readReturn) fromRead++; else fromSend++;
    }

    private void violate(String m) { violations++; lastViolation = m; }
    void resetStats() { injected = fromRead = fromSend = sendWaitCycles = 0; }
}
