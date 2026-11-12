// mach.h (WORKER20, P2, rows C1 and B18): the machine harness the Java-driven suites of v1.3 port onto. v1.3's suites
// drive `ref`'s Chip through three harness classes, and this header gives each its C++ counterpart:
//
//   Java (sim/ref/meshsim)                 here
//   Config (spec 10), Config.machine(),    mach::MachConfig, MachConfig::machine(), MachConfig::defaults()
//     Config.defaults()                    (step 3 completes the fields and `cfg.<field>=`; D41 Q4)
//   Chip (run, step, runUntil, drain,      mach::Chip: either machine, its tiles' units, C&C, the cores, the checks
//     quiescent, tiles[], cc, cores, ...)
//   Tile (sram, ts, ag, setStream, ...)    mach::TileRef: a tile's units by name and Tile's own operations
//   CnC (command, go, runWhenReady, ...)   mach::Cc: C&C 1's Java model, queued from the harness between cycles
//   Mach (boot, bootCores, word, ...)      mach::Mach: the machine as specified, booting images
//   Net (at, rd, cncSends, runAll, ...)    mach::Net: the coreless machine fed packets from the harness
//   A2A                                    mach::A2A: the digit all-to-all's data, loaded and verified
//   Fields (the table entries, DMA_CONFIG) mach::fields (below); LinkWord is units/linkword.h's `lw`
//   T                                      suite.h
//
// STATUS. The header is published before its body (owner, 2026-11-10) so P4-P6's workers can plan against it. What a
// suite may rely on now: the names, the Java each one stands for, and the semantics stated here. Members marked
// [step 2] (the checker, reset), [step 3] (Config's remaining fields) and [step 4] (PhaseLog, the TRACE log) are
// declared now and come with those steps of WORKER20's plan (sessions/WORKER20/NOTES.md). Ask W20 for anything a suite
// needs that isn't here: the harness grows with the suites (V135-INVENTORY's wave 2).
//
// THE TWO MACHINES (ref's Config, B18).
//   - The machine as specified (MachConfig::machine(), ref's Config.machine(): coreless off, realResponder on): every
//     tile halted in the boot ROM at reset, its controller on; C&C 1 boots an image (Mach::boot) and runs the phases.
//     With `cc_cores`, C&C 1-3 are cores (units 64-66) booted through the host port (Mach::boot_cores).
//   - The coreless test machine (MachConfig::defaults(), ref's Config.defaults(): coreless, realResponder off): every
//     tile in RUN with no controller executing, the GATHER engine a drain; the harness configures the engines (TileRef's
//     set_* and pulse, ref's Tile.set*) and feeds packets (Net). Its tiles are the full tiles (the register block, the
//     director, port B, the engines, big SRAM) with the core held off, as in ref (`core.enabled = !coreless`).
//
// TIME. `Chip::cycle()` is ref's `Chip.cycle`: the last cycle run, 0 before the first. (The framework numbers cycles from
// 1 and `Sim::now()` is the next cycle to run, so cycle() == now() - 1, and every cycle a suite prints is ref's.) `step`
// runs one cycle (ref's `step`: `++cycle`). `run`, `run_until` and `drain` first wake every unit, as ref's `run` and
// `runUntil` do (`wakeAll`: the harness may have changed any unit between cycles), and scan the FIFO overflows at the end
// (ref's `scanOverflows`). `run_until(cond, max)` evaluates cond between cycles, before each, exactly as ref's runUntil.
//
// BETWEEN CYCLES ONLY. Every member here is called between cycles (D6: harness code never runs inside the cycle loop).
// The mutators (pokes, set_*, pulse, C&C's queue, the host port, Net's queues) wake the units they touch, so a suite
// that changes state through them may `step` at once; a suite that changes a unit directly (through `unit<T>`) calls
// `Chip::touched(name)` (or uses run/run_until/drain, which wake everything).
//
// THE UNITS. `TileRef::unit<T>(name)` is tile k's unit "t<k>.<name>" (chip.h's unit_prefix; C&C 1-3 as cores are
// "cc<c>.<name>"), in namespace units, the C++ type of ref's member: "sram" BigSram (ref's sram), "ts" TableStore (ts),
// "hist" HistStore (hist), "ag" AddrGen (ag), "we" WriteEngine (writer), "gather" Gather (gather), "inj" Injection (inj
// and srq: one unit here), "router" Router (router), "dir" Director (dir), "ctl" TileCtl (ctl), "core" TileCore (core),
// "slot" PeriphSlot (slot); the chip's "cnc" Cnc (cc), "ep2", "ep3" Endpoint (ccs), "cc<c>.host" HostPort. Statistics a suite reads that a C++ unit doesn't keep yet are added to the unit (counters only:
// no cost in the loop beyond the increment) by whoever ports the suite, or asked of W20.
//
// IMAGES. Mach::Image is ref's Mach.Image (DIR/NAME.code.bin, .data.bin, .sym). The directories searched, in order: the
// MachConfig's `image_dirs`, else $SIM_IMAGES (':'-separated), else
// fw/images (W19), then tests/golden-m2/images, tests/golden/images and tests/m3-images.
//
// SHUFFLING (B24, C25). ref's `shuffleSeed` >= 0 shuffles every child order every cycle; here it sets the framework's
// debug shuffle (sim.h DEBUG CHECKS) with that seed. The seeds don't map to ref's orders (W3's generator), but a suite's
// output is order-independent by design, so its log must equal ref's shuffled step's too.
//
// THE CHECKER [step 2]. ref's per-cycle invariant checks (Chip.checkInvariants, scanOverflows; spec 9) are on in the
// debug and suite builds and compiled out of the timing builds (owner, Q1). `violations()`, `violation_messages()` and
// `violation_report()` are ref's `violations`, `violationMessages` and `violationReport()`, message for message.
//
// THIS IS TEST-SIDE CODE (the configuration regime, D6): std::function, std::vector and strings are fine here.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <map>
#include <string>
#include <vector>

