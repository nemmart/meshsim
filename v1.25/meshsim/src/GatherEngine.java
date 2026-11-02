import java.util.ArrayList;
import java.util.List;

/**
 * The GATHER engine (spec 4.4, v1.2; v1.25 section 8c): serves local mode-4 gather requests. For each request taken from its
 * 16-entry queue it puts the value's reads in the SRAM read queue (client 0, the highest priority, above the address
 * generator), one per cycle, the ordinal being the payload's bits 51-32 (fixed bits at any size, 3.1):
 *   k odd: k reads of 64 bits at DMA_RECORD_BASE_ADDRESS + 2k * ordinal + 2i, i = 0 .. k-1, each a 64-bit mode-1 packet;
 *   k even (v1.25): k/2 reads of 128 bits at DMA_RECORD_BASE_ADDRESS + 2k * ordinal + 4i, i = 0 .. k/2-1, each a 128-bit
 *   mode-1 packet (PTR[me] advances by 4 per packet).
 * Each return leaves as a mode-1 packet to the tile itself (dst = me, address 0): a gather value, landing at PTR[me],
 * carrying the request's counter field, so a gather of n values counts n x (packets per value), k or k/2; the field (4.4: requests are never
 * counted; their counter field passes to the values, which are counted at the director like any write). k is DMA_CONFIG's k - 1 field + 1 (1-8, proposed); DMA_RECORD_BASE_ADDRESS is the register.
 * A read the SRAM read queue does not take is presented again and nothing advances (the queue fills; its overflow is the
 * GATHER-queue sticky bit, DEV 9).
 *
 * GATHER busy (status): the queue holds a request, the engine is still issuing one, or a read it issued has not been
 * returned (released by the SRAM read queue) yet.
 */
final class GatherEngine implements Component {
    final int me;
    WordFIFO in;              // the request queue (16); null when a drain stands in (M1-M3 machines)
    WordFIFO queue;           // the queue, for busy()
    WordHold out;             // read-queue client 1
    SRAMReadQueue srq;
    int k = 1, base;          // set from DMA_CONFIG and DMA_RECORD_BASE_ADDRESS
    private int left, ord, i, ctr;
    private boolean wide;     // v1.25: k even, 128-bit reads and packets
    long requests, reads, packets, stallCycles;   // reads = packets: one value packet per read
    List<long[]> log;

    GatherEngine(int me) { this.me = me; }

    @Override public void runCycle(long t) {
        if (out.refused(t)) { out.retry(t); stallCycles++; return; }
        if (left == 0) {
            if (!in.ready(t)) return;
            long d = in.peekA(t), h = in.peekB(t);
            in.take(t);
            if (log != null) log.add(new long[] { t, d, h });
            wide = (k & 1) == 0;
            ord = (int) (d >>> 32) & 0xFFFFF; ctr = LinkWord.counter(h); left = wide ? k / 2 : k; i = 0; requests++;   // 4.4: the entry holds the 20-bit ordinal, payload [51:32], and the counter field
        }
        int addr = (base + 2 * k * ord + (wide ? 4 : 2) * i) & 0x1FFFFF;
        out.put(t, SRAMReadQueue.reqA(addr, wide ? Fields.SIZE_128 : Fields.SIZE_64, LinkWord.M_SRC, ctr, me), SRAMReadQueue.reqB(0), 0);
        packets++;
        reads++; i++; left--;
    }
    boolean busy(long t) { return !queue.isEmpty(t) || left > 0 || !out.isEmpty(t) || srq.inFlightOf(SRAMReadQueue.GATHER) > 0; }
    void record() { log = new ArrayList<>(); }
    /** HALT (13a): the queue and the active register clear. */
    void haltReset() { if (queue != null) queue.clear(); left = 0; }
}
