// regblock.h: the register block's M1 subset as plain code: TileControl.endCycle(t) and its view, with RequestPort's
// count of the request FIFO (sim/tests/AUDIT-M1.md sections 2-4; sim/ref/meshsim/src/TileControl.java, RequestPort.java).
//
// `end_cycle(in)` is endCycle(t) in its order, from what reached the block during t; then `reg()`, `wake()`, `irq()` and
// `busy()` are the view at t + 1. The request FIFO is only its count: the bench frees entries (REQ-FREED), the core's
// REQUEST_SEND stores push them; its contents never leave the tile in M1.
#pragma once

#include <cstdint>

#include "units/regmap.h"
#include "units/rv32.h"

namespace units {

struct RegBlockState {
    // ---- configuration ----
    uint32_t params = 0, request_fifo_depth = 128;
    bool done_gate = true;
    // ---- state (TileControl.reset) ----
    bool done = false, landed = false, gate = false, ready_level = false, request_busy_prev = false;
    uint32_t events = 0, mask = EVENTS_MASK_RESET, required = 0, conditions = 0, status = 0;
    uint32_t req_n = 0, set_w[3] = {};
    // ---- statistics, and ref's violations (counted) ----
    uint64_t gos = 0, dones = 0, readies = 0, sends = 0, trace_stores = 0, violations = 0, sim_exit_at = 0;
    uint32_t sim_exit_value = 0;

    // What reached the block during t (all of it from t's cycle; at most one bus operation, the core's).
    struct In {
        bool go = false, landed = false, freed = false;
        uint32_t op = 0, arg = 0, v = 0;   // op: 0 none, BUS_STORE (register, value), BUS_CONSUME (-, bits), BUS_ERROR (cond, -)
    };

    UNITS_NOINLINE void end_cycle(uint64_t t, const In& in) {
        if (in.freed) { if (req_n == 0) violations++; else req_n--; }   // the bus freed the head at t, before the tile ran
        if (in.go) { done = landed = gate = false; gos++; events |= 1u << EV_GO; }                    // 1.
        if (in.op == BUS_STORE) apply(t, in.arg, in.v);                                             // 2.
        if (in.landed) { landed = true; events |= 1u << EV_COMMAND; }                                // 3.
        if (in.op == BUS_ERROR) conditions |= 1u << in.arg;                                          // 4. new errors
        bool rd = done && landed;                                                                    //    READY's edge
        if (rd && !ready_level) readies++;
        ready_level = rd;
        if (in.op == BUS_CONSUME) events &= ~(in.v & ~EVENTS_LIVE);                                  // 5. the consume
        bool rb = busy();                                                                            //    the edge events
        if (!rb && request_busy_prev) events |= 1u << EV_REQUEST_ROOM;
        request_busy_prev = rb;
        // The counter events [7:0]: no counter is ever armed in M1 (a counter store is fatal), so no edge.
        status = uint32_t(done) << ST_DONE | uint32_t(landed) << ST_LANDED | uint32_t(rb) << ST_REQUEST_BUSY;   // 6.
    }

    // The core's store of t (TileControl.apply, M1's registers).
    void apply(uint64_t t, uint32_t r, uint32_t v) {
        switch (r) {
        case PULSE_DONE:
            if (v & 1) {
                if (!(mask & 1u << EV_GO)) violations++;                 // done pulsed with GO clear in the mask
                done = true; dones++;
                if (done_gate) gate = true;
                events &= ~COUNTER_EVENTS;
            }
            break;
        case EVENTS_MASK: mask = v & EVENTS_VALID; break;
        case EVENTS_REQUIRED: required = v & EVENTS_VALID; break;
        case REQUEST_SET_W0: case REQUEST_SET_W1: case REQUEST_SET_W2: set_w[r - REQUEST_SET_W0] = v; break;
        case REQUEST_SEND:
            if (req_n >= request_fifo_depth) { violations++; break; }   // pushed into a full FIFO (a simulator bug in ref)
            req_n++; sends++; set_w[0] = set_w[1] = set_w[2] = 0;
            break;
        case SIM_EXIT: sim_exit_at = t; sim_exit_value = v; break;
        case TRACE: trace_stores++; break;
        default: break;
        }
    }

    uint64_t digest() const {
        const uint64_t w[] = {uint64_t(done) | uint64_t(landed) << 1 | uint64_t(gate) << 2 | uint64_t(ready_level) << 3
                                  | uint64_t(request_busy_prev) << 4,
                              events, mask, required, conditions, status, req_n, set_w[0], set_w[1], set_w[2], gos, dones,
                              readies, sends, trace_stores, violations, sim_exit_at, sim_exit_value, params,
                              request_fifo_depth, uint64_t(done_gate)};
        uint64_t h = 0;
        for (uint64_t v : w) h = digest_mix(h, v);
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
        default: return 0;
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
