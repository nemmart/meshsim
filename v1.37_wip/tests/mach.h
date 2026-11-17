// mach.h: the machine harness: the 64-tile chip as units::build_chip assembles it, with what a test needs to configure
// it, run it and read it back, between cycles. It began as the C++ counterpart of v1.3's Java harness (ref's Config,
// Chip, Tile, CnC, Mach, Net, A2A, Fields), which v1.3's suites drive; v1.36 moved the tile to the RTL's hardware
// (machine-spec 5-13: no GO, DONE or READY registers, no commands and no command block, no TRACE or SIM_EXIT words, an
// NMI, the boot ROM's LOAD), and this header was cut back to what that hardware has. Every name of a register, a field
// or a table here is v1.36's (src/units/regmap.h).
//
//   mach::MachConfig    the machine's parameters; MachConfig::machine() and MachConfig::defaults() (the two machines)
//   mach::fields        the encoders of the tables' entries and the units' configuration words
//   mach::Chip          either machine: time, its tiles' units, the master side, the checker
//   mach::TileRef       a tile's units by name, back doors into its memories, tables and registers
//   mach::Cc            the master side's behavioural model (below), queued from the harness between cycles
//   mach::ReqBus        the request tree driven from a test (MachConfig::reqbus_host)
//   mach::Mach          the machine as specified: images placed in the tiles' memories, read back
//   mach::Net           a machine fed packets and reads from the harness, with the directors' arrival logs
//   mach::A2A           the digit all-to-all's data, loaded into the tables and verified
//
// THE TWO MACHINES.
//   - The machine as specified (MachConfig::machine(): coreless off, real_responder on): every tile's core runs from
//     the reset PC in the RTL's boot ROM (fw/images/boot/, read by tests/rtl_images.h). A compute tile's ROM pulls its image
//     with a LOAD request to the master tile (6.5), which the master tile's firmware serves on the chip (below); on the
//     bench machine nothing serves it, so such a tile waits in its ROM for ever. With MachConfig::boot_local every
//     compute tile has the strap boot_local (7.1), and its ROM enters the image the harness placed in its IMEM
//     (Mach::place) instead.
//   - The coreless test machine (MachConfig::defaults(): coreless, real_responder off): the tiles' cores never execute
//     (TileCore::coreless, harness-only), the gather unit is a drain; the harness configures the units through
//     TileRef's back doors and feeds packets (Net). Every other unit of the tile is the real one.
//
// THE MASTER SIDE.
//   - THE CHIP (MachConfig::chip(), special_tiles): units::build_chip's (src/units/chip.h): the master, memory and tracer
//     tiles as units 64-66 ("cc1.*", "cc2.*", "cc3.*"), each the base tile with its straps and its peripheral 1
//     (Ingress/Egress; the RW engine; the Tracer), on its attach tile's attach port; the request tree; the
//     nets at the RTL chip's latencies. The master and memory tiles start from the images placed in their IMEM and DMEM
//     (boot_local: Mach::place_elf), the others from the boot ROM's LOAD. This is the machine every test of v1.36's chip
//     builds (tests/test_sort.cpp).
//   - A TEST BENCH'S (without special_tiles; tests/bench_master.h): the 64 compute tiles with v1.35's behavioural model
//     of C&C ("cnc"), C&C 2's and 3's endpoints ("ep2", "ep3") on the attach tiles 0, 7 and 15, for the tests of the
//     tile's units that feed a machine from the harness. What it still means on v1.36's tiles: its GO link drives every tile's
//     `release` input, which is BARRIER_RELEASE's pulse (18); its READY input receives each tile's `line` bit 0, which is
//     BARRIER_ARRIVE's pulse, so its "all READY, then GO" is "every expected tile arrived, then the release"; `line`
//     bit 1 is the tile's error line; its sends are packets from id 64 through tile 0's attach port, so a WRITE of it
//     into a tile's IMEM, DMEM or scratchpad lands as the NoC port lands it. What it no longer means: its program's
//     `command` (a packet into v1.35's command block) and v1.35's boot (the image broadcast, CMD_JUMP) address
//     hardware that is gone and no v1.36 image understands them; the model's sequencer keeps them (cnc.h is left as it
//     was), this harness does not offer them. The model has no request service: a tile's REPORT or LOAD reaches the
//     request tree's root and, without MachConfig::reqbus_host, goes nowhere.
//
// TIME. `Chip::cycle()` is the last cycle run, 0 before the first (the framework numbers cycles from 1 and `Sim::now()`
// is the next cycle to run, so cycle() == now() - 1). `step` runs one cycle. `run`, `run_until` and `drain` first wake
// every unit (the harness may have changed any unit between cycles) and scan the FIFO overflows at the end.
// `run_until(cond, max)` evaluates cond between cycles, before each.
//
// WHAT A READ RETURNS. The tile control unit applies the edge that ends cycle t at its step in t + 1 (tile_ctl.h), so
// between cycles its state (TileRef::block, reg, status, events, errors) is the state at the end of the cycle before
// the one just run, plus any back-door write since. The counters alone are brought to the end of the cycle just run
// (TileRef::remaining, and phase_done with it): the decrement and the store the unit applies at its next step counted in. A back-door write (set_*, dma_start) changes the unit's
// state at once and is published at the unit's next step: the units that read it see it from the next cycle on.
//
// BETWEEN CYCLES ONLY. Every member here is called between cycles (D6: harness code never runs inside the cycle loop).
// The mutators wake the units they touch, so a test that changes state through them may `step` at once; a test that
// changes a unit directly (through `unit<T>`) calls `Chip::touched(name)` (or uses run/run_until/drain, which wake
// everything).
//
// THE UNITS. `TileRef::unit<T>(name)` is tile k's unit "t<k>.<name>" (chip.h's unit_prefix; units 64-66 are
// "cc<c>.<name>"), in namespace units: "sram" BigSram (the scratchpad), "ts" TableStore (PTR, STREAM, GROUP), "hist"
// HistStore, "ag" AddrGen (the DMA unit's address generator), "we" WriteEngine, "gather" Gather, "inj" Injection (the
// inject unit, the read arbiter and the tokens), "router" Router, "dir" Director (the receive unit's traffic director),
// "ctl" TileCtl (the tile control unit), "core" TileCore, "slot" PeriphSlot; the chip's "cnc" Cnc, "ep2", "ep3" Endpoint,
// "cc<c>.host" HostPort.
//
// SHUFFLING. `shuffle_seed` >= 0 sets the framework's debug shuffle (sim.h DEBUG CHECKS) with that seed: a test's output
// must not depend on the order units of one level are stepped in.
//
// THE CHECKER. The per-cycle invariant checks (src/check.h's facts, tests/checker.h's rules) are on in the debug and
// suite builds and compiled out of the timing builds. `violations()`, `violation_messages()` and `violation_report()`
// report them (tests/test_checker.cpp provokes it). The checks chapter 20 promises that no unit records are listed in
// OPEN-v1.36.md, (a).
//
// THIS IS TEST-SIDE CODE (the configuration regime, D6): std::function, std::vector and strings are fine here.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "sim.h"
#include "units/chip.h"
#include "units/file_store.h"
#include "units/regblock.h"
#include "units/regmap.h"

