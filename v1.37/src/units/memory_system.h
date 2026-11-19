// memory_system.h: the memory system, the memory tile's peripheral 1 (machine-spec 17.11; 23): type 6, version 0. Its
// hardware is the cache, the AXI arbiter and the chip's `lpddr` port, here units/cache_system.h's CacheSystem (the
// cache's controller, the arbiter, the LPDDR model), which the harness gives it after the build with the file store
// (attach). It is a peripheral so that the cache's error reaches the tile's error line (owner); nothing in it is
// started, configured or fed by a program. v1.37 stage 1 (the INTEGRATOR).
//
//   PARAMS   type 6, version 0, no constants.
//   STATUS, EVENTS   the standard bits alone, which read 0: no work, no DMA.
//   ERRORS   [0] lpddr_error, sticky until the reset: the cache's error flag (an LPDDR error response it had no client to
//            tell of: a write-back's, or a line fetched for `fill`'s read), as the cache's output shows it.
//   Its own words: none (every word of blocks 2-7 unmapped). A START, and a WRITE to its region (the memory tile's 0x04),
//   are UB (invariant 38): the simulator stops.
//
// ITS STEP takes the memory system's begin(t) (cache_system.h, THE EDGE OF t), so the units' edges go on in every cycle
// whoever else steps; it is never idle once attached, so the memory tile's slot steps it every cycle and its error is
// seen in the cycle the cache raises it. Unattached (a machine with no LPDDR in use) it is idle and does nothing.
//
// THE RESET (17.11): the cache empty (23.6.10), the arbiter empty (23.11), ERRORS clear. LPDDR's contents are kept.
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "units/cache_system.h"
#include "units/event_counts.h"
#include "units/peripheral.h"

namespace units {

struct MemorySystem : PeripheralBase<MemorySystem> {
    static constexpr int TYPE = 6;
    static constexpr int DIGEST_N = 1;
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1, EV_BITS = 2 };
    enum : int { E_LPDDR_ERROR = 1 };

    CacheSystem* sys = nullptr;                    // the hardware, the harness's after the build (attach)
    EventCounts ev;

    MemorySystem() { reset(0); }
    void attach(CacheSystem* s) { sys = s; }

    // ---- identity
    int type() const { return TYPE; }
    int params() const { return TYPE; }            // version 0, no constants
    // ---- inputs: none is defined
    void accept(int64_t t, int src, uint32_t offset, int, uint64_t, uint64_t) {
        FATAL("the memory system: undefined behavior at %lld: a WRITE to the memory tile's region 0x04 (offset 0x%x, from %d; 17.11, invariant 38)",
              (long long)t, offset, src);
    }
    void run(int64_t t) { FATAL("the memory system: undefined behavior at %lld: a START (17.11, invariant 38)", (long long)t); }
    int write_word(int64_t, int, uint32_t) { return -1; }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    // ---- level reads
    bool running(int64_t) const { return false; }
    int errors(int64_t) const { return errors_; }
    EventCounts& events() { return ev; }
    bool word(int64_t, int, uint32_t&) { return false; }
    // ---- work
    void step(int64_t t) {
        if (!sys) return;
        sys->begin(t);
        if (sys->ctl.out().error) errors_ |= E_LPDDR_ERROR;
    }
    bool idle(int64_t) const { return sys == nullptr; }
    void digest_words(int64_t* out) const { out[0] = errors_; }
    uint64_t violation_count() const { return 0; }
    void reset(int64_t t) {
        static const uint8_t depth[EV_BITS] = {1, 1};
        static const bool flags[EV_BITS] = {false, false};
        ev.init(EV_BITS, depth, flags);
        ev.clear_all();
        errors_ = 0;
        if (sys) sys->reset();
        (void)t;
    }

private:
    int errors_ = 0;
};

}  // namespace units
