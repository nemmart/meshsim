// peripheral.h: what every peripheral offers its slot (spec 4.17; sim/ref/meshsim/src/Peripheral.java), as a fixed set
// of member functions the slot calls through a switch on the peripheral's type (no virtual calls; owner of the
// slot; agreed for the Tracer (type 3), C&C 1's peripheral (type 1) and C&C 2's stub (type 2)).
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
//                 uint32_t consume_events_late(int64_t t, uint32_t bits)   the same for the load presented in cycle t,
//                                                                 applied after step(t): the slot's call (17.2: a set
//                                                                 of t survives the load; a fill sets the depth);
//                                                                 default: events().consume_late(t, bits)
//   level reads   bool running(int64_t t); int errors(int64_t t); uint32_t status_bits(int64_t t)   ([31:2] only)
//                 void sync(int64_t t); EventCounts& events()     the slot calls sync(t) before events().value(t)
//                 bool word(int64_t t, int w, uint32_t& v)        w 32-127; false: undefined (reads 0, flagged)
//                 bool store_stalls(int64_t t, int w)             a store to w must retry (C&C 1's UART_TX)
//   static map    bool load_pops(int w) const                     (optional) a word 32-127 whose load has an effect at the
//                                                                 end of its cycle (17.6: UART_RX and STREAM_RECEIVE pop):
//                                                                 the core tells the slot of such a load (CoreOp::LOAD)
//                 void load_effect(int64_t t, int w)              that effect, for the load presented in cycle t, applied
//                                                                 after step(t): the slot's call
//   static map    bool may_stall(int w) const                     (optional) a word 32-127 whose store may stall (the
//                                                                 RTL's register_bus, own_stalling: Ingress/Egress's
//                                                                 UART_TX, STREAM_SEND and STREAM_SEND_LAST): the bus
//                                                                 answers such a store the cycle after its owner takes
//                                                                 it, whether or not it waited; without it, none
//   static map    bool writable(int w) const                      (optional) a defined word 32-127 a store writes; the
//                                                                 others defined are read-only, a store a bus error. The
//                                                                 core judges a window store's bus error from it at once,
//                                                                 as ref's write() does; without it, every defined word is
//                                                                 taken as writable and a refused store is FATAL
//   work          void step(int64_t t)                                                  [runCycle]
//   the DMA       int out_queues() const; bool out_ready(int64_t t, int q); void out_take(int64_t t, int q, uint64_t w[2]);
//                 void out_start(int64_t t, int q, int64_t words)
//   wake          bool idle(int64_t t); int64_t next_wake_at(int64_t t); void skip_cycles(int64_t n)
//   fingerprint   static constexpr int DIGEST_N; void digest_words(int64_t out[DIGEST_N]) const
//   flags         uint64_t violation_count() const               ref's violate(): the slot forwards new ones to the tile
//   reset         void reset(int64_t t)
//   framework     void ports(OwnPortsDecl&)                       declare the peripheral's own ports (C&C 1: READY,
//                                                                 the error lines, GO); default none
//                 void begin(sim::Ctx&, int64_t t, OwnPorts&)     level 0 of t, first: take the own inputs due (C&C 1:
//                                                                 READY arrivals, error lines); default none
//                 void end(sim::Ctx&, int64_t t, OwnPorts&)       after step(t): put the own outputs (C&C 1: GO);
//                                                                 default none
//
// OWN PORTS. The framework rewrites a port handle where it sits at the build and requires it inside the unit object,
// so a peripheral (on the heap, owned by the slot) can't hold its own handles. The slot unit holds them for it
// (`OwnPorts`, fixed capacity per kind, filled at configuration): the peripheral declares each through `OwnPortsDecl`,
// which returns the handle's index, keeps the indices (plain integers survive the build's move), and reaches the
// handles through the `OwnPorts&` its `begin` and `end` are given. The wiring code (a chip builder, a test) wires
// `slot.own.link_in[i]` / `slot.own.link_out[i]` with the indices the peripheral exposes. Kinds as needed: Link in and
// out now (C&C 1's); another kind is one more array and one more declare method.
//
// TIMING (ref's rule, Peripheral.java): an input called with t takes effect at the end of t; a level read with t sees
// the end of t - 1. On the framework (AUDIT-M3 3.1) the slot applies the inputs at level 0 of the next cycle, so:
//   - accept(t, ...) is called at level 0 of t + 3 (the director's Link(3)), with t the director's cycle. The
//     peripheral lands it at the end of t + RP_LATENCY (= t + 2), as ref does; it must accept the call though it may
//     already have been settled through t + 2 (but not t + 3). [The replayer: its landings are checked against that.]
//   - run(t), write_word(t, ...), consume_events_late(t, ...) for a store or consuming load at t are called at level 0 of t + 1,
//     before any level read of t + 1. consume_events (S3) and the EVENTS_MASK store come first, then the landings,
//     then run / write_word: ref's endCycle(t) before its settle(t + 1).
//   - The level reads the slot publishes for t + 1 are taken after those (then step(t + 1) runs).
//   - DMA (17.4; the RTL's peripheral_dma.veryl): DMA_START presented in cycle p: out_start(p, q, n) at level 0 of p + 1,
//     also for n = 0; out_ready / out_take at t >= p + 1 see the end of t - 1 and are called before step(t); the
//     transfer ends the cycle after its last take (p + 1 for n = 0), when dma_done is set and dma_busy falls at its end.
#pragma once