namespace units { struct IngressEgress; struct Updater; }   // v1.37: the master tile's, with MachConfig::host_link

namespace mach {

// ================================================================================================= the configuration
// The machine's parameters: the fields the harness builds from (units::G1Config, G1Ctx, NetConfig, ChipSpec, the request
// tree's and the checker's). `set` is the one `cfg.<field>=value` front end of every driver; the names are v1.3's
// Config's (the drivers' options were written with them).
struct MachConfig {
    // ---- the machine ----
    bool coreless = true;              // no tile's core executes (defaults); machine(): false
    bool real_responder = false;       // the gather unit real (machine()); false: a drain
    bool boot_local = false;           // every compute tile has the strap boot_local (7.1): its ROM enters the image placed
                                       //   in its IMEM (Mach::place) instead of pulling one with LOAD (6.5)
    bool special_tiles = false;        // THE CHIP (units::build_chip; chip()): the master, memory and tracer tiles as units
                                       //   64-66, the request tree, the nets. false: a test bench's machine, the 64
                                       //   compute tiles with v1.35's model of C&C in the master's place (above)
    bool host_link = false;            // v1.37 (the chip only; CHANGES-v1.37.md): the master tile's Ingress/Egress has the host
                                       //   link and its DMA (ingress_egress.h), the Updater is its peripheral 2 (updater.h), and
                                       //   the Updater and the Tracer's memory system share one file store. false: v1.36's chip
    // ---- the network (16) ----
    unsigned link_latency = 3, queue_depth = 16, intra_tile_latency = 1;
    bool skew = true;                  // v1.35's start skew: +0 or +1 a tile on the release's link
    int64_t skew_seed = 1;
    // ---- the receive side, the memories, the units (8-15) ----
    unsigned read_port_depth = 64;     // the read unit's queue (READ_FREE's reset value)
    unsigned gather_depth = 64;        // GATHER_QUEUE_DEPTH (13)
    unsigned cm_latency = 2;           // WRITE_PORT_LATENCY (12.3)
    unsigned rp_latency = 2;           // (read by k_min alone)
    unsigned K = 8;                    // LAND_LATENCY (12.3)
    unsigned ts_engine_latency = 1;    // TABLE_LATENCY
    unsigned reg_load_latency = 2;     // a window load's cycles (6.4)
    unsigned ref_cycle = 40;           // core cycles per reference-oscillator cycle (unit 64's UART, cc_cores)
    uint32_t mem_chunks = 1u << 20;    // the scratchpad's words
    unsigned read_latency = 3;         // READ_LATENCY (15, 19): a read's take to its row; the RTL's scratchpad's
    unsigned read_tokens = 64;         // TOKEN_COUNT (14.2)
    bool test_read_clients = false;    // the read arbiter's clients fed from the harness (Net::rd), no address generator
    bool reqbus_host = false;          // the request tree driven from the test (Chip::reqbus()): its receive queues and
                                       //   the special tiles' inputs on the machine with C&C's model
    // ---- the master side's model and the nets (18) ----
    unsigned go_latency = 8;           // the release's link, from the model's GO to a tile
    unsigned ready_latency = 8;        // a tile's `line` (BARRIER_ARRIVE's pulse, the error line) to the model
    unsigned cnc_reaction = 1;         // every expected tile arrived -> the release
    unsigned cnc_rx_depth = 64;
    bool cnc_go_settle = true;         // the model sends nothing until the release has reached every tile
    // the chip's nets (18, 19): independent, each from 0 up (RELEASE_LATENCY from 1: Ingress/Egress's flip-flop's cycle
    // is its first); the defaults are the RTL chip's, whose nets are wires
    unsigned arrive_latency = 0, release_latency = 1, error_latency = 0;
    bool loopback_last = true, loopback_tokens = true;
    int loopback_depth = -1;           // -1: read_tokens
    unsigned replayer_log_entries = 256;   // the replayer's log queues
    // ---- the request unit and tree (11), the peripherals (17) ----
    unsigned request_queue_depth = 4;  // REQUEST_QUEUE_DEPTH (19); `cfg.requestFifoDepth=`
    unsigned req_hop_latency = 3;
    std::array<unsigned, 3> receive_queue_depth = {128, 256, 128};
    int periph_type = 4;               // every compute tile's peripheral 1: 4 the replayer, 0 none
    int test_peripheral_tile = -1;     // that compute tile's peripheral 1 is the test peripheral, the other tiles' as
                                       //   periph_type says
    // ---- the simulator and its checks ----
    int64_t shuffle_seed = -1;         // >= 0: the debug shuffle with this seed
    bool step_all = false;             // the framework's debug options (sim.h DEBUG CHECKS): every unit stepped every
    bool instance_major = false;       //   cycle; the instance-major order (else type-major)
    unsigned max_violation_messages = 50;
    bool check_read_port_overlap = true, check_disjoint = true;
    bool measure_s1 = true, check_errors = true;
    uint32_t check_capacity = 1u << 18;   // the checker's facts (src/check.h) per machine (fatal when full)
    // ---- the harness ----
    uint32_t log_capacity = 1u << 14;      // Net: director-log entries per tile (fatal when exceeded, never grows)
    uint32_t phase_log_capacity = 1u << 16;   // the model's log of its releases (Cc::phase_log; fatal when exceeded)
    unsigned threads = 1, parts = 1;       // the framework's split (row bands); a test's default is one thread

