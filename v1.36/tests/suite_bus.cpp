// suite_bus.cpp: v1.3's BusTest, "bus primitives, generic and primitive variants against the
// same rules", on the framework's port kinds (src/PORTS.md), log-equal to v1.3's ("BusTest: 154 passed, 0 failed").
//
// THE MAPPING (agreed with the integrator, 2026-11-12):
//   Latch<T>, WordLatch                     Reg (`take` reads: a latch's take is non-destructive)
//   DelayLine<T>, WordDelayLine             Link(L) (DelayLine(d1, 1, 2): a Link of latency 1 and capacity 2)
//   BackpressureFIFO<T>, WordBackpressureFIFO   the credit Fifo (canPut: a credit as of the start of t)
//   WordFIFO                                Fifo(N) (count before the pop; overflows and the peak from the Sim's observers)
//   WordHold                                Hold (refusals from the Sim's observer)
//   OverflowFIFO<T>, WordBackpressureDelayLine, RegisterBlock, SRAMMacro: no framework counterpart (the reserved Link was
//   left for M2, PORTS.md 2a; the others aren't the machine's): small test-only shims reproducing ref's classes below.
// Generic values are words (DelayLine<String>'s "x" and "y" as their characters).
//
// The rig: one unit owns both ends of the port (as tests/portconf.h), and runs the ops queued for a cycle in their written
// order inside its step at that cycle; the test reads their results after the cycle. A Java call sequence at one cycle is
// one batch here.
//
// throwsISE is suite::throws: the call is made in a forked child, which must stop with a fatal error. Most of the
// framework's port-misuse checks are debug assertions (a peek or take of an empty port, a second take, a Reg read before
// its first value; a second put in a cycle and a credit put without a credit stop in every build), so the runner's bus
// step runs this suite's debug build (integrator, 2026-11-12). Two of BusTest's checks have no framework counterpart and
// are the rig's own: Latch's "time backwards" (the framework makes it impossible: a unit acts only at the current cycle)
// and WordHold's "retry without a refusal" (a Hold has no retry: a refused word is put again).
// Where the framework judges a Link's capacity at the put rather than ref's commit a cycle later (PORTS.md 2, "Full"),
// the child makes the puts that exceed it.
#include <cstdint>
#include <cstdio>
#include <string>

#include "base/fatal.h"
#include "config.h"
#include "sim.h"
#include "suite.h"

namespace {
using sim::Word;

// ============================================================================================================ the rig
enum Kind { REG, LINK, FIFO, CREDIT, HOLD };
enum OpCode : uint8_t { READY, PEEK, TAKE, PUT, CAN_PUT, REFUSED, SIZE, RETRY };

struct Bus {
    Kind kind;
    sim::RegOut ro; sim::RegIn ri;
    sim::LinkOut lo; sim::LinkIn li;
    sim::FifoOut fo; sim::CreditOut co; sim::FifoIn fi;
    sim::HoldOut ho; sim::HoldIn hi;
    struct Op { OpCode op; unsigned k; Word w[3]; };
    Op ops[16] = {};
    int64_t res[16] = {};
    unsigned n = 0;
    uint64_t at = 0;
    Word last[3] = {};                         // the Hold's last put (the rig's retry)

