// replayer.h: the replayer, peripheral type 4 (spec 4.19; 4.17.1), cycle by cycle (WORKER15).
//
// sim/ref/meshsim/src/Replayer.java as it runs in v1.3 (v1.26.1: RUN_CONFIG, the 68-word record, the flush at the state
// word's write, part 0 as the working state, the underflow and leftover errors), with REPLAYER-NOTES' model choices
// M1-M6 and N1-N12. Method for method, so the two can be compared every cycle (tests/ref/ReplayConf.java).
//
// Timing, as ref (Peripheral.java): inputs (accept, run, write_word, a consumed event) take effect at the end of t; a
// packet accepted at t lands at the end of t + 2; level reads at t see the end of t - 1. ref applies inputs lazily
// (`settle(T)`: everything timed before T, landings before inputs within a cycle) the first time it is touched at T;
// so does this model, from fixed rings. `step(T)` is ref's runCycle(T).
//
// The cycle loop's rules (D6, D24): no STL, no allocation, no virtual calls. Every queue is a fixed array; the front end's
// two instructions and the row in flight are fields; violations and errors are recorded as fixed-size facts (a code,
// the cycle, two words), counted per code, and formatted after the run (`fact_text`). ref's UndefinedBehavior is FATAL.
// The rows leave through a sink: a running hash of every record always, and an optional record buffer given at
// configuration (owner, Q3).
#pragma once

#include <cstddef>
#include <cstdint>

#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/replayer_alu.h"
#include "units/row_core.h"

namespace units {

// A violation or error, recorded as fixed-size facts (D24).
struct ReplayerFact {
    int64_t t = 0;
    uint16_t code = 0;
    uint64_t a = 0, b = 0;
};

class Replayer;

// A code or log queue and its decompressor (4.19.15): Replayer.Stream.
struct ReplayerStream {
    enum : int { AVAIL = 0, WAIT = 1, END = 2, OVER = 3 };
    static constexpr unsigned MAX_CAP = 256;
    unsigned id = 0;               // 0 code, 1 read log, 2 write log (names, errors)
    unsigned cap = 0, half = 0, value_bits = 0;
    bool is_write = false;
    uint64_t lo[MAX_CAP] = {}, hi[MAX_CAP] = {};
    unsigned head = 0, occ = 0, pend = 0, popped_since = 0;
    int credit = -1;               // its credit bit (4.19.8)
    int64_t base = 0;              // stream bit position of the head entry's bit 0
    int64_t consumed = 0;          // stream bit position of the next unread bit
    unsigned words = 0, values_out = 0;   // the size, latched: 128-bit words (padding included) and values out
    int64_t limit = 0;             // words x 128
    unsigned taken = 0;            // values popped this pass
    int64_t end_bits = -1;         // the bit position after the last value, once popped (0 for an empty stream)
    unsigned landed_entries = 0;   // entries pushed since the flush
    bool comp = false;
    uint64_t E[16] = {};
    int64_t cycle = 0;
    unsigned pops = 0;
    int64_t pop_cycle = INT64_MIN;
    uint64_t peek_value = 0;
    unsigned peek_bits = 0;
    int peek_hit = -1;
    uint64_t values = 0, hits = 0, misses = 0;