    static MachConfig machine();
    static MachConfig chip();          // machine() with the special tiles: v1.36's chip
    static MachConfig defaults();
    unsigned k_min() const;            // max(2L + 4, L + 4, cm_latency, rp_latency), L = ts_engine_latency
    MachConfig& with_ts_latency(unsigned L);
    // A field by v1.3's name (`cfg.<field>=value`: an int, a long, a boolean as Java parses them): false and `err` for an
    // unknown field, a dropped one (dropped() says why) or a bad value.
    bool set(const std::string& field, const std::string& value, std::string& err);
    // Every `cfg.<field>=value` among args, in order (others left alone); false and `err` at the first bad one.
    bool set_args(const std::vector<std::string>& args, std::string& err);
    // v1.3's Config fields this harness does not have, each with its reason: never set by a driver, or (v1.36) a
    // parameter of hardware or of a check that is gone.
    static const std::vector<std::pair<std::string, std::string>>& dropped();
};

// ============================================================================================================== fields
// The encoders of the tables' entries and of the units' configuration words (machine-spec 5.4, 8.1, 8.3, 8.4, 12.1, 13;
// regmap.h's fields).
namespace fields {
inline constexpr uint32_t SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2, SIZE_UNDEF = 3, COUNT_MAX = units::STREAM_HDR_COUNT_MASK;
inline constexpr uint32_t RUN_STREAMS = 0, RUN_GROUPS = 1;          // DMA_CONFIG.kind
// a header block (5.4): size [1:0], counter [4:2], mode [7:5], dst [14:8]
constexpr uint32_t hdr_block(uint32_t size, uint32_t counter, uint32_t mode, uint32_t dst) {
    return (size & 3) << units::HDR_SIZE_LO | (counter & 7) << units::HDR_COUNTER_LO | (mode & 7) << units::HDR_MODE_LO |
           (dst & units::HDR_DST_MASK) << units::HDR_DST_LO;
}
// STREAM_SRC_ADDR (8.3): a 21-bit scratchpad word address
constexpr uint32_t stream_src(uint32_t word) { return word & units::WORD_ADDRESS_MASK; }
// STREAM_HDR (8.3): the header block, count [31:15]
constexpr uint32_t stream_hdr(uint32_t count, uint32_t size, uint32_t counter, uint32_t mode, uint32_t dst) {
    return hdr_block(size, counter, mode, dst) | (count & COUNT_MAX) << units::STREAM_HDR_COUNT_LO;
}
// GROUP (8.4): word 0 rotations [17:0], first_stream [25:18], streams - 1 [29:26]; word 1 the gap [7:0]
constexpr uint32_t group_w0(uint32_t rotations, uint32_t first_stream, uint32_t streams) {
    return (rotations & units::GROUP_ROTATIONS_MASK) | (first_stream & units::GROUP_FIRST_MASK) << units::GROUP_FIRST_LO |
           ((streams - 1) & units::GROUP_STREAMS_MASK) << units::GROUP_STREAMS_LO;
}
constexpr uint32_t group_w1(uint32_t gap) { return gap & units::GROUP_GAP_MASK; }
// DMA_CONFIG (8.1): kind [0], start [8:1], count [17:9], gap [20:18]
constexpr uint32_t dma_config(uint32_t kind, uint32_t start, uint32_t count, uint32_t gap = 0) {
    return (kind & 1) << units::DMA_KIND_LO | (start & units::DMA_START_MASK) << units::DMA_START_LO |
           (count & units::DMA_COUNT_MASK) << units::DMA_COUNT_LO | (gap & units::DMA_GAP_MASK) << units::DMA_GAP_LO;
}
// RECEIVE_CONFIG (12.1): bucket_digit [2:0], histogram_digit [5:3]
constexpr uint32_t receive_config(uint32_t bucket_digit, uint32_t histogram_digit) {
    return (bucket_digit & 7) << units::RECEIVE_BUCKET_LO | (histogram_digit & 7) << units::RECEIVE_HIST_LO;
}
// GATHER_CONFIG (13): record_base [20:0], value_size (k - 1) [23:21]
constexpr uint32_t gather_config(uint32_t record_base, uint32_t k) {
    return (record_base & units::WORD_ADDRESS_MASK) | ((k - 1) & units::GATHER_VALUE_SIZE_MASK) << units::GATHER_VALUE_SIZE_LO;
}
}  // namespace fields

class Chip;

// ================================================================================== the request tree from a test
// On the machine with C&C's model and MachConfig::reqbus_host: the request tree's receive queues and the special tiles'
// inputs, driven by the test between cycles (units::reqbus::ReqHost). t is the cycle the call stands for (the next,
// cycle() + 1, for the receivers' side; the cycle just run for controller_send); the reads are as at the end of the
// cycle just run, the test's own pops already counted out.
class ReqBus {
public:
    explicit ReqBus(Chip& c) : c_(&c) {}
    bool pending(unsigned target, int64_t t) const;                  // the target's queue holds a request at t
    uint32_t get(unsigned target, int64_t t, unsigned i) const;      // word i of the head, 0 if none
    uint32_t receive(unsigned target, int64_t t);                    // pops the head at t (its credit back at t + 1),
                                                                     //   returns its word 3; an underflow (0) if none
    bool peek_request(unsigned target, int64_t t, uint32_t w[4]) const;   // the head's four words (false: none)
    // a special tile's request (cc 0-2: ids 64-66) entering the root as of t
    void controller_send(int64_t t, unsigned cc, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3);
    size_t queue_size(unsigned target) const;
    int64_t credits(unsigned target) const;                          // the root's credits for the target's queue
    uint64_t underflows() const { return underflows_; }              // the test's receives of an empty queue
    uint64_t received(unsigned target) const { return received_[target % 3]; }
    uint64_t dropped() const;                                        // requests the root dropped (no such target)
    uint64_t delivered() const;
    bool idle() const;                                               // no beat in a node's input or on its way to one,
                                                                     //   no request on its way to a queue
    // every request delivered to a receive queue, {delivered at, requester, w0, w1, w2, w3}, in delivery order
    std::vector<std::array<int64_t, 6>> delivered_log() const;
private:
    Chip* c_;
    uint64_t underflows_ = 0, popped_[3] = {}, received_[3] = {};
    bool head(unsigned target, int64_t t, uint32_t w[4]) const;
};

// ============================================================================================================== a tile
class TileRef {
public:
    TileRef(Chip& c, unsigned k) : c_(&c), k_(k) {}
    unsigned idx() const { return k_; }
    template <class T> T& unit(const char* name) const;     // "t<k>.<name>" (above)

