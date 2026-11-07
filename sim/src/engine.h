// engine.h: the framework's internals (WORKER3, stage 2). Not for unit authors: sim.h includes it at its end so that
// Ctx's operations inline into each unit's step. Everything here is the cycle loop's: plain arrays carved from one block
// per thread, W1's structures, no STL, no allocation, no virtual calls.
//
// Every port kind of PORTS.md: Link, Fifo (and the credit Fifo), Hold, Reg, Wire, RegFile, Count. The reserved Link
// (WordBackpressureDelayLine) is M2's (PORTS.md 2a).
#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#include "base/bitmap.h"
#include "base/bucket.h"
#include "base/fatal.h"
#include "base/ring.h"
#include "base/value_pool.h"

namespace sim {

enum class Kind : uint8_t { link, fifo, credit, hold, reg, wire, regfile, count, cret };   // cret: a credit's return
inline constexpr unsigned kKinds = 9;
inline constexpr Cycle kNoCycle = ~Cycle(0);
// The cycle loop's hot paths (a put, a take, a peek, a mark): always inline into the unit's step, whatever the compiler's
// size limits (a put that grew past them became a call, costing the mesh 7% more instructions: the performance pass).
#define SIM_HOT [[gnu::always_inline]] inline
inline constexpr uint32_t kNone = ~0u;
// W10: calibration's clock: the time stamp counter where there is one, else the monotonic clock in nanoseconds.
inline uint64_t sim_ticks() {
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
#endif
}
inline constexpr uint16_t kNoClass = 0xFFFF;   // a port carrying its payload by value

// A port's ring of visible entries, slim (the performance pass): its storage packed in the thread's block (base/words.h),
// its depth and width the port's own; W1's Ring keeps a name and full-width fields a port doesn't need. Width 0 (an
// event Link) keeps only the count. Full is a framework bug here (the ports check capacity before), so a DCHECK.
inline constexpr Word kZeroWords[3] = {0, 0, 0};
struct SlimRing {
    Word* w = nullptr;
    uint32_t head = 0, n = 0, depth = 0, wid = 0;   // 32-bit, as W1's Ring: with 16-bit fields clang compiled the wraps
                                                    // below to branches, which the predictor misses (+13% mispredicts on
                                                    // the mesh, measured in the performance pass); 32-bit gives cmovs
    // Width 0: the storage is the shared zero words (index x 0 is always 0), so peek needs no branch on the width.
    void init(Word* storage, unsigned width, unsigned d) {
        w = width ? storage : const_cast<Word*>(kZeroWords);
        head = 0; n = 0; depth = d; wid = width;
    }
    unsigned count() const { return n; }
    bool empty() const { return n == 0; }
    unsigned tail() const { unsigned j = head + n; return j >= depth ? j - depth : j; }   // the slot the next push takes
    unsigned slot(unsigned k) const { unsigned j = head + k; return j >= depth ? j - depth : j; }
    unsigned width() const { return wid; }
    const Word* peek(unsigned k = 0) const {
        unsigned j = head + k;
        if (j >= depth) j -= depth;
        return w + size_t(j) * wid;
    }
    Word* push() {
        DCHECK(n < depth, "ring full: depth %u", depth);
        unsigned j = head + n++;
        if (j >= depth) j -= depth;
        return w + size_t(j) * wid;
    }
    void pop() {
        DCHECK(n > 0, "ring pop when empty");
        --n;
        if (++head == depth) head = 0;
    }
};

// One per Link input: the visible words (a ring as deep as the capacity) and the count in the wheel.
struct LinkPort {
    SlimRing ring;
    uint32_t owner;           // the consumer's instance
    uint16_t inflight;        // entries in the wheel for this port
    uint16_t cap;             // in flight + visible (WordDelayLine: L + 2)
    uint16_t cls;             // the size class of a by-reference payload, kNoClass if by value
    uint8_t bit, width;
#ifdef SIM_DEBUG
    Cycle last_take;          // a second take in a cycle (DCHECK)
#endif
};

// One per Fifo input (a credit Fifo too: then `ret` is the route its takes return credits on, and the producer's credits
// live here, read by the producer as room as of the start of its cycle).
struct FifoPort {
    SlimRing ring;
    Cycle last_take;          // the cycle of the last take: the occupancy at the start of t is count + (last_take == t)
    Cycle first_overflow;     // the put cycle of the first overflow, kNoCycle if none
    uint64_t overflows;
    uint32_t owner, count_idx;
    uint32_t ret;             // credit Fifo: the return route; kNone for a plain Fifo
    uint32_t credits, producer;
    uint16_t depth, cls;
    uint8_t bit, width, pbit;
};

// One per Reg output: the registered value, its readers.
struct RegPort {
    Word w[3];
    uint32_t readers, nreaders;   // into Engine::reader_inst / reader_bit
    uint8_t width;
    bool valid;
};

// One per Wire: its producer's latest value; its readers; the cycle of its last write and last read (debug discipline).
struct WirePort {
    Word v;
    Cycle written_at, read_at;
    uint32_t producer, readers, nreaders;
    uint32_t level;               // the producer's level (checks)
};

// One per RegFile: 32-bit registers owned by one unit.
struct RegFilePort {
    uint32_t* regs;
    Cycle read_at;
    uint32_t owner, readers, nreaders, n;
};

// One per Count (a connected set of producers and readers).
struct CountPort {
    uint64_t total, last_amount;  // last_amount: incremented during `last_cycle`
    Cycle last_cycle;
    uint32_t max_producer_level;  // now() is a wire read: its readers sit above every producer (checked in debug)
};

// One per Hold input.
struct HoldPort {
    Word w[3];
    Cycle last_take, refused_at;
    uint64_t refusals;
    uint32_t owner, producer;
    uint16_t cls;
    uint8_t bit, pbit, width;
    bool occ;
};

// The resolved route of an output: where a put's entry is appended (config.h). A data value: the same code puts locally
// or across a domain.
struct Route {
    Cycle last_put;           // one put per output per cycle (fatal)
    uint32_t dest;            // the destination's pool index: an entry's first word
    uint16_t wheel;           // its wheel (Engine::wheels)
    uint16_t latency;
    uint16_t cls;             // the size class of a by-reference payload, kNoClass if by value
    uint8_t width;
    Kind kind;
#ifdef SIM_DEBUG
    uint32_t owner;           // the producer's instance (checks)
#endif
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
    void (*one)(Engine&, RunSet&, uint32_t k);   // one instance (the debug orders)
    unsigned char* units;     // the type's arena, at this run's first unit
    uint32_t first, count, level, nready, nnext;
    uint16_t type;
    Form form;
    uint8_t cur;              // which of bits[] is this cycle's ready set (the other is the next cycle's): the end of a
                              // cycle flips it, nothing is copied
    uint32_t nwords;
    uint64_t* bits[2];
    uint64_t* ready() const { return bits[cur]; }
    uint64_t* next() const { return bits[cur ^ 1]; }
    static bool test(const uint64_t* b, uint32_t k) { return b[k >> 6] >> (k & 63) & 1; }
    static void set(uint64_t* b, uint32_t k) { b[k >> 6] |= uint64_t(1) << (k & 63); }
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
    RegPort* regs = nullptr;
    WirePort* wires = nullptr;
    RegFilePort* regfiles = nullptr;
    CountPort* counts = nullptr;
    uint32_t* reader_inst = nullptr;   // the readers of Regs, Wires and RegFiles, by port, contiguous
    uint8_t* reader_bit = nullptr;
    Route* routes = nullptr;
    SlotPool* pools = nullptr;         // one per size class (by-reference payloads)
    Wheel* wheels = nullptr;
    RunSet* runs = nullptr;
    uint32_t nwheels = 0, nruns = 0, nlinks = 0, nfifos = 0, nholds = 0, nroutes = 0, nregs = 0, nwires = 0, nregfiles = 0,
             ncounts = 0;
    uint32_t npools = 0;
    uint64_t* pool_allow = nullptr;    // per class: the slots units may hold outside the ports (declared held, refused holds)
    uint64_t leak_every = 0;           // debug: check the pools every N cycles (0: at the end of each run only)
    // the debug orders and step-all (sim.h, DEBUG CHECKS)
    bool step_all = false, instance_major = false;
    uint64_t shuffle = 0;              // the seeded order's state (0: none)
    uint32_t* add_order = nullptr;     // per instance: its place in the configuration's add order (instance-major)
    uint32_t* scratch = nullptr;       // a level's ready instances, ninst entries
    // the missed-wake detector (D20; debug builds)
    struct Watch { Kind kind; uint32_t idx; const char* name; };   // a level input, or a Hold or credit output
    Watch* watch = nullptr;
    uint32_t* watch_first = nullptr;   // per instance: its watches are [watch_first[i], watch_first[i + 1])
    uint32_t* missed_after = nullptr;  // per instance: the threshold (0: off)
    Cycle* suspect_since = nullptr;    // per instance: since when asleep with data waiting, kNoCycle if not a suspect
    uint32_t* suspects = nullptr;      // the suspects, nsuspects of them
    uint32_t nsuspects = 0;
    bool missed_fatal = true, detector = false;
    // Diagnostics don't allocate (D24): a report is formatted here with snprintf, inside the run as well as after it.
    char diag[1024] = {};
    uint32_t cur_level = 0;            // the level running (checks)
    uint64_t pending = 0;         // entries in the wheels (fast-forward when 0 and nothing is ready)
    // W10: the stop request (Ctx::stop): the earliest cycle a unit asked the run to stop at the end of, kNoCycle if none;
    // one domain breaks the run there (stop_breaks), a split domain runs on to its interval's barrier (threads.cpp)
    Cycle stop_at = kNoCycle;
    bool stop_breaks = true;
    // W10: the port observer's arrival cycles (Options::observe_arrivals): per Link input, one per ring slot, at
    // link_at + link_at_off[port]; null when off (nothing recorded)
    uint64_t* inst_ticks = nullptr; // W10: Options::calibrate: time stamp ticks per instance's steps, summed (the runs'
                                    // calibrating loops, config.h run_form_cal; null: not timed)
    Cycle* link_at = nullptr;
    uint32_t* link_at_off = nullptr;
    // Cost per cycle with activity, not structure (the performance pass):
    uint64_t due[64] = {};        // per cycle mod 64: the wheels with entries due (when every latency < 64: due_ok)
    bool due_ok = false;
    uint32_t nrw = 0;             // words of the run masks
    uint64_t* run_ready = nullptr;// runs with ready instances this cycle (one bit per run, in level order)
    uint64_t* run_next = nullptr; // runs with next-cycle requests
    // near wakes (2 .. 15 cycles ahead): per slot (cycle mod 16) a bitmap of instances, with a summary of its non-zero words
    static constexpr unsigned kNear = 16;
    uint64_t* near_bits = nullptr;   // kNear x niw words
    uint64_t* near_sum = nullptr;    // kNear x nsw words
    uint32_t niw = 0, nsw = 0, near_busy = 0;
    const char* const* link_names = nullptr;
    const char* const* unit_names = nullptr;   // for messages
    const char* const* route_names = nullptr;

