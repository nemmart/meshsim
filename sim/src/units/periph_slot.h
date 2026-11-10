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
//   + the peripherals' own ports, held in `own` for them (peripheral.h, OWN PORTS: C&C 1's READY, error lines, GO)
//
// THE STEP at t (AUDIT-M3 3.1's order):
//   level 0: (1) each peripheral's begin(t) [C&C 1's READY arrivals, error lines]; (2) the core's consume of t - 1
//   (consume_events, DMA done) and its EVENTS_MASK store; (3) the landings due (accept); (4) the core's other inputs of
//   t - 1 (run, write_word, DMA_HDR/ADDR, PULSE_DMA); (5) publish the window (only registers that changed).
//   then: for n = 1..3, the peripheral's step(t), its DMA's work, end(t); the flags (invariant 16, violation counts).
//   wake: t + 1 if any peripheral isn't idle or a DMA is busy; else its next_wake_at; else on S1/S2/S3 (D23: no polling).
#pragma once

#include <cstdint>

#include "config.h"
#include "sim.h"
#include "base/fatal.h"
#include "units/linkword.h"
#include "units/peripheral.h"
#include "units/replayer.h"
#include "units/tracer.h"

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
    static constexpr unsigned R_WRITABLE = 112;            // [112, 115): words 32-127 a store writes (the rest of the
                                                           //   defined ones are read-only: a store is a bus error); static,
                                                           //   from the peripheral's writable(w) (all defined if it has none)
    static constexpr unsigned PER = 115;
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

// A packet a DMA sends (ref's dmaOut entry): the header as LinkWord.hdr builds it.
struct DmaPacket {
    int64_t t = 0;
    uint64_t lo = 0, h = 0, hi = 0;
};

// The slot as ref's PeripheralSlot runs it (immediate calls, ref's timing), without ports: the unit below wraps it, and
// the tests drive it directly. Facts and counts instead of strings (D24).
struct SlotCore {
    // The peripherals: [1]-[3], created at configuration (D6: no allocation in the run). type picks the class. A Ref owns
    // its peripheral (move-only: the build moves units into their arena).
    struct Ref {
        PeriphType type = PT_ABSENT;
        void* p = nullptr;
        Ref() = default;
        Ref(Ref&& o) noexcept : type(o.type), p(o.p) { o.p = nullptr; o.type = PT_ABSENT; }
        Ref& operator=(Ref&& o) noexcept;
        Ref(const Ref&) = delete;
        Ref& operator=(const Ref&) = delete;
        ~Ref();
    } per[4];

    // A peripheral's DMA (4.17.9): PeripheralSlot.Dma's state.
    struct Dma {
        uint32_t hdr = 0, addr = 0;
        unsigned done = 0;                                 // DMA done's count (depth 1)
        int64_t done_at = INT64_MIN, started_at = INT64_MIN;
        bool busy = false;
        int64_t left = 0;
        int q = 0;
        uint64_t transfers = 0, packets = 0, ignored = 0;
        bool busy_at(int64_t t) const { return busy && t > started_at; }
        uint32_t done_bit(int64_t t) const { return done > 0 && done_at < t ? 2u : 0u; }
    } dma[4];

    uint32_t mask[4] = {};
    int64_t consume_t[4] = {-1, -1, -1, -1}, pend_mask_t[4] = {-1, -1, -1, -1};
    uint32_t consume_bits[4] = {}, pend_mask[4] = {};
    uint64_t full_seen[4] = {};
    unsigned tile = 0;
    bool dma_port = false;                                 // a controller: the DMA's packets go to a port with room
    // statistics (diagnostic): ref's accepted, reservedAccesses; violations: the slot's own flags (violation_total adds
    // the peripherals', which ref's host forwards into one count)
    uint64_t accepted = 0, reserved_accesses = 0, violations = 0;
    // facts (D24): the slot's own flags
    enum : uint16_t { F_RESERVED = 1, F_DMA_HDR_BUSY, F_DMA_ADDR_BUSY, F_FULL_POST, F_PERIPHERAL };
    struct Fact { int64_t t; uint16_t code; uint32_t a, b; };
    static constexpr unsigned FACTS = 32;
    Fact facts[FACTS] = {};
    uint64_t n_facts = 0;
    // the DMA packets of the last run_cycle (at most one per peripheral), for the unit to put / a test to read
    DmaPacket out[3];
    unsigned n_out = 0;

    SlotCore() = default;
    SlotCore(SlotCore&&) noexcept = default;
    SlotCore(const SlotCore&) = delete;
    SlotCore& operator=(const SlotCore&) = delete;
    // Configuration: create peripheral n of type `type` (the slot owns it), from the slot's configuration.
    void install(unsigned n, PeriphType type, const PeriphSlotConfig& c = PeriphSlotConfig{});

