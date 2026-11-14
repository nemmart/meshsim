// chip.h: the chip's assembly (machine-spec 2.1, 2.3: "the tiles, the links between their routers, the request tree
// and the global nets"; the RTL's rtl/chip/src/chip.veryl). A tile is built by `tile_cycle` (config.h's MIXED FIDELITY: a
// function that adds the tile's units, wires them inside and returns its external ports), a special tile by
// `special_cycle`; `build_chip` adds the 64 compute tiles, the mesh links, the master, memory and tracer tiles on their
// attach links, the request tree and the nets, and places everything in thread domains.
//
// THE CHIP (build_chip)
//   - the compute tiles 0-63, each with the attach port of the special tile whose attach tile it is (0, 7, 15: 16.1);
//   - the links (16.1): Link(link_latency = 3) each way between neighbours on the ring and in the columns, and between
//     each special tile's router and its attach tile's: three register stages each, a hop of four with the route block;
//   - the master tile (unit 64, "cc1.*"), the memory tile (65, "cc2.*") and the tracer tile (66, "cc3.*"): the base tile
//     with its straps (tile_ctl.h's default_straps), its router a special tile's (router.h), its peripheral 1 in its slot:
//     Ingress/Egress, the RW engine, the Tracer (ingress_egress.h, rw_engine.h, tracer.h); the memory and tracer tiles'
//     peripherals' DMAs enter their routers (16.4);
//   - the request tree (11.3; reqbus.h): every tile's sending side, the row nodes and the root, each special tile's
//     receive queue with its credit;
//   - the global nets (18), at the chip's latencies (NetConfig: ARRIVE_LATENCY 0, RELEASE_LATENCY 1, ERROR_LATENCY 0, the
//     RTL chip's wires): each compute tile's BARRIER_ARRIVE and error line through its row's NetRow into the master
//     tile's Ingress/Egress, the memory and tracer tiles' error lines, the master tile's own (the toast pin), and
//     BARRIER_RELEASE from Ingress/Egress to every compute tile in the same cycle.
// The chip has no behavioural master: v1.35's model of C&C is a test bench's (tests/bench_master.h), which builds its
// machine from `add_mesh` and `add_request_tree`.
//
// THE NETS' LINKS. A tile control unit puts BARRIER_ARRIVE in the cycle its flip-flop is 1 and the error line's level in
// the first cycle of each level (tile_ctl.h). BARRIER_ARRIVE crosses a Link(1 + ARRIVE_LATENCY) into its row's NetRow,
// which drives the row's wire in the cycle the link delivers; Ingress/Egress, at a later level of that cycle, takes it
// as an arrival that its reads of that cycle see: ARRIVALS set at the end of the cycle the pulse reached it (17.6). An
// error line crosses a Link(1 + ERROR_LATENCY) the same way, so ERROR_LINES shows a level one cycle later than the RTL's
// wire does (PENDING-v1.36.md: a tile control unit learns its flip-flop's value in the cycle it drives it, and a Wire
// cannot cross thread domains). BARRIER_RELEASE is a Link(RELEASE_LATENCY) from Ingress/Egress to each tile.
//
// THE PARTITION. Every unit of tile k goes in `domain[k]`; a special tile's with its attach tile's; the request tree's
// nodes with tile 0's, each sender's leaf with its tile's, each receive queue with its special tile's; the NetRows with
// the master tile's (they drive Wires into its slot). The nets, the request tree's links and the special tiles' core
// links cross domains at latency 1, so the barrier interval of a split chip is 1 (threads.h).
#pragma once

#include <cstdint>
#include <string>

#include "config.h"
#include "sim.h"
#include "units/mem.h"
#include "units/net.h"
#include "units/reqbus.h"

