// chip.h (WORKER9, G2): the chip's assembly. A tile is built by `tile_cycle` (config.h's MIXED FIDELITY: a function that
// adds the tile's units, wires them inside and returns its external ports); `build_chip` adds 64 tiles, the mesh links,
// C&C 1's model, the row collectors and C&C 2-3's endpoints, and places everything in thread domains (form 2, W4).
// Declarations; the definitions come with stage 4.
//
// THE OTHER GROUPS' PARTS OF A TILE. The tile builder adds the network's units itself and asks each group's builder for
// its part through a plain function, which adds that group's units, wires them inside, and returns the ports the network
// meets (the ports agreed with W8, 2026-11-07, NOTES.md). G3 and G4 will add their parts the same way.
//
// THE PARTITION (form 2). Every unit of tile k goes in `domain[k]`; C&C, the collectors and the request bus's stub go in
// tile 0's domain (C&C's send is a Hold into tile 0's router: it must not cross), endpoint k + 1 in tile k's (k = 1, 2:
// their receive FIFOs). Then every crossing is a mesh link (link_latency 3), a GO link (go_latency + skew + 1) or a line
// link (ready_latency - 2), so the barrier interval is 3: `build_chip`'s caller checks `Sim::partition()`.
#pragma once

#include <cstdint>
#include <string>

#include "config.h"
#include "sim.h"
#include "units/cnc.h"
#include "units/mem.h"
#include "units/net.h"
#include "units/reqbus.h"

