// cache_system.h: the path to LPDDR as one piece of the simulator: the cache's controller (units/cache_ctl.h), the AXI
// arbiter (units/axi_arbiter.h) and the LPDDR model on the file store (units/lpddr_model.h), wired as machine-spec 23
// has them, with the cache on the arbiter's port 0. updater_wip, WORKER5; its clients' ports and the joint's checks,
// the INTEGRATOR (v1.37 stage 1). In the chip it is the memory tile's peripheral 1's hardware (units/memory_system.h,
// machine-spec 17.11); a bench may use it alone (the Tracer on its fill port: tracer_block --fill cache).
//
// ITS CLIENTS (23.10, 17.11's ports): the Tracer's fill port (attach, below); look-up port 1 and the arbiter's client 1,
// the Updater's (updater_joint below, units/updater_top.h's AxiDev and LookupDev); the arbiter's client 2, the RW
// engine's (RwJoint below, through axi_arbiter.h's RwEngineOnArbiter). The three are stepped by three tiles, one after
// another in a cycle, in an order the framework chooses, so no client reaches the units' insides: each, at its step of
// t, calls begin(t), reads the units' outputs of t (functions of their registers alone, axi_port.h's convention), and
// posts its own outputs of t. THE EDGE OF t IS TAKEN LAZILY: by the first begin(t + 1), from whichever client or the
// memory tile's peripheral, or by finish(). So it comes after every client's step of t whatever their order, and the
// cycle is the units' convention exactly: every output of t, then every edge of t. There is no latency on the ports
// (23.1: "latency on them does not matter to it"; the INTEGRATOR's C2).
//
// A CYCLE NO CLIENT STEPS IN is an edge with every client idle; begin() takes it so, and stops the simulator if the
// units were not idle then (a client asleep with something of its own in flight: a missed wake).
//
// THE JOINT'S CHECKS (WORKER9's, what machine-spec 23.6.8 asks of a client, checked where the clients meet; counted, not
// FATAL, so a planted bug is reported with the run's other results): no update names a page whose write by the Updater
// is not yet answered; the Updater writes no page of which the cache holds a line, and reads none of which it holds a
// dirty one; at an update's reply LPDDR holds the new entry; one look-up request outstanding a client and a reply only
// for a client with one. The cache's own stale-line check stays on (FATAL, as WORKER5 has it).
#pragma once

#include <cstdint>
#include <cstring>

#include "base/fatal.h"
#include "units/axi_arbiter.h"
#include "units/cache_ctl.h"
#include "units/file_store.h"
#include "units/lookup_port.h"
#include "units/lpddr_model.h"
#include "units/tracer.h"
#include "units/updater_top.h"

namespace units {

struct CacheSystemConfig {
    cache::Config cache;
    axi::ArbiterConfig arbiter;
    axi::LpddrTiming lpddr;
    bool stale_check = true;           // the cache's stale-line check against the store
};

class CacheSystem {
public:
    CacheSystem(tracer::FileStore* store, const CacheSystemConfig& c = {}) : ctl(c.cache), arb(c.arbiter), lpddr(store, c.lpddr), store_(store) {
        if (c.stale_check) ctl.check_store = store;
        port.ctx = this;
        port.read_ready = [](void* x, int64_t t) { CacheSystem& c = *static_cast<CacheSystem*>(x); c.begin(t); return c.ctl.out().fr_ready; };
        port.write_ready = [](void* x, int64_t t) { CacheSystem& c = *static_cast<CacheSystem*>(x); c.begin(t); return c.ctl.out().fw_ready; };
        port.read = [](void* x, int64_t t, int32_t root, int depth, int64_t line, tracer::MemSystem::Response&) -> int64_t {
            CacheSystem& c = *static_cast<CacheSystem*>(x);
            c.begin(t);
            if (c.tracer_)   // the statistics' classes only: the map's file is the one the Tracer's files word names
                c.ctl.cfg.map_root_for_stats = int32_t(uint32_t(c.tracer_->rec[2 * tracer::Tracer::REC_FILES] >> 33) & 0x7FFFFFFF);
            cache::FillRead& r = c.in.fr;
            r.valid = true; r.root = root; r.depth = uint8_t(depth); r.line = line;
            return INT64_MAX;
        };
        port.write = [](void* x, int64_t t, int32_t root, int depth, int64_t line, const uint64_t* data, int&) -> int64_t {
            CacheSystem& c = *static_cast<CacheSystem*>(x);
            c.begin(t);
            cache::FillWrite& w = c.in.fw;
            w.valid = true; w.root = root; w.depth = uint8_t(depth); w.line = line;
            std::memcpy(w.data, data, sizeof w.data);
            return INT64_MAX;
        };
        port.read_response = [](void* x, int64_t t, tracer::MemSystem::Response& r) {
            CacheSystem& c = *static_cast<CacheSystem*>(x);
            c.begin(t);
            const cache::Outputs& o = c.ctl.out();
            if (!o.frr_valid) return false;
            std::memcpy(r.data, o.frr_data, sizeof r.data);
            r.status = o.frr_status;
            return true;
        };
        port.write_response = [](void* x, int64_t t, int& status) {
            CacheSystem& c = *static_cast<CacheSystem*>(x);
            c.begin(t);
            const cache::Outputs& o = c.ctl.out();
            if (!o.fwr_valid) return false;
            status = o.fwr_status;
            return true;
        };
        port.tick = [](void* x, int64_t t) { static_cast<CacheSystem*>(x)->begin(t); };
        updater_axi.sys = this; updater_lookup.sys = this; rw.sys = this;
    }
    CacheSystem(const CacheSystem&) = delete;
    CacheSystem& operator=(const CacheSystem&) = delete;

