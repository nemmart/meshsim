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

// G3's part (the SRAM read queue, its clients and the read side of the injection): empty until G3 builds it. Its
// interface to the step is the three calls below; until then it never has a return and holds no reads.
struct InjReadSide {
    uint32_t reads = 0;                                        // tokens held by reads (issued, not yet injected)
    void declare(sim::Ports&) {}
    void step(sim::Ctx&, uint32_t /*tokens_free_at_start*/) {}  // G3: returns placed, one release, one issue
    bool has_return(sim::Ctx&) const { return false; }         // a read return ready to inject
    const uint64_t* peek_return(sim::Ctx&) const { return nullptr; }
    void injected(sim::Ctx&, uint64_t /*hdr*/) {}              // G3: frees the token, tells the read port
    uint64_t digest() const { return reads; }
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
    uint32_t cap = 64;                // NetConfig::read_tokens

    // ---- state ----
    uint32_t loop_held = 0;           // tokens held by packets on their way through the loopback FIFO
    uint8_t busy_out = 0;             // the last value put on send_busy
    InjReadSide rs;

    // ---- statistics and ref's checks (invariants 5, 10, 17, 21: counted, reported after the run; D24) ----
    uint64_t injected = 0, from_read = 0, from_send = 0, send_wait_cycles = 0, token_wait_cycles = 0;
    uint64_t violations = 0;

    Injection(sim::Ports& p, unsigned tile, const NetConfig& c);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;

    bool loops(unsigned dst) const { return dst == me || dst == lw::BROADCAST; }   // ref's SRAMReadQueue.loops (me < 64)
};

}  // namespace units
