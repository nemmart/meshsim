// test_cache: the cache's controller alone (src/units/cache_ctl.h; ../v1.4_wip/planning/CACHE-INTERFACE.md, "CI"), on
// the arbiter and the LPDDR model (src/units/cache_system.h), with test drivers on its look-up ports and its fill
// port: every operation and every case of a reply, on a miss and on a hit; a miss, an eviction, a write-back; a
// discard of a page with clean, dirty and in-flight lines; an update beside a fill of the same line; the update's
// reply after its write's response; fill's read after a write accepted before it; LPDDR errors of every kind; the
// handshake's rules; and random traffic from several clients and the fill port against the stand-in with no cache
// (tests/cache_ref.h): every reply equal throughout and LPDDR equal at the end. Each in both steps (hits_under_miss off and on) where it matters.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cache_figures.h"
#include "cache_ref.h"
#include "harness.h"
#include "units/cache_system.h"

using units::CacheSystem;
using units::CacheSystemConfig;
using units::tracer::FileStore;
namespace axi = units::axi;
namespace lookup = units::lookup;
namespace cache = units::cache;
using cacheref::Answer;

namespace {

// ---- a world of files on a store, built by hand
struct World {
    FileStore fs;
    int32_t next = 32 * 16;
    int32_t stride = 32;                          // 512: every page in the same 32 sets, for evictions
    uint64_t seed;
    explicit World(uint64_t s = 0) : seed(s) {}
    int32_t page(bool index) {
        const int32_t a = next;
        next += stride;
        uint64_t pg[512];
        for (int i = 0; i < 512; i++) pg[i] = index ? ~uint64_t(0) : (uint64_t(a) << 20 ^ uint64_t(i) ^ seed);
        fs.write_page(a, pg);
        return a;
    }
    void set(int32_t pg, unsigned i, uint32_t e) {
        uint64_t p[512];
        fs.read_page(pg, p);
        FileStore::set_entry(p, int(i), e);
        fs.write_page(pg, p);
    }
    uint32_t get(int32_t pg, unsigned i) const {
        uint64_t p[512];
        fs.read_page(pg, p);
        return FileStore::entry_of(p, int(i));
    }
};
uint32_t own(int32_t a) { return uint32_t(a) | lookup::OWNED; }
uint32_t shared(int32_t a) { return uint32_t(a); }

// The directed world:
//   A, depth 2: root RA. RA[0] -> IA (owned): IA[0] -> DA0 (owned), IA[1] hole, IA[2] -> DS (shared), IA[40] -> DA1.
//     RA[1] -> IS (shared): IS[0] -> DS2 (owned bit set, but the path is not). RA[2] hole.
//   B, depth 1: root RB. RB[0] -> DB (owned); RB[5] -> DS (shared).
//   C, depth 3: RC[0] -> C1 -> C2 -> DC, all owned.
struct Directed : World {
    int32_t RA, IA, DA0, DA1, DS, IS, DS2, RB, DB, RC, C1, C2, DC;
    explicit Directed(uint64_t s = 0) : World(s) {
        RA = page(true); IA = page(true); DA0 = page(false); DA1 = page(false); DS = page(false); IS = page(true); DS2 = page(false);
        RB = page(true); DB = page(false); RC = page(true); C1 = page(true); C2 = page(true); DC = page(false);
        set(RA, 0, own(IA)); set(IA, 0, own(DA0)); set(IA, 2, shared(DS)); set(IA, 40, own(DA1));
        set(RA, 1, shared(IS)); set(IS, 0, own(DS2));
        set(RB, 0, own(DB)); set(RB, 5, shared(DS));
        set(RC, 0, own(C1)); set(C1, 0, own(C2)); set(C2, 7, own(DC));
    }
};

// ---- the drivers
struct PortDriver {
    unsigned port = 0;
    std::vector<lookup::Request> reqs;
    struct Got { lookup::Reply r; int64_t taken = -1, replied = -1; };
    std::vector<Got> got;
    size_t next = 0;
    bool waiting = false;
    bool done() const { return next == reqs.size() && !waiting; }
};
struct FillOp {
    bool write = false;
    int32_t root = 0;
    uint8_t depth = 0;
    int64_t line = 0;
    uint64_t data[16] = {};
    // results
    uint8_t status = 0xFF;
    uint64_t got[16] = {};
    int64_t taken = -1, answered = -1;
};
struct FillDriver {
    std::vector<FillOp> ops;
    size_t next = 0;
    bool waiting = false;
    bool done() const { return next == ops.size() && !waiting; }
};

struct Bench {
    CacheSystem sys;
    PortDriver ports[lookup::CLIENTS];
    FillDriver fill;
    int64_t t = 0;
    unsigned replies_seen = 0;
    Bench(FileStore* fs, const CacheSystemConfig& c = {}) : sys(fs, c) { for (unsigned p = 0; p < lookup::CLIENTS; p++) ports[p].port = p; }

