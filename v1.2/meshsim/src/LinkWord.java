/**
 * The 256-bit link word (spec 3.1, v1.2): payload [127:0], address [159:128] (32-bit byte address: region [31:24],
 * offset [23:0]), size [161:160], counter [164:162], mode [167:165], src [174:168], dst [181:175]; [255:182] reserved,
 * sent as 0 and ignored. meshsim carries it as three longs on every bus: {@code a} = payload [63:0], {@code b} = the
 * header, {@code c} = payload [127:64]. The reserved bits are not carried: they are always 0, and no receiver reads them
 * (the hardware stores them in full, item 117; that costs area, not behaviour).
 *
 * The header long (simulator layout): src [6:0], dst [13:7], mode [16:14], counter [19:17], size [21:20], address
 * [53:22]; simulator-only bits: the SRAM read queue's token [59:54] with {@link #TAG_FROM_SRQ} (bit 62), a held
 * loopback token {@link #TAG_TOKEN} (bit 61) and a read-port return {@link #TAG_READPORT} (bit 63). Bit 60 is spare. None
 * of the simulator-only bits changes a routing, landing or counting decision.
 *
 * Size (3.1): 00 = 32 bits, 01 = 64, 10 = 128, so 1 << size chunks; 11 is undefined (meshsim errors). A 32-bit payload is
 * in [31:0], a 64-bit one in [63:0]; the bits above the size are 0.
 *
 * Modes (3.1, D6, D43): 0 write at address, 1 big-SRAM write by source (PTR[src]), 2 bucket write (PTR[digit]),
 * 3 augmented write (PTR[src]), 4 gather request (local only, never counted), 5 count only; 6-7 reserved (discarded
 * silently at the destination, no count, no error; meshsim flags them). There is no invalid code.
 *
 * Counter (3.1, 4.12): 0 uncounted, 1-7 WRITE_COUNTER_1-7 at the receiver, decremented K cycles after its director
 * accepts the packet (modes 0-3 and 5).
 *
 * Bad packets (3.1, D26; the hardware's cheap checks, done by the receiving director): a mode-0 packet not naturally
 * aligned to its size, a mode-0 packet to region 0x02, 0x07-0xFE, 0xFF or an absent peripheral's region, and a 128-bit
 * mode 3. Modes 1-5 ignore the address, region included. Also the address space (2.1) and the digit (4.15).
 */
final class LinkWord {
    /** Endpoints (2.1): 64-66 the controllers C&C 1-3, 126 the broadcast, 127 none. */
    static final int CC1 = 64, CC2 = 65, CC3 = 66, BROADCAST = 126, NONE = 127;
    static final int CNC = CC1;                                      // v1.1's name for endpoint 64
    static boolean controller(int id) { return id >= CC1 && id <= CC3; }
    /** The tile a controller is attached to (4.16): C&C n at tile n - 1; a tile is itself. */
    static int tileOf(int id) { return controller(id) ? id - CC1 : id; }

    static final int M_WRITE = 0, M_SRC = 1, M_BUCKET = 2, M_AUG = 3, M_GATHER = 4, M_COUNT = 5, M_RSVD6 = 6, M_RSVD7 = 7;
    static final int N_MODES = 8, MODE_MASK = 7;
    static final String[] MODE_NAME = { "write@addr", "write@PTR[src]", "bucket", "augmented", "gather", "count-only", "reserved6", "reserved7" };
    static boolean reservedMode(int m) { return m >= M_RSVD6; }
    /** Modes the counter field counts (3.1): 0-3 and 5. */
    static boolean countedMode(int m) { return m <= M_AUG || m == M_COUNT; }

    /** Sizes (3.1): the 2-bit code; an element is 1 << size chunks. */
    static final int SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2, SIZE_UNDEF = 3;
    static final String[] SIZE_NAME = { "32", "64", "128", "11(undefined)" };
    static final int N_COUNTERS = 7;                                 // WRITE_COUNTER_1-7; counter field 0 is uncounted

    /** Regions (2.2): the top byte of a 32-bit address. */
    static final int R_DMEM = 0x00, R_PMEM = 0x01, R_BOOTROM = 0x02, R_SRAM = 0x03, R_PERIPH1 = 0x04, R_PERIPH3 = 0x06, R_REGS = 0xFF;
    static int region(int addr) { return addr >>> 24; }
    static int offset(int addr) { return addr & 0xFFFFFF; }
    /** The packet address of big-SRAM chunk c (1, Addresses): 0x03000000 | c << 2. */
    static int sramAddr(int chunk) { return R_SRAM << 24 | (chunk & 0x1FFFFF) << 2; }

