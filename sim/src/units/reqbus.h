// reqbus.h: the request bus (spec 3.3, 4.18; sim/ref/meshsim/src/RequestBus.java, RequestPort.java; AUDIT-M3 3.4),
// WORKER14. A 16-bit, 8-ary tree from every tile's request port and the controllers' to the targets' receive queues:
// 128-bit requests of 8 beats, one beat a hop a cycle, wormhole per request; eight row nodes (tiles 8r .. 8r + 7) and a
// root (the row nodes and C&C 1-3); round-robin at each node from the input after the last served; the root skips a head
// whose target has no credit, drops targets 3-7 (flagged), and forwards whole requests into the receive queues (target
// c is read by C&C c + 1); one credit per queue entry, a pop returns one usable the next cycle.
//
// UNITS (one implementation, D34; ref's structure, so no unit has more than 14 inputs):
//   ReqLeaf   a port's send side (RequestPort's FIFO): the core's REQUEST_SEND pushes over `push` (Link(1): a push at t
//             is the bus's from t + 1); one beat a cycle leaves into its node's input while that input has room; the
//             last beat frees the head (`freed`, a Wire to the core: canPush's `freedAt == t`; `freed_ev`, Link(1) to
//             the register block: the FIFO's count). One per sender, in its tile's domain.
//   ReqNode   a row node (8 inputs, one output to the root) or the root (8 row inputs and 3 controller leaves; outputs
//             the three receive queues). Beats arrive over Link(hop) (ref's arrivals at t + lat).
//   ReqRecv   a target's receive queue, in its controller's domain: the delivered request arrives over Link(hop) (ref's
//             toReceiver), its head is published in a RegFile {pending, w0, w1, w2, w3} as a load at t sees it
//             (delivered before t); the core's REQUEST_RECEIVE pops it over `pop` (Link(1)), and returns the root's
//             credit over the core's own `credit` Link(1 | 3).
//
// ROOM (D34). In `ref` a sender sees its node input's occupancy (beats in it and on their way, as the node left it at
// t - 1) at the instant it sends. **Mode 0** (`credit_latency` 0, exact): each node publishes in a RegFile, per input,
// the beats it has forwarded through t - 1 (written first in its step); the sender compares with what it has sent. A
// RegFile is same-cycle and same-domain: the levels run root < rows < leaves < cores, and a split is fatal at build
// (a RegFile or Wire across domains). **Mode 3** (the spec under D29, the default): each forwarded beat returns a credit
// over Link(3), so a sender sees room three cycles late; it counts every cycle where `ref` would have had room and it
// didn't (`lag_cycles`: D29's "count the case"), as the root counts the cycles it skipped a target whose credit was on
// its way. With `lag_cycles` 0 a run equals `ref`.
//
// EVENT-DRIVEN (D23; AUDIT-M2 section 4's first case): a unit is stepped while it holds a beat, a request or a pending
// free, and when something arrives; nothing scans the 64 tiles. An idle bus costs nothing.
//
// Payloads. A beat (Link width 3): w[0] = k | src << 3 | storeT << 10 (k the beat 0-7, src the request's unit 0-66,
// storeT its REQUEST_SEND cycle); w[1] = w0 | w1 << 32; w[2] = w2 | w3 << 32 (every beat carries the request: the node
// takes it at beat 0). A push (Link(1), width 2): {w0 | w1 << 32, w2 | w3 << 32}, storeT its arrival - 1. A delivery
// (Link(hop), width 3) as a beat with k = 7. A credit (Link(3), width 1, mode 3): the cycle the beat was forwarded. A
// queue credit (the core's, width 1): the pop's cycle.
#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <string>

#include "base/fatal.h"
#include "config.h"
#include "check.h"
#include "sim.h"