    void cycle() {
        // the drivers' outputs of t
        for (PortDriver& d : ports) {
            lookup::Request& r = sys.in.req[d.port];
            if (!d.waiting && d.next < d.reqs.size()) { r = d.reqs[d.next]; r.valid = true; }
            else r.valid = false;
        }
        bool fr = false, fw = false;
        if (!fill.waiting && fill.next < fill.ops.size()) {
            const FillOp& o = fill.ops[fill.next];
            if (o.write) { cache::FillWrite& w = sys.in.fw; w.valid = true; w.root = o.root; w.depth = o.depth; w.line = o.line; std::memcpy(w.data, o.data, 128); fw = true; }
            else { cache::FillRead& r = sys.in.fr; r.valid = true; r.root = o.root; r.depth = o.depth; r.line = o.line; fr = true; }
        }
        const cache::Outputs o = sys.ctl.out();          // the cache's outputs of t
        lookup::Request offered[lookup::CLIENTS];
        for (unsigned p = 0; p < lookup::CLIENTS; p++) offered[p] = sys.in.req[p];
        sys.tick(t);
        // what moved in t
        if (o.reply.valid) {
            replies_seen++;
            PortDriver& d = ports[o.reply.client];
            if (!d.waiting) test::fail(__FILE__, __LINE__, "a reply to port %u, which has nothing outstanding, at %lld", unsigned(o.reply.client), (long long)t);
            else { d.got.back().r = o.reply; d.got.back().replied = t; d.waiting = false; }
        }
        for (PortDriver& d : ports)
            if (offered[d.port].valid && o.ready[d.port]) { d.got.push_back({{}, t, -1}); d.next++; d.waiting = true; }
        if (o.frr_valid || o.fwr_valid) {
            FillOp& op = fill.ops[fill.next - 1];
            if (!fill.waiting || op.write != o.fwr_valid) test::fail(__FILE__, __LINE__, "a fill response out of turn at %lld", (long long)t);
            op.status = o.fwr_valid ? o.fwr_status : o.frr_status;
            if (o.frr_valid) std::memcpy(op.got, o.frr_data, 128);
            op.answered = t; fill.waiting = false;
        }
        if ((fr && o.fr_ready) || (fw && o.fw_ready)) { fill.ops[fill.next].taken = t; fill.next++; fill.waiting = true; }
        t++;
    }
    bool done() const {
        for (const PortDriver& d : ports) if (!d.done()) return false;
        return fill.done() && sys.idle();
    }
    void run(int64_t limit = 2000000) {
        const int64_t end = t + limit;
        while (!done() && t < end) cycle();
        CHECK(done());
    }
};

lookup::Request look(uint32_t root, unsigned depth, uint32_t page) {
    lookup::Request r; r.op = lookup::LOOK_UP; r.root = root; r.depth = uint8_t(depth); r.page = page; return r;
}
lookup::Request update(uint32_t root, unsigned depth, uint32_t page, unsigned level, uint32_t e) {
    lookup::Request r = look(root, depth, page); r.op = lookup::UPDATE; r.level = uint8_t(level); r.entry = e; return r;
}
lookup::Request discard(int32_t page) { lookup::Request r; r.op = lookup::DISCARD; r.root = uint32_t(page); return r; }
FillOp fread(int32_t root, unsigned depth, int64_t line) { FillOp o; o.root = root; o.depth = uint8_t(depth); o.line = line; return o; }
FillOp fwrite(int32_t root, unsigned depth, int64_t line, uint64_t tag) {
    FillOp o = fread(root, depth, line); o.write = true;
    for (int i = 0; i < 16; i++) o.data[i] = tag * 0x1000 + uint64_t(i);
    return o;
}
Answer ans(const lookup::Reply& r) { return {r.status, r.level, r.entry}; }

CacheSystemConfig step(bool two) { CacheSystemConfig c; c.cache.hits_under_miss = two; return c; }

}  // namespace

// ================================================================ every case of a look-up's reply, on a miss and a hit
TEST(cache_lookup_every_case) {
    for (bool two : {false, true}) {
        Directed w;
        Bench b(&w.fs, step(two));
        const lookup::Request rs[] = {
            look(w.RA, 2, 0),            // found
            look(w.RA, 2, 1),            // a hole at level 1
            look(w.RA, 2, 2),            // not owned at level 1
            look(w.RA, 2, 1024),         // not owned at level 0
            look(w.RA, 2, 2048),         // a hole at level 0
            look(w.RA, 2, 1u << 20),     // beyond
            look(w.RA, 0, 0),            // depth 0: beyond
            look(w.RB, 1, 0),            // found, depth 1
            look(w.RB, 1, 5),            // not owned at level 0, depth 1
            look(w.RC, 3, 7),            // found, depth 3
            look(w.RA, 2, 40),           // found, a second line of IA
        };
        const Answer expect[] = {
            {lookup::OK, 1, own(w.DA0)}, {lookup::HOLE, 1, lookup::NO_PAGE}, {lookup::NOT_OWNED, 1, shared(w.DS)},
            {lookup::NOT_OWNED, 0, shared(w.IS)}, {lookup::HOLE, 0, lookup::NO_PAGE}, {lookup::BEYOND, 0, 0}, {lookup::BEYOND, 0, 0},
            {lookup::OK, 0, own(w.DB)}, {lookup::NOT_OWNED, 0, shared(w.DS)}, {lookup::OK, 2, own(w.DC)}, {lookup::OK, 1, own(w.DA1)},
        };
        PortDriver& d = b.ports[lookup::UPDATER];
        for (const auto& r : rs) d.reqs.push_back(r);
        for (const auto& r : rs) d.reqs.push_back(r);     // again: now on hits
        b.run();
        const size_t n = sizeof rs / sizeof rs[0];
        for (size_t k = 0; k < 2 * n; k++) {
            CHECK(ans(d.got[k].r) == expect[k % n]);
            CHECK_EQ(unsigned(d.got[k].r.client), unsigned(lookup::UPDATER));
            CHECK(d.got[k].replied > d.got[k].taken);
        }
        // a hit of a depth-2 file's two index lines: under 10 cycles (CI section 7); its miss about 80 a line more
        CHECK(d.got[n].replied - d.got[n].taken < 10);
        CHECK(d.got[0].replied - d.got[0].taken > 150);
        CHECK_EQ(b.sys.ctl.misses[cache::CacheCtl::LOOKUP_INDEX], 7u);   // RA's line 0, IA's lines 0 and 1, RB's, RC's, C1's, C2's
                                                                         // (IS's never: the walk stops at RA[1], not owned)
        CHECK_EQ(b.sys.lpddr.unwritten_reads, 0u);
    }
}

