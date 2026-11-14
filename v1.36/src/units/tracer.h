// tracer.h: the Tracer, the tracer tile's peripheral 1, type 3 (spec 17.9, `spec/jolt/tracer.md`). The RV64 row machine
// that executes the guest as the rows of Jolt's expanded trace and writes each window's record and streams: the region
// and the record, the control (the state write, START, words 32-51, the events and the two input credits), the
// execution, the memory side (the two caches, the write-back buffer, the fill port), the jump LRU and the map probe,
// and the output (the FIFOs, the encoders, the drain, output queues 0-3 with their credits, done).
//
// A MODEL OF THE RTL, CYCLE FOR CYCLE (v1.36). The hardware is Snarky One's `rtl/blocks/tracer_unit`; the cycle
// function (`cycle`) follows its modules register for register where a register decides a cycle, and computes the
// data (a row's values, a line's bytes, a stream's bits) functionally, at the cycle the hardware has it:
//   - the rows (tracer_rows.veryl): a row's reads, start, outcomes, access, look-up and completion; the stall causes;
//   - the templates (replayer_fetch.veryl): the decode table's entry, the ROM's words, the core-fed words, the row word;
//   - the instructions (tracer_instruction.veryl): the next pc, the early fetch, the fetch at a completion;
//   - the fetch and the data access (tracer_fetch.veryl, tracer_data.veryl) on the two caches, true LRU;
//   - the fill port's four channels and the write-back buffer (tracer_fill.veryl), the map's reads (tracer_memory.veryl);
//   - the look-up (tracer_jump_lookup.veryl); the run (tracer_run.veryl); the output (tracer_output.veryl,
//     tracer_packer.veryl, tracer_credits.veryl).
// So a run's cycles, its rows' cycles and the statistics counters are the RTL's, given the same memory system and the
// same core. What the spec does not state and the RTL decides is listed in PENDING-v1.36.md's notes and in each place.
//
// THE INTERFACE (the peripheral method list, unchanged from v1.35): a member object of the slot, which calls these
// methods through a switch on the peripheral's type. Inputs (accept, run, write_word) take effect at the end of their
// cycle, a region packet at the end of t + RP_LATENCY (the cycle the write port presents it); level reads (running,
// errors, word, the events' value) return the end of t - 1. Inputs are applied lazily, on the first read or step of a
// later cycle (settle), so the Tracer is exact whatever order the slot calls in.
//
// THE MEMORY SYSTEM is behind the fill port (17.9.5, 17.9.14): a MemSystem over a FileStore, answering each request
// some cycles after its handshake: its own model's latencies, or fixed ones (`fill_latency`), as the RTL's tests'.
//
// ERRORS AND FLAGS. ERRORS bits stop the Tracer from the cycle after (17.9.9); each is also recorded as a fixed-size
// fact (cycle, bits, cause, two values) in `error_log`. meshsim's own flags (`violate`) are facts in `violations`.
// Undefined behaviour is FATAL.
#pragma once

#include <cstdint>
#include <memory>

#include "units/event_counts.h"
#include "units/file_store.h"
#include "units/lru_coder.h"
#include "units/lru_index.h"
#include "units/mem_system.h"
#include "units/peripheral.h"
#include "units/replayer_alu.h"
#include "units/row_core.h"
#include "units/rvc.h"
#include "units/stream_word.h"

namespace units {
namespace tracer {

class Tracer : public PeripheralBase<Tracer> {
public:
    // ================================================================ the window (17.9.1)
    static constexpr int TYPE = 3;
    static constexpr int RP_LATENCY = 2;  // a region packet accepted at t is presented by the write port at t + 2
    enum : int {
        W_RUN_CONFIG = 32, W_ROWS_RUN = 33, W_EXP_OFFSET = 34, W_EXP_OPCODE = 35, W_STATE_LENGTH = 36,
        W_CODE_LENGTH = 37, W_RLOG_LENGTH = 38, W_WLOG_LENGTH = 39, W_HASH_MUL = 40, W_HASH_STEP = 41,
        W_HASH_LINES = 42, W_EXP_TEMPLATE = 43, W_STAT = 44, STAT_N = 8
    };
    // the statistics counters, words 44-51 (17.9.8), in word order
    enum : int { ST_CYCLES = 0, ST_STALL_FETCH, ST_STALL_DATA, ST_STALL_MAP, ST_STALL_OUTPUT, ST_MISS_I, ST_MISS_D, ST_PROBES };
    static constexpr uint32_t RC_ROWS = 0xFFFFFF, RC_COMP_CODE = 1u << 26, RC_COMP_RLOG = 1u << 27,
                              RC_COMP_WLOG = 1u << 28, RC_DEFINED = 0x1FFFFFFF;
    static constexpr int MAX_ROWS = 65536;
    // EVENTS, packed from bit 0 (17.9.1, item 161). Bits 0 and 1 are the control block's standard ones (17.1): done is
    // posted here, dma_done by the peripheral's DMA (the slot's).
    enum : unsigned { EV_RUN_DONE = 0, EV_DMA_DONE = 1, EV_ADVICE = 2, EV_EXP = 3, EV_NEWEXP = 4, EV_SVC_STATE = 5,
                      EV_SVC_CODE = 6, EV_SVC_RLOG = 7, EV_SVC_WLOG = 8, EV_BITS = 9 };
    // ERRORS, packed from bit 0 (17.9.1)
    enum : int { ERR_OVERRUN = 0, ERR_INVALID = 1, ERR_ADVICE_UNDER = 2, ERR_MAP = 3, ERR_STORE = 4, ERR_PAGE = 5,
                 ERR_RANGE = 6, ERR_ASSERT = 7, ERR_MISALIGNED = 8, ERR_WRITE_BACK = 9, ERR_BITS = 10 };
    // the record (17.8.2, 17.9.2)
    static constexpr int REC_WORDS = 68, REC_FILES = 66, REC_BITMAP = 67;
    // tags in the log FIFO (17.9.6)
    enum : uint8_t { TAG_LOAD = 0, TAG_OTHER = 1, TAG_WRITE = 2 };
    static constexpr int QCAP = 256, QHALF = 128, CACHE_LINES = 128, LRU_ENTRIES = 64;
    static constexpr int PROBE_LINES = 3;   // MAP_PROBE_LINES (17.9.4)
    // The waits on the core, which no counter counts (17.9.8): [S_ADVICE] an advice row's empty read queue, [S_EXP] a
    // core-fed expansion's words. Simulator's statistics.
    enum : int { S_ADVICE = 0, S_EXP = 1, S_N };

