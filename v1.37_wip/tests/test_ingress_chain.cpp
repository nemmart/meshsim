// test_ingress_chain: the chain of chapter 28 with no chips (v1.37 stage 4, the INTEGRATOR): a host's End and two or
// three Ingress/Egress units back to back (ingress_egress.h with WORKER8's Node; tests/host_end.h), each in a slot
// driven directly in place of its tile (SlotCore, as ingress_bench.h), its pin chain_next 1 but the last's; each
// unit's link_down to the next one's link_up, a byte each way a cycle, every line a chip cycle long.
//   - every word to every ASIC once and in order (28.12: every ASIC gets them all), both streams;
//   - the ASICs' replies (Egress, 17.6.8) at the host sorted by hops (0 the nearest), each in its order;
//   - the stop bits holding the host back when the last ASIC's queue fills, through the forwarding buffers (28.10,
//     28.12), with nothing refused anywhere, and every word arriving once the queue is drained;
//   - the reset of the middle ASIC (17.6.14, 28.14): its neighbours' link ends hear "I have started again", their
//     ERRORS.link_error 1; its own and the host's not.
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "harness.h"
#include "host_end.h"
#include "packet_writer.h"
#include "units/ingress_egress.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/regmap.h"

using namespace units;

namespace {

constexpr unsigned W_FOREGROUND_WORDS = 64, W_BACKGROUND_WORDS = 65, W_DMA_FORWARD_COUNT = 66, W_EGRESS_COUNT = 67;
constexpr uint32_t ERR_LINK = 1u << 1;
using Pins = IngressEgress::Pins;

struct Chain;
// One ASIC: its slot, the words its Updater's port took (always ready), its DMA's packets.
struct Asic {
    SlotCore s;
    std::vector<uint64_t> ported_lo;
    std::vector<uint64_t> fg_lo;                             // the foreground's words, read out by the DMA
    Chain* chain = nullptr;
    unsigned index = 0;
    IngressEgress& ie() { return s.ingress_egress(1); }
};

struct Chain {
    HostEnd host;
    std::vector<std::unique_ptr<Asic>> asics;
    std::vector<Pins::Out> snap;                             // every unit's outputs at the cycle's start
    uint8_t host_byte = 0;                                    // the host's byte of this cycle
    int64_t t = 1;
    Chain(unsigned n, const std::vector<uint8_t>& file) {
        std::string why;
        if (!HostEnd::from_packet_file(file.data(), file.size(), host.packets, why)) FATAL("test_ingress_chain: %s", why.c_str());
        for (unsigned i = 0; i < n; i++) {
            auto a = std::make_unique<Asic>();
            a->s.tile = lw::CC1;
            a->s.install(1, PT_INGRESS);
            a->chain = this; a->index = i;
            IngressEgress& ie = a->ie();
            ie.chain_next = i + 1 < n;
            ie.boundary = &Chain::cycle; ie.far_end = a.get();
            ie.reset(0);
            a->s.ext_sink.ctx = a.get();
            a->s.ext_sink.out = [](void*, int64_t) { units::wordport::SinkOut o; o.ready = true; return o; };
            a->s.ext_sink.take = [](void* x, int64_t, const units::wordport::SourceOut& w) { static_cast<Asic*>(x)->ported_lo.push_back(w.data[0]); };
            asics.push_back(std::move(a));
        }
        snap.resize(n);
    }
    // A unit's far end: the bytes of its neighbours' outputs at the cycle's start (the host's for the first).
    static void cycle(void* self, int64_t, const Pins::Out&, Pins::In& in) {
        Asic& a = *static_cast<Asic*>(self);
        Chain& c = *a.chain;
        in.link_up_rx[0] = a.index == 0 ? c.host_byte : c.snap[a.index - 1].link_down_tx[0];
        in.link_down_rx[0] = a.index + 1 < c.asics.size() ? c.snap[a.index + 1].link_up_tx[0] : 0;
    }
    void tick() {
        for (size_t i = 0; i < asics.size(); i++) snap[i] = asics[i]->ie().out();
        {   // the host's end: its link packets pushed as there is room, one link cycle with the first unit's byte
            units::IngressEgress::LinkAllocs a;
            while (host.next < host.packets.size() && host.end.room(host.packets[host.next].stream) >= host.packets[host.next].n) {
                host.end.push(host.packets[host.next].stream, host.packets[host.next].w, host.packets[host.next].n);
                host.next++;
            }
            host_byte = host.end.clock(snap[0].link_up_tx[0]);
        }
        for (auto& a : asics) { a->s.run_cycle(t); for (unsigned k = 0; k < a->s.n_out; k++) a->fg_lo.push_back(a->s.out[k].lo); a->s.end_cycle(t); }
        t++;
    }
    void run(int64_t n) { for (int64_t k = 0; k < n; k++) tick(); }
};

std::vector<uint8_t> data_of(uint32_t tag, unsigned words) {
    std::vector<uint8_t> d;
    for (unsigned w = 0; w < words; w++) {
        const uint64_t lo = uint64_t(tag) << 32 | w, hi = ~lo;
        for (uint64_t half : {lo, hi}) for (unsigned b = 0; b < 8; b++) d.push_back(uint8_t(half >> (8 * b)));
    }
    return d;
}
void add(std::vector<uint8_t>& file, unsigned stream, uint32_t number, uint32_t tag, unsigned words) {
    packet_writer::Fields f;
    f.stream = stream; f.number = number; f.length = words;
    const std::vector<uint8_t> d = data_of(tag, words);
    packet_writer::packet(file, f, d.data());
}
// The words of `tags` in order, `words` each, as their low halves.
std::vector<uint64_t> words_of(uint32_t first, unsigned n, unsigned words) {
    std::vector<uint64_t> v;
    for (uint32_t k = first; k < first + n; k++) for (unsigned w = 0; w < words; w++) v.push_back(uint64_t(k) << 32 | w);
    return v;
}
// The foreground, read out at each unit by its DMA as its queue holds words (the flag's work done by polling).
void read_foreground(Chain& c) {
    for (auto& a : c.asics) {
        uint32_t v = 0, busy = 0;
        a->s.peek(c.t, 1, P_STATUS, busy);
        if (busy & 2) continue;
        a->s.peek(c.t, 1, W_FOREGROUND_WORDS, v);
        if (v == 0) continue;
        a->s.write(c.t, 1, P_DMA_HDR, lw::SIZE_128 | lw::M_WRITE << HDR_MODE_LO | 64u << HDR_DST_LO | v << DMA_HDR_COUNT_LO);
        a->s.write(c.t, 1, P_DMA_START, 1);
    }
}

}  // namespace

