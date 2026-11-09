// tile_ctl.h: the tile's control in an M2 tile (WORKER8, G1): the register block as a unit with the M2 ports
// (TileControl.java; AUDIT-M2 3.7), the core's M2 ports, and the fragment of the tile builder that adds G1's units and
// returns the handles W9's builder wires. DECLARATIONS: the ports are the agreed interface (sessions/WORKER8/NOTES.md);
// the steps come with the implementation. W5's M1 units (controller.h: Core, RegBlock, TestBench) stay for M1's goldens.
//
// Levels (from the wires): 0 the block (TileControl.endCycle(t - 1) from what reached it at t, then the view for t);
// 1 the core (Controller.runCycle(t)). The director has no wire: it runs at any level.
//
// The register block's inputs, each registered (so endCycle(t) runs at level 0 of t + 1 from inputs delivered at t + 1):
//   bus         Link(1), width 2   the core's store, consuming load or bus error of t (W5's encoding)
//   go          Link(go_latency + skew[k] + 1), width 0, from W9's C&C model
//   landed      Link(cm_latency + 1), width 0, from the director: a port-B write covering command slot 0 landed at t
//   kline       Link(K + 1), width 1, from the director: counter id, a decrement due at t (judged without the same
//               cycle's store, D27; then the store's add)
//   send_busy   Reg, width 1, from W9's injection unit: the send hold occupied at the end of t; STATUS [3] is that OR
//               the core's own put at t (a send store or a retry: ref's `!sendHold.isEmpty(t)`, WordHold.isEmpty =
//               !occ && !pendPut), which the block sees on `bus`; none landed yet reads as not busy
//   bad         Link(1), width 0, from the director: a bad packet dropped at t (ERRORS [1] at endCycle(t)). A condition
//               that sets ERRORS in ref's cycle reaches the block as an event (PORTS.md 8): it steps the block. UNEXPECTED
//               and DONE_EARLY arise in the block itself (the K-line's event, a store); BUS_ERROR and ILLEGAL on `bus`.
//   ovf         Link(1), width 0, from W9's router (when the tile builder offers it, `ControlIO::router_ovf_ev`): an
//               overflow counted at t (D28's timing) as an event that steps the block (D29): ERRORS [0] at endCycle(t)
//   router_ovf, we_in_ovf, gather_ovf: the overflow Counts (statistics; ROUTER_OVF read only while the router offers no
//               event). The write engine's and the gather queue's overflows become events when G3/G4 build those units.
// Its outputs:
//   regs        RegFile, read by the core at level 1: the view for t (see TileCtl::kView)
//   wake, irq   Wires to the core
//   dma         RegFile (2: DMA_CONFIG, the record base), read by the write engine, the AG and the gather engine
//               (write_engine.h): their configuration, from t + 1
//   line        Link(ready_latency - 2), width 1, to W9's row collector: bit 0 READY rose at the end of t, bit 1 the
//               error line's level after endCycle(t); put only in a cycle where READY rises or the error line changes
// Added with G3/G4: the AG's start pulse (a Wire written at level 0 of t + 1), the AG's and the gather's status (Regs),
// the read port (a Fifo(64) from the core, its free count), and the condition Counts of the table FIFOs, AG drift, the
// read port.
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "config.h"
#include "sim.h"
#include "units/chip.h"
#include "units/director.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/big_sram.h"
#include "units/mem.h"
#include "units/table_store.h"
#include "units/write_engine.h"
#include "units/regblock.h"
#include "units/regmap.h"
#include "units/periph_slot.h"
#include "units/reqbus.h"
#include "units/rv32.h"