    // ================================================================ the fill port's latencies (17.9.14)
    // 0: the memory system's own model times the request (MemSystem's walk, hit and miss). Above 0: a read is
    // answered that many cycles after its request is taken, [0] the memory file's, [1] the map's, and a write a cycle
    // after its request is taken: the RTL's tests' model of the memory system.
    int fill_latency[2] = {0, 0};

    // ================================================================ facts
    // Why an ERRORS bit was set.
    enum : int {
        E_RQ_FULL = 1, E_EQ_FULL, E_CLASS3, E_NO_32BIT_FORM, E_ROM_PAST_511, E_INVALID_OPCODE, E_INVALID_OPERAND,
        E_INVALID_FORMAT, E_ADVICE_ZERO, E_MEM_RANGE_ALIGN, E_STORE_NOT_WRITABLE, E_ASSERT, E_PC0, E_IFETCH_BEYOND,
        E_DATA_BEYOND, E_MAP_OFFSET, E_MAP_HOLE, E_MAP_BEYOND, E_MAP_FREE, E_MAP_FULL, E_WRITE_BACK
    };
    // meshsim's flags (violate).
    enum : int { V_ADVICE_UNCONSUMED = 1, V_UNDEFINED, V_END_NO_NFV, V_XFER_NO_NFV, V_INLINE_NO_TEMPLATE };
    static void violation_text(int code, uint64_t a, uint64_t b, char* o, size_t n);   // the checker's text
    struct Fact {
        int64_t t = 0;
        int code = 0, bits = 0;
        uint64_t a = 0, b = 0;
    };
    static constexpr int FACTS = 64;  // the first FACTS of each kind are kept; the counts go on
    Fact error_log[FACTS], violations[FACTS];
    int64_t n_errors_logged = 0, n_violations = 0;
    static const char* error_name(int code);
    static const char* violation_name(int code);

    // ================================================================ construction (configuration)
    // me: the controller's endpoint (ref's `me`, for messages). icache_lines: 4.21.5's 128, a parameter for B1's sweep.
    // seen_capacity: the instruction lines a segment may touch (the cold/capacity breakdown of the misses; FATAL beyond).
    explicit Tracer(int me, int icache_lines = CACHE_LINES, int seen_capacity = 1 << 17);
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;
    ~Tracer();

    // The memory. In the chip (the slot creates the Tracer bare, as `ref`'s Config does) the harness hands it its
    // memory: own_memory(store) keeps the store and a memory system over it, and makes the memory cold at every
    // state-word landing (T2's onStateWrite). Tests and tools may instead point `mem` at their own.
    void own_memory(std::unique_ptr<FileStore> store) {
        own_store_ = std::move(store);
        own_mem_ = std::make_unique<MemSystem>(own_store_.get());
        mem = own_mem_.get();
        map_mem = nullptr;
        cold_mem_on_state_write = true;
    }
    FileStore* store() { return mem ? mem->store : nullptr; }
    MemSystem* mem = nullptr;      // the fill port's other side (17.9.5, 17.9.14)
    MemSystem* map_mem = nullptr;  // the map probe's, if a harness separates them (B1); else mem
    bool cold_mem_on_state_write = false;  // the word-0 write makes the memory system cold (the harness's choice)

