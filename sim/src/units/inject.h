// inject.h (WORKER9, G2; the read side is G3's): the injection point and the loopback tokens (spec 4.5; ref's
// InjectionPath.java and the token part of SRAMReadQueue.java; AUDIT-M2 3.2). Declarations: the ports and the state;
// the step comes with stage 2.
//
// ONE UNIT FOR THE READ QUEUE AND THE INJECTION POINT (AUDIT-M2 section 2, item 2): ref runs `srq` then `inj` as one
// component because they share the token pool. Here the send side and the tokens are built first (G2); G3 adds the SRAM
// read queue (clients, issue, area returns, the token free list, the read-return holding register, the read port's
// `injectedAt`) as the member `rs` with its own ports and its own phase at the start of the step, without restructuring:
//
//     step(t):  frees of t - 1 (loop_left) applied; start-of-cycle token count fixed       (ref: SRAMReadQueue.advance)
//               rs.step(...)  [G3: area returns, one release into the read return, one issue]
//               inject: a read return first (rs), else the send hold's packet; a send for the tile itself or a
//                       broadcast takes a token (takeForSend: room by the count at the start of t) or waits
//               the send-hold status Reg (below)
//
// PORTS
//   in   send        HoldIn, 3 words: W8's core's send holding register (a refused put is retried by the core at t + 1).
//        loop_left   LinkIn, width 0: from the router, a token freed (free from the cycle it arrives).
//   out  to_router   LinkOut, 3 words, latency intra_tile_latency: into the router's loopback route block (src unset).
//        send_busy   RegOut, 1 word, to W8's register block: the send hold OCCUPIED at the end of t (after t's take).
//                    ref's STATUS bit is `!sendHold.isEmpty(t)` at endCycle(t) = occupied at the end of t OR a put
//                    made at t (WordHold.isEmpty: `!occ && !pendPut`). The put of t is the core's (a send store or a
//                    retry), which only the core knows at t: W8's block ORs it in (it reaches the block with the store).
//                    The Reg is put only when the value changes, so it costs nothing on a quiet tile.
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "check.h"
#include "sim.h"
#include "units/net.h"

