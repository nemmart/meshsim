// test_tile_control.cpp: the tile control unit's registers and equations (regblock.h's RegBlockState) and the register
// bus's decode (register_bus.h), each driven directly, cycle by cycle. The expectations are the specification's, written
// here from its text: machine-spec 5.1-5.5 (the map, the access faults), 7.1 (PARAMS, STATUS), 7.2 (the events: counted
// and live, the consume rule, the one tie rule), 7.3 (the barrier), 7.4 (the errors), 8.2 (DMA_START), 11.1
// (STATUS.request_full), 12.2 (the counters) and 17.1 (a peripheral's window). The tile programs of test_rtl_tile reach
// the common paths; these are the edges they do not: a set and a clear in one cycle, a store and a decrement in one
// cycle, the arrival gate's last cycle, the release against an ARRIVE.
#include "harness.h"
#include "units/regblock.h"
#include "units/register_bus.h"

using namespace units;

namespace {

using In = RegBlockState::In;

// The events' and errors' bits (7.2, 7.4), and the tile window's words by offset (5.5): written here, not taken from
// regmap.h's names for them.
constexpr uint32_t kRelease = 1u << 0, kDmaDone = 1u << 1, kAllDone = 1u << 2, kRequestPending = 1u << 3, kPeripheral1 = 1u << 4;
constexpr uint32_t kCounterDone(unsigned n) { return 1u << (6 + n); }
constexpr uint32_t kErrCounterUnderflow = 1u << 2, kErrEarlyArrival = 1u << 6, kErrBadPacket = 1u << 1, kErrPeripheral1 = 1u << 10;
constexpr unsigned kWordStatus = 0x004 / 4, kWordErrors = 0x008 / 4, kWordEvents = 0x00C / 4, kWordMask = 0x014 / 4,
                   kWordArrive = 0x018 / 4, kWordDmaStart = 0x044 / 4, kWordRequestSend = 0x10C / 4;
constexpr unsigned kWordCounter(unsigned n) { return (0x140 + 4 * n) / 4; }

In store(unsigned word, uint32_t value) { In in; in.store = true; in.word = word; in.value = value; return in; }
In decrement(unsigned counter) { In in; in.decrement = counter; return in; }
In idle() { return In(); }

RegBlockState compute_tile() {
    RegBlockState s;
    s.straps.tile_id = 5; s.straps.peripherals = 1; s.straps.barrier = true;
    return s;
}

}  // namespace

// 7.1: PARAMS holds the straps alone: tile_id [6:0], peripherals [9:7], receiver [10], request_target [13:11], attach
// [16:14], barrier [17], boot_local [18]. The reset (18): EVENTS_MASK is release and all_counters_done; all else 0.
TEST(params_and_the_reset) {
    RegBlockState s = compute_tile();
    CHECK_EQ(s.reg(PARAMS), 5u | 1u << 7 | 1u << 17);
    CHECK_EQ(s.reg(kWordStatus), 0u);
    CHECK_EQ(s.reg(kWordErrors), 0u);
    CHECK_EQ(s.reg(kWordEvents), 0u);
    CHECK_EQ(s.reg(kWordMask), 0x5u);
    CHECK(!s.wake());
    CHECK(!s.nmi());
    Straps memory;                                           // the memory tile's (2.1): tile 65, target 1, its own attach bit
    memory.tile_id = 65; memory.peripherals = 1; memory.receiver = true; memory.request_target = 1; memory.attach = 2; memory.boot_local = true;
    CHECK_EQ(params_value(memory), 65u | 1u << 7 | 1u << 10 | 1u << 11 | 2u << 14 | 1u << 18);
}

