// ingress_bench.h: the master tile's slot with Ingress/Egress, driven directly (v1.37; tests/test_ingress_link.cpp
// and tests/test_packet_vectors.cpp): SlotCore with Ingress/Egress (17.6) as peripheral 1, a load, a store
// and a consuming load of EVENTS at cycle t as the slot's own calls, then the slot's cycle t (as test_tracer_slot's
// reference run drives a slot). The DMA's packets are kept as they leave it. The word port's sink is the bench's own
// (SlotCore::ext_sink): its ready always 1, always 0, or at random from a seed (a register of its own, a xorshift
// stepped every cycle), and every word it takes kept with its cycle.
#pragma once

#include <cstdint>
#include <vector>

#include "units/host_link.h"
#include "units/ingress_egress.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/regmap.h"

namespace ingress_bench {

using namespace units;

// 17.6.1's numbers (v1.37's stage 3; II's until then: the bits and the words moved)
constexpr uint32_t ST_FOREGROUND_READY = 1u << 2, ST_BACKGROUND_READY = 1u << 3;
constexpr uint32_t EVENT_DMA_DONE = 1u << 1, EVENT_FOREGROUND_SERVICE = 1u << 6, EVENT_BACKGROUND_SERVICE = 1u << 7;
constexpr uint32_t EVENT_EGRESS_CREDIT = 1u << 8;
// the black box's link (until stage 4): a packet failing a check is link_error, one with a field that cannot be is
// format_error with it (ingress_egress.h, THE LINK); CRC and SEQUENCE are II's names for the first kind
constexpr uint32_t ERR_LINK = 1u << 1, ERR_CRC = ERR_LINK, ERR_SEQUENCE = ERR_LINK, ERR_FORMAT = ERR_LINK | 1u << 2;
constexpr uint32_t ERR_EGRESS = 1u << 3;
constexpr unsigned W_FOREGROUND_WORDS = 64, W_BACKGROUND_WORDS = 65, W_DMA_FORWARD_COUNT = 66, W_EGRESS_COUNT = 67;
constexpr unsigned W_UART_TX = 30, W_UART_RX = 31, W_UART_CONTROL = 42;
constexpr unsigned INGRESS_QUEUE_WORDS_DEFAULT = 1024;        // 19
constexpr uint32_t DISCARD = 1u << 31;                        // DMA_FORWARD_COUNT [31]
constexpr uint32_t EVENT_IDLE = EVENT_EGRESS_CREDIT;          // 17.6.14: egress_credit at 2; 17.6.9: uart_tx and uart_rx 0 in the UART's reset

struct Landed { int64_t t; uint64_t lo, hi; uint32_t addr; unsigned dst, counter, mode, size; };
struct Ported { int64_t t; uint64_t lo, hi; };                 // a word the sink took, at the end of cycle t

struct Bench {
    SlotCore s;
    std::vector<uint8_t> file;
    std::vector<Landed> out;
    std::vector<Ported> ported;
    int64_t t = 1;
    enum Ready { ALWAYS, NEVER, RANDOM } ready = ALWAYS;
    uint64_t rng = 0x2545F4914F6CDD1Dull;                     // the sink's register, for RANDOM
    int64_t rng_at = 0;                                       // the cycle it was last stepped for
    explicit Bench(std::vector<uint8_t> bytes, int64_t cycles_per_byte = 0, unsigned queue_words = INGRESS_QUEUE_WORDS_DEFAULT)
        : file(std::move(bytes)) {
        s.tile = lw::CC1;
        PeriphSlotConfig c;
        c.ingress_queue_words = queue_words;
        s.install(1, PT_INGRESS, c);
        ie().link_cycles_per_byte = cycles_per_byte;
        ie().set_link(file.data(), file.size());
        s.ext_sink.ctx = this;
        s.ext_sink.out = [](void* x, int64_t at) {
            Bench& b = *static_cast<Bench*>(x);
            units::wordport::SinkOut o;
            o.ready = b.ready == ALWAYS || (b.ready == RANDOM && (b.random_at(at) & 3) != 0);
            return o;
        };
        s.ext_sink.take = [](void* x, int64_t at, const units::wordport::SourceOut& w) {
            static_cast<Bench*>(x)->ported.push_back({at, w.data[0], w.data[1]});
        };
    }
    // The sink's random register as it stands in cycle `at` (stepped once a cycle; a function of the cycle alone).
    uint64_t random_at(int64_t at) {
        while (rng_at < at) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; rng_at++; }
        return rng;
    }
    IngressEgress& ie() { return s.ingress_egress(1); }
    void tick() {
        s.run_cycle(t);
        for (unsigned k = 0; k < s.n_out; k++) {
            const uint64_t h = s.out[k].h;
            out.push_back({t, s.out[k].lo, s.out[k].hi, lw::addr(h), lw::dst(h), lw::counter(h), lw::mode(h), lw::size(h)});
        }
        s.end_cycle(t);
        t++;
    }
    void run(int64_t cycles) { for (int64_t k = 0; k < cycles; k++) tick(); }
    uint32_t load(unsigned w) { uint32_t v = 0; s.peek(t, 1, w, v); return v; }
    uint32_t events() { uint32_t v = 0; s.read(t, 1, P_EVENTS, v); return v; }     // a consuming load
    void mask(uint32_t m) { s.write(t, 1, P_EVENTS_MASK, m); tick(); }
    void store(unsigned w, uint32_t v) { s.write(t, 1, w, v); tick(); }
    // A transfer of n words of stream `queue` to `addr` at tile `dst`, counted on `counter`: to its dma_done.
    void dma(unsigned queue, unsigned n, unsigned dst, uint32_t addr, unsigned counter) {
        s.write(t, 1, P_DMA_HDR, lw::SIZE_128 | counter << HDR_COUNTER_LO | lw::M_WRITE << HDR_MODE_LO | dst << HDR_DST_LO | n << DMA_HDR_COUNT_LO | queue << DMA_HDR_QUEUE_LO);
        tick();
        s.write(t, 1, P_DMA_DEST_ADDR, addr);
        tick();
        s.write(t, 1, P_DMA_START, 1);
        tick();
        for (int k = 0; k < 4000 && (load(P_STATUS) & 2); k++) tick();   // STATUS.dma_busy falls at the transfer's end
    }
    // The words the DMA sent since `from`, checked against a packet's data from word `first`: true if they are those.
    bool sent(size_t from, uint32_t tag, unsigned first, unsigned n, uint32_t addr, unsigned dst, unsigned counter) const {
        return out.size() == from + n && sent_part(from, tag, first, n, addr, dst, counter);
    }
    // The same for n words of them, more having been sent after.
    bool sent_part(size_t from, uint32_t tag, unsigned first, unsigned n, uint32_t addr, unsigned dst, unsigned counter) const {
        if (out.size() < from + n) return false;
        for (unsigned k = 0; k < n; k++) {
            const Landed& l = out[from + k];
            const uint64_t lo = uint64_t(tag) << 32 | (first + k);
            if (l.lo != lo || l.hi != ~lo || l.addr != addr + 16 * k || l.dst != dst || l.counter != counter || l.mode != lw::M_WRITE || l.size != lw::SIZE_128) return false;
        }
        return true;
    }
};


}  // namespace ingress_bench