#include "config.h"
#include "phase_log.h"
#include "sim.h"
#include "units/chip.h"
#include "units/regblock.h"

namespace mach {

// ================================================================================================= the configuration
// ref's Config (spec 10), the fields the harness builds from. Defaults are ref's. Step 3 adds the remaining used fields,
// `set` (one `cfg.<field>=value` front end for every driver) and the list of dropped fields.
struct MachConfig {
    // ---- the machine (Config.machine / defaults) ----
    bool coreless = true;              // tiles start in RUN, no controller executing (defaults); machine(): false
    bool real_responder = false;       // the GATHER engine real (machine()); false: a drain
    bool cc_cores = false;             // C&C 1-3 as cores (units 64-66; ref's ccCores)
    bool cc3_tracer = false;           // C&C 3's peripheral 1 the Tracer (needs cc_cores)
    // ---- network (3.2, 4.7) ----
    unsigned link_latency = 3, queue_depth = 16, intra_tile_latency = 1;
    bool skew = true;
    int64_t skew_seed = 1;
    std::vector<int> start_skew;       // per tile, 0 or 1 (empty: drawn from skew_seed when skew is on); ref's startSkew
    // ---- receive side, memory, engines (4.1-4.11) ----
    unsigned we_in_depth = 4, read_port_depth = 64, gather_depth = 16, cm_latency = 2, rp_latency = 2;
    unsigned K = 8, ts_engine_latency = 1, reg_load_latency = 2, ref_cycle = 40;
    int ag_lookahead = -1;             // <= 0: 2 ts_engine_latency + 2
    uint32_t mem_chunks = 1u << 20;
    unsigned area_queue_depth = 8, sram_read_latency = 2, read_tokens = 64;
    bool write_combining = false, fifo_count_before_pop = true;
    bool test_read_clients = false;    // the read queue's AG client a harness-fed FIFO (Net::rd), no AG
    bool reqbus_host = false;          // the request bus driven from the test (Chip::reqbus(), W22-10): its receive queues and
                                       //   controller inputs on the machine with C&C's model (ref's RequestBus there)
    // ---- C&C, the lines (6) ----
    unsigned go_latency = 8, run_latency = 1, ready_latency = 8, cnc_reaction = 1, stage_delay = 64;
    unsigned cnc_rx_depth = 64, cc_dma_depth = 4, cc_link_latency = 1;
    bool done_gate = true, cnc_go_settle = true;
    int error_latency = -1;            // -1: ready_latency (the C++ machine requires it so: one line per tile)
    bool loopback_last = true, loopback_tokens = true;
    int loopback_depth = -1;           // -1: read_tokens
    unsigned replayer_log_entries = 256;   // the replayer's log queues (ref's -Dreplayer.logq; the kit's -q 512)
    bool counter_store_clears_latch = true;   // (inert in ref: no unit reads it; likewise run_latency, stage_delay)
    // ---- the request bus (3.3, 4.18), the peripheral slot (4.17) ----
    unsigned request_fifo_depth = 128, req_hop_latency = 3;
    std::array<unsigned, 3> receive_queue_depth = {128, 256, 128};
    int periph_type = 4;               // every mesh tile's peripheral 1: 4 the replayer, 0 none
    int test_periph_tile = -1;         // ref's testPeripheral (StageB, W25): the replayer on this mesh tile only, the
                                       //   others none (with periph_type 0)
    int test_peripheral_tile = -1;     // ref's testPeripheral giving a TestPeripheral (W26): that mesh tile's peripheral 1
                                       //   is the test peripheral, the other tiles' as periph_type says
    std::vector<int> parked;           // ref's Chip.parked (StageB's -Dstageb.lone): tiles nothing must reach. Here they are
                                       //   simulated all the same (asleep, they cost little); Chip::parked_untouched is ref's check
    // ---- the simulator and its checks (spec 9) [step 2] ----
    int64_t shuffle_seed = -1;         // >= 0: the debug shuffle with this seed
    bool step_all = false;             // the framework's debug options (sim.h DEBUG CHECKS; W22's ask 8): every unit
    bool instance_major = false;       //   stepped every cycle; the instance-major order (else type-major)
    unsigned max_violation_messages = 50;
    bool strict_replies = false, check_read_port_overlap = true, check_command_read = true, check_disjoint = true;
    bool measure_s1 = true, check_errors = true, check_unexpected = true;
    bool log_trace = false;            // ref's logTrace (8.15, H6): every tile's TRACE stores recorded; Chip::log()'s lines
    uint32_t trace_capacity = 1u << 14;   // TRACE stores kept per tile (fatal when exceeded)
    uint32_t check_capacity = 1u << 18;   // the checker's facts (src/check.h) per machine (fatal when full)
    // ---- the harness ----
    std::vector<std::string> image_dirs;   // Mach::Image's search path (empty: the default above)
    uint32_t log_capacity = 1u << 14;      // Net: director-log entries per tile (fatal when exceeded, never grows)
    uint32_t host_capacity = 4096;         // each controller's host-port queue (packets)
    uint32_t phase_log_capacity = 1u << 16;   // C&C's phase log (GOs; fatal when exceeded)
    unsigned threads = 1, parts = 1;       // the framework's split (row bands); a suite's default is one thread

