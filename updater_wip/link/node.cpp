// node.cpp: see node.h.
#include "node.h"

#include <algorithm>
#include <cstring>

namespace hl {

void PacketFifo::push(uint8_t length, uint8_t hops, uint32_t dsum, const uint8_t* data) {
    Entry& e = ring[(head + count) % ring.size()];
    e.length = length; e.hops = hops; e.dsum = dsum; memcpy(e.data, data, size_t(length) * WORD_BYTES);
    count++; words += length;
    most = std::max(most, words);
}
const Offer* PacketFifo::front() {
    if (!count) return nullptr;
    const Entry& e = ring[head];
    offer.length = e.length; offer.hops = e.hops; offer.dsum = e.dsum; offer.data = e.data;
    return &offer;
}
void PacketFifo::pop() { words -= ring[head].length; head = (head + 1) % uint32_t(ring.size()); count--; }

ChainNode::ChainNode(const Params& params, bool down_neighbour, uint32_t forward_words)
    : p(params), has_down(down_neighbour), fwd_words(forward_words), up(params), down(params) {
    for (auto& f : fwd_down) f.size(forward_words);
    for (auto& f : fwd_up) f.size(forward_words);
}

void ChainNode::canon(Writer& a) const {
    up.canon(a);
    if (has_down) down.canon(a);
    for (const auto* set : {fwd_down, fwd_up}) for (unsigned s = 0; s < STREAMS; s++) const_cast<PacketFifo&>(set[s]).io(a);
}

void ChainNode::step(const NodeIn& in) {
    own_taken = own_passed = false;
    // The upper end: from the host, to the host.
    In u;
    u.rx_byte = in.up_rx;
    u.rx_forwards = has_down;
    const Offer* forwarded[STREAMS];
    for (unsigned s = 0; s < STREAMS; s++) {
        u.say_stop[s] = in.own_stop[s] || (has_down && fwd_down[s].half());
        u.rx_free[s] = has_down ? std::min(in.own_free[s], fwd_down[s].free_words()) : in.own_free[s];
        forwarded[s] = fwd_up[s].front();
        u.offer[s] = forwarded[s];
    }
    bool own_first = false;
    if (in.own_offer && (!forwarded[FG] || HL_PLANT(p, OWN_BEFORE_FORWARDED))) { u.offer[FG] = in.own_offer; own_first = true; }
    const Taken tu = up.step(u);
    for (unsigned s = 0; s < STREAMS; s++) {
        if (!tu.tx[s]) continue;
        if (s == FG && own_first) { own_taken = true; own_passed = forwarded[FG] != nullptr; }
        else fwd_up[s].pop();
    }
    if (up.r.rx_valid && has_down)
        fwd_down[up.r.rx_stream].push(up.r.rx_length, uint8_t(up.r.rx_hops + (HL_PLANT(p, HOPS_NOT_COUNTED) ? 0 : 1)), up.r.rx_dsum, up.rx_data());
    if (!has_down) return;
    // The lower end: down the chain, and back from it.
    In d;
    d.rx_byte = in.down_rx;
    d.rx_forwards = true;
    for (unsigned s = 0; s < STREAMS; s++) {
        d.say_stop[s] = fwd_up[s].half();
        d.rx_free[s] = fwd_up[s].free_words();
        d.offer[s] = fwd_down[s].front();
    }
    const Taken td = down.step(d);
    for (unsigned s = 0; s < STREAMS; s++) if (td.tx[s]) fwd_down[s].pop();
    if (down.r.rx_valid)
        fwd_up[down.r.rx_stream].push(down.r.rx_length, uint8_t(down.r.rx_hops + (HL_PLANT(p, HOPS_NOT_COUNTED) ? 0 : 1)), down.r.rx_dsum, down.rx_data());
}

}  // namespace hl
