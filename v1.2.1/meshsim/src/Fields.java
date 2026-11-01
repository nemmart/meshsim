/**
 * Bit layouts of the table entries and DMA_CONFIG (spec 4.2, 8.1, 8.5, 8.9; v1.2). The engines' Java encoders and
 * decoders; {@link Registers} holds the same fields for mesh.h and REGISTERS.md (RegDocTest checks they agree).
 *
 * STREAM[i] (v1.2, D4): one word in each of three 256 x 32 arrays, read in parallel at the same index (4.10):
 *   STREAM_SRC[i]:   source chunk address [20:0], gap after [23:21], spare [31:24]
 *   STREAM_DEST[i]:  the first element's 32-bit byte address at the receiver (mode 0 only)
 *   STREAM_COUNT[i]: count [16:0], read size [18:17], counter [21:19], mode [24:22], d [31:25] (the common header
 *                    layout, 8.1: [24:17] is the link word's size, counter and mode block)
 * The address generator's read-ahead entry is two longs: e0 = STREAM_SRC | STREAM_DEST << 32, e1 = STREAM_COUNT.
 * GROUP[g], low:     R [17:0]
 * GROUP[g], high:    start stream [7:0], t - 1 [11:8], gap [19:12]
 * DMA_CONFIG:         run kind [0] (0 stream run, 1 group run), run start [8:1], run count [17:9], bucket digit [20:18],
 *                     histogram digit [23:21], k - 1 [26:24]
 */
final class Fields {
    private Fields() {}
    /** Read sizes (3.1, 4.2): 00 32 bits, 01 64, 10 128 (an element is 1 << size chunks); 11 undefined. */
    static final int SIZE_32 = LinkWord.SIZE_32, SIZE_64 = LinkWord.SIZE_64, SIZE_128 = LinkWord.SIZE_128, SIZE_UNDEF = LinkWord.SIZE_UNDEF;
    static final int COUNT_MAX = 0x1FFFF;                    // 17 bits: 131,071 elements (D30)

    /** The common header layout (8.1): size [18:17], counter [21:19], mode [24:22], dst (or d) [31:25]. */
    static int hdrBits(int size, int counter, int mode, int dst) { return (size & 3) << 17 | (counter & 7) << 19 | (mode & 7) << 22 | (dst & 0x7F) << 25; }
    static int hSize(int v)    { return (v >>> 17) & 3; }
    static int hCounter(int v) { return (v >>> 19) & 7; }
    static int hMode(int v)    { return (v >>> 22) & 7; }
    static int hDst(int v)     { return (v >>> 25) & 0x7F; }

    static int streamSrc(int chunk, int gapAfter) { return (chunk & 0x1FFFFF) | (gapAfter & 7) << 21; }
    static int streamSrc(int chunk) { return streamSrc(chunk, 0); }
    static int streamCount(int count, int size, int counter, int mode, int d) { return (count & COUNT_MAX) | hdrBits(size, counter, mode, d); }
    /** The read-ahead entry's first long: STREAM_SRC and STREAM_DEST. */
    static long streamE0(int src, int dest) { return (src & 0xFFFFFFFFL) | (long) dest << 32; }
    static int sSrc(long e0)      { return (int) (e0 & 0x1FFFFF); }
    static int sGap(long e0)      { return (int) ((e0 >>> 21) & 7); }
    static int sDest(long e0)     { return (int) (e0 >>> 32); }
    static int sCount(long e1)    { return (int) (e1 & COUNT_MAX); }
    static int sSize(long e1)     { return hSize((int) e1); }
    static int sCounter(long e1)  { return hCounter((int) e1); }
    static int sMode(long e1)     { return hMode((int) e1); }
    static int sD(long e1)        { return hDst((int) e1); }

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

    /** Java-side start (coreless tests and the controller's PULSE_DMA store): the run in DMA_CONFIG. */
    static final int PULSE_START = 2;
}
