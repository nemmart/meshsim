// inject.h: the inject unit (machine-spec 14; the RTL's rtl/blocks/inject_unit): the read arbiter, the read tokens with
// the return queues, and the injection point. Everything the tile sends passes through it, a scratchpad read turned
// into a packet or the write unit's packet, except a peripheral DMA's (16.4).
//
// THE CYCLE. T is the cycle of a step; a client's read put on its Hold in T - 1 is OFFERED in T (the Hold is the
// client's output register), so the arbiter takes a read in the cycle it is offered.
//   1. tokens: a loopback packet's token whose return arrived (`loop_left`) is free from this cycle (14.2: freed when
//      the traffic director takes the copy, which is the cycle before; the chip's link from the router is the eject
//      link's latency + 1). The count held at the start of T is fixed here.
//   2. the return: the row of the read taken in T - READ_LATENCY arrives from the scratchpad (`ret`, in take order: the
//      return queues are one line, 15) and is injected in T as one packet: its header's fields, and as payload its
//      size's words of the row from its word. Its token is freed (a loopback packet's stays held), its client's `done`
//      pulses (the read unit's: `rp_injected`).
//   3. the read arbiter (14.1): the first client offering a read, in fixed priority, the gather unit, the address
//      generator, the read unit; the address generator only while the gather unit is not busy (13). That read is taken
//      if no write meets it at its bank in T (the write engine's `wr_note`: 15, a write wins its bank) and fewer than
//      TOKEN_COUNT tokens were held at the start of T; otherwise nothing is taken, and a lower client does not get the
//      cycle. The read taken goes to the scratchpad (`sram_rd`).
//   4. the write unit's packet (`send`), in a cycle with no return: it is injected, unless it loops back and no token
//      is free after the arbiter's take (14.2: the arbiter first).
// A packet injected in T is on `to_router`, a Link(intra_tile_latency = 1), the link's register stage: at the router's
// local input in T + 1 (14.3).
//
// gather_busy as the arbiter needs it in T (13: the queue holds a request, or a request's last read is not yet taken; a
// level from the gather unit's registers): the gather unit's read is offered (its Hold occupied), or a GATHER entered
// its queue at the end of T - 1 (the director's `gather_note`, put in the cycle it accepts one). The gather unit puts a
// read in every cycle it has one to offer, so nothing else holds its queue non-empty.
//
// PORTS
//   in   send        HoldIn, 3 words: the write unit's holding register (a refused put is retried by the core at t + 1).
//        loop_left   LinkIn, width 0: from the router, a loopback packet's token returned (free from the cycle it arrives).
//   out  to_router   LinkOut, 3 words, latency intra_tile_latency: into the router's local input (src unset).
//        send_busy   RegOut, 1 word, to the tile control unit: the holding register OCCUPIED at the end of t (after t's
//                    take). The Reg is put only when the value changes, so it costs nothing on a quiet tile.
//        send_taken  WireOut (with `send_wire`), to the core's write unit: the write unit's packets injected so far, this
//                    cycle's included. It is `packet_ready` for the write unit's stall (9: a send stalls only while
//                    the holding register is full and is not being injected that cycle): a send made in cycle t is
//                    taken iff every packet the write unit put before is injected by the end of t (tile_ctl.h).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "check.h"
#include "sim.h"
#include "units/big_sram.h"
#include "units/mem.h"
#include "units/net.h"