    [[gnu::always_inline]] void mark(uint32_t i) {
        const uint32_t ri = run_of[i];
        RunSet& r = runs[ri];
        uint32_t k = i - r.first;
        uint64_t* b = r.ready();
        if (!RunSet::test(b, k)) {
            RunSet::set(b, k);
            if (r.nready++ == 0) run_ready[ri >> 6] |= uint64_t(1) << (ri & 63);
        }
    }
    [[gnu::always_inline]] void mark_bit(uint32_t i, unsigned bit) { mask[i] |= uint64_t(1) << bit; mark(i); }
    // A Wire's or RegFile's change wakes its readers in this cycle: they run at higher levels (checked).
    void mark_readers(uint32_t first, uint32_t n) {
        for (uint32_t k = first; k < first + n; k++) {
            DCHECK(runs[run_of[reader_inst[k]]].level > cur_level, "a wire's reader %s is not above its producer's level",
                   unit_names[reader_inst[k]]);
            mark_bit(reader_inst[k], reader_bit[k]);
        }
    }
    void count_inc(uint32_t c, uint64_t n) {
        CountPort& p = counts[c];
        if (p.last_cycle != now) { p.last_cycle = now; p.last_amount = 0; }
        p.last_amount += n;
        p.total += n;
    }

    template <unsigned W>
    [[gnu::always_inline]] Word* append(uint32_t route) {   // the put's hot path: inline in every step
        Route& r = routes[route];
#ifdef SIM_DEBUG
        DCHECK(r.owner == ctx.cur_, "put on '%s' by %s, not its owner", route_names[route], unit_names[ctx.cur_]);
#endif
        DCHECK(r.width == W, "put of %u words on '%s', declared width %u", W, route_names[route], r.width);
        if (__builtin_expect(r.last_put == now, 0)) die_two_puts(route);
        r.last_put = now;
        const Wheel& wh = wheels[r.wheel];
        const Cycle at = now + r.latency;
        Bucket& b = wh.buckets[at & wh.mask];
        if (b.empty() && due_ok) due[at & 63] |= uint64_t(1) << r.wheel;   // the bucket's first entry only: no chain of
        Word* e = b.append();                                                // read-modify-writes across a cycle's puts
        e[0] = r.dest;
        pending++;
        return e + 1;
    }