namespace units {
namespace reqbus {

constexpr int BEATS = 8, BUF_BEATS = 8, ROWS = 8, ROOT_INPUTS = 11, TARGETS = 3, SENDERS = 67;

// Harness-only (the test's receives, ref's RequestBus.receive called several times in one cycle): the pops of one
// cycle beyond the first, which the harness's host carries over its Link one a cycle. {the pops' cycle, how many}, set
// between cycles by the harness, applied with that cycle's first pop (its Link word); unused (n 0) otherwise.
struct HostExtra { int64_t at = -1; uint32_t n = 0; };
constexpr unsigned HOST_EXTRA = 8;

struct ReqBusConfig {
    unsigned credit_latency = 3;            // D34: 0 exact as ref (one domain), 3 the spec (splittable); set explicitly
    unsigned hop_latency = 3;               // reqHopLatency
    unsigned fifo_depth = 128;              // requestFifoDepth
    unsigned queue_depth[TARGETS] = {128, 256, 128};   // receiveQueueDepth
};

struct Req {
    uint32_t w[4] = {};
    int64_t store_t = 0;
    uint32_t src = 0;
    uint32_t arrived = 0, sent = 0;
    uint32_t target() const { return (w[0] >> 7) & 7; }
};

inline sim::Word beat_w0(unsigned k, unsigned src, int64_t store_t) {
    return sim::Word(k) | sim::Word(src) << 3 | sim::Word(store_t) << 10;
}
inline unsigned beat_k(sim::Word w) { return unsigned(w & 7); }
inline unsigned beat_src(sim::Word w) { return unsigned(w >> 3) & 0x7F; }
inline int64_t beat_store_t(sim::Word w) { return int64_t(w >> 10); }

// ---------------------------------------------------------------- D29's count (mode 3)
// A sender (or the root, per target) blocked at cycle u with `view` beats in use (or 0 credits) would have had room in
// ref if the credits for forwards (pops) before u, still on their way at u, bring it under the limit. Recorded for the
// last few cycles; settled as the credits arrive (they carry the forward's cycle). Fixed size.
struct LagTracker {
    static constexpr int N = 8;
    int64_t at[N];          // a blocked cycle, or -1
    int64_t need[N];        // how many of the in-flight credits it would have taken to have room
    int64_t got[N];
    bool counted[N];
    uint64_t lag_cycles = 0;
    LagTracker() { for (int i = 0; i < N; i++) { at[i] = -1; need[i] = got[i] = 0; counted[i] = false; } }
    void blocked(int64_t u, int64_t need_) {
        const int i = int(u % N);
        at[i] = u; need[i] = need_; got[i] = 0; counted[i] = false;
    }
    void credit(int64_t forwarded_at) {   // a credit for a forward (or pop) at f: ref had it from f + 1
        for (int i = 0; i < N; i++)
            if (at[i] > forwarded_at && !counted[i] && ++got[i] >= need[i]) { counted[i] = true; lag_cycles++; }
    }
};

// ---------------------------------------------------------------- a sender's leaf
struct ReqLeaf {
    // ---- ports
    sim::LinkIn push;                 // width 2, from the core (Link(1))
    sim::LinkOut beat;                // width 3, to the node's input (Link(hop))
    sim::WireOut freed;               // to the core: the head freed at t (1 during t)
    sim::LinkOut freed_ev;            // width 0, to the register block (Link(1))
    sim::RegFileIn room;              // mode 0: the node's forwarded counts (its input `slot`)
    sim::LinkIn credit;               // mode 3: width 1, a forwarded beat's cycle (Link(3))
    // ---- configuration
    unsigned src = 0;                 // 0-63 a tile, 64-66 a controller
    unsigned slot = 0;                // the node's input index (published register)
    bool exact = false;               // mode 0
    bool stamp = false;               // a controller: the requestor 64 + c is stamped here (the root's input)
    unsigned depth = 128;
    // ---- state (RequestPort's FIFO and leaf link)
    struct Entry { uint32_t w[4]; int64_t store_t; };
    Entry* fifo = nullptr;
    unsigned head = 0, n = 0;
    unsigned beats_sent = 0;          // of the head request
    uint64_t sent_total = 0;          // beats sent on the leaf link
    uint64_t fwd_known = 0;           // mode 3: credits received (beats forwarded, as known here)
    bool freed_on = false;            // `freed` driven 1 this cycle
    int64_t freed_at = INT64_MIN;
    uint64_t sends = 0, overflows = 0;
    LagTracker lag;

    ReqLeaf(sim::Ports& p, unsigned src_, unsigned slot_, const ReqBusConfig& rc, bool stamp_)
        : src(src_), slot(slot_), exact(rc.credit_latency == 0), stamp(stamp_), depth(rc.fifo_depth) {
        p.in(push, "push", 2);
        p.out(beat, "beat", 3);
        p.out(freed, "freed");
        p.out(freed_ev, "freed_ev", 0);
        if (exact) p.in(room, "room"); else p.in(credit, "credit", 1);
        fifo = new Entry[depth];
    }
    ReqLeaf(const ReqLeaf&) = delete;
    ReqLeaf& operator=(const ReqLeaf&) = delete;
    ReqLeaf(ReqLeaf&& o) noexcept { *this = static_cast<ReqLeaf&&>(o); }
    ReqLeaf& operator=(ReqLeaf&& o) noexcept {
        if (this != &o) {
            push = o.push; beat = o.beat; freed = o.freed; freed_ev = o.freed_ev; room = o.room; credit = o.credit;
            src = o.src; slot = o.slot; exact = o.exact; stamp = o.stamp; depth = o.depth;
            delete[] fifo; fifo = o.fifo; o.fifo = nullptr;
            head = o.head; n = o.n; beats_sent = o.beats_sent; sent_total = o.sent_total; fwd_known = o.fwd_known;
            freed_on = o.freed_on; freed_at = o.freed_at; sends = o.sends; overflows = o.overflows; lag = o.lag;
        }
        return *this;
    }
    ~ReqLeaf() { delete[] fifo; }