// 28.12: every word to every ASIC, once and in order, on both streams; a chain of two and of three.
TEST(chain_every_word_to_every_asic_once_and_in_order) {
    std::vector<uint8_t> file;
    for (uint32_t k = 1; k <= 30; k++) add(file, 2, k, k, 32);   // the background: 960 words
    for (uint32_t k = 1; k <= 6; k++) add(file, 1, k, 0x100 + k, 5);   // the foreground: 30 words
    for (unsigned n : {2u, 3u}) {
        Chain c(n, file);
        for (auto& a : c.asics) a->s.write(0, 1, W_DMA_FORWARD_COUNT, 30 * 32);
        for (int k = 0; k < 600000; k++) {
            c.tick();
            if (k % 64 == 0) read_foreground(c);
            bool all = true;
            for (auto& a : c.asics) all = all && a->ported_lo.size() == 30 * 32 && a->fg_lo.size() == 30;
            if (all) break;
        }
        const std::vector<uint64_t> bg = words_of(1, 30, 32), fg = words_of(0x101, 6, 5);
        for (auto& a : c.asics) {
            CHECK(a->ported_lo == bg);
            CHECK(a->fg_lo == fg);
            uint32_t e = 0;
            a->s.peek(c.t, 1, P_ERRORS, e);
            CHECK_EQ(e, 0u);
            CHECK_EQ(a->ie().node->counts_above().refused, 0u);
        }
        std::printf("test_ingress_chain: %u ASICs: every word at every ASIC by cycle %lld\n", n, (long long)c.t);
    }
}

// 17.6.8, 28.12: each ASIC's reply, a frame of its own length, at the host sorted by hops: the nearest's at hops 0.
TEST(chain_replies_sorted_by_hops) {
    std::vector<uint8_t> file;
    add(file, 1, 1, 1, 1);
    Chain c(3, file);
    c.run(6000);                                             // the links in step
    for (unsigned i = 0; i < 3; i++) {
        Asic& a = *c.asics[i];
        const unsigned n = 20 + 17 * i;                      // 20, 37, 54 words: 1, 2 and 2 link packets
        a.s.write(c.t, 1, W_EGRESS_COUNT, n);
        for (unsigned w = 0; w < n; w++) {
            const uint64_t lo = uint64_t(0xE0 + i) << 32 | w;
            a.s.accept(c.t, 1, lo, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04100000u, lw::SIZE_128), ~lo);
        }
    }
    std::vector<uint64_t> got[3];
    for (int k = 0; k < 200000; k++) {
        c.tick();
        units::IngressEgress::LinkAllocs a;
        for (unsigned h = 0; h < 3; h++) {
            ::hostlink::Word w;
            while (c.host.end.take_from(h, w)) { uint64_t lo; std::memcpy(&lo, w.byte, 8); got[h].push_back(lo); }
        }
        if (got[0].size() == 20 && got[1].size() == 37 && got[2].size() == 54) break;
    }
    for (unsigned h = 0; h < 3; h++) {
        CHECK_EQ(got[h].size(), size_t(20 + 17 * h));
        for (size_t w = 0; w < got[h].size(); w++) CHECK_EQ(got[h][w], uint64_t(0xE0 + h) << 32 | w);
    }
}

