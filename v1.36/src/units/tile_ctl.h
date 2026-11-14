// tile_ctl.h: the tile's control: the tile control unit as a framework unit (TileCtl, around regblock.h's state), the
// core with its memories, register_bus and the core's side of the write, read and request units (TileCore), and the
// fragment of the tile builder that adds a tile's control units and returns the handles the builder wires (add_g1).
// Sections are v1.36's machine-spec's (the RTL's rtl/tile/base/spec.md).
//
// Levels (from the wires): the tile control unit runs before the core in a cycle: at t it applies the edge that ends
// t - 1 (regblock.h's end_cycle(t - 1), from what reached it at t: the shifted end of cycle, PORTS.md 7), then publishes
// the state at the end of t - 1, which the core reads at t.
//
// THE REGISTER BUS'S TIMING (6.4; the RTL's register_bus). For an access the core issues in cycle t:
//   - a window load is presented to its owner in t + 1 and returns the state at the end of t: the core waits a cycle
//     and reads the published view at t + 1; its side effect (an EVENTS load's consumption, a REQUEST_RECEIVE's pop)
//     is at the end of t + 1: put on `side`, a Link(1), at t + 1;
//   - a window store takes effect at the end of t + 1: put on `bus`, a Link(2), at t, with the core's status pulses
//     (which reach ERRORS at the end of t + 1 too) and its canonical-wait level (core_status.canonical_wait, a cycle
//     late);
//   - a table load returns the state at the end of t - 1 (the tables' RegFiles at t), a table store takes effect at
//     the end of t (the table stores' `window`, a Link(1)).
// Stores run one a cycle and window loads one every other cycle.
//
// THE TILE CONTROL UNIT'S PORTS (TileCtl):
//   bus         Link(2), width 2   the core's window store, status pulse or canonical-wait level of t
//   side        Link(1), width 1   a load's side effect: a consuming EVENTS load; receive_underflow's pulse
//   release     Link, width 0      BARRIER_RELEASE's pulse (18), from the master tile's side
//   kline       Link(LAND_LATENCY + 1), width 1, from the director: a counter's decrement due (12.3)
//   bad, ovf, drift, gather_ovf_ev  width 0, Link(1): the errors' pulses of the receive unit, the router, the DMA unit
//               and the gather unit (7.4)
//   we_ovf, ts_ovf, hist_ovf        width 0, Link(1): overflows of v1.35's write-engine and table FIFOs, which v1.36's
//               hardware does not have (12.4, 12.5): fatal, named (PENDING-v1.36.md)
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
//   line        Link, width 1: the tile's nets to the master tile's side (18): bit 0 BARRIER_ARRIVE's pulse, bit 1 the
//               error line's level; put in a cycle BARRIER_ARRIVE pulses or the error line changes
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
    BUS_CANON = 3,       // arg: core_status.canonical_wait's level
    SIDE_EVENTS = 1,     // a consuming load of EVENTS presented this cycle
    SIDE_ERROR = 2,      // arg: an error's bit: receive_underflow (a REQUEST_RECEIVE of an empty queue)
};