namespace units {

// The token pool (14.2): TOKEN_COUNT tokens shared by the reads and the write unit's loopback packets. Room is judged
// on the count at the start of the cycle plus what was taken during it: a token freed in a cycle is free from the next.
struct Tokens {
    uint32_t cap = 64;
    uint32_t reads = 0;        // held by reads (taken, not yet injected)
    uint32_t loop_held = 0;    // held by loopback packets injected, until the director takes their copies
    uint32_t start = 0;        // held at the start of this cycle (fixed by the step's first phase)
    uint32_t taken = 0;        // taken during this cycle (a read's take, a send)
    bool free() const { return start + taken < cap; }
    uint64_t digest() const { return uint64_t(reads) | uint64_t(loop_held) << 32; }
};

// The read side: the arbiter, the return line and the reads' injection.
//
// Configuration: present only in a tile with the scratchpad's units (ControlIO::mem); clients 0 (the gather unit) and 1
// (the address generator) only when their units have their read Holds, client 2 (the read unit: the core's
// Fifo(read_port_depth)) with the read side. `wr_note` and `gather_note` are declared when the tile has the write
// engine and the director to put them (a bench without them has no write to meet and no GATHER to wait for).
struct ReadSideConfig {
    bool present = false;
    bool gather = false, ag = false;   // the clients
    bool wr_note = false, gather_note = false;
    bool gather_wire = false;          // `gather_taken` is declared (a tile with the gather unit to read it)
    bool drift = false;                // `ag_drift` is declared (a tile with the tile control unit to take it)
    bool send_wire = false;            // the injection point drives `send_taken` (the tile's core reads it)
    unsigned read_port_depth = 64;
    unsigned read_latency = 3;         // READ_LATENCY (15, 19): the RTL's is 3 (scratchpad_pkg)
    uint32_t mem_chunks = 1u << 20;
};

// (harness-only): the Net harness feeding a tile (tests/mach.h, Net::at and Net::rd). Null on every machine but the
// harness's coreless Net machine, so nothing here runs otherwise. The rings are the harness's (allocated at
// configuration, fixed size: a push past them is fatal in the harness, between cycles). Entries are {T, w0, w1, w2}.
//   ret     packets injected as read returns (Net.at): at the read side's step at u, the head with T <= u is the
//           return of u (`late` counts T < u); a scratchpad return in the same cycle is fatal
//   cl[k]   the read arbiter's client k's requests (Net.rd): at most one moved a cycle into the client's FIFO (the
//           ring's first `vis` entries), at the step of u when T <= u and fewer than 60 are in it; the FIFO replaces
//           the three clients' inputs in the arbiter (client 0's FIFO non-empty is its gather_busy)
struct HarnessFeed {
    struct Ring {
        uint64_t (*e)[4] = nullptr;
        uint32_t cap = 0, head = 0, n = 0, vis = 0;
        const uint64_t* at(uint32_t i) const { return e[(head + i) % cap]; }
        void pop() { head = (head + 1) % cap; n--; }
    };
    Ring ret, cl[3];
    bool clients = false;          // the FIFOs above are the read arbiter's three clients
    bool on = false;               // fed only in Net.step / runAll: set by the harness then
    uint64_t late = 0;
    bool pending() const { return ret.n || cl[0].n || cl[1].n || cl[2].n; }
};

struct InjReadSide {
    static constexpr unsigned LINE = 16;   // the return line: READ_LATENCY + 1 entries at most
    enum : unsigned { GATHER = 0, AG = 1, READPORT = 2 };

    // ---- ports (declared with the read side) ----
    sim::HoldIn client_hold[2];        // gather, AG: width 2 {reqA, reqB}
    sim::FifoIn rp_q;                  // the read unit: width 2, Fifo(read_port_depth)
    sim::LinkIn ret;                   // the scratchpad's row, width 2, Link(READ_LATENCY - 1) from its step
    sim::HoldOut sram_rd;              // width 1: the read taken, to the scratchpad
    sim::LinkIn wr_note;               // width 1, Link(1): the word address of the write engine's write of this cycle
    sim::LinkIn gather_note;           // width 0, Link(1): the director accepted a GATHER the cycle before
    sim::RegOut gather_srq_busy;       // width 1 (with the gather client): its read still offered at the end of t
    sim::WireOut gather_taken;         // (cfg.gather_wire) to the gather unit: its reads taken so far, this cycle's included: whether
                                       // its offer register's read leaves in this cycle (gather.h)
    sim::LinkOut ag_drift;             // width 0, Link(1), to the tile control unit (cfg.drift): ERRORS.drift's pulse (8.4), put in
                                       // a cycle a group transfer's read (mem::A_GROUP) is offered and not taken
    sim::CountOut rp_injected;         // read-unit returns injected

    // ---- configuration ----
    ReadSideConfig cfg;
    uint8_t me = 0;
    bool tokens_on = true;
    uint32_t cap = 64;

    // ---- state: the return line, in take order ----
    struct Read { uint64_t a, b, taken_at; uint8_t client, zero; };
    Read line[LINE] = {};
    uint8_t head = 0, n = 0;
    uint32_t in_flight[3] = {};
    // this cycle's return, as a packet (valid in the step that made it only)
    bool cur_v = false;
    uint64_t cur_w[3] = {};
    uint8_t busy_out = 0;              // the last gather_srq_busy put
    HarnessFeed* feed = nullptr;       // (harness-only): the Net harness's feed; null otherwise