    sim::Wake step(sim::Ctx& c) {
        const int64_t t = int64_t(c.now());
        if (freed_on) { c.drive(freed, 0); freed_on = false; }
        // the pushes of t - 1 (RequestPort.send at the end of t - 1)
        while (c.ready(push)) {
            const sim::Word* w = c.peek(push);
            if (n >= depth) {                 // a push into a full FIFO: the core checks canPush first (ref: violate)
                overflows++;
                sim::check::record(uint64_t(t - 1), int(src), sim::check::Kind::CTL_REQ_FULL);   // W26: ref's ctl.violate at the send
            }
            else {
                Entry& e = fifo[(head + n) % depth];
                e.w[0] = uint32_t(w[0]); e.w[1] = uint32_t(w[0] >> 32); e.w[2] = uint32_t(w[1]); e.w[3] = uint32_t(w[1] >> 32);
                e.store_t = t - 1;
                n++; sends++;
            }
            c.take(push);
        }
        // mode 3: the credits that arrived (each a beat forwarded)
        if (!exact)
            while (c.ready(credit)) {
                lag.credit(int64_t(c.peek(credit)[0]));
                fwd_known++;
                c.take(credit);
            }
        // the head's next beat, if its node's input has room (ref: occ < 8, as the node left it at t - 1)
        if (n > 0) {
            const uint64_t fwd = exact ? uint64_t(c.read(room, slot)) + (sent_total & ~uint64_t(0xFFFFFFFF)) : fwd_known;
            uint64_t f = fwd;
            if (exact && f > sent_total) f -= uint64_t(1) << 32;   // the register is the low 32 bits of the count
            const uint64_t occ = sent_total - f;
            if (occ < BUF_BEATS) {
                const Entry& e = fifo[head];
                uint32_t w0 = e.w[0];
                if (stamp) w0 = (w0 & ~0x7Fu) | src;
                c.put(beat, beat_w0(beats_sent, src, e.store_t), sim::Word(w0) | sim::Word(e.w[1]) << 32,
                      sim::Word(e.w[2]) | sim::Word(e.w[3]) << 32);
                sent_total++;
                if (++beats_sent == BEATS) {
                    head = (head + 1) % depth; n--; beats_sent = 0;
                    freed_at = t; freed_on = true;
                    c.drive(freed, 1);
                    c.put(freed_ev);
                }
            } else if (!exact) {
                lag.blocked(t, int64_t(occ - (BUF_BEATS - 1)));   // the in-flight credits that would have made room
            }
        }
        if (n > 0 || freed_on) return c.next();
        return sim::Wake::none();
    }
    uint64_t digest() const {
        uint64_t h = sent_total * 131 + fwd_known * 7 + n * 3 + beats_sent;
        for (unsigned k = 0; k < n; k++) h = h * 31 + fifo[(head + k) % depth].w[3] + uint64_t(fifo[(head + k) % depth].store_t);
        return h;
    }
};

// ---------------------------------------------------------------- a node: a row node or the root
struct ReqNode {
    static constexpr int MAX_IN = ROOT_INPUTS;
    // ---- ports
    sim::LinkIn in[MAX_IN];           // beats (Link(hop))
    sim::RegFileOut fwd;              // mode 0: per input, the beats forwarded through t - 1 (low 32 bits)
    sim::LinkOut credit_out[MAX_IN];  // mode 3: a credit per forwarded beat (Link(3))
    sim::LinkOut out;                 // a row node: beats to the root's input `row`
    sim::RegFileIn out_room;          // a row node, mode 0: the root's forwarded counts
    sim::LinkIn out_credit;           // a row node, mode 3: the root's credits
    sim::LinkOut deliver[TARGETS];    // the root: whole requests to the receive queues (Link(hop))
    sim::LinkIn qcredit[TARGETS];     // the root: a pop's credit, from the target's controller core (Link(1 | 3))
    // ---- configuration
    bool root = false, exact = false;
    unsigned row = 0, n_in = 0;
    uint32_t present = 0;             // the inputs declared (bit i)
    uint32_t qpresent = 0;            // the root: the queue credit inputs declared
    unsigned queue_depth[TARGETS] = {};
    // ---- state (RequestBus's Buf for each input, rowCur/rowRr or rootCur/rootRr, credits)
    struct Buf {
        Req r[2];
        unsigned head = 0, n = 0;
        uint64_t arrived_total = 0, fwd_total = 0;   // beats arrived; beats forwarded
        Req* head_req() { return n ? &r[head] : nullptr; }
    } buf[MAX_IN];
    int cur = -1, rr = 0;
    bool dropping = false;
    int64_t credits[8] = {};          // the root: per target (ref's credits[8]; 3-7 unused)
    uint64_t out_sent = 0, out_fwd_known = 0;   // a row node: beats sent to the root, and the root's forwards known
    uint64_t beats_at_root = 0, violations = 0, dropped = 0, delivered = 0;
    LagTracker lag;                   // a row node toward the root
    LagTracker qlag[TARGETS];         // the root: a target skipped for credit while a pop's credit was on its way
    HostExtra host_extra[TARGETS][HOST_EXTRA];   // the root: the harness's further pops' credits (harness-only)
    bool forwarded_now = false;

    // A row node r (inputs: bit c of `present`) or the root (row = 8: inputs 0-7 the row nodes, 8-10 the controllers).
    ReqNode(sim::Ports& p, bool root_, unsigned row_, uint32_t present_, uint32_t qpresent_, const ReqBusConfig& rc)
        : root(root_), exact(rc.credit_latency == 0), row(row_), n_in(root_ ? ROOT_INPUTS : 8), present(present_),
          qpresent(qpresent_) {
        static const char* kIn[ROOT_INPUTS] = {"in0", "in1", "in2", "in3", "in4", "in5", "in6", "in7", "in8", "in9", "in10"};
        static const char* kCr[ROOT_INPUTS] = {"cr0", "cr1", "cr2", "cr3", "cr4", "cr5", "cr6", "cr7", "cr8", "cr9", "cr10"};
        for (unsigned i = 0; i < n_in; i++)
            if (present >> i & 1) {
                p.in(in[i], kIn[i], 3);
                if (!exact) p.out(credit_out[i], kCr[i], 1);
            }
        if (exact) p.out(fwd, "fwd", n_in);
        if (root) {
            static const char* kDel[TARGETS] = {"deliver0", "deliver1", "deliver2"};
            static const char* kQc[TARGETS] = {"qcredit0", "qcredit1", "qcredit2"};
            for (int k = 0; k < TARGETS; k++) {
                if (qpresent >> k & 1) { p.out(deliver[k], kDel[k], 3); p.in(qcredit[k], kQc[k], 1); }
                queue_depth[k] = rc.queue_depth[k];
                credits[k] = rc.queue_depth[k];
            }
            rr = ROOT_INPUTS - 1;
        } else {
            p.out(out, "out", 3);
            if (exact) p.in(out_room, "out_room"); else p.in(out_credit, "out_credit", 1);
            rr = 7;
        }
    }

