// updater_view.h: what the proof bench reads of the chip's Updater, its controller (units/updater_top.h) on the memory
// system's client port 1 and look-up port 1 (units/cache_system.h; machine-spec 17.10, 23.10). Test-side code.
//
// v1.37 stage 1 (the INTEGRATOR): the Updater's black box (units/updater.h) is no longer a chip's: it stays for the
// comparisons that check the controller against it (tests/ctl_bench.h, test_updater_ctl, test_updater). WORKER6's
// setting (MESHSIM_UPDATER_CTL, the controller on its test pieces in the chip) is gone with it: the controller is the
// machine. A slow memory side (set_lookup) is the LPDDR model's latencies.
#pragma once

#include <cstdint>

#include "mach.h"
#include "units/cache_system.h"
#include "units/periph_slot.h"
#include "units/updater_top.h"

namespace updater_view {

using units::tracer::FileStore;

inline units::PeriphSlot& master_slot(mach::Mach& m) { return m.c.unit<units::PeriphSlot>(units::unit_prefix(units::ID_MASTER) + "slot"); }

struct UpdaterView {
    units::UpdaterTop* top = nullptr;
    units::CacheSystem* sys = nullptr;
    using Done = void (*)(void* ctx, UpdaterView& v, int64_t t);
    Done done_fn = nullptr;
    void* done_ctx = nullptr;

    void attach(mach::Mach& m) { top = &m.updater(); sys = &m.cache_system(); }
    uint32_t run_config() const { return top->ctl.q.run_config; }
    uint32_t word_count() const { return top->ctl.q.word_count; }
    uint32_t page_count() const { return top->ctl.q.page_count; }
    uint32_t image_root() const { return top->ctl.q.image_root; }
    uint32_t tracer_root() const { return top->ctl.q.tracer_root; }
    uint32_t stat(int k) const { return top->ctl.q.stat[k]; }
    bool busy() const { return top->ctl.q.busy; }
    FileStore* store() const { return top->store; }
    int errors(int64_t t) const { return top->errors(t); }
    // the joint's checks that fired (cache_system.h, THE JOINT'S CHECKS)
    uint64_t unwritten_reads() const { return top->violation_count(); }
    unsigned input_words() const { return top->input_words(); }   // the words held behind the data input
    unsigned free_page_entries() const { return top->free_page_entries(); }
    // a slow memory side: every burst of the LPDDR model this many cycles, from 70
    void set_lookup(int cycles) { sys->lpddr.timing.read_latency = unsigned(cycles); sys->lpddr.timing.write_latency = unsigned(cycles); }
    void on_done(Done f, void* ctx) { done_fn = f; done_ctx = ctx; top->on_done = &from_top; top->on_done_ctx = this; }

private:
    static void from_top(void* v, units::UpdaterTop&, int64_t t) { auto* x = static_cast<UpdaterView*>(v); x->done_fn(x->done_ctx, *x, t); }
};

}  // namespace updater_view