    uint32_t present() const;
    // ---- ref's PeripheralSlot, method for method ----
    void accept(int64_t t, unsigned n, uint64_t lo, uint64_t h, uint64_t hi);
    bool read(int64_t t, unsigned n, unsigned w, uint32_t& v);          // false: a bus error
    bool peek(int64_t t, unsigned n, unsigned w, uint32_t& v);          // read without its effects (the consume, a flag)
    bool store_stalls(int64_t t, unsigned n, unsigned w);
    bool write(int64_t t, unsigned n, unsigned w, uint32_t v);          // false: a bus error
    bool defined(unsigned n, unsigned w);                               // a load of w is not flagged (configuration)
    uint32_t live_events(int64_t t);
    uint32_t live_errors(int64_t t);
    void run_cycle(int64_t t, unsigned dma_room = ~0u);                 // dma_room: packets the DMA port takes at t
    void end_cycle(int64_t t);
    void reset(int64_t t);
    uint64_t violation_total();                                         // ref's slot.violations
    bool idle(int64_t t);
    int64_t next_wake_at(int64_t t);
    void skip_cycles(int64_t n);
    unsigned digest_words(int64_t* out, unsigned cap);
    // The debug checks' digest (sim.h, DEBUG CHECKS): the slot's state and each peripheral's progress.
    uint64_t digest();

    // The dispatch: f(concrete&) for peripheral n's class (periph_slot.cpp; one case per type).
    template <class F>
    decltype(auto) visit(unsigned n, F&& f);

    // The peripheral at n as its class (tests, the harness; FATAL if it is another type).
    Replayer& replayer(unsigned n);
    tracer::Tracer& tracer(unsigned n);   // W14
    TestPeripheral& test_peripheral(unsigned n);
    Cnc1Peripheral& cnc1(unsigned n);         // W16 (the chip sets C&C 1's ref_cycle)
    CcStub& cc_stub(unsigned n);              // W16

private:
    void violate(int64_t t, uint16_t code, uint32_t a = 0, uint32_t b = 0);
    uint32_t reserved(int64_t t, unsigned n, unsigned w, bool store);
    void dma_start(int64_t t, unsigned n);
    void dma_run(int64_t t, unsigned n, unsigned& room);
};

// The framework unit (AUDIT-M3 3.1): SlotCore behind the ports above.
struct PeriphSlot {
    // ---- ports ----
    sim::LinkIn region;      // S1: width 3, from the director, Link(3)
    sim::LinkIn core;        // S2/S3: width 1 (CoreOp), from the core, Link(1)
    sim::RegFileOut window;  // S4/S5/S8: Win::REGS registers
    sim::CreditOut dma;      // S6: width 3, into the controller router's CC_DMA_IN (cfg.controller)

    PeriphSlotConfig cfg;
    SlotCore s;
    OwnPorts own;                         // the peripherals' own ports (peripheral.h, OWN PORTS), declared for them
    uint32_t published[Win::REGS] = {};   // the RegFile as last written (only changed registers are written)
    uint8_t own_words[4][Win::WORDS - Win::OWN0] = {};   // D35: peripheral n's defined own words (k = w - 32), from the first publish
    uint8_t own_words_n[4] = {};
    // A mesh tile has no DMA port: its peripherals' DMA packets are recorded, as ref's slot keeps them in dmaOut (its
    // dmaPort is null there): every one counted, the last DMA_RECORD kept (a ring, oldest first from dma_recorded).
    static constexpr unsigned DMA_RECORD = 16;
    DmaPacket dma_record[DMA_RECORD] = {};
    uint64_t dma_recorded = 0;
    bool first = true;

    PeriphSlot(sim::Ports& p, const PeriphSlotConfig& c);
    PeriphSlot(PeriphSlot&&) noexcept = default;
    PeriphSlot(const PeriphSlot&) = delete;
    PeriphSlot& operator=(const PeriphSlot&) = delete;

    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const { return const_cast<SlotCore&>(s).digest() ^ uint64_t(first); }

