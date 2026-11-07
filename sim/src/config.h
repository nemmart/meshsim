// config.h: building a machine (WORKER3, stage 1: for review; declarations and the dispatch template, no implementation).
//
// The configuration regime (D6): any C++ that makes it clear. A machine is built in four steps, each once:
//   1. add units  `cfg.add<T>(name, args...)` (the name unique, copied) constructs a T (its constructor declares its ports, sim.h) and returns it
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
//   - sizing (every number reported by `sizes()`, by structure and by unit type, so tests compare them with hand-derived
//     ones and footprint per chip is tracked from M1, D15): each Link's ring L + 2 (or as declared), each Fifo's ring its
//     depth, each Hold one slot; the wheels (THE WHEEL'S SIZING RULE below); the far-wake list one per instance; the run
//     sets one bit per instance per run; the wires, register files, counts and routes; the value array for payloads above
//     128 bits only (D17);
//   - routes: each output's route resolved once to a data value (the wheel of its kind and width in its own thread, or the
//     outbox wheel for the destination's thread: W4), so a put runs the same code either way;
//   - levels: from the Wires, RegFiles and Count `now` reads; a loop is fatal with its units named; a (type, form) runs at
//     the largest level of its instances, re-checked after that;
//   - checks: every declared port wired (an unwired input or output is fatal, named), widths equal, at most 64 mask bits per
//     unit, every latency within the wheel.
//
// THE WHEEL'S SIZING RULE, and its cost. One wheel per (kind, width) per thread, of W buckets, W the next power of two
// above the largest latency on that wheel; each bucket's capacity is the number of outputs that put into that wheel (each
// puts at most once a cycle), so nothing can overflow: W x outputs x (width + 1) words. That is the worst case (every
// output putting every cycle), and it is not small: W6's mesh (F) spent 32 KB of its 127 KB on the link wheel, a quarter,
// holding ~142 of its 256 entries at a busy 0.35. Flagged for M2, not changed now: (i) a wheel per (kind, width,
// latency), L buckets each, which costs sum over outputs of L x (width + 1) words (a latency-1 output on a 4-bucket wheel
// pays 4 slots for 1); (ii) capacities from a declared peak rate, fatal when exceeded. (i) keeps "nothing can overflow";
// (ii) doesn't. Sizes reports the wheels apart so the choice can be made on M2's numbers.
//
// MIXED FIDELITY (SIMULATOR-NEXT 1, the owner's 2026-11-06 additions). A unit, or a whole tile, can be cycle-level or
// high-level per instance, chosen at configuration like a port's route, with nothing new in the cycle loop:
//   - a unit type's two forms (cycle, result) share their ports; `add` or `add_result` picks one per instance;
//   - a tile is built by an ordinary function that adds its units, wires them inside, and returns its external port
//     handles; a high-level tile is another function that adds one unit with the same external ports (kinds and widths)
//     and returns the same handles. The caller wires tiles to each other through those handles and can't tell them apart:
//
//         struct TileIO { LinkIn in[4]; LinkOut out[4]; LinkIn go; ... };          // the tile's external ports
//         TileIO tile_cycle(Config& c, unsigned k);    // router, director, ..., core: dozens of units, wired inside
//         TileIO tile_high(Config& c, unsigned k);     // one TileModel unit, the same external ports
//         for (k ...) io[k] = fast(k) ? tile_high(cfg, k) : tile_cycle(cfg, k);
//         for (k ...) for (d ...) cfg.link(io[k].out[d], io[neighbour(k, d)].in[opposite(d)], 3);
//
//   - a high-level unit emits a result at a calibrated time with the ordinary mechanism: on its input at t it computes the
//     result at once, keeps it with its due cycle t + k (k from calibration runs at cycle level), returns
//     `Wake::at_cycle(t + k)`, and at t + k puts it on its output, which delivers it at t + k + L like any put. Several
//     results in flight are a small queue in its own state, sized at configuration; its wake is the earliest due.
//   `sketch/tile_builder.cpp` compiles both and the high-level unit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "sim.h"