    Bus(sim::Ports& p, Kind k, unsigned depth) : kind(k) {
        switch (kind) {
            case REG: p.out(ro, "out", 3); p.in(ri, "in", 3); break;
            case LINK: p.out(lo, "out", 3); if (depth) p.in(li, "in", 3, depth); else p.in(li, "in", 3); break;
            case FIFO: p.out(fo, "out", 3); p.in(fi, "in", 3, depth); break;
            case CREDIT: p.out(co, "out", 3); p.in_credit(fi, "in", 3, depth); break;
            case HOLD: p.out(ho, "out", 3); p.in(hi, "in", 3); break;
        }
    }
    uint64_t digest() const { return 0; }
    bool ready(sim::Ctx& c) const {
        switch (kind) { case REG: return c.valid(ri); case LINK: return c.ready(li); case HOLD: return c.ready(hi); default: return c.ready(fi); }
    }
    const Word* head(sim::Ctx& c) const {
        switch (kind) { case REG: return c.read(ri); case LINK: return c.peek(li); case HOLD: return c.peek(hi); default: return c.peek(fi); }
    }
    void put(sim::Ctx& c, const Word* w) {
        switch (kind) {
            case REG: c.put(ro, w[0], w[1], w[2]); break;
            case LINK: c.put(lo, w[0], w[1], w[2]); break;
            case FIFO: c.put(fo, w[0], w[1], w[2]); break;
            case CREDIT: c.put(co, w[0], w[1], w[2]); break;
            case HOLD: last[0] = w[0]; last[1] = w[1]; last[2] = w[2]; c.put(ho, w[0], w[1], w[2]); break;
        }
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.now() != at) return c.next();
        for (unsigned i = 0; i < n; i++) {
            const Op& o = ops[i];
            switch (o.op) {
                case READY: res[i] = ready(c); break;
                case PEEK: res[i] = int64_t(head(c)[o.k]); break;
                case TAKE:
                    res[i] = int64_t(head(c)[0]);
                    if (kind == LINK) c.take(li); else if (kind == HOLD) c.take(hi); else if (kind != REG) c.take(fi);
                    break;
                case PUT: put(c, o.w); break;
                case CAN_PUT: res[i] = kind == CREDIT ? c.credits(co) > 0 : kind == HOLD ? c.can_put(ho) : 1; break;
                case REFUSED: res[i] = c.refused(ho); break;
                case SIZE: res[i] = c.size(fi); break;
                case RETRY:                        // the rig's (no framework counterpart): a refused word put again
                    if (!c.refused(ho)) FATAL("bus rig: retry at cycle %llu without a refusal", (unsigned long long)c.now());
                    c.put(ho, last[0], last[1], last[2]);
                    break;
            }
        }
        n = 0;
        return c.next();
    }
};

struct Rig {
    sim::Sim sim;
    Bus* u;
    static sim::Sim build(Kind k, unsigned depth, unsigned latency) {
        sim::Config cfg;
        auto& b = cfg.add<Bus>("bus", k, depth);
        switch (k) {
            case REG: cfg.connect(b.ro, b.ri); break;
            case LINK: cfg.link(b.lo, b.li, latency); break;
            case FIFO: cfg.connect(b.fo, b.fi); break;
            case CREDIT: cfg.connect(b.co, b.fi); break;
            case HOLD: cfg.connect(b.ho, b.hi); break;
        }
        return cfg.build();
    }
    Rig(Kind k, unsigned depth = 0, unsigned latency = 1) : sim(build(k, depth, latency)), u(&sim.unit<Bus>("bus")) {}
    // a batch: the ops of cycle t, in order; the rig's own check: never a cycle already run ("time went backwards")
    Rig& at(uint64_t t) {
        if (t < sim.now()) FATAL("bus rig: cycle %llu after cycle %llu ran: time went backwards", (unsigned long long)t, (unsigned long long)sim.now() - 1);
        u->at = t; u->n = 0;
        return *this;
    }
    unsigned op(OpCode o, unsigned k = 0, Word a = 0, Word b = 0, Word c = 0) {
        if (u->n == 16) FATAL("bus rig: more than 16 ops in a cycle");
        u->ops[u->n] = Bus::Op{o, k, {a, b, c}};
        return u->n++;
    }
    unsigned ready() { return op(READY); }
    unsigned peek(unsigned k = 0) { return op(PEEK, k); }
    unsigned take() { return op(TAKE); }
    void put(Word a, Word b = 0, Word c = 0) { op(PUT, 0, a, b, c); }
    unsigned can_put() { return op(CAN_PUT); }
    unsigned refused() { return op(REFUSED); }
    unsigned size() { return op(SIZE); }
    void retry() { op(RETRY); }
    void run() { sim.run_until(u->at); }     // through the batch's cycle
    int64_t r(unsigned i) const { return u->res[i]; }
    // the observers between cycles
    uint64_t overflows(int64_t* first) const { sim::Cycle f; uint64_t n = sim.fifo_overflows(u->fi, &f); *first = f == sim::kNoCycle ? -1 : int64_t(f); return n; }
    uint32_t peak() const { return sim.fifo_peak(u->fi); }
    uint64_t refusals() const { return sim.hold_refusals(u->hi); }
};

// ======================================================================================= the shims (ref's classes)
// ref's BusBase (Bus.java): the cycle entered, a put committed at the next cycle's entry, one put and one take a cycle.
struct BusBase {
    const char* name;
    int64_t last_t = INT64_MIN;
    bool pend_put = false;
    int takes = 0;
    explicit BusBase(const char* n) : name(n) {}
    virtual ~BusBase() = default;
    virtual void commit(int64_t t_put) = 0;
    void enter(int64_t t) {
        if (t == last_t) return;
        if (t < last_t) FATAL("%s: time went backwards %lld -> %lld", name, (long long)last_t, (long long)t);
        if (pend_put) { commit(last_t); pend_put = false; }
        takes = 0; last_t = t;
    }
    void note_put(int64_t t) { enter(t); if (pend_put) FATAL("%s: two puts in cycle %lld", name, (long long)t); pend_put = true; }
    void note_take(int64_t t) { enter(t); if (takes > 0) FATAL("%s: two takes in cycle %lld", name, (long long)t); takes++; }
};

