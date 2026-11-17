// updater_top.h: the Updater with its real controller (updater_ctl.h), as the master tile's peripheral 2 (updater_wip,
// WORKER6; UPDATER-INTERFACE.md, "UI"). The slot installs it in place of the black box (updater.h) when the machine asks
// for it (MachConfig::updater_ctl); the slot's dispatch tag is PT_UPDATER_CTL, and PARAMS still says type 5.
//
// What is here and not in the controller: the control block's events, counted as the black box counts them (done,
// depth 1; the credits, depth 2) from the controller's pulses; ERRORS, the controller's bits ORed with the shim's
// overrun; the window's undefined behaviour, checked as the black box checks it; the region's two parts; and the wiring
// of the controller's three ports to what is on the other side: the word source (the shim, or a test's feeder), the
// AXI slave and the cache (the test pieces under tests/, or later WORKER5's arbiter and cache).
//
// THE SHIM (segment_shim.h) is v1.37's segment queue in front of the word port. Every line that knows it is marked
// "shim": deleting them, and giving `source` Ingress's forwarder, is all that WORKER7's forward count needs here.
//
// A CYCLE: step(t) takes every unit's outputs of t first (the controller's, the source's, the AXI slave's, the
// cache's), posts the control block's events from them, then steps every unit's edge with the others' outputs of t.
#pragma once

#include <cstdint>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/linkword.h"
#include "units/peripheral.h"
#include "units/segment_shim.h"
#include "units/updater_ctl.h"

namespace units {
namespace tracer { class FileStore; }

namespace updctl {
// What is on the far side of each port, as the top steps it: its outputs of a cycle, and its edge. Given by a harness.
struct AxiDev {
    virtual axi::SlaveOut out() const = 0;
    virtual void edge(int64_t t, const axi::MasterOut& m) = 0;
    virtual bool quiet() const = 0;                // nothing in flight: an edge would change nothing
    virtual uint64_t violations() const { return 0; }
protected:
    ~AxiDev() = default;
};
struct LookupDev {
    virtual bool ready() const = 0;                // port 1's ready
    virtual lookup::Reply reply() const = 0;       // the shared reply bus
    virtual void edge(int64_t t, const lookup::Request& port1) = 0;
    virtual bool quiet() const = 0;
    virtual uint64_t violations() const { return 0; }
protected:
    ~LookupDev() = default;
};
struct WordDev {
    virtual wordport::SourceOut out() const = 0;
    virtual void edge(int64_t t, const wordport::SinkOut& k) = 0;
    virtual bool quiet() const = 0;
protected:
    ~WordDev() = default;
};
}  // namespace updctl

struct UpdaterTop : PeripheralBase<UpdaterTop> {
    using Ctl = updctl::Ctl;
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1, EV_SEGMENT_CREDIT = 2, EV_FREE_PAGE_CREDIT = 3, EV_BITS = 4 };
    enum : unsigned { PART_SEGMENT = 1, PART_FREE_PAGES = 2 };
    static constexpr int TYPE = 5;
    static constexpr int DIGEST_N = 3;

    Ctl ctl;
    SegmentShim shim;                              // shim
    bool shim_on = true;                           // shim: region part 1 and EVENTS [2] are the shim's
    updctl::WordDev* source = nullptr;             // the word source when the shim is off (a test's feeder; Ingress later)
    updctl::AxiDev* lpddr = nullptr;               // the AXI slave on the controller's port
    updctl::LookupDev* cache = nullptr;            // the cache on its look-up port
    tracer::FileStore* store = nullptr;            // the memory behind them: for the harness's checks only
    EventCounts ev;
    // A harness's observer of a run's end (null: none), called in the cycle done is posted: the images are whole then.
    void (*on_done)(void* ctx, UpdaterTop& u, int64_t t) = nullptr;
    void* on_done_ctx = nullptr;

    UpdaterTop() { reset(0); }