    // ================================================================ the peripheral interface (the list)
    static_assert(TYPE == PT_TRACER, "the Tracer's type tag");
    static constexpr int DIGEST_N = STAT_N;  // the peripheral.h: digest_words' length
    int params() const { return TYPE; }
    int type() const { return TYPE; }
    uint64_t violation_count() const { return uint64_t(n_violations); }  // the slot forwards new ones
    EventCounts& events() { return ev; }
    // A consuming load presented in cycle t: `consume_events` when it is applied before step(t) (the block bench, the
    // rig), `consume_events_late` when after it (the slot): a set of t then survives the load either way (17.2).
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    uint32_t consume_events_late(int64_t t, uint32_t bits) { return ev.consume_late(t, bits); }
    // accept(t): the write port presents the packet in cycle t + 2, and it lands at that cycle's end. run and write_word
    // for a store presented in cycle t: in effect at the end of t.
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi);
    void run(int64_t t);
    bool running(int64_t t) { settle(t); return s_running_; }
    void sync(int64_t t) { settle(t); }
    int errors(int64_t t) { settle(t); return s_errors_; }
    uint32_t status_bits(int64_t) const { return 0; }
    bool word(int64_t t, int w, uint32_t& v);           // false: an undefined word
    int write_word(int64_t t, int w, uint32_t v);       // 1 written, 0 read-only (a bus error), -1 undefined
    bool writable(int w) const {
        return w == W_RUN_CONFIG || w == W_HASH_MUL || w == W_HASH_STEP || w == W_HASH_LINES || w == W_EXP_TEMPLATE;
    }
    bool store_stalls(int64_t, int) const { return false; }
    void reset(int64_t t);
    void step(int64_t T);                               // one clock cycle
    bool idle(int64_t) const { return false; }
    // The wake rule: whether step(t) can change anything. False only when no run is in progress, nothing of the
    // pipeline or the fill port is in flight, and no input or landing is pending; then skipping step(t) is exact.
    bool must_step() const { return running_ || !quiet() || inputs_.n > 0 || landing_.n > 0; }
    int64_t next_wake_at(int64_t) const { return INT64_MAX; }
    void skip_cycles(int64_t n) const;
    void digest_words(int64_t out[DIGEST_N]) const;     // the counters, words 44-51
    // The DMA's side (17.4, 17.9.6): output queue q's head is there at the start of cycle t (out_ready), the DMA takes
    // it in t (out_take: one pop a packet), a transfer of `words` entries starts with DMA_START presented in t
    // (out_start). The DMA's busy, which the run's end waits for, is kept here from these: up from the start's next
    // cycle through the cycle after its last take (peripheral_dma.veryl).
    int out_queues() const { return 4; }
    bool out_ready(int64_t t, int q);
    void out_take(int64_t t, int q, uint64_t w[2]);
    void out_start(int64_t t, int q, int64_t words);

    // ================================================================ for harnesses
    uint64_t rec[REC_WORDS * 2] = {};  // the record, lo and hi per word: [0] state, [1] stream, [2-65] registers, ...
    uint16_t decode_table[2048] = {};
    uint64_t rom_lo[512] = {}, rom_hi[512] = {};
    bool stopped() const { return stopped_; }
    bool rows_ended(int64_t t) { settle(t); return rows_ended_; }
    int cache_lines(bool data) const { return data ? dcache_.size() : icache_.size(); }
    uint32_t advice_left() const { return uint32_t((rec[3] >> 38) & 0x7FFFFF); }
    int64_t lowest() const { return 0x80000000LL - int64_t((rec[2 * REC_FILES + 1] >> 8) & 0x7FFFFFFF); }
    // meshsim only (the one-memory acceptance): the data cache's dirty lines and the write-back buffer's line written
    // back into the memory, untimed.
    int write_back_dirty_untimed();
    // the files word for (memory root, depth, map root, depth, log2 K, lowest_address): a tool's helper
    static void files_word(int32_t mem_root, int mem_depth, int32_t map_root, int map_depth, int log2k, int64_t lowest,
                           uint64_t out[2]);
    // A row's trace, for a harness (null: none): called at each row's start and completion with the cycle.
    void (*on_row)(void* ctx, int64_t t, bool start) = nullptr;
    void* on_row_ctx = nullptr;

    // The observation a conformance test compares (tests/test_tracer.cpp): the control's state, the statistics,
    // stat[8], the waits on the core, oq[1..3]'s entries and pushes, the two fact counts, the record's hash, the events.
    static constexpr int OBS_N = 16 + 10 + 8 + S_N + 6 + 2 + 1 + int(EV_BITS);
    void observe(int64_t out[OBS_N]) const;

    // The canonical fingerprint: unit 0 `tr`, 1 `tr.ic`, 2 `tr.dc`, 3 `tr.mem`, 4 `tr.files`, at the end of cycle c.
    // Out: FpOut (tests/statefp.h) or any sink with w(name, v) and wi(name, i, v).
    static constexpr int FP_UNITS = 5;
    static constexpr const char* FP_NAMES[FP_UNITS] = {"tr", "tr.ic", "tr.dc", "tr.mem", "tr.files"};
    // In a chip, what is still in the slot's links at the end of cycle c: the region packets the director accepted at
    // c - 2 .. c, the core's START or window store of c, a DMA_START of c, a consuming EVENTS load of c.
    // PeriphSlot::fingerprint_tracer builds it; null for a Tracer driven directly.
    static constexpr int FP_IN_RUN = 1, FP_IN_WORD = 2;   // FpPending::In::kind
    struct FpPending {
        unsigned n_land = 0, n_in = 0;
        struct Land { int64_t e; int src; uint32_t offset; int size; uint64_t lo, hi; } land[4];
        struct In { int64_t e; int kind, word; uint32_t value; } in[2];
        bool start = false;
        int start_q = 0;
        int64_t start_words = 0;
        uint32_t consume = 0;
    };
    template <class Out> void fingerprint(int unit, Out& o, int64_t c, const FpPending* pending = nullptr) const;

