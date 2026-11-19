// test_core_tile.cpp: the core (src/units/rv32.h, the RTL's) on the tile's environment (rv32.h's header: bus_kind,
// bus_load, bus_store, meip, nmi, core_status), access by access: which cycle the core asks what an access is, makes
// it and retires its instruction in, for a table, a register window, a store that may stall and an access fault; what
// it never hands the environment (DMEM, and IMEM and the boot ROM as data); core_status's pulses; the wake. A scripted
// environment, no framework: tile_ctl.h's TileCore::Env is the real one, and tests/test_rtl_tile.cpp runs it.
//
// The cycles (rv32.h's: step(env, 1) is the first after reset) rest on:
//   (R0) the first instruction is in execute in cycle 4 and retires in 5, and straight-line code retires one a cycle
//        (the core specification's §1, Pipeline); a load or store is raised in the cycle q its instruction leaves
//        execute, 4 for the first, and leaves writeback, retiring, in the cycle of its answer;
//   (data) "one outstanding operation. The next request may be raised in the cycle the previous response arrives" (§1,
//        Data);
//   (bus) the register bus's timing (Snarky One's rtl/blocks/register_bus/src/register_bus.veryl, its header's
//        "Timing"), for a request the bus accepts in cycle t: "A table access is presented on its table's interface
//        in cycle t ... The bus answers in t + 1"; "A register-window access is ... presented on its owner's interface
//        in t + 1 ... The owner answers a load in t + 1 with rdata from its registers, the state at the end of t ...
//        the bus ... answers in t + 2"; "A store that may stall ... is presented from t + 1 in every cycle until its
//        owner's ready is 1, and takes effect at the end of that cycle c; the bus answers in c + 1. Every other store
//        to a register window is answered in t + 1"; "An access fault is answered in t + 1, with error 1 and read data
//        0", and "reaches no owner";
//   (map) machine-spec 5.1: "IMEM and the boot ROM are fetched only" and DMEM is the core's, 32 KiB; 6.2 and the core
//        specification's §1, Core ports: core_status's access_fault and illegal_instruction are "one-cycle pulses from
//        registers, in the cycle after the core takes" the trap;
//   (wake) the core specification's §1, Resume timing: with the wake condition first holding in R, "`wfi_sleeping` is 0
//        from R + 1, and the instruction after the WFI moves into execute at the end of R + 1 and, with nothing else
//        stalling, retires in R + 3"; Core ports: core_status_canonical_wait is "1 in the cycle after each cycle in
//        which `wfi_sleeping` is 1", the instruction port idle.
#include <cstdint>
#include <vector>

#include "harness.h"
#include "rv32_asm.h"
#include "units/rv32.h"

using namespace units;
using namespace rv32_asm;