// ================================================================ updates: every case; written through before the reply
TEST(cache_update_every_case) {
    for (bool two : {false, true}) {
        Directed w;
        const int32_t NEW = w.page(false);
        Bench b(&w.fs, step(two));
        PortDriver& d = b.ports[lookup::RW_ENGINE];
        d.reqs = {
            update(w.RA, 2, 1, 1, own(NEW)),            // IA[1], a hole, becomes NEW: ok, the entry before
            look(w.RA, 2, 1),                          // and is found
            update(w.RA, 2, 1024, 1, own(NEW)),        // the path's level 0 not owned: nothing written
            update(w.RA, 2, 2048, 1, own(NEW)),        // a hole at level 0
            update(w.RA, 2, 0, 2, own(NEW)),           // level >= depth: beyond
            update(w.RA, 2, 2, 1, own(NEW)),           // a shared entry at the level itself: replaced
            update(w.RA, 2, 2048, 0, own(w.IA)),       // level 0 itself
            look(w.RA, 2, 2048),                       // now through IA again: IA[0] -> DA0
        };
        const Answer expect[] = {{lookup::OK, 1, lookup::NO_PAGE}, {lookup::OK, 1, own(NEW)}, {lookup::NOT_OWNED, 0, shared(w.IS)},
                                 {lookup::HOLE, 0, lookup::NO_PAGE}, {lookup::BEYOND, 0, 0}, {lookup::OK, 1, shared(w.DS)},
                                 {lookup::OK, 0, lookup::NO_PAGE}, {lookup::OK, 1, own(w.DA0)}};
        // watch LPDDR at each reply: an update's entry is there when its reply is on the bus
        while (!b.done() && b.t < 100000) {
            const cache::Outputs o = b.sys.ctl.out();
            if (o.reply.valid && d.got.size() == 1) CHECK_EQ(w.get(w.IA, 1), own(NEW));
            if (o.reply.valid && d.got.size() == 6) CHECK_EQ(w.get(w.IA, 2), own(NEW));
            if (o.reply.valid && d.got.size() == 7) CHECK_EQ(w.get(w.RA, 2), own(w.IA));
            b.cycle();
        }
        CHECK(b.done());
        for (size_t k = 0; k < 8; k++) CHECK(ans(d.got[k].r) == expect[k]);
        CHECK_EQ(w.get(w.IS, 0), own(w.DS2));                    // the frozen file untouched
        // the reply comes after the write: a line's write, about 80 cycles, on top of the walk
        CHECK(d.got[0].replied - d.got[0].taken > 150 + 70);
        CHECK_EQ(b.sys.ctl.update_writes, 3u);
    }
}

// The reply waits for the write's response however long LPDDR takes; a look-up of the same line meanwhile sees the new
// entry; a second update waits for the first's reply.
TEST(cache_update_waits_for_its_response) {
    Directed w;
    const int32_t NEW = w.page(false), NEW2 = w.page(false);
    CacheSystemConfig c;
    c.lpddr.write_latency = 600;
    Bench b(&w.fs, c);
    b.ports[lookup::UPDATER].reqs = {look(w.RA, 2, 1), update(w.RA, 2, 1, 1, own(NEW))};
    b.run();
    const int64_t written_at = b.t;
    (void)written_at;
    PortDriver& u = b.ports[lookup::UPDATER];
    CHECK(u.got[1].replied - u.got[1].taken > 600);
    // now with others asking while one is in flight
    Bench b2(&w.fs, c);
    b2.ports[lookup::UPDATER].reqs = {look(w.RA, 2, 3), update(w.RA, 2, 3, 1, own(NEW2))};
    b2.ports[lookup::RW_ENGINE].reqs = {update(w.RB, 1, 1, 0, own(NEW))};
    b2.ports[0].reqs = {};
    // a looker that asks again and again while the update is in flight
    for (int k = 0; k < 300; k++) b2.ports[lookup::BINDER].reqs.push_back(look(w.RA, 2, 3));
    b2.run();
    PortDriver& up = b2.ports[lookup::UPDATER];
    PortDriver& rw = b2.ports[lookup::RW_ENGINE];
    PortDriver& lk = b2.ports[lookup::BINDER];
    CHECK(ans(up.got[1].r) == (Answer{lookup::OK, 1, lookup::NO_PAGE}));
    CHECK(ans(rw.got[0].r) == (Answer{lookup::OK, 0, lookup::NO_PAGE}));
    // one update in flight at a time: the two replies at least a write apart
    const int64_t a = up.got[1].replied, r = rw.got[0].replied;
    CHECK(a > r ? a - r > 600 : r - a > 600);
    // the looker saw the hole, then the new entry, never anything else; and saw the new one before the update's reply
    bool seen_new = false;
    int64_t first_new = -1;
    for (auto& g : lk.got) {
        const Answer x = ans(g.r);
        if (x == (Answer{lookup::OK, 1, own(NEW2)})) { if (!seen_new) first_new = g.replied; seen_new = true; }
        else { CHECK(!seen_new); CHECK(x == (Answer{lookup::HOLE, 1, lookup::NO_PAGE})); }
    }
    CHECK(seen_new);
    CHECK(first_new < up.got[1].replied);
}

