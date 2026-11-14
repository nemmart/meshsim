// rv32.h: the tile's core, the RTL's `rv32_core` (spec/core.md §1; Snarky One's rtl/blocks/core/src) cycle for cycle:
// RV32I + C + Zmmul + Zicsr + Zicond in machine mode, a four-stage pipeline (fetch, decode, execute, writeback) whose
// fetch runs ahead of it. `Rv32::step(env, t)` is one clock cycle: the RTL's combinational logic on the registers as
// cycle t finds them and the inputs of t, then its registers' update at the edge that ends t. The sections below are
// the RTL's modules, under its names: fetch.veryl (the halfword queue), rv32_pipeline.veryl (the stages, the
// forwarding interlocks, the redirects, WFI), lsu.veryl (one outstanding access), csr.veryl, and rv32_core.veryl
// (the windows and core_status). rv32_decode.h is decompress.veryl and decode.veryl.
//
// What the model leaves out, because the tile never does it: a port that holds ready low (IMEM, DMEM, the boot ROM and
// the register bus accept in every cycle, spec/core.md "Obligations of the enclosing unit"), an instruction port that
// answers later than the next cycle, and the coprocessor port (the tile has no accelerator: custom-0 and custom-1 are
// illegal instructions).
//
// ---- The core's ports
// Instruction port: a fetch raised in cycle t reads its words in t (as the edge that ends t finds the memories) and is
// answered in t + 1. The words come from `imem` and `bootrom` here, or from `env.bus_fetch`.
//
// Data port: a load or store is raised in the cycle q its instruction leaves execute; it leaves writeback in the cycle
// of its answer. The target gives two things (`route`): the cycle the access is made in, q + m, when a load reads the
// environment as that cycle shows it and a store, or a load's side effect, is posted (spec/machine-spec.md 6.4: the
// sample point is the end of q + m - 1, the effect the end of q + m); and whether the answer comes in that cycle or
// the next. As the RTL's memories and register_bus have them (`Rv32Timing` holds the latencies L):
//   DMEM, the table window (L = 1)          made in q, answered in q + 1
//   an address in no window                 answered in q + 1 with an error, and not made
//   a register-window load (L = 2)          made in q + L - 1, answered in q + L
//   a register-window store                 made in q + L - 1 and answered in that cycle
//   a register-window store that may stall  made in the first cycle from q + L - 1 on in which its owner takes it,
//                                           and answered in the next (`may_stall`)
//
// Interrupt inputs, read in every cycle: `meip` (the wake; `mip`.MEIP) and `nmi`.
//
// Outputs, each from a register, as the RTL's: `in_wfi` (wfi_sleeping), `core_status_canonical_wait`,
// `core_status_access_fault`, `core_status_illegal_instruction`. After step(t) they hold the values of cycle t + 1.
//
// ---- The environment
// Everything the core sees of its tile comes through `env` (one class per use: the unit's adapter over the framework's
// Ctx, a plain struct in tests). Two kinds:
//
// A bus (tests/core_programs.h: the RTL's test memory and devices), which gives the whole map:
//   uint32_t bus_fetch(a, err)       the instruction word at a; err: a is in no fetch window
//   uint32_t bus_route(a)            a data access's latency at a (made the cycle before its answer), 0: in no data window
//   bool bus_access(t, a, store, mask, wdata, rdata)   the access, made in cycle t; false: answered with an error
//   bool meip(), bool nmi()          the interrupt inputs at t
//
// The tile (the map of regmap.h, the register block and the units behind it), v1.35's interface:
//   uint32_t reg(unsigned r)    the register block's view at t (the state at the end of t - 1)
//   bool wake()                 the wake term at t: MEIP
//   bool irq()                  ERRORS != 0 at t: the NMI (`nmi()`, where the environment has it, is read instead)
//   bool store_may_stall(a)     optional: a store to register-window address a may stall (the header's table)
//   bool can_push()             a REQUEST_SEND store may complete at t (RequestPort.canPush)
//   void post(op, arg, v)       what the block applies at the end of t (one a cycle): BUS_STORE (register, value),
//                               BUS_CONSUME (-, the EVENTS bits returned), BUS_ERROR (condition, -)
// M2, each optional (an environment without it, M1's benches, stops with a FATAL naming what it lacks):
//   void send(lo, hdr, hi)      a ROUTER_SEND_* store's packet on the send holding register (the injection unit)
//   bool send_refused()         the send put of t - 1 was refused: the core puts the same packet again and stalls
//   void table_store(table, i, v)  a table-window store (table_store.h), applied before t + 1's readers
//   uint32_t hist(i)            HIST[i] for a one-cycle table-window load
// M3, the peripheral slot (AUDIT-M3 3.1), each optional (without them a present window is FATAL):
//   int window_load(n, w, v)    a load of peripheral n's window word w at t (the slot's RegFile; a consuming EVENTS
//                               load also puts its consume): 1 read, 2 a reserved or undefined word (reads 0, flagged)
//   bool window_stalls(n, w)    a store to w must retry at t (S8)
//   bool window_store(t, n, w, v)  a store of t to the slot (S2); false: a bus error (a standard read-only word, or a
//                               defined read-only word of the peripheral's)
// What isn't modelled yet stops with FATAL("... not modelled yet"), naming the register or window.
//
// ---- v135_platform
// One option, for the tile's register block, C&C and firmware as v1.35 left them, which the platform stage deletes
// with them: every use is marked `v135_platform`. Set (the default), `step` runs v1.35's core whole, its function and
// its one-instruction-a-cycle timing (rv32_v135.h), and the data side keeps what v1.35's programs rely on (loads from
// program memory and the boot ROM, a memory's aliases). v1.35's protocols and references hold that core's cycles (a
// command lands a fixed time after the last; the goldens replay their events at ref's cycles), so its platform does
// not run on the RTL's core. Clear, the core is the RTL's, on either environment.
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/fatal.h"
#include "check.h"
#include "units/linkword.h"
#include "units/regmap.h"
#include "units/rv32_decode.h"
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

// Controller.checkBootRom (v1.25, DECISIONS-v1.25 s8d): the boot ROM is 256 bytes, 64 words; a larger image
// is refused (FATAL, at configuration: ref's IllegalStateException), naming where it came from.
inline void check_bootrom(size_t words, const char* from) {
    if (words > BOOTROM_MAX_WORDS)
        FATAL("the boot ROM image %s is %zu bytes; the ROM is %u (spec 6.2, DECISIONS-v1.25 s8d)", from, 4 * words, 4u * BOOTROM_MAX_WORDS);
}

// The tile's bus as the core's data port sees it (the header's table). The pipeline's own costs are not parameters:
// they follow from its structure.
struct Rv32Timing {
    uint32_t reg_load_latency = 2;     // L of the register block and the windows (WINDOW_LOAD_LATENCY)
    uint32_t table_load_latency = 1;   // L of the table window (TABLE_LOAD_LATENCY)
    uint32_t branch_penalty = 1, mul_latency = 3;   // (v135_platform) v1.35's core's stall cycles
    uint32_t ref_cycle = 40;           // (v135_platform) core cycles per `mtime` tick
};

