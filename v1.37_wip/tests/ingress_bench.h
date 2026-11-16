// ingress_bench.h: the master tile's slot with Ingress's host link, driven directly (v1.37; tests/test_ingress_link.cpp
// and tests/test_packet_vectors.cpp): SlotCore with Ingress/Egress and its host link as peripheral 1, a load, a store
// and a consuming load of EVENTS at cycle t as the slot's own calls, then the slot's cycle t (as test_tracer_slot's
// reference run drives a slot). The DMA's packets are kept as they leave it.
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

// II's numbers
constexpr uint32_t ST_FOREGROUND_READY = 1u << 3, ST_BACKGROUND_READY = 1u << 4;
constexpr uint32_t EVENT_DMA_DONE = 1u << 1, EVENT_FOREGROUND_SERVICE = 1u << 8, EVENT_BACKGROUND_SERVICE = 1u << 9;
constexpr uint32_t ERR_CRC = 1u << 1, ERR_SEQUENCE = 1u << 2, ERR_FORMAT = 1u << 3;
constexpr unsigned W_FOREGROUND_WORDS = 80, W_BACKGROUND_WORDS = 81;
constexpr uint32_t EVENT_IDLE = 1u << 5 | 1u << 7;            // 17.6: uart_tx and stream_out read 1 while it is idle

struct Landed { int64_t t; uint64_t lo, hi; uint32_t addr; unsigned dst, counter, mode, size; };

struct Bench {
    SlotCore s;
    std::vector<uint8_t> file;
    std::vector<Landed> out;
    int64_t t = 1;
    explicit Bench(std::vector<uint8_t> bytes, int64_t cycles_per_byte = 0) : file(std::move(bytes)) {
        s.tile = lw::CC1;
        PeriphSlotConfig c;
        c.host_link = true;
        s.install(1, PT_INGRESS, c);
        ie().link_cycles_per_byte = cycles_per_byte;
        ie().set_link(file.data(), file.size());
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
    // A transfer of n words of stream `queue` to `addr` at tile `dst`, counted on `counter`: to its dma_done.
    void dma(unsigned queue, unsigned n, unsigned dst, uint32_t addr, unsigned counter) {
        s.write(t, 1, P_DMA_HDR, lw::SIZE_128 | counter << HDR_COUNTER_LO | lw::M_WRITE << HDR_MODE_LO | dst << HDR_DST_LO | n << DMA_HDR_COUNT_LO | queue << DMA_HDR_QUEUE_LO);
        tick();
        s.write(t, 1, P_DMA_DEST_ADDR, addr);
        tick();
        s.write(t, 1, P_DMA_START, 1);
        tick();
        for (int k = 0; k < 200 && (load(P_STATUS) & 2); k++) tick();   // STATUS.dma_busy falls at the transfer's end
    }
    // The words the DMA sent since `from`, checked against a packet's data from word `first`: true if they are those.
    bool sent(size_t from, uint32_t tag, unsigned first, unsigned n, uint32_t addr, unsigned dst, unsigned counter) const {
        if (out.size() != from + n) return false;
        for (unsigned k = 0; k < n; k++) {
            const Landed& l = out[from + k];
            const uint64_t lo = uint64_t(tag) << 32 | (first + k);
            if (l.lo != lo || l.hi != ~lo || l.addr != addr + 16 * k || l.dst != dst || l.counter != counter || l.mode != lw::M_WRITE || l.size != lw::SIZE_128) return false;
        }
        return true;
    }
};


}  // namespace ingress_bench
