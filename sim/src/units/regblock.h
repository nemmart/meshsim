// regblock.h: the register block as plain code: TileControl.endCycle(t) and its view, with RequestPort's count of the
// request FIFO (sim/tests/AUDIT-M1.md sections 2-4; sim/ref/meshsim/src/TileControl.java, RequestPort.java). M1's subset
// by W5; M2's registers by W8 (AUDIT-M2 3.3, 3.7): the send port's registers, the write counters (with the director's
// due decrements, judged without the same cycle's store: D27), their events and qualifiers, DMA_CONFIG and the record
// base, the read port's setup, the condition counts, STATUS's M2 bits, the READY edge and the error line.
//
// `end_cycle(in)` is endCycle(t) in its order, from what reached the block during t; then `reg()`, `wake()`, `irq()` and
// `busy()` are the view at t + 1. The request FIFO is only its count: the bench frees entries (REQ-FREED), the core's
// REQUEST_SEND stores push them; its contents never leave the tile in M1.
#pragma once

#include <cstdint>

#include "units/regmap.h"
#include "base/fatal.h"
#include "units/rv32.h"

namespace units {

struct RegBlockState {
    // ---- configuration ----
    uint32_t params = 0, request_fifo_depth = 128;
    bool done_gate = true;
    // ---- state (TileControl.reset) ----
    bool done = false, landed = false, gate = false, ready_level = false, request_busy_prev = false;
    uint32_t events = 0, mask = EVENTS_MASK_RESET, required = 0, conditions = 0, status = 0;
    uint32_t req_n = 0, set_w[3] = {};          // the request port's FIFO count and REQUEST_SET_W0-W2
    // ---- M2 (W8): TileControl's registers and qualifiers ----
    uint32_t router_hdr = 0, router_addr = 0, router_w[3] = {}, dma_config = 0, record_base = 0, rd_setup = 0;
    uint64_t rp_errors = 0;            // W11: read-port stores dropped (ReadPort.errors: ERRORS [7], READ_PORT)
    bool q_all = false, q_one[8] = {}, cond_all_prev = true, cond_one_prev[8] = {true, true, true, true, true, true, true, true};
    bool rd_done_prev = false;
    uint32_t remaining[8] = {};                 // the write counters (20 bits), 1-7: the director's in ref
    uint64_t ev_prev[15] = {};                  // the condition counts as endCycle last saw them (Conditions.N)
    bool ready_pulse = false;                   // READY rose at this endCycle (Chip collects readyPulseAt == t)
    bool error_line = false;                    // TILE_CONDITIONS != 0 after this endCycle
    uint64_t unexpected_total = 0, done_early = 0, counter_wraps = 0, router_sends = 0, bad_total = 0;
    enum : unsigned { COND_ROUTER_OVF = 0, COND_BAD = 1, COND_WE_IN_OVF = 4, COND_TABLE_OVF = 5 };
    // ---- statistics, and ref's violations (counted) ----
    uint64_t gos = 0, dones = 0, readies = 0, sends = 0, trace_stores = 0, violations = 0, sim_exit_at = 0;
    uint32_t sim_exit_value = 0;

    // What reached the block during t (all of it from t's cycle; at most one bus operation, the core's).
    struct In {
        bool go = false, landed = false, freed = false;
        uint32_t op = 0, arg = 0, v = 0;   // op: 0 none, BUS_STORE (register, value), BUS_CONSUME (-, bits), BUS_ERROR (cond, -)
        // M2
        uint32_t kline = 0;                // a decrement of this counter (1-7) fell due at t (the director's K-stage line)
        bool send_busy = false;            // the send hold occupied at the end of t (W9's Reg), or the core's put at t
        bool bad = false;                  // the director dropped a bad packet at t (an event: ERRORS [1] at endCycle(t))
        bool router_ovf_ev = false;        // the router counted an overflow at t (an event, D29: ERRORS [0] at endCycle(t))
        uint64_t router_ovf = 0, we_in_ovf = 0, gather_ovf = 0;   // FIFO overflow counts through t (D28: Counts)
        // W11 (G3), D29: overflows judged at t, as events: ERRORS [4] (the write engine's input), [5] (the table
        // store's and HIST's request FIFOs) at endCycle(t)
        bool we_in_ovf_ev = false, table_ovf_ev = false;
    };