    // ---- the FIFOs (17.9.6)
    uint32_t* code_fifo = nullptr;  // MAX_ROWS each
    uint64_t* log_fifo = nullptr;
    uint8_t* log_tag = nullptr;
    int code_n = 0, log_n = 0;

    // ---- output queues 1-3 (17.9.6), each with its credits (tracer_credits.veryl)
    struct OutQueue {
        uint64_t lo[QCAP], hi[QCAP];
        int head = 0, occ = 0;
        int64_t pushed = 0, read = 0;
        int credits = 0;                      // 0-2: the _ready event is up while not 0
        int pushed_part = 0, popped_part = 0; // entries since the last credit added, and since the last taken, mod 128
        bool ended = false;                   // the stream has ended
        int64_t ended_at = INT64_MIN;         // ... at this cycle's edge
        int64_t push_t = INT64_MIN, pop_t = INT64_MIN;  // the last push's and pop's cycles (one of each a cycle at most)
        int64_t stall_cycles = 0, max_occ = 0;
        void reset() {
            head = occ = 0; pushed = read = 0; credits = pushed_part = popped_part = 0; ended = false;
            ended_at = push_t = pop_t = INT64_MIN;
        }
        // the entries at the start of cycle t, whichever of t's push and pop are already made
        int count(int64_t t) const { return occ + (pop_t == t ? 1 : 0) - (push_t == t ? 1 : 0); }
        void push(int64_t t, uint64_t l, uint64_t h);
        void end(int64_t t);
        void take(int64_t t, uint64_t w[2]);
    };
    OutQueue oq[4];  // [1..3]; [0] unused (queue 0 is part 0 read in place)

    // ================================================================ statistics (the simulator's)
    int64_t stalls[S_N] = {};
    int64_t rows = 0, rows_m = 0, rows_pad = 0, instrs = 0, run_cycles = 0, runs = 0;
    int64_t redirects = 0;            // instructions whose successor was fetched at their completion
    int64_t i_access = 0, i_miss = 0, d_access = 0, d_miss = 0, writebacks = 0, take_backs = 0, lru_hits = 0,
            lru_misses = 0, probe_reads = 0;
    int64_t i_miss_by[4] = {};        // [cold first, cold second, capacity first, capacity second]
    int64_t straddles = 0;
    int64_t run_start_at = 0, rows_end_at = 0, drained_at = 0;
    int64_t state_writes = 0, new_expansions = 0, drain_cycles = 0, scan_stalls = 0;
    uint32_t stat[STAT_N] = {};  // words 44-51: 32 bits, wrapping

    EventCounts ev;

private:
    struct Input {
        int64_t t;
        int kind, a, b;
        uint64_t lo, hi;
    };
    static constexpr int IN_RUN = 1, IN_WORD = 2, INPUTS = 64;
    struct Ring {  // a small FIFO of inputs, fixed size
        Input e[INPUTS];
        int head = 0, n = 0;
        void clear() { head = n = 0; }
        void push(const Input& x);
        const Input& peek() const { return e[head]; }
        Input pop() { Input x = e[head]; head = (head + 1) % INPUTS; n--; return x; }
    };

    // ---- record accessors
    uint64_t reg_value(int r) const { return r == 0 ? 0 : rec[4 + r]; }
    uint64_t pc() const { return rec[0]; }
    int32_t instr_idx() const { return int32_t(uint32_t(rec[1])); }
    int state_offset() const { return int(uint32_t(rec[1] >> 32) & 0xFFFF); }
    void set_advice_left(uint32_t n) { rec[3] = (rec[3] & ~(uint64_t(0x7FFFFF) << 38)) | uint64_t(n & 0x7FFFFF) << 38; }
    int32_t mem_root() const { return int32_t(rec[2 * REC_FILES] & 0x7FFFFFFF); }
    int mem_depth() const { return int(uint32_t(rec[2 * REC_FILES] >> 31) & 3); }
    int32_t map_root() const { return int32_t(uint32_t(rec[2 * REC_FILES] >> 33) & 0x7FFFFFFF); }
    int map_depth() const { return int(rec[2 * REC_FILES + 1] & 3); }
    int log2k() const { return int(uint32_t(rec[2 * REC_FILES + 1] >> 2) & 63); }

    void violate(int64_t t, int code, uint64_t a = 0, uint64_t b = 0);
    void error(int64_t t, int bits, int code, uint64_t a = 0, uint64_t b = 0);
    [[noreturn]] void ub(int64_t t, const char* what, uint64_t a = 0, uint64_t b = 0) const;