    // ---- identity
    int type() const { return TYPE; }
    int params() const { return TYPE; }
    // ---- inputs
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
        const unsigned part = offset >> 20 & 7;
        if (size != int(lw::SIZE_128))
            FATAL("the Updater: undefined behavior at %lld: a WRITE of another size than 128 bits into its region (offset 0x%x, from %d)",
                  (long long)t, offset, src);
        if (part == PART_SEGMENT && shim_on) shim.push(lo, hi);                     // shim
        else if (part == PART_FREE_PAGES) ctl.push_free_page(uint32_t(lo));
        else FATAL("the Updater: undefined behavior at %lld: a WRITE to part %u of its region, which maps parts %s (offset 0x%x, from %d)",
                   (long long)t, part, shim_on ? "1 and 2" : "2", offset, src);
    }
    void run(int64_t t) {
        if (ctl.q.busy) FATAL("the Updater: undefined behavior at %lld: a START during a run (17.2)", (long long)t);
        if (!lpddr || !cache) FATAL("the Updater's controller: no AXI slave or no cache on its ports (a harness attaches them)");
        if (!shim_on && !source) FATAL("the Updater's controller: no word source on its port");
        ctl.start();
    }
    int write_word(int64_t t, int w, uint32_t v) {
        if (w >= Ctl::W_STAT_CYCLES && w <= Ctl::W_STAT_PAGES_WRITTEN) return 0;   // read-only
        if (w < Ctl::W_RUN_CONFIG || w > Ctl::W_SOURCE_ROOT) return -1;
        if (ctl.q.busy) FATAL("the Updater: undefined behavior at %lld: a store to word %d of block 2 during a run (UI: written while idle)", (long long)t, w);
        ctl.write_block2(w, v);
        return 1;
    }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    // ---- level reads: busy until done is posted, which is a cycle after the controller's busy falls
    bool running(int64_t) const { return ctl.q.busy || ctl.q.done_pulse; }
    int errors(int64_t) const { return ctl.q.errors | (shim.overrun ? Ctl::ERR_QUEUE_OVERRUN : 0); }   // shim
    EventCounts& events() { return ev; }
    bool word(int64_t, int w, uint32_t& v) { return ctl.word(w, v); }
    bool writable(int w) const { return w >= Ctl::W_RUN_CONFIG && w <= Ctl::W_SOURCE_ROOT; }

    // ---- the cycle
    void step(int64_t t) {
        const Ctl::Outputs o = ctl.out();
        Ctl::Inputs in;
        in.wp = shim_on ? shim.out() : source ? source->out() : wordport::SourceOut{};   // shim
        if (lpddr) in.axi = lpddr->out();
        if (cache) { in.lk_ready = cache->ready(); in.reply = cache->reply(); }
        if ((o.axi.arvalid || o.axi.awvalid || o.axi.wvalid) && !lpddr) FATAL("the Updater's controller: a burst with no AXI slave on its port");
        if (o.lk.valid && !cache) FATAL("the Updater's controller: a request with no cache on its look-up port");
        if (o.done) ev.post(t, EV_DONE);
        if (o.free_page_credit) ev.post(t, EV_FREE_PAGE_CREDIT);
        if (shim_on && shim.credit) ev.post(t, EV_SEGMENT_CREDIT);                  // shim
        if (lpddr) lpddr->edge(t, o.axi);
        if (cache) cache->edge(t, o.lk);
        if (shim_on) shim.edge(o.wp);                                                // shim
        else if (source) source->edge(t, o.wp);
        ctl.edge(t, in);
        if (o.done && on_done) on_done(on_done_ctx, *this, t);
    }
    bool idle(int64_t) const {
        const wordport::SourceOut s = shim_on ? shim.out() : source ? source->out() : wordport::SourceOut{};
        const bool word_moves = s.valid && ctl.q.ready;
        const bool src_quiet = shim_on ? shim.quiet() : !source || source->quiet();
        return ctl.quiet() && !word_moves && src_quiet && (!lpddr || lpddr->quiet()) && (!cache || cache->quiet());
    }
    uint64_t violation_count() const { return (lpddr ? lpddr->violations() : 0) + (cache ? cache->violations() : 0); }
    void digest_words(int64_t* out) const {
        const Ctl::Regs& r = ctl.q;
        out[0] = int64_t(sim::hash_fields(r.busy, r.errors, r.run_config, r.word_count, r.page_count, r.image_root, r.tracer_root, r.source_root, r.stat));
        out[1] = int64_t(sim::hash_fields(r.fifo_n, r.nb, r.run_addr, r.run_left, r.out_valid, r.fp_n, r.fp_head, uint8_t(r.state), r.held, r.held_page,
                                          shim.n, shim.head, ev.count, ev.last_post_at));
        out[2] = int64_t(sim::hash_fields(r.bb, r.lru, r.ar_valid, r.aw_valid, r.w_valid, r.lk.valid, r.lk_wait));
    }
    // The reset, and nothing else, empties the queues and fills the credits to 2 (UI, "The region").
    void reset(int64_t t) {
        ctl.reset();
        shim.reset();                                                               // shim
        static const uint8_t depth[EV_BITS] = {1, 1, 2, 2};
        static const bool flags[EV_BITS] = {false, false, true, true};
        ev.init(EV_BITS, depth, flags);
        ev.clear_all();
        if (shim_on) ev.set(t, EV_SEGMENT_CREDIT, 2);                               // shim
        ev.set(t, EV_FREE_PAGE_CREDIT, 2);
    }

    // ---- for a harness
    unsigned segment_entries() const { return shim.n; }                            // shim
    unsigned free_page_entries() const { return ctl.q.fp_n; }
};

}  // namespace units