// 12.2: a store of N adds N and arms; each packet counts 1; a decrement that leaves the counter at 0 while armed sets
// counter_n_done and, with every counter at 0, all_counters_done, and disarms; a store and a decrement in one cycle
// give count + N - decrement, so a drain in the arming cycle sets the events; a decrement at count + N = 0 leaves 0,
// sets ERRORS.counter_underflow and no event; a store of 0 does nothing.
TEST(counters_and_their_events) {
    RegBlockState s = compute_tile();
    uint64_t t = 1;
    s.end_cycle(t++, store(kWordCounter(3), 2));
    CHECK_EQ(s.reg(kWordCounter(3)), 2u);
    CHECK_EQ(s.reg(kWordEvents), 0u);
    s.end_cycle(t++, decrement(3));
    CHECK_EQ(s.reg(kWordCounter(3)), 1u);
    CHECK_EQ(s.reg(kWordEvents), 0u);
    s.end_cycle(t++, decrement(3));
    CHECK_EQ(s.reg(kWordCounter(3)), 0u);
    CHECK_EQ(s.reg(kWordEvents), kCounterDone(3) | kAllDone);
    CHECK(s.wake());                                         // EVENTS_MASK's reset value selects all_counters_done
    // two counters armed: the first drain sets its own event alone
    s.end_cycle(t++, store(kWordCounter(1), 1));             // arming any counter clears all_counters_done
    CHECK_EQ(s.reg(kWordEvents), kCounterDone(3));
    s.end_cycle(t++, store(kWordCounter(2), 1));
    s.end_cycle(t++, decrement(1));
    CHECK_EQ(s.reg(kWordEvents), kCounterDone(3) | kCounterDone(1));
    s.end_cycle(t++, decrement(2));
    CHECK_EQ(s.reg(kWordEvents), kCounterDone(3) | kCounterDone(1) | kCounterDone(2) | kAllDone);
    // a store and a decrement in one cycle: 0 + 1 - 1 = 0, a drain in the arming cycle: the events stand
    In both = store(kWordCounter(4), 1);
    both.decrement = 4;
    s.end_cycle(t++, both);
    CHECK_EQ(s.reg(kWordCounter(4)), 0u);
    CHECK_EQ(s.reg(kWordEvents) & (kCounterDone(4) | kAllDone), kCounterDone(4) | kAllDone);
    CHECK_EQ(s.reg(kWordErrors), 0u);
    // ... and 3 + 2 - 1 = 4 when the counter holds 3
    s.end_cycle(t++, store(kWordCounter(5), 3));
    In add = store(kWordCounter(5), 2);
    add.decrement = 5;
    s.end_cycle(t++, add);
    CHECK_EQ(s.reg(kWordCounter(5)), 4u);
    // a store of 0 does nothing: it neither arms nor clears
    const uint32_t before = s.reg(kWordEvents);
    s.end_cycle(t++, store(kWordCounter(6), 0));
    CHECK_EQ(s.reg(kWordEvents), before);
    // an underflow: counter 7 is 0
    s.end_cycle(t++, decrement(7));
    CHECK_EQ(s.reg(kWordCounter(7)), 0u);
    CHECK_EQ(s.reg(kWordErrors), kErrCounterUnderflow);
    CHECK_EQ(s.reg(kWordEvents) & kCounterDone(7), 0u);
    CHECK(s.nmi());
}

// 7.2: a load of EVENTS consumes the counted bits it returned that EVENTS_MASK selects, and no other; an event set in
// the cycle of the consumption, after the load's sample point, survives; a wake consumes nothing.
TEST(the_consume_rule) {
    RegBlockState s = compute_tile();
    uint64_t t = 1;
    s.end_cycle(t++, store(kWordCounter(1), 1));
    s.end_cycle(t++, decrement(1));
    CHECK_EQ(s.reg(kWordEvents), kCounterDone(1) | kAllDone);
    s.end_cycle(t++, store(kWordMask, 0xFFFFFFFF));          // 14 bits, one per event
    CHECK_EQ(s.reg(kWordMask), 0x3FFFu);
    s.end_cycle(t++, store(kWordMask, kCounterDone(1)));
    CHECK(s.wake());
    In load;
    load.events_load = true;
    s.end_cycle(t++, load);
    CHECK_EQ(s.reg(kWordEvents), kAllDone);                  // counter_1_done consumed; all_counters_done not selected
    CHECK(!s.wake());
    // the set of the consumption's cycle survives: counter 2 drains while a load consumes counter_2_done's earlier set
    s.end_cycle(t++, store(kWordMask, kCounterDone(2)));
    s.end_cycle(t++, store(kWordCounter(2), 1));
    s.end_cycle(t++, decrement(2));
    s.end_cycle(t++, store(kWordCounter(2), 1));             // armed again: the event cleared by the arming
    CHECK_EQ(s.reg(kWordEvents) & kCounterDone(2), 0u);
    s.end_cycle(t++, decrement(2));
    CHECK_EQ(s.reg(kWordEvents) & kCounterDone(2), kCounterDone(2));
    // a set in the cycle a load consumes the event's earlier occurrence: the load returned the earlier one, and the
    // new one survives (a release pending, consumed, and the next release in the same cycle)
    s.end_cycle(t++, store(kWordMask, kRelease));
    In release;
    release.release = true;
    s.end_cycle(t++, release);
    CHECK_EQ(s.reg(kWordEvents) & kRelease, kRelease);
    In race = release;
    race.events_load = true;
    s.end_cycle(t++, race);
    CHECK_EQ(s.reg(kWordEvents) & kRelease, kRelease);
    s.end_cycle(t++, load);                                  // and a load alone consumes it
    CHECK_EQ(s.reg(kWordEvents) & kRelease, 0u);
}