    // ---- the memories (back doors: no timing) ----
    uint32_t sram_peek(uint32_t word) const;                // the scratchpad: one 32-bit word, by word address
    void sram_poke(uint32_t word, uint32_t v);
    uint64_t sram_peek64(uint32_t word) const;              // words w, w + 1
    void sram_poke64(uint32_t word, uint64_t v);
    // A table's word (units::TBL_PTR, TBL_STREAM_SRC, TBL_STREAM_DEST, TBL_STREAM_HDR; TBL_GROUP's index is its word:
    // GROUP[g]'s are 2g and 2g + 1). A poke stores the word's field bits alone (table_mask). The address generator and
    // the write engine read what was poked; a core's load of STREAM or GROUP reads the table store's `view`, which only
    // the core's own stores write.
    uint32_t table_peek(uint32_t table, uint32_t i) const;
    void table_poke(uint32_t table, uint32_t i, uint32_t v);
    uint32_t hist_peek(uint32_t i) const;                   // HIST
    void hist_poke(uint32_t i, uint32_t v);
    // The core's memories by byte address: DMEM (region 0x00) and IMEM (0x01), a whole word. peek_local returns the
    // word with the NoC port's write of the cycle just run already applied (the director hands it to the core a cycle
    // later).
    uint32_t peek_local(uint32_t byte_addr) const;
    void poke_local(uint32_t byte_addr, uint32_t v);
    uint32_t dmem(uint32_t word) const;                     // DMEM's word `word`