// How a core's run ended (TRACE-FORMAT's END): it runs until one of these, then nothing (TraceRun's chip stops there).
enum class Stop : uint8_t { running, halt, parked, ub };
// (invariant 8, ControllerMemories' read tracking; a statistic in every build, out of the digests): the data words
// holding a reply or a read-port result the core hasn't loaded or stored yet, and command slot 0 not yet loaded. The
// port-B side (marking) is the tile core's (tile_ctl.h, TileCore::track_port_b); the core's side is `loaded`/`stored`
// (ref's cm.loaded, cm.stored), which also remember the last few accesses with their cycle: a port-B write reaches the
// core a cycle after ref's memory has it, so a word the core touched in that cycle ends up read, as in ref.
struct CmemTrack {
    static constexpr uint32_t CMD_DWORD = 0x7FC0 >> 2;
    uint64_t unread[MEM_WORDS / 64] = {};
    bool slot0_unread = false;
    uint64_t overwritten_unread = 0, unread_commands = 0;   // ref's cmem.overwrittenUnread, unreadCommands
    uint64_t writes_cmd = 0; int64_t landed_at = -1;        // ref's cmem.writesCmd, landedAt (statistics)
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
        MSTATUS = 0x300, MISA = 0x301, MIE_CSR = 0x304, MTVEC = 0x305, MSTATUSH = 0x310, MCOUNTINHIBIT = 0x320,
        MSCRATCH = 0x340, MEPC = 0x341, MCAUSE = 0x342, MTVAL = 0x343, MIP = 0x344, MCYCLE = 0xB00, MINSTRET = 0xB02,
        MCYCLEH = 0xB80, MINSTRETH = 0xB82, MTIME = 0xC01, MTIMEH = 0xC81, MVENDORID = 0xF11, MARCHID = 0xF12,
        MIMPID = 0xF13, MHARTID = 0xF14, MCONFIGPTR = 0xF15,
        MISA_VALUE = 0x40000104,                             // RV32, I and C (csr.veryl)
        MSTATUS_MIE = 1u << 3, MSTATUS_MPIE = 1u << 7, MEIE = 1u << 11,
        CAUSE_FETCH = 1, CAUSE_ILLEGAL = 2, CAUSE_BREAKPOINT = 3, CAUSE_LOAD_MISALIGNED = 4, CAUSE_LOAD_FAULT = 5,
        CAUSE_STORE_MISALIGNED = 6, CAUSE_STORE_FAULT = 7, CAUSE_ECALL = 11,
        CAUSE_EXTERNAL = 0x8000000B, CAUSE_NMI = 0x80000000,
        BOOTROM_BYTES = 4 * BOOTROM_MAX_WORDS,               // the boot ROM's window (BOOT_ROM_BYTES)
        QUEUE_CAPACITY = 6,                                  // fetch's queue, in halfwords (fetch.veryl)
    };
    static constexpr uint64_t kNever = ~uint64_t(0);

    // ---- architectural state (csr.veryl's reset; x1-x31 are not reset in the RTL, and are 0 here) ----
    uint32_t x[32] = {};
    uint32_t pc = BOOTROM_BASE;            // the reset PC until the first step; then the next instruction to retire's
    uint32_t mstatus = 0, mie = 0, mtvec = 0, mscratch = 0, mepc = 0, mcause = 0, mtval = 0;
    uint64_t mcycle = 0, minstret = 0;
    uint32_t hart_id = 0;                  // mhartid
    int halt = RUN;
    bool in_wfi = false;                   // wfi_sleeping: the core waits after a WFI
    bool in_handler = false;               // csr.veryl's `trapped`: a trap was taken since reset (no NMI can be taken)
    bool parked = false, ub = false;
    // ---- the outputs from registers (rv32_core.veryl's core_status), with in_wfi ----
    bool core_status_canonical_wait = false, core_status_access_fault = false, core_status_illegal_instruction = false;
    // ---- fetch (fetch.veryl): the halfword queue, and the response of the fetch raised in the cycle before ----
    struct Fetch {
        uint16_t q[QUEUE_CAPACITY] = {};
        bool qe[QUEUE_CAPACITY] = {};      // the halfword's word failed
        uint8_t n = 0;                     // queue_count
        uint32_t q_pc = 0, next = 0;       // queue_pc, next_fetch_address
        bool skip = false;                 // skip_low_halfword: a redirect to an odd halfword's response is due
        bool starting = true;              // starting_after_reset
        bool resp = false;                 // a response arrives in this cycle
        uint32_t rw[2] = {};               // its words, and their errors
        bool re[2] = {};
    } f;
    // ---- decode, execute and writeback (rv32_pipeline.veryl, section 1) ----
    bool d_valid = false, d_ferr = false;
    uint32_t d_pc = 0, d_raw = 0;
    bool x_valid = false, x_exc = false, x_redirected = false, x_p1 = false, x_p2 = false;
    uint32_t x_pc = 0, x_raw = 0, x_cause = 0;
    rv32d::Decoded x_dec;
    bool w_valid = false, w_writes = false, w_ready = false, w_exc = false, w_csr_writes = false;
    rv32d::Class w_cls = rv32d::ILLEGAL;
    uint8_t w_rd = 0;
    uint32_t w_pc = 0, w_raw = 0, w_result = 0, w_cause = 0;
    uint32_t w_mul = 0;                    // a multiply's product (the RTL finishes it in writeback)
    uint32_t w_next_pc = 0, w_a = 0, w_b = 0;   // the next instruction's address; rs1 and rs2 as read (the trace)
    bool redir_pending = false, redir_cancels = false;
    uint32_t redir_target = 0;
    // ---- the load/store unit (lsu.veryl): the one outstanding access ----
    bool lsu_out = false, lsu_store = false, lsu_made = false, lsu_err = false;
    uint32_t lsu_addr = 0, lsu_size = 0, lsu_wdata = 0, lsu_rdata = 0;
    uint64_t lsu_make_at = 0, lsu_resp_at = 0;   // the cycle the access is made in (the first it may be), and its answer's
    uint8_t lsu_answer = 1;                      // the answer comes this many cycles after the access is made (0 or 1)
    // ---- v1.35's send port ----
    uint64_t pending_t = kNever; uint64_t send_w[3] = {};   // M2: a send store's packet awaiting acceptance (ref's pendingT)
    int ck_tile = -1; uint64_t ck_t = 0;   // the checker's facts (src/check.h) under this tile, -1 none; the step's cycle
    CmemTrack* cm = nullptr;               // invariant 8's read tracking (the tile core's; null: none)
    // (diagnostic only; owner's Q3, 2026-11-11): the lowest nonzero stack pointer, SortTest's "stack headroom"
    // (ref's monitor reads every core's x[2] after every cycle; here taken at the writes to x2, the only place it
    // changes, so idle-skipped cycles need nothing). Off by default: the harness arms it (min_sp_watch) with the current
    // x2. Off, it costs one predictable branch on a retire that writes a register; nothing else reads it.
    bool min_sp_watch = false; int32_t min_sp = INT32_MAX;
    // ---- configuration, and permanent state allocated apart ----
    Rv32Timing timing;
    bool v135_platform = true;             // the header: the core for v1.35's register block and firmware
    uint32_t reset_trap_vector = 0;        // mtvec's reset value (RESET_TRAP_VECTOR): what reset() sets
    uint32_t periph_present = 1;           // PARAMS [2:0]: which windows are present (present: not modelled yet)
    uint32_t* dmem = nullptr;              // MEM_WORDS each
    uint32_t* imem = nullptr;
    const uint32_t* bootrom = nullptr; uint32_t bootrom_words = 0;
    TraceLog* log = nullptr;               // null: no trace
    // ---- the run ----
    uint64_t last = 0;                     // the last cycle stepped (cycle 1 is the first)
    Stop stop = Stop::running; uint64_t stop_cycle = 0;
    // ---- statistics and ref's violations (counted, never a stop: TileControl.violate) ----
    // Every cycle the core runs is one of: an instruction retiring (instructions), a cycle asleep after a WFI
    // (cycles_wfi), or anything else (cycles_stalled: the pipeline's own cycles, a wait for an answer, a trap's entry).
    // stall_reg_load: the cycles of a register-block, window or table access beyond its first (L - 1 each).
    uint64_t instructions = 0, cycles_stalled = 0, stall_reg_load = 0, stall_request = 0, cycles_wfi = 0, traps = 0, bus_errors = 0,
             illegals = 0, violations = 0, alias_accesses = 0, reserved_accesses = 0, trace_stores = 0, stall_send = 0,
             sends = 0;
    // (P5; statistics the control suites read, ref's Controller fields; not in the digest, not in the trace):
    // the last trap's cycle and cause (trapAt, trapCause: -1 before any), the first instruction retired from program
    // memory since the reset (firstImageAt: the image running; -1 before). A reset sets them back to -1, as ref's reset.
    int64_t trap_at = -1, first_image_at = -1;
    int32_t trap_cause = -1;
    // the WFI wakes (TileControl.takeWake's goWakes, eventWakes; ref counts them in the block, here where the
    // core takes the wake) and the program's WFIs (Controller.wfiLog: {entered, woke, cause 1 GO / 2 event} per WFI
    // outside the boot ROM; the first kWfiLog kept, wfi_n counts all, D24: a fixed array). Statistics only.
    static constexpr unsigned kWfiLog = 16;
    int64_t wfi_log[kWfiLog][3] = {};
    uint32_t wfi_n = 0;
    uint64_t go_wakes = 0, event_wakes = 0;
    int64_t wfi_at = 0; bool wfi_in_rom = false;
    // TileControl.takeWake's check: a wake by an event other than GO while done is set, the done gate off (Config.
    // doneGate; with the gate only GO wakes): the fact only (ref's ctl.violate)
    template <class Env>
    void woken_in_done(Env& e, uint64_t t) {
        if constexpr (requires { e.done_gate(); })
            if (ck_tile >= 0 && !e.done_gate() && (e.reg(STATUS) >> ST_DONE & 1) && !(e.reg(EVENTS) >> EV_GO & 1))
                sim::check::record(t, ck_tile, sim::check::Kind::CTL_WFI_WOKEN);
    }
    // A WFI's wait ends at t (or the WFI of t does not wait): the statistics, and the trace's WAKE line for a wait.
    template <class Env>
    void took_wake(Env& e, uint64_t t, bool slept) {
        bool go = false;
        if constexpr (requires { e.reg(0u); }) go = e.reg(EVENTS) >> EV_GO & 1;
        if (go) go_wakes++; else event_wakes++;
        woken_in_done(e, t);
        if (slept && log) log->add(t, T_WAKE, go ? 1 : 2);
        if (wfi_in_rom) return;
        if (wfi_n < kWfiLog) { wfi_log[wfi_n][0] = wfi_at; wfi_log[wfi_n][1] = int64_t(t); wfi_log[wfi_n][2] = go ? 1 : 2; }
        wfi_n++;
    }

    // The reset: every stage empty, the CSRs at their reset values, fetch starting at `reset_pc` in the next cycle
    // stepped (t + 1). The memories and x1-x31 keep their contents.
    void reset(uint64_t t, uint32_t reset_pc) {
        pc = reset_pc;
        mstatus = mie = mscratch = mepc = mcause = mtval = 0; mtvec = reset_trap_vector;
        mcycle = minstret = 0;
        halt = RUN; in_wfi = in_handler = parked = ub = false;
        core_status_canonical_wait = core_status_access_fault = core_status_illegal_instruction = false;
        f = Fetch{};
        d_valid = x_valid = w_valid = redir_pending = lsu_out = false;
        pending_t = kNever; stall = 0; load_wait_t = kNever;                  // (v135_platform)
        stop = Stop::running; stop_cycle = 0; last = t;
        trap_at = first_image_at = -1; trap_cause = -1;
    }

