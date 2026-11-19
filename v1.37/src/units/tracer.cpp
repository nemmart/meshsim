// tracer.cpp: the Tracer (tracer.h): the control, then the cycle function, a model of the RTL (Snarky One's
// rtl/blocks/tracer_unit) register for register where a register decides a cycle, then the output path.
#include "units/tracer.h"

#include "check.h"

#include <cstdio>
#include <cstring>

#include "base/fatal.h"

namespace units {
namespace tracer {

namespace {
// The events' kinds (17.9.1): done and new_expansion counted to 1, the two credits to 2 and flagged when a post finds
// them full; dma_done is the slot's (its DMA posts it); the four _ready events are live.
constexpr uint8_t kDepths[Tracer::EV_BITS] = {1, 1, 2, 2, 1, 0, 0, 0, 0};
constexpr bool kFlags[Tracer::EV_BITS] = {false, false, true, true, false, false, false, false, false};

// replayer_alu_pkg's writes_rd: every executed opcode but NOP, the branches, the stores, the assertions, VirtualHostIO
// and FENCE, whose rd slot is an immediate.
bool writes_rd(unsigned op) {
    switch (op) {
        case alu::NOP: case alu::BEQ: case alu::BNE: case alu::BLT: case alu::BGE: case alu::BLTU: case alu::BGEU:
        case alu::FENCE: case alu::SD: case alu::STORE_B: case alu::STORE_H: case alu::STORE_W: case alu::ASSERT_EQ:
        case alu::ASSERT_LTE: case alu::ASSERT_DIV0: case alu::ASSERT_UREM: case alu::ASSERT_MULU: case alu::HOSTIO:
            return false;
        default: return true;
    }
}
}  // namespace

const char* Tracer::error_name(int code) {
    switch (code) {
        case E_RQ_FULL: return "a push into the full read queue (dropped)";
        case E_EQ_FULL: return "a push into the full expansion queue (dropped)";
        case E_CLASS3: return "decode-table class 3";
        case E_NO_32BIT_FORM: return "a 16-bit encoding with no 32-bit form";
        case E_ROM_PAST_511: return "a template past ROM word 511";
        case E_INVALID_OPCODE: return "an expansion word's opcode is not executed by the replayer";
        case E_INVALID_OPERAND: return "a reserved operand code (146-255)";
        case E_INVALID_FORMAT: return "an expansion word's format 6 or 7";
        case E_ADVICE_ZERO: return "an advice row with the advice field at 0";
        case E_MEM_RANGE_ALIGN: return "a memory row outside the provable range or misaligned";
        case E_STORE_NOT_WRITABLE: return "a store to a line that isn't writable (a hole or a page not owned)";
        case E_ASSERT: return "an assertion failed";
        case E_PC0: return "a control transfer to pc 0 (17.9.4)";
        case E_IFETCH_BEYOND: return "an instruction fetch beyond the file's capacity";
        case E_DATA_BEYOND: return "a data fill beyond the file's capacity";
        case E_MAP_OFFSET: return "a jump whose offset from lowest_address doesn't fit 32 bits";
        case E_MAP_HOLE: return "the jump map's line is a hole (a dense file: corrupt)";
        case E_MAP_BEYOND: return "the jump map's line is beyond its capacity";
        case E_MAP_FREE: return "jump target not in the map (a free entry)";
        case E_MAP_FULL: return "jump target not in the map (probe_lines full lines)";
        case E_WRITE_BACK: return "a write-back answered `error` (17.9.5)";
        default: return "?";
    }
}
const char* Tracer::violation_name(int code) {
    switch (code) {
        case V_ADVICE_UNCONSUMED: return "a word-0 write with advice still unconsumed: a mid-segment rewrite is UB (4.21.3)";
        case V_UNDEFINED: return "undefined result";
        case V_END_NO_NFV: return "the program's end on a JAL row without next_from_value";
        case V_XFER_NO_NFV: return "a control transfer from a row without next_from_value";
        case V_INLINE_NO_TEMPLATE:
            return "an inline expansion completed a row with no EXPANSION_TEMPLATE store since its new expansion (item 145)";
        default: return "?";
    }
}


Tracer::Tracer(int me, int icache_lines, int seen_capacity) : me_(me), icache_lines_(icache_lines) {
    if (icache_lines < 1) FATAL("Tracer: icache_lines %d", icache_lines);
    icache_.init(icache_lines);
    dcache_.init(CACHE_LINES);
    jump_lru_.init(LRU_ENTRIES);
    i_seen_.init(seen_capacity);
    iline_ = new Line[size_t(icache_lines) + 1];
    dline_ = new Line[size_t(CACHE_LINES) + 1];
    code_fifo = new uint32_t[MAX_ROWS];
    log_fifo = new uint64_t[MAX_ROWS];
    log_tag = new uint8_t[MAX_ROWS];
    ev.init(EV_BITS, kDepths, kFlags);
}

Tracer::~Tracer() {
    delete[] iline_;
    delete[] dline_;
    delete[] code_fifo;
    delete[] log_fifo;
    delete[] log_tag;
}

void Tracer::files_word(int32_t mem_root, int mem_depth, int32_t map_root, int map_depth, int log2k, int64_t lowest,
                        uint64_t out[2]) {
    const int64_t io = 0x80000000LL - lowest;
    if (io < 0 || io >= (int64_t(1) << 31))
        FATAL("lowest_address 0x%llx: the I/O region must be under 2 GiB below 0x8000_0000", (unsigned long long)lowest);
    out[0] = (uint64_t(uint32_t(mem_root)) & 0x7FFFFFFF) | uint64_t(mem_depth & 3) << 31 |
             (uint64_t(uint32_t(map_root)) & 0x7FFFFFFF) << 33;
    out[1] = uint64_t(map_depth & 3) | uint64_t(log2k & 63) << 2 | uint64_t(io) << 8;
}

// ================================================================ facts
void Tracer::violate(int64_t t, int code, uint64_t a, uint64_t b) {
    if (n_violations < FACTS) {
        Fact& f = violations[n_violations];
        f.t = t; f.code = code; f.bits = 0; f.a = a; f.b = b;
    }
    n_violations++;
    sim::check::record(uint64_t(t), me_, sim::check::Kind::TRACER_VIOLATION, uint64_t(code), a, b);
}
void Tracer::violation_text(int code, uint64_t a, uint64_t b, char* o, size_t n) {
    char nb[24];
    auto op = [&](uint64_t v) -> const char* {
        const char* nm = v < 256 ? alu::name_or_null(unsigned(v)) : nullptr;
        if (nm) return nm;
        std::snprintf(nb, sizeof nb, "op%lld", (long long)int32_t(uint32_t(v)));
        return nb;
    };
    switch (code) {
        case V_ADVICE_UNCONSUMED: std::snprintf(o, n, "a word-0 write with advice still unconsumed (%llu values): a mid-segment rewrite is UB (17.9.11)", (unsigned long long)a); break;
        case V_UNDEFINED: std::snprintf(o, n, "undefined result: %s in %s", b ? reinterpret_cast<const char*>(uintptr_t(b)) : "?", op(a)); break;
        case V_XFER_NO_NFV: std::snprintf(o, n, "a control transfer from a row without next_from_value (%s)", op(a)); break;
        default: std::snprintf(o, n, "%s", violation_name(code)); break;
    }
}
// Sets ERRORS bits, in the cycle of the error's pulse; every error but queue overrun stops the Tracer from the next
// cycle (17.9.9; tracer_run.veryl's `stopped`).
void Tracer::error(int64_t t, int bits, int code, uint64_t a, uint64_t b) {
    errors_ |= bits;
    if (n_errors_logged < FACTS) {
        Fact& f = error_log[n_errors_logged];
        f.t = t; f.code = code; f.bits = bits; f.a = a; f.b = b;
    }
    n_errors_logged++;
    if ((bits & ~(1 << ERR_OVERRUN)) != 0) stopping_ = true;
}
void Tracer::ub(int64_t t, const char* what, uint64_t a, uint64_t b) const {
    FATAL("undefined behaviour at %lld in the Tracer (%d): %s (0x%llx, 0x%llx)", (long long)t, me_, what,
          (unsigned long long)a, (unsigned long long)b);
}

void Tracer::Ring::push(const Input& x) {
    if (n == INPUTS) FATAL("Tracer: more than %d inputs pending", INPUTS);
    e[(head + n) % INPUTS] = x;
    n++;
}

// ================================================================ inputs: deferred to the end of their cycle
void Tracer::accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
    if (t == last_accept_) FATAL("Tracer: two region packets in cycle %lld", (long long)t);
    if (t < last_accept_) FATAL("Tracer: accept's time went backwards %lld -> %lld", (long long)last_accept_, (long long)t);
    if (t + RP_LATENCY < settled_)
        FATAL("Tracer: a landing at %lld after cycle %lld was settled", (long long)(t + RP_LATENCY), (long long)settled_);
    last_accept_ = t;
    landing_.push(Input{t + RP_LATENCY, src, int(offset), size, lo, hi});
}
void Tracer::run(int64_t t) {
    if (t < settled_) FATAL("Tracer: START at %lld after cycle %lld was settled", (long long)t, (long long)settled_);
    inputs_.push(Input{t, IN_RUN, 0, 0, 0, 0});
}

bool Tracer::word(int64_t t, int w, uint32_t& v) {
    settle(t);
    switch (w) {
        case W_RUN_CONFIG: v = s_run_config_; return true;
        case W_ROWS_RUN: v = uint32_t(s_rows_); return true;
        case W_EXP_OFFSET: v = uint32_t(s_exp_offset_); return true;
        case W_EXP_OPCODE: v = s_exp_opcode_; return true;
        case W_STATE_LENGTH: v = REC_WORDS; return true;
        case W_CODE_LENGTH: v = s_code_l_; return true;
        case W_RLOG_LENGTH: v = s_rlog_l_; return true;
        case W_WLOG_LENGTH: v = s_wlog_l_; return true;
        case W_HASH_MUL: v = s_mul_; return true;
        case W_HASH_STEP: v = s_step_; return true;
        case W_HASH_LINES: v = s_lines_; return true;
        case W_EXP_TEMPLATE: v = 0; return true;
        default:
            if (w >= W_STAT && w < W_STAT + STAT_N) { v = s_stat_[w - W_STAT]; return true; }
            return false;
    }
}
int Tracer::write_word(int64_t t, int w, uint32_t v) {
    switch (w) {
        case W_RUN_CONFIG: case W_HASH_MUL: case W_HASH_STEP: case W_HASH_LINES: case W_EXP_TEMPLATE:
            if (t < settled_) FATAL("Tracer: a word-%d store at %lld after cycle %lld was settled", w, (long long)t, (long long)settled_);
            inputs_.push(Input{t, IN_WORD, w, int(v), 0, 0});
            return 1;
        case W_ROWS_RUN: case W_EXP_OFFSET: case W_EXP_OPCODE: case W_STATE_LENGTH: case W_CODE_LENGTH:
        case W_RLOG_LENGTH: case W_WLOG_LENGTH:
            return 0;  // read-only
        default: return w >= W_STAT && w < W_STAT + STAT_N ? 0 : -1;  // the counters: read-only
    }
}

// The reset (17.9.12): ends any run; empties every queue, both FIFOs and the write-back buffer; invalidates both caches
// and the jump LRU, writing no dirty line back; zeroes STATUS, EVENTS, ERRORS, RUN_CONFIG, ROWS_RUN, EXPANSION_OFFSET,
// EXPANSION_OPCODE, the length words, the hash's constants, the statistics counters, the template latch and the
// bitmap. The state word, the stream word, the files word, the register file, the decode table and the ROM keep their
// contents. (EVENTS_MASK is the control block's, the slot's.)
void Tracer::reset(int64_t) {
    inputs_.clear(); landing_.clear(); settled_ = INT64_MIN;
    running_ = stopped_ = stopping_ = started_ = rows_ended_ = row_done_reg_ = state_written_ = false;
    rows_left_ = rows_done_ = 0; errors_ = 0; n_errors_logged = 0;
    run_config_ = hash_mul_ = hash_step_ = hash_lines_ = 0; length_code_ = length_rlog_ = length_wlog_ = 0;
    lengths_valid_ = false; presented_ = 0; expansion_offset_ = 0; latch_ = -1; template_stored_ = false;
    bitmap_[0] = bitmap_[1] = 0; rec[2 * REC_BITMAP] = rec[2 * REC_BITMAP + 1] = 0;
    rq_head_ = rq_occ_ = rq_half_ = rq_popped_ = 0; eq_head_ = eq_occ_ = eq_popped_ = 0;
    rq_pop_t_ = eq_pop_t_ = INT64_MIN;
    icache_.clear(); dcache_.clear(); jump_lru_.clear(); i_seen_.clear();
    rows_ = Rows(); tpl_ = Templates(); ins_ = Instructions(); xf_ = Fetch(); xd_ = Data(); fill_ = Fill(); xj_ = Jump();
    code_n = log_n = 0; rlog_values_ = wlog_values_ = 0;
    run_active_ = end_in_ = choosing_ = draining_ = done_ = false; q0ptr_ = REC_WORDS; q0_holding_ = false;
    code_next_ = log_next_ = 0; code_data_valid_ = log_data_valid_ = false; rlog_given_ = wlog_given_ = 0;
    for (int q = 1; q <= 3; q++) { oq[q].reset(); pk_[q].reset(); }
    xfer_queue_ = -1; xfer_left_ = 0; dma_busy_from_ = INT64_MAX; dma_last_take_ = INT64_MIN;
    ev.clear_all(); ev.live_bits = 0;
    for (int i = 0; i < STAT_N; i++) stat[i] = 0;
}

// Applies every input and landing of cycles before T, then takes the snapshot that reads at T return.
void Tracer::settle(int64_t T) {
    if (T <= settled_) return;
    while (true) {
        const int64_t tl = landing_.n == 0 ? INT64_MAX : landing_.peek().t;
        const int64_t ti = inputs_.n == 0 ? INT64_MAX : inputs_.peek().t;
        const int64_t tm = tl < ti ? tl : ti;
        if (tm >= T) break;
        if (tl == tm) {
            const Input p = landing_.pop();
            land(p.t, p.kind, uint32_t(p.a), p.b, p.lo, p.hi);
            continue;
        }
        const Input in = inputs_.pop();
        if (in.kind == IN_RUN) do_run(in.t); else store_word(in.t, in.a, uint32_t(in.b));
    }
    settled_ = T;
    s_running_ = running_; s_errors_ = errors_; s_rows_ = rows_done_; s_run_config_ = run_config_;
    s_exp_offset_ = expansion_offset_; s_exp_opcode_ = presented_;
    s_code_l_ = lengths_valid_ ? length_code_ : 0; s_rlog_l_ = lengths_valid_ ? length_rlog_ : 0;
    s_wlog_l_ = lengths_valid_ ? length_wlog_ : 0;
    s_mul_ = hash_mul_; s_step_ = hash_step_; s_lines_ = hash_lines_;
    for (int i = 0; i < STAT_N; i++) s_stat_[i] = stat[i];
    // the live events: the output's levels, withdrawn after a stop (17.9.9)
    ev.live(EV_SVC_STATE, q0_holding_ && !stopped_);
    for (int q = 1; q <= 3; q++) ev.live(EV_SVC_STATE + unsigned(q), oq[q].credits > 0 && !stopped_);
}

void Tracer::store_word(int64_t t, int w, uint32_t v) {
    switch (w) {
        case W_RUN_CONFIG:
            if (running_) ub(t, "a RUN_CONFIG store while busy (17.9.1)");
            run_config_ = v & RC_DEFINED;
            break;
        case W_HASH_MUL:
            if (running_) ub(t, "a HASH_MUL store while busy (17.9.1)");
            hash_mul_ = v;
            break;
        case W_HASH_STEP:
            if (running_) ub(t, "a HASH_STEP store while busy (17.9.1)");
            hash_step_ = v & 0xFFFFFF;
            break;
        case W_HASH_LINES:
            if (running_) ub(t, "a HASH_LINES store while busy (17.9.1)");
            hash_lines_ = v & 31;
            break;
        case W_EXP_TEMPLATE:
            if (v & 0x80000000u) {
                const int tn = int(v & 0x7F);
                bitmap_[tn >> 6] |= uint64_t(1) << (tn & 63);
                latch_ = tn;
                template_stored_ = true;
            } else {
                latch_ = -1;
            }
            rec[2 * REC_BITMAP] = bitmap_[0]; rec[2 * REC_BITMAP + 1] = bitmap_[1];
            break;
        default: FATAL("Tracer: store_word %d", w);
    }
}

// ================================================================ the write port (17.9.2)
// The packet the write port presents in cycle t, landing at its end. The record's spare bits are not held: the state
// word's [127:112], the stream word's [127:125], the files word's [127:103]; register 0 reads 0 (tracer_record.veryl,
// tracer_rows.veryl).
void Tracer::land(int64_t t, int, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
    const uint32_t part = offset >> 20;
    if (size != 2 || (offset & 15) != 0) ub(t, "a 32- or 64-bit or misaligned packet (17.9.2: 128-bit packets only)", offset, uint64_t(size));
    switch (part) {
        case 0: {
            const int wd = int(offset >> 4);
            if (offset < 0x440) {
                if (running_) ub(t, "a part-0 write while busy (17.9.2): record word", uint64_t(wd));
                if (wd == REC_BITMAP) return;  // accepted and dropped
                if (wd == 0) hi &= 0x0000FFFFFFFFFFFFull;
                if (wd == 1) hi &= 0x1FFFFFFFFFFFFFFFull;
                if (wd == 2) lo = 0;
                if (wd == REC_FILES) hi &= 0x0000007FFFFFFFFFull;
                rec[2 * wd] = lo; rec[2 * wd + 1] = hi;
                if (wd == 0) state_write(t);
            } else if (offset >= 0x1000 && offset < 0x2000) {
                if (running_) ub(t, "a decode-table write while busy (17.9.2)", offset);
                const int e0 = int((offset - 0x1000) >> 4) * 8;
                for (int k = 0; k < 8; k++)
                    decode_table[e0 + k] = uint16_t((k < 4 ? lo >> (16 * k) : hi >> (16 * (k - 4))) & 0xFFFF);
            } else if (offset >= 0x2000 && offset < 0x4000) {
                if (running_) ub(t, "a ROM write while busy (17.9: the ROM is written only while idle)", offset);
                const int j = int((offset - 0x2000) >> 4);
                rom_lo[j] = lo; rom_hi[j] = hi;
            } else {
                ub(t, "a write to an unmapped part-0 offset", offset);
            }
            break;
        }
        case 2: {  // full by the count before the cycle's pop (17.9.3)
            if (rq_occ_ + (rq_pop_t_ == t ? 1 : 0) >= QCAP) { error(t, 1 << ERR_OVERRUN, E_RQ_FULL); return; }
            const int i = (rq_head_ + rq_occ_) % QCAP;
            rq_lo_[i] = lo; rq_hi_[i] = hi; rq_occ_++;
            break;
        }
        case 4: {
            if (eq_occ_ + (eq_pop_t_ == t ? 1 : 0) >= QCAP) { error(t, 1 << ERR_OVERRUN, E_EQ_FULL); return; }
            const int i = (eq_head_ + eq_occ_) % QCAP;
            eq_lo_[i] = lo; eq_hi_[i] = hi; eq_occ_++;
            break;
        }
        default:
            ub(t, "a write to an unmapped part (17.9.2)", part);
    }
}

// The state write, as it lands (17.9.3): the read queue, the expansion queue, both FIFOs and output queues 1-3 emptied,
// output queue 0's pointer reset; the two input credits filled, the output queues' cleared; the half-entry pointer, the
// length words and the statistics counters cleared; both caches and the jump LRU invalidated, the write-back buffer's
// line dropped (a write already requested still holds the buffer's place until its response); the template latch and
// the bitmap cleared. The execution starts over from the state word: no row in flight, the next instruction at its pc.
void Tracer::state_write(int64_t t) {
    // word 0 lands before word 1 (state first), so the advice field is still the last segment's leftover here. The
    // reset ends a segment and keeps the stream word (17.9.12): the first state write after it rewrites nothing.
    if (state_written_ && advice_left() > 0) violate(t, V_ADVICE_UNCONSUMED, advice_left());
    state_written_ = true;
    rq_head_ = rq_occ_ = rq_half_ = rq_popped_ = 0; eq_head_ = eq_occ_ = eq_popped_ = 0;
    code_n = log_n = 0; rlog_values_ = wlog_values_ = 0;
    length_code_ = length_rlog_ = length_wlog_ = 0; lengths_valid_ = false;
    run_active_ = end_in_ = choosing_ = draining_ = false; q0ptr_ = 0; q0_holding_ = false;
    code_next_ = log_next_ = 0; code_data_valid_ = log_data_valid_ = false; rlog_given_ = wlog_given_ = 0;
    for (int q = 1; q <= 3; q++) { oq[q].reset(); pk_[q].reset(); }
    ev.set(t, EV_ADVICE, 2); ev.set(t, EV_EXP, 2);
    icache_.clear(); dcache_.clear(); jump_lru_.clear(); i_seen_.clear();
    if (!fill_.write_requested) fill_.write_held = false;
    fill_.write_line_kept = false;
    latch_ = -1; bitmap_[0] = bitmap_[1] = 0; rec[2 * REC_BITMAP] = rec[2 * REC_BITMAP + 1] = 0;
    for (int i = 0; i < STAT_N; i++) stat[i] = 0;
    // tracer_rows: no row in flight; replayer_fetch: empty, the next instruction the run's first; tracer_instruction:
    // the next pc the state word's, nothing fetched or held
    Rows fresh;
    fresh.run_first = rows_.run_first;
    rows_ = fresh;
    tpl_ = Templates();
    ins_ = Instructions();
    ins_.known = true; ins_.next_pc = rec[0];
    if (cold_mem_on_state_write && mem) mem->cold();
    state_writes++;
}

// ================================================================ START (17.9.3)
// START presented in cycle t: at its end STATUS.busy rises, ROWS_RUN clears, the rows the run may start are
// RUN_CONFIG.rows; the output starts over (the FIFOs, the packers, output queues 1-3 and their credits, the length
// words, both sets' tables); the bitmap clears, then takes the latched template's bit if the run starts inside a
// core-fed expansion with a template latched. The record, the input queues and their credits, the caches, the jump LRU
// and the latch stay.
void Tracer::do_run(int64_t t) {
    if (running_) ub(t, "START while busy (17.2)");
    if (!state_written_) ub(t, "START with no state write since the reset (17.9.3)");
    const int n = int(run_config_ & RC_ROWS);
    if (n > MAX_ROWS) ub(t, "RUN_CONFIG.rows above 65,536 (17.9.1)", uint64_t(n));
    if (!mem && !fill_port) FATAL("Tracer (%d): START at %lld without a memory system (own_memory, mem or fill_port, before the run)", me_, (long long)t);
    running_ = true; rows_ended_ = false; done_ = false; started_ = true;
    rows_left_ = n; rows_done_ = 0; runs++; run_start_at = t;
    rows_.run_first = true;
    code_n = log_n = 0; rlog_values_ = wlog_values_ = 0;
    set1_code_.reset(); set1_rlog_.reset(); set1_wlog_.reset();
    length_code_ = length_rlog_ = length_wlog_ = 0; lengths_valid_ = false;
    run_active_ = true; end_in_ = choosing_ = draining_ = false; q0ptr_ = 0; q0_holding_ = false;
    code_next_ = log_next_ = 0; code_data_valid_ = log_data_valid_ = false; rlog_given_ = wlog_given_ = 0;
    for (int q = 1; q <= 3; q++) { oq[q].reset(); pk_[q].reset(); }
    bitmap_[0] = bitmap_[1] = 0;
    if (tpl_.core_fed && state_offset() != 0 && latch_ >= 0) bitmap_[latch_ >> 6] |= uint64_t(1) << (latch_ & 63);
    rec[2 * REC_BITMAP] = bitmap_[0]; rec[2 * REC_BITMAP + 1] = bitmap_[1];
}

bool Tracer::quiet() const {
    return xf_.state == XF_IDLE && xd_.state == XD_IDLE && xj_.state == XJ_IDLE && !fill_.read_request_valid &&
           !fill_.read_outstanding && !fill_.write_held && !fill_.write_request_valid && !fill_.write_outstanding &&
           !fill_.map_pending && !fill_.map_line_valid && !tpl_.rom_pending_word && !tpl_.rom_pending_policy &&
           !tpl_.entry_pending && !row_done_reg_ && !started_ && !rows_.reads_made && !rows_.wb_pending &&
           !xj_.probe_answer && !ins_.requesting;
}

// Whether the state write lands at the end of cycle T: the fill port's `invalidate`, which holds a write-back's
// request in its cycle (tracer_fill.veryl).
bool Tracer::state_write_lands_at(int64_t T) const {
    for (int k = 0; k < landing_.n; k++) {
        const Input& x = landing_.e[(landing_.head + k) % INPUTS];
        if (x.t == T && uint32_t(x.a) == 0) return true;
        if (x.t > T) break;
    }
    return false;
}

// ================================================================ the cycle
void Tracer::step(int64_t T) {
    settle(T);
    cycle(T);
    if (fill_port && fill_port->tick) fill_port->tick(fill_port->ctx, T);   // v1.37: a clocked memory system's edge
}

// The row word's checks and kind (tracer_rows.veryl: head_invalid, head_advice), of the word the templates hold.
void Tracer::head_decode() {
    using namespace rowcore;
    const uint64_t lo = tpl_.head_lo;
    hd_.invalid = false; hd_.advice = false; hd_.invalid_code = 0;
    if (tpl_.head_invalid) { hd_.invalid = true; hd_.invalid_code = tpl_.head_invalid_code; return; }
    const unsigned why = invalid_word(lo);
    if (why != INV_OK) {
        hd_.invalid = true;
        hd_.invalid_code = why == INV_OPCODE ? E_INVALID_OPCODE : why == INV_OPERAND ? E_INVALID_OPERAND : E_INVALID_FORMAT;
        return;
    }
    hd_.advice = from_log(lo) && op(lo) == alu::ADVICE;
}

// A row's start, its cycle 1 (tracer_rows.veryl): the word's operands (replayer_operands.veryl), the registers' values
// (every earlier row's write is in the record: rows never overlap), the advice value, and what the ALU makes of them
// (replayer_alu.veryl), kept for the row's later cycles: its address, what it does to pc, its value.
void Tracer::row_start(int64_t T) {
    using namespace rowcore;
    Rows& r = rows_;
    const uint64_t lo = tpl_.head_lo, hi = tpl_.head_hi;
    const uint32_t w = tpl_.head_code;
    if (reserved(lo) != 0) ub(T, "an expansion word with reserved bits [61:55] set", lo);
    const unsigned op = rowcore::op(lo), c_rd = rowcore::c_rd(lo), c_rs1 = rowcore::c_rs1(lo), c_rs2 = rowcore::c_rs2(lo);
    const unsigned fmt = format(lo), toff = tpl_.head_temporary_offset ? 1 : 0;
    const bool nfv = next_from_value(lo), hld = hold(lo), i0p0 = imm0pc0(lo), flog = from_log(lo), oimm = offset_imm(lo);
    const uint64_t src_imm = needs_source_imm(lo) ? source_imm(fmt, w) : 0, so = oimm ? src_imm : 0;
    const int r_rd = reg(c_rd, w, toff), r_rs1 = reg(c_rs1, w, toff), r_rs2 = reg(c_rs2, w, toff);
    const uint64_t v_rd = r_rd >= 0 ? reg_value(r_rd) : imm_val(c_rd, hi, src_imm);
    const uint64_t v_rs1 = r_rs1 >= 0 ? reg_value(r_rs1) : imm_val(c_rs1, hi, src_imm);
    const uint64_t v_rs2 = r_rs2 >= 0 ? reg_value(r_rs2) : imm_val(c_rs2, hi, src_imm);
    const uint64_t imm_sum = (r_rd < 0 ? v_rd : 0) + (r_rs1 < 0 ? v_rs1 : 0) + (r_rs2 < 0 ? v_rs2 : 0);

    r.op = op;
    r.last = seq_remaining(lo) == 0 || tpl_.head_padding || tpl_.head_nop;
    r.memory = flog && (alu::is_load(op) || alu::is_store(op));
    r.store = alu::is_store(op);
    r.advice = flog && op == alu::ADVICE;
    r.end = r.last && op == alu::JAL && i0p0 && r_rs1 < 0 && v_rs1 == 0;
    r.writes = writes_rd(op) && r_rd > 0;
    r.rd = r_rd;
    r.rs2 = v_rs2;
    r.code = w;
    r.index = tpl_.head_index;
    r.padding = tpl_.head_padding;
    r.first = tpl_.head_index == 0 || tpl_.head_padding || tpl_.head_nop || r.run_first;
    r.mul = alu::cls(op) == 'M';
    r.advice_value = 0;
    if (r.advice) {  // the read queue's value, taken with the start (tracer_read_queue.veryl)
        r.advice_value = rq_half_ == 0 ? rq_lo_[rq_head_] : rq_hi_[rq_head_];
        set_advice_left(advice_left() - 1);
        if (rq_half_ == 0) {
            rq_half_ = 1;
        } else {
            rq_half_ = 0; rq_head_ = (rq_head_ + 1) % QCAP; rq_occ_--; rq_pop_t_ = T;
            if (++rq_popped_ == QHALF) { rq_popped_ = 0; if (!stopped_) ev.post(T, EV_ADVICE); }
        }
    }

    // the ALU: rd's value and the new pc; a row that is not its expansion's last keeps its pc (17.8.7)
    const uint64_t pcv = pc();
    const unsigned pc_inc = !r.last ? 0 : (w & 1) == 0 ? 2 : 4;
    alu::Out out;
    alu::exec(op, v_rs1, v_rs2, v_rd, pcv, pc_inc, i0p0, r.advice_value, so, out);
    if (out.undefined != nullptr) violate(T, V_UNDEFINED, op, uint64_t(reinterpret_cast<uintptr_t>(out.undefined)));
    r.result = out.rd;
    r.assert_failed = !assertion_holds(op, v_rs1, v_rs2);
    r.mem_addr = alu::mem_addr(op, v_rs1, imm_sum);
    const uint64_t np = r.last ? out.new_pc : pcv;
    const bool zero = r.last ? np == 0 : pcv == 0, moves = r.last && np != pcv;
    if (hld) { r.next_kind = NK_HOLD; r.next_pc = pcv; }
    else if (zero) { r.next_kind = NK_STOP; r.next_pc = 0; }
    else if (moves && nfv) { r.next_kind = NK_TRANSFER; r.next_pc = np; }
    else { r.next_kind = NK_SEQUENTIAL; r.next_pc = pcv + pc_inc; }
    if (!hld && r.end && !nfv) violate(T, V_END_NO_NFV);
    if (!hld && !zero && moves && !nfv) violate(T, V_XFER_NO_NFV, op);   // UB (17.8.7): it runs on sequentially
    // its one offset in the guest's file: a memory row's address's, or a new pc's
    r.offset = r.memory ? r.mem_addr - uint64_t(lowest())
             : r.next_kind == NK_STOP ? uint64_t(0) - uint64_t(lowest()) : np - uint64_t(lowest());

    // the ALU's done: cycle 3, cycle 5 for an M row, and for a VirtualPext whose mask is not one run of ones the cycle
    // after its extraction ends, a bit of the mask a cycle (replayer_alu.veryl)
    int done_after = r.mul ? 4 : 2;
    if (op == alu::PEXT || op == alu::PEXT_S) {
        const uint64_t mask = v_rs2, run_starts = mask & ~(mask << 1);
        if ((run_starts & (run_starts - 1)) != 0) done_after = 4 + (63 - __builtin_clzll(mask));
    }
    r.alu_done_at = T + done_after;
    r.stage = 2;
    r.in_flight = true; r.after_done = false; r.data_answered = false; r.lookup_taken = false;
    rows_left_--;
    if (on_row) on_row(on_row_ctx, T, true);
}

// A row's completion (tracer_rows.veryl): rd written, pc, instr_idx and the row offset moved, its code value and its
// log word to the output (17.9.4's table).
void Tracer::row_complete(int64_t T, uint64_t data_load, uint32_t target_base) {
    Rows& r = rows_;
    if (r.writes) rec[4 + r.rd] = r.memory ? data_load : r.result;
    uint32_t idx = uint32_t(instr_idx());
    switch (r.next_kind) {
        case NK_HOLD: break;
        case NK_STOP: idx = 0; break;
        case NK_TRANSFER: idx = target_base; break;
        default: idx += 1; break;
    }
    const uint64_t offset = r.last ? 0 : uint64_t(r.index + 1) & 0xFFFF;
    rec[0] = r.next_pc;
    rec[1] = (rec[1] & ~uint64_t(0xFFFFFFFFFFFFull)) | uint64_t(idx) | offset << 32;
    if (r.first) push_code(r.code);
    const bool looks_up = r.next_kind == NK_TRANSFER || (r.next_kind == NK_STOP && !r.end);
    if (r.memory) push_log(r.store ? TAG_WRITE : TAG_LOAD, data_load);
    else if (r.advice) push_log(TAG_OTHER, r.advice_value);
    else if (r.end) push_log(TAG_OTHER, 0);
    else if (looks_up) push_log(TAG_OTHER, uint64_t(target_base));
    rows++;
    if (r.mul) rows_m++;
    if (r.padding) rows_pad++;
    if (r.last) instrs++;
    if (r.inline_row && !template_stored_) violate(T, V_INLINE_NO_TEMPLATE);
    if (on_row) on_row(on_row_ctx, T, false);
}

// One clock cycle: the levels of cycle T from the registers (the state at the end of T - 1), in the order the RTL's
// combinational paths run, then every register's update. The sections follow the RTL's modules; a name is the RTL's
// signal of that name.
void Tracer::cycle(int64_t T) {
    using namespace rowcore;
    Rows& r = rows_;
    Templates& t = tpl_;
    Instructions& in = ins_;
    Fetch& xf = xf_;
    Data& xd = xd_;
    Fill& f = fill_;
    Jump& xj = xj_;

    // ---------------------------------------------------------------- the run's levels (tracer_run.veryl)
    const bool busy = running_;
    const bool run = busy && !rows_ended_ && !stopped_;          // tracing && !stopped
    const bool started = started_;
    const bool rows_end = !stopped_ && busy && !rows_ended_ && rows_left_ == 0 && (row_done_reg_ || started);
    const bool invalidate = state_write_lands_at(T);
    if (busy) { stat[ST_CYCLES]++; if (!rows_ended_ && !stopped_) run_cycles++; }

    // ---------------------------------------------------------------- the fill port's responses (the memory system)
    const bool polled = fill_port && fill_port->read_response;     // v1.37: a clocked memory system (FillPort)
    const bool read_response_valid =
        f.read_outstanding && (polled ? fill_port->read_response(fill_port->ctx, T, f.response) : T == f.read_response_at);
    const bool write_response_valid =
        f.write_outstanding && (polled ? fill_port->write_response(fill_port->ctx, T, f.write_status) : T == f.write_response_at);
    const int fill_status = f.response.status;
    const bool data_read_done = read_response_valid && f.read_owner == OWNER_DATA;
    const bool map_read_done = read_response_valid && f.read_owner == OWNER_MAP;
    const bool fetch_read_done = read_response_valid && f.read_owner == OWNER_FETCH;
    if (read_response_valid) {   // tracer_fill's error_page_missing
        if (fill_status == FileStore::ST_BEYOND)
            error(T, 1 << ERR_PAGE, f.read_owner == OWNER_FETCH ? E_IFETCH_BEYOND : f.read_owner == OWNER_DATA ? E_DATA_BEYOND : E_MAP_BEYOND,
                  uint64_t(f.read_line));
        else if (fill_status == FileStore::ST_HOLE && f.read_owner == OWNER_MAP)
            error(T, 1 << ERR_PAGE, E_MAP_HOLE, uint64_t(f.read_line));
    }
    if (write_response_valid && f.write_status == MemSystem::WRITE_ERROR)
        error(T, 1 << ERR_WRITE_BACK, E_WRITE_BACK, f.write_line);

    // ---------------------------------------------------------------- the rows: the outcomes' needs (tracer_rows.veryl)
    const bool alu_done = r.in_flight && T == r.alu_done_at;
    const bool outcome = r.in_flight && (alu_done || r.after_done);
    const bool row_looks_up = r.next_kind == NK_TRANSFER || (r.next_kind == NK_STOP && !r.end);
    const bool data_valid = outcome && r.memory && !r.data_answered;
    const bool lookup_valid = outcome && row_looks_up && !r.lookup_taken;

    // ---------------------------------------------------------------- the data access (tracer_data.veryl)
    const bool d_row_start = xd.state == XD_IDLE && data_valid;
    const bool out_of_range = d_row_start && r.offset >= (uint64_t(8) << log2k());
    const bool misaligned = d_row_start && (r.mem_addr & 7) != 0;
    const bool d_look_up = d_row_start && !out_of_range && !misaligned;
    const bool d_hit = d_look_up && dcache_.peek((r.offset >> 7) & 0xFFFFFFF) >= 0;
    const bool in_miss = xd.state == XD_MISS;
    const bool take_back_now = in_miss && xd.taking_back && !xd.victim_dirty;
    const bool read_for_swap = in_miss && xd.taking_back && xd.victim_dirty && !f.write_requested;
    const bool read_for_evict = in_miss && !xd.taking_back && xd.victim_dirty && !f.write_held;
    const bool read_for_clean = in_miss && !xd.taking_back && !xd.victim_dirty;
    const bool take_back = take_back_now || read_for_swap;
    const bool data_read_want = xd.state == XD_EVICT || xd.state == XD_REQUEST;
    const bool in_access = xd.state == XD_ACCESS;
    const bool store_refused = in_access && r.store && !dline_[xd.slot].writable;
    const bool store_write = in_access && r.store && dline_[xd.slot].writable;
    const bool data_ready = in_access || xd.state == XD_FAILED;
    const bool data_error = store_refused || xd.state == XD_FAILED;
    const uint64_t data_load = in_access ? dline_[xd.slot].d[xd.doubleword] : 0;
    const bool data_busy = data_valid || xd.state != XD_IDLE;
    if (xd.state == XD_FAILED && (xd.range_failed || xd.alignment_failed))
        error(T, (xd.range_failed ? 1 << ERR_RANGE : 0) | (xd.alignment_failed ? 1 << ERR_MISALIGNED : 0), E_MEM_RANGE_ALIGN, r.mem_addr);
    if (store_refused) error(T, 1 << ERR_STORE, E_STORE_NOT_WRITABLE, r.mem_addr);

    // ---------------------------------------------------------------- the look-up (tracer_jump_lookup.veryl)
    const bool lookup_ready = xj.state == XJ_IDLE;
    const bool accept = lookup_valid && lookup_ready;
    const bool in_lookup = xj.state == XJ_LOOKUP;
    const bool lru_answer = in_lookup && (xj.lru_hit || xj.too_far);
    const bool probe_start = in_lookup && !xj.lru_hit && !xj.too_far;
    const bool map_read_valid = xj.state == XJ_READ;
    const bool in_decide = xj.state == XJ_DECIDE;
    const bool last_line = xj.lines_read == PROBE_LINES;
    const bool probe_missing = in_decide && xj.compare_missing;
    const bool probe_found = in_decide && !xj.compare_missing && xj.compare_hit;
    const bool probe_miss = in_decide && !xj.compare_missing && !xj.compare_hit && (xj.compare_free || last_line);
    const bool probe_next = in_decide && !xj.compare_missing && !xj.compare_hit && !xj.compare_free && !last_line;
    const bool target_valid = lru_answer || xj.probe_answer;
    const bool target_error = (lru_answer && xj.too_far) || (xj.probe_answer && !xj.probe_found_answer);
    const uint32_t target_base = in_lookup ? xj.lru_base : xj.compare_base;
    if (lru_answer && xj.too_far) error(T, 1 << ERR_MAP, E_MAP_OFFSET, r.next_pc);
    if (xj.probe_answer && !xj.probe_found_answer && !xj.probe_missing_answer)
        error(T, 1 << ERR_MAP, xj.compare_free ? E_MAP_FREE : E_MAP_FULL, r.next_pc, uint64_t(xj.line));

    // ---------------------------------------------------------------- the rows: completion and the next row's start
    const bool data_fine = !r.memory || (data_ready && !data_error);
    const bool target_fine = !row_looks_up || (target_valid && !target_error);
    const bool completing = r.in_flight && r.after_done && run && data_fine && target_fine && !r.assert_failed;
    if (r.in_flight && r.after_done && run && r.assert_failed) error(T, 1 << ERR_ASSERT, E_ASSERT, r.op);
    const bool word_valid = t.head_valid;
    const bool next_row = run && rows_left_ != 0 && word_valid;
    if (word_valid) head_decode();
    const bool advice_valid = rq_occ_ > 0;
    const bool can_start = next_row && !hd_.invalid && (!hd_.advice || advice_valid);
    // the reads, in a cycle with no row in flight or in the cycle the row completes; port b free of the write-back
    const bool make_reads = !r.reads_made && (!r.in_flight || completing) && can_start && (completing || !r.wb_pending);
    const bool starting = r.reads_made && !r.in_flight && next_row && (!r.reads_advice || advice_valid);
    if (next_row && !r.in_flight && hd_.invalid) error(T, 1 << ERR_INVALID, hd_.invalid_code, t.head_code, t.head_lo);
    if (next_row && !r.in_flight && !hd_.invalid && hd_.advice && advice_left() == 0) error(T, 1 << ERR_ADVICE_UNDER, E_ADVICE_ZERO);
    const bool write_back_write = r.wb_pending && !make_reads && !(q0_holding_ && !run);   // not in an image read's cycle

    // ---------------------------------------------------------------- the templates (replayer_fetch.veryl)
    const bool word_ready = starting;
    const bool room = (!t.head_valid || word_ready) && !t.rom_pending_word;
    const bool word_in_budget = t.head_valid ? rows_left_ > 1 : rows_left_ != 0;
    const int rows_ahead = (t.head_valid ? 1 : 0) + (t.rom_pending_word ? 1 : 0);
    const bool instruction_in_budget = rows_left_ > rows_ahead;
    const bool needs_instruction = run && !t.active && !t.entry_pending && !(t.head_valid && t.head_invalid);
    const bool code_valid = in.held;
    const bool take_code = needs_instruction && code_valid && instruction_in_budget;
    const bool padding_word = (in.held_code & 3) == 0;
    const bool code_invalid = in.held_invalid;
    // the entry, the cycle after its instruction is taken
    const unsigned entry = t.entry_pending ? decode_table[decode_index(t.instruction)] : 0;
    const unsigned entry_class = entry & 3, entry_start = (entry >> 2) & 0x1FF;
    const int entry_kind = entry_class == 0 ? TK_ROM : entry_class == 3 ? TK_BAD : TK_CORE;
    const bool posted = t.entry_pending && entry_class != 0 && entry_class != 3;
    const bool new_expansion = posted && run;
    const bool current_active = t.active || t.entry_pending;
    const int current_kind = t.entry_pending ? entry_kind : t.kind;
    const int current_start = t.entry_pending ? int(entry_start) : t.start;
    const int rom_word_address = t.start + t.index;
    const bool past_rom = rom_word_address > 511;
    const bool may_send = run && current_active && room && word_in_budget;
    const bool send_padding = may_send && current_kind == TK_PADDING;
    const bool send_nop = may_send && current_kind == TK_NOP;
    const bool send_bad = may_send && (current_kind == TK_BAD || (current_kind == TK_ROM && t.policy_known && past_rom));
    const bool send_rom = may_send && current_kind == TK_ROM && t.policy_known && !past_rom;
    const bool send_rom_first = may_send && current_kind == TK_ROM && !t.policy_known && t.index == 0;
    const bool read_policy = run && current_active && current_kind == TK_ROM && !t.policy_known && t.index != 0 && !t.rom_pending_policy;
    const bool expansion_valid = eq_occ_ > 0;
    const uint64_t expansion_lo = eq_lo_[eq_head_], expansion_hi = eq_hi_[eq_head_];
    const bool pop_expansion = may_send && current_kind == TK_CORE && expansion_valid;
    const bool rd_field_zero = ((t.instruction >> 7) & 31) == 0;
    auto nop_policy = [](unsigned rd0v, bool rd_zero) { return rd_zero && (rd0v == 1 || rd0v == 3); };
    const unsigned expansion_rd0 = rd0(expansion_lo);
    const bool pop_first_nop = pop_expansion && !t.policy_known && nop_policy(expansion_rd0, rd_field_zero);
    // the data a ROM read of the last cycle brings now
    const uint64_t rom_data_lo = rom_lo[t.rom_address], rom_data_hi = rom_hi[t.rom_address];
    const unsigned rom_rd0 = rd0(rom_data_lo);
    const bool rom_nop = nop_policy(rom_rd0, rd_field_zero);
    const bool arriving_policy = t.rom_pending_policy;
    const bool arriving_nop = arriving_policy && rom_nop;
    const bool arriving_word = t.rom_pending_word && !arriving_nop;
    const bool arriving_end = t.rom_pending_word && !arriving_nop && seq_remaining(rom_data_lo) == 0 && t.kind == TK_ROM;
    const bool finishing = send_padding || send_nop || send_bad || pop_first_nop ||
                           (pop_expansion && seq_remaining(expansion_lo) == 0) || arriving_end;
    const bool rom_moves = next_from_value(rom_data_lo) || hold(rom_data_lo);
    const bool expansion_moves = next_from_value(expansion_lo) || hold(expansion_lo);
    const bool instruction_end = finishing;
    const bool rom_end_sequential = arriving_end && !rom_moves;
    const bool instruction_end_moves = send_padding || send_bad || (pop_expansion && !pop_first_nop && expansion_moves) ||
                                       (t.rom_pending_word && rom_moves);
    const bool expansion_wait = run && current_active && current_kind == TK_CORE && !expansion_valid;

    // ---------------------------------------------------------------- the instructions (tracer_instruction.veryl)
    const bool next_is_zero = in.next_pc == 0;
    const bool free = run && !in.requesting && !in.held;
    const bool successor_needed = rows_left_ > 2 || (rows_left_ == 2 && !starting);
    const bool end_early = instruction_end && free && successor_needed && (in.current_padding || !instruction_end_moves);
    const bool end_now = rom_end_sequential && free && successor_needed;
    const bool instruction_resolved = r.in_flight && alu_done && r.last;
    const bool instruction_done = completing && r.last;
    const bool resolved = instruction_resolved && !in.early[0];
    const bool redirect = instruction_done && !in.early[0];
    const bool known_now = (in.known || redirect) && free && rows_left_ != 0;
    const bool issue = end_now || known_now;
    const bool issue_fetch = issue && !next_is_zero;
    const bool issue_padding = issue && next_is_zero;
    const bool fetch_valid = in.requesting || issue_fetch;
    const uint64_t fetch_offset = in.next_pc - uint64_t(lowest());
    const bool taking = in.held && take_code;

    // ---------------------------------------------------------------- the fetch (tracer_fetch.veryl)
    const bool look_up_first = xf.state == XF_IDLE && fetch_valid;
    const bool look_up_second = xf.state == XF_LOOK_UP;
    const bool look_up_beyond = (look_up_first && (fetch_offset >> 42) != 0) || (look_up_second && xf.next_line_beyond);
    const uint64_t look_up_line = look_up_second ? xf.line : (fetch_offset >> 7) & 0x7FFFFFFFFull;
    const bool x_hit = (look_up_first || look_up_second) && !look_up_beyond && icache_.peek(look_up_line) >= 0;
    const bool x_miss = (look_up_first || look_up_second) && !x_hit;
    const bool fetch_read_want = xf.state == XF_REQUEST;
    const bool x_filled = xf.state == XF_WAIT_FILL && fetch_read_done;
    const bool x_install = x_filled && fill_status != FileStore::ST_BEYOND;
    uint32_t parcel = 0, next_parcel = 0;
    if (xf.state == XF_READ) {
        const uint64_t* d = iline_[xf.slot].d;
        const int b = xf.second ? 0 : xf.parcel_index * 2;   // the second half is its line's first 16 bits
        parcel = uint32_t((d[b >> 3] >> (8 * (b & 7))) & 0xFFFF);
        if (!xf.second && xf.parcel_index < 63) next_parcel = uint32_t((d[(b + 2) >> 3] >> (8 * ((b + 2) & 7))) & 0xFFFF);
    }
    const bool wide = (parcel & 3) == 3;
    const bool straddles = xf.state == XF_READ && !xf.second && wide && xf.parcel_index == 63;
    const bool done_read = xf.state == XF_READ && !straddles;
    const bool fetch_ready = done_read || xf.state == XF_FAILED;
    const bool fetch_error = xf.state == XF_FAILED;
    uint32_t fetch_instruction = 0;
    if (done_read) fetch_instruction = xf.second ? (parcel << 16 | xf.first_parcel) : wide ? (next_parcel << 16 | parcel) : parcel;
    if (xf.state == XF_FAILED && xf.failed_beyond) error(T, 1 << ERR_PAGE, E_IFETCH_BEYOND, xf.line << 7);
    const bool answered = in.requesting && fetch_ready;
    const bool fetched = answered && !fetch_error;

    // ---------------------------------------------------------------- the fill port's grants (tracer_fill.veryl)
    const bool dropped_presented = f.write_request_valid && !f.write_line_kept;
    const bool read_free = !f.read_request_valid && !f.read_outstanding && !dropped_presented;
    const bool map_read_want = map_read_valid && !f.map_pending;
    const bool data_read_granted = read_free && data_read_want;
    const bool map_read_granted = read_free && !data_read_want && map_read_want;
    const bool fetch_read_granted = read_free && !data_read_want && !map_read_want && fetch_read_want && !data_busy && !f.map_pending;
    const bool read_granted = data_read_granted || map_read_granted || fetch_read_granted;
    const bool request_write = f.write_held && !f.write_requested && read_free && !read_granted && !take_back && !invalidate;

    // ---------------------------------------------------------------- the stall causes (tracer_rows.veryl; 17.9.8)
    if (busy) {
        const bool past_schedule = r.in_flight && r.stage > 4;
        const bool between_rows = run && !r.in_flight && !starting && rows_left_ != 0;
        if (run && past_schedule && r.memory) stat[ST_STALL_DATA]++;
        else if (run && past_schedule && row_looks_up) stat[ST_STALL_MAP]++;
        else if (between_rows && !r.waits_on_core) stat[ST_STALL_FETCH]++;
        else if (between_rows) stalls[hd_.advice && word_valid ? S_ADVICE : S_EXP]++;
    }

    // ================================================================ the registers' updates

    // a row.s start, before the templates move the row word
    if (starting) { r.inline_row = t.head_inline; row_start(T); }

    // ---------------------------------------------------------------- the fill port and the memory system
    if (read_granted) {
        f.read_request_valid = true;
        f.read_owner = data_read_granted ? OWNER_DATA : map_read_granted ? OWNER_MAP : OWNER_FETCH;
        const bool map = f.read_owner == OWNER_MAP;
        f.read_root = map ? map_root() : mem_root();
        f.read_depth = map ? map_depth() : mem_depth();
        f.read_line = data_read_granted ? int64_t(xd.line) : map ? xj.line : int64_t(xf.line);
    } else if (f.read_request_valid && (!fill_port || fill_port->read_ready(fill_port->ctx, T))) {
        // its handshake: `mem` is always ready, a harness's model (fill_port) as it says
        f.read_request_valid = false;
        f.read_outstanding = true;
        const bool map = f.read_owner == OWNER_MAP;
        MemSystem* m = map && map_mem ? map_mem : mem;
        f.read_response_at = fill_port ? fill_port->read(fill_port->ctx, T, f.read_root, f.read_depth, f.read_line, f.response)
                                       : m->port_read(T, f.read_root, f.read_depth, f.read_line, fill_latency[map ? 1 : 0], f.response);
        if (f.read_response_at <= T) FATAL("Tracer: a fill read answered at %lld, not after its handshake at %lld", (long long)f.read_response_at, (long long)T);
        if (map) probe_reads++;
    }
    if (read_response_valid) f.read_outstanding = false;
    if (write_response_valid) { f.write_held = false; f.write_requested = false; f.write_outstanding = false; }
    else if (request_write) f.write_requested = true;
    else if (take_back && !f.write_requested) f.write_held = false;
    if (request_write) {
        f.write_request_valid = true;
        f.write_root = mem_root(); f.write_depth = mem_depth();
    } else if (f.write_request_valid && (!fill_port || fill_port->write_ready(fill_port->ctx, T))) {
        // its handshake: the line is written as it is taken
        f.write_request_valid = false;
        f.write_outstanding = true;
        f.write_response_at = fill_port ? fill_port->write(fill_port->ctx, T, f.write_root, f.write_depth, int64_t(f.write_line), f.write_data, f.write_status)
                                        : mem->port_write(T, f.write_root, f.write_depth, int64_t(f.write_line), f.write_data,
                                                          fill_latency[0] > 0, f.write_status);
        writebacks++;
    }
    // the map's reads (tracer_memory.veryl): one at a time, the response registered
    if (map_read_granted) f.map_pending = true;
    else if (f.map_line_valid) f.map_pending = false;
    const bool map_line_valid = f.map_line_valid;
    f.map_line_valid = map_read_done;
    if (map_read_done) { std::memcpy(f.map_line_data, f.response.data, sizeof f.map_line_data); f.map_line_status = f.response.status; }

    // ---------------------------------------------------------------- the data access
    switch (xd.state) {
        case XD_IDLE:
            if (d_row_start) {
                xd.line = (r.offset >> 7) & 0xFFFFFFF;
                xd.doubleword = int((r.offset >> 3) & 15);
                xd.taking_back = f.write_held && f.write_line_kept && f.write_line == xd.line;
                xd.range_failed = out_of_range; xd.alignment_failed = misaligned;
                if (d_look_up) d_access++;
                if (!d_look_up) xd.state = XD_FAILED;
                else if (d_hit) { xd.slot = dcache_.find(xd.line); xd.state = XD_ACCESS; }
                else xd.state = XD_VICTIM;
            }
            break;
        case XD_VICTIM: {   // the miss, counted; its victim, the oldest line once every line is valid
            d_miss++; if (busy) stat[ST_MISS_D]++;
            const bool full = dcache_.size() >= CACHE_LINES;
            const int v = full ? dcache_.eldest() : -1;
            xd.victim_dirty = v >= 0 && dline_[v].dirty;
            xd.victim_line = v >= 0 ? dcache_.key(v) : 0;
            xd.state = XD_MISS;
            break;
        }
        case XD_MISS:
            if (take_back_now) {   // the buffer's line into a clean victim
                const int s = dcache_.insert(xd.line);
                std::memcpy(dline_[s].d, f.write_data, sizeof f.write_data);
                dline_[s].dirty = true; dline_[s].writable = true;
                if (dcache_.size() > CACHE_LINES) dcache_.remove(dcache_.eldest());
                take_backs++;
                xd.state = XD_READ_LINE;
            } else if (read_for_swap) xd.state = XD_SWAP;
            else if (read_for_evict) xd.state = XD_EVICT;
            else if (read_for_clean) xd.state = XD_REQUEST;
            break;
        case XD_EVICT: {    // the dirty victim into the buffer; the line's read wanted
            const int v = dcache_.eldest();
            f.write_held = true; f.write_line_kept = true; f.write_line = xd.victim_line;
            std::memcpy(f.write_data, dline_[v].d, sizeof f.write_data);
            dline_[v].dirty = false;
            xd.state = data_read_granted ? XD_WAIT_FILL : XD_REQUEST;
            break;
        }
        case XD_SWAP: {     // the dirty victim into the buffer, the buffer's line into its place
            const int v = dcache_.eldest();
            uint64_t back[16];
            std::memcpy(back, f.write_data, sizeof back);
            f.write_held = true; f.write_line_kept = true; f.write_line = xd.victim_line;
            std::memcpy(f.write_data, dline_[v].d, sizeof f.write_data);
            dcache_.remove(v);
            const int s = dcache_.insert(xd.line);
            std::memcpy(dline_[s].d, back, sizeof back);
            dline_[s].dirty = true; dline_[s].writable = true;
            take_backs++;
            xd.state = XD_READ_LINE;
            break;
        }
        case XD_REQUEST: if (data_read_granted) xd.state = XD_WAIT_FILL; break;
        case XD_WAIT_FILL:
            if (data_read_done) {
                if (fill_status == FileStore::ST_BEYOND) {
                    xd.range_failed = xd.alignment_failed = false;   // the stop is the fill port's page_missing alone
                    xd.state = XD_FAILED;
                } else {
                    const int s = dcache_.insert(xd.line);
                    std::memcpy(dline_[s].d, f.response.data, sizeof f.response.data);
                    dline_[s].dirty = false; dline_[s].writable = fill_status == FileStore::ST_OK;
                    if (dcache_.size() > CACHE_LINES) dcache_.remove(dcache_.eldest());
                    xd.state = XD_READ_LINE;
                }
            }
            break;
        case XD_READ_LINE: xd.slot = dcache_.peek(xd.line); xd.state = XD_ACCESS; break;
        case XD_ACCESS:
            if (store_write) { dline_[xd.slot].d[xd.doubleword] = r.rs2; dline_[xd.slot].dirty = true; }
            xd.state = XD_IDLE;
            break;
        default: xd.state = XD_IDLE; break;
    }

    // ---------------------------------------------------------------- the look-up
    switch (xj.state) {
        case XJ_IDLE:
            if (accept) {
                xj.key = uint32_t(r.offset); xj.too_far = (r.offset >> 32) != 0;
                xj.lru_hit = false; xj.lru_base = 0;
                if (!xj.too_far) {
                    const int s = jump_lru_.find(xj.key);   // a hit moves its entry to the front
                    if (s >= 0) { xj.lru_hit = true; xj.lru_base = uint32_t(jump_base_[s]); lru_hits++; }
                }
                xj.state = XJ_LOOKUP;
            }
            break;
        case XJ_LOOKUP:
            if (probe_start) { lru_misses++; if (busy) stat[ST_PROBES]++; }
            xj.state = probe_start ? XJ_HOME : XJ_IDLE;
            break;
        case XJ_HOME: {     // the home line, the product's top k bits
            const int k = int(hash_lines_);
            xj.line = k == 0 ? 0 : int64_t((uint64_t(xj.key) * uint64_t(hash_mul_) & 0xFFFFFFFFull) >> (32 - k));
            xj.lines_read = 0;
            xj.state = XJ_READ;
            break;
        }
        case XJ_READ: if (map_read_granted) xj.state = XJ_WAIT_LINE; break;
        case XJ_WAIT_LINE:
            if (map_line_valid) {   // its 16 entries against the key, at once: the lowest match's base
                xj.compare_hit = xj.compare_free = false;
                xj.compare_missing = f.map_line_status == FileStore::ST_HOLE || f.map_line_status == FileStore::ST_BEYOND;
                for (int j = 15; j >= 0; j--) {
                    const uint64_t e = f.map_line_data[j];
                    if (uint32_t(e) == xj.key) { xj.compare_hit = true; xj.compare_base = uint32_t(e >> 32); }
                    if (uint32_t(e) == 0xFFFFFFFFu) xj.compare_free = true;
                }
                xj.lines_read++;
                xj.state = XJ_DECIDE;
            }
            break;
        case XJ_DECIDE:
            if (probe_found) {
                const int s = jump_lru_.insert(xj.key);
                jump_base_[s] = int32_t(xj.compare_base);
                if (jump_lru_.size() > LRU_ENTRIES) jump_lru_.remove(jump_lru_.eldest());
            }
            if (probe_next) {
                const int k = int(hash_lines_);
                xj.line = int64_t((uint64_t(xj.line) + (hash_step_ & 0xFFFFFF)) & ((uint64_t(1) << k) - 1));
            }
            xj.state = probe_next ? XJ_READ : XJ_IDLE;
            break;
        default: xj.state = XJ_IDLE; break;
    }
    xj.probe_answer = probe_found || probe_miss || probe_missing;
    xj.probe_found_answer = probe_found;
    xj.probe_missing_answer = probe_missing;

    // ---------------------------------------------------------------- the fetch
    if (look_up_first || look_up_second) {
        i_access++;
        if (look_up_first) { xf.second = false; xf.line = look_up_line; xf.parcel_index = int((fetch_offset >> 1) & 63); }
        xf.failed_beyond = look_up_beyond;
        if (x_hit) xf.slot = icache_.find(look_up_line);
        if (x_miss) {
            i_miss++; if (busy) stat[ST_MISS_I]++;
            if (!look_up_beyond) {
                const bool first_seen = i_seen_.peek(look_up_line) < 0;
                if (first_seen) {
                    if (i_seen_.size() >= i_seen_.capacity()) FATAL("Tracer: more than %d instruction lines seen in a segment", i_seen_.capacity());
                    i_seen_.insert(look_up_line);
                }
                i_miss_by[(first_seen ? 0 : 2) + (look_up_second ? 1 : 0)]++;
            }
        }
        xf.state = look_up_beyond ? XF_FAILED : x_hit ? XF_READ : XF_REQUEST;
    } else {
        switch (xf.state) {
            case XF_REQUEST: if (fetch_read_granted) xf.state = XF_WAIT_FILL; break;
            case XF_WAIT_FILL:
                if (fetch_read_done) {
                    if (x_install) {
                        xf.slot = icache_.insert(xf.line);
                        std::memcpy(iline_[xf.slot].d, f.response.data, sizeof f.response.data);
                        iline_[xf.slot].dirty = false; iline_[xf.slot].writable = false;
                        if (icache_.size() > icache_lines_) icache_.remove(icache_.eldest());
                        xf.state = XF_READ_LINE;
                    } else xf.state = XF_FAILED;
                }
                break;
            case XF_READ_LINE: xf.state = XF_READ; break;
            case XF_READ:
                if (straddles) {
                    this->straddles++;
                    xf.second = true; xf.next_line_beyond = xf.line == 0x7FFFFFFFFull; xf.line += 1; xf.first_parcel = parcel;
                    xf.state = XF_LOOK_UP;
                } else xf.state = XF_IDLE;
                break;
            default: xf.state = XF_IDLE; break;
        }
    }

    // ---------------------------------------------------------------- the instructions
    if (instruction_end && !instruction_done) {
        in.early[in.ended & 1] = end_early;
        in.ended++;
    } else if (instruction_done && !instruction_end) {
        in.early[0] = in.early[1];
        in.early[1] = end_early;
        in.ended--;
    } else if (instruction_done && instruction_end) {
        in.early[0] = in.ended == 1 ? end_early : in.early[1];
        in.early[1] = end_early;
    }
    if (issue) in.known = false;
    else if (redirect || end_early) in.known = true;
    if (redirect) redirects++;
    if (issue_fetch) in.requesting = !fetch_ready;
    else if (answered) in.requesting = false;
    if (resolved) in.next_pc = r.next_pc;
    else if (taking && !in.held_padding) in.next_pc += in.held_wide ? 4 : 2;
    if (taking) in.current_padding = in.held_padding;
    if (fetched || issue_padding) in.held = true;
    else if (taking) in.held = false;
    if (issue_padding) {
        in.held_code = 0; in.held_invalid = false; in.held_padding = true;
    } else if (fetched) {
        const bool w32 = (fetch_instruction & 3) == 3;
        const uint32_t expanded = w32 ? fetch_instruction : rvc::code_word(fetch_instruction & 0xFFFF);
        in.held_invalid = !w32 && expanded == 0;
        in.held_code = in.held_invalid ? (rvc::uncompress(fetch_instruction & 0xFFFF) & ~1u) : expanded;
        in.held_padding = false; in.held_wide = w32;
    }

    // ---------------------------------------------------------------- the templates
    // the row word: a ROM read's word as it arrives, else the expansion queue's head or an invalid expansion's mark
    if (t.rom_pending_word) {
        t.head_lo = rom_data_lo; t.head_hi = rom_data_hi; t.head_code = t.instruction;
        const bool one_row = t.rom_pending_padding || t.rom_pending_nop;
        t.head_temporary_offset = one_row ? false : arriving_policy ? rd_field_zero && rom_rd0 == 2 : t.temporary_offset;
        t.head_index = one_row ? 0 : t.rom_pending_index;
        t.head_padding = t.rom_pending_padding; t.head_nop = t.rom_pending_nop; t.head_nop_virtual = t.nop_virtual;
        t.head_invalid = false; t.head_inline = false;
    } else if (room) {
        t.head_lo = expansion_lo; t.head_hi = expansion_hi; t.head_code = t.instruction;
        t.head_temporary_offset = t.policy_known ? t.temporary_offset : rd_field_zero && expansion_rd0 == 2;
        t.head_index = t.index;
        t.head_padding = false; t.head_nop = false; t.head_nop_virtual = false;
        t.head_invalid = send_bad;
        t.head_invalid_code = current_kind == TK_BAD ? (t.no_form ? E_NO_32BIT_FORM : E_CLASS3) : E_ROM_PAST_511;
        t.head_inline = t.cls == 1;
    }
    if (arriving_word || send_bad || (pop_expansion && !pop_first_nop)) t.head_valid = true;
    else if (word_ready) t.head_valid = false;
    if (pop_expansion) {    // the word leaves the expansion queue; its credit's return (replayer_input_queue.veryl)
        eq_head_ = (eq_head_ + 1) % QCAP; eq_occ_--; eq_pop_t_ = T;
        if (++eq_popped_ == QHALF) { eq_popped_ = 0; if (!stopped_) ev.post(T, EV_EXP); }
    }
    if (new_expansion) {    // the post (17.9.4): the event, the credit's fill, the words 34 and 35, the latch's clear
        presented_ = t.instruction; expansion_offset_ = t.index;
        ev.set(T, EV_EXP, 2); eq_popped_ = 0;
        ev.post(T, EV_NEWEXP);
        latch_ = -1; template_stored_ = false;
        new_expansions++;
    }
    if (posted) t.core_fed = true;
    if (taking) t.core_fed = false;
    t.rom_pending_padding = send_padding;
    t.rom_pending_nop = send_nop || pop_first_nop;
    t.rom_pending_index = t.index;
    if (send_padding) t.rom_address = 0;
    else if (send_nop || pop_first_nop) t.rom_address = 1;
    else if (send_rom) t.rom_address = rom_word_address;
    else if (send_rom_first || read_policy) t.rom_address = current_start;
    t.rom_pending_word = send_padding || send_nop || send_rom || send_rom_first || pop_first_nop;
    t.rom_pending_policy = send_rom_first || read_policy;
    const bool was_entry_pending = t.entry_pending;
    t.entry_pending = take_code && !padding_word && !code_invalid;
    if (take_code) {
        t.instruction = in.held_code;
        t.index = t.first ? state_offset() : 0;
        t.first = false;
        t.active = padding_word || code_invalid;
        t.kind = code_invalid ? TK_BAD : TK_PADDING;
        t.no_form = code_invalid; t.cls = 0;
        t.policy_known = false; t.temporary_offset = false; t.nop_virtual = false;
    } else {
        if (finishing) t.active = false;
        else if (was_entry_pending) t.active = true;
        if (was_entry_pending) { t.start = int(entry_start); t.kind = entry_kind; t.cls = int(entry_class); }
        if (arriving_policy) {
            t.policy_known = true;
            if (rom_nop) { t.kind = TK_NOP; t.nop_virtual = rom_rd0 == 1; }
            else t.temporary_offset = rd_field_zero && rom_rd0 == 2;
        }
        if (pop_expansion && !t.policy_known) {
            t.policy_known = true;
            if (pop_first_nop) { t.kind = TK_NOP; t.nop_virtual = expansion_rd0 == 1; }
            else t.temporary_offset = rd_field_zero && expansion_rd0 == 2;
        }
        if (send_rom || send_rom_first || (pop_expansion && !pop_first_nop)) t.index++;
    }

    // ---------------------------------------------------------------- the rows
    r.reads_made = make_reads;
    if (make_reads) r.reads_advice = hd_.advice;
    r.waits_on_core = word_valid ? hd_.advice && !advice_valid : expansion_wait;
    if (completing && r.writes) r.wb_pending = true;
    else if (write_back_write) r.wb_pending = false;
    if (starting) {
        // started above, before the templates moved the row word
    } else if (completing) {
        row_complete(T, data_load, target_base);
        r.in_flight = false; r.after_done = false; r.data_answered = false; r.lookup_taken = false;
        r.run_first = false;
    } else {
        if (r.in_flight && alu_done) r.after_done = true;
        if (data_valid && data_ready) r.data_answered = true;
        if (lookup_valid && lookup_ready) r.lookup_taken = true;
        if (r.in_flight && r.stage != 7) r.stage++;
    }

    // ---------------------------------------------------------------- the run
    if (row_done_reg_) rows_done_++;
    row_done_reg_ = completing;
    started_ = false;
    if (rows_end) { rows_ended_ = true; rows_end_at = T; }
    drain_cycle(T);
    end_in_ = rows_end;
    if (stopping_) { stopped_ = true; stopping_ = false; }
}

bool Tracer::assertion_holds(unsigned op, uint64_t a, uint64_t b) {
    switch (op) {
        case alu::ASSERT_EQ: return a == b;
        case alu::ASSERT_LTE: return a <= b;
        case alu::ASSERT_DIV0: return a != 0 || b == ~uint64_t(0);
        case alu::ASSERT_UREM: return b == 0 || a < b;
        case alu::ASSERT_MULU: return alu::mulhu(a, b) == 0;
        default: return true;
    }
}

void Tracer::push_code(uint32_t w) {
    if (code_n >= MAX_ROWS) FATAL("Tracer: the code FIFO is full (%d)", MAX_ROWS);
    code_fifo[code_n++] = w;
    set1_code_.encode(uint64_t(w));
}
void Tracer::push_log(uint8_t tag, uint64_t v) {
    if (log_n >= MAX_ROWS) FATAL("Tracer: the log FIFO is full (%d)", MAX_ROWS);
    log_fifo[log_n] = v; log_tag[log_n] = tag; log_n++;
    if (tag == TAG_WRITE) { set1_wlog_.encode(v); wlog_values_++; }
    else { set1_rlog_.encode(v); rlog_values_++; if (tag == TAG_LOAD) set1_wlog_.last_load(v); }
}

// 17.9.6's choice: LRU-coded when enabled and fewer 128-bit words than raw, raw on a tie.
streamword::Size Tracer::choose(int64_t values, int64_t raw_words, int64_t coded_bits, bool enabled) {
    const int64_t coded = (coded_bits + 127) / 128;
    const bool c = enabled && coded < raw_words;
    return streamword::make(c ? coded : raw_words, values, c);
}

// ================================================================ the output path (tracer_output.veryl)
void Tracer::OutQueue::push(int64_t t, uint64_t l, uint64_t h) {
    if (occ >= QCAP) FATAL("Tracer: a push into a full output queue (the packer's room check)");
    const int k = (head + occ) % QCAP;
    lo[k] = l; hi[k] = h; occ++; pushed++; push_t = t;
    if (occ > max_occ) max_occ = occ;
}
void Tracer::OutQueue::end(int64_t t) { ended = true; ended_at = t; }
// The DMA's read of the head, in cycle t (tracer_credits.veryl: a credit is taken by the 128th pop since the last
// taken, or by the pop, after the stream's end, of the queue's only entry).
void Tracer::OutQueue::take(int64_t t, uint64_t w[2]) {
    const bool last = ended && ended_at < t && count(t) == 1;
    w[0] = lo[head]; w[1] = hi[head]; head = (head + 1) % QCAP; occ--; read++; pop_t = t;
    if (popped_part == QHALF - 1 || last) { popped_part = 0; credits--; }
    else popped_part++;
}

// One cycle of the output: the choice, the cycle after the rows' end reaches its register; then the drain: each scan
// reads its FIFO an entry ahead and gives it to set 2, whose token enters its packer's register and is packed the
// cycle after; a packer whose entry finds its queue full holds it, and its scan waits; then the run's end.
void Tracer::drain_cycle(int64_t T) {
    const bool busy = running_;
    if (draining_) {
        bool blocked[4] = {}, token_ready[4] = {};
        // ---- the packers (tracer_packer.veryl) and the credits' adds (tracer_credits.veryl)
        const bool finish[4] = {false, code_next_ >= code_n && !code_data_valid_, rlog_given_ == rlog_values_,
                                wlog_given_ == wlog_values_};
        bool packs[4] = {};
        for (int q = 1; q <= 3; q++) {
            Packer& p = pk_[q];
            OutQueue& x = oq[q];
            const bool full = x.count(T) == QCAP;
            const bool completes = p.held_valid && p.bits.fill + p.held_length >= 128;
            packs[q] = p.held_valid && !(completes && full);
            token_ready[q] = !p.held_valid || packs[q];
            const bool last_part = finish[q] && !p.held_valid && !p.ended && p.bits.fill != 0;
            const bool has_entry = completes || last_part;
            const bool push = has_entry && !full;
            blocked[q] = has_entry && full;
            const bool ending = finish[q] && !p.held_valid && !p.ended && (p.bits.fill == 0 || !full);
            auto out = [&x, T](uint64_t a, uint64_t b) { x.push(T, a, b); };
            if (packs[q]) {
                p.bits.put(p.held_lo, p.held_length < 64 ? p.held_length : 64, out);
                if (p.held_length > 64) p.bits.put(p.held_hi, p.held_length - 64, out);
                p.held_valid = false;
            } else if (last_part && !full) {
                p.bits.finish(out);
            }
            if (blocked[q]) { x.stall_cycles++; scan_stalls++; }
            const bool add = (push && x.pushed_part == QHALF - 1) || (ending && ((x.pushed_part + (push ? 1 : 0)) % QHALF) != 0);
            x.pushed_part = (x.pushed_part + (push ? 1 : 0)) % QHALF;
            if (add) x.credits++;
            if (ending) { p.ended = true; x.end(T); }
        }
        // a token into a packer's register: the value as set 2 codes it, or raw
        auto give = [this](int q, LruCoder& set2, uint64_t v, int raw_bits) {
            Packer& p = pk_[q];
            if (comp_[q]) {
                Bits tok;
                auto none = [](uint64_t, uint64_t) {};
                set2.encode(v, tok, none);
                p.held_lo = tok.lo; p.held_hi = tok.hi; p.held_length = tok.fill;
            } else {
                p.held_lo = v; p.held_hi = 0; p.held_length = raw_bits;
            }
            p.held_valid = true;
        };
        // ---- the code scan
        {
            const bool advance = code_data_valid_ && token_ready[1];
            const bool take = !code_data_valid_ || advance, more = code_next_ < code_n;
            if (advance) give(1, set2_code_, uint64_t(code_fifo[code_held_]), 32);
            if (take) { code_data_valid_ = more; code_held_ = code_next_; if (more) code_next_++; }
        }
        // ---- the log scan: tags 0 and 1 to the read log, 2 to the write log; every load passes the write log's E[15]
        bool log_blocked;
        {
            const uint8_t tag = log_data_valid_ ? log_tag[log_held_] : 0;
            const bool wr = tag == TAG_WRITE;
            log_blocked = log_data_valid_ ? blocked[wr ? 3 : 2] : (blocked[2] || blocked[3]);
            const bool advance = log_data_valid_ && token_ready[wr ? 3 : 2];
            const bool take = !log_data_valid_ || advance, more = log_next_ < log_n;
            if (advance) {
                const uint64_t v = log_fifo[log_held_];
                if (wr) { give(3, set2_wlog_, v, 64); wlog_given_++; }
                else { give(2, set2_rlog_, v, 64); rlog_given_++; }
                if (tag == TAG_LOAD) set2_wlog_.last_load(v);
            }
            if (take) { log_data_valid_ = more; log_held_ = log_next_; if (more) log_next_++; }
        }
        if (busy && (blocked[1] || log_blocked)) stat[ST_STALL_OUTPUT]++;
        // ---- the run's end: the first cycle with both scans and every packer ended, output queues 1-3 empty, output
        // queue 0 read through word 67 and the DMA's last transfer ended
        const bool q0_held = q0_holding_ || q0_pop_t_ == T;
        bool drained = !q0_held && !dma_busy(T);
        for (int q = 1; q <= 3; q++) drained = drained && pk_[q].ended && oq[q].ended_at < T && oq[q].count(T) == 0;
        if (drained && run_active_ && busy && rows_ended_ && !stopped_) {
            streamword::Size sz[3];
            streamword::parse(rec[2], rec[3], sz);
            for (int q = 1; q <= 3; q++)
                if (pk_[q].bits.words != sz[q - 1].words)
                    FATAL("Tracer: set 2's words of stream %d differ from set 1's: %lld against %u", q, (long long)pk_[q].bits.words, sz[q - 1].words);
            done_ = true; running_ = false; run_active_ = false;
            drained_at = T; drain_cycles += T - rows_end_at;
            ev.post(T, EV_RUN_DONE);
        }
    }
    if (choosing_) {   // the choice (17.9.6): the stream word's fields, the length words; the drain begins
        const bool cc = run_config_ & RC_COMP_CODE, cr = run_config_ & RC_COMP_RLOG, cw = run_config_ & RC_COMP_WLOG;
        const streamword::Size sc = choose(code_n, (code_n + 3) / 4, set1_code_.bits, cc);
        const streamword::Size sr = choose(rlog_values_, (rlog_values_ + 1) / 2, set1_rlog_.bits, cr);
        const streamword::Size sw = choose(wlog_values_, (wlog_values_ + 1) / 2, set1_wlog_.bits, cw);
        uint64_t s[2];
        streamword::build(sc, sr, sw, advice_left(), s);
        rec[2] = s[0]; rec[3] = s[1];
        length_code_ = sc.words; length_rlog_ = sr.words; length_wlog_ = sw.words; lengths_valid_ = true;
        comp_[1] = sc.compressed; comp_[2] = sr.compressed; comp_[3] = sw.compressed;
        set2_code_.reset(); set2_rlog_.reset(); set2_wlog_.reset();
        draining_ = true; q0ptr_ = 0; q0_holding_ = true; q0_since_ = T + 1;
    }
    choosing_ = end_in_;
}

// ---- the DMA's side (17.4): four output queues; queue 0 is part 0 read in place
void Tracer::out_start(int64_t t, int q, int64_t words) {
    settle(t);
    if (xfer_left_ > 0) FATAL("Tracer: a transfer started at %lld with one in progress", (long long)t);
    if (q == 0) {
        if (q0ptr_ + words > REC_WORDS) ub(t, "a transfer from output queue 0 past word 67 (17.9.6)", uint64_t(words), uint64_t(q0ptr_));
    } else {
        // what the queue's credits cover (tracer_credits.veryl): 128 entries a credit, less those already read of the
        // credit being taken; once the stream has ended, every entry the queue holds
        const OutQueue& x = oq[q];
        const int64_t covered = x.ended && x.ended_at < t ? int64_t(x.occ) : int64_t(x.credits) * QHALF - x.popped_part;
        if (words > covered)
            ub(t, "a transfer from an output queue of more entries than its credits cover (17.9.6)", uint64_t(words), uint64_t(covered));
    }
    xfer_queue_ = q; xfer_left_ = words;
    dma_busy_from_ = t + 1; dma_last_take_ = t;
}
bool Tracer::out_ready(int64_t t, int q) {
    settle(t);
    if (q != xfer_queue_ || xfer_left_ == 0 || t < dma_busy_from_) return false;
    if (q == 0) return q0_holding_ && t >= q0_since_ && q0_pop_t_ != t;
    const OutQueue& x = oq[q];
    return x.pop_t != t && x.occ - (x.push_t == t ? 1 : 0) > 0;
}
void Tracer::out_take(int64_t t, int q, uint64_t w[2]) {
    if (!out_ready(t, q)) FATAL("Tracer: out_take at %lld with nothing ready", (long long)t);
    if (q == 0) {
        w[0] = rec[2 * q0ptr_]; w[1] = rec[2 * q0ptr_ + 1];
        q0ptr_++; q0_pop_t_ = t;
        if (q0ptr_ == REC_WORDS) q0_holding_ = false;
    } else {
        oq[q].take(t, w);
    }
    xfer_left_--;
    dma_last_take_ = t;
}

void Tracer::skip_cycles(int64_t n) const {
    if (n > 0 && must_step()) FATAL("Tracer: cycles skipped while it runs or something is in flight");
}
void Tracer::digest_words(int64_t out[DIGEST_N]) const {
    for (int i = 0; i < STAT_N; i++) out[i] = int64_t(int32_t(stat[i]));
}

void Tracer::observe(int64_t out[OBS_N]) const {
    int n = 0;
    const int64_t priv[16] = {errors_, running_, stopped_, rows_done_, rows_left_, rows_.in_flight, rows_.stage,
                              int64_t(int32_t(presented_)), expansion_offset_, latch_, rq_occ_, rq_half_, eq_occ_,
                              xd_.state, q0ptr_, xfer_left_};
    for (int64_t v : priv) out[n++] = v;
    const int64_t st[10] = {code_n, log_n, rows, instrs, i_miss, d_miss, writebacks, lru_hits, lru_misses, probe_reads};
    for (int64_t v : st) out[n++] = v;
    for (int i = 0; i < STAT_N; i++) out[n++] = int64_t(stat[i]);
    for (int i = 0; i < S_N; i++) out[n++] = stalls[i];
    for (int q = 1; q <= 3; q++) { out[n++] = oq[q].occ; out[n++] = oq[q].pushed; }
    out[n++] = n_errors_logged;
    out[n++] = n_violations;
    uint64_t h = 17;
    for (uint64_t x : rec) h = h * 31 + x;
    out[n++] = int64_t(h);
    for (unsigned b = 0; b < EV_BITS; b++) out[n++] = ev.count_of(b);
    if (n != OBS_N) FATAL("Tracer::observe: %d fields", n);
}

// meshsim only: the data cache's dirty lines, and the write-back buffer's line if its write is not yet made, written
// into the memory, untimed.
int Tracer::write_back_dirty_untimed() {
    int n = 0;
    FileStore::Walk w;
    if (fill_.write_held && !fill_.write_outstanding && fill_.write_line_kept) {
        mem->store->walk(mem_root(), mem_depth(), int64_t(fill_.write_line), w);
        mem->store->write_line(w.data_line, fill_.write_data);
        fill_.write_held = fill_.write_requested = fill_.write_request_valid = false;
        n++;
    }
    for (int s = dcache_.eldest(); s >= 0; s = dcache_.next(s)) {
        if (!dline_[s].dirty) continue;
        mem->store->walk(mem_root(), mem_depth(), int64_t(dcache_.key(s)), w);
        mem->store->write_line(w.data_line, dline_[s].d);
        dline_[s].dirty = false;
        n++;
    }
    return n;
}

}  // namespace tracer
}  // namespace units
