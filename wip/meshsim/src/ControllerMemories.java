/**
 * The controller's program and data memories (spec 4.11, v1.2), seen from port B: 32 KB each, at regions 0x01 and 0x00
 * (2.2), each four word-interleaved banks ({@link BankedMemory}). Port A (the core) touches one bank per access; port B
 * writes one naturally aligned 32-, 64- or 128-bit mode-0 packet per cycle at its byte offset, one word per bank.
 *
 * Port B is written only by the traffic director, through a cm_latency pipe (4.8), and only mode-0 packets to regions
 * 0x00 and 0x01 reach it (the director has dropped bad packets). The offset's high bits are ignored (the access aliases
 * into the memory, 2.2); the director flags an out-of-bounds offset (invariant 19).
 *
 * "Command landed" (4.8, 4.12, D3): a write that covers command slot 0 (data memory 0x00007FC0) sets it when it lands
 * here; TileControl latches it at the end of the cycle ({@link #landedAt}). The controller's own store to slot 0 doesn't.
 *
 * Command check: slot 0 written while the previous command word there has not been loaded by the core since it landed
 * is a violation (a command overwritten before the woken tile read it; 6.3).
 *
 * Invariant 8 (a statistic by default): a write into the reply block or a read-port result marks its data-memory words;
 * a second one before the core loads or stores the word is counted ({@link #overwrittenUnread}, per phase in PhaseLog),
 * and is a violation only with Config.strictReplies.
 */
final class ControllerMemories implements Component {
    static final int WORDS = BankedMemory.WORDS, BYTES = BankedMemory.BYTES, PORT_A = 0, PORT_B = 1;
    /** The command block (16 words at 0x7FC0) and the reply block (256 words at 0x7BC0), byte offsets in data memory (4.12). */
    static final int COMMAND = 0x7FC0, REPLY = 0x7BC0, CMD_DWORD = COMMAND >>> 2, REPLY_DWORD = REPLY >>> 2;
    final Config cfg;
    final BankedMemory imem = new BankedMemory("pmem");
    final BankedMemory dmem = new BankedMemory("dmem");
    WordSource portB;
    long landedAt = -1;
    long writesData, writesProgram, writesCmd, writesReply;      // port-B writes: data memory, program memory; covering slot 0; into the reply block
    final long[] bySize = new long[3];
    private final boolean[] replyUnread = new boolean[WORDS];
    long violations; String lastViolation;

    ControllerMemories(Config cfg) { this.cfg = cfg; }

    static boolean inCommand(int dataByte) { return dataByte >= COMMAND && dataByte < COMMAND + 64; }
    static boolean inReply(int dataByte) { return dataByte >= REPLY && dataByte < REPLY + 1024; }

    @Override public void runCycle(long t) {
        if (!portB.ready(t)) return;
        long d = portB.peekA(t), h = portB.peekB(t), hi = portB.peekC(t);
        portB.take(t);
        int a = LinkWord.addr(h), size = LinkWord.size(h), n = LinkWord.chunks(size);
        boolean data = LinkWord.region(a) == LinkWord.R_DMEM;
        BankedMemory m = data ? dmem : imem;
        int w0 = (LinkWord.offset(a) & (BYTES - 1)) >>> 2;
        bySize[size & 3]++;
        if (data) writesData++; else writesProgram++;
        boolean result = LinkWord.readPortTag(h);
        for (int i = 0; i < n; i++) {
            int w = (w0 + i) & (WORDS - 1);
            int v = (int) ((i < 2 ? d : hi) >>> (32 * (i & 1)));
            if (data && (result || inReply(w << 2))) markReply(w, d, h);
            m.write32(PORT_B, t, w, v);
        }
        if (data && inReply(w0 << 2)) writesReply++;
        if (data && w0 <= CMD_DWORD && w0 + n > CMD_DWORD) {          // covers slot 0: command landed (4.8)
            writesCmd++;
            if (slot0Unread && discardNext) discardedCommands++;
            else if (slot0Unread) { unreadCommands++; if (cfg.checkCommandRead) { violations++; lastViolation = "a command landed in command slot 0 before the core read the previous one (6.3): " + LinkWord.describe(d, h, hi); } }
            discardNext = false;
            slot0Unread = true; landedAt = t;
        }
    }
    private void markReply(int w, long d, long h) {
        if (replyUnread[w]) {
            overwrittenUnread++;
            if (cfg.strictReplies) { violations++; lastViolation = "a reply or read-port result overwrote an unread one at data word 0x" + Integer.toHexString(w) + " (invariant 8, strict): " + LinkWord.describe(d, h); }
        }
        replyUnread[w] = true;
    }

    /** Port A touched data word w (a load or store by the core): the reply there has been consumed. */
    void loaded(int w) { replyUnread[w] = false; if (w == CMD_DWORD) slot0Unread = false; }
    private boolean slot0Unread;
    long unreadCommands;
    boolean discardNext; long discardedCommands;     // a harness may discard a pending command by design (simulator only)
    long overwrittenUnread;                 // invariant 8 as a statistic: reply and result words rewritten before the core read them
    void stored(int w) { replyUnread[w] = false; }

    /** Host access by 32-bit byte address (regions 0x00 and 0x01; the high offset bits ignored). */
    int peek(int addr) { return (LinkWord.region(addr) == LinkWord.R_DMEM ? dmem : imem).peek((LinkWord.offset(addr) & (BYTES - 1)) >>> 2); }
    void poke(int addr, int v) { (LinkWord.region(addr) == LinkWord.R_DMEM ? dmem : imem).poke((LinkWord.offset(addr) & (BYTES - 1)) >>> 2, v); }
}
