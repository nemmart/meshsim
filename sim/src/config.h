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
//     sets one bit per instance per run; the wires, register files, counts and routes; one slot pool per size class
//     (by-reference payloads, sim.h), its capacity the most payloads its ports can hold at once: each by-reference port
//     carries at most L + 2 (Link), depth + 1 (Fifo: the visible ones and one in flight), depth (credit Fifo), 2 (Hold:
//     the held one and one in flight), plus what either end declares it holds;
//   - routes: each output's route resolved once to a data value (the wheel of its kind and width in its own thread, or the
//     outbox wheel for the destination's thread: W4), so a put runs the same code either way;
//   - levels: from the Wires and RegFiles; a loop is fatal with its units named; a (type, form) runs at the largest level
//     of its instances, re-checked after that (Count `now` reads are checked at the read, debug);
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

// The resolved route of an output (engine.h's Route) is a data value: `wheel` is the first bucket of the wheel it lands in
// (its (kind, width) wheel in its own thread, or an outbox), `dest` the destination's pool index (an entry's first word:
// W6's E1, Bucket unchanged), `latency` the delay (1 for Fifo, Hold, Reg). A put of width w is
//     Bucket& b = r.wheel[(now + r.latency) & r.mask]; Word* e = b.append(); e[0] = r.dest; e[1..w] = words;
// with w a compile-time constant of the put's arity (sim.h's put overloads are fixed-arity), so the copy is fixed-size
// (D16), and the same instructions whether the destination is local or across a domain.

// Every option is implemented or refused at build() ("option X not implemented"): never silently ignored.
struct Options {
    uint64_t shuffle_seed = 0;   // 0: none; else instances and runs within a level are stepped in a seeded random order
    enum class Order : uint8_t { type_major, instance_major } order = Order::type_major;   // the debug comparison (PLAN-M1 item 6)
    bool wire_checks = true;     // wire discipline (written after a read in the cycle, written twice): debug builds
    bool step_all = false;       // step every instance every cycle (the missed-wake check, sim.h DEBUG CHECKS)
    unsigned missed_wake_after = 1;   // the missed-wake detector (debug builds; off under step-all): cycles asleep with data
                                      // waiting and nothing on its way, 0 = off
    bool missed_wake_fatal = true;    // else report and go on
    unsigned leak_check_every = 0;    // debug builds: check the slot pools every N cycles too (always at the end of a run)
    unsigned threads = 1;        // 1 only: independent chips run on threads with run_independent (W4, form 1)
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

template <class T>
concept HasDigest = requires(const T& u) { { u.digest() } -> std::convertible_to<uint64_t>; };

namespace detail {
// What the framework knows of a unit type, made once per type by a template (no RTTI).
struct TypeInfo {
    std::string name;
    size_t size, align;
    void (*move)(void* dst, void* src);           // move-construct at dst, then destroy src
    void (*destroy)(void* obj);
    void (*run[2])(Engine&, RunSet&);             // by Form: cycle, result (null if the type has no result form)
    void (*one[2])(Engine&, RunSet&, uint32_t);   // one instance, by Form (the debug orders)
    uint64_t (*digest)(const void* obj);          // T::digest() if it has one, else its bytes (sim.h, DEBUG CHECKS)
};

// The run function of a (type, form): one per run, called once per run per cycle with the run's ready instances. This
// is the whole dispatch: the step is inlined, nothing is called per port.
template <class T, bool Result>
void run_form(Engine& e, RunSet& r) {
    T* units = reinterpret_cast<T*>(r.units);
    r.ready.for_each_set([&](size_t k) {
        e.enter(r, uint32_t(k));
        Wake w;
        if constexpr (Result) w = units[k].step_result(e.ctx); else w = units[k].step(e.ctx);
        e.after(r, uint32_t(k), w);
    });
    r.ready.clear_all();
    r.nready = 0;
}

template <class T, bool Result>
void step_one(Engine& e, RunSet& r, uint32_t k) {
    T* units = reinterpret_cast<T*>(r.units);
    e.enter(r, k);
    Wake w;
    if constexpr (Result) w = units[k].step_result(e.ctx); else w = units[k].step(e.ctx);
    e.after(r, k, w);
}

template <class T>
std::string type_name() {   // from the compiler's signature, for messages ("unit Router")
    std::string f = __PRETTY_FUNCTION__;
    size_t a = f.find("T = ");
    if (a == std::string::npos) return "?";
    a += 4;
    size_t b = f.find_first_of(";]", a);
    return f.substr(a, b - a);
}

template <class T>
uint64_t digest_of(const void* p) {
    if constexpr (HasDigest<T>) return static_cast<const T*>(p)->digest();
    else if constexpr (std::is_empty_v<T>) { (void)p; return 0; }   // no state
    else return hash_bytes(p, sizeof(T));      // only with a unique object representation (static_assert at add)
}

// The type's description: pure, made once per type (a function-local static, so thread-safe) and shared by every
// configuration; each Config numbers the types it uses itself (E1), in the order they are first added to it.
template <UnitType T>
const TypeInfo* type_ops() {
    static const TypeInfo info = [] {
        TypeInfo t;
        t.name = type_name<T>();
        t.size = sizeof(T);
        t.align = alignof(T);
        t.move = [](void* dst, void* src) { ::new (dst) T(std::move(*static_cast<T*>(src))); static_cast<T*>(src)->~T(); };
        t.destroy = [](void* p) { static_cast<T*>(p)->~T(); };
        t.run[0] = &run_form<T, false>;
        t.run[1] = nullptr;
        t.one[0] = &step_one<T, false>;
        t.one[1] = nullptr;
        if constexpr (HasResultForm<T>) { t.run[1] = &run_form<T, true>; t.one[1] = &step_one<T, true>; }
        t.digest = &digest_of<T>;
        return t;
    }();
    return &info;
}
}  // namespace detail

class Sim;

class Config {
    friend class Ports;     // a unit's declarations land in the configuration being built
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