    void bind() { ctx.e_ = this; }
    void enter(RunSet& r, uint32_t k) { ctx.cur_ = r.first + k; ctx.mask_ = &mask[r.first + k]; }
    // A step's wake replaces the instance's last: next cycle (the run's next set), 2-15 cycles (the near wheel), further
    // (the heap), or none. Inline: the common cases are a few instructions; the rare ones are calls.
    [[gnu::always_inline]] void after(RunSet& r, uint32_t k, Wake w) {
        const uint32_t i = r.first + k;
        steps[i]++;
        mask[i] &= ~edges[i];
        const Cycle at = w.at;
        if constexpr (kDebug) note_suspect(i, at);
        if (__builtin_expect(heap_pos[i] != kNone, 0)) heap_remove(i);   // a far wake overtaken
        if (at == now + 1) {
            wake_at[i] = kNoCycle;
            uint64_t* nb = r.next();
            if (!RunSet::test(nb, k)) {
                RunSet::set(nb, k);
                if (r.nnext++ == 0) { const uint32_t ri = run_of[i]; run_next[ri >> 6] |= uint64_t(1) << (ri & 63); }
            }
            return;
        }
        wake_at[i] = kNoCycle;
        if (at == Wake::kNever) return;
        after_far(i, at);
    }
    void after_far(uint32_t i, Cycle at);   // 2 cycles ahead or more: the near wheel or the heap
    void note_suspect(uint32_t i, Cycle at);
    void cycle();
    void run_until(Cycle end);
    void check_leaks() const;          // debug builds: each class's slots in use against its ports and declared holds
    void run_level_debug(uint32_t& r); // the debug orders: one level's ready instances, ordered and stepped one by one
    bool on_its_way(uint32_t i) const; // the detector: a delivery, a credit return or a refusal will step instance i
    void check_suspects();
    Cycle suspect_deadline() const;    // the earliest cycle a suspect is judged: an idle jump stops there (debug)
    const char* describe_suspect(uint32_t i);   // into diag[] (D24)
    bool idle() const;

