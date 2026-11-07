// digest_view.h: what the unit-by-unit digest walks (WORKER4, E5; digest.h). Kept apart so config_impl.h can hold one.
#pragma once

#include <cstdint>
#include <vector>

#include "engine.h"

namespace sim::detail {

struct DigestPort {
    Kind kind;
    bool out;
    uint32_t eng, idx;                    // the engine holding the port (a domain), and its pool index there
    uint32_t ceng = kNone, cidx = kNone;  // a credit Fifo across domains: where its producer's credits live (the proxy)
};
struct DigestUnit {
    uint32_t eng, inst;                   // the unit's engine and instance
    bool skip = false;                    // a boundary unit (threads.h): no state of the machine's own
    std::vector<DigestPort> ports;
    std::vector<std::pair<uint32_t, uint32_t>> handles;   // (offset, bytes) of each port handle in the unit's object
};
struct DigestView {
    std::vector<DigestUnit> units;        // in add order
};

}  // namespace sim::detail