namespace units {

// The token pool (4.5): one pool of read_tokens shared by the reads (G3) and the send port's loopback packets. Room is
// judged on the count at the start of the cycle plus what was taken during it (SRAMReadQueue.tokenFree): a token freed
// in a cycle is free from the next.
struct Tokens {
    uint32_t cap = 64;
    uint32_t reads = 0;        // held by reads (issued, not yet injected): G3's
    uint32_t loop_held = 0;    // held by packets on their way through the loopback FIFO
    uint32_t start = 0;        // held at the start of this cycle (fixed by the step's first phase)
    uint32_t taken = 0;        // taken during this cycle (a read's issue, a send)
    bool free() const { return start + taken < cap; }
    uint64_t digest() const { return uint64_t(reads) | uint64_t(loop_held) << 32; }
};

// G3's part (W11): the SRAM read queue (SRAMReadQueue.java), its clients and the read side of the injection.
//
// Configuration: present only in a tile with G3's units (ControlIO::mem); clients 0 (the gather engine) and 1 (the
// address generator) only when their units have their read Holds (W12), client 2 (the read port: the core's
// Fifo(read_port_depth)) with the read side.
//
// The step at t (after the frees of t - 1 and the start-of-cycle count, Injection::step):
//   1. area returns: each area's Link(sram_read_latency) {tok | part << 16, lo, hi}: the read's chunks placed, ready at t;
//   2. one release into the read-return holding register (inside this unit: ref's WordHold, put at t, injected at t + 1,
//      never refused since the injection takes it first): the gather's head, the AG's head (each in issue order, ready),
//      else the read port's earliest ready entry (by ready cycle, then token: ref's TreeSet);
//   3. issue: a refused SRAM read Hold is put again and nothing issues; else, with a token free, the first client with
//      a request is taken and issued: a token from the head of the free list (ref's ArrayDeque order), its request to
//      the big SRAM's read Hold. A misaligned read is undefined (fatal, named); one past mem_chunks or of size 11 is a
//      zero-width entry (payload 0), ready at t + 2 + sram_read_latency.
// `injected` (the injection unit injected a read return at t): reads - 1, the token to the free list's tail, held on as
// a loopback token if the packet loops; a read-port return counts on `rp_injected` (ReadPort.injectedAt).
struct ReadSideConfig {
    bool present = false;
    bool gather = false, ag = false;   // W12's clients
    unsigned read_port_depth = 64;
    unsigned sram_read_latency = 2;
    uint32_t mem_chunks = 1u << 20;
};

// W20 (harness-only; P2): ref's Net harness feeding a tile (sim/tests/mach.h, Net::at and Net::rd). Null on every machine
// but the harness's coreless Net machine, so nothing here runs otherwise. The rings are the harness's (allocated at
// configuration, fixed size: a push past them is fatal in the harness, between cycles). Entries are {T, w0, w1, w2}.
//   ret     packets for the read-return holding register (Net.at): at the read side's step at u, the head with T <= u
//           becomes the register's word visible at u, as ref's feed() put it at u - 1 (`late` counts T < u); a srq
//           release of u - 1 at the same time would be ref's double put: fatal
//   cl[k]   read-queue client k's requests (Net.rd, ref's testReadClients): at most one moved a cycle into the client's
//           FIFO (the ring's first `vis` entries), at the step of u when T <= u and fewer than 60 are in it (ref's
//           `f.size(t) >= 60`); the FIFO replaces the three clients' inputs in the issue
struct HarnessFeed {
    struct Ring {
        uint64_t (*e)[4] = nullptr;
        uint32_t cap = 0, head = 0, n = 0, vis = 0;
        const uint64_t* at(uint32_t i) const { return e[(head + i) % cap]; }
        void pop() { head = (head + 1) % cap; n--; }
    };
    Ring ret, cl[3];
    bool clients = false;          // ref's testReadClients: the FIFOs above are the read queue's three clients
    uint64_t late = 0;
    bool pending() const { return ret.n || cl[0].n || cl[1].n || cl[2].n; }
};

struct InjReadSide {
    static constexpr unsigned MAX = 64, AREAS = 16;
    enum : unsigned { GATHER = 0, AG = 1, READPORT = 2 };

    // ---- ports (declared with the read side) ----
    sim::HoldIn client_hold[2];        // gather, AG: width 2 {reqA, reqB}
    sim::FifoIn rp_q;                  // the read port: width 2, Fifo(read_port_depth)
    sim::LinkIn ret[AREAS];            // the big SRAM's area returns, width 3
    sim::HoldOut sram_rd;              // width 1: BigSRAM.readReq
    sim::RegOut gather_srq_busy;       // width 1 (with the gather client): its Hold occupied or a gather read in flight
    sim::CountOut rp_injected;         // read-port returns injected

    // ---- configuration ----
    ReadSideConfig cfg;
    uint8_t me = 0;
    bool tokens_on = true;
    uint32_t cap = 64;

    // ---- state (StateFp.srq's) ----
    uint64_t reqA[MAX] = {}, reqB[MAX] = {}, issueT[MAX] = {}, readyT[MAX] = {}, lo[MAX] = {}, hi[MAX] = {};
    uint8_t parts[MAX] = {}, cl[MAX] = {}, zero[MAX] = {}, held_slot[MAX] = {};
    uint8_t free_q[MAX] = {}; uint8_t free_head = 0, free_n = 0;
    uint8_t ord[2][MAX] = {}; uint8_t ord_head[2] = {}, ord_n[2] = {};
    struct Rp { uint64_t t; uint32_t tok; uint32_t pad; };
    Rp rp[MAX] = {}; uint8_t rp_n = 0;
    uint32_t in_flight[3] = {};
    uint64_t last_rd = 0;              // the read put on sram_rd (re-put on a refusal)
    // the read-return holding register: `cur` visible at t (put at t - 1), `nxt` put at t
    bool cur_v = false, nxt_v = false;
    uint64_t cur_w[3] = {}, nxt_w[3] = {}, nxt_at = 0;
    uint8_t busy_out = 0;              // the last gather_srq_busy put
    HarnessFeed* feed = nullptr;       // W20 (harness-only): ref's Net harness's feed; null otherwise

