// engine.cpp: the cycle loop (WORKER3, stage 2; sim.h's THE CYCLE). Deliver, run by level, end. No STL, no allocation, no
// virtual calls: a switch per wheel (not per entry) picks the delivery loop of its (kind, width).
#include "sim.h"

#include <cstdio>

namespace sim {

namespace {
template <template <unsigned> class F>
void by_width(unsigned w, Engine& e, const Bucket& b) {
    switch (w) {
        case 0: F<0>::go(e, b); break;
        case 1: F<1>::go(e, b); break;
        case 2: F<2>::go(e, b); break;
        default: F<3>::go(e, b); break;
    }
}
template <unsigned W> struct DLink { static void go(Engine& e, const Bucket& b) { detail::deliver_link<W>(e, b); } };
template <unsigned W> struct DFifo { static void go(Engine& e, const Bucket& b) { if constexpr (W) detail::deliver_fifo<W>(e, b); } };
template <unsigned W> struct DHold { static void go(Engine& e, const Bucket& b) { if constexpr (W) detail::deliver_hold<W>(e, b); } };
template <unsigned W> struct DCredit { static void go(Engine& e, const Bucket& b) { if constexpr (W) detail::deliver_credit<W>(e, b); } };
template <unsigned W> struct DReg { static void go(Engine& e, const Bucket& b) { if constexpr (W) detail::deliver_reg<W>(e, b); } };
}  // namespace

void Engine::cycle() {
    // 1. deliver t: each wheel's bucket, then the far wakes due
    for (uint32_t w = 0; w < nwheels; w++) {
        Bucket& b = wheels[w].buckets[now & wheels[w].mask];
        if (b.empty()) continue;
        switch (wheels[w].kind) {
            case Kind::link: by_width<DLink>(wheels[w].width, *this, b); break;
            case Kind::fifo: by_width<DFifo>(wheels[w].width, *this, b); break;
            case Kind::hold: by_width<DHold>(wheels[w].width, *this, b); break;
            case Kind::credit: by_width<DCredit>(wheels[w].width, *this, b); break;
            case Kind::reg: by_width<DReg>(wheels[w].width, *this, b); break;
            case Kind::cret: detail::deliver_cret(*this, b); break;
            default: FATAL("wheel of kind %u: not in this increment", unsigned(wheels[w].kind));
        }
        pending -= b.count();
        b.clear();
    }
    while (heap_n && wake_at[heap[0]] == now) mark(heap_pop());
    // 2. run, level by level (runs are sorted by level)
    for (uint32_t r = 0; r < nruns; r++)
        if (runs[r].nready) { cur_level = runs[r].level; runs[r].fn(*this, runs[r]); }
    // 3. end: the next cycle's run sets become current
    for (uint32_t r = 0; r < nruns; r++) {
        RunSet& rs = runs[r];
        Bitmap t = rs.ready; rs.ready = rs.next; rs.next = t;
        rs.nready = rs.nnext;
        rs.nnext = 0;
    }
    now++;
}

bool Engine::idle() const {
    if (pending) return false;
    for (uint32_t r = 0; r < nruns; r++) if (runs[r].nready) return false;
    return true;
}

void Engine::run_until(Cycle end) {
    while (now < end) {
        cycle();
        if constexpr (kDebug) if (leak_every && npools && now % leak_every == 0) check_leaks();
        if (idle()) {                                  // nothing in flight, nothing ready: jump to the next wake
            Cycle t = heap_n ? wake_at[heap[0]] : end;
            if (t > end) t = end;
            if (t > now) now = t;
        }
    }
    if constexpr (kDebug) if (npools) check_leaks();
}

// The leak check (debug): a slot in use is on a port (visible in a ring or a hold, or an entry in a wheel) or held by a
// unit, within what the units declared (Ports::by_ref's `held`, and one per by-reference Hold for a refused word awaiting
// its retry). More than that is a slot allocated and never put or freed.
void Engine::check_leaks() const {
    uint64_t queued[64] = {};
    if (npools > 64) FATAL("more than 64 size classes");
    for (uint32_t i = 0; i < nlinks; i++) if (links[i].cls != kNoClass) queued[links[i].cls] += links[i].ring.count();
    for (uint32_t i = 0; i < nfifos; i++) if (fifos[i].cls != kNoClass) queued[fifos[i].cls] += fifos[i].ring.count();
    for (uint32_t i = 0; i < nholds; i++) if (holds[i].cls != kNoClass) queued[holds[i].cls] += holds[i].occ;
    for (uint32_t w = 0; w < nwheels; w++) {
        const Wheel& wh = wheels[w];
        if (wh.kind != Kind::link && wh.kind != Kind::fifo && wh.kind != Kind::credit && wh.kind != Kind::hold) continue;
        for (uint32_t b = 0; b <= wh.mask; b++) {
            const Bucket& bk = wh.buckets[b];
            const Word* p = bk.begin();
            for (uint32_t k = 0; k < bk.count(); k++, p += wh.width + 1) {
                uint16_t c = wh.kind == Kind::link ? links[p[0]].cls : wh.kind == Kind::hold ? holds[p[0]].cls : fifos[p[0]].cls;
                if (c != kNoClass) queued[c]++;
            }
        }
    }
    for (uint32_t c = 0; c < npools; c++) {
        const uint64_t used = pools[c].in_use();
        if (used > queued[c] + pool_allow[c])
            FATAL("slot pool '%s' leaks at cycle %llu: %llu slots in use, %llu on its ports, at most %llu held by units: %llu "
                  "allocated and never put or freed", pools[c].name(), (unsigned long long)now, (unsigned long long)used,
                  (unsigned long long)queued[c], (unsigned long long)pool_allow[c], (unsigned long long)(used - queued[c] - pool_allow[c]));
    }
}

void Engine::after(RunSet& r, uint32_t k, Wake w) {
    const uint32_t i = r.first + k;
    steps[i]++;
    mask[i] &= ~edges[i];
    const Cycle at = w.at;
    if (at == Wake::kNever) { if (heap_pos[i] != kNone) heap_remove(i); return; }
    if (__builtin_expect(at <= now, 0)) die_wake(i, at);
    if (at == now + 1) {
        if (heap_pos[i] != kNone) heap_remove(i);
        if (!r.next.test(k)) { r.next.set(k); r.nnext++; }
        return;
    }
    heap_set(i, at);
}

// ---- the far wakes: a binary min-heap of instances by (wake_at, instance), each instance at most once -------------------
namespace {
inline bool before(const Engine& e, uint32_t a, uint32_t b) {
    return e.wake_at[a] < e.wake_at[b] || (e.wake_at[a] == e.wake_at[b] && a < b);
}
void sift_up(Engine& e, uint32_t p) {
    uint32_t x = e.heap[p];
    while (p) {
        uint32_t q = (p - 1) / 2;
        if (!before(e, x, e.heap[q])) break;
        e.heap[p] = e.heap[q]; e.heap_pos[e.heap[p]] = p; p = q;
    }
    e.heap[p] = x; e.heap_pos[x] = p;
}
void sift_down(Engine& e, uint32_t p) {
    uint32_t x = e.heap[p];
    for (;;) {
        uint32_t c = 2 * p + 1;
        if (c >= e.heap_n) break;
        if (c + 1 < e.heap_n && before(e, e.heap[c + 1], e.heap[c])) c++;
        if (!before(e, e.heap[c], x)) break;
        e.heap[p] = e.heap[c]; e.heap_pos[e.heap[p]] = p; p = c;
    }
    e.heap[p] = x; e.heap_pos[x] = p;
}
}  // namespace

void Engine::heap_set(uint32_t i, Cycle at) {
    if (heap_pos[i] == kNone) {
        wake_at[i] = at;
        heap[heap_n] = i; heap_pos[i] = heap_n; heap_n++;
        sift_up(*this, heap_pos[i]);
        return;
    }
    const Cycle old = wake_at[i];
    wake_at[i] = at;
    if (at < old) sift_up(*this, heap_pos[i]); else sift_down(*this, heap_pos[i]);
}
void Engine::heap_remove(uint32_t i) {
    uint32_t p = heap_pos[i];
    heap_pos[i] = kNone;
    wake_at[i] = kNoCycle;
    if (--heap_n == p) return;
    heap[p] = heap[heap_n]; heap_pos[heap[p]] = p;
    sift_up(*this, p);
    sift_down(*this, heap_pos[heap[p]] == p ? p : heap_pos[heap[p]]);
}
uint32_t Engine::heap_pop() {
    uint32_t i = heap[0];
    heap_remove(i);
    return i;
}

void Engine::die_two_puts(uint32_t route) const {
    FATAL("two puts on '%s' in cycle %llu (one put per output per cycle)", route_names[route], (unsigned long long)now);
}
void Engine::die_no_credit(uint32_t route) const {
    FATAL("put on '%s' at cycle %llu without a credit (a credit fifo's producer puts only with room)", route_names[route],
          (unsigned long long)now);
}
void Engine::die_wire(uint32_t w, const char* what) const {
    FATAL("wire of %s %s, cycle %llu", unit_names[wires[w].producer], what, (unsigned long long)now);
}
void Engine::die_link_full(uint32_t port) const {
    FATAL("link '%s' full at cycle %llu: capacity %u (the consumer must take each word as it arrives)", links[port].ring.name(),
          (unsigned long long)now, unsigned(links[port].cap));
}
void Engine::die_wake(uint32_t i, Cycle at) const {
    FATAL("unit '%s' asked to be woken at cycle %llu, at or before the current cycle %llu", unit_names[i],
          (unsigned long long)at, (unsigned long long)now);
}

}  // namespace sim