// ================================================================ the fill port: reads and writes, every status
TEST(cache_fill_port) {
    for (bool two : {false, true}) {
        Directed w;
        Bench b(&w.fs, step(two));
        auto& ops = b.fill.ops;
        ops.push_back(fread(w.RA, 2, 5));                  // DA0's line 5: ok
        ops.push_back(fread(w.RA, 2, 32 + 3));             // IA[1]: a hole: zeros
        ops.push_back(fread(w.RA, 2, 64 + 1));             // IA[2]: DS, shared: not_owned with the line
        ops.push_back(fread(w.RA, 2, 1024 * 32 + 2));      // RA[1] shared: not_owned, the walk goes on: DS2's line
        ops.push_back(fread(w.RA, 2, int64_t(1) << 25));   // beyond
        ops.push_back(fread(w.DB, 0, 9));                  // depth 0: the root is the data page
        ops.push_back(fwrite(w.RA, 2, 6, 1));              // ok: dirty in the cache
        ops.push_back(fread(w.RA, 2, 6));                  // reads it back
        ops.push_back(fwrite(w.RA, 2, 64 + 1, 2));         // shared: error
        ops.push_back(fwrite(w.RA, 2, 32, 3));             // a hole: error
        ops.push_back(fwrite(w.RA, 2, 40 * 32 + 1, 4));    // DA1's line 1, not in the cache: allocated, no read
        b.run();
        auto line = [&](int32_t page, unsigned l, uint64_t* out) { uint64_t p[512]; w.fs.read_page(page, p); std::memcpy(out, p + 16 * l, 128); };
        uint64_t x[16];
        CHECK_EQ(ops[0].status, lookup::OK); line(w.DA0, 5, x); CHECK(std::memcmp(x, ops[0].got, 128) == 0);
        CHECK_EQ(ops[1].status, lookup::HOLE); for (uint64_t v : ops[1].got) CHECK_EQ(v, 0u);
        CHECK_EQ(ops[2].status, lookup::NOT_OWNED); line(w.DS, 1, x); CHECK(std::memcmp(x, ops[2].got, 128) == 0);
        CHECK_EQ(ops[3].status, lookup::NOT_OWNED); line(w.DS2, 2, x); CHECK(std::memcmp(x, ops[3].got, 128) == 0);
        CHECK_EQ(ops[4].status, lookup::BEYOND);
        CHECK_EQ(ops[5].status, lookup::OK); line(w.DB, 9, x); CHECK(std::memcmp(x, ops[5].got, 128) == 0);
        CHECK_EQ(ops[6].status, cache::FILL_WRITE_OK);
        CHECK(std::memcmp(ops[7].got, ops[6].data, 128) == 0);
        line(w.DA0, 6, x); CHECK(std::memcmp(x, ops[6].data, 128) != 0);   // dirty: not yet in LPDDR
        CHECK(b.sys.ctl.holds_dirty(w.DA0 + 6));
        CHECK_EQ(ops[8].status, cache::FILL_WRITE_ERROR);
        CHECK_EQ(ops[9].status, cache::FILL_WRITE_ERROR);
        CHECK_EQ(ops[10].status, cache::FILL_WRITE_OK);
        CHECK_EQ(b.sys.ctl.allocations, 2u);              // DA0's line 6 and DA1's line 1: neither read from LPDDR
        CHECK(b.sys.ctl.holds_dirty(w.DA1 + 1));
        CHECK(!b.sys.ctl.out().error);
    }
}

// ================================================================ fill's read after its write (23.6.4)
// fill's read and write on their own channels, each offered until its handshake, as the Tracer may; and look-ups,
// each offered until its handshake, to keep the walker busy and move its round robin.
namespace {
struct Overlap {
    CacheSystem sys;
    int64_t t = 0;
    lookup::Request req[lookup::CLIENTS];
    bool req_on[lookup::CLIENTS] = {};
    FillOp *fr = nullptr, *fw = nullptr;          // offered
    FillOp *frw = nullptr, *fww = nullptr;        // taken, not yet answered
    Overlap(FileStore* fs, const CacheSystemConfig& c) : sys(fs, c) {}
    void offer(unsigned p, const lookup::Request& r) { req[p] = r; req[p].valid = true; req_on[p] = true; }
    void cycle() {
        for (unsigned p = 0; p < lookup::CLIENTS; p++) { sys.in.req[p] = req_on[p] ? req[p] : lookup::Request{}; sys.in.req[p].valid = req_on[p]; }
        sys.in.fr.valid = fr != nullptr;
        if (fr) { sys.in.fr.root = fr->root; sys.in.fr.depth = fr->depth; sys.in.fr.line = fr->line; }
        sys.in.fw.valid = fw != nullptr;
        if (fw) { sys.in.fw.root = fw->root; sys.in.fw.depth = fw->depth; sys.in.fw.line = fw->line; std::memcpy(sys.in.fw.data, fw->data, 128); }
        const cache::Outputs o = sys.ctl.out();
        sys.tick(t);
        for (unsigned p = 0; p < lookup::CLIENTS; p++) if (req_on[p] && o.ready[p]) req_on[p] = false;
        if (o.frr_valid) {
            if (!frw) test::fail(__FILE__, __LINE__, "a fill read's response with none outstanding, at %lld", (long long)t);
            else { frw->status = o.frr_status; std::memcpy(frw->got, o.frr_data, 128); frw->answered = t; frw = nullptr; }
        }
        if (o.fwr_valid) {
            if (!fww) test::fail(__FILE__, __LINE__, "a fill write's response with none outstanding, at %lld", (long long)t);
            else { fww->status = o.fwr_status; fww->answered = t; fww = nullptr; }
        }
        if (fr && o.fr_ready) { fr->taken = t; frw = fr; fr = nullptr; }
        if (fw && o.fw_ready) { fw->taken = t; fww = fw; fw = nullptr; }
        t++;
    }
    template <class P> void until(P done, int64_t limit = 100000) {
        const int64_t end = t + limit;
        while (!done() && t < end) cycle();
        CHECK(done());
    }
    void run(int64_t limit = 100000) { until([&] { return !fr && !fw && !frw && !fww && sys.idle(); }, limit); }
};
// what the rule asks: the read taken after the write's handshake and before its response, and answered with its data
void check_read_after_write(const FillOp& w, const FillOp& r) {
    CHECK(w.taken < r.taken && r.taken < w.answered);            // the case: both in the cache's registers together
    CHECK_EQ(w.status, cache::FILL_WRITE_OK);
    CHECK_EQ(r.status, lookup::OK);
    CHECK(std::memcmp(r.got, w.data, 128) == 0);
    CHECK(r.answered > w.answered);
}
}  // namespace

// The walker busy with a look-up while the write is taken and the read the cycle after: both wait, and the round robin
// (from port 1's look-up) would come to the read first.
TEST(cache_fill_read_after_write) {
    for (bool two : {false, true}) {
        Directed w;
        Overlap b(&w.fs, step(two));
        FillOp in = fread(w.RB, 1, 0), wr = fwrite(w.RB, 1, 0, 2), rd = fread(w.RB, 1, 0);
        b.fr = &in;                                               // DB's line 0 in the cache, clean
        b.run();
        CHECK_EQ(in.status, lookup::OK);
        b.offer(1, look(w.RB, 1, 0));                             // a hit: the walker busy for some cycles
        b.cycle();
        b.fw = &wr;
        b.until([&] { return wr.taken >= 0; });
        b.fr = &rd;
        b.run();
        check_read_after_write(wr, rd);
    }
}