namespace sim {

// The resolved route of an output: where a put's entry is appended. A data value (no pointer to a port object, no
// virtual call): `wheel` is the first bucket of the wheel it lands in (its (kind, width) wheel in its own thread, or an
// outbox), `dest` the destination's pool index (an entry's first word: W6's E1, Bucket unchanged), `latency` the delay
// (1 for Fifo, Hold, Reg). A put of width w is
//     Bucket& b = r.wheel[(now + r.latency) & wheel_mask]; Word* e = b.append(); e[0] = r.dest; e[1..w] = words;
// with w a compile-time constant of the put's arity (sim.h's put overloads are fixed-arity), so the copy is fixed-size
// (D16), and the same instructions whether the destination is local or across a domain.
struct Bucket;
struct Route {
    Bucket* wheel;
    uint32_t dest;
    uint16_t latency;
    uint8_t width;     // checks only (debug: the put's arity equals the declared width)
    uint8_t kind;      // checks only
};

struct Options {
    uint64_t shuffle_seed = 0;   // 0: none; else instances and runs within a level are stepped in a seeded random order
    enum class Order : uint8_t { type_major, instance_major } order = Order::type_major;   // the debug comparison (PLAN-M1 item 6)
    bool wire_checks = true;     // wire discipline (written after a read in the cycle, written twice): debug builds
    bool step_all = false;       // step every instance every cycle (the missed-wake check, sim.h DEBUG CHECKS)
    unsigned threads = 1;        // W4
};

// Sizes computed by build(): words allocated per thread, by structure and by unit type, and per chip (D15).
struct SizeLine {
    std::string what;                 // e.g. "wheel link w3", "ring fifo w3", "run sets", "unit Router", "ports of Router"
    uint64_t words;
    uint32_t count;                   // structures (or instances) behind the line
};
struct Sizes {
    std::vector<SizeLine> by_structure;   // every pool, ring, wheel, bucket, run set, wire, count and route table
    std::vector<SizeLine> by_type;        // each unit type: its arena (unit state), and the storage of its input ports
    uint64_t total_words;                 // per thread: the sum of either list
    unsigned chips;                       // `Config::chip` scopes seen (1 if none)
    uint64_t words_per_chip;              // total / chips, when every chip has the same units (else per chip below)
    std::vector<uint64_t> per_chip;       // words attributed to each chip (its units and their input ports)
    uint32_t wheel_buckets;               // W, per wheel (kept for the sizing tests)
};

class Sim;

class Config {
public:
    explicit Config(Options o = {});
    ~Config();

    // The thread domain of the units added from now on (W4; M1 has one).
    void domain(unsigned d);
    // The chip the units added from now on belong to, for footprint accounting only (Sizes::per_chip).
    void chip(unsigned c);

    // The cycle-accurate form; add_result: the result-only form (D7), for a type that has step_result.
    template <UnitType T, class... A>
    T& add(const std::string& name, A&&... a) { return add_form<T>(Form::cycle, name, std::forward<A>(a)...); }
    template <HasResultForm T, class... A>
    T& add_result(const std::string& name, A&&... a) { return add_form<T>(Form::result, name, std::forward<A>(a)...); }

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
    void* stage(uint16_t type, size_t size, size_t align, Form f, const std::string& name);
    Ports& begin_ports(void* unit);
    void end_ports();
    template <UnitType T, class... A>
    T& add_form(Form f, const std::string& name, A&&... a) {
        // What sim.h's UNIT STATE asks, as far as the compiler can check it: the build moves units into their arena, and
        // a step is called directly (no virtual call). A pointer into the unit itself can't be checked here; the staging
        // storage is poisoned for ASan once the units are moved (debug builds), so a stale one faults on first use.
        static_assert(std::is_move_constructible_v<T>, "a unit type must be move-constructible (it is moved into its arena)");
        static_assert(!std::is_polymorphic_v<T>, "a unit type has no virtual functions (D6: no virtual calls in the loop)");
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
// The delivery loops, internal: one per (kind, width), templated on the word count (D16: widths 0-3), e.g.
//     template <unsigned W> void deliver_fifo(Engine&, const Bucket&);
// Never visible to unit authors.

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
