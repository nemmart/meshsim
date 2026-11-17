// updater_view.h: the whole chip with the Updater's controller (updater_wip, WORKER6). `install_ctl` puts the controller
// (units/updater_top.h) in the black box's place on a machine built with the host link, with the shim, the test LPDDR
// and the stand-in cache on the file store the Tracer reads; `UpdaterView` is what the proof bench reads of whichever is
// installed. Test-side code.
//
// THE SETTING: proof::Case::updater_ctl, off by default; or, for every case of a run, the environment variable
// MESHSIM_UPDATER_CTL (any value: on; a number above 0: the seed of the varied timing, the test LPDDR's jitter and the
// stand-in at random). With it off, the chip is v1.37's as merged.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>

#include "mach.h"
#include "standin_cache.h"
#include "test_lpddr.h"
#include "units/periph_slot.h"
#include "units/updater.h"
#include "units/updater_top.h"

namespace updater_view {

using units::tracer::FileStore;

// The environment's setting: -1 off; 0 on, fixed timing; above 0 on, varied with this seed.
inline int64_t env_setting() {
    const char* v = std::getenv("MESHSIM_UPDATER_CTL");
    if (!v) return -1;
    return std::strtoll(v, nullptr, 10) > 0 ? std::strtoll(v, nullptr, 10) : 0;
}

struct Rig {                                       // the test pieces behind the controller's ports, owned by it
    test_pieces::TestLpddr lpddr;
    test_pieces::StandInCache cache;
    Rig(FileStore& s, uint64_t seed) : lpddr(s, timing(seed)), cache(s, seed ? seed * 7 + 1 : 0) { cache.lpddr = &lpddr; }
    static units::axi::LpddrTiming timing(uint64_t seed) {
        units::axi::LpddrTiming t;
        if (seed) { t.jitter = true; t.seed = uint32_t(seed); }
        return t;
    }
};

inline units::PeriphSlot& master_slot(mach::Mach& m) { return m.c.unit<units::PeriphSlot>(units::unit_prefix(units::ID_MASTER) + "slot"); }

// The controller in the black box's place, before the first cycle; the black box's store is kept.
inline void install_ctl(mach::Mach& m, uint64_t seed) {
    units::PeriphSlot& slot = master_slot(m);
    FileStore* store = slot.s.updater(2).store;
    slot.s.per[2] = units::SlotCore::Ref{};                  // the black box deleted
    auto* top = new units::UpdaterTop();
    auto rig = std::make_shared<Rig>(*store, seed);
    top->lpddr = &rig->lpddr; top->cache = &rig->cache; top->store = store;
    top->keep = rig;
    slot.s.per[2].type = units::PT_UPDATER_CTL;
    slot.s.per[2].p = top;
    m.c.touched(units::unit_prefix(units::ID_MASTER) + "slot");
}

// What the proof bench reads of the Updater, whichever is installed.
struct UpdaterView {
    units::Updater* bb = nullptr;
    units::UpdaterTop* top = nullptr;
    using Done = void (*)(void* ctx, UpdaterView& v, int64_t t);
    Done done_fn = nullptr;
    void* done_ctx = nullptr;

    void attach(mach::Mach& m) {
        units::PeriphSlot& slot = master_slot(m);
        bb = nullptr; top = nullptr;
        if (slot.s.per[2].type == units::PT_UPDATER_CTL) top = static_cast<units::UpdaterTop*>(slot.s.per[2].p);
        else bb = &slot.s.updater(2);
    }
    bool ctl() const { return top != nullptr; }
    uint32_t run_config() const { return bb ? bb->run_config_ : top->ctl.q.run_config; }
    uint32_t word_count() const { return bb ? bb->word_count_ : top->ctl.q.word_count; }
    uint32_t page_count() const { return bb ? bb->page_count_ : top->ctl.q.page_count; }
    uint32_t image_root() const { return bb ? bb->image_root_ : top->ctl.q.image_root; }
    uint32_t tracer_root() const { return bb ? bb->tracer_root_ : top->ctl.q.tracer_root; }
    uint32_t stat(int k) const { return bb ? bb->stat_[k] : top->ctl.q.stat[k]; }
    bool busy() const { return bb ? bb->busy_ : top->ctl.q.busy; }
    FileStore* store() const { return bb ? bb->store : top->store; }
    int errors(int64_t t) const { return bb ? bb->errors(t) : top->errors(t); }
    // reads of a page nothing wrote, and for the controller every check of its test pieces
    uint64_t unwritten_reads() const { return bb ? bb->unwritten_reads : top->violation_count(); }
    unsigned segment_entries() const { return bb ? bb->segment_entries() : top->segment_entries(); }
    unsigned free_page_entries() const { return bb ? bb->free_page_entries() : top->free_page_entries(); }
    // a slow memory side: the black box's cycles for a look-up; the stand-in's for each line it reads
    void set_lookup(int cycles) {
        if (bb) bb->timing.lookup = cycles;
        else static_cast<Rig*>(top->keep.get())->cache.line_cycles = unsigned(cycles);
    }
    void on_done(Done f, void* ctx) {
        done_fn = f; done_ctx = ctx;
        if (bb) { bb->on_done = &from_bb; bb->on_done_ctx = this; }
        else { top->on_done = &from_top; top->on_done_ctx = this; }
    }

private:
    static void from_bb(void* v, units::Updater&, int64_t t) { auto* x = static_cast<UpdaterView*>(v); x->done_fn(x->done_ctx, *x, t); }
    static void from_top(void* v, units::UpdaterTop&, int64_t t) { auto* x = static_cast<UpdaterView*>(v); x->done_fn(x->done_ctx, *x, t); }
};

}  // namespace updater_view