// The write and the read wait while the miss handler works on their set; another look-up, picked by the round robin
// meanwhile, leaves its pointer below the read's; at the miss handler's end the read would be served first.
TEST(cache_fill_read_after_a_waiting_write) {
    for (bool two : {false, true}) {
        World w;
        w.stride = 512;                                           // every page in the same sets
        const int32_t P = w.page(false), R1 = w.page(true), R2 = w.page(true);
        Overlap b(&w.fs, step(two));
        FillOp in = fread(P, 0, 0), wr = fwrite(P, 0, 0, 2), rd = fread(P, 0, 0);
        b.fr = &in;                                               // P's line 0 in the cache, clean
        b.run();
        CHECK_EQ(in.status, lookup::OK);
        b.offer(1, look(uint32_t(R1), 1, 0));                     // R1's line 0 misses: the miss handler, on P's set
        for (int k = 0; k < 6; k++) b.cycle();
        b.fw = &wr;
        b.until([&] { return wr.taken >= 0; });
        b.fr = &rd;
        b.until([&] { return rd.taken >= 0; });
        for (int k = 0; k < 6; k++) b.cycle();
        b.offer(2, look(uint32_t(R2), 1, 0));                     // picked by the round robin while the miss handler works
        b.run();
        check_read_after_write(wr, rd);
    }
}

// ================================================================ a miss, an eviction, a write-back
// Nine lines of one set written dirty: the ninth evicts the least recently used, written back first, in its place.
TEST(cache_eviction_and_write_back) {
    for (bool two : {false, true}) {
        World w;
        // a depth-1 file whose data pages are 16 KB apart: the same 32 sets
        const int32_t root = w.page(true);
        std::vector<int32_t> pages;
        for (int k = 0; k < 9; k++) {
            while ((w.next & 511) != (root + 32) % 512) w.next += 32;
            pages.push_back(w.page(false));
            w.set(root, unsigned(k), own(pages.back()));
        }
        Bench b(&w.fs, step(two));
        for (int k = 0; k < 9; k++) b.fill.ops.push_back(fwrite(root, 1, 32 * k + 3, uint64_t(100 + k)));
        b.fill.ops.push_back(fread(root, 1, 3));                     // the first: written back, read again from LPDDR
        b.run();
        for (auto& o : b.fill.ops) CHECK_EQ(o.status, 0u);
        CHECK_EQ(b.sys.ctl.write_backs, 2u);                          // the first line for the ninth, the second for the read
        uint64_t p[512];
        w.fs.read_page(pages[0], p);
        CHECK(std::memcmp(p + 48, b.fill.ops[0].data, 128) == 0);
        CHECK(std::memcmp(b.fill.ops[9].got, b.fill.ops[0].data, 128) == 0);
        // what is in LPDDR plus what is dirty in the cache is everything written
        b.sys.ctl.write_dirty_to(w.fs);
        for (int k = 0; k < 9; k++) { w.fs.read_page(pages[k], p); CHECK(std::memcmp(p + 48, b.fill.ops[k].data, 128) == 0); }
    }
}

// ================================================================ discard: clean, dirty and in-flight lines
TEST(cache_discard) {
    Directed w;
    const int32_t NEW = w.page(false);
    CacheSystemConfig c;
    c.lpddr.write_latency = 400;
    Bench b(&w.fs, c);
    uint64_t before[512];
    w.fs.read_page(w.DA0, before);
    b.fill.ops = {fread(w.RA, 2, 1), fread(w.RA, 2, 2), fwrite(w.RA, 2, 3, 7), fwrite(w.RA, 2, 4, 8)};   // clean and dirty lines of DA0
    b.run();
    CHECK(b.sys.ctl.holds(w.DA0 + 1) && b.sys.ctl.holds_dirty(w.DA0 + 3));
    // an update in flight on a line of IA, then a discard of IA's page and one of DA0's
    b.ports[lookup::UPDATER].reqs = {update(w.RA, 2, 1, 1, own(NEW))};
    for (int k = 0; k < 120; k++) b.cycle();                        // the update's write is in flight now
    CHECK(b.sys.ctl.holds(w.IA));
    b.ports[lookup::RW_ENGINE].reqs = {discard(w.DA0), discard(w.IA)};
    b.run();
    for (unsigned l = 0; l < 32; l++) { CHECK(!b.sys.ctl.holds(w.DA0 + l)); CHECK(!b.sys.ctl.holds(w.IA + l)); }
    uint64_t after[512];
    w.fs.read_page(w.DA0, after);
    CHECK(std::memcmp(before, after, sizeof before) == 0);          // the dirty lines never written
    CHECK_EQ(b.sys.ctl.discarded_dirty, 2u);
    // the page with the line in flight answered after the update's reply
    CHECK(b.ports[lookup::RW_ENGINE].got[1].replied > b.ports[lookup::UPDATER].got[0].replied);
    CHECK(b.ports[lookup::RW_ENGINE].got[0].replied < b.ports[lookup::UPDATER].got[0].replied);
    CHECK_EQ(w.get(w.IA, 1), own(NEW));
    CHECK(ans(b.ports[lookup::RW_ENGINE].got[1].r) == (Answer{lookup::OK, 0, 0}));
}

// ================================================================ an update beside a fill of the same line
// The fill walks IA's line while the update of an entry in it is in flight: it sees the new entry, from the cache.
TEST(cache_update_beside_a_fill) {
    Directed w;
    const int32_t NEW = w.page(false);
    CacheSystemConfig c;
    c.lpddr.write_latency = 400;
    Bench b(&w.fs, c);
    b.ports[lookup::UPDATER].reqs = {update(w.RA, 2, 1, 1, own(NEW))};
    for (int k = 0; k < 200; k++) b.cycle();                        // walked, written in the cache, its write in flight
    b.fill.ops = {fread(w.RA, 2, 32 + 9)};                          // page 1's line 9: through IA[1]
    b.run();
    CHECK_EQ(b.fill.ops[0].status, lookup::OK);
    uint64_t p[512];
    w.fs.read_page(NEW, p);
    CHECK(std::memcmp(b.fill.ops[0].got, p + 16 * 9, 128) == 0);
    CHECK(b.fill.ops[0].answered < b.ports[lookup::UPDATER].got[0].replied);
}

