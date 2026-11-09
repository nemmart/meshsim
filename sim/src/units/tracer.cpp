// tracer.cpp: the Tracer (tracer.h), ported from `ref`'s Tracer.java (WORKER14), method for method and in its order.
#include "units/tracer.h"

#include <cstdio>
#include <cstring>

#include "base/fatal.h"

namespace units {
namespace tracer {

namespace {
// EventCounts as Tracer.java declares them: depths {1,0,0,2,0,2,1,0,0,0,0}, full-flagged bits 3 and 5 (the credits).
constexpr uint8_t kDepths[Tracer::EV_BITS] = {1, 0, 0, 2, 0, 2, 1, 0, 0, 0, 0};
constexpr bool kFlags[Tracer::EV_BITS] = {false, false, false, true, false, true, false, false, false, false, false};
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
        case E_PC0: return "a control transfer to pc 0 (4.21.4, item 146)";
        case E_IFETCH_BEYOND: return "an instruction fetch beyond the file's capacity";
        case E_DATA_BEYOND: return "a data fill beyond the file's capacity";
        case E_MAP_OFFSET: return "a jump whose offset from lowest_address doesn't fit 32 bits";
        case E_MAP_HOLE: return "the jump map's line is a hole (a dense file: corrupt)";
        case E_MAP_BEYOND: return "the jump map's line is beyond its capacity";
        case E_MAP_FREE: return "jump target not in the map (a free entry)";
        case E_MAP_FULL: return "jump target not in the map (probe_lines full lines)";
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
}
// Sets ERRORS bits; every error but queue overrun stops the Tracer where it is (item 152).
void Tracer::error(int64_t t, int bits, int code, uint64_t a, uint64_t b) {
    errors_ |= bits;
    if (n_errors_logged < FACTS) {
        Fact& f = error_log[n_errors_logged];
        f.t = t; f.code = code; f.bits = bits; f.a = a; f.b = b;
    }
    n_errors_logged++;
    if ((bits & ~(1 << ERR_OVERRUN)) != 0) stopped_ = true;
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
    last_accept_ = t;
    landing_.push(Input{t + RP_LATENCY, src, int(offset), size, lo, hi});
}
void Tracer::run(int64_t t) { inputs_.push(Input{t, IN_RUN, 0, 0, 0, 0}); }

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
            inputs_.push(Input{t, IN_WORD, w, int(v), 0, 0});
            return 1;
        case W_ROWS_RUN: case W_EXP_OFFSET: case W_EXP_OPCODE: case W_STATE_LENGTH: case W_CODE_LENGTH:
        case W_RLOG_LENGTH: case W_WLOG_LENGTH:
            return 0;  // a bus error (8.16)
        default: return w >= W_STAT && w < W_STAT + STAT_N ? 0 : -1;  // the counters: read-only, a bus error
    }
}

void Tracer::reset(int64_t) {
    inputs_.clear(); landing_.clear(); settled_ = INT64_MIN;
    running_ = stopped_ = rows_ended_ = drained_ = false; cur_ = Instr(); errors_ = 0; n_errors_logged = 0;
    run_config_ = hash_mul_ = hash_step_ = hash_lines_ = 0; length_code_ = length_rlog_ = length_wlog_ = 0;
    rq_head_ = rq_occ_ = rq_half_ = rq_popped_ = 0; eq_head_ = eq_occ_ = eq_popped_ = 0;
    icache_.clear(); dcache_.clear(); jump_lru_.clear();
    code_n = log_n = 0; ev.clear_all(); presented_ = 0; expansion_offset_ = 0; latch_ = -1;
    for (int i = 0; i < STAT_N; i++) stat[i] = 0;
    stat_until_ = INT64_MIN;
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
    s_code_l_ = length_code_; s_rlog_l_ = length_rlog_; s_wlog_l_ = length_wlog_;
    s_mul_ = hash_mul_; s_step_ = hash_step_; s_lines_ = hash_lines_;
    for (int i = 0; i < STAT_N; i++) s_stat_[i] = stat[i];
    ev.live(EV_SVC_STATE, rows_ended_ && !stopped_ && q0ptr_ < REC_WORDS);
    for (int q = 1; q <= 3; q++) ev.live(EV_SVC_STATE + unsigned(q), rows_ended_ && !stopped_ && oq[q].flag());
}

void Tracer::store_word(int64_t t, int w, uint32_t v) {
    switch (w) {
        case W_RUN_CONFIG:
            if (running_) ub(t, "a RUN_CONFIG write while running (4.21.3)");
            run_config_ = v & RC_DEFINED;
            break;
        case W_HASH_MUL:
            if (running_) ub(t, "a HASH_MUL write while running (4.21.3)");
            hash_mul_ = v;
            break;
        case W_HASH_STEP:
            if (running_) ub(t, "a HASH_STEP write while running (4.21.3)");
            hash_step_ = v & 0xFFFFFF;
            break;
        case W_HASH_LINES:
            if (running_) ub(t, "a HASH_LINES write while running (4.21.3)");
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
            break;
        default: FATAL("Tracer: store_word %d", w);
    }
}

