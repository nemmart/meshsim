// digest.h: the machine's digest, unit by unit (the owner's decision 2026-11-07). Not for unit authors.
//
// A machine's digest is a hash, in the order the units were added, of each unit's state (its `digest()`, or its bytes)
// and then the state of its ports: each Link, Fifo, credit Fifo, Hold, Reg and Count input, and each Wire, RegFile and
// Count output. A payload carried by reference is hashed by its slot's contents, never its index; the slot pools are
// hashed as a multiset of contents per slot size. So the digest doesn't depend on how the build numbered anything, and a
// machine split over several domains (threads.h) gives the same digest as the same machine in one: the partition
// changes indices and where a port lives, never what is in it. Only the visible state is hashed, not who is scheduled
// (step-all and the ready-driven run must agree).
#pragma once

#include <cstdint>
#include <vector>

#include "internals.h"

namespace sim::detail {

}  // namespace sim::detail
