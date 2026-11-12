// check.h (WORKER20, P2, rows B19/B20): ref's per-cycle invariant checks (Chip.checkInvariants, scanOverflows; spec 9),
// the units' side. A framework addition; harness-only in effect (nothing is recorded unless the harness installs a sink).
//
// HOW IT WORKS. ref keeps, per unit, a violation counter and the last violation's text; at the end of every cycle
// Chip.checkInvariants compares each counter with what it reported before and reports one message per changed counter
// (the unit's last text), in a fixed order. Here a unit that counts a violation also records a FACT: fixed size, no
// text, into a list the harness sized at startup (D24: no allocation in the loop). Between cycles the harness
// (sim/tests/checker.cpp) turns the facts into ref's exact strings, in ref's report order, with ref's rules (one message
// per check per tile per cycle, the last fact's; the once-only checks; FIFO overflows on ref's 16-cycle scan), into
// Chip::violations() and violation_messages() (sim/tests/mach.h).
//
// THE SWITCH (D6; the owner, 2026-11-10). SIM_CHECKS is on in the debug build (SIM_DEBUG) and in the suite build (MODE=
// suite: release flags plus -DSIM_CHECKS); off in the timing builds (MODE=release), where `record` compiles to nothing.
// The units' own counters are kept in every build, as before: only the facts are switched.
//
// RECORDING A FACT (for the units' authors, wave 2 included):
//     check::record(cycle, tile, check::Kind::DIR_BAD_PACKET, d, h, hi);
//   - `cycle` is the cycle ref's check sees it: ref runs checkInvariants at the end of cycle t, so a unit stepping at t
//     records t (c.now()); a unit whose step at t + 1 is ref's end of t (the register block, sim.h THE SHIFTED END OF
//     CYCLE) records t.
//   - `tile` is ref's: 0-63 a mesh tile, 64-66 C&C 1-3 as cores, -1 the chip (C&C's model, the request bus).
//   - up to four 64-bit arguments, as the Kind's comment lists them (the harness formats them).
//   - record exactly where ref's unit does `violations++; lastViolation = ...` (or Chip.violation): same conditions, same
//     order within the unit. Two facts of one Kind's slot in one cycle on one tile give one message, the later one's.
//   - a Kind that ref formats from unit state (a counter name, a register name) carries that state as arguments: the
//     formatter has no access to the unit.
// A Kind's message text lives in checker.cpp's table, word for word ref's; add a Kind here and its format there together.
//
// THREADS. The sink's index is atomic: units on several threads may record. One unit's facts keep their order.
#pragma once

#include <atomic>
#include <cstdint>

#if defined(SIM_DEBUG) && !defined(SIM_CHECKS)
#define SIM_CHECKS 1
#endif

