// suite_cccores.cpp (WORKER23, P5, inventory C18): v1.3's CcCoresTest (sim/ref/meshsim/test/CcCoresTest.java) on W20's
// harness, group for group, check for check, in Java's order and words. run_tests.sh's steps `cccores` (-1) and
// `cccores-shuf` (4); its output equals ref's log (tests/ctlsuites/cccores*.log). Usage: suite_cccores [shuffleSeed]
//
// WORKER14: C&C 1-3 as cores (Config.ccCores):
//  - the switch off: the Java model, no controller tiles;
//  - the boot ROM's branch for 64-66 (6.2 step 4): each controller waits in its ROM; the harness lands a program and its
//    first command through the host port (W14-02); the ROM jumps; the program reads PARAMS' id and exits with it;
//  - the link (W14-01): C&C 1's send port writes into a tile's data memory through tile 0's fifth port;
//  - the request tree: C&C 2's request reaches target 0 stamped with requestor 65; C&C 1's request port reads it;
//  - the controllers' boot (6.2) with the firmware (sw/cc/): C&C 1 boots C&C 2 and C&C 3, takes their "started"
//    requests, boots the tiles with READY/GO, then runs phases on the harness's commands; against the Java model.
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "ctl.h"
#include "../suite.h"

using namespace ctl;
using mach::Chip;
using mach::Mach;
using mach::MachConfig;