    void attach(tracer::Tracer& tr) { tr.fill_port = &port; tracer_ = &tr; }
    tracer::FileStore* store() const { return store_; }

    // ---- the cycle (header)
    // Takes every edge before t: the posted cycle's with its clients' outputs, any later one with every client idle.
    void begin(int64_t t) {
        if (t <= edged_) FATAL("cache system: a client's step of %lld after the edge of %lld (the clients' order in a cycle)", (long long)t, (long long)edged_);
        if (edged_ == INT64_MIN) { edged_ = t - 1; post_t_ = t; return; }
        while (edged_ < t - 1) {
            const int64_t c = edged_ + 1;
            if (c != post_t_) {
                if (!idle()) FATAL("cache system: cycle %lld with no client stepped while it works (a client asleep with something in flight)", (long long)c);
                clear_posted();
            }
            edge(c);
            edged_ = c;
            clear_posted();
        }
        post_t_ = t;
    }
    // A bench's eager cycle (test_cache): it has set `in` and `clients` for t itself; the edge of t is taken now. The fill
    // port's two requests are cleared after it (a request is handed over at its handshake); a look-up port's request is
    // the bench's to hold.
    void tick(int64_t t) {
        begin(t);
        edge(t);
        edged_ = t;
        in.fr.valid = false; in.fw.valid = false;
    }
    // A harness's end of a run, after the last cycle it ran: the edge of that cycle, so that every unit's state is whole.
    void finish() { if (edged_ != INT64_MIN && post_t_ > edged_) begin(post_t_ + 1); }
    bool idle() const { return ctl.idle() && arb.idle() && lpddr.idle(); }
    // The chip's reset (17.11; 23.6.10, 23.11): the cache empty, the arbiter empty, the model's bursts gone. LPDDR's
    // contents are kept (18: a peripheral's memories are kept).
    void reset() {
        ctl.reset(); arb.reset(); lpddr.reset();
        clear_posted();
        for (auto& p : pending_writes_) p.valid = false;
        for (bool& b : lk_outstanding_) b = false;
        for (auto& u : upd_) u.valid = false;
    }

    // ---- the clients' posts of a cycle (after begin(t))
    void post_axi(int64_t t, unsigned c, const axi::MasterOut& m) {
        if (t != post_t_) FATAL("cache system: client %u's AXI outputs of %lld posted in cycle %lld", c, (long long)t, (long long)post_t_);
        if (c == axi::CACHE || c >= axi::CLIENTS) FATAL("cache system: AXI client %u", c);
        clients[c] = m;
    }
    void post_lookup(int64_t t, unsigned p, const lookup::Request& r) {
        if (t != post_t_) FATAL("cache system: look-up port %u's request of %lld posted in cycle %lld", p, (long long)t, (long long)post_t_);
        if (p >= lookup::CLIENTS) FATAL("cache system: look-up port %u", p);
        in.req[p] = r;
    }