    void init(unsigned i, unsigned capacity, unsigned bits, bool write) {
        if (capacity > MAX_CAP) FATAL("replayer stream: capacity %u > %u", capacity, MAX_CAP);
        id = i; cap = capacity; half = capacity / 2; value_bits = bits; is_write = write;
    }
    void clear() { head = 0; occ = 0; pend = 0; base = 0; consumed = 0; popped_since = 0; landed_entries = 0; taken = 0; end_bits = -1; }
    void flush() { clear(); for (uint64_t& x : E) x = 0; }
    void latch(unsigned w, unsigned v, bool c) {
        words = w; values_out = v; comp = c; limit = int64_t(128) * w; taken = 0; end_bits = v == 0 ? 0 : -1;
    }
    // For tests: flush and latch in one.
    void reset(bool compressed, unsigned w, unsigned v) { flush(); latch(w, v, compressed); }
    // Input left over at done: 0 none, 1 values not popped, 2 words after its last value, 3 entries pushed past its words.
    // `n` gets the count.
    unsigned leftover(uint64_t& n) const {
        if (taken < values_out) { n = values_out - taken; return 1; }
        int64_t used = int64_t(uint64_t(end_bits + 127) >> 7);
        if (used < int64_t(words)) { n = uint64_t(int64_t(words) - used); return 2; }
        if (landed_entries > words) { n = landed_entries - words; return 3; }
        n = 0;
        return 0;
    }
    bool low_water() const { return cap - occ - pend >= half; }
    // A push lands at the end of cycle t: dropped if the queue was full before the cycle's pop (4.19.8, 4.13).
    bool push(int64_t t, uint64_t l, uint64_t h) {
        unsigned pops_at_t = pop_cycle == t ? pops : 0;
        if (occ + pops_at_t >= cap) return false;
        unsigned i = (head + occ) % cap;
        lo[i] = l; hi[i] = h; occ++;
        if (pend > 0) pend--;
        landed_entries++;
        return true;
    }
    int64_t landed_bits() const { return base + int64_t(128) * occ; }
    uint64_t bits(int64_t pos, unsigned n) const {   // n <= 64 bits from stream position pos, LSB first
        uint64_t v = 0;
        for (unsigned k = 0; k < n;) {
            int64_t p = pos + k;
            unsigned e = unsigned(uint64_t(p - base) >> 7), b = unsigned((p - base) & 127);
            unsigned slot = (head + e) % cap;
            uint64_t word = b < 64 ? lo[slot] >> b : hi[slot] >> (b - 64);
            unsigned take = n - k < 64 - (b & 63) ? n - k : 64 - (b & 63);
            uint64_t m = take == 64 ? ~uint64_t(0) : (uint64_t(1) << take) - 1;
            v |= (word & m) << k;
            k += take;
        }
        return v;
    }
    int need(int64_t n) const { if (consumed + n > limit) return OVER; if (consumed + n > landed_bits()) return WAIT; return AVAIL; }
    // Decode the next value without taking it.
    int peek() {
        if (taken >= values_out) return END;
        if (!comp) {
            int s = need(value_bits); if (s != AVAIL) return s;
            peek_value = bits(consumed, value_bits); peek_bits = value_bits; peek_hit = -1; return AVAIL;
        }
        int s = need(1); if (s != AVAIL) return s;
        if (bits(consumed, 1) == 1) {
            s = need(5); if (s != AVAIL) return s;
            peek_hit = int(bits(consumed + 1, 4)); peek_value = E[peek_hit]; peek_bits = 5; return AVAIL;
        }
        s = need(2); if (s != AVAIL) return s;
        unsigned n = bits(consumed + 1, 1) == 1 ? 64 : 32;
        s = need(2 + n); if (s != AVAIL) return s;
        peek_value = bits(consumed + 2, n); peek_bits = 2 + n; peek_hit = -1; return AVAIL;
    }
    // Take the value peek() decoded: advance, and update the table (true LRU, move to front; 4.19.15).
    void commit() {
        consumed += peek_bits; values++;
        if (++taken == values_out) end_bits = consumed;
        if (!comp) return;
        unsigned lru_top = is_write ? 14 : 15;
        if (peek_hit >= 0) {
            hits++;
            if (is_write && peek_hit == 15) return;             // the last-load entry: nothing reordered
            uint64_t v = E[peek_hit];
            for (int i = peek_hit; i > 0; i--) E[i] = E[i - 1];
            E[0] = v;
        } else {
            misses++;
            for (unsigned i = lru_top; i > 0; i--) E[i] = E[i - 1];   // the old E[lru_top] is dropped
            E[0] = peek_value;
        }
    }
    void last_load(uint64_t v) { E[15] = v; }
    // ev: the replayer's events (a credit back per half a queue popped)
    void release_consumed(EventCounts& ev) { release_to(consumed, ev); }
    void release_to(int64_t pos, EventCounts& ev) {
        while (occ > 0 && (base + 128 <= pos || (end_bits >= 0 && pos >= end_bits && base < end_bits))) {
            head = (head + 1) % cap; occ--; base += 128;
            if (pop_cycle != cycle) { pop_cycle = cycle; pops = 0; }
            pops++;
            if (++popped_since == half) { popped_since = 0; if (credit >= 0) ev.post(cycle, unsigned(credit)); }
        }
    }
};

class Replayer {
public:
    // ---- constants (Replayer.java) --------------------------------------------------------------------------------------
    static constexpr unsigned TYPE = 4, PARAMS = 4;
    static constexpr int RP_LATENCY = 2;
    static constexpr unsigned OUTQ_ROWS = 32, REC_LONGS = 10, REC_SIGN_IMM = 22;
    static constexpr unsigned REC_WORDS = 68, PART0_WORDS = 66, REC_FILES = 66, REC_BITMAP = 67;
    static constexpr uint32_t RC_ROWS = 0xFFFFFF, RC_STORE_LOG = 1u << 24, RC_RD_PREV = 1u << 25, RC_TRACER_COMP = 7u << 26, RC_DEFINED = 0x1FFFFFFF;
    static constexpr unsigned W_RUN_CONFIG = 32, W_ROWS_RUN = 33, W_EXP_OFFSET = 34, W_EXP_OPCODE = 35;
    static constexpr unsigned LOG_QUEUE_ENTRIES = 256;   // v1.26 (V3-12); ref's -Dreplayer.logq=512 is not modelled
    static constexpr unsigned EXP_CAP = 256, EXP_HALF = 128;
    // events (4.19.9)
    static constexpr unsigned EV_RUN_DONE = 0, EV_CODE = 2, EV_RLOG = 3, EV_WLOG = 4, EV_EXP = 5, EV_NEWEXP = 6;
    // ERRORS (4.19.11)
    static constexpr unsigned ERR_BAD_WRITE = 0, ERR_OVERRUN = 1, ERR_INVALID = 2, ERR_CODE_UNDER = 3, ERR_RLOG_UNDER = 4,
                              ERR_WLOG_UNDER = 5, ERR_LEFTOVER = 6, N_ERR = 7;
    // stalls (Replayer.Stall)
    enum : unsigned { ST_IMAGE, ST_CODE, ST_NEWEXP, ST_EXP, ST_RLOG, ST_WLOG, ST_OUTQ, N_STALL };
    // facts: violations (ref's violate()), then the error lists (invalids, overruns, underflows, leftovers)
    enum : uint16_t {
        V_NO_FLUSH = 1,       // a pass started without a write to the state word since the last pass
        V_TWO_SOURCES,        // two sources feed one queue port in a pass (invariant 18): a = old src, b = new
        V_FOREIGN_SRC,        // another unit wrote a queue during a pass: a = src, b = queue
        V_CREDIT,             // a queue pushed past the credits taken for it: a = queue, b = pushed
        V_OFFSET_IMM,         // offset_imm on a row that isn't ShiftData: a = op
        V_LOG_AND_NFV,        // a row with both from_log and next_from_value
        V_UNDEFINED,          // an undefined ALU result (invariant 16): a = op
        V_END_WORD,           // the program-end row's log word isn't 0: a = the word
        V_NO_NFV,             // a control transfer from a row without next_from_value: a = op
        V_MISALIGNED,         // a misaligned memory row: a = op, b = address
        N_VIOLATION,
        F_INVALID = 32,       // an invalid expansion: a = cause (INV_*), b = detail
        F_OVERRUN,            // a queue overrun: a = queue
        F_UNDERFLOW,          // an underflow: a = stream, b = cause (UF_*)
        F_LEFTOVER,           // done with input left over: a = streams (bit s: stream s), b = stream 0's cause
    };
    enum : uint64_t { INV_CLASS3 = 1, INV_ROM_END = 2, INV_WORD = 3 };   // b: the instruction / start<<16|row / rowcore cause
    enum : uint64_t { UF_CODE_OVER_DECODE = 1, UF_CODE_PAST_VALUES = 2, UF_LOG_END = 3, UF_LOG_OVER = 4, UF_NFV_OVER = 5,
                      UF_TRANSFER = 6 };
    static constexpr unsigned FACT_RING = 64;

