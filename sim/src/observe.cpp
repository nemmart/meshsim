// observe.cpp: the port observer (WORKER10; W8's request for the canonical fingerprint, AUDIT-M2 6). Between runs only:
// reads one input port of a built machine, with what is still in the wheels, into the caller's buffer. The configuration
// regime by its place (outside the loop), but allocation-free: a fingerprint per cycle calls it for every bus of a tile.
#include "config_impl.h"
#include "internals.h"
#include "threads.h"

namespace sim {

namespace {

using SImpl = detail::Internals::SImpl;

// The domain machine holding the unit whose handle is at `h` (one domain: the machine itself).
const SImpl& owner_of(const SImpl* s, const void* h, const char* what) {
    if (s->running) FATAL("Sim::observe(%s) during a run: between runs only", what);
    if (!s->threads) return *s;
    const unsigned char* a = static_cast<const unsigned char*>(h);
    for (const auto& d : s->threads->doms)
        for (const auto& st : d.S->stores) {
            const size_t sz = d.S->types[st.type]->size;
            if (a >= st.base && a < st.base + sz * st.count) return *d.S;
        }
    FATAL("Sim::observe(%s): the handle is not inside a unit of this machine (pass the unit's own member: unit<T>(name).port)", what);
}

void put_entry(PortEntry* out, uint32_t max, uint32_t& n, Cycle at, const Word* w, unsigned width, const char* what) {
    if (n >= max) FATAL("Sim::observe(%s): more than %u entries (the buffer's size)", what, max);
    PortEntry& e = out[n++];
    e.at = at;
    for (unsigned k = 0; k < 3; k++) e.w[k] = k < width ? w[k] : 0;
}

// The entry for input `dest` in the bucket of cycle `t` (now <= t <= now + the wheel's mask: each wheel over its own
// window, so no bucket is read twice) of every wheel of kind `k` and width `width`, if any.
const Word* in_bucket(const Engine& e, Kind k, unsigned width, uint32_t dest, Cycle t) {
    for (uint32_t wi = 0; wi < e.nwheels; wi++) {
        const Wheel& wh = e.wheels[wi];
        if (wh.kind != k || wh.width != width || t > e.now + wh.mask) continue;
        const Bucket& b = wh.buckets[t & wh.mask];
        const Word* p = b.begin();
        for (uint32_t m = b.count(); m; m--, p += width + 1) if (p[0] == dest) return p + 1;
    }
    return nullptr;
}

// The put made at c (now - 1) into a Fifo, credit Fifo, Hold or Reg: the entry in the bucket of now.
void pending(const Engine& e, Kind k, unsigned width, uint32_t dest, PortView& v) {
    if (const Word* w = in_bucket(e, k, width, dest, e.now)) {
        v.put = true;
        for (unsigned i = 0; i < 3; i++) v.put_w[i] = i < width ? w[i] : 0;
    }
}

}  // namespace

PortView Sim::observe(const LinkIn& p, PortEntry* out, uint32_t max) const {
    const SImpl& s = owner_of(impl_, &p, "link");
    const Engine& e = s.e;
    const LinkPort& l = e.links[p.idx];
    PortView v;
    if (l.ring.count() && !e.link_at)
        FATAL("Sim::observe(link '%s'): words visible and no arrival cycles kept (build with Options::observe_arrivals)",
              e.link_names[p.idx]);
    for (unsigned k = 0; k < l.ring.count(); k++)
        put_entry(out, max, v.n, e.link_at[e.link_at_off[p.idx] + l.ring.slot(k)], l.ring.peek(k), l.width, "link");
    // in flight: every wheel bucket from now on, in arrival order (one entry per port per bucket at most)
    uint32_t mask = 0;
    for (uint32_t wi = 0; wi < e.nwheels; wi++)
        if (e.wheels[wi].kind == Kind::link && e.wheels[wi].width == l.width && e.wheels[wi].mask > mask) mask = e.wheels[wi].mask;
    // W10: a crossing Link's words may also sit in the source's outbox (direct delivery), translated by its xdst
    for (uint32_t i = 0; i < e.nxin; i++) if (e.xin[i].width == l.width && e.xin[i].mask > mask) mask = e.xin[i].mask;
    uint32_t found = 0;
    for (Cycle t = e.now; t <= e.now + mask; t++) {
        const Word* w = in_bucket(e, Kind::link, l.width, p.idx, t);
        for (uint32_t i = 0; !w && i < e.nxin; i++) {
            const Engine::XIn& x = e.xin[i];
            if (x.width != l.width || t > e.now + x.mask) continue;   // each outbox over its own window: no bucket twice
            const Bucket& b = x.buckets[t & x.mask];
            const Word* q = b.begin();
            for (uint32_t m = b.count(); m; m--, q += x.width + 1u) if (x.xdst[q[0]] == p.idx) { w = q + 1; break; }
        }
        if (w) { put_entry(out, max, v.n, t, w, l.width, "link"); found++; }
    }
    if (found != e.link_inflight(p.idx))
        FATAL("Sim::observe(link '%s'): %u words in flight, %u found in the wheels", e.link_names[p.idx], e.link_inflight(p.idx), found);
    return v;
}

PortView Sim::observe(const FifoIn& p, PortEntry* out, uint32_t max) const {
    const SImpl& s = owner_of(impl_, &p, "fifo");
    const Engine& e = s.e;
    const FifoPort& f = e.fifos[p.idx];
    PortView v;
    for (unsigned k = 0; k < f.ring.count(); k++) put_entry(out, max, v.n, 0, f.ring.peek(k), f.width, "fifo");
    pending(e, f.ret == kNone ? Kind::fifo : Kind::credit, f.width, p.idx, v);
    return v;
}

PortView Sim::observe(const HoldIn& p, PortEntry* out, uint32_t max) const {
    const SImpl& s = owner_of(impl_, &p, "hold");
    const Engine& e = s.e;
    const HoldPort& h = e.holds[p.idx];
    PortView v;
    if (h.occ) { v.valid = true; put_entry(out, max, v.n, 0, h.w, h.width, "hold"); }
    pending(e, Kind::hold, h.width, p.idx, v);
    return v;
}

PortView Sim::observe(const RegIn& p, PortEntry* out, uint32_t max) const {
    const SImpl& s = owner_of(impl_, &p, "reg");
    const Engine& e = s.e;
    const RegPort& r = e.regs[p.idx];
    PortView v;
    if (r.valid) { v.valid = true; put_entry(out, max, v.n, 0, r.w, r.width, "reg"); }
    pending(e, Kind::reg, r.width, p.idx, v);
    return v;
}

}  // namespace sim
