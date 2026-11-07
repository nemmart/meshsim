// sim.h: the unit author's API (WORKER3, stage 1: for review; declarations, no implementation yet).
//
// What a unit's author writes is a plain struct: its ports (handles), its own state, a constructor that declares the
// ports, and `Wake step(Ctx&)`. The framework owns every port's storage, decides who runs when, and moves the words.
// The exact rule of each port kind, with the `ref` class it comes from, is in PORTS.md; configuration (adding units,
// wiring them, sizing, building) is in config.h.
//
// ---------------------------------------------------------------------------------------------------------------------
// THE CYCLE (per thread; t is the cycle). Cycles are numbered from 1, as in `ref` (`Chip.step` runs `++cycle`) and its
// traces; configuration (loading images included) happens before cycle 1.
//
//   1. Deliver t. The framework walks the delivery buckets due at t, one wheel per port kind (and word width):
//        Link   words put at t - L join the port's visible ring;
//        Fifo   a put of t - 1 is judged against the occupancy at the start of t - 1 (PORTS.md 3): pushed, or dropped and
//               counted as an overflow;
//        Hold   a put of t - 1 is accepted if the hold is empty, else refused (the producer learns it at t);
//        Reg    a put of t - 1 replaces the value;
//        credit a take of t - 1 on a credit Fifo returns its credit to the producer;
//        wake   a unit's requested wake for t.
//      Each sets a bit in the receiving instance's ready mask and puts the instance on this cycle's run set.
//      Nothing a unit does during t changes what was delivered at t.
//   2. Run t, level by level (levels: section LEVELS below). For each level, for each run (type, form) at that level, for
//      each instance in the run's ready set: step it. While a unit steps at level k it may READ
//        - its own state;
//        - its inputs as delivered at t, minus its own takes (Link, Fifo, Hold, Reg);
//        - Wires, RegFiles and Counts (`now`) as written by units at levels below k during t, and their values from
//          earlier cycles where not rewritten;
//        - its outputs' producer-side state as of the start of t (Hold: can_put, refused, held_at_start; credits);
//      and WRITE
//        - its own state;
//        - takes on its inputs (seen by itself at once);
//        - puts on its outputs (into the bucket of t + L, L >= 1: never visible during t);
//        - its Wires, RegFile and Counts (visible to later levels during t; a changed Wire or RegFile wakes its readers
//          for this cycle).
//      A unit never reads or writes another unit's state; it holds no pointer to one (its ports are indices).
//   3. End t. Each stepped unit's Wake is scheduled (none, the next cycle, a cycle t' > t); edge bits are cleared; bucket
//      t is cleared. (W4: the barrier and the outbox merge come here, every barrier interval.)
//
// `sim/README.md`'s "commit" (staged writes and pushes applied) is step 1 of t + 1: a put at t is applied when it is
// delivered, before anything can observe it. The result is `ref`'s "a put at t is visible at t + L".
//
// THE SHIFTED END OF CYCLE. A unit that in `ref` runs after every other unit of its cycle and reads what they did
// (`TileControl.endCycle(t)`) is written as a unit at level 0 of the next cycle: what the others did at t reaches it as
// registered inputs delivered at t + 1 (Links of latency 1, Counts read `through_last`), it does `endCycle(t)`'s work, and
// drives the Wires and RegFile that the others read at t + 1 at later levels. That is `ref`'s "a store at t acts from
// t + 1; status computed at t is read at t + 1", with no phase of its own.
//
// LEVELS. Levels come from the Wires and RegFiles: a unit's level is its longest wire path from a unit with no wire
// input. All instances of a (type, form) run at one level, the largest of theirs. A combinational loop is rejected at
// build with its units named. A Count's `now` is a wire read too, but declares no edge: debug builds check at the read
// that every producer of the count sits below the reader (else read `through_last`). Rule for authors: an input that
// must be this cycle's value is a Wire; anything else is registered (Link, Fifo, Hold, Reg).
//
// WAKES. Every instance is stepped at the first cycle (to drive its initial outputs). After that a unit is stepped at t
// only if something was delivered to it at t, a Wire or RegFile it reads changed at t, or its last step asked for t (a
// Hold's refusal and a credit's return count as deliveries to the producer). Data waiting in an input does not by
// itself step the unit again: a unit that leaves an input untaken returns `next()` (or the cycle it can act) if it
// wants to see it again. So an idle unit is never visited, and a long sleep (`Wake::at_cycle(t + 10^9)`) costs one
// wake. A unit that counts cycles (mcycle) catches up from the cycle it last ran, which it keeps itself.
//
// DISPATCH. A run is keyed by (level, type, form) and calls one function pointer, generated by a template when the type
// is registered, that loops over the run's ready instances with `T::step` (or `T::step_result`) inlined: no dispatch per
// unit or per port, no virtual call. Forms: `Form::cycle` (cycle-accurate, required) and `Form::result` (D7: a type may
// add `Wake step_result(Ctx&)`, same ports, computing results without per-cycle timing), chosen per instance.
//
// COST MODEL (release). `ready` tests a bit of the instance's mask (no port is read); `peek` is a load from the port's
// ring; `take` a decrement and a bit update; a put appends one destination word and the words to a bucket of its (kind,
// width) wheel (a route lookup, no branch on the kind or the domain); a Wire read is a load, a write a compare and a store
// (and the readers' bits when it changes). A ready mark per delivered word costs ~2.7 ns at saturation (W6, R): 11-13% of
// a saturated mesh, and 1.85x faster than polling on a quiet one, which is what a chip mostly is.
//
// PAYLOADS AND WIDTHS (D16, D17). Everything in motion is 0 to 3 untyped 64-bit words; the link word is three words
// inline (D17). A unit holds a 128-bit value inline as two words (W6: 2 ns per multiply cheaper than pool indices, 25%
// more throughput at 12 threads; the `limbs` form, built with clang, D18). A payload wider than three words (a 1024-bit
// cache line on the Tracer's fill bus, after M1) travels by reference: its port declares a size class (`Ports::by_ref`,
// slots of N words), word 0 of its payload is a slot's index, the producer allocates the slot and fills it before the
// put, and the consumer frees it after taking it (or later, if it declared that it holds it). Each class's pool is sized
// at startup from the ports that carry it (config.h); allocation takes the lowest free index (deterministic); exceeding
// the size is fatal, naming the class. A Fifo overflow frees the dropped payload's slot; a refused Hold put keeps its
// slot (the producer retries with it). Across threads the outbox merge copies the slot into the destination's pool and
// rewrites the index (W4; the route already knows its class). A port's width is a runtime value in its
// declaration and is never visible to authors beyond that; inside the framework each (kind, width) has its own wheel
// with fixed-size entries and its own delivery loop, templated on the word count (widths 1, 2, 3, and 0 for events,
// which carry only the destination word): a few instantiations, each genuinely different code. Nothing is templated on
// payload types (D16).
//
// UNIT STATE. Units are constructed at configuration, then moved into one arena per type when the machine is built, and
// their port handles are rewritten in place. So a unit type must be move-constructible (checked at `add`), and a unit
// may hold no pointer into itself or into another unit: another unit is reached only through ports. Pointers to
// permanent state allocated apart (a memory from `alloc_big`, a read-only table built at configuration) are fine; they
// don't move.
//
// DEBUG CHECKS (config.h's Options), each compared by the digest of every port and unit (Sim::digest). A unit's part of
// the digest is `uint64_t digest() const` if its type defines one; nothing for a type with no state (std::is_empty_v);
// otherwise its bytes, allowed only when the type has
// no padding and nothing else whose bytes may differ between equal values (std::has_unique_object_representations_v,
// checked by a static_assert at `add`). A type with padding, or holding a pointer (to a memory allocated apart: its value
// differs between two machines built side by side), must define digest(), usually one line with `hash_fields` (below);
// so a flaky digest is a compile error, not an intermittent difference.
// The machine's digest goes unit by unit in add order (W4, E5: digest.h), so it doesn't depend on how the build numbered
// anything, and a machine split over threads gives the same digest as in one: **a unit's own digest() leaves out its port
// handles** (they are the build's numbering, not its state; the default, its bytes, leaves them out by itself), and
// **a slot index is opaque** (R1): which index a by-reference payload gets changes with the partition, so neither the
// unit's behaviour nor its digest() may depend on the value (hash the slot's contents; the framework does so for ports).
//   shuffle      within a level, runs and instances in a seeded random order: any difference is an undeclared dependence
//                (a missing Wire); bisect the level to find it.
//   two orders   type-major (each run's instances together) against instance-major (each instance's units together).
//   wires        a Wire written after a reader read it in the same cycle, or written twice in a cycle: fatal.
//   step-all     every instance stepped every cycle, whatever its wakes. Its digest must equal the ready-driven run's
//                (WORKER28's proof for idle-skipping): it catches a missed wake, which otherwise fails silently. The
//                rule it checks: a step at a cycle the unit did not ask for, with nothing delivered, changes nothing
//                but what the skipped cycles would have changed (a unit's catch-up counters count the same either way),
//                so a correct unit pays nothing for it.
//   missed wake  (D20; debug builds) at the end of every cycle, a unit asleep (not stepped next cycle, no wake pending)
//                with data waiting in an input (a Link, Fifo or Hold input with a word visible) is reported, naming the
//                unit and the port, once it has stayed so for `missed_wake_after` cycles (Options; default 1); fatal by
//                default (`missed_wake_fatal`). A unit is legitimately asleep with data waiting only when something already
//                on its way will step it, and the engine checks those for each suspect: a delivery in the wheel addressed
//                to one of its inputs (a word in flight on a Link to it), a credit output with no credit left (each of the
//                consumer's takes returns one), or a Hold put it made this cycle (a refusal at t + 1 wakes it). So a unit blocked on an output is not
//                flagged while its credit or refusal is coming; one whose put was accepted and still sleeps on its data
//                is. Not seen by this rule: a unit waiting for a Wire to change while data sits in an input; it is
//                flagged (raise `missed_wake_after` for it, or take the data into the unit's own state).
//   report       at the end of a run (Sim::sleepers), every unit asleep with data waiting, its ports and since when,
//                whether or not the detector is on.
//
// THE TWO REGIMES (D6). A unit's constructor runs at configuration (any C++). `step` runs in the cycle loop: no STL, no
// allocation (the hook in W1's alloc_guard aborts on one), no virtual calls, no exceptions, untyped 64-bit words.
// ---------------------------------------------------------------------------------------------------------------------
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "base/value_pool.h"