namespace units {

// ------------------------------------------------------------------------------------------------- the register block
struct TileCtl {
    // ---- inputs ----
    sim::LinkIn bus, go, landed, kline, bad, ovf;
    sim::LinkIn we_ovf, ts_ovf, hist_ovf;   // W11 (D29): width 0, Link(1): an overflow judged at t (ERRORS [4], [5])
    sim::RegIn send_busy;
    sim::CountIn router_ovf, we_in_ovf, gather_ovf;
    // G4 (W12), declared when `g4`: STATUS's gather_busy as the OR of three inputs (gather.h), the queue's overflow event
    sim::RegIn gather_busy, gather_srq_busy;   // the gather's own half; the read queue's (W11, through MemIO)
    sim::LinkIn gather_put, gather_ovf_ev;     // width 0, Link(1): the director's mode-4 put; the gather queue's overflow
    sim::RegIn ag_status;                      // the AG's running | done << 1 at the end of t (ag.h)
    sim::LinkIn req_freed_ev;                  // W14: width 0, Link(1): the request bus freed the FIFO's head at t
    sim::LinkIn drift;                         // width 0, Link(1): the AG's drift (ERRORS [3])
    sim::WireOut ag_start;                     // PULSE_DMA's starts so far, driven at level 0 of t + 1 (endCycle(t))
    sim::RegFileIn slot_win;                   // W15 (M3): the peripheral slot's window RegFile (its live bits), when `slot`
    // ---- outputs ----
    sim::RegFileOut regs, dma;
    sim::WireOut wake, irq;
    sim::LinkOut line;
    RegBlockState s;

    // The registers the core reads through `regs` (indices: the register numbers): PARAMS .. SRAM_RD_DEST_ADDR, the
    // counters (their value at the start of t), ROUTER_HDR/ADDR and ROUTER_SET_W0-W2 (a send store at t builds its packet
    // from them as they stand at t).
    static constexpr unsigned kRegs = SRAM_RD_DEST_ADDR + 1;
    static_assert(kRegs <= 64, "publish()'s mask");
    uint32_t pub[kRegs] = {};                  // the register file as last written (zero at power-on, as the engine's)
    uint32_t pub_dma[2] = {};                  // the `dma` RegFile likewise

    bool error_out = false;            // the error line's level as last put on `line`
    bool ovf_event = false;            // `ovf` declared: ROUTER_OVF arrives as an event (D29)
    bool g4 = false;                   // G4's inputs declared (the tile has the read side and G4's engines)
    bool slot = false;                 // W15: the peripheral slot's RegFile declared

