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

struct LoadProfile;   // W10: below

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
    unsigned threads = 1;        // W4: OS threads for a machine split over domains (Config::domain), at most one per
                                 // domain; domains are spread over them in order. Independent chips: run_independent
    std::vector<int> cpus;       // W4 (A2, D21): thread k pinned to cpus[k % size]; empty: the detected default placement
                                 // (topology.h); a CPU of -1: that thread not pinned ({-1}: leave placement to the OS)
    bool barrier_block = false;  // W4: the barrier sleeps (futex) after its spin, instead of yielding (barrier.h)
    unsigned barrier_spins = 2000;   // W4: pauses before the barrier yields (or sleeps); 0: yield at once; ~0u: spin only
    unsigned engines = 0;        // W10, A MEASUREMENT OPTION ONLY: leave it 0. A machine split over domains is built as
                                 // one engine per thread (its domains grouped in order, min(threads, domains) groups);
                                 // engines > threads instead builds that many and runs several on a thread in turn
                                 // (W4's layout), only to measure a split's own cost (bench_threads --engines)
    // W10, placement: how a machine is split over threads (sim/planning/sessions/WORKER10/NOTES.md, "Partitioning")
    enum class Placement : uint8_t { declared, by_load } placement = Placement::declared;
                                 // declared: Config::domain's domains, grouped in order, one group per thread;
                                 // by_load: the framework forms granules (units that must stay together: joined by a
                                 // Wire, RegFile, Count, a Fifo, Hold or Reg, a Link below min_interval, or one declared
                                 // domain) and assigns them to threads, balancing load and keeping crossings few
    const LoadProfile* load = nullptr;     // by_load: measured loads (Sim::load_profile); null: Config::weight,
                                                // else 1 per unit. Read during build() only
    double crossing_ns = 0;      // by_load with a profile: a price per word crossing between threads (ns), weighed against
                                 // load in the placement objective. 0 (balance only) by default: Quest 142 found no gain
                                 // on the chip at 20 or 60 ns, a loss on the mesh; ~20 ns is a crossing's cost (Quest 141)
    unsigned min_interval = 0;   // by_load: the smallest barrier interval allowed; 0: the largest Link latency that still
                                 // leaves at least 4 granules per thread
    bool calibrate = false;      // W10: time each unit's steps (for Sim::load_profile): two clock reads per step
    bool time_threads = false;   // W10, A MEASUREMENT OPTION: a split run times its phases per thread (Sim::thread_times)
    bool observe_arrivals = false;   // W10: keep each visible Link word's arrival cycle for Sim::observe (a store per
                                     // delivered Link word; off: observe(LinkIn) is fatal while a word is visible)
};

// W10: a calibration run's load per unit (Options::calibrate, Sim::load_profile), for Placement::by_load. A plain value:
// keep it, write it out (to_text) and read it back (from_text) for later runs of the same machine.
struct LoadProfile {
    std::vector<std::string> unit;   // unit names
    std::vector<double> load;        // per unit: the time its steps took (time stamp ticks, or ns without a counter)
    std::vector<std::string> link;   // Link inputs ("unit.port")
    std::vector<double> words;       // per Link input: words delivered (each entry its width + 1)
    double ticks_per_ns = 1;         // the clock's rate during the calibration (to price a crossing word in load units)
    uint64_t cycles = 0;             // cycles the profile covers
    std::string to_text() const;     // "loadprofile v2", "cycles N", "ticks_per_ns X", then "unit NAME LOAD" and
                                     // "link NAME WORDS" lines (v1, "NAME LOAD" lines only, is still read)
    static LoadProfile from_text(const std::string& text);   // fatal on a malformed text, naming the line
};

// W10, the port observer (W8's request): one input port's contents at the end of cycle c, the last cycle run
// (Sim::now() == c + 1), into the caller's buffer. ref's order (tests/ref/StateFp.java): a Link's entries in arrival
// order, visible then in flight, each with its arrival cycle; a Fifo's visible entries; a Hold's word; a Reg's value; and
// for a Fifo, credit Fifo, Hold or Reg the put made at c, judged at c + 1.
struct PortEntry { Cycle at; Word w[3]; };    // at: a Link entry's arrival cycle (0 for the other kinds); w: its words
struct PortView {
    uint32_t n = 0;                           // entries written, in order
    bool put = false;                         // Fifo, credit Fifo, Hold, Reg: a put made at c (judged at c + 1)
    Word put_w[3] = {};                       //   its words
    bool valid = false;                       // Hold: occupied; Reg: a value has landed (either way n = 1)
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
    void (*run_cal[2])(Engine&, RunSet&);         // W10: the same, timing each step (Options::calibrate)
    void (*one[2])(Engine&, RunSet&, uint32_t);   // one instance, by Form (the debug orders)
    uint64_t (*digest)(const void* obj);          // T::digest() if it has one, else its bytes (sim.h, DEBUG CHECKS)
    bool own_digest = false;                      // W4 (E5): T::digest() is the author's; else the machine's digest
                                                  // hashes the bytes with the port handles left out (digest.cpp)
};

