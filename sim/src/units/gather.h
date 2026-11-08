// gather.h: the GATHER engine (sim/ref/meshsim/src/GatherEngine.java; AUDIT-M2 3.6).
// Configuration part by W8 (G1); the behaviour by W12 (G4).
//
// The unit: its input from the director (`in`, the gather queue, mode 4), its configuration (k from DMA_CONFIG, base
// from DMA_RECORD_BASE_ADDRESS: Tile.setMasterConfig / setRecordBase), derived from the register block's `dma`
// RegFile (write_engine.h) at every step, never copied (ref reads its fields live: `k` and `base` each cycle; `wide`
// is fixed when a request is taken). ref's reset value of k is 1 (DMA_CONFIG 0 gives k = 1 too).
//
// The step at t (GatherEngine.runCycle):
//   1. a refusal of t - 1's put: put the same words again, nothing advances (stallCycles);
//   2. else, with no active request, take the queue's head: the 20-bit ordinal (payload [51:32]), the counter field,
//      wide = k even, left = k or k / 2;
//   3. one read: chunk (base + 2k * ordinal + (wide ? 4 : 2) * i) mod 2^21, 64 or 128 bits, mode 1, the request's
//      counter, to the tile itself, address 0.
// Wakes: a delivery to the queue, a refusal (the framework's edge bit), the queue's overflow (W10's overflow_steps),
// and `next()` after every put (to see the next cycle: the next read, a refusal, or the busy half falling).
//
// When the tile has no read side (`wired` false: the network-only and M1/G1 benches, until W11's read queue is in the
// tile), the G4 ports are not declared and a request is fatal, named, as before.
//
// G4'S PORTS (WORKER12), agreed with W11 (src/units/mem.h) and the integrator, 2026-11-08.
//
//   port   kind              from -> to                      words                                  latency
//   in     Fifo, width 3     director -> gather              mode-4 link words, Fifo(gather_depth);  1
//                                                            its overflow reaches the block as an
//                                                            event by W10's Fifo hook (GATHER_OVF, D29)
//   dma    RegFile           block -> gather                 k (DMA_CONFIG), the record base         (level, as the AG)
//   rd     Hold, width 2     gather -> srq client 0 (W11)    mem::req_a, mem::req_b                  (Hold)
//   busy   Reg, width 1      gather -> block                 its own half of GatherEngine.busy at    (Reg)
//                                                            the end of t: the queue non-empty after
//                                                            t's take, OR left > 0, OR a put on `rd`
//                                                            at t
//   ovf    Link(1), width 0  gather -> block                 the queue overflowed (judged at the     1
//                                                            put's t + 1, W10's hook; ERRORS [6]
//                                                            as an event, D29; D28 as the router)
//
// STATUS's gather_busy at the block's endCycle(t) is the OR of three inputs (owner, 2026-11-08), together ref's
//   !gatherIn.isEmpty(t) || left > 0 || !out.isEmpty(t) || srq.inFlightOf(GATHER) > 0:
//   `busy` above; the srq's Reg (W11): the gather Hold occupied at the end of t, OR inFlightOf(GATHER) > 0 (only the
//   consumer knows whether it took the word at t); the director's `gather_put`, a width-0 Link(1) put with a mode-4
//   packet at t (ref's isEmpty counts the queue's pending put, which reaches this unit only at t + 1).
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "sim.h"
#include "units/mem.h"
#include "units/net.h"
#include "units/write_engine.h"

namespace units {

struct Gather {
    sim::FifoIn in;            // width 3: mode-4 packets from the director, Fifo(gather_depth)
    sim::RegFileIn dma;
    // G4: declared when `wired`
    sim::HoldOut rd;
    sim::RegOut busy;
    sim::LinkOut ovf;

    // ---- configuration ----
    uint32_t me = 0;           // the tile (the reads' destination)
    bool wired = false;
    // ---- state (GatherEngine's, StateFp.gather: left, ord, i, ctr, wide) ----
    uint32_t left = 0, ord = 0, i = 0, ctr = 0;
    bool wide = false;
    sim::Word held[2] = {};    // the words on `rd` (the producer's copy, for the retry)
    bool busy_out = false;     // the value last put on `busy`
    uint64_t ovf_seen = 0;     // the queue's overflows already reported
    // ---- statistics (GatherEngine: requests, reads = packets, stallCycles) ----
    uint64_t requests = 0, reads = 0, stall_cycles = 0, overflows = 0;

    Gather() = default;
    Gather(sim::Ports& p, unsigned gather_depth, uint32_t tile = 0, bool wired_ = false) : me(tile), wired(wired_) {
        p.in(in, "in", 3, gather_depth);
        p.in(dma, "dma");
        if (wired) {
            p.overflow_steps(in);
            p.out(rd, "rd", mem::kReadReqWidth);
            p.out(busy, "busy", 1);
            p.out(ovf, "ovf", 0);
        }
    }

    sim::Wake step(sim::Ctx& c) {
        if (!wired) {
            if (c.ready(in)) FATAL("gather engine: a request arrived at cycle %llu: no read side in this tile", (unsigned long long)c.now());
            return sim::Wake::none();
        }
        const uint64_t o = c.overflows(in);                  // the queue's overflow: an event to the block (D29)
        if (o != ovf_seen) { overflows += o - ovf_seen; ovf_seen = o; c.put(ovf); }
        bool put = false;
        if (c.refused(rd)) {                                 // 1. the retry: nothing advances
            c.put(rd, held[0], held[1]);
            stall_cycles++; put = true;
        } else {
            if (left == 0 && c.ready(in)) {                  // 2. the next request
                const sim::Word* w = c.peek(in);
                const uint32_t kk = k(c.read(dma, DMA_R_CONFIG));
                wide = (kk & 1) == 0;
                ord = uint32_t(w[0] >> 32) & 0xFFFFF;
                ctr = lw::counter(w[1]);
                left = wide ? kk / 2 : kk; i = 0; requests++;
                c.take(in);
            }
            if (left > 0) {                                  // 3. one read
                const uint32_t kk = k(c.read(dma, DMA_R_CONFIG)), b = base(c.read(dma, DMA_R_RECORD_BASE));
                held[0] = mem::req_a(read_chunk(b, kk, ord, i, wide), wide ? lw::SIZE_128 : lw::SIZE_64, lw::M_SRC, ctr, me);
                held[1] = mem::req_b(0);
                c.put(rd, held[0], held[1]);
                reads++; i++; left--; put = true;
            }
        }
        const bool b = put || left > 0 || c.ready(in);       // the own half of busy at the end of t
        if (b != busy_out) { c.put(busy, b); busy_out = b; }
        return put || c.ready(in) ? c.next() : sim::Wake::none();
    }

    static Gather idle() { return Gather(); }             // the reset state, outside a machine (fingerprint writers)
    static uint32_t k(uint32_t dma_config) { return mc_k(dma_config); }
    static uint32_t base(uint32_t record_base) { return record_base & 0x1FFFFF; }
    static uint32_t read_chunk(uint32_t base, uint32_t k, uint32_t ord, uint32_t i, bool wide) {
        return (base + 2 * k * ord + (wide ? 4 : 2) * i) & 0x1FFFFF;
    }
    uint64_t digest() const { return sim::hash_fields(left, ord, i, ctr, wide, held[0], held[1], busy_out, ovf_seen); }
};

}  // namespace units