    TileCtl(sim::Ports& p, const RegBlockState& initial, bool router_ovf_event = false, bool g4_ = false, bool slot_ = false)
        : s(initial), ovf_event(router_ovf_event), g4(g4_), slot(slot_) {
        if (slot) p.in(slot_win, "slot_win");
        if (ovf_event) p.in(ovf, "ovf", 0);
        p.in(we_ovf, "we_ovf", 0); p.in(ts_ovf, "ts_ovf", 0); p.in(hist_ovf, "hist_ovf", 0);
        if (g4) {
            p.in(gather_busy, "gather_busy", 1); p.in(gather_srq_busy, "gather_srq_busy", 1);
            p.in(gather_put, "gather_put", 0); p.in(gather_ovf_ev, "gather_ovf_ev", 0);
            p.in(ag_status, "ag_status", 1); p.in(drift, "drift", 0);
            p.out(ag_start, "ag_start");
        }
        p.in(bus, "bus", 2); p.in(go, "go", 0); p.in(landed, "landed", 0); p.in(kline, "kline", 1);
        p.in(req_freed_ev, "req_freed_ev", 0);
        p.in(send_busy, "send_busy", 1);
        p.in(bad, "bad", 0);
        p.in(router_ovf, "router_ovf"); p.in(we_in_ovf, "we_in_ovf"); p.in(gather_ovf, "gather_ovf");
        p.out(regs, "regs", kRegs); p.out(dma, "dma", DMA_REGS); p.out(wake, "wake"); p.out(irq, "irq");
        p.out(line, "line", 1);
    }
    // Stepped when something reaches it (and at the first cycle): endCycle(t - 1) from what reached it at t (the
    // shifted end of cycle, PORTS.md 7), then the view for t. A Count wakes nobody: a condition count that rises with
    // nothing else reaching the block is seen at its next step (never in M2's programs: no overflow, no bad packet).
    sim::Wake step(sim::Ctx& c) {
        RegBlockState::In in;
        if (c.ready(go)) { c.take(go); in.go = true; }
        if (c.ready(landed)) { c.take(landed); in.landed = true; }
        if (c.ready(kline)) { in.kline = uint32_t(c.peek(kline)[0]); c.take(kline); }
        if (c.ready(bad)) { c.take(bad); in.bad = true; }
        if (c.ready(req_freed_ev)) { c.take(req_freed_ev); in.freed = true; }   // W14: RequestPort's free at t (count)
        if (ovf_event && c.ready(ovf)) { c.take(ovf); in.router_ovf_ev = true; }
        if (c.ready(we_ovf)) { c.take(we_ovf); in.we_in_ovf_ev = true; }
        if (c.ready(ts_ovf)) { c.take(ts_ovf); in.table_ovf_ev = true; }
        if (c.ready(hist_ovf)) { c.take(hist_ovf); in.table_ovf_ev = true; }
        bool send_put = false;
        if (c.ready(bus)) {
            const sim::Word* m = c.peek(bus);
            in.op = uint32_t(m[0] >> 32); in.arg = uint32_t(m[0]); in.v = uint32_t(m[1]);
            send_put = in.op == BUS_STORE && in.arg >= ROUTER_SEND_32 && in.arg <= ROUTER_SEND_128;
            c.take(bus);
        }
        in.send_busy = send_put || (c.valid(send_busy) && c.read(send_busy)[0] != 0);   // ref: !sendHold.isEmpty(t)
        in.router_ovf = ovf_event ? s.ev_prev[0] : c.through_last(router_ovf);
        in.we_in_ovf = s.ev_prev[4]; in.gather_ovf = c.through_last(gather_ovf);   // [4]: the event above (D29)
        if (g4) {
            bool put = false;
            if (c.ready(gather_put)) { c.take(gather_put); put = true; }
            if (c.ready(gather_ovf_ev)) { c.take(gather_ovf_ev); in.gather_ovf_ev = true; }
            in.gather_ovf = s.ev_prev[RegBlockState::COND_GATHER_OVF];          // the event, not the Count
            in.gather_busy = put || reg_bit(c, gather_busy) || reg_bit(c, gather_srq_busy);
            if (c.ready(drift)) { c.take(drift); in.drift_ev = true; }
            const sim::Word st = c.valid(ag_status) ? c.read(ag_status)[0] : 0;
            in.ag_running = st & 1; in.ag_done = st >> 1 & 1;
        }
        if (slot) s.slot_live = c.read(slot_win, Win::R_LIVE);   // W15: liveEvents(t), liveErrors(t), as ref's view at t
        s.end_cycle(c.now() - 1, in);
        if (g4) c.drive(ag_start, s.dma_starts);
        // only the registers that changed (W12, D31): the same register file, without 33 writes a step
        for (uint64_t m = s.publish(pub); m; m &= m - 1) { const unsigned r = unsigned(__builtin_ctzll(m)); c.write(regs, r, pub[r]); }
        if constexpr (sim::kDebug)
            for (unsigned r = 0; r < kRegs; r++)
                if (pub[r] != s.reg(r)) FATAL("register block: register %u is 0x%x, published 0x%x (RegBlockState::publish misses it)", r, s.reg(r), pub[r]);
        if (s.dma_config != pub_dma[0]) { pub_dma[0] = s.dma_config; c.write(dma, DMA_R_CONFIG, s.dma_config); }
        if (s.record_base != pub_dma[1]) { pub_dma[1] = s.record_base; c.write(dma, DMA_R_RECORD_BASE, s.record_base); }
        c.drive(wake, s.wake());
        c.drive(irq, s.irq());
        if (s.ready_pulse || s.error_line != error_out) {
            c.put(line, sim::Word(s.ready_pulse) | sim::Word(s.error_line) << 1);
            error_out = s.error_line;
        }
        // W16: STATUS's send-busy from the core's own put at t is the block's alone (the injection unit's Reg only
        // changes if the send waits): look again next cycle, when the Reg says whether the hold is still occupied.
        // Otherwise a core polling STATUS after a send that left at once reads busy until something else steps the block
        // (C&C 1's first send in test_cc_units; M2's programs always had a landing or a K line to step it).
        return send_put ? c.next() : sim::Wake::none();
    }
    uint64_t digest() const { return digest_mix(s.digest(), error_out); }
    static bool reg_bit(const sim::Ctx& c, sim::RegIn r) { return c.valid(r) && c.read(r)[0] != 0; }
};

// ------------------------------------------------------------------------- previews (configuration regime, read-only)
// For the fingerprint writers only, between runs (Sim::observe; the machine built with Options::observe_arrivals). The
// block's endCycle(c) runs at level 0 of c + 1; ref's state at the end of c is endCycle(c) applied to a copy of the block,
// from its inputs arriving at c + 1 (those its step at c + 1 applies; with none, endCycle is a no-op). `U` is the M2
// block (TileCtl) or W5's M1 block (RegBlock: bus, GO, landed, freed).
template <class U>
RegBlockState block_at_end(const sim::Sim& m, const U& u, uint64_t c) {
    constexpr uint32_t kMax = 64;
    RegBlockState b = u.s;
    RegBlockState::In in;
    sim::PortEntry e[kMax];
    bool send_put = false;
    auto at_next = [&](const sim::LinkIn& p, auto f) {
        uint32_t n = m.observe(p, e, kMax).n;
        for (uint32_t i = 0; i < n; i++) if (e[i].at == c + 1) f(e[i]);
    };
    at_next(u.bus, [&](const sim::PortEntry& x) {
        in.op = uint32_t(x.w[0] >> 32); in.arg = uint32_t(x.w[0]); in.v = uint32_t(x.w[1]);
        send_put = in.op == BUS_STORE && in.arg >= ROUTER_SEND_32 && in.arg <= ROUTER_SEND_128;
    });
    at_next(u.go, [&](const sim::PortEntry&) { in.go = true; });
    at_next(u.landed, [&](const sim::PortEntry&) { in.landed = true; });
    if constexpr (requires { u.kline; }) {                               // M2's block
        at_next(u.kline, [&](const sim::PortEntry& x) { in.kline = uint32_t(x.w[0]); });
        at_next(u.bad, [&](const sim::PortEntry&) { in.bad = true; });
        if (u.ovf_event) at_next(u.ovf, [&](const sim::PortEntry&) { in.router_ovf_ev = true; });
        at_next(u.we_ovf, [&](const sim::PortEntry&) { in.we_in_ovf_ev = true; });
        at_next(u.ts_ovf, [&](const sim::PortEntry&) { in.table_ovf_ev = true; });
        at_next(u.hist_ovf, [&](const sim::PortEntry&) { in.table_ovf_ev = true; });
        sim::PortView r = m.observe(u.send_busy, e, kMax);
        in.send_busy = send_put || (r.put ? r.put_w[0] != 0 : (r.valid && e[0].w[0] != 0));
        in.router_ovf = b.ev_prev[0]; in.we_in_ovf = b.ev_prev[4]; in.gather_ovf = b.ev_prev[6];   // Counts: not here
        if (u.g4) {                                                      // G4: the gather-busy OR, the overflow event
            bool put = false;
            at_next(u.gather_put, [&](const sim::PortEntry&) { put = true; });
            at_next(u.gather_ovf_ev, [&](const sim::PortEntry&) { in.gather_ovf_ev = true; });
            auto reg = [&](const sim::RegIn& p) {
                sim::PortView v = m.observe(p, e, kMax);
                return v.put ? v.put_w[0] != 0 : (v.valid && e[0].w[0] != 0);
            };
            in.gather_busy = put || reg(u.gather_busy) || reg(u.gather_srq_busy);
            at_next(u.drift, [&](const sim::PortEntry&) { in.drift_ev = true; });
            sim::PortView v = m.observe(u.ag_status, e, kMax);
            const sim::Word st = v.put ? v.put_w[0] : (v.valid ? e[0].w[0] : 0);
            in.ag_running = st & 1; in.ag_done = st >> 1 & 1;
        }
    } else {                                                             // M1's block (W5's bench)
        at_next(u.freed, [&](const sim::PortEntry&) { in.freed = true; });
    }
    b.end_cycle(c, in);
    return b;
}
// What the block's endCycle(c) puts on `line` (READY's rising edge, bit 0; the error line, bit 1), which it puts at
// c + 1: W9's C&C fingerprint writers (G2Chip::edge) read it here as ref has it at the end of c.
struct LineEdge { bool put = false; sim::Word word = 0; };
inline LineEdge line_edge(const sim::Sim& m, const TileCtl& u, uint64_t c) {
    RegBlockState b = block_at_end(m, u, c);
    LineEdge r;
    r.put = b.ready_pulse || b.error_line != u.error_out;
    if (r.put) r.word = sim::Word(b.ready_pulse) | sim::Word(b.error_line) << 1;
    return r;
}

// ------------------------------------------------------------------------------------------------ the core, M2 tile
// The core of an M2 tile (W5's Rv32 with M2's ports; W5's `Core` stays for M1's bench). Its memories live outside the
// unit (`dmem`, `imem` in Rv32: a unit is moved into an arena at build); port B's link words are applied before its
// step (ref: written at t + cm_latency, seen by a load at t + cm_latency + 1).
struct TileCore {
    sim::RegFileIn regs, hist;
    sim::WireIn wake, irq;
    sim::LinkIn port_b;                // width 3: link words from the director, Link(cm_latency + 1)
    sim::LinkOut bus;                  // width 2: the core's store, consume or bus error of t -> the block, Link(1)
    sim::HoldOut send;                 // width 3: the send holding register -> W9's injection unit
    sim::LinkOut tables, hist_w;       // width 2: table-window stores -> the table store, HIST (Link(1))
    sim::FifoOut rd_port;              // W11: width 2, the read port's requests -> the read queue (client 2)
    sim::CountIn rp_injected;          // W11: read-port returns injected (free entries: injections before t)
    sim::CountOut rp_stores;           // W11: read-port reads queued (ReadPort.stores) -> the director (results outstanding)
    sim::RegFileIn win;                // W15 (M3): the peripheral slot's window RegFile (loads read it: AUDIT-M3 3.1 S4)
    sim::LinkOut periph;               // W15 (M3): width 1, a window store or a consuming EVENTS load -> the slot, Link(1)
    bool slot = false;                 // W15: the two above declared
    // W14 (D34, the request bus): the port's send side (every tile and controller) and a controller's receive side
    sim::LinkOut req_push;             // width 2: REQUEST_SEND's request {w0 | w1 << 32, w2 | w3 << 32} -> the leaf, Link(1)
    sim::WireIn req_freed;             // the leaf freed the FIFO's head at t (canPush's freedAt == t)
    sim::RegFileIn recv;               // a controller: its target's queue head {pending, w0-w3} as a load at t sees it
    sim::LinkOut recv_pop, recv_credit;   // a controller's REQUEST_RECEIVE: the pop (Link(1)) and the root's credit
    bool receiver = false;
    bool done_gate = true;             // the block's: with the gate closed only GO wakes (a live bit doesn't)
    uint32_t req_set_w[3] = {};        // REQUEST_SET_W0-W2 as this core stored them (the block's copy is a cycle later)
    Rv32 rv;
    // W11: the read port (ReadPort.java). Its queue is the Fifo into the read queue; its accounting is the core's stores
    // and the injection unit's injections (free entries) and the director's acceptances (outstanding).
    uint8_t rp_tile = 0;
    unsigned rp_depth = 64;
    uint64_t rp_stored = 0, rp_dropped = 0, rp_violations = 0;