namespace sim {

using Word = uint64_t;
using Cycle = uint64_t;

// ---- digests ----------------------------------------------------------------------------------------------------
// hash_fields(a, b, ...): a digest of the fields named, each of a type with a unique object representation (integers,
// handles, arrays of them); a unit with padding or pointers lists the fields that are its state:
//     uint64_t digest() const { return hash_fields(x, pc, mcycle, stall_until) ^ hash_bytes(dmem, kDmemBytes); }
inline uint64_t hash_mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}
inline uint64_t hash_bytes(const void* p, size_t n, uint64_t h = 0x84222325cbf29ce4ull) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}
template <class... F>
uint64_t hash_fields(const F&... f) {
    static_assert((std::has_unique_object_representations_v<F> && ...),
                  "hash_fields: a field has padding or several representations of one value; hash its parts");
    uint64_t h = 0x9e3779b97f4a7c15ull;
    ((h = hash_mix(h, hash_bytes(&f, sizeof(F)))), ...);
    return h;
}

// ---- port handles ------------------------------------------------------------------------------------------------
// A handle is a small index into its kind's pool (ports are numbered consumer type-major, then instance), never a pointer
// to a port object. An input also carries its bit in the owner's ready mask. A unit declares at most 64 bits (inputs,
// plus a bit for each Hold or credit output, which hear refusals and credits). Handles are given provisional values by
// the declaration and rewritten to their final indices by the build; until then they are only good for wiring.

