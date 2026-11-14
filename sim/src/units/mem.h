// mem.h: the memory path's ports that other groups meet. DECLARATIONS ONLY: the agreed crossing between
// G3 (the SRAM read queue in the injection unit, the big SRAM, the write engine, the table store and HIST) and G4
// the address generator, the GATHER engine), and the words they carry (AUDIT-M2 3.2, 3.5, 3.6; ref's SRAMReadQueue,
// TableStore, AddressGenerator, GatherEngine).
//
// THE CROSSING WITH G4 (both ends build to this list; a change goes through the integrator)
//
//   port              kind  from -> to                               words                         latency
//   gather read       Hold  gather `rd`   -> srq client 0            2: reqA, reqB (below)          (Hold) refused, retried t + 1
//   AG read           Hold  AG `rd`       -> srq client 1            2: reqA, reqB                  (Hold) refused, retried t + 1
//   gather srq busy   Reg   srq `gather_srq_busy` -> register block   1: the gather's Hold occupied at the end of t, OR
//                                                                       SRAMReadQueue.inFlightOf(GATHER) > 0 (one of
//                                                                       STATUS's three gather-busy inputs, ORed)
//   PTR view          RegFile  table store `ptr` -> the AG             256 registers: PTR as committed at the end of
//                                                                       t - 1 (ref's AG peeks before the store's
//                                                                       write-back of t; with_ag only)
//   STREAM request    Link  AG `stream_req` -> table store           3: op, index, addend (below)   ts_engine_latency
//   GROUP request     Link  AG `group_req`  -> table store           3                              ts_engine_latency
//   STREAM return     Link  table store -> AG `stream_ret`           3: TableStore.retStream's      ts_engine_latency
//   GROUP return      Link  table store -> AG `group_ret`            3: TableStore.retGroup's       ts_engine_latency
//
// The read queue serves its clients as ref does: priority gather (0), AG (1), read port (2), one issue a cycle while a
// token is free; a client's Hold is taken when its request issues (so a client sees `refused` when its previous request
// is still held at the end of the cycle it puts again: PORTS.md 5). Returns are packets through the injection point (no
// return port between G3 and G4). The read port (client 2) is the core's Fifo(read_port_depth), G3's own.
//
// Wiring: the units of both groups are added by the control builder (the add_g1); the crossing inside a tile is wired
// there, and the read queue's ports (in the injection unit) through `MemIO` below, which tile_cycle connects.
#pragma once

#include <cstdint>

#include "sim.h"

namespace units {
namespace mem {

// ---- read-queue clients (SRAMReadQueue.GATHER, AG, READPORT) ----
enum : unsigned { CLIENT_GATHER = 0, CLIENT_AG = 1, CLIENT_READPORT = 2, CLIENTS = 3 };
constexpr unsigned kReadReqWidth = 2;

// A client's request words (SRAMReadQueue.reqA/reqB): a = source chunk (21) | width (2) << 21 | mode (3) << 23 |
// counter (3) << 26 | dst (7) << 29; b = the outgoing packet's 32-bit address (mode 0; otherwise 0). Width is the size
// code: 0 32 bits, 1 64, 2 128, 3 undefined (a zero-width read: no access, payload 0).
inline uint64_t req_a(uint32_t src_chunk, uint32_t width, uint32_t mode, uint32_t counter, uint32_t dst) {
    return uint64_t(src_chunk & 0x1FFFFF) | uint64_t(width & 3) << 21 | uint64_t(mode & 7) << 23 |
           uint64_t(counter & 7) << 26 | uint64_t(dst & 0x7F) << 29;
}
inline uint64_t req_b(uint32_t dst_addr) { return dst_addr; }
inline uint32_t a_addr(uint64_t a) { return uint32_t(a & 0x1FFFFF); }
inline uint32_t a_width(uint64_t a) { return uint32_t(a >> 21 & 3); }
inline uint32_t a_mode(uint64_t a) { return uint32_t(a >> 23 & 7); }
inline uint32_t a_counter(uint64_t a) { return uint32_t(a >> 26 & 7); }
inline uint32_t a_dst(uint64_t a) { return uint32_t(a >> 29 & 0x7F); }

// ---- the table store's request and return words (TableStore) ----
// Request: a = op, b = index (STREAM + i, GROUP + 2g, PTR + i: index spaces, not addresses), c = addend.
// Returns: STREAM a = STREAM_SRC[i] | STREAM_DEST[i] << 32 (Fields.streamE0), b = STREAM_COUNT[i], c = the request's index;
//          GROUP  a = the request's index, b = the 64-bit entry, c = 0;
//          PTR    a = index (0-255), b = the old pointer, c = 0.
enum : uint32_t { OP_READ64 = 1, OP_ADD_RET = 3 };
enum : uint32_t { IDX_PTR = 0x100, IDX_STREAM = 0x200, IDX_GROUP = 0x400 };
constexpr unsigned kTsWidth = 3;

}  // namespace mem

// The read queue's ports as the control builder hands them to tile_cycle (it lives in the injection unit, the
// Injection::rs): the clients' outputs and the big SRAM's ends.
struct MemIO {
    bool present = false;              // false: a network-only tile (no read side)
    bool gather = false, ag = false;   // the clients present (their `rd` Holds declared and given below)
    unsigned read_port_depth = 64, sram_read_latency = 2;
    uint32_t mem_chunks = 1u << 20;
    sim::HoldOut rd_gather;            // the gather `rd` (client 0), width 2
    sim::HoldOut rd_ag;                // the AG `rd` (client 1), width 2
    sim::FifoOut rd_port;              // the core's read port (client 2), width 2, Fifo(read_port_depth)
    sim::HoldIn sram_rd;               // the big SRAM's read holding register, width 1 (BigSRAM.readReq)
    sim::LinkOut sram_ret[16];         // the big SRAM's area returns, width 3, Link(sram_read_latency)
    sim::RegIn gather_srq_busy;        // the register block: gather Hold occupied or a gather read in flight, width 1
    sim::CountIn rp_injected;          // the core: read-port returns injected (ReadPort.injectedAt), read through t - 1
};

}  // namespace units
