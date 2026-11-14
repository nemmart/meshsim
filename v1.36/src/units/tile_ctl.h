// tile_ctl.h: the tile's control: the tile control unit as a framework unit (TileCtl, around regblock.h's state), the
// core with its memories, register_bus and the core's side of the write, read and request units (TileCore), and the
// fragment of the tile builder that adds a tile's control units and returns the handles the builder wires (add_g1).
// Sections are v1.36's machine-spec's (the RTL's rtl/tile/base/spec.md).
//
// Levels (from the wires): the tile control unit runs before the core in a cycle: at t it applies the edge that ends
// t - 1 (regblock.h's end_cycle(t - 1), from what reached it at t: the shifted end of cycle, PORTS.md 7), then publishes
// the state at the end of t - 1, which the core reads at t.
//
// THE REGISTER BUS'S TIMING (6.4; the RTL's register_bus, its header's "Timing"). The core (rv32.h) calls the bus in the
// cycle an access is made in, m: for an access it raises in cycle q, m = q for a table and q + 1 for a register
// window (the bus's stage); a store that may stall is made in the first cycle from q + 1 its owner is ready in.
//   - a window load returns the state at the end of m - 1: the published view at m; its side effect (an EVENTS
//     load's consumption, a REQUEST_RECEIVE's pop) is at the end of m: put on `side`, a Link(1), at m;
//   - a window store takes effect at the end of m: put on `bus`, a Link(1), at m, as are the core's status pulses
//     in the cycle of each (core_status's flip-flops: the cycle after the trap), which reach ERRORS at its end; the
//     registers on the core's side (the write, read and request units') are written in m, which no load of m reads;
//   - core_status.canonical_wait, a flip-flop of the core: its level in cycle t + 1 is known as the core's step of
//     t ends, and put on `canon`, a Link(2), then, when it changes: the tile control unit has it at the edge that
//     ends t + 1 (7.3);
//   - a table load returns the state at the end of m - 1 (the tables' RegFiles at m), a table store takes effect at
//     the end of m (the table stores' `window`, a Link(1));
//   - an access fault reaches no owner: the core answers it itself, the cycle after it is raised (Env::bus_kind).
// Stores run one a cycle and window loads one every other cycle.
//
// THE TILE CONTROL UNIT'S PORTS (TileCtl):
//   bus         Link(1), width 2   the core's window store or status pulse, made in t
//   canon       Link(2), width 1   core_status.canonical_wait's level in the cycle after the core's step that put it
//   side        Link(1), width 1   a load's side effect: a consuming EVENTS load; receive_underflow's pulse
//   release     Link, width 0      BARRIER_RELEASE's pulse (18), from the master tile's Ingress/Egress; only with the
//                                  strap `barrier` (7.1: a special tile's barrier is not wired)
//   kline       Link(LAND_LATENCY + 1), width 1, from the director: a counter's decrement due (12.3)
//   bad, ovf, drift, gather_ovf_ev  width 0, Link(1): the errors' pulses of the receive unit, the router, the DMA unit
//               and the gather unit (7.4)
//   ts_ovf      width 0, Link(1): an overflow of v1.35's STREAM and GROUP request FIFOs, which v1.36's hardware does not
//               have (8.5): fatal, named (PENDING-v1.36.md)
//   req_freed_ev  width 0, Link(1): the request queue's head left
//   gather_busy, gather_srq_busy (Regs), gather_put (Link(1)): STATUS.gather_busy's inputs (gather.h)
//   ag_status   Reg: the address generator's running | done << 1
//   slot_win    RegFile: the peripheral slot's window (its live bits), when the tile has the slot
//   recv        RegFile: a receiving tile's receive queue (EVENTS.request_pending's level)
// Its outputs:
//   regs        RegFile (TILE_REGS, by window word), read by the core: the state at the end of t - 1
//   wake, irq   Wires to the core: meip and nmi (6.2), each the unit's flip-flop
//   dma         RegFile (DMA_REGS: DMA_CONFIG, RECEIVE_CONFIG, GATHER_CONFIG), read by the write engine, the address
//               generator and the gather unit (write_engine.h)
//   ag_start    Wire: DMA_START's starts so far (a change = a start)
//   arrive      Link, width 0: BARRIER_ARRIVE (18), put in the cycle its flip-flop is 1; only with the strap `barrier`
//   error       Link, width 1: the error line's level (18), put in the first cycle of each new level
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "config.h"
#include "check.h"
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
#include "units/register_bus.h"
#include "units/reqbus.h"
#include "units/rv32.h"