namespace {

int64_t shuffle = -1;

MachConfig cfg(bool cores) { MachConfig k = MachConfig::machine(); k.shuffle_seed = shuffle; k.cc_cores = cores; return k; }

// ---------------------------------------------------------------- a tiny assembler (x0-relative register accesses)
int R(int off) { return off - 0x800; }                  // X0REL
uint32_t lw(int rd, int rs1, int imm) { return uint32_t((imm & 0xFFF) << 20 | rs1 << 15 | 2 << 12 | rd << 7 | 0x03); }
uint32_t sw(int rs2, int rs1, int imm) { return uint32_t(((imm >> 5) & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | 2 << 12 | (imm & 0x1F) << 7 | 0x23); }
uint32_t addi(int rd, int rs1, int imm) { return uint32_t((imm & 0xFFF) << 20 | rs1 << 15 | rd << 7 | 0x13); }
uint32_t andi(int rd, int rs1, int imm) { return uint32_t((imm & 0xFFF) << 20 | rs1 << 15 | 7 << 12 | rd << 7 | 0x13); }
uint32_t srli(int rd, int rs1, int sh) { return uint32_t(sh << 20 | rs1 << 15 | 5 << 12 | rd << 7 | 0x13); }
uint32_t lui(int rd, int imm20) { return uint32_t(imm20) << 12 | uint32_t(rd << 7 | 0x37); }
uint32_t beq(int rs1, int rs2, int off) {
    return uint32_t(((off >> 12) & 1) << 31) | uint32_t(((off >> 5) & 0x3F) << 25 | rs2 << 20 | rs1 << 15 | ((off >> 1) & 0xF) << 8 |
                                                         ((off >> 11) & 1) << 7 | 0x63);
}
constexpr uint32_t J_SELF = 0x0000006F;
constexpr int A0 = 10, T0 = 5, T1 = 6, T2 = 7;
constexpr int PARAMS = 0x000, EVENTS_PEEK = 0x01C, ROUTER_HDR = 0x028, ROUTER_ADDR = 0x02C, ROUTER_SEND_32 = 0x03C,
              REQ_W0 = 0x048, REQ_W1 = 0x04C, REQ_W2 = 0x050, REQ_SEND = 0x054, REQ_GET_W0 = 0x048, REQ_RECEIVE = 0x054,
              SIM_EXIT = 0x1F8;

// The program for controller cc: a0 = PARAMS' id; C&C 1 sends 0x5A5 to tile 5's data memory at 0x100 and then waits
// for a request and exits with its W3 + 0x1000 * its requestor; C&C 2 sends C&C 1 a "started" request carrying its id;
// every other exits with its id.
std::vector<uint32_t> program(int cc) {
    std::vector<uint32_t> p;
    p.push_back(lw(A0, 0, R(PARAMS))); p.push_back(srli(A0, A0, 20)); p.push_back(andi(A0, A0, 0x7F));
    if (cc == 0) {
        p.push_back(lui(T0, 5 << 13));                         // ROUTER_HDR: dst 5 [31:25], mode 0, uncounted
        p.push_back(sw(T0, 0, R(ROUTER_HDR)));
        p.push_back(addi(T1, 0, 0x100)); p.push_back(sw(T1, 0, R(ROUTER_ADDR)));
        p.push_back(addi(T2, 0, 0x5A5)); p.push_back(sw(T2, 0, R(ROUTER_SEND_32)));
        const int wait = int(p.size());                        // wait: EVENTS_PEEK [12], request pending (live)
        p.push_back(lw(T0, 0, R(EVENTS_PEEK))); p.push_back(srli(T0, T0, 12)); p.push_back(andi(T0, T0, 1));
        p.push_back(beq(T0, 0, 4 * (wait - int(p.size()))));
        p.push_back(lw(T1, 0, R(REQ_GET_W0))); p.push_back(andi(T1, T1, 0x7F));
        p.push_back(lw(A0, 0, R(REQ_RECEIVE)));                // pops
        p.push_back(0x00C31313);                               // slli t1, t1, 12
        p.push_back(0x00650533);                               // add a0, a0, t1
    } else if (cc == 1) {
        p.push_back(lui(T0, 1)); p.push_back(addi(T0, T0, -2048));   // REQ_CC1(REQ_OP_STARTED, 0, 0) = 2 << 10
        p.push_back(sw(T0, 0, R(REQ_W0))); p.push_back(sw(0, 0, R(REQ_W1))); p.push_back(sw(0, 0, R(REQ_W2)));
        p.push_back(sw(A0, 0, R(REQ_SEND)));
    }
    p.push_back(sw(A0, 0, R(SIM_EXIT))); p.push_back(J_SELF);
    return p;
}

void report_violations(Chip& c) {
    if (c.violations() != 0) std::fputs(c.violation_report().c_str(), stdout);
}

void switch_off() {
    suite::group("the switch off");
    Chip c(cfg(false));
    suite::check(!c.cfg().cc_cores, "no controller tiles: the Java model");
    // ref: tile 0's router's fifth port is C&C 1's endpoint's (fromCnc, toCnc). Here the chip wires C&C 1's model (unit
    // "cnc") to tile 0's attached router (chip.cpp); the unit exists and tile 0's router is the attached one.
    c.unit<units::Cnc>("cnc");
    suite::check(c.tile(0).unit<units::Router>("router").attached, "tile 0's fifth port on C&C 1's endpoint");
    c.run(300);
    suite::eq(0, int64_t(c.violations()), "violations");
}

void rom_branch() {
    suite::group("the boot ROM's branch for 64-66");
    Chip c(cfg(true));
    suite::check(c.cfg().cc_cores, "controller tiles built");
    c.run(200);
    for (unsigned cc = 0; cc < 3; cc++) {
        units::Rv32& u = core(c, 64 + cc);
        suite::check(u.in_wfi && (u.pc & 0xFF000000u) == BOOTROM_BASE, "C&C %u waits in its ROM (WFI)", cc + 1);
        suite::eq(cc == 0 ? 1 : cc + 1, periph_type(c, 64 + cc, 1), "C&C %u's peripheral 1's type", cc + 1);
    }
    suite::check(core(c, 0).in_wfi, "tiles wait in their ROM too");
    // C&C 2 and 3 first, then C&C 1 (whose program waits for C&C 2's request)
    for (unsigned cc : {1u, 2u, 0u}) { c.host_load(cc, PMEM_BASE, program(int(cc))); c.host_command(cc, CMD_JUMP << 24, {PMEM_BASE}); }
    const bool ok = c.run_until([&] { for (unsigned u = 64; u < 67; u++) if (core(c, u).halt != HALT_EXIT) return false; return true; }, 5000);
    suite::check(ok, "every controller jumped to its program and exited");
    suite::eq(65, sim_exit_value(c, 65), "C&C 2 read its id from PARAMS");
    suite::eq(66, sim_exit_value(c, 66), "C&C 3 read its id from PARAMS");
    suite::eq(65LL * 0x1000 + 65, sim_exit_value(c, 64) & 0xFFFFFFFFLL, "C&C 1 received C&C 2's request: requestor 65, W3 = 65");
    suite::eq(0x5A5, c.tile(5).peek_local(0x100), "C&C 1's packet reached tile 5's data memory through tile 0's fifth port");
    suite::eq(1, int64_t(req_received(c, 0)), "target 0 popped once");
    suite::check(core(c, 0).in_wfi && (core(c, 0).pc & 0xFF000000u) == BOOTROM_BASE, "tile 0 still in its ROM");
    suite::eq(0, int64_t(c.violations()), "violations");
    report_violations(c);
}

// Run the harness's next C&C 1 command and wait until C&C 1 has completed it (its mailbox), so the next command never
// lands before C&C 1 has read this one.
bool cc1_step(Mach& m, const std::function<void()>& cmd, uint64_t max) {
    const uint32_t k = m.mbox(0); cmd();
    return m.run([&] { return m.mbox(0) > k; }, max);
}

void boot_tiles() {
    suite::group("the controllers' boot and phases (sw/cc)");
    // the Java model: the same tile program and phases
    Mach j(Mach::cfg(shuffle));
    j.boot("cmd");
    j.c.cc().command(BROADCAST, 1 << 24 | 5, {7}).go().command(BROADCAST, 1 << 24 | 10, {1}).go().command(BROADCAST, 4 << 24).go();
    suite::check(j.run([&] { return j.all_exited(); }, 20000), "the model: every tile ran the three phases and exited");
    int64_t model_running = 0; for (unsigned k = 0; k < 64; k++) model_running = std::max(model_running, core(j.c, k).first_image_at);
    // the cores
    Mach m(Mach::cores_cfg(shuffle));
    m.boot_cores("cmd");
    suite::check(m.run([&] { return m.cores_booted(); }, 20000), "C&C 1 booted C&C 2, C&C 3 and the tiles: %s", m.halt_summary().c_str());
    suite::eq(65, m.mbox(4), "the first \"started\" request came from C&C 2");
    suite::eq(66, m.mbox(5), "the second from C&C 3");
    for (unsigned cc = 1; cc < 3; cc++)
        suite::check(core(m.c, 64 + cc).first_image_at > 0 && m.c.tile(64 + cc).peek_local(m.cc23.sym("mbox")) != 0,
                     "C&C %u runs its image and sent \"started\"", cc + 1);
    suite::check(m.run([&] { for (unsigned k = 0; k < 64; k++) if (core(m.c, k).first_image_at < 0) return false; return true; }, 2000),
                 "every tile runs its image");
    int64_t running = 0; for (unsigned k = 0; k < 64; k++) running = std::max(running, core(m.c, k).first_image_at);
    std::printf("  boot, reset release -> all 64 tiles running 'cmd': cores %lld cycles (C&C 1 running at %lld, C&C 2/3 started by %lld), the Java model %lld\n",
                (long long)running, (long long)core(m.c, 64).first_image_at,
                (long long)std::max(core(m.c, 65).first_image_at, core(m.c, 66).first_image_at), (long long)model_running);
    suite::check(cc1_step(m, [&] { m.phase(1 << 24 | 5, {7}); }, 20000), "phase 1 through C&C 1");
    suite::check(cc1_step(m, [&] { m.phase(1 << 24 | 10, {1}); }, 20000), "phase 2 through C&C 1");
    suite::check(cc1_step(m, [&] { m.phase(4 << 24); }, 20000), "phase 3 (exit) through C&C 1");
    suite::check(m.run([&] { return m.all_exited(); }, 20000), "every tile ran the three phases and exited: %s", m.halt_summary().c_str());
    int bad = 0; for (unsigned i = 0; i < 64; i++) if (m.exit_value(i) != 23 || m.exit_value(i) != j.exit_value(i)) bad++;
    suite::eq(0, bad, "every tile's sum (5 + 7 + 10 + 1) as under the Java model");
    suite::eq(4, int64_t(cc1p(m.c).gos), "four GOs: the boot's and three phases'");
    suite::eq(0, int64_t(m.c.violations()), "violations");
    report_violations(m.c);
    suite::eq(0, int64_t(j.c.violations()), "the model: violations");
}

// Boot with rcp_boot (recipe 22: CMD_STAGE installs recipe 24's handler and enables the hard interrupt on every tile)
// and the UART at divisor 1. (Built inside the caller: a Mach isn't movable.)
void boot_handlers(Mach& m) {
    m.boot_cores("rcp_boot");
    suite::check(m.run([&] { return m.cores_booted(); }, 20000), "booted");
    suite::check(cc1_step(m, [&] { m.cc1_command(Mach::CC_UART, 1); }, 5000), "UART_DIVISOR = 1 (a fast dump)");
}

std::string serial(Chip& c) { units::Cnc1Peripheral& p = cc1p(c); return std::string(p.serial_out, std::min<unsigned>(p.serial_n, p.kSerial)); }

void error_path() {
    suite::group("the error path: a tile's and C&C 2's 24 reports (DECISIONS-v1.25 8d)");
    Mach m(Mach::cores_cfg(shuffle));
    boot_handlers(m);
    const Mach::Image boot = Mach::Image::load("rcp_boot", m.c.cfg());
    suite::check(cc1_step(m, [&] { m.phase(CMD_STAGE << 24); }, 20000), "CMD_STAGE: every tile's handler installed");
    suite::check(m.run([&] { return m.all_in_wfi(); }, 5000), "the tiles back in wait_for_command()");
    int irq = 0; for (unsigned k = 0; k < 64; k++) if (core(m.c, k).mtvec == boot.sym("trap_handler") && (core(m.c, k).mstatus & 8) != 0) irq++;
    suite::eq(64, irq, "every tile: the image's handler, interrupts on");
    units::Rv32& cc1 = core(m.c, 64); units::Rv32& cc2 = core(m.c, 65);
    suite::check((cc1.mstatus & 8) != 0 && cc1.mtvec == m.cc1.sym("cc1_trap"), "C&C 1: its own handler, interrupts on");
    suite::check((cc2.mstatus & 8) != 0 && cc2.mtvec == m.cc23.sym("trap_handler"), "C&C 2: recipe 24's handler, interrupts on");
    m.c.set_check_errors(false);
    const int64_t t0 = m.c.cycle();
    m.c.tile(9).set_errors(1u << units::RegBlockState::COND_DRIFT);      // two units fail at once
    m.c.tile(65).set_errors(1u << units::RegBlockState::COND_DRIFT);
    suite::check(m.run([&] { return m.toast_info(10) != 0; }, 3000000), "C&C 1 trapped, drained the reports and finished its dump");
    suite::check(cc1p(m.c).toast, "the toast pin is latched");
    suite::eq(CAUSE_EXTERNAL, int32_t(m.toast_info(0)), "C&C 1's mcause: the hard interrupt (its ERRORS [11], the unit-error bit)");
    suite::eq(1LL << 9, int64_t(m.toast_info(2)), "ERROR_LINES_0: tile 9");
    suite::eq(1 << 1, int32_t(m.toast_info(4)), "ERROR_LINES_2 [1]: C&C 2");
    suite::eq(48, int32_t(m.toast_info(5)), "48 reports drained");
    suite::eq(2, int32_t(m.toast_info(6)), "two units began a set");
    suite::eq(2, int32_t(m.toast_info(7)), "both sets complete: 24 reports each, told by sequence 0");
    suite::eq(0, int32_t(m.toast_info(8)), "no flags (every sequence number in its set, once)");
    const std::string out = serial(m.c);
    suite::check(out.find("00000009 00000018 00000018 \n") != std::string::npos && out.find("00000041 00000018 00000018 \n") != std::string::npos,
                 "the UART dump: tile 9 and C&C 2 (65), 24 of 24 each");
    int64_t lines = 0; for (char ch : out) if (ch == '\n') lines++;
    suite::eq(1 + 2 + 48, lines, "the dump's lines: the summary, two units, 48 reports");
    std::printf("  error path: errors at %lld; C&C 1 trapped at +%lld; drain done at +%lld (mcycle); dump done at +%lld\n",
                (long long)t0, (long long)(cc1.trap_at - t0), (long long)(int64_t(m.toast_info(9)) - t0), (long long)(int64_t(m.toast_info(10)) - t0));
    suite::eq(0, int64_t(m.c.violations()), "violations");
    report_violations(m.c);
}

void rom_dump() {
    suite::group("the error path: a unit still under the ROM's handler, its two-report dump (DECISIONS-v1.25 8d)");
    MachConfig k = Mach::cores_cfg(shuffle); k.check_errors = false;
    Mach m(k);
    m.uart_divisor = 1;                                          // C&C 1 traps right after the boot: no command first
    m.boot_cores("cc_bad");                                      // tile 12: a bus error right after GO
    suite::check(m.run([&] { return m.toast_info(10) != 0; }, 400000), "C&C 1 trapped, drained and dumped");
    units::Rv32& t12 = core(m.c, 12);
    suite::eq(CAUSE_LOAD_FAULT, t12.trap_cause, "tile 12 trapped on its load (a bus error)");
    suite::check((t12.mtvec & 0xFF000000u) == BOOTROM_BASE, "tile 12's mtvec: still the ROM's handler");
    suite::eq(CAUSE_EXTERNAL, int32_t(m.toast_info(0)), "C&C 1's mcause: the hard interrupt");
    suite::eq(1LL << 12, int64_t(m.toast_info(2)), "ERROR_LINES_0: tile 12");
    suite::eq(2, int32_t(m.toast_info(5)), "two reports drained");
    suite::eq(1, int32_t(m.toast_info(6)), "one unit began a set");
    suite::eq(1, int32_t(m.toast_info(7)), "its set complete: two reports, told by sequence 24");
    suite::eq(0, int32_t(m.toast_info(8)), "no flags (24 and 25, once each)");
    const uint32_t r = 0x1000, w0a = m.c.tile(64).peek_local(r), w0b = m.c.tile(64).peek_local(r + 16);
    suite::eq(24, (w0a >> 21) & 0x7FF, "the first report's sequence number: 24");
    suite::eq(25, (w0b >> 21) & 0x7FF, "the second's: 25");
    suite::eq(12, w0a & 0x7F, "stamped by the request tree with tile 12's id");
    suite::check((m.c.tile(64).peek_local(r + 4) & (1u << 9)) != 0, "report 24's first word: ERRORS, with [9] (bus error)");
    suite::eq(CAUSE_LOAD_FAULT, int32_t(m.c.tile(64).peek_local(r + 8)), "report 24's second word: mcause");
    const std::string out = serial(m.c);
    suite::check(out.find("0000000c 00000002 00000002 \n") != std::string::npos, "the UART dump: tile 12, 2 of 2");
    suite::eq(0, int64_t(m.c.violations()), "violations");
    report_violations(m.c);
}

void watchdog() {
    suite::group("the error path: the watchdog");
    Mach m(Mach::cores_cfg(shuffle));
    boot_handlers(m);
    suite::check(cc1_step(m, [&] { m.cc1_command(Mach::CC_WATCHDOG, 300); }, 5000), "WATCHDOG = 300");
    m.phase(CMD_STAGE << 24 | 5);                                // 5 packets announced, none sent: no tile READY again
    suite::check(m.run([&] { return m.toast_info(10) != 0; }, 400000), "the watchdog expired; C&C 1 dumped what it had");
    suite::check(cc1p(m.c).toast, "the toast pin is latched");
    suite::eq(0, int32_t(m.toast_info(0)), "mcause 0: the watchdog, not a trap");
    suite::eq(0, int32_t(m.toast_info(5)), "no reports");
    suite::eq(0, int64_t(m.c.violations()), "violations");
    report_violations(m.c);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) shuffle = std::strtoll(argv[1], nullptr, 10);
    switch_off();
    rom_branch();
    boot_tiles();
    error_path();
    rom_dump();
    watchdog();
    return suite::done(shuffle >= 0 ? "CcCoresTest (shuffled, seed " + std::to_string(shuffle) + ")" : "CcCoresTest");
}
