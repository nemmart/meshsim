// ctl.h (WORKER23, P5): helpers shared by the control suites (tests/ctl/suite_*.cpp), v1.3's suites ported onto W20's
// harness (tests/mach.h) and suite.h. Test-side code (the configuration regime, D6).
//
// What lives here: ref's constants the suites name (Registers, Controller), the units by ref's member names (a tile's
// core, a controller's peripheral 1, C&C 1's peripheral), and the stand-ins for harness operations W20 hasn't added yet
// (each marked STAND-IN, with the W20 request it waits for; none edits W20's files).
#pragma once

#include <cstdint>
#include <string>

#include "../mach.h"
#include "units/chip.h"
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

// ---- STAND-INS for harness operations W20 hasn't added yet (requests through the integrator; NOTES.md) ----
// W20 request 9: ref's `tile.ctl.conditions |= bits` between cycles (CcCoresTest's errorPath, CondTest's injected
// errors). W20's form: a HostWrites field applied right after the block's endCycle of the cycle just run (as ref's poke
// follows that cycle's end). This stand-in ORs the bits into the block's state directly, so its endCycle of that cycle
// already sees them: possibly a cycle early. Replaced when W20's operation lands.
inline void inject_errors(mach::Chip& c, unsigned k, uint32_t bits) {
    const std::string n = unit_name(k, "ctl");
    c.unit<units::TileCtl>(n).s.conditions |= bits;
    c.touched(n);
}
// W20 request 10: ref's `chip.cfg.checkErrors = false` between cycles (invariant 15 off from then on). This stand-in
// does nothing: the release builds (no checker) are unaffected; the checker's builds count the injected errors.
inline void set_check_errors(mach::Chip&, bool) {}

}  // namespace ctl