namespace units {

// The core's words to the tile control unit: on `bus` {op << 32 | arg, value}, on `side` {op << 32 | arg}.
enum : uint32_t {
    BUS_STORE = 1,       // arg: the tile window's word (regmap.h's Reg); value
    BUS_STATUS = 2,      // arg: an error's bit (7.4): core_status's access_fault or illegal_instruction, read_overflow
    SIDE_EVENTS = 1,     // a consuming load of EVENTS presented this cycle
    SIDE_ERROR = 2,      // arg: an error's bit: receive_underflow (a REQUEST_RECEIVE of an empty queue)
};

// ------------------------------------------------------------------------------------------ the tile control unit
struct TileCtl {
    // ---- inputs ----
    sim::LinkIn bus, canon, side, release, kline, bad, ovf;
    sim::LinkIn ts_ovf;
    sim::CountIn router_ovf, gather_ovf;   // the overflow Counts (statistics: the events above carry them)
    sim::RegIn send_busy;                      // the inject unit's holding-register status (v1.36 has no STATUS bit for it)
    sim::RegIn gather_busy, gather_srq_busy;   // the gather's own half; the read queue's (through MemIO)
    sim::LinkIn gather_put, gather_ovf_ev;     // width 0, Link(1): the director's GATHER put; the gather queue's overflow
    sim::RegIn ag_status;                      // the AG's running | done << 1 at the end of t (ag.h)
    sim::LinkIn req_freed_ev;                  // width 0, Link(1): the request tree took the queue's head at t
    sim::LinkIn drift;                         // width 0, Link(1): the AG's drift (ERRORS.drift)
    sim::RegFileIn slot_win;                   // the peripheral slot's window RegFile (its live bits), when `slot`
    sim::RegFileIn recv;                       // the receive queue's head (ReqRecv::R_PENDING), when `receiver`
    // ---- outputs ----
    sim::RegFileOut regs, dma;
    sim::WireOut wake, irq, ag_start;
    sim::LinkOut arrive, error;
    RegBlockState s;

    uint32_t pub[TILE_REGS] = {};              // the register file as last written (zero at power-on, as the engine's)
    uint32_t pub_dma[DMA_REGS] = {};           // the `dma` RegFile likewise
    bool error_out = false;                    // the error line's level as last put on `line`
    bool g4 = false;                           // the read side's inputs declared (the tile has the read side)
    bool slot = false;                         // the peripheral slot's RegFile declared
    bool receiver = false;                     // the receive queue's RegFile declared
    int ck_tile = -1;                          // the checker's facts (src/check.h): ERRORS and the error line; -1 none
    uint32_t ck_errors = 0;
    bool ck_line = false;