    static MachConfig machine();       // ref's Config.machine()
    static MachConfig defaults();      // ref's Config.defaults()
    unsigned ag_lookahead_used() const { return ag_lookahead > 0 ? unsigned(ag_lookahead) : 2 * ts_engine_latency + 2; }
    unsigned k_min() const;            // ref's kMin(): max(2L + 4, L + 4, cm_latency, rp_latency), L = ts_engine_latency
    MachConfig& with_ts_latency(unsigned L);   // ref's withTsLatency
    // ref's field by name (SortTest's `cfg.<field>=value`, ref's spelling and types: int, long, boolean as Java parses
    // them): false and `err` for an unknown field, a dropped one (dropped() says why) or a bad value. Fields no unit of
    // ref reads (runLatency, stageDelay, counterStoreClearsLatch; rpLatency only in kMin) are inert here too; errorLatency
    // other than readyLatency and logTrace (until H6) stop the build, naming them: never silently ignored.
    bool set(const std::string& field, const std::string& value, std::string& err);
    // Every `cfg.<field>=value` among args, in order (others left alone); false and `err` at the first bad one.
    bool set_args(const std::vector<std::string>& args, std::string& err);
    // The fields of ref's Config no v1.35 driver, kit, sweep or documented option sets (D41 Q4), each with its reason.
    static const std::vector<std::pair<std::string, std::string>>& dropped();
};

// ============================================================================================ Fields (ref's Fields.java)
// The engines' encoders, as the coreless suites compose STREAM, GROUP and DMA_CONFIG words (spec 4.2, 8.1, 8.5, 8.9).
namespace fields {
inline constexpr uint32_t SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2, SIZE_UNDEF = 3, COUNT_MAX = 0x1FFFF;
inline constexpr uint32_t RUN_STREAMS = 0, RUN_GROUPS = 1, PULSE_START = 2;
constexpr uint32_t hdr_bits(uint32_t size, uint32_t counter, uint32_t mode, uint32_t dst) {
    return (size & 3) << 17 | (counter & 7) << 19 | (mode & 7) << 22 | (dst & 0x7F) << 25;
}
constexpr uint32_t stream_src(uint32_t chunk, uint32_t gap_after = 0) { return (chunk & 0x1FFFFF) | (gap_after & 7) << 21; }
constexpr uint32_t stream_count(uint32_t count, uint32_t size, uint32_t counter, uint32_t mode, uint32_t d) {
    return (count & COUNT_MAX) | hdr_bits(size, counter, mode, d);
}
constexpr uint32_t group_lo(uint32_t R) { return R & 0x3FFFF; }
constexpr uint32_t group_hi(uint32_t start_stream, uint32_t t, uint32_t gap) {
    return (start_stream & 0xFF) | ((t - 1) & 0xF) << 8 | (gap & 0xFF) << 12;
}
constexpr uint32_t master_config(uint32_t kind, uint32_t start, uint32_t count, uint32_t bucket_digit, uint32_t hist_digit,
                                 uint32_t k) {
    return (kind & 1) | (start & 0xFF) << 1 | (count & 0x1FF) << 9 | (bucket_digit & 7) << 18 | (hist_digit & 7) << 21 |
           ((k - 1) & 7) << 24;
}
}  // namespace fields

class Chip;

// ======================================================================= the request bus from a test (ref's RequestBus)
// On the machine with C&C's Java model and MachConfig::reqbus_host (RequestBusTest's Net(Config.machine())): ref's
// RequestBus as a test drives it, between cycles. t is the cycle the call stands for (ref's: the next, cycle() + 1, for
// the receivers' side; the cycle just run for controller_send); the reads are as ref has them at the end of the cycle
// just run, the test's own pops already counted out.
class ReqBus {
public:
    explicit ReqBus(Chip& c) : c_(&c) {}
    bool pending(unsigned target, int64_t t) const;                  // ref's pending(target, t)
    uint32_t get(unsigned target, int64_t t, unsigned i) const;      // ref's get(target, t, i): word i of the head, 0 if none
    uint32_t receive(unsigned target, int64_t t);                   // ref's receive: pops the head at t (its credit back at
                                                                     //   t + 1), returns its word 3; an underflow (0) if none
    bool peek_request(unsigned target, int64_t t, uint32_t w[4]) const;   // ref's peekRequest (false: none)
    void controller_send(int64_t t, unsigned cc, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3);   // ref's controllerSend
    size_t queue_size(unsigned target) const;                        // ref's queue[target].size()
    int64_t credits(unsigned target) const;                          // ref's credits[target]
    uint64_t underflows() const { return underflows_; }              // ref's underflows (the test's receives)
    uint64_t dropped() const;                                        // ref's dropped.size()
    uint64_t delivered() const;                                      // ref's delivered.size()
    // ref's delivered list (W22-19): every request delivered to a receive queue, {deliveredAt, srcTile, w0, w1, w2, w3},
    // in delivery order (all targets)
    std::vector<std::array<int64_t, 6>> delivered_log() const;
private:
    Chip* c_;
    uint64_t underflows_ = 0, popped_[3] = {};
    bool head(unsigned target, int64_t t, uint32_t w[4]) const;
};

// ================================================================================================ a tile (ref's Tile)
class TileRef {
public:
    TileRef(Chip& c, unsigned k) : c_(&c), k_(k) {}
    unsigned idx() const { return k_; }                     // ref's idx
    template <class T> T& unit(const char* name) const;     // "t<k>.<name>" (above)

