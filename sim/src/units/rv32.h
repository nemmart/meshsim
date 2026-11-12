// rv32.h: the tile controller's core, `ref`'s Controller.java (v1.3) line for line, as plain code.
//
// `Rv32::step(env, t)` is Controller.runCycle(t), preceded by the catch-up of the cycles since the core last ran (it sleeps
// through a stall, a WFI and the end of its run). Everything the core sees of its tile comes through `env` (one class per
// use: the unit's adapter over the framework's Ctx, a plain struct in tests and in the direct driver):
//
//   uint32_t reg(unsigned r)    the register block's view at t (PARAMS, STATUS, ERRORS, EVENTS, EVENTS_MASK,
//                               EVENTS_REQUIRED: the state at the end of t - 1)
//   bool wake()                 the WFI wake term at t (TileControl.wakeTerm)
//   bool irq()                  ERRORS != 0 at t (TileControl.errorsAt(t) != 0)
//   bool can_push()             a REQUEST_SEND store may complete at t (RequestPort.canPush)
//   void post(op, arg, v)       what the block applies at the end of t (one a cycle): BUS_STORE (register, value),
//                               BUS_CONSUME (-, the EVENTS bits returned), BUS_ERROR (condition, -)
//
// M2 (W8), each optional (an environment without it, M1's benches, stops with a FATAL naming what it lacks):
//   void send(lo, hdr, hi)      a ROUTER_SEND_* store's packet on the send holding register (W9's injection unit)
//   bool send_refused()         the send put of t - 1 was refused: the core puts the same packet again and stalls
//   void table_store(table, i, v)  a table-window store (table_store.h), applied before t + 1's readers
//   uint32_t hist(i)            HIST[i] for a one-cycle table-window load
// M3 (W15), the peripheral slot (AUDIT-M3 3.1), each optional (without them a present window is FATAL):
//   int window_load(n, w, v)    a load of peripheral n's window word w at t (the slot's RegFile; a consuming EVENTS
//                               load also puts its consume): 1 read, 2 a reserved or undefined word (reads 0, flagged)
//   bool window_stalls(n, w)    a store to w must retry at t (S8)
//   bool window_store(t, n, w, v)  a store of t to the slot (S2); false: a bus error (a standard read-only word, or a
//                               defined read-only word of the peripheral's)
// What isn't modelled yet stops with FATAL("... not modelled yet"), naming the register or window.
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/fatal.h"
#include "check.h"
#include "units/linkword.h"
#include "units/regmap.h"
#include "units/table_store.h"
#include "units/trace_log.h"

// A profiling build (-DUNITS_PROFILE) keeps the core's step and its interpreter out of line, so `perf` can tell them from
// the engine's run function they are otherwise inlined into.
#ifdef UNITS_PROFILE
#define UNITS_NOINLINE __attribute__((noinline))
#else
#define UNITS_NOINLINE
#endif

