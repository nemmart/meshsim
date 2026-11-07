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

// G3's part (the SRAM read queue, its clients and the read side of the injection): empty until G3 builds it. Its
// interface to the step is the calls below; until then it never has a return and holds no reads.
struct InjReadSide {
    void declare(sim::Ports&) {}
    void step(sim::Ctx&, Tokens&) {}                           // G3: returns placed, one release, one issue (takes a token)
    bool has_return(sim::Ctx&) const { return false; }         // a read return ready to inject
    const uint64_t* peek_return(sim::Ctx&) const { return nullptr; }
    void injected(sim::Ctx&, uint64_t /*hdr*/, Tokens&) {}     // G3: frees the read's token (moved to the loopback if the
                                                               //  packet loops), tells the read port
    uint64_t digest() const { return 0; }
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

    Injection(sim::Ports& p, unsigned tile, const NetConfig& c);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;

    bool loops(unsigned dst) const { return dst == me || dst == lw::BROADCAST; }   // ref's SRAMReadQueue.loops (me < 64)
    void check(uint64_t h);           // InjectionPath's invariants (meshsim's checks; the packet is injected anyway)
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
            if (r) { rs.injected(c, h, tok); from_read++; }
            else { c.take(send); from_send++; }
            check(h);
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
    return sim::Wake::none();
}

}  // namespace units
