// rv32.h: the tile's core as plain code.
//
// CORE MINIMUM FOR THE PLATFORM BRING-UP (v1.36): this is v1.35's core model (ref's Controller.java) with what the
// RTL's firmware images need to run, and no more; branch `v1.36-core` replaces it with the core of spec/core.md (ISA,
// traps, CSRs, the pipeline's cycle costs). What this file has of v1.36: RV32I + C + Zmmul + Zicsr + Zicond; fetch on any
// halfword from IMEM and the boot ROM; every trap an ordinary trap to mtvec (no simulator halt); the NMI and MEIP; the
// CSR list and reset values; misaligned loads and stores as traps; IMEM and the boot ROM fetch-only and no aliasing;
// region 0xFF through the tile's register bus (register_bus.h), with a window load's 2 cycles; the canonical wait
// reported to the tile. Its cycle costs are still v1.35's knobs (one instruction a cycle, +branch_penalty for a taken
// branch or jump, mul_latency for a multiply) but the two the platform fixes: a window load's latency and the resume
// after a WFI's wake (PENDING-v1.36.md lists the rest).
//
// `Rv32::step(env, t)` runs cycle t, preceded by the catch-up of the cycles since the core last ran (it sleeps through
// a stall and a WFI). Everything the core sees of its tile comes through `env` (the unit's adapter over the framework's
// Ctx, tile_ctl.h's TileCore::Env; a plain struct in tests):
//
//   uint32_t hart_id()               mhartid: the tile's id (the RTL's hart_id input)
//   bool meip()                      the tile's wake at t, MEIP (6.2: from the tile control unit's flip-flop)
//   bool nmi()                       the tile's errors at t, the NMI (6.2: likewise)
//   void core_status(bit)            core_status's pulse for a trap taken at t: ERR_ACCESS_FAULT or
//                                    ERR_ILLEGAL_INSTRUCTION (7.4); it reaches ERRORS at the end of t + 1
//   void canonical_wait(level)       core_status.canonical_wait: the core is, or no longer is, in the canonical wait at t
//   int bus_load(t, a, whole, v)     a load from region 0xFF issued at t (`whole`: a whole aligned word), with its side
//                                    effect: 0 done, v the value; 1 an access fault
//   int bus_store(t, a, v, whole)    a store to region 0xFF issued at t: 0 done; 1 an access fault; 2 it stalls, and is
//                                    issued again at t + 1 (a send into a full holding register, a full request queue)
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/fatal.h"
#include "check.h"
#include "units/linkword.h"
#include "units/regmap.h"
#include "units/trace_log.h"

// A profiling build (-DUNITS_PROFILE) keeps the core's step and its interpreter out of line, so `perf` can tell them from
// the engine's run function they are otherwise inlined into.
#ifdef UNITS_PROFILE
#define UNITS_NOINLINE __attribute__((noinline))
#else
#define UNITS_NOINLINE
#endif