struct InPort  { uint32_t idx = ~0u; uint32_t bit = 0; };
struct OutPort { uint32_t route = ~0u; };   // index into the thread's route table: where a put's entry goes (below)

// Registered kinds: a put at t is seen at t + L (L = 1 except for a Link).
struct LinkIn    : InPort {};     // a level bit: set while a word is visible
struct LinkOut   : OutPort {};
struct FifoIn    : InPort {};     // a level bit: set while not empty
struct FifoOut   : OutPort {};    // never refused; an overflow is dropped and counted (PORTS.md 3)
struct CreditOut : OutPort { uint32_t bit = 0; };   // into a credit Fifo: room as of the start of t; an edge bit on return
struct HoldIn    : InPort {};     // a level bit: set while occupied
struct HoldOut   : OutPort { uint32_t bit = 0; };   // an edge bit set when a put is refused
struct RegIn     : InPort {};     // an edge bit: set when the value changes
struct RegOut    : OutPort {};
// This cycle's values (level-ordered).
struct WireIn    : InPort {};     // an edge bit: set when the value changes
struct WireOut   { uint32_t idx = ~0u; };
struct RegFileIn : InPort {};     // an edge bit: set when any register changes
struct RegFileOut{ uint32_t idx = ~0u; };
struct CountIn   { uint32_t idx = ~0u; };   // no bit: a Count is read, it wakes nobody
struct CountOut  { uint32_t idx = ~0u; };