    // ---- the memories (back doors, as ref's poke and peek: no timing) ----
    uint32_t sram_peek(uint32_t chunk) const;               // BigSRAM.peek / poke: one 32-bit chunk
    void sram_poke(uint32_t chunk, uint32_t v);
    uint64_t sram_peek64(uint32_t chunk) const;             // BigSRAM.peek64 / poke64: chunks c, c + 1
    void sram_poke64(uint32_t chunk, uint64_t v);
    uint32_t ts_peek(uint32_t i) const;                     // TableStore.peek / poke (PTR, STREAM, GROUP by index)
    void ts_poke(uint32_t i, uint32_t v);
    uint32_t hist_peek(uint32_t i) const;                   // the HIST store's peek / poke
    void hist_poke(uint32_t i, uint32_t v);
    uint32_t peek_local(uint32_t byte_addr) const;          // Controller.peekLocal / pokeLocal: the controller's
    void poke_local(uint32_t byte_addr, uint32_t v);        //  memories (program 0x01xxxxxx, data otherwise)
    uint32_t dmem(uint32_t word) const;                     // ControllerMemories.dmem[word]

    // ---- Tile's own operations (coreless tests, ref's Tile.set*) ----
    void set_master_config(uint32_t v);                     // DMA_CONFIG: the AG's run, the digits, k (UB: FATAL)
    void set_record_base(uint32_t chunk);                   // DMA_RECORD_BASE_ADDRESS (augment and gather)
    void set_ptr(unsigned i, uint32_t chunk);               // PTR[i]
    uint32_t ptr(unsigned i) const;
    void set_stream(unsigned i, uint32_t src, uint32_t dest, uint32_t count);   // STREAM[i]'s three words
    void set_group(unsigned g, uint32_t lo, uint32_t hi);   // GROUP[g]
    void pulse(uint32_t bits);                              // TILE_PULSE's start, acting from the next cycle
    // ref's sendHold (the send port's holding register, from a test): put(t, ...) at t = cycle() (as of the cycle just
    // run: Sim::put_late) or cycle() + 1 (made by the core in its step then); empty(t) for t = cycle() + 1; refusals
    void send_put(int64_t t, uint64_t lo, uint64_t hdr, uint64_t hi);
    bool send_empty(int64_t t) const;
    uint64_t send_refusals() const;
    // ref's writer.record()/log and gatherStub.record()/log: {t, payload [63:0], header, payload [127:64]} from now on
    void record_writer();
    std::vector<std::array<uint64_t, 4>> writer_log() const;
    // H6: every TRACE store's {cycle, value} (ref's TileControl.traceHook; logTrace's lines), from now on
    void record_trace();
    std::vector<std::array<int64_t, 2>> trace_log() const;
    void record_gather();
    std::vector<std::array<uint64_t, 4>> gather_log() const;
    // ref's `ctl.conditions |= bits` from a test (W23's request 9): ERRORS from the next cycle on (its view, its error
    // line and the checker at that cycle's end), as ref's set between cycles
    void set_errors(uint32_t bits);
    uint32_t remaining(unsigned ctr) const;                 // the director's write counters (dir.remaining[ctr])
    void set_remaining(unsigned ctr, uint32_t v);
    bool mem_idle() const;                                  // mem_idle (4.1)
    bool ts_busy() const;                                   // ts_busy (4.15)
    bool quiescent() const;                                 // Tile.quiescent
    // ref's EngineTest.phaseDone (W22-15; AgTest's and EngineTest's runLocal stop on it): the AG's run over, every counter
    // 0, nothing on the delay line (dir.inFlight), mem_idle, port B idle (dir.cmIdle), the table stores quiet; as ref
    // has them at the end of the cycle just run (the delay line's and port B's words not yet landed by then)
    bool phase_done() const;