namespace units {

// W8's part of a tile (the register block, the director, port B, the core) as the network meets it.
// W14 (D34, the request bus; NOTES.md section 14): a sender's port as its tile's control offers it. `present`: the
// control has a request port (W8's; the network-only stand-in has none). `recv`: a controller's receive side.
struct ReqIO {
    bool present = false;
    sim::LinkOut push;            // the core's REQUEST_SEND: Link(1), width 2 -> the sender's leaf
    sim::WireIn freed;            // the leaf's `freed` -> the core (canPush)
    sim::LinkIn freed_ev;         // the leaf's `freed_ev` -> the register block (Link(1), width 0)
    bool recv = false;
    sim::RegFileIn head;          // the target's queue -> the core (RegFile 5)
    sim::LinkOut pop;             // the core's REQUEST_RECEIVE -> the queue (Link(1), width 0)
    sim::LinkOut qcredit;         // the core's pop -> the root's credit (Link(1) or (3), width 1)
};
struct ControlIO {
    sim::HoldOut send;            // the core's send holding register -> the injection unit (3 words)
    sim::RegIn send_busy;         // the injection unit -> the register block (STATUS's send-busy, with the core's own put)
    sim::LinkIn eject;            // the router -> the director (3 words, intra_tile_latency)
    sim::LinkIn go;               // C&C -> the register block (width 0, go_latency + skew + 1)
    sim::LinkOut line;            // the register block -> the row collector (width 1, ready_latency - 2)
    sim::CountIn router_ovf;      // the router's FIFO overflows (ROUTER_OVF), read `through_last` at endCycle
    sim::LinkIn router_ovf_ev;    // D29: width 0, Link(1) from the router's `ovf_ev`, put in the step that counts an
                                  // overflow (t + 1, D28), so the block steps and its endCycle(t + 1) sets ERRORS [0]
    MemIO mem;                    // W11 (G3, approved 2026-11-08): the read queue's ports, which live in the injection
                                  // unit; tile_cycle wires them when `mem.present` (W12's clients by its flags)
    sim::CreditOut dma;           // W15: the peripheral slot's DMA output (AUDIT-M3 3.1 S6, width 3), C&C 1-3 only ->
                                  // the controller router's CC_DMA_IN, a credit Fifo of cc_dma_depth (4); set when
    bool has_dma = false;         // the builder declared it (the slot's PeriphSlotConfig::controller)
    ReqIO req;                    // W14 (D34): the request port's send side, and a controller's receive side
};
// W14's ReqIO, ControlIO's request side (sessions/WORKER14/NOTES.md section 14), wired by units::reqbus::wire_sender.
// ReqIO is defined before ControlIO (above).
// Adds tile `k`'s control units (names prefixed "t<k>."), wires them inside, returns the ports above. `ctx` is the
// builder's own (images, configuration).
using ControlBuilder = ControlIO (*)(sim::Config& cfg, unsigned k, void* ctx);

// The tile's external ports: what the chip wires.
struct TileIO {
    sim::LinkIn link_in[4];       // by arrival port (FROM_LEFT .. FROM_DOWN)
    sim::LinkOut link_out[4];     // by direction (TO_RIGHT .. TO_UP)
    sim::LinkIn go;
    sim::LinkOut line;
    bool attached = false;        // tiles 0-2: the fifth port
    sim::HoldIn from_cnc;         // M2: C&C's model and the endpoints
    sim::FifoOut to_cnc;
    ReqIO req;                    // W14: the control's request side (build_chip wires it to the bus)
    sim::LinkIn from_cc;          // M3 (cc_cores): the C&C links
    sim::LinkOut to_cc;
};

// A controller's external ports (M3, cc_cores: C&C 1-3 as units 64-66; AUDIT-M3 3.4).
struct CcIO {
    sim::LinkIn link_in;          // CC_LINK_IN <- tile c's to-controller output, Link(cc_link_latency)
    sim::LinkOut link_out;        // CC_LINK_OUT -> tile c's from-controller route block, Link(cc_link_latency)
    sim::FifoIn dma;              // CC_DMA_IN (a credit Fifo; with a DMA only): the slot's S6
    bool has_dma = false;
    sim::LinkIn go;               // the register block's GO (nothing drives a controller's GO in ref)
    sim::LinkOut line;            // the register block's line word: READY (ignored for controllers) and the error level
    ReqIO req;                    // W14: the controller's request port, send and receive sides (build_chip wires them)
};

struct ChipSpec {
    NetConfig net;
    const Hops* hops = nullptr;               // outlives the machine
    const CncProgram* program = nullptr;      // C&C 1's program; outlives the machine
    ControlBuilder control = nullptr;         // W8's part of each tile (null: a stand-in that sinks the eject bus and
    void* control_ctx = nullptr;              //  never sends, never READY: the network-only checks)
    const uint8_t* domain = nullptr;          // 64 entries (null: one domain)
    units::reqbus::ReqBusConfig reqbus;       // W14 (D34): the request bus; credit_latency set explicitly (3 the spec,
                                              // 0 exact as ref: one domain), never from the partition
    // M3 (WORKER16): C&C 1-3 as cores (ref's Config.ccCores; AUDIT-M3 3.4). Units 64-66 are built like tiles (`control`
    // with k = 64 + c, the controller's router), each in tile c's domain (the C&C link is a Link(1)); tiles 0-2's fifth
    // port is the C&C links; M2's Java model of C&C 1, its row collectors' consumer and the endpoints are not built.
    bool cc_cores = false;
    bool cc3_dma = false;                     // C&C 3's DMA input (ref's cc3Tracer)
    uint32_t host_capacity = 4096;            // each host port's queue (packets)
};

TileIO tile_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned k);
// Controller c (0-2) as unit 64 + c (spec.cc_cores): its control part, the controller's router, the injection unit,
// the host port ("cc<c + 1>.host"), in the current domain.
CcIO cc_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned c);
// The chip. With spec.cc_cores, the 64 tiles and C&C 1-3 as units 64-66 with their links; the chip lines (the row
// collectors into C&C 1's peripheral, GO out of it, the controllers' lines) are the slot's, wired with it (W15, W16).
void build_chip(sim::Config& cfg, const ChipSpec& spec);

// Domains by bands of rows: `parts` bands (1, 2, 4 or 8), rows split evenly. W10's partitioner may replace it.
void row_bands(unsigned parts, uint8_t domain[64]);

// Unit names (for observers and the fingerprint): "t<k>.router", "t<k>.inj", "cnc", "row<r>", "ep2", "ep3"; units
// 64-66 (C&C 1-3 as cores, M3) "cc1.router" ... "cc3.inj" (AUDIT-M3 6.9's names).
// "t<k>." for 0-63, "cc<k - 63>." for 64-66 (inline: the control builders use it without the chip's objects)
inline std::string unit_prefix(unsigned k) { return k < 64 ? "t" + std::to_string(k) + "." : "cc" + std::to_string(k - 63) + "."; }
std::string tile_unit(unsigned k, const char* unit);

}  // namespace units