// ---- wakes --------------------------------------------------------------------------------------------------------
struct Wake {
    Cycle at;                                        // the cycle to be stepped at; kNever: only on an input event
    static constexpr Cycle kNever = ~Cycle(0);
    static constexpr Wake none() { return Wake{kNever}; }
    static constexpr Wake at_cycle(Cycle t) { return Wake{t}; }
};

enum class Form : uint8_t { cycle, result };

struct Engine;   // engine.h: the framework's internals

// ---- the step's view of the machine ------------------------------------------------------------------------------
// One Ctx per thread; the framework sets the current instance before each step. Every method is inline over the thread's
// pools (stage 2). Misuse (peek of an empty input, a put of the wrong width, a second take) is DCHECK; a second put on a
// port in one cycle and every capacity overflow are fatal in every build.
class Ctx {
public:
    Cycle now() const;
    Wake next() const;                               // at(now + 1)
    Wake in(Cycle n) const;                          // at(now + n), n >= 1
    uint64_t mask() const;                           // this instance's ready bits (levels and this cycle's edges)
    void stop();                                     // W10: the run stops at the end of this cycle (one domain; split:
                                                     // at the barrier ending this cycle's interval); Sim::stop_cycle

    // Link
    bool ready(LinkIn p) const;                      // a word is visible (mask bit)
    const Word* peek(LinkIn p) const;                // the head's words
    const Word* peek(LinkIn p, unsigned k) const;    // W10: the k-th oldest visible word (0: head; k < size)
    unsigned size(LinkIn p) const;                   // W10: words visible, after this cycle's takes
    void take(LinkIn p);
    void put(LinkOut p, Word a);
    void put(LinkOut p, Word a, Word b);
    void put(LinkOut p, Word a, Word b, Word c);
    void put(LinkOut p);                             // a width-0 Link: an event (GO, READY, a line's edge)

    // Fifo (and credit Fifo, consumer side)
    bool ready(FifoIn p) const;
    const Word* peek(FifoIn p, unsigned k = 0) const;   // the k-th oldest visible entry (0: head)
    void take(FifoIn p);
    unsigned size(FifoIn p) const;                   // after this cycle's takes
    unsigned size_at_start(FifoIn p) const;          // at the start of t (WordFIFO.sizeAtStart)
    void put(FifoOut p, Word a);
    void put(FifoOut p, Word a, Word b);
    void put(FifoOut p, Word a, Word b, Word c);
    // credit Fifo, producer side
    unsigned credits(CreditOut p) const;             // room as of the start of t (this cycle's put not deducted)
    void put(CreditOut p, Word a);                   // fatal without a credit
    void put(CreditOut p, Word a, Word b);
    void put(CreditOut p, Word a, Word b, Word c);

    // Hold
    bool ready(HoldIn p) const;
    const Word* peek(HoldIn p) const;
    void take(HoldIn p);
    void put(HoldOut p, Word a);
    void put(HoldOut p, Word a, Word b);
    void put(HoldOut p, Word a, Word b, Word c);
    bool refused(HoldOut p) const;                   // the put of t - 1 was refused: put the same words again
                                                     // (an accepted put wakes no one: with more data waiting, return next())
    bool can_put(HoldOut p) const;                   // empty at the start of t ("not busy")
    bool held_at_start(HoldOut p) const;             // occupied at the start of t (WordHold.heldAtStart)

    // Reg: a registered value, many readers
    bool valid(RegIn p) const;                       // a value has landed
    const Word* read(RegIn p) const;
    void put(RegOut p, Word a);
    void put(RegOut p, Word a, Word b);
    void put(RegOut p, Word a, Word b, Word c);

    // Wire: this cycle's value (one word); its producer is at a lower level than every reader
    Word read(WireIn p) const;
    void drive(WireOut p, Word v);

    // RegFile: 32-bit registers owned by one unit, read at later levels
    uint32_t read(RegFileIn p, unsigned i) const;
    void write(RegFileOut p, unsigned i, uint32_t v);

    // Count
    void inc(CountOut p, uint64_t n = 1);
    uint64_t now(CountIn p) const;                   // every increment so far (a wire read: producers at lower levels)
    uint64_t through_last(CountIn p) const;          // as at the end of t - 1 (what endCycle(t - 1) saw)