    // ---- the units, and their inputs of the cycle being posted
    cache::CacheCtl ctl;
    AxiArbiter arb;
    LpddrModel lpddr;
    tracer::Tracer::FillPort port;
    cache::Inputs in;
    axi::MasterOut clients[axi::CLIENTS];

    // ---- the Updater's ports (units/updater_top.h): the arbiter's client 1 and look-up port 1
    struct UpdaterAxi final : updctl::AxiDev {
        CacheSystem* sys = nullptr;
        unsigned client = axi::UPDATER;            // planted bugs move it (tests/joined_teeth.sh)
        void begin(int64_t t) override { sys->begin(t); }
        axi::SlaveOut out() const override { return sys->arb.client_out(client); }
        void edge(int64_t t, const axi::MasterOut& m) override { sys->post_axi(t, client, m); }
        bool quiet() const override { return sys->idle(); }
        uint64_t violations() const override { return sys->joint_violations(); }
    } updater_axi;
    struct UpdaterLookup final : updctl::LookupDev {
        CacheSystem* sys = nullptr;
        unsigned port = lookup::UPDATER;
        void begin(int64_t t) override { sys->begin(t); }
        bool ready() const override { return sys->ctl.out().ready[port]; }
        lookup::Reply reply() const override {
            lookup::Reply r = sys->ctl.out().reply;
            if (r.valid && r.client != port) r.valid = false;   // the shared bus: another client's reply is not this one's
            else if (r.valid) r.client = lookup::UPDATER;
            return r;
        }
        void edge(int64_t t, const lookup::Request& r) override { sys->post_lookup(t, port, r); }
        bool quiet() const override { return sys->idle(); }
        uint64_t violations() const override { return 0; }   // counted once, on the AXI side
    } updater_lookup;
    // ---- the RW engine's port (rw_engine.h's LpddrSlave): the arbiter's client 2
    struct RwJoint final : LpddrSlave {
        CacheSystem* sys = nullptr;
        unsigned client = axi::RW_ENGINE;
        RwEngineOnArbiter a;
        LpddrIn cycle(int64_t t, const LpddrOut& o) override {
            sys->begin(t);
            a.in = sys->arb.client_out(client);
            const LpddrIn r = a.cycle(t, o);
            sys->post_axi(t, client, a.out);
            return r;
        }
    } rw;

    // ---- the joint's checks (header)
    enum Check : unsigned { J_UPDATE_BEFORE_WRITE = 0, J_WRITE_CACHED_PAGE, J_READ_DIRTY_PAGE, J_UPDATE_NOT_IN_LPDDR, J_HANDSHAKE, J_CHECKS };
    uint64_t joint[J_CHECKS] = {};
    uint64_t joint_violations() const { uint64_t n = 0; for (uint64_t v : joint) n += v; return n; }
    static const char* joint_name(unsigned k) {
        static const char* n[J_CHECKS] = {"an update naming a page whose write is not answered", "a direct write to a page the cache holds a line of",
                                          "a direct read of a page the cache holds a dirty line of", "an update's reply with LPDDR not holding the entry",
                                          "a look-up's handshake out of turn (two outstanding, or a reply to none)"};
        return k < J_CHECKS ? n[k] : "?";
    }
    // ---- the overlap of the Updater and the Tracer (WORKER9's figures)
    uint64_t both_outstanding_cycles = 0;          // cycles with a look-up of port 1 and a fill request both outstanding
    uint64_t lookups = 0, updates = 0;             // port 1's requests taken

private:
    void clear_posted() {
        in.fr.valid = false; in.fw.valid = false;
        for (auto& r : in.req) r = lookup::Request{};
        for (unsigned c = 1; c < axi::CLIENTS; c++) clients[c] = axi::MasterOut{};
    }
    void edge(int64_t t) {
        checks(t);
        clients[axi::CACHE] = ctl.out().axi;
        const axi::MasterOut m = arb.controller_out();
        const axi::SlaveOut s = lpddr.out();
        in.axi = arb.client_out(axi::CACHE);
        ctl.edge(t, in);
        lpddr.edge(t, m);
        arb.edge(t, clients, s);
    }