namespace sim::check {

// ref's report slots, in ref's order within a cycle (Chip.runCycle, then checkInvariants). The harness reports a
// cycle's facts slot by slot in this order, tile by tile (ascending) inside each per-tile slot.
enum class Slot : uint8_t {
    // before checkInvariants (Chip.runCycle)
    CC1_PERIPH,     // tile -1: C&C 1's peripheral (cc1p's callback)
    S1_LATE,        // tile -1: S1's measurement (s1.lastLate)
    ERRORS,         // per tile: ERRORS bits newly set while any error line is high (invariant 15; cfg.checkErrors)
    // checkInvariants(t): the overflow scan first when t % 16 == 0 (OVERFLOW facts; also whenever the harness scans)
    OVERFLOW,       // per tile, then the chip's (cnc.rx, memsub.rx, cc3.rx), once per FIFO, at the overflow's own cycle
    // per tile 0-63, ref's seen[i][0..7], s2[i][0..5], refusalReported[i]
    EJECT, ROUTE, INJ, DIR, DISJOINT, HIST17, ROTATION, CTL, CMEM, READPORT, AG, SRQ, WRITER, REFUSAL,
    // the cores (CcCores.check), then the chip's
    CORES, REQBUS, RX_LOST, ENDPOINT,
    COUNT
};

// The facts. Each names its slot (kSlot below) and ref's text (checker.cpp). Arguments in order: a0, a1, a2, a3.
enum class Kind : uint16_t {
    NONE = 0,
    // ---- ERRORS (Chip.runCycle; TileControl) ----
    ERRORS_SET,                 // a0 the ERRORS bits now set (the harness applies ref's condSeen)
    ERROR_LINE,                 // a0 1/0: the tile's error line rose or fell (no message; drives condSeen's clearing)
    // ---- C&C 1's peripheral, S1 ----
    CC1_PERIPH,                 // a0 an index into checker.cpp's table of cc1p messages, a1-a3 its arguments
    S1_LATE,                    // a0 the tile, a1 the cycles late (s1.lastLate's form, checker.cpp)
    // ---- invariant 1 ----
    FIFO_OVERFLOW,              // a0 the FIFO's id in the harness's table of ref's names (checker.cpp), at its cycle
    // ---- the router ----
    EJECT_WRONG_TILE,           // (no arguments)
    ILLEGAL_FORWARD,            // (no arguments)
    // ---- the injection path (InjectionPath.violate) ----
    INJ_BAD_PACKET,             // a0 the reason's index (LinkWord.bad), a1 d, a2 h, a3 hi
    INJ_SIZE_UNDEF,             // a1 d, a2 h, a3 hi
    INJ_NO_ENDPOINT,            // a1 d, a2 h, a3 hi
    INJ_GATHER_ELSEWHERE,       // a1 d, a2 h
    INJ_RESERVED_MODE,          // a1 d, a2 h, a3 hi
    INJ_COUNT_CTR0,             // a1 d, a2 h, a3 hi
    // ---- the traffic director (TrafficDirector.violate) ----
    DIR_READPORT_OVERLAP,       // a0 outstanding, a1 d, a2 h, a3 hi
    DIR_BAD_PACKET,             // a0 the reason's index, a1 d, a2 h, a3 hi
    DIR_SIZE_UNDEF,             // a1 d, a2 h, a3 hi
    DIR_COUNT_CTR0,             // a1 d, a2 h, a3 hi
    DIR_PAST_SRAM,              // a0 the chunk, a1 d, a2 h, a3 hi
    DIR_UNEXPECTED,             // a0 the counter, a1 d, a2 h, a3 hi (K from the harness's config)
    DIR_UNCOUNTED_MODE,         // a1 d, a2 h, a3 hi
    DIR_UNCOUNTED_REGION,       // a0 the region, a1 d, a2 h, a3 hi
    DIR_UNCOUNTED_SRC,          // a0 the source unit, a1 d, a2 h, a3 hi
    DIR_OVERLAY_AFTER_COMMAND,  // a1 d, a2 h, a3 hi
    DIR_PAST_CMEM,              // a0 the region (0 data, else program), a1 d, a2 h, a3 hi
    DIR_RESERVED_BYTES,         // a1 d, a2 h, a3 hi
    DIR_COMMAND_BLOCK,          // a1 d, a2 h, a3 hi
    DIR_REPLY_BLOCK,            // a1 d, a2 h, a3 hi
    DIR_COMMAND_KIND,           // a0 the kind (2 broadcast, else unicast), a1 d, a2 h, a3 hi
    // ---- big SRAM (invariant 2) ----
    DISJOINT,                   // a0 the chunk, a1 what it did (0 read, 1 written), a2 what it was (0 read, 1 written),
                                //   a3 the phase (the harness reports once per tile and phase, as ref)
    // ---- the HIST store, the AG's rotations ----
    HIST_OVERFLOW,              // (no arguments; once per tile)
    ROTATION_LATE,              // a0 the AG's run number (ref reports the first late rotation of each run: its counter
                                //   restarts at a run's load, Chip.checkInvariants' s[7])
    // ---- the register block (TileControl.violate) ----
    CTL_WFI_WOKEN,              // (no arguments)
    CTL_RESERVED_BITS,          // a0 the register's index (checker.cpp's names), a1 the reserved bits set
    CTL_RECORD_BASE_ODD,        // a0 the value
    CTL_RD_DEST_SIZE,           // (no arguments)
    CTL_RESERVED_ACCESS,        // a0 the access's index in checker.cpp's table, a1 its address
    CTL_DONE_GO_MASKED,         // (no arguments)
    CTL_COUNTER_WRAP,           // a0 the counter
    CTL_DONE_EARLY,             // a0 a mask of counters still expecting arrivals, a1-a3 their counts (checker.cpp)
    // the core's checks, through the block (Controller.java's ctl.violate; W26)
    CTL_HALT,                   // a0 the halt (Controller.HALT_*: ecall 2, ebreak 3, fetch 6, jump 7, trap inside the
                                //   handler 8), a1 its detail (fetch: the pc; jump: the target; trap: the cause)
    CTL_MRET,                   // (no arguments)
    CTL_ALIAS,                  // a0 the (word) address past the end of data or program memory
    CTL_DMEM_RESERVED,          // a0 the store's address (into data memory's first 16 bytes)
    CTL_REQ_FULL,               // (no arguments) a request pushed into a full request FIFO (RequestPort.send; harness only)
    CTL_TABLE_BUSY,             // a0 the table (table_store.h: STREAM_SRC 0, DEST 1, COUNT 2, GROUP 3, PTR 4, HIST 5), a1 the
                                //   word index: the window accessed while its engine uses it (Controller.window, 4.10)
    SLOT_VIOLATION,             // the peripheral slot's own (PeripheralSlot.violate, W26): a0 its code (periph_slot.h F_*),
                                //   a1, a2 its arguments, a3 a full post's depth; F_PERIPHERAL: a1 n, a2 the peripheral's type
    TRACER_VIOLATION,           // the Tracer's (Tracer.violate through its slot, W26): a0 its code (tracer.h V_*), a1, a2 its
                                //   arguments (V_UNDEFINED: a2 the ALU's text, a static string)
    PERIPH_VIOLATION,           // a peripheral's violation through its slot to the block (ref's Peripheral.Host.violate):
                                //   the replayer's (F2): a0 its code, a1-a3 its arguments (units/replayer.h)
    // ---- the controller's memories (ControllerMemories) ----
    CMEM_COMMAND_UNREAD,        // a1 d, a2 h, a3 hi (cfg.checkCommandRead)
    CMEM_REPLY_OVERWRITE,       // a0 the data word, a1 d, a2 h, a3 hi (cfg.strictReplies)
    // ---- the read port ----
    RP_SIZE_UNDEF,              // (no arguments)
    RP_QUEUE_FULL,              // a0 the address
    RP_DEST_ALIGN,              // a0 the destination
    // ---- the address generator ----
    AG_SIZE_UNDEF, AG_AUG128,   // (no arguments)
    AG_DEST_ALIGN,              // a0 the destination, a1 the size
    AG_SRC_ALIGN,               // a0 the source chunk, a1 the size
    AG_DEST_ENDPOINT,           // a0 d
    AG_BROADCAST_ON_TILE,       // a0 the tile
    AG_GATHER_DEST,             // a0 d
    AG_COUNT_CTR0,              // (no arguments)
    AG_START_WHILE_RUNNING,     // (as ref's AG: checker.cpp)
    AG_TABLE_WRAP,              // (as ref's AG: checker.cpp)
    // ---- the read queue, the write engine ----
    SRQ_PAST_SRAM,              // a0 the client, a1 the chunk
    WE_PAST_SRAM,               // a0 the chunk, a1 d, a2 h, a3 hi
    // ---- the read-return register ----
    READ_RETURN_REFUSED,        // (no arguments; once per tile)
    // ---- the chip ----
    REQBUS_QUEUE_OVERFLOW,      // a0 the target
    REQBUS_NO_RECEIVER,         // a0 the source unit, a1 the target
    CNC_RX_LOST,                // (no arguments)
    EP_BAD_PACKET,              // a0 the header (LinkWord.whyBad's reason from it), a1 the sender's address
    EP_SIZE_UNDEF,              // a1 the sender's address
    EP_RESERVED_MODE,           // a0 the mode, a1 the sender's address
    // ---- the cores' own checks (CcCores.check: the same per-unit kinds, reported in slot CORES at tile 64-66) ----
    COUNT
};

// The slot of each Kind (the cores' facts go to CORES whatever their kind: the harness decides by the tile).
constexpr Slot slot_of(Kind k) {
    switch (k) {
    case Kind::ERRORS_SET: case Kind::ERROR_LINE: return Slot::ERRORS;
    case Kind::CC1_PERIPH: return Slot::CC1_PERIPH;
    case Kind::S1_LATE: return Slot::S1_LATE;
    case Kind::FIFO_OVERFLOW: return Slot::OVERFLOW;
    case Kind::EJECT_WRONG_TILE: return Slot::EJECT;
    case Kind::ILLEGAL_FORWARD: return Slot::ROUTE;
    case Kind::DISJOINT: return Slot::DISJOINT;
    case Kind::HIST_OVERFLOW: return Slot::HIST17;
    case Kind::ROTATION_LATE: return Slot::ROTATION;
    case Kind::CMEM_COMMAND_UNREAD: case Kind::CMEM_REPLY_OVERWRITE: return Slot::CMEM;
    case Kind::RP_SIZE_UNDEF: case Kind::RP_QUEUE_FULL: case Kind::RP_DEST_ALIGN: return Slot::READPORT;
    case Kind::SRQ_PAST_SRAM: return Slot::SRQ;
    case Kind::WE_PAST_SRAM: return Slot::WRITER;
    case Kind::READ_RETURN_REFUSED: return Slot::REFUSAL;
    case Kind::REQBUS_QUEUE_OVERFLOW: case Kind::REQBUS_NO_RECEIVER: return Slot::REQBUS;
    case Kind::CNC_RX_LOST: return Slot::RX_LOST;
    case Kind::EP_BAD_PACKET: case Kind::EP_SIZE_UNDEF: case Kind::EP_RESERVED_MODE: return Slot::ENDPOINT;
    default: break;
    }
    if (k >= Kind::INJ_BAD_PACKET && k <= Kind::INJ_COUNT_CTR0) return Slot::INJ;
    if (k >= Kind::DIR_READPORT_OVERLAP && k <= Kind::DIR_COMMAND_KIND) return Slot::DIR;
    if (k >= Kind::CTL_WFI_WOKEN && k <= Kind::PERIPH_VIOLATION) return Slot::CTL;   // (a peripheral's: the block's, ref's)
    if (k >= Kind::AG_SIZE_UNDEF && k <= Kind::AG_TABLE_WRAP) return Slot::AG;
    return Slot::COUNT;
}

struct Fact {
    uint64_t cycle;
    int32_t tile;
    Kind kind;
    uint16_t pad;
    uint64_t a[4];
};

// The harness's list (sized at startup; fatal when full: the harness's MachConfig says how many).
struct Sink {
    Fact* f = nullptr;
    uint32_t cap = 0;
    std::atomic<uint32_t> n{0};
    void add(uint64_t cycle, int tile, Kind k, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);   // check.cpp
};

// The installed sink: null on every machine but the harness's (nothing is recorded then).
inline Sink* sink = nullptr;

#ifdef SIM_CHECKS
inline constexpr bool kOn = true;
inline void record(uint64_t cycle, int tile, Kind k, uint64_t a0 = 0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0) {
    if (Sink* s = sink) [[unlikely]] s->add(cycle, tile, k, a0, a1, a2, a3);
}
#else
inline constexpr bool kOn = false;
inline void record(uint64_t, int, Kind, uint64_t = 0, uint64_t = 0, uint64_t = 0, uint64_t = 0) {}
#endif

}  // namespace sim::check
