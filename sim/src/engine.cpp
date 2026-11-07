// engine.cpp: the cycle loop (WORKER3, stage 2; sim.h's THE CYCLE). Deliver, run by level, end. No STL, no allocation, no
// virtual calls: a switch per wheel (not per entry) picks the delivery loop of its (kind, width).
#include "sim.h"

#include <cstdio>

#include <unistd.h>

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

namespace {
inline void deliver_wheel(Engine& e, const Wheel& wh, Bucket& b) {
    switch (wh.kind) {
        case Kind::link: by_width<DLink>(wh.width, e, b); break;
        case Kind::fifo: by_width<DFifo>(wh.width, e, b); break;
        case Kind::hold: by_width<DHold>(wh.width, e, b); break;
        case Kind::credit: by_width<DCredit>(wh.width, e, b); break;
        case Kind::reg: by_width<DReg>(wh.width, e, b); break;
        case Kind::cret: detail::deliver_cret(e, b); break;
        default: FATAL("wheel of kind %u", unsigned(wh.kind));
    }
    e.pending -= b.count();
    b.clear();
}
}  // namespace

// One cycle. Its fixed cost does not grow with the machine: delivery visits the wheels with entries due (one mask word),
// the run phase the runs with ready instances, the end of the cycle the runs with next-cycle requests.
[[gnu::always_inline]] inline void Engine::cycle() {
    // 1. deliver t: the wheels with entries due, then the near and far wakes due
    if (due_ok) {
        uint64_t m = due[now & 63];
        due[now & 63] = 0;
        while (m) {
            const uint32_t w = uint32_t(__builtin_ctzll(m));
            m &= m - 1;
            Bucket& b = wheels[w].buckets[now & wheels[w].mask];
            if (!b.empty()) deliver_wheel(*this, wheels[w], b);
        }
    } else if (pending) {
        for (uint32_t w = 0; w < nwheels; w++) {
            Bucket& b = wheels[w].buckets[now & wheels[w].mask];
            if (!b.empty()) deliver_wheel(*this, wheels[w], b);
        }
    }
    if (near_busy >> (now & (kNear - 1)) & 1) near_slot();
    while (heap_n && wake_at[heap[0]] == now) mark(heap_pop());
    if (step_all)                                      // the missed-wake check: every instance, every cycle
        for (uint32_t r = 0; r < nruns; r++) {
            RunSet& rs = runs[r];
            for (uint32_t k = 0; k < rs.count; k++) RunSet::set(rs.ready(), k);
            rs.nready = rs.count;
            run_ready[r >> 6] |= uint64_t(1) << (r & 63);
        }
    // 2. run, level by level (runs are numbered in level order; a wire's change marks only higher runs, so the masks are
    //    read again as they go)
    if (shuffle || instance_major) {
        for (uint32_t r = 0; r < nruns;) run_level_debug(r);
        for (uint32_t w = 0; w < nrw; w++) run_ready[w] = 0;
    } else {
        for (uint32_t w = 0; w < nrw; w++)
            while (uint64_t m = run_ready[w]) {
                const uint32_t r = (w << 6) + uint32_t(__builtin_ctzll(m));
                run_ready[w] = m & (m - 1);
                if constexpr (kDebug) cur_level = runs[r].level;
                runs[r].fn(*this, runs[r]);
            }
    }
    if constexpr (kDebug) if (detector) check_suspects();
    // 3. end: the runs with next-cycle requests make them current (the others' ready sets are already empty)
    for (uint32_t w = 0; w < nrw; w++) {
        uint64_t m = run_next[w];
        run_next[w] = 0;
        run_ready[w] = m;
        while (m) {
            RunSet& rs = runs[(w << 6) + uint32_t(__builtin_ctzll(m))];
            m &= m - 1;
            rs.cur ^= 1;                               // next becomes ready; the old ready set is already empty
            rs.nready = rs.nnext;
            rs.nnext = 0;
        }
    }
    now++;
}

bool Engine::idle() const {
    if (pending) return false;
    for (uint32_t w = 0; w < nrw; w++) if (run_ready[w]) return false;
    return true;
}

// The near wakes of this cycle's slot: an instance whose wake is still this cycle is marked (a later step may have moved
// its wake: its old bit is then stale, and ignored).
void Engine::near_slot() {
    const unsigned s = unsigned(now & (kNear - 1));
    uint64_t* bits = near_bits + size_t(s) * niw;
    uint64_t* sum = near_sum + size_t(s) * nsw;
    for (uint32_t q = 0; q < nsw; q++) {
        uint64_t sm = sum[q];
        sum[q] = 0;
        while (sm) {
            const uint32_t wi = (q << 6) + uint32_t(__builtin_ctzll(sm));
            sm &= sm - 1;
            uint64_t m = bits[wi];
            bits[wi] = 0;
            while (m) {
                const uint32_t i = (wi << 6) + uint32_t(__builtin_ctzll(m));
                m &= m - 1;
                if (wake_at[i] == now && heap_pos[i] == kNone) { wake_at[i] = kNoCycle; mark(i); }
            }
        }
    }
    near_busy &= ~(1u << s);
}