// 28.12: "the chain comes before an ASIC's own data": the first ASIC's reply of 8 link packets and the second's of 4,
// written in the same cycle; the second's reach the first while it is still sending its own, and each is sent before
// the first's next own link packet: at the host, in the order they arrived, the second's link packets are not all
// after the first's.
TEST(chain_forwarded_before_own) {
    std::vector<uint8_t> file;
    add(file, 1, 1, 1, 1);
    Chain c(2, file);
    c.run(6000);
    for (unsigned i = 0; i < 2; i++) {
        Asic& a = *c.asics[i];
        const unsigned n = i == 0 ? 256 : 128;
        a.s.write(c.t, 1, W_EGRESS_COUNT, n);
        for (unsigned w = 0; w < n; w++) {
            const uint64_t lo = uint64_t(0xE0 + i) << 32 | w;
            a.s.accept(c.t, 1, lo, lw::hdr(7, 64, lw::M_WRITE, 0, 0x04100000u, lw::SIZE_128), ~lo);
        }
    }
    std::vector<unsigned> order;                             // the hops of each link packet, in the order they arrived
    size_t words = 0;
    for (int k = 0; k < 200000 && words < 384; k++) {
        c.tick();
        units::IngressEgress::LinkAllocs a;
        ::hostlink::Word w;
        unsigned hops = 0;
        bool last = false;
        while (c.host.end.take(::hostlink::FOREGROUND, w, &hops, &last)) { words++; if (last) order.push_back(hops); }
    }
    CHECK_EQ(words, size_t(384));
    CHECK_EQ(order.size(), size_t(12));
    size_t last_own = 0, first_forwarded = order.size();
    for (size_t k = 0; k < order.size(); k++) { if (order[k] == 0) last_own = k; if (order[k] == 1 && first_forwarded == order.size()) first_forwarded = k; }
    CHECK(first_forwarded < last_own);
    std::printf("test_ingress_chain: the replies' link packets at the host by hops, in their order:");
    for (unsigned h : order) std::printf(" %u", h);
    std::printf("\n");
}

// 28.10, 28.12: the last ASIC's queue not drained: its stop bit up, its forwarding buffers above filling to half and
// saying stop in turn, the host held back; nothing refused at any link end; drained, every word arrives everywhere.
TEST(chain_stop_bits_hold_the_host_back) {
    std::vector<uint8_t> file;
    for (uint32_t k = 1; k <= 120; k++) add(file, 2, k, k, 32);   // 3,840 words: past every queue and buffer
    Chain c(3, file);
    for (unsigned i = 0; i < 2; i++) c.asics[i]->s.write(0, 1, W_DMA_FORWARD_COUNT, 120 * 32);
    c.run(150000);                                           // the last ASIC takes nothing
    uint32_t held = 0;
    c.asics[2]->s.peek(c.t, 1, W_BACKGROUND_WORDS, held);
    CHECK(held >= 512 && held <= 512 + 8 * 32);
    CHECK(c.asics[2]->ie().node->above().stop_said[1]);
    CHECK(c.asics[1]->ie().node->above().stop_said[1]);     // its forwarding buffer down, half full
    CHECK(c.host.end.status().stop_heard[1]);
    CHECK(c.host.next < c.host.packets.size() || c.host.end.room(::hostlink::BACKGROUND) < 1024);
    for (auto& a : c.asics) { CHECK_EQ(a->ie().node->counts_above().refused, 0u); CHECK_EQ(a->ie().node->counts_below().refused, 0u); }
    c.asics[2]->s.write(c.t, 1, W_DMA_FORWARD_COUNT, 120 * 32);
    for (int k = 0; k < 2000000 && c.asics[2]->ported_lo.size() < 120 * 32; k++) c.tick();
    const std::vector<uint64_t> bg = words_of(1, 120, 32);
    for (auto& a : c.asics) CHECK(a->ported_lo == bg);
    for (auto& a : c.asics) { CHECK_EQ(a->ie().node->counts_above().refused, 0u); CHECK_EQ(a->ie().node->counts_below().refused, 0u); }
}

// 17.6.14, 28.14: the middle ASIC's reset: its link ends start again saying so; its neighbours' link ends hear it and
// their ERRORS.link_error is 1; the middle's own ERRORS 0 (it started itself), the host's End without an error.
TEST(chain_reset_of_the_middle_asic_is_its_neighbours_link_error) {
    std::vector<uint8_t> file;
    add(file, 2, 1, 1, 4);
    Chain c(3, file);
    c.run(8000);
    for (auto& a : c.asics) { uint32_t e = 0; a->s.peek(c.t, 1, P_ERRORS, e); CHECK_EQ(e, 0u); CHECK(a->ie().node->above().in_step); }
    c.asics[1]->s.reset(c.t);
    c.run(20000);
    uint32_t e[3] = {};
    for (unsigned i = 0; i < 3; i++) c.asics[i]->s.peek(c.t, 1, P_ERRORS, e[i]);
    CHECK_EQ(e[0], ERR_LINK);
    CHECK_EQ(e[1], 0u);
    CHECK_EQ(e[2], ERR_LINK);
    CHECK(c.asics[0]->ie().node->below().other_end);
    CHECK(c.asics[2]->ie().node->above().other_end);
    CHECK(!c.host.end.status().error);
    for (auto& a : c.asics) CHECK(a->ie().node->above().in_step);
}