    // RequestBus.choose: the next input after `last` (round-robin) whose head has its first beat in; at the root, one
    // whose target has a credit or no queue.
    int choose(int64_t t) {
        uint32_t skipped = 0;
        for (unsigned i = 1; i <= n_in; i++) {
            const unsigned k = (unsigned(rr) + i) % n_in;
            Req* q = buf[k].head_req();
            if (!q || q->arrived == 0) continue;
            if (root) {
                const unsigned tg = q->target();
                if (tg < TARGETS && credits[tg] <= 0) { skipped |= 1u << tg; continue; }
            }
            if (root && !exact) note_skips(t, skipped);
            return int(k);
        }
        if (root && !exact) note_skips(t, skipped);
        return -1;
    }
    void note_skips(int64_t t, uint32_t skipped) {
        for (int tg = 0; tg < TARGETS; tg++)
            if (skipped >> tg & 1) qlag[tg].blocked(t, 1 - credits[tg]);
    }

    sim::Wake step(sim::Ctx& c) {
        const int64_t t = int64_t(c.now());
        // mode 0: publish the forwards through t - 1, before this cycle's (the senders at later levels see these)
        if (exact)
            for (unsigned i = 0; i < n_in; i++)
                if (present >> i & 1) c.write(fwd, i, uint32_t(buf[i].fwd_total));
        // the root: the pops' credits (usable from the cycle after the pop: Link(1) in mode 0)
        if (root)
            for (int k = 0; k < TARGETS; k++)
                if (qpresent >> k & 1)
                    while (c.ready(qcredit[k])) {
                        const int64_t pop = int64_t(c.peek(qcredit[k])[0]);
                        if (!exact) qlag[k].credit(pop);
                        credits[k]++;
                        c.take(qcredit[k]);
                        for (HostExtra& h : host_extra[k])        // the same cycle's further pops (harness-only)
                            if (h.n && h.at == pop) {
                                for (uint32_t j = 0; j < h.n; j++) { if (!exact) qlag[k].credit(pop); credits[k]++; }
                                h.n = 0; h.at = -1;
                            }
                    }
        // a row node, mode 3: the root's credits
        if (!root && !exact)
            while (c.ready(out_credit)) {
                lag.credit(int64_t(c.peek(out_credit)[0]));
                out_fwd_known++;
                c.take(out_credit);
            }
        // the beats arriving at t (Buf.arrive): a beat 0 brings its request
        for (unsigned i = 0; i < n_in; i++)
            if (present >> i & 1)
                while (c.ready(in[i])) {
                    const sim::Word* w = c.peek(in[i]);
                    Buf& b = buf[i];
                    if (beat_k(w[0]) == 0) {
                        if (b.n == 2) FATAL("reqbus: node %u input %u: a third request in an 8-beat buffer", row, i);
                        Req& q = b.r[(b.head + b.n) % 2];
                        q.w[0] = uint32_t(w[1]); q.w[1] = uint32_t(w[1] >> 32); q.w[2] = uint32_t(w[2]); q.w[3] = uint32_t(w[2] >> 32);
                        q.store_t = beat_store_t(w[0]); q.src = beat_src(w[0]); q.arrived = 0; q.sent = 0;
                        b.n++;
                    }
                    // the arriving beat belongs to the first request still arriving (ref: the first with arrived < 8)
                    for (unsigned j = 0; j < b.n; j++) {
                        Req& q = b.r[(b.head + j) % 2];
                        if (q.arrived < BEATS) { q.arrived++; break; }
                    }
                    b.arrived_total++;
                    c.take(in[i]);
                }
        forwarded_now = false;
        if (!root) row_step(c, t); else root_step(c, t);
        // mode 0: a forward at t is published at t + 1's step (the senders' room), so step then even if idle after it
        return busy() || (exact && forwarded_now) ? c.next() : sim::Wake::none();
    }
    bool busy() const {
        if (cur >= 0) return true;
        for (unsigned i = 0; i < n_in; i++) if (buf[i].n) return true;
        return false;
    }