    // ---- a test's loads and stores through the core (ref's Controller.load(t, ea, width), store(t, ea, v, mask)) ----
    // load: what a load at t = cycle() + 1 returns (the memories, the boot ROM, HIST, the registers as the core sees
    // them at t, a present peripheral's window); nullopt is ref's null (a bus error). Its side effects are ref's, made
    // by the core in its step at t: EVENTS consumes what it returned under the mask, a window's consuming word likewise,
    // REQUEST_RECEIVE on a mesh tile reads 0 and sets receive underflow (ERRORS [14], visible two cycles later).
    std::optional<uint32_t> load(int64_t t, uint32_t ea, unsigned width) const;
    // store: ref's bus-error reason ("" for none, ref's null); a store that isn't a bus error is made by the core in its
    // step at t (t >= cycle() + 1), through the instruction's path. `mask` as ref's (-1 a whole word).
    std::string store(int64_t t, uint32_t ea, uint32_t v, uint32_t mask = 0xFFFFFFFFu);

    // ---- the request port from a test (W22-11; ref's req.setW, req.send, req.canPush, req.setW[]) ----
    void req_set_w(unsigned i, uint32_t v);                 // ref's req.setW(i, v): REQUEST_SET_W<i> without a store
    void req_send(int64_t t, uint32_t w3);                  // ref's req.send(t, w3), t the cycle just run: pushed as of t
    bool req_can_push(int64_t t) const;                     // ref's req.canPush(t), t = cycle() + 1
    uint32_t req_w(unsigned i) const;                       // ref's req.setW[i]
    // ---- the read port's host queue (W22-18; ref's ReadPort.hostQueue, idle, allAccepted) ----
    void rp_host_queue(uint32_t addr, uint32_t dest);       // a 32-bit read on counter 2 to dest, one a cycle from the next
    bool rp_idle() const;                                   // every entry free and nothing queued from the host
    bool rp_all_accepted() const;                           // every result issued accepted by the director, none queued
    // ---- the write engine's request lines from a test (W22-16; ref's weTsReq.put, weHistReq.put: AgTest's figure 12) ----
    // put(t, op, idx, add) at t = cycle() + 1: on the line to PTR (idx: mem::IDX_PTR + i) or to HIST, arriving L later
    void we_ts_req(int64_t t, uint32_t op, uint32_t idx, uint32_t add);
    void we_hist_req(int64_t t, uint32_t op, uint32_t idx, uint32_t add);
    // ---- the register block from a test (W23's (d)) ----
    void set_events(uint32_t bits);                         // ref's ctl.events |= bits (sticky; from the next cycle on)
    void schedule_go(int64_t t);                            // ref's ctl.scheduleGo(t): GO seen at t (this tile only)

    // ---- the controller (the machine as specified) ----
    uint32_t pc() const;
    uint32_t x(unsigned r) const;
    bool in_wfi() const;
    int halt() const;                                       // ref's Controller.halt: RUN, HALT_EXIT, ...
    int64_t exit_value() const;                             // TileControl.simExitValue (SIM_EXIT, 8.15)
    std::string halt_report() const;                        // Controller.haltReport()

