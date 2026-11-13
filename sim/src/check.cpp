// check.cpp (WORKER20, P2): the checker's sink (check.h).
#include "check.h"

#include "base/fatal.h"

namespace sim::check {

void Sink::add(uint64_t cycle, int tile, Kind k, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    const unsigned ki = unsigned(k);
    if (ki < 256 && ((off[ki >> 6] >> (ki & 63)) & 1)) return;
    const uint32_t i = n.fetch_add(1, std::memory_order_relaxed);
    if (i >= cap) FATAL("the checker's fact list is full (%u facts): raise MachConfig::check_capacity", cap);
    Fact& x = f[i];
    x.cycle = cycle; x.tile = tile; x.kind = k; x.pad = 0;
    x.a[0] = a0; x.a[1] = a1; x.a[2] = a2; x.a[3] = a3;
}

}  // namespace sim::check