namespace units {

struct PeriphSlot;                // periph_slot.h

// the part of a tile (the register block, the director, port B, the core) as the network meets it.
// (D34, the request bus): a sender's port as its tile's control offers it. `present`: the
// control has a request port (the; the network-only stand-in has none). `recv`: a special tile's receive side.
struct ReqIO {
    bool present = false;
    sim::LinkOut push;            // the core's REQUEST_SEND: Link(1), width 2 -> the sender's leaf
    sim::WireIn freed;            // the leaf's `freed` -> the core (canPush)
    sim::LinkIn freed_ev;         // the leaf's `freed_ev` -> the register block (Link(1), width 0)
    bool recv = false;
    sim::RegFileIn head;          // the target's queue -> the core (RegFile 5)
    sim::RegFileIn head_ctl;      // the same -> the tile control unit (EVENTS.request_pending's level, 11.1)
    sim::LinkOut pop;             // the core's REQUEST_RECEIVE -> the queue (Link(1), width 0)
    sim::LinkOut qcredit;         // the core's pop -> the root's credit (Link(1), width 1)
};
struct ControlIO {
    sim::HoldOut send;            // the core's send holding register -> the injection unit (3 words)
    sim::RegIn send_busy;         // the injection unit -> the register block (STATUS's send-busy, with the core's own put)
    bool has_send_taken = false;  // the one below is given (a control part with the core's write unit)
    sim::WireIn send_taken;       // the injection unit -> the write unit: its packets injected so far (inject.h)
    sim::LinkIn eject;            // the router -> the director (3 words, intra_tile_latency)
    bool barrier = false;         // the strap (7.1): the two below are declared
    sim::LinkIn release;          // BARRIER_RELEASE's pulse -> the tile control unit (width 0; 18)
    sim::LinkOut arrive;          // the tile control unit's BARRIER_ARRIVE (width 0; 18)
    sim::LinkOut error;           // the tile's error line's level, at each change (width 1; 18)
    sim::CountIn router_ovf;      // the router's FIFO overflows (ROUTER_OVF), read `through_last` at endCycle
    sim::LinkIn router_ovf_ev;    // D29: width 0, Link(1) from the router's `ovf_ev`, put in the step that counts an
                                  // overflow (t + 1, D28), so the block steps and its endCycle(t + 1) sets ERRORS [0]
    MemIO mem;                    // (G3, approved 2026-11-08): the read queue's ports, which live in the injection
                                  // unit; tile_cycle wires them when `mem.present` (the clients by its flags)
    sim::CreditOut dma;           // a special tile's peripheral's DMA (16.4, width 3) -> its router's DMA input, a credit
    bool has_dma = false;         // Fifo of cc_dma_depth; set when the builder declared it (PeriphSlotConfig::controller)
    ReqIO req;                    // the request unit's send side, and a special tile's receive side
    PeriphSlot* slot = nullptr;   // the slot unit as staged (configuration only: Ingress/Egress's own ports, wired by
                                  // build_chip); null without one
};
// the ReqIO, ControlIO's request side, wired by units::reqbus::wire_sender.
// Adds tile `k`'s control units (names prefixed "t<k>."), wires them inside, returns the ports above. `ctx` is the
// builder's own (images, configuration).
using ControlBuilder = ControlIO (*)(sim::Config& cfg, unsigned k, void* ctx);

// A compute tile's external ports: what the chip wires.
struct TileIO {
    sim::LinkIn link_in[4];       // by arrival port (FROM_LEFT .. FROM_DOWN)
    sim::LinkOut link_out[4];     // by direction (TO_RIGHT .. TO_UP)
    sim::LinkIn release;          // BARRIER_RELEASE (18)
    sim::LinkOut arrive, error;   // BARRIER_ARRIVE and the error line (18)
    bool attached = false;        // an attach tile (0, 7, 15: lw::attach_tile): one of the two pairs below
    sim::LinkIn attach_in;        // the attach port (16.1): from its special tile's router, and to it
    sim::LinkOut attach_out;
    sim::HoldIn from_bench;       // ChipSpec::bench_attach: a test bench's endpoint at the attach port
    sim::FifoOut to_bench;
    ReqIO req;                    // the control's request side (wired to the request tree)
};

// A special tile's external ports (units 64-66).
struct SpecialIO {
    sim::LinkIn attach_in;        // <- its attach tile's attach output
    sim::LinkOut attach_out;      // -> its attach tile's attach input
    sim::LinkOut error;           // its error line (a special tile never arrives: its barrier is not wired, 7.3)
    ReqIO req;                    // its request unit, the send and receive sides
    PeriphSlot* slot = nullptr;   // its slot as staged (the control's `io.slot`), or null
};

struct ChipSpec {
    NetConfig net;
    const Hops* hops = nullptr;               // outlives the machine
    ControlBuilder control = nullptr;         // the part of each tile (null: a stand-in that sinks the eject bus and
    void* control_ctx = nullptr;              //  never sends, never arrives: add_mesh for the network-only checks)
    const uint8_t* domain = nullptr;          // 64 entries (null: one domain)
    // The request tree (11.3). fifo_depth is REQUEST_QUEUE_DEPTH, the depth the cores stall at; hop_latency 1 (a cycle a
    // node) and credit_latency 1 (a sender sees its node's buffer as it stood at the start of the cycle) are the RTL's.
    units::reqbus::ReqBusConfig reqbus;
    bool bench_attach = false;                // (tests) the attach tiles' attach ports are a bench's endpoints (a Hold
                                              //   in, a Fifo out: tests/bench_master.h), not links to special tiles
};

// The nets of one mesh row (18), wires in the RTL: its eight tiles' BARRIER_ARRIVE pulses and error lines, bundled for
// the master tile's Ingress/Egress, whose unit could not hold 128 inputs (a unit declares at most 64). It adds no cycle:
// it drives its Wire in the cycle its links deliver, and Ingress/Egress reads it at a later level of that cycle.
struct NetRow {
    sim::LinkIn arrive[8], error[8];
    sim::WireOut out;             // [7:0] the tiles whose BARRIER_ARRIVE reaches Ingress/Egress in this cycle, [15:8] the
                                  // error lines' levels
    uint8_t levels = 0;
    NetRow(sim::Ports& p, unsigned) {
        for (unsigned i = 0; i < 8; i++) { p.in(arrive[i], "arrive", 0); p.in(error[i], "error", 1); }
        p.out(out, "out");
    }
    sim::Wake step(sim::Ctx& c) {
        unsigned a = 0;
        for (unsigned i = 0; i < 8; i++) {
            if (c.ready(arrive[i])) { c.take(arrive[i]); a |= 1u << i; }
            if (c.ready(error[i])) { levels = uint8_t((levels & ~(1u << i)) | (c.peek(error[i])[0] & 1) << i); c.take(error[i]); }
        }
        c.drive(out, sim::Word(a) | sim::Word(levels) << 8);
        return a ? c.next() : sim::Wake::none();          // the pulses' bits fall in the next cycle
    }
    uint64_t digest() const { return levels; }
};

TileIO tile_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned k);
// Special tile s (0 the master tile, 1 the memory tile, 2 the tracer tile) as unit 64 + s: its control part, a special
// tile's router, the injection unit, in the current domain.
SpecialIO special_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned s);
// The 64 compute tiles and the links between them (16.1).
void add_mesh(sim::Config& cfg, const ChipSpec& spec, TileIO io[64]);
// The request tree (11.3) for the senders whose control has a request unit: the tiles', and with `special` the three
// special tiles', which also get their receive queues.
void add_request_tree(sim::Config& cfg, const ChipSpec& spec, TileIO* io, SpecialIO* special);
// The chip (above).
void build_chip(sim::Config& cfg, const ChipSpec& spec);

// Domains by bands of rows: `parts` bands (1, 2, 4 or 8), rows split evenly. the partitioner may replace it.
void row_bands(unsigned parts, uint8_t domain[64]);

// Unit names (for observers and the fingerprint): "t<k>.router", "t<k>.inj", ...; the special tiles 64-66 "cc1.router"
// ... "cc3.inj" (v1.35's prefixes, which the harness and the fingerprints use); "nets.row<r>"; reqbus.h's.
// "t<k>." for 0-63, "cc<k - 63>." for 64-66 (inline: the control builders use it without the chip's objects)
inline std::string unit_prefix(unsigned k) { return k < 64 ? "t" + std::to_string(k) + "." : "cc" + std::to_string(k - 63) + "."; }
std::string tile_unit(unsigned k, const char* unit);

}  // namespace units
