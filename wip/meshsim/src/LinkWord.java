/**
 * The 169-bit link word (spec 3.1, v1.1): src 7, dst 7, mode 4, addr 21, size 2, payload 128. meshsim carries it as
 * three longs on every bus: {@code a} = payload [63:0], {@code b} = the header, {@code c} = payload [127:64].
 * The header's low 41 bits are src (7) | dst (7) << 7 | addr (21) << 14 | mode (4) << 35 | size (2) << 39. Bit 63 is
 * {@link #TAG_READPORT}, a simulator-only mark on a read-port return (not a link-word bit; it changes no routing or
 * landing decision). Bits 41-62 are never used.
 *
 * Size (3.1): 00 = 32 bits, 01 = 64, 10 = 128, so an element is 1 << size chunks; 11 is reserved and its behaviour
 * undefined (no hardware check; meshsim flags it). A 32-bit payload is in bits [31:0] and a 64-bit one in [63:0]; the
 * bits above the size are 0.
 *
 * Modes (3.1, renumbered 2026-10-26): 0 invalid, 1 gather request, 2 big-SRAM write by source (PTR[src]), 3 big-SRAM
 * write at an explicit address (aligned to its size), 4 bucket write, 5 augmented write, 6 controller-memory write,
 * 7 command write (32 bits, command slot addr[3:0]), 8 reply (32 or 64 bits, landing by source), 9 peripheral write,
 * 10-15 reserved. Groups: big SRAM 2-5, controller memory 6-8, the peripheral 9. A bad packet is mode 0 or 10-15,
 * mode 5 at 128 bits, a mode-3 packet whose addr isn't aligned to its size, mode 7 above 32 bits, or mode 8 at 128
 * bits: the director drops it, uncounted, and sets the bad-mode-or-size condition (4.8, 4.13).
 * Also the address space (2.1) and the digit (4.15).
 */
final class LinkWord {
    static final int CNC = 64, MEMSUB = 65, BROADCAST = 126, NONE = 127;
    static final int M_INVALID = 0, M_GATHER = 1, M_SRC = 2, M_EXPLICIT = 3, M_BUCKET = 4, M_AUG = 5, M_CMEM = 6, M_CMD = 7,
            M_REPLY = 8, M_PERIPH = 9;
    static final int N_MODES = 16, MODE_MASK = 15;
    static final String[] MODE_NAME = { "invalid", "gather", "write@PTR[src]", "write@addr", "bucket", "augmented", "cmem", "command",
            "reply", "peripheral", "reserved10", "reserved11", "reserved12", "reserved13", "reserved14", "reserved15" };
    /** Sizes (3.1): the 2-bit code; an element is 1 << size chunks. */
    static final int SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2, SIZE_UNDEF = 3;
    static final String[] SIZE_NAME = { "32", "64", "128", "11(undefined)" };
    static final long HDR_MASK = (1L << 41) - 1;
    /** Addresses are 21 bits (3.1, 4.1): chunk addresses wrap mod 2^21. */
    static final int ADDR_MASK = 0x1FFFFF;
    /** Simulator-only: header bit 63 marks a read-port return (the injection point frees its entry; the director accounts it). */
    static final long TAG_READPORT = 1L << 63;
    /** Simulator-only: header bit 62 marks a read return from the SRAM read queue, its token in bits [46:41]; bit 61 a
     *  packet holding a loopback token (4.5) until it leaves the loopback FIFO. Neither changes any routing or landing. */
    static final long TAG_FROM_SRQ = 1L << 62, TAG_TOKEN = 1L << 61;

    /** A bad mode (3.1, 4.8): 0 or 10-15. */
    static boolean badMode(int mode) { return mode == M_INVALID || mode > M_PERIPH; }
    /** A bad packet (3.1): a bad mode, mode 5 at 128 bits, a misaligned mode 3 (64 bits at an odd addr, 128 bits at an
     *  addr not a multiple of 4), mode 7 above 32 bits or mode 8 at 128 bits. Size 11 is undefined, not bad. */
    static boolean badPacket(int mode, int size, int addr) {
        if (badMode(mode)) return true;
        return switch (mode) {
            case M_AUG, M_REPLY -> size == SIZE_128;
            case M_CMD -> size == SIZE_64 || size == SIZE_128;
            case M_EXPLICIT -> size != SIZE_UNDEF && (addr & ((1 << size) - 1)) != 0;
            default -> false;
        };
    }
    static boolean badPacket(long h) { return badPacket(mode(h), size(h), addr(h)); }
    /** Chunks of a payload of this size (1, 2, 4; 8 for the undefined code). */
    static int chunks(int size) { return 1 << size; }

    private LinkWord() {}

    static long hdr(int src, int dst, int mode, int addr, int size) {
        return (src & 0x7F) | ((long) (dst & 0x7F) << 7) | ((long) (addr & ADDR_MASK) << 14) | ((long) (mode & MODE_MASK) << 35)
                | ((long) (size & 3) << 39);
    }
    static int src(long h)  { return (int) (h & 0x7F); }
    static int dst(long h)  { return (int) ((h >>> 7) & 0x7F); }
    static int addr(long h) { return (int) ((h >>> 14) & ADDR_MASK); }
    static int mode(long h) { return (int) ((h >>> 35) & MODE_MASK); }
    static int size(long h) { return (int) ((h >>> 39) & 3); }
    static boolean readPortTag(long h) { return (h & TAG_READPORT) != 0; }
    /** The router stamps src (spec 3.1). */
    static long withSrc(long h, int src) { return (h & ~0x7FL) | (src & 0x7F); }

    /** The payload's bits above its size cleared (3.1: they are 0): {lo, hi}. */
    static long maskLo(long lo, int size) { return size == SIZE_32 ? lo & 0xFFFFFFFFL : lo; }
    static long maskHi(long hi, int size) { return size == SIZE_128 || size == SIZE_UNDEF ? hi : 0; }

    /** Digit selector (spec 4.15): 0 none, 1..4 = byte d of the key in payload bits 31..0, 1 = least significant. */
    static int digit(long payload, int sel) { return sel == 0 ? 0 : (int) ((payload >>> (8 * (sel - 1))) & 0xFF); }

    static String describe(long payload, long h) { return describe(payload, h, 0); }
    static String describe(long payload, long h, long hi) {
        int s = size(h);
        String p = s == SIZE_32 ? String.format("0x%08x", payload & 0xFFFFFFFFL) : s == SIZE_64 ? String.format("0x%016x", payload)
                : String.format("0x%016x_%016x", hi, payload);
        return "src=" + src(h) + " dst=" + dst(h) + " mode=" + mode(h) + " addr=0x" + Integer.toHexString(addr(h)) + " size=" + SIZE_NAME[s]
                + " payload=" + p;
    }
}
