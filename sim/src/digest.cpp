// digest.cpp: the machine's digest, unit by unit (WORKER4, E5). The configuration regime: called between cycles.
#include "digest.h"

#include <map>

namespace sim::detail {

namespace {
struct Hash {
    uint64_t h = 0x9e3779b97f4a7c15ull;
    void add(uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); h *= 0xff51afd7ed558ccdull; }
};
uint64_t slot_hash(const SlotPool& p, uint32_t i) {
    Hash s;
    for (unsigned w = 0; w < p.slot_words(); w++) s.add(p.slot(i)[w]);
    return s.h;
}
// One payload of `width` words: word 0 is a slot index when the port carries by reference.
void payload(Hash& h, const Engine& e, uint16_t cls, const Word* w, unsigned width) {
    if (cls != kNoClass && width) {
        h.add(slot_hash(e.pools[cls], uint32_t(w[0])));
        for (unsigned k = 1; k < width; k++) h.add(w[k]);
        return;
    }
    for (unsigned k = 0; k < width; k++) h.add(w[k]);
}
void ring(Hash& h, const Engine& e, uint16_t cls, const Ring& r, unsigned width) {
    h.add(r.count());
    for (unsigned i = 0; i < r.count(); i++) payload(h, e, cls, r.peek(i), width);
}
}  // namespace

void Internals::record_view(const Config::Impl& I, const std::vector<uint32_t>& inst_of, DigestView& v) {
    v.units.clear();
    v.units.reserve(I.units.size());
    for (uint32_t u = 0; u < I.units.size(); u++) {
        DigestUnit du{0, inst_of[u], false, {}};
        for (uint32_t id : I.units[u].decls) {
            const auto& d = I.decls[id];
            const bool keep = d.out ? (d.kind == Kind::wire || d.kind == Kind::regfile || d.kind == Kind::count)
                                    : (d.kind != Kind::wire && d.kind != Kind::regfile);
            if (!keep || d.final == kNone) continue;
            du.ports.push_back(DigestPort{d.kind, d.out, 0, d.final});
        }
        v.units.push_back(std::move(du));
    }
}

uint64_t Internals::digest(const DigestView& v, const Sim::Impl* const* sims, Cycle now) {
    Hash h;
    h.add(now);
    for (const DigestUnit& u : v.units) {
        if (u.skip) continue;
        const Sim::Impl& S = *sims[u.eng];
        h.add(S.types[S.inst[u.inst].type]->digest(S.obj(u.inst)));
        for (const DigestPort& p : u.ports) {
            const Engine& e = sims[p.eng]->e;
            switch (p.kind) {
                case Kind::link: {
                    const LinkPort& l = e.links[p.idx];
                    ring(h, e, l.cls, l.ring, l.width); h.add(l.inflight);
                    break;
                }
                case Kind::fifo:
                case Kind::credit: {
                    const FifoPort& f = e.fifos[p.idx];
                    ring(h, e, f.cls, f.ring, f.width); h.add(f.overflows); h.add(f.first_overflow);
                    if (p.kind == Kind::credit)
                        h.add(p.ceng == kNone ? f.credits : sims[p.ceng]->e.fifos[p.cidx].credits);
                    break;
                }
                case Kind::hold: {
                    const HoldPort& q = e.holds[p.idx];
                    h.add(q.occ); h.add(q.refusals); h.add(q.refused_at);
                    if (q.occ) payload(h, e, q.cls, q.w, q.width);
                    break;
                }
                case Kind::reg: {
                    const RegPort& r = e.regs[p.idx];
                    h.add(r.valid);
                    for (unsigned k = 0; k < r.width; k++) h.add(r.w[k]);
                    break;
                }
                case Kind::count: h.add(e.counts[p.idx].total); break;
                case Kind::wire: h.add(e.wires[p.idx].v); break;
                case Kind::regfile:
                    for (uint32_t k = 0; k < e.regfiles[p.idx].n; k++) h.add(e.regfiles[p.idx].regs[k]);
                    break;
                default: break;
            }
        }
    }
    // the slot pools: per slot size, how many are in use and the multiset of their contents (a sum: order-free)
    std::map<unsigned, std::pair<uint64_t, uint64_t>> classes;
    std::vector<const Sim::Impl*> seen;
    for (const DigestUnit& u : v.units) {
        bool dup = false;
        for (auto* s : seen) dup = dup || s == sims[u.eng];
        if (dup) continue;
        seen.push_back(sims[u.eng]);
        const Engine& e = sims[u.eng]->e;
        for (uint32_t k = 0; k < e.npools; k++) {
            const SlotPool& p = e.pools[k];
            auto& c = classes[p.slot_words()];
            for (uint32_t i = 0; i < p.capacity(); i++)
                if (p.allocated(i)) { c.first++; c.second += slot_hash(p, i); }
        }
    }
    for (auto& [w, c] : classes) { h.add(w); h.add(c.first); h.add(c.second); }
    return h.h;
}

}  // namespace sim::detail