#include "units/rv32_v135.h"   // (v135_platform) v1.35's core: v135_step, v135_digest and their state

    // ================================================================ the cycle
    // One clock cycle, t, after catching up the cycles slept since `last`. Returns the next cycle the core needs to
    // run: t + 1, or kNever (only if its inputs change) once it is in the canonical wait, where every cycle is the
    // same until the wake, or its run has ended. A step at a cycle it didn't ask for is that cycle, so a core stepped
    // every cycle and one stepped only when it asks end in the same state.
    template <class Env>
    UNITS_NOINLINE uint64_t step(Env& e, uint64_t t) {
        if constexpr (requires { e.irq(); }) { if (v135_platform) return v135_step(e, t); }
        else if (v135_platform) FATAL("the core: v135_platform is the tile's (a bus runs the RTL's core)");
        ck_t = t;                                                              // the cycle of this step's facts
        if (stop != Stop::running && stop != Stop::parked) return kNever;   // the run ended: nothing more (mcycle frozen)
        catch_up(t);
        if constexpr (requires { e.send_refused(); }) {                 // (v1.35's send port) a refused send: the core
            if (pending_t == t - 1 && e.send_refused()) {               // puts the packet again, and nothing moves in t
                e.send(send_w[0], send_w[1], send_w[2]);
                pending_t = t; stall_send++; cycles_stalled++; mcycle++;
                if (lsu_out && lsu_made) lsu_resp_at++;
                return t + 1;
            }
        }
        const bool asleep = cycle(e, t);
        if (halt != RUN || ub) { stop = halt != RUN ? Stop::halt : Stop::ub; stop_cycle = t; return kNever; }
        // parked (the handler's park loop): the run's end as recorded (stop, stop_cycle), but the core goes on running
        // its loop every cycle, as the hardware does
        if (parked && stop == Stop::running) { stop = Stop::parked; stop_cycle = t; }
        return asleep ? kNever : t + 1;
    }

    // The cycles last + 1 .. t - 1 were skipped: each was a cycle of the canonical wait (the core asked for nothing
    // earlier, and nothing it reads changed, or it would have been stepped).
    void catch_up(uint64_t t) {
        const uint64_t gap = t - last - 1;
        last = t;
        if (gap == 0) return;
        DCHECK(in_wfi, "core skipped %llu cycles outside a WFI's wait", (unsigned long long)gap);
        mcycle += gap; cycles_wfi += gap;
    }

    // The interrupt inputs at t: MEIP, the tile's wake, and the NMI, the tile's errors.
    template <class Env>
    bool line_meip(Env& e) const {
        if constexpr (requires { e.meip(); }) return e.meip();
        else return e.wake();
    }
    template <class Env>
    bool line_nmi(Env& e) const {
        if constexpr (requires { e.nmi(); }) return e.nmi();
        else return e.irq();
    }

    // The core's digest: its state and its memories, not their addresses. A core that slept (ready-driven) and one
    // stepped every cycle (step-all) differ only in what catch_up adds per skipped cycle, so those are folded in
    // relative to `last`: mcycle and the WFI cycle count grow by one per cycle of the canonical wait.
    uint64_t digest() const {
        if (v135_platform) return v135_digest();
        uint64_t h = 0;
        for (uint32_t v : x) h = digest_mix(h, v);
        const uint64_t w[] = {
            pc, mstatus, mie, mtvec, mscratch, mepc, mcause, mtval, mcycle - last, minstret, uint64_t(halt),
            uint64_t(in_wfi) | uint64_t(in_handler) << 1 | uint64_t(parked) << 2 | uint64_t(ub) << 3 |
                uint64_t(core_status_canonical_wait) << 4 | uint64_t(core_status_access_fault) << 5 |
                uint64_t(core_status_illegal_instruction) << 6,
            uint64_t(stop), stop_cycle, instructions, cycles_stalled, cycles_wfi - last, stall_reg_load, stall_request, traps, bus_errors,
            illegals, violations, alias_accesses, reserved_accesses, trace_stores,
            // fetch
            f.n, f.q_pc, f.next, uint64_t(f.skip) | uint64_t(f.starting) << 1 | uint64_t(f.resp) << 2,
            f.resp ? uint64_t(f.rw[0]) | uint64_t(f.rw[1]) << 32 : 0, f.resp ? uint64_t(f.re[0]) | uint64_t(f.re[1]) << 1 : 0,
            // decode, execute, writeback (a stage's contents count only while it is valid)
            uint64_t(d_valid) | uint64_t(x_valid) << 1 | uint64_t(w_valid) << 2 | uint64_t(redir_pending) << 3 |
                uint64_t(lsu_out) << 4,
            d_valid ? uint64_t(d_pc) | uint64_t(d_raw) << 32 : 0, d_valid ? uint64_t(d_ferr) : 0,
            x_valid ? uint64_t(x_pc) | uint64_t(x_raw) << 32 : 0,
            x_valid ? uint64_t(x_exc) | uint64_t(x_redirected) << 1 | uint64_t(x_p1) << 2 | uint64_t(x_p2) << 3 |
                          uint64_t(x_cause) << 8 : 0,
            w_valid ? uint64_t(w_pc) | uint64_t(w_raw) << 32 : 0,
            w_valid ? uint64_t(w_result) | uint64_t(w_mul) << 32 : 0,
            w_valid ? uint64_t(w_exc) | uint64_t(w_ready) << 1 | uint64_t(w_cause) << 8 | uint64_t(w_next_pc) << 32 : 0,
            redir_pending ? uint64_t(redir_cancels) | uint64_t(redir_target) << 32 : 0,
            lsu_out ? uint64_t(lsu_addr) | uint64_t(lsu_wdata) << 32 : 0,
            lsu_out ? uint64_t(lsu_store) | uint64_t(lsu_made) << 1 | uint64_t(lsu_err) << 2 | uint64_t(lsu_size) << 8 |
                          uint64_t(lsu_rdata) << 32 : 0,
            lsu_out ? lsu_make_at << 1 | lsu_answer : 0, lsu_out && lsu_made ? lsu_resp_at : 0,
        };
        for (uint64_t v : w) h = digest_mix(h, v);
        for (unsigned i = 0; i < f.n; i++) h = digest_mix(h, uint64_t(f.q[i]) | uint64_t(f.qe[i]) << 16);
        for (uint32_t i = 0; i < MEM_WORDS; i++) h = digest_mix(h, uint64_t(dmem[i]) | uint64_t(imem[i]) << 32);
        return h;
    }

    // A halt: the run ends at t (SIM_EXIT's store; v1.35's core has others).
    void halt_with(uint64_t t, int why, uint32_t at) {
        halt = why;
        if (log) log->add(t, T_HALT, 0, uint32_t(why), at);
    }

    // ================================================================ rv32_pipeline.veryl, fetch.veryl, lsu.veryl
    // The RTL's cycle. Returns whether t was a cycle of the canonical wait with no wake: every cycle after it is the
    // same until an input changes.
    template <class Env>
    UNITS_NOINLINE bool cycle(Env& e, uint64_t t) {
        using namespace rv32d;
        // ---- core_status (rv32_core.veryl): the pulses of a trap taken in t - 1 are the tile's ERRORS bits
        if constexpr (requires { e.post(0u, 0u, 0u); }) {
            if (core_status_access_fault) e.post(BUS_ERROR, COND_BUS_ERROR, 0);
            if (core_status_illegal_instruction) e.post(BUS_ERROR, COND_ILLEGAL, 0);
        }
        // ---- the data port: an outstanding access made in a later cycle than it was raised in (a register window's)
        if (lsu_out && !lsu_made && lsu_make_at <= t) make_access_at(e, t);

        // ---- csr.veryl: the interrupts, from the registers and the inputs of t
        const bool meip = line_meip(e);
        const bool nmi_ready = line_nmi(e) && !in_handler;
        const bool meip_ready = meip && (mie & MEIE);
        const bool interrupt_pending = nmi_ready || ((mstatus & MSTATUS_MIE) && meip_ready);
        const uint32_t interrupt_cause = nmi_ready ? uint32_t(CAUSE_NMI) : uint32_t(CAUSE_EXTERNAL);
        const bool wfi_wake_up = nmi_ready || meip_ready;
        const uint32_t trap_vector = mtvec & ~3u, return_pc = mepc & ~1u;

        // ---- section 7: writeback: completion, exceptions and commit
        const bool lsu_done = lsu_out && lsu_made && lsu_resp_at == t;
        const bool w_mem = w_cls == LOAD || w_cls == STORE;
        const bool w_finishes = !w_mem || lsu_done;
        bool w_traps = false;
        uint32_t w_trap_cause = 0, csr_old = 0;
        if (w_valid) {
            if (w_exc) { w_traps = true; w_trap_cause = w_cause; }
            else if (w_cls == ECALL) { w_traps = true; w_trap_cause = CAUSE_ECALL; }
            else if (w_cls == EBREAK) { w_traps = true; w_trap_cause = CAUSE_BREAKPOINT; }
            else if (w_cls == CSR && (!csr_read(w_raw >> 20, meip, csr_old) || (w_csr_writes && (w_raw >> 30) == 3))) {
                w_traps = true; w_trap_cause = CAUSE_ILLEGAL;
            } else if (w_mem && lsu_done && lsu_err) {
                w_traps = true; w_trap_cause = w_cls == LOAD ? uint32_t(CAUSE_LOAD_FAULT) : uint32_t(CAUSE_STORE_FAULT);
            }
        }
        const bool w_commits = w_valid && !w_traps && w_finishes;
        const bool w_returns = w_commits && w_cls == MRET;
        const bool commit_writes = w_commits && w_writes;
        const uint32_t commit_value = w_cls == LOAD ? load_value() : w_cls == MUL ? w_mul : w_cls == CSR ? csr_old : w_result;

        // ---- section 8: interrupts and WFI
        const bool x_live = x_valid && !(redir_pending && redir_cancels);
        const bool interrupt_holds_execute = interrupt_pending && x_live;
        const bool interrupt_taken = interrupt_holds_execute && !w_valid;
        const bool trap_taken = w_traps || interrupt_taken;
        const bool wfi_retires = w_commits && w_cls == WFI;
        const bool wfi_holds_decode = (x_valid && x_dec.cls == WFI) || (w_valid && w_cls == WFI) || in_wfi;

        // ---- sections 4, 5 and 9: execute
        const bool w_can_accept = !w_valid || (w_commits && !w_returns && !w_csr_writes);
        const bool flush = trap_taken || w_returns;
        const bool x_runs = x_live && !x_exc;
        const bool x_waits = (x_p1 || x_p2) && (!w_ready || x_dec.cls == MUL);
        const bool x_accesses = x_runs && (x_dec.cls == LOAD || x_dec.cls == STORE);
        const bool lsu_can_start = !lsu_out || lsu_done;
        const bool x_moves = x_live && !interrupt_holds_execute && w_can_accept
            && (x_exc || (!x_waits && (!x_accesses || lsu_can_start)));
        // What the instruction leaving execute does there (the ALU, a branch's decision, the addresses).
        const bool x_compressed = (x_raw & 3) != 3;
        const uint32_t x_next_pc = x_pc + (x_compressed ? 2 : 4);
        uint32_t x_a = 0, x_b = 0, x_result = 0, x_target = 0, x_product = 0;
        bool x_transfers = false, x_misaligned = false, x_result_ready = false;
        if (x_moves && x_runs) {
            const Decoded& d = x_dec;
            x_a = x_p1 ? w_result : x[d.rs1];                // forwarded from writeback (it is ready: no wait), or the
            x_b = x_p2 ? w_result : x[d.rs2];                // register file's
            const uint32_t oa = d.a == A_PC ? x_pc : d.a == A_ZERO ? 0 : x_a, ob = d.b_imm ? d.imm : x_b;
            switch (d.cls) {
            case ALU: case CSR: x_result = alu(d.alu, oa, ob); x_result_ready = d.cls == ALU; break;
            case JAL: x_result = x_next_pc; x_result_ready = true; x_transfers = true; x_target = x_pc + d.imm; break;
            case JALR: x_result = x_next_pc; x_result_ready = true; x_transfers = true; x_target = (x_a + d.imm) & ~1u; break;
            case BRANCH: x_transfers = branch_holds(d.f3, x_a, x_b); x_target = x_pc + d.imm; break;
            case LOAD: case STORE:
                x_result = x_a + d.imm;
                x_misaligned = x_result & ((1u << (d.f3 & 3)) - 1);
                break;
            case MUL: x_product = multiply(d.f3, x_a, x_b); break;
            default: break;
            }
        }
        const bool branch_redirect = x_moves && x_transfers && !x_redirected;

        // ---- section 9: redirects, and decode's instruction
        const bool redirect_valid = trap_taken || w_returns || redir_pending;
        const uint32_t redirect_pc = trap_taken ? trap_vector : w_returns ? return_pc : redir_target;
        const bool d_can_move = !wfi_holds_decode && (!x_live || x_moves);
        const bool d_moves = d_valid && d_can_move && !flush && !redir_pending;
        const bool d_compressed = (d_raw & 3) != 3;
        const Decoded dec = decode(d_compressed ? expand(d_raw & 0xFFFF) : d_raw);
        const bool d_exc = d_ferr || dec.cls == ILLEGAL;
        // A JAL, or a return (a JALR through x1) whose x1 is ready, resolved in decode.
        const bool x_writes_destination = x_dec.writes && !x_exc;
        const bool return_address_ready = !(x_valid && x_writes_destination && x_dec.rd == 1)
            && !(w_valid && w_writes && w_rd == 1);
        const bool d_jumps_early = !d_exc && (dec.cls == JAL || (dec.cls == JALR && dec.rs1 == 1 && return_address_ready));
        const uint32_t d_jump_target = (dec.cls == JAL ? d_pc + dec.imm : x[1] + dec.imm) & ~1u;
        const bool early_redirect = d_moves && d_jumps_early;
        const bool fetched_ready = !d_valid || d_can_move;
        // Whether writeback's instruction of the next cycle writes a source of decode's (section 4's decisions).
        const bool w_stays = w_valid && !w_commits;
        const auto next_writeback_writes = [&](unsigned source) {
            return x_moves ? x_writes_destination && x_dec.rd == source : w_stays && w_writes && w_rd == source;
        };
        const bool next_p1 = dec.uses1 && next_writeback_writes(dec.rs1);
        const bool next_p2 = dec.uses2 && next_writeback_writes(dec.rs2);

        // ---- fetch.veryl: the queue with what arrives in t, the front instruction, the request
        const bool restart = f.starting || redirect_valid;
        const uint32_t restart_pc = (f.starting ? pc : redirect_pc) & ~1u;
        uint16_t av[QUEUE_CAPACITY + 3] = {};
        bool ae[QUEUE_CAPACITY + 3] = {};
        unsigned an = f.n;
        for (unsigned i = 0; i < an; i++) { av[i] = f.q[i]; ae[i] = f.qe[i]; }
        if (f.resp) {
            if (f.skip) {
                av[an] = uint16_t(f.rw[0] >> 16); ae[an++] = f.re[0];
                av[an] = uint16_t(f.rw[1]); ae[an++] = f.re[1];
                av[an] = uint16_t(f.rw[1] >> 16); ae[an++] = f.re[1];
            } else {
                av[an] = uint16_t(f.rw[0]); ae[an++] = f.re[0];
                av[an] = uint16_t(f.rw[0] >> 16); ae[an++] = f.re[0];
            }
        }
        DCHECK(an <= QUEUE_CAPACITY || restart, "fetch's queue overflows: %u halfwords", an);
        const bool front_compressed = (av[0] & 3) != 3, front_error = ae[0];
        const bool front_one = front_error || front_compressed;
        const bool fetched_valid = an >= 1 && (front_one || an >= 2);
        const uint32_t fetched_instruction = front_compressed ? av[0] : uint32_t(av[1]) << 16 | av[0];
        const bool fetched_error = front_error || (!front_compressed && ae[1]);
        const bool instruction_taken = fetched_valid && fetched_ready && !restart;
        const unsigned taken = !instruction_taken ? 0 : front_one ? 1 : 2;
        const bool raise = restart || an + 2 <= QUEUE_CAPACITY;
        const uint32_t request_address = restart ? restart_pc : f.next;
        const bool fetch_outstanding = f.resp;               // rv32_core.veryl: its response's cycle included

        // ================================================= section 10: the edge that ends t
        // ---- statistics, the trace and the checks of what happens in t
        if (w_commits) {
            instructions++;
            if (first_image_at < 0 && region(w_pc) == R_PMEM) first_image_at = int64_t(t);
            if (log) log->add(t, T_RETIRE, commit_writes, w_pc, w_raw, w_a, w_b, commit_value);
        } else if (in_wfi) cycles_wfi++;
        else cycles_stalled++;
        if (in_wfi && wfi_wake_up) took_wake(e, t, true);
        if (wfi_retires) {
            wfi_at = int64_t(t); wfi_in_rom = region(w_pc) == R_BOOTROM;
            if (in_handler && !interrupt_pending) parked = true;    // a WFI in the handler with nothing to take
            if (wfi_wake_up) took_wake(e, t, false);
        }
        if (w_commits && w_cls == JAL && w_next_pc == w_pc && in_handler) parked = true;   // the handler's park loop

        // ---- csr.veryl: the counters, then a trap's entry, MRET, or a CSR write (at most one a cycle)
        bool mcycle_written = false, minstret_written = false;
        if (trap_taken) {
            const uint32_t cause = w_traps ? w_trap_cause : interrupt_cause;
            const uint32_t at = w_traps ? w_pc : x_pc;
            const uint32_t tval = !w_traps || cause == CAUSE_FETCH || cause == CAUSE_ECALL ? 0
                                : cause == CAUSE_ILLEGAL ? w_raw : cause == CAUSE_BREAKPOINT ? w_pc : w_result;
            traps++; trap_at = int64_t(t); trap_cause = int32_t(cause);
            in_handler = true;
            mepc = at & ~1u; mcause = cause; mtval = tval;
            mstatus = (mstatus & MSTATUS_MIE) ? uint32_t(MSTATUS_MPIE) : 0u;
            pc = trap_vector;
            if (cause == CAUSE_ILLEGAL) illegals++;
            if (cause == CAUSE_FETCH || cause == CAUSE_LOAD_FAULT || cause == CAUSE_STORE_FAULT) bus_errors++;
            if (log) log->add(t, T_TRAP, 0, mcause, mepc, mtval, pc);
        } else if (w_returns) {
            mstatus = ((mstatus & MSTATUS_MPIE) ? uint32_t(MSTATUS_MIE) : 0u) | MSTATUS_MPIE;
            pc = return_pc;
        } else if (w_commits && w_cls == CSR && w_csr_writes) {
            const uint32_t op = w_raw >> 12 & 3, v = op == 1 ? w_result : op == 2 ? csr_old | w_result : csr_old & ~w_result;
            csr_write(w_raw >> 20, v, mcycle_written, minstret_written);
        }
        if (w_commits && !w_returns) pc = w_next_pc;
        if (w_commits && !minstret_written) minstret++;
        if (!mcycle_written) mcycle++;
        core_status_access_fault = trap_taken && (mcause == CAUSE_FETCH || mcause == CAUSE_LOAD_FAULT || mcause == CAUSE_STORE_FAULT);
        core_status_illegal_instruction = trap_taken && mcause == CAUSE_ILLEGAL;
        core_status_canonical_wait = in_wfi && !raise && !fetch_outstanding;
        const bool asleep = core_status_canonical_wait && !wfi_wake_up;
        in_wfi = (in_wfi || wfi_retires) && !wfi_wake_up;

        // ---- the register file
        if (commit_writes) {
            x[w_rd] = commit_value;
            if (min_sp_watch && w_rd == 2 && commit_value != 0 && int32_t(commit_value) < min_sp) min_sp = int32_t(commit_value);
        }

        // ---- the load/store unit: a new access starts as its instruction moves to writeback
        const bool lsu_starts = x_moves && x_accesses && !x_misaligned;
        if (lsu_starts) {
            lsu_out = true; lsu_made = false; lsu_err = false;
            lsu_store = x_dec.cls == STORE; lsu_addr = x_result; lsu_size = 1u << (x_dec.f3 & 3); lsu_wdata = x_b;
            const DataRoute r = route(e, lsu_addr, lsu_store);
            lsu_make_at = t + r.make; lsu_answer = r.answer; stall_reg_load += r.make + r.answer - 1;
        } else if (lsu_done) lsu_out = false;

        // ---- writeback
        if (x_moves) {
            w_valid = true;
            w_pc = x_pc; w_raw = x_raw; w_cls = x_dec.cls; w_rd = x_dec.rd;
            w_writes = x_writes_destination; w_ready = x_result_ready; w_result = x_result; w_mul = x_product;
            w_exc = x_exc || x_misaligned;
            w_cause = x_exc ? x_cause : x_dec.cls == LOAD ? uint32_t(CAUSE_LOAD_MISALIGNED) : uint32_t(CAUSE_STORE_MISALIGNED);
            w_csr_writes = x_dec.csr_writes;
            w_next_pc = x_transfers ? x_target : x_next_pc; w_a = x_a; w_b = x_b;
        } else if (w_traps || w_commits) w_valid = false;

        // ---- execute, with the forwarding decisions
        if (flush || (redir_pending && redir_cancels)) x_valid = false;
        else if (d_moves) x_valid = true;
        else if (x_moves) x_valid = false;
        if (d_moves) {
            x_pc = d_pc; x_raw = d_raw; x_dec = dec; x_exc = d_exc; x_redirected = d_jumps_early;
            x_cause = d_ferr ? uint32_t(CAUSE_FETCH) : uint32_t(CAUSE_ILLEGAL);
            x_p1 = next_p1; x_p2 = next_p2;
        } else {
            x_p1 = x_p1 && !w_commits; x_p2 = x_p2 && !w_commits;
        }

        // ---- the branch redirect
        redir_pending = branch_redirect || early_redirect;
        redir_cancels = branch_redirect;
        redir_target = branch_redirect ? x_target : d_jump_target;

        // ---- decode
        if (fetched_ready) { d_pc = f.q_pc; d_raw = fetched_instruction; d_ferr = fetched_error; }
        if (flush || redirect_valid) d_valid = false;
        else if (fetched_ready) d_valid = fetched_valid;

        // ---- fetch: the queue less what decode took, and the request raised in t, answered in t + 1
        if (restart) {
            f.n = 0; f.skip = restart_pc >> 1 & 1; f.q_pc = restart_pc;
        } else {
            f.n = uint8_t(an - taken);
            for (unsigned i = 0; i < f.n; i++) { f.q[i] = av[i + taken]; f.qe[i] = ae[i + taken]; }
            if (f.resp) f.skip = false;
            f.q_pc += 2 * taken;
        }
        f.starting = false;
        f.resp = raise;
        if (raise) {
            const uint32_t word = request_address & ~3u;
            f.rw[0] = fetch_word(e, word, f.re[0]);
            f.rw[1] = 0; f.re[1] = false;
            if (request_address & 2) f.rw[1] = fetch_word(e, word + 4, f.re[1]);
            f.next = word + (request_address & 2 ? 8 : 4);
        }

        // ---- the data port: the outstanding access is made in the cycle before its answer
        if (lsu_starts && lsu_make_at == t) make_access_at(e, t);   // an access made in the cycle it is raised in
        return asleep;
    }

    // ---- section 5: the ALU, the branch conditions, and multiply.veryl's results
    static uint32_t alu(rv32d::Alu op, uint32_t a, uint32_t b) {
        using namespace rv32d;
        switch (op) {
        case ADD: return a + b;
        case SUB: return a - b;
        case SLL: return a << (b & 31);
        case SLT: return int32_t(a) < int32_t(b);
        case SLTU: return a < b;
        case XOR: return a ^ b;
        case SRL: return a >> (b & 31);
        case SRA: return uint32_t(int32_t(a) >> (b & 31));
        case OR: return a | b;
        case CZERO_EQZ: return b == 0 ? 0 : a;
        case CZERO_NEZ: return b != 0 ? 0 : a;
        default: return a & b;
        }
    }
    static bool branch_holds(unsigned f3, uint32_t a, uint32_t b) {
        switch (f3) {
        case 0: return a == b;
        case 1: return a != b;
        case 4: return int32_t(a) < int32_t(b);
        case 5: return int32_t(a) >= int32_t(b);
        case 6: return a < b;
        default: return a >= b;
        }
    }
    static uint32_t multiply(unsigned f3, uint32_t a, uint32_t b) {
        const int64_t la = int32_t(a), lb = int32_t(b);
        switch (f3) {
        case 0: return a * b;
        case 1: return uint32_t(uint64_t(la * lb) >> 32);
        case 2: return uint32_t(uint64_t(la * int64_t(uint64_t(b))) >> 32);
        default: return uint32_t((uint64_t(a) * uint64_t(b)) >> 32);
        }
    }
    // lsu.veryl: the load's value, from the answered word's lanes, sign- or zero-extended.
    uint32_t load_value() const {
        const uint32_t v = lsu_rdata >> ((lsu_addr & 3) * 8), f3 = w_raw >> 12 & 7;   // (a compressed load is a word load)
        if (lsu_size == 4) return v;
        const bool is_unsigned = (w_raw & 3) == 3 && (f3 & 4);
        if (lsu_size == 1) return is_unsigned ? v & 0xFF : uint32_t(int32_t(int8_t(v)));
        return is_unsigned ? v & 0xFFFF : uint32_t(int32_t(int16_t(v)));
    }

    // ================================================================ the CSRs (csr.veryl)
    // The CSR's value; false: not implemented (an illegal instruction).
    bool csr_read(uint32_t csr, bool meip, uint32_t& v) const {
        v = 0;
        switch (csr) {
        case MSTATUS: v = mstatus | 3u << 11; return true;
        case MISA: v = MISA_VALUE; return true;
        case MIE_CSR: v = mie; return true;
        case MTVEC: v = mtvec; return true;
        case MSCRATCH: v = mscratch; return true;
        case MEPC: v = mepc; return true;
        case MCAUSE: v = mcause; return true;
        case MTVAL: v = mtval; return true;
        case MIP: v = meip ? uint32_t(MEIE) : 0u; return true;
        case MCYCLE: v = uint32_t(mcycle); return true;
        case MCYCLEH: v = uint32_t(mcycle >> 32); return true;
        case MINSTRET: v = uint32_t(minstret); return true;
        case MINSTRETH: v = uint32_t(minstret >> 32); return true;
        case MHARTID: v = hart_id; return true;
        case MSTATUSH: case MCOUNTINHIBIT: case MVENDORID: case MARCHID: case MIMPID: case MCONFIGPTR: return true;
        default:                                                               // the counters and events that read 0
            return (csr >= 0x323 && csr <= 0x33F) || (csr >= 0xB03 && csr <= 0xB1F) || (csr >= 0xB83 && csr <= 0xB9F);
        }
    }
    // A CSR write (to an implemented, writable address). A write to a counter replaces its count of this cycle.
    void csr_write(uint32_t csr, uint32_t v, bool& mcycle_written, bool& minstret_written) {
        switch (csr) {
        case MSTATUS: mstatus = v & (MSTATUS_MIE | MSTATUS_MPIE); break;
        case MIE_CSR: mie = v & MEIE; break;
        case MTVEC: mtvec = v & ~3u; break;
        case MSCRATCH: mscratch = v; break;
        case MEPC: mepc = v & ~1u; break;
        case MCAUSE: mcause = v & 0x8000001Fu; break;
        case MTVAL: mtval = v; break;
        case MCYCLE: mcycle = (mcycle & ~0xFFFFFFFFull) | v; mcycle_written = true; break;
        case MCYCLEH: mcycle = (mcycle & 0xFFFFFFFFull) | uint64_t(v) << 32; mcycle_written = true; break;
        case MINSTRET: minstret = (minstret & ~0xFFFFFFFFull) | v; minstret_written = true; break;
        case MINSTRETH: minstret = (minstret & 0xFFFFFFFFull) | uint64_t(v) << 32; minstret_written = true; break;
        default: break;                                                        // the rest ignore writes
        }
    }

    // ================================================================ the ports: the instruction port
    // The instruction word at `a` (word-aligned), as a fetch raised in this cycle reads it; `err`: outside IMEM's
    // window and the boot ROM's (rv32_core.veryl: such a word is answered with an error whatever the memories return).
    template <class Env>
    uint32_t fetch_word(Env& e, uint32_t a, bool& err) {
        if constexpr (requires { e.bus_fetch(a, err); }) return e.bus_fetch(a, err);
        else {
            err = false;
            if (region(a) == R_PMEM && offset(a) < MEM_BYTES) return imem[offset(a) >> 2];
            if (region(a) == R_BOOTROM && offset(a) < BOOTROM_BYTES) return offset(a) >> 2 < bootrom_words ? bootrom[offset(a) >> 2] : 0;
            err = true;
            return 0;
        }
    }

    // ================================================================ the ports: the data port
    // When the data access at `a`, raised in cycle q, is made and answered (the header's table): made in q + make, or the
    // first later cycle it can be, and answered `answer` cycles after that (0 or 1; 1 if make is 0).
    struct DataRoute { uint32_t make, answer; };
    template <class Env>
    DataRoute route(Env& e, uint32_t a, bool store) const {
        if constexpr (requires { e.bus_route(a); }) { const uint32_t l = e.bus_route(a); return {l ? l - 1 : 0, 1}; }
        else {
            if (in_tables(a)) return {timing.table_load_latency - 1, 1};
            if (region(a) != 0xFF) return {0, 1};             // DMEM, or no window (data_decoder.veryl)
            return {timing.reg_load_latency - 1, !store || may_stall(e, a) ? 1u : 0u};
        }
    }
    // A store to `a` in the register windows may stall (register_bus: it is answered the cycle after its owner takes it).
    // v1.35's map: REQUEST_SEND, the sends, and what the environment's `store_may_stall` adds.
    template <class Env>
    bool may_stall(Env& e, uint32_t a) const {
        if constexpr (requires { e.store_may_stall(a); }) { if (e.store_may_stall(a)) return true; }
        if (!in_regs(a) || in_windows(a) || (a & 3)) return false;
        const unsigned i = (a - REG_BASE) >> 2;
        return i == REQUEST_SEND || (i >= ROUTER_SEND_32 && i <= ROUTER_SEND_128);
    }
    // Makes the outstanding access in cycle t, if it can be made then, and sets its answer's cycle.
    template <class Env>
    void make_access_at(Env& e, uint64_t t) {
        if (make_access(e, t)) { lsu_made = true; lsu_resp_at = t + lsu_answer; }
    }
    // The outstanding access, made in cycle t. False: its owner does not take it in t (v1.35's: a full request FIFO, a
    // window that stalls), and it is tried again in t + 1.
    template <class Env>
    bool make_access(Env& e, uint64_t t) {
        const uint32_t ea = lsu_addr, sh = (ea & 3) * 8;
        const uint32_t mask = (lsu_size == 4 ? 0xFFFFFFFFu : lsu_size == 2 ? 0xFFFFu : 0xFFu) << sh;
        if constexpr (requires { e.bus_route(ea); }) {
            lsu_err = e.bus_route(ea) == 0 || !e.bus_access(t, ea, lsu_store, mask, lsu_wdata << sh, lsu_rdata);
            return true;
        } else {
            if (lsu_store) {
                if (lsu_size == 4 && in_regs(ea) && !in_windows(ea) && (ea - REG_BASE) >> 2 == REQUEST_SEND && !e.can_push()) {
                    if (log) log->add(t, T_REFUSED);
                    stall_request++;
                    return false;                             // a full request FIFO: made at t + 1
                }
                if (lsu_size == 4 && in_windows(ea) && window_present(window_of(ea))) {
                    if constexpr (requires { e.window_stalls(0u, 0u); }) {
                        if (e.window_stalls(window_of(ea), (ea & 0x1FF) >> 2)) return false;   /* 8.14: UART_TX while the TX FIFO is full: retried */
                    } else FATAL("tile core: a store to peripheral %u's window at 0x%x: this bench has no peripheral slot", window_of(ea), ea);
                }
                lsu_err = !store(e, t, ea, lsu_wdata << sh, mask, w_pc);
                if (halt != RUN) {                            // SIM_EXIT: the run ends with its store
                    instructions++;
                    if (log) log->add(t, T_RETIRE, 0, w_pc, w_raw, w_a, w_b, 0);
                }
            } else {
                lsu_rdata = 0;
                lsu_err = !load(e, ea, lsu_size, lsu_rdata);
            }
            return true;
        }
    }

    // ================================================================ the data side: the tile's map
    // A data-memory word. (v135_platform) Past the memory's end the offset's high bits are ignored (it aliases; ref
    // flags it); the RTL's core faults such an access (data_decoder.veryl: in no window).
    uint32_t mem_word(uint32_t a) {
        uint32_t off = offset(a);
        if (off >= MEM_BYTES) {
            alias_accesses++; violations++;
            if (ck_tile >= 0) sim::check::record(ck_t, ck_tile, sim::check::Kind::CTL_ALIAS, a);   // (ck_t: this step's)
        }
        return (off & (MEM_BYTES - 1)) >> 2;
    }
    bool window_present(unsigned n) const { return n >= 1 && n <= 3 && (periph_present >> (n - 1) & 1); }

    // A load of `width` bytes (the aligned word holding ea); false: a bus error.
    template <class Env>
    bool load(Env& e, uint32_t ea, uint32_t width, uint32_t& v) {   // (ck_t: the load's cycle for the checker's facts)
        uint32_t a = ea & ~3u;
        if (region(a) == R_DMEM) {
            if (!v135_platform && offset(a) >= MEM_BYTES) return false;
            const uint32_t w = mem_word(a); if (cm) cm->loaded(w, ck_t); v = dmem[w]; return true;   // (cm)
        }
        // (v135_platform) program memory and the boot ROM as data: the RTL's serve fetches only, and a load faults
        if (region(a) == R_PMEM) { if (!v135_platform) return false; v = imem[mem_word(a)]; return true; }
        if (region(a) == R_BOOTROM) {
            if (!v135_platform) return false;
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
                /* the slot's window RegFile (PeripheralSlot.read); 2: a reserved or undefined word, read 0, flagged */
                if (e.window_load(n, (a & 0x1FF) >> 2, v) == 2) {
                    reserved_accesses++; violations++;
                    // ref's PeripheralSlot.reserved (load): the slot's own check through the block (code 1, F_RESERVED)
                    if (ck_tile >= 0) sim::check::record(ck_t, ck_tile, sim::check::Kind::SLOT_VIOLATION, 1, n << 8 | ((a & 0x1FF) >> 2), 0);
                }
                return true;
            } else FATAL("tile core: a load from peripheral %u's window at 0x%x: this bench has no peripheral slot", n, a);
        }
        unsigned i = (a - REG_BASE) >> 2;
        const RegInfo& r = reg_info(i);
        // (the request port's receive side, 4.18): where the Env has it. A tile's reads 0 and REQUEST_RECEIVE sets
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
                if (ck_tile >= 0) sim::check::record(ck_t, ck_tile, sim::check::Kind::CTL_RESERVED_ACCESS, 0, 0x800 + 4 * i);
            }
            v = 0; return true;
        }
        switch (i) {
        case EVENTS: {                                                         // consumes what it returned under the mask
            v = e.reg(EVENTS);
            if constexpr (requires { e.live_events(); }) v |= e.live_events();   // request pending (EVENTS [12])
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
            if (!v135_platform && offset(a) >= MEM_BYTES) return false;
            uint32_t w = mem_word(a);
            if (cm) cm->stored(w, t);                                 // invariant 8's read tracking
            if (offset(a) < DMEM_FIRST) {
                violations++;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_DMEM_RESERVED, ea);
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
                    return e.window_store(t, n, (a & 0x1FF) >> 2, v);           /* false: a bus error */
                } else FATAL("tile core: a store to peripheral %u's window at 0x%x: this bench has no peripheral slot", n, a);
            }
            unsigned i = (a - REG_BASE) >> 2;
            const RegInfo& r = reg_info(i);
            if (r.store == A_OUT) FATAL("tile core: a store to %s (0x%x): not modelled yet", r.name, a);
            if (r.store == A_NONE) {
                if (r.load != A_NONE) return false;                            // a read-only register: bus error
                reserved_accesses++; violations++;                             // reserved: ignored
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_RESERVED_ACCESS, 1, 0x800 + 4 * i);
                return true;
            }
            if (v & ~defined_bits(i)) {                                        // reserved bits set: ignored (invariant 21)
                violations++;
                if (ck_tile >= 0) sim::check::record(t, ck_tile, sim::check::Kind::CTL_RESERVED_BITS, i, v & ~defined_bits(i));
            }
            if (ck_tile >= 0 && i == DMA_RECORD_BASE_ADDRESS && (v & 1))       // TileControl.write's checks (facts only)
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
