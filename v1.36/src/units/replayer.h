// replayer.h: the replayer, peripheral type 4 (spec 17.8), cycle by cycle, as the RTL builds it.
//
// v1.36: the model is the RTL's `replayer_unit` (Snarky One, rtl/blocks/replayer_unit/src/*.veryl), part for part and
// register for register, so that a pass takes the RTL's cycles as well as giving its rows:
//   the input queues     replayer_input_queue.veryl: 256 entries, one pop a cycle, the count of entries left (17.8.8)
//   the decoders         stream_decoder.veryl: an output register a decode fills when empty, the tail, the announcement
//                        (`value_soon`), the underflow on the consumer's asking, `complete` (17.8.12)
//   the fetch            replayer_fetch.veryl: the decode table and the ROM read a cycle after their address, the row
//                        word register, the rd = x0 policies (17.8.5, 17.8.6)
//   the row engine       replayer_row_engine.veryl: the reads a cycle before the start, the start, the log word in cycle
//                        2, the completion at the ALU's `done`, the write-back register (17.8.7, 17.8.11)
//   the ALU's schedule   replayer_alu.veryl: `done` in cycle 3, 5 for an M row, later for a VirtualPext whose mask has
//                        two or more runs; the values are replayer_alu.h's
//   the pass             replayer_pass.veryl: RUN_CONFIG, ROWS_RUN, EXPANSION_OFFSET, EXPANSION_OPCODE, the end, the
//                        error stops, the leftover check a cycle after the end (17.8.1, 17.8.10)
//   the output queue     replayer_output_queue.veryl: 32 rows, a row a cycle out of the row port (17.8.11)
// The control block's counted events (17.1, 17.2; peripheral_control.veryl) are the EventCounts `ev`: the slot holds
// the block's other words.
//
// The interface is the peripheral's (units/peripheral.h): an input called with t (a write the port presents, a store,
// a consuming load) takes effect at the end of t, and a level read at t sees the end of t - 1. `step(T)` runs cycle
// T's logic on the state the inputs before T left; the inputs of T are applied after it, lazily (`settle`), in the
// write port's order. A consuming load of EVENTS at t is `consume_events`, called after `step(t)`.
//   write(t, offset, lo, hi)   the RTL's write port (`noc_write`): a 128-bit WRITE presented in cycle t, landing at its
//                              end (17.8.9)
//   accept(t, ...)             the slot's: the director accepted the packet at t, and the port presents it at
//                              t + RP_LATENCY (units/peripheral.h)
//
// The cycle loop's rules (D6, D24): no STL, no allocation, no virtual calls. Violations (UB the simulator reports,
// 17.8.24) and errors are recorded as fixed-size facts (a code, the cycle, two words), counted per code, and formatted
// after the run (`fact_text`). The rows leave through a sink: a running hash of every record always, and an optional
// record buffer given at configuration.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/memsum.h"
#include "units/peripheral.h"
#include "units/replayer_alu.h"
#include "units/row_core.h"
#include "units/stream_word.h"

namespace units {

// A violation or error, recorded as fixed-size facts (D24).
struct ReplayerFact {
    int64_t t = 0;
    uint16_t code = 0;
    uint64_t a = 0, b = 0;
};

// One input queue (17.8.8; replayer_input_queue.veryl): REPLAY_QUEUE_ENTRIES entries of 128 bits, and the count of
// entries left since the last credit return or fill.
struct ReplayerQueue {
    static constexpr unsigned CAP = 256, HALF = 128;
    uint64_t lo[CAP] = {}, hi[CAP] = {};
    unsigned head = 0, occ = 0;        // occ: the entries held
    unsigned left = 0;                 // entries left since the last return or restart, 0 to HALF - 1
    int64_t pop_at = INT64_MIN;        // the cycle of its last pop: full is the count before the cycle's pop
    uint64_t entered = 0;              // entries that have entered since the state write (a statistic)