    // ---- statistics ----
    uint64_t issued[3] = {}, packets = 0, out_of_range = 0, violations = 0;
    uint64_t bank_wait_cycles = 0;     // a read offered and not taken: a write at its bank (15)
    uint64_t token_wait_cycles = 0;    // a read offered and not taken: no token free (14.2); counted in stepped cycles
    uint64_t gather_gate_cycles = 0;   // the address generator offering while the gather unit is busy (14.1)
    uint64_t drift_cycles = 0;         // a group transfer's read offered and not taken (8.4: ERRORS.drift)
    uint64_t rp_injected_n = 0;        // read-unit returns injected
    uint64_t rp_inj_t = ~0ull, rp_inj_now = 0;   // the cycle of the last read-unit injection and how many then
    uint64_t lat_max[3] = {};          // take to injection: READ_LATENCY for every read (15)

    void declare(sim::Ports& p) {
        if (!cfg.present) return;
        if (cfg.gather) p.in(client_hold[GATHER], "rd-gather", 2);
        if (cfg.ag) p.in(client_hold[AG], "rd-ag", 2);
        p.in(rp_q, "rd-port", 2, cfg.read_port_depth);
        p.in(ret, "sram-ret", 2);
        p.out(sram_rd, "sram-rd", 1);
        if (cfg.wr_note) p.in(wr_note, "wr-note", 1);
        if (cfg.gather_note) p.in(gather_note, "gather-note", 0);
        if (cfg.gather) p.out(gather_srq_busy, "gather-srq-busy", 1);
        if (cfg.ag && cfg.drift) p.out(ag_drift, "ag-drift", 0);
        if (cfg.gather && cfg.gather_wire) p.out(gather_taken, "gather-taken");
        p.out(rp_injected, "rp-injected");
    }
    void configure(const ReadSideConfig& c, unsigned tile, const NetConfig& nc) {
        cfg = c; me = uint8_t(tile); tokens_on = nc.loopback_tokens; cap = nc.read_tokens;
        if (!cfg.present) return;
        if (cfg.read_latency < 2 || cfg.read_latency + 1 > LINE) FATAL("inject unit: READ_LATENCY %u (2-%u)", cfg.read_latency, LINE - 1);
        if (!scratchpad_size_ok(cfg.mem_chunks)) FATAL("inject unit: a scratchpad of %u words (machine-spec 15)", cfg.mem_chunks);
    }
    bool loops(unsigned dst) const { return dst == me || (dst == lw::BROADCAST && me < 64); }
    static unsigned chunks_of(unsigned size) { return size == lw::SIZE_UNDEF ? 0 : 1u << size; }
    bool idle() const { return n == 0; }

    void returns(sim::Ctx& c);                             // phase 2: this cycle's return, into cur_w
    void arbitrate(sim::Ctx& c, Tokens& tok);              // phase 3
    int take_one(sim::Ctx& c, Tokens& tok);                // the arbiter's take: the client served, or -1
    void take(sim::Ctx& c, Tokens& tok, unsigned client, uint64_t a, uint64_t b);
    void injected(sim::Ctx& c, uint64_t h, Tokens& tok);
    void publish_busy(sim::Ctx& c);
    bool waiting(sim::Ctx& c) const;                       // a client still offers a read after this step
    void harness_feed(uint64_t t);                         // (harness-only): the feed (above)
    uint64_t digest() const;
};

struct Injection {
    // ---- ports ----
    sim::HoldIn send;
    sim::LinkIn loop_left;
    sim::LinkOut to_router;
    sim::RegOut send_busy;
    sim::WireOut send_taken;

    // ---- configuration ----
    uint8_t me = 0;
    bool tokens_on = true;            // NetConfig::loopback_tokens
    bool send_wire = false;

    // ---- state ----
    Tokens tok;
    uint64_t sends_taken = 0;         // the write unit's packets injected (`send_taken`)
    uint8_t busy_out = 0;             // the last value put on send_busy
    InjReadSide rs;

