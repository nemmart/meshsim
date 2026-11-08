// inject.cpp (WORKER9): the injection unit's configuration, InjectionPath's checks and its digest. The step is inline in
// inject.h.
#include "units/inject.h"

#include "base/fatal.h"

namespace units {

Injection::Injection(sim::Ports& p, unsigned tile, const NetConfig& c, const ReadSideConfig& rc) : me(uint8_t(tile)), tokens_on(c.loopback_tokens) {
    if (tile > 63) FATAL("injection: tile %u (a controller's injection is not modelled in M2)", tile);
    tok.cap = c.read_tokens;
    p.in(send, "send", 3);
    p.in(loop_left, "loop-left", 0);
    p.out(to_router, "to-router", 3);
    p.out(send_busy, "send-busy", 1);
    rs.configure(rc, tile, c);
    rs.declare(p);
}

uint64_t InjReadSide::digest() const {
    if (!cfg.present) return 0;
    uint64_t h = sim::hash_fields(free_head, free_n, ord_head, ord_n, rp_n, in_flight, last_rd, cur_v, nxt_v, nxt_at, cur_w, nxt_w, busy_out);
    for (unsigned k = 0; k < free_n; k++) h = sim::hash_mix(h, free_q[(free_head + k) % MAX]);
    for (unsigned t = 0; t < cap; t++)
        if (held_slot[t]) h = sim::hash_mix(h, sim::hash_fields(t, reqA[t], reqB[t], issueT[t], readyT[t], lo[t], hi[t], parts[t], cl[t], zero[t]));
    for (unsigned k = 0; k < 2; k++) for (unsigned i = 0; i < ord_n[k]; i++) h = sim::hash_mix(h, ord[k][(ord_head[k] + i) % MAX]);
    for (unsigned i = 0; i < rp_n; i++) h = sim::hash_mix(h, sim::hash_fields(rp[i].t, rp[i].tok));
    return h;
}

// InjectionPath's invariants 5, 17, 10, 21 (judged against peripheral 1 alone, which every unit has; LinkWord.whyBad
// with periphPresent 1, size 11), counted: meshsim's checks, the packet is injected anyway.
void Injection::check(uint64_t h) {
    const unsigned m = lw::mode(h), s = lw::size(h), d = lw::dst(h);
    const uint32_t a = lw::addr(h);
    bool bad = false;
    if (m == lw::M_AUG) bad = s == lw::SIZE_128;
    else if (m == lw::M_WRITE) {                                    // LinkWord.badPacket(mode 0)
        const bool aligned = s == lw::SIZE_UNDEF || (a & ((4u << s) - 1)) == 0;
        const unsigned r = a >> 24;
        if (!aligned) bad = true;
        else if (r == lw::R_DMEM || r == lw::R_PMEM || r == lw::R_SRAM) bad = false;
        else if (r >= lw::R_PERIPH1 && r <= lw::R_PERIPH3) bad = ((1u >> (r - lw::R_PERIPH1)) & 1) == 0;
        else bad = true;
    }
    if (bad) violations++;
    if (s == lw::SIZE_UNDEF) violations++;
    if ((d > lw::CC3 && d < lw::BROADCAST) || d == lw::NONE) violations++;
    if (m == lw::M_GATHER && d != me) violations++;
    if (lw::reserved_mode(m)) violations++;
    if (m == lw::M_COUNT && lw::counter(h) == 0) violations++;
}

uint64_t Injection::digest() const {
    return sim::hash_fields(me, tokens_on, busy_out, tok.cap, tok.reads, tok.loop_held) ^ rs.digest();
}

}  // namespace units