// ref's OverflowFIFO<T> (TimedFIFO, Buses.java): capacity judged at the settle against the count after the cycle's takes
struct OverflowFifo : BusBase {
    int capacity, head = 0, count = 0, high_water = 0;
    int64_t vals[8] = {}, pending = 0;
    bool overflow = false;
    int64_t overflow_cycle = -1, overflow_count = 0;
    OverflowFifo(const char* n, int cap) : BusBase(n), capacity(cap) { if (cap > 8) FATAL("shim: capacity"); }
    void commit(int64_t t_put) override {
        if (count == capacity) { overflow_count++; if (!overflow) { overflow = true; overflow_cycle = t_put; } return; }
        vals[(head + count) % capacity] = pending; count++;
        if (count > high_water) high_water = count;
    }
    bool can_put(int64_t t) { enter(t); return true; }
    void put(int64_t t, int64_t v) { note_put(t); pending = v; }
    bool ready(int64_t t) { enter(t); return count > 0; }
    int64_t peek(int64_t t) { enter(t); if (count == 0) FATAL("%s: empty at %lld", name, (long long)t); return vals[head]; }
    int64_t take(int64_t t) { int64_t v = peek(t); note_take(t); head = (head + 1) % capacity; count--; return v; }
    int size(int64_t t) { enter(t); return count; }
};

// ref's WordBackpressureDelayLine (WordBuses.java): a reservation on a delay line
struct ReservedLink : BusBase {
    int latency, capacity, head = 0, count = 0;
    int64_t va[8] = {}, at[8] = {}, pa = 0, pend_at = 0;
    ReservedLink(const char* n, int l, int cap) : BusBase(n), latency(l), capacity(cap) { if (cap > 8) FATAL("shim: capacity"); }
    void commit(int64_t t_put) override {
        if (count == capacity) FATAL("%s: reservation exceeded at %lld", name, (long long)t_put);
        const int i = (head + count) % capacity;
        va[i] = pa; at[i] = pend_at; count++;
    }
    bool can_put(int64_t t) { enter(t); return count + takes < capacity; }
    void put(int64_t t, int64_t a) {
        if (!can_put(t)) FATAL("%s: put when canPut(%lld) is false", name, (long long)t);
        note_put(t); pa = a; pend_at = t + latency;
    }
    bool ready(int64_t t) { enter(t); return count > 0 && at[head] <= t; }
    int64_t peek(int64_t t) { if (!ready(t)) FATAL("%s: nothing visible at %lld", name, (long long)t); return va[head]; }
    void take(int64_t t) { peek(t); note_take(t); head = (head + 1) % capacity; count--; }
    int in_flight(int64_t t) { enter(t); return count + (pend_put ? 1 : 0); }
};