    // ---- statistics and the checks (invariants 5, 10, 17, 21: counted, reported after the run; D24) ----
    uint64_t injected = 0, from_read = 0, from_send = 0, send_wait_cycles = 0;
    uint64_t violations = 0;
    // a token wait is counted every cycle the send waits; this unit sleeps through the wait, so it keeps when the wait
    // began and counts the cycles when it ends (token_waits(t) is the count at the end of t)
    uint64_t token_wait_cycles = 0, wait_from = 0;
    uint64_t token_waits(uint64_t t) const { return token_wait_cycles + (wait_from ? t - wait_from + 1 : 0); }

    Injection(sim::Ports& p, unsigned tile, const NetConfig& c, const ReadSideConfig& rc = ReadSideConfig{});
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;

    // 14.2: a packet to the tile itself, or a compute tile's broadcast (a special tile's never reaches itself)
    bool loops(unsigned dst) const { return dst == me || (dst == lw::BROADCAST && me < 64); }
    void check(uint64_t t, uint64_t d, uint64_t h, uint64_t hi);   // the injection's invariants (meshsim's checks; injected anyway)
};

// ---- the step (inline: the run function inlines it) -----------------------------------------------------------------
inline sim::Wake Injection::step(sim::Ctx& c) {
    // 1. the tokens returned (14.2) are free from this cycle: the count at the start of t is fixed from here
    if (c.ready(loop_left)) { c.take(loop_left); tok.loop_held--; }
    tok.start = tok.reads + tok.loop_held;
    tok.taken = 0;
    // 2, 3. this cycle's return; the arbiter
    if (rs.cfg.present) { rs.returns(c); rs.arbitrate(c, tok); }
    // the injection point: a read return first, otherwise the write unit's packet (14.3)
    const bool r = rs.cur_v, s = c.ready(send);
    if (r || s) {
        if (r && s) send_wait_cycles++;
        bool go = true;
        uint64_t d, h, cw;
        if (r) { d = rs.cur_w[0]; h = rs.cur_w[1]; cw = rs.cur_w[2]; }
        else {
            const sim::Word* w = c.peek(send);
            d = w[0]; h = w[1]; cw = w[2];
            if (tokens_on && loops(lw::dst(h))) {             // a loopback packet takes a token, after the arbiter
                if (!tok.free()) { if (!wait_from) wait_from = c.now(); go = false; }
                else { tok.taken++; tok.loop_held++; h |= lw::TAG_TOKEN; }
            }
        }
        if (go) {
            // a cycle the send isn't judged (a read return goes first) or goes ends its wait
            if (wait_from) { token_wait_cycles += c.now() - wait_from; wait_from = 0; }
            if (r) {
                if (!(h & lw::TAG_FROM_SRQ)) rs.cur_v = false;   // (harness-only): a fed packet, no token
                else rs.injected(c, h, tok);
                from_read++;
            }
            else { c.take(send); from_send++; sends_taken++; if (send_wire) c.drive(send_taken, sends_taken); }
            check(c.now(), d, h, cw);
            c.put(to_router, d, h, cw);
            injected++;
        }
    }
    if (rs.cfg.present) rs.publish_busy(c);
    // the holding register's status (the occupied half; the tile control unit ORs in the core's put of t)
    const uint8_t busy = c.ready(send) ? 1 : 0;
    if (busy != busy_out) { busy_out = busy; c.put(send_busy, busy); }
    // A send that lost to a read return is judged again next cycle.
    if (r && c.ready(send)) return c.next();
    // A send waiting for a token is woken by the router's loop_left (a token returned) or by a return's arrival. A
    // client still offering (a write at its bank, the gather unit busy, a lower priority) is looked at again next
    // cycle while a token can be free then; with none, the unit sleeps until one is freed, which arrives on a port.
    if (!rs.cfg.present) return sim::Wake::none();
    if (rs.feed && rs.feed->pending() && (rs.feed->on || rs.feed->cl[0].vis || rs.feed->cl[1].vis || rs.feed->cl[2].vis)) return c.next();
    if (rs.waiting(c) && tok.reads + tok.loop_held < tok.cap) return c.next();
    return sim::Wake::none();
}

// ---- the read side ---------------------------------------------------------------------------------------------
// The return of this cycle: the head of the line, taken READ_LATENCY cycles ago, with the row the scratchpad sends.
inline void InjReadSide::returns(sim::Ctx& c) {
    const uint64_t t = c.now();
    cur_v = false;
    if (c.ready(ret)) {
        const sim::Word* w = c.peek(ret);
        const uint64_t rlo = w[0], rhi = w[1];
        c.take(ret);
        if (n == 0) FATAL("inject unit: tile %u, cycle %llu: a row from the scratchpad with no read in flight", me, (unsigned long long)t);
        const Read& rd = line[head];
        if (rd.taken_at + cfg.read_latency != t)
            FATAL("inject unit: tile %u, cycle %llu: the read taken at %llu returned after %llu cycles, not READ_LATENCY %u",
                  me, (unsigned long long)t, (unsigned long long)rd.taken_at, (unsigned long long)(t - rd.taken_at), cfg.read_latency);
        const uint64_t a = rd.a, b = rd.b;
        const unsigned mode = unsigned(a >> 23 & 7), dst = unsigned(a >> 29 & 0x7F), size = unsigned(a >> 21 & 3);
        // the payload: the size's words of the row from the read's word, the bits above the size 0
        uint64_t lo = 0, hi = 0;
        if (!rd.zero) {
            const unsigned words = chunks_of(size), o = unsigned(a & 3);
            for (unsigned j = 0; j < words; j++) {
                const unsigned k = o + j;
                const uint64_t v = ((k < 2 ? rlo : rhi) >> (32 * (k & 1))) & 0xFFFFFFFFull;
                if (j < 2) lo |= v << (32 * j); else hi |= v << (32 * (j - 2));
            }
        }
        const uint64_t h = lw::hdr(0, dst, mode, unsigned(a >> 26 & 7), uint32_t(b), size) | lw::TAG_FROM_SRQ |
                           (rd.client == READPORT ? lw::TAG_READPORT : 0) | (tokens_on && loops(dst) ? lw::TAG_TOKEN : 0);
        cur_v = true;
        cur_w[0] = lw::mask_lo(lo, size); cur_w[1] = h; cur_w[2] = lw::mask_hi(hi, size);
        packets++;
    }
    if (feed) harness_feed(t);                                // (harness-only): the Net harness's packets
}

// Phase 3, and ERRORS.drift's pulse (8.4; the RTL's address_generator: `drift = group_valid && !read_request_ready`):
// a group transfer's read offered in this cycle and not taken in it, whatever held it back (a higher client, the gather
// unit busy, a write at its bank, no token).
inline void InjReadSide::arbitrate(sim::Ctx& c, Tokens& tok) {
    const bool group_read = cfg.ag && cfg.drift && !(feed && feed->clients) && c.ready(client_hold[AG]) &&
                            (c.peek(client_hold[AG])[0] & mem::A_GROUP) != 0;
    const int taken = take_one(c, tok);
    if (group_read && taken != int(AG)) { drift_cycles++; c.put(ag_drift); }
}

inline int InjReadSide::take_one(sim::Ctx& c, Tokens& tok) {
    // the write of this cycle, if any: its bank
    bool wr = false; uint32_t wr_bank = 0;
    if (cfg.wr_note && c.ready(wr_note)) { wr = true; wr_bank = scratchpad_bank(cfg.mem_chunks, uint32_t(c.peek(wr_note)[0])); c.take(wr_note); }
    bool gnote = false;
    if (cfg.gather_note && c.ready(gather_note)) { gnote = true; c.take(gather_note); }
    // the served client: the first offering, the address generator only while the gather unit is not busy
    int served = -1;
    uint64_t a = 0, b = 0;
    if (feed && feed->clients) {                              // (harness-only): the harness's three FIFOs
        const bool gbusy = feed->cl[GATHER].vis != 0;
        for (unsigned k = 0; k < 3 && served < 0; k++) {
            if (!feed->cl[k].vis || (k == AG && gbusy)) continue;
            served = int(k); a = feed->cl[k].at(0)[1]; b = feed->cl[k].at(0)[2];
        }
    } else {
        const bool g = cfg.gather && c.ready(client_hold[GATHER]);
        const bool gbusy = g || gnote;
        const bool ag = cfg.ag && c.ready(client_hold[AG]);
        if (ag && gbusy) gather_gate_cycles++;
        if (g) { served = GATHER; const sim::Word* w = c.peek(client_hold[GATHER]); a = w[0]; b = w[1]; }
        else if (ag && !gbusy) { served = AG; const sim::Word* w = c.peek(client_hold[AG]); a = w[0]; b = w[1]; }
        else if (c.ready(rp_q)) { served = READPORT; const sim::Word* w = c.peek(rp_q); a = w[0]; b = w[1]; }
    }
    if (served < 0) return -1;
    // taken only if no write meets it at its bank and a token is free; else nothing is taken this cycle (14.1)
    if (wr && wr_bank == scratchpad_bank(cfg.mem_chunks, uint32_t(a & 0x1FFFFF))) { bank_wait_cycles++; return -1; }
    if (!tok.free()) { token_wait_cycles++; return -1; }
    if (feed && feed->clients) { feed->cl[served].pop(); feed->cl[served].vis--; }
    else if (served == READPORT) c.take(rp_q);
    else c.take(client_hold[served]);
    take(c, tok, unsigned(served), a, b);
    return served;
}

inline void InjReadSide::take(sim::Ctx& c, Tokens& tok, unsigned client, uint64_t a, uint64_t b) {
    const uint64_t t = c.now();
    if (n == LINE) FATAL("inject unit: tile %u: the return line is full", me);
    tok.taken++; tok.reads++;
    issued[client]++; in_flight[client]++;
    if (client == GATHER && cfg.gather_wire) c.drive(gather_taken, issued[GATHER]);
    const unsigned width = unsigned(a >> 21 & 3);
    unsigned words = chunks_of(width);
    const uint32_t addr = uint32_t(a & 0x1FFFFF);
    if (words != 0 && (addr & (words - 1)) != 0) {
        static const char* const kClient[] = {"gather unit", "address generator", "read unit"};
        FATAL("inject unit: tile %u, cycle %llu: the %s's read at word 0x%x, not aligned to its size (%u bits): undefined "
              "behaviour (machine-spec 15)", me, (unsigned long long)t, kClient[client], addr, 32 * words);
    } else if (words != 0 && addr + words > cfg.mem_chunks) { out_of_range++; violations++; words = 0; }   // UB (15): reads 0
    Read& rd = line[(head + n) % LINE]; n++;
    rd.a = a; rd.b = b; rd.taken_at = t; rd.client = uint8_t(client); rd.zero = words == 0;
    c.put(sram_rd, uint64_t(addr) | uint64_t(words) << 21);
}

inline void InjReadSide::injected(sim::Ctx& c, uint64_t h, Tokens& tok) {
    if (n == 0) FATAL("inject unit: tile %u: a return injected with no read in flight", me);
    const Read& rd = line[head];
    const unsigned client = rd.client;
    if (c.now() - rd.taken_at > lat_max[client]) lat_max[client] = c.now() - rd.taken_at;
    head = uint8_t((head + 1) % LINE); n--;
    cur_v = false;
    tok.reads--; in_flight[client]--;
    if (tokens_on && loops(lw::dst(h))) tok.loop_held++;   // the token stays with the loopback packet (14.2)
    if (h & lw::TAG_READPORT) {
        c.inc(rp_injected); rp_injected_n++;
        if (c.now() != rp_inj_t) { rp_inj_t = c.now(); rp_inj_now = 0; }   // its entry is free from t + 1 (10)
        rp_inj_now++;
    }
}

inline void InjReadSide::publish_busy(sim::Ctx& c) {
    if (!cfg.gather) return;
    const uint8_t b = c.ready(client_hold[GATHER]) ? 1 : 0;
    if (b != busy_out) { busy_out = b; c.put(gather_srq_busy, b); }
}

inline bool InjReadSide::waiting(sim::Ctx& c) const {
    if (feed && feed->clients) return feed->cl[0].vis || feed->cl[1].vis || feed->cl[2].vis;
    return (cfg.gather && c.ready(client_hold[GATHER])) || (cfg.ag && c.ready(client_hold[AG])) || c.ready(rp_q);
}

// ---- (harness-only): the Net harness's feed, on the coreless Net machine only (feed != null) --------------------
inline void InjReadSide::harness_feed(uint64_t t) {
    HarnessFeed& f = *feed;
    if (!f.on) return;                                         // a cycle run outside Net's step: nothing fed
    if (f.ret.n && f.ret.at(0)[0] <= t) {
        const uint64_t* e = f.ret.at(0);
        if (cur_v) FATAL("Net harness: tile %u, cycle %llu: a packet fed as a read return in the cycle a scratchpad read "
                         "returns", me, (unsigned long long)t);
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

}  // namespace units