    TileCore(sim::Ports& p, const Rv32& initial, unsigned tile = 0, unsigned read_port_depth = 64, bool slot_ = false,
             bool receiver_ = false, bool done_gate_ = true)
        : slot(slot_), receiver(receiver_), done_gate(done_gate_), rv(initial), rp_tile(uint8_t(tile)), rp_depth(read_port_depth) {
        if (slot) { p.in(win, "win"); p.out(periph, "periph", 1); }
        // W14: the request port's send side (every core) and a controller's receive side
        p.out(req_push, "req_push", 2); p.in(req_freed, "req_freed");
        if (receiver) { p.in(recv, "recv"); p.out(recv_pop, "recv_pop", 0); p.out(recv_credit, "recv_credit", 1); }
        p.in(regs, "regs"); p.in(hist, "hist"); p.in(wake, "wake"); p.in(irq, "irq"); p.in(port_b, "port_b", 3);
        p.out(bus, "bus", 2); p.out(send, "send", 3); p.out(tables, "tables", 2); p.out(hist_w, "hist_w", 2);
        p.out(rd_port, "rd_port", 2); p.in(rp_injected, "rp_injected"); p.out(rp_stores, "rp_stores");
    }
    struct Env {
        sim::Ctx& c; TileCore& u;
        uint32_t reg(unsigned r) const { return c.read(u.regs, r); }
        // W14: TileControl.wakeTerm with the live bit (request pending): the block's wake covers its sticky events; a
        // pending request wakes as EVENTS [12] under the mask (and never through a closed gate)
        bool wake() const {
            if (c.read(u.wake) != 0) return true;
            const uint32_t live = live_events();
            if (!live) return false;
            if (u.done_gate && (reg(STATUS) >> ST_DONE & 1)) return false;
            const uint32_t e = (reg(EVENTS) | live) & EVENTS_VALID, m = reg(EVENTS_MASK), q = reg(EVENTS_REQUIRED);
            return (e & m & ~q) != 0 || (q != 0 && (e & q) == q);
        }
        bool irq() const { return c.read(u.irq) != 0; }
        // RequestPort.canPush(t): room at the end of t - 1 (STATUS [6], request busy), or the head freed at t
        bool can_push() const { return !(reg(STATUS) >> ST_REQUEST_BUSY & 1) || c.read(u.req_freed) != 0; }
        void post(uint32_t op, uint32_t arg, uint32_t v) {
            if (op == BUS_STORE && arg >= REQUEST_SET_W0 && arg <= REQUEST_SET_W2) u.req_set_w[arg - REQUEST_SET_W0] = v;
            if (op == BUS_STORE && arg == REQUEST_SEND) {   // RequestPort.send: the request leaves for the leaf (Link(1))
                c.put(u.req_push, sim::Word(u.req_set_w[0]) | sim::Word(u.req_set_w[1]) << 32,
                      sim::Word(u.req_set_w[2]) | sim::Word(v) << 32);
                u.req_set_w[0] = u.req_set_w[1] = u.req_set_w[2] = 0;
            }
            c.put(u.bus, sim::Word(op) << 32 | arg, sim::Word(v));
        }
        // the receive side (4.18): a tile's reads 0, REQUEST_RECEIVE underflows; a controller's reads its queue's head
        uint32_t live_events() const {
            return u.receiver && c.read(u.recv, reqbus::ReqRecv::R_PENDING) ? 1u << EV_REQUEST_PENDING : 0;
        }
        uint32_t req_get(unsigned i) const { return u.receiver ? c.read(u.recv, reqbus::ReqRecv::R_W0 + i) : 0; }
        uint32_t req_receive(bool& ok) const {
            ok = u.receiver && c.read(u.recv, reqbus::ReqRecv::R_PENDING) != 0;
            if (!ok) return 0;
            c.put(u.recv_pop);
            c.put(u.recv_credit, sim::Word(c.now()));
            return c.read(u.recv, reqbus::ReqRecv::R_W3);
        }
        void send(uint64_t lo, uint64_t hdr, uint64_t hi) { c.put(u.send, lo, hdr, hi); }
        bool send_refused() const { return c.refused(u.send); }
        void table_store(uint32_t table, uint32_t i, uint32_t v) {
            if (table == TBL_HIST) c.put(u.hist_w, sim::Word(i), sim::Word(v));
            else c.put(u.tables, sim::Word(table) << 32 | i, sim::Word(v));
        }
        // ReadPort.free(t): entries free at the start of t (the stores so far; returns injected before t)
        uint32_t rp_free() const { return uint32_t(int64_t(u.rp_depth) - int64_t(u.rp_stored - c.through_last(u.rp_injected))); }
        // ReadPort.store at t, with SRAM_RD_DEST_ADDR as it stands at t; false: no free entry, dropped
        bool read_port(uint64_t, uint32_t chunk) {
            const uint32_t setup = c.read(u.regs, SRAM_RD_DEST_ADDR);
            const unsigned size = setup >> 17 & 3, ctr = setup >> 19 & 7;
            if (size == lw::SIZE_UNDEF) u.rp_violations++;
            if (int32_t(rp_free()) <= 0) { u.rp_dropped++; u.rp_violations++; return false; }
            const uint32_t a = uint32_t(lw::R_DMEM) << 24 | (setup & 0x1FFFF);
            if (!lw::aligned(a, size)) u.rp_violations++;            // its result is a bad packet (invariant 20)
            c.put(u.rd_port, mem::req_a(chunk, size, lw::M_WRITE, ctr, u.rp_tile), mem::req_b(a));
            u.rp_stored++;
            c.inc(u.rp_stores);
            return true;
        }
        // W15 (M3): PeripheralSlot.read / storeStalls / write as the core meets them (AUDIT-M3 3.1 S2-S4, S8)
        int window_load(unsigned n, unsigned w, uint32_t& v) {
            if (!u.slot) FATAL("tile core: a load from peripheral %u's window: the tile has no peripheral slot", n);
            if (w == 5) {                                  // a consuming load: the value, and its consume under the mask
                v = c.read(u.win, Win::reg(n, Win::R_STD + 5));
                const uint32_t m = c.read(u.win, Win::reg(n, Win::R_STD + 7));
                if (v & m) c.put(u.periph, CoreOp::consume(n, v & m));
                return 1;
            }
            if (w < Win::STD) { v = c.read(u.win, Win::reg(n, Win::R_STD + w)); return 1; }   // 8, 9 without a DMA: 0
            v = 0;
            if (w < Win::OWN0) return 2;
            const unsigned k = w - Win::OWN0;
            if (!(c.read(u.win, Win::reg(n, Win::R_DEFINED + k / 32)) >> (k % 32) & 1)) return 2;
            v = c.read(u.win, Win::reg(n, Win::R_OWN + k));
            return 1;
        }
        bool window_stalls(unsigned n, unsigned w) const {
            if (!u.slot || w < Win::OWN0) return false;
            const unsigned k = w - Win::OWN0;
            return c.read(u.win, Win::reg(n, Win::R_STALL + k / 32)) >> (k % 32) & 1;
        }
        bool window_store(uint64_t, unsigned n, unsigned w, uint32_t v) {
            if (!u.slot) FATAL("tile core: a store to peripheral %u's window: the tile has no peripheral slot", n);
            if (w == 0 || w == 1 || w == 2 || w == 5 || w == 6) return false;   // read-only standard words
            if (w >= Win::OWN0) {
                const unsigned k = w - Win::OWN0;
                const bool def = c.read(u.win, Win::reg(n, Win::R_DEFINED + k / 32)) >> (k % 32) & 1;
                const bool wr = c.read(u.win, Win::reg(n, Win::R_WRITABLE + k / 32)) >> (k % 32) & 1;
                if (def && !wr) return false;              // a defined read-only word: a bus error
            }
            c.put(u.periph, CoreOp::store(n, w, v));       // the slot applies it at level 0 of t + 1 (and flags reserved)
            return true;
        }
        uint32_t hist(uint32_t i) const {                // RmwPipe.ctrlRead: on a write-back cycle, the engine's value
            if (c.read(u.hist, HIST_WB_AT) == uint32_t(c.now() + 1)) return c.read(u.hist, HIST_WB_VAL);
            return c.read(u.hist, i);
        }
    };
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(port_b)) {
            if (rv.stop == Stop::running) apply_port_b(c.peek(port_b));
            c.take(port_b);
        }
        Env e{c, *this};
        uint64_t next = rv.step(e, c.now());
        return next == Rv32::kNever ? sim::Wake::none() : sim::Wake::at_cycle(next);
    }
    uint64_t digest() const { return digest_mix(rv.digest(), rv.pending_t ^ rv.send_w[0] ^ rv.send_w[1] << 1 ^ rv.send_w[2] << 2); }
    // ControllerMemories.runCycle: one naturally aligned 32-, 64- or 128-bit write at its offset (high bits ignored).
    void apply_port_b(const sim::Word* w) {
        uint64_t h = w[1];
        uint32_t a = lw::addr(h), n = lw::chunks(lw::size(h));
        uint32_t* m = lw::region(a) == lw::R_DMEM ? rv.dmem : rv.imem;
        uint32_t w0 = (lw::offset(a) & (MEM_BYTES - 1)) >> 2;
        for (uint32_t i = 0; i < n; i++) m[(w0 + i) & (MEM_WORDS - 1)] = uint32_t((i < 2 ? w[0] : w[2]) >> (32 * (i & 1)));
    }
};