    // The joint's checks of cycle t, from every output of t (before the edge). The Updater's side only: client 1, port 1.
    struct Range { bool valid = false; uint64_t lo = 0, hi = 0; };
    static constexpr unsigned MAX_W = 16;
    Range pending_writes_[MAX_W];                  // the Updater's write bursts from AW's handshake to B
    bool lk_outstanding_[lookup::CLIENTS] = {};
    struct Upd { bool valid = false; lookup::Request r; } upd_[lookup::CLIENTS];   // the update a port has outstanding
    void checks(int64_t) {
        const unsigned C = axi::UPDATER, P = lookup::UPDATER;
        const axi::MasterOut& m = clients[C];
        const axi::SlaveOut& s = arb.client_out(C);
        const cache::Outputs& o = ctl.out();
        if (m.awvalid && s.awready) {
            const uint64_t lo = m.awaddr, hi = m.awaddr + 16ull * (m.awlen + 1u);
            bool put = false;
            for (auto& p : pending_writes_) if (!p.valid) { p.valid = true; p.lo = lo; p.hi = hi; put = true; break; }
            if (!put) FATAL("cache system: more than %u of the Updater's writes outstanding", MAX_W);
            for (uint64_t a = lo; a < hi; a += 128) if (ctl.holds(int64_t(a >> 7))) { joint[J_WRITE_CACHED_PAGE]++; break; }
        }
        if (s.bvalid) {                            // a burst's one response, the oldest first (AXI: in order for one ID)
            for (auto& p : pending_writes_) if (p.valid) { p.valid = false; break; }
        }
        if (m.arvalid && s.arready) {
            const uint64_t lo = m.araddr, hi = m.araddr + 16ull * (m.arlen + 1u);
            for (uint64_t a = lo; a < hi; a += 128) if (ctl.holds_dirty(int64_t(a >> 7))) { joint[J_READ_DIRTY_PAGE]++; break; }
        }
        const lookup::Request& r = in.req[P];
        if (r.valid && o.ready[P]) {
            if (lk_outstanding_[P]) joint[J_HANDSHAKE]++;
            lk_outstanding_[P] = true;
            if (r.op == lookup::UPDATE) {
                updates++;
                upd_[P].valid = true; upd_[P].r = r;
                if (r.entry != lookup::NO_PAGE) {
                    const uint64_t a = uint64_t(r.entry & lookup::ADDRESS) * 128;
                    for (const auto& p : pending_writes_) if (p.valid && a < p.hi && a + 4096 > p.lo) { joint[J_UPDATE_BEFORE_WRITE]++; break; }
                }
            } else lookups++;
        }
        if (o.reply.valid && o.reply.client < lookup::CLIENTS) {
            const unsigned k = o.reply.client;
            if (k == P) {
                if (!lk_outstanding_[k]) joint[J_HANDSHAKE]++;
                lk_outstanding_[k] = false;
                if (upd_[k].valid && o.reply.status == lookup::OK && store_ && !lpddr_holds_entry(upd_[k].r)) joint[J_UPDATE_NOT_IN_LPDDR]++;
                upd_[k].valid = false;
            }
        }
        if (lk_outstanding_[P] && (tracer_fill_outstanding())) both_outstanding_cycles++;
    }
    bool tracer_fill_outstanding() const { return tracer_ && tracer_->fill_outstanding(); }
    // Whether LPDDR holds the update's entry: walked from the root through the store's pages (the simulator's view).
    bool lpddr_holds_entry(const lookup::Request& r) const {
        int32_t page = int32_t(r.root & lookup::ADDRESS);
        for (unsigned level = 0; level < r.depth; level++) {
            const uint64_t* p = store_->raw_page(page);
            if (!p) return false;
            const unsigned i = unsigned(r.page >> (10 * (r.depth - 1 - level))) & 1023;
            const uint32_t e = tracer::FileStore::entry_of(p, int(i));
            if (level == r.level) return e == r.entry;
            if (e == lookup::NO_PAGE) return false;
            page = int32_t(e & lookup::ADDRESS);
        }
        return false;
    }

    tracer::FileStore* store_ = nullptr;
    int64_t edged_ = INT64_MIN, post_t_ = INT64_MIN;
    const tracer::Tracer* tracer_ = nullptr;
};

}  // namespace units
