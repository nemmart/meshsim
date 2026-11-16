// The RTL's own core programs on the core model (tests/core-programs/README.md): each program's pass, the cycle of its
// result and its retired instructions equal to the RTL's, every retirement and trap in the RTL's cycle, and CoreMark's
// report equal to the RTL's baseline. The stand is core_programs.h.
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core_programs.h"
#include "harness.h"
#include "sha256.h"

namespace {

const std::string kDir = "tests/core-programs/";

struct Program { std::string elf; uint64_t cycles, retired; };

// A list's lines without its comments, as fields.
std::vector<std::vector<std::string>> read_list(const std::string& name) {
    std::vector<std::vector<std::string>> rows;
    std::ifstream in(kDir + name);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        std::vector<std::string> row;
        for (std::string field; s >> field;) row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}
std::vector<Program> programs() {
    std::vector<Program> p;
    for (const auto& row : read_list("PROGRAMS"))
        if (row.size() == 3) p.push_back({row[0], std::stoull(row[1]), std::stoull(row[2])});
    return p;
}
std::string stem(const std::string& elf) { return elf.substr(0, elf.size() - 4); }

// The retire trace as README.md gives it: the RTL test's cycles (0 the first), a line per retirement and per trap.
std::vector<std::string> retire_trace(const units::TraceLog& log) {
    std::vector<std::string> lines;
    char b[96];
    for (uint32_t i = 0; i < log.n; i++) {
        const units::TraceRec& r = log.rec[i];
        const unsigned long long c = (unsigned long long)r.cycle - 1;
        if (r.kind == units::T_RETIRE) snprintf(b, sizeof b, "ret: %llu %x %x", c, r.w[0], r.w[1]);
        else if (r.kind == units::T_TRAP) snprintf(b, sizeof b, "trap: %llu %x %x %x", c, r.w[0], r.w[1], r.w[2]);
        else continue;
        lines.push_back(b);
    }
    return lines;
}
std::string hash_of(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& l : lines) { text += l; text += '\n'; }
    return test::sha256_hex(text.data(), text.size());
}

}  // namespace

TEST(core_programs_fixtures_are_the_listed_files) {
    const auto sums = read_list("SHA256SUMS");
    CHECK_EQ(sums.size(), 54u);                              // the 54 programs (the boot ROM: fw/images/MANIFEST)
    for (const auto& row : sums) {
        CHECK_EQ(row.size(), 2u);
        std::ifstream in(kDir + "elf/" + row[1], std::ios::binary);
        std::ostringstream bytes;
        bytes << in.rdbuf();
        if (test::sha256_hex(bytes.str().data(), bytes.str().size()) != row[0]) { test::fail(__FILE__, __LINE__, "%s is not the listed file", row[1].c_str()); return; }
    }
}

// Every program: its verdict, its cycles and retired instructions, and its whole retire trace against the RTL's.
TEST(core_programs_pass_in_the_rtls_cycles) {
    const std::vector<Program> list = programs();
    CHECK_EQ(list.size(), 53u);                              // the 48, the benchmark run, the interrupt programs
    std::map<std::string, std::string> hashes;
    for (const auto& row : read_list("RETIRE.sha256")) if (row.size() == 2) hashes[row[1]] = row[0];
    CHECK_EQ(hashes.size(), 54u);
    unsigned bad = 0;
    for (const Program& p : list) {
        core_programs::Run r;
        units::TraceBuffer tb(uint32_t(p.retired + 4096));
        r.trace = &tb;
        const std::string error = r.boot(kDir + "elf/" + p.elf);
        if (!error.empty()) { test::fail(__FILE__, __LINE__, "%s", error.c_str()); return; }
        const bool result = r.run(p.cycles + 1000);
        std::string why;
        if (!result) why = "no result within the RTL's cycles + 1000";
        else if (r.bus.failed) why = "failed with code " + std::to_string(r.bus.failure_code);
        else if (r.cycles != p.cycles) why = "its result in cycle " + std::to_string(r.cycles) + ", the RTL's " + std::to_string(p.cycles);
        else if (r.retired != p.retired) why = std::to_string(r.retired) + " instructions retired, the RTL's " + std::to_string(p.retired);
        else if (tb.log.full) why = "the trace overflowed";
        else if (hash_of(retire_trace(tb.log)) != hashes[stem(p.elf)]) why = "its retire trace is not the RTL's (RETIRE.sha256)";
        if (!why.empty()) { test::fail(__FILE__, __LINE__, "%s: %s", p.elf.c_str(), why.c_str()); bad++; }
    }
    CHECK_EQ(bad, 0u);
}