// The run function of a (type, form): one per run, called once per run per cycle with the run's ready instances. This
// is the whole dispatch: the step is inlined, nothing is called per port.
template <class T, bool Result>
void run_form(Engine& e, RunSet& r) {
    T* units = reinterpret_cast<T*>(r.units);
    uint64_t* b = r.ready();
    for (uint32_t wi = 0; wi < r.nwords; wi++) {        // each word read and cleared: no separate clear (marks during a
        uint64_t m = b[wi];                             // run reach only higher runs)
        if (!m) continue;
        b[wi] = 0;
        while (m) {
            const uint32_t k = (wi << 6) + uint32_t(__builtin_ctzll(m));
            m &= m - 1;
            e.enter(r, k);
            Wake w;
            if constexpr (Result) w = units[k].step_result(e.ctx); else w = units[k].step(e.ctx);
            e.after(r, k, w);
        }
    }
    r.nready = 0;
}

// W10: the same loop, timing each step into the instance's ticks (Options::calibrate only: bound in place of run_form
// at build, so an ordinary run has no clock read and no branch for it).
template <class T, bool Result>
void run_form_cal(Engine& e, RunSet& r) {
    T* units = reinterpret_cast<T*>(r.units);
    uint64_t* b = r.ready();
    for (uint32_t wi = 0; wi < r.nwords; wi++) {
        uint64_t m = b[wi];
        if (!m) continue;
        b[wi] = 0;
        while (m) {
            const uint32_t k = (wi << 6) + uint32_t(__builtin_ctzll(m));
            m &= m - 1;
            const uint64_t t0 = sim_ticks();
            e.enter(r, k);
            Wake w;
            if constexpr (Result) w = units[k].step_result(e.ctx); else w = units[k].step(e.ctx);
            e.after(r, k, w);
            e.inst_ticks[r.first + k] += sim_ticks() - t0;
        }
    }
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
        t.run_cal[0] = &run_form_cal<T, false>;
        t.run_cal[1] = nullptr;
        t.one[0] = &step_one<T, false>;
        t.one[1] = nullptr;
        if constexpr (HasResultForm<T>) { t.run[1] = &run_form<T, true>; t.one[1] = &step_one<T, true>; t.run_cal[1] = &run_form_cal<T, true>; }
        t.digest = &digest_of<T>;
        t.own_digest = HasDigest<T>;
        return t;
    }();
    return &info;
}
}  // namespace detail

class Sim;

namespace detail { struct Internals; }   // W4: the digest unit by unit (digest.h, E5) and the split into domains (E3)

class Config {
    friend class Ports;     // a unit's declarations land in the configuration being built
    friend struct detail::Internals;
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

    // W10: a unit's declared load, for Placement::by_load without a measured profile (default 1).
    void weight(const std::string& unit, double w);

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
    std::string partition() const;     // W4: the domains, threads, crossings by kind, and the barrier interval with the
                                       // crossing that set it ("one domain" for a machine that isn't split)
    // W10, the port observer (PortView above): between runs only (fatal during one), reads only, allocates nothing;
    // fatal if the port holds more than `max` entries. The handle is the one in the unit object (unit<T>(name).port);
    // a split machine's port is found by the handle's address, wherever its in-flight entries were drained.
    PortView observe(const LinkIn& p, PortEntry* out, uint32_t max) const;
    PortView observe(const FifoIn& p, PortEntry* out, uint32_t max) const;   // a credit Fifo too
    PortView observe(const HoldIn& p, PortEntry* out, uint32_t max) const;
    PortView observe(const RegIn& p, PortEntry* out, uint32_t max) const;
    // W10, the stop request (Ctx::stop): the cycle a unit asked the last run to stop in, kNoCycle if none. One domain:
    // the run returned right after it (now() == stop_cycle() + 1); split: at the barrier ending its interval, every
    // thread at the same cycle. A run clears it at its start.
    Cycle stop_cycle() const;
    // W10, the between-cycles wake (W9's driver): between runs only (fatal during one), the named unit is stepped at the
    // next cycle run, now(), exactly, in whichever domain it sits (as a timed wake due then; its own wakes are unchanged).
    void wake(const std::string& unit);
    // W10 (measurement): with Options::time_threads, a split machine's phases as shares of its threads' time over the runs
    // ("compute imbalance barrier drain publish clear end other"); "one domain" otherwise.
    std::string thread_times() const;
    // W10: after runs with Options::calibrate, the time each unit's steps took over them (its load).
    LoadProfile load_profile() const;

    Sim(Sim&& o) noexcept;
    Sim& operator=(Sim&& o) noexcept;
    ~Sim();

private:
    friend class Config;
    friend struct detail::Internals;
    struct Impl;
    explicit Sim(Impl* i) : impl_(i) {}
    void* unit_ptr(const std::string& name, const detail::TypeInfo* type) const;
    Impl* impl_ = nullptr;
};

template <class T>
T& Sim::unit(const std::string& name) { return *static_cast<T*>(unit_ptr(name, detail::type_ops<T>())); }

}  // namespace sim