    // Host: where violations go (the slot's violate, a cold path), set by attach. Fixed-size facts only.
    using ViolateFn = void (*)(void* ctx, int64_t t, unsigned code);

    explicit Replayer(unsigned me = 0);
    Replayer(const Replayer&) = delete;
    Replayer& operator=(const Replayer&) = delete;
    void attach(ViolateFn fn, void* ctx) { host_fn_ = fn; host_ctx_ = ctx; }

    // ---- the Peripheral interface (Peripheral.java, non-virtual) ----------------------------------------------------
    unsigned type() const { return TYPE; }
    uint32_t params() const { return PARAMS; }
    void accept(int64_t t, unsigned src, uint32_t offset, unsigned size, uint64_t lo, uint64_t hi);
    void run(int64_t t);
    bool running(int64_t t) { settle(t); return s_running_; }
    bool idle(int64_t) const { return !running_ && in_n_ == 0 && land_n_ == 0 && outq_n_ == 0; }
    int64_t next_wake_at(int64_t) const { return INT64_MAX; }
    uint32_t status_bits(int64_t) const { return 0; }
    uint32_t errors(int64_t t);
    EventCounts& events() { return ev; }
    // A consuming load's effect (the slot's endCycle): one count from each set bit, and the refill allowances.
    void consume_events(uint32_t bits);
    void sync(int64_t t) { settle(t); }
    bool word(int64_t t, unsigned w, uint32_t& v);
    int write_word(int64_t t, unsigned w, uint32_t v);
    bool store_stalls(int64_t, unsigned) const { return false; }
    unsigned out_queues() const { return 0; }
    void reset(int64_t t);
    void step(int64_t T);   // runCycle

