// engine.h: the framework's internals (WORKER3, stage 2). Not for unit authors: sim.h includes it at its end so that
// Ctx's operations inline into each unit's step. Everything here is the cycle loop's: plain arrays carved from one block
// per thread, W1's structures, no STL, no allocation, no virtual calls.
//
// Increment 2 (NOTES.md, stage 2): Link, Fifo and Hold, the wheels, the run sets, wakes. Reg, Wire, RegFile, Count and
// the credit Fifo follow in increment 3 (their Ctx operations are declared in sim.h, defined then).
#pragma once

#include <cstdint>

#include "base/bitmap.h"
#include "base/bucket.h"
#include "base/fatal.h"
#include "base/ring.h"

namespace sim {

enum class Kind : uint8_t { link, fifo, credit, hold, reg, wire, regfile, count };
inline constexpr unsigned kKinds = 8;
inline constexpr Cycle kNoCycle = ~Cycle(0);
inline constexpr uint32_t kNone = ~0u;

// One per Link input: the visible words (a ring as deep as the capacity) and the count in the wheel.
struct LinkPort {
    Ring ring;
    Cycle last_take;
    uint32_t owner;           // the consumer's instance
    uint32_t inflight;        // entries in the wheel for this port
    uint16_t cap;             // in flight + visible (WordDelayLine: L + 2)
    uint8_t bit, width;
};

// One per Fifo input.
struct FifoPort {
    Ring ring;
    Cycle last_take;          // the cycle of the last take: the occupancy at the start of t is count + (last_take == t)
    Cycle first_overflow;     // the put cycle of the first overflow, kNoCycle if none
    uint64_t overflows;
    uint32_t owner, count_idx;
    uint16_t depth;
    uint8_t bit, width;
};

// One per Hold input.
struct HoldPort {
    Word w[3];
    Cycle last_take, refused_at;
    uint64_t refusals;
    uint32_t owner, producer;
    uint8_t bit, pbit, width;
    bool occ;
};

// The resolved route of an output: where a put's entry is appended (config.h). A data value: the same code puts locally
// or across a domain.
struct Route {
    Bucket* wheel;            // the first of the wheel's buckets
    Cycle last_put;           // one put per output per cycle (fatal)
    uint32_t mask;            // wheel size - 1
    uint32_t dest;            // the destination's pool index: an entry's first word
    uint32_t owner;           // the producer's instance (checks)
    uint16_t latency;
    uint8_t width;
    Kind kind;
};

struct Wheel {
    Bucket* buckets;
    uint32_t mask;
    Kind kind;
    uint8_t width;
};

struct Engine;

// A run: the ready instances of one (level, type, form), contiguous instance ids [first, first + count).
struct RunSet {
    void (*fn)(Engine&, RunSet&);
    unsigned char* units;     // the type's arena, at this run's first unit
    uint32_t first, count, level, nready, nnext;
    uint16_t type;
    Form form;
    Bitmap ready, next;       // this cycle's, the next cycle's
};

struct Engine {
    Cycle now = 1;
    Ctx ctx;
    // per instance
    uint64_t* mask = nullptr;     // ready bits: levels (an input holds data) and this cycle's edges
    uint64_t* edges = nullptr;    // which of the instance's bits are edges (cleared after each step)
    Cycle* wake_at = nullptr;     // a far wake's cycle (in the heap), else kNoCycle
    uint32_t* run_of = nullptr;
    uint32_t* heap_pos = nullptr; // the instance's place in the heap, kNone if absent
    uint32_t* heap = nullptr;     // the far wakes: a binary min-heap of instances by (wake_at, instance)
    uint64_t* steps = nullptr;
    uint32_t ninst = 0, heap_n = 0;
    // pools
    LinkPort* links = nullptr;
    FifoPort* fifos = nullptr;
    HoldPort* holds = nullptr;
    Route* routes = nullptr;
    Wheel* wheels = nullptr;
    RunSet* runs = nullptr;
    uint32_t nwheels = 0, nruns = 0, nlinks = 0, nfifos = 0, nholds = 0, nroutes = 0;
    uint64_t pending = 0;         // entries in the wheels (fast-forward when 0 and nothing is ready)
    const char* const* unit_names = nullptr;   // for messages
    const char* const* route_names = nullptr;