    void near_set(uint32_t i, Cycle at) {
        const unsigned s = unsigned(at & (kNear - 1));
        near_bits[size_t(s) * niw + (i >> 6)] |= uint64_t(1) << (i & 63);
        near_sum[size_t(s) * nsw + (i >> 12)] |= uint64_t(1) << ((i >> 6) & 63);
        near_busy |= 1u << s;
    }
    void near_slot();                  // this cycle's slot: mark the instances whose wake is now
    Cycle next_wake() const;           // the earliest near or far wake, kNoCycle if none
    bool asleep(uint32_t i) const;     // no step next cycle, no wake pending
    void heap_set(uint32_t i, Cycle at);
    void heap_remove(uint32_t i);
    uint32_t heap_pop();

    [[noreturn]] void die_two_puts(uint32_t route) const;
    [[noreturn]] void die_no_credit(uint32_t route) const;
    [[noreturn]] void die_wire(uint32_t w, const char* what) const;
    [[noreturn]] void die_link_full(uint32_t port) const;
    [[noreturn]] void die_wake(uint32_t i, Cycle at) const;
};

// ---- Ctx's operations ---------------------------------------------------------------------------------------------
SIM_HOT Cycle Ctx::now() const { return e_->now; }
SIM_HOT Wake Ctx::next() const { return Wake{e_->now + 1}; }
SIM_HOT Wake Ctx::in(Cycle n) const { return Wake{e_->now + n}; }
SIM_HOT uint64_t Ctx::mask() const { return *mask_; }
SIM_HOT void Ctx::stop() { if (e_->stop_at == kNoCycle) e_->stop_at = e_->now; }

// Link
SIM_HOT bool Ctx::ready(LinkIn p) const { return *mask_ >> p.bit & 1; }
SIM_HOT const Word* Ctx::peek(LinkIn p) const {
    const LinkPort& l = e_->links[p.idx];
    DCHECK(l.owner == cur_ && !l.ring.empty(), "peek of an empty or foreign link (%s)", e_->unit_names[cur_]);
    return l.ring.peek(0);
}
SIM_HOT const Word* Ctx::peek(LinkIn p, unsigned k) const {
    const LinkPort& l = e_->links[p.idx];
    DCHECK(l.owner == cur_ && k < l.ring.count(), "peek(%u) of a link holding %u, or foreign (%s)", k, l.ring.count(),
           e_->unit_names[cur_]);
    return l.ring.peek(k);
}
SIM_HOT unsigned Ctx::size(LinkIn p) const { return e_->links[p.idx].ring.count(); }
SIM_HOT void Ctx::take(LinkIn p) {
    LinkPort& l = e_->links[p.idx];
#ifdef SIM_DEBUG
    DCHECK(l.owner == cur_ && !l.ring.empty() && l.last_take != e_->now, "take of an empty or foreign link, or a second take (%s)",
           e_->unit_names[cur_]);
    l.last_take = e_->now;
#endif
    l.ring.pop();
    if (l.ring.empty()) *mask_ &= ~(uint64_t(1) << p.bit);
}
namespace detail {
SIM_HOT LinkPort& link_put_check(Engine* e, LinkOut p) {
    LinkPort& l = e->links[e->routes[p.route].dest];
    if (__builtin_expect(l.ring.count() + l.inflight >= l.cap, 0)) e->die_link_full(e->routes[p.route].dest);
    l.inflight++;
    return l;
}
}  // namespace detail
SIM_HOT void Ctx::put(LinkOut p) { detail::link_put_check(e_, p); e_->append<0>(p.route); }
SIM_HOT void Ctx::put(LinkOut p, Word a) { detail::link_put_check(e_, p); Word* w = e_->append<1>(p.route); w[0] = a; }
SIM_HOT void Ctx::put(LinkOut p, Word a, Word b) {
    detail::link_put_check(e_, p); Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b;
}
SIM_HOT void Ctx::put(LinkOut p, Word a, Word b, Word c) {
    detail::link_put_check(e_, p); Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c;
}

// Fifo
SIM_HOT bool Ctx::ready(FifoIn p) const { return *mask_ >> p.bit & 1; }
SIM_HOT const Word* Ctx::peek(FifoIn p, unsigned k) const {
    const FifoPort& f = e_->fifos[p.idx];
    DCHECK(f.owner == cur_ && k < f.ring.count(), "peek(%u) of a fifo holding %u, or foreign (%s)", k, f.ring.count(),
           e_->unit_names[cur_]);
    return f.ring.peek(k);
}
SIM_HOT void Ctx::take(FifoIn p) {
    FifoPort& f = e_->fifos[p.idx];
    DCHECK(f.owner == cur_ && !f.ring.empty() && f.last_take != e_->now, "take of an empty or foreign fifo, or a second take (%s)",
           e_->unit_names[cur_]);
    f.last_take = e_->now;
    f.ring.pop();
    if (f.ring.empty()) *mask_ &= ~(uint64_t(1) << p.bit);
    if (f.ret != kNone) e_->append<0>(f.ret);          // a credit Fifo: the credit returns, seen by the producer at t + 1
}
SIM_HOT unsigned Ctx::size(FifoIn p) const { return e_->fifos[p.idx].ring.count(); }
SIM_HOT unsigned Ctx::size_at_start(FifoIn p) const {
    const FifoPort& f = e_->fifos[p.idx];
    return f.ring.count() + (f.last_take == e_->now);
}
SIM_HOT void Ctx::put(FifoOut p, Word a) { Word* w = e_->append<1>(p.route); w[0] = a; }
SIM_HOT void Ctx::put(FifoOut p, Word a, Word b) { Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b; }
SIM_HOT void Ctx::put(FifoOut p, Word a, Word b, Word c) { Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c; }

// credit Fifo, producer side
SIM_HOT unsigned Ctx::credits(CreditOut p) const {
    const Route& r = e_->routes[p.route];
    return e_->fifos[r.dest].credits + (r.last_put == e_->now);   // as of the start of t, whatever this cycle's put
}
namespace detail {
SIM_HOT void credit_spend(Engine* e, CreditOut p) {
    FifoPort& f = e->fifos[e->routes[p.route].dest];
    if (__builtin_expect(f.credits == 0, 0)) e->die_no_credit(p.route);
    f.credits--;
}
}  // namespace detail
SIM_HOT void Ctx::put(CreditOut p, Word a) { detail::credit_spend(e_, p); Word* w = e_->append<1>(p.route); w[0] = a; }
SIM_HOT void Ctx::put(CreditOut p, Word a, Word b) {
    detail::credit_spend(e_, p); Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b;
}
SIM_HOT void Ctx::put(CreditOut p, Word a, Word b, Word c) {
    detail::credit_spend(e_, p); Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c;
}

// Hold
SIM_HOT bool Ctx::ready(HoldIn p) const { return *mask_ >> p.bit & 1; }
SIM_HOT const Word* Ctx::peek(HoldIn p) const {
    const HoldPort& h = e_->holds[p.idx];
    DCHECK(h.owner == cur_ && h.occ, "peek of an empty or foreign hold (%s)", e_->unit_names[cur_]);
    return h.w;
}
SIM_HOT void Ctx::take(HoldIn p) {
    HoldPort& h = e_->holds[p.idx];
    DCHECK(h.owner == cur_ && h.occ, "take of an empty or foreign hold (%s)", e_->unit_names[cur_]);
    h.occ = false;
    h.last_take = e_->now;
    *mask_ &= ~(uint64_t(1) << p.bit);
}
SIM_HOT void Ctx::put(HoldOut p, Word a) { Word* w = e_->append<1>(p.route); w[0] = a; }
SIM_HOT void Ctx::put(HoldOut p, Word a, Word b) { Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b; }
SIM_HOT void Ctx::put(HoldOut p, Word a, Word b, Word c) { Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c; }
SIM_HOT bool Ctx::refused(HoldOut p) const { return e_->holds[e_->routes[p.route].dest].refused_at == e_->now - 1; }
SIM_HOT bool Ctx::can_put(HoldOut p) const {
    const HoldPort& h = e_->holds[e_->routes[p.route].dest];
    return !h.occ && h.last_take != e_->now;
}
SIM_HOT bool Ctx::held_at_start(HoldOut p) const {
    const HoldPort& h = e_->holds[e_->routes[p.route].dest];
    return h.occ || h.last_take == e_->now;
}

// Reg
SIM_HOT bool Ctx::valid(RegIn p) const { return e_->regs[p.idx].valid; }
SIM_HOT const Word* Ctx::read(RegIn p) const {
    DCHECK(e_->regs[p.idx].valid, "read of a reg with no value yet (%s)", e_->unit_names[cur_]);
    return e_->regs[p.idx].w;
}
SIM_HOT void Ctx::put(RegOut p, Word a) { Word* w = e_->append<1>(p.route); w[0] = a; }
SIM_HOT void Ctx::put(RegOut p, Word a, Word b) { Word* w = e_->append<2>(p.route); w[0] = a; w[1] = b; }
SIM_HOT void Ctx::put(RegOut p, Word a, Word b, Word c) { Word* w = e_->append<3>(p.route); w[0] = a; w[1] = b; w[2] = c; }

// Wire
SIM_HOT Word Ctx::read(WireIn p) const {
    WirePort& w = e_->wires[p.idx];
    if constexpr (kDebug) w.read_at = e_->now;
    return w.v;
}
SIM_HOT void Ctx::drive(WireOut p, Word v) {
    WirePort& w = e_->wires[p.idx];
    DCHECK(w.producer == cur_, "drive of wire by %s, not its producer", e_->unit_names[cur_]);
    if constexpr (kDebug) {
        if (w.written_at == e_->now) e_->die_wire(p.idx, "written twice in one cycle");
        if (w.read_at == e_->now) e_->die_wire(p.idx, "written after a reader read it in this cycle");
        w.written_at = e_->now;
    }
    if (w.v == v) return;
    w.v = v;
    e_->mark_readers(w.readers, w.nreaders);
}

// RegFile
SIM_HOT uint32_t Ctx::read(RegFileIn p, unsigned i) const {
    RegFilePort& r = e_->regfiles[p.idx];
    DCHECK(i < r.n, "register %u of %u (%s)", i, r.n, e_->unit_names[cur_]);
    if constexpr (kDebug) r.read_at = e_->now;
    return r.regs[i];
}
SIM_HOT void Ctx::write(RegFileOut p, unsigned i, uint32_t v) {
    RegFilePort& r = e_->regfiles[p.idx];
    DCHECK(r.owner == cur_ && i < r.n, "write of register %u of %u by %s", i, r.n, e_->unit_names[cur_]);
    if constexpr (kDebug) if (r.read_at == e_->now) FATAL("register file written after a reader read it in cycle %llu (%s)",
                                                          (unsigned long long)e_->now, e_->unit_names[cur_]);
    if (r.regs[i] == v) return;
    r.regs[i] = v;
    e_->mark_readers(r.readers, r.nreaders);
}

// Count
SIM_HOT void Ctx::inc(CountOut p, uint64_t n) { e_->count_inc(p.idx, n); }
SIM_HOT uint64_t Ctx::now(CountIn p) const {
    const CountPort& c = e_->counts[p.idx];
    DCHECK(c.max_producer_level < e_->cur_level || c.max_producer_level == kNone,
           "Count::now read by %s at level %u, not above a producer (level %u): read through_last",
           e_->unit_names[cur_], e_->cur_level, c.max_producer_level);
    return c.total;
}
SIM_HOT uint64_t Ctx::through_last(CountIn p) const {
    const CountPort& c = e_->counts[p.idx];
    return c.total - (c.last_cycle == e_->now ? c.last_amount : 0);
}

// By-reference payloads
namespace detail {
inline uint16_t cls_of(const Engine* e, LinkOut p) { return e->routes[p.route].cls; }
inline uint16_t cls_of(const Engine* e, FifoOut p) { return e->routes[p.route].cls; }
inline uint16_t cls_of(const Engine* e, CreditOut p) { return e->routes[p.route].cls; }
inline uint16_t cls_of(const Engine* e, HoldOut p) { return e->routes[p.route].cls; }
inline uint16_t cls_of(const Engine* e, LinkIn p) { return e->links[p.idx].cls; }
inline uint16_t cls_of(const Engine* e, FifoIn p) { return e->fifos[p.idx].cls; }
inline uint16_t cls_of(const Engine* e, HoldIn p) { return e->holds[p.idx].cls; }
template <class P>
SlotPool& pool_of(Engine* e, P p, uint32_t cur) {
    uint16_t c = cls_of(e, p);
    DCHECK(c != kNoClass, "a slot operation on a port carrying its payload by value (%s)", e->unit_names[cur]);
    (void)cur;
    return e->pools[c];
}
}  // namespace detail
SIM_HOT uint32_t Ctx::alloc(LinkOut p) { return detail::pool_of(e_, p, cur_).alloc(); }
SIM_HOT uint32_t Ctx::alloc(FifoOut p) { return detail::pool_of(e_, p, cur_).alloc(); }
SIM_HOT uint32_t Ctx::alloc(CreditOut p) { return detail::pool_of(e_, p, cur_).alloc(); }
SIM_HOT uint32_t Ctx::alloc(HoldOut p) { return detail::pool_of(e_, p, cur_).alloc(); }
template <class P> Word* Ctx::slot(P p, uint32_t i) const { return detail::pool_of(e_, p, cur_).slot(i); }
template <class P> void Ctx::free(P p, uint32_t i) { detail::pool_of(e_, p, cur_).free(i); }

// ---- the delivery loops, one per (kind, width) (D16) ---------------------------------------------------------------
namespace detail {
template <unsigned W>
void deliver_link(Engine& e, const Bucket& b) {
    const Word* p = b.begin();
    if (__builtin_expect(e.link_at != nullptr, 0))                      // the observer's arrival cycles (W10): per bucket
        for (uint32_t n = b.count(); n; n--, p += W + 1) e.link_at[e.link_at_off[p[0]] + e.links[p[0]].ring.tail()] = e.now;
    p = b.begin();
    for (uint32_t n = b.count(); n; n--, p += W + 1) {
        LinkPort& l = e.links[p[0]];
        Word* d = l.ring.push();                                         // width 0: the count only
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
            if (f.count_idx != kNone) e.count_inc(f.count_idx, 1);       // counted at t + 1 (PORTS.md 3, 8)
            if (f.cls != kNoClass) e.pools[f.cls].free(uint32_t(p[1]));   // a dropped by-reference payload: its slot back
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
template <unsigned W>
void deliver_credit(Engine& e, const Bucket& b) {                       // a credit Fifo's data: room was reserved
    const Word* p = b.begin();
    for (uint32_t n = b.count(); n; n--, p += W + 1) {
        FifoPort& f = e.fifos[p[0]];
        Word* d = f.ring.push();                                         // full is a bug: the producer had a credit
        for (unsigned k = 0; k < W; k++) d[k] = p[1 + k];
        e.mark_bit(f.owner, f.bit);
    }
}
inline void deliver_cret(Engine& e, const Bucket& b) {                  // credits returned at t - 1
    const Word* p = b.begin();
    for (uint32_t n = b.count(); n; n--, p += 1) {
        FifoPort& f = e.fifos[p[0]];
        f.credits++;
        e.mark_bit(f.producer, f.pbit);
    }
}
template <unsigned W>
void deliver_reg(Engine& e, const Bucket& b) {
    const Word* p = b.begin();
    for (uint32_t n = b.count(); n; n--, p += W + 1) {
        RegPort& r = e.regs[p[0]];
        bool same = r.valid;
        for (unsigned k = 0; k < W; k++) same = same && r.w[k] == p[1 + k];
        if (same) continue;                                              // the registered value again: no change, no wake
        for (unsigned k = 0; k < W; k++) r.w[k] = p[1 + k];
        r.valid = true;
        for (uint32_t k = r.readers; k < r.readers + r.nreaders; k++) e.mark_bit(e.reader_inst[k], e.reader_bit[k]);
    }
}
}  // namespace detail

}  // namespace sim
