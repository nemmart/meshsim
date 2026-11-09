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

namespace units {

// W8's part of a tile (the register block, the director, port B, the core) as the network meets it.
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
};
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
    sim::HoldIn from_cnc;
    sim::FifoOut to_cnc;
};

struct ChipSpec {
    NetConfig net;
    const Hops* hops = nullptr;               // outlives the machine
    const CncProgram* program = nullptr;      // C&C 1's program; outlives the machine
    ControlBuilder control = nullptr;         // W8's part of each tile (null: a stand-in that sinks the eject bus and
    void* control_ctx = nullptr;              //  never sends, never READY: the network-only checks)
    const uint8_t* domain = nullptr;          // 64 entries (null: one domain)
};

TileIO tile_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned k);
void build_chip(sim::Config& cfg, const ChipSpec& spec);

// Domains by bands of rows: `parts` bands (1, 2, 4 or 8), rows split evenly. W10's partitioner may replace it.
void row_bands(unsigned parts, uint8_t domain[64]);

// Unit names (for observers and the fingerprint): "t<k>.router", "t<k>.inj", "cnc", "row<r>", "ep2", "ep3".
std::string tile_unit(unsigned k, const char* unit);

}  // namespace units