    // ---- for tests, the drivers and the harness ---------------------------------------------------------------------
    unsigned landed(int64_t t) const;
    uint32_t status(int64_t t, unsigned i) { settle(t); return i == 0 ? s_status0_ : i == 1 ? s_status1_ : 0; }
    uint64_t reg(unsigned r) const { return r == 0 ? 0 : region0[4 + r]; }
    uint64_t pc() const { return pc_; }
    uint32_t instr_idx() const { return instr_idx_; }
    unsigned rows_done() const { return rows_done_; }
    unsigned next_offset_now() const { return next_offset(); }
    bool is_running() const { return running_; }
    bool store_log_latched() const { return f_store_; }
    uint32_t run_config_now() const { return run_config_; }
    unsigned rows_per_pass_now() const { return rows_per_pass_; }
    unsigned out_queue_rows() const { return outq_n_; }
    uint64_t cond_events(unsigned k) const { return cond_events_[k]; }
    // The error lists' sizes (ref's violations, invalids, overruns, underflows, leftovers) and clean().
    uint64_t fact_count(uint16_t code) const { return code < 64 ? fact_counts_[code] : 0; }
    uint64_t violations() const { uint64_t n = 0; for (unsigned c = 1; c < N_VIOLATION; c++) n += fact_counts_[c]; return n; }
    uint64_t invalids() const { return fact_counts_[F_INVALID]; }
    uint64_t overruns() const { return fact_counts_[F_OVERRUN]; }
    uint64_t underflows() const { return fact_counts_[F_UNDERFLOW]; }
    uint64_t leftovers() const { return fact_counts_[F_LEFTOVER]; }
    bool clean() const { return violations() + invalids() + overruns() + underflows() + leftovers() == 0; }
    // The last facts recorded (up to FACT_RING), oldest first, and one's text (after the run; not in the cycle loop).
    const ReplayerFact& fact(unsigned i) const { return facts_[(fact_n_ - fact_shown_ + i) % FACT_RING]; }
    unsigned fact_shown() const { return fact_shown_; }
    static void fact_text(const ReplayerFact& f, char* buf, size_t n);