    void settle(int64_t T);
    void store_word(int64_t t, int w, uint32_t v);
    void land(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi);
    void state_write(int64_t t);
    void do_run(int64_t t);
    void cycle(int64_t T);
    void drain_cycle(int64_t T);
    bool quiet() const;
    bool state_write_lands_at(int64_t T) const;
    void head_decode();
    void row_start(int64_t T);
    void row_complete(int64_t T, uint64_t data_load, uint32_t target_base);
    void push_code(uint32_t w);
    void push_log(uint8_t tag, uint64_t v);
    static streamword::Size choose(int64_t values, int64_t raw_words, int64_t coded_bits, bool enabled);
    static bool assertion_holds(unsigned op, uint64_t a, uint64_t b);

    const int me_;
    std::unique_ptr<FileStore> own_store_;
    std::unique_ptr<MemSystem> own_mem_;

    // ---- registers (17.9.1)
    uint32_t run_config_ = 0, hash_mul_ = 0, hash_step_ = 0, hash_lines_ = 0;
    uint32_t length_code_ = 0, length_rlog_ = 0, length_wlog_ = 0;
    bool lengths_valid_ = false;
    uint32_t presented_ = 0;      // EXPANSION_OPCODE
    int expansion_offset_ = 0;    // EXPANSION_OFFSET, latched at the post
    int latch_ = -1;
    bool template_stored_ = false;
    uint64_t bitmap_[2] = {};

    // ---- the input queues (17.9.3): replayer_input_queue.veryl, tracer_read_queue.veryl
    uint64_t rq_lo_[QCAP] = {}, rq_hi_[QCAP] = {};
    int rq_head_ = 0, rq_occ_ = 0, rq_half_ = 0, rq_popped_ = 0;
    uint64_t eq_lo_[QCAP] = {}, eq_hi_[QCAP] = {};
    int eq_head_ = 0, eq_occ_ = 0, eq_popped_ = 0;
    int64_t rq_pop_t_ = INT64_MIN, eq_pop_t_ = INT64_MIN;   // the last pop's cycle: a push is full by the count before it

    // ---- the caches (17.9.5) and the jump LRU: keys in access order, values by slot
    struct Line {
        uint64_t d[16];
        bool dirty, writable;
    };
    int icache_lines_;
    LruIndex icache_, dcache_, jump_lru_, i_seen_;
    Line* iline_ = nullptr;  // icache_lines_ + 1
    Line* dline_ = nullptr;  // CACHE_LINES + 1
    int32_t jump_base_[LRU_ENTRIES + 1] = {};

    // ---- the run (tracer_run.veryl)
    bool running_ = false;       // STATUS.busy
    bool stopped_ = false;       // a stopping error, until the reset
    bool stopping_ = false;      // ... raised in this cycle: stopped from the next
    bool started_ = false;       // the cycle after START
    bool rows_ended_ = false;    // from the cycle after the rows' end to the next START
    bool row_done_reg_ = false;  // row_done, a cycle late (tracer_unit.veryl)
    bool state_written_ = false; // a state write since the reset (17.9.3: a START needs one)
    int rows_left_ = 0, rows_done_ = 0;
    int errors_ = 0;

    // ---- the rows (tracer_rows.veryl)
    enum : int { NK_HOLD = 0, NK_STOP, NK_TRANSFER, NK_SEQUENTIAL };
    struct Rows {
        bool reads_made = false, reads_advice = false, in_flight = false, after_done = false, data_answered = false,
             lookup_taken = false, run_first = false, wb_pending = false, waits_on_core = false;
        int stage = 0;
        int64_t alu_done_at = INT64_MIN;
        // the row in flight, from its start
        bool memory = false, store = false, advice = false, end = false, last = false, first = false, writes = false,
             assert_failed = false, padding = false, mul = false, inline_row = false;
        unsigned op = 0;
        int rd = 0, index = 0, next_kind = NK_SEQUENTIAL;
        uint32_t code = 0;
        uint64_t rs2 = 0, advice_value = 0, mem_addr = 0, offset = 0, next_pc = 0, result = 0;
    } rows_;
    // the row word's decode, of the word the templates hold (head_decode)
    struct Head {
        bool invalid = false, advice = false;
        int invalid_code = 0;
    } hd_;

    // ---- the templates (replayer_fetch.veryl)
    enum : int { TK_PADDING = 0, TK_ROM, TK_NOP, TK_CORE, TK_BAD };
    struct Templates {
        bool first = true, entry_pending = false, active = false, policy_known = false, temporary_offset = false,
             nop_virtual = false, rom_pending_word = false, rom_pending_policy = false, rom_pending_padding = false,
             rom_pending_nop = false;
        uint32_t instruction = 0;
        int kind = TK_PADDING, start = 0, index = 0, rom_pending_index = 0, rom_address = 0, no_form = 0, cls = 0;
        // the row word
        bool head_valid = false, head_temporary_offset = false, head_padding = false, head_nop = false,
             head_nop_virtual = false, head_invalid = false, head_inline = false;
        uint64_t head_lo = 0, head_hi = 0;
        uint32_t head_code = 0;
        int head_index = 0, head_invalid_code = 0;
        bool core_fed = false;   // tracer_execution.veryl: the instruction held is core-fed
    } tpl_;

