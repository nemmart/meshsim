// tracer.h: the Tracer, C&C 3's peripheral 1, type 3 (spec 4.21; 8.16; DECISIONS-v1.3 sections 1-25), ported from
// `ref`'s Tracer.java (WORKER14). The RV64 row machine that executes the guest as the rows of Jolt's expanded trace and
// writes each window's run log: the region and the record, control (word 0, PULSE_RUN, RUN_CONFIG, words 32-51, the
// events and the two input credits), execution (fetch through the instruction cache, RVC, the decode table, ROM and
// controller-fed templates, the rows of 4.21.4), the jump LRU and the map probe, the two 128-line caches and the fill bus
// to a MemSystem, the code and log FIFOs and set 1's sizing, every ERRORS bit with item 152's stopping rule, 4.21.9's
// timing, and the output path of 4.21.6 (set 2, the packers, output queues 0-3 with their credits, the drain, done).
//
// THE INTERFACE (owner, 2026-11-09; W15's peripheral method list): a member object of W15's slot, which calls these
// methods through a switch on the peripheral's type (no virtual calls). They are `ref`'s Peripheral methods with `ref`'s
// timing, unchanged: inputs (accept, run, write_word) take effect at the end of their cycle, a region packet lands at
// t + RP_LATENCY; level reads (running, errors, word, events' value) return the end of t - 1. `ref` settles lazily, on
// the first read or step of a cycle (settle); so does this port, so it is exact whatever order the slot calls in.
//
// THE MEMORY SYSTEM is the Tracer's own (MemSystem and FileStore called synchronously, as `ref` does); a harness sets
// `mem` (and `map_mem` if the map's probes have their own shared cache) before the first run.
//
// MODEL CHOICES are `ref`'s (Tracer.java's class comment): a row's memory traffic timed at its start, serially, one read
// outstanding; its result committed at its start, visible to later rows; stall cycles counted by cause.
//
// ERRORS AND FLAGS. ERRORS bits stop the Tracer where it is (item 152), as in `ref`; each is also recorded as a fixed-size
// fact (cycle, bits, cause, two values) in `error_log`. `ref`'s meshsim flags (`violate`) are facts in `violations`.
// `ref`'s UndefinedBehavior and IllegalStateException are FATAL (D24: formatted with fprintf, no allocation).
#pragma once