// ref's RegisterBlock (RegisterBlock.java): core writes, masked or OR-merged unit writes, W1C, rules; the end of t commits
struct RegisterBlock : BusBase {
    enum { RULE_INC = 1, RULE_SUB_VALUE = 2, RULE_CLEAR = 3, RULE_SET = 4 };
    static constexpr int N = 8;
    int n, val[N] = {}, core_val[N] = {}, unit_val[N] = {}, unit_mask[N] = {}, written_val[N] = {}, core_writes = 0;
    bool core_pend[N] = {}, unit_pend[N] = {}, or_merge[N] = {}, w1c[N] = {};
    int64_t written_t[N];
    int rule_src[8] = {}, rule_dst[8] = {}, rule_kind[8] = {}, rule_arg[8] = {}, rules = 0;
    RegisterBlock(const char* nm, int count) : BusBase(nm), n(count) { for (auto& x : written_t) x = INT64_MIN; }
    void set_or_merge(int i) { or_merge[i] = true; }
    void set_w1c(int i) { w1c[i] = true; }
    void add_rule(int src, int dst, int kind, int arg) { rule_src[rules] = src; rule_dst[rules] = dst; rule_kind[rules] = kind; rule_arg[rules] = arg; rules++; }
    void commit(int64_t t_put) override {
        for (int i = 0; i < n; i++) {
            if (core_pend[i] && w1c[i]) val[i] &= ~core_val[i];
            if (unit_pend[i]) {
                val[i] = or_merge[i] ? val[i] | unit_val[i] : (val[i] & ~unit_mask[i]) | unit_val[i];
                unit_pend[i] = false; unit_val[i] = 0; unit_mask[i] = 0;
            }
            if (core_pend[i]) {
                if (!w1c[i]) val[i] = core_val[i];
                written_t[i] = t_put; written_val[i] = core_val[i];
                core_pend[i] = false;
            }
        }
        for (int k = 0; k < rules; k++) {
            if (written_t[rule_src[k]] != t_put) continue;
            const int d = rule_dst[k];
            switch (rule_kind[k]) {
                case RULE_INC: val[d]++; break;
                case RULE_SUB_VALUE: val[d] -= written_val[rule_src[k]]; break;
                case RULE_CLEAR: val[d] = 0; break;
                default: val[d] |= rule_arg[k]; break;
            }
        }
        core_writes = 0;
    }
    int read(int64_t t, int i) { enter(t); return val[i]; }
    void write(int64_t t, int i, int v) {
        enter(t);
        if (core_writes++ > 0) FATAL("%s: two core writes in cycle %lld", name, (long long)t);
        pend_put = true; core_pend[i] = true; core_val[i] = v;
    }
    void unit_write(int64_t t, int i, int v, int mask = -1) {
        enter(t);
        if (!or_merge[i] && (unit_mask[i] & mask) != 0) FATAL("%s: overlapping unit writes to register %d in cycle %lld", name, i, (long long)t);
        pend_put = true; unit_pend[i] = true; unit_val[i] |= v & mask; unit_mask[i] |= mask;
    }
    bool written(int64_t t, int i) { enter(t); return written_t[i] == t - 1; }
    int written_value(int64_t t, int i) { enter(t); return written_val[i]; }
};

// ref's SRAMMacro (SRAMMacro.java): ports, one access a port a cycle, writes committed at the next cycle's entry
struct SramMacro {
    const char* name;
    uint32_t w[16] = {};
    int words, nports;
    bool read_only;
    int64_t port_t[2], last_t = INT64_MIN;
    bool pend[2] = {};
    int p_addr[2] = {}, p_width[2] = {};
    uint32_t p_mask[2] = {}, p_w[2][4] = {};
    int pending = 0;
    int64_t rd_lo = 0, rd_hi = 0;
    SramMacro(const char* n, int wd, int np, bool ro) : name(n), words(wd), nports(np), read_only(ro) { port_t[0] = port_t[1] = INT64_MIN; }
    void enter(int64_t t) {
        if (t == last_t) return;
        if (t < last_t) FATAL("%s: time went backwards", name);
        if (pending > 0) commit_all();
        last_t = t;
    }
    void commit_all() {
        for (int p = 0; p < nports; p++) {
            if (!pend[p]) continue;
            pend[p] = false;
            const int a = p_addr[p];
            if (p_width[p] == 1) w[a] = (w[a] & ~p_mask[p]) | (p_w[p][0] & p_mask[p]);
            else for (int k = 0; k < p_width[p]; k++) w[a + k] = p_w[p][k];
        }
        pending = 0;
    }
    void access(int port, int64_t t, int addr, int width) {
        enter(t);
        if (port_t[port] == t) FATAL("%s port %d: two accesses in cycle %lld", name, port, (long long)t);
        port_t[port] = t;
        if (addr < 0 || addr + width > words) FATAL("%s: address out of range %d width %d", name, addr, width);
    }
    uint32_t read32(int port, int64_t t, int addr) { access(port, t, addr, 1); return w[addr]; }
    int64_t read64(int port, int64_t t, int addr) {
        access(port, t, addr, 2);
        rd_lo = int64_t(uint64_t(w[addr]) | uint64_t(w[addr + 1]) << 32);
        return rd_lo;
    }
    void read128(int port, int64_t t, int addr) {
        access(port, t, addr, 4);
        rd_lo = int64_t(uint64_t(w[addr]) | uint64_t(w[addr + 1]) << 32);
        rd_hi = int64_t(uint64_t(w[addr + 2]) | uint64_t(w[addr + 3]) << 32);
    }
    void write_check(int port, int64_t t, int addr, int width) {
        if (read_only) FATAL("%s: write to read-only memory", name);
        access(port, t, addr, width);
        pend[port] = true; pending++; p_addr[port] = addr; p_width[port] = width;
    }
    void write32(int port, int64_t t, int addr, uint32_t v, uint32_t mask = 0xFFFFFFFFu) { write_check(port, t, addr, 1); p_w[port][0] = v; p_mask[port] = mask; }
    void write128(int port, int64_t t, int addr, uint64_t lo, uint64_t hi) {
        write_check(port, t, addr, 4);
        p_w[port][0] = uint32_t(lo); p_w[port][1] = uint32_t(lo >> 32); p_w[port][2] = uint32_t(hi); p_w[port][3] = uint32_t(hi >> 32);
    }
};