namespace {

// Addresses of v1.36's map, written here (machine-spec 5.1, 5.2, 5.5).
constexpr uint32_t IMEM = 0x01000000, ROM = 0x02000000, HIST = 0xFFFFE000, UNMAPPED = 0xFFFFF600, TILE_WINDOW = 0xFFFFF800,
                   PARAMS_ADDRESS = TILE_WINDOW, EVENTS_ADDRESS = TILE_WINDOW + 0xC, WRITE_W0_ADDRESS = TILE_WINDOW + 0x88,
                   WRITE_SEND_32_ADDRESS = TILE_WINDOW + 0x94;
constexpr uint32_t ACCESS_FAULT_BIT = 7, ILLEGAL_INSTRUCTION_BIT = 8;      // ERRORS' bits (7.4)
constexpr uint32_t CAUSE_ILLEGAL = 2, CAUSE_LOAD_FAULT = 5, CAUSE_STORE_FAULT = 7;

struct Asked { uint64_t t; uint32_t a; bool store, whole; };
struct Made { uint64_t t; uint32_t a, v; bool store; };
struct Pulse { uint64_t t; uint32_t bit; };

// The scripted tile: what each address of region 0xFF is, by the test's sets; every call recorded with its cycle.
struct Env {
    uint64_t now = 0;
    bool meip_level = false, nmi_level = false;
    std::vector<uint32_t> faults, stalls;                    // the addresses that fault; the stores that may stall
    bool ready = true;                                       // a stalling store's owner takes it in this cycle
    uint32_t view = 0;                                       // what a load made in this cycle returns
    std::vector<Asked> asked;
    std::vector<Made> made;
    std::vector<Pulse> pulses;
    uint32_t refused = 0;
    static bool in(const std::vector<uint32_t>& set, uint32_t a) { for (uint32_t x : set) if (x == a) return true; return false; }
    bool meip() const { return meip_level; }
    bool nmi() const { return nmi_level; }
    void core_status(uint32_t bit) { pulses.push_back({now, bit}); }
    BusKind bus_kind(uint32_t a, bool store, bool whole) {
        asked.push_back({now, a, store, whole});
        if (!whole || in(faults, a)) return BusKind::fault;
        if (a < UNMAPPED) return BusKind::table;
        return store && in(stalls, a) ? BusKind::window_stalls : BusKind::window;
    }
    uint32_t bus_load(uint64_t t, uint32_t a) { made.push_back({t, a, 0, false}); return view; }
    bool bus_store(uint64_t t, uint32_t a, uint32_t v) {
        if (in(stalls, a) && !ready) { refused++; return false; }
        made.push_back({t, a, v, true});
        return true;
    }
};

struct M {
    std::vector<uint32_t> dmem, imem, rom;
    TraceBuffer tb;
    Rv32 core;
    Env env;
    uint64_t t = 0;
    uint64_t asked_for = 0;                                  // what the last step returned
    M() : dmem(MEM_WORDS, 0), imem(MEM_WORDS, 0), rom(BOOTROM_MAX_WORDS, 0), tb(1024) {
        core.dmem = dmem.data(); core.imem = imem.data(); core.bootrom = rom.data(); core.bootrom_words = BOOTROM_MAX_WORDS;
        core.log = &tb.log;
        core.reset_trap_vector = IMEM + 0x100;
        core.reset(0, IMEM);
        for (uint32_t i = 0x40; i < 0x48; i++) imem[i] = NOP;   // the handler
    }
    void prog(std::initializer_list<uint32_t> ins) { uint32_t at = 0; for (uint32_t i : ins) imem[at++] = i; }
    uint64_t step() { env.now = ++t; return asked_for = core.step(env, t); }
    void run_to(uint64_t cycle) { while (t < cycle) step(); }
    uint64_t retired(uint32_t pc) const {                    // the cycle the instruction at pc retires in; 0: it did not
        for (uint32_t i = 0; i < tb.log.n; i++)
            if (tb.log.rec[i].kind == T_RETIRE && tb.log.rec[i].w[0] == pc) return tb.log.rec[i].cycle;
        return 0;
    }
};

}  // namespace

// (bus): a window store is made in the cycle after it is raised and answered in that cycle; (data): the next is raised
// as it is answered. Three stores, raised in 4, 5 and 6: made in 5, 6 and 7, where each retires.
TEST(window_stores_are_made_and_answered_the_cycle_after_they_are_raised) {
    M m;
    m.core.x[1] = TILE_WINDOW; m.core.x[2] = 0x22;
    m.prog({SW(2, 1, 0x88), SW(2, 1, 0x8C), SW(2, 1, 0x90), NOP});
    m.run_to(4);
    CHECK_EQ(m.env.asked.size(), size_t(1));                 // asked once, as it is raised
    CHECK_EQ(m.env.asked[0].t, uint64_t(4));
    CHECK(m.env.asked[0].a == WRITE_W0_ADDRESS && m.env.asked[0].store && m.env.asked[0].whole);
    CHECK_EQ(m.env.made.size(), size_t(0));
    m.run_to(8);
    CHECK_EQ(m.env.made.size(), size_t(3));
    for (uint32_t k = 0; k < 3; k++) {
        CHECK_EQ(m.env.asked[k].t, uint64_t(4 + k));
        CHECK_EQ(m.env.made[k].t, uint64_t(5 + k));
        CHECK(m.env.made[k].store && m.env.made[k].a == WRITE_W0_ADDRESS + 4 * k && m.env.made[k].v == 0x22);
        CHECK_EQ(m.retired(IMEM + 4 * k), uint64_t(5 + k));
    }
    CHECK_EQ(m.retired(IMEM + 12), uint64_t(8));
    CHECK_EQ(m.env.asked.size(), size_t(3));
}

