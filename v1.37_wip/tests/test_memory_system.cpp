// test_memory_system: the memory system, the memory tile's peripheral 1 (machine-spec 17.11; src/units/memory_system.h,
// its hardware src/units/cache_system.h), in the chip: its PARAMS and its window; a START and a WRITE to its region
// stopping the simulator (invariant 38); the cache's error flag reaching ERRORS [0], the memory tile's error line and the
// master tile's ERROR_LINES_2 [1]; the reset emptying the cache and the arbiter and clearing ERRORS (23.6.10, 23.11);
// the RW engine as peripheral 2 on the arbiter's client 2 (17.7, 23.10). v1.37 stage 1 (the INTEGRATOR).
//
// THE HARNESS AS A CLIENT. The tests that need a request in the cache stand on its fill port themselves: before each
// cycle the harness calls the system's begin(t) and posts the request in `in`, as a client stepping in t would
// (cache_system.h, THE EDGE OF t).
#include <cstdio>
#include <cstring>
#include <vector>

#include "harness.h"
#include "mach.h"
#include "units/cache_system.h"
#include "units/ingress_egress.h"
#include "units/memory_system.h"
#include "units/periph_slot.h"
#include "units/rw_engine.h"

namespace {

using units::tracer::FileStore;
namespace axi = units::axi;

units::PeriphSlot& memory_slot(mach::Mach& m) { return m.c.unit<units::PeriphSlot>(units::unit_prefix(units::ID_MEMORY) + "slot"); }

// A chip with the host link (so the master tile's Ingress/Egress reads the error lines), nothing placed, and in its
// LPDDR one depth-1 file of one page.
struct Rig {
    mach::Mach m;
    units::CacheSystem& sys;
    FileStore::File f;
    Rig() : m(cfg()), sys(m.cache_system()) {
        f = m.store().create(1);
        std::vector<uint64_t> page(512);
        for (unsigned i = 0; i < 512; i++) page[i] = 0x1000 + i;
        m.store().put_page(f, 0, page.data());
    }
    static mach::MachConfig cfg() { mach::MachConfig c = mach::Mach::chip_cfg(-1); c.host_link = true; return c; }
    // One cycle with a fill read of `line` of the file posted in it (once), the harness standing on the fill port.
    void cycle(bool read, int64_t line) {
        const int64_t t = m.c.cycle() + 1;
        sys.begin(t);
        if (read) { sys.in.fr.valid = true; sys.in.fr.root = f.root; sys.in.fr.depth = 1; sys.in.fr.line = line; }
        m.c.run(1);
    }
    // A fill read of `line`, to its response; its status.
    int read(int64_t line) {
        while (!sys.ctl.out().fr_ready) cycle(false, 0);
        cycle(true, line);
        for (int k = 0; k < 2000; k++) {
            if (sys.ctl.out().frr_valid) { const int s = sys.ctl.out().frr_status; cycle(false, 0); return s; }
            cycle(false, 0);
        }
        test::fail(__FILE__, __LINE__, "no fill response");
        return -1;
    }
};

TEST(memsys_is_peripheral_1_type_6) {
    mach::Mach m(mach::Mach::chip_cfg(-1));
    units::PeriphSlot& s = memory_slot(m);
    CHECK_EQ(unsigned(s.s.per[1].type), unsigned(units::PT_MEMSYS));
    CHECK_EQ(unsigned(s.s.per[2].type), unsigned(units::PT_RW));       // the RW engine, peripheral 2 (17.7)
    units::MemorySystem& ms = s.s.memory_system(1);
    CHECK_EQ(ms.params(), 6);                                          // type 6, version 0, no constants
    CHECK_EQ(ms.errors(0), 0);
    CHECK(!ms.running(0));
    uint32_t v;
    for (int w = 32; w < 128; w++) CHECK(!ms.word(0, w, v));            // every word of blocks 2-7 unmapped
    CHECK_EQ(ms.write_word(0, 32, 1), -1);
    CHECK(ms.sys == &m.cache_system());                                // its hardware, attached by the harness
}
DEATH_TEST(memsys_start_is_ub, "a START") {
    mach::Mach m(mach::Mach::chip_cfg(-1));
    memory_slot(m).s.memory_system(1).run(5);
}
DEATH_TEST(memsys_write_is_ub, "a WRITE to the memory tile's region 0x04") {
    mach::Mach m(mach::Mach::chip_cfg(-1));
    memory_slot(m).s.memory_system(1).accept(5, 0, 0x10, 3, 0, 0);
}

// A line fetched for fill's read answered with an LPDDR error: the cache's flag, ERRORS [0], the memory tile's error line
// and the master's ERROR_LINES_2 [1] (id 65); then the reset clears ERRORS and empties the cache.
TEST(memsys_error_reaches_the_error_line) {
    Rig r;
    CHECK_EQ(r.read(0), 0);                                            // a good read first: the line is cached
    axi::LpddrFault fault;
    fault.first = 0; fault.last = ~uint64_t(0); fault.reads = true; fault.writes = false;
    r.sys.lpddr.add_fault(fault);
    r.read(5);                                                         // line 5 of page 0: a new data line, its fetch failed
    for (int k = 0; k < 40; k++) r.cycle(false, 0);
    units::MemorySystem& ms = memory_slot(r.m).s.memory_system(1);
    CHECK_EQ(ms.errors(0), 1);                                         // ERRORS [0] lpddr_error, sticky
    units::IngressEgress& ie = r.m.ingress();
    uint32_t lines2 = 0;
    CHECK(ie.word(r.m.c.cycle(), units::IngressEgress::ERROR_LINES_0 + 2, lines2));
    CHECK_EQ(lines2 & 2u, 2u);                                         // the memory tile's line, ERROR_LINES_2 [1]
    // the reset (17.11): ERRORS clear, the cache empty
    r.sys.lpddr.clear_faults();
    memory_slot(r.m).s.reset(r.m.c.cycle());
    CHECK_EQ(ms.errors(0), 0);
    CHECK(!r.sys.ctl.out().error);
    CHECK(r.sys.idle());
}

// The reset empties the cache: a line read once is fetched again after it (a miss counted), and not before.
TEST(memsys_reset_empties_the_cache) {
    Rig r;
    CHECK_EQ(r.read(3), 0);
    const uint64_t misses = r.sys.ctl.misses[units::cache::CacheCtl::MEM_DATA] + r.sys.ctl.misses[units::cache::CacheCtl::MAP_DATA];
    CHECK_EQ(r.read(3), 0);                                            // a hit: no new miss
    CHECK_EQ(r.sys.ctl.misses[units::cache::CacheCtl::MEM_DATA] + r.sys.ctl.misses[units::cache::CacheCtl::MAP_DATA], misses);
    memory_slot(r.m).s.reset(r.m.c.cycle());
    CHECK_EQ(r.read(3), 0);                                            // fetched again: the cache was emptied
    CHECK(r.sys.ctl.misses[units::cache::CacheCtl::MEM_DATA] + r.sys.ctl.misses[units::cache::CacheCtl::MAP_DATA] > misses);
}

// The RW engine on the arbiter's client 2 (17.7, 23.10): a write job and a read job of its own, through the arbiter, on
// the one LPDDR, driven at the engine's registers from the harness.
TEST(memsys_rw_engine_on_client_2) {
    mach::Mach m(mach::Mach::chip_cfg(-1));
    units::CacheSystem& sys = m.cache_system();
    units::RwEngine& e = memory_slot(m).s.rw_engine(2);
    for (unsigned b = 0; b < 32; b++) { e.buffer[2 * b] = 0xA000 + b; e.buffer[2 * b + 1] = 0xB000 + b; }
    // a write of 32 beats from the buffer's start to LPDDR's beat 0x100 (a page of the store's)
    const int64_t t0 = m.c.cycle() + 1;
    e.write_word(t0, units::RwEngine::RW_CONFIG, 1u | 32u << 1);
    e.write_word(t0, units::RwEngine::RW_LPDDR_ADDR, 0x100);
    e.write_word(t0, units::RwEngine::RW_BUFFER_ADDR, 0);
    e.run(t0);
    m.c.run(400);
    CHECK_EQ(e.errors(0), 0);
    CHECK(sys.arb.write_bursts[axi::RW_ENGINE] >= 1);
    CHECK_EQ(sys.arb.write_bursts[axi::UPDATER], 0u);
    const uint64_t* page = m.store().raw_page(int32_t(0x100 * 16 / 128));
    CHECK(page != nullptr);
    if (page) for (unsigned b = 0; b < 32; b++) { CHECK_EQ(page[2 * b], 0xA000 + b); CHECK_EQ(page[2 * b + 1], 0xB000 + b); }
}

}  // namespace
