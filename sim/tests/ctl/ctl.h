// ctl.h: helpers shared by the control suites (tests/ctl/suite_*.cpp), v1.3's suites ported onto the
// harness (tests/mach.h) and suite.h. Test-side code (the configuration regime, D6).
//
// What lives here: ref's constants the suites name (Registers, Controller), the units by ref's member names (a tile's
// core, a controller's peripheral 1, C&C 1's peripheral), and the stand-ins for harness operations the harness hasn't added yet
// (each marked STAND-IN, with the request it waits for; none edits the files).
#pragma once

#include <charconv>
#include <cstdint>
#include <string>

#include "../mach.h"
#include "units/chip.h"
#include "units/cnc.h"
#include "units/cnc1_periph.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/regmap.h"
#include "units/reqbus.h"
#include "units/router.h"
#include "units/rv32.h"
#include "units/tile_ctl.h"

namespace ctl {

// ---- ref's Registers and Controller constants ----
inline constexpr uint32_t PMEM_BASE = 0x01000000, BOOTROM_BASE = units::BOOTROM_BASE;
inline constexpr uint32_t CMD_JUMP = 0x4A, CMD_STAGE = 0x53;
inline constexpr unsigned BROADCAST = units::lw::BROADCAST;
inline constexpr int RUN = units::Rv32::RUN, HALT_EXIT = units::Rv32::HALT_EXIT;
inline constexpr int64_t CAUSE_EXTERNAL = int32_t(units::Rv32::CAUSE_EXTERNAL), CAUSE_LOAD_FAULT = units::Rv32::CAUSE_LOAD_FAULT,
                         CAUSE_STORE_FAULT = units::Rv32::CAUSE_STORE_FAULT, CAUSE_ILLEGAL = units::Rv32::CAUSE_ILLEGAL;

// ---- the units by ref's names ----
// a unit's name prefix: tiles 0-63 "t<k>.", the controllers 64-66 "cc<c>."
inline std::string unit_name(unsigned k, const char* n) { return units::unit_prefix(k) + n; }
// ref's tile.core (Controller): the core's architectural state and statistics
inline units::Rv32& core(mach::Chip& c, unsigned k) { return c.unit<units::TileCore>(unit_name(k, "core")).rv; }
// ref's tile.slot
inline units::SlotCore& slot(mach::Chip& c, unsigned k) { return c.unit<units::PeriphSlot>(unit_name(k, "slot")).s; }
// ref's slot.p[n].type() (-1: absent, as ref's `p[n] != null ? ... : -1`)
inline int periph_type(mach::Chip& c, unsigned k, unsigned n) {
    units::SlotCore& s = slot(c, k);
    const units::PeriphType t = s.per[n].type;
    if (t == units::PT_ABSENT) return -1;
    if (t == units::PT_CC_STUB) return s.cc_stub(n).type();
    return int(t);
}
// ref's chip.cc1p, with C&C 1 a core (cc_cores): C&C 1's peripheral 1
inline units::Cnc1Peripheral& cc1p(mach::Chip& c) { return slot(c, 64).cnc1(1); }
// ref's reqBus.received[target]: requests target `target` (0-2) popped
inline uint64_t req_received(mach::Chip& c, unsigned target) {
    return c.unit<units::reqbus::ReqRecv>("cc" + std::to_string(target + 1) + ".reqrecv").received;
}
// ref's tile.ctl.simExitValue (an int)
inline int64_t sim_exit_value(mach::Chip& c, unsigned k) { return c.tile(k).exit_value(); }

// ref's chip.cc1p.gos on the Java model's machine: C&C 1's model drives PULSE_GO, counted in the same cycle as CnC.gos
inline uint64_t model_gos(mach::Chip& c) { return c.unit<units::Cnc>("cnc").gos; }
// ref's chip.cc.readyBits(t) on the Java model's machine (C&C 1's model keeps the READY arrivals, cnc.h)
inline uint64_t model_ready_bits(mach::Chip& c, int64_t t) {
    const units::Cnc& n = c.unit<units::Cnc>("cnc");
    uint64_t b = 0; for (unsigned k = 0; k < 64; k++) if (n.ready(k, t)) b |= 1ull << k;
    return b;
}

// Java's String.format("%.<n>f", x): the double's shortest round-trip decimal (Java's FormattedFloatingDecimal), then
// rounded HALF_UP to n places (printf rounds the exact binary value instead: 129.25 -> "129.2", Java "129.3").
inline std::string java_fixed(double x, unsigned n) {
    char buf[400];
    auto res = std::to_chars(buf, buf + sizeof buf, x < 0 ? -x : x, std::chars_format::fixed);
    std::string s(buf, res.ptr);
    const size_t dot = s.find('.');
    std::string ip = dot == std::string::npos ? s : s.substr(0, dot), fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    while (fp.size() < n + 1) fp += '0';
    std::string digits = ip + fp.substr(0, n);
    if (fp[n] >= '5') {
        int k = int(digits.size()) - 1;
        while (k >= 0) { if (digits[size_t(k)] == '9') { digits[size_t(k)] = '0'; k--; } else { digits[size_t(k)]++; break; } }
        if (k < 0) digits.insert(digits.begin(), '1');
    }
    std::string r = n ? digits.substr(0, digits.size() - n) + "." + digits.substr(digits.size() - n) : digits;
    return (x < 0 ? "-" : "") + r;
}

}  // namespace ctl