    UNITS_NOINLINE void end_cycle(uint64_t t, const In& in) {
        if (in.kline) {                                                  // the director's decrement due at t (D27)
            if (remaining[in.kline] == 0) unexpected_total++;           // stays 0: ERRORS [2] (4.12)
            else remaining[in.kline]--;
        }
        if (in.freed) { if (req_n == 0) violations++; else req_n--; }   // the bus freed the head at t, before the tile ran
        if (in.go) { done = landed = gate = false; gos++; events |= 1u << EV_GO; }                    // 1.
        uint32_t clears = 0;
        if (in.op == BUS_STORE) clears = apply(t, in.arg, in.v);                                     // 2.
        if (in.landed) { landed = true; events |= 1u << EV_COMMAND; }                                // 3.
        if (in.op == BUS_ERROR) conditions |= 1u << in.arg;                                          // 4. new errors
        if (in.bad) { bad_total++; conditions |= 1u << COND_BAD; }
        if (in.router_ovf_ev) conditions |= 1u << COND_ROUTER_OVF;
        if (in.we_in_ovf_ev) conditions |= 1u << COND_WE_IN_OVF;
        if (in.table_ovf_ev) conditions |= 1u << COND_TABLE_OVF;
        const uint64_t counts[] = {in.router_ovf, bad_total, unexpected_total, 0, in.we_in_ovf, 0, in.gather_ovf, rp_errors, done_early};
        for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
            if (counts[i] > ev_prev[i]) conditions |= 1u << i;
            ev_prev[i] = counts[i];
        }
        bool rd = done && landed;                                                                    //    READY's edge
        ready_pulse = rd && !ready_level;
        if (ready_pulse) readies++;
        ready_level = rd;
        if (in.op == BUS_CONSUME) events &= ~(in.v & ~EVENTS_LIVE);                                  // 5. the consume
        // [10]: the AG's done (G4); rd_done_prev stays false until it exists
        bool rb = busy();                                                                            //    the edge events
        if (!rb && request_busy_prev) events |= 1u << EV_REQUEST_ROOM;
        request_busy_prev = rb;
        counter_events(clears);
        error_line = conditions != 0;
        status = uint32_t(in.send_busy) << ST_SEND_BUSY | uint32_t(done) << ST_DONE | uint32_t(landed) << ST_LANDED
                 | uint32_t(rb) << ST_REQUEST_BUSY;                                                  // 6.
    }

    bool counters_zero() const { for (unsigned c = 1; c <= 7; c++) if (remaining[c]) return false; return true; }
    // TileControl.counterEvents: [6:0] and [7] on their done condition's rising edge, qualified, not while the done
    // gate is closed; a clear in the same cycle wins.
    void counter_events(uint32_t clears) {
        bool any = false, gated = gate && done_gate;
        for (unsigned c = 1; c <= 7; c++) {
            if (remaining[c]) { q_one[c] = true; any = true; }
            bool cond = remaining[c] == 0;
            unsigned bit = c - 1;
            if (cond && !cond_one_prev[c] && q_one[c]) {
                q_one[c] = false;
                if (!gated && !(clears >> bit & 1)) events |= 1u << bit;
            }
            cond_one_prev[c] = cond;
        }
        if (any) q_all = true;
        bool cond = counters_zero();
        if (cond && !cond_all_prev && q_all) {
            q_all = false;
            if (!gated && !(clears >> EV_COUNTERS_DONE & 1)) events |= 1u << EV_COUNTERS_DONE;
        }
        cond_all_prev = cond;
    }

