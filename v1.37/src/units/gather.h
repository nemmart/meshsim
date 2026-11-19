// gather.h: the gather unit (machine-spec 13; the RTL's rtl/blocks/gather_unit: gather_unit.veryl's header).
//
// The unit: its input from the director (`in`, the gather queue of GATHER_QUEUE_DEPTH = TOKEN_COUNT requests), and its
// configuration, GATHER_CONFIG, from the tile control unit's `dma` RegFile (write_engine.h). A request's value, its
// start, its k and so its reads, is computed from GATHER_CONFIG as it stands when the request leaves the queue; no
// later store moves a read of that request (a store while the unit is busy is UB, 13: this is the RTL's choice, its
// README's decisions).
//
// THE HARDWARE'S CYCLES. A request the director accepts in cycle a enters the queue at the edge that ends a: at the
// head from a + 1. The read the unit offers is a register, held until the read arbiter takes it. The register takes
// its next read at the edge that ends a cycle in which it is empty or its read is taken: the value's next read, or
// when the value has none, the first read of the request at the queue's head, WHICH LEAVES THE QUEUE IN THAT CYCLE;
// with neither, it empties. So a request that finds the unit idle has its first read offered in a + 2, and a request
// behind a read the arbiter does not take stays in the queue. The queue is full when it holds its depth at the start
// of a cycle, the count before the cycle's pop: a request then is lost, and ERRORS.gather_overflow's pulse is in that
// cycle.
//
// THE MODEL'S STEP at t is the hardware's cycle t. `rd`, a Hold, is the offer register: a read put in t - 1 is
// offered in t. Whether the arbiter takes it in t is `taken` (a Wire: the reads it has taken, this cycle's included),
// so the step knows, as the register does, whether it loads at this cycle's end:
//   1. the register full and its read not taken in t: nothing (the Hold keeps the word; stall_cycles);
//   2. else, with no value in progress, the queue's head leaves: the 20-bit ordinal (payload [51:32]), the counter
//      field, wide = k even, left = k or k / 2;
//   3. one read: chunk (base + 2k * ordinal + (wide ? 4 : 2) * i) mod 2^21, 64 or 128 bits, mode 1, the request's
//      counter, to the tile itself, address 0: put on `rd`, offered in t + 1.
// `full` is the queue's count at the start of t at its depth: the traffic director, which accepts a GATHER in t, reads
// it then, loses the request and pulses the overflow (director.h), so the queue's Fifo never overflows.
// Wakes: a delivery to the queue, `taken` moving, and `next()` while the register is full or the queue holds a request.
//
// When the tile has no read side (`wired` false: the network-only benches), the ports below are not declared and a
// request is fatal, named, as before.
//
//   port   kind              from -> to                      words                                  latency
//   in     Fifo, width 3     director -> gather              mode-4 link words, Fifo(gather_depth)   1
//   dma    RegFile           block -> gather                 GATHER_CONFIG                           (level, as the AG)
//   rd     Hold, width 2     gather -> the arbiter's client 0   mem::req_a, mem::req_b               (Hold)
//   taken  Wire              the arbiter -> gather           its takes of the gather's reads so far  (this cycle's)
//   full   Wire              gather -> director              the queue holds its depth at the start  (this cycle's)
//                                                            of the cycle
//   busy   Reg, width 1      gather -> block                 its own half of STATUS.gather_busy at   (Reg)
//                                                            the end of t: the offer register full
//                                                            in t + 1
//
// STATUS's gather_busy at the block's endCycle(t) is the OR of three inputs (owner, 2026-11-08), together ref's
//   !gatherIn.isEmpty(t) || left > 0 || !out.isEmpty(t) || srq.inFlightOf(GATHER) > 0:
//   `busy` above; the srq's Reg: the gather Hold occupied at the end of t, OR inFlightOf(GATHER) > 0 (only the
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
    // declared when `wired`
    sim::HoldOut rd;
    sim::WireIn taken;
    sim::WireOut full;
    sim::RegOut busy;

    // ---- configuration ----
    uint32_t me = 0;           // the tile (the reads' destination)
    uint32_t depth = 0;        // GATHER_QUEUE_DEPTH
    bool wired = false;
    // ---- state (GatherEngine's, StateFp.gather: left, ord, i, ctr, wide) ----
    uint32_t left = 0, ord = 0, i = 0, ctr = 0;
    bool wide = false;
    uint32_t req_k = 1, req_base = 0;   // GATHER_CONFIG as it stood when the request left the queue: its value's k and start
    sim::Word held[2] = {};    // the words on `rd`
    bool offered = false;      // the offer register is full: a read put on `rd` and not yet taken
    sim::Word taken_seen = 0;  // `taken` as of the step before
    bool busy_out = false;     // the value last put on `busy`
    bool full_out = false;     // the value last driven on `full`
    // ---- statistics (GatherEngine: requests, reads = packets, stallCycles) ----
    uint64_t requests = 0, reads = 0, stall_cycles = 0;
    // (harness-only; P2): ref's Drain in the GATHER engine's place (Config.realResponder off, the coreless test
    // machine): one entry taken a cycle, counted (Drain.taken), nothing read. Set by the harness only.
    bool drain = false;
    uint64_t drained = 0;
    uint64_t (*log)[4] = nullptr;      // the drain's log (harness-only), when the harness gives one
    uint32_t log_cap = 0, log_n = 0;

    Gather() = default;
    Gather(sim::Ports& p, unsigned gather_depth, uint32_t tile = 0, bool wired_ = false) : me(tile), depth(gather_depth), wired(wired_) {
        p.in(in, "in", 3, gather_depth);
        p.in(dma, "dma");
        if (wired) {
            p.out(rd, "rd", mem::kReadReqWidth);
            p.in(taken, "taken");
            p.out(full, "full");
            p.out(busy, "busy", 1);
        }
    }

    sim::Wake step(sim::Ctx& c) {
        if (!wired) {
            if (c.ready(in)) FATAL("gather engine: a request arrived at cycle %llu: no read side in this tile", (unsigned long long)c.now());
            return sim::Wake::none();
        }
        // the queue's count at the start of this cycle (its pop of this cycle not yet made) at its depth
        const bool f = c.size_at_start(in) >= depth;
        if (f != full_out) { full_out = f; c.drive(full, f); }
        if (drain) {                                         // (harness-only): ref's Drain
            if (c.ready(in)) {
                if (log) {                                     // ref's gatherStub.record()/log: {t, payload, header}
                    if (log_n == log_cap) FATAL("gather drain: the harness's log is full (%u entries)", log_cap);
                    const sim::Word* w = c.peek(in);
                    uint64_t* e = log[log_n++];
                    e[0] = c.now(); e[1] = w[0]; e[2] = w[1]; e[3] = w[2];
                }
                c.take(in); drained++;
            }
            return c.ready(in) ? c.next() : sim::Wake::none();
        }
        if (c.refused(rd)) FATAL("gather unit (tile %u), cycle %llu: a read put while the offer register was full", me, (unsigned long long)c.now());
        const sim::Word tk = c.read(taken);
        const bool taken_now = offered && tk != taken_seen;  // the arbiter takes the offered read in this cycle
        taken_seen = tk;
        if (offered && !taken_now) stall_cycles++;           // 1. held: the register keeps its read, the queue its head
        else {
            offered = false;
            if (left == 0 && c.ready(in)) {                  // 2. the next request leaves the queue
                const sim::Word* w = c.peek(in);
                const uint32_t kk = k(c.read(dma, DMA_R_GATHER));
                wide = (kk & 1) == 0;
                req_k = kk; req_base = base(c.read(dma, DMA_R_GATHER));   // no later store moves this request's reads
                ord = uint32_t(w[0] >> 32) & 0xFFFFF;
                ctr = lw::counter(w[1]);
                left = wide ? kk / 2 : kk; i = 0; requests++;
                c.take(in);
            }
            if (left > 0) {                                  // 3. one read, offered in t + 1
                held[0] = mem::req_a(read_chunk(req_base, req_k, ord, i, wide), wide ? lw::SIZE_128 : lw::SIZE_64, lw::M_SRC, ctr, me);
                held[1] = mem::req_b(0);
                c.put(rd, held[0], held[1]);
                reads++; i++; left--; offered = true;
            }
        }
        if (offered != busy_out) { c.put(busy, offered); busy_out = offered; }   // its half of busy: a read offered in t + 1
        return offered || c.ready(in) || full_out ? c.next() : sim::Wake::none();
    }

    static Gather idle() { return Gather(); }             // the reset state, outside a machine (fingerprint writers)
    static uint32_t k(uint32_t gather_config) { return gc_k(gather_config); }
    static uint32_t base(uint32_t gather_config) { return gc_record_base(gather_config); }
    static uint32_t read_chunk(uint32_t base, uint32_t k, uint32_t ord, uint32_t i, bool wide) {
        return (base + 2 * k * ord + (wide ? 4 : 2) * i) & 0x1FFFFF;
    }
    uint64_t digest() const { return sim::hash_fields(left, ord, i, ctr, wide, req_k, req_base, held[0], held[1], busy_out, offered, taken_seen, full_out); }
};

}  // namespace units