    units::RegBlockState block(bool with_cond = true) const;   // the register block as ref has it now (mach.cpp)

private:
    Chip* c_;
    unsigned k_;
};

// =========================================================================================== C&C 1's Java model (CnC)
// ref's CnC queue, from the harness. A step queued between cycles joins C&C's queue at once (a release point, cnc.h)
// and wakes C&C for the next cycle, as ref's harness queues steps between cycles. With cc_cores there is no CnC (C&C 1
// is a core): use Mach's cc1_command and phase.
class Cc {
public:
    explicit Cc(Chip& c) : c_(&c) {}
    Cc& load_words(unsigned dst, uint32_t byte_addr, const std::vector<uint32_t>& words);   // CnC.loadWords
    Cc& command(unsigned dst, uint32_t word, const std::vector<uint32_t>& args = {});        // CnC.command
    Cc& send(unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi = 0);
    Cc& go();                                               // CnC.go
    Cc& run_when_ready();                                   // CnC.runWhenReady (last_run_at)
    Cc& at_all_ready(std::function<void(int64_t)> fn);      // atAllReady: fn(cycle) between cycles, after it ran
    Cc& wait_cycles(uint32_t n);                            // CnC.waitCycles
    Cc& call(std::function<void(int64_t)> fn);              // CnC.call: fn(the cycle it ran), run between cycles right
                                                            //  after that cycle (ref's lambdas record cycles and state)
    // CnC.waitUntil(cond) in W23's form (approved): C&C waits at this step; the harness checks cond between cycles once
    // C&C has reached it, and C&C moves on in the next cycle. Runs go cycle by cycle while one is pending.
    Cc& wait_until(std::function<bool()> cond);
    // ref's CnC.reset(hard) (B20): a step taking its cycle whose end resets the chip (Chip::reset), two waiting cycles,
    // then READY_EXPECT for all 64 tiles (the model's firmware)
    Cc& reset(bool hard);
    void set_expect(uint64_t tiles);                        // ref's cc1p.expect (READY_EXPECT_0-1): the tiles GO waits for
    // ref's drainReports (C&C 1's request service, modelled; needs MachConfig::reqbus_host): from now on, one request a
    // cycle from target 0's queue: REQ_OP_REPORT into reports(), anything else into requests(), REQ_OP_RESULT also into
    // results() ({t, requestor, w2, w3}) and answered by C&C 1 with a 32-bit mode-0 write to the address in W1 on the
    // request's reply counter, before its own steps. Runs go cycle by cycle while on.
    void drain_reports(bool on);
    std::vector<std::array<int64_t, 5>> reports() const;            // {t, w0, w1, w2, w3}, in arrival order
    std::vector<std::array<int64_t, 5>> reports_of(unsigned id) const;   // ref's reportsOf(id): a unit's reports
    std::vector<std::array<int64_t, 5>> requests() const;
    std::vector<std::array<int64_t, 4>> results() const;            // {t, requestor, w2, w3}
    bool busy() const;                                      // CnC.busy(): steps left in its queue
    int64_t last_go_at() const, last_all_ready_at() const, last_run_at() const;
    // CnC.phaseLog: {all READY seen, GO store} per GO
    std::vector<std::array<int64_t, 2>> phase_log() const;
    size_t phases_logged() const;                           // phaseLog.size(), without the copy (for run conditions)
    std::vector<std::array<uint64_t, 4>> received() const;  // Endpoint.received: {t, payload lo, header, payload hi}
    uint64_t ready_bits() const;                            // the READY bits as C&C sees them
    // (received: ref's cnc.received, on either machine without cc_cores: {t, payload [63:0], header, payload [127:64]})
private:
    Chip* c_;
};

// ===================================================================================================== the chip
class Chip {
public:
    explicit Chip(const MachConfig& cfg);
    ~Chip();
    Chip(const Chip&) = delete;
    Chip& operator=(const Chip&) = delete;

    const MachConfig& cfg() const { return cfg_; }
    sim::Sim& sim();                                        // the framework's machine (observers, unit<T>)
    template <class T> T& unit(const std::string& name);    // any unit by name ("cnc", "cc1.core", "t5.ag", ...)
    void touched(const std::string& name);                  // a unit changed directly: stepped at the next cycle

    // ---- time (above) ----
    int64_t cycle() const;                                  // ref's Chip.cycle
    void step();                                            // one cycle
    void run(uint64_t n);                                   // wake all, n cycles, scan overflows
    bool run_until(const std::function<bool()>& cond, uint64_t max);   // true if cond held (checked before each cycle)
    bool drain(uint64_t max);                               // run until quiescent
    bool quiescent() const;                                 // nothing in flight anywhere (ref's quiescent)
    std::vector<int> start_skew() const;                    // the skew each tile was given (ref's startSkew)

    // ---- the parts ----
    TileRef tile(unsigned k) { return TileRef(*this, k); }  // 0-63; 64-66 the controllers with cc_cores
    Cc& cc();                                               // C&C 1's model (not with cc_cores)
    PhaseLog& phases();                                     // ref's Chip.phases, attached at the first call (phase_log.h)
    ReqBus& reqbus();                                       // ref's Chip.reqBus from a test (MachConfig::reqbus_host)
    bool all_stopped() const;                               // no tile's core in RUN
    bool all_exited() const;                                // every tile ended through SIM_EXIT
    bool all_in_wfi() const;
    bool parked_untouched() const;                          // ref's parkedUntouched (MachConfig::parked; true if none)
    // ref's Replayer.rowListener on tile k's replayer (peripheral 1): fn(the record, REC_LONGS words; the cycle it left)
    // for every row the replayer sinks, called between cycles after the cycle it left in (runs go cycle by cycle then)
    void on_row(unsigned tile, std::function<void(const uint64_t* rec, int64_t cycle)> fn);

    // ---- the cores (cc_cores; ref's CcCores) ----
    void host_load(unsigned cc, uint32_t byte_addr, const std::vector<uint32_t>& words);       // CcCores.hostLoad
    void host_command(unsigned cc, uint32_t word, const std::vector<uint32_t>& args);          // CcCores.hostCommand