    // The sink (4.19.12): one row every sink_interval cycles; a running hash of every record (h = mix(h ^ word), word 0
    // first), and the records into `buf` if given (capacity in rows; FATAL beyond it).
    void set_row_buffer(uint64_t* buf, uint64_t capacity_rows) { row_buf_ = buf; row_cap_ = capacity_rows; }
    uint64_t rows_sunk() const { return rows_sunk_; }
    uint64_t row_hash() const { return row_hash_; }
    unsigned sink_interval = 1;

    // ---- state (public for the unit tests, as ref's package-private fields) ------------------------------------------
    unsigned me;
    uint64_t region0[PART0_WORDS * 2] = {};   // state word, stream word, the register image: lo, hi
    uint16_t decode_table[2048] = {};
    uint64_t rom_lo[512] = {}, rom_hi[512] = {};
    EventCounts ev;
    ReplayerStream code, rlog, wlog;
    struct ExpQueue {
        uint64_t lo[EXP_CAP] = {}, hi[EXP_CAP] = {};
        unsigned head = 0, occ = 0, pend = 0, popped_since = 0;
        bool newest_valid = false;
        unsigned newest_seq = 0;
        int64_t cycle = 0;
        unsigned pops = 0;
        int64_t pop_cycle = INT64_MIN;
        void reset() { head = 0; occ = 0; pend = 0; newest_valid = false; popped_since = 0; }
        bool low_water() const { return EXP_CAP - occ - pend >= EXP_HALF && newest_valid && newest_seq > pend; }
        bool push(int64_t t, uint64_t l, uint64_t h) {
            unsigned pops_at_t = pop_cycle == t ? pops : 0;
            if (occ + pops_at_t >= EXP_CAP) return false;
            unsigned i = (head + occ) % EXP_CAP;
            lo[i] = l; hi[i] = h; occ++;
            if (pend > 0) pend--;
            newest_valid = true; newest_seq = unsigned(l >> 32) & 0xFFF;
            return true;
        }
        uint64_t head_lo() const { return lo[head]; }
        uint64_t head_hi() const { return hi[head]; }
    } exp;
    // statistics
    uint64_t stalls[N_STALL] = {};
    uint64_t pass_cycles = 0, rows_total = 0, rows_m = 0, rows_ctrl = 0, new_expansions = 0, passes = 0;
    uint64_t pext_non_contiguous = 0, config_writes = 0, flushes = 0, ignored_writes = 0;
    int64_t pass_start_at = 0, pass_end_at = 0;

private:
    struct Instr {
        uint32_t w = 0;
        unsigned kind = 0, start = 0, offset = 0, rd0 = 0, next_row = 0, first_row = 0;
        bool last_started = false, ctrl_first_seen = false;
        int64_t decoded_at = 0;
        int64_t code_end = 0;
    };
    struct Pending {
        int rd_reg = -1;
        uint64_t rd_val = 0, new_pc = 0;
        uint32_t new_idx = 0;
        bool last_of_instr = false;
        int64_t code_end = 0;
        uint64_t rec[REC_LONGS] = {};
    };
    struct Input {
        int64_t t;
        unsigned kind;   // landing: the source; else IN_RUN, IN_CONFIG
        uint32_t a, b;   // landing: addr, size; config: the value
        uint64_t lo, hi;
    };
    static constexpr unsigned IN_RUN = 2, IN_CONFIG = 3, RING = 16;