// ================================================================ the write port (4.21.2)
void Tracer::land(int64_t t, int, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
    const uint32_t part = offset >> 20;
    if (size != 2 || (offset & 15) != 0) ub(t, "a 32- or 64-bit or misaligned packet (4.21.2: 128-bit packets only)", offset, uint64_t(size));
    switch (part) {
        case 0: {
            const int wd = int(offset >> 4);
            if (offset < 0x440) {
                if (running_) ub(t, "a part-0 write while running (4.21.3): record word", uint64_t(wd));
                if (wd == REC_BITMAP) return;  // an output: accepted and ignored
                rec[2 * wd] = lo; rec[2 * wd + 1] = hi;
                if (wd == 0) state_write(t);
            } else if (offset >= 0x1000 && offset < 0x2000) {
                if (running_) ub(t, "a decode-table write while running (4.21.2)", offset);
                const int e0 = int((offset - 0x1000) >> 4) * 8;
                for (int k = 0; k < 8; k++)
                    decode_table[e0 + k] = uint16_t((k < 4 ? lo >> (16 * k) : hi >> (16 * (k - 4))) & 0xFFFF);
            } else if (offset >= 0x2000 && offset < 0x4000) {
                if (running_) ub(t, "a ROM write while running (item 156: single-ported)", offset);
                const int j = int((offset - 0x2000) >> 4);
                rom_lo[j] = lo; rom_hi[j] = hi;
            } else {
                ub(t, "a write to an unmapped part-0 offset", offset);
            }
            break;
        }
        case 2: {
            if (rq_occ_ >= QCAP) { error(t, 1 << ERR_OVERRUN, E_RQ_FULL); return; }
            const int i = (rq_head_ + rq_occ_) % QCAP;
            rq_lo_[i] = lo; rq_hi_[i] = hi; rq_occ_++;
            break;
        }
        case 4: {
            if (eq_occ_ >= QCAP) { error(t, 1 << ERR_OVERRUN, E_EQ_FULL); return; }
            const int i = (eq_head_ + eq_occ_) % QCAP;
            eq_lo_[i] = lo; eq_hi_[i] = hi; eq_occ_++;
            break;
        }
        default:
            ub(t, part == 1 || part == 3 ? "a write to an unused part (4.21.2)" : "a write to an unmapped part (4.21.2)", part);
    }
}

// The write to word 0 lands (4.21.3): every queue emptied, the credits set, the counters cleared, the caches and the
// jump LRU invalidated without writing back dirty lines, the template latch and bitmap cleared.
void Tracer::state_write(int64_t t) {
    // word 0 lands before word 1 (state first), so the advice field is still the last segment's leftover here
    if (advice_left() > 0) violate(t, V_ADVICE_UNCONSUMED, advice_left());
    rq_head_ = rq_occ_ = rq_half_ = rq_popped_ = 0; eq_head_ = eq_occ_ = eq_popped_ = 0; exp_discard_ = 0;
    code_n = log_n = 0; length_code_ = length_rlog_ = length_wlog_ = 0; rows_ended_ = false; drained_ = false;
    q0ptr_ = REC_WORDS;
    for (int q = 1; q <= 3; q++) oq[q].reset();
    ev.set(t, EV_ADVICE, 2); ev.set(t, EV_EXP, 2);
    icache_.clear(); dcache_.clear(); jump_lru_.clear(); i_seen_.clear();
    latch_ = -1; bitmap_[0] = bitmap_[1] = 0; cur_ = Instr();
    for (int i = 0; i < STAT_N; i++) stat[i] = 0;
    stat_until_ = INT64_MIN;
    if (cold_mem_on_state_write && mem) mem->cold();
    state_writes++;
}

// ================================================================ PULSE_RUN (4.21.3)
void Tracer::do_run(int64_t t) {
    if (running_) ub(t, "PULSE_RUN while a run is in progress (4.17.2)");
    const int n = int(run_config_ & RC_ROWS);
    if (n > MAX_ROWS) ub(t, "RUN_CONFIG [23:0] above 65,536 rows is UB on the Tracer (4.21.3)", uint64_t(n));
    if (code_n != 0 || log_n != 0) FATAL("Tracer: PULSE_RUN at %lld with the last window's FIFOs not drained", (long long)t);
    running_ = true; rows_ended_ = false; drained_ = false; done_ = false; q0ptr_ = REC_WORDS; xfer_left_ = 0;
    for (int q = 1; q <= 3; q++) oq[q].reset();
    rows_target_ = n; rows_done_ = 0; start_at_ = t + 1; run_id_++; runs++; run_start_at = t;
    set1_code_.reset(); set1_rlog_.reset(); set1_wlog_.reset(); rlog_values_ = wlog_values_ = 0;
    length_code_ = length_rlog_ = length_wlog_ = 0;
    bitmap_[0] = bitmap_[1] = 0;
    if (cur_.valid && cur_.kind == rowcore::K_CTRL && cur_.next_row > 0 && latch_ >= 0)
        bitmap_[latch_ >> 6] |= uint64_t(1) << (latch_ & 63);
}