    TileCtl(sim::Ports& p, const RegBlockState& initial, bool g4_ = false, bool slot_ = false, bool receiver_ = false)
        : s(initial), g4(g4_), slot(slot_), receiver(receiver_) {
        if (slot) p.in(slot_win, "slot_win");
        if (receiver) p.in(recv, "recv");
        p.in(ovf, "ovf", 0);
        p.in(ts_ovf, "ts_ovf", 0);
        if (g4) {
            p.in(gather_busy, "gather_busy", 1); p.in(gather_srq_busy, "gather_srq_busy", 1);
            p.in(gather_put, "gather_put", 0); p.in(gather_ovf_ev, "gather_ovf_ev", 0);
            p.in(ag_status, "ag_status", 1); p.in(drift, "drift", 0);
            p.out(ag_start, "ag_start");
        }
        p.in(bus, "bus", 2); p.in(canon, "canon", 1); p.in(side, "side", 1); p.in(kline, "kline", 1);
        if (s.straps.barrier) { p.in(release, "release", 0); p.out(arrive, "arrive", 0); }
        p.in(req_freed_ev, "req_freed_ev", 0);
        p.in(send_busy, "send_busy", 1);
        p.in(bad, "bad", 0);
        p.in(router_ovf, "router_ovf"); p.in(gather_ovf, "gather_ovf");
        p.out(regs, "regs", TILE_REGS); p.out(dma, "dma", DMA_REGS); p.out(wake, "wake"); p.out(irq, "irq");
        p.out(error, "error", 1);
    }
    // Stepped when something reaches it (and at the first cycle): the edge that ends t - 1, from what reached it at t,
    // then the state at the end of t - 1 for t's readers.
    sim::Wake step(sim::Ctx& c) {
        RegBlockState::In in;
        const uint64_t t = c.now();
        if (s.straps.barrier && c.ready(release)) { c.take(release); in.release = true; }
        if (c.ready(kline)) { in.decrement = uint32_t(c.peek(kline)[0]); c.take(kline); }
        if (c.ready(bad)) { c.take(bad); in.error_pulses |= 1u << ERR_BAD_PACKET; }
        if (c.ready(req_freed_ev)) { c.take(req_freed_ev); in.freed = true; }
        if (c.ready(ovf)) { c.take(ovf); in.error_pulses |= 1u << ERR_ROUTER_OVERFLOW; }
        // v1.35's request FIFOs in front of STREAM and GROUP, which v1.36's address generator does not have (8.5): an
        // overflow there is a table read lost that the hardware would have made: stop.
        if (c.ready(ts_ovf))
            FATAL("tile %u, cycle %llu: the simulator's STREAM or GROUP request FIFO overflowed (v1.35's FIFOs: machine-spec "
                  "8.5; PENDING-v1.36.md)", s.straps.tile_id, (unsigned long long)t);
        if (c.ready(bus)) {
            const sim::Word* m = c.peek(bus);
            const uint32_t op = uint32_t(m[0] >> 32), arg = uint32_t(m[0]);
            if (op == BUS_STORE) { in.store = true; in.word = arg; in.value = uint32_t(m[1]); }
            else if (op == BUS_STATUS) in.error_pulses |= 1u << arg;
            c.take(bus);
        }
        if (c.ready(canon)) { s.canon = c.peek(canon)[0] != 0; c.take(canon); }   // the level in the cycle this edge ends
        if (c.ready(side)) {
            const sim::Word m = c.peek(side)[0];
            if (uint32_t(m >> 32) == SIDE_EVENTS) in.events_load = true;
            else in.error_pulses |= 1u << uint32_t(m);
            c.take(side);
        }
        if (g4) {
            bool put = false;
            if (c.ready(gather_put)) { c.take(gather_put); put = true; }
            if (c.ready(gather_ovf_ev)) { c.take(gather_ovf_ev); in.error_pulses |= 1u << ERR_GATHER_OVERFLOW; }
            in.gather_busy = put || reg_bit(c, gather_busy) || reg_bit(c, gather_srq_busy);
            if (c.ready(drift)) { c.take(drift); in.error_pulses |= 1u << ERR_DRIFT; }
            const sim::Word st = c.valid(ag_status) ? c.read(ag_status)[0] : 0;
            in.ag_running = st & 1; in.ag_done = st >> 1 & 1;
        }
        if (slot) s.slot_live = c.read(slot_win, Win::R_LIVE);
        if (receiver) s.request_pending = c.read(recv, reqbus::ReqRecv::R_PENDING) != 0;
        s.end_cycle(t - 1, in);
        if constexpr (sim::check::kOn) {                       // the checker's facts (invariant 15)
            if (ck_tile >= 0) {
                const uint32_t v = s.errors_value();
                if (v != ck_errors) { ck_errors = v; sim::check::record(t - 1, ck_tile, sim::check::Kind::ERRORS_SET, v); }
                if (s.nmi() != ck_line) { ck_line = s.nmi(); sim::check::record(t - 1, ck_tile, sim::check::Kind::ERROR_LINE, ck_line); }
            }
        }
        if (g4) c.drive(ag_start, s.dma_starts);
        // only the registers that changed (D31)
        for (uint32_t m = s.publish(pub); m; m &= m - 1) { const unsigned r = RegBlockState::kPubWord[__builtin_ctz(m)]; c.write(regs, r, pub[r]); }
        const uint32_t d[DMA_REGS] = {s.dma_config, s.receive_config, s.gather_config};
        for (unsigned i = 0; i < DMA_REGS; i++) if (d[i] != pub_dma[i]) { pub_dma[i] = d[i]; c.write(dma, i, d[i]); }
        c.drive(wake, s.wake());
        c.drive(irq, s.nmi());
        // the nets (18), in the cycle after this edge, each from its flip-flop
        if (s.arrive_pulse) {
            if (!s.straps.barrier) FATAL("tile %u: BARRIER_ARRIVE pulsed on a tile whose barrier is not wired", s.straps.tile_id);
            c.put(arrive);
        }
        if (s.nmi() != error_out) { error_out = s.nmi(); c.put(error, sim::Word(error_out)); }
        // The address generator's start is the block's until the generator's step: look again in the next cycle, when its
        // status says the transfer runs (STATUS.dma_busy, and an empty transfer's end: 8.2). And BARRIER_ARRIVE's
        // flip-flop falls in the next cycle.
        return s.dma_pulse || s.arrive_pulse ? c.next() : sim::Wake::none();
    }
    uint64_t digest() const { return digest_mix(s.digest(), error_out); }
    static bool reg_bit(const sim::Ctx& c, sim::RegIn r) { return c.valid(r) && c.read(r)[0] != 0; }
};

// ------------------------------------------------------------------------------------------------ the core in a tile
// The block `core` (6): the Rv32 with IMEM, DMEM and the boot ROM, register_bus (5), and the core's side of the units
// whose registers only the core reaches: the write unit (9: WRITE_HDR, WRITE_DEST_ADDR, WRITE_W0-W2 and the sends, in
// front of the inject unit's holding register), the read unit's registers (10) and the request unit's REQUEST_OUT_W0-W2
// (11.1). Its memories live outside the unit (`dmem`, `imem` in Rv32: a unit is moved into an arena at build); the
// director's writes (the NoC port, 6.3) are applied before its step.
struct TileCore {
    sim::RegFileIn regs, hist, tview, ptrv;   // the tile window; HIST; STREAM and GROUP; PTR (table_store.h)
    sim::WireIn wake, irq;
    sim::LinkIn port_b;                // width 3: packets from the director, Link(WRITE_PORT_LATENCY + 1)
    sim::LinkOut bus, side, canon;     // -> the tile control unit: Link(1), width 2; Link(1), width 1; Link(2), width 1
    sim::HoldOut send;                 // width 3: the write unit's packet -> the inject unit's holding register
    sim::LinkOut tables, hist_w;       // width 2: table stores -> the table store, HIST (Link(1))
    sim::FifoOut rd_port;              // width 2, the read unit's reads -> the read queue (client 2)
    sim::CountIn rp_injected;          // read-unit returns injected (free entries: injections before t)
    sim::CountOut rp_stores;           // read-unit reads queued -> the director (results outstanding)
    sim::RegFileIn win;                // the peripheral slot's window RegFile (window loads read it)
    sim::LinkOut periph;               // width 1, a window store or a consuming EVENTS load -> the slot, Link(1)
    bool slot = false;                 // the two above declared
    // the request unit: the send side (every tile) and a receiving tile's receive side
    sim::LinkOut req_push;             // width 2: REQUEST_SEND's request {w0 | w1 << 32, w2 | w3 << 32} -> the tree, Link(1)
    sim::WireIn req_freed;             // the tree took the queue's head at t (1 during t)
    sim::RegFileIn recv;               // a receiver: its queue's head {pending, w0-w3} as a load at t sees it
    sim::LinkOut recv_pop, recv_credit;   // a receiver's REQUEST_RECEIVE: the pop (Link(1)) and the root's credit
    bool receiver = false;
    Rv32 rv;
    uint32_t tile = 0;                 // the tile's id: mhartid, the read unit's dst
    bus::Straps straps;                // register_bus's: the peripherals present, and their DMAs
    // ---- the write unit (9): its registers, and the packets on their way to the holding register ----
    uint32_t write_hdr = 0, write_dest = 0, write_w[3] = {};
    // A send made in cycle m (the header) builds its packet and puts it on `send` in m (visible to the inject unit at
    // m + 1: 14.3's "injected in t + 2 at the earliest", t the cycle the store is raised in). The Hold is the holding
    // register: a put it refuses (full, and not injected that cycle) is put again. Up to two packets wait here, the one
    // being put and the next send's; a send with both places taken is not taken, and stalls (PENDING-v1.36.md: the
    // RTL's write unit stalls a send only while the holding register is full and not injected that cycle).
    uint64_t sq[2][3] = {};
    int64_t sq_put[2] = {-1, -1};      // the cycle of the packet's last put; -1 not put yet
    uint32_t sq_n = 0;
    // ---- the read unit (10) ----
    uint32_t read_hdr = 0, read_dest = 0;
    unsigned rp_depth = 64;
    uint64_t rp_stored = 0, rp_dropped = 0, rp_violations = 0, rp_peak = 0;
    // ---- the request unit (11.1) ----
    uint32_t req_out_w[3] = {};
    uint32_t req_n = 0, req_depth = 4; // the request queue's count as the core's stall sees it; REQUEST_QUEUE_DEPTH
    bool canon_put = false;            // core_status.canonical_wait's level as last put on `canon`
    // ---- statistics ----
    uint64_t sends = 0, stall_send = 0, stall_request = 0, reads = 0;
    // (harness-only): the coreless test machine (tests/mach.h, MachConfig::coreless): the core never executes; the NoC
    // port's writes still land in its memories. Set by the harness only, before the first cycle.
    bool coreless = false;