// ================================================================ LPDDR errors
TEST(cache_lpddr_errors) {
    Directed w;
    const int32_t NEW = w.page(false);
    // a look-up whose level-1 line fails; an update whose walk fails; an update whose write fails
    {
        Bench b(&w.fs);
        axi::LpddrFault f;
        f.first = uint64_t(w.IA) << 7; f.last = f.first + 127; f.reads = true; f.writes = false; f.resp = axi::SLVERR; f.bursts = 2;
        b.sys.lpddr.add_fault(f);
        b.ports[lookup::UPDATER].reqs = {look(w.RA, 2, 0), update(w.RA, 2, 0, 1, own(NEW)), look(w.RA, 2, 0)};
        b.run();
        auto& g = b.ports[lookup::UPDATER].got;
        CHECK(ans(g[0].r) == (Answer{lookup::LPDDR_ERROR, 1, 0}));
        CHECK(ans(g[1].r) == (Answer{lookup::LPDDR_ERROR, 1, 0}));
        CHECK(ans(g[2].r) == (Answer{lookup::OK, 1, own(w.DA0)}));   // the fault's count used: fetched now
        CHECK_EQ(b.sys.ctl.fetch_errors, 2u);
        CHECK(!b.sys.ctl.out().error);
    }
    {
        Bench b(&w.fs);
        axi::LpddrFault f;
        f.first = uint64_t(w.IA) << 7; f.last = f.first + 127; f.reads = false; f.writes = true; f.resp = axi::DECERR;
        b.sys.lpddr.add_fault(f);
        b.ports[lookup::UPDATER].reqs = {update(w.RA, 2, 0, 1, own(NEW)), look(w.RA, 2, 0)};
        b.run();
        auto& g = b.ports[lookup::UPDATER].got;
        CHECK(ans(g[0].r) == (Answer{lookup::LPDDR_ERROR, 1, own(w.DA0)}));
        CHECK(ans(g[1].r) == (Answer{lookup::OK, 1, own(NEW)}));     // the cache holds the new entry (CI section 2)
        CHECK_EQ(w.get(w.IA, 0), own(w.DA0));                       // LPDDR does not
    }
    // the fill port: a read whose line fails (beyond, the flag); a write whose walk fails (error); a write-back that fails
    {
        Bench b(&w.fs);
        axi::LpddrFault f;
        f.first = uint64_t(w.DA0) << 7; f.last = f.first + 127; f.reads = true; f.writes = false; f.resp = axi::SLVERR; f.bursts = 1;
        b.sys.lpddr.add_fault(f);
        b.fill.ops = {fread(w.RA, 2, 0)};
        b.run();
        CHECK_EQ(b.fill.ops[0].status, lookup::BEYOND);
        CHECK(b.sys.ctl.out().error);
    }
    {
        Directed w2;
        Bench b(&w2.fs);
        axi::LpddrFault f;
        f.first = uint64_t(w2.IA) << 7; f.last = f.first + 127; f.reads = true; f.writes = false; f.resp = axi::SLVERR; f.bursts = 1;
        b.sys.lpddr.add_fault(f);
        b.fill.ops = {fwrite(w2.RA, 2, 0, 1), fwrite(w2.RA, 2, 0, 2)};
        b.run();
        CHECK_EQ(b.fill.ops[0].status, cache::FILL_WRITE_ERROR);
        CHECK_EQ(b.fill.ops[1].status, cache::FILL_WRITE_OK);
        CHECK(!b.sys.ctl.out().error);
    }
    {
        World w3;
        const int32_t root = w3.page(true);
        std::vector<int32_t> pages;
        for (int k = 0; k < 9; k++) {
            while ((w3.next & 511) != (root + 32) % 512) w3.next += 32;
            pages.push_back(w3.page(false));
            w3.set(root, unsigned(k), own(pages.back()));
        }
        Bench b(&w3.fs);
        axi::LpddrFault f;
        f.first = uint64_t(pages[0]) << 7; f.last = f.first + 4095; f.reads = false; f.writes = true; f.resp = axi::SLVERR;
        b.sys.lpddr.add_fault(f);
        for (int k = 0; k < 9; k++) b.fill.ops.push_back(fwrite(root, 1, 32 * k, uint64_t(k)));
        b.run();
        for (auto& o : b.fill.ops) CHECK_EQ(o.status, cache::FILL_WRITE_OK);
        CHECK_EQ(b.sys.ctl.write_back_errors, 1u);
        CHECK(b.sys.ctl.out().error);
    }
}