    void clear() { head = 0; occ = 0; left = 0; entered = 0; }
    bool valid() const { return occ != 0; }
    uint64_t head_lo() const { return occ ? lo[head] : 0; }
    uint64_t head_hi() const { return occ ? hi[head] : 0; }
    // A push presented in cycle t: dropped if the queue held CAP entries at the start of t.
    bool push(int64_t t, uint64_t l, uint64_t h) {
        if (occ + (pop_at == t ? 1u : 0u) >= CAP) return false;
        unsigned i = (head + occ) % CAP;
        lo[i] = l; hi[i] = h; occ++; entered++;
        return true;
    }
    // The pop of cycle t. `restart`: the credit's fill in this cycle, which the pop is not counted against. Returns
    // the credit's return: the HALFth entry since the last return or restart leaves.
    bool pop(int64_t t, bool restart) {
        head = (head + 1) % CAP; occ--; pop_at = t;
        if (restart) return false;
        if (++left == HALF) { left = 0; return true; }
        return false;
    }
};

// One stream's decoder (17.8.12; stream_decoder.veryl), between its queue and its consumer. `eval` computes the
// cycle's signals from the registers and the queue's head; `update` is the clock edge.
struct StreamDecoder {
    unsigned value_bits = 64;
    bool last_load = false;            // the write log's: E[15] is the last load
    // ---- registers
    unsigned position = 0;             // the next token's bit: in the head, 0-128; while straddling, in the tail, 63-128
    bool straddle = false;
    uint64_t tail_lo = 0, tail_hi = 0; // the entry taken into the tail, whole: its bits [127:63] are the tail
    unsigned taken = 0;                // entries taken from the queue
    unsigned decoded = 0;              // values decoded before the last cycle
    bool decode_pending = false;       // ... and a decode in the last cycle
    uint64_t E[16] = {};
    bool move_pending = false;         // a decode's move of the table, made the cycle after it
    unsigned move_last = 0;
    bool value_valid = false;          // the output register
    uint64_t value_register = 0;
    bool value_is_hit = false;
    unsigned value_entry = 0;          // a hit's table entry (the RTL holds it one-hot)
    // ---- the cycle's signals (eval)
    bool decode = false, value_soon = false, take = false, take_to_tail = false;
    bool more_values = false, past_words = false;
    bool tok_hit = false;
    unsigned tok_index = 0, tok_end = 0;
    uint64_t tok_value = 0;
    bool tok_compressed = false;
    // ---- statistics
    uint64_t values = 0, hits = 0, misses = 0;   // values taken by the consumer; decodes that hit and missed
    uint64_t miss64 = 0;                          // a miss of size 1 (UB for the code stream, 17.8.12)

    void init(unsigned bits, bool write) { value_bits = bits; last_load = write; }
    // The state write (and the reset): the counters, the position, the output register, the table.
    void clear() {
        position = 0; straddle = false; taken = 0; decoded = 0; decode_pending = false; move_pending = false;
        value_valid = false;
        for (uint64_t& e : E) e = 0;
        values = 0; hits = 0; misses = 0;
    }
    unsigned values_decoded() const { return decoded + (decode_pending ? 1u : 0u); }
    uint64_t value() const { return value_is_hit ? E[value_entry] : value_register; }

    void eval(bool run, bool entry_valid, uint64_t e_lo, uint64_t e_hi, const streamword::Size& sz) {
        typedef unsigned __int128 u128;
        const u128 entry = u128(e_hi) << 64 | e_lo;
        more_values = values_decoded() < sz.values;
        const bool head_in_stream = taken < sz.words;
        const bool last_entry = taken + 1 == sz.words;
        // the 66 bits from the position: the head's, or the tail's then the head's
        const u128 tail_entry = u128(tail_hi) << 64 | tail_lo;
        const u128 low = straddle ? (tail_entry >> 63) << 63 : entry;
        const u128 high = entry & ((u128(1) << 66) - 1);
        u128 win = position == 0 ? low : position >= 128 ? high : (low >> position) | (high << (128 - position));
        const uint64_t w0 = uint64_t(win), w1 = uint64_t(win >> 64);
        tok_compressed = sz.compressed;
        tok_hit = (w0 & 1) != 0;
        tok_index = unsigned(w0 >> 1) & 15;
        const bool miss_long = (w0 >> 1 & 1) != 0;
        const uint64_t miss_value = miss_long ? (w0 >> 2 | w1 << 62) : (w0 >> 2 & 0xFFFFFFFFull);
        const bool fits_long = position <= 128 - 66;
        bool fits;
        if (!sz.compressed) { fits = position <= 128 - value_bits; tok_end = position + value_bits; }
        else if (tok_hit) { fits = position <= 128 - 5; tok_end = position + 5; }
        else if (miss_long) { fits = fits_long; tok_end = position + 66; }
        else { fits = position <= 128 - 34; tok_end = position + 34; }
        tok_value = !sz.compressed ? w0 : miss_value;
        if (value_bits < 64) tok_value &= (uint64_t(1) << value_bits) - 1;
        tok_long = sz.compressed && !tok_hit && miss_long;
        const bool in_tail = fits || entry_valid;
        const bool in_head = entry_valid && head_in_stream && fits && (fits_long || last_entry);
        const bool token_ready = straddle ? in_tail : in_head;
        decode = run && !value_valid && more_values && token_ready;
        const bool token_certain = entry_valid && (straddle || (head_in_stream && fits_long));
        value_soon = run && !value_valid && more_values && token_certain;
        take_to_tail = more_values && position >= 63 && !last_entry;
        const bool take_last = !more_values && position != 0;
        take = entry_valid && !straddle && (take_to_tail || take_last);
        past_words = more_values && !straddle && (!head_in_stream || (last_entry && !fits));
    }
    // The consumer asks for a value the decoder does not hold and never will.
    bool underflow(bool value_ready) const { return value_ready && !value_valid && (!more_values || past_words); }
    bool complete(const streamword::Size& sz) const { return values_decoded() >= sz.values && !value_valid && taken == sz.words; }

