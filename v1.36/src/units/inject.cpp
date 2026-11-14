// inject.cpp: the inject unit's configuration, the injection's checks and its digest. The step is inline in
// inject.h.
#include "units/inject.h"

#include "base/fatal.h"

namespace units {

Injection::Injection(sim::Ports& p, unsigned tile, const NetConfig& c, const ReadSideConfig& rc) : me(uint8_t(tile)), tokens_on(c.loopback_tokens) {
    if (tile > 63 && !lw::controller(tile)) FATAL("injection: unit %u (0-63 a tile, 64-66 a controller)", tile);
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
    uint64_t h = sim::hash_fields(head, n, in_flight, busy_out);
    for (unsigned k = 0; k < n; k++) { const Read& r = line[(head + k) % LINE]; h = sim::hash_mix(h, sim::hash_fields(r.a, r.b, r.taken_at, r.client, r.zero)); }
    return h;
}

// InjectionPath's invariants 5, 17, 10, 21 (judged against peripheral 1 alone, which every unit has; LinkWord.whyBad
// with periphPresent 1, size 11), counted: meshsim's checks, the packet is injected anyway.
void Injection::check(uint64_t t, uint64_t dw, uint64_t h, uint64_t hi) {
    namespace ck = sim::check;
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
    // each counted violation also a fact for the checker (src/check.h; nothing unless the harness installs a sink)
    if (bad) { violations++; ck::record(t, int(me), ck::Kind::INJ_BAD_PACKET, 0, dw, h, hi); }
    if (s == lw::SIZE_UNDEF) { violations++; ck::record(t, int(me), ck::Kind::INJ_SIZE_UNDEF, 0, dw, h, hi); }
    if ((d > lw::CC3 && d < lw::BROADCAST) || d == lw::NONE) { violations++; ck::record(t, int(me), ck::Kind::INJ_NO_ENDPOINT, 0, dw, h, hi); }
    if (m == lw::M_GATHER && d != me) { violations++; ck::record(t, int(me), ck::Kind::INJ_GATHER_ELSEWHERE, 0, dw, h, hi); }
    if (lw::reserved_mode(m)) { violations++; ck::record(t, int(me), ck::Kind::INJ_RESERVED_MODE, 0, dw, h, hi); }
    if (m == lw::M_COUNT && lw::counter(h) == 0) { violations++; ck::record(t, int(me), ck::Kind::INJ_COUNT_CTR0, 0, dw, h, hi); }
}

uint64_t Injection::digest() const {
    return sim::hash_fields(me, tokens_on, busy_out, tok.cap, tok.reads, tok.loop_held) ^ rs.digest();
}

}  // namespace units