// ================================================================ random traffic against the stand-in with no cache
// Three look-up clients, each on files of its own (one writer for a file), all sharing a frozen file; the fill port on
// a file of its own, reading and writing. Every reply equal to the stand-in's, request by request; LPDDR equal at the
// end, the cache's dirty lines swept out first. A phase of discards of scratch pages the fill port wrote, which no one
// touches afterwards (and which are left out of the comparison: their dirty lines were thrown away).
namespace {
struct RandomWorld : World {
    int32_t frozen_root, frozen_index;
    std::vector<int32_t> frozen_data;
    struct ClientFiles { int32_t root; std::vector<int32_t> index, data; };
    ClientFiles cf[lookup::CLIENTS];
    int32_t fill_root;
    std::vector<int32_t> fill_index, fill_data, scratch;
    explicit RandomWorld(uint64_t s) : World(s) {
        stride = s % 4 < 2 ? 512 : 32;
        test::Rng rng(s);
        frozen_root = page(true); frozen_index = page(true);
        for (int k = 0; k < 6; k++) { frozen_data.push_back(page(false)); set(frozen_index, unsigned(k), own(frozen_data.back())); }
        set(frozen_root, 0, own(frozen_index));
        for (unsigned c = 1; c < lookup::CLIENTS; c++) {
            ClientFiles& f = cf[c];
            f.root = page(true);
            for (int k = 0; k < 3; k++) f.index.push_back(page(true));
            for (int k = 0; k < 8; k++) f.data.push_back(page(false));
            for (int k = 0; k < 3; k++) set(f.root, unsigned(k * 3), own(f.index[k]));
            set(f.root, 20, shared(frozen_index));
            for (int k = 0; k < 8; k++) set(f.index[k % 3], unsigned(rng.below(64)), own(f.data[k]));
            set(f.index[0], 63, shared(frozen_data[0]));
        }
        fill_root = page(true);
        for (int k = 0; k < 2; k++) { fill_index.push_back(page(true)); set(fill_root, unsigned(k), own(fill_index[k])); }
        set(fill_root, 2, shared(frozen_index));
        for (int k = 0; k < 12; k++) { fill_data.push_back(page(false)); set(fill_index[k % 2], unsigned(k / 2), own(fill_data[k])); }
        set(fill_index[0], 10, shared(frozen_data[1]));
        for (int k = 0; k < 4; k++) { scratch.push_back(page(false)); set(fill_index[1], unsigned(20 + k), own(scratch[k])); }
    }
};

lookup::Request random_request(test::Rng& rng, const RandomWorld& w, unsigned c, bool discards) {
    const RandomWorld::ClientFiles& f = w.cf[c];
    const uint32_t pages[] = {0, 1, 3, 4, 6, 7, 9, 10, 63, 20 * 1024, 20 * 1024 + 2, 1500, 3000, 2 * 1024 + 5, 1u << 20};
    const uint32_t page = rng.below(4) == 0 ? uint32_t(rng.below(9 * 1024)) : pages[rng.below(sizeof pages / sizeof pages[0])];
    const unsigned r = unsigned(rng.below(discards ? 10 : 9));
    if (r < 5) return look(uint32_t(f.root), rng.below(8) == 0 ? unsigned(rng.below(2)) : 2, page);   // depth 0 or 1 now and then
    if (r < 9) {
        const unsigned level = unsigned(rng.below(2));
        uint32_t e;
        switch (rng.below(4)) {
            case 0: e = lookup::NO_PAGE; break;
            case 1: e = level == 0 ? own(f.index[rng.below(3)]) : own(f.data[rng.below(8)]); break;
            case 2: e = level == 0 ? shared(w.frozen_index) : shared(w.frozen_data[rng.below(6)]); break;
            default: e = level == 0 ? own(f.index[rng.below(3)]) : own(f.data[rng.below(8)]); break;
        }
        return update(uint32_t(f.root), 2, page, rng.below(16) == 0 ? 2 : level, e);
    }
    return discard(w.scratch[rng.below(w.scratch.size())]);
}
FillOp random_fill(test::Rng& rng, const RandomWorld& w, bool scratch_too) {
    int64_t line;
    switch (rng.below(6)) {
        case 0: line = int64_t(1024 + 1) * 32 + int64_t(rng.below(32)); break;   // root 1 -> fill_index[1]
        case 1: line = int64_t(2048) * 32 + int64_t(rng.below(32 * 6)); break;   // the frozen file: not owned
        case 2: line = int64_t(rng.below(1024 * 32)); break;                       // anywhere under index 0: holes and pages
        case 3: line = scratch_too ? (int64_t(1024 + 20 + rng.below(4)) * 32 + int64_t(rng.below(32))) : int64_t(rng.below(6 * 32)); break;
        case 4: line = int64_t(rng.below(6)) * 32 + int64_t(rng.below(32)); break;
        default: line = rng.below(16) == 0 ? (int64_t(1) << 25) + 5 : int64_t(rng.below(6)) * 32 + int64_t(rng.below(32)); break;
    }
    return rng.below(3) == 0 ? fwrite(w.fill_root, 2, line, rng.next()) : fread(w.fill_root, 2, line);
}
}  // namespace