    // ---- statistics, ref's checks ----
    uint64_t issued[3] = {}, stall_cycles = 0, window_full_cycles = 0, packets = 0, out_of_range = 0, violations = 0;
    uint64_t rp_injected_n = 0;        // read-port returns injected (ReadPort.injected; the "rdport" fingerprint)
    uint64_t rp_ties = 0;              // a read-port entry ready in the same cycle as one already waiting (by token order)

    void declare(sim::Ports& p) {
        if (!cfg.present) return;
        if (cfg.gather) p.in(client_hold[GATHER], "rd-gather", 2);
        if (cfg.ag) p.in(client_hold[AG], "rd-ag", 2);
        p.in(rp_q, "rd-port", 2, cfg.read_port_depth);
        for (unsigned a = 0; a < AREAS; a++) p.in(ret[a], "sram-ret", 3);
        p.out(sram_rd, "sram-rd", 1);
        if (cfg.gather) p.out(gather_srq_busy, "gather-srq-busy", 1);
        p.out(rp_injected, "rp-injected");
    }
    void configure(const ReadSideConfig& c, unsigned tile, const NetConfig& n) {
        cfg = c; me = uint8_t(tile); tokens_on = n.loopback_tokens; cap = n.read_tokens;
        if (cap > MAX) FATAL("read queue: read_tokens %u (at most %u)", cap, MAX);
        for (uint32_t i = 0; i < cap; i++) free_q[i] = uint8_t(i);
        free_head = 0; free_n = uint8_t(cap);
    }
    bool loops(unsigned dst) const { return dst == me || (dst == lw::BROADCAST && me < 64); }
    static unsigned chunks_of(unsigned size) { return size == lw::SIZE_UNDEF ? 0 : 1u << size; }

    void step(sim::Ctx& c, Tokens& tok);
    void harness_feed(uint64_t t);                         // W20 (harness-only): the feed (above)
    void harness_issue(sim::Ctx& c, Tokens& tok);          // W20 (harness-only): the issue from the harness's clients
    bool has_return(sim::Ctx&) const { return cur_v; }
    const uint64_t* peek_return(sim::Ctx&) const { return cur_w; }
    void injected(sim::Ctx& c, uint64_t h, Tokens& tok);
    sim::Wake wake(sim::Ctx& c, const Tokens& tok) const;

    void ready_at(uint32_t t_, uint64_t t) {               // SRAMReadQueue.ready
        readyT[t_] = t;
        if (cl[t_] == READPORT) {
            unsigned k = rp_n;
            for (unsigned i = 0; i < rp_n; i++) if (rp[i].t == t) { rp_ties++; break; }
            while (k > 0 && (rp[k - 1].t > t || (rp[k - 1].t == t && rp[k - 1].tok > t_))) { rp[k] = rp[k - 1]; k--; }
            rp[k] = Rp{t, t_, 0}; rp_n++;
        }
    }
    void place(uint32_t t_, uint64_t rlo, uint64_t rhi) {
        const uint32_t addr = uint32_t(reqA[t_] & 0x1FFFFF);
        const unsigned n = chunks_of(unsigned(reqA[t_] >> 21 & 3)), o = addr & 3;
        for (unsigned j = 0; j < n; j++) {
            const unsigned k = o + j;
            const uint64_t v = ((k < 2 ? rlo : rhi) >> (32 * (k & 1))) & 0xFFFFFFFFull;
            if (j < 2) lo[t_] |= v << (32 * j); else hi[t_] |= v << (32 * (j - 2));
        }
    }
    void issue(sim::Ctx& c, Tokens& tok, unsigned client, uint64_t a, uint64_t b);
    void emit(uint64_t t, uint32_t t_);
    void publish_busy(sim::Ctx& c);
    uint64_t digest() const;
};

struct Injection {
    // ---- ports ----
    sim::HoldIn send;
    sim::LinkIn loop_left;
    sim::LinkOut to_router;
    sim::RegOut send_busy;