    // The replayer at n's canonical fingerprint (AUDIT-M3 6.8) at the end of cycle c, between runs: ref's replayer then
    // still holds the inputs that here are in the slot's links (S1: region-0x04 writes the director accepted at c - 2 ..
    // c; S2: the core's PULSE_RUN or RUN_CONFIG store of c), so they are read with the port observer and written as
    // ref's. Out: FpOut or any sink with w(name, v), wi(name, i, v).
    template <class Out> void fingerprint_replayer(const sim::Sim& m, unsigned n, Out& o, int64_t c);
    // W14: the Tracer at n's fingerprint unit `unit` (Tracer::FP_NAMES; AUDIT-M3 6.6) at the end of cycle c, between
    // runs, the same way: the landings of c - 2 .. c and the core's PULSE_RUN or window store of c read from the links,
    // a PULSE_DMA of c applied as ref's slot does at once (out_start with DMA_HDR's queue and length), and a consuming
    // EVENTS load of c as ref's slot applies it at c's end.
    template <class Out> void fingerprint_tracer(const sim::Sim& m, unsigned n, int unit, Out& o, int64_t c);

private:
    void publish(sim::Ctx& c, int64_t t);
    void full_values(int64_t t, uint32_t* out);
};

template <class Out>
void PeriphSlot::fingerprint_replayer(const sim::Sim& m, unsigned n, Out& o, int64_t c) {
    Replayer::FpPending p;
    sim::PortEntry e[8];
    const sim::PortView v1 = m.observe(region, e, 8);
    for (uint32_t k = 0; k < v1.n; k++) {
        const uint64_t h = e[k].w[1];
        if (lw::region(lw::addr(h)) != 0x03 + n) continue;
        if (p.n_land == 4) FATAL("slot %u: more than 4 landings in flight", cfg.tile);
        p.land[p.n_land++] = {int64_t(e[k].at) - 1, unsigned(lw::src(h)), lw::offset(lw::addr(h)) >> 2, unsigned(lw::size(h)), e[k].w[0], e[k].w[2]};
    }
    const sim::PortView v2 = m.observe(core, e, 8);
    for (uint32_t k = 0; k < v2.n; k++) {
        const uint64_t x = e[k].w[0];
        if (CoreOp::kind(x) != CoreOp::STORE || CoreOp::n(x) != n) continue;
        const unsigned w = CoreOp::word(x);
        const int64_t t = int64_t(e[k].at) - 1;
        if (w == 3 && (CoreOp::value(x) & 1)) p.in[p.n_in++] = {t, 2, 0};
        else if (w == Replayer::W_RUN_CONFIG) p.in[p.n_in++] = {t, 3, CoreOp::value(x)};
    }
    s.replayer(n).fingerprint(o, c, &p);
}

template <class Out>
void PeriphSlot::fingerprint_tracer(const sim::Sim& m, unsigned n, int unit, Out& o, int64_t c) {
    tracer::Tracer& tr = s.tracer(n);
    tracer::Tracer::FpPending p;
    sim::PortEntry e[8];
    const sim::PortView v1 = m.observe(region, e, 8);
    for (uint32_t k = 0; k < v1.n; k++) {
        const uint64_t h = e[k].w[1];
        if (lw::region(lw::addr(h)) != 0x03 + n) continue;
        if (p.n_land == 4) FATAL("slot %u: more than 4 landings in flight", cfg.tile);
        // ref's Tracer.accept(t): e = t + 2, the packet's src, offset, size (lo, hi); t = the arrival - 3 (S1, Link(3))
        p.land[p.n_land++] = {int64_t(e[k].at) - 3 + tracer::Tracer::RP_LATENCY, int(lw::src(h)), lw::offset(lw::addr(h)),
                              int(lw::size(h)), e[k].w[0], e[k].w[2]};
    }
    const sim::PortView v2 = m.observe(core, e, 8);
    for (uint32_t k = 0; k < v2.n; k++) {
        const uint64_t x = e[k].w[0];
        if (CoreOp::n(x) != n) continue;
        if (CoreOp::kind(x) == CoreOp::CONSUME) { p.consume |= CoreOp::value(x) & ~2u; continue; }   // applied at c's end in ref
        if (CoreOp::kind(x) != CoreOp::STORE) continue;
        const unsigned w = CoreOp::word(x);
        const uint32_t v = CoreOp::value(x);
        const int64_t t = int64_t(e[k].at) - 1;   // S2, Link(1): the store's cycle
        if (w == 3 && (v & 1)) p.in[p.n_in++] = {t, tracer::Tracer::FP_IN_RUN, 0, 0};
        else if (w >= 32 && tr.writable(int(w))) p.in[p.n_in++] = {t, tracer::Tracer::FP_IN_WORD, int(w), v};
        else if (w == 4 && (v & 1) && tr.out_queues() > 0) {   // PULSE_DMA: ref's slot.dmaStart at once
            const uint32_t hdr = s.dma[n].hdr;
            if ((hdr & 0xFFF) > 0) { p.start = true; p.start_q = int((hdr >> 12) & 3); p.start_words = int64_t(hdr & 0xFFF); }
        }
    }
    tr.fingerprint(unit, o, c, &p);
}

}  // namespace units