// Eight programs traces line by line, so that a difference names the first cycle it shows in.
TEST(core_programs_retire_in_the_rtls_cycles_line_by_line) {
    for (const char* name : {"rv32ui-jalr", "rv32ui-ld_st", "rv32um-mul", "firmware-isa_edges", "irq_wfi", "nmi_wfi", "nmi_run", "trap_nmi"}) {
        std::vector<std::string> want;
        std::ifstream in(kDir + "retire/" + name + ".rtl");
        for (std::string line; std::getline(in, line);) want.push_back(line);
        CHECK(!want.empty());
        core_programs::Run r;
        units::TraceBuffer tb(8192);
        r.trace = &tb;
        CHECK(r.boot(kDir + "elf/" + name + ".elf").empty());
        CHECK(r.run(100000));
        const std::vector<std::string> got = retire_trace(tb.log);
        for (size_t i = 0; i < want.size() || i < got.size(); i++) {
            const std::string w = i < want.size() ? want[i] : "(nothing)", g = i < got.size() ? got[i] : "(nothing)";
            if (w != g) { test::fail(__FILE__, __LINE__, "%s, line %zu: the RTL's '%s', ours '%s'", name, i + 1, w.c_str(), g.c_str()); return; }
        }
    }
}

// CoreMark's own report on the benchmark run: its ticks (mcycle), instructions (minstret) and iterations are the
// RTL's baseline's.
TEST(core_programs_coremark_reports_the_rtls_baseline) {
    std::map<std::string, std::string> bench;
    for (const auto& row : read_list("BENCH")) if (row.size() == 2) bench[row[0]] = row[1];
    CHECK_EQ(bench.size(), 3u);
    core_programs::Run r;
    CHECK(r.boot(kDir + "elf/coremark-bench.elf").empty());
    CHECK(r.run(4000000));
    CHECK(r.bus.passed && !r.bus.failed);
    std::map<std::string, std::string> report;
    std::istringstream console(r.bus.console);
    for (std::string line; std::getline(console, line);) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon), value = line.substr(colon + 1);
        key.erase(key.find_last_not_of(' ') + 1);
        value.erase(0, value.find_first_not_of(' '));
        report[key] = value;
    }
    if (report["Total ticks"] != bench["cycles"] || report["Instructions"] != bench["instructions"] || report["Iterations"] != bench["iterations"])
        test::fail(__FILE__, __LINE__, "CoreMark reports %s ticks, %s instructions, %s iterations; the RTL's baseline %s, %s, %s",
                   report["Total ticks"].c_str(), report["Instructions"].c_str(), report["Iterations"].c_str(),
                   bench["cycles"].c_str(), bench["instructions"].c_str(), bench["iterations"].c_str());
}