Cycle Engine::next_wake() const {
    Cycle t = heap_n ? wake_at[heap[0]] : kNoCycle;
    if (near_busy) {
        const unsigned s = unsigned(now & (kNear - 1));
        const uint32_t rot = ((near_busy >> s) | (near_busy << (kNear - s))) & ((1u << kNear) - 1);
        const Cycle n = now + unsigned(__builtin_ctz(rot));
        if (n < t) t = n;
    }
    return t;
}

bool Engine::asleep(uint32_t i) const {
    const RunSet& r = runs[run_of[i]];
    return !RunSet::test(r.next(), i - r.first) && wake_at[i] == kNoCycle;
}

// Runs cycles now, now + 1, ..., end: through `end` inclusive (config.h, Sim::run_until).
void Engine::run_until(Cycle end) {
    while (now <= end) {
        cycle();
        if (__builtin_expect(stop_at != kNoCycle, 0) && stop_breaks) break;   // W10: a unit asked to stop after this cycle
        if constexpr (kDebug) if (leak_every && npools && now % leak_every == 0) check_leaks();
        if (!step_all && idle()) {                     // nothing in flight, nothing ready: jump to the next wake
            Cycle t = next_wake();
            if constexpr (kDebug) if (detector && nsuspects) { const Cycle d = suspect_deadline(); if (d < t) t = d; }
            if (t > end + 1) t = end + 1;
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

// A wake 2 cycles ahead or more (after()'s rare path): the near wheel, or the heap.
void Engine::after_far(uint32_t i, Cycle at) {
    if (__builtin_expect(at <= now, 0)) die_wake(i, at);
    if (at - now < kNear) { wake_at[i] = at; near_set(i, at); return; }
    heap_set(i, at);
}

// The detector's bookkeeping (debug): asleep with data waiting makes a suspect, judged at the end of the cycle.
void Engine::note_suspect(uint32_t i, Cycle at) {
    if (!detector) return;
    if (at == Wake::kNever && (mask[i] & ~edges[i]) && missed_after[i]) {
        if (suspect_since[i] == kNoCycle) { suspect_since[i] = now; suspects[nsuspects++] = i; }
    } else {
        suspect_since[i] = kNoCycle;                   // dropped from the list at the end of the cycle
    }
}

// ---- the debug orders: one level's ready instances in instance-major or a seeded order, stepped one by one ----------
void Engine::run_level_debug(uint32_t& r) {
    const uint32_t level = runs[r].level, first = r;
    uint32_t n = 0;
    for (; r < nruns && runs[r].level == level; r++) {
        RunSet& rs = runs[r];
        if (rs.nready)
            for (uint32_t wi = 0; wi < rs.nwords; wi++)
                for (uint64_t m = rs.ready()[wi]; m; m &= m - 1) scratch[n++] = rs.first + (wi << 6) + uint32_t(__builtin_ctzll(m));
    }
    if (instance_major) {                              // by add order (each instance's units together): a shell sort, in place
        for (uint32_t gap = n / 2; gap; gap /= 2)
            for (uint32_t a = gap; a < n; a++)
                for (uint32_t b = a; b >= gap && add_order[scratch[b - gap]] > add_order[scratch[b]]; b -= gap) {
                    uint32_t x = scratch[b]; scratch[b] = scratch[b - gap]; scratch[b - gap] = x;
                }
    }
    if (shuffle) {                                     // Fisher-Yates with splitmix64
        for (uint32_t a = n; a > 1; a--) {
            uint64_t z = (shuffle += 0x9e3779b97f4a7c15ull);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            z ^= z >> 31;
            uint32_t b = uint32_t(z % a);
            uint32_t x = scratch[a - 1]; scratch[a - 1] = scratch[b]; scratch[b] = x;
        }
    }
    cur_level = level;
    for (uint32_t j = 0; j < n; j++) {
        const uint32_t i = scratch[j];
        RunSet& rs = runs[run_of[i]];
        rs.one(*this, rs, i - rs.first);
    }
    for (uint32_t q = first; q < r; q++) { for (uint32_t wi = 0; wi < runs[q].nwords; wi++) runs[q].ready()[wi] = 0; runs[q].nready = 0; }
}

// ---- the missed-wake detector (D20; debug builds) ------------------------------------------------------------------
// A unit asleep (no step next cycle, no far wake) with data waiting in an input is fine only while something already on
// its way will step it: a delivery to one of its inputs (in a wheel), a credit output with no credit left (the consumer
// holds them all, and each take returns one: a return in the wheel, or one to come; if it never takes, that is a
// deadlock, not a missed wake), or a Hold put it made this cycle (a refusal at t + 1 wakes it). Anything else is reported once it has lasted the unit's
// threshold. Not seen: a wait on a Wire's change (sim.h).
// The jump of an idle machine must not skip a suspect's judgement (W4's finding): the earliest cycle at whose end a
// suspect would be reported. On an idle machine nothing is in flight, so only a credit output with no credit left can keep
// a suspect fine, and it stays so until some unit steps (which ends the idleness): such suspects set no deadline.
Cycle Engine::suspect_deadline() const {
    Cycle d = kNoCycle;
    for (uint32_t k = 0; k < nsuspects; k++) {
        const uint32_t i = suspects[k];
        if (suspect_since[i] == kNoCycle || on_its_way(i)) continue;
        const Cycle due = suspect_since[i] + missed_after[i] - 1;   // check_suspects reports at the end of this cycle
        if (due < d) d = due;
    }
    return d;
}

bool Engine::on_its_way(uint32_t i) const {
    for (uint32_t w = watch_first[i]; w < watch_first[i + 1]; w++) {
        const Watch& x = watch[w];
        if (x.kind == Kind::link) { if (links[x.idx].inflight) return true; continue; }
        if (x.kind == Kind::hold && x.name == nullptr) { if (routes[x.idx].last_put == now) return true; continue; }   // a Hold output
        if (x.kind == Kind::cret && fifos[x.idx].credits == 0) return true;   // no credit left: the consumer's takes return them
        Kind wk = x.kind == Kind::cret ? Kind::cret : x.kind;       // fifo, credit or hold inputs; cret: a credit output's returns
        for (uint32_t h = 0; h < nwheels; h++) {
            const Wheel& wh = wheels[h];
            if (wh.kind != wk) continue;
            for (uint32_t b = 0; b <= wh.mask; b++) {
                const Bucket& bk = wh.buckets[b];
                const Word* p = bk.begin();
                for (uint32_t k = 0; k < bk.count(); k++, p += wh.width + 1) if (p[0] == x.idx) return true;
            }
        }
    }
    return false;
}
// The suspect's description, formatted into diag[] (D24: no allocation in the run, diagnostics included); a description
// longer than the buffer is cut, never grown.
const char* Engine::describe_suspect(uint32_t i) {
    size_t n = size_t(std::snprintf(diag, sizeof diag, "%s is asleep with data waiting in", unit_names[i]));
    for (uint32_t w = watch_first[i]; w < watch_first[i + 1] && n < sizeof diag; w++) {
        const Watch& x = watch[w];
        if (!x.name) continue;
        bool data = false;
        if (x.kind == Kind::link) data = !links[x.idx].ring.empty();
        else if (x.kind == Kind::fifo || x.kind == Kind::credit) data = !fifos[x.idx].ring.empty();
        else if (x.kind == Kind::hold) data = holds[x.idx].occ;
        if (data) n += size_t(std::snprintf(diag + n, sizeof diag - n, " %s", x.name));
    }
    return diag;
}
void Engine::check_suspects() {
    uint32_t w = 0;
    for (uint32_t k = 0; k < nsuspects; k++) {
        const uint32_t i = suspects[k];
        if (suspect_since[i] == kNoCycle) continue;
        if (!asleep(i) || !(mask[i] & ~edges[i])) { suspect_since[i] = kNoCycle; continue; }
        if (on_its_way(i)) { suspect_since[i] = now + 1; suspects[w++] = i; continue; }
        if (now + 1 - suspect_since[i] >= missed_after[i]) {
            const char* d = describe_suspect(i);
            if (missed_fatal)
                FATAL("missed wake at cycle %llu: %s, and nothing on its way will step it (no wake asked, no delivery, credit or "
                      "refusal due); a unit that leaves data in an input returns next() (sim.h, WAKES)", (unsigned long long)now, d);
            char line[sizeof diag + 64];                // report and go on: one write(2) of a formatted line (no stdio, no heap)
            int len = std::snprintf(line, sizeof line, "missed wake at cycle %llu: %s\n", (unsigned long long)now, d);
            if (len > int(sizeof line) - 1) len = int(sizeof line) - 1;
            if (len > 0 && ::write(2, line, size_t(len)) < 0) {}
            suspect_since[i] = kNoCycle;               // reported: not again until it steps and sleeps on data again
            continue;
        }
        suspects[w++] = i;
    }
    nsuspects = w;
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
    FATAL("link '%s' full at cycle %llu: capacity %u (in flight and visible: the latency plus the input's declared depth, 2 unless declared)", link_names[port],
          (unsigned long long)now, unsigned(links[port].cap));
}
void Engine::die_wake(uint32_t i, Cycle at) const {
    FATAL("unit '%s' asked to be woken at cycle %llu, at or before the current cycle %llu", unit_names[i],
          (unsigned long long)at, (unsigned long long)now);
}

}  // namespace sim