    // ---- the instructions (tracer_instruction.veryl)
    struct Instructions {
        bool known = false, requesting = false, held = false, held_invalid = false, held_padding = false,
             held_wide = false, current_padding = false;
        bool early[2] = {false, false};
        int ended = 0;
        uint64_t next_pc = 0;
        uint32_t held_code = 0;
    } ins_;

    // ---- the fetch (tracer_fetch.veryl)
    enum : int { XF_IDLE = 0, XF_LOOK_UP, XF_READ_LINE, XF_READ, XF_REQUEST, XF_WAIT_FILL, XF_FAILED };
    struct Fetch {
        int state = XF_IDLE, slot = -1, parcel_index = 0;
        bool second = false, next_line_beyond = false, failed_beyond = false;
        uint64_t line = 0;
        uint32_t first_parcel = 0;
    } xf_;

    // ---- the data access (tracer_data.veryl)
    enum : int { XD_IDLE = 0, XD_VICTIM, XD_MISS, XD_EVICT, XD_SWAP, XD_REQUEST, XD_WAIT_FILL, XD_READ_LINE, XD_ACCESS,
                 XD_FAILED };
    struct Data {
        int state = XD_IDLE, slot = -1, doubleword = 0;
        bool victim_dirty = false, taking_back = false, range_failed = false, alignment_failed = false;
        uint64_t line = 0, victim_line = 0;
    } xd_;

    // ---- the fill port and the write-back buffer (tracer_fill.veryl; the map's reads, tracer_memory.veryl)
    enum : int { OWNER_DATA = 0, OWNER_MAP, OWNER_FETCH };
    struct Fill {
        bool read_request_valid = false, read_outstanding = false;
        int read_owner = OWNER_DATA, read_depth = 0;
        int32_t read_root = 0;
        int64_t read_line = 0, read_response_at = INT64_MIN;
        MemSystem::Response response;
        bool write_held = false, write_requested = false, write_request_valid = false, write_line_kept = true,
             write_outstanding = false;
        int write_depth = 0, write_status = 0;
        int32_t write_root = 0;
        uint64_t write_line = 0, write_data[16] = {};
        int64_t write_response_at = INT64_MIN;
        bool map_pending = false, map_line_valid = false;
        uint64_t map_line_data[16] = {};
        int map_line_status = 0;
    } fill_;

    // ---- the look-up (tracer_jump_lookup.veryl)
    enum : int { XJ_IDLE = 0, XJ_LOOKUP, XJ_HOME, XJ_READ, XJ_WAIT_LINE, XJ_DECIDE };
    struct Jump {
        int state = XJ_IDLE, lines_read = 0;
        bool too_far = false, lru_hit = false, compare_hit = false, compare_free = false, compare_missing = false,
             probe_answer = false, probe_found_answer = false, probe_missing_answer = false;
        uint32_t key = 0, lru_base = 0, compare_base = 0;
        int64_t line = 0;
    } xj_;

    // ---- set 1 (17.9.6)
    LruCoder set1_code_{false}, set1_rlog_{false}, set1_wlog_{true};
    int rlog_values_ = 0, wlog_values_ = 0;

    // ---- inputs, deferred to the end of their cycle
    Ring inputs_, landing_;
    int64_t settled_ = INT64_MIN, last_accept_ = INT64_MIN;

    // ---- level reads: the snapshot at the end of t - 1
    uint32_t s_stat_[STAT_N] = {};
    bool s_running_ = false;
    int s_errors_ = 0, s_rows_ = 0, s_exp_offset_ = 0;
    uint32_t s_exp_opcode_ = 0, s_run_config_ = 0, s_code_l_ = 0, s_rlog_l_ = 0, s_wlog_l_ = 0, s_mul_ = 0, s_step_ = 0,
             s_lines_ = 0;