TEST(cache_random_against_no_cache) {
    for (uint64_t seed = 1; seed <= 24; seed++) {
        RandomWorld w(seed), r(seed);                 // the cache's LPDDR, and the stand-in's
        cacheref::Ref ref(&r.fs);
        CacheSystemConfig c;
        c.cache.hits_under_miss = seed % 2 == 0;
        c.lpddr.jitter = seed % 3 != 0; c.lpddr.seed = uint32_t(seed);
        Bench b(&w.fs, c);
        test::Rng rng(seed * 977);
        // phase 1: the fill port writes the scratch pages, among its other traffic; phase 2: everything, with discards
        for (int phase = 0; phase < 2; phase++) {
            for (unsigned p = 1; p < lookup::CLIENTS; p++) {
                b.ports[p].reqs.clear(); b.ports[p].got.clear(); b.ports[p].next = 0;
                for (int k = 0; k < 150; k++) b.ports[p].reqs.push_back(random_request(rng, w, p, phase == 1));
            }
            b.fill.ops.clear(); b.fill.next = 0;
            for (int k = 0; k < 300; k++) b.fill.ops.push_back(random_fill(rng, w, phase == 0));
            b.run(4000000);
            // the stand-in, each client's requests in its order (they touch no file another writes)
            for (unsigned p = 1; p < lookup::CLIENTS; p++)
                for (size_t k = 0; k < b.ports[p].reqs.size(); k++) {
                    const Answer x = ref.request(b.ports[p].reqs[k]);
                    if (!(ans(b.ports[p].got[k].r) == x)) {
                        test::fail(__FILE__, __LINE__, "seed %llu phase %d port %u request %zu: the cache's %u/%u/%08x, the stand-in's %u/%u/%08x",
                                   (unsigned long long)seed, phase, p, k, b.ports[p].got[k].r.status, b.ports[p].got[k].r.level,
                                   b.ports[p].got[k].r.entry, x.status, x.level, x.entry);
                        return;
                    }
                }
            for (FillOp& o : b.fill.ops) {
                if (o.write) { CHECK_EQ(o.status, ref.fill_write(o.root, o.depth, o.line, o.data)); continue; }
                uint64_t x[16];
                const uint8_t st = ref.fill_read(o.root, o.depth, o.line, x);
                CHECK_EQ(o.status, st);
                if (st != lookup::BEYOND && std::memcmp(x, o.got, 128) != 0) {
                    test::fail(__FILE__, __LINE__, "seed %llu: a fill read of line %lld differs", (unsigned long long)seed, (long long)o.line);
                    return;
                }
            }
        }
        CHECK(!b.sys.ctl.out().error);
        CHECK_EQ(b.sys.lpddr.unwritten_reads, 0u);
        // LPDDR, with the cache's dirty lines swept out, equals the stand-in's, but for the discarded scratch pages
        b.sys.ctl.write_dirty_to(w.fs);
        bool same = true;
        w.fs.for_each_stored([&](int32_t a, const uint64_t* p) {
            for (int32_t s : w.scratch) if (a == s) return;
            uint64_t q[512];
            r.fs.read_page(a, q);
            if (std::memcmp(p, q, 4096) != 0) same = false;
        });
        CHECK(same);
        CHECK(b.sys.ctl.discards > 0 && b.sys.ctl.update_writes > 0 && b.sys.ctl.write_backs + b.sys.ctl.allocations > 0);
        if (std::getenv("CACHE_TEST_VERBOSE"))
            std::printf("seed %llu: updates %llu, discards %llu, write-backs %llu, allocations %llu, replays %llu, served under a miss %llu, "
                        "fill reads %llu, look-ups %llu, misses %llu/%llu/%llu, hits %llu/%llu/%llu\n",
                        (unsigned long long)seed, (unsigned long long)b.sys.ctl.updates, (unsigned long long)b.sys.ctl.discards,
                        (unsigned long long)b.sys.ctl.write_backs, (unsigned long long)b.sys.ctl.allocations, (unsigned long long)b.sys.ctl.replays,
                        (unsigned long long)b.sys.ctl.served_under_miss, (unsigned long long)b.sys.ctl.fill_reads, (unsigned long long)b.sys.ctl.lookups,
                        (unsigned long long)b.sys.ctl.misses[0], (unsigned long long)b.sys.ctl.misses[1], (unsigned long long)b.sys.ctl.misses[4],
                        (unsigned long long)b.sys.ctl.hits[0], (unsigned long long)b.sys.ctl.hits[1], (unsigned long long)b.sys.ctl.hits[4]);
        if (std::getenv("CACHE_TEST_VERBOSE")) cachefig::print(("seed " + std::to_string(seed)).c_str(), b.sys);
    }
}

// The handshake's rules: a port's ready is 0 from its handshake until the cycle after its reply; one reply a cycle.
TEST(cache_handshake_rules) {
    Directed w;
    Bench b(&w.fs);
    for (unsigned p = 0; p < lookup::CLIENTS; p++)
        for (int k = 0; k < 10; k++) b.ports[p].reqs.push_back(look(w.RA, 2, uint32_t(k)));
    while (!b.done() && b.t < 100000) {
        const cache::Outputs o = b.sys.ctl.out();
        for (unsigned p = 0; p < lookup::CLIENTS; p++) {
            if (b.ports[p].waiting) CHECK(!o.ready[p]);
            if (o.reply.valid && o.reply.client == p) CHECK(!o.ready[p]);
        }
        b.cycle();
    }
    CHECK(b.done());
    for (unsigned p = 0; p < lookup::CLIENTS; p++) CHECK_EQ(b.ports[p].got.size(), size_t(10));
}

// The figures of an idle cache (the fixed LPDDR model), printed for the report and held within bounds: a look-up of a
// depth-2 file on misses and on hits, an update whose path hits, a fill read on three misses and on hits, a fill write
// that hits and one that allocates.
TEST(cache_idle_figures) {
    Directed w;
    const int32_t NEW = w.page(false);
    Bench b(&w.fs);
    auto one_port = [&](const lookup::Request& r) {
        PortDriver& d = b.ports[lookup::UPDATER];
        d.reqs.push_back(r);
        b.run();
        return d.got.back().replied - d.got.back().taken;
    };
    auto one_fill = [&](const FillOp& o) {
        b.fill.ops.push_back(o);
        b.run();
        return b.fill.ops.back().answered - b.fill.ops.back().taken;
    };
    const int64_t look_miss = one_port(look(w.RA, 2, 0)), look_hit = one_port(look(w.RA, 2, 0));
    const int64_t update_hit = one_port(update(w.RA, 2, 1, 1, own(NEW)));
    const int64_t fill_miss = one_fill(fread(w.RC, 3, 7 * 32 + 4)), fill_hit = one_fill(fread(w.RC, 3, 7 * 32 + 4));
    const int64_t fwrite_hit = one_fill(fwrite(w.RC, 3, 7 * 32 + 4, 1)), fwrite_alloc = one_fill(fwrite(w.RC, 3, 7 * 32 + 5, 2));
    std::printf("an idle cache: a look-up of depth 2 on two misses %lld cycles, on hits %lld; an update on hits %lld; a fill read of depth 3 on "
                "four misses %lld, on hits %lld; a fill write on hits %lld, allocated %lld\n", (long long)look_miss, (long long)look_hit,
                (long long)update_hit, (long long)fill_miss, (long long)fill_hit, (long long)fwrite_hit, (long long)fwrite_alloc);
    CHECK(look_hit <= 8);
    CHECK(look_miss <= look_hit + 2 * 90);
    CHECK(update_hit <= look_hit + 90);
    CHECK(fill_hit <= 10);
    CHECK(fill_miss <= fill_hit + 4 * 90);
    CHECK(fwrite_hit <= 10);
    CHECK(fwrite_alloc <= 20);                        // the miss handler installs it, then the request is walked again
}
