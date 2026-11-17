// updater_top.h: the Updater with its real controller (updater_ctl.h), as the master tile's peripheral 2 (updater_wip,
// WORKER6; UPDATER-INTERFACE.md, "UI"). The slot installs it in place of the black box (updater.h) when the machine asks
// for it (MachConfig::updater_ctl); the slot's dispatch tag is PT_UPDATER_CTL, and PARAMS still says type 5.
//
// What is here and not in the controller: the control block's events, counted as the black box counts them (done,
// depth 1; the free page queue's credit, depth 2) from the controller's pulses; ERRORS, the controller's bits; the
// window's undefined behaviour, checked as the black box checks it; the region's one part, the free page queue; and the
// wiring of the controller's ports to what is on the other side: the AXI slave and the cache (the test pieces under
// tests/, or WORKER5's arbiter and cache).
//
// THE DATA INPUT is the word port's sink (word_port.h), and the only way in (owner, 2026-11-17): port_sink() and
// port_take(t, word), the two methods the slot wires to Ingress's forwarder (periph_slot.h, THE WORD PORT), as the
// black box has them (updater.h). The segment queue that stood in front of the port as a shim until Ingress's forward
// count landed (WORKER7) is gone, with region part 1 and EVENTS [2], which reads 0 (updater_wip, WORKER9).
//
// A CYCLE, as the slot runs it: port_sink() is the controller's `ready` of t, taken before any step; step(t) takes
// every unit's outputs of t (the controller's, the AXI slave's, the cache's), posts the control block's events from
// them and steps the far side's edges with the controller's outputs of t; port_take(t, word), if a word moved in t,
// gives the word; settle(t) is the controller's one edge, with its inputs of t, the word among them. The controller
// uses the port's input only as `valid && ready`, so a cycle with no move is its edge with `valid` 0, exactly. A
// harness that steps the top itself calls the same three in that order (tests/ctl_bench.h).
#pragma once

#include <cstdint>
#include <memory>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/linkword.h"
#include "units/peripheral.h"
#include "units/updater_ctl.h"
#include "units/word_port.h"

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
}  // namespace updctl