    TileCore(sim::Ports& p, const Rv32& initial, unsigned tile_ = 0, unsigned read_port_depth = 64, bool slot_ = false,
             bool receiver_ = false, bus::Straps straps_ = {}, unsigned request_queue_depth = 4)
        : slot(slot_), receiver(receiver_), rv(initial), tile(tile_), straps(straps_), rp_depth(read_port_depth),
          req_depth(request_queue_depth) {
        if (slot) { p.in(win, "win"); p.out(periph, "periph", 1); }
        p.out(req_push, "req_push", 2); p.in(req_freed, "req_freed");
        if (receiver) { p.in(recv, "recv"); p.out(recv_pop, "recv_pop", 0); p.out(recv_credit, "recv_credit", 1); }
        p.in(regs, "regs"); p.in(hist, "hist"); p.in(tview, "tview"); p.in(ptrv, "ptrv");
        p.in(wake, "wake"); p.in(irq, "irq"); p.in(port_b, "port_b", 3);
        p.out(bus, "bus", 2); p.out(side, "side", 1); p.out(canon, "canon", 1); p.out(send, "send", 3); p.out(tables, "tables", 2); p.out(hist_w, "hist_w", 2);
        p.out(rd_port, "rd_port", 2); p.in(rp_injected, "rp_injected"); p.out(rp_stores, "rp_stores");
    }

    struct Env {
        sim::Ctx& c; TileCore& u;
        // ---- the core's inputs, and its status pulse of this cycle (6.2, 7.4) ----
        bool meip() const { return c.read(u.wake) != 0; }
        bool nmi() const { return c.read(u.irq) != 0; }
        void core_status(uint32_t bit) { c.put(u.bus, sim::Word(BUS_STATUS) << 32 | bit, sim::Word(0)); }