    // The clock edge. e_lo, e_hi: the queue's head (for the tail).
    void update(bool value_ready, uint64_t e_lo, uint64_t e_hi, bool last_load_valid, uint64_t last_load_value) {
        // the table: the last cycle's decode's move, then the last load
        const uint64_t offered = value();
        if (move_pending) {
            for (unsigned i = move_last; i >= 1; i--) E[i] = E[i - 1];
            E[0] = offered;
        }
        if (last_load && last_load_valid) E[15] = last_load_value;
        // the output register
        if (!value_valid) { value_register = tok_value; value_is_hit = tok_compressed && tok_hit; value_entry = tok_index; }
        else if (move_pending) value_entry = 0;
        if (value_valid && value_ready) values++;
        if (decode) value_valid = true;
        else if (value_ready) value_valid = false;
        const unsigned lru_last = last_load ? 14 : 15;
        const bool lru_decode = decode && tok_compressed;
        move_pending = lru_decode && (!tok_hit || !(last_load && tok_index == 15));
        move_last = tok_hit ? tok_index : lru_last;
        if (lru_decode) { if (tok_hit) hits++; else misses++; if (tok_long) miss64++; }
        // the counters and the position
        decoded = values_decoded();
        decode_pending = decode;
        const bool was_straddling = straddle;
        if (take) {
            taken++;
            if (take_to_tail) { straddle = true; tail_lo = e_lo; tail_hi = e_hi; }
            else position = 0;
        }
        if (decode) {
            if (!was_straddling) position = tok_end;
            else if (tok_end >= 128) { straddle = false; position = tok_end - 128; }
            else position = tok_end;
        }
    }
    bool tok_long = false;
};

class Replayer : public PeripheralBase<Replayer> {
public:
    // ---- constants -------------------------------------------------------------------------------------------------------
    static constexpr unsigned TYPE = 4, PARAMS = 4;
    static constexpr int RP_LATENCY = units::RP_LATENCY;
    static constexpr unsigned OUTQ_ROWS = 32, REC_LONGS = 10, REC_SIGN_IMM = 22;
    static constexpr unsigned REC_WORDS = 68, PART0_WORDS = 66, REC_FILES = 66, REC_BITMAP = 67;
    static constexpr uint32_t RC_ROWS = 0xFFFFFF, RC_STORE_LOG = 1u << 24, RC_RD_PREV = 1u << 25, RC_TRACER_COMP = 7u << 26, RC_DEFINED = 0x1FFFFFFF;
    // Its own words of the window (17.8.1): block 2's words 0-3. The control block's words are the slot's.
    static constexpr unsigned W_RUN_CONFIG = 32, W_ROWS_RUN = 33, W_EXP_OFFSET = 34, W_EXP_OPCODE = 35;
    static constexpr unsigned LOG_QUEUE_ENTRIES = ReplayerQueue::CAP;   // REPLAY_QUEUE_ENTRIES (19): every input queue's
    static constexpr unsigned EXP_CAP = ReplayerQueue::CAP, EXP_HALF = ReplayerQueue::HALF;
    // EVENTS (17.8.1)
    static constexpr unsigned EV_RUN_DONE = 0, EV_CODE = 2, EV_RLOG = 3, EV_WLOG = 4, EV_EXP = 5, EV_NEWEXP = 6;
    // ERRORS (17.8.1), packed from bit 0
    static constexpr unsigned ERR_OVERRUN = 0, ERR_INVALID = 1, ERR_CODE_UNDER = 2, ERR_RLOG_UNDER = 3, ERR_WLOG_UNDER = 4,
                              ERR_LEFTOVER = 5, N_ERR = 6;
    // The cycles of a pass in which a row could run and none starts or completes, by what it waits for:
    //   ST_PIPE    the fetch's and the row engine's own cycles: an entry or a ROM word being read, the reads, the
    //              write-back (the shared NOP row's two cycles are here)
    //   ST_CODE    the code decoder has no value
    //   ST_NEWEXP  a core-fed expansion's first word, ST_EXP a later one: the expansion queue is empty
    //   ST_RLOG    a read-log word: a row's own, not there and not announced, or a control transfer's
    //   ST_WLOG    a write-log word
    //   ST_OUTQ    the output queue is full
    //   ST_ALU     a VirtualPext's extra cycles (a mask of two or more runs)
    enum : unsigned { ST_PIPE, ST_CODE, ST_NEWEXP, ST_EXP, ST_RLOG, ST_WLOG, ST_OUTQ, ST_ALU, N_STALL };
    // facts: violations (UB the simulator reports, 17.8.24), then the error lists (invalids, overruns, underflows, leftovers)
    enum : uint16_t {
        V_NO_FLUSH = 1,       // START with no state write since the reset or the previous pass's START (17.8.10)
        V_TWO_SOURCES,        // two sources feed one queue port in a pass (invariant 18): a = old src, b = new
        V_FOREIGN_SRC,        // another unit wrote a queue during a pass: a = src, b = queue
        V_CREDIT,             // a queue pushed past the credits taken for it: a = queue, b = pushed
        V_OFFSET_IMM,         // offset_imm on a row that isn't ShiftData: a = op
        V_LOG_AND_NFV,        // a row with both from_log and next_from_value
        V_UNDEFINED,          // an undefined ALU result (invariant 16): a = op
        V_END_WORD,           // the program-end row's log word isn't 0: a = the word
        V_NO_NFV,             // a row without next_from_value whose new pc differs from its pc: a = op
        V_MISALIGNED,         // a misaligned memory row (counted, no UB): a = op, b = address
        V_EXP_LEFT,           // a word left in the expansion queue at a new expansion (17.8.8): a = the words
        V_CODE_MISS64,        // a code miss with size bit 1 (17.8.12)
        N_VIOLATION,
        F_INVALID = 32,       // an invalid expansion: a = cause (INV_*), b = detail
        F_OVERRUN,            // a queue overrun: a = queue
        F_UNDERFLOW,          // an underflow: a = stream, b = cause (UF_*)
        F_LEFTOVER,           // done with input left over: a = streams (bit s: stream s), b = stream 0's cause
    };
    enum : uint64_t { INV_CLASS3 = 1, INV_ROM_END = 2, INV_WORD = 3 };   // b: the instruction / start<<16|row / rowcore cause
    enum : uint64_t { UF_CODE_OVER_DECODE = 1, UF_CODE_PAST_VALUES = 2, UF_LOG_END = 3, UF_LOG_OVER = 4,
                      UF_TRANSFER = 6, UF_TRANSFER_END = 7 };   // 6: past the read log's words, 7: past its values
    static constexpr unsigned FACT_RING = 64;
    // The error lists, each entry a text. The first `list_capacity` entries of each are kept, formatted when recorded
    // (snprintf into a buffer allocated at construction; D24), and every entry is counted.
    enum : unsigned { L_VIOLATIONS = 0, L_INVALID, L_OVERRUNS, L_UNDERFLOWS, L_LEFTOVER, N_LISTS };
    static constexpr unsigned LIST_CAPACITY = 4, LIST_TEXT = 256;