// ---------------------------------------------------------------------------------------------- the core in an M2 tile
// W5's Core gains, in an M2 tile (TileConfig::m2): `send`, a HoldOut of width 3 to W9's injection unit (a send store at
// t puts the packet at t; refused at t + 1: retried, the core stalls); its `memw` input becomes port B, link words from
// the director (Link(cm_latency + 1)); `tables` and `hist_w`, Link(1)s of width 2 to the table store and HIST
// (table_store.h: a table-window store of t, {table << 32 | index, value}); `hist`, HIST's RegFile (a HIST load is one
// cycle, as ref's). A load of PTR, STREAM or GROUP is a bus error, as in ref.
// W14 (D34): the request port is modelled (REQUEST_SEND pushes over `req_push` to the bus's leaf, `req_freed` gives
// canPush's free of t; the receive side reads a controller's queue through `recv`; NOTES.md section 14).

// ------------------------------------------------------------------------------------- G1's part of the tile builder
// The configuration G1's units depend on (the D12 header's keys that reach them).
struct G1Config {
    unsigned tile = 0;
    Rv32Timing timing;                 // reg_load_latency, branch_penalty, mul_latency, ref_cycle
    bool done_gate = true;
    uint32_t mem_chunks = 1u << 20;
    uint32_t periph_present = 1;
    bool periph_slot = false;          // W15 (M3): the peripheral slot (the replayer as peripheral 1) and its paths
    PeriphType periph1 = PT_REPLAYER;  // W15: a mesh tile's peripheral 1 in the slot (tests: PT_TEST)
    bool cc3_tracer = false;           // W16 (M3): C&C 3's peripheral 1 the Tracer (ref's cc3Tracer), else the stub
    unsigned K = 8, cm_latency = 2;    // the write counters' line, director -> port B
    // G3 (W11): the memory path's keys
    unsigned ts_engine_latency = 1;    // tsEngineLatency: the engines' Links to and from the table store and HIST
    unsigned ag_lookahead = 0;         // agLookahead: the AG's read-ahead rings; 0: ref's default, 2 ts_engine_latency + 2
    unsigned area_queue_depth = 8, sram_read_latency = 2, read_port_depth = 64;
    bool write_combining = false, fifo_count_before_pop = true;
    bool sram_running_sum = true;      // D31: BigSram keeps its fingerprint sum per store; false in timing runs
};

