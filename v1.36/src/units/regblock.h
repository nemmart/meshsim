// regblock.h: the tile control unit as plain code (machine-spec 7; the RTL's rtl/blocks/tile_control_unit, whose next-state
// equations these are), with the registers of the tile window that the units behind it hold and that the core stores
// through register_bus: block 1's DMA_CONFIG and DMA_START (8.1), block 4's count of the request queue (11.1), block 5's
// RECEIVE_CONFIG and COUNTER_1-7 (12.1, 12.2, with the receive unit's counter events) and block 6's GATHER_CONFIG (13).
// The write unit's and the read unit's registers, and REQUEST_OUT_W0-W2, are the core's side's (tile_ctl.h, TileCore):
// nothing but the core's own stores and loads reaches them.
//
// `end_cycle(p, in)` is the edge that ends cycle p, from what is presented to the block in p: a window store or a
// consuming EVENTS load that register_bus presents in p (issued by the core in p - 1: 6.4), the units' pulses and levels
// of p. Then `reg()`, `wake()`, `nmi()` and the rest are the state at the end of p: what a load presented in p + 1
// returns, and the core's inputs in p + 1, each from its flip-flop.
//
// The events (7.2), the RTL's order of priority at the end of a cycle, for each counted event:
//   - 0 on a clear that starts a new wait: DMA_START's for dma_done, ARRIVE's for all_counters_done and the counter
//     events;
//   - 1 on a set, whatever a consuming load returned;
//   - 0 on a clear by arming (a counter's events), which meets no set but its own cycle's drain;
//   - 0 if an EVENTS load presented in the cycle consumes it (it returned it set, and EVENTS_MASK selects it);
//   - otherwise as it was.
// A counter event is armed by an arm of its counter (any counter's, for all_counters_done), and disarmed by its drain,
// which in the same cycle sets the event if it was armed or is armed then. ARRIVE neither arms nor disarms.
#pragma once

#include <cstdint>

#include "check.h"
#include "units/regmap.h"
#include "base/fatal.h"
#include "units/rv32.h"

namespace units {

struct RegBlockState {
    // ---- configuration ----
    Straps straps;                              // PARAMS (7.1)
    uint32_t request_queue_depth = 4;           // REQUEST_QUEUE_DEPTH (19)
    // ---- state: the tile control unit's flip-flops ----
    bool arrived = false;                       // STATUS.arrived
    uint32_t events = 0;                        // the counted events (all of depth 1): EVENTS without the live bits
    uint32_t mask = EVENTS_MASK_RESET;          // EVENTS_MASK
    bool all_armed = false;                     // all_counters_done's arming
    uint32_t armed = 0;                         // counter_n_done's arming, at bit n - 1
    uint32_t errors = 0;                        // ERRORS' sticky bits [9:0]
    bool wake_ff = false, error_raised = false; // meip; nmi and the error line
    bool was_ready = false, arrive_pulse = false;   // the tile's readiness a cycle ago; BARRIER_ARRIVE in the next cycle
    bool canon = false;                         // core_status.canonical_wait's level (the core's, a cycle late)
    // ---- state: the units' registers the block holds ----
    uint32_t remaining[8] = {};                 // COUNTER_1-7 (20 bits), at 1-7
    uint32_t dma_config = 0, receive_config = 0, gather_config = 0;
    uint32_t req_n = 0;                         // the request queue's count (11.1)
    uint32_t status = 0;                        // STATUS as a load presented in the next cycle reads it
    // the peripheral slot's live bits (its window RegFile's Win::R_LIVE, set by TileCtl before end_cycle): [2:0]
    // EVENTS.peripheral_1-3 (each peripheral's EVENTS & EVENTS_MASK != 0), [5:3] ERRORS.peripheral_1-3 (ERRORS != 0)
    uint32_t slot_live = 0;
    bool request_pending = false;               // EVENTS.request_pending's level (a tile with a receive queue)
    uint32_t live_events() const {
        return ((slot_live & straps.peripherals & 7) << EV_PERIPH_1) | (request_pending ? 1u << EV_REQUEST_PENDING : 0u);
    }
    uint32_t live_errors() const { return (slot_live >> 3 & straps.peripherals & 7) << ERR_PERIPH_1; }
    // ---- the DMA unit's status as the block keeps it (8.2) ----
    bool dma_done_prev = false;                 // the address generator's done at the last end_cycle (its edge sets dma_done)
    uint32_t dma_starts = 0;                    // DMA_START's starts so far (the AG's `start` Wire: a change = a start)
    bool dma_pulse = false;                     // a start applied at this end_cycle (the AG's until its step)
    // ---- statistics, and the facts of the cycle (not state) ----
    uint64_t releases = 0, arrivals = 0, sends = 0, underflows = 0, early_arrivals = 0, counter_wraps = 0, violations = 0;
    uint64_t bad_total = 0;
    int64_t counter_write_at[8] = {-1, -1, -1, -1, -1, -1, -1, -1};   // each counter's last store
    int ck_tile = -1;                           // the checker's facts (src/check.h) under this tile; -1 none

