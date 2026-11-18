// word_port.h: the Updater's data input, a flow-controlled port of 128-bit words from Ingress on the same tile
// (../v1.4_wip/planning/UPDATER-INTERFACE.md, "The data input", and INGRESS-INTERFACE.md, "The word port to the
// Updater"; the owner's design of 2026-11-16). Types only, no logic: what Ingress's forwarder and the Updater's
// controller compile against (updater_wip, WORKER6 and WORKER7).
//
// The source is Ingress: it offers the word at the head of its background queue while DMA_FORWARD_COUNT's count is not
// 0 and its discard bit is 0. The sink is the Updater. There are no credits and no event.
//
// A CYCLE, as axi_port.h's: every output is a flip-flop. In cycle t each side's outputs are a function of its own
// registers alone; a word moves in t when valid and ready are both 1 in t's outputs. At the edge that ends t each side
// takes the other side's outputs of t, never its insides. Words arrive in the stream's order, each once.
//
// valid MAY FALL without a word having moved: a store of 0 to the count, or of a count with discard set, takes the
// offer back. A sink must not depend on it staying. While the count stands and the queue holds words, the source
// holds it.
//
// THE NAMES THE SLOT WIRES (periph_slot.h): a peripheral is a source if it has port_source() and port_sent(t), a sink
// if it has port_sink() and port_take(t, word). The slot takes both outputs of cycle t before any peripheral steps,
// and when a word moves in t it calls the source's port_sent(t) and the sink's port_take(t, word), both at the edge
// that ends t.
#pragma once

#include <cstdint>

namespace units {
namespace wordport {

struct SourceOut {                               // what Ingress drives
    bool valid = false;
    uint64_t data[2] = {};                       // [0] bits 63:0, [1] bits 127:64, as an AXI beat's (axi_port.h)
};
struct SinkOut {                                 // what the Updater drives
    bool ready = false;
};

inline bool moves(const SourceOut& s, const SinkOut& k) { return s.valid && k.ready; }

}  // namespace wordport
}  // namespace units