    // ---- the units' registers (back doors: the tile control unit's state, published at its next step) ----
    void set_dma_config(uint32_t v);                        // DMA_CONFIG (8.1)
    void set_receive_config(uint32_t v);                    // RECEIVE_CONFIG (12.1); a reserved digit (5-7) is fatal
    void set_gather_config(uint32_t v);                     // GATHER_CONFIG (13)
    // DMA_START's start (8.2): the start count the address generator watches, and dma_done's clear; fatal while a
    // transfer runs. (A store's STATUS.dma_busy is set by the store itself; here it follows the generator's status.)
    void dma_start();
    uint32_t remaining(unsigned ctr) const;                 // COUNTER_n (12.2), n 1-7, at the end of the cycle just run
    void set_remaining(unsigned ctr, uint32_t v);           // the count alone: it arms no event (a store does)
    void set_errors(uint32_t bits);                         // ERRORS' sticky bits [9:0] set (7.4): nmi and the error
                                                            //   line from the unit's next step
    void set_events(uint32_t bits);                         // counted events set (7.2), likewise
    const units::RegBlockState& block() const;              // the tile control unit's state (above: WHAT A READ RETURNS)
    uint32_t reg(unsigned r) const { return block().reg(r); }   // a word of the tile window it holds (regmap.h's Reg)
    uint32_t status() const { return block().reg(units::STATUS); }
    uint32_t events() const { return block().events_value(); }
    uint32_t errors() const { return block().errors_value(); }