    // What is presented to the block in cycle p.
    struct In {
        // register_bus's (one access a cycle): a store to a word of the tile window (regmap.h's Reg), or a load of EVENTS
        bool store = false; uint32_t word = 0, value = 0;
        bool events_load = false;
        // the barrier's net (18)
        bool release = false;
        // the units' pulses: the request queue's head freed; the receive unit's decrement (a counter, 1-7, from the delay
        // line); the errors' (7.4: bit numbers ERR_*), the core's status pulses among them
        bool freed = false;
        uint32_t decrement = 0;
        uint32_t error_pulses = 0;
        // the units' levels of the cycle
        bool gather_busy = false;
        bool ag_running = false, ag_done = false;
    };
    bool dma_busy(const In& in) const { return in.ag_running || dma_pulse; }
    bool dma_done_level(const In& in) const { return in.ag_done && !dma_pulse; }

    UNITS_NOINLINE void end_cycle(uint64_t t, const In& in) {
        const bool barrier = straps.barrier;
        const bool arrive = in.store && in.word == ARRIVE && (in.value & 1);
        const bool mask_store = in.store && in.word == EVENTS_MASK;
        const bool release_pulse = in.release && barrier;
        if (in.freed) { if (req_n == 0) violations++; else req_n--; }   // the queue's head left in p

        // ---- the counters (12.2): next = count + N - decrement; their events' pulses ----
        uint32_t arm = 0, drained = 0;
        bool nonzero = false, underflow = false, all_left_zero = true;
        for (unsigned c = 1; c <= 7; c++) {
            const bool st = in.store && in.word == unsigned(COUNTER_1) + c - 1;
            const uint32_t n = st ? in.value & COUNTER_MASK : 0;
            const bool dec = in.decrement == c;
            const uint32_t count = remaining[c];
            if (count) nonzero = true;
            if (st) counter_write_at[c] = int64_t(t);
            if (n) arm |= 1u << (c - 1);
            const bool sum_zero = count == 0 && n == 0;
            if (dec && sum_zero) { underflow = true; underflows++; }
            const bool taken = dec && !sum_zero;
            const uint64_t next = uint64_t(count) + n - (taken ? 1 : 0);
            if (next > COUNTER_MASK) {                                 // a sum past 2^20 - 1: UB (12.2); it wraps
                counter_wraps++; violations++;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_COUNTER_WRAP, c);
            }
            remaining[c] = uint32_t(next) & COUNTER_MASK;
            if (taken && remaining[c] == 0) drained |= 1u << (c - 1);
            if (remaining[c] != 0) all_left_zero = false;
        }
        const bool all_drained = drained != 0 && all_left_zero;
        const bool any_arm = arm != 0;

        // ---- the other stores ----
        bool dma_clear = false;
        dma_pulse = false;
        if (in.store) {
            switch (in.word) {
            case DMA_CONFIG: dma_config = in.value & DMA_CONFIG_BITS; break;
            case DMA_START:                                            // 8.2: clears dma_done, sets dma_busy
                if (in.ag_running) FATAL("tile control: DMA_START at cycle %llu while a transfer is in progress: UB (8.2)", (unsigned long long)t);
                dma_clear = true; dma_starts++; dma_pulse = true;
                break;
            case RECEIVE_CONFIG: receive_config = in.value & RECEIVE_CONFIG_BITS; break;   // (a reserved digit, 5-7: the write engine stops at its use)
            case GATHER_CONFIG: gather_config = in.value & GATHER_CONFIG_BITS; break;
            case REQUEST_SEND:
                if (req_n >= request_queue_depth) violations++;        // the core stalls a send into a full queue
                else { req_n++; sends++; }
                break;
            default: break;                                            // ARRIVE, EVENTS_MASK, the counters: above and below
            }
        }

        // ---- the events' next state (7.2) ----
        const uint32_t consumed = in.events_load ? events & mask : 0;
        const bool dma_set = dma_done_level(in) && !dma_done_prev;     // the end of the transfer (8.2)
        dma_done_prev = dma_done_level(in);
        const bool all_set = all_drained && (all_armed || any_arm);
        const uint32_t counter_set = drained & (armed | arm);
        auto bit = [](bool b, unsigned i) { return b ? 1u << i : 0u; };
        auto kept = [&](unsigned i) { return (events >> i & 1) && !(consumed >> i & 1); };
        uint32_t next = 0;
        next |= bit(release_pulse || kept(EV_RELEASE), EV_RELEASE);
        next |= bit(!dma_clear && (dma_set || kept(EV_DMA_DONE)), EV_DMA_DONE);
        next |= bit(!arrive && (all_set || (!any_arm && kept(EV_ALL_COUNTERS_DONE))), EV_ALL_COUNTERS_DONE);
        if (!arrive) next |= (counter_set | (~arm & (events >> EV_COUNTER_1_DONE) & ~(consumed >> EV_COUNTER_1_DONE) & 0x7F)) << EV_COUNTER_1_DONE;
        events = next;
        all_armed = !all_drained && (any_arm || all_armed);
        armed = ~drained & (arm | armed) & 0x7F;
        if (mask_store) mask = in.value & EVENTS_VALID;