    void mark(uint32_t i) {
        RunSet& r = runs[run_of[i]];
        uint32_t k = i - r.first;
        if (!r.ready.test(k)) { r.ready.set(k); r.nready++; }
    }
    void mark_bit(uint32_t i, unsigned bit) { mask[i] |= uint64_t(1) << bit; mark(i); }

    template <unsigned W>
    Word* append(uint32_t route) {
        Route& r = routes[route];
        DCHECK(r.owner == ctx.cur_, "put on '%s' by %s, not its owner", route_names[route], unit_names[ctx.cur_]);
        DCHECK(r.width == W, "put of %u words on '%s', declared width %u", W, route_names[route], r.width);
        if (__builtin_expect(r.last_put == now, 0)) die_two_puts(route);
        r.last_put = now;
        Word* e = r.wheel[(now + r.latency) & r.mask].append();
        e[0] = r.dest;
        pending++;
        return e + 1;
    }

    void bind() { ctx.e_ = this; }
    void enter(RunSet& r, uint32_t k) { ctx.cur_ = r.first + k; ctx.mask_ = &mask[r.first + k]; }
    void after(RunSet& r, uint32_t k, Wake w);
    void cycle();
    void run_until(Cycle end);
    bool idle() const;

    void heap_set(uint32_t i, Cycle at);
    void heap_remove(uint32_t i);
    uint32_t heap_pop();

