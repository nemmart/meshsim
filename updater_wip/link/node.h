// node.h: a chain node of HOST-LINK.md ("HL", "The chain"): two link ends, one up the chain towards the host and one
// down, with the forwarding between them in both directions.
//
// A packet taken on the upper end goes, in the same cycle, to the node itself (its words, for its own queue) and into
// the forwarding buffer down (the whole packet, hops plus one). A packet taken on the lower end goes into the
// forwarding buffer up. Each end is offered the forwarding buffer's packet before the node's own (HL: the chain
// comes before an ASIC's own data). What is forwarded keeps its stream, length, data and data checksum; its numbers,
// acknowledge fields and flags are the outgoing end's, and that end seals the header (link_end.cpp, start_header).
//
// QUESTIONS.md: G5, a packet is acknowledged when it is taken here, into the queue and the buffer, not when the next
// link has acknowledged it. G15, a node is told whether it has a neighbour down the chain (has_down): the last has
// no lower end at all. G16, a forwarding buffer for each stream and direction; a link's stop bit is up when the
// node's own queue or the forwarding buffer behind that link is half full.
#pragma once

#include <vector>

#include "link_end.h"

namespace hl {

// A forwarding buffer: whole packets, first in first out, its room counted in words.
struct PacketFifo {
    struct Entry { uint8_t length = 0, hops = 0; uint32_t dsum = 0; uint8_t data[MAX_WORDS * WORD_BYTES]; };
    std::vector<Entry> ring;
    uint32_t head = 0, count = 0, words = 0, cap_words = 0, most = 0;
    Offer offer;

    void size(uint32_t cap) { cap_words = cap; ring.assign(cap ? cap : 1, Entry()); head = count = words = 0; }
    bool empty() const { return count == 0; }
    uint32_t free_words() const { return cap_words - words; }
    bool half() const { return words * 2 >= cap_words; }
    void push(uint8_t length, uint8_t hops, uint32_t dsum, const uint8_t* data);
    const Offer* front();                  // the oldest packet, as an offer to a sender; null if empty
    void pop();
    template <class A> void io(A& a) {
        a(count); a(words);
        for (uint32_t i = 0; i < count; i++) {
            Entry& e = ring[(head + i) % ring.size()];
            a(e.length); a(e.hops); a(e.dsum);
            for (unsigned b = 0; b < unsigned(e.length) * WORD_BYTES; b++) a(e.data[b]);
        }
    }
};

struct NodeIn {
    uint8_t up_rx = 0, down_rx = 0;        // the two wires in
    bool own_stop[STREAMS] = {};           // the node's own queues ask for "stop"
    uint32_t own_free[STREAMS] = {};       // the words they can take
    const Offer* own_offer = nullptr;      // the node's own next packet towards the host (foreground), or null
};

class ChainNode {
public:
    Params p;
    bool has_down = false;                 // G15
    uint32_t fwd_words = 1024;             // G16: a forwarding buffer's size
    LinkEnd up, down;
    PacketFifo fwd_down[STREAMS], fwd_up[STREAMS];
    bool own_taken = false;                // this cycle: the node's own offer was taken
    bool own_passed = false;               // this cycle: an own packet was taken while a forwarded one waited (a planted bug's)

    ChainNode(const Params& params, bool down_neighbour, uint32_t forward_words);
    void step(const NodeIn& in);           // then: up.r.rx_valid and up.rx_data() are a packet for the node itself

    template <class A> void io(A& a) { up.io(a); if (has_down) down.io(a); for (auto& f : fwd_down) f.io(a); for (auto& f : fwd_up) f.io(a); }
    void canon(Writer& a) const;
};

}  // namespace hl