namespace units {

enum : uint32_t { BUS_STORE = 1, BUS_CONSUME = 2, BUS_ERROR = 3 };

// A unit's digest (sim.h, DEBUG CHECKS): words folded in order.
inline uint64_t digest_mix(uint64_t h, uint64_t v) {
    uint64_t z = h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// W23 (P5): Controller.checkBootRom (v1.25, DECISIONS-v1.25 s8d): the boot ROM is 256 bytes, 64 words; a larger image
// is refused (FATAL, at configuration: ref's IllegalStateException), naming where it came from.
inline void check_bootrom(size_t words, const char* from) {
    if (words > BOOTROM_MAX_WORDS)
        FATAL("the boot ROM image %s is %zu bytes; the ROM is %u (spec 6.2, DECISIONS-v1.25 s8d)", from, 4 * words, 4u * BOOTROM_MAX_WORDS);
}

struct Rv32Timing {
    uint32_t reg_load_latency = 2, branch_penalty = 1, mul_latency = 3, ref_cycle = 40;
};

// How a core's run ended (TRACE-FORMAT's END): it runs until one of these, then nothing (TraceRun's chip stops there).
enum class Stop : uint8_t { running, halt, parked, ub };

// W26 (invariant 8, ControllerMemories' read tracking; a statistic in every build, out of the digests): the data words
// holding a reply or a read-port result the core hasn't loaded or stored yet, and command slot 0 not yet loaded. The
// port-B side (marking) is the tile core's (tile_ctl.h, TileCore::track_port_b); the core's side is `loaded`/`stored`
// (ref's cm.loaded, cm.stored), which also remember the last few accesses with their cycle: a port-B write reaches the
// core a cycle after ref's memory has it, so a word the core touched in that cycle ends up read, as in ref.
struct CmemTrack {
    static constexpr uint32_t CMD_DWORD = 0x7FC0 >> 2;
    uint64_t unread[MEM_WORDS / 64] = {};
    bool slot0_unread = false;
    uint64_t overwritten_unread = 0, unread_commands = 0;   // ref's cmem.overwrittenUnread, unreadCommands
    struct Touch { uint64_t t = ~0ull; uint32_t w = 0; bool load = false; };
    Touch touch[4] = {};
    uint8_t touch_n = 0;
    void touched(uint32_t w, uint64_t t, bool load) { touch[touch_n++ & 3] = Touch{t, w, load}; }
    void loaded(uint32_t w, uint64_t t) { unread[w >> 6] &= ~(1ull << (w & 63)); if (w == CMD_DWORD) slot0_unread = false; touched(w, t, true); }
    void stored(uint32_t w, uint64_t t) { unread[w >> 6] &= ~(1ull << (w & 63)); touched(w, t, false); }
    bool touched_at(uint32_t w, uint64_t t, bool load_only) const {
        for (const Touch& x : touch) if (x.t == t && x.w == w && (x.load || !load_only)) return true;
        return false;
    }
};

struct Rv32 {
    enum : int { RUN = 0, HALT_ECALL = 2, HALT_EBREAK = 3, HALT_EXIT = 5, HALT_FETCH = 6, HALT_JUMP = 7, HALT_TRAP2 = 8 };
    enum : uint32_t {
        MSTATUS = 0x300, MISA = 0x301, MIE_CSR = 0x304, MTVEC = 0x305, MSCRATCH = 0x340, MEPC = 0x341, MCAUSE = 0x342,
        MTVAL = 0x343, MIP = 0x344, MCYCLE = 0xB00, MCYCLEH = 0xB80, MTIME = 0xC01, MTIMEH = 0xC81,
        MSTATUS_MIE = 1u << 3, MSTATUS_MPIE = 1u << 7, MEIE = 1u << 11,
        CAUSE_FETCH = 1, CAUSE_ILLEGAL = 2, CAUSE_LOAD_FAULT = 5, CAUSE_STORE_FAULT = 7, CAUSE_EXTERNAL = 0x8000000B,
        WFI = 0x10500073,
    };
    static constexpr uint64_t kNever = ~uint64_t(0);

    // ---- architectural state (Controller.reset: pc in the boot ROM, everything else 0) ----
    uint32_t x[32] = {};
    uint32_t pc = BOOTROM_BASE;
    uint32_t mstatus = 0, mie = 0, mtvec = 0, mscratch = 0, mepc = 0, mcause = 0, mtval = 0;
    uint64_t mcycle = 0;
    int halt = RUN;
    bool in_wfi = false, in_handler = false, parked = false, ub = false;
    uint32_t stall = 0;
    uint64_t load_wait_t = kNever; uint32_t load_wait_pc = 0;
    uint64_t pending_t = kNever; uint64_t send_w[3] = {};   // M2: a send store's packet awaiting acceptance (ref's pendingT)
    int ck_tile = -1; uint64_t ck_t = 0;   // W20: the checker's facts (src/check.h) under this tile, -1 none; the step's cycle
    CmemTrack* cm = nullptr;               // W26: invariant 8's read tracking (the tile core's; null: none)
    // W24 (diagnostic only; owner's Q3, 2026-11-11): the lowest nonzero stack pointer, SortTest's "stack headroom"
    // (ref's monitor reads every core's x[2] after every cycle; here taken at the writes to x2, the only place it
    // changes, so idle-skipped cycles need nothing). Off by default: the harness arms it (min_sp_watch) with the current
    // x2. Off, it costs one predictable branch on a retire that writes a register; nothing else reads it.
    bool min_sp_watch = false; int32_t min_sp = INT32_MAX;
    // ---- configuration, and permanent state allocated apart ----
    Rv32Timing timing;
    uint32_t periph_present = 1;           // PARAMS [2:0]: which windows are present (present: not modelled yet)
    uint32_t* dmem = nullptr;              // MEM_WORDS each
    uint32_t* imem = nullptr;
    const uint32_t* bootrom = nullptr; uint32_t bootrom_words = 0;
    TraceLog* log = nullptr;               // null: no trace
    // ---- the run ----
    uint64_t last = 0;                     // the last cycle stepped (cycle 1 is the first)
    Stop stop = Stop::running; uint64_t stop_cycle = 0;
    // ---- statistics (Controller's) and ref's violations (counted, never a stop: TileControl.violate) ----
    uint64_t instructions = 0, cycles_stalled = 0, stall_branch = 0, stall_mul = 0, stall_reg_load = 0,
             stall_request = 0, cycles_wfi = 0, traps = 0, bus_errors = 0, illegals = 0, violations = 0,
             alias_accesses = 0, reserved_accesses = 0, trace_stores = 0, stall_send = 0, sends = 0;
    // W23 (P5; statistics the control suites read, ref's Controller fields; not in the digest, not in the trace):
    // the last trap's cycle and cause (trapAt, trapCause: -1 before any), the first fetch from program memory since
    // the reset (firstImageAt: the image running; -1 before). A reset (W20) sets them back to -1, as ref's reset.
    int64_t trap_at = -1, first_image_at = -1;
    int32_t trap_cause = -1;
    // W23 (P5): the WFI wakes (TileControl.takeWake's goWakes, eventWakes; ref counts them in the block, here where the
    // core takes the wake) and the program's WFIs (Controller.wfiLog: {entered, woke, cause 1 GO / 2 event} per WFI
    // outside the boot ROM; the first kWfiLog kept, wfi_n counts all, D24: a fixed array). Statistics only.
    static constexpr unsigned kWfiLog = 16;
    int64_t wfi_log[kWfiLog][3] = {};
    uint32_t wfi_n = 0;
    uint64_t go_wakes = 0, event_wakes = 0;
    int64_t wfi_at = 0; bool wfi_in_rom = false;
    // W26: TileControl.takeWake's check: a wake by an event other than GO while done is set, the done gate off (Config.
    // doneGate; with the gate only GO wakes): the fact only (ref's ctl.violate)
    template <class Env>
    void woken_in_done(Env& e, uint64_t t) {
        if constexpr (requires { e.done_gate(); })
            if (ck_tile >= 0 && !e.done_gate() && (e.reg(STATUS) >> ST_DONE & 1) && !(e.reg(EVENTS) >> EV_GO & 1))
                sim::check::record(t, ck_tile, sim::check::Kind::CTL_WFI_WOKEN);
    }
    void took_wake(bool go, int64_t entered, uint64_t t, bool rom) {
        if (go) go_wakes++; else event_wakes++;
        if (rom) return;
        if (wfi_n < kWfiLog) { wfi_log[wfi_n][0] = entered; wfi_log[wfi_n][1] = int64_t(t); wfi_log[wfi_n][2] = go ? 1 : 2; }
        wfi_n++;
    }

    // ================================================================ the cycle
    // Controller.runCycle(t), after catching up the cycles slept since `last`. Returns the next cycle the core needs
    // to run (kNever: only if its inputs change). A step at a cycle it didn't ask for is exactly ref's runCycle, so a
    // core stepped every cycle and one stepped only when it asks end in the same state.
    template <class Env>
    UNITS_NOINLINE uint64_t step(Env& e, uint64_t t) {
        ck_t = t;                                                              // W20: the cycle of this step's facts
        if (stop != Stop::running) return kNever;                     // the run ended: nothing more (mcycle frozen)
        catch_up(t);
        mcycle++;                                                       // 1.
        // 2. halted: a halt ends the run (below), so never here.
        if constexpr (requires { e.send_refused(); }) {                 // 3. a refused send: retry, the core stalls
            if (pending_t == t - 1 && e.send_refused()) {
                e.send(send_w[0], send_w[1], send_w[2]);
                pending_t = t; stall_send++; cycles_stalled++;
                return t + 1;
            }
        }
        if (interrupt_pending(e) && stall == 0) {                       // 4.
            in_wfi = false; trap(t, CAUSE_EXTERNAL, 0, pc, pc);
            return after(t);
        }
        if (in_wfi) {                                                   // 5.
            if (!e.wake()) { cycles_wfi++; return kNever; }
            in_wfi = false;
            took_wake(e.reg(EVENTS) >> EV_GO & 1, wfi_at, t, wfi_in_rom);   // W23: statistics
            woken_in_done(e, t);                                       // W26
            if (log) log->add(t, T_WAKE, (e.reg(EVENTS) >> EV_GO & 1) ? 1 : 2);
        }
        if (stall > 0) { stall--; cycles_stalled++; return t + 1 + stall; }   // 6.
        fetch_execute(e, t);                                            // 7.
        return after(t);
    }

    // The cycles t0 = last + 1 .. t - 1 were skipped: each would have been a stall cycle or a cycle asleep in WFI (the
    // core asked for nothing earlier, and nothing it reads changed, or it would have been stepped).
    void catch_up(uint64_t t) {
        uint64_t gap = t - last - 1;
        last = t;
        if (gap == 0) return;
        mcycle += gap;
        if (in_wfi) { cycles_wfi += gap; return; }
        DCHECK(gap <= stall, "core skipped %llu cycles with %u stall cycles left",
               (unsigned long long)gap, stall);
        stall -= uint32_t(gap); cycles_stalled += gap;
    }
    uint64_t after(uint64_t t) {
        if (halt != RUN || ub || parked) { stop = halt != RUN ? Stop::halt : ub ? Stop::ub : Stop::parked; stop_cycle = t; return kNever; }
        if (in_wfi) return kNever;
        return t + 1 + stall;
    }
    template <class Env>
    bool interrupt_pending(Env& e) const { return (mstatus & MSTATUS_MIE) && (mie & MEIE) && e.irq(); }

    // The core's digest: its state and its memories, not their addresses. A core that slept (ready-driven) and one stepped
    // every cycle (step-all) differ only in what catch_up adds per skipped cycle, so those are folded in relative to
    // `last`: mcycle and the stall and WFI cycle counts grow by one per cycle in every state the core can sleep in, and the
    // stall's end cycle doesn't move.
    uint64_t digest() const {
        uint64_t h = 0;
        for (uint32_t v : x) h = digest_mix(h, v);
        const uint64_t w[] = {pc, mstatus, mie, mtvec, mscratch, mepc, mcause, mtval, mcycle - last, uint64_t(halt),
                              uint64_t(in_wfi) | uint64_t(in_handler) << 1 | uint64_t(parked) << 2 | uint64_t(ub) << 3,
                              stall ? stall + last : 0, load_wait_t, load_wait_pc, uint64_t(stop), stop_cycle,
                              instructions, cycles_stalled + cycles_wfi - last, stall_branch, stall_mul, stall_reg_load,
                              stall_request, traps, bus_errors, illegals, violations, alias_accesses, reserved_accesses,
                              trace_stores};
        for (uint64_t v : w) h = digest_mix(h, v);
        for (uint32_t i = 0; i < MEM_WORDS; i++) h = digest_mix(h, uint64_t(dmem[i]) | uint64_t(imem[i]) << 32);
        return h;
    }

    // ================================================================ traps and halts
    void trap(uint64_t t, uint32_t cause, uint32_t tval, uint32_t at, uint32_t resume) {
        if (in_handler && cause != CAUSE_EXTERNAL) { stop_loudly(t, HALT_TRAP2, at, cause); return; }
        traps++; trap_at = int64_t(t); trap_cause = int32_t(cause);
        mepc = cause == CAUSE_EXTERNAL ? resume : at; mcause = cause; mtval = tval;
        mstatus = (mstatus & ~(MSTATUS_MIE | MSTATUS_MPIE)) | ((mstatus & MSTATUS_MIE) ? uint32_t(MSTATUS_MPIE) : 0u);
        in_handler = true; pc = mtvec & ~3u; stall = 0;
        if (log) log->add(t, T_TRAP, 0, mcause, mepc, mtval, pc);
    }
    // ecall, ebreak, a fetch fault, a misaligned jump, a trap inside the handler: ref stops loudly (and counts a violation).
    // (`detail`: the checker's text, as ref's stopLoudly message: the fetch's pc, the jump's target, the trap's cause)
    void stop_loudly(uint64_t t, int why, uint32_t at, uint32_t detail = 0) {
        violations++;
        if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_HALT, uint64_t(why), detail);   // W26
        halt_with(t, why, at);
    }
    void halt_with(uint64_t t, int why, uint32_t at) {
        halt = why;
        if (log) log->add(t, T_HALT, 0, uint32_t(why), at);
    }
    template <class Env>
    void illegal(Env& e, uint64_t t, uint32_t ipc, uint32_t ins) {
        illegals++; e.post(BUS_ERROR, COND_ILLEGAL, 0);
        trap(t, CAUSE_ILLEGAL, ins, ipc, ipc);
    }
    template <class Env>
    void bus_error(Env& e, uint64_t t, uint32_t ipc, uint32_t ins, uint32_t ea) {
        bus_errors++; e.post(BUS_ERROR, COND_BUS_ERROR, 0);
        uint32_t op = ins & 0x7F;
        trap(t, op == 0x23 ? CAUSE_STORE_FAULT : op == 0x03 ? CAUSE_LOAD_FAULT : CAUSE_FETCH, ea, ipc, ipc);
    }
    void undefined(uint64_t t, bool store, uint32_t width, uint32_t ea, uint32_t ipc) {
        ub = true;
        if (log) log->add(t, T_UB, store, ea, ipc, width);
    }

    // ================================================================ fetch and execute
    template <class Env>
    UNITS_NOINLINE void fetch_execute(Env& e, uint64_t t) {
        uint32_t ipc = pc, ins;
        if ((ipc & 3) == 0 && region(ipc) == R_PMEM) { ins = imem[mem_word(ipc)]; if (first_image_at < 0) first_image_at = int64_t(t); }
        else if ((ipc & 3) == 0 && region(ipc) == R_BOOTROM && offset(ipc) >> 2 < bootrom_words) ins = bootrom[offset(ipc) >> 2];
        else { stop_loudly(t, HALT_FETCH, ipc, ipc); return; }
        execute(e, t, ins, ipc);
    }

    template <class Env>
    void execute(Env& e, uint64_t t, uint32_t ins, uint32_t ipc) {
        instructions++;
        const uint32_t op = ins & 0x7F, rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7, rs1 = (ins >> 15) & 31,
                       rs2 = (ins >> 20) & 31, f7 = ins >> 25;
        const int32_t sins = int32_t(ins);
        const uint32_t a = x[rs1], b = x[rs2];
        uint32_t next = ipc + 4, res = 0;
        bool wr = false;
        switch (op) {
        case 0x37: res = ins & 0xFFFFF000u; wr = true; break;                                   // lui
        case 0x17: res = ipc + (ins & 0xFFFFF000u); wr = true; break;                           // auipc
        case 0x6F: {                                                                            // jal
            uint32_t imm = uint32_t(sins >> 31) << 20 | (ins & 0xFF000) | ((ins >> 20) & 1) << 11 | ((ins >> 21) & 0x3FF) << 1;
            res = next; wr = true; next = ipc + imm; stall += timing.branch_penalty; stall_branch += timing.branch_penalty;
            if (next & 3) { stop_loudly(t, HALT_JUMP, ipc, next); return; }
            if (next == ipc && in_handler) parked = true;                                       // the handler's park loop
            break;
        }
        case 0x67:                                                                              // jalr
            if (f3 != 0) { illegal(e, t, ipc, ins); return; }
            res = next; wr = true; next = (a + uint32_t(sins >> 20)) & ~1u;
            stall += timing.branch_penalty; stall_branch += timing.branch_penalty;
            if (next & 3) { stop_loudly(t, HALT_JUMP, ipc, next); return; }
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
            default: illegal(e, t, ipc, ins); return;
            }
            if (take) { next = ipc + imm; stall += timing.branch_penalty; stall_branch += timing.branch_penalty; }
            break;
        }
        case 0x03: {                                                                            // loads
            uint32_t ea = a + uint32_t(sins >> 20);
            if (timing.reg_load_latency > 1 && in_regs(ea) && !(load_wait_t == t - 1 && load_wait_pc == ipc)) {
                load_wait_t = t; load_wait_pc = ipc; instructions--; stall_reg_load++; cycles_stalled++;
                return;                                       // a block or window load samples in its second cycle
            }
            uint32_t width = 1u << (f3 & 3);
            if (f3 > 5 || f3 == 3) { illegal(e, t, ipc, ins); return; }
            if (ea & (width - 1)) { undefined(t, false, width, ea, ipc); return; }
            uint32_t v;
            if (!load(e, ea, width, v)) { bus_error(e, t, ipc, ins, ea); return; }
            uint32_t sh = (ea & 3) * 8;
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
            uint32_t ea = a + (uint32_t(sins >> 25) << 5 | rd);
            if (f3 == 2 && in_regs(ea) && !in_windows(ea) && (ea & 3) == 0 && (ea - REG_BASE) >> 2 == REQUEST_SEND
                && !e.can_push()) {
                if (log) log->add(t, T_REFUSED);
                instructions--; stall_request++; cycles_stalled++;
                return;                                       // a full request FIFO: re-executed at t + 1
            }
            if (f3 == 2 && in_windows(ea) && window_present(window_of(ea))) {
                if constexpr (requires { e.window_stalls(0u, 0u); }) {
                    if (e.window_stalls(window_of(ea), (ea & 0x1FF) >> 2)) {
                        instructions--; cycles_stalled++;
                        return;                                   /* 8.14: UART_TX while the TX FIFO is full: retried */
                    }
                } else FATAL("tile core: a store to peripheral %u's window at 0x%x: this bench has no peripheral slot", window_of(ea), ea);
            }
            if (f3 > 2) { illegal(e, t, ipc, ins); return; }
            uint32_t width = 1u << f3;
            if (ea & (width - 1)) { undefined(t, true, width, ea, ipc); return; }
            uint32_t sh = (ea & 3) * 8;
            uint32_t mask = (f3 == 2 ? 0xFFFFFFFFu : f3 == 1 ? 0xFFFFu : 0xFFu) << sh;
            if (!store(e, t, ea, b << sh, mask, ipc)) { bus_error(e, t, ipc, ins, ea); return; }
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
            case 1: if (f7 != 0) { illegal(e, t, ipc, ins); return; } res = a << sh; break;
            default:
                if (f7 == 0) res = a >> sh;
                else if (f7 == 0x20) res = uint32_t(int32_t(a) >> sh);
                else { illegal(e, t, ipc, ins); return; }
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
                default: illegal(e, t, ipc, ins); return;
                }
                stall += timing.mul_latency - 1; stall_mul += timing.mul_latency - 1;
            } else if (f7 == 0 || f7 == 0x20) {
                if (f7 == 0x20 && f3 != 0 && f3 != 5) { illegal(e, t, ipc, ins); return; }
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
            } else { illegal(e, t, ipc, ins); return; }
            wr = true;
            break;
        case 0x0F: if (f3 > 1) { illegal(e, t, ipc, ins); return; } break;                      // fence: a no-op
        case 0x73:
            if (ins == 0x00000073) { stop_loudly(t, HALT_ECALL, ipc); return; }
            if (ins == 0x00100073) { stop_loudly(t, HALT_EBREAK, ipc); return; }
            if (ins == WFI) {
                if (in_handler && !interrupt_pending(e)) parked = true;
                wfi_at = int64_t(t);                          // W23: statistics (wfiLog)
                if (!e.wake()) { in_wfi = true; wfi_in_rom = region(ipc) == R_BOOTROM; }
                else { took_wake(e.reg(EVENTS) >> EV_GO & 1, int64_t(t), t, region(ipc) == R_BOOTROM); woken_in_done(e, t); }   // falls through (W26)
            } else if (ins == 0x30200073) {                   // mret: legal, flagged (every trap is terminal)
                violations++;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_MRET);   // W26
                next = mepc;
                mstatus = (mstatus & ~MSTATUS_MIE) | ((mstatus & MSTATUS_MPIE) ? uint32_t(MSTATUS_MIE) : 0u) | MSTATUS_MPIE;
            } else if (f3 != 0 && f3 != 4) {                  // the CSR instructions
                uint32_t csr = ins >> 20, old;
                if (!csr_read(e, csr, old)) { illegal(e, t, ipc, ins); return; }
                uint32_t src = f3 >= 5 ? rs1 : a;
                bool writes = (f3 & 3) == 1 || rs1 != 0;
                if (writes) {
                    uint32_t v = (f3 & 3) == 1 ? src : (f3 & 3) == 2 ? old | src : old & ~src;
                    if (!csr_write(csr, v)) { illegal(e, t, ipc, ins); return; }
                }
                res = old; wr = true;
            } else { illegal(e, t, ipc, ins); return; }
            break;
        default: illegal(e, t, ipc, ins); return;
        }
        bool w = wr && rd != 0;
        if (log) log->add(t, T_RETIRE, w, ipc, ins, a, b, res);
        if (w) {
            x[rd] = res;
            if (min_sp_watch && rd == 2 && res != 0 && int32_t(res) < min_sp) min_sp = int32_t(res);   // W24 (above)
        }
        pc = next;
    }

    // ================================================================ the CSRs
    template <class Env>
    bool csr_read(Env& e, uint32_t csr, uint32_t& v) const {
        uint64_t mt = mcycle / (timing.ref_cycle ? timing.ref_cycle : 1);
        switch (csr) {
        case MSTATUS: v = mstatus | 3u << 11; return true;
        case MISA: v = 1u << 30 | 1u << 8 | 1u << 12; return true;
        case MIE_CSR: v = mie; return true;
        case MTVEC: v = mtvec; return true;
        case MSCRATCH: v = mscratch; return true;
        case MEPC: v = mepc; return true;
        case MCAUSE: v = mcause; return true;
        case MTVAL: v = mtval; return true;
        case MIP: v = e.irq() ? uint32_t(MEIE) : 0u; return true;
        case MCYCLE: v = uint32_t(mcycle); return true;
        case MCYCLEH: v = uint32_t(mcycle >> 32); return true;
        case MTIME: v = uint32_t(mt); return true;
        case MTIMEH: v = uint32_t(mt >> 32); return true;
        default: return false;
        }
    }
    bool csr_write(uint32_t csr, uint32_t v) {
        switch (csr) {
        case MSTATUS: mstatus = v & (MSTATUS_MIE | MSTATUS_MPIE); return true;
        case MIE_CSR: mie = v & MEIE; return true;
        case MTVEC: mtvec = v & ~3u; return true;
        case MSCRATCH: mscratch = v; return true;
        case MEPC: mepc = v & ~3u; return true;
        case MCAUSE: mcause = v; return true;
        case MTVAL: mtval = v; return true;
        case MCYCLE: mcycle = (mcycle & ~0xFFFFFFFFull) | v; return true;
        case MCYCLEH: mcycle = (mcycle & 0xFFFFFFFFull) | uint64_t(v) << 32; return true;
        case MISA: case MIP: return true;                                     // read-only here: writes ignored
        default: return false;                                                // mtime and mtimeh included
        }
    }

    // ================================================================ the data side
    // A data- or program-memory word: the offset's high bits are ignored (it aliases; ref flags it).
    uint32_t mem_word(uint32_t a) {
        uint32_t off = offset(a);
        if (off >= MEM_BYTES) {
            alias_accesses++; violations++;
            if (ck_tile >= 0) sim::check::record(ck_t, ck_tile, sim::check::Kind::CTL_ALIAS, a);   // W26 (ck_t: this step's)
        }
        return (off & (MEM_BYTES - 1)) >> 2;
    }
    bool window_present(unsigned n) const { return n >= 1 && n <= 3 && (periph_present >> (n - 1) & 1); }

    // A load of `width` bytes (the aligned word holding ea); false: a bus error.
    template <class Env>
    bool load(Env& e, uint32_t ea, uint32_t width, uint32_t& v) {   // (ck_t: the load's cycle for the checker's facts)
        uint32_t a = ea & ~3u;
        if (region(a) == R_DMEM) { const uint32_t w = mem_word(a); if (cm) cm->loaded(w, ck_t); v = dmem[w]; return true; }   // (W26: cm)
        if (region(a) == R_PMEM) { v = imem[mem_word(a)]; return true; }
        if (region(a) == R_BOOTROM) {
            if (offset(a) >> 2 >= bootrom_words) return false;
            v = bootrom[offset(a) >> 2]; return true;
        }
        if (in_tables(a)) {
            if (width != 4 || a >= PTR_BASE) return false;                     // PTR, STREAM, GROUP: bus error
            if constexpr (requires { e.hist(0u); }) { v = e.hist(table_addr(a).index); return true; }
            else FATAL("tile core: a load from HIST in the table window at 0x%x: this bench has no HIST", a);
        }
        if (in_regs(a)) {
            if (width != 4) return false;
            return read_reg(e, a, v);
        }
        return false;
    }
    template <class Env>
    bool read_reg(Env& e, uint32_t a, uint32_t& v) {
        if (unsigned n = window_of(a)) {
            if (!window_present(n)) return false;                              // an absent peripheral: bus error
            if constexpr (requires { e.window_load(0u, 0u, v); }) {
                /* W15: the slot's window RegFile (PeripheralSlot.read); 2: a reserved or undefined word, read 0, flagged */
                if (e.window_load(n, (a & 0x1FF) >> 2, v) == 2) {
                    reserved_accesses++; violations++;
                    // W26: ref's PeripheralSlot.reserved (load): the slot's own check through the block (code 1, F_RESERVED)
                    if (ck_tile >= 0) sim::check::record(ck_t, ck_tile, sim::check::Kind::SLOT_VIOLATION, 1, n << 8 | ((a & 0x1FF) >> 2), 0);
                }
                return true;
            } else FATAL("tile core: a load from peripheral %u's window at 0x%x: this bench has no peripheral slot", n, a);
        }
        unsigned i = (a - REG_BASE) >> 2;
        const RegInfo& r = reg_info(i);
        // W14 (the request port's receive side, 4.18): where the Env has it. A tile's reads 0 and REQUEST_RECEIVE sets
        // receive underflow (ERRORS [14]); a controller's reads its target's queue (RequestPort.get / receive).
        if constexpr (requires { e.req_get(0u); }) {
            if (i >= REQUEST_GET_W0 && i <= REQUEST_RECEIVE) {
                if (i == REQUEST_RECEIVE) {
                    bool ok = false;
                    v = e.req_receive(ok);
                    if (!ok) e.post(BUS_ERROR, COND_RECV_UNDERFLOW, 0);
                } else {
                    v = e.req_get(i - REQUEST_GET_W0);
                }
                return true;
            }
        }
        if (r.load == A_OUT) FATAL("tile core: a load from %s (0x%x): not modelled yet", r.name, a);
        if (r.load == A_NONE) {                                                // write-only or reserved: reads 0
            if (r.store == A_NONE) {
                reserved_accesses++; violations++;
                if (ck_tile >= 0) sim::check::record(ck_t, ck_tile, sim::check::Kind::CTL_RESERVED_ACCESS, 0, 0x800 + 4 * i);   // W20
            }
            v = 0; return true;
        }
        switch (i) {
        case EVENTS: {                                                         // consumes what it returned under the mask
            v = e.reg(EVENTS);
            if constexpr (requires { e.live_events(); }) v |= e.live_events();   // W14: request pending (EVENTS [12])
            uint32_t c = v & e.reg(EVENTS_MASK) & ~EVENTS_LIVE;
            if (c) e.post(BUS_CONSUME, 0, c);
            return true;
        }
        case EVENTS_PEEK:
            v = e.reg(EVENTS);
            if constexpr (requires { e.live_events(); }) v |= e.live_events();
            return true;
        case SRAM_RD_ADDR_QUEUE:                                               // the read port's free entries at t (4.3)
            if constexpr (requires { e.rp_free(); }) { v = e.rp_free(); return true; }
            else FATAL("tile core: a load from SRAM_RD_ADDR_QUEUE at 0x%x: this bench has no read port", a);
        default: v = e.reg(i); return true;   // PARAMS, STATUS, ERRORS, the mask, required; M2: ROUTER_HDR/ADDR, DMA_*,
                                              // the counters (at the start of t), SRAM_RD_DEST_ADDR
        }
    }

    // TileControl.definedBits: a store's defined bits per register (the rest reserved; a violation, ignored).
    static uint32_t defined_bits(unsigned i) {
        switch (i) {
        case PULSE_DONE: case PULSE_DMA: return 1;
        case EVENTS_MASK: case EVENTS_REQUIRED: return EVENTS_VALID;
        case ROUTER_HDR: return 1u | 0xFFF80000u;
        case DMA_CONFIG: return (1u << 27) - 1;
        case DMA_RECORD_BASE_ADDRESS: case SRAM_RD_ADDR_QUEUE: return 0x1FFFFF;
        case SRAM_RD_DEST_ADDR: return 0x3FFFFF;
        default: return i >= WRITE_COUNTER_1 && i <= WRITE_COUNTER_7 ? 0xFFFFFu : ~0u;
        }
    }
    // A ROUTER_SEND_* store (8.4): the packet from the registers as they stand at t (the block's view: ROUTER_HDR's
    // counter, mode and dst, ROUTER_ADDR, W0-W2 below the store's top word), put on the send holding register at t.
    template <class Env>
    void send_store(Env& e, uint64_t t, uint32_t size, uint32_t v) {
        if constexpr (requires { e.send(0ull, 0ull, 0ull); }) {
            uint32_t h = e.reg(ROUTER_HDR), w0 = e.reg(ROUTER_SET_W0), w1 = e.reg(ROUTER_SET_W1), w2 = e.reg(ROUTER_SET_W2);
            send_w[0] = size == lw::SIZE_32 ? uint64_t(v) : size == lw::SIZE_64 ? uint64_t(v) << 32 | w0 : uint64_t(w1) << 32 | w0;
            send_w[1] = lw::hdr(0, h >> 25 & 0x7F, h >> 22 & 7, h >> 19 & 7, e.reg(ROUTER_ADDR), size);
            send_w[2] = size == lw::SIZE_128 ? uint64_t(v) << 32 | w2 : 0;
            e.send(send_w[0], send_w[1], send_w[2]);
            pending_t = t; sends++;
        } else FATAL("tile core: a send store at cycle %llu: this bench has no send port", (unsigned long long)t);
    }

    // A store at t (value and mask shifted into place); false: a bus error.
    template <class Env>
    bool store(Env& e, uint64_t t, uint32_t ea, uint32_t v, uint32_t mask, uint32_t ipc) {
        uint32_t a = ea & ~3u;
        if (region(a) == R_DMEM) {
            uint32_t w = mem_word(a);
            if (cm) cm->stored(w, t);                                 // W26: invariant 8's read tracking
            if (offset(a) < DMEM_FIRST) {
                violations++;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_DMEM_RESERVED, ea);   // W26
            }
            dmem[w] = (dmem[w] & ~mask) | (v & mask);
            return true;
        }
        if (region(a) == R_PMEM || region(a) == R_BOOTROM) return false;
        if (in_tables(a)) {
            if (mask != 0xFFFFFFFFu) return false;
            if constexpr (requires { e.table_store(0u, 0u, 0u); }) {
                TableAddr ta = table_addr(a);
                e.table_store(ta.table, ta.index, v & table_mask(ta.table));
                return true;
            } else FATAL("tile core: a store to the table window at 0x%x: this bench has no table store", a);
        }
        if (in_regs(a)) {
            if (mask != 0xFFFFFFFFu) return false;
            if (unsigned n = window_of(a)) {
                if (!window_present(n)) return false;
                if constexpr (requires { e.window_store(uint64_t(0), 0u, 0u, 0u); }) {
                    return e.window_store(t, n, (a & 0x1FF) >> 2, v);           /* W15: false: a bus error */
                } else FATAL("tile core: a store to peripheral %u's window at 0x%x: this bench has no peripheral slot", n, a);
            }
            unsigned i = (a - REG_BASE) >> 2;
            const RegInfo& r = reg_info(i);
            if (r.store == A_OUT) FATAL("tile core: a store to %s (0x%x): not modelled yet", r.name, a);
            if (r.store == A_NONE) {
                if (r.load != A_NONE) return false;                            // a read-only register: bus error
                reserved_accesses++; violations++;                             // reserved: ignored
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_RESERVED_ACCESS, 1, 0x800 + 4 * i);   // W20
                return true;
            }
            if (v & ~defined_bits(i)) {                                        // reserved bits set: ignored (invariant 21)
                violations++;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_RESERVED_BITS, i, v & ~defined_bits(i));
            }
            if (ck_tile >= 0 && i == DMA_RECORD_BASE_ADDRESS && (v & 1))       // W20: TileControl.write's checks (facts only)
                sim::check::record(t, ck_tile, sim::check::Kind::CTL_RECORD_BASE_ODD, v);
            if (ck_tile >= 0 && i == SRAM_RD_DEST_ADDR && (v >> 17 & 3) == lw::SIZE_UNDEF)
                sim::check::record(t, ck_tile, sim::check::Kind::CTL_RD_DEST_SIZE);
            if (i == TRACE) trace_stores++;
            if (i == SRAM_RD_ADDR_QUEUE) {                                     // the read port (4.3; Controller.java:380)
                if constexpr (requires { e.read_port(uint64_t(0), 0u); }) {
                    const bool queued = e.read_port(t, v & 0x1FFFFF);
                    e.post(BUS_STORE, i, (v & 0x1FFFFF) | (queued ? 0u : 1u << 31));   // bit 31: dropped (ERRORS [7])
                } else FATAL("tile core: a store to SRAM_RD_ADDR_QUEUE at 0x%x: this bench has no read port", a);
                return true;
            }
            e.post(BUS_STORE, i, v);
            if (i >= ROUTER_SEND_32 && i <= ROUTER_SEND_128) send_store(e, t, i - ROUTER_SEND_32, v);
            if (i == SIM_EXIT) halt_with(t, HALT_EXIT, ipc);
            return true;
        }
        return false;
    }
};

}  // namespace units
