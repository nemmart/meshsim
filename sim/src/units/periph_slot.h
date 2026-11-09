// periph_slot.h: a unit's peripherals (spec 4.17; sim/ref/meshsim/src/PeripheralSlot.java; AUDIT-M3 3.1), WORKER15.
// DECLARATIONS: the ports, the window's register layout and the dispatch are the agreed interface (W14, W16; sessions/
// WORKER15/NOTES.md); the step comes with the implementation.
//
// One framework unit per tile (mesh tile or controller): up to three peripherals at regions 0x04-0x06 and windows
// 0xFFFFFA00 + 0x200 (n - 1), owned by the slot (created at configuration from PeriphSlotConfig) and called synchronously
// through `visit`, a switch on the peripheral's type (units/peripheral.h's method set; no virtual calls). The slot owns
// EVENTS_MASK, the consuming loads, the window's access rules, the live bits and the DMAs (controllers only).
//
// PORTS (AUDIT-M3 3.1's table):
//   S1 region (LinkIn, width 3: lo, header, hi)  <- the director: a mode-0 write to region 0x04-0x06 at t, Link(3);
//                                                    applied at level 0 of t + 3 (accept(t, ...): lands at the end of t + 2)
//   S2/S3 core (LinkIn, width 1: CoreOp)          <- the core: a window store or a consuming EVENTS load at t, Link(1);
//                                                    applied at level 0 of t + 1 (at most one per cycle)
//   S4/S5/S8 window (RegFileOut, WIN_REGS)        -> the core (window loads), the register block (EVENTS [15:13],
//                                                    ERRORS [13:11], the WFI wake), the store stalls; written only at
//                                                    level 0: what ref's reads at t return (the end of t - 1)
//   S6 dma (CreditOut, width 3: lo, header, hi)   -> the controller router's CC_DMA_IN, Fifo(ccDmaDepth = 4): room as of
//                                                    the start of t, one packet a cycle (controllers with a DMA only)
//   S7 rows                                       an observer, not a port: the replayer's sink (hash, optional buffer)
//   + a peripheral's own ports (Peripheral::ports: C&C 1's READY, error lines, GO)
//
// THE STEP at t (AUDIT-M3 3.1's order):
//   level 0: (1) each peripheral's begin(t) [C&C 1's READY arrivals, error lines]; (2) the core's consume of t - 1
//   (consume_events, DMA done) and its EVENTS_MASK store; (3) the landings due (accept); (4) the core's other inputs of
//   t - 1 (run, write_word, DMA_HDR/ADDR, PULSE_DMA); (5) publish the window (only registers that changed).
//   then: for n = 1..3, the peripheral's step(t), its DMA's work, end(t); the flags (invariant 16, violation counts).
//   wake: t + 1 if any peripheral isn't idle or a DMA is busy; else its next_wake_at; else on S1/S2/S3 (D23: no polling).
#pragma once

#include <cstdint>

#include "sim.h"
#include "base/fatal.h"
#include "units/peripheral.h"