    // ---- the checker [step 2] ----
    uint64_t violations() const;                            // ref's Chip.violations
    const std::vector<std::string>& violation_messages() const;   // the first max_violation_messages, ref's text
    void clear_violation_messages();
    // ref's Config switches changed mid-run (W23's request 10, CtrlTest): from the next cycle on, as ref reads them each
    // cycle (the report through the cycle just run is made first); and ref's `violations = 0` with the messages cleared
    void set_check_errors(bool on);
    void set_strict_replies(bool on);
    void set_max_violation_messages(unsigned n);
    void clear_violations();
    std::string violation_report();                         // ref's violationReport() (scans the overflows first)
    void scan_overflows();
    // ref's Chip.newPhase (the agreed form): between cycles, a new disjoint-buffer phase on every tile (BigSram::new_phase,
    // W26's), each woken. (Every GO starts one too, inside the tile: W26's.)
    void new_phase();                                  // ref's scanOverflows
    void finalize_checks() const;                           // the checker's report through the cycle just run
    // ---- the fingerprint (B25; D41 Q5: in place of ref's -Dmeshsim.digest=FILE) ----
    // With MESHSIM_DIGEST=FILE in the environment, every Chip appends, when destroyed (and those still alive at exit),
    // one line in creation order: "chip N cycle C units U mcycle M wfi W instructions I violations V fp HASH", the totals
    // as ref's digest prints them and HASH the canonical state fingerprint (tests/statefp.h, equal to ref's StateFp) of
    // the whole chip at its last cycle, in place of ref's digest hash. digest() is that line's text after "chip N ".
    std::string digest() const;
    std::vector<std::string> fingerprint_lines() const;     // the StateFp point's lines ("cycle unit hash"), for diffing
    void fingerprint_dump(const std::string& unit, FILE* out) const;   // one unit's words (StateFp's fpdump), for diffing
    // ref's Chip.log as far as v1.35 keeps it: with log_trace, "cycle T tile K: TRACE 0xV" for every TRACE store, in
    // cycle order (tiles in order within a cycle)
    std::vector<std::string> log() const;
    std::string ag_report() const;                          // ref's Chip.agReport (one line, ref's text)
    // ---- reset (6.1) [step 2] ----
    void reset(bool hard);                                  // takes effect at the end of the current cycle
    uint64_t resets() const;
    int64_t last_reset_at() const;
    void reset_stats();                                     // ref's resetStats: the units' statistics to 0, every FIFO's
                                                            //   peak to its occupancy (the framework FIFOs' peaks, the
                                                            //   write-engine input's and the GATHER queue's, exist under
                                                            //   SIM_CHECKS only: Sim::fifo_peak)
    std::vector<std::array<uint64_t, 4>> received(unsigned cc) const;   // ref's ccs[cc].received (C&C cc + 1; no cc_cores)

    struct Impl;                                            // mach.cpp: the machine, its memories, the harness units
    Impl& impl() { return *impl_; }
    const Impl& impl_const() const { return *impl_; }
private:
    MachConfig cfg_;
    std::string digest_impl(std::vector<std::string>* lines_out, const std::string* dump_unit = nullptr, FILE* dump_out = nullptr) const;
    std::unique_ptr<Impl> impl_;
};

// =============================================================================== the machine as specified (Mach)
class Mach {
public:
    struct Image {                                          // Mach.Image
        std::string name;
        std::vector<uint32_t> code, data;
        std::map<std::string, uint32_t> syms;
        uint32_t sym(const std::string& s) const;           // FATAL "<name>: no symbol <s>", as ref's IllegalArgument
        static Image load(const std::string& name, const MachConfig& cfg);   // the search path above; FATAL if absent
        static Image load_dir(const std::string& dir, const std::string& name);
    };

    explicit Mach(const MachConfig& cfg);
    static MachConfig cfg(int64_t shuffle);                 // Mach.cfg: machine() with the shuffle seed
    static MachConfig cores_cfg(int64_t shuffle);           // Mach.coresCfg: cfg plus cc_cores
    Chip c;
    int64_t boot_start = 0, run_at = -1;                    // Mach.bootStart, runAt
    uint32_t uart_divisor = 0;                              // Mach.uartDivisor

    // ---- the boot through C&C's model (6.2): the image broadcast, CMD_JUMP, READY, GO ----
    Image boot(const std::string& name);
    Image boot(const Image& im);
    // ---- the boot with C&C 1-3 as cores (cores_cfg; W14's bootCores and hostBoot) ----
    static constexpr uint32_t CC_PHASE = 0x50, CC_GO = 0x47, CC_EXPECT = 0x45, CC_EXIT = 0x58, CC_WATCHDOG = 0x57,
                              CC_UART = 0x55, CC_SEGMENT = 0x54;
    static constexpr uint32_t STAGE_CHUNK = 0x10000;
    Image cc1, cc23;
    Image boot_cores(const std::string& tile);
    Image boot_cores(const Image& tile, const Image* cc3);  // cc3 null: cc23 for C&C 3 too
    void host_boot(const Image& im, const std::vector<uint32_t>& data);
    void stage_cc1(uint32_t chunk, const std::vector<uint32_t>& words, uint32_t rows);
    uint32_t mbox(unsigned i) const;                        // C&C 1's mailbox word i (cc1.c)
    uint32_t toast_info(unsigned i) const;
    void cc1_command(uint32_t type, uint32_t arg, const std::vector<uint32_t>& args = {});
    void phase(uint32_t word, const std::vector<uint32_t>& args = {});
    bool cores_booted() const;                              // mbox(3) == 2

