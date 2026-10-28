/**
 * Bit layouts of the table entries and DMA_CONFIG (spec 4.2, 8.5, 8.9; v1.1). The engines' Java encoders and decoders;
 * {@link Registers} holds the same fields for mesh.h and REGISTERS.md (RegDocTest checks they agree).
 *
 * STREAM[i] (v1.1): one word in each of three 256 x 32 arrays, read in parallel at the same index (4.10):
 *   STREAM_SRC[i]:   source chunk address [20:0]
 *   STREAM_DEST[i]:  destination word [20:0] (modes 3, 6 and 9: element 0's addr, + 1, 2 or 4 per element), d [27:21]
 *                    (0-63 a relative tile, 64 C&C, 65-127 errors)
 *   STREAM_COUNT[i]: count [17:0], mode [21:18] (4 bits: 1-5 or 8), read size [23:22] (00 32, 01 64, 10 128; 11
 *                    undefined), gap after [26:24]
 * The address generator's read-ahead entry is two longs: e0 = STREAM_SRC | STREAM_DEST << 32, e1 = STREAM_COUNT.
 * GROUP[g], low:     R [17:0]
 * GROUP[g], high:    start stream [7:0], t - 1 [11:8], gap [19:12]
 * DMA_CONFIG:         run kind [0] (0 stream run, 1 group run), run start [8:1], run count [17:9], bucket digit [20:18],
 *                     histogram digit [23:21], k - 1 [26:24]
 * TILE_PULSE:         [0] done, [1] start, [2] replay start (reserved until the replayer is specified), [3] reserved
 */
final class Fields {
    private Fields() {}
    /** Read sizes (3.1, 4.2): 00 32 bits, 01 64, 10 128 (an element is 1 << size chunks); 11 undefined. */
    static final int SIZE_32 = LinkWord.SIZE_32, SIZE_64 = LinkWord.SIZE_64, SIZE_128 = LinkWord.SIZE_128, SIZE_UNDEF = LinkWord.SIZE_UNDEF;

    static int streamSrc(int src) { return src & 0x1FFFFF; }
    static int streamDest(int word, int d) { return (word & 0x1FFFFF) | (d & 0x7F) << 21; }
    static int streamCount(int count, int mode, int size, int gapAfter) {
        return (count & 0x3FFFF) | (mode & 15) << 18 | (size & 3) << 22 | (gapAfter & 7) << 24;
    }
    /** The read-ahead entry's first long: STREAM_SRC and STREAM_DEST. */
    static long streamE0(int src, int dest) { return (src & 0xFFFFFFFFL) | (long) dest << 32; }
    static int sSrc(long e0)      { return (int) (e0 & 0x1FFFFF); }
    static int sWord(long e0)     { return (int) ((e0 >>> 32) & 0x1FFFFF); }
    static int sD(long e0)        { return (int) ((e0 >>> 53) & 0x7F); }
    static int sCount(long e1)    { return (int) (e1 & 0x3FFFF); }
    static int sMode(long e1)     { return (int) ((e1 >>> 18) & 15); }
    static int sSize(long e1)     { return (int) ((e1 >>> 22) & 3); }
    static int sGap(long e1)      { return (int) ((e1 >>> 24) & 7); }

    static int groupLo(int R) { return R & 0x3FFFF; }
    static int groupHi(int startStream, int t, int gap) { return (startStream & 0xFF) | ((t - 1) & 0xF) << 8 | (gap & 0xFF) << 12; }
    static int cR(long e)     { return (int) (e & 0x3FFFF); }
    static int cStart(long e) { return (int) ((e >>> 32) & 0xFF); }
    static int cT(long e)     { return (int) ((e >>> 40) & 0xF) + 1; }
    static int cGap(long e)   { return (int) ((e >>> 44) & 0xFF); }

    static final int RUN_STREAMS = 0, RUN_GROUPS = 1;
    static int masterConfig(int kind, int start, int count, int bucketDigit, int histDigit, int k) {
        return (kind & 1) | (start & 0xFF) << 1 | (count & 0x1FF) << 9 | (bucketDigit & 7) << 18 | (histDigit & 7) << 21
                | ((k - 1) & 7) << 24;
    }
    static int mcKind(int v)       { return v & 1; }
    static int mcStart(int v)      { return (v >>> 1) & 0xFF; }
    static int mcCount(int v)      { return (v >>> 9) & 0x1FF; }
    static int mcBucketDigit(int v) { return (v >>> 18) & 7; }
    static int mcHistDigit(int v)   { return (v >>> 21) & 7; }
    static int mcK(int v)           { return ((v >>> 24) & 7) + 1; }

    static final int PULSE_DONE = 1, PULSE_START = 2, PULSE_REPLAY = 4, PULSE_RSVD3 = 8;
}