// 7.3: ARRIVE sets STATUS.arrived and clears the counter events, and neither arms nor disarms them; with a counter
// non-zero it sets ERRORS.early_arrival. The tile is ready when arrived and the core is in the canonical wait, and
// BARRIER_ARRIVE pulses for one cycle after it becomes ready. From ARRIVE until the cycle after the release the wake is
// EVENTS.release alone. The release sets EVENTS.release and clears STATUS.arrived, and wins over an ARRIVE.
TEST(the_barrier) {
    RegBlockState s = compute_tile();
    uint64_t t = 1;
    s.end_cycle(t++, store(kWordMask, 0x3FFF));
    In done;                                                 // the end of a transfer: dma_done, which the mask selects
    done.ag_done = true;
    s.end_cycle(t++, done);
    CHECK_EQ(s.reg(kWordEvents), kDmaDone);
    CHECK(s.wake());
    In arrive = store(kWordArrive, 1);
    arrive.ag_done = true;
    s.end_cycle(t++, arrive);
    CHECK_EQ(s.reg(kWordStatus), 1u);
    CHECK_EQ(s.reg(kWordEvents), kDmaDone);                  // every other event still sets, and stays
    CHECK(!s.wake());                                        // the gate: the wake is EVENTS.release alone
    CHECK(!s.arrive_pulse);                                  // not ready: the core is not in the canonical wait
    In level = done;
    s.canon = true;                                          // core_status.canonical_wait
    s.end_cycle(t++, level);
    CHECK(s.arrive_pulse);
    s.end_cycle(t++, level);
    CHECK(!s.arrive_pulse);                                  // one cycle's pulse
    In release = done;
    release.release = true;
    s.end_cycle(t++, release);
    CHECK_EQ(s.reg(kWordStatus), 0u);
    CHECK_EQ(s.reg(kWordEvents), kDmaDone | kRelease);
    CHECK(s.wake());
    s.end_cycle(t++, done);
    CHECK(s.wake());                                         // the gate is off: dma_done and release, which the mask selects
    // the release wakes the core whatever EVENTS_MASK holds: with a mask of 0, the wake in the cycle after the release
    // is the gate's (EVENTS.release alone), and in the cycle after that the mask's, none
    In consume = done;
    consume.events_load = true;
    s.end_cycle(t++, consume);                               // (the release and dma_done consumed)
    CHECK_EQ(s.reg(kWordEvents), 0u);
    s.end_cycle(t++, store(kWordMask, 0));
    s.end_cycle(t++, store(kWordArrive, 1));
    In again;
    again.release = true;
    s.end_cycle(t++, again);
    CHECK_EQ(s.reg(kWordEvents), kRelease);
    CHECK(s.wake());
    s.end_cycle(t++, idle());
    CHECK_EQ(s.reg(kWordEvents), kRelease);
    CHECK(!s.wake());
    // an ARRIVE with bit 0 clear does nothing
    s.end_cycle(t++, store(kWordArrive, 0));
    CHECK_EQ(s.reg(kWordStatus), 0u);
    // the release wins over an ARRIVE of the same cycle
    In both = store(kWordArrive, 1);
    both.release = true;
    s.end_cycle(t++, both);
    CHECK_EQ(s.reg(kWordStatus), 0u);
    // without the barrier's nets (a special tile) the release is ignored and BARRIER_ARRIVE never pulses
    RegBlockState special;
    special.straps.tile_id = 64;
    In r;
    r.release = true;
    special.end_cycle(1, r);
    CHECK_EQ(special.reg(kWordEvents), 0u);
}