    // ---- the tables by name ----
    void set_ptr(unsigned i, uint32_t word);                // PTR[i]
    uint32_t ptr(unsigned i) const;
    void set_stream(unsigned i, uint32_t src, uint32_t dest, uint32_t hdr);   // STREAM[i]'s three words
    void set_group(unsigned g, uint32_t w0, uint32_t w1);   // GROUP[g]'s two words

    // ---- the write unit's holding register from a test (9, 14.3) ----
    // send_put: a packet put on the inject unit's holding register as of the cycle just run (Sim::put_late), as the write
    // unit puts one; the coreless machine only (a running core's write unit owns the port). send_empty: nothing in the
    // register and nothing put for the next cycle.
    void send_put(uint64_t lo, uint64_t hdr, uint64_t hi);
    bool send_empty() const;
    uint64_t send_refusals() const;
    // the write engine's and the gather drain's logs: {t, payload [63:0], header, payload [127:64]} from now on
    void record_writer();
    std::vector<std::array<uint64_t, 4>> writer_log() const;
    void record_gather();                                   // (the drain: real_responder off)
    std::vector<std::array<uint64_t, 4>> gather_log() const;

    bool mem_idle() const;                                  // nothing on its way into the scratchpad, no write queued
    bool ts_busy() const;                                   // PTR's or HIST's pipeline or request FIFOs busy
    // A transfer and its landing are over: the address generator not running, every counter 0, no decrement on the
    // delay line, mem_idle, no NoC-port write on its way, the table stores quiet.
    bool phase_done() const;

    // ---- the request unit from a test (11.1) ----
    void req_set_w(unsigned i, uint32_t v);                 // REQUEST_OUT_W<i>, without a store
    uint32_t req_w(unsigned i) const;
    // REQUEST_SEND with word 3 as of the cycle just run (t = cycle()): the request joins the queue at once (any number
    // between cycles; one into a full queue is dropped, counted and recorded as a fact), W0-W2 clear
    void req_send(int64_t t, uint32_t w3);
    bool req_can_push(int64_t t) const;                     // the queue has a free entry at t = cycle() + 1
    bool req_idle() const;                                  // the request queue empty, a push of the cycle just run included
    // ---- the read unit (10) ----
    bool rp_idle() const;                                   // every entry free
    bool rp_all_accepted() const;                           // every result issued accepted by the director
    // ---- the write engine's request lines from a test (figure 12's PTR and HIST ports) ----
    // put(t, op, idx, add) at t = cycle() + 1: on the line to PTR (idx: mem::IDX_PTR + i) or to HIST, arriving L later
    void we_ts_req(int64_t t, uint32_t op, uint32_t idx, uint32_t add);
    void we_hist_req(int64_t t, uint32_t op, uint32_t idx, uint32_t add);