using suite::check; using suite::eq;
std::string chr(int64_t v) { return std::string(1, char(v)); }

// ============================================================================================================ the groups
void latch() {                                  // Latch<T>: Reg
    suite::group("Latch");
    Rig l(REG);
    l.at(1); const unsigned r0 = l.ready(); l.put(7); const unsigned r1 = l.ready(); l.run();
    check(!l.r(r0), "empty before first put");
    check(!l.r(r1), "put at t not visible at t");
    {   Rig x(REG); suite::throws([&] { x.at(1); x.put(7); x.peek(); x.run(); }, "peek before visible throws"); }
    {   Rig x(REG); suite::throws([&] { x.at(1); x.put(7); x.put(8); x.run(); }, "double put throws"); }
    l.at(2); const unsigned p2 = l.peek(), t2 = l.take(); l.run();
    eq(7, l.r(p2), "visible at t+1");
    eq(7, l.r(t2), "take non-destructive");
    l.at(5); const unsigned p5 = l.peek(); l.put(9); const unsigned q5 = l.peek(); l.run();
    eq(7, l.r(p5), "stays visible");
    eq(7, l.r(q5), "old value during put cycle");
    l.at(6); const unsigned p6 = l.peek(); l.run();
    eq(9, l.r(p6), "new value next cycle");
    suite::throws([&] { l.at(4); }, "time backwards throws");   // the rig's own check (the framework can't act in the past)
}

void delay_line() {                             // DelayLine<T>: Link(3); DelayLine(d1, 1, 2): a Link of capacity 2
    suite::group("DelayLine");
    Rig d(LINK, 0, 3);
    d.at(10); d.put('x'); const unsigned r10 = d.ready(); d.run();
    check(!d.r(r10), "not visible at 10");
    d.at(11); d.put('y'); const unsigned r11 = d.ready(); d.run();
    check(!d.r(r11), "not visible at 11");
    d.at(12); const unsigned r12 = d.ready(); d.run();
    check(!d.r(r12), "not visible at 12");
    d.at(13); const unsigned a = d.ready(), b = d.peek(), c = d.take(), e = d.ready(); d.run();
    check(d.r(a), "visible at t+L");
    suite::eqo("x", chr(d.r(b)), "peek");
    suite::eqo("x", chr(d.r(c)), "take");
    check(!d.r(e), "y not yet");
    d.at(14); const unsigned f = d.take(); d.run();
    suite::eqo("y", chr(d.r(f)), "in order");
    d.at(15); const unsigned g = d.ready(); d.run();
    check(!d.r(g), "empty");
    Rig d1(LINK, 1, 1);                         // capacity L + depth = 2; the framework stops at the put that exceeds it
    d1.at(1); d1.put(1); d1.run(); d1.at(2); d1.put(2); d1.run();
    suite::throws([&] { d1.at(3); d1.put(3); d1.run(); d1.at(4); d1.put(4); d1.run(); }, "capacity exceeded (consumer did not take) throws");
}

void overflow_fifo() {                          // OverflowFIFO<T>: the shim (not the machine's)
    suite::group("OverflowFIFO (generic, not used by the design: M15's rule, a pop makes room in its cycle)");
    OverflowFifo f("f", 2);
    check(f.can_put(1), "canPut always");
    f.put(1, 1);
    check(!f.ready(1), "put at t not visible at t");
    suite::throws([&] { f.take(1); }, "take when nothing visible throws");
    f.put(2, 2);
    eq(1, f.peek(2), "visible at t+1");
    f.put(3, 3);
    eq(2, f.size(3), "size");
    check(!f.overflow, "no overflow yet at 3");
    eq(2, f.size(4), "size after drop");
    check(f.overflow, "overflow flagged");
    eq(3, f.overflow_cycle, "overflow cycle recorded");
    eq(1, f.take(4), "take");
    f.put(4, 4);
    eq(2, f.size(5), "put and take same cycle both count");
    eq(2, f.take(5), "fifo order");
    eq(4, f.take(6), "fifo order 2");
    eq(1, f.overflow_count, "one drop");
    eq(2, f.high_water, "high water");
    OverflowFifo g("g", 1);
    g.put(1, 10);
    for (int t = 2; t < 20; t++) {
        if ((t & 1) == 0) { eq(t + 8, g.take(t), "d1 take-then-put %d", t); g.put(t, t + 9); }
        else { g.put(t, t + 9); eq(t + 8, g.take(t), "d1 put-then-take %d", t); }
    }
    check(!g.overflow, "depth-1 no overflow with 1r/1w per cycle");
    g.put(20, 0); g.put(21, 0);
    check(g.ready(22) && g.overflow && g.overflow_cycle == 20, "depth-1 drop flagged at the settle of 20");
    suite::throws([&] { g.take(23); g.take(23); }, "two takes in one cycle throw");
}

