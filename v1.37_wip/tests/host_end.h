// host_end.h: the host's end of the first link (machine-spec 28; v1.37 stage 4, the INTEGRATOR), for the harnesses:
// WORKER8's `hostlink::End` (../updater_wip/link, hostlink.h) on the master tile's port `link_up`, through Ingress/
// Egress's far end (ingress_egress.h, THE PINS, THE LINK). Not chip hardware: the host is outside the chip.
//
//   THE HOST'S LINK PACKETS are given as they are to go: each a stream and 1 to 32 words, in order (a frame starts a
//   link packet: 29.2, 30). The End numbers them, keeps them until acknowledged, sends them again, and recovers;
//   `push` puts each in the End's queue once the End has room for it, in order, stream by stream as one sequence.
//   `from_packet_file` reads them out of a packet file of the black box's (units/host_link.h: the tests' writer and
//   gold's), which says each link packet's stream and words: until gold is brought to chapter 28, the split a proof's
//   frames are sent in.
//   THE CHIP'S REPLIES are the End's, by hops (End::take_from): read by the test.
//   THE LINE: `rate` link cycles a chip cycle (Ingress/Egress's link_rate, the same), each a byte each way, the End
//   clocked once a link cycle with the byte the chip put on the line. The wire is a chip cycle long: the End's bytes of
//   chip cycle t reach the chip's Node in t, and the Node's in t reach the End in t + 1 (its output flip-flops). `noise` may change a byte the chip receives
//   (the line's own errors, which the link recovers from: 28.8).
//
// The End allocates (its queues are std::deques): its calls are made with the simulator's guard lifted, as the
// Node's are (IngressEgress::LinkAllocs).
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "hostlink.h"
#include "units/host_link.h"
#include "units/ingress_egress.h"

struct HostEnd {
    using Pins = units::IngressEgress::Pins;
    struct Packet { ::hostlink::Stream stream; unsigned n; ::hostlink::Word w[::hostlink::MAX_WORDS]; };

    ::hostlink::End end;
    std::vector<Packet> packets;       // the host's link packets, in the order they go
    size_t next = 0;                   // the next to push into the End
    unsigned rate = 1;                 // link cycles a chip cycle
    unsigned every = 1;                // or chip cycles a link cycle (Ingress/Egress's link_every, the same)
    std::function<uint8_t(int64_t t, unsigned k, uint8_t byte)> noise;   // the byte the chip receives; null: as sent
    std::function<void(::hostlink::End& end, int64_t t)> hook;           // the End in each chip cycle, before anything
    std::function<bool(size_t i, int64_t t)> gate;                       // may link packet i go into the End in t? null: yes
    uint64_t link_cycles = 0, words_pushed = 0;
    uint64_t stop_cycles = 0;          // the chip cycles the End held words unsent with the chip's stop bit up for their stream
    bool file_cut = false;             // the packet file ended inside a packet: the rest was not sent
    int64_t last_push_at = -1;         // the chip cycle the last link packet went into the End's queue
    ::hostlink::Sizes sizes;
    explicit HostEnd(const ::hostlink::Sizes& z = ::hostlink::Sizes()) : end(z), sizes(z) {}

    // The black box's packet file as the host's link packets (header): those with data, in the file's order. False, with
    // `why`, if it is not a packet file whose every packet passes its checks.
    // (`cut`: a file that ends inside a packet is the packets before it, and *cut is set.)
    static bool from_packet_file(const uint8_t* bytes, size_t n, std::vector<Packet>& out, std::string& why, bool* cut = nullptr) {
        units::hostlink::Reader r;
        r.open(bytes, n);
        units::hostlink::Packet p;
        units::hostlink::Check check;
        size_t taken = 0;
        for (;;) {
            const units::hostlink::Reader::Next k = r.next(p, check, taken);
            if (k == units::hostlink::Reader::Next::END) return true;
            if (k == units::hostlink::Reader::Next::CUT) {
                if (cut) { *cut = true; return true; }
                why = "the file ends inside a packet"; return false;
            }
            if (check != units::hostlink::Check::OK) { why = "a packet fails its checks"; return false; }
            if (p.length == 0) continue;
            Packet h;
            h.stream = p.stream == 1 ? ::hostlink::FOREGROUND : ::hostlink::BACKGROUND;
            h.n = p.length;
            for (unsigned w = 0; w < p.length; w++) { std::memcpy(h.w[w].byte, &p.lo[w], 8); std::memcpy(h.w[w].byte + 8, &p.hi[w], 8); }
            out.push_back(h);
        }
    }

    void attach(units::IngressEgress& ie) { ie.boundary = &HostEnd::cycle; ie.far_end = this; ie.link_rate = rate; ie.link_every = every; }
    bool all_pushed() const { return next == packets.size(); }

    static void cycle(void* self, int64_t t, const Pins::Out& o, Pins::In& in) { static_cast<HostEnd*>(self)->step(t, o, in); }
    void step(int64_t t, const Pins::Out& o, Pins::In& in) {
        units::IngressEgress::LinkAllocs a;
        if (hook) hook(end, t);
        while (next < packets.size() && end.room(packets[next].stream) >= packets[next].n && (!gate || gate(next, t))) {
            if (!end.push(packets[next].stream, packets[next].w, packets[next].n)) FATAL("host_end: the End took no link packet with room for it");
            words_pushed += packets[next].n; last_push_at = t;
            next++;
        }
        if (every > 1 && t % int64_t(every) != 0) return;   // no link cycle in this chip cycle
        {
            const ::hostlink::LinkStatus st = end.status();
            for (unsigned s = 0; s < 2; s++)
                if (st.stop_heard[s] && end.room(::hostlink::Stream(s)) < sizes.send_words) { stop_cycles++; break; }
        }
        for (unsigned k = 0; k < rate; k++) {
            const uint8_t b = end.clock(o.link_up_tx[k]);
            in.link_up_rx[k] = noise ? noise(t, k, b) : b;
            link_cycles++;
        }
    }
};