    void violate(int64_t t, uint16_t code, uint64_t a = 0, uint64_t b = 0);
    void record(int64_t t, uint16_t code, uint64_t a, uint64_t b);
    void note_input(int64_t t);
    void settle(int64_t T);
    unsigned next_offset() const;
    void do_run(int64_t t);
    void flush(int64_t t);
    void latch_sizes();
    void stop_core(int64_t t, bool post_done);
    void land(int64_t t, unsigned src, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi);
    void drain(int64_t T);
    void front_end(int64_t T);
    void start_row(int64_t T);
    uint64_t pop_transfer(int64_t T);
    void finish_row(int64_t T);
    void check_leftover(int64_t T);
    void underflow(int64_t T, unsigned stream, uint64_t cause);
    void invalid(int64_t T, uint64_t cause, uint64_t detail);
    void exp_pop();
    void exp_new_expansion();
    ReplayerStream& stream(unsigned i) { return i == 0 ? code : i == 1 ? rlog : wlog; }
    unsigned half(unsigned q) const { return q == 0 ? code.half : q == 1 ? rlog.half : q == 2 ? wlog.half : EXP_HALF; }

    ViolateFn host_fn_ = nullptr;
    void* host_ctx_ = nullptr;

    uint32_t run_config_ = 0;
    unsigned rows_per_pass_ = 0;
    bool running_ = false;
    uint64_t pc_ = 0;
    uint32_t instr_idx_ = 0;
    bool f_store_ = false, f_rd_prev_ = false, state_written_ = false;
    unsigned start_offset_ = 0, stopped_offset_ = 0;
    int64_t copy_done_at_ = 0;
    unsigned rows_started_ = 0, rows_done_ = 0;
    Instr cur_, nxt_;
    bool has_cur_ = false, has_nxt_ = false;
    bool first_instr_ = false;
    int64_t busy_until_ = INT64_MIN;
    Pending inflight_;
    bool has_inflight_ = false;
    uint32_t presented_ = 0;
    int64_t new_exp_set_at_ = INT64_MIN, done_set_at_ = INT64_MIN;
    int64_t last_start_at_ = INT64_MIN;
    bool code_ended_ = false;
    unsigned exp_discard_ = 0;
    int src_of_[4] = {-1, -1, -1, -1};
    unsigned pushed_[4] = {}, allowance_[4] = {};
    uint64_t cond_events_[N_ERR] = {};

    // inputs, deferred to the end of their cycle: fixed rings
    Input inputs_[RING] = {}, landing_[RING] = {};
    unsigned in_h_ = 0, in_n_ = 0, land_h_ = 0, land_n_ = 0;
    int64_t accept_times_[8] = {};
    uint64_t accept_n_ = 0;
    int64_t settled_ = INT64_MIN, last_input_t_ = INT64_MIN;

    // level reads: the snapshot at the end of t - 1
    bool s_running_ = false;
    uint32_t s_status0_ = 0, s_status1_ = 0, s_rows_ = 0, s_run_config_ = 0;

    // the output queue and the sink
    uint64_t outq_[OUTQ_ROWS][REC_LONGS] = {};
    int64_t outq_at_[OUTQ_ROWS] = {};
    unsigned outq_h_ = 0, outq_n_ = 0;
    int64_t last_sink_ = INT64_MIN / 2;
    uint64_t* row_buf_ = nullptr;
    uint64_t row_cap_ = 0, rows_sunk_ = 0, row_hash_ = 0;

    // facts
    ReplayerFact facts_[FACT_RING] = {};
    unsigned fact_n_ = 0, fact_shown_ = 0;
    uint64_t fact_counts_[64] = {};

    rowcore::Decoded dec_;
};

}  // namespace units