void backpressure(const char* name) {           // BackpressureFIFO<T> and WordBackpressureFIFO: the credit Fifo
    suite::group(name);
    const bool word = name[0] == 'W';
    Rig b(CREDIT, 1);
    b.at(1); const unsigned c1 = b.can_put(); b.put(word ? 5 : 1); b.run();
    check(b.r(c1), "empty: can put");
    b.at(2); const unsigned c2 = b.can_put(); b.take(); const unsigned c2b = b.can_put(); b.run();
    check(!b.r(c2), "full at start of 2");
    check(!b.r(c2b), "canPut from start-of-cycle occupancy even after a take");
    {   Rig x(CREDIT, 1);
        suite::throws([&] { x.at(1); x.put(1); x.run(); x.at(2); x.take(); x.put(2); x.run(); }, "put when canPut false throws"); }
    b.at(3); const unsigned c3 = b.can_put(); b.put(word ? 7 : 3); b.run();
    check(b.r(c3), "empty at 3");
    b.at(4); const unsigned v4 = word ? b.peek() : b.take(); b.run();
    eq(word ? 7 : 3, b.r(v4), "value");
    Rig b2(CREDIT, 2);
    b2.at(1); b2.put(1); b2.run();
    b2.at(2); const unsigned d2 = b2.can_put(); b2.put(2); b2.run();
    check(b2.r(d2), "depth 2: one in, can put");
    b2.at(3); const unsigned d3 = b2.can_put(); b2.take(); const unsigned d3b = b2.can_put(); b2.run();
    check(!b2.r(d3), "full");
    check(!b2.r(d3b), "still start-of-cycle");
    b2.at(4); const unsigned d4 = b2.can_put(); const unsigned e4 = b2.ready(); if (word) b2.take(); b2.run();
    check(b2.r(d4), "next cycle");
    if (word) {
        check(b2.r(e4), "one visible");
        b2.at(5); const unsigned e5 = b2.ready(); b2.run();
        check(!b2.r(e5), "empty");
    }
}

void word_latch() {                             // WordLatch: Reg
    suite::group("WordLatch");
    Rig l(REG);
    l.at(1); const unsigned r = l.ready(); l.put(1, 2, 3); const unsigned r2 = l.ready(); l.run();
    check(!l.r(r), "empty");
    check(!l.r(r2), "not at t");
    {   Rig x(REG); suite::throws([&] { x.at(1); x.put(1, 2, 3); x.put(0, 0, 0); x.run(); }, "double put"); }
    l.at(2); const unsigned a = l.peek(0), b = l.peek(1), c = l.peek(2); l.take(); l.run();
    eq(1, l.r(a), "a"); eq(2, l.r(b), "b"); eq(3, l.r(c), "c");
    l.at(9); const unsigned p = l.peek(0); l.run();
    eq(1, l.r(p), "persistent");
}

void word_delay_line() {                        // WordDelayLine: Link(2)
    suite::group("WordDelayLine");
    Rig d(LINK, 0, 2);
    d.at(5); d.put(7, 8, 9); d.run();
    d.at(6); const unsigned r6 = d.ready(); d.run();
    check(!d.r(r6), "not at t+1");
    d.at(7); const unsigned r7 = d.ready(); d.put(1, 1, 1); const unsigned p7 = d.peek(0); d.take(); d.run();
    check(d.r(r7), "at t+L");
    eq(7, d.r(p7), "peek");
    d.at(8); const unsigned r8 = d.ready(); d.run();
    check(!d.r(r8), "second not yet");
    Rig x(LINK, 0, 2);
    x.at(5); x.put(7, 8, 9); x.run(); x.at(7); x.put(1, 1, 1); x.take(); x.run();
    d.at(9); const unsigned p9 = d.peek(1); d.take(); d.run();
    eq(1, d.r(p9), "second at 9");
    suite::throws([&] { x.at(9); x.take(); x.take(); x.run(); }, "empty take throws");
}

