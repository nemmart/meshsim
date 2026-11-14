// core_programs.h: the stand the RTL's own core programs run on (tests/core-programs/README.md): the core
// (src/units/rv32.h, the RTL's) on a bus that is Snarky One's test memory and devices
// (rtl/blocks/core/test/src/rv32_core_test_memory.rs, rv32_core_test_devices.rs, rv32_core_test_pkg.rs) with its ideal
// timing: every port ready in every cycle and answering the next. A program is its ELF file's loadable segments,
// placed as program_test.rs boots them: DMEM filled with the dirty pattern, then the segments at their load addresses.
//
// The test memory map (rv32_core_test_pkg.rs):
//   0x0000_0000  IMEM, 32 KiB (fetches only: a data access faults)
//   0x1000_0000  DMEM, 32 KiB
//   0x2000_0000  result: a store reports a pass          0x2000_0004  result: a store reports a failure, its code
//   0x2000_0010  side effects: a load returns the number of earlier loads
//   0x2000_0014  side effects: a store is counted and kept; it is also the event trigger (below)
//   0x2000_0018  console: a store's low byte
//   0x2000_001C  barrier: a store lowers the line (the core's MEIP) and with bit 0 arms; release() raises it;
//                a load reads the line in bit 0 and the arming in bit 1
// Any other access to the system window is answered with an error; an address in no window faults (the core's own).
//
// The event trigger, for the programs that take interrupts (tests/core-programs/src/events.h; the RTL's test was given
// the same): a store of KIND << 8 | DELAY to the side-effect write device raises an input DELAY cycles after the cycle
// the test sees the store counted in. KIND 0: the barrier's release in that cycle (the line, MEIP, high from the next);
// KIND 1: the core's nmi input high from that cycle on.
//
// Cycles are the RTL test's: cycle 0 is the first after reset, and a run's count is the cycle in which the test sees
// the result, the cycle after the result store's answer (program_test.rs's loop index at its return). The devices act
// in the cycle of their answer, at the edge that ends it; the core makes an access the cycle before its answer
// (rv32.h), so what a store changes is seen two cycles after it is made.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "units/rv32.h"

namespace core_programs {

constexpr uint32_t IMEM_BASE = 0x00000000, DMEM_BASE = 0x10000000, SYSTEM_BASE = 0x20000000, SYSTEM_BYTES = 0x10000000;
constexpr uint32_t BYTES = 0x8000;                           // IMEM's and DMEM's size (the default configuration)
constexpr uint32_t RESULT_PASS = 0x20000000, RESULT_FAIL = 0x20000004, SIDE_EFFECT_READ = 0x20000010,
                   SIDE_EFFECT_WRITE = 0x20000014, CONSOLE = 0x20000018, BARRIER = 0x2000001C;
constexpr uint32_t DIRTY_DMEM_PATTERN = 0xbad00000;

struct Bus {
    std::vector<uint32_t> imem = std::vector<uint32_t>(BYTES / 4, 0), dmem = std::vector<uint32_t>(BYTES / 4, 0);
    uint64_t now = 0;                                        // the cycle being stepped (the RTL test's: 0 the first)
    // The devices' state, and since when it holds: a change made by an access of cycle t holds from t + 2.
    bool passed = false, failed = false;
    uint64_t result_at = 0;
    uint32_t failure_code = 0, side_effect_reads = 0, side_effect_writes = 0, side_effect_last_write = 0;
    std::string console;
    bool armed = false;                                      // the barrier's arming
    std::vector<std::pair<uint64_t, bool>> line_changes;     // the barrier line's changes: {the cycle it holds from, level}
    uint64_t nmi_from = ~uint64_t(0);                        // the nmi input is high from this cycle on

    // ---- rv32.h's bus
    uint32_t bus_fetch(uint32_t a, bool& err) const { err = a - IMEM_BASE >= BYTES; return imem[(a >> 2) % (BYTES / 4)]; }
    uint32_t bus_route(uint32_t a) const { return a - DMEM_BASE < BYTES || a - SYSTEM_BASE < SYSTEM_BYTES ? 1 : 0; }
    bool bus_access(uint64_t, uint32_t a, bool store, uint32_t mask, uint32_t wdata, uint32_t& rdata) {
        rdata = 0;
        if (a - DMEM_BASE < BYTES) {
            uint32_t& w = dmem[(a - DMEM_BASE) >> 2];
            if (store) w = (w & ~mask) | (wdata & mask); else rdata = w;
            return true;
        }
        if (!store) {
            if (a == SIDE_EFFECT_READ) { rdata = side_effect_reads++; return true; }
            if (a == BARRIER) { rdata = uint32_t(armed) << 1 | uint32_t(line_at(now + 1)); return true; }
            return false;
        }
        switch (a) {
        case RESULT_PASS: if (!passed && !failed) result_at = now + 2; passed = true; return true;
        case RESULT_FAIL:
            if (!failed) failure_code = wdata;
            if (!passed && !failed) result_at = now + 2;
            failed = true;
            return true;
        case SIDE_EFFECT_WRITE: {
            side_effect_writes++; side_effect_last_write = wdata;
            const uint64_t at = now + 2 + (wdata & 0xFF);    // the cycle the test sees the store counted in, + DELAY
            if (wdata >> 8 == 0) release(at + 1);
            if (wdata >> 8 == 1) nmi_from = at;
            return true;
        }
        case CONSOLE: console.push_back(char(wdata)); return true;
        case BARRIER:
            line_changes.push_back({now + 2, false});
            if (wdata & 1) armed = true;
            return true;
        default: return false;
        }
    }
    bool meip() const { return line_at(now); }
    bool nmi() const { return now >= nmi_from; }

