// Sketch 1 (WORKER3, stage 1): a FIFO consumer against sim.h, compiled, not run. `ref`'s WriteEngine.java (the DMA write
// engine, spec 4.9) with the same behaviour, less its S1 accounting hooks and log (measurement only). The link word's
// helpers would live in a shared header; they are inlined here to keep the sketch self-contained.
#include "base/fatal.h"
#include "config.h"
#include "sim.h"

namespace sketch {
using namespace sim;

// LinkWord.java's header fields (the simulator layout of word b).
inline unsigned lw_src(Word h) { return h & 0x7F; }
inline unsigned lw_mode(Word h) { return (h >> 14) & 7; }
inline unsigned lw_size(Word h) { return (h >> 20) & 3; }
inline uint32_t lw_addr(Word h) { return uint32_t(h >> 22); }
inline unsigned lw_chunks(unsigned size) { return 1u << size; }
inline unsigned lw_digit(Word d, unsigned sel) { return (d >> (sel & 0xFF)) & ((1u << (sel >> 8)) - 1); }   // illustrative
enum : unsigned { M_WRITE = 0, M_SRC = 1, M_BUCKET = 2, M_AUG = 3 };
enum : Word { OP_ADD_RET = 2, PTR = 0x100 };                                                                 // illustrative
inline Word sram_write_req(uint32_t chunk, unsigned n) { return Word(chunk) | Word(n) << 32; }               // illustrative

struct WriteEngine {
    // ---- ports ----
    FifoIn in;                        // from the traffic director (4 entries)
    LinkOut ts_req; LinkIn ts_ret;    // pointer RMWs with the table store
    LinkOut hist_req;                 // the HIST store
    HoldOut to_sram;                  // the big SRAM's write holding register
    // ---- configuration (DMA_CONFIG, DMA_RECORD_BASE_ADDRESS) ----
    unsigned bucket_digit = 0, hist_digit = 0; uint32_t augment_base = 0, mem_chunks;
    // ---- the pointer stage: a ring of `cap` entries; `awaiting` the entries waiting for their PTR return, in order ----
    static constexpr unsigned kMaxCap = 16;
    unsigned cap, head = 0, count = 0, aw_head = 0, aw_count = 0;
    Word st_d[kMaxCap], st_h[kMaxCap], st_c[kMaxCap]; uint32_t st_ptr[kMaxCap]; bool st_have[kMaxCap];
    unsigned awaiting[kMaxCap];
    Word sent[3];                     // the words last put to the SRAM (a refusal puts them again)
    // ---- stats ----
    uint64_t taken = 0, written = 0, stall_cycles = 0, augmented = 0, explicit_ = 0, out_of_range = 0;

    WriteEngine(Ports& p, unsigned in_depth, unsigned ts_latency, uint32_t chunks)
        : mem_chunks(chunks), cap(2 * ts_latency + 2) {
        if (cap > kMaxCap) FATAL("write engine: pointer stage %u > %u", cap, kMaxCap);
        p.in(in, "we-in", 3, in_depth);
        p.out(ts_req, "we>ts", 3); p.in(ts_ret, "ts>we", 3);
        p.out(hist_req, "we>hist", 3);
        p.out(to_sram, "sram.wr", 3);
    }

    uint64_t digest() const {
        return hash_fields(in, ts_req, ts_ret, hist_req, to_sram, head, count, aw_head, aw_count, st_d, st_h, st_c, st_ptr,
                           awaiting, sent, taken, written, stall_cycles, augmented, explicit_, out_of_range);
    }
    Wake step(Ctx& c) {
        // pointer returns, in order
        if (c.ready(ts_ret)) {
            uint32_t old = uint32_t(c.peek(ts_ret)[1]); c.take(ts_ret);
            if (aw_count) { unsigned i = awaiting[aw_head]; aw_head = (aw_head + 1) % cap; aw_count--; st_ptr[i] = old & 0x1FFFFF; st_have[i] = true; }
        }
        // the write side
        if (c.refused(to_sram)) { c.put(to_sram, sent[0], sent[1], sent[2]); stall_cycles++; }
        else if (count > 0 && st_have[head]) {
            Word lo = st_d[head], h = st_h[head], hi = st_c[head]; uint32_t a = st_ptr[head];
            unsigned n, mode = lw_mode(h);
            if (mode == M_AUG) { lo = (lo & 0xFFFFFFFF) | Word(((a - augment_base) & 0x1FFFFF) >> 1) << 32; hi = 0; n = 2; augmented++; }
            else n = lw_chunks(lw_size(h));
            if (a & (n - 1)) FATAL("write engine: a write through PTR = chunk 0x%x, not aligned to its %u-bit write (UB, s8b)", a, 32 * n);
            else if (a + n > mem_chunks) out_of_range++;                        // invariant 19: flagged, dropped
            else { sent[0] = sram_write_req(a, n); sent[1] = lo; sent[2] = hi; c.put(to_sram, sent[0], sent[1], sent[2]); written++; }
            st_have[head] = false; head = (head + 1) % cap; count--;
        }
        // the input side: one packet a cycle while the pointer stage has room
        if (count < cap && c.ready(in)) {
            const Word* w = c.peek(in); Word d = w[0], h = w[1], hi = w[2]; c.take(in);
            taken++;
            unsigned mode = lw_mode(h), i = (head + count) % cap;
            st_d[i] = d; st_h[i] = h; st_c[i] = hi; count++;
            if (mode == M_WRITE) { st_ptr[i] = (lw_addr(h) & 0xFFFFFF) >> 2; st_have[i] = true; explicit_++; }
            else {
                unsigned idx = mode == M_BUCKET ? lw_digit(d, bucket_digit) : lw_src(h);
                unsigned stp = mode == M_AUG ? 2 : lw_chunks(lw_size(h));
                c.put(ts_req, OP_ADD_RET, PTR + idx, stp);
                st_have[i] = false; awaiting[(aw_head + aw_count++) % cap] = i;
                if (hist_digit) c.put(hist_req, OP_ADD_RET, lw_digit(d, hist_digit), 1);
            }
        }
        // again next cycle if the head can be written or a packet can be taken; otherwise a return, a packet or a
        // refusal wakes it
        return (count > 0 && st_have[head]) || (count < cap && c.ready(in)) ? c.next() : Wake::none();
    }
};

// How it is configured (the tile's part): a director's Fifo output and the table stores' Links, wired once.
struct Director { FifoOut to_we; Director(Ports& p) { p.out(to_we, "dir>we", 3); } Wake step(Ctx&) { return Wake::none(); } };

void configure_example(Config& cfg) {
    auto& dir = cfg.add<Director>("t0.dir");
    auto& we = cfg.add<WriteEngine>("t0.we", 4u, 1u, 1u << 21);
    cfg.connect(dir.to_we, we.in);
    (void)we;   // ts_req / ts_ret / hist_req / to_sram wired to the table store, HIST and the SRAM the same way
}

static_assert(UnitType<WriteEngine>);

}  // namespace sketch