// (bus), (data): a window load is made the cycle after it is raised, returning that cycle's view, and answered the
// cycle after that. Three loads retire in 6, 8 and 10, made in 5, 7 and 9.
TEST(window_loads_are_made_the_cycle_after_and_answered_the_next) {
    M m;
    m.core.x[1] = TILE_WINDOW;
    m.prog({LW(2, 1, 0), LW(3, 1, 0xC), LW(4, 1, 0), NOP});
    for (int i = 0; i < 11; i++) { m.env.view = 100 + uint32_t(m.t + 1); m.step(); }   // cycle t's view: 100 + t
    CHECK_EQ(m.env.made.size(), size_t(3));
    const uint32_t address[3] = {PARAMS_ADDRESS, EVENTS_ADDRESS, PARAMS_ADDRESS};
    for (uint32_t k = 0; k < 3; k++) {
        CHECK_EQ(m.env.asked[k].t, uint64_t(4 + 2 * k));
        CHECK(!m.env.asked[k].store && m.env.asked[k].whole);
        CHECK_EQ(m.env.made[k].t, uint64_t(5 + 2 * k));
        CHECK(!m.env.made[k].store && m.env.made[k].a == address[k]);
        CHECK_EQ(m.retired(IMEM + 4 * k), uint64_t(6 + 2 * k));
        CHECK_EQ(m.core.x[2 + k], 105u + 2 * k);
    }
    CHECK_EQ(m.retired(IMEM + 12), uint64_t(11));
}

// (bus): a table access is made in the cycle it is raised in and answered in the next: two stores made in 4 and 5,
// retiring in 5 and 6, and a load made in 6, with that cycle's view, retiring in 7.
TEST(table_accesses_are_made_as_they_are_raised) {
    M m;
    m.core.x[1] = HIST; m.core.x[2] = 0x1ABCD;
    m.prog({SW(2, 1, 0x14), SW(2, 1, 0x404), LW(4, 1, 0x14), NOP});
    for (int i = 0; i < 8; i++) { m.env.view = 100 + uint32_t(m.t + 1); m.step(); }
    CHECK_EQ(m.env.made.size(), size_t(3));
    for (uint32_t k = 0; k < 3; k++) {
        CHECK_EQ(m.env.asked[k].t, uint64_t(4 + k));
        CHECK_EQ(m.env.made[k].t, uint64_t(4 + k));
        CHECK_EQ(m.retired(IMEM + 4 * k), uint64_t(5 + k));
    }
    CHECK(m.env.made[0].store && m.env.made[0].a == HIST + 0x14 && m.env.made[0].v == 0x1ABCD);
    CHECK(m.env.made[1].store && m.env.made[1].a == HIST + 0x404);
    CHECK(!m.env.made[2].store && m.env.made[2].a == HIST + 0x14);
    CHECK_EQ(m.core.x[4], 106u);
    CHECK_EQ(m.retired(IMEM + 12), uint64_t(8));
}

// (bus): a store that may stall whose owner is ready: presented in the cycle after it is raised, 5, taken there, and
// answered in 6, a cycle later than a store that cannot stall.
TEST(a_store_that_may_stall_is_answered_the_cycle_after_its_owner_takes_it) {
    M m;
    m.env.stalls = {WRITE_SEND_32_ADDRESS};
    m.core.x[1] = TILE_WINDOW; m.core.x[2] = 7;
    m.prog({SW(2, 1, 0x94), ADDI(3, 0, 1), ADDI(3, 3, 1)});
    m.run_to(8);
    CHECK_EQ(m.env.made.size(), size_t(1));
    CHECK_EQ(m.env.made[0].t, uint64_t(5));
    CHECK_EQ(m.retired(IMEM), uint64_t(6));
    CHECK_EQ(m.retired(IMEM + 4), uint64_t(7));
    CHECK_EQ(m.retired(IMEM + 8), uint64_t(8));
    CHECK_EQ(m.core.stall_store, uint64_t(0));
}