#include <cstdint>

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
    // ================================================================ the window (8.16)
    static constexpr int TYPE = 3;
    static constexpr int RP_LATENCY = 2;  // Peripheral.RP_LATENCY (4.17.5)
    enum : int {
        W_RUN_CONFIG = 32, W_ROWS_RUN = 33, W_EXP_OFFSET = 34, W_EXP_OPCODE = 35, W_STATE_LENGTH = 36,
        W_CODE_LENGTH = 37, W_RLOG_LENGTH = 38, W_WLOG_LENGTH = 39, W_HASH_MUL = 40, W_HASH_STEP = 41,
        W_HASH_LINES = 42, W_EXP_TEMPLATE = 43, W_STAT = 44, STAT_N = 8
    };
    // the statistics counters, words 44-51 (8.16, item 157), in word order
    enum : int { ST_CYCLES = 0, ST_STALL_FETCH, ST_STALL_DATA, ST_STALL_MAP, ST_STALL_OUTPUT, ST_MISS_I, ST_MISS_D, ST_PROBES };
    static constexpr uint32_t RC_ROWS = 0xFFFFFF, RC_COMP_CODE = 1u << 26, RC_COMP_RLOG = 1u << 27,
                              RC_COMP_WLOG = 1u << 28, RC_DEFINED = 0x1FFFFFFF;
    static constexpr int MAX_ROWS = 65536;
    // events (4.21.3, item 144)
    enum : unsigned { EV_RUN_DONE = 0, EV_ADVICE = 3, EV_EXP = 5, EV_NEWEXP = 6, EV_SVC_STATE = 7, EV_SVC_CODE = 8,
                      EV_SVC_RLOG = 9, EV_SVC_WLOG = 10, EV_BITS = 11 };
    // ERRORS (4.21.8)
    enum : int { ERR_OVERRUN = 1, ERR_INVALID = 2, ERR_ADVICE_UNDER = 4, ERR_MAP = 7, ERR_STORE = 8, ERR_PAGE = 9,
                 ERR_RANGE = 10, ERR_ASSERT = 11, ERR_MISALIGNED = 12 };
    // the record (4.19.2, 4.21.2)
    static constexpr int REC_WORDS = 68, REC_FILES = 66, REC_BITMAP = 67;
    // tags in the log FIFO (4.21.6)
    enum : uint8_t { TAG_LOAD = 0, TAG_OTHER = 1, TAG_WRITE = 2 };
    static constexpr int QCAP = 256, QHALF = 128, CACHE_LINES = 128, LRU_ENTRIES = 64;
    // stalls by cause (statistics)
    enum : int { S_IFETCH = 0, S_DMISS, S_BUS, S_PROBE, S_ADVICE, S_NEWEXP, S_EXP, S_N };

    // ================================================================ parameters (4.21.9)
    int row_cycles = 4, mul_cycles = 6, probe_compare = 2, probe_lines = 3;

    // ================================================================ facts (D24)
    // Why an ERRORS bit was set (ref's errorLog strings, as codes).
    enum : int {
        E_RQ_FULL = 1, E_EQ_FULL, E_CLASS3, E_NO_32BIT_FORM, E_ROM_PAST_511, E_INVALID_OPCODE, E_INVALID_OPERAND,
        E_INVALID_FORMAT, E_ADVICE_ZERO, E_MEM_RANGE_ALIGN, E_STORE_NOT_WRITABLE, E_ASSERT, E_PC0, E_IFETCH_BEYOND,
        E_DATA_BEYOND, E_MAP_OFFSET, E_MAP_HOLE, E_MAP_BEYOND, E_MAP_FREE, E_MAP_FULL
    };
    // ref's meshsim flags (violate).
    enum : int { V_ADVICE_UNCONSUMED = 1, V_UNDEFINED, V_END_NO_NFV, V_XFER_NO_NFV, V_INLINE_NO_TEMPLATE };
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

    MemSystem* mem = nullptr;      // the fill bus's other side (4.21.5)
    MemSystem* map_mem = nullptr;  // the map probe's, if a harness separates them (B1); else mem
    bool cold_mem_on_state_write = false;  // ref's onStateWrite = ms::cold: the word-0 write makes the memory cold

    // ================================================================ the peripheral interface (W15's list)
    static_assert(TYPE == PT_TRACER, "the Tracer's type tag");
    static constexpr int DIGEST_N = STAT_N;  // W15's peripheral.h: digest_words' length
    int params() const { return TYPE; }
    int type() const { return TYPE; }
    uint64_t violation_count() const { return uint64_t(n_violations); }  // the slot forwards new ones (W15)
    EventCounts& events() { return ev; }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }   // ref's onConsume: the Tracer has none
    // accept(t) lands at the end of t + 2. On the framework the slot calls it at level 0 of t + 3 (W15's peripheral.h),
    // so the Tracer may already be settled through t + 2 (settled_ = t + 2: inputs before t + 2 applied): still in
    // time. Settled past that (a read or step of t + 3 already made) is a simulator bug: FATAL. run and write_word for a
    // store at t come at level 0 of t + 1, before any read of t + 1: settled_ <= t + 1 is in time.
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi);
    void run(int64_t t);
    bool running(int64_t t) { settle(t); return s_running_; }
    void sync(int64_t t) { settle(t); }
    int errors(int64_t t) { settle(t); return s_errors_; }
    uint32_t status_bits(int64_t) const { return 0; }
    bool word(int64_t t, int w, uint32_t& v);           // false: an undefined word
    int write_word(int64_t t, int w, uint32_t v);       // 1 written, 0 read-only (a bus error), -1 undefined
    bool store_stalls(int64_t, int) const { return false; }
    void reset(int64_t t);
    void step(int64_t T);                               // ref's runCycle
    bool idle(int64_t) const { return false; }          // ref: the Tracer reports idle never
    // The wake rule (AUDIT-M3 3.3, D23): whether step(t) can change anything. False only when not running, no dropped
    // expansion word waits, and no input or landing is pending; then skipping step(t) is exact (settle is lazy and
    // idempotent, and the counters tick only while running). The slot may sleep C&C 3's peripheral on it.
    bool must_step() const { return running_ || (exp_discard_ > 0 && eq_occ_ > 0) || inputs_.n > 0 || landing_.n > 0; }
    int64_t next_wake_at(int64_t) const { return INT64_MAX; }
    void skip_cycles(int64_t n) const;
    void digest_words(int64_t out[DIGEST_N]) const;     // the counters, words 44-51 (ref: int to long)
    int out_queues() const { return 4; }
    bool out_ready(int64_t t, int q) const;
    void out_take(int64_t t, int q, uint64_t w[2]);
    void out_start(int64_t t, int q, int64_t words);

    // ================================================================ for harnesses (ref's package-private members)
    uint64_t rec[REC_WORDS * 2] = {};  // the record, lo and hi per word: [0] state, [1] stream, [2-65] registers, ...
    uint16_t decode_table[2048] = {};
    uint64_t rom_lo[512] = {}, rom_hi[512] = {};
    bool stopped() const { return stopped_; }
    bool rows_ended(int64_t t) { settle(t); return rows_ended_; }
    int cache_lines(bool data) const { return data ? dcache_.size() : icache_.size(); }
    uint32_t advice_left() const { return uint32_t((rec[3] >> 38) & 0x7FFFFF); }
    int64_t lowest() const { return 0x80000000LL - int64_t((rec[2 * REC_FILES + 1] >> 8) & 0x7FFFFFFF); }
    // meshsim only (the one-memory acceptance): the data cache's dirty lines written back into the memory, untimed.
    int write_back_dirty_untimed();
    // the files word for (memory root, depth, map root, depth, log2 K, lowest_address): a tool's helper
    static void files_word(int32_t mem_root, int mem_depth, int32_t map_root, int map_depth, int log2k, int64_t lowest,
                           uint64_t out[2]);

    // TracerConf's observation (tests/ref/TracerConf.java OBS, in its order): 16 private fields, 10 statistics, stat[8],
    // stalls[7], oq[1..3].occ and .pushed, the two fact counts, the record's hash, the 11 event counts.
    static constexpr int OBS_N = 16 + 10 + 8 + 7 + 6 + 2 + 1 + 11;
    void observe(int64_t out[OBS_N]) const;

    // The canonical fingerprint (AUDIT-M3 6.6; ref's side StateFpM3): unit 0 `tr`, 1 `tr.ic`, 2 `tr.dc`, 3 `tr.mem`,
    // 4 `tr.files`, at the end of cycle c. Out: FpOut (tests/statefp.h) or any sink with w(name, v) and wi(name, i, v).
    static constexpr int FP_UNITS = 5;
    static constexpr const char* FP_NAMES[FP_UNITS] = {"tr", "tr.ic", "tr.dc", "tr.mem", "tr.files"};
    template <class Out> void fingerprint(int unit, Out& o, int64_t c) const;

    // ---- the FIFOs (4.21.6)
    uint32_t* code_fifo = nullptr;  // MAX_ROWS each
    uint64_t* log_fifo = nullptr;
    uint8_t* log_tag = nullptr;
    int code_n = 0, log_n = 0;

    // ---- output queues 1-3 (4.21.6)
    struct OutQueue {
        uint64_t lo[QCAP], hi[QCAP];
        int64_t at[QCAP];
        int head = 0, occ = 0;
        int64_t pushed = 0, read = 0;
        static constexpr int BOUNDS = 1 << 10;
        int64_t bound[BOUNDS];  // each credit's last entry (cumulative)
        int granted = 0;
        bool ended = false;
        int64_t last_pop_t = INT64_MIN;
        int pops_at = 0;
        int64_t pops_cycle = INT64_MIN;
        int64_t stall_cycles = 0, max_occ = 0;
        void reset() { head = occ = 0; pushed = read = 0; granted = 0; ended = false; last_pop_t = INT64_MIN; pops_cycle = INT64_MIN; }
        bool room(int64_t t) const { return occ + (pops_cycle == t ? pops_at : 0) < QCAP; }
        void push(int64_t t, uint64_t l, uint64_t h);
        void end();
        int taken() const { int n = 0; while (n < granted && bound[n] <= read) n++; return n; }
        bool flag() const { return granted > taken(); }
        int64_t covered() const { return granted == 0 ? 0 : bound[granted - 1] - read; }
        bool ready(int64_t t) const { return occ > 0 && at[head] < t; }
        void take(int64_t t, uint64_t w[2]);
    };
    OutQueue oq[4];  // [1..3]; [0] unused (queue 0 is part 0 read in place)

    // ================================================================ statistics
    int64_t stalls[S_N] = {};
    int64_t rows = 0, rows_m = 0, rows_pad = 0, instrs = 0, run_cycles = 0, runs = 0;
    int64_t i_access = 0, i_miss = 0, d_access = 0, d_miss = 0, writebacks = 0, lru_hits = 0, lru_misses = 0, probe_reads = 0;
    int64_t i_miss_by[4] = {}, i_cyc_by[4] = {};  // [cold first, cold second, capacity first, capacity second]
    int64_t i_miss_lpddr = 0, straddles = 0;
    int64_t run_start_at = 0, rows_end_at = 0, drained_at = 0;
    int64_t state_writes = 0, new_expansions = 0, drain_cycles = 0, scan_stalls = 0;
    uint32_t stat[STAT_N] = {};  // words 44-51: 32 bits, wrapping

    EventCounts ev;