// ------------------------------------------------------------------------------------------- the tile's bus
// The RTL's boot ROM (fw/boot/boot_rom.S, built by fw/build.sh: fw/images/boot/boot_rom.elf) on the core in the tile's map,
// with the register bus's timing as the RTL's register_bus has it (rv32.h's header): a window load answered two
// cycles after it is raised, a window store the cycle after, in the cycle it is made, REQUEST_SEND's store, which
// may stall, the cycle after it is made. The RTL's boot_rom_program_test measures the ROM's cycles on its block
// `core` (rtl/blocks/core/README.md, "The boot ROM's program"; tests/core-programs/BOOT), cycle 0 the first after
// reset: each figure ends at the cycle in which the named store is raised on the system port, and starts at the
// reset, or at the cycle of the test's stimulus (the COUNT's decrement of counter 7, the release), whose event the
// tile sets at that cycle's end, so that MEIP, the wake of the events under EVENTS_MASK, is high from the next.
namespace {

struct TileBus {
    static constexpr uint32_t IMEM = 0x01000000, ROM = 0x02000000, ROM_BYTES = 256, WINDOW = 0xFFFFF800,
                              PARAMS = 0xFFFFF800, EVENTS = 0xFFFFF80C, ARRIVE = 0xFFFFF818, REQUEST_OUT_W1 = 0xFFFFF904,
                              REQUEST_SEND = 0xFFFFF90C;
    static constexpr uint32_t RELEASE = 1u << 0, ALL_COUNTERS_DONE = 1u << 2, MASK = RELEASE | ALL_COUNTERS_DONE;
    std::vector<uint32_t> rom = std::vector<uint32_t>(ROM_BYTES / 4, 0), imem = std::vector<uint32_t>(16, 0);
    uint32_t params = 0;
    uint64_t now = 0;
    uint32_t events = 0;                                     // EVENTS as cycle `now` shows it (the end of now - 1)
    uint32_t set_bits = 0, clear_bits = 0;                   // what the end of `now` sets, and a load consumes
    struct Raised { uint64_t cycle; uint32_t address; };
    std::vector<Raised> raised;                              // every data access, in the cycle it is raised
    void next_cycle() { events = (events & ~clear_bits) | set_bits; set_bits = clear_bits = 0; now++; }
    uint32_t bus_fetch(uint32_t a, bool& err) const {
        err = false;
        if (a - ROM < ROM_BYTES) return rom[(a - ROM) >> 2];
        if (a - IMEM < 0x8000) return a - IMEM < 4 * imem.size() ? imem[(a - IMEM) >> 2] : 0;
        err = true;
        return 0;
    }
    uint32_t bus_route(uint32_t a) { raised.push_back({now, a}); return a >= WINDOW ? 2 : a < 0x8000 ? 1 : 0; }
    bool bus_store_at_once(uint32_t a) const { return a != REQUEST_SEND; }
    bool bus_access(uint64_t, uint32_t a, bool store, uint32_t, uint32_t, uint32_t& rdata) {
        rdata = 0;
        if (!store && a == PARAMS) rdata = params;
        if (!store && a == EVENTS) { rdata = events; clear_bits |= events & MASK; }   // consumed at this cycle's end
        if (store && a == ARRIVE) clear_bits |= ALL_COUNTERS_DONE;
        return true;
    }
    bool meip() const { return (events & MASK) != 0; }
    bool nmi() const { return false; }
};

// The RTL's boot ROM on the core, from reset, with an image whose first instruction is a store to REQUEST_OUT_W1.
struct BootRun {
    TileBus bus;
    units::Rv32 core;
    std::vector<uint32_t> dmem = std::vector<uint32_t>(units::MEM_WORDS, 0), imem = std::vector<uint32_t>(units::MEM_WORDS, 0);
    bool asleep = false;                                     // the core asked for no further cycle: the canonical wait

    bool boot(uint32_t params) {
        bus.params = params;
        const std::string error = core_programs::read_elf("fw/images/boot/boot_rom.elf", [&](uint32_t a, uint8_t byte) {
            if (a - TileBus::ROM < TileBus::ROM_BYTES) core_programs::place_byte(bus.rom, a - TileBus::ROM, byte);
            return true;                                     // (the segment starts with the file's own header)
        });
        bus.imem[0] = 0x90002223;                            // sw zero, REQUEST_OUT_W1(zero)
        bus.imem[1] = 0x0000006F;                            // j .
        core.pc = TileBus::ROM;
        core.dmem = dmem.data(); core.imem = imem.data();
        return error.empty();
    }
    void step() { asleep = core.step(bus, bus.now + 1) == units::Rv32::kNever; bus.next_cycle(); }
    // Runs until a store to `address` is raised; its cycle, or -1 after 200 cycles.
    int64_t until_raised(uint32_t address) {
        const size_t seen = bus.raised.size();
        for (int i = 0; i < 200; i++) {
            step();
            for (size_t k = seen; k < bus.raised.size(); k++) if (bus.raised[k].address == address) return int64_t(bus.raised[k].cycle);
        }
        return -1;
    }
    // Runs into the canonical wait and three cycles on.
    bool until_wait() {
        for (int i = 0; i < 200 && !(asleep && core.core_status_canonical_wait); i++) step();
        const bool waits = asleep && core.core_status_canonical_wait;
        for (int i = 0; i < 3; i++) step();
        return waits && asleep;
    }
    // The test's stimulus in this cycle: the event is set at its end. Returns the cycle.
    int64_t stimulus(uint32_t event) { const int64_t at = int64_t(bus.now); bus.set_bits |= event; step(); return at; }
};

std::map<std::string, long long> boot_figures() {
    std::map<std::string, long long> boot;
    for (const auto& row : read_list("BOOT")) if (row.size() == 2) boot[row[0]] = std::stoll(row[1]);
    return boot;
}

}  // namespace

