/**
 * The 102-bit link word (spec 3.1; 4-bit mode decided 9 October): {@code long payload} (64 bits) and
 * {@code long hdr} whose low 38 bits are src (7) | dst (7) << 7 | addr (20) << 14 | mode (4) << 34. Bits above 38 are
 * never used. Also the packet modes (spec 3.1), the address space (spec 2.1) and the digit (spec 4.16).
 * Modes: 0 invalid, 1 gather request, 2 write at PTR[src], 3 bucket write, 4 augmented write, 5 controller
 * memory, 6 mailbox, 7 reply, 8-15 reserved; 0 and 8-15 are bad modes (dropped by the director, bad-mode sticky bit).
 */
final class LinkWord {
    static final int CNC = 64, MEMSUB = 65, BROADCAST = 126, NONE = 127;
    static final int M_INVALID = 0, M_GATHER = 1, M_SRC = 2, M_BUCKET = 3, M_AUG = 4, M_CMEM = 5, M_MBOX = 6, M_REPLY = 7;
    static final int N_MODES = 16, MODE_MASK = 15;
    static final String[] MODE_NAME = { "invalid", "gather", "write@PTR[src]", "bucket", "augmented", "cmem", "mailbox", "reply",
            "reserved8", "reserved9", "reserved10", "reserved11", "reserved12", "reserved13", "reserved14", "reserved15" };
    static final long HDR_MASK = (1L << 38) - 1;
    /** A bad mode (spec 3.1; 4.8): 0 or 8-15. The director drops it and sets the bad-mode sticky bit. */
    static boolean badMode(int mode) { return mode == M_INVALID || mode > M_REPLY; }

    private LinkWord() {}

    static long hdr(int src, int dst, int mode, int addr) {
        return (src & 0x7F) | ((long) (dst & 0x7F) << 7) | ((long) (addr & 0xFFFFF) << 14) | ((long) (mode & MODE_MASK) << 34);
    }
    static int src(long h)  { return (int) (h & 0x7F); }
    static int dst(long h)  { return (int) ((h >>> 7) & 0x7F); }
    static int addr(long h) { return (int) ((h >>> 14) & 0xFFFFF); }
    static int mode(long h) { return (int) ((h >>> 34) & MODE_MASK); }
    /** The router stamps src (spec 3.1). */
    static long withSrc(long h, int src) { return (h & ~0x7FL) | (src & 0x7F); }

    /** Digit selector (spec 4.16): 0 none, 1..4 = byte d of the key in payload bits 31..0, 1 = least significant. */
    static int digit(long payload, int sel) { return sel == 0 ? 0 : (int) ((payload >>> (8 * (sel - 1))) & 0xFF); }

    static String describe(long payload, long h) {
        return "src=" + src(h) + " dst=" + dst(h) + " mode=" + mode(h) + " addr=0x" + Integer.toHexString(addr(h)) + " payload=0x" + Long.toHexString(payload);
    }
}