// (bus): its owner not ready in 5 and 6: presented in each, taken in 7, answered in 8; the instructions behind it
// retire in 9 and 10. Asked what it is once, as it is raised.
TEST(a_stalled_store_is_presented_until_its_owner_is_ready) {
    M m;
    m.env.stalls = {WRITE_SEND_32_ADDRESS};
    m.env.ready = false;
    m.core.x[1] = TILE_WINDOW; m.core.x[2] = 7;
    m.prog({SW(2, 1, 0x94), ADDI(3, 0, 1), ADDI(3, 3, 1)});
    m.run_to(4);
    CHECK_EQ(m.env.refused, 0u);                              // raised in 4: not presented yet
    m.run_to(6);
    CHECK_EQ(m.env.refused, 2u);                              // presented in 5 and 6
    CHECK_EQ(m.env.made.size(), size_t(0));
    m.env.ready = true;
    m.run_to(10);
    CHECK_EQ(m.env.made.size(), size_t(1));
    CHECK_EQ(m.env.made[0].t, uint64_t(7));
    CHECK_EQ(m.env.made[0].v, 7u);
    CHECK_EQ(m.retired(IMEM), uint64_t(8));
    CHECK_EQ(m.retired(IMEM + 4), uint64_t(9));
    CHECK_EQ(m.retired(IMEM + 8), uint64_t(10));
    CHECK_EQ(m.core.x[3], 2u);
    CHECK_EQ(m.env.asked.size(), size_t(1));
    CHECK_EQ(m.core.stall_store, uint64_t(2));
    CHECK_EQ(m.core.mcycle, uint64_t(10));                    // §1: mcycle "counts every cycle"
}

// (bus), (map): an access fault of region 0xFF is answered the cycle after it is raised, whatever it is, and reaches
// no owner: a window load raised in 4 traps in 5 (a window load that does not fault is answered in 6), with
// core_status's pulse in 6.
TEST(a_faulting_window_load_traps_the_cycle_after_it_is_raised_and_is_never_made) {
    M m;
    m.env.faults = {TILE_WINDOW + 0x1C0};                     // block 7: unmapped (5.5)
    m.core.x[1] = TILE_WINDOW;
    m.prog({LW(2, 1, 0x1C0), NOP});
    m.run_to(7);
    CHECK_EQ(m.env.asked.size(), size_t(1));
    CHECK_EQ(m.env.made.size(), size_t(0));
    CHECK_EQ(m.core.trap_at, int64_t(5));
    CHECK_EQ(m.core.mcause, CAUSE_LOAD_FAULT);
    CHECK_EQ(m.core.mtval, TILE_WINDOW + 0x1C0);
    CHECK_EQ(m.core.mepc, IMEM);
    CHECK_EQ(m.retired(IMEM), uint64_t(0));
    CHECK_EQ(m.env.pulses.size(), size_t(1));
    CHECK(m.env.pulses[0].t == 6 && m.env.pulses[0].bit == ACCESS_FAULT_BIT);
}

// The same for a store (a store to an R word, 5.1), and for an access of less than a whole word, which the core
// reports as such when it asks.
TEST(a_faulting_store_and_a_byte_access_fault_likewise) {
    M m;
    m.env.faults = {PARAMS_ADDRESS};
    m.core.x[1] = TILE_WINDOW;
    m.prog({SW(2, 1, 0), NOP});
    m.run_to(7);
    CHECK_EQ(m.env.made.size(), size_t(0));
    CHECK_EQ(m.core.trap_at, int64_t(5));
    CHECK_EQ(m.core.mcause, CAUSE_STORE_FAULT);
    CHECK_EQ(m.core.mtval, PARAMS_ADDRESS);
    CHECK(m.env.pulses.size() == 1 && m.env.pulses[0].t == 6 && m.env.pulses[0].bit == ACCESS_FAULT_BIT);

    M b;
    b.core.x[1] = TILE_WINDOW;
    b.prog({I(0xD, 1, 0, 2, 0x03), NOP});                     // lb x2, 0xD(x1): a byte of EVENTS
    b.run_to(7);
    CHECK_EQ(b.env.asked.size(), size_t(1));
    CHECK(b.env.asked[0].a == EVENTS_ADDRESS && !b.env.asked[0].whole && !b.env.asked[0].store);   // its word's address
    CHECK_EQ(b.env.made.size(), size_t(0));
    CHECK_EQ(b.core.trap_at, int64_t(5));
    CHECK_EQ(b.core.mcause, CAUSE_LOAD_FAULT);
    CHECK_EQ(b.core.mtval, TILE_WINDOW + 0xD);
}