namespace units {

// A unit's digest (sim.h, DEBUG CHECKS): words folded in order.
inline uint64_t digest_mix(uint64_t h, uint64_t v) {
    uint64_t z = h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// The boot ROM is BOOT_ROM_BYTES, 256 bytes (6.3); a larger image is refused (FATAL, at configuration), naming where it
// came from.
inline void check_bootrom(size_t words, const char* from) {
    if (words > BOOTROM_MAX_WORDS)
        FATAL("the boot ROM image %s is %zu bytes; the ROM is %u (machine-spec 6.3)", from, 4 * words, 4u * BOOTROM_MAX_WORDS);
}

struct Rv32Timing {
    uint32_t reg_load_latency = WINDOW_LOAD_LATENCY;   // 6.4: a window load's cycles (a table's is 1)
    uint32_t branch_penalty = 1, mul_latency = 3, ref_cycle = 40;   // v1.35's knobs (ref_cycle: C&C 1's model only)
    // spec/core.md, Wait for interrupt: the instruction after a WFI retires 3 cycles after the cycle the wake condition
    // first holds (R + 3 after a release in R), and, when the condition holds as the WFI retires, 3 cycles after the WFI
    uint32_t wfi_resume = 3;
    // The cycles from a WFI to the first cycle of its canonical wait (asleep, the instruction port idle): fetch fills
    // its queue behind the WFI first. The RTL's figure depends on the code after the WFI (PENDING-v1.36.md).
    uint32_t canonical_wait_delay = 3;
};

// How a core's run ended: a core that parks in its handler (a jump to itself, a WFI) is recorded as parked, and goes on
// running its loop. (`halt` and `ub` are v1.35's ends; nothing reaches them in v1.36: every trap is an ordinary trap.)
enum class Stop : uint8_t { running, halt, parked, ub };

struct Rv32 {
    enum : uint32_t {
        MSTATUS = 0x300, MISA = 0x301, MIE_CSR = 0x304, MTVEC = 0x305, MSTATUSH = 0x310, MCOUNTINHIBIT = 0x320,
        MSCRATCH = 0x340, MEPC = 0x341, MCAUSE = 0x342, MTVAL = 0x343, MIP = 0x344, MCYCLE = 0xB00, MINSTRET = 0xB02,
        MCYCLEH = 0xB80, MINSTRETH = 0xB82, MVENDORID = 0xF11, MHARTID = 0xF14, MCONFIGPTR = 0xF15,
        MSTATUS_MIE = 1u << 3, MSTATUS_MPIE = 1u << 7, MEIE = 1u << 11, MISA_VALUE = 0x40000104,
        CAUSE_FETCH = 1, CAUSE_ILLEGAL = 2, CAUSE_BREAKPOINT = 3, CAUSE_LOAD_MISALIGNED = 4, CAUSE_LOAD_FAULT = 5,
        CAUSE_STORE_MISALIGNED = 6, CAUSE_STORE_FAULT = 7, CAUSE_ECALL = 11, CAUSE_EXTERNAL = 0x8000000B,
        CAUSE_NMI = 0x80000000,
        WFI = 0x10500073,
    };
    static constexpr uint64_t kNever = ~uint64_t(0);

    // ---- architectural state (the reset: pc the boot ROM's first word, mtvec the tile's, everything else 0) ----
    uint32_t x[32] = {};
    uint32_t pc = BOOTROM_BASE;
    uint32_t mstatus = 0, mie = 0, mtvec = 0, mscratch = 0, mepc = 0, mcause = 0, mtval = 0;
    uint64_t mcycle = 0, minstret = 0;
    bool in_wfi = false, in_handler = false, parked = false;
    bool trapped = false;                  // the core has taken a trap since reset: it takes no NMI (6.2)
    bool canon_on = false;                 // the canonical wait reported to the tile
    uint64_t canon_at = kNever;            // the cycle a WFI's wait becomes the canonical wait
    uint32_t stall = 0;
    uint64_t load_wait_t = kNever; uint32_t load_wait_pc = 0;
    int ck_tile = -1; uint64_t ck_t = 0;   // the checker's facts (src/check.h) under this tile, -1 none; the step's cycle
    // (diagnostic only): the lowest nonzero stack pointer, taken at the writes to x2. Off by default: the harness arms
    // it (min_sp_watch) with the current x2.
    bool min_sp_watch = false; int32_t min_sp = INT32_MAX;
    // ---- configuration, and permanent state allocated apart ----
    Rv32Timing timing;
    uint32_t reset_mtvec = 0;              // mtvec's reset value: the boot ROM's handler (6.2)
    uint32_t* dmem = nullptr;              // MEM_WORDS each
    uint32_t* imem = nullptr;
    const uint32_t* bootrom = nullptr; uint32_t bootrom_words = 0;
    TraceLog* log = nullptr;               // null: no trace
    // ---- the run ----
    uint64_t last = 0;                     // the last cycle stepped (cycle 1 is the first)
    Stop stop = Stop::running; uint64_t stop_cycle = 0;
    // ---- statistics ----
    uint64_t instructions = 0, cycles_stalled = 0, stall_branch = 0, stall_mul = 0, stall_reg_load = 0,
             stall_store = 0, cycles_wfi = 0, traps = 0, bus_errors = 0, illegals = 0, wakes = 0;
    // the last trap's cycle and cause (-1 before any), the first fetch from IMEM since the reset (-1 before)
    int64_t trap_at = -1, first_image_at = -1;
    int64_t trap_cause = -1;

    // The reset (18): the architectural state; the memories keep their contents.
    void reset() {
        pc = BOOTROM_BASE; mstatus = mie = mscratch = mepc = mcause = mtval = 0; mtvec = reset_mtvec & ~3u;
        mcycle = minstret = 0;
        in_wfi = in_handler = parked = trapped = canon_on = false; canon_at = kNever; stall = 0; load_wait_t = kNever;
        stop = Stop::running; stop_cycle = 0; trap_at = first_image_at = -1; trap_cause = -1;
    }

    // ================================================================ the cycle
    // Cycle t, after catching up the cycles slept since `last`. Returns the next cycle the core needs to run (kNever:
    // only if its inputs change). A step at a cycle it didn't ask for changes nothing but what that cycle would.
    template <class Env>
    UNITS_NOINLINE uint64_t step(Env& e, uint64_t t) {
        ck_t = t;
        catch_up(t);
        mcycle++;
        if (in_wfi) {                                                   // the wait: (mip & mie) != 0, or the NMI
            const bool irq = (mie & MEIE) && e.meip(), nmi = !trapped && e.nmi();
            if (!irq && !nmi) {
                if (!canon_on && t >= canon_at) { canon_on = true; e.canonical_wait(true); }
                cycles_wfi++;
                return canon_on ? kNever : canon_at;
            }
            in_wfi = false; wakes++;
            if (canon_on) { canon_on = false; e.canonical_wait(false); }
            if (log) log->add(t, T_WAKE, 2);
            // the next instruction at t + wfi_resume; an interrupt the wake brings is taken a cycle sooner (spec/core.md, WFI:
            // w + 2)
            stall = timing.wfi_resume - ((nmi || (mstatus & MSTATUS_MIE)) && timing.wfi_resume ? 1 : 0);
        }
        if (stall > 0) { stall--; cycles_stalled++; return t + 1 + stall; }
        // an instruction boundary: the NMI, then the machine external interrupt (spec/core.md, Traps)
        if (!trapped && e.nmi()) { trap(t, CAUSE_NMI, 0, pc); return after(t); }
        if ((mstatus & MSTATUS_MIE) && (mie & MEIE) && e.meip()) { trap(t, CAUSE_EXTERNAL, 0, pc); return after(t); }
        fetch_execute(e, t);
        return after(t);
    }

    // The cycles last + 1 .. t - 1 were skipped: each would have been a stall cycle or a cycle asleep in WFI (the core
    // asked for nothing earlier, and nothing it reads changed, or it would have been stepped).
    void catch_up(uint64_t t) {
        uint64_t gap = t - last - 1;
        last = t;
        if (gap == 0) return;
        mcycle += gap;
        if (in_wfi) { cycles_wfi += gap; return; }
        DCHECK(gap <= stall, "core skipped %llu cycles with %u stall cycles left", (unsigned long long)gap, stall);
        stall -= uint32_t(gap); cycles_stalled += gap;
    }
    uint64_t after(uint64_t t) {
        // parked (the handler's park: a jump to itself, a WFI): the run's end as recorded (stop, stop_cycle); the core
        // goes on running its loop
        if (parked && stop == Stop::running) { stop = Stop::parked; stop_cycle = t; }
        if (in_wfi) return canon_at > t ? canon_at : kNever;
        return t + 1 + stall;
    }

    // The core's digest: its state and its memories, not their addresses. A core that slept (ready-driven) and one stepped
    // every cycle (step-all) differ only in what catch_up adds per skipped cycle, so those are folded in relative to
    // `last`.
    uint64_t digest() const {
        uint64_t h = 0;
        for (uint32_t v : x) h = digest_mix(h, v);
        const uint64_t w[] = {pc, mstatus, mie, mtvec, mscratch, mepc, mcause, mtval, mcycle - last, minstret,
                              uint64_t(in_wfi) | uint64_t(in_handler) << 1 | uint64_t(parked) << 2 | uint64_t(trapped) << 3
                                  | uint64_t(canon_on) << 4,
                              stall ? stall + last : 0, load_wait_t, load_wait_pc, uint64_t(stop), stop_cycle, canon_at,
                              instructions, cycles_stalled + cycles_wfi - last, stall_branch, stall_mul, stall_reg_load,
                              stall_store, traps, bus_errors, illegals, wakes};
        for (uint64_t v : w) h = digest_mix(h, v);
        for (uint32_t i = 0; i < MEM_WORDS; i++) h = digest_mix(h, uint64_t(dmem[i]) | uint64_t(imem[i]) << 32);
        return h;
    }

    // ================================================================ traps
    // Trap entry (spec/core.md, Traps): every trap goes to mtvec, the NMI included, and ends the NMI's eligibility.
    void trap(uint64_t t, uint32_t cause, uint32_t tval, uint32_t epc) {
        traps++; trap_at = int64_t(t); trap_cause = int64_t(cause); trapped = true;
        mepc = epc & ~1u; mcause = cause; mtval = tval;
        mstatus = (mstatus & ~(MSTATUS_MIE | MSTATUS_MPIE)) | ((mstatus & MSTATUS_MIE) ? uint32_t(MSTATUS_MPIE) : 0u);
        in_handler = true; pc = mtvec & ~3u; stall = 0;
        if (log) log->add(t, T_TRAP, 0, mcause, mepc, mtval, pc);
    }
    template <class Env>
    void illegal(Env& e, uint64_t t, uint32_t ipc, uint32_t raw) {       // mtval: the original bits (16-bit: zero-extended)
        instructions--; illegals++; e.core_status(ERR_ILLEGAL_INSTRUCTION);
        trap(t, CAUSE_ILLEGAL, raw, ipc);
    }
    template <class Env>
    void access_fault(Env& e, uint64_t t, uint32_t ipc, uint32_t cause, uint32_t tval) {
        bus_errors++; e.core_status(ERR_ACCESS_FAULT);
        trap(t, cause, tval, ipc);
    }

    // ================================================================ fetch and execute
    // One halfword of an instruction: IMEM and the boot ROM serve fetches (5.1); anywhere else is an access fault.
    bool fetch16(uint64_t t, uint32_t a, uint32_t& h) {
        uint32_t w;
        if (region(a) == R_IMEM && offset(a) < MEM_BYTES) { w = imem[offset(a) >> 2]; if (first_image_at < 0) first_image_at = int64_t(t); }
        else if (region(a) == R_BOOTROM && offset(a) >> 2 < bootrom_words) w = bootrom[offset(a) >> 2];
        else return false;
        h = (w >> (8 * (a & 2))) & 0xFFFF;
        return true;
    }
    template <class Env>
    UNITS_NOINLINE void fetch_execute(Env& e, uint64_t t) {
        const uint32_t ipc = pc;
        uint32_t lo, hi;
        if (!fetch16(t, ipc, lo)) { access_fault(e, t, ipc, CAUSE_FETCH, 0); return; }
        if ((lo & 3) != 3) {                                             // a 16-bit instruction: its 32-bit form
            const uint32_t ins = expand(lo);
            if (ins == 0) { instructions++; illegal(e, t, ipc, lo); return; }
            execute(e, t, ins, ipc, 2, lo);
            return;
        }
        if (!fetch16(t, ipc + 2, hi)) { access_fault(e, t, ipc, CAUSE_FETCH, 0); return; }
        execute(e, t, lo | hi << 16, ipc, 4, lo | hi << 16);
    }

    // ---- RV32C: the 32-bit form of a 16-bit instruction, 0 for an illegal or reserved one ----
    static uint32_t enc_i(uint32_t imm, uint32_t rs1, uint32_t f3, uint32_t rd, uint32_t op) { return imm << 20 | rs1 << 15 | f3 << 12 | rd << 7 | op; }
    static uint32_t enc_s(uint32_t imm, uint32_t rs2, uint32_t rs1, uint32_t f3) { return (imm >> 5 & 0x7F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (imm & 0x1F) << 7 | 0x23; }
    static uint32_t enc_r(uint32_t f7, uint32_t rs2, uint32_t rs1, uint32_t f3, uint32_t rd) { return f7 << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | rd << 7 | 0x33; }
    static uint32_t enc_b(uint32_t off, uint32_t rs2, uint32_t rs1, uint32_t f3) {
        return (off >> 12 & 1) << 31 | (off >> 5 & 0x3F) << 25 | rs2 << 20 | rs1 << 15 | f3 << 12 | (off >> 1 & 0xF) << 8 | (off >> 11 & 1) << 7 | 0x63;
    }
    static uint32_t enc_j(uint32_t off, uint32_t rd) {
        return (off >> 20 & 1) << 31 | (off >> 1 & 0x3FF) << 21 | (off >> 11 & 1) << 20 | (off >> 12 & 0xFF) << 12 | rd << 7 | 0x6F;
    }
    static uint32_t expand(uint32_t h) {
        const uint32_t f3 = h >> 13 & 7, b12 = h >> 12 & 1;
        const uint32_t rdp = 8 + (h >> 2 & 7), rs1p = 8 + (h >> 7 & 7);   // the three-bit registers x8-x15
        const uint32_t rd = h >> 7 & 31, rs2 = h >> 2 & 31;
        const uint32_t imm6 = (b12 ? 0xFFFFFFE0u : 0u) | (h >> 2 & 0x1F);  // sign-extended [5|4:0]
        switch (h & 3) {
        case 0:
            switch (f3) {
            case 0: {                                                    // c.addi4spn
                const uint32_t u = (h >> 7 & 0xF) << 6 | (h >> 11 & 3) << 4 | (h >> 5 & 1) << 3 | (h >> 6 & 1) << 2;
                return u ? enc_i(u, 2, 0, rdp, 0x13) : 0;
            }
            case 2: case 6: {                                            // c.lw, c.sw
                const uint32_t u = (h >> 10 & 7) << 3 | (h >> 6 & 1) << 2 | (h >> 5 & 1) << 6;
                return f3 == 2 ? enc_i(u, rs1p, 2, rdp, 0x03) : enc_s(u, rdp, rs1p, 2);
            }
            default: return 0;
            }
        case 1:
            switch (f3) {
            case 0: return enc_i(imm6 & 0xFFF, rd, 0, rd, 0x13);         // c.addi (c.nop)
            case 1: case 5: {                                            // c.jal (RV32), c.j
                const uint32_t off = (b12 ? 0xFFFFF800u : 0u) | (h >> 8 & 1) << 10 | (h >> 9 & 3) << 8 | (h >> 6 & 1) << 7
                                     | (h >> 7 & 1) << 6 | (h >> 2 & 1) << 5 | (h >> 11 & 1) << 4 | (h >> 3 & 7) << 1;
                return enc_j(off, f3 == 1 ? 1 : 0);
            }
            case 2: return enc_i(imm6 & 0xFFF, 0, 0, rd, 0x13);          // c.li
            case 3:
                if (rd == 2) {                                           // c.addi16sp
                    const uint32_t v = (b12 ? 0xFFFFFE00u : 0u) | (h >> 3 & 3) << 7 | (h >> 5 & 1) << 6 | (h >> 2 & 1) << 5 | (h >> 6 & 1) << 4;
                    return v ? enc_i(v & 0xFFF, 2, 0, 2, 0x13) : 0;
                }
                return imm6 ? (imm6 << 12 | rd << 7 | 0x37) : 0;         // c.lui
            case 4:
                switch (h >> 10 & 3) {
                case 0: return b12 ? 0 : enc_i(rs2, rs1p, 5, rs1p, 0x13);             // c.srli
                case 1: return b12 ? 0 : enc_i(0x400 | rs2, rs1p, 5, rs1p, 0x13);     // c.srai
                case 2: return enc_i(imm6 & 0xFFF, rs1p, 7, rs1p, 0x13);              // c.andi
                default:
                    if (b12) return 0;                                   // RV64's c.subw, c.addw
                    switch (h >> 5 & 3) {
                    case 0: return enc_r(0x20, rdp, rs1p, 0, rs1p);      // c.sub
                    case 1: return enc_r(0, rdp, rs1p, 4, rs1p);         // c.xor
                    case 2: return enc_r(0, rdp, rs1p, 6, rs1p);         // c.or
                    default: return enc_r(0, rdp, rs1p, 7, rs1p);        // c.and
                    }
                }
            default: {                                                   // c.beqz, c.bnez
                const uint32_t off = (b12 ? 0xFFFFFF00u : 0u) | (h >> 5 & 3) << 6 | (h >> 2 & 1) << 5 | (h >> 10 & 3) << 3 | (h >> 3 & 3) << 1;
                return enc_b(off, 0, rs1p, f3 == 6 ? 0 : 1);
            }
            }
        case 2:
            switch (f3) {
            case 0: return b12 ? 0 : enc_i(rs2, rd, 1, rd, 0x13);        // c.slli
            case 2: {                                                    // c.lwsp
                const uint32_t u = b12 << 5 | (h >> 4 & 7) << 2 | (h >> 2 & 3) << 6;
                return rd ? enc_i(u, 2, 2, rd, 0x03) : 0;
            }
            case 4:
                if (!b12) {
                    if (rs2 == 0) return rd ? enc_i(0, rd, 0, 0, 0x67) : 0;          // c.jr
                    return enc_r(0, rs2, 0, 0, rd);                                  // c.mv
                }
                if (rs2 == 0) return rd ? enc_i(0, rd, 0, 1, 0x67) : 0x00100073;     // c.jalr; c.ebreak
                return enc_r(0, rs2, rd, 0, rd);                                     // c.add
            case 6: {                                                    // c.swsp
                const uint32_t u = (h >> 9 & 0xF) << 2 | (h >> 7 & 3) << 6;
                return enc_s(u, rs2, 2, 2);
            }
            default: return 0;
            }
        default: return 0;
        }
    }

    // One instruction at t: `ins` its 32-bit form, `len` its length, `raw` its bits as fetched (an illegal one's mtval).
    template <class Env>
    void execute(Env& e, uint64_t t, uint32_t ins, uint32_t ipc, uint32_t len, uint32_t raw) {
        instructions++;
        const uint32_t op = ins & 0x7F, rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7, rs1 = (ins >> 15) & 31,
                       rs2 = (ins >> 20) & 31, f7 = ins >> 25;
        const int32_t sins = int32_t(ins);
        const uint32_t a = x[rs1], b = x[rs2];
        uint32_t next = ipc + len, res = 0;
        bool wr = false;
        switch (op) {
        case 0x37: res = ins & 0xFFFFF000u; wr = true; break;                                   // lui
        case 0x17: res = ipc + (ins & 0xFFFFF000u); wr = true; break;                           // auipc
        case 0x6F: {                                                                            // jal
            uint32_t imm = uint32_t(sins >> 31) << 20 | (ins & 0xFF000) | ((ins >> 20) & 1) << 11 | ((ins >> 21) & 0x3FF) << 1;
            res = next; wr = true; next = ipc + imm; stall += timing.branch_penalty; stall_branch += timing.branch_penalty;
            if (next == ipc && in_handler) parked = true;                                       // the handler's park
            break;
        }
        case 0x67:                                                                              // jalr
            if (f3 != 0) { illegal(e, t, ipc, raw); return; }
            res = next; wr = true; next = (a + uint32_t(sins >> 20)) & ~1u;
            stall += timing.branch_penalty; stall_branch += timing.branch_penalty;
            break;
        case 0x63: {                                                                            // branches
            uint32_t imm = uint32_t(sins >> 31) << 12 | ((ins >> 7) & 1) << 11 | ((ins >> 25) & 0x3F) << 5 | ((ins >> 8) & 0xF) << 1;
            bool take;
            switch (f3) {
            case 0: take = a == b; break;
            case 1: take = a != b; break;
            case 4: take = int32_t(a) < int32_t(b); break;
            case 5: take = int32_t(a) >= int32_t(b); break;
            case 6: take = a < b; break;
            case 7: take = a >= b; break;
            default: illegal(e, t, ipc, raw); return;
            }
            if (take) { next = ipc + imm; stall += timing.branch_penalty; stall_branch += timing.branch_penalty; }
            break;
        }
        case 0x03: {                                                                            // loads
            const uint32_t ea = a + uint32_t(sins >> 20);
            if (f3 > 5 || f3 == 3) { illegal(e, t, ipc, raw); return; }
            const uint32_t width = 1u << (f3 & 3);
            if (ea & (width - 1)) { instructions--; trap(t, CAUSE_LOAD_MISALIGNED, ea, ipc); return; }
            if (timing.reg_load_latency > 1 && in_windows(ea) && !(load_wait_t == t - 1 && load_wait_pc == ipc)) {
                load_wait_t = t; load_wait_pc = ipc; instructions--; stall_reg_load++; cycles_stalled++;
                return;                                       // a window load returns in its second cycle (6.4)
            }
            uint32_t v;
            if (!load(e, t, ea, width, v)) { instructions--; access_fault(e, t, ipc, CAUSE_LOAD_FAULT, ea); return; }
            const uint32_t sh = (ea & 3) * 8;
            switch (f3) {
            case 0: res = uint32_t(int32_t(int8_t(v >> sh))); break;
            case 1: res = uint32_t(int32_t(int16_t(v >> sh))); break;
            case 2: res = v; break;
            case 4: res = (v >> sh) & 0xFF; break;
            default: res = (v >> sh) & 0xFFFF; break;
            }
            wr = true;
            break;
        }
        case 0x23: {                                                                            // stores
            const uint32_t ea = a + (uint32_t(sins >> 25) << 5 | rd);
            if (f3 > 2) { illegal(e, t, ipc, raw); return; }
            const uint32_t width = 1u << f3;
            if (ea & (width - 1)) { instructions--; trap(t, CAUSE_STORE_MISALIGNED, ea, ipc); return; }
            const uint32_t sh = (ea & 3) * 8;
            const uint32_t mask = (f3 == 2 ? 0xFFFFFFFFu : f3 == 1 ? 0xFFFFu : 0xFFu) << sh;
            const int r = store(e, t, ea, b << sh, mask);
            if (r == 2) {                                     // it stalls: issued again at t + 1
                if (log) log->add(t, T_REFUSED);
                instructions--; stall_store++; cycles_stalled++;
                return;
            }
            if (r == 1) { instructions--; access_fault(e, t, ipc, CAUSE_STORE_FAULT, ea); return; }
            break;
        }
        case 0x13: {                                                                            // op-imm
            uint32_t imm = uint32_t(sins >> 20), sh = rs2;
            switch (f3) {
            case 0: res = a + imm; break;
            case 2: res = int32_t(a) < int32_t(imm); break;
            case 3: res = a < imm; break;
            case 4: res = a ^ imm; break;
            case 6: res = a | imm; break;
            case 7: res = a & imm; break;
            case 1: if (f7 != 0) { illegal(e, t, ipc, raw); return; } res = a << sh; break;
            default:
                if (f7 == 0) res = a >> sh;
                else if (f7 == 0x20) res = uint32_t(int32_t(a) >> sh);
                else { illegal(e, t, ipc, raw); return; }
            }
            wr = true;
            break;
        }
        case 0x33:                                                                              // op
            if (f7 == 1) {                                    // Zmmul; the divide opcodes are illegal
                int64_t la = int32_t(a), lb = int32_t(b);
                switch (f3) {
                case 0: res = a * b; break;
                case 1: res = uint32_t(uint64_t(la * lb) >> 32); break;
                case 2: res = uint32_t(uint64_t(la * int64_t(uint64_t(b))) >> 32); break;
                case 3: res = uint32_t((uint64_t(a) * uint64_t(b)) >> 32); break;
                default: illegal(e, t, ipc, raw); return;
                }
                stall += timing.mul_latency - 1; stall_mul += timing.mul_latency - 1;
            } else if (f7 == 7) {                             // Zicond
                if (f3 == 5) res = b == 0 ? 0 : a;            // czero.eqz
                else if (f3 == 7) res = b != 0 ? 0 : a;       // czero.nez
                else { illegal(e, t, ipc, raw); return; }
            } else if (f7 == 0 || f7 == 0x20) {
                if (f7 == 0x20 && f3 != 0 && f3 != 5) { illegal(e, t, ipc, raw); return; }
                switch (f3) {
                case 0: res = f7 == 0 ? a + b : a - b; break;
                case 1: res = a << (b & 31); break;
                case 2: res = int32_t(a) < int32_t(b); break;
                case 3: res = a < b; break;
                case 4: res = a ^ b; break;
                case 5: res = f7 == 0 ? a >> (b & 31) : uint32_t(int32_t(a) >> (b & 31)); break;
                case 6: res = a | b; break;
                default: res = a & b; break;
                }
            } else { illegal(e, t, ipc, raw); return; }
            wr = true;
            break;
        case 0x0F: if (f3 != 0) { illegal(e, t, ipc, raw); return; } break;                     // fence: a no-op; fence.i illegal
        case 0x73:
            if (ins == 0x00000073) { instructions--; trap(t, CAUSE_ECALL, 0, ipc); return; }
            if (ins == 0x00100073) { instructions--; trap(t, CAUSE_BREAKPOINT, ipc, ipc); return; }
            if (ins == WFI) {
                if (in_handler) parked = true;                // a WFI in the handler: its park
                if (((mie & MEIE) && e.meip()) || (!trapped && e.nmi())) stall += timing.wfi_resume - 1;   // no wait
                else { in_wfi = true; canon_on = false; canon_at = t + timing.canonical_wait_delay; }
            } else if (ins == 0x30200073) {                   // mret
                next = mepc;
                mstatus = (mstatus & ~MSTATUS_MIE) | ((mstatus & MSTATUS_MPIE) ? uint32_t(MSTATUS_MIE) : 0u) | MSTATUS_MPIE;
            } else if (f3 != 0 && f3 != 4) {                  // the CSR instructions
                uint32_t csr = ins >> 20, old;
                if (!csr_read(e, csr, old)) { illegal(e, t, ipc, raw); return; }
                uint32_t src = f3 >= 5 ? rs1 : a;
                bool writes = (f3 & 3) == 1 || rs1 != 0;
                if (writes) {
                    uint32_t v = (f3 & 3) == 1 ? src : (f3 & 3) == 2 ? old | src : old & ~src;
                    if (!csr_write(csr, v)) { illegal(e, t, ipc, raw); return; }
                }
                res = old; wr = true;
            } else { illegal(e, t, ipc, raw); return; }
            break;
        default: illegal(e, t, ipc, raw); return;                       // custom-0 and custom-1 among them (no coprocessor)
        }
        bool w = wr && rd != 0;
        if (log) log->add(t, T_RETIRE, w, ipc, ins, a, b, res);
        if (w) {
            x[rd] = res;
            if (min_sp_watch && rd == 2 && res != 0 && int32_t(res) < min_sp) min_sp = int32_t(res);
        }
        minstret++;
        pc = next;
    }

    // ================================================================ the CSRs (spec/core.md, CSRs)
    template <class Env>
    bool csr_read(Env& e, uint32_t csr, uint32_t& v) const {
        switch (csr) {
        case MSTATUS: v = mstatus | 3u << 11; return true;
        case MISA: v = MISA_VALUE; return true;
        case MIE_CSR: v = mie; return true;
        case MTVEC: v = mtvec; return true;
        case MSCRATCH: v = mscratch; return true;
        case MEPC: v = mepc; return true;
        case MCAUSE: v = mcause; return true;
        case MTVAL: v = mtval; return true;
        case MIP: v = e.meip() ? uint32_t(MEIE) : 0u; return true;
        case MCYCLE: v = uint32_t(mcycle); return true;
        case MCYCLEH: v = uint32_t(mcycle >> 32); return true;
        case MINSTRET: v = uint32_t(minstret); return true;
        case MINSTRETH: v = uint32_t(minstret >> 32); return true;
        case MHARTID: v = e.hart_id(); return true;
        default:
            v = 0;                                                             // the read-zero ones
            return csr == MSTATUSH || csr == MCOUNTINHIBIT || (csr >= 0x323 && csr <= 0x33F) || (csr >= 0xB03 && csr <= 0xB1F)
                   || (csr >= 0xB83 && csr <= 0xB9F) || (csr >= MVENDORID && csr <= 0xF13) || csr == MCONFIGPTR;
        }
    }
    bool csr_write(uint32_t csr, uint32_t v) {
        if ((csr >> 10 & 3) == 3) return false;                                // a read-only address
        switch (csr) {
        case MSTATUS: mstatus = v & (MSTATUS_MIE | MSTATUS_MPIE); return true;
        case MIE_CSR: mie = v & MEIE; return true;
        case MTVEC: mtvec = v & ~3u; return true;
        case MSCRATCH: mscratch = v; return true;
        case MEPC: mepc = v & ~1u; return true;
        case MCAUSE: mcause = v & 0x8000001Fu; return true;
        case MTVAL: mtval = v; return true;
        case MCYCLE: mcycle = (mcycle & ~0xFFFFFFFFull) | v; return true;
        case MCYCLEH: mcycle = (mcycle & 0xFFFFFFFFull) | uint64_t(v) << 32; return true;
        case MINSTRET: minstret = ((minstret & ~0xFFFFFFFFull) | v) - 1; return true;      // its own increment dropped
        case MINSTRETH: minstret = ((minstret & 0xFFFFFFFFull) | uint64_t(v) << 32) - 1; return true;
        default: return true;                                                  // listed, writes ignored (csr_read refused the rest)
        }
    }

    // ================================================================ the data side
    // A load of `width` bytes (the aligned word holding ea); false: an access fault. DMEM alone serves loads (5.1): IMEM
    // and the boot ROM are fetch-only, and an address past DMEM's size faults (no aliasing).
    template <class Env>
    bool load(Env& e, uint64_t t, uint32_t ea, uint32_t width, uint32_t& v) {
        const uint32_t a = ea & ~3u;
        if (region(a) == R_DMEM) { if (offset(a) >= MEM_BYTES) return false; v = dmem[offset(a) >> 2]; return true; }
        if (region(a) == R_REGS) return e.bus_load(t, a, width == 4, v) == 0;
        return false;
    }
    // A store at t (value and mask shifted into place): 0 done, 1 an access fault, 2 it stalls.
    template <class Env>
    int store(Env& e, uint64_t t, uint32_t ea, uint32_t v, uint32_t mask) {
        const uint32_t a = ea & ~3u;
        if (region(a) == R_DMEM) {
            if (offset(a) >= MEM_BYTES) return 1;
            const uint32_t w = offset(a) >> 2;
            dmem[w] = (dmem[w] & ~mask) | (v & mask);
            return 0;
        }
        if (region(a) == R_REGS) return e.bus_store(t, a, v, mask == 0xFFFFFFFFu);
        return 1;
    }
};

}  // namespace units