    // By-reference payloads (PAYLOADS AND WIDTHS): a slot of the port's size class. alloc on an output; slot and free on
    // either end. Word 0 of the payload is the index.
    uint32_t alloc(LinkOut p);
    uint32_t alloc(FifoOut p);
    uint32_t alloc(CreditOut p);
    uint32_t alloc(HoldOut p);
    template <class P> Word* slot(P p, uint32_t i) const;    // P: LinkIn/Out, FifoIn/Out, CreditOut, HoldIn/Out
    template <class P> void free(P p, uint32_t i);

private:
    friend struct Engine;
    friend struct TestingAccess;   // tests/: port observers inside a step (the conformance driver)
    Engine* e_ = nullptr;
    uint32_t cur_ = 0;           // the instance being stepped
    uint64_t* mask_ = nullptr;   // its ready bits
};

// ---- declaring a unit's ports (configuration) -----------------------------------------------------------------------
// A unit type's constructor takes `Ports&` first and declares each port once, by reference to its handle member:
//
//     struct Thing {
//         FifoIn in; HoldOut out;
//         explicit Thing(Ports& p, unsigned depth) { p.in(in, "in", 3, depth); p.out(out, "out", 3); }
//         Wake step(Ctx& c);
//     };
//
// Widths are words per entry (0 for an event Link, 1 to 3 otherwise; a link word is 3, D17). Capacities that belong to the receiver are declared
// on the input (a Fifo's depth); latencies belong to the wiring (config.h). Names are copied (any std::string is fine).
class Ports {
public:
    void in(LinkIn& h, const char* name, unsigned width);
    void in(LinkIn& h, const char* name, unsigned width, unsigned depth);   // W10: the consumer may hold up to `depth`
                                                     // words (capacity L + depth; PORTS.md 2b); the line above is depth 2
    void in(FifoIn& h, const char* name, unsigned width, unsigned depth);
    void in_credit(FifoIn& h, const char* name, unsigned width, unsigned depth);
    void in(HoldIn& h, const char* name, unsigned width);
    void in(RegIn& h, const char* name, unsigned width);
    void in(WireIn& h, const char* name);
    void in(RegFileIn& h, const char* name);
    void in(CountIn& h, const char* name);

    void out(LinkOut& h, const char* name, unsigned width);
    void out(FifoOut& h, const char* name, unsigned width);
    void out(CreditOut& h, const char* name, unsigned width);
    void out(HoldOut& h, const char* name, unsigned width);
    void out(RegOut& h, const char* name, unsigned width);
    void out(WireOut& h, const char* name, Word reset_value = 0);
    void out(RegFileOut& h, const char* name, unsigned registers);
    void out(CountOut& h, const char* name);

    // A port carrying payloads by reference: its size class (slots of `slot_words` words), declared on both ends (checked
    // equal), and how many slots this end holds beyond the port's own capacity (taken and kept, or allocated ahead of
    // the put). Link, Fifo, credit Fifo and Hold only.
    void by_ref(LinkIn& h, unsigned slot_words, unsigned held = 0);
    void by_ref(LinkOut& h, unsigned slot_words, unsigned held = 0);
    void by_ref(FifoIn& h, unsigned slot_words, unsigned held = 0);
    void by_ref(FifoOut& h, unsigned slot_words, unsigned held = 0);
    void by_ref(CreditOut& h, unsigned slot_words, unsigned held = 0);
    void by_ref(HoldIn& h, unsigned slot_words, unsigned held = 0);
    void by_ref(HoldOut& h, unsigned slot_words, unsigned held = 0);

    Ports(const Ports&) = delete;
    Ports& operator=(const Ports&) = delete;

private:
    friend class Config;
    Ports() = default;
    void by_ref_decl(uint32_t id, unsigned slot_words, unsigned held);
    void* impl_ = nullptr;
};

// ---- what a unit type must provide --------------------------------------------------------------------------------
template <class T>
concept UnitType = requires(T& u, Ctx& c) {
    { u.step(c) } -> std::same_as<Wake>;
};
template <class T>
concept HasResultForm = UnitType<T> && requires(T& u, Ctx& c) {
    { u.step_result(c) } -> std::same_as<Wake>;
};

}  // namespace sim

#include "engine.h"   // the internals Ctx inlines over (not for unit authors)