        // ---- register_bus (5): what an access to region 0xFF is, as it is raised. Every access fault (5.1, 5.3) is
        // found here, from the address, the straps and the peripheral's map alone, and reaches no owner ----
        BusKind bus_kind(uint32_t a, bool store, bool whole) const {
            const bus::Decoded d = bus::decode(u.straps, a, store, whole);
            switch (d.target) {
            case bus::Target::FAULT: return BusKind::fault;
            case bus::Target::ZERO: return store ? BusKind::fault : BusKind::window;   // a load that reads 0, as slowly
            case bus::Target::TABLE: return BusKind::table;
            case bus::Target::TILE:                            // the stores that may stall (9, 11.1)
                return store && ((d.word >= WRITE_SEND_32 && d.word <= WRITE_SEND_128) || d.word == REQUEST_SEND)
                    ? BusKind::window_stalls : BusKind::window;
            case bus::Target::PERIPH_CONTROL: need_slot(d.n); return BusKind::window;
            default:                                           // a peripheral's own word
                need_slot(d.n);
                if (!own_bit(d.n, Win::R_DEFINED, d.word)) return BusKind::fault;            // undefined: unmapped (5.3)
                if (store && !own_bit(d.n, Win::R_WRITABLE, d.word)) return BusKind::fault;  // an R word
                // the stores that may stall: the peripheral type's static list (the RTL's register_bus, own_stalling:
                // Ingress/Egress's UART_TX, STREAM_SEND and STREAM_SEND_LAST), whether or not the word stalls now
                return store && own_bit(d.n, Win::R_MAYSTALL, d.word) ? BusKind::window_stalls : BusKind::window;
            }
        }
        // ---- a load made in t (a fault never is): the value at the end of t - 1, the view at t ----
        uint32_t bus_load(uint64_t, uint32_t a) {
            const bus::Decoded d = bus::decode(u.straps, a, false, true);
            switch (d.target) {
            case bus::Target::TABLE: {
                const TableAddr ta = table_addr(a);
                if (ta.table == TBL_HIST) { engine_idle(c.read(u.hist, HIST_RMW_AT), "HIST"); return c.read(u.hist, ta.index & 255); }
                if (ta.table == TBL_PTR) { engine_idle(c.read(u.ptrv, PTR_RMW_AT), "PTR"); return c.read(u.ptrv, ta.index & 255); }
                return c.read(u.tview, table_view_index(ta.table, ta.index));
            }
            case bus::Target::TILE: return tile_load(d.word);
            case bus::Target::PERIPH_CONTROL: return periph_control_load(d.n, d.word);
            case bus::Target::PERIPH_OWN: return periph_own_load(d.n, d.word);
            default: return 0;                                 // a W word, a DMA-less peripheral's block 1
            }
        }
        uint32_t tile_load(unsigned w) {
            switch (w) {
            case EVENTS: {                                     // consumes what it returned under the mask (7.2)
                const uint32_t v = c.read(u.regs, EVENTS);
                if (v & c.read(u.regs, EVENTS_MASK) & ~EVENTS_LIVE) c.put(u.side, sim::Word(SIDE_EVENTS) << 32);
                return v;
            }
            case WRITE_HDR: return u.write_hdr;
            case WRITE_DEST_ADDR: return u.write_dest;
            case READ_HDR: return u.read_hdr;
            case READ_DEST_ADDR: return u.read_dest;
            case READ_FREE: return rp_free() & READ_FREE_MASK;
            case REQUEST_IN_W0: case REQUEST_IN_W1: case REQUEST_IN_W2:   // an empty queue, or none, reads 0 (11.1)
                return u.receiver ? c.read(u.recv, reqbus::ReqRecv::R_W0 + (w - REQUEST_IN_W0)) : 0;
            case REQUEST_RECEIVE: {                            // pops; an empty queue returns 0 and sets receive_underflow
                if (!u.receiver || c.read(u.recv, reqbus::ReqRecv::R_PENDING) == 0) {
                    c.put(u.side, sim::Word(SIDE_ERROR) << 32 | uint32_t(ERR_RECEIVE_UNDERFLOW));
                    return 0;
                }
                c.put(u.recv_pop);
                c.put(u.recv_credit, sim::Word(c.now()));
                return c.read(u.recv, reqbus::ReqRecv::R_W3);
            }
            default: return c.read(u.regs, w);                 // the tile control unit's view (block 0, and the units')
            }
        }
        // ---- a store made in t (a fault never is), in effect at the end of t; false: one that may stall, which its
        // owner does not take in t ----
        bool bus_store(uint64_t, uint32_t a, uint32_t v) {
            const bus::Decoded d = bus::decode(u.straps, a, true, true);
            switch (d.target) {
            case bus::Target::TABLE: {
                const TableAddr ta = table_addr(a);
                const uint32_t val = v & table_mask(ta.table, ta.index);
                if (ta.table == TBL_HIST) c.put(u.hist_w, sim::Word(ta.index), sim::Word(val));
                else c.put(u.tables, sim::Word(ta.table) << 32 | ta.index, sim::Word(val));
                return true;
            }
            case bus::Target::TILE: return tile_store(d.word, v);
            case bus::Target::PERIPH_CONTROL: return periph_control_store(d.n, d.word, v);
            case bus::Target::PERIPH_OWN: return periph_own_store(d.n, d.word, v);
            default: FATAL("tile core: a store to 0x%x made, which register_bus refuses", a);
            }
        }
        bool tile_store(unsigned w, uint32_t v) {
            switch (w) {
            // the write unit (9): the size is the send's, ignored on a store and read 0 (5.4)
            case WRITE_HDR: u.write_hdr = v & (0xFCu | HDR_DST_MASK << HDR_DST_LO | WRITE_HDR_NO_INCREMENT); return true;
            case WRITE_DEST_ADDR: u.write_dest = v; return true;
            case WRITE_W0: case WRITE_W1: case WRITE_W2: u.write_w[w - WRITE_W0] = v; return true;
            case WRITE_SEND_32: case WRITE_SEND_64: case WRITE_SEND_128: return write_send(w - WRITE_SEND_32, v);
            // the read unit (10): the mode and dst are the unit's
            case READ_HDR: u.read_hdr = v & 0x1F; return true;
            case READ_DEST_ADDR: u.read_dest = v; return true;
            case READ_SEND: read_send(v & WORD_ADDRESS_MASK); return true;
            // the request unit (11.1)
            case REQUEST_OUT_W0: case REQUEST_OUT_W1: case REQUEST_OUT_W2: u.req_out_w[w - REQUEST_OUT_W0] = v; return true;
            case REQUEST_SEND:
                if (u.req_n >= u.req_depth) { u.stall_request++; return false; }   // full, and no entry left this cycle
                c.put(u.req_push, sim::Word(u.req_out_w[0]) | sim::Word(u.req_out_w[1]) << 32,
                      sim::Word(u.req_out_w[2]) | sim::Word(v) << 32);
                u.req_out_w[0] = u.req_out_w[1] = u.req_out_w[2] = 0;
                u.req_n++;
                break;                                         // and the tile control unit's count (STATUS.request_full)
            default: break;
            }
            c.put(u.bus, sim::Word(BUS_STORE) << 32 | w, sim::Word(v));
            return true;
        }
        // A send (9): the packet from the registers as they stand, the top word above WRITE_W0-W2. A WRITE advances
        // WRITE_DEST_ADDR by the size unless no_increment; a packet of any other mode carries address 0 (4.1); W0-W2
        // clear.
        bool write_send(unsigned size, uint32_t v) {
            if (u.sq_n == 2) { u.stall_send++; return false; }
            const uint32_t h = u.write_hdr, mode = hdr_mode(h);
            const uint32_t w0 = u.write_w[0], w1 = u.write_w[1], w2 = u.write_w[2];
            uint64_t* q = u.sq[u.sq_n];
            q[0] = size == lw::SIZE_32 ? uint64_t(v) : size == lw::SIZE_64 ? uint64_t(v) << 32 | w0 : uint64_t(w1) << 32 | w0;
            q[1] = lw::hdr(0, hdr_dst(h), mode, hdr_counter(h), mode == lw::M_WRITE ? u.write_dest : 0, size);
            q[2] = size == lw::SIZE_128 ? uint64_t(v) << 32 | w2 : 0;
            u.sq_put[u.sq_n++] = -1;
            if (mode == lw::M_WRITE && !(h & WRITE_HDR_NO_INCREMENT)) u.write_dest += 4u << size;
            u.write_w[0] = u.write_w[1] = u.write_w[2] = 0;
            u.sends++;
            return true;
        }
        // READ_FREE (10): entries free at the start of t (the reads queued so far; returns injected before t)
        uint32_t rp_free() const { return uint32_t(int64_t(u.rp_depth) - int64_t(u.rp_stored - c.through_last(u.rp_injected))); }
        // READ_SEND (10): queues a read of READ_HDR's size from word address `chunk`, to land at READ_DEST_ADDR, counted
        // on READ_HDR's counter; then READ_DEST_ADDR advances by the size, queued or not. A full queue drops the read
        // and sets ERRORS.read_overflow.
        void read_send(uint32_t chunk) {
            const unsigned size = hdr_size(u.read_hdr), ctr = hdr_counter(u.read_hdr);
            const uint32_t dest = u.read_dest;
            u.read_dest += size == lw::SIZE_UNDEF ? 0 : 4u << size;
            namespace ck = sim::check;
            const int tile = u.rv.ck_tile;
            if (size == lw::SIZE_UNDEF) { u.rp_violations++; if (tile >= 0) ck::record(c.now(), tile, ck::Kind::RP_SIZE_UNDEF); }
            if (int32_t(rp_free()) <= 0) {
                u.rp_dropped++;
                if (tile >= 0) ck::record(c.now(), tile, ck::Kind::RP_QUEUE_FULL, chunk);
                c.put(u.bus, sim::Word(BUS_STATUS) << 32 | uint32_t(ERR_READ_OVERFLOW), sim::Word(0));
                return;
            }
            if (!lw::aligned(dest, size)) {                    // UB (1.3): its result is a misaligned packet
                u.rp_violations++;
                if (tile >= 0) ck::record(c.now(), tile, ck::Kind::RP_DEST_ALIGN, dest);
            }
            c.put(u.rd_port, mem::req_a(chunk, size, lw::M_WRITE, ctr, u.tile), mem::req_b(dest));
            u.rp_stored++; u.reads++;
            c.inc(u.rp_stores);
            if (const uint64_t used = u.rp_stored - c.through_last(u.rp_injected); used > u.rp_peak) u.rp_peak = used;
        }
        // PTR and HIST (12.5): the core shares its engine's two ports, and in a cycle the engine reads the table the
        // engine wins: the load's value is undefined. A program uses PTR or HIST only while its engine is idle
        // (EVENTS.counter_n_done says so), so this is a program's error: stop, as for every undefined behaviour.
        void engine_idle(uint32_t rmw_at, const char* table) const {
            if (rmw_at == uint32_t(c.now() + 1))
                FATAL("tile %u, cycle %llu: a load of %s in a cycle its engine reads it: the value is undefined "
                      "(machine-spec 12.5: a program uses PTR and HIST only while their engines are idle)",
                      u.tile, (unsigned long long)c.now(), table);
        }

