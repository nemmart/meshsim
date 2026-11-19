// wire_ends.h: the chip's two link ports on WORKER10's wire (wire/), for v1.37's chain of processes
// (MILESTONE-v1.37.md, "The picture"; the INTEGRATOR's stage 6). In place of the host's End (host_end.h), Ingress/
// Egress's far end is the wire: every chip cycle, from cycle 0, `exchange` on the end towards the driver with the
// bytes of `link_up`, and, with an ASIC below, on the end away from it with the bytes of `link_down` (and the pin
// chain_next 1). The wire's lane is the link's rate: B bytes a cycle each way, B link cycles a chip cycle.
//
// The chip's outputs of a cycle are flip-flops, final when given; its inputs of the cycle are what the wire gives,
// which a neighbour gave a window earlier. Nothing else of the wire reaches the chip.
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "units/ingress_egress.h"
#include "wire.h"

struct WireEnds {
    wire::LinkEnd* up = nullptr;       // towards the driver; never null on a chip
    wire::LinkEnd* down = nullptr;     // away from it: null for the last ASIC
    unsigned rate = 1;                 // the wire's lane bytes: the link's rate
    uint64_t cycles = 0;               // the cycles exchanged

    void attach(units::IngressEgress& ie) {
        if (!up) FATAL("wire_ends: a chip with no link towards the driver");
        ie.boundary = &WireEnds::cycle; ie.far_end = this;
        ie.link_rate = rate; ie.link_every = 1;
        ie.chain_next = down != nullptr;
    }
    static void cycle(void* self, int64_t, const units::IngressEgress::Pins::Out& o, units::IngressEgress::Pins::In& in) {
        WireEnds& w = *static_cast<WireEnds*>(self);
        w.up->exchange(o.link_up_tx, in.link_up_rx);
        if (w.down) w.down->exchange(o.link_down_tx, in.link_down_rx);
        w.cycles++;
    }
};
