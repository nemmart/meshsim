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

#include "check.h"
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
    // W15 (M3): the peripheral slot's live bits at t (its window RegFile's Win::R_LIVE, set by TileCtl before end_cycle):
    // [2:0] EVENTS [15:13] ((events | DMA done) & EVENTS_MASK != 0 per peripheral), [5:3] ERRORS [13:11] (ERRORS != 0)
    uint32_t slot_live = 0;
    uint32_t live_events() const { return (slot_live & 7) << EV_PERIPH_1; }
    uint32_t live_errors() const { return (slot_live >> 3 & 7) << 11; }
    bool q_all = false, q_one[8] = {}, cond_all_prev = true, cond_one_prev[8] = {true, true, true, true, true, true, true, true};
    bool rd_done_prev = false;
    uint32_t dma_starts = 0;                    // G4 (W12): PULSE_DMA starts so far (the AG's `start` Wire: a change = a start)
    bool dma_pulse = false;                     // PULSE_DMA's start applied at this endCycle (the AG's pStart until its step)
    uint32_t remaining[8] = {};                 // the write counters (20 bits), 1-7: the director's in ref
    uint64_t ev_prev[15] = {};                  // the condition counts as endCycle last saw them (Conditions.N)
    bool ready_pulse = false;                   // READY rose at this endCycle (Chip collects readyPulseAt == t)
    bool error_line = false;                    // TILE_CONDITIONS != 0 after this endCycle
    uint64_t unexpected_total = 0, done_early = 0, counter_wraps = 0, router_sends = 0, bad_total = 0;
    enum : unsigned { COND_ROUTER_OVF = 0, COND_BAD = 1, COND_DRIFT = 3, COND_WE_IN_OVF = 4, COND_TABLE_OVF = 5, COND_GATHER_OVF = 6 };
    // ---- statistics, and ref's violations (counted) ----
    uint64_t gos = 0, dones = 0, readies = 0, sends = 0, trace_stores = 0, violations = 0, sim_exit_at = 0;
    // W23 (P5): TileControl's statistics the control suites read (not in the digest): counters-done edges with no
    // qualifying arrival (spuriousEdges), and the cycles of the last done, the last landed command and the last READY
    // pulse (lastDoneAt, lastLandedAt: -1 before any; readyPulseAt: INT64_MIN before any)
    uint64_t spurious_edges = 0;
    int64_t last_done_at = -1, last_landed_at = -1, ready_pulse_at = INT64_MIN;
    int64_t counter_write_at[8] = {-1, -1, -1, -1, -1, -1, -1, -1};   // each write counter's last store (counterWriteAt)
    uint64_t sends_by_size[3] = {};             // send stores by size: 32, 64, 128 bits (sendsBySize)
    int ck_tile = -1;                  // W20: the checker's facts (src/check.h) under this tile; -1 none (a preview's copy)
    int64_t (*tlog)[2] = nullptr;      // W20 (harness-only; H6): every TRACE store {t, value}, into the harness's array
    uint32_t tlog_cap = 0, tlog_n = 0; //   (null otherwise, and in a preview's copy)
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
        // G4 (W12)
        bool gather_busy = false;          // GatherEngine.busy(t): the OR of its three inputs (gather.h)
        bool gather_ovf_ev = false;        // the gather queue overflowed, judged at t (an event, D29: ERRORS [6])
        bool ag_running = false, ag_done = false;   // the AG's running and done at the end of t (its status Reg)
        bool drift_ev = false;             // a refusal in a group run at t (an event, D29: ERRORS [3], Conditions.DRIFT)
        // W11 (G3), D29: overflows judged at t, as events: ERRORS [4] (the write engine's input), [5] (the table
        // store's and HIST's request FIFOs) at endCycle(t)
        bool we_in_ovf_ev = false, table_ovf_ev = false;
    };
    // AddressGenerator.runningForStatus / doneForStatus at this endCycle: the AG's state at the end of t with the pulse
    // of this endCycle (ref's pStart, set by apply and consumed at the AG's next step)
    bool rd_running(const In& in) const { return in.ag_running || dma_pulse; }
    bool rd_done(const In& in) const { return in.ag_done && !dma_pulse; }

    UNITS_NOINLINE void end_cycle(uint64_t t, const In& in) {
        if (in.kline) {                                                  // the director's decrement due at t (D27)
            if (remaining[in.kline] == 0) unexpected_total++;           // stays 0: ERRORS [2] (4.12)
            else remaining[in.kline]--;
        }
        if (in.freed) { if (req_n == 0) violations++; else req_n--; }   // the bus freed the head at t, before the tile ran
        if (in.go) { done = landed = gate = false; gos++; events |= 1u << EV_GO; }                    // 1.
        uint32_t clears = 0;
        dma_pulse = false;
        if (in.op == BUS_STORE) clears = apply(t, in.arg, in.v, in.ag_running);                                     // 2.
        if (in.landed) { landed = true; last_landed_at = int64_t(t); events |= 1u << EV_COMMAND; }   // 3.
        if (in.op == BUS_ERROR) conditions |= 1u << in.arg;                                          // 4. new errors
        if (in.bad) { bad_total++; conditions |= 1u << COND_BAD; }
        if (in.router_ovf_ev) conditions |= 1u << COND_ROUTER_OVF;
        if (in.gather_ovf_ev) conditions |= 1u << COND_GATHER_OVF;
        if (in.drift_ev) conditions |= 1u << COND_DRIFT;
        if (in.we_in_ovf_ev) conditions |= 1u << COND_WE_IN_OVF;
        if (in.table_ovf_ev) conditions |= 1u << COND_TABLE_OVF;
        const uint64_t counts[] = {in.router_ovf, bad_total, unexpected_total, 0, in.we_in_ovf, 0, in.gather_ovf, rp_errors, done_early};
        for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
            if (counts[i] > ev_prev[i]) conditions |= 1u << i;
            ev_prev[i] = counts[i];
        }
        bool rd = done && landed;                                                                    //    READY's edge
        ready_pulse = rd && !ready_level;
        if (ready_pulse) { readies++; ready_pulse_at = int64_t(t); }
        ready_level = rd;
        if (in.op == BUS_CONSUME) events &= ~(in.v & ~EVENTS_LIVE);                                  // 5. the consume
        const bool rdd = rd_done(in);                                                               //    [10]: rd_done's edge
        if (rdd && !rd_done_prev) events |= 1u << EV_RUN_DONE;
        rd_done_prev = rdd;
        bool rb = busy();                                                                            //    the edge events
        if (!rb && request_busy_prev) events |= 1u << EV_REQUEST_ROOM;
        request_busy_prev = rb;
        counter_events(clears);
        error_line = (conditions | live_errors()) != 0;                   // TileControl.errorLine: the slot's too
        status = uint32_t(rd_running(in)) << ST_RD_RUNNING | uint32_t(rdd) << ST_RD_DONE
                 | uint32_t(in.gather_busy) << ST_GATHER_BUSY
                 | uint32_t(in.send_busy) << ST_SEND_BUSY | uint32_t(done) << ST_DONE | uint32_t(landed) << ST_LANDED
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
        } else if (cond && !cond_all_prev) spurious_edges++;                                   // W23: statistics
        cond_all_prev = cond;
    }

    // The core's store of t (TileControl.apply); returns the event bits it clears this cycle (beating a same-cycle set).
    uint32_t apply(uint64_t t, uint32_t r, uint32_t v, bool ag_running = false) {
        uint32_t clears = 0;
        switch (r) {
        case PULSE_DONE:
            if (v & 1) {
                if (!counters_zero()) {                                   // done early (4.13): ERRORS [8]
                    done_early++;
                    if (ck_tile >= 0) {                                   // W20: invariant 4's fact (the counts, 20 bits each)
                        uint64_t m = 0, w[3] = {};
                        for (unsigned c = 1; c <= 7; c++) if (remaining[c]) { m |= 1u << c; w[(c - 1) / 3] |= uint64_t(remaining[c]) << (20 * ((c - 1) % 3)); }
                        sim::check::record(t, ck_tile, sim::check::Kind::CTL_DONE_EARLY, m, w[0], w[1], w[2]);
                    }
                }
                if (!(mask & 1u << EV_GO)) {                             // done pulsed with GO clear in the mask
                    violations++;
                    if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_DONE_GO_MASKED);
                }
                done = true; dones++; last_done_at = int64_t(t);
                if (done_gate) gate = true;
                clears = COUNTER_EVENTS;
                events &= ~clears; q_all = false;
                for (bool& q : q_one) q = false;
            }
            break;
        case PULSE_DMA:                                                   // TileControl.apply (W12, G4)
            if (v & 1) {
                if (!ag_running) { clears |= 1u << EV_RUN_DONE; events &= ~clears; }   // running: the AG stops (UB, 8.6)
                dma_starts++; dma_pulse = true;
            }
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
            router_sends++; sends_by_size[r - ROUTER_SEND_32]++;     // W23: statistics (sendsBySize)
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
        case TRACE:
            trace_stores++;
            if (tlog) {                                                  // W20 (harness-only): ref's traceHook / logTrace
                if (tlog_n == tlog_cap) FATAL("register block: the harness's TRACE log is full (%u stores)", tlog_cap);
                tlog[tlog_n][0] = int64_t(t); tlog[tlog_n][1] = int64_t(v); tlog_n++;
            }
            break;
        default:
            if (r >= WRITE_COUNTER_1 && r <= WRITE_COUNTER_7) {           // 4.12: a write adds; N > 0 arms; 0 does nothing
                unsigned k = r - WRITE_COUNTER_1 + 1;
                counter_write_at[k] = int64_t(t);                        // W23: statistics (counterWriteAt)
                uint32_t n = v & 0xFFFFF;
                if (n) {
                    uint64_t sum = uint64_t(remaining[k]) + n;
                    if (sum > 0xFFFFF) {
                        counter_wraps++; violations++;
                        if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_COUNTER_WRAP, k);   // W20
                    }
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
                               rd_setup, dma_starts, uint64_t(q_all) | uint64_t(cond_all_prev) << 1 | uint64_t(rd_done_prev) << 2
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
        case ERRORS: return conditions | live_errors();                   // TileControl.errorsAt: the slot's live bits
        case EVENTS: return (events | live_events()) & EVENTS_VALID;      // TileControl.eventsAt: the slot's live bits
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
    // The registers whose value differs from `pub` (the register file as its owner last wrote it, zero at power-on), as a
    // mask of register numbers; `pub` is brought up to date. The values are reg()'s, straight-line (W12, D31: the block
    // writes only these; the engine ignores a write of an unchanged value, so the register file is the same). A register
    // added to reg() must be added here: TileCtl's debug check compares every one with reg() after each publish.
    uint64_t publish(uint32_t* pub) const {
        uint64_t m = 0;
        auto chk = [&](unsigned r, uint32_t v) { if (pub[r] != v) { pub[r] = v; m |= uint64_t(1) << r; } };
        chk(PARAMS, params); chk(STATUS, status); chk(ERRORS, conditions | live_errors()); chk(EVENTS, (events | live_events()) & EVENTS_VALID);
        chk(EVENTS_MASK, mask); chk(EVENTS_REQUIRED, required); chk(ROUTER_HDR, router_hdr); chk(ROUTER_ADDR, router_addr);
        chk(ROUTER_SET_W0, router_w[0]); chk(ROUTER_SET_W1, router_w[1]); chk(ROUTER_SET_W2, router_w[2]);
        chk(DMA_CONFIG, dma_config); chk(DMA_RECORD_BASE_ADDRESS, record_base); chk(SRAM_RD_DEST_ADDR, rd_setup);
        for (unsigned c = 1; c <= 7; c++) chk(WRITE_COUNTER_1 + c - 1, remaining[c]);
        return m;
    }
    bool wake() const {                                                   // TileControl.wakeTerm
        uint32_t e = (events | live_events()) & EVENTS_VALID;
        if (gate) return e >> EV_GO & 1;
        return (e & mask & ~required) != 0 || (required != 0 && (e & required) == required);
    }
    bool irq() const { return (conditions | live_errors()) != 0; }
};

}  // namespace units