    // ---- configuration ----
    uint8_t me = 0;
    bool tokens_on = true;            // NetConfig::loopback_tokens

    // ---- state ----
    Tokens tok;
    uint8_t busy_out = 0;             // the last value put on send_busy
    InjReadSide rs;

    // ---- statistics and ref's checks (invariants 5, 10, 17, 21: counted, reported after the run; D24) ----
    uint64_t injected = 0, from_read = 0, from_send = 0, send_wait_cycles = 0;
    uint64_t violations = 0;
    // ref counts a token wait every cycle the send waits; this unit sleeps through the wait, so it keeps when the wait
    // began and counts the cycles when it ends (token_waits(t) is ref's tokenWaitCycles at the end of t)
    uint64_t token_wait_cycles = 0, wait_from = 0;
    uint64_t token_waits(uint64_t t) const { return token_wait_cycles + (wait_from ? t - wait_from + 1 : 0); }

    Injection(sim::Ports& p, unsigned tile, const NetConfig& c, const ReadSideConfig& rc = ReadSideConfig{});
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;

    // ref's SRAMReadQueue.loops: a controller's broadcast never reaches itself (W14-01; M3, C&C 1-3 as units 64-66)
    bool loops(unsigned dst) const { return dst == me || (dst == lw::BROADCAST && me < 64); }
    void check(uint64_t t, uint64_t d, uint64_t h, uint64_t hi);   // InjectionPath's invariants (meshsim's checks; injected anyway)
};

// ---- the step (inline: the run function inlines it) -----------------------------------------------------------------
inline sim::Wake Injection::step(sim::Ctx& c) {
    // the frees of t - 1 take effect (SRAMReadQueue.advance): the count at the start of t is fixed from here
    if (c.ready(loop_left)) { c.take(loop_left); tok.loop_held--; }
    tok.start = tok.reads + tok.loop_held;
    tok.taken = 0;
    rs.step(c, tok);                                          // G3: area returns, one release, one issue
    const bool r = rs.has_return(c), s = c.ready(send);
    if (r || s) {
        if (r && s) send_wait_cycles++;
        bool go = true;
        uint64_t d, h, cw;
        if (r) { const uint64_t* w = rs.peek_return(c); d = w[0]; h = w[1]; cw = w[2]; }
        else {
            const sim::Word* w = c.peek(send);
            d = w[0]; h = w[1]; cw = w[2];
            if (tokens_on && loops(lw::dst(h))) {             // a send for the tile itself or a broadcast takes a token
                if (!tok.free()) { if (!wait_from) wait_from = c.now(); go = false; }
                else { tok.taken++; tok.loop_held++; h |= lw::TAG_TOKEN; }
            }
        }
        if (go) {
            // a cycle the send isn't judged (a read return goes first) or goes ends its wait
            if (wait_from) { token_wait_cycles += c.now() - wait_from; wait_from = 0; }
            if (r) {
                if (rs.feed && !(h & lw::TAG_FROM_SRQ)) rs.cur_v = false;   // W20 (harness-only): a fed packet, no token
                else rs.injected(c, h, tok);
                from_read++;
            }
            else { c.take(send); from_send++; }
            check(c.now(), d, h, cw);
            c.put(to_router, d, h, cw);
            injected++;
        }
    }
    // STATUS's send-busy (the occupied half; W8's block ORs in the core's put of t)
    const uint8_t busy = c.ready(send) ? 1 : 0;
    if (busy != busy_out) { busy_out = busy; c.put(send_busy, busy); }
    // A send that lost to a read return is judged again next cycle.
    if (r && c.ready(send)) return c.next();
    // A send waiting for a token is woken by the router's loop_left (a token freed), the read side by its own wakes (G3).
    // The wait may outlast a cycle with no free on its way (the loopback FIFO drains when the eject selector reaches
    // it): the tile builder gives this unit its own missed-wake threshold.
    return rs.cfg.present ? rs.wake(c, tok) : sim::Wake::none();
}

// ---- the read side's step (W11) -------------------------------------------------------------------------------------
inline void InjReadSide::step(sim::Ctx& c, Tokens& tok) {
    if (!cfg.present) return;
    const uint64_t t = c.now();
    // the read-return hold: the put of t - 1 is visible now (the previous one was injected when it was)
    if (cur_v) FATAL("read queue: tile %u, cycle %llu: a read return not injected the cycle it was visible", me, (unsigned long long)t);
    if (nxt_v && nxt_at < t) { cur_v = true; cur_w[0] = nxt_w[0]; cur_w[1] = nxt_w[1]; cur_w[2] = nxt_w[2]; nxt_v = false; }
    if (feed) harness_feed(t);                                // W20 (harness-only): ref's Net.feed() before cycle t
    // 1. returns from the areas
    for (unsigned a = 0; a < AREAS; a++) {
        if (!c.ready(ret[a])) continue;
        const sim::Word* w = c.peek(ret[a]);
        const uint32_t t_ = uint32_t(w[0] & 0xFFFF);
        place(t_, w[1], w[2]);
        c.take(ret[a]);
        if (parts[t_] == 0) FATAL("read queue: tile %u: a return for token %u with no part outstanding", me, t_);
        if (--parts[t_] == 0) ready_at(t_, t);
    }
    // 2. one release a cycle, by client priority
    int32_t pick = -1;
    for (unsigned k = 0; k < 2 && pick < 0; k++)
        if (ord_n[k]) {
            const uint32_t h = ord[k][ord_head[k]];
            if (parts[h] == 0 && readyT[h] <= t) { pick = int32_t(h); ord_head[k] = uint8_t((ord_head[k] + 1) % MAX); ord_n[k]--; }
        }
    if (pick < 0 && rp_n && rp[0].t <= t) {
        pick = int32_t(rp[0].tok);
        for (unsigned k = 1; k < rp_n; k++) rp[k - 1] = rp[k];
        rp_n--;
    }
    if (pick >= 0) emit(t, uint32_t(pick));
    // 3. issue
    if (feed && feed->clients) { harness_issue(c, tok); publish_busy(c); return; }   // W20 (harness-only)
    if (c.refused(sram_rd)) { c.put(sram_rd, last_rd); stall_cycles++; }
    else if (!tok.free()) {
        if ((cfg.gather && c.ready(client_hold[GATHER])) || (cfg.ag && c.ready(client_hold[AG])) || c.ready(rp_q)) window_full_cycles++;
    } else if (cfg.gather && c.ready(client_hold[GATHER])) {
        const sim::Word* w = c.peek(client_hold[GATHER]); const uint64_t a = w[0], b = w[1];
        c.take(client_hold[GATHER]); issue(c, tok, GATHER, a, b);
    } else if (cfg.ag && c.ready(client_hold[AG])) {
        const sim::Word* w = c.peek(client_hold[AG]); const uint64_t a = w[0], b = w[1];
        c.take(client_hold[AG]); issue(c, tok, AG, a, b);
    } else if (c.ready(rp_q)) {
        const sim::Word* w = c.peek(rp_q); const uint64_t a = w[0], b = w[1];
        c.take(rp_q); issue(c, tok, READPORT, a, b);
    }
    publish_busy(c);
}

inline void InjReadSide::issue(sim::Ctx& c, Tokens& tok, unsigned client, uint64_t a, uint64_t b) {
    const uint64_t t = c.now();
    if (free_n == 0) FATAL("read queue: tile %u: a token free by count but none on the free list", me);
    const uint32_t t_ = free_q[free_head];
    free_head = uint8_t((free_head + 1) % MAX); free_n--;
    tok.taken++; tok.reads++;
    reqA[t_] = a; reqB[t_] = b; issueT[t_] = t; cl[t_] = uint8_t(client); lo[t_] = hi[t_] = 0; readyT[t_] = ~0ull;
    held_slot[t_] = 1;
    if (client != READPORT) { ord[client][(ord_head[client] + ord_n[client]) % MAX] = uint8_t(t_); ord_n[client]++; }
    issued[client]++; in_flight[client]++;
    const unsigned width = unsigned(a >> 21 & 3);
    unsigned n = chunks_of(width);
    const uint32_t addr = uint32_t(a & 0x1FFFFF);
    if (n != 0 && (addr & (n - 1)) != 0) {
        static const char* const kClient[] = {"gather", "address generator", "read port"};
        FATAL("read queue: tile %u, cycle %llu: the %s's read of chunk 0x%x, not aligned to its size (%u bits): undefined "
              "behaviour (v1.25 s8b)", me, (unsigned long long)t, kClient[client], addr, 32 * n);
    } else if (n != 0 && addr + n > cfg.mem_chunks) { out_of_range++; violations++; n = 0; }   // invariant 19: reads 0
    if (n == 0) { parts[t_] = 0; zero[t_] = 1; ready_at(t_, t + 2 + cfg.sram_read_latency); return; }
    zero[t_] = 0; parts[t_] = 1;
    last_rd = uint64_t(addr) | uint64_t(n) << 21 | uint64_t(t_) << 24;               // BigSRAM.readReq, part 0
    c.put(sram_rd, last_rd);
}

// The return as one packet (SRAMReadQueue.emit): size the request's width, payload the chunks read, the token in the
// header's simulator-only bits.
inline void InjReadSide::emit(uint64_t t, uint32_t t_) {
    const uint64_t a = reqA[t_], b = reqB[t_];
    const unsigned mode = unsigned(a >> 23 & 7), dst = unsigned(a >> 29 & 0x7F), size = unsigned(a >> 21 & 3);
    const uint64_t pl = zero[t_] ? 0 : lo[t_], ph = zero[t_] ? 0 : hi[t_];
    const uint64_t h = lw::hdr(0, dst, mode, unsigned(a >> 26 & 7), uint32_t(b), size) | uint64_t(t_) << lw::TOKEN_SHIFT |
                       lw::TAG_FROM_SRQ | (cl[t_] == READPORT ? lw::TAG_READPORT : 0) | (tokens_on && loops(dst) ? lw::TAG_TOKEN : 0);
    if (nxt_v) FATAL("read queue: tile %u: two releases in cycle %llu", me, (unsigned long long)t);
    nxt_v = true; nxt_at = t;
    nxt_w[0] = lw::mask_lo(pl, size); nxt_w[1] = h; nxt_w[2] = lw::mask_hi(ph, size);
    packets++;
}

inline void InjReadSide::injected(sim::Ctx& c, uint64_t h, Tokens& tok) {
    const uint32_t t_ = lw::token(h);
    cur_v = false;
    if (!held_slot[t_]) FATAL("read queue: tile %u: injected token %u, not held", me, t_);
    held_slot[t_] = 0;
    tok.reads--; in_flight[cl[t_]]--;
    if (tokens_on && loops(lw::dst(h))) tok.loop_held++;   // the token moves to the loopback packet: still held
    free_q[(free_head + free_n) % MAX] = uint8_t(t_); free_n++;
    if (h & lw::TAG_READPORT) { c.inc(rp_injected); rp_injected_n++; }
    publish_busy(c);
}

inline void InjReadSide::publish_busy(sim::Ctx& c) {
    if (!cfg.gather) return;
    const uint8_t b = (c.ready(client_hold[GATHER]) || in_flight[GATHER] > 0) ? 1 : 0;
    if (b != busy_out) { busy_out = b; c.put(gather_srq_busy, b); }
}

// Stepped next cycle while the hold has a packet to inject, a release or an issue may happen; at the earliest zero-width
// ready cycle; otherwise by the area returns, the clients' puts and the router's frees.
inline sim::Wake InjReadSide::wake(sim::Ctx& c, const Tokens& tok) const {
    const uint64_t t = c.now();
    if (nxt_v) return c.next();
    uint64_t hat = ~0ull;                                      // W20 (harness-only): the harness's next feed or issue
    if (feed && feed->pending()) {
        if (feed->ret.n) hat = feed->ret.at(0)[0];
        for (unsigned k = 0; k < 3; k++) {
            const HarnessFeed::Ring& r = feed->cl[k];
            const uint64_t a = r.vis ? (tok.reads + tok.loop_held < cap ? t + 1 : ~0ull) : r.n ? r.at(0)[0] : ~0ull;
            hat = a < hat ? a : hat;
        }
        if (hat <= t + 1) return c.next();
    }
    const bool client = (cfg.gather && c.ready(client_hold[GATHER])) || (cfg.ag && c.ready(client_hold[AG])) || c.ready(rp_q);
    if (client && tok.reads + tok.loop_held < cap) return c.next();
    uint64_t at = hat;
    for (unsigned k = 0; k < 2; k++)
        if (ord_n[k]) { const uint32_t h = ord[k][ord_head[k]]; if (parts[h] == 0) at = readyT[h] < at ? readyT[h] : at; }
    if (rp_n) at = rp[0].t < at ? rp[0].t : at;
    if (at == ~0ull) return sim::Wake::none();
    return at <= t + 1 ? c.next() : sim::Wake::at_cycle(at);
}


// ---- W20 (harness-only; P2): ref's Net harness's feed, on the coreless Net machine only (feed != null) --------------
inline void InjReadSide::harness_feed(uint64_t t) {
    HarnessFeed& f = *feed;
    if (f.ret.n && f.ret.at(0)[0] <= t) {
        const uint64_t* e = f.ret.at(0);
        if (cur_v) FATAL("Net harness: tile %u, cycle %llu: a packet fed into the read-return register the cycle the read "
                         "queue released into it (ref: a double put)", me, (unsigned long long)t);
        cur_v = true; cur_w[0] = e[1]; cur_w[1] = e[2]; cur_w[2] = e[3];
        if (e[0] < t) f.late++;
        f.ret.pop();
    }
    if (f.clients)
        for (unsigned k = 0; k < 3; k++) {
            HarnessFeed::Ring& r = f.cl[k];
            if (r.vis < r.n && r.vis < 60 && r.at(r.vis)[0] <= t) r.vis++;
        }
}
inline void InjReadSide::harness_issue(sim::Ctx& c, Tokens& tok) {   // SRAMReadQueue's issue over the harness's clients
    HarnessFeed& f = *feed;
    if (c.refused(sram_rd)) { c.put(sram_rd, last_rd); stall_cycles++; return; }
    if (!tok.free()) { if (f.cl[0].vis || f.cl[1].vis || f.cl[2].vis) window_full_cycles++; return; }
    for (unsigned k = 0; k < 3; k++) {
        HarnessFeed::Ring& r = f.cl[k];
        if (!r.vis) continue;
        const uint64_t a = r.at(0)[1], b = r.at(0)[2];
        r.pop(); r.vis--;
        issue(c, tok, k, a, b);
        return;
    }
}

}  // namespace units