    // ---- the core ----
    uint32_t pc() const;
    uint32_t x(unsigned r) const;
    bool in_wfi() const;

private:
    Chip* c_;
    unsigned k_;
};

// ================================================================================= the master side's model (Cc)
// C&C's behavioural model (cnc.h), queued from the harness: a step queued between cycles joins the model's queue at once
// and wakes it for the next cycle. What its steps mean on v1.36's tiles is in the header (THE MASTER SIDE). With
// cc_cores there is no model.
class Cc {
public:
    explicit Cc(Chip& c) : c_(&c) {}
    // 128-bit WRITE packets of `words` to byte address `byte_addr` of dst (a tile, or BROADCAST), from id 64
    Cc& load_words(unsigned dst, uint32_t byte_addr, const std::vector<uint32_t>& words);
    Cc& send(unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi = 0);
    Cc& go();                                               // waits for every expected tile's arrival, then the release
    Cc& run_when_ready();                                   // go(), recording the release's cycle (last_run_at)
    Cc& at_all_ready(std::function<void(int64_t)> fn);      // waits for every expected tile's arrival, then fn(cycle)
    Cc& wait_cycles(uint32_t n);
    Cc& call(std::function<void(int64_t)> fn);              // fn(the cycle it ran), run between cycles right after it
    // The model waits at this step; the harness checks cond between cycles once the model has reached it, and the model
    // moves on in the next cycle. Runs go cycle by cycle while one is pending.
    Cc& wait_until(std::function<bool()> cond);
    void set_expect(uint64_t tiles);                        // the tiles whose arrival the release waits for
    // The master's request service as far as a test needs it (needs MachConfig::reqbus_host): from now on, one request
    // a cycle from target 0's queue: a REPORT (op 1: 11.5) into reports(), anything else into requests(). Runs go cycle
    // by cycle while on.
    void drain_reports(bool on);
    std::vector<std::array<int64_t, 5>> reports() const;            // {t, w0, w1, w2, w3}, in arrival order
    std::vector<std::array<int64_t, 5>> reports_of(unsigned id) const;   // a requester's reports
    std::vector<std::array<int64_t, 5>> requests() const;
    bool busy() const;                                      // steps left in its queue
    int64_t last_go_at() const, last_all_ready_at() const, last_run_at() const;
    std::vector<std::array<int64_t, 2>> phase_log() const;  // {every expected tile seen arrived, the release} per release
    size_t phases_logged() const;
    std::vector<std::array<uint64_t, 4>> received() const;  // every packet the model received: {t, payload [63:0],
                                                            //   header, payload [127:64]}
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
    int64_t cycle() const;
    void step();                                            // one cycle
    void run(uint64_t n);                                   // wake all, n cycles, scan overflows
    bool run_until(const std::function<bool()>& cond, uint64_t max);   // true if cond held (checked before each cycle)
    bool drain(uint64_t max);                               // run until quiescent
    bool quiescent() const;                                 // nothing in flight anywhere
    std::vector<int> start_skew() const;                    // the skew each tile's release link was given

    // ---- the parts ----
    TileRef tile(unsigned k) { return TileRef(*this, k); }  // 0-63; 64-66 on the chip (special_tiles)
    Cc& cc();                                               // the bench machine's model of the master side
    ReqBus& reqbus();                                       // the request tree from a test (MachConfig::reqbus_host)
    bool toast() const;                                     // the chip: the toast pin (17.6) and the cycle at whose end it
    int64_t toast_at() const;                               //   was set (-1: not set); the bench: the model's latch
    bool all_in_wfi() const;                                // every compute tile's core in a WFI

    // ---- the checker ----
    uint64_t violations() const;
    const std::vector<std::string>& violation_messages() const;   // the first max_violation_messages
    void clear_violation_messages();
    void set_check_errors(bool on);                         // from the next cycle on
    void set_max_violation_messages(unsigned n);
    void clear_violations();
    std::string violation_report();                         // (scans the overflows first)
    void scan_overflows();
    void new_phase();                                       // a new disjoint-buffer phase on every tile (BigSram)
    void finalize_checks() const;                           // every fact recorded so far processed
    std::string ag_report() const;                          // the address generators' statistics (one line)
    std::string s1_report() const;                          // item S1's measurement over the tiles (debug and suite builds)
    void reset_stats();                                     // the units' statistics to 0, every FIFO's peak to its
                                                            //   occupancy (the framework FIFOs' peaks: SIM_CHECKS only)
    std::vector<std::array<uint64_t, 4>> received(unsigned cc) const;   // the model's (0) and the endpoints' (1, 2) packets

    struct Impl;                                            // mach.cpp: the machine, its memories, the harness units
    Impl& impl() { return *impl_; }
    const Impl& impl_const() const { return *impl_; }
private:
    MachConfig cfg_;
    std::unique_ptr<Impl> impl_;
};

// =============================================================================== the machine as specified (Mach)
class Mach {
public:
    explicit Mach(const MachConfig& cfg);
    static MachConfig cfg(int64_t shuffle);                 // machine() with the shuffle seed
    static MachConfig chip_cfg(int64_t shuffle);            // MachConfig::chip() with the shuffle seed: v1.36's chip
    Chip c;