private:
    struct Instr {
        bool valid = false;
        uint32_t w = 0;
        unsigned kind = 0, start = 0, rd0 = 0, offset = 0;
        int cls = 0;  // the decode table's class; -1 for a 16-bit encoding with no 32-bit form
        int next_row = 0;
        bool ctrl_first_seen = false, fetched = false, presented = false;
        int64_t code_run = -1;  // the run whose code FIFO holds its value
    };
    struct Input {
        int64_t t;
        int kind, a, b;
        uint64_t lo, hi;
    };
    static constexpr int IN_RUN = 1, IN_WORD = 2, INPUTS = 64;
    struct Ring {  // a small FIFO of inputs (ref's ArrayDeque), fixed size
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
    void set_pc_idx(uint64_t pc, int32_t idx) { rec[0] = pc; rec[1] = (rec[1] & ~uint64_t(0xFFFFFFFF)) | uint64_t(uint32_t(idx)); }
    void set_state_offset(int off) { rec[1] = (rec[1] & ~(uint64_t(0xFFFF) << 32)) | uint64_t(off & 0xFFFF) << 32; }
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
    void row(int64_t T);  // ref's step
    void eq_pop(int64_t T);
    void pop_advice(int64_t T);
    void push_code(uint32_t w);
    void push_log(uint8_t tag, uint64_t v);
    int64_t fetch(int64_t T, Instr& in);
    int64_t fetch16(int64_t t, int64_t o, uint32_t& out, bool second);
    int64_t data_line(int64_t t, int64_t line);
    uint64_t lookup(int64_t T, uint64_t np);
    void end_rows(int64_t T);
    void start_drain(int64_t T);
    void drain_cycle(int64_t T);
    static streamword::Size choose(int64_t values, int64_t raw_words, int64_t coded_bits, bool enabled);
    static bool assertion_holds(unsigned op, uint64_t a, uint64_t b);

    const int me_;

    // ---- registers (4.21.3)
    uint32_t run_config_ = 0, hash_mul_ = 0, hash_step_ = 0, hash_lines_ = 0;
    uint32_t length_code_ = 0, length_rlog_ = 0, length_wlog_ = 0;

    // ---- the input queues (4.21.3)
    uint64_t rq_lo_[QCAP] = {}, rq_hi_[QCAP] = {};
    int rq_head_ = 0, rq_occ_ = 0, rq_half_ = 0, rq_popped_ = 0;
    uint64_t eq_lo_[QCAP] = {}, eq_hi_[QCAP] = {};
    int eq_head_ = 0, eq_occ_ = 0, eq_popped_ = 0;

    // ---- the caches (4.21.5) and the jump LRU: keys in access order, values by slot
    struct Line {
        uint64_t d[16];
        bool dirty, writable;
    };
    int icache_lines_;
    LruIndex icache_, dcache_, jump_lru_, i_seen_;
    Line* iline_ = nullptr;  // icache_lines_ + 1
    Line* dline_ = nullptr;  // CACHE_LINES + 1
    int32_t jump_base_[LRU_ENTRIES + 1] = {};

    // ---- set 1 (4.21.6)
    LruCoder set1_code_{false}, set1_rlog_{false}, set1_wlog_{true};
    int rlog_values_ = 0, wlog_values_ = 0;

    // ---- the core
    bool running_ = false, stopped_ = false, rows_ended_ = false, drained_ = false;
    int rows_target_ = 0, rows_done_ = 0;
    int64_t start_at_ = 0, busy_until_ = INT64_MIN, wait_until_ = INT64_MIN;
    int wait_cause_ = 0;
    bool wait_set_ = false;  // ref's waitCause is null until the first fetch stall
    bool pk_made_ = false;   // ref's packers are null until the first drain
    uint32_t presented_ = 0;
    int expansion_offset_ = 0;
    int latch_ = -1;
    bool template_stored_ = false;
    uint64_t bitmap_[2] = {};
    Instr cur_;
    int exp_discard_ = 0;
    int64_t run_id_ = 0;
    rowcore::Decoded dec_;
    alu::Out alu_;
    MemSystem::Response resp_;
    int64_t stat_until_ = INT64_MIN;
    int stat_cause_ = 0;
    int errors_ = 0;
    int64_t bus_wait_ = 0;
    int64_t lookup_ready_ = 0;

    // ---- inputs, deferred to the end of their cycle
    Ring inputs_, landing_;
    int64_t settled_ = INT64_MIN, last_accept_ = INT64_MIN;

    // ---- level reads: the snapshot at the end of t - 1
    uint32_t s_stat_[STAT_N] = {};
    bool s_running_ = false;
    int s_errors_ = 0, s_rows_ = 0, s_exp_offset_ = 0;
    uint32_t s_exp_opcode_ = 0, s_run_config_ = 0, s_code_l_ = 0, s_rlog_l_ = 0, s_wlog_l_ = 0, s_mul_ = 0, s_step_ = 0,
             s_lines_ = 0;

    // ---- the output path
    int q0ptr_ = REC_WORDS;  // output queue 0's read pointer: 68 = nothing to read
    int64_t q0_last_t_ = INT64_MIN, scan_start_ = 0;
    bool scans_done_ = false, done_ = false;
    int code_scan_ = 0, log_scan_ = 0;
    LruCoder set2_code_{false}, set2_rlog_{false}, set2_wlog_{true};
    Bits pk_code_, pk_rlog_, pk_wlog_;
    bool comp0_ = false, comp1_ = false, comp2_ = false, code_ended_ = false, log_ended_ = false;
    int64_t xfer_queue_ = -1, xfer_left_ = 0;
    int64_t push_t_ = 0;
};