    // log_entries: the read-log and write-log queues' entries. The RTL builds REPLAY_QUEUE_ENTRIES alone, so any other
    // value is FATAL (v1.35's -Dreplayer.logq=512 went with its model).
    explicit Replayer(unsigned me = 0, unsigned log_entries = LOG_QUEUE_ENTRIES, unsigned list_capacity = LIST_CAPACITY);
    Replayer(const Replayer&) = delete;
    Replayer& operator=(const Replayer&) = delete;

    // ---- the Peripheral interface (non-virtual) -----------------------------------------------------------------------
    int type() const { return int(TYPE); }
    int params() const { return int(PARAMS); }
    // The write port (17.8.9): a 128-bit WRITE at `offset` of the region (bits [23:0]), presented in cycle t.
    void write(int64_t t, int src, uint32_t offset, uint64_t lo, uint64_t hi);
    // The slot's: a packet the director accepted at t; the port presents it at t + RP_LATENCY. The slot may call it at
    // level 0 of t + 3, the replayer settled through t + 2 at most (units/peripheral.h).
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi);
    void run(int64_t t);                                   // START, a store presented in cycle t
    bool running(int64_t t) { settle(t); return s_running_; }
    bool idle(int64_t) const { return !busy_ && in_n_ == 0 && land_n_ == 0 && outq_n_ == 0 && quiet_ >= QUIET; }
    int64_t next_wake_at(int64_t) const { return INT64_MAX; }
    int errors(int64_t t);
    EventCounts& events() { return ev; }
    // A consuming load's effect, the load presented in the cycle `step` ran last: one count from each set bit, but a
    // credit filled, or set at its depth, in that cycle keeps its count (17.8.1; peripheral_control.veryl). Returns
    // the bits it took a count from.
    uint32_t consume_events(uint32_t bits);
    void sync(int64_t t) { settle(t); }
    bool word(int64_t t, int w, uint32_t& v);
    int write_word(int64_t t, int w, uint32_t v);
    bool writable(int w) const { return w == int(W_RUN_CONFIG); }   // words 33-35 read-only
    uint64_t violation_count() const { return violations(); }
    void reset(int64_t t);
    void step(int64_t T);   // cycle T