    [[noreturn]] void die_two_puts(uint32_t route) const;
    [[noreturn]] void die_link_full(uint32_t port) const;
    [[noreturn]] void die_wake(uint32_t i, Cycle at) const;
};

// ---- Ctx's operations ---------------------------------------------------------------------------------------------
inline Cycle Ctx::now() const { return e_->now; }
inline Wake Ctx::next() const { return Wake{e_->now + 1}; }
inline Wake Ctx::in(Cycle n) const { return Wake{e_->now + n}; }
inline uint64_t Ctx::mask() const { return *mask_; }

// Link
inline bool Ctx::ready(LinkIn p) const { return *mask_ >> p.bit & 1; }
inline const Word* Ctx::peek(LinkIn p) const {
    const LinkPort& l = e_->links[p.idx];
    DCHECK(l.owner == cur_ && !l.ring.empty(), "peek of an empty or foreign link (%s)", e_->unit_names[cur_]);
    return l.ring.peek(0);
}
inline void Ctx::take(LinkIn p) {
    LinkPort& l = e_->links[p.idx];
    DCHECK(l.owner == cur_ && !l.ring.empty() && l.last_take != e_->now, "take of an empty or foreign link, or a second take (%s)",
           e_->unit_names[cur_]);
    l.last_take = e_->now;
    l.ring.pop();
    if (l.ring.empty()) *mask_ &= ~(uint64_t(1) << p.bit);
}
namespace detail {
inline LinkPort& link_put_check(Engine* e, LinkOut p) {
    LinkPort& l = e->links[e->routes[p.route].dest];
    if (__builtin_expect(l.ring.count() + l.inflight >= l.cap, 0)) e->die_link_full(e->routes[p.route].dest);
    l.inflight++;
    return l;
}
}  // namespace detail
inline void Ctx::put(LinkOut p) { detail::link_put_check(e_, p); e_->append<0>(p.route); }
inline void Ctx::put(LinkOut p, Word a) { detail::link_put_check(e_, p); Word* w = e_->append<1>(p.route); w[0] = a; }
inline void Ctx::put(LinkOut p, Word a, Word b) {
    detail::link_put_check(e_, p); Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b;
}
inline void Ctx::put(LinkOut p, Word a, Word b, Word c) {
    detail::link_put_check(e_, p); Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c;
}

// Fifo
inline bool Ctx::ready(FifoIn p) const { return *mask_ >> p.bit & 1; }
inline const Word* Ctx::peek(FifoIn p, unsigned k) const {
    const FifoPort& f = e_->fifos[p.idx];
    DCHECK(f.owner == cur_ && k < f.ring.count(), "peek(%u) of a fifo holding %u, or foreign (%s)", k, f.ring.count(),
           e_->unit_names[cur_]);
    return f.ring.peek(k);
}
inline void Ctx::take(FifoIn p) {
    FifoPort& f = e_->fifos[p.idx];
    DCHECK(f.owner == cur_ && !f.ring.empty() && f.last_take != e_->now, "take of an empty or foreign fifo, or a second take (%s)",
           e_->unit_names[cur_]);
    f.last_take = e_->now;
    f.ring.pop();
    if (f.ring.empty()) *mask_ &= ~(uint64_t(1) << p.bit);
}
inline unsigned Ctx::size(FifoIn p) const { return e_->fifos[p.idx].ring.count(); }
inline unsigned Ctx::size_at_start(FifoIn p) const {
    const FifoPort& f = e_->fifos[p.idx];
    return f.ring.count() + (f.last_take == e_->now);
}
inline void Ctx::put(FifoOut p, Word a) { Word* w = e_->append<1>(p.route); w[0] = a; }
inline void Ctx::put(FifoOut p, Word a, Word b) { Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b; }
inline void Ctx::put(FifoOut p, Word a, Word b, Word c) { Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c; }

// Hold
inline bool Ctx::ready(HoldIn p) const { return *mask_ >> p.bit & 1; }
inline const Word* Ctx::peek(HoldIn p) const {
    const HoldPort& h = e_->holds[p.idx];
    DCHECK(h.owner == cur_ && h.occ, "peek of an empty or foreign hold (%s)", e_->unit_names[cur_]);
    return h.w;
}
inline void Ctx::take(HoldIn p) {
    HoldPort& h = e_->holds[p.idx];
    DCHECK(h.owner == cur_ && h.occ, "take of an empty or foreign hold (%s)", e_->unit_names[cur_]);
    h.occ = false;
    h.last_take = e_->now;
    *mask_ &= ~(uint64_t(1) << p.bit);
}
inline void Ctx::put(HoldOut p, Word a) { Word* w = e_->append<1>(p.route); w[0] = a; }
inline void Ctx::put(HoldOut p, Word a, Word b) { Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b; }
inline void Ctx::put(HoldOut p, Word a, Word b, Word c) { Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c; }
inline bool Ctx::refused(HoldOut p) const { return e_->holds[e_->routes[p.route].dest].refused_at == e_->now - 1; }
inline bool Ctx::can_put(HoldOut p) const {
    const HoldPort& h = e_->holds[e_->routes[p.route].dest];
    return !h.occ && h.last_take != e_->now;
}
inline bool Ctx::held_at_start(HoldOut p) const {
    const HoldPort& h = e_->holds[e_->routes[p.route].dest];
    return h.occ || h.last_take == e_->now;
}

// ---- the delivery loops, one per (kind, width) (D16) ---------------------------------------------------------------
namespace detail {
template <unsigned W>
void deliver_link(Engine& e, const Bucket& b) {
    const Word* p = b.begin();
    for (uint32_t n = b.count(); n; n--, p += W + 1) {
        LinkPort& l = e.links[p[0]];
        Word* d = l.ring.push();
        if constexpr (W == 0) d[0] = 0;
        for (unsigned k = 0; k < W; k++) d[k] = p[1 + k];
        l.inflight--;
        e.mark_bit(l.owner, l.bit);
    }
}
template <unsigned W>
void deliver_fifo(Engine& e, const Bucket& b) {
    const Word* p = b.begin();
    const Cycle put_at = e.now - 1;
    for (uint32_t n = b.count(); n; n--, p += W + 1) {
        FifoPort& f = e.fifos[p[0]];
        if (f.ring.count() + (f.last_take == put_at) >= f.depth) {      // full at the start of the put's cycle (WordFIFO)
            f.overflows++;
            if (f.first_overflow == kNoCycle) f.first_overflow = put_at;
            continue;
        }
        Word* d = f.ring.push();
        for (unsigned k = 0; k < W; k++) d[k] = p[1 + k];
        e.mark_bit(f.owner, f.bit);
    }
}
template <unsigned W>
void deliver_hold(Engine& e, const Bucket& b) {
    const Word* p = b.begin();
    for (uint32_t n = b.count(); n; n--, p += W + 1) {
        HoldPort& h = e.holds[p[0]];
        if (h.occ) {                                                     // not taken at the put's cycle: refused
            h.refused_at = e.now - 1;
            h.refusals++;
            e.mark_bit(h.producer, h.pbit);
            continue;
        }
        for (unsigned k = 0; k < W; k++) h.w[k] = p[1 + k];
        h.occ = true;
        e.mark_bit(h.owner, h.bit);
    }
}
}  // namespace detail

}  // namespace sim