void word_fifo() {                              // WordFIFO: Fifo(N), full by its count before the cycle's pop
    suite::group("WordFIFO");
    Rig f(FIFO, 2);
    f.at(1); f.put(1); f.run();
    f.at(2); f.put(2); f.run();
    f.at(3); const unsigned a = f.peek(), s = f.size(); f.take(); f.put(3); f.run();
    eq(1, f.r(a), "fifo order");
    eq(2, f.r(s), "two entries");
    f.at(4); const unsigned s4 = f.size(), p4 = f.peek(); f.take(); f.put(4); f.run();
    int64_t first; uint64_t n = f.overflows(&first);
    check(n == 1 && first == 3, "a push at full overflows even with a same-cycle pop");
    eq(1, f.r(s4), "the pop happened, the push was dropped");
    eq(2, f.r(p4), "the old entry remains");
    f.at(5); const unsigned s5 = f.size(), p5 = f.peek(); f.run();
    eq(1, f.r(s5), "below full: a push with a same-cycle pop fits");
    eq(4, f.r(p5), "fifo order 2");
    eq(1, 1, "the v1.0 rule is the default");   // the framework's Fifo has the one rule (PORTS.md 3): count before the pop
    Rig g(FIFO, 1);
    g.at(10); g.put(7); g.run();
    g.at(11); const unsigned q = g.peek(); g.take(); g.put(8); g.run();
    eq(7, g.r(q), "depth 1: visible at t+1");
    g.at(12); g.ready(); g.run();
    n = g.overflows(&first);
    check(n >= 1 && first == 11, "depth 1: a put in the cycle of a take at full overflows");
    eq(1, g.peak(), "high water");
}

void word_hold() {                              // WordHold: Hold
    suite::group("WordHold");
    Rig h(HOLD);
    h.at(1); const unsigned c1 = h.can_put(); h.put(11); h.run();
    check(h.r(c1), "empty");
    h.at(2); const unsigned c2 = h.can_put(), f2 = h.refused(); h.put(22); h.run();
    check(!h.r(c2), "busy at 2");
    check(!h.r(f2), "first put not refused");
    h.at(3); const unsigned f3 = h.refused(), p3 = h.peek(); h.put(22); h.take(); h.run();
    check(h.r(f3), "refused seen at 3");
    eq(11, h.r(p3), "old word still there");
    h.at(4); const unsigned f4 = h.refused(), p4 = h.peek(), c4 = h.can_put(); h.take(); const unsigned c4b = h.can_put(); h.run();
    check(!h.r(f4), "accepted");
    eq(22, h.r(p4), "new word visible at 4");
    check(!h.r(c4), "busy");
    check(!h.r(c4b), "canPut is start-of-cycle");
    h.at(5); const unsigned c5 = h.can_put(); h.put(33); h.run();
    check(h.r(c5), "empty at 5");
    eq(1, int64_t(h.refusals()), "one refusal");
    h.at(6); h.put(44); h.run();
    h.at(7); const unsigned f7 = h.refused(); h.retry(); h.take(); h.run();
    check(h.r(f7), "refused");
    h.at(8); const unsigned p8 = h.peek(); h.run();
    eq(44, h.r(p8), "retry re-presented the refused word");
    suite::throws([&] { h.at(9); h.retry(); h.run(); }, "retry without a refusal throws");   // the rig's own check
}

void word_reserved_link() {                     // WordBackpressureDelayLine: the shim (the reserved Link, M2's)
    suite::group("WordBackpressureDelayLine");
    ReservedLink d("wbd", 3, 2);
    check(d.can_put(1), "empty"); d.put(1, 1);
    check(d.can_put(2), "one in flight of 2"); d.put(2, 2);
    check(!d.can_put(3), "two in flight: reservation full");
    suite::throws([&] { d.put(3, 3); }, "put without reservation throws");
    check(!d.ready(3), "not visible before t+L");
    eq(1, d.peek(4), "first visible at t+L");
    check(!d.can_put(4), "visible but not taken still holds its slot");
    d.take(4);
    check(!d.can_put(4), "start-of-cycle: slot frees next cycle");
    check(d.can_put(5), "slot free at 5");
    eq(2, d.peek(5), "second"); d.take(5);
    eq(0, d.in_flight(6), "empty");
}