    // ---- for tests, the drivers and the harness ---------------------------------------------------------------------
    unsigned landed(int64_t t) const;
    // Register r as the rows' order leaves it: the write-back register's value while it is still to land.
    uint64_t reg(unsigned r) const { return r == 0 ? 0 : regs_[r]; }
    uint64_t pc() const { return pc_; }
    uint32_t instr_idx() const { return instr_idx_; }
    unsigned rows_done() const { return rows_run_; }
    unsigned next_offset_now() const { return row_offset_; }
    bool is_running() const { return busy_; }
    bool store_log_latched() const { return (run_config_ & RC_STORE_LOG) != 0; }
    uint32_t run_config_now() const { return run_config_; }
    unsigned rows_left_now() const { return rows_left_; }
    unsigned out_queue_rows() const { return outq_n_; }
    uint64_t cond_events(unsigned k) const { return cond_events_[k]; }
    const streamword::Size& size(unsigned s) const { return sz_[s]; }
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
    static void violation_text(unsigned me, int64_t t, uint16_t c, uint64_t a, uint64_t b, uint64_t x, char* o, size_t n);
    uint64_t list_count(unsigned l) const { return list_n_[l]; }
    uint32_t list_capacity() const { return list_cap_; }
    const char* list_entry(unsigned l, uint32_t i) const { return &list_text_[(size_t(l) * list_cap_ + i) * LIST_TEXT]; }

    // The sink (17.8.11), the row port's consumer: ready one cycle in sink_interval; a running hash of every record
    // (h = mix(h ^ word), word 0 first), the records into `buf` if given (capacity in rows; FATAL beyond it), and the
    // cycle each row left the port into `times` if given (the same capacity).
    void set_row_buffer(uint64_t* buf, uint64_t capacity_rows) { row_buf_ = buf; row_cap_ = capacity_rows; }
    void set_row_times(int64_t* times) { row_times_ = times; }
    uint64_t* row_buffer() const { return row_buf_; }
    uint64_t rows_sunk() const { return rows_sunk_; }
    // (harness-only; P2): the harness's row hook (tests/mach.h) drains the buffer between cycles: the rows from
    // row_base() on are in it, from its start; drained() moves the base to rows_sunk() (0 and unused otherwise).
    uint64_t row_base() const { return row_base_; }
    void drained() { row_base_ = rows_sunk_; }
    uint64_t row_hash() const { return row_hash_; }
    int64_t last_row_at() const { return last_sink_; }
    unsigned sink_interval = 1;

    // ---- state (public for the unit tests) ----------------------------------------------------------------------------
    unsigned me;
    uint64_t region0[4] = {};          // the state word and the stream word as written: lo, hi
    uint16_t decode_table[2048] = {};
    uint64_t rom_lo[512] = {}, rom_hi[512] = {};
    EventCounts ev;
    ReplayerQueue code, rlog, wlog, exp;
    StreamDecoder code_d, rlog_d, wlog_d;
    // statistics
    uint64_t stalls[N_STALL] = {};
    uint64_t pass_cycles = 0, rows_total = 0, rows_m = 0, rows_ctrl = 0, new_expansions = 0, passes = 0;
    uint64_t pext_non_contiguous = 0, pext_extra_cycles = 0, config_writes = 0, flushes = 0, ignored_writes = 0;
    int64_t pass_start_at = 0, pass_end_at = 0;

private:
    struct Input {
        int64_t t;
        unsigned kind;   // a write: the source; else IN_RUN, IN_CONFIG
        uint32_t a, b;   // a write: the offset, the size; config: the value
        uint64_t lo, hi;
    };
    static constexpr unsigned IN_RUN = 0x10000, IN_CONFIG = 0x10001, RING = 16;
    static constexpr unsigned QUIET = 3;   // cycles after a pass's end or an input in which the unit still settles
    enum : unsigned { FK_PADDING = 0, FK_ROM, FK_NOP, FK_CORE, FK_BAD };

    void violate(int64_t t, uint16_t code, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0);
    void record(int64_t t, uint16_t code, uint64_t a, uint64_t b, uint64_t c = 0);
    void list_add(int64_t t, uint16_t code, uint64_t a, uint64_t b, uint64_t c);
    void note_input(int64_t t);
    void settle(int64_t T);
    void do_run(int64_t t);
    void state_write(int64_t t, uint64_t lo, uint64_t hi);
    void present(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi);
    void land(int64_t t, unsigned src, uint32_t offset, unsigned size, uint64_t lo, uint64_t hi);
    void work(int64_t T);
    void sink(int64_t T);
    void start_row(int64_t T);
    void compute_row(int64_t T, uint64_t log_word);
    void post(int64_t T, unsigned bit);
    unsigned leftover_of(unsigned s, uint64_t& n) const;
    ReplayerQueue& queue(unsigned i) { return i == 0 ? code : i == 1 ? rlog : i == 2 ? wlog : exp; }
    const StreamDecoder& decoder(unsigned i) const { return i == 0 ? code_d : i == 1 ? rlog_d : wlog_d; }