    // The missed-wake detector's threshold for one unit (D20; Options::missed_wake_after for the rest): a unit with a
    // legitimate wait the detector can't see (on a Wire, with data in an input) gets its own, and no one else's moves.
    void missed_wake_after(const std::string& unit, unsigned cycles);

    Sim build();

private:
    void* stage(const detail::TypeInfo* type, Form f, const std::string& name);
    Ports& begin_ports(void* unit);
    void end_ports();
    template <UnitType T, class... A>
    T& add_form(Form f, const std::string& name, A&&... a) {
        // What sim.h's UNIT STATE asks, as far as the compiler can check it: the build moves units into their arena, and
        // a step is called directly (no virtual call). A pointer into the unit itself can't be checked here; the staging
        // storage is poisoned for ASan once the units are moved (debug builds), so a stale one faults on first use.
        static_assert(std::is_move_constructible_v<T>, "a unit type must be move-constructible (it is moved into its arena)");
        static_assert(!std::is_polymorphic_v<T>, "a unit type has no virtual functions (D6: no virtual calls in the loop)");
        static_assert(HasDigest<T> || std::is_empty_v<T> || std::has_unique_object_representations_v<T>,
                      "a unit type with padding or pointers defines `uint64_t digest() const` (sim.h, DEBUG CHECKS)");
        void* slot = stage(detail::type_ops<T>(), f, name);
        T* u = new (slot) T(begin_ports(slot), std::forward<A>(a)...);
        end_ports();
        return *u;
    }
    struct Impl;
    Impl* impl_;
};


class Sim {
public:
    void run(Cycle n);                 // the next n cycles: now() .. now() + n - 1
    void run_until(Cycle t);           // through cycle t inclusive (a halt at t is reached); afterwards now() is t + 1
    Cycle now() const;

    // Observers, between cycles (tests, tracing): a unit by name, a port's state, a digest of every port and unit.
    template <class T> T& unit(const std::string& name);
    uint64_t digest() const;
    const Sizes& sizes() const;
    uint64_t steps(const std::string& unit_name) const;   // how many times an instance was stepped (idle-unit tests)
    uint16_t type_of(const std::string& unit_name) const; // the unit's type id in this machine (first-add order, E1)
    std::string sleepers() const;      // the end-of-run report: units asleep with data waiting, their ports, since when

    Sim(Sim&& o) noexcept;
    Sim& operator=(Sim&& o) noexcept;
    ~Sim();

private:
    friend class Config;
    struct Impl;
    explicit Sim(Impl* i) : impl_(i) {}
    void* unit_ptr(const std::string& name, const detail::TypeInfo* type) const;
    Impl* impl_ = nullptr;
};

template <class T>
T& Sim::unit(const std::string& name) { return *static_cast<T*>(unit_ptr(name, detail::type_ops<T>())); }

}  // namespace sim