// 7.2, 7.3: ARRIVE's clear wins over a set of the same cycle, and ARRIVE does not disarm: a later drain still sets the
// event. An ARRIVE in a cycle that starts with a counter non-zero sets ERRORS.early_arrival.
TEST(arrive_against_the_counter_events) {
    RegBlockState s = compute_tile();
    uint64_t t = 1;
    s.end_cycle(t++, store(kWordCounter(1), 2));
    In arrive = store(kWordArrive, 1);
    arrive.decrement = 1;                                    // 2 -> 1 in ARRIVE's cycle: no drain, and the arming stays
    s.end_cycle(t++, arrive);
    CHECK_EQ(s.reg(kWordErrors), kErrEarlyArrival);
    CHECK_EQ(s.reg(kWordEvents), 0u);
    s.end_cycle(t++, decrement(1));                          // still armed: the drain sets both events
    CHECK_EQ(s.reg(kWordEvents), kCounterDone(1) | kAllDone);
    s.end_cycle(t++, store(kWordCounter(2), 1));
    In tie = store(kWordArrive, 1);                          // the drain and ARRIVE in one cycle: the clear wins
    tie.decrement = 2;
    s.end_cycle(t++, tie);
    CHECK_EQ(s.reg(kWordEvents), 0u);
    CHECK_EQ(s.reg(kWordCounter(2)), 0u);
}

// 8.2: DMA_START clears EVENTS.dma_done, and its clear wins over a set of the same cycle; STATUS.dma_busy is 1 from
// the start. 11.1: STATUS.request_full is 1 while the request queue, of REQUEST_QUEUE_DEPTH = 4, is full.
TEST(dma_start_and_the_request_queue) {
    RegBlockState s = compute_tile();
    uint64_t t = 1;
    In done;
    done.ag_done = true;
    s.end_cycle(t++, done);
    CHECK_EQ(s.reg(kWordEvents), kDmaDone);
    s.end_cycle(t++, store(kWordDmaStart, 1));               // (the generator's status no longer says done)
    CHECK_EQ(s.reg(kWordEvents), 0u);
    CHECK_EQ(s.reg(kWordStatus), 2u);                        // dma_busy
    In tie = store(kWordDmaStart, 1);                        // the previous transfer's end and the start in one cycle
    s.end_cycle(t++, idle());
    s.end_cycle(t++, done);
    s.end_cycle(t++, idle());                                // (done fell: the next rise is a new end)
    tie.ag_done = true;
    s.end_cycle(t++, tie);
    CHECK_EQ(s.reg(kWordEvents), 0u);
    for (unsigned i = 0; i < 3; i++) { s.end_cycle(t++, store(kWordRequestSend, 0)); CHECK_EQ(s.reg(kWordStatus) & 8u, 0u); }
    s.end_cycle(t++, store(kWordRequestSend, 0));
    CHECK_EQ(s.reg(kWordStatus) & 8u, 8u);
    In freed;
    freed.freed = true;
    s.end_cycle(t++, freed);
    CHECK_EQ(s.reg(kWordStatus) & 8u, 0u);
}