    // A row node: wormhole, round-robin over inputs holding a first beat; stamps the requestor 8r + c.
    void row_step(sim::Ctx& c, int64_t t) {
        if (cur < 0) cur = choose(t);
        if (cur < 0) return;
        Buf& b = buf[cur];
        Req& q = *b.head_req();
        if (q.sent >= q.arrived) return;
        uint64_t f = exact ? uint64_t(c.read(out_room, row)) + (out_sent & ~uint64_t(0xFFFFFFFF)) : out_fwd_known;
        if (exact && f > out_sent) f -= uint64_t(1) << 32;
        const uint64_t occ = out_sent - f;
        if (occ >= BUF_BEATS) {
            if (!exact) lag.blocked(t, int64_t(occ - (BUF_BEATS - 1)));
            return;
        }
        const uint32_t w0 = (q.w[0] & ~0x7Fu) | (8 * row + unsigned(cur));
        c.put(out, beat_w0(q.sent, q.src, q.store_t), sim::Word(w0) | sim::Word(q.w[1]) << 32,
              sim::Word(q.w[2]) | sim::Word(q.w[3]) << 32);
        out_sent++;
        forwarded(c, cur, t);
        if (++q.sent == BEATS) { pop_head(b); rr = cur; cur = -1; }
    }

    // The root: round-robin skipping a head whose target has no credit; one beat a cycle; whole requests into the queues.
    void root_step(sim::Ctx& c, int64_t t) {
        if (cur < 0) {
            cur = choose(t);
            if (cur >= 0) {
                const unsigned tg = buf[cur].head_req()->target();
                dropping = tg >= TARGETS;
                if (dropping) {
                    violations++;
                    sim::check::record(uint64_t(t), -1, sim::check::Kind::REQBUS_NO_RECEIVER, buf[cur].head_req()->src, tg);   // W26
                }
                else credits[tg]--;
            }
        }
        if (cur < 0) return;
        Buf& b = buf[cur];
        Req& q = *b.head_req();
        if (q.sent >= q.arrived) return;
        beats_at_root++;
        forwarded(c, cur, t);
        if (++q.sent == BEATS) {
            if (dropping) dropped++;
            else {
                if (!(qpresent >> q.target() & 1))
                    FATAL("reqbus: a request for target %u at %lld, which has no receive queue in this build", q.target(), (long long)t);
                c.put(deliver[q.target()], beat_w0(7, q.src, q.store_t), sim::Word(q.w[0]) | sim::Word(q.w[1]) << 32,
                      sim::Word(q.w[2]) | sim::Word(q.w[3]) << 32);
                delivered++;
            }
            pop_head(b); rr = cur; cur = -1;
        }
    }
    void forwarded(sim::Ctx& c, int i, int64_t t) {
        buf[i].fwd_total++;
        forwarded_now = true;
        if (!exact) c.put(credit_out[i], sim::Word(t));
    }
    static void pop_head(Buf& b) { b.head = (b.head + 1) % 2; b.n--; }
    uint64_t lag_cycles() const {
        uint64_t s = lag.lag_cycles;
        for (int k = 0; k < TARGETS; k++) s += qlag[k].lag_cycles;
        return s;
    }
    uint64_t digest() const {
        uint64_t h = uint64_t(cur + 1) * 131 + uint64_t(rr) * 7 + out_sent + beats_at_root * 3;
        for (unsigned i = 0; i < n_in; i++) h = h * 31 + buf[i].arrived_total * 5 + buf[i].fwd_total;
        for (int k = 0; k < TARGETS; k++) h = h * 17 + uint64_t(credits[k]);
        return h;
    }
};

// The root is its own type: the framework orders levels per type (mode 0: root < rows < leaves).
struct ReqRoot : ReqNode {
    using ReqNode::ReqNode;
};

// ---------------------------------------------------------------- a target's receive queue
struct ReqRecv {
    enum : unsigned { R_PENDING = 0, R_W0 = 1, R_W1, R_W2, R_W3, REGS };
    sim::LinkIn deliver;              // width 3, from the root (Link(hop))
    sim::LinkIn pop;                  // width 0, from the controller core (Link(1)): a REQUEST_RECEIVE of t - 1
    sim::RegFileOut head;             // to the controller core: the head as a load at t sees it
    unsigned target = 0, depth = 128;
    struct Entry { uint32_t w[4]; int64_t delivered_t, store_t; };
    Entry* q = nullptr;
    unsigned qh = 0, qn = 0;
    uint32_t pub[REGS] = {};
    uint64_t received = 0, violations = 0;
    HostExtra host_extra[HOST_EXTRA];  // the harness's further pops of one cycle (harness-only)
    std::vector<std::array<int64_t, 6>>* dlog = nullptr;   // W20 (harness-only): the deliveries, into the harness's vector
                                                          //   (reserved at configuration: no allocation in the run)

    ReqRecv(sim::Ports& p, unsigned target_, const ReqBusConfig& rc) : target(target_), depth(rc.queue_depth[target_]) {
        p.in(deliver, "deliver", 3);
        p.in(pop, "pop", 0);
        p.out(head, "head", REGS);
        q = new Entry[depth];
    }
    ReqRecv(const ReqRecv&) = delete;
    ReqRecv& operator=(const ReqRecv&) = delete;
    ReqRecv(ReqRecv&& o) noexcept { *this = static_cast<ReqRecv&&>(o); }
    ReqRecv& operator=(ReqRecv&& o) noexcept {
        if (this != &o) {
            deliver = o.deliver; pop = o.pop; head = o.head; target = o.target; depth = o.depth;
            delete[] q; q = o.q; o.q = nullptr;
            qh = o.qh; qn = o.qn; received = o.received; violations = o.violations; dlog = o.dlog;
            for (unsigned i = 0; i < HOST_EXTRA; i++) host_extra[i] = o.host_extra[i];
            for (unsigned i = 0; i < REGS; i++) pub[i] = o.pub[i];
        }
        return *this;
    }
    ~ReqRecv() { delete[] q; }