TEST(core_programs_boot_rom_takes_the_rtls_cycles_from_reset) {
    auto boot = boot_figures();
    CHECK_EQ(boot.size(), 5u);
    BootRun loaded;                                          // a loaded tile (PARAMS.boot_local 0): its LOAD request
    CHECK(loaded.boot(0));
    CHECK_EQ(loaded.until_raised(TileBus::REQUEST_SEND), boot["reset_to_load_request_send"]);
    BootRun local;                                           // PARAMS.boot_local (bit 18): the image's first instruction
    CHECK(local.boot(1u << 18));
    CHECK_EQ(local.until_raised(TileBus::REQUEST_OUT_W1), boot["reset_to_image_boot_local"]);
}

TEST(core_programs_boot_rom_takes_the_rtls_cycles_from_its_waits) {
    auto boot = boot_figures();
    BootRun plain;                                           // no barrier: the load's COUNT, then the image
    CHECK(plain.boot(0));
    CHECK(plain.until_raised(TileBus::REQUEST_SEND) >= 0);
    CHECK(plain.until_wait());                               // asleep in its wait for the load
    int64_t counted = plain.stimulus(TileBus::ALL_COUNTERS_DONE);
    CHECK_EQ(plain.until_raised(TileBus::REQUEST_OUT_W1) - counted, boot["count_to_image_no_barrier"]);

    BootRun barrier;                                         // PARAMS.barrier (bit 17): the COUNT, ARRIVE, the release
    CHECK(barrier.boot(1u << 17));
    CHECK(barrier.until_raised(TileBus::REQUEST_SEND) >= 0);
    CHECK(barrier.until_wait());
    counted = barrier.stimulus(TileBus::ALL_COUNTERS_DONE);
    CHECK_EQ(barrier.until_raised(TileBus::ARRIVE) - counted, boot["count_to_arrive"]);
    CHECK(barrier.until_wait());                             // the canonical wait for the release
    const int64_t released = barrier.stimulus(TileBus::RELEASE);
    CHECK_EQ(barrier.until_raised(TileBus::REQUEST_OUT_W1) - released, boot["release_to_image"]);
}

// The waits (WAITS): `wfi_sleeping` and `core_status_canonical_wait` change in the RTL's cycles. The core specification's
// §1, Core ports: the canonical wait's level is "1 in the cycle after each cycle in which `wfi_sleeping` is 1", the
// instruction port idle, "through the first cycle in which `wfi_sleeping` is 0 again"; the tile's BARRIER_ARRIVE rests on
// its rise (7.3). The model's outputs after a step are the next cycle's (rv32.h).
TEST(core_programs_wait_in_the_rtls_cycles) {
    std::map<std::string, std::vector<std::string>> want;
    size_t lines = 0;
    for (const auto& row : read_list("WAITS"))
        if (row.size() == 4) { want[row[0]].push_back(row[1] + " " + row[2] + " " + row[3]); lines++; }
    CHECK_EQ(lines, 122u);
    CHECK_EQ(want.size(), 3u);                               // irq_wfi, nmi_wfi and trap_nmi wait; nmi_run never does
    for (const Program& p : programs()) {
        const std::string name = stem(p.elf);
        if (name != "irq_wfi" && name != "nmi_wfi" && name != "nmi_run" && name != "trap_nmi") continue;
        core_programs::Run r;
        CHECK(r.boot(kDir + "elf/" + p.elf).empty());
        std::vector<std::string> got;
        bool wfi = false, canon = false;
        while (r.now < p.cycles) {                           // after this step, the levels of cycle r.now
            r.step();
            if (r.core.in_wfi == wfi && r.core.core_status_canonical_wait == canon) continue;
            wfi = r.core.in_wfi; canon = r.core.core_status_canonical_wait;
            got.push_back(std::to_string(r.now) + " " + (wfi ? "1" : "0") + " " + (canon ? "1" : "0"));
        }
        const std::vector<std::string>& w = want[name];
        for (size_t i = 0; i < w.size() || i < got.size(); i++) {
            const std::string a = i < w.size() ? w[i] : "(nothing)", b = i < got.size() ? got[i] : "(nothing)";
            if (a != b) { test::fail(__FILE__, __LINE__, "%s, change %zu: the RTL's '%s', ours '%s'", name.c_str(), i + 1, a.c_str(), b.c_str()); return; }
        }
    }
}