        // ---- a peripheral's window (17.1), the peripheral slot's (periph_slot.h): its control and DMA blocks, and
        // the peripheral's own words 32-127 ----
        void need_slot(unsigned n) const { if (!u.slot) FATAL("tile core: peripheral %u's window: the tile has no peripheral slot", n); }
        uint32_t periph_control_load(unsigned n, unsigned w) {
            need_slot(n);
            const uint32_t v = c.read(u.win, Win::reg(n, Win::R_STD + Win::std(w)));
            if (w == P_EVENTS) {                               // a consuming load: under the peripheral's mask (17.2)
                const uint32_t m = c.read(u.win, Win::reg(n, Win::R_STD + P_EVENTS_MASK));
                if (v & m) c.put(u.periph, CoreOp::consume(n, v & m));
            }
            return v;
        }
        bool own_bit(unsigned n, unsigned base, unsigned w) const {
            const unsigned k = w - Win::OWN0;
            return c.read(u.win, Win::reg(n, base + k / 32)) >> (k % 32) & 1;
        }
        uint32_t periph_own_load(unsigned n, unsigned w) { return c.read(u.win, Win::reg(n, Win::R_OWN + (w - Win::OWN0))); }
        // A window store made in t: the slot applies it at the end of t (its `core` port, a Link(1)).
        bool periph_control_store(unsigned n, unsigned w, uint32_t v) { c.put(u.periph, CoreOp::store(n, w, v)); return true; }
        bool periph_own_store(unsigned n, unsigned w, uint32_t v) {
            if (own_bit(n, Win::R_STALL, w)) return false;     // a store that stalls (17.6's UART_TX and the like)
            c.put(u.periph, CoreOp::store(n, w, v));
            return true;
        }
    };

    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        if (c.ready(port_b)) { apply_port_b(c.peek(port_b)); c.take(port_b); }
        if (c.read(req_freed) != 0 && req_n > 0) req_n--;        // the tree took the queue's head in this cycle
        // the write unit's packets: the one put at t - 1 was taken into the holding register, or refused and put again;
        // then the next, and a send made in this cycle
        if (sq_n && sq_put[0] == int64_t(t) - 1) {
            if (c.refused(send)) { c.put(send, sq[0][0], sq[0][1], sq[0][2]); sq_put[0] = int64_t(t); }
            else { sq[0][0] = sq[1][0]; sq[0][1] = sq[1][1]; sq[0][2] = sq[1][2]; sq_put[0] = sq_put[1]; sq_n--; }
        }
        if (coreless) return sim::Wake::none();                  // (harness-only)
        Env e{c, *this};
        uint64_t next = rv.step(e, t);
        if (sq_n && sq_put[0] < 0) { c.put(send, sq[0][0], sq[0][1], sq[0][2]); sq_put[0] = int64_t(t); }
        if (rv.core_status_canonical_wait != canon_put) {         // its level in t + 1, for the edge that ends t + 1
            canon_put = rv.core_status_canonical_wait;
            c.put(canon, sim::Word(canon_put));
        }
        if (sq_n && t + 1 < next) next = t + 1;
        return next == Rv32::kNever ? sim::Wake::none() : sim::Wake::at_cycle(next);
    }
    uint64_t digest() const {
        uint64_t h = rv.digest();
        const uint64_t w[] = {write_hdr, write_dest, write_w[0], write_w[1], write_w[2], sq_n, read_hdr, read_dest, rp_stored,
                              rp_dropped, req_out_w[0], req_out_w[1], req_out_w[2], req_n, canon_put};
        for (uint64_t v : w) h = digest_mix(h, v);
        for (uint32_t i = 0; i < sq_n; i++) h = digest_mix(digest_mix(digest_mix(h, sq[i][0]), sq[i][1]), sq[i][2]);
        return h;
    }
    // The NoC port (6.3): one naturally aligned 32-, 64- or 128-bit write into DMEM or IMEM at its offset.
    void apply_port_b(const sim::Word* w) {
        uint64_t h = w[1];
        uint32_t a = lw::addr(h), n = lw::chunks(lw::size(h));
        uint32_t* m = lw::region(a) == lw::R_DMEM ? rv.dmem : rv.imem;
        uint32_t w0 = (lw::offset(a) & (MEM_BYTES - 1)) >> 2;
        for (uint32_t i = 0; i < n; i++) m[(w0 + i) & (MEM_WORDS - 1)] = uint32_t((i < 2 ? w[0] : w[2]) >> (32 * (i & 1)));
    }
};