    // The core's store of t (TileControl.apply); returns the event bits it clears this cycle (beating a same-cycle set).
    uint32_t apply(uint64_t t, uint32_t r, uint32_t v) {
        uint32_t clears = 0;
        switch (r) {
        case PULSE_DONE:
            if (v & 1) {
                if (!counters_zero()) done_early++;                       // done early (4.13): ERRORS [8]
                if (!(mask & 1u << EV_GO)) violations++;                 // done pulsed with GO clear in the mask
                done = true; dones++;
                if (done_gate) gate = true;
                clears = COUNTER_EVENTS;
                events &= ~clears; q_all = false;
                for (bool& q : q_one) q = false;
            }
            break;
        case PULSE_DMA:
            if (v & 1) FATAL("register block: PULSE_DMA at cycle %llu: not modelled in M2 yet (G4)", (unsigned long long)t);
            break;
        case DMA_CONFIG:
            if ((v >> 18 & 7) > 4 || (v >> 21 & 7) > 4)
                FATAL("register block: DMA_CONFIG 0x%x at cycle %llu: a reserved digit selector (5-7: UB, spec 4.15)", v, (unsigned long long)t);
            dma_config = v;
            break;
        case DMA_RECORD_BASE_ADDRESS: record_base = v & 0x1FFFFF; break;
        case ROUTER_HDR: router_hdr = v & ~(3u << 17) & ~0x1FFFEu; break;   // size and spare: stores ignored, read 0
        case ROUTER_ADDR: router_addr = v; break;
        case ROUTER_SET_W0: case ROUTER_SET_W1: case ROUTER_SET_W2: router_w[r - ROUTER_SET_W0] = v; break;
        case ROUTER_SEND_32: case ROUTER_SEND_64: case ROUTER_SEND_128:  // captured at t (8.4) by the core
            router_sends++;
            if ((router_hdr >> 22 & 7) == 0 && !(router_hdr & 1)) router_addr += 4u << (r - ROUTER_SEND_32);
            router_w[0] = router_w[1] = router_w[2] = 0;
            break;
        case SRAM_RD_DEST_ADDR: rd_setup = v & 0x3FFFFF; break;
        case SRAM_RD_ADDR_QUEUE: {                                       // W11: the destination advances, queued or not
            const unsigned size = rd_setup >> 17 & 3, n = size == 3 ? 0 : 4u << size;
            rd_setup = (rd_setup & ~0x1FFFFu) | ((rd_setup + n) & 0x1FFFFu);
            if (v >> 31) rp_errors++;                                    // dropped: no free entry (the core's bit 31)
            break;
        }
        case EVENTS_MASK: mask = v & EVENTS_VALID; break;
        case EVENTS_REQUIRED: required = v & EVENTS_VALID; break;
        case REQUEST_SET_W0: case REQUEST_SET_W1: case REQUEST_SET_W2: set_w[r - REQUEST_SET_W0] = v; break;
        case REQUEST_SEND:
            if (req_n >= request_fifo_depth) { violations++; break; }   // pushed into a full FIFO (a simulator bug in ref)
            req_n++; sends++; set_w[0] = set_w[1] = set_w[2] = 0;
            break;
        case SIM_EXIT: sim_exit_at = t; sim_exit_value = v; break;
        case TRACE: trace_stores++; break;
        default:
            if (r >= WRITE_COUNTER_1 && r <= WRITE_COUNTER_7) {           // 4.12: a write adds; N > 0 arms; 0 does nothing
                unsigned k = r - WRITE_COUNTER_1 + 1;
                uint32_t n = v & 0xFFFFF;
                if (n) {
                    uint64_t sum = uint64_t(remaining[k]) + n;
                    if (sum > 0xFFFFF) { counter_wraps++; violations++; }
                    remaining[k] = uint32_t(sum & 0xFFFFF);
                    clears = 1u << (k - 1) | 1u << EV_COUNTERS_DONE;
                    events &= ~clears; q_all = true; q_one[k] = true;
                }
            }
            break;
        }
        return clears;
    }

    uint64_t digest() const {
        const uint64_t w[] = {uint64_t(done) | uint64_t(landed) << 1 | uint64_t(gate) << 2 | uint64_t(ready_level) << 3
                                  | uint64_t(request_busy_prev) << 4,
                              events, mask, required, conditions, status, req_n, set_w[0], set_w[1], set_w[2], gos, dones,
                              readies, sends, trace_stores, violations, sim_exit_at, sim_exit_value, params,
                              request_fifo_depth, uint64_t(done_gate)};
        uint64_t h = 0;
        for (uint64_t v : w) h = digest_mix(h, v);
        const uint64_t m2[] = {router_hdr, router_addr, router_w[0], router_w[1], router_w[2], dma_config, record_base,
                               rd_setup, uint64_t(q_all) | uint64_t(cond_all_prev) << 1 | uint64_t(rd_done_prev) << 2
                                   | uint64_t(ready_pulse) << 3 | uint64_t(error_line) << 4,
                               unexpected_total, done_early, counter_wraps, router_sends, bad_total, rp_errors};
        for (uint64_t v : m2) h = digest_mix(h, v);
        for (unsigned c = 0; c < 8; c++) h = digest_mix(h, uint64_t(remaining[c]) << 2 | uint64_t(q_one[c]) << 1 | cond_one_prev[c]);
        for (uint64_t v : ev_prev) h = digest_mix(h, v);
        return h;
    }

    // ---- the view at t + 1 ----
    bool busy() const { return req_n >= request_fifo_depth; }
    uint32_t reg(unsigned r) const {
        switch (r) {
        case PARAMS: return params;
        case STATUS: return status;
        case ERRORS: return conditions;                                   // the peripherals' live bits: 0 in M1
        case EVENTS: return events & EVENTS_VALID;                        // the live bits: 0 in M1
        case EVENTS_MASK: return mask;
        case EVENTS_REQUIRED: return required;
        case ROUTER_HDR: return router_hdr;
        case ROUTER_ADDR: return router_addr;
        case ROUTER_SET_W0: case ROUTER_SET_W1: case ROUTER_SET_W2: return router_w[r - ROUTER_SET_W0];   // for a send's packet
        case DMA_CONFIG: return dma_config;
        case DMA_RECORD_BASE_ADDRESS: return record_base;
        case SRAM_RD_DEST_ADDR: return rd_setup;
        default:
            if (r >= WRITE_COUNTER_1 && r <= WRITE_COUNTER_7) return remaining[r - WRITE_COUNTER_1 + 1];   // at the start of t
            return 0;
        }
    }
    bool wake() const {                                                   // TileControl.wakeTerm
        uint32_t e = events & EVENTS_VALID;
        if (gate) return e >> EV_GO & 1;
        return (e & mask & ~required) != 0 || (required != 0 && (e & required) == required);
    }
    bool irq() const { return conditions != 0; }
};

}  // namespace units
