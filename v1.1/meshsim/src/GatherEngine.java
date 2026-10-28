import java.util.ArrayList;
import java.util.List;

/**
 * The GATHER engine (spec 4.4): serves local mode-1 gather requests. For each request taken from its 16-entry queue it
 * puts k gather-value reads in the SRAM read queue (client 0, the highest priority, above the address generator), one
 * per cycle: 64 bits at DMA_RECORD_BASE_ADDRESS + 2k * ordinal + 2i, i = 0 .. k-1, the ordinal being the payload's bits 51-32
 * (fixed bits at any size, 3.1). Each return leaves as a 64-bit mode-2 packet (size 01) to the tile itself (dst = me, addr 0): a gather value, landing at PTR[me] and
 * counted in DMA_SRAM_REMAINING. k is DMA_CONFIG's k - 1 field + 1 (1-8, proposed); DMA_RECORD_BASE_ADDRESS is the register.
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
    private int left, ord, i;
    long requests, reads, stallCycles;
    List<long[]> log;

    GatherEngine(int me) { this.me = me; }

    @Override public void runCycle(long t) {
        if (out.refused(t)) { out.retry(t); stallCycles++; return; }
        if (left == 0) {
            if (!in.ready(t)) return;
            long d = in.peekA(t), h = in.peekB(t);
            in.take(t);
            if (log != null) log.add(new long[] { t, d, h });
            ord = (int) (d >>> 32) & 0xFFFFF; left = k; i = 0; requests++;   // 4.4: the entry holds the 20-bit ordinal, payload [51:32]
        }
        int addr = (base + 2 * k * ord + 2 * i) & 0x1FFFFF;
        out.put(t, SRAMReadQueue.reqA(addr, Fields.SIZE_64, LinkWord.M_SRC, me), SRAMReadQueue.reqB(0, 0), 0);
        reads++; i++; left--;
    }
    boolean busy(long t) { return !queue.isEmpty(t) || left > 0 || !out.isEmpty(t) || srq.inFlightOf(SRAMReadQueue.GATHER) > 0; }
    void record() { log = new ArrayList<>(); }
    /** HALT (13a): the queue and the active register clear. */
    void haltReset() { if (queue != null) queue.clear(); left = 0; }
}