// ------------------------------------------------------------------------------------- the tile builder's control part
// The configuration a tile's control units depend on.
struct G1Config {
    unsigned tile = 0;
    Rv32Timing timing;
    // The straps (2.2, 7.1), for a compute tile; `straps_of` (below) gives each unit's.
    uint32_t periph_present = 1;       // PARAMS.peripherals
    bool periph_slot = false;          // the peripheral slot (the replayer as peripheral 1) and its paths
    PeriphType periph1 = PT_REPLAYER;  // a compute tile's peripheral 1 in the slot (tests: PT_TEST)
    unsigned replayer_log_entries = 256;   // the replayer's log queues
    int test_periph_tile = -1;         // (harness-only): >= 0: this compute tile's peripheral 1 is the test peripheral
    int periph_only = -1;              // (harness-only): >= 0: only this compute tile has peripheral 1
    uint32_t mem_chunks = 1u << 20;    // the scratchpad's words: SCRATCHPAD_BYTES / 4 (4 MiB: 19)
    unsigned K = 8;                    // LAND_LATENCY (12.3): the counters' delay line
    unsigned cm_latency = 2;           // WRITE_PORT_LATENCY (12.3): the director's writes into DMEM and IMEM
    unsigned request_queue_depth = 4;  // REQUEST_QUEUE_DEPTH (19)
    // the memory path's keys
    unsigned ts_engine_latency = 1;    // TABLE_LATENCY: the engines' Links to and from the table store and HIST
    unsigned ag_lookahead = 0;         // the AG's read-ahead rings; 0: READ_AHEAD = 2 TABLE_LATENCY + 2 (8.5)
    unsigned read_latency = 3;         // READ_LATENCY (15, 19): the RTL's scratchpad's
    unsigned read_port_depth = 64;     // READ_QUEUE_DEPTH (10)
    bool sram_running_sum = true;      // D31: BigSram keeps its fingerprint sum per store; false in timing runs
};

