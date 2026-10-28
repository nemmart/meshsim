/**
 * The controller's instruction and data memories (spec 4.11), seen from port B. Each is 8 KB = 2,048 words,
 * two word-interleaved 1K x 32 banks; port A (the core) touches one bank per access, port B writes two
 * consecutive words (one per bank) per cycle at any word address. Modelled as one dual-port SRAMMacro per memory
 * (port 0 = A, port 1 = B); the banking constrains nothing the macro allows.
 *
 * Port B is written only by the traffic director, through a cm_latency pipe (spec 4.8). Decode (packed map,
 * proposed as implemented): mode 5 writes words addr and addr + 1, each decoded on its own by its low 12 bits
 * (0x000-0x7FF instruction memory, 0x800-0xFFF data memory; a pair straddling 0x7FF/0x800 writes one word in
 * each); mode 6 writes the payload's low 32 bits to mailbox slot addr[7:0]; mode 7 writes them to data-memory
 * word addr[10:0]. Out-of-range addresses are invariant violations reported by the director (11-13).
 * A mode-6 write to slot 0 sets "command landed" (spec 6.3; TileControl latches it at the end of the cycle).
 *
 * Command check: a mode-6 write to slot 0 while the previous command word there has not been loaded
 * by the core since it landed is a violation (a command overwritten before the woken tile read it).
 *
 * Invariant 8 (3 October: a statistic by default): a mode-7 write (or read-port result) marks its data-memory
 * word; a second one before the core loads or stores that word is counted ({@link #overwrittenUnread}, per phase in
 * PhaseLog), and is a violation only with Config.strictReplies. Exchanges that land words the program deliberately
 * ignores are legitimate.
 */
final class ControllerMemories implements Component {
    static final int WORDS = 2048, PORT_A = 0, PORT_B = 1;
    final Config cfg;
    final SRAMMacro imem = new SRAMMacro("imem", WORDS, 2, false);
    final SRAMMacro dmem = new SRAMMacro("dmem", WORDS, 2, false);
    WordSource portB;
    long landedAt = -1;
    long writesCmem, writesMbox, writesReply;     // modes 5, 6, 7 (were writes4..6)
    private final boolean[] replyUnread = new boolean[WORDS];
    long violations; String lastViolation;

    ControllerMemories(Config cfg) { this.cfg = cfg; }

    @Override public void runCycle(long t) {
        if (!portB.ready(t)) return;
        long d = portB.peekA(t), h = portB.peekB(t);
        portB.take(t);
        int a = LinkWord.addr(h);
        switch (LinkWord.mode(h)) {
            case LinkWord.M_CMEM -> {
                writesCmem++;
                int w0 = a & 0xFFF, w1 = (a + 1) & 0xFFF;
                if ((w0 >>> 11) == (w1 >>> 11) && w1 == w0 + 1) mem(w0).write64(PORT_B, t, w0 & 0x7FF, d);
                else { mem(w0).write32(PORT_B, t, w0 & 0x7FF, (int) d); mem(w1).write32(PORT_B, t, w1 & 0x7FF, (int) (d >>> 32)); }
            }
            case LinkWord.M_MBOX -> {
                writesMbox++;
                int slot = a & 0xFF;
                dmem.write32(PORT_B, t, (cfg.mailboxBaseWord + slot) & 0x7FF, (int) d);
                if (slot == 0) {
                    if (slot0Unread && discardNext) discardedCommands++;   // C&C's REPORT replacing a pending command
                    else if (slot0Unread) { unreadCommands++; if (cfg.checkCommandRead) { violations++; lastViolation = "a command landed in mailbox slot 0 before the core read the previous one: " + LinkWord.describe(d, h); } }
                    discardNext = false;
                    slot0Unread = true; landedAt = t;
                }
            }
            default -> {
                writesReply++;
                int w = a & 0x7FF;
                if (replyUnread[w]) {
                    overwrittenUnread++;
                    if (cfg.strictReplies) { violations++; lastViolation = "mode-7 reply overwrote an unread reply at data word 0x" + Integer.toHexString(w) + " (invariant 8, strict): " + LinkWord.describe(d, h); }
                }
                replyUnread[w] = true;
                dmem.write32(PORT_B, t, w, (int) d);
            }
        }
    }

    /** Port A touched data word w (a load or store by the core): the reply there has been consumed. */
    void loaded(int w) { replyUnread[w] = false; if (w == ((cfg.mailboxBaseWord) & 0x7FF)) slot0Unread = false; }
    private boolean slot0Unread;
    long unreadCommands;
    boolean discardNext; long discardedCommands;     // C&C's REPORT discards a pending command by design (CnC.report; simulator only)
    long overwrittenUnread;                 // invariant 8 as a statistic: mode-7 words rewritten before the core read them
    void stored(int w) { replyUnread[w] = false; }

    private SRAMMacro mem(int packedWord) { return packedWord < 0x800 ? imem : dmem; }

    /** Host access by packed-map word address. */
    int peekPacked(int w) { return mem(w & 0xFFF).peek(w & 0x7FF); }
    void pokePacked(int w, int v) { mem(w & 0xFFF).poke(w & 0x7FF, v); }
}
