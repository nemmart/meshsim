// config.h: building a machine (WORKER3, stage 1: for review; declarations and the dispatch template, no implementation).
//
// The configuration regime (D6): any C++ that makes it clear. A machine is built in four steps, each once:
//   1. add units  `cfg.add<T>(name, args...)` constructs a T (its constructor declares its ports, sim.h) and returns it
//                 for wiring. The reference stays valid until build() (units are staged in stable storage).
//   2. wire       `cfg.link(out, in, L)`, `cfg.connect(out, in)`: an output to an input of the same kind and width.
//                 Link, Fifo, Hold and credit are one to one; Reg, Wire and RegFile fan out; a Count may have several
//                 producers (`connect(CountOut, CountIn)` and `count_overflows(FifoIn, CountIn)`) and several readers.
//   3. build      the sizing pass and the rest (below); returns the Sim. Every structure is allocated once, at its exact
//                 maximum, per thread, carved from W1's blocks; units are moved into one TypeArena per type.
//   4. run        `sim.run(n)`; observers read units and ports between cycles.
//
// build():
//   - types: each unit type gets a small integer id at its first add (no RTTI); each (type, form) one run function;
//   - numbering: ports by kind, consumer type-major then instance; handles rewritten to their final indices;
//   - sizing: per thread, each Link's ring L + 2 (or as declared), each Fifo's ring its depth, each Hold one slot, the
//     wheels (one per kind and width) of W buckets (W the next power of two above the largest latency), each bucket's
//     capacity the number of ports of that kind and width that can put into it in one cycle (one put per port per cycle;
//     wakes: one per instance), the far-wake list one per instance, the run sets one bit per instance per run, the 128-bit
//     value array (W6). `sizes()` reports every number, so tests compare them with hand-derived ones;
//   - routes: each output's route resolved once to a data value (the wheel of its kind and width in its own thread, or the
//     outbox wheel for the destination's thread: W4), so a put runs the same code either way;
//   - levels: from the Wires, RegFiles and Count `now` reads; a loop is fatal with its units named; a (type, form) runs at
//     the largest level of its instances, re-checked after that;
//   - checks: every declared port wired (an unwired input or output is fatal, named), widths equal, at most 64 mask bits per
//     unit, every latency within the wheel.
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <utility>

#include "sim.h"

namespace sim {

// The resolved route of an output: where a put's entry is appended. A data value (no pointer to a port object, no
// virtual call): `wheel` is the first bucket of the wheel it lands in (its thread's own, or an outbox), `dest` the
// destination's pool index (an entry's first word), `latency` the delay (1 for Fifo, Hold, Reg). A put is
//     Bucket& b = r.wheel[(now + r.latency) & wheel_mask]; Word* e = b.append(); e[0] = r.dest; e[1..w] = words;
// the same instructions whether the destination is local or across a domain.
struct Bucket;
struct Route {
    Bucket* wheel;
    uint32_t dest;
    uint16_t latency;
    uint8_t width;
    uint8_t kind;      // checks only
};

struct Options {
    uint64_t shuffle_seed = 0;   // 0: none; else instances and runs within a level are stepped in a seeded random order
    enum class Order : uint8_t { type_major, instance_major } order = Order::type_major;   // the debug comparison (PLAN-M1 item 6)
    bool wire_checks = true;     // wire discipline (written after a read in the cycle, written twice): debug builds
    bool step_all = false;       // step every instance every cycle (the missed-wake check, sim.h DEBUG CHECKS)
    unsigned threads = 1;        // W4
};

// Sizes computed by build(), for the acceptance tests.
struct Sizes {
    uint32_t wheel_buckets;
    uint32_t bucket_capacity[8][4];   // [kind][width]
    uint32_t ring_words, value_slots, run_sets, far_wakes;
    uint64_t total_words;             // per thread
};

class Sim;

class Config {
public:
    explicit Config(Options o = {});
    ~Config();

    // The thread domain of the units added from now on (W4; M1 has one).
    void domain(unsigned d);

    // The cycle-accurate form; add_result: the result-only form (D7), for a type that has step_result.
    template <UnitType T, class... A>
    T& add(const char* name, A&&... a) { return add_form<T>(Form::cycle, name, std::forward<A>(a)...); }
    template <HasResultForm T, class... A>
    T& add_result(const char* name, A&&... a) { return add_form<T>(Form::result, name, std::forward<A>(a)...); }

    // Wiring.
    void link(LinkOut o, LinkIn i, unsigned latency);
    void connect(FifoOut o, FifoIn i);
    void connect(CreditOut o, FifoIn i);          // i declared with Ports::in_credit
    void connect(HoldOut o, HoldIn i);
    void connect(RegOut o, RegIn i);              // fan-out: one call per reader
    void connect(WireOut o, WireIn i);            // fan-out
    void connect(RegFileOut o, RegFileIn i);      // fan-out
    void connect(CountOut o, CountIn i);          // several producers and readers
    void count_overflows(FifoIn f, CountIn c);    // the Fifo's overflows also increment c (ROUTER_OVF sums all router FIFOs)

    Sim build();

private:
    // A type's id and its run functions, made once per type by the template below (no RTTI).
    template <class T> uint16_t type_id();
    void* stage(uint16_t type, size_t size, size_t align, Form f, const char* name);
    Ports& begin_ports(void* unit);
    void end_ports();
    template <UnitType T, class... A>
    T& add_form(Form f, const char* name, A&&... a) {
        void* slot = stage(type_id<T>(), sizeof(T), alignof(T), f, name);
        T* u = new (slot) T(begin_ports(slot), std::forward<A>(a)...);
        end_ports();
        return *u;
    }
    struct Impl;
    Impl* impl_;
};

// The run function of a (type, form): one per run, called once per run per cycle with the run's ready instances. This is
// the whole dispatch: the step is inlined, nothing is called per port.
struct RunSet;      // the run's ready instances (a Bitmap over the type's instances) and the type's arena
struct Engine;      // the thread's state: Ctx, wheels, run sets
template <UnitType T>
void run_cycle_form(Engine& e, RunSet& r);         // for each ready i: e.ctx.enter(r, i); w = units[i].step(e.ctx); e.after(r, i, w)
template <HasResultForm T>
void run_result_form(Engine& e, RunSet& r);        // the same with step_result

class Sim {
public:
    void run(Cycle n);                 // n cycles
    void run_until(Cycle t);
    Cycle now() const;

    // Observers, between cycles (tests, tracing): a unit by name, a port's state, a digest of every port and unit.
    template <class T> T& unit(const std::string& name);
    uint64_t digest() const;
    const Sizes& sizes() const;
    uint64_t steps(const std::string& unit_name) const;   // how many times an instance was stepped (idle-unit tests)
};

}  // namespace sim