// A tile's control part, as the tile builder meets it: chip.h's `ControlIO`, returned by a `ControlBuilder`.
// The units added for unit k (names "t<k>.core", "t<k>.ctl", "t<k>.dir", "t<k>.we", "t<k>.ag", "t<k>.gather", "t<k>.ts",
// "t<k>.hist", "t<k>.sram", and "t<k>.slot" with the peripheral slot), wired to each other inside. `ctx` is a G1Ctx.
struct G1Ctx {
    G1Config cfg;                      // per tile: cfg.tile is overwritten with k
    const uint32_t* bootrom = nullptr; uint32_t bootrom_words = 0;
    uint32_t bootrom_handler = 0;      // the ROM's handler, mtvec's reset value (6.2): a byte offset in the ROM
    uint32_t* mem = nullptr;           // units x 2 x MEM_WORDS words (DMEM, IMEM per unit); outlives the machine (the boot
                                       // fills them through the NoC port; a boot_local tile's are placed before the run)
    unsigned units = 64;               // the units `mem` holds: 64 compute tiles, or 67 with the special tiles as 64-66
    unsigned gather_depth = 64;        // GATHER_QUEUE_DEPTH = TOKEN_COUNT (13)
    SramBlocks* sram = nullptr;        // the tiles' scratchpads (one per tile, mapped at configuration); outlives the machine
    // The straps of unit k (2.1, 7.1), when the builder's default (a compute tile's: peripheral 1, no receive side, the
    // barrier wired, boot_local 0, its attach bits) is not it: null, or a function of k.
    Straps (*straps)(unsigned k, const G1Config& cfg) = nullptr;
};
// A compute tile's straps (2.2; base_tile's): its id, its peripherals, no receive side, the special tiles attached at
// it, the barrier wired, boot_local 0. A special tile's (units 64-66): its own attach bit, a receive side for its
// target, no barrier, and boot_local 1 on the master and memory tiles (6.5).
Straps default_straps(unsigned k, uint32_t periph_present);
ControlIO add_g1(sim::Config& cfg, unsigned k, void* ctx);
static_assert(std::is_same_v<decltype(&add_g1), ControlBuilder>);

}  // namespace units