    sim::Wake step(sim::Ctx& c) {
        const int64_t t = int64_t(c.now());
        // the pops of t - 1 (RequestBus.receive: popped at once in ref; the next load is at t at the earliest)
        while (c.ready(pop)) {
            if (qn == 0 || q[qh].delivered_t >= t - 1) violations++;   // the core pops only a visible head
            else { qh = (qh + 1) % depth; qn--; received++; }
            c.take(pop);
            for (HostExtra& h : host_extra)                   // the same cycle's further pops (harness-only)
                if (h.n && h.at == t - 1) {
                    for (uint32_t j = 0; j < h.n; j++) {
                        if (qn == 0 || q[qh].delivered_t >= t - 1) violations++;
                        else { qh = (qh + 1) % depth; qn--; received++; }
                    }
                    h.n = 0; h.at = -1;
                }
        }
        // deliveries arriving at t (ref's toReceiver drained at the start of runCycle(t)): visible from t + 1
        while (c.ready(deliver)) {
            const sim::Word* w = c.peek(deliver);
            if (dlog) {                      // W20 (harness-only; W22-19): ref's delivered list, {deliveredAt, srcTile, w0..w3}
                if (dlog->size() == dlog->capacity()) FATAL("reqbus: the harness's delivery log is full (%zu)", dlog->size());
                dlog->push_back({t, int64_t(beat_src(w[0])), int64_t(uint32_t(w[1])), int64_t(w[1] >> 32), int64_t(uint32_t(w[2])), int64_t(w[2] >> 32)});
            }
            if (qn >= depth) {               // ref: "the root's credits should prevent it"
                violations++;
                sim::check::record(uint64_t(t), -1, sim::check::Kind::REQBUS_QUEUE_OVERFLOW, target);   // W26
            }
            else {
                Entry& e = q[(qh + qn) % depth];
                e.w[0] = uint32_t(w[1]); e.w[1] = uint32_t(w[1] >> 32); e.w[2] = uint32_t(w[2]); e.w[3] = uint32_t(w[2] >> 32);
                e.delivered_t = t; e.store_t = beat_store_t(w[0]);
                qn++;
            }
            c.take(deliver);
        }
        // the head as a load at t sees it (delivered before t)
        const bool vis = qn > 0 && q[qh].delivered_t < t;
        uint32_t v[REGS] = {vis ? 1u : 0u, vis ? q[qh].w[0] : 0, vis ? q[qh].w[1] : 0, vis ? q[qh].w[2] : 0, vis ? q[qh].w[3] : 0};
        for (unsigned i = 0; i < REGS; i++) if (v[i] != pub[i]) { pub[i] = v[i]; c.write(head, i, v[i]); }
        // a head delivered at t becomes visible at t + 1
        if (qn > 0 && !vis) return c.next();
        return sim::Wake::none();
    }
    uint64_t digest() const { return received * 131 + qn * 7 + qh + violations; }
};

// ---------------------------------------------------------------- the builder
// The bus for the senders present (bit k of `tiles`: tile k; bit c of `ccs`: C&C c + 1, which also gets its receive
// queue): leaves "t<k>.reqleaf" / "cc<c+1>.reqleaf", the nodes "reqbus.row<r>" and "reqbus.root", the queues
// "cc<c+1>.reqrecv". The caller wires each leaf's `push` (Link(1)) from its core, `freed` (Wire) to its core and
// `freed_ev` (Link(1)) to its register block; each queue's `head` (RegFile) to its controller core and `pop` (Link(1))
// from it; and the controller core's queue credit into `root->qcredit[c]` with `queue_credit_latency()`.
struct ReqBusIO {
    ReqLeaf* leaf[SENDERS] = {};
    ReqNode* row[ROWS] = {};
    ReqNode* root = nullptr;
    ReqRecv* recv[TARGETS] = {};
    ReqBusConfig rc;
    unsigned queue_credit_latency() const { return rc.credit_latency == 0 ? 1 : rc.credit_latency; }
};
inline const char* leaf_name(unsigned k, char* buf) {   // buf: 16 bytes
    if (k < 64) { buf[0] = 't'; int n = 1; if (k >= 10) buf[n++] = char('0' + k / 10); buf[n++] = char('0' + k % 10);
                  const char* s = ".reqleaf"; while (*s) buf[n++] = *s++; buf[n] = 0; }
    else { const char* s = "cc?.reqleaf"; int n = 0; while (*s) buf[n++] = *s++; buf[n] = 0; buf[2] = char('1' + (k - 64)); }
    return buf;
}
// Thread domains (W4), when the machine is split: the nodes' domain, each leaf's (its tile's), each queue's (its
// controller's). Without one, every unit goes in the current domain. Mode 0 split is refused at build (a RegFile or Wire
// across domains), as D34 wants; the caller sets cfg.domain again after the call if it needs to.
struct ReqBusPlacement {
    unsigned bus = 0;
    unsigned leaf[SENDERS] = {};
    unsigned recv[TARGETS] = {};
};
inline ReqBusIO add_reqbus(sim::Config& cfg, const ReqBusConfig& rc, uint64_t tiles, unsigned ccs,
                           const ReqBusPlacement* place = nullptr, bool cc_leaves = true) {   // (W20: false, ReqHost's)
    if (rc.credit_latency != 0 && rc.credit_latency != 3)
        FATAL("reqbus: credit latency %u: 0 (exact, D34) or 3 (the spec)", rc.credit_latency);
    ReqBusIO io;
    io.rc = rc;
    const unsigned hop = rc.hop_latency, cl = rc.credit_latency;
    if (place) cfg.domain(place->bus);
    io.root = &cfg.add<ReqRoot>("reqbus.root", true, 8u, 0xFFu | uint32_t(ccs & 7) << 8, uint32_t(ccs & 7), rc);
    for (unsigned r = 0; r < ROWS; r++) {
        static const char* kRow[ROWS] = {"reqbus.row0", "reqbus.row1", "reqbus.row2", "reqbus.row3",
                                          "reqbus.row4", "reqbus.row5", "reqbus.row6", "reqbus.row7"};
        io.row[r] = &cfg.add<ReqNode>(kRow[r], false, r, uint32_t(tiles >> (8 * r)) & 0xFF, 0u, rc);
        cfg.link(io.row[r]->out, io.root->in[r], hop);
        if (cl == 0) cfg.connect(io.root->fwd, io.row[r]->out_room);
        else cfg.link(io.root->credit_out[r], io.row[r]->out_credit, cl);
    }
    char nm[16];
    for (unsigned k = 0; k < SENDERS; k++) {
        const bool tile = k < 64;
        if (tile ? !(tiles >> k & 1) : !(ccs >> (k - 64) & 1) || !cc_leaves) continue;
        ReqNode& node = tile ? *io.row[k / 8] : *io.root;
        const unsigned slot = tile ? k % 8 : 8 + (k - 64);
        if (place) cfg.domain(place->leaf[k]);
        io.leaf[k] = &cfg.add<ReqLeaf>(leaf_name(k, nm), k, slot, rc, !tile);
        cfg.link(io.leaf[k]->beat, node.in[slot], hop);
        if (cl == 0) cfg.connect(node.fwd, io.leaf[k]->room);
        else cfg.link(node.credit_out[slot], io.leaf[k]->credit, cl);
    }
    for (unsigned c = 0; c < TARGETS; c++) {
        if (!(ccs >> c & 1)) continue;
        static const char* kRecv[TARGETS] = {"cc1.reqrecv", "cc2.reqrecv", "cc3.reqrecv"};
        if (place) cfg.domain(place->recv[c]);
        io.recv[c] = &cfg.add<ReqRecv>(kRecv[c], c, rc);
        cfg.link(io.root->deliver[c], io.recv[c]->deliver, hop);
    }
    return io;
}
// Wires sender k (a tile 0-63, a controller 64 + c) to its leaf, and a controller's receive side to its queue. `Req` is
// chip.h's ReqIO (the control's request side).
template <class Req>
void wire_sender(sim::Config& cfg, const ReqBusIO& io, unsigned k, Req& r) {
    ReqLeaf& l = *io.leaf[k];
    cfg.link(r.push, l.push, 1);
    cfg.connect(l.freed, r.freed);
    cfg.link(l.freed_ev, r.freed_ev, 1);
    if (r.recv) {
        if (k < 64 || !io.recv[k - 64]) FATAL("reqbus: sender %u has a receive side but no queue", k);
        ReqRecv& q = *io.recv[k - 64];
        cfg.connect(q.head, r.head);
        cfg.link(r.pop, q.pop, 1);
        cfg.link(r.qcredit, io.root->qcredit[k - 64], io.queue_credit_latency());
    }
}

// W20 (harness-only; P2, W22-10): ref's RequestBus driven from a test on the machine with C&C's Java model
// (RequestBusTest's Net(Config.machine())): `controllerSend` (a request entering the root's controller input 8 + c, its
// eight beats arriving one a cycle from t + hop, ref's: no request port in between) and the receive queues' pops (ref's
// `receive`: popped at the end of t, the credit back to the root at t + 1). The harness (sim/tests/mach.cpp) puts beat 0
// as of the cycle just run (Sim::put_late) and leaves beats 1-7 and the pops to this unit. Built only when asked
// (ChipSpec::reqbus_host); the machine with C&C 1-3 as cores has their cores' ports instead.
struct ReqHost {
    sim::LinkOut beat[TARGETS];       // to the root's input 8 + c (Link(hop))
    sim::LinkIn credit[TARGETS];      // mode 3: the root's credits for those inputs (consumed, unused: ref has none)
    sim::LinkOut pop[TARGETS];        // to the receive queue (Link(1)): ref's receive at t
    sim::LinkOut qcredit[TARGETS];    // to the root (Link(queue credit latency))
    sim::RegFileIn head[TARGETS];     // the receive queues' heads (wired, unread: the harness reads the queues themselves)
    bool exact = false;
    struct Send { uint32_t w[4]; int64_t store_t; unsigned next; bool on; };
    Send send[TARGETS] = {};
    int64_t pop_at[TARGETS] = {-1, -1, -1};
    explicit ReqHost(sim::Ports& p, const ReqBusConfig& rc) : exact(rc.credit_latency == 0) {
        static const char* kB[TARGETS] = {"beat0", "beat1", "beat2"}, *kC[TARGETS] = {"credit0", "credit1", "credit2"};
        static const char* kP[TARGETS] = {"pop0", "pop1", "pop2"}, *kQ[TARGETS] = {"qcredit0", "qcredit1", "qcredit2"};
        for (unsigned c = 0; c < TARGETS; c++) {
            p.out(beat[c], kB[c], 3);
            if (!exact) p.in(credit[c], kC[c], 1);
            p.out(pop[c], kP[c], 0);
            p.out(qcredit[c], kQ[c], 1);
            static const char* kH[TARGETS] = {"head0", "head1", "head2"};
            p.in(head[c], kH[c]);
        }
    }
    sim::Wake step(sim::Ctx& c) {
        const int64_t t = int64_t(c.now());
        bool more = false;
        for (unsigned k = 0; k < TARGETS; k++) {
            if (!exact) while (c.ready(credit[k])) c.take(credit[k]);
            Send& s = send[k];
            if (s.on && s.next < BEATS) {
                c.put(beat[k], beat_w0(s.next, 64 + k, s.store_t), sim::Word(s.w[0]) | sim::Word(s.w[1]) << 32,
                      sim::Word(s.w[2]) | sim::Word(s.w[3]) << 32);
                if (++s.next == BEATS) s.on = false;
            }
            more = more || s.on;
            if (pop_at[k] == t) { c.put(pop[k]); c.put(qcredit[k], sim::Word(t)); pop_at[k] = -1; }
            else if (pop_at[k] >= 0) more = true;
        }
        return more ? c.next() : sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};

// A control built without the bus (a lone tile's tests): its request side tied off. A REQUEST_SEND is FATAL there
// (nothing to carry it); a controller's queue reads empty.
struct ReqTie {
    sim::LinkIn push, pop, qcredit;
    sim::WireOut freed;
    sim::LinkOut freed_ev;
    sim::RegFileOut head;
    bool recv = false;
    ReqTie(sim::Ports& p, bool recv_) : recv(recv_) {
        p.in(push, "push", 2); p.out(freed, "freed"); p.out(freed_ev, "freed_ev", 0);
        if (recv) { p.out(head, "head", ReqRecv::REGS); p.in(pop, "pop", 0); p.in(qcredit, "qcredit", 1); }
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(push)) FATAL("reqbus: a REQUEST_SEND at cycle %llu in a build without the request bus", (unsigned long long)c.now());
        if (recv && (c.ready(pop) || c.ready(qcredit))) FATAL("reqbus: a REQUEST_RECEIVE pop in a build without the request bus");
        return sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};
template <class Req>
void tie_off(sim::Config& cfg, const std::string& name, Req& r) {
    if (!r.present) return;
    ReqTie& z = cfg.add<ReqTie>(name, r.recv);
    cfg.link(r.push, z.push, 1);
    cfg.connect(z.freed, r.freed);
    cfg.link(z.freed_ev, r.freed_ev, 1);
    if (r.recv) { cfg.connect(z.head, r.head); cfg.link(r.pop, z.pop, 1); cfg.link(r.qcredit, z.qcredit, 1); }
}

// The units as built (the ReqBusIO pointers are for wiring only: the build moves units into their arena).
struct ReqBusUnits {
    ReqLeaf* leaf[SENDERS] = {};
    ReqNode* row[ROWS] = {};
    ReqNode* root = nullptr;
    ReqRecv* recv[TARGETS] = {};
    ReqHost* host = nullptr;          // the harness's host (the test's receives and controller sends), if built
    bool exact = false;
};
inline ReqBusUnits resolve(sim::Sim& m, uint64_t tiles, unsigned ccs, const ReqBusConfig& rc) {
    ReqBusUnits u;
    u.exact = rc.credit_latency == 0;
    u.root = &m.unit<ReqRoot>("reqbus.root");
    static const char* kRow[ROWS] = {"reqbus.row0", "reqbus.row1", "reqbus.row2", "reqbus.row3",
                                      "reqbus.row4", "reqbus.row5", "reqbus.row6", "reqbus.row7"};
    for (unsigned r = 0; r < ROWS; r++) u.row[r] = &m.unit<ReqNode>(kRow[r]);
    char nm[16];
    for (unsigned k = 0; k < SENDERS; k++)
        if (k < 64 ? (tiles >> k & 1) : (ccs >> (k - 64) & 1)) u.leaf[k] = &m.unit<ReqLeaf>(leaf_name(k, nm));
    static const char* kRecv[TARGETS] = {"cc1.reqrecv", "cc2.reqrecv", "cc3.reqrecv"};
    for (unsigned c = 0; c < TARGETS; c++) if (ccs >> c & 1) u.recv[c] = &m.unit<ReqRecv>(kRecv[c]);
    return u;
}
// The D29 count (mode 3): the cycles where the bus would have differed from ref (0 in mode 0). Every run reports it.
inline uint64_t lag_cycles(const ReqBusUnits& u) {
    uint64_t s = u.root ? u.root->lag_cycles() : 0;
    for (unsigned r = 0; r < ROWS; r++) if (u.row[r]) s += u.row[r]->lag_cycles();
    for (unsigned k = 0; k < SENDERS; k++) if (u.leaf[k]) s += u.leaf[k]->lag.lag_cycles;
    return s;
}

}  // namespace reqbus
}  // namespace units