// 7.4: ERRORS' bits are sticky until the reset, except the peripherals', which are live and 0 for an absent peripheral;
// a non-zero ERRORS raises the NMI and the error line. 7.2: a peripheral's event is live too.
// The live levels reach the core a cycle after a load reads them (the RTL's rtl/blocks/tile_control_unit/README.md,
// "Timing": meip and nmi come from flip-flops, "a live event's or a peripheral error's level in t reaches it in t + 1
// too, a cycle after a load presented in t reads it"; so "a live event that falls may leave the wake up for one more
// cycle"). `slot_live` is the level of the cycle after the edge, which a load of that cycle reads; `edge` is the tile
// control unit's step: the edge, then the levels it will sample at the next (tile_ctl.h).
TEST(errors_and_the_live_bits) {
    RegBlockState s = compute_tile();
    uint64_t t = 1;
    auto edge = [&](const In& in) { s.end_cycle(t++, in); s.slot_live_ff = s.slot_live; s.request_pending_ff = s.request_pending; };
    s.slot_live = 1u << 3 | 1u << 4 | 1u;                   // peripheral 1's and 2's ERRORS != 0; peripheral 1's event
    edge(idle());
    CHECK_EQ(s.reg(kWordErrors), kErrPeripheral1);           // peripheral 2 is absent (PARAMS.peripherals)
    CHECK_EQ(s.reg(kWordEvents), kPeripheral1);              // a load of this cycle reads the levels
    CHECK(!s.nmi());                                         // the NMI and the wake follow them a cycle late
    edge(store(kWordMask, kPeripheral1));
    CHECK(s.nmi());
    CHECK(s.wake());                                         // EVENTS_MASK selects it from this edge
    In load;                                                 // a consuming load changes no live bit
    load.events_load = true;
    edge(load);
    CHECK_EQ(s.reg(kWordEvents), kPeripheral1);
    s.slot_live = 0;
    edge(idle());
    CHECK_EQ(s.reg(kWordErrors), 0u);
    CHECK_EQ(s.reg(kWordEvents), 0u);
    CHECK(s.nmi());                                          // one more cycle
    CHECK(s.wake());
    edge(idle());
    CHECK(!s.nmi());
    CHECK(!s.wake());
    s.request_pending = true;                                // EVENTS.request_pending's level, likewise
    edge(store(kWordMask, kRequestPending));
    CHECK_EQ(s.reg(kWordEvents), kRequestPending);
    CHECK(!s.wake());
    edge(idle());
    CHECK(s.wake());
    s.request_pending = false;
    edge(idle()); edge(idle());
    CHECK(!s.wake());
    In bad;
    bad.error_pulses = kErrBadPacket;
    edge(bad);
    CHECK(s.nmi());                                          // a sticky error raised at the end of t is the NMI in t + 1
    edge(idle());
    CHECK_EQ(s.reg(kWordErrors), kErrBadPacket);             // sticky
    CHECK(s.nmi());
}