    // An image placed in a unit's memories (a back door, before the unit's core needs it: 6.5's boot_local start, or a
    // test's own data): `words` from byte address `byte_addr` of IMEM (region 0x01) or DMEM (0x00).
    void place(unsigned unit, uint32_t byte_addr, const std::vector<uint32_t>& words);
    // An ELF file's loadable segments (the images: fw/images/) placed likewise, as the RTL's chip tests place a
    // firmware (tests/rtl_images.h's memory_images, bench.rs's): IMEM and DMEM each from its base, .bss's zeros included.
    void place_elf(unsigned unit, const std::string& path);

    // ---- running and reading back ----
    bool run(const std::function<bool()>& cond, uint64_t max) { return c.run_until(cond, max); }
    bool all_in_wfi() const { return c.all_in_wfi(); }
    uint32_t word(unsigned unit, uint32_t byte_addr) const; // TileRef::peek_local

    // ---- v1.37's chip (MachConfig::host_link) ----
    // The packet file Ingress reads, the link's bytes (kept here), before the first cycle.
    void set_link(std::vector<uint8_t> bytes);
    units::IngressEgress& ingress();                        // the master tile's Ingress/Egress
    units::Updater& updater();                              // the master tile's peripheral 2
    units::tracer::FileStore& store();                      // LPDDR as files: the Updater's and the Tracer's memory system's
};

// ===================================================================== a machine fed from the harness (Net)
// Either machine (not with cc_cores): per-tile packet queues fed through each tile's read-return holding register (the
// inject unit's top priority: a packet scheduled for cycle T is taken by it at T), per-tile requests of the read
// arbiter's clients, the model's sends, and per-tile arrival logs at the director. The feeding runs between cycles before
// each cycle. The harness's ports into the inject unit and the director's log are optional parts of those units,
// declared only on a Net machine (nothing in the units' loop otherwise).
class Net {
public:
    explicit Net(const MachConfig& cfg);
    static MachConfig cfg(int64_t shuffle);                 // defaults(), check_errors off
    Chip c;
    int64_t late() const;                                   // packets taken later than scheduled (must stay 0)

    // tile's packet for injection at cycle T (nondecreasing T per tile): 64 bits on counter 1, or in full
    void at(unsigned tile, int64_t T, unsigned dst, unsigned mode, uint32_t addr, uint64_t payload);
    void at(unsigned tile, int64_t T, unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size,
            uint64_t lo, uint64_t hi);
    // a read request from client cl (0-2) of tile's read arbiter, presented from cycle T (FIFO order per client)
    void rd(unsigned tile, unsigned cl, int64_t T, uint32_t src_word, unsigned width, unsigned mode, unsigned counter,
            unsigned dst, uint32_t dst_addr);
    // the model's sends, in order (it puts one at T - 1, visible at T)
    struct CncSend { int64_t T; unsigned dst, mode; uint32_t addr; uint64_t lo; unsigned size = 1; uint64_t hi = 0; unsigned counter = 1; };
    void cnc_send(const CncSend& s);
    // the model's (cc 0) or an endpoint's (1, 2) packet put as of the cycle just run (Sim::put_late), seen by the mesh in
    // the next cycle; refused (and lost) if its holding register is full
    void send(unsigned cc, unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi);
    std::vector<std::array<uint64_t, 4>> received(unsigned cc) const;   // Chip::received
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
// The data sets (Data(seed, n) with java.util.Random, or a stream matrix from data/), the 16-group schedule, the send
// buffer at word 0 and the receive buffer at 0x40000.
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
    void load(Chip& c, int gap) const;                      // the coreless machine's buffers, tables and registers
    int verify(Chip& c) const;                              // records out of place (0: every one landed)
    static int64_t run(Chip& c, int64_t max);               // every tile's DMA_START, then until every tile is done
};

// ---- inline definitions ----
template <class T> T& Chip::unit(const std::string& name) { return sim().unit<T>(name); }
template <class T> T& TileRef::unit(const char* name) const { return c_->unit<T>(units::unit_prefix(k_) + name); }

}  // namespace mach
