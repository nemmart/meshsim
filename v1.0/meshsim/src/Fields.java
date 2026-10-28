/**
 * Bit layouts of the table entries and the master registers. The spec gives the fields and widths; positions are
 * the simulator's proposal (spec 8: "assigned last"), listed in the deviations. M4 generates mesh.h from these.
 *
 * STREAM[i], low word:  source chunk address [19:0], controller-memory address [31:20] (mode 5: word)
 * STREAM[i], high word: count [17:0], destination offset d [23:18], mode [26:24] (3 bits, zero-extended to the packet's
 *                     4; codes 1-5, 0/6/7 reserved: 9 October), read size [28:27], gap after [31:29]
 * GROUP[g], low:     R [17:0]
 * GROUP[g], high:    start stream [7:0], t - 1 [11:8], gap [19:12]
 * DMA_CONFIG:         run kind [0] (0 stream run, 1 group run), run start [8:1], run count [17:9], bucket digit [20:18],
 *                     histogram digit [23:21], k - 1 [26:24] (27 bits; The attach bit [24] left, augmenting is mode 4,
 *                     and k - 1 moved down from [27:25])
 * DMA_PULSE:          [0] done, [1] start, [3:2] reserved; start acts before done (v1.0: arm merged into start)
 */
final class Fields {
    private Fields() {}
    static final int SIZE_RSVD = 0, SIZE_32 = 1, SIZE_64 = 2, SIZE_128 = 3;

    static int streamLo(int src, int cm) { return (src & 0xFFFFF) | (cm & 0xFFF) << 20; }
    static int streamHi(int count, int d, int mode, int size, int gapAfter) {
        return (count & 0x3FFFF) | (d & 63) << 18 | (mode & 7) << 24 | (size & 3) << 27 | (gapAfter & 7) << 29;
    }
    static int sSrc(long e)   { return (int) (e & 0xFFFFF); }
    static int sCm(long e)    { return (int) ((e >>> 20) & 0xFFF); }
    static int sCount(long e) { return (int) ((e >>> 32) & 0x3FFFF); }
    static int sD(long e)     { return (int) ((e >>> 50) & 63); }
    static int sMode(long e)  { return (int) ((e >>> 56) & 7); }
    static int sSize(long e)  { return (int) ((e >>> 59) & 3); }
    static int sGap(long e)   { return (int) ((e >>> 61) & 7); }

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

    static final int PULSE_DONE = 1, PULSE_START = 2;
}