    // ---- the output path (tracer_output.veryl)
    struct Packer {   // tracer_packer.veryl: the token register; the buffer is `bits`
        bool held_valid = false, ended = false;
        uint64_t held_lo = 0, held_hi = 0;
        int held_length = 0;
        Bits bits;
        void reset() { held_valid = ended = false; bits.reset(); }
    };
    bool run_active_ = false, end_in_ = false, choosing_ = false, draining_ = false, done_ = false;
    int q0ptr_ = REC_WORDS;      // output queue 0's pointer
    bool q0_holding_ = false;    // ... and whether it still has words to read (state_valid, state_ready)
    int64_t q0_pop_t_ = INT64_MIN, q0_since_ = INT64_MAX;
    int code_next_ = 0, log_next_ = 0, code_held_ = 0, log_held_ = 0, rlog_given_ = 0, wlog_given_ = 0;
    bool code_data_valid_ = false, log_data_valid_ = false;
    LruCoder set2_code_{false}, set2_rlog_{false}, set2_wlog_{true};
    Packer pk_[4];               // [1] code, [2] the read log, [3] the write log
    bool comp_[4] = {};          // the choice: stream q is LRU-coded
    int64_t xfer_queue_ = -1, xfer_left_ = 0;
    int64_t dma_busy_from_ = INT64_MAX, dma_last_take_ = INT64_MIN;   // the DMA's busy (peripheral_dma.veryl)
    bool dma_busy(int64_t T) const { return T >= dma_busy_from_ && (xfer_left_ > 0 || T <= dma_last_take_ + 1); }
};

template <class Out>
void Tracer::fingerprint(int unit, Out& o, int64_t c, const FpPending* pending) const {
    auto mx = [c](int64_t v) { return uint64_t(v > c ? v : c); };
    auto cache = [&o](const LruIndex& ix, const Line* lines) {
        o.w("lines", uint64_t(ix.size()));
        for (int s = ix.eldest(); s >= 0; s = ix.next(s)) {
            o.w("line", ix.key(s));
            for (int k = 0; k < 16; k++) o.wi("d", unsigned(k), lines[s].d[k]);
            o.w("dirty", lines[s].dirty ? 1 : 0); o.w("writable", lines[s].writable ? 1 : 0);
        }
    };
    auto coder = [&o](const LruCoder& k) { for (int i = 0; i < 16; i++) o.wi("e", unsigned(i), k.table()[i]); };
    switch (unit) {
        case 1: cache(icache_, iline_); return;
        case 2: cache(dcache_, dline_); return;
        case 3: mem->fingerprint(o, c); return;
        case 4: o.w("nextFree", uint64_t(mem->store->next_free())); o.w("files", mem->store->files_sum()); return;
        default: break;
    }
    // 1. registers
    const uint32_t regs[7] = {run_config_, hash_mul_, hash_step_, hash_lines_, length_code_, length_rlog_, length_wlog_};
    for (uint32_t v : regs) o.w("reg", uint64_t(int64_t(int32_t(v))));
    // 2. the record
    for (int k = 0; k < REC_WORDS * 2; k++) o.wi("rec", unsigned(k), rec[k]);
    // 3. the decode table and the ROM, as memory sums
    uint64_t sd = 0, sr = 0;
    for (int k = 0; k < 2048; k++) if (decode_table[k]) sd += FileStore::term64(12, uint64_t(k), decode_table[k]);
    o.w("decodeTable", sd);
    for (int j = 0; j < 512; j++) {
        if (rom_lo[j]) sr += FileStore::term64(13, 2 * uint64_t(j), rom_lo[j]);
        if (rom_hi[j]) sr += FileStore::term64(13, 2 * uint64_t(j) + 1, rom_hi[j]);
    }
    o.w("rom", sr);
    // 4. the input queues
    o.w("rq.occ", uint64_t(rq_occ_)); o.w("rqHalf", uint64_t(rq_half_)); o.w("rqPopped", uint64_t(rq_popped_));
    for (int k = 0; k < rq_occ_; k++) { const int i = (rq_head_ + k) % QCAP; o.w("rq.lo", rq_lo_[i]); o.w("rq.hi", rq_hi_[i]); }
    o.w("eq.occ", uint64_t(eq_occ_)); o.w("eqPopped", uint64_t(eq_popped_));
    for (int k = 0; k < eq_occ_; k++) { const int i = (eq_head_ + k) % QCAP; o.w("eq.lo", eq_lo_[i]); o.w("eq.hi", eq_hi_[i]); }
    // 5. the run and the pipeline's control state
    o.w("running", running_); o.w("stopped", stopped_); o.w("rowsEnded", rows_ended_); o.w("done", done_);
    o.w("rowsLeft", uint64_t(rows_left_)); o.w("rowsDone", uint64_t(rows_done_));
    o.w("presented", uint64_t(int64_t(int32_t(presented_)))); o.w("expansionOffset", uint64_t(int64_t(expansion_offset_)));
    o.w("latch", uint64_t(int64_t(latch_))); o.w("templateStored", template_stored_);
    o.w("bitmap0", bitmap_[0]); o.w("bitmap1", bitmap_[1]);
    o.w("errors", uint64_t(errors_));
    o.w("row.inFlight", rows_.in_flight); o.w("row.stage", uint64_t(rows_.stage)); o.w("row.readsMade", rows_.reads_made);
    o.w("row.aluDoneAt", mx(rows_.alu_done_at));
    o.w("tpl.active", tpl_.active); o.w("tpl.entryPending", tpl_.entry_pending); o.w("tpl.kind", uint64_t(tpl_.kind));
    o.w("tpl.index", uint64_t(tpl_.index)); o.w("tpl.headValid", tpl_.head_valid); o.w("tpl.coreFed", tpl_.core_fed);
    o.w("ins.known", ins_.known); o.w("ins.requesting", ins_.requesting); o.w("ins.held", ins_.held);
    o.w("ins.nextPc", ins_.next_pc); o.w("ins.ended", uint64_t(ins_.ended));
    o.w("fetch.state", uint64_t(xf_.state)); o.w("data.state", uint64_t(xd_.state)); o.w("jump.state", uint64_t(xj_.state));
    o.w("fill.read", uint64_t(fill_.read_request_valid) | uint64_t(fill_.read_outstanding) << 1);
    o.w("fill.write", uint64_t(fill_.write_held) | uint64_t(fill_.write_requested) << 1 |
                          uint64_t(fill_.write_request_valid) << 2 | uint64_t(fill_.write_line_kept) << 3);
    o.w("fill.writeLine", fill_.write_held ? fill_.write_line : 0);
    // 6. the events' counts (with a consume of c still in the slot's link applied)
    if (pending && pending->consume) {
        EventCounts after = ev;
        after.consume(pending->consume);
        for (unsigned b = 0; b < EV_BITS; b++) o.wi("ev", b, after.count_of(b));
    } else {
        for (unsigned b = 0; b < EV_BITS; b++) o.wi("ev", b, ev.count_of(b));
    }
    // 7. the statistics counters
    for (int k = 0; k < STAT_N; k++) o.wi("stat", unsigned(k), stat[k]);
    // 8. the jump LRU, eldest to newest
    o.w("lru.n", uint64_t(jump_lru_.size()));
    for (int s = jump_lru_.eldest(); s >= 0; s = jump_lru_.next(s)) { o.w("lru.key", jump_lru_.key(s)); o.w("lru.base", uint64_t(uint32_t(jump_base_[s]))); }
    // 9. the FIFOs and set 1
    o.w("codeN", uint64_t(code_n));
    for (int k = 0; k < code_n; k++) o.w("code", code_fifo[k]);
    o.w("logN", uint64_t(log_n));
    for (int k = 0; k < log_n; k++) { o.w("log", log_fifo[k]); o.w("tag", log_tag[k]); }
    o.w("rlogValues", uint64_t(rlog_values_)); o.w("wlogValues", uint64_t(wlog_values_));
    for (const LruCoder* k : {&set1_code_, &set1_rlog_, &set1_wlog_}) { coder(*k); o.w("bits", uint64_t(k->bits)); }
    // 10. the output path
    o.w("q0ptr", uint64_t(q0ptr_)); o.w("q0holding", q0_holding_); o.w("draining", draining_);
    o.w("codeNext", uint64_t(code_next_)); o.w("logNext", uint64_t(log_next_));
    const bool st = pending && pending->start;   // a DMA_START of c
    o.w("xferQueue", uint64_t(st ? int64_t(pending->start_q) : xfer_queue_));
    o.w("xferLeft", uint64_t(st ? pending->start_words : xfer_left_));
    for (const LruCoder* k : {&set2_code_, &set2_rlog_, &set2_wlog_}) coder(*k);
    for (int q = 1; q <= 3; q++) {
        const Packer& p = pk_[q];
        o.w("pk.lo", p.bits.lo); o.w("pk.hi", p.bits.hi); o.w("pk.fill", uint64_t(p.bits.fill)); o.w("pk.words", uint64_t(p.bits.words));
        o.w("pk.held", p.held_valid); o.w("pk.ended", p.ended);
        const OutQueue& x = oq[q];
        o.w("oq.occ", uint64_t(x.occ));
        for (int k = 0; k < x.occ; k++) { const int i = (x.head + k) % QCAP; o.w("oq.lo", x.lo[i]); o.w("oq.hi", x.hi[i]); }
        o.w("oq.pushed", uint64_t(x.pushed)); o.w("oq.read", uint64_t(x.read)); o.w("oq.credits", uint64_t(x.credits));
    }
    // 11. inputs not yet in effect
    const unsigned pl = pending ? pending->n_land : 0, pi = pending ? pending->n_in : 0;
    o.w("landing.n", uint64_t(landing_.n) + pl);
    for (int k = 0; k < landing_.n; k++) {
        const Input& x = landing_.e[(landing_.head + k) % INPUTS];
        o.w("e", uint64_t(x.t)); o.w("src", uint64_t(int64_t(x.kind))); o.w("offset", uint64_t(int64_t(x.a)));
        o.w("size", uint64_t(int64_t(x.b))); o.w("lo", x.lo); o.w("hi", x.hi);
    }
    for (unsigned k = 0; k < pl; k++) {
        const FpPending::Land& x = pending->land[k];
        o.w("e", uint64_t(x.e)); o.w("src", uint64_t(int64_t(x.src))); o.w("offset", uint64_t(int64_t(int32_t(x.offset))));
        o.w("size", uint64_t(int64_t(x.size))); o.w("lo", x.lo); o.w("hi", x.hi);
    }
    o.w("inputs.n", uint64_t(inputs_.n) + pi);
    for (int k = 0; k < inputs_.n; k++) {
        const Input& x = inputs_.e[(inputs_.head + k) % INPUTS];
        o.w("e", uint64_t(x.t)); o.w("kind", uint64_t(int64_t(x.kind))); o.w("word", uint64_t(int64_t(x.a)));
        o.w("value", uint64_t(uint32_t(x.b)));
    }
    for (unsigned k = 0; k < pi; k++) {
        const FpPending::In& x = pending->in[k];
        o.w("e", uint64_t(x.e)); o.w("kind", uint64_t(int64_t(x.kind))); o.w("word", uint64_t(int64_t(x.word)));
        o.w("value", uint64_t(x.value));
    }
}

}  // namespace tracer
}  // namespace units
