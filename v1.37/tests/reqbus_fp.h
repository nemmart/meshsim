// reqbus_fp.h: the request bus's canonical state as ref's StateFp writes it (AUDIT-M2/M3 section 6: `reqbus`
// and each port's `req`), from the C++ units (src/units/reqbus.h) at the end of cycle c, between runs.
//
// ref's bus counts a beat in its node input from the cycle it is sent (`occ`, `arrivals`), a request in its input from
// its first beat's send, a pop at once (and its credit as usable from c + 1), and a delivery in `toReceiver` until it
// lands; here those are in flight on the Links. The writer reads them with the port observer (Sim::observe, the machine
// built with Options::observe_arrivals) and writes ref's view: in mode 0 that is ref's state exactly; in mode 3 also,
// as long as `lag_cycles` is 0 (a credit in flight is counted where ref would have had it).
#pragma once

#include <cstdint>
#include <vector>

#include "config.h"
#include "units/reqbus.h"

namespace reqfp {

using units::reqbus::BEATS;
using units::reqbus::Req;
using units::reqbus::ReqBusUnits;
using units::reqbus::ReqLeaf;
using units::reqbus::ReqNode;

template <class Out>
void req(Out& o, const Req& q) {
    for (int i = 0; i < 4; i++) o.w("w", q.w[i]);
    o.w("storeT", uint64_t(q.store_t));
    o.w("src", q.src);
    o.w("arrived", q.arrived);
    o.w("sent", q.sent);
}

// A node input as ref's Buf: the requests and beats in it, then those on their way (the Link into it).
template <class Out>
void buf(Out& o, const sim::Sim& m, const ReqNode& n, unsigned i) {
    constexpr uint32_t kMax = 16;
    sim::PortEntry e[kMax];
    uint32_t k = 0;
    if (n.present >> i & 1) k = m.observe(n.in[i], e, kMax).n;
    const ReqNode::Buf& b = n.buf[i];
    o.w("occ", (b.arrived_total - b.fwd_total) + k);
    // a request whose first beat is still on its way
    bool fresh = false;
    Req nq;
    for (uint32_t j = 0; j < k; j++)
        if (units::reqbus::beat_k(e[j].w[0]) == 0) {
            fresh = true;
            nq.w[0] = uint32_t(e[j].w[1]); nq.w[1] = uint32_t(e[j].w[1] >> 32);
            nq.w[2] = uint32_t(e[j].w[2]); nq.w[3] = uint32_t(e[j].w[2] >> 32);
            nq.store_t = units::reqbus::beat_store_t(e[j].w[0]);
            nq.src = units::reqbus::beat_src(e[j].w[0]);
        }
    o.w("reqs", b.n + (fresh ? 1 : 0));
    for (unsigned j = 0; j < b.n; j++) req(o, b.r[(b.head + j) % 2]);
    if (fresh) req(o, nq);
    o.w("arrivals", k);
    for (uint32_t j = 0; j < k; j++) o.w("arrival", e[j].at);
}

// StateFp.reqbus. `qpop[c]`: no longer needed (the pops are observed); `c` the cycle.
template <class Out>
void reqbus(Out& o, const sim::Sim& m, const ReqBusUnits& u, uint64_t c) {
    for (unsigned r = 0; r < 8; r++)
        for (unsigned k = 0; k < 8; k++) buf(o, m, *u.row[r], k);
    for (unsigned k = 0; k < 11; k++) buf(o, m, *u.root, k);
    for (unsigned r = 0; r < 8; r++) { o.w("rowCur", uint64_t(int64_t(u.row[r]->cur))); o.w("rowRr", uint64_t(int64_t(u.row[r]->rr))); }
    o.w("rootCur", uint64_t(int64_t(u.root->cur)));
    o.w("rootRr", uint64_t(int64_t(u.root->rr)));
    o.w("rootDropping", u.root->dropping ? 1 : 0);
    // the queue credits: in flight here, usable in ref from the cycle after the pop
    constexpr uint32_t kMax = 64;
    sim::PortEntry e[kMax];
    int64_t cred[8] = {};
    std::vector<uint64_t> pending_t[3];
    for (int k = 0; k < 3; k++) {
        cred[k] = u.root->credits[k];
        if (!(u.root->qpresent >> k & 1)) continue;
        const uint32_t n = m.observe(u.root->qcredit[k], e, kMax).n;
        for (uint32_t j = 0; j < n; j++) {
            const uint64_t pop = e[j].w[0];
            if (pop + 1 <= c) cred[k]++;                // ref applied it at pop + 1
            else pending_t[k].push_back(pop + 1);
        }
        if (u.host && u.host->pop_at[k] >= 0) {          // the test's pop of c + 1, made in ref already: not yet on its Link
            const uint64_t pop = uint64_t(u.host->pop_at[k]);
            if (pop + 1 <= c) cred[k]++;
            else pending_t[k].push_back(pop + 1);
        }
        for (const units::reqbus::HostExtra& h : u.root->host_extra[k])   // the harness's further pops of a cycle
            for (uint32_t j = 0; j < h.n; j++) {
                if (uint64_t(h.at) + 1 <= c) cred[k]++;
                else pending_t[k].push_back(uint64_t(h.at) + 1);
            }
    }
    for (int k = 0; k < 8; k++) o.w("credits", uint64_t(cred[k]));
    // the queues: as delivered, less the heads popped at c (their pop still on its way here)
    for (int k = 0; k < 3; k++) {
        const units::reqbus::ReqRecv* q = u.recv[k];
        if (!q) { o.w("queue.n", 0); continue; }
        uint32_t popped = m.observe(q->pop, e, kMax).n;
        for (const units::reqbus::HostExtra& h : q->host_extra) popped += h.n;
        if (u.host && u.host->pop_at[k] >= 0) popped++;
        if (popped > q->qn) FATAL("reqfp: more pops in flight than requests queued");
        o.w("queue.n", q->qn - popped);
        for (unsigned j = popped; j < q->qn; j++) {
            const auto& x = q->q[(q->qh + j) % q->depth];
            for (int i = 0; i < 4; i++) o.w("queue", x.w[i]);
            o.w("queue", uint64_t(x.delivered_t));
            o.w("queue", uint64_t(x.store_t));
        }
    }
    // the credit returns ref holds: the pops of c (in target order: a cycle's pops are made in that order)
    const uint64_t nret = pending_t[0].size() + pending_t[1].size() + pending_t[2].size();
    o.w("creditReturns.n", nret);
    for (int k = 0; k < 3; k++)
        for (uint64_t at : pending_t[k]) { o.w("creditReturns", at); o.w("creditReturns", uint64_t(k)); }
    // the deliveries on their way (ref's toReceiver), in arrival order
    struct D { uint64_t at; Req q; } d[8];
    uint32_t nd = 0;
    for (int k = 0; k < 3; k++) {
        if (!u.recv[k]) continue;
        const uint32_t n = m.observe(u.recv[k]->deliver, e, kMax).n;
        for (uint32_t j = 0; j < n; j++) {
            if (nd == 8) FATAL("reqfp: more than 8 deliveries in flight");
            D& x = d[nd++];
            x.at = e[j].at;
            x.q.w[0] = uint32_t(e[j].w[1]); x.q.w[1] = uint32_t(e[j].w[1] >> 32);
            x.q.w[2] = uint32_t(e[j].w[2]); x.q.w[3] = uint32_t(e[j].w[2] >> 32);
            x.q.store_t = units::reqbus::beat_store_t(e[j].w[0]);
            x.q.src = units::reqbus::beat_src(e[j].w[0]);
            x.q.arrived = x.q.sent = BEATS;
        }
    }
    for (uint32_t i = 1; i < nd; i++)
        for (uint32_t j = i; j > 0 && d[j - 1].at > d[j].at; j--) { D tmp = d[j]; d[j] = d[j - 1]; d[j - 1] = tmp; }
    o.w("toReceiver.n", nd);
    for (uint32_t i = 0; i < nd; i++) { o.w("toReceiver.at", d[i].at); req(o, d[i].q); }
}

// StateFp.req (a port): its FIFO (with the push of c still on its way), the head's beats sent, REQUEST_SET_W0-W2.
template <class Out>
void port(Out& o, const sim::Sim& m, const ReqLeaf* l, const uint32_t set_w[3], uint64_t c) {
    constexpr uint32_t kMax = 4;
    sim::PortEntry e[kMax];
    const uint32_t k = l ? m.observe(l->push, e, kMax).n : 0;
    const unsigned n = l ? l->n : 0;
    o.w("fifo.n", n + k);
    for (unsigned j = 0; j < n; j++) {
        const auto& x = l->fifo[(l->head + j) % l->depth];
        for (int i = 0; i < 4; i++) o.w("fifo", x.w[i]);
        o.w("fifo", uint64_t(x.store_t));
    }
    for (uint32_t j = 0; j < k; j++) {
        o.w("fifo", uint32_t(e[j].w[0])); o.w("fifo", uint32_t(e[j].w[0] >> 32));
        o.w("fifo", uint32_t(e[j].w[1])); o.w("fifo", uint32_t(e[j].w[1] >> 32));
        o.w("fifo", e[j].at - 1);                        // pushed at c (Link(1)): its store cycle
    }
    o.w("beatsSent", l ? l->beats_sent : 0);
    for (int i = 0; i < 3; i++) o.wi("setW", unsigned(i), set_w[i]);
    (void)c;
}

}  // namespace reqfp