#include <cstdint>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"

namespace units {

// The peripheral types (PARAMS [7:0]; spec 4.17.1, D46) and the slot's dispatch tags.
enum PeriphType : uint8_t {
    PT_ABSENT = 0,
    PT_INGRESS = 1,   // Ingress/Egress, the master tile's (17.6; ingress_egress.h)
    PT_RW = 2,        // the RW engine, the memory tile's (17.7; rw_engine.h)
    PT_TRACER = 3,    // the Tracer, the tracer tile's (17.9; v1.35's behind 17.1's control block)
    PT_REPLAYER = 4,  // the replayer, every mesh tile's peripheral 1 (Replayer)
    PT_UPDATER = 5,   // v1.37: the Updater, the master tile's peripheral 2 with the host link (updater.h)
    PT_TEST = 15,     // the suites' test peripheral (TestPeripheral)
    PT_UPDATER_CTL = 16,   // v1.37: the Updater with its real controller (updater_top.h; PARAMS type 5): a dispatch tag only
};

constexpr int RP_LATENCY = 2;   // rp_latency (4.17.5)

// A slot's storage for its peripherals' own ports (a member of the slot unit; see OWN PORTS above).
struct OwnPorts {
    static constexpr unsigned MAX_LINK_IN = 16, MAX_LINK_OUT = 80, MAX_WIRE_IN = 8;
    sim::LinkIn link_in[MAX_LINK_IN] = {};
    sim::LinkOut link_out[MAX_LINK_OUT] = {};
    sim::WireIn wire_in[MAX_WIRE_IN] = {};
    uint8_t n_link_in = 0, n_link_out = 0, n_wire_in = 0;
};
// Configuration only: a peripheral declares its own ports through it, each into the slot's storage; returns the index.
class OwnPortsDecl {
public:
    OwnPortsDecl(sim::Ports& p, OwnPorts& o) : p_(p), o_(o) {}
    unsigned link_in(const char* name, unsigned width) {
        if (o_.n_link_in == OwnPorts::MAX_LINK_IN) FATAL("a peripheral's own ports: more than %u Link inputs", OwnPorts::MAX_LINK_IN);
        p_.in(o_.link_in[o_.n_link_in], name, width);
        return o_.n_link_in++;
    }
    unsigned link_out(const char* name, unsigned width) {
        if (o_.n_link_out == OwnPorts::MAX_LINK_OUT) FATAL("a peripheral's own ports: more than %u Link outputs", OwnPorts::MAX_LINK_OUT);
        p_.out(o_.link_out[o_.n_link_out], name, width);
        return o_.n_link_out++;
    }
    unsigned wire_in(const char* name) {
        if (o_.n_wire_in == OwnPorts::MAX_WIRE_IN) FATAL("a peripheral's own ports: more than %u Wire inputs", OwnPorts::MAX_WIRE_IN);
        p_.in(o_.wire_in[o_.n_wire_in], name);
        return o_.n_wire_in++;
    }
private:
    sim::Ports& p_;
    OwnPorts& o_;
};

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
    uint32_t consume_events_late(int64_t t, uint32_t bits) { return static_cast<Self*>(this)->events().consume_late(t, bits); }
    bool may_stall(int) const { return false; }
    bool store_stalls(int64_t, int) const { return false; }
    bool load_pops(int) const { return false; }
    void load_effect(int64_t, int) {}
    int out_queues() const { return 0; }
    bool out_ready(int64_t, int) const { return false; }
    void out_take(int64_t, int, uint64_t*) { FATAL("peripheral: out_take without an output queue"); }
    void out_start(int64_t, int, int64_t) {}
    void ports(OwnPortsDecl&) {}
    void begin(sim::Ctx&, int64_t, OwnPorts&) {}
    void end(sim::Ctx&, int64_t, OwnPorts&) {}
};

}  // namespace units