// (map): DMEM is the core's own: a store and a load of it, made as they are raised (stores in 4 and 5, the load in
// 6, answered in 7), reach no environment. IMEM, the boot ROM and DMEM past its 32 KiB are in no data window: each
// load traps the cycle after it is raised, and the environment is never asked.
TEST(dmem_is_the_cores_and_imem_and_the_boot_rom_are_fetched_only) {
    M m;
    m.core.x[1] = 0x100; m.core.x[2] = 0xCAFE0123;
    m.prog({SW(2, 1, 0), SW(2, 1, 8), LW(3, 1, 8), NOP});
    m.run_to(8);
    CHECK_EQ(m.dmem[0x40], 0xCAFE0123u);
    CHECK_EQ(m.dmem[0x42], 0xCAFE0123u);
    CHECK_EQ(m.core.x[3], 0xCAFE0123u);
    CHECK_EQ(m.retired(IMEM), uint64_t(5));
    CHECK_EQ(m.retired(IMEM + 4), uint64_t(6));
    CHECK_EQ(m.retired(IMEM + 8), uint64_t(7));
    CHECK_EQ(m.env.asked.size(), size_t(0));
    CHECK_EQ(m.core.traps, uint64_t(0));

    for (uint32_t base : {IMEM, ROM, uint32_t(0x8000)}) {
        M f;
        f.imem[8] = 0x12345678; f.rom[8] = 0x12345678;
        f.core.x[1] = base;
        f.prog({LW(2, 1, 0x20), NOP});
        f.run_to(7);
        CHECK_EQ(f.core.trap_at, int64_t(5));
        CHECK_EQ(f.core.mcause, CAUSE_LOAD_FAULT);
        CHECK_EQ(f.core.mtval, base + 0x20);
        CHECK_EQ(f.core.x[2], 0u);
        CHECK_EQ(f.env.asked.size(), size_t(0));
        CHECK(f.env.pulses.size() == 1 && f.env.pulses[0].t == 6 && f.env.pulses[0].bit == ACCESS_FAULT_BIT);
    }
}

// (map): an illegal instruction, in execute in 4, traps from writeback in 5: core_status's pulse in 6, once.
TEST(an_illegal_instruction_pulses_core_status_the_cycle_after_its_trap) {
    M m;
    m.prog({0x00000000, NOP});
    m.run_to(9);
    CHECK_EQ(m.core.trap_at, int64_t(5));
    CHECK_EQ(m.core.mcause, CAUSE_ILLEGAL);
    CHECK_EQ(m.env.pulses.size(), size_t(1));
    CHECK(m.env.pulses[0].t == 6 && m.env.pulses[0].bit == ILLEGAL_INSTRUCTION_BIT);
}

// (wake): the core waits after a WFI with mie.MEIE set and mstatus.MIE clear; once it is in the canonical wait it asks
// for no further cycle. MEIP from cycle R: wfi_sleeping (in_wfi) is 0 from R + 1, core_status_canonical_wait is still
// 1 in R + 1 and 0 from R + 2, and the instruction after the WFI retires in R + 3. The cycles the core slept through
// are counted by mcycle.
TEST(the_wake_resumes_in_the_rtls_cycles) {
    M m;
    m.core.x[5] = 0x800;                                      // MEIE
    m.prog({CSR(1, 0x304, 5, 0), WFI, ADDI(3, 0, 1), ADDI(3, 3, 1)});
    for (int i = 0; i < 40 && m.asked_for != Rv32::kNever; i++) m.step();
    CHECK_EQ(m.asked_for, uint64_t(Rv32::kNever));
    CHECK(m.core.in_wfi && m.core.core_status_canonical_wait);
    CHECK_EQ(m.retired(IMEM + 8), uint64_t(0));
    const uint64_t R = m.t + 20;                              // the core is not stepped while it sleeps
    m.t = R - 1;
    m.env.meip_level = true;
    CHECK_EQ(m.step(), R + 1);                                // R
    CHECK(!m.core.in_wfi);                                    // (the outputs after a step are the next cycle's)
    CHECK(m.core.core_status_canonical_wait);
    m.step();                                                 // R + 1
    CHECK(!m.core.core_status_canonical_wait);
    m.run_to(R + 4);
    CHECK_EQ(m.retired(IMEM + 8), R + 3);
    CHECK_EQ(m.retired(IMEM + 12), R + 4);
    CHECK_EQ(m.core.mcycle, R + 4);
    CHECK_EQ(m.core.traps, uint64_t(0));
}