    // ---- the pass and block 2 (replayer_pass.veryl)
    uint32_t run_config_ = 0;
    unsigned rows_left_ = 0, rows_run_ = 0;
    bool busy_ = false, started_ = false, check_ = false;
    uint32_t posted_offset_ = 0, posted_opcode_ = 0;
    bool state_written_ = false;
    streamword::Size sz_[3] = {};
    // ---- the fetch (replayer_fetch.veryl)
    bool f_first_ = true, f_entry_pending_ = false, f_active_ = false;
    uint32_t f_instr_ = 0;
    uint16_t f_entry_ = 0;             // the decode table's entry, read at the take
    unsigned f_kind_ = FK_PADDING, f_start_ = 0, f_index_ = 0;
    bool f_policy_known_ = false, f_temp_offset_ = false, f_nop_virtual_ = false;
    bool f_rom_pending_word_ = false, f_rom_pending_policy_ = false, f_rom_pending_padding_ = false, f_rom_pending_nop_ = false;
    unsigned f_rom_pending_index_ = 0;
    uint64_t f_rom_lo_ = 0, f_rom_hi_ = 0;   // the ROM's read data
    // the row word
    bool h_valid_ = false;
    uint64_t h_lo_ = 0, h_hi_ = 0;
    uint32_t h_code_ = 0;
    bool h_temp_offset_ = false, h_padding_ = false, h_nop_ = false, h_nop_virtual_ = false, h_invalid_ = false, h_core_ = false;
    unsigned h_index_ = 0;
    uint64_t h_bad_cause_ = 0, h_bad_detail_ = 0;
    // ---- the row engine (replayer_row_engine.veryl)
    bool e_reads_made_ = false, e_in_flight_ = false, e_in_cycle_2_ = false, e_done_seen_ = false;
    bool e_reads_take_read_ = false, e_reads_take_write_ = false;
    bool wb_pending_ = false;          // the write-back register still to land (its value is in regs_ already)
    unsigned wb_reg_ = 0;
    uint64_t wb_old_ = 0;              // the register's value before it, for an image packet that drops it
    int64_t wb_landed_at_ = INT64_MIN;
    uint64_t regs_[128] = {};
    uint64_t pc_ = 0;
    uint32_t instr_idx_ = 0;
    unsigned row_offset_ = 0;
    // the row in flight, latched at its start
    struct Row {
        uint64_t lo = 0, hi = 0;
        uint32_t w = 0;
        bool temp_offset = false, padding = false, nop = false, nop_virtual = false, core = false;
        unsigned index = 0;
        bool takes_read = false, takes_write = false, load = false;
        int r_rd = -1, r_rs1 = -1, r_rs2 = -1;   // each slot's register, or -1 for an immediate slot
        uint64_t v_rs1 = 0, v_rs2 = 0, v_rd = 0, src_imm = 0;   // each slot's value: its register's before the row, or its immediate
        // its outcomes, from its cycle 2
        bool last = false, control_log = false, writes = false, hold = false, stop = false, transfer = false;
        int rd_reg = -1;
        uint64_t rd_val = 0, new_pc = 0;
        uint64_t rec[REC_LONGS] = {};
    } row_;
    int64_t a_done_at_ = INT64_MIN;    // the cycle of the ALU's `done`
    int64_t a_sched_at_ = INT64_MIN;   // ... by the row's schedule (17.8.7)
    // ---- the checks of the write port's sources and credits (17.8.24)
    int src_of_[4] = {-1, -1, -1, -1};
    unsigned pushed_[4] = {}, allowance_[4] = {};
    uint64_t cond_events_[N_ERR] = {};
    // ---- the events' ties with a consuming load (peripheral_control.veryl): the cycle of each bit's fill, and of a set
    // that found its count at its depth
    int64_t ev_fill_at_[8], ev_full_at_[8];
    int64_t worked_ = INT64_MIN;       // the last cycle step ran
    unsigned quiet_ = QUIET;

    // inputs, deferred to the end of their cycle: fixed rings
    Input inputs_[RING] = {}, landing_[RING] = {};
    unsigned in_h_ = 0, in_n_ = 0, land_h_ = 0, land_n_ = 0;
    int64_t accept_times_[8] = {};
    uint64_t accept_n_ = 0;
    int64_t settled_ = INT64_MIN, last_input_t_ = INT64_MIN, last_accept_t_ = INT64_MIN;