        // ---- the barrier (7.3) ----
        const bool gate_next = arrive || arrived;                      // the arrival gate in the next cycle
        const bool ready = arrived && canon;
        arrive_pulse = barrier && ready && !was_ready;
        was_ready = ready;
        if (arrive) arrivals++;
        if (release_pulse) releases++;
        const bool early = arrive && nonzero;                          // 7.3: a counter non-zero at the start of the cycle
        if (early) {
            early_arrivals++;
            if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_DONE_EARLY);
        }
        arrived = !release_pulse && (arrive || arrived);

        // ---- the errors (7.4) ----
        uint32_t pulses = in.error_pulses & ((1u << ERR_PERIPH_1) - 1);
        if (underflow) pulses |= 1u << ERR_COUNTER_UNDERFLOW;
        if (early) pulses |= 1u << ERR_EARLY_ARRIVAL;
        if (pulses >> ERR_BAD_PACKET & 1) bad_total++;
        errors |= pulses;

        // ---- the core's inputs and the error line, from flip-flops: the state at the end of this cycle, with this
        // cycle's live levels ----
        wake_ff = gate_next ? (events >> EV_RELEASE & 1) != 0 : ((events | live_events()) & mask) != 0;
        error_raised = errors != 0 || live_errors() != 0;
        status = uint32_t(arrived) << ST_ARRIVED | uint32_t(dma_busy(in)) << ST_DMA_BUSY
                 | uint32_t(in.gather_busy) << ST_GATHER_BUSY | uint32_t(request_full()) << ST_REQUEST_FULL;
    }

    uint64_t digest() const {
        const uint64_t w[] = {uint64_t(arrived) | uint64_t(all_armed) << 1 | uint64_t(wake_ff) << 2 | uint64_t(error_raised) << 3
                                  | uint64_t(was_ready) << 4 | uint64_t(arrive_pulse) << 5 | uint64_t(canon) << 6
                                  | uint64_t(dma_done_prev) << 7 | uint64_t(dma_pulse) << 8 | uint64_t(request_pending) << 9,
                              events, mask, armed, errors, status, req_n, dma_config, receive_config, gather_config, dma_starts,
                              slot_live, params_value(straps), request_queue_depth, releases, arrivals, sends, underflows,
                              early_arrivals, counter_wraps, violations, bad_total};
        uint64_t h = 0;
        for (uint64_t v : w) h = digest_mix(h, v);
        for (unsigned c = 0; c < 8; c++) h = digest_mix(h, remaining[c]);
        return h;
    }

    // ---- the state at the end of the cycle: what a load presented in the next reads ----
    bool request_full() const { return req_n >= request_queue_depth; }
    bool counters_zero() const { for (unsigned c = 1; c <= 7; c++) if (remaining[c]) return false; return true; }
    uint32_t events_value() const { return (events | live_events()) & EVENTS_VALID; }
    uint32_t errors_value() const { return (errors | live_errors()) & ERRORS_VALID; }
    uint32_t reg(unsigned r) const {
        switch (r) {
        case PARAMS: return params_value(straps);
        case STATUS: return status;
        case ERRORS: return errors_value();
        case EVENTS: case EVENTS_PEEK: return events_value();
        case EVENTS_MASK: return mask;
        case DMA_CONFIG: return dma_config;
        case RECEIVE_CONFIG: return receive_config;
        case GATHER_CONFIG: return gather_config;
        default:
            if (r >= COUNTER_1 && r <= COUNTER_7) return remaining[r - COUNTER_1 + 1];
            return 0;
        }
    }
    // The registers whose value differs from `pub` (the register file as its owner last wrote it, zero at power-on, by
    // window word), as a mask over kPubWord; `pub` is brought up to date. A register added to reg() must be added here:
    // TileCtl's debug check compares every one with reg() after each publish.
    static constexpr unsigned kPub = 16;
    static constexpr unsigned kPubWord[kPub] = {PARAMS, STATUS, ERRORS, EVENTS, EVENTS_PEEK, EVENTS_MASK, DMA_CONFIG, RECEIVE_CONFIG,
                                               COUNTER_1, COUNTER_1 + 1, COUNTER_1 + 2, COUNTER_1 + 3, COUNTER_1 + 4,
                                               COUNTER_1 + 5, COUNTER_7, GATHER_CONFIG};
    uint32_t publish(uint32_t* pub) const {
        uint32_t m = 0;
        for (unsigned i = 0; i < kPub; i++) {
            const uint32_t v = reg(kPubWord[i]);
            if (pub[kPubWord[i]] != v) { pub[kPubWord[i]] = v; m |= 1u << i; }
        }
        return m;
    }
    bool wake() const { return wake_ff; }            // meip (6.2)
    bool nmi() const { return error_raised; }        // nmi, and the tile's error line (18)
};

}  // namespace units