    static final long SRC_MASK = 0x7F;
    /** Simulator-only: bit 63 a read-port return; bit 62 a read return from the SRAM read queue, its token in [59:54];
     *  bit 61 a packet holding a loopback token (4.5) until it leaves the loopback FIFO. */
    static final long TAG_READPORT = 1L << 63, TAG_FROM_SRQ = 1L << 62, TAG_TOKEN = 1L << 61;
    static final int TOKEN_SHIFT = 54;
    static final long HDR_MASK = (1L << 54) - 1;
    static int token(long h) { return (int) ((h >>> TOKEN_SHIFT) & 0x3F); }

    private LinkWord() {}

    static long hdr(int src, int dst, int mode, int counter, int addr, int size) {
        return (src & 0x7F) | (long) (dst & 0x7F) << 7 | (long) (mode & 7) << 14 | (long) (counter & 7) << 17
                | (long) (size & 3) << 20 | (addr & 0xFFFFFFFFL) << 22;
    }
    static int src(long h)     { return (int) (h & 0x7F); }
    static int dst(long h)     { return (int) ((h >>> 7) & 0x7F); }
    static int mode(long h)    { return (int) ((h >>> 14) & 7); }
    static int counter(long h) { return (int) ((h >>> 17) & 7); }
    static int size(long h)    { return (int) ((h >>> 20) & 3); }
    static int addr(long h)    { return (int) ((h >>> 22) & 0xFFFFFFFFL); }
    static boolean readPortTag(long h) { return (h & TAG_READPORT) != 0; }
    /** The router stamps src (spec 3.1). */
    static long withSrc(long h, int src) { return (h & ~SRC_MASK) | (src & 0x7F); }
    static long withDst(long h, int dst) { return (h & ~(0x7FL << 7)) | (long) (dst & 0x7F) << 7; }

    /** Chunks of a payload of this size (1, 2, 4; 8 for the undefined code). */
    static int chunks(int size) { return 1 << size; }
    /** Bytes of a payload of this size (4, 8, 16). */
    static int bytes(int size) { return 4 << size; }
    /** A byte address naturally aligned to a size (2.2). */
    static boolean aligned(int addr, int size) { return size == SIZE_UNDEF || (addr & (bytes(size) - 1)) == 0; }
    /** A chunk address naturally aligned to a size (4.1). */
    static boolean chunkAligned(int chunk, int size) { return size == SIZE_UNDEF || (chunk & (chunks(size) - 1)) == 0; }

    /** Bad packet (3.1): {@code periphPresent} is the receiver's PARAMS [2:0]. Size 11 is undefined, not bad. */
    static boolean badPacket(int mode, int size, int addr, int periphPresent) {
        if (mode == M_AUG) return size == SIZE_128;
        if (mode != M_WRITE) return false;
        if (!aligned(addr, size)) return true;
        int r = region(addr);
        if (r == R_DMEM || r == R_PMEM || r == R_SRAM) return false;
        if (r >= R_PERIPH1 && r <= R_PERIPH3) return (periphPresent >> (r - R_PERIPH1) & 1) == 0;
        return true;                                                  // 0x02, 0x07-0xFE, 0xFF
    }
    static boolean badPacket(long h, int periphPresent) { return badPacket(mode(h), size(h), addr(h), periphPresent); }
    /** Why a packet is bad (for messages), or null. */
    static String whyBad(long h, int periphPresent) {
        int m = mode(h), s = size(h), a = addr(h);
        if (m == M_AUG && s == SIZE_128) return "mode 3 at 128 bits";
        if (m != M_WRITE) return null;
        if (!aligned(a, s)) return "address 0x" + Integer.toHexString(a) + " not aligned to its size (" + SIZE_NAME[s] + " bits)";
        return badPacket(m, s, a, periphPresent) ? "region 0x" + Integer.toHexString(region(a)) + " not reachable by a packet" : null;
    }

    /** The payload's bits above its size cleared (3.1: they are 0). */
    static long maskLo(long lo, int size) { return size == SIZE_32 ? lo & 0xFFFFFFFFL : lo; }
    static long maskHi(long hi, int size) { return size == SIZE_128 || size == SIZE_UNDEF ? hi : 0; }

    /** Digit selector (spec 4.15): 0 none, 1..4 = byte d of the key in payload bits 31..0, 1 = least significant. */
    static int digit(long payload, int sel) { return sel == 0 ? 0 : (int) ((payload >>> (8 * (sel - 1))) & 0xFF); }

    static String describe(long payload, long h) { return describe(payload, h, 0); }
    static String describe(long payload, long h, long hi) {
        int s = size(h);
        String p = s == SIZE_32 ? String.format("0x%08x", payload & 0xFFFFFFFFL) : s == SIZE_64 ? String.format("0x%016x", payload)
                : String.format("0x%016x_%016x", hi, payload);
        return "src=" + src(h) + " dst=" + dst(h) + " mode=" + mode(h) + "(" + MODE_NAME[mode(h)] + ") ctr=" + counter(h)
                + " addr=0x" + String.format("%08x", addr(h)) + " size=" + SIZE_NAME[s] + " payload=" + p;
    }
}