    // The release: the line is high from cycle `from` on (the RTL test's barrier_release in cycle from - 1).
    void release(uint64_t from) { line_changes.push_back({from, true}); armed = false; }
    // The line in cycle c: its last change by then; a release wins over a store's lowering at the same edge.
    bool line_at(uint64_t c) const {
        uint64_t last = 0;
        bool level = false;
        for (const auto& [from, v] : line_changes)
            if (from <= c && (from > last || (from == last && v))) { last = from; level = v; }
        return level;
    }
};

// An ELF file's loadable segments, byte by byte: `place(address, byte)` for each byte of each segment at its load
// address, its file bytes and then zeros to its memory size (.bss); false from `place` refuses the file. Returns an
// error, or "".
template <class Place>
std::string read_elf(const std::string& path, Place place) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return path + ": cannot open";
    std::vector<uint8_t> d;
    uint8_t buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    const auto u16 = [&](size_t o) { return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8; };
    const auto u32 = [&](size_t o) { return u16(o) | u16(o + 2) << 16; };
    if (d.size() < 52 || std::memcmp(d.data(), "\177ELF\1\1", 6) != 0) return path + ": not a 32-bit little-endian ELF file";
    const uint32_t phoff = u32(28), phentsize = u16(42), phnum = u16(44);
    for (uint32_t i = 0; i < phnum; i++) {
        const size_t p = phoff + size_t(i) * phentsize;
        if (p + 32 > d.size()) return path + ": a bad program header";
        const uint32_t type = u32(p), off = u32(p + 4), paddr = u32(p + 12), filesz = u32(p + 16), memsz = u32(p + 20);
        if (type != 1 || memsz == 0) continue;               // PT_LOAD
        if (off + filesz > d.size() || filesz > memsz) return path + ": a bad segment";
        for (uint32_t k = 0; k < memsz; k++)
            if (!place(paddr + k, k < filesz ? d[off + k] : uint8_t(0))) return path + ": a byte outside the memories";
    }
    return "";
}
// A byte into a memory of words.
inline void place_byte(std::vector<uint32_t>& words, uint32_t offset, uint8_t byte) {
    const uint32_t sh = (offset & 3) * 8;
    uint32_t& w = words[offset >> 2];
    w = (w & ~(0xFFu << sh)) | uint32_t(byte) << sh;
}
// An ELF file's loadable segments into the bus's memories (program_test.rs's memory_images): every byte is in IMEM
// or DMEM.
inline std::string load_elf(const std::string& path, Bus& bus) {
    return read_elf(path, [&](uint32_t a, uint8_t byte) {
        if (a - IMEM_BASE < BYTES) place_byte(bus.imem, a - IMEM_BASE, byte);
        else if (a - DMEM_BASE < BYTES) place_byte(bus.dmem, a - DMEM_BASE, byte);
        else return false;
        return true;
    });
}

// One run: the core out of reset at IMEM's base on the bus, until the program reports a result or `limit` cycles pass.
struct Run {
    Bus bus;
    units::Rv32 core;
    uint64_t now = 0;                                        // the next cycle to step
    uint64_t cycles = 0, retired = 0;                        // the result's, as the RTL test counts them (the header)
    units::TraceBuffer* trace = nullptr;                     // the retire trace, if kept

    std::string boot(const std::string& elf) {
        for (uint32_t i = 0; i < BYTES / 4; i++) bus.dmem[i] = DIRTY_DMEM_PATTERN ^ i;
        core.pc = IMEM_BASE;
        core.dmem = bus.dmem.data(); core.imem = bus.imem.data();   // (the digest's; the core reads the bus)
        if (trace) core.log = &trace->log;
        return load_elf(elf, bus);
    }
    // Cycle c of the RTL test is step c + 1 here (rv32.h: cycle 1 is the first).
    void step() { bus.now = now; core.step(bus, now + 1); now++; }
    // True: a result within the limit. The cycle the test sees the result in is run too: its retirement counts.
    bool run(uint64_t limit) {
        while (now < limit) {
            step();
            if ((bus.passed || bus.failed) && now > bus.result_at) { cycles = bus.result_at; retired = core.instructions; return true; }
        }
        return false;
    }
};

}  // namespace core_programs