template <class Out>
void Tracer::fingerprint(int unit, Out& o, int64_t c) const {
    auto mx = [c](int64_t v) { return uint64_t(v > c ? v : c); };
    const uint64_t NONE = ~uint64_t(0);
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
    // 5. the core
    o.w("running", running_); o.w("stopped", stopped_); o.w("rowsEnded", rows_ended_); o.w("drained", drained_);
    o.w("done", done_); o.w("rowsTarget", uint64_t(rows_target_)); o.w("rowsDone", uint64_t(rows_done_));
    o.w("startAt", mx(start_at_)); o.w("busyUntil", mx(busy_until_)); o.w("waitUntil", mx(wait_until_));
    o.w("waitCause", wait_until_ > c && wait_set_ ? uint64_t(wait_cause_) : NONE);
    o.w("presented", uint64_t(int64_t(int32_t(presented_)))); o.w("expansionOffset", uint64_t(int64_t(expansion_offset_)));
    o.w("latch", uint64_t(int64_t(latch_))); o.w("templateStored", template_stored_);
    o.w("bitmap0", bitmap_[0]); o.w("bitmap1", bitmap_[1]);
    o.w("expDiscard", uint64_t(exp_discard_));
    o.w("statUntil", mx(stat_until_)); o.w("statCause", stat_until_ > c ? uint64_t(stat_cause_) : NONE);
    o.w("errors", uint64_t(errors_));
    if (!cur_.valid) o.w("cur", 0);
    else {
        o.w("cur", 1); o.w("cur.w", uint64_t(int64_t(int32_t(cur_.w)))); o.w("cur.kind", cur_.kind); o.w("cur.start", cur_.start);
        o.w("cur.rd0", cur_.rd0); o.w("cur.offset", cur_.offset); o.w("cur.cls", uint64_t(int64_t(cur_.cls)));
        o.w("cur.nextRow", uint64_t(int64_t(cur_.next_row))); o.w("cur.ctrlFirstSeen", cur_.ctrl_first_seen);
        o.w("cur.fetched", cur_.fetched); o.w("cur.presented", cur_.presented);
        o.w("cur.codeThisRun", cur_.code_run == run_id_ ? 1 : 0);
    }
    // 6. the events' counts
    for (unsigned b = 0; b < EV_BITS; b++) o.wi("ev", b, ev.count_of(b));
    // 7. the statistics counters
    for (int k = 0; k < STAT_N; k++) o.wi("stat", unsigned(k), stat[k]);
    // 8. the jump LRU, eldest to newest
    o.w("lru.n", uint64_t(jump_lru_.size()));
    for (int s = jump_lru_.eldest(); s >= 0; s = jump_lru_.next(s)) { o.w("lru.pc", jump_lru_.key(s)); o.w("lru.base", uint64_t(uint32_t(jump_base_[s]))); }
    // 9. the FIFOs and set 1
    o.w("codeN", uint64_t(code_n));
    for (int k = 0; k < code_n; k++) o.w("code", code_fifo[k]);
    o.w("logN", uint64_t(log_n));
    for (int k = 0; k < log_n; k++) { o.w("log", log_fifo[k]); o.w("tag", log_tag[k]); }
    o.w("rlogValues", uint64_t(rlog_values_)); o.w("wlogValues", uint64_t(wlog_values_));
    for (const LruCoder* k : {&set1_code_, &set1_rlog_, &set1_wlog_}) { coder(*k); o.w("bits", uint64_t(k->bits)); }
    // 10. the output path
    o.w("q0ptr", uint64_t(q0ptr_)); o.w("scanStart", mx(scan_start_));
    o.w("scansDone", scans_done_); o.w("codeScan", uint64_t(code_scan_)); o.w("logScan", uint64_t(log_scan_));
    o.w("comp0", comp0_); o.w("comp1", comp1_); o.w("comp2", comp2_); o.w("codeEnded", code_ended_); o.w("logEnded", log_ended_);
    o.w("xferQueue", uint64_t(xfer_queue_)); o.w("xferLeft", uint64_t(xfer_left_));
    for (const LruCoder* k : {&set2_code_, &set2_rlog_, &set2_wlog_}) coder(*k);
    for (const Bits* b : {&pk_code_, &pk_rlog_, &pk_wlog_}) {
        if (!pk_made_) { o.w("pk", 0); continue; }
        o.w("pk", 1); o.w("pk.lo", b->lo); o.w("pk.hi", b->hi); o.w("pk.fill", uint64_t(b->fill)); o.w("pk.words", uint64_t(b->words));
    }
    for (int q = 1; q <= 3; q++) {
        const OutQueue& x = oq[q];
        o.w("oq.occ", uint64_t(x.occ));
        for (int k = 0; k < x.occ; k++) { const int i = (x.head + k) % QCAP; o.w("oq.lo", x.lo[i]); o.w("oq.hi", x.hi[i]); }
        o.w("oq.pushed", uint64_t(x.pushed)); o.w("oq.read", uint64_t(x.read)); o.w("oq.granted", uint64_t(x.granted));
        for (int k = 0; k < x.granted; k++) o.w("oq.bound", uint64_t(x.bound[k]));
        o.w("oq.ended", x.ended ? 1 : 0);
    }
    // 11. inputs not yet in effect
    o.w("landing.n", uint64_t(landing_.n));
    for (int k = 0; k < landing_.n; k++) {
        const Input& x = landing_.e[(landing_.head + k) % INPUTS];
        o.w("e", uint64_t(x.t)); o.w("src", uint64_t(int64_t(x.kind))); o.w("offset", uint64_t(int64_t(x.a)));
        o.w("size", uint64_t(int64_t(x.b))); o.w("lo", x.lo); o.w("hi", x.hi);
    }
    o.w("inputs.n", uint64_t(inputs_.n));
    for (int k = 0; k < inputs_.n; k++) {
        const Input& x = inputs_.e[(inputs_.head + k) % INPUTS];
        o.w("e", uint64_t(x.t)); o.w("kind", uint64_t(int64_t(x.kind))); o.w("word", uint64_t(int64_t(x.a)));
        o.w("value", uint64_t(uint32_t(x.b)));
    }
}

}  // namespace tracer
}  // namespace units