    // level reads: the snapshot at the end of t - 1
    bool s_running_ = false;
    uint32_t s_rows_ = 0, s_run_config_ = 0, s_offset_ = 0, s_opcode_ = 0, s_errors_ = 0;

    // the output queue and the sink
    uint64_t outq_[OUTQ_ROWS][REC_LONGS] = {};
    unsigned outq_h_ = 0, outq_n_ = 0;
    int64_t last_sink_ = INT64_MIN / 2;
    uint64_t* row_buf_ = nullptr;
    int64_t* row_times_ = nullptr;
    uint64_t row_cap_ = 0, rows_sunk_ = 0, row_hash_ = 0, row_base_ = 0;

    // facts
    ReplayerFact facts_[FACT_RING] = {};
    std::unique_ptr<char[]> list_text_;   // N_LISTS x list_cap_ entries of LIST_TEXT bytes
    uint32_t list_cap_ = 0;
    uint64_t list_n_[N_LISTS] = {};
    unsigned fact_n_ = 0, fact_shown_ = 0;
    uint64_t fact_counts_[64] = {};

public:
    // The inputs the replayer holds at the end of cycle c that, in the framework, are still in the slot's links:
    // writes (accepted at c - 2 .. c: e = accept + 2, src, word address, size, lo, hi) and inputs (a START or
    // RUN_CONFIG store of c: e, kind 2 or 3, value). PeriphSlot::fingerprint_replayer builds it.
    struct FpPending {
        unsigned n_land = 0, n_in = 0;
        struct Land { int64_t e; unsigned src; uint32_t addr, size; uint64_t lo, hi; } land[4];
        struct In { int64_t e; unsigned kind; uint32_t value; } in[2];
    };
    // The fingerprint of its state at the end of cycle c: every register of the model, in the order below. Out: FpOut
    // (tests/statefp.h), or any sink with w(name, v) and wi(name, i, v). `pending`: the inputs still in the slot's
    // links (in a chip; its own lists are then empty); null for a replayer driven directly.
    template <class Out> void fingerprint(Out& o, int64_t c, const FpPending* pending = nullptr) const;
};

