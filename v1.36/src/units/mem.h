// mem.h: the read clients' request words and the ports between a tile's control part and its inject unit (machine-spec
// 14.1, 15). DECLARATIONS ONLY.
//
//   port              kind  from -> to                               words                         when
//   gather read       Hold  gather `rd`   -> the arbiter's client 0   2: reqA, reqB (below)          offered the cycle after its put
//   AG read           Hold  AG `rd`       -> the arbiter's client 1   2: reqA, reqB                  the same
//   read unit         Fifo  the core      -> the arbiter's client 2   2: reqA, reqB                  the same
//   gather srq busy   Reg   inject `gather_srq_busy` -> tile control  1: the gather unit's read still offered at the end of t (one
//                                                                       of STATUS.gather_busy's three inputs, ORed: gather.h)
//   wr_note           Link(1)  write engine -> the arbiter            1: the word address of the write the scratchpad does in
//                                                                       the next cycle (no read is taken at its bank then)
//   gather_note       Link(1)  director -> the arbiter                0: a GATHER accepted (the gather unit busy in the next cycle)
//   PTR view          RegFile  table store `ptr` -> the AG, the core  PTR as committed at the end of t - 1 (table_store.h)
//   STREAM, GROUP     RegFile  table store `view` -> the AG, the core the tables as committed at the end of t - 1
//
// The read arbiter (in the inject unit: inject.h) serves its clients in fixed priority, the gather unit (0), the address
// generator (1), the read unit (2), one take a cycle while a token is free and no write meets the read at its bank; a
// client's Hold is taken when its read is taken (so a client sees `refused` when its previous read is still offered at
// the end of the cycle it puts again: PORTS.md 5). Returns are packets through the injection point. The read unit
// (client 2) is the core's Fifo(read_port_depth).
//
// Wiring: the units of both groups are added by the control builder (the add_g1); the crossing inside a tile is wired
// there, and the read queue's ports (in the injection unit, the) through `MemIO` below, which tile_cycle connects.
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
// A_GROUP, the address generator's alone: the read is a group transfer's, so a cycle it is offered and not taken is
// drift (8.4; the RTL's address_generator: `drift = group_valid && !read_request_ready`), which the arbiter signals.
constexpr uint64_t A_GROUP = uint64_t(1) << 36;
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

// ---- PTR's and HIST's request and return words (table_store.h; the director sends them, 12.4, 12.5) ----
// Request: a = op (OP_ADD_RET), b = index (PTR + i, or HIST's digit), c = addend.
// PTR's return, to the write engine: a = index (0-255), b = the old pointer, c = 0.| STREAM_DEST[i] << 32 (Fields.streamE0), b = STREAM_COUNT[i], c = the request's index;
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
    unsigned read_port_depth = 64, read_latency = 3;   // READ_LATENCY (15)
    uint32_t mem_chunks = 1u << 20;
    sim::HoldOut rd_gather;            // the gather `rd` (client 0), width 2
    sim::HoldOut rd_ag;                // the AG `rd` (client 1), width 2
    sim::FifoOut rd_port;              // the core's read port (client 2), width 2, Fifo(read_port_depth)
    sim::HoldIn sram_rd;               // the scratchpad's read, width 1 (big_sram.h)
    sim::LinkOut sram_ret;             // the scratchpad's row, width 2, Link(READ_LATENCY - 1)
    bool has_notes = false;            // the two below are given (a tile with the write engine and the director)
    sim::LinkOut wr_note;              // the write engine: its write of the next cycle, width 1, Link(1) (inject.h)
    sim::LinkOut gather_note;          // the director: a GATHER accepted, width 0, Link(1) (inject.h)
    sim::RegIn gather_srq_busy;        // the register block: gather Hold occupied or a gather read in flight, width 1
    bool has_gather_taken = false;     // the one below is given (a tile with the gather unit)
    sim::WireIn gather_taken;          // the gather unit: the arbiter's takes of its reads (inject.h, gather.h)
    bool has_drift = false;            // the one below is given (a tile with the tile control unit)
    sim::LinkIn drift;                 // the tile control unit: ERRORS.drift's pulse, width 0, Link(1) (inject.h)
    sim::CountIn rp_injected;          // the core: read-port returns injected (ReadPort.injectedAt), read through t - 1
};

}  // namespace units