struct UpdaterTop : PeripheralBase<UpdaterTop> {
    using Ctl = updctl::Ctl;
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1, EV_UNUSED = 2, EV_FREE_PAGE_CREDIT = 3, EV_BITS = 4 };   // [2]: as the black box's
    enum : unsigned { PART_FREE_PAGES = 2 };
    static constexpr int TYPE = 5;
    static constexpr int DIGEST_N = 3;

    Ctl ctl;
    updctl::AxiDev* lpddr = nullptr;               // the AXI slave on the controller's port
    updctl::LookupDev* cache = nullptr;            // the cache on its look-up port
    tracer::FileStore* store = nullptr;            // the memory behind them: for the harness's checks only
    EventCounts ev;
    // A harness's observer of a run's end (null: none), called in the cycle done is posted: the images are whole then.
    void (*on_done)(void* ctx, UpdaterTop& u, int64_t t) = nullptr;
    void* on_done_ctx = nullptr;
    std::shared_ptr<void> keep;                    // what a harness gives it to own (the test pieces behind its ports)

    UpdaterTop() { reset(0); }

    // ---- identity
    int type() const { return TYPE; }
    int params() const { return TYPE; }
    // ---- inputs
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t) {   // a free page is in [31:0] (UI)
        const unsigned part = offset >> 20 & 7;
        if (size != int(lw::SIZE_128))
            FATAL("the Updater: undefined behavior at %lld: a WRITE of another size than 128 bits into its region (offset 0x%x, from %d)",
                  (long long)t, offset, src);
        if (part == PART_FREE_PAGES) ctl.push_free_page(uint32_t(lo));
        else FATAL("the Updater: undefined behavior at %lld: a WRITE to part %u of its region, which maps part 2 alone (offset 0x%x, from %d)",
                   (long long)t, part, offset, src);
    }
    void run(int64_t t) {
        if (ctl.q.busy) FATAL("the Updater: undefined behavior at %lld: a START during a run (17.2)", (long long)t);
        if (!lpddr || !cache) FATAL("the Updater's controller: no AXI slave or no cache on its ports (a harness attaches them)");
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
    int errors(int64_t) const { return ctl.q.errors; }
    EventCounts& events() { return ev; }
    bool word(int64_t, int w, uint32_t& v) { return ctl.word(w, v); }
    bool writable(int w) const { return w >= Ctl::W_RUN_CONFIG && w <= Ctl::W_SOURCE_ROOT; }

    // ---- the data input: the word port's sink (header, THE DATA INPUT)
    wordport::SinkOut port_sink() const { wordport::SinkOut k; k.ready = ctl.q.ready; return k; }
    // The word that moved in t (valid and ready both 1 in t's outputs): one of the inputs of the controller's edge of t.
    void port_take(int64_t t, const wordport::SourceOut& w) {
        if (!edge_due_ || edge_t_ != t) FATAL("the Updater's controller: a word on its data input at %lld outside its cycle (step, port_take, settle)", (long long)t);
        if (!w.valid || !ctl.q.ready) FATAL("the Updater's controller: a word on its data input at %lld with valid %d and ready %d", (long long)t, int(w.valid), int(ctl.q.ready));
        if (in_.wp.valid) FATAL("the Updater's controller: two words on its data input at %lld", (long long)t);
        in_.wp = w;
        words_in++;
    }

    // ---- the cycle (header, A CYCLE)
    void step(int64_t t) {
        if (edge_due_) FATAL("the Updater's controller: step(%lld) with the edge of %lld not taken (settle)", (long long)t, (long long)edge_t_);
        const Ctl::Outputs o = ctl.out();
        in_ = Ctl::Inputs{};
        if (lpddr) in_.axi = lpddr->out();
        if (cache) { in_.lk_ready = cache->ready(); in_.reply = cache->reply(); }
        if ((o.axi.arvalid || o.axi.awvalid || o.axi.wvalid) && !lpddr) FATAL("the Updater's controller: a burst with no AXI slave on its port");
        if (o.lk.valid && !cache) FATAL("the Updater's controller: a request with no cache on its look-up port");
        if (o.done) ev.post(t, EV_DONE);
        if (o.free_page_credit) ev.post(t, EV_FREE_PAGE_CREDIT);
        if (lpddr) lpddr->edge(t, o.axi);
        if (cache) cache->edge(t, o.lk);
        done_out_ = o.done;
        edge_due_ = true; edge_t_ = t;
    }
    void settle(int64_t t) {
        if (!edge_due_) return;                    // a cycle the slot did not step this peripheral in
        if (edge_t_ != t) FATAL("the Updater's controller: settle(%lld) after step(%lld)", (long long)t, (long long)edge_t_);
        edge_due_ = false;
        ctl.edge(t, in_);
        if (done_out_ && on_done) on_done(on_done_ctx, *this, t);
    }
    // The slot adds the word port: it is not idle while a word can move (periph_slot.cpp).
    bool idle(int64_t) const { return ctl.quiet() && (!lpddr || lpddr->quiet()) && (!cache || cache->quiet()); }
    uint64_t violation_count() const { return (lpddr ? lpddr->violations() : 0) + (cache ? cache->violations() : 0); }
    void digest_words(int64_t* out) const {
        const Ctl::Regs& r = ctl.q;
        out[0] = int64_t(sim::hash_fields(r.busy, r.errors, r.run_config, r.word_count, r.page_count, r.image_root, r.tracer_root, r.source_root, r.stat));
        out[1] = int64_t(sim::hash_fields(r.fifo_n, r.nb, r.run_addr, r.run_left, r.out_valid, r.fp_n, r.fp_head, uint8_t(r.state), r.held, r.held_page,
                                          ev.count, ev.last_post_at));
        out[2] = int64_t(sim::hash_fields(r.bb, r.lru, r.ar_valid, r.aw_valid, r.w_valid, r.lk.valid, r.lk_wait));
    }
    // The reset, and nothing else, empties the queues and fills the credits to 2 (UI, "The region").
    void reset(int64_t t) {
        ctl.reset();
        edge_due_ = false; in_ = Ctl::Inputs{};
        static const uint8_t depth[EV_BITS] = {1, 1, 0, 2};               // [2] unused: reads 0 (as the black box's)
        static const bool flags[EV_BITS] = {false, false, false, true};  // a credit returned past its depth is a fault
        ev.init(EV_BITS, depth, flags);
        ev.clear_all();
        ev.set(t, EV_FREE_PAGE_CREDIT, 2);
    }

    // ---- for a harness
    unsigned input_words() const { return ctl.q.fifo_n; }                          // the words held behind the port
    unsigned free_page_entries() const { return ctl.q.fp_n; }
    uint64_t words_in = 0;                                                         // the words taken on the port

private:
    // The controller's inputs of the cycle step(t) began, kept until settle(t) takes its edge: not hardware's state.
    Ctl::Inputs in_;
    bool edge_due_ = false, done_out_ = false;
    int64_t edge_t_ = 0;
};

}  // namespace units