template <class Out>
void Replayer::fingerprint(Out& o, int64_t c, const FpPending* pending) const {
    auto sx = [](int64_t v) { return uint64_t(v); };
    auto mx = [c](int64_t v) { return uint64_t(v > c ? v - c : 0); };
    auto term64 = [](uint64_t region, uint64_t index, uint64_t v) { return fmix64(fmix64(region << 56 ^ index) ^ v); };
    // 1. the state word and the stream word as written, the registers, the tables as memory sums, block 2
    for (unsigned k = 0; k < 4; k++) o.wi("region0.", k, region0[k]);
    for (unsigned k = 1; k < 128; k++) o.wi("reg.", k, regs_[k]);
    uint64_t sd = 0, sr = 0;
    for (unsigned k = 0; k < 2048; k++) if (decode_table[k]) sd += term64(12, k, decode_table[k]);
    o.w("decodeTable", sd);
    for (unsigned j = 0; j < 512; j++) {
        if (rom_lo[j]) sr += term64(13, 2 * uint64_t(j), rom_lo[j]);
        if (rom_hi[j]) sr += term64(13, 2 * uint64_t(j) + 1, rom_hi[j]);
    }
    o.w("rom", sr);
    o.w("runConfig", run_config_); o.w("rowsLeft", rows_left_); o.w("rowsRun", rows_run_); o.w("busy", busy_);
    o.w("started", started_); o.w("check", check_); o.w("postedOffset", posted_offset_); o.w("postedOpcode", posted_opcode_);
    o.w("stateWritten", state_written_);
    // 2. the queues and the decoders
    for (const ReplayerQueue* q : {&code, &rlog, &wlog, &exp}) {
        o.w("q.occ", q->occ);
        for (unsigned k = 0; k < q->occ; k++) { const unsigned i = (q->head + k) % ReplayerQueue::CAP; o.w("q.lo", q->lo[i]); o.w("q.hi", q->hi[i]); }
        o.w("q.left", q->left); o.w("q.popAtC", q->pop_at == c ? 1 : 0);
    }
    for (const StreamDecoder* d : {&code_d, &rlog_d, &wlog_d}) {
        o.w("d.position", d->position); o.w("d.straddle", d->straddle);
        if (d->straddle) { o.w("d.tail", d->tail_lo >> 63 | d->tail_hi << 1); o.w("d.tail", d->tail_hi >> 63); }
        o.w("d.taken", d->taken); o.w("d.decoded", d->values_decoded()); o.w("d.movePending", d->move_pending);
        if (d->move_pending) o.w("d.moveLast", d->move_last);
        for (unsigned k = 0; k < 16; k++) o.w("d.E", d->E[k]);
        o.w("d.valueValid", d->value_valid);
        if (d->value_valid) o.w("d.value", d->value());
    }
    // 3. the fetch and the row word
    o.w("f.first", f_first_); o.w("f.entryPending", f_entry_pending_); o.w("f.active", f_active_);
    if (f_entry_pending_ || f_active_) {
        o.w("f.instr", f_instr_); o.w("f.kind", f_entry_pending_ ? f_entry_ : f_kind_); o.w("f.start", f_start_); o.w("f.index", f_index_);
        o.w("f.policyKnown", f_policy_known_); o.w("f.tempOffset", f_temp_offset_); o.w("f.nopVirtual", f_nop_virtual_);
    }
    o.w("f.romPendingWord", f_rom_pending_word_); o.w("f.romPendingPolicy", f_rom_pending_policy_);
    if (f_rom_pending_word_ || f_rom_pending_policy_) { o.w("f.romLo", f_rom_lo_); o.w("f.romHi", f_rom_hi_); }
    o.w("h.valid", h_valid_);
    if (h_valid_) {
        o.w("h.lo", h_lo_); o.w("h.hi", h_hi_); o.w("h.code", h_code_); o.w("h.index", h_index_);
        o.w("h.flags", (h_temp_offset_ ? 1u : 0u) | (h_padding_ ? 2u : 0u) | (h_nop_ ? 4u : 0u) | (h_nop_virtual_ ? 8u : 0u) | (h_invalid_ ? 16u : 0u));
    }
    // 4. the row engine
    o.w("e.readsMade", e_reads_made_); o.w("e.inFlight", e_in_flight_); o.w("e.inCycle2", e_in_cycle_2_); o.w("e.doneSeen", e_done_seen_);
    o.w("e.wbPending", wb_pending_);
    o.w("pc", pc_); o.w("instrIdx", instr_idx_); o.w("rowOffset", row_offset_);
    if (e_in_flight_) {
        o.w("row.lo", row_.lo); o.w("row.hi", row_.hi); o.w("row.w", row_.w); o.w("row.index", row_.index);
        o.w("row.doneIn", mx(a_done_at_));
        if (!e_in_cycle_2_) for (unsigned k = 0; k < REC_LONGS; k++) o.w("row.rec", row_.rec[k]);
    }
    // 5. the events' counts, ERRORS
    for (unsigned b = 0; b < ev.n; b++) o.wi("ev", b, ev.count_of(b));
    uint64_t err = 0;
    for (unsigned k = 0; k < N_ERR; k++) if (cond_events_[k] > 0) err |= uint64_t(1) << k;
    o.w("errors", err);
    // 6. the output queue, oldest first
    o.w("outQ.n", outq_n_);
    for (unsigned k = 0; k < outq_n_; k++) {
        const unsigned i = (outq_h_ + k) % OUTQ_ROWS;
        for (unsigned j = 0; j < REC_LONGS; j++) o.w("out", outq_[i][j]);
    }
    // 7. inputs not yet in effect: its own lists, then (in a chip) the slot's links'
    if (pending && (land_n_ || in_n_)) FATAL("replayer %u: fingerprint: inputs both in its lists and in the slot's links", me);
    const unsigned nl = land_n_ + (pending ? pending->n_land : 0), ni = in_n_ + (pending ? pending->n_in : 0);
    o.w("landing.n", nl);
    for (unsigned k = 0; k < land_n_; k++) {
        const Input& x = landing_[(land_h_ + k) % RING];
        o.w("e", sx(x.t)); o.w("src", x.kind); o.w("addr", x.a >> 2); o.w("size", 2); o.w("lo", x.lo); o.w("hi", x.hi);
    }
    if (pending)
        for (unsigned k = 0; k < pending->n_land; k++) {
            const FpPending::Land& x = pending->land[k];
            o.w("e", sx(x.e)); o.w("src", x.src); o.w("addr", x.addr); o.w("size", x.size); o.w("lo", x.lo); o.w("hi", x.hi);
        }
    o.w("inputs.n", ni);
    for (unsigned k = 0; k < in_n_; k++) {
        const Input& x = inputs_[(in_h_ + k) % RING];
        o.w("e", sx(x.t)); o.w("kind", x.kind == IN_RUN ? 2 : 3); o.w("value", x.kind == IN_CONFIG ? x.a : 0);
    }
    if (pending)
        for (unsigned k = 0; k < pending->n_in; k++) { o.w("e", sx(pending->in[k].e)); o.w("kind", pending->in[k].kind); o.w("value", pending->in[k].value); }
}

}  // namespace units