// ------------------------------------------------------------------------------------------ the tile control unit
struct TileCtl {
    // ---- inputs ----
    sim::LinkIn bus, side, release, kline, bad, ovf;
    sim::LinkIn we_ovf, ts_ovf, hist_ovf;
    sim::CountIn router_ovf, we_in_ovf, gather_ovf;   // the overflow Counts (statistics: the events above carry them)
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
    sim::LinkOut line;
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
        p.in(we_ovf, "we_ovf", 0); p.in(ts_ovf, "ts_ovf", 0); p.in(hist_ovf, "hist_ovf", 0);
        if (g4) {
            p.in(gather_busy, "gather_busy", 1); p.in(gather_srq_busy, "gather_srq_busy", 1);
            p.in(gather_put, "gather_put", 0); p.in(gather_ovf_ev, "gather_ovf_ev", 0);
            p.in(ag_status, "ag_status", 1); p.in(drift, "drift", 0);
            p.out(ag_start, "ag_start");
        }
        p.in(bus, "bus", 2); p.in(side, "side", 1); p.in(release, "release", 0); p.in(kline, "kline", 1);
        p.in(req_freed_ev, "req_freed_ev", 0);
        p.in(send_busy, "send_busy", 1);
        p.in(bad, "bad", 0);
        p.in(router_ovf, "router_ovf"); p.in(we_in_ovf, "we_in_ovf"); p.in(gather_ovf, "gather_ovf");
        p.out(regs, "regs", TILE_REGS); p.out(dma, "dma", DMA_REGS); p.out(wake, "wake"); p.out(irq, "irq");
        p.out(line, "line", 1);
    }
    // Stepped when something reaches it (and at the first cycle): the edge that ends t - 1, from what reached it at t,
    // then the state at the end of t - 1 for t's readers.
    sim::Wake step(sim::Ctx& c) {
        RegBlockState::In in;
        const uint64_t t = c.now();
        if (c.ready(release)) { c.take(release); in.release = true; }
        if (c.ready(kline)) { in.decrement = uint32_t(c.peek(kline)[0]); c.take(kline); }
        if (c.ready(bad)) { c.take(bad); in.error_pulses |= 1u << ERR_BAD_PACKET; }
        if (c.ready(req_freed_ev)) { c.take(req_freed_ev); in.freed = true; }
        if (c.ready(ovf)) { c.take(ovf); in.error_pulses |= 1u << ERR_ROUTER_OVERFLOW; }
        // v1.35's FIFOs in front of the write engine, PTR and HIST: v1.36's write engine is a fixed pipeline that never
        // refuses, and its tables have no request FIFOs (12.4, 12.5). Until the simulator's engines are rebuilt, an
        // overflow there is a packet lost that the hardware would have written: stop.
        if (c.ready(we_ovf) || c.ready(ts_ovf) || c.ready(hist_ovf))
            FATAL("tile %u, cycle %llu: the simulator's write-engine or table request FIFO overflowed (v1.35's FIFOs: v1.36's "
                  "write engine never refuses, machine-spec 12.4, 12.5; PENDING-v1.36.md)", s.straps.tile_id, (unsigned long long)t);
        if (c.ready(bus)) {
            const sim::Word* m = c.peek(bus);
            const uint32_t op = uint32_t(m[0] >> 32), arg = uint32_t(m[0]);
            if (op == BUS_STORE) { in.store = true; in.word = arg; in.value = uint32_t(m[1]); }
            else if (op == BUS_STATUS) in.error_pulses |= 1u << arg;
            else if (op == BUS_CANON) s.canon = arg != 0;              // the level in the cycle this edge ends
            c.take(bus);
        }
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
        if (s.arrive_pulse || s.nmi() != error_out) {          // the nets (18), in the cycle after this edge
            c.put(line, sim::Word(s.arrive_pulse) | sim::Word(s.nmi()) << 1);
            error_out = s.nmi();
        }
        // The address generator's start is the block's until the generator's step: look again in the next cycle, when its
        // status says the transfer runs (STATUS.dma_busy, and an empty transfer's end: 8.2).
        return s.dma_pulse ? c.next() : sim::Wake::none();
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
    sim::LinkOut bus, side;            // -> the tile control unit: Link(2), width 2; Link(1), width 1
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
    // A send store at t builds its packet at t and puts it on `send` at t + 1 (visible to the inject unit at t + 2:
    // 14.3's "injected in t + 2 at the earliest"). The Hold is the holding register: a put it refuses (full, and not
    // injected that cycle) is put again. Up to two packets wait here, the one being put and the next send's; a send
    // with both places taken stalls.
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
    // ---- a window store on its way to the peripheral slot (it takes effect at the end of t + 1) ----
    uint64_t periph_op = 0;
    bool periph_pending = false;
    // ---- statistics ----
    uint64_t sends = 0, stall_send = 0, stall_request = 0, reads = 0;

    TileCore(sim::Ports& p, const Rv32& initial, unsigned tile_ = 0, unsigned read_port_depth = 64, bool slot_ = false,
             bool receiver_ = false, bus::Straps straps_ = {}, unsigned request_queue_depth = 4)
        : slot(slot_), receiver(receiver_), rv(initial), tile(tile_), straps(straps_), rp_depth(read_port_depth),
          req_depth(request_queue_depth) {
        if (slot) { p.in(win, "win"); p.out(periph, "periph", 1); }
        p.out(req_push, "req_push", 2); p.in(req_freed, "req_freed");
        if (receiver) { p.in(recv, "recv"); p.out(recv_pop, "recv_pop", 0); p.out(recv_credit, "recv_credit", 1); }
        p.in(regs, "regs"); p.in(hist, "hist"); p.in(tview, "tview"); p.in(ptrv, "ptrv");
        p.in(wake, "wake"); p.in(irq, "irq"); p.in(port_b, "port_b", 3);
        p.out(bus, "bus", 2); p.out(side, "side", 1); p.out(send, "send", 3); p.out(tables, "tables", 2); p.out(hist_w, "hist_w", 2);
        p.out(rd_port, "rd_port", 2); p.in(rp_injected, "rp_injected"); p.out(rp_stores, "rp_stores");
    }

    struct Env {
        sim::Ctx& c; TileCore& u;
        // ---- the core's inputs and its status (6.2) ----
        uint32_t hart_id() const { return u.tile; }
        bool meip() const { return c.read(u.wake) != 0; }
        bool nmi() const { return c.read(u.irq) != 0; }
        void core_status(uint32_t bit) { c.put(u.bus, sim::Word(BUS_STATUS) << 32 | bit, sim::Word(0)); }
        void canonical_wait(bool level) { c.put(u.bus, sim::Word(BUS_CANON) << 32 | uint32_t(level), sim::Word(0)); }

        // ---- register_bus (5): a load issued at t (a window's: in its second cycle, so the view is its sample point) ----
        int bus_load(uint64_t, uint32_t a, bool whole, uint32_t& v) {
            const bus::Decoded d = bus::decode(u.straps, a, false, whole);
            v = 0;
            switch (d.target) {
            case bus::Target::FAULT: return 1;
            case bus::Target::ZERO: return 0;
            case bus::Target::TABLE: {
                const TableAddr ta = table_addr(a);
                if (ta.table == TBL_HIST) v = hist(ta.index);
                else if (ta.table == TBL_PTR) v = c.read(u.ptrv, ta.index & 255);
                else v = c.read(u.tview, table_view_index(ta.table, ta.index));
                return 0;
            }
            case bus::Target::TILE: v = tile_load(d.word); return 0;
            case bus::Target::PERIPH_CONTROL: return periph_control_load(d.n, d.word, v);
            default: return periph_own_load(d.n, d.word, v);
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
        // ---- a store issued at t ----
        int bus_store(uint64_t t, uint32_t a, uint32_t v, bool whole) {
            const bus::Decoded d = bus::decode(u.straps, a, true, whole);
            switch (d.target) {
            case bus::Target::FAULT: case bus::Target::ZERO: return 1;
            case bus::Target::TABLE: {
                const TableAddr ta = table_addr(a);
                const uint32_t val = v & table_mask(ta.table, ta.index);
                if (ta.table == TBL_HIST) c.put(u.hist_w, sim::Word(ta.index), sim::Word(val));
                else c.put(u.tables, sim::Word(ta.table) << 32 | ta.index, sim::Word(val));
                return 0;
            }
            case bus::Target::TILE: return tile_store(t, d.word, v);
            case bus::Target::PERIPH_CONTROL: return periph_control_store(d.n, d.word, v);
            default: return periph_own_store(d.n, d.word, v);
            }
        }
        int tile_store(uint64_t t, unsigned w, uint32_t v) {
            switch (w) {
            // the write unit (9): the size is the send's, ignored on a store and read 0 (5.4)
            case WRITE_HDR: u.write_hdr = v & (0xFCu | HDR_DST_MASK << HDR_DST_LO | WRITE_HDR_NO_INCREMENT); return 0;
            case WRITE_DEST_ADDR: u.write_dest = v; return 0;
            case WRITE_W0: case WRITE_W1: case WRITE_W2: u.write_w[w - WRITE_W0] = v; return 0;
            case WRITE_SEND_32: case WRITE_SEND_64: case WRITE_SEND_128: return write_send(w - WRITE_SEND_32, v);
            // the read unit (10): the mode and dst are the unit's
            case READ_HDR: u.read_hdr = v & 0x1F; return 0;
            case READ_DEST_ADDR: u.read_dest = v; return 0;
            case READ_SEND: read_send(v & WORD_ADDRESS_MASK); return 0;
            // the request unit (11.1)
            case REQUEST_OUT_W0: case REQUEST_OUT_W1: case REQUEST_OUT_W2: u.req_out_w[w - REQUEST_OUT_W0] = v; return 0;
            case REQUEST_SEND:
                if (u.req_n >= u.req_depth) { u.stall_request++; return 2; }   // full, and no entry left this cycle
                c.put(u.req_push, sim::Word(u.req_out_w[0]) | sim::Word(u.req_out_w[1]) << 32,
                      sim::Word(u.req_out_w[2]) | sim::Word(v) << 32);
                u.req_out_w[0] = u.req_out_w[1] = u.req_out_w[2] = 0;
                u.req_n++;
                break;                                         // and the tile control unit's count (STATUS.request_full)
            default: break;
            }
            c.put(u.bus, sim::Word(BUS_STORE) << 32 | w, sim::Word(v));
            (void)t;
            return 0;
        }
        // A send (9): the packet from the registers as they stand, the top word above WRITE_W0-W2. A WRITE advances
        // WRITE_DEST_ADDR by the size unless no_increment; a packet of any other mode carries address 0 (4.1); W0-W2
        // clear.
        int write_send(unsigned size, uint32_t v) {
            if (u.sq_n == 2) { u.stall_send++; return 2; }
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
            return 0;
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
        // HIST (12.5): on a write-back cycle the engine wins, and the load returns the engine's value
        uint32_t hist(uint32_t i) const {
            if (c.read(u.hist, HIST_WB_AT) == uint32_t(c.now() + 1)) return c.read(u.hist, HIST_WB_VAL);
            return c.read(u.hist, i & 255);
        }

        // ---- a peripheral's window (17.1), on the simulator's peripheral slot, whose control block is still v1.35's
        // (periph_slot.h: 3 the start, 4 the DMA's start, 5 EVENTS, 6 EVENTS_PEEK, 7 EVENTS_MASK, 8 DMA_HDR, 9
        // DMA_DEST_ADDR): the bus presents v1.36's words and the slot's own words 32-127 as they are ----
        static unsigned slot_word(unsigned w) {
            switch (w) {
            case P_EVENTS: return 5;
            case P_EVENTS_PEEK: return 6;
            case P_EVENTS_MASK: return 7;
            case P_START: return 3;
            case P_DMA_HDR: return 8;
            case P_DMA_DEST_ADDR: return 9;
            case P_DMA_START: return 4;
            default: return w;                                 // PARAMS, STATUS, ERRORS
            }
        }
        void need_slot(unsigned n) const { if (!u.slot) FATAL("tile core: peripheral %u's window: the tile has no peripheral slot", n); }
        int periph_control_load(unsigned n, unsigned w, uint32_t& v) {
            need_slot(n);
            v = c.read(u.win, Win::reg(n, Win::R_STD + slot_word(w)));
            if (w == P_EVENTS) {                               // a consuming load: under the peripheral's mask (17.2)
                const uint32_t m = c.read(u.win, Win::reg(n, Win::R_STD + 7));
                if (v & m) c.put(u.periph, CoreOp::consume(n, v & m));
            }
            return 0;
        }
        bool own_bit(unsigned n, unsigned base, unsigned w) const {
            const unsigned k = w - Win::OWN0;
            return c.read(u.win, Win::reg(n, base + k / 32)) >> (k % 32) & 1;
        }
        int periph_own_load(unsigned n, unsigned w, uint32_t& v) {
            need_slot(n);
            if (!own_bit(n, Win::R_DEFINED, w)) return 1;      // a word the peripheral does not define: unmapped (5.3)
            v = c.read(u.win, Win::reg(n, Win::R_OWN + (w - Win::OWN0)));
            return 0;
        }
        int periph_put(unsigned n, unsigned w, uint32_t v) {   // the slot applies it at the end of t + 1
            if (u.periph_pending) FATAL("tile core: two window stores to the peripheral slot in one cycle");
            u.periph_op = CoreOp::store(n, w, v); u.periph_pending = true;
            return 0;
        }
        int periph_control_store(unsigned n, unsigned w, uint32_t v) { need_slot(n); return periph_put(n, slot_word(w), v); }
        int periph_own_store(unsigned n, unsigned w, uint32_t v) {
            need_slot(n);
            if (!own_bit(n, Win::R_DEFINED, w) || !own_bit(n, Win::R_WRITABLE, w)) return 1;   // unmapped, or an R word
            if (own_bit(n, Win::R_STALL, w)) return 2;         // a store that stalls (17.6's UART_TX and the like)
            return periph_put(n, w, v);
        }
    };

    sim::Wake step(sim::Ctx& c) {
        const uint64_t t = c.now();
        if (c.ready(port_b)) { apply_port_b(c.peek(port_b)); c.take(port_b); }
        if (c.read(req_freed) != 0 && req_n > 0) req_n--;        // the tree took the queue's head in this cycle
        if (periph_pending) { c.put(periph, periph_op); periph_pending = false; }   // the window store of t - 1
        // the write unit's packets: the one put at t - 1 was taken into the holding register, or refused and put again;
        // then the next
        if (sq_n && sq_put[0] == int64_t(t) - 1) {
            if (c.refused(send)) { c.put(send, sq[0][0], sq[0][1], sq[0][2]); sq_put[0] = int64_t(t); }
            else { sq[0][0] = sq[1][0]; sq[0][1] = sq[1][1]; sq[0][2] = sq[1][2]; sq_put[0] = sq_put[1]; sq_n--; }
        }
        if (sq_n && sq_put[0] < 0) { c.put(send, sq[0][0], sq[0][1], sq[0][2]); sq_put[0] = int64_t(t); }
        Env e{c, *this};
        uint64_t next = rv.step(e, t);
        if ((sq_n || periph_pending) && t + 1 < next) next = t + 1;
        return next == Rv32::kNever ? sim::Wake::none() : sim::Wake::at_cycle(next);
    }
    uint64_t digest() const {
        uint64_t h = rv.digest();
        const uint64_t w[] = {write_hdr, write_dest, write_w[0], write_w[1], write_w[2], sq_n, read_hdr, read_dest, rp_stored,
                              rp_dropped, req_out_w[0], req_out_w[1], req_out_w[2], req_n, periph_pending ? periph_op + 1 : 0};
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
    bool cc3_tracer = false;           // the tracer tile's peripheral 1 the Tracer, else the stub
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
    unsigned area_queue_depth = 8, sram_read_latency = 2, read_port_depth = 64;
    bool write_combining = false, fifo_count_before_pop = true;
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
    unsigned we_in_depth = 4, gather_depth = 64;   // v1.35's write-engine FIFO; GATHER_QUEUE_DEPTH = TOKEN_COUNT (13)
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
