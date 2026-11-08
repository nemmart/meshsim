// regions.cpp (WORKER10): the machine's memory, structure by structure, for footprint measurements (D15, D26: one
// chip's state should fit the caches of the cores it is split over). Configuration regime; nothing here runs in a cycle.
#include "config_impl.h"
#include "internals.h"
#include "threads.h"

namespace sim::detail {

namespace {
void one_machine(const Internals::SImpl& S, int domain, std::vector<Internals::MemRegion>& out) {
    const uint64_t* B = S.block ? S.block->words() : nullptr;
    for (const auto& c : S.carved) {
        if (!B || !c.words || c.what.rfind("padding", 0) == 0) continue;
        std::string what = c.what;
        if (c.type >= 0 && size_t(c.type) < S.types.size()) what += " (ports of " + S.types[c.type]->name + ")";
        out.push_back({what, B + c.off, c.words * 8, domain});
    }
    for (const auto& st : S.stores)
        out.push_back({"unit " + S.types[st.type]->name, st.base, size_t(S.types[st.type]->size) * st.count, domain});
    if (!S.link_at_v.empty()) out.push_back({"observer: arrival cycles", S.link_at_v.data(), S.link_at_v.size() * 8, domain});
    if (!S.run_ticks_v.empty()) out.push_back({"calibration: ticks", S.run_ticks_v.data(), S.run_ticks_v.size() * 8, domain});
}
}  // namespace

std::vector<Internals::MemRegion> Internals::regions(const Sim& s) {
    std::vector<MemRegion> out;
    const SImpl& S = *s.impl_;
    if (!S.threads) { one_machine(S, 0, out); return out; }
    const Threads& T = *S.threads;
    for (size_t d = 0; d < T.doms.size(); d++) one_machine(*T.doms[d].S, int(d), out);
    for (const auto& ob : T.outboxes) {
        const size_t W = size_t(ob.mask) + 1;
        out.push_back({std::string("outbox ") + (ob.direct ? "(direct) " : "") + "buckets", ob.buckets, W * sizeof(Bucket), int(ob.src)});
        // the entries: each bucket's storage, contiguous after the headers (threads.cpp's carving)
        const Word* first = ob.buckets[0].begin();
        out.push_back({std::string("outbox ") + (ob.direct ? "(direct) " : "") + "entries", first,
                       W * Bucket::words_needed(ob.width + 1u, ob.routes) * 8, int(ob.src)});
    }
    return out;
}

}  // namespace sim::detail
