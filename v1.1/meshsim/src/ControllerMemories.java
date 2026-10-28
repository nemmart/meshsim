/**
 * The controller's instruction and data memories (spec 4.11, v1.1), seen from port B. Each is 8 KB = 2,048 words in
 * four word-interleaved 512 x 32 banks ({@link BankedMemory}): port A (the core) touches one bank per access, and port B
 * writes 1, 2 or 4 consecutive words per cycle at any word address, one per bank.
 *
 * Port B is written only by the traffic director, through a cm_latency pipe (spec 4.8). Decode (packed map, 4.11):
 * mode 6 writes words addr .. addr + n - 1, n = 1, 2 or 4 by the packet's size, the payload's chunk i to word addr + i,
 * each decoded on its own by its low 12 bits (0x000-0x7FF instruction memory, 0x800-0xFFF data memory); mode 7 (a command
 * write, 32 bits) writes command slot addr[3:0], the data word at packed 0xFF0 + slot (4.12); mode 8 (a reply) writes
 * the reply block's word 2 src (packed 0xEF0 + 2 src): a 32-bit reply word 0 only, a 64-bit reply both (4.12).
 * Out-of-range mode-6 words are invariant violations reported by the director (11).
 * A command write to slot 0 sets "command landed" (spec 6.3; TileControl latches it at the end of the cycle).
 *
 * Command check: a command write to slot 0 while the previous command word there has not been loaded by the core since
 * it landed is a violation (a command overwritten before the woken tile read it).
 *
 * Invariant 8 (a statistic by default): a reply or a read-port result marks its data-memory words; a second one before
 * the core loads or stores the word is counted ({@link #overwrittenUnread}, per phase in PhaseLog), and is a violation
 * only with Config.strictReplies. Exchanges that land words the program deliberately ignores are legitimate.
 */
final class ControllerMemories implements Component {
    static final int WORDS = 2048, PORT_A = 0, PORT_B = 1;
    final Config cfg;
    final BankedMemory imem = new BankedMemory("imem");
    final BankedMemory dmem = new BankedMemory("dmem");
    WordSource portB;
    long landedAt = -1;
    long writesCmem, writesCmd, writesReply;      // modes 6, 7, 8
    final long[] cmemBySize = new long[3];        // mode-6 packets by size (32, 64, 128)
    private final boolean[] replyUnread = new boolean[WORDS];
    long violations; String lastViolation;

    ControllerMemories(Config cfg) { this.cfg = cfg; }

    /** Data-memory word indexes of the command block's slot 0 and the reply block's word 0 (4.12). */
    static final int CMD_DWORD = TrafficDirector.COMMAND_WORD - 0x800, REPLY_DWORD = TrafficDirector.REPLY_WORD - 0x800;

    @Override public void runCycle(long t) {
        if (!portB.ready(t)) return;
        long d = portB.peekA(t), h = portB.peekB(t), hi = portB.peekC(t);
        portB.take(t);
        int a = LinkWord.addr(h);
        switch (LinkWord.mode(h)) {
            case LinkWord.M_CMEM -> {
                writesCmem++;
                int size = LinkWord.size(h), n = LinkWord.chunks(size);
                cmemBySize[size]++;
                boolean result = LinkWord.readPortTag(h);
                for (int i = 0; i < n; i++) {
                    int w = (a + i) & 0xFFF;
                    int v = (int) ((i < 2 ? d : hi) >>> (32 * (i & 1)));
                    if (result && w >= 0x800) markReply(w & 0x7FF, d, h);
                    mem(w).write32(PORT_B, t, w & 0x7FF, v);
                }
            }
            case LinkWord.M_CMD -> {
                writesCmd++;
                int slot = a & 0xF;
                dmem.write32(PORT_B, t, CMD_DWORD + slot, (int) d);
                if (slot == 0) {
                    if (slot0Unread && discardNext) discardedCommands++;   // C&C's REPORT replacing a pending command
                    else if (slot0Unread) { unreadCommands++; if (cfg.checkCommandRead) { violations++; lastViolation = "a command landed in command slot 0 before the core read the previous one: " + LinkWord.describe(d, h); } }
                    discardNext = false;
                    slot0Unread = true; landedAt = t;
                }
            }
            case LinkWord.M_REPLY -> {
                writesReply++;
                int w = REPLY_DWORD + 2 * LinkWord.src(h);
                markReply(w, d, h);
                dmem.write32(PORT_B, t, w, (int) d);
                if (LinkWord.size(h) == LinkWord.SIZE_64) { markReply(w + 1, d, h); dmem.write32(PORT_B, t, w + 1, (int) (d >>> 32)); }
            }
            default -> throw new IllegalStateException("port B: mode " + LinkWord.mode(h));
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
    boolean discardNext; long discardedCommands;     // C&C's REPORT discards a pending command by design (CnC.report; simulator only)
    long overwrittenUnread;                 // invariant 8 as a statistic: reply and result words rewritten before the core read them
    void stored(int w) { replyUnread[w] = false; }

    private BankedMemory mem(int packedWord) { return packedWord < 0x800 ? imem : dmem; }

    /** Host access by packed-map word address. */
    int peekPacked(int w) { return mem(w & 0xFFF).peek(w & 0x7FF); }
    void pokePacked(int w, int v) { mem(w & 0xFFF).poke(w & 0x7FF, v); }
}