    // ---- running and reading back ----
    bool run(const std::function<bool()>& cond, uint64_t max) { return c.run_until(cond, max); }
    bool all_stopped() const { return c.all_stopped(); }
    bool all_exited() const { return c.all_exited(); }
    bool all_in_wfi() const { return c.all_in_wfi(); }
    int64_t exit_value(unsigned tile) const;
    uint32_t word(unsigned tile, uint32_t byte_addr) const;
    uint32_t word(unsigned tile, const Image& im, const std::string& sym, unsigned i) const;
    std::string halt_summary() const;                       // Mach.haltSummary, ref's text
};

// ===================================================================== a machine fed from the harness (Net)
// ref's Net, on any machine (the coreless one or Config.machine(), as ref's; not with cc_cores): per-tile packet queues
// fed through each tile's read-return holding register (the injection point's top
// priority: a packet scheduled for cycle T is taken by the injection point at T), per-tile read-queue client requests,
// C&C's sends, and per-tile arrival logs at the director. The feeding is ref's `feed()`, run between cycles before each
// cycle, so it sees exactly what ref's harness saw. The harness's ports into the injection unit and the director's
// log are optional parts of those units, declared only on a Net machine (nothing in the units' loop otherwise).
class Net {
public:
    explicit Net(const MachConfig& cfg);
    static MachConfig cfg(int64_t shuffle);                 // Net.cfg: defaults(), check_unexpected and check_errors off
    Chip c;
    int64_t late() const;                                   // packets taken later than scheduled (must stay 0)

    // Net.at: tile's packet for injection at cycle T (nondecreasing T per tile): 64 bits on counter 1, or in full
    void at(unsigned tile, int64_t T, unsigned dst, unsigned mode, uint32_t addr, uint64_t payload);
    void at(unsigned tile, int64_t T, unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size,
            uint64_t lo, uint64_t hi);
    // Net.rd: a read-queue request from client cl (0-2) of tile, presented from cycle T (FIFO order per client)
    void rd(unsigned tile, unsigned cl, int64_t T, uint32_t src_chunk, unsigned width, unsigned mode, unsigned counter,
            unsigned dst, uint32_t dst_addr);
    // Net.cncSends: {T, dst, mode, addr, lo[, size, hi[, counter]]}, in order (C&C 1 puts it at T - 1, visible at T)
    struct CncSend { int64_t T; unsigned dst, mode; uint32_t addr; uint64_t lo; unsigned size = 1; uint64_t hi = 0; unsigned counter = 1; };
    void cnc_send(const CncSend& s);
    // ref's ccs[cc].send(c.cycle, ...) between cycles: C&C cc + 1's endpoint puts a packet as of the cycle just run
    // (Sim::put_late), seen by the mesh in the next cycle, as ref's; refused (and lost, as ref's) if its Hold is full.
    void send(unsigned cc, unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi);
    // ref's ccs[cc].received: {t, payload [63:0], header, payload [127:64]} of every packet C&C cc + 1 received
    std::vector<std::array<uint64_t, 4>> received(unsigned cc) const;
    static uint32_t cm(uint32_t packed_word);               // Net.cm: a v1.1 packed controller-memory word's address
    static uint64_t tagged(uint64_t seq, int64_t T) { return seq << 32 | (uint64_t(T) & 0xFFFFFFFFull); }

    bool pending() const;
    void step();                                            // feed, then one cycle
    bool run_all(uint64_t max);                             // until every queue is empty and the machine quiescent
    // the director's log: {t, payload [63:0], header, payload [127:64]} of every accepted packet
    std::vector<std::array<uint64_t, 4>> log(unsigned tile) const;
    void clear_logs();                                      // the directors' logs and the endpoints' received
    unsigned max_router_peak() const;
};

// ===================================================================================== the digit all-to-all (A2A)
// ref's A2A: the data sets (Data(seed, n) with java.util.Random, or a stream matrix from data/), the 16-group schedule,
// the send buffer at chunk 0 and the receive buffer at 0x40000.
struct A2A {
    static const int G4[16][4];
    static constexpr uint32_t SRC = 0, INBOX = 0x40000;
    int cnt[64][64] = {};
    std::vector<int64_t> rec[64];
    int R[16] = {};
    int dstoff[64][64] = {};

    A2A(int64_t seed, int n);                               // v3.10's Data(seed, n)
    explicit A2A(const std::string& matrix);                // a stream matrix (data/zipfraw.txt, trace.txt, one99.txt)
    int expected(unsigned d) const;
    int64_t bound(int gap) const;
    int first(unsigned s, unsigned d) const;
    void load(Chip& c, int gap) const;                      // A2A.load: the coreless machine's buffers and engines
    int verify(Chip& c) const;                              // A2A.verify: records out of place (0: every one landed)
    static int64_t run(Chip& c, int64_t max);               // A2A.run
};

// ---- inline definitions ----
template <class T> T& Chip::unit(const std::string& name) { return sim().unit<T>(name); }
template <class T> T& TileRef::unit(const char* name) const { return c_->unit<T>(units::unit_prefix(k_) + name); }

}  // namespace mach
