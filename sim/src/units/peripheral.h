// peripheral.h: what every peripheral offers its slot (spec 4.17; sim/ref/meshsim/src/Peripheral.java), as a fixed set
// of member functions the slot calls through a switch on the peripheral's type (no virtual calls; WORKER15, owner of the
// slot; agreed with W14 (the Tracer, type 3) and W16 (C&C 1's peripheral, type 1; C&C 2's stub, type 2)).
//
// A peripheral is a class deriving from PeripheralBase<Self> (CRTP, non-virtual: the base only supplies the defaults
// ref's interface has; a peripheral's own member of the same name hides it, and the slot's dispatch calls the concrete
// class's). The
// slot owns its peripherals (created at configuration) and calls them synchronously, as ref's slot does.
//
// THE METHODS (`ref` name in brackets; t is the cycle; "level read" = the state at the end of t - 1):
//   identity      int type() const; int params() const                                  [type, params]
//   inputs        void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi)   [accept]
//                 void run(int64_t t)                                                   [run]  (PULSE_RUN)
//                 int  write_word(int64_t t, int w, uint32_t v)   w 32-127: 1 written, 0 read-only, -1 undefined
//                 uint32_t consume_events(uint32_t bits)          a consuming load's effect: one count from each set
//                                                                 bit; returns the bits it took (ref's onConsume)
//   level reads   bool running(int64_t t); int errors(int64_t t); uint32_t status_bits(int64_t t)   ([31:2] only)
//                 void sync(int64_t t); EventCounts& events()     the slot calls sync(t) before events().value(t)
//                 bool word(int64_t t, int w, uint32_t& v)        w 32-127; false: undefined (reads 0, flagged)
//                 bool store_stalls(int64_t t, int w)             a store to w must retry (C&C 1's UART_TX)
//   work          void step(int64_t t)                                                  [runCycle]
//   the DMA       int out_queues() const; bool out_ready(int64_t t, int q); void out_take(int64_t t, int q, uint64_t w[2]);
//                 void out_start(int64_t t, int q, int64_t words)
//   wake          bool idle(int64_t t); int64_t next_wake_at(int64_t t); void skip_cycles(int64_t n)
//   fingerprint   static constexpr int DIGEST_N; void digest_words(int64_t out[DIGEST_N]) const
//   flags         uint64_t violation_count() const               ref's violate(): the slot forwards new ones to the tile
//   reset         void reset(int64_t t)
//   framework     void ports(sim::Ports&)                         declare the peripheral's own ports (C&C 1: READY,
//                                                                 the error lines, GO); default none
//                 void begin(sim::Ctx&, int64_t t)                level 0 of t, first: take the own inputs due (C&C 1:
//                                                                 READY arrivals, error lines); default none
//                 void end(sim::Ctx&, int64_t t)                  after step(t): put the own outputs (C&C 1: GO);
//                                                                 default none
//
// TIMING (ref's rule, Peripheral.java): an input called with t takes effect at the end of t; a level read with t sees
// the end of t - 1. On the framework (AUDIT-M3 3.1) the slot applies the inputs at level 0 of the next cycle, so:
//   - accept(t, ...) is called at level 0 of t + 3 (the director's Link(3)), with t the director's cycle. The
//     peripheral lands it at the end of t + RP_LATENCY (= t + 2), as ref does; it must accept the call though it may
//     already have been settled through t + 2 (but not t + 3). [The replayer: its landings are checked against that.]
//   - run(t), write_word(t, ...), consume_events(...) for a store or consuming load at t are called at level 0 of t + 1,
//     before any level read of t + 1. consume_events (S3) and the EVENTS_MASK store come first, then the landings,
//     then run / write_word: ref's endCycle(t) before its settle(t + 1).
//   - The level reads the slot publishes for t + 1 are taken after those (then step(t + 1) runs).
//   - DMA: out_ready / out_take at t see the end of t - 1, as ref's.
#pragma once

#include <cstdint>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"

namespace units {

// The peripheral types (PARAMS [7:0]; spec 4.17.1, D46) and the slot's dispatch tags.
enum PeriphType : uint8_t {
    PT_ABSENT = 0,
    PT_CNC1 = 1,      // C&C 1's Ingress/Egress and UART (Cnc1Peripheral)          W16
    PT_CC_STUB = 2,   // C&C 2's (and C&C 3's without the Tracer) stub (CcCores.Stub)  W16
    PT_TRACER = 3,    // the Tracer, C&C 3 (Tracer.TYPE)                           W14
    PT_REPLAYER = 4,  // the replayer, every mesh tile's peripheral 1 (Replayer)   W15
    PT_TEST = 15,     // the suites' test peripheral (TestPeripheral)              W15
};

constexpr int RP_LATENCY = 2;   // rp_latency (4.17.5)

// The defaults ref's Peripheral interface gives (default methods). Non-virtual: hidden, not overridden.
template <class Self>
struct PeripheralBase {
    static constexpr int DIGEST_N = 0;
    bool idle(int64_t) const { return false; }
    int64_t next_wake_at(int64_t) const { return INT64_MAX; }
    void skip_cycles(int64_t) {}
    void digest_words(int64_t*) const {}
    uint32_t status_bits(int64_t) const { return 0; }
    void sync(int64_t) {}
    bool word(int64_t, int, uint32_t&) { return false; }
    int write_word(int64_t t, int w, uint32_t) { uint32_t v; return static_cast<Self*>(this)->word(t, w, v) ? 0 : -1; }
    bool store_stalls(int64_t, int) const { return false; }
    int out_queues() const { return 0; }
    bool out_ready(int64_t, int) const { return false; }
    void out_take(int64_t, int, uint64_t*) { FATAL("peripheral: out_take without an output queue"); }
    void out_start(int64_t, int, int64_t) {}
    void ports(sim::Ports&) {}
    void begin(sim::Ctx&, int64_t) {}
    void end(sim::Ctx&, int64_t) {}
};

}  // namespace units