// ================================================================ the cycle
void Tracer::step(int64_t T) {
    settle(T);
    if (exp_discard_ > 0 && eq_occ_ > 0) {  // a NOP-substituted expansion's words, dropped one a cycle (N3)
        eq_pop(T);
        exp_discard_--;
    }
    if (running_) stat[ST_CYCLES]++;  // STATUS [0] is 1: the drain included, and after a stopping error
    if (!running_ || stopped_) return;
    if (rows_ended_) { drain_cycle(T); return; }
    run_cycles++;
    if (T < start_at_) return;
    if (T < stat_until_) stat[stat_cause_]++;
    if (busy_until_ >= T) {
        if (T == busy_until_) {
            rows_done_++;
            if (rows_done_ == rows_target_) end_rows(T);
        }
        return;
    }
    if (rows_target_ == 0) { end_rows(T); return; }
    if (T < wait_until_) {
        stalls[wait_cause_]++;
        if (wait_cause_ == S_IFETCH) stat[ST_STALL_FETCH]++;
        return;
    }
    row(T);
}

// Starts the next row at T if its inputs are present; else counts the stall.
void Tracer::row(int64_t T) {
    using namespace rowcore;
    // ---- the instruction
    if (!cur_.valid) {
        cur_ = Instr();
        cur_.valid = true;
        cur_.next_row = state_offset();  // a segment's start may be inside an expansion (4.21.4)
    }
    Instr& in = cur_;
    if (!in.fetched) {
        if (pc() == 0) {
            in.w = 0; in.kind = K_PAD; in.fetched = true; in.next_row = 0;
        } else {
            const int64_t ready = fetch(T, in);
            if (stopped_) return;
            in.fetched = true;
            if (ready > T) {
                wait_until_ = ready; wait_cause_ = S_IFETCH; stalls[S_IFETCH]++; stat[ST_STALL_FETCH]++;
                return;
            }
        }
    }
    if (in.kind == K_CTRL && !in.presented) {
        in.presented = true;
        presented_ = in.w; expansion_offset_ = in.next_row;
        ev.set(T, EV_EXP, 2); eq_popped_ = 0;
        ev.post(T, EV_NEWEXP);
        latch_ = -1; template_stored_ = false;
        new_expansions++;
    }
    if (in.kind == K_BAD) {
        error(T, 1 << ERR_INVALID, in.cls == 3 ? E_CLASS3 : E_NO_32BIT_FORM, in.w);
        return;
    }

    // ---- the row's expansion word
    uint64_t lo, hi;
    const int row_idx = in.next_row;
    switch (in.kind) {
        case K_PAD: lo = rom_lo[0]; hi = rom_hi[0]; break;
        case K_NOP: lo = rom_lo[1]; hi = rom_hi[1]; break;
        case K_ROM: {
            const int a = int(in.start) + row_idx;
            if (a > 511) { error(T, 1 << ERR_INVALID, E_ROM_PAST_511, uint64_t(a)); return; }
            lo = rom_lo[a]; hi = rom_hi[a];
            break;
        }
        default:
            if (exp_discard_ > 0) { stalls[S_EXP]++; return; }  // the previous expansion's dropped words still arriving
            if (eq_occ_ == 0) { stalls[in.ctrl_first_seen ? S_EXP : S_NEWEXP]++; return; }
            lo = eq_lo_[eq_head_]; hi = eq_hi_[eq_head_];
    }
    if (reserved(lo) != 0) ub(T, "an expansion word with reserved bits [61:55] set", lo);
    {
        const unsigned why = invalid_word(lo);
        if (why != INV_OK) {
            error(T, 1 << ERR_INVALID, why == INV_OPCODE ? E_INVALID_OPCODE : why == INV_OPERAND ? E_INVALID_OPERAND : E_INVALID_FORMAT, lo);
            return;
        }
    }
    const unsigned op = rowcore::op(lo), c_rd = rowcore::c_rd(lo), c_rs1 = rowcore::c_rs1(lo), c_rs2 = rowcore::c_rs2(lo);
    const unsigned seq_rem = seq_remaining(lo), fmt = format(lo), rd0w = rd0(lo);
    const bool nfv = next_from_value(lo), hld = hold(lo), i0p0 = imm0pc0(lo), flog = from_log(lo), oimm = offset_imm(lo);
    // the rd = x0 policy for a controller-fed expansion, read from the first word delivered (4.19.4, N3)
    if (in.kind == K_CTRL && !in.ctrl_first_seen) {
        in.ctrl_first_seen = true;
        const unsigned rd_field = (in.w >> 7) & 31;
        in.offset = rd_field == 0 && rd0w == 2 ? 1 : 0;
        if (rd_field == 0 && (rd0w == 1 || rd0w == 3) && row_idx == 0) {
            eq_pop(T); exp_discard_ = int(seq_rem); in.kind = K_NOP; in.rd0 = rd0w;
            stalls[S_EXP]++;  // the NOP row starts next cycle
            return;
        }
    }
    // ---- operands (row_core, shared with the replayer)
    const uint32_t w = in.w;
    const uint64_t src_imm = needs_source_imm(lo) ? source_imm(fmt, w) : 0, so = oimm ? src_imm : 0;
    const int r_rd = reg(c_rd, w, in.offset), r_rs1 = reg(c_rs1, w, in.offset), r_rs2 = reg(c_rs2, w, in.offset);
    const uint64_t v_rd = r_rd >= 0 ? reg_value(r_rd) : imm_val(c_rd, hi, src_imm);
    const uint64_t v_rs1 = r_rs1 >= 0 ? reg_value(r_rs1) : imm_val(c_rs1, hi, src_imm);
    const uint64_t v_rs2 = r_rs2 >= 0 ? reg_value(r_rs2) : imm_val(c_rs2, hi, src_imm);
    const uint64_t imm_sum = (r_rd < 0 ? v_rd : 0) + (r_rs1 < 0 ? v_rs1 : 0) + (r_rs2 < 0 ? v_rs2 : 0);

    const bool is_mem = flog && (alu::is_load(op) || alu::is_store(op));
    const bool is_advice = flog && op == alu::ADVICE;
    // ---- polled inputs first: advice (4.21.4)
    uint64_t advice_val = 0;
    if (is_advice) {
        if (advice_left() == 0) { error(T, 1 << ERR_ADVICE_UNDER, E_ADVICE_ZERO); return; }
        if (rq_occ_ == 0) { stalls[S_ADVICE]++; return; }
        advice_val = rq_half_ == 0 ? rq_lo_[rq_head_] : rq_hi_[rq_head_];
    }
    const bool last = seq_rem == 0 || in.kind == K_PAD || in.kind == K_NOP;

    // ---- the row starts: everything below commits
    int64_t ready = T;
    bus_wait_ = 0;
    if (in.kind == K_CTRL) eq_pop(T);
    if (is_advice) pop_advice(T);
    uint64_t log_word = 0;
    int tag = -1;
    int mem_slot = -1, mem_word = 0;
    if (is_mem) {
        const uint64_t ea = v_rs1 + imm_sum;
        const uint64_t mem_a = alu::mem_addr(op, v_rs1, imm_sum);
        const uint64_t o = mem_a - uint64_t(lowest());
        int bad = 0;
        if (o >= (uint64_t(8) << log2k())) bad |= 1 << ERR_RANGE;  // the range comparator (8 K, Q9)
        if ((mem_a & 7) != 0) bad |= 1 << ERR_MISALIGNED;           // the floor address (item 150)
        if (bad != 0) { error(T, bad, E_MEM_RANGE_ALIGN, mem_a, ea); return; }
        ready = data_line(T, int64_t(o >> 7));
        if (stopped_) return;
        mem_slot = dcache_.find(o >> 7);
        mem_word = int((o >> 3) & 15);
        if (alu::is_store(op) && !dline_[mem_slot].writable) { error(T, 1 << ERR_STORE, E_STORE_NOT_WRITABLE, mem_a); return; }
        log_word = dline_[mem_slot].d[mem_word];  // the loaded doubleword, or the pre-value
        tag = alu::is_load(op) ? TAG_LOAD : TAG_WRITE;
    } else if (is_advice) {
        log_word = advice_val;
        tag = TAG_OTHER;
    }

    const bool compressed = (w & 1) == 0;
    const unsigned pc_inc = !last ? 0 : compressed ? 2 : 4;
    const uint64_t pcv = pc();
    alu::exec(op, v_rs1, v_rs2, v_rd, pcv, pc_inc, i0p0, log_word, so, alu_);
    if (alu_.undefined != nullptr) violate(T, V_UNDEFINED, op);
    // assertions (4.21.4: evaluated, appendix 2 H)
    if (!assertion_holds(op, v_rs1, v_rs2)) { error(T, 1 << ERR_ASSERT, E_ASSERT, v_rs1, v_rs2); return; }
    // pc and instr_idx (4.19.7, 4.21.4)
    uint64_t new_pc;
    int32_t new_idx;
    if (hld) {
        new_pc = pcv; new_idx = instr_idx();
    } else {
        const uint64_t np = alu_.new_pc;
        if (np == 0 && op == alu::JAL && i0p0 && v_rs1 == 0) {  // the program's end (any JAL with immediate 0)
            if (!nfv) violate(T, V_END_NO_NFV);
            new_pc = 0; new_idx = 0; log_word = 0; tag = TAG_OTHER;
        } else if (np != pcv) {
            if (np == 0 && op != alu::JAL) { error(T, 1 << ERR_MAP, E_PC0, op); return; }
            if (!nfv) violate(T, V_XFER_NO_NFV, op);
            const uint64_t base = lookup(T, np);
            if (stopped_) return;
            if (lookup_ready_ > ready) ready = lookup_ready_;
            new_pc = np; new_idx = int32_t(uint32_t(base)); log_word = base & 0xFFFFFFFF; tag = TAG_OTHER;
        } else {
            new_pc = pcv + pc_inc; new_idx = int32_t(uint32_t(instr_idx()) + 1);
        }
    }
    // commit: memory, registers, the FIFOs
    if (mem_slot >= 0 && alu::is_store(op)) { dline_[mem_slot].d[mem_word] = v_rs2; dline_[mem_slot].dirty = true; }
    const bool writes = r_rd >= 0 && op != alu::NOP;
    if (writes && r_rd != 0) rec[4 + r_rd] = alu_.rd;
    if (in.code_run != run_id_) { push_code(in.w); in.code_run = run_id_; }
    if (tag >= 0) push_log(uint8_t(tag), log_word);
    if (in.kind == K_CTRL && in.cls == 1 && !template_stored_) violate(T, V_INLINE_NO_TEMPLATE);
    set_pc_idx(new_pc, new_idx);
    in.next_row++;
    rows++;
    if (alu::cls(op) == 'M') rows_m++;
    if (in.kind == K_PAD) rows_pad++;
    if (last) {
        cur_.valid = false; instrs++; set_state_offset(0);
    } else {
        expansion_offset_ = in.next_row; set_state_offset(in.next_row);  // the state word is live (4.21.2)
    }
    if (ready > T) {
        stalls[is_mem ? S_DMISS : S_PROBE] += ready - T - bus_wait_;
        // the counters: the whole wait, the write-back buffer's included (STAT_STALL_DATA), one cycle at a time from T
        stat_cause_ = is_mem ? ST_STALL_DATA : ST_STALL_MAP; stat_until_ = ready; stat[stat_cause_]++;
    }
    busy_until_ = ready + (alu::cls(op) == 'M' ? mul_cycles : row_cycles) - 1;
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

void Tracer::eq_pop(int64_t T) {
    eq_head_ = (eq_head_ + 1) % QCAP; eq_occ_--;
    if (++eq_popped_ == QHALF) { eq_popped_ = 0; ev.post(T, EV_EXP); }
}
void Tracer::pop_advice(int64_t T) {
    set_advice_left(advice_left() - 1);
    if (rq_half_ == 0) {
        rq_half_ = 1;
    } else {
        rq_half_ = 0; rq_head_ = (rq_head_ + 1) % QCAP; rq_occ_--;
        if (++rq_popped_ == QHALF) { rq_popped_ = 0; ev.post(T, EV_ADVICE); }
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

// ================================================================ fetch (4.21.4 step 1, 4.21.5)
// Fetches and decodes the instruction at pc into in; returns the cycle its halves are present.
int64_t Tracer::fetch(int64_t T, Instr& in) {
    const int64_t o = int64_t(pc() - uint64_t(lowest()));
    int64_t t = T;
    uint32_t h0, h1;
    t = fetch16(t, o, h0, false);
    if (stopped_) return t;
    uint32_t word;
    if ((h0 & 3) == 3) {
        const bool straddle = (uint64_t(o + 2) >> 7) != (uint64_t(o) >> 7);
        if (straddle) straddles++;
        t = fetch16(t, o + 2, h1, straddle);
        if (stopped_) return t;
        word = h0 | h1 << 16;
    } else {
        word = rvc::code_word(h0);
        if (word == 0) {
            in.w = rvc::uncompress(h0) & ~1u; in.kind = rowcore::K_BAD; in.cls = -1;
            return t;
        }
    }
    in.w = word;
    rowcore::decode(word, decode_table, rom_lo, dec_);
    in.kind = dec_.kind; in.start = dec_.start; in.rd0 = dec_.rd0; in.offset = dec_.offset; in.cls = int(dec_.cls);
    if (in.kind == rowcore::K_NOP) in.next_row = 0;
    return t;
}

// 16 bits at .pages offset o through the instruction cache; returns when they are present.
int64_t Tracer::fetch16(int64_t t, int64_t o, uint32_t& out, bool second) {
    const int64_t line = int64_t(uint64_t(o) >> 7);
    i_access++;
    int s = icache_.find(uint64_t(line));
    if (s < 0) {
        i_miss++; stat[ST_MISS_I]++;
        bool first_seen = i_seen_.peek(uint64_t(line)) < 0;
        if (first_seen) {
            if (i_seen_.size() >= i_seen_.capacity()) FATAL("Tracer: more than %d instruction lines seen in a segment", i_seen_.capacity());
            i_seen_.insert(uint64_t(line));
        }
        const int cls = (first_seen ? 0 : 2) + (second ? 1 : 0);
        const int64_t lp = mem->lpddr_reads;
        const int64_t t0 = t > mem->free_at() ? t : mem->free_at();
        mem->read(t0, mem_root(), mem_depth(), line, resp_);
        i_miss_by[cls]++; i_cyc_by[cls] += resp_.ready_at - t;
        if (mem->lpddr_reads != lp) i_miss_lpddr++;
        if (resp_.status == FileStore::ST_BEYOND) { error(t, 1 << ERR_PAGE, E_IFETCH_BEYOND, uint64_t(o)); return t; }
        s = icache_.insert(uint64_t(line));
        std::memcpy(iline_[s].d, resp_.data, sizeof resp_.data);
        iline_[s].dirty = false; iline_[s].writable = false;
        if (icache_.size() > icache_lines_) icache_.remove(icache_.eldest());
        t = resp_.ready_at;
    }
    const int b = int(o & 127);
    out = uint32_t((iline_[s].d[b >> 3] >> (8 * (b & 7))) & 0xFFFF);
    return t;
}

// The data line (its .pages line number) into the data cache; returns when it is present. A store's write-allocate
// fills first (4.21.5); a dirty victim is written back as soon as the fill completes.
int64_t Tracer::data_line(int64_t t, int64_t line) {
    d_access++;
    if (dcache_.find(uint64_t(line)) >= 0) return t;
    d_miss++; stat[ST_MISS_D]++;
    const int64_t t0 = t > mem->free_at() ? t : mem->free_at();
    if (t0 > t) { stalls[S_BUS] += t0 - t; bus_wait_ = t0 - t; }
    mem->read(t0, mem_root(), mem_depth(), line, resp_);
    if (resp_.status == FileStore::ST_BEYOND) { error(t, 1 << ERR_PAGE, E_DATA_BEYOND, uint64_t(line)); return t; }
    const int s = dcache_.insert(uint64_t(line));
    std::memcpy(dline_[s].d, resp_.data, sizeof resp_.data);
    dline_[s].dirty = false;
    dline_[s].writable = resp_.status == FileStore::ST_OK;
    const int64_t ready = resp_.ready_at;
    if (dcache_.size() > CACHE_LINES) {
        const int v = dcache_.eldest();
        const uint64_t vkey = dcache_.key(v);
        if (dline_[v].dirty) {
            mem->write(ready, mem_root(), mem_depth(), int64_t(vkey), dline_[v].d);
            writebacks++;
        }
        dcache_.remove(v);
    }
    return ready;
}

// ================================================================ jump targets (4.21.4)
// The base instr_idx of the jump target np: the LRU, else the map probe. Errors stop (lookup_ready_: when known).
uint64_t Tracer::lookup(int64_t T, uint64_t np) {
    lookup_ready_ = T;
    {
        const int s = jump_lru_.find(np);
        if (s >= 0) { lru_hits++; return uint64_t(uint32_t(jump_base_[s])); }
    }
    lru_misses++; stat[ST_PROBES]++;
    const uint64_t o = np - uint64_t(lowest());
    if (o >= (uint64_t(1) << 32)) { error(T, 1 << ERR_MAP, E_MAP_OFFSET, np); return 0; }
    const uint32_t key = uint32_t(o);
    const int k = int(hash_lines_);
    int64_t h = k == 0 ? 0 : int64_t((uint64_t(key) * uint64_t(hash_mul_) & 0xFFFFFFFFull) >> (32 - k));
    int64_t t = T;
    MemSystem* mm = map_mem ? map_mem : mem;
    for (int n = 0; n < probe_lines; n++) {
        int64_t t0 = t > mem->free_at() ? t : mem->free_at();
        if (mm->free_at() > t0) t0 = mm->free_at();
        mm->read(t0, map_root(), map_depth(), h, resp_);
        probe_reads++;
        t = resp_.ready_at + probe_compare;
        if (resp_.status == FileStore::ST_HOLE || resp_.status == FileStore::ST_BEYOND) {
            error(T, 1 << ERR_PAGE, resp_.status == FileStore::ST_HOLE ? E_MAP_HOLE : E_MAP_BEYOND, uint64_t(h));
            return 0;
        }
        bool free = false;
        for (int j = 0; j < 16; j++) {
            const uint64_t e = resp_.data[j];
            const uint32_t ek = uint32_t(e);
            if (ek == key) {
                const uint64_t base = e >> 32;
                int s = jump_lru_.peek(np);  // absent: a miss above, nothing since
                if (s < 0) s = jump_lru_.insert(np); else jump_lru_.find(np);
                jump_base_[s] = int32_t(uint32_t(base));
                if (jump_lru_.size() > LRU_ENTRIES) jump_lru_.remove(jump_lru_.eldest());
                lookup_ready_ = t;
                return base;
            }
            if (ek == 0xFFFFFFFFu) free = true;
        }
        if (free) { error(T, 1 << ERR_MAP, E_MAP_FREE, np, uint64_t(h)); return 0; }
        h = int64_t((uint64_t(h) + (hash_step_ & 0xFFFFFF)) & ((uint64_t(1) << k) - 1));
    }
    error(T, 1 << ERR_MAP, E_MAP_FULL, np, uint64_t(probe_lines));
    return 0;
}

// ================================================================ the rows' end (4.21.6)
void Tracer::end_rows(int64_t T) {
    rows_ended_ = true; rows_end_at = T;
    const bool cc = run_config_ & RC_COMP_CODE, cr = run_config_ & RC_COMP_RLOG, cw = run_config_ & RC_COMP_WLOG;
    const streamword::Size sc = choose(code_n, (code_n + 3) / 4, set1_code_.bits, cc);
    const streamword::Size sr = choose(rlog_values_, (rlog_values_ + 1) / 2, set1_rlog_.bits, cr);
    const streamword::Size sw = choose(wlog_values_, (wlog_values_ + 1) / 2, set1_wlog_.bits, cw);
    uint64_t s[2];
    streamword::build(sc, sr, sw, advice_left(), s);
    rec[2] = s[0]; rec[3] = s[1];
    rec[2 * REC_BITMAP] = bitmap_[0]; rec[2 * REC_BITMAP + 1] = bitmap_[1];
    length_code_ = sc.words; length_rlog_ = sr.words; length_wlog_ = sw.words;
    start_drain(T);
}
// 4.21.6's choice: LRU-coded when enabled and fewer 128-bit words than raw, raw on a tie.
streamword::Size Tracer::choose(int64_t values, int64_t raw_words, int64_t coded_bits, bool enabled) {
    const int64_t coded = (coded_bits + 127) / 128;
    const bool c = enabled && coded < raw_words;
    return streamword::make(c ? coded : raw_words, values, c);
}

// ================================================================ the output path (4.21.6)
void Tracer::OutQueue::push(int64_t t, uint64_t l, uint64_t h) {
    if (occ >= QCAP) FATAL("Tracer: a push into a full output queue (the scan's room check)");
    const int k = (head + occ) % QCAP;
    lo[k] = l; hi[k] = h; at[k] = t; occ++; pushed++;
    if (occ > max_occ) max_occ = occ;
    if (pushed % QHALF == 0) {
        if (granted >= BOUNDS) FATAL("Tracer: more than %d output credits in a window", BOUNDS);
        bound[granted++] = pushed;
    }
}
void Tracer::OutQueue::end() {
    ended = true;
    if (pushed % QHALF != 0) {
        if (granted >= BOUNDS) FATAL("Tracer: more than %d output credits in a window", BOUNDS);
        bound[granted++] = pushed;
    }
}
void Tracer::OutQueue::take(int64_t t, uint64_t w[2]) {
    w[0] = lo[head]; w[1] = hi[head]; head = (head + 1) % QCAP; occ--; read++;
    if (pops_cycle != t) { pops_cycle = t; pops_at = 0; }
    pops_at++; last_pop_t = t;
}

void Tracer::start_drain(int64_t T) {
    q0ptr_ = 0; scan_start_ = T + 2;  // [7] in the cycle after the last row, the scans in the cycle after that
    streamword::Size sz[3];
    streamword::parse(rec[2], rec[3], sz);
    comp0_ = sz[0].compressed; comp1_ = sz[1].compressed; comp2_ = sz[2].compressed;
    set2_code_.reset(); set2_rlog_.reset(); set2_wlog_.reset();
    for (int q = 1; q <= 3; q++) oq[q].reset();
    pk_code_.reset(); pk_rlog_.reset(); pk_wlog_.reset();
    code_scan_ = log_scan_ = 0; code_ended_ = log_ended_ = scans_done_ = done_ = false;
}

// One cycle of the drain: each scan moves one FIFO entry into its encoder and packer while its queue has room.
void Tracer::drain_cycle(int64_t T) {
    push_t_ = T;
    auto to1 = [this](uint64_t a, uint64_t b) { oq[1].push(push_t_, a, b); };
    auto to2 = [this](uint64_t a, uint64_t b) { oq[2].push(push_t_, a, b); };
    auto to3 = [this](uint64_t a, uint64_t b) { oq[3].push(push_t_, a, b); };
    const int64_t st0 = scan_stalls;
    if (!scans_done_ && T >= scan_start_) {
        // the code scan
        if (!code_ended_) {
            if (code_scan_ < code_n) {
                const uint64_t v = code_fifo[code_scan_];
                const int bits = comp0_ ? set2_code_.peek_bits(v) : 32;
                if (pk_code_.fill + bits >= 128 && !oq[1].room(T)) { oq[1].stall_cycles++; scan_stalls++; }
                else {
                    if (comp0_) set2_code_.encode(v, pk_code_, to1); else pk_code_.put(v, 32, to1);
                    code_scan_++;
                }
            } else if (pk_code_.fill == 0 || oq[1].room(T)) {
                pk_code_.finish(to1); oq[1].end(); code_ended_ = true;
            }
        }
        // the log scan: tags 0 and 1 to the read log, 2 to the write log; every load passes set 2's write-log table
        if (!log_ended_) {
            if (log_scan_ < log_n) {
                const uint64_t v = log_fifo[log_scan_];
                const uint8_t tag = log_tag[log_scan_];
                const bool wr = tag == TAG_WRITE;
                LruCoder& c = wr ? set2_wlog_ : set2_rlog_;
                Bits& pk = wr ? pk_wlog_ : pk_rlog_;
                const bool cmp = wr ? comp2_ : comp1_;
                OutQueue& q = oq[wr ? 3 : 2];
                const int bits = cmp ? c.peek_bits(v) : 64;
                if (pk.fill + bits >= 128 && !q.room(T)) { q.stall_cycles++; scan_stalls++; }
                else {
                    if (wr) { if (cmp) c.encode(v, pk, to3); else pk.put(v, 64, to3); }
                    else { if (cmp) c.encode(v, pk, to2); else pk.put(v, 64, to2); }
                    if (tag == TAG_LOAD) set2_wlog_.last_load(v);
                    log_scan_++;
                }
            } else {
                const bool r2 = pk_rlog_.fill == 0 || oq[2].room(T), r3 = pk_wlog_.fill == 0 || oq[3].room(T);
                if (r2 && r3) { pk_rlog_.finish(to2); pk_wlog_.finish(to3); oq[2].end(); oq[3].end(); log_ended_ = true; }
            }
        }
        if (code_ended_ && log_ended_) {
            scans_done_ = true;
            streamword::Size sz[3];
            streamword::parse(rec[2], rec[3], sz);
            if (pk_code_.words != sz[0].words || pk_rlog_.words != sz[1].words || pk_wlog_.words != sz[2].words)
                FATAL("Tracer: set 2's words differ from set 1's: %lld/%lld/%lld against %u/%u/%u", (long long)pk_code_.words,
                      (long long)pk_rlog_.words, (long long)pk_wlog_.words, sz[0].words, sz[1].words, sz[2].words);
            code_n = log_n = 0;
        }
    }
    if (scan_stalls != st0) stat[ST_STALL_OUTPUT]++;  // either scan stalled on a full output queue (item 157)
    // done comes last (4.21.6): the rows, both scans, every queue empty and the last transfer finished, all as of the
    // end of T - 1 (so the cycle the DMA took the last word doesn't matter)
    if (scans_done_ && q0ptr_ == REC_WORDS && q0_last_t_ < T && xfer_left_ == 0) {
        bool empty = true;
        for (int q = 1; q <= 3; q++) empty = empty && oq[q].occ == 0 && oq[q].last_pop_t < T;
        if (empty) {
            done_ = true; running_ = false; drained_at = T; drain_cycles += T - rows_end_at; ev.post(T, EV_RUN_DONE);
        }
    }
}

// ---- the DMA's side (4.17.9): four output queues; queue 0 is part 0 read in place
void Tracer::out_start(int64_t t, int q, int64_t words) {
    if (xfer_left_ > 0) FATAL("Tracer: a transfer started at %lld with one in progress", (long long)t);
    if (q == 0) {
        if (q0ptr_ + words > REC_WORDS) ub(t, "a transfer from output queue 0 reading past word 67 (4.21.6)", uint64_t(words), uint64_t(q0ptr_));
    } else if (words > oq[q].covered()) {
        ub(t, "a transfer from an output queue of more words than its credits cover (4.21.6)", uint64_t(words), uint64_t(oq[q].covered()));
    }
    xfer_queue_ = q; xfer_left_ = words;
}
bool Tracer::out_ready(int64_t t, int q) const {
    if (q != xfer_queue_ || xfer_left_ == 0) return false;
    if (q == 0) return rows_ended_ && q0ptr_ < REC_WORDS && rows_end_at < t;
    return oq[q].ready(t);
}
void Tracer::out_take(int64_t t, int q, uint64_t w[2]) {
    if (!out_ready(t, q)) FATAL("Tracer: out_take at %lld with nothing ready", (long long)t);
    if (q == 0) { w[0] = rec[2 * q0ptr_]; w[1] = rec[2 * q0ptr_ + 1]; q0ptr_++; q0_last_t_ = t; }
    else oq[q].take(t, w);
    xfer_left_--;
}

void Tracer::skip_cycles(int64_t n) const {
    if (n > 0 && (running_ || stat_until_ > settled_))
        FATAL("Tracer: the counters (words 44-51) would tick while its tile sleeps");
}
void Tracer::digest_words(int64_t out[STAT_N]) const {
    for (int i = 0; i < STAT_N; i++) out[i] = int64_t(int32_t(stat[i]));
}

void Tracer::observe(int64_t out[OBS_N]) const {
    int n = 0;
    const int64_t priv[16] = {errors_, running_, stopped_, rows_done_, busy_until_, wait_until_, stat_until_,
                              int64_t(int32_t(presented_)), expansion_offset_, latch_, rq_occ_, rq_half_, eq_occ_,
                              exp_discard_, q0ptr_, xfer_left_};
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

int Tracer::write_back_dirty_untimed() {
    int n = 0;
    FileStore::Walk w;
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