namespace units {

class Replayer;
struct TestPeripheral;
namespace tracer { class Tracer; }
struct Cnc1Peripheral;   // W16
struct CcStub;           // W16

// The core's word on S2/S3 (one per cycle): a store to window word w of peripheral n, or a consuming EVENTS load.
struct CoreOp {
    enum : unsigned { STORE = 0, CONSUME = 1 };
    static uint64_t store(unsigned n, unsigned w, uint32_t v) { return uint64_t(STORE) | uint64_t(n) << 2 | uint64_t(w) << 4 | uint64_t(v) << 32; }
    // bits: the value the load returned, under EVENTS_MASK (ref: consumeBits = value & mask)
    static uint64_t consume(unsigned n, uint32_t bits) { return uint64_t(CONSUME) | uint64_t(n) << 2 | uint64_t(5) << 4 | uint64_t(bits) << 32; }
    static unsigned kind(uint64_t x) { return unsigned(x & 3); }
    static unsigned n(uint64_t x) { return unsigned(x >> 2) & 3; }
    static unsigned word(uint64_t x) { return unsigned(x >> 4) & 0x7F; }
    static uint32_t value(uint64_t x) { return uint32_t(x >> 32); }
};

// The window RegFile's layout: per peripheral n (1-3) a block of WIN_PER registers at WIN_PER * (n - 1); then the live
// bits. A reader computes a window load's value from it (and flags what ref's slot flags: a reserved or undefined word).
struct Win {
    static constexpr unsigned WORDS = 128;                 // window words 0-127
    static constexpr unsigned STD = 10, OWN0 = 32;         // words 0-9 standard, 10-31 reserved, 32-127 the peripheral's
    // per peripheral:
    static constexpr unsigned R_STD = 0;                   // [0, 10): words 0-9 as a non-consuming load reads them:
                                                           //   0 PARAMS, 1 STATUS, 2 ERRORS, 3 0, 4 0, 5 EVENTS (the value a
                                                           //   consuming load returns), 6 EVENTS_PEEK, 7 EVENTS_MASK,
                                                           //   8 DMA_HDR, 9 DMA_ADDR (0 without a DMA)
    static constexpr unsigned R_OWN = 10;                  // [10, 106): words 32-127 (0 where undefined)
    static constexpr unsigned R_DEFINED = 106;             // [106, 109): words 32-127 defined, a bit each (set at
                                                           //   configuration: 0 for reserved words 10-31 and for undefined
                                                           //   own words: a load reads 0 and is flagged, ref's reserved())
    static constexpr unsigned R_STALL = 109;               // [109, 112): words 32-127 whose store stalls now (S8)
    static constexpr unsigned PER = 112;
    // the tile's live bits:
    static constexpr unsigned R_LIVE = 3 * PER;            // [2:0] EVENTS [15:13] ((events | done) & mask != 0, per n);
                                                           // [5:3] ERRORS [13:11] (errors != 0, per n)
    static constexpr unsigned REGS = R_LIVE + 1;
    static unsigned reg(unsigned n, unsigned r) { return PER * (n - 1) + r; }
};

struct PeriphSlotConfig {
    unsigned tile = 0;                   // the unit's endpoint id (0-63 a tile, 64-66 C&C 1-3)
    PeriphType type[4] = {};             // [1]-[3]; PT_ABSENT: absent
    bool controller = false;             // C&C 1-3: the DMA port is declared (a mesh tile's peripherals have none)
    uint64_t replayer_row_buffer = 0;    // rows of the replayer's optional record buffer (tests; 0: the hash only)
};

struct PeriphSlot {
    // ---- ports ----
    sim::LinkIn region;      // S1: width 3, from the director, Link(3)
    sim::LinkIn core;        // S2/S3: width 1 (CoreOp), from the core, Link(1)
    sim::RegFileOut window;  // S4/S5/S8: Win::REGS registers
    sim::CreditOut dma;      // S6: width 3, into the controller router's CC_DMA_IN (cfg.controller)

    PeriphSlotConfig cfg;

    // The peripherals: [1]-[3], created at configuration (D6: no allocation in the run). type[n] picks the class.
    struct Ref {
        PeriphType type = PT_ABSENT;
        void* p = nullptr;
    } per[4];

    // A peripheral's DMA (4.17.9): PeripheralSlot.Dma's state.
    struct Dma {
        uint32_t hdr = 0, addr = 0;
        unsigned done = 0;                                 // DMA done's count (depth 1)
        int64_t done_at = INT64_MIN, started_at = INT64_MIN;
        bool busy = false;
        int64_t left = 0;
        int q = 0;
        uint64_t transfers = 0, packets = 0, ignored = 0;  // statistics (transfers, packets: T2's summary)
    } dmas[4];

    uint32_t mask[4] = {};
    uint64_t full_seen[4] = {}, violations_seen[4] = {};
    // statistics (diagnostic): ref's accepted, runsIgnored, reservedAccesses, violations
    uint64_t accepted = 0, runs_ignored = 0, reserved_accesses = 0, violations = 0;

    PeriphSlot(sim::Ports& p, const PeriphSlotConfig& c);
    ~PeriphSlot();
    PeriphSlot(const PeriphSlot&) = delete;
    PeriphSlot& operator=(const PeriphSlot&) = delete;

    sim::Wake step(sim::Ctx& c);

    // PARAMS [2:0] (ref's present()).
    uint32_t present() const;
    // The peripheral at n as its class (tests, the harness; FATAL if it is another type).
    Replayer& replayer(unsigned n);
    tracer::Tracer& tracer(unsigned n);

    // The dispatch: f(concrete&) for peripheral n's class. Adding a type is one case here (and its class's include in
    // periph_slot.cpp, where visit is instantiated).
    template <class F>
    decltype(auto) visit(unsigned n, F&& f);

    // The reset (6.1): every peripheral idle, EVENTS_MASK 0, the DMAs idle.
    void reset(int64_t t);
    // Stage 1's fingerprint words: the peripherals' digest_words, in order.
    unsigned digest_words(int64_t* out, unsigned cap) const;
};

}  // namespace units