// 5.1-5.5, 17.1: the decode of region 0xFF. A tile with peripheral 1 alone, without a DMA, then with one.
TEST(register_bus_decode) {
    using bus::Target;
    bus::Straps straps;
    straps.peripherals = 1;
    auto load = [&](uint32_t a) { return bus::decode(straps, a, false, true).target; };
    auto store = [&](uint32_t a) { return bus::decode(straps, a, true, true).target; };
    // the tables, windows 0-10: every word loaded and stored; window 11 and everything below 0xFFFF_E000 unmapped
    for (uint32_t a : {0xFFFFE000u, 0xFFFFE3FCu, 0xFFFFE400u, 0xFFFFE800u, 0xFFFFEC00u, 0xFFFFF000u, 0xFFFFF400u, 0xFFFFF5FCu}) {
        CHECK(load(a) == Target::TABLE);
        CHECK(store(a) == Target::TABLE);
    }
    for (uint32_t a : {0xFFFFF600u, 0xFFFFF7FCu, 0xFFFFDFFCu, 0xFFFF0000u, 0xFF000000u}) {
        CHECK(load(a) == Target::FAULT);
        CHECK(store(a) == Target::FAULT);
    }
    // the tile window (5.5): R, a store faults; W, a load reads 0; R/W both; an undefined word and block 7 unmapped
    for (uint32_t a : {0xFFFFF800u, 0xFFFFF804u, 0xFFFFF808u, 0xFFFFF80Cu, 0xFFFFF810u, 0xFFFFF8CCu, 0xFFFFF910u, 0xFFFFF91Cu}) {   // R
        CHECK(load(a) == Target::TILE);
        CHECK(store(a) == Target::FAULT);
    }
    for (uint32_t a : {0xFFFFF818u, 0xFFFFF844u, 0xFFFFF888u, 0xFFFFF890u, 0xFFFFF894u, 0xFFFFF89Cu, 0xFFFFF8C8u, 0xFFFFF900u, 0xFFFFF90Cu}) {   // W
        CHECK(load(a) == Target::ZERO);
        CHECK(store(a) == Target::TILE);
    }
    for (uint32_t a : {0xFFFFF814u, 0xFFFFF840u, 0xFFFFF880u, 0xFFFFF884u, 0xFFFFF8C0u, 0xFFFFF8C4u, 0xFFFFF940u, 0xFFFFF944u, 0xFFFFF95Cu, 0xFFFFF980u}) {   // R/W
        CHECK(load(a) == Target::TILE);
        CHECK(store(a) == Target::TILE);
    }
    for (uint32_t a : {0xFFFFF81Cu, 0xFFFFF83Cu, 0xFFFFF848u, 0xFFFFF8A0u, 0xFFFFF8D0u, 0xFFFFF920u, 0xFFFFF960u, 0xFFFFF984u, 0xFFFFF9C0u, 0xFFFFF9FCu}) {
        CHECK(load(a) == Target::FAULT);
        CHECK(store(a) == Target::FAULT);
    }
    // the word a decoded tile access names: the window's word, block << 4 | word
    CHECK_EQ(bus::decode(straps, 0xFFFFF95C, true, true).word, 5u * 16 + 7);
    // a sub-word access anywhere in region 0xFF
    CHECK(bus::decode(straps, 0xFFFFF800, false, false).target == Target::FAULT);
    CHECK(bus::decode(straps, 0xFFFFE000, true, false).target == Target::FAULT);
    // peripheral 1's window (17.1), 0xFFFF_FA00: the control block as the tile's, START W; without a DMA every word of
    // block 1 reads 0 and a store there faults; its own words are its type's; peripherals 2 and 3 are absent
    for (uint32_t a : {0xFFFFFA00u, 0xFFFFFA04u, 0xFFFFFA08u, 0xFFFFFA0Cu, 0xFFFFFA10u}) {
        CHECK(load(a) == Target::PERIPH_CONTROL);
        CHECK(store(a) == Target::FAULT);
    }
    CHECK(load(0xFFFFFA14) == Target::PERIPH_CONTROL);
    CHECK(store(0xFFFFFA14) == Target::PERIPH_CONTROL);
    CHECK(load(0xFFFFFA18) == Target::ZERO);
    CHECK(store(0xFFFFFA18) == Target::PERIPH_CONTROL);
    CHECK(load(0xFFFFFA1C) == Target::FAULT);
    for (uint32_t a : {0xFFFFFA40u, 0xFFFFFA44u, 0xFFFFFA48u, 0xFFFFFA7Cu}) {
        CHECK(load(a) == Target::ZERO);
        CHECK(store(a) == Target::FAULT);
    }
    CHECK(load(0xFFFFFA80) == Target::PERIPH_OWN);
    CHECK(store(0xFFFFFBFC) == Target::PERIPH_OWN);
    CHECK_EQ(bus::decode(straps, 0xFFFFFA80, false, true).n, 1u);
    CHECK_EQ(bus::decode(straps, 0xFFFFFA84, false, true).word, 33u);
    for (uint32_t a : {0xFFFFFC00u, 0xFFFFFC40u, 0xFFFFFE00u, 0xFFFFFFFCu}) {
        CHECK(load(a) == Target::FAULT);
        CHECK(store(a) == Target::FAULT);
    }
    // with a DMA: DMA_HDR and DMA_DEST_ADDR R/W, DMA_START W, the rest of block 1 unmapped
    straps.peripheral_dmas = 1;
    CHECK(load(0xFFFFFA40) == Target::PERIPH_CONTROL);
    CHECK(store(0xFFFFFA44) == Target::PERIPH_CONTROL);
    CHECK(load(0xFFFFFA48) == Target::ZERO);
    CHECK(store(0xFFFFFA48) == Target::PERIPH_CONTROL);
    CHECK(load(0xFFFFFA4C) == Target::FAULT);
    CHECK(store(0xFFFFFA4C) == Target::FAULT);
}