// G1's part of a tile, as W9's tile builder meets it: chip.h's `ControlIO` (the ports agreed in NOTES.md, plus
// ROUTER_OVF), returned by a `ControlBuilder`.
// The units G1 adds for tile k (names "t<k>.core", "t<k>.ctl", "t<k>.dir", and the configuration parts of G3/G4's
// units it owns until they extend them: "t<k>.we", "t<k>.ag", "t<k>.gather", "t<k>.ts", "t<k>.hist"), wired to each
// other inside; the director's we-in and gather outputs go to the write engine's and the gather engine's inputs. `ctx`
// is a G1Ctx. A ControlBuilder (chip.h).
struct G1Ctx {
    G1Config cfg;                      // per tile: cfg.tile is overwritten with k
    const uint32_t* bootrom = nullptr; uint32_t bootrom_words = 0;
    uint32_t* mem = nullptr;           // units x 2 x MEM_WORDS words (data, program per unit), zero at power-on; outlives
                                       // the machine (the boot fills them through port B)
    unsigned units = 64;               // the units `mem` holds: 64 tiles, or 67 with C&C 1-3 as units 64-66 (M3)
    unsigned we_in_depth = 4, gather_depth = 16;
    SramBlocks* sram = nullptr;        // G3: the tiles' big-SRAM blocks (one per tile, mapped at configuration); outlives
                                       // the machine
};
ControlIO add_g1(sim::Config& cfg, unsigned k, void* ctx);
static_assert(std::is_same_v<decltype(&add_g1), ControlBuilder>);

}  // namespace units