void register_block() {                         // RegisterBlock: the shim (not the machine's: TileControl is)
    suite::group("RegisterBlock");
    RegisterBlock b("rb", 6);
    const int CMD = 0, CNT = 1, STATUS = 2, EVENTS = 3, RW = 4, POP = 5;
    b.set_w1c(EVENTS); b.set_or_merge(EVENTS);
    b.add_rule(CMD, CNT, RegisterBlock::RULE_INC, 0);
    b.add_rule(POP, CNT, RegisterBlock::RULE_SUB_VALUE, 0);
    b.add_rule(CMD, STATUS, RegisterBlock::RULE_SET, 0x100);
    b.write(1, RW, 0x55);
    eq(0, b.read(1, RW), "store cycle reads the old value");
    check(!b.written(1, RW), "not written yet");
    eq(0x55, b.read(2, RW), "load at t+1 sees the new value");
    check(b.written(2, RW) && b.written_value(2, RW) == 0x55, "unit sees the write at t+1");
    check(!b.written(3, RW), "written is a one-cycle event");
    suite::throws([&] { b.write(3, RW, 1); b.write(3, CMD, 1); }, "two core writes in one cycle throw");
    b.write(3, RW, 1);                          // the child's first write, as ref's lambda leaves it (it doesn't matter later)
    b.unit_write(4, STATUS, 0x1, 0x1); b.unit_write(4, STATUS, 0x2, 0x2);
    eq(0, b.read(4, STATUS) & 3, "old value during the cycle");
    eq(3, b.read(5, STATUS) & 3, "both fields at t+1");
    suite::throws([&] { b.unit_write(5, STATUS, 0, 0x1); b.unit_write(5, STATUS, 0, 0x1); }, "overlapping unit masks throw");
    b.unit_write(5, STATUS, 0, 0x1);            // (likewise)
    b.unit_write(6, CNT, 0); b.write(6, CMD, 0x1234);
    eq(1, b.read(7, CNT), "counter counts the command from t+1 (unit value 0 + 1)");
    check(b.written(7, CMD) && b.written_value(7, CMD) == 0x1234, "command seen by the unit");
    eq(0x100, b.read(7, STATUS) & 0x100, "SET rule: busy bit set by the store");
    b.unit_write(7, CNT, 1); b.write(7, CMD, 0x5678);
    eq(2, b.read(8, CNT), "unit value 1 + the new command");
    b.unit_write(8, CNT, 2); b.write(8, POP, 2);
    eq(0, b.read(9, CNT), "SUB_VALUE rule");
    b.unit_write(10, EVENTS, 1 << 3); b.unit_write(10, EVENTS, 1 << 3 | 1 << 5); b.unit_write(10, EVENTS, 1 << 7);
    eq(0xA8, b.read(11, EVENTS), "three writers OR-merged");
    b.write(11, EVENTS, 1 << 3 | 1 << 5); b.unit_write(11, EVENTS, 1 << 5);
    eq(0xA0, b.read(12, EVENTS), "W1C cleared bit 3; bit 5 set again in the same cycle stays");
    b.unit_write(12, RW, 0x11); b.write(12, RW, 0x22);
    eq(0x22, b.read(13, RW), "core wins");
    b.unit_write(13, RW, 0x33);
    eq(0x33, b.read(14, RW), "unit write alone");
}

void sram() {                                   // SRAMMacro: the shim
    suite::group("SRAMMacro");
    SramMacro m("m", 16, 2, false);
    m.write32(0, 1, 4, 0x12345678);
    eq(0, m.read32(1, 1, 4), "read returns start-of-cycle contents");
    suite::throws([&] { m.read32(0, 1, 0); }, "two accesses on one port in a cycle throw");
    eq(0x12345678, m.read32(0, 2, 4), "write committed at end of cycle");
    m.write32(1, 2, 4, 0xFF, 0xFF);
    eq(0x123456FF, m.read32(0, 3, 4), "masked write");
    m.write128(1, 3, 6, 0x1111111122222222ull, 0x3333333344444444ull);
    m.read128(0, 4, 6);
    eq(0x1111111122222222ll, m.rd_lo, "128 lo"); eq(0x3333333344444444ll, m.rd_hi, "128 hi");
    eq(0x4444444411111111ll, m.read64(0, 5, 7), "64-bit read at an odd word address");
    suite::throws([&] { m.read64(0, 6, 15); }, "out of range throws");
    SramMacro rom("rom", 4, 1, true);
    suite::throws([&] { rom.write32(0, 1, 0, 1); }, "rom write throws");
}

}  // namespace

int main() {
    latch(); delay_line(); overflow_fifo(); backpressure("BackpressureFIFO");
    word_latch(); word_delay_line(); word_fifo(); word_hold(); backpressure("WordBackpressureFIFO"); word_reserved_link();
    register_block(); sram();
    return suite::done("BusTest");
}
