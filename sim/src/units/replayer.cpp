// replayer.cpp: the replayer's cycle (WORKER15): sim/ref/meshsim/src/Replayer.java, method for method.
#include "units/replayer.h"

#include <cstdio>

#include "units/stream_word.h"

namespace units {

namespace {
uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
}  // namespace

Replayer::Replayer(unsigned m) : me(m) {
    const uint8_t depth[] = {1, 0, 2, 2, 2, 2, 1};
    const bool flags[] = {false, false, true, true, true, true, false};
    ev.init(7, depth, flags);
    code.init(0, 256, 32, false);
    rlog.init(1, LOG_QUEUE_ENTRIES, 64, false);
    wlog.init(2, LOG_QUEUE_ENTRIES, 64, true);
    code.credit = EV_CODE; rlog.credit = EV_RLOG; wlog.credit = EV_WLOG;
}

// ================================================================ facts (D24)
void Replayer::record(int64_t t, uint16_t c, uint64_t a, uint64_t b) {
    ReplayerFact& f = facts_[fact_n_ % FACT_RING];
    f.t = t; f.code = c; f.a = a; f.b = b;
    fact_n_++;
    if (fact_shown_ < FACT_RING) fact_shown_++;
    fact_counts_[c]++;
}
void Replayer::violate(int64_t t, uint16_t c, uint64_t a, uint64_t b) { record(t, c, a, b); }

void Replayer::fact_text(const ReplayerFact& f, char* buf, size_t n) {
    const unsigned long long a = f.a, b = f.b;
    const long long t = f.t;
    switch (f.code) {
        case V_NO_FLUSH: std::snprintf(buf, n, "@%lld a pass started without a write to the state word since the last pass", t); break;
        case V_TWO_SOURCES: std::snprintf(buf, n, "@%lld two sources feed one queue port in a pass (invariant 18): %llu and %llu", t, a, b); break;
        case V_FOREIGN_SRC: std::snprintf(buf, n, "@%lld unit %llu wrote the replayer's queue %llu during a pass", t, a, b); break;
        case V_CREDIT: std::snprintf(buf, n, "@%lld queue %llu pushed past the credits taken for it: %llu entries", t, a, b); break;
        case V_OFFSET_IMM: std::snprintf(buf, n, "@%lld offset_imm set on a %s row", t, alu::name_or_null(unsigned(a)) ? alu::name_or_null(unsigned(a)) : "?"); break;
        case V_LOG_AND_NFV: std::snprintf(buf, n, "@%lld a row with both from_log and next_from_value", t); break;
        case V_UNDEFINED: std::snprintf(buf, n, "@%lld undefined result (invariant 16) in opcode %llu", t, a); break;
        case V_END_WORD: std::snprintf(buf, n, "@%lld the program-end row's log word is %llu, not 0", t, a); break;
        case V_NO_NFV: std::snprintf(buf, n, "@%lld a control transfer from a row without next_from_value (opcode %llu)", t, a); break;
        case V_MISALIGNED: std::snprintf(buf, n, "@%lld a misaligned opcode %llu at 0x%llx", t, a, b); break;
        case F_INVALID: std::snprintf(buf, n, "@%lld invalid expansion: cause %llu, detail 0x%llx", t, a, b); break;
        case F_OVERRUN: std::snprintf(buf, n, "@%lld queue %llu", t, a); break;
        case F_UNDERFLOW: std::snprintf(buf, n, "@%lld underflow of stream %llu, cause %llu", t, a, b); break;
        case F_LEFTOVER: std::snprintf(buf, n, "@%lld input left over: streams 0x%llx", t, a); break;
        default: std::snprintf(buf, n, "@%lld fact %u", t, unsigned(f.code)); break;
    }
}

// ================================================================ inputs, deferred to the end of their cycle
void Replayer::note_input(int64_t t) {
    if (t < last_input_t_) FATAL("replayer %u: time went backwards %lld -> %lld", me, (long long)last_input_t_, (long long)t);
    if (t < settled_) FATAL("replayer %u: an input at %lld after cycle %lld was settled", me, (long long)t, (long long)settled_);
    last_input_t_ = t;
}

void Replayer::accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
    uint32_t addr = offset >> 2;
    // a landing is an input at t + RP_LATENCY: in time while the replayer is settled through that cycle at most
    if (t < last_accept_t_) FATAL("replayer %u: accept's time went backwards %lld -> %lld", me, (long long)last_accept_t_, (long long)t);
    if (t + RP_LATENCY < settled_) FATAL("replayer %u: a landing at %lld after cycle %lld was settled", me, (long long)(t + RP_LATENCY), (long long)settled_);
    last_accept_t_ = t;
    if (accept_n_ > 0 && accept_times_[(accept_n_ - 1) & 7] == t) FATAL("replayer %u: two mode-9 packets in cycle %lld", me, (long long)t);
    accept_times_[accept_n_++ & 7] = t;
    if (land_n_ == RING) FATAL("replayer %u: %u packets landing at once", me, RING);
    landing_[(land_h_ + land_n_++) % RING] = Input{t + RP_LATENCY, unsigned(src), addr, unsigned(size), lo, hi};
}
void Replayer::run(int64_t t) {
    note_input(t);
    if (in_n_ == RING) FATAL("replayer %u: %u inputs pending", me, RING);
    inputs_[(in_h_ + in_n_++) % RING] = Input{t, IN_RUN, 0, 0, 0, 0};
}

void Replayer::reset(int64_t t) {
    in_n_ = 0; land_n_ = 0; settled_ = INT64_MIN;
    stop_core(t, false);
    exp_discard_ = 0; new_exp_set_at_ = INT64_MIN; done_set_at_ = INT64_MIN; ev.clear_all();
    code.clear(); rlog.clear(); wlog.clear(); exp.reset();
    for (uint64_t& c : cond_events_) c = 0;
    outq_n_ = 0;
    run_config_ = 0;
}

unsigned Replayer::landed(int64_t t) const {
    unsigned n = accept_n_ < 8 ? unsigned(accept_n_) : 8;
    for (unsigned i = 0; i < n; i++)
        if (accept_times_[(accept_n_ - 1 - i) & 7] == t - RP_LATENCY) return 1;
    return 0;
}

int Replayer::errors(int64_t t) {
    settle(t);
    uint32_t e = 0;
    for (unsigned k = 0; k < N_ERR; k++) if (cond_events_[k] > 0) e |= 1u << k;
    return int(e);
}
bool Replayer::word(int64_t t, int w, uint32_t& v) {
    settle(t);
    switch (w) {
        case W_RUN_CONFIG: v = s_run_config_; return true;
        case W_ROWS_RUN: v = s_rows_ & RC_ROWS; return true;
        case W_EXP_OFFSET: v = s_status0_ >> 16; return true;
        case W_EXP_OPCODE: v = s_status1_; return true;
        default: return false;
    }
}
int Replayer::write_word(int64_t t, int w, uint32_t v) {
    if (w == W_RUN_CONFIG) {
        note_input(t);
        if (in_n_ == RING) FATAL("replayer %u: %u inputs pending", me, RING);
        inputs_[(in_h_ + in_n_++) % RING] = Input{t, IN_CONFIG, v, 0, 0, 0};
        return 1;
    }
    uint32_t x;
    return word(t, w, x) ? 0 : -1;
}
uint32_t Replayer::consume_events(uint32_t bits) {
    uint32_t took = ev.consume(bits);
    for (unsigned b = EV_CODE; b <= EV_EXP; b++)
        if (took >> b & 1) { unsigned q = b - EV_CODE; allowance_[q] += half(q); }
    return took;
}

// ================================================================ the snapshot at the end of t - 1
void Replayer::settle(int64_t T) {
    if (T <= settled_) return;
    for (;;) {
        int64_t tl = land_n_ ? landing_[land_h_].t : INT64_MAX;
        int64_t ti = in_n_ ? inputs_[in_h_].t : INT64_MAX;
        int64_t tm = tl < ti ? tl : ti;
        if (tm >= T) break;
        if (tl == tm) {
            Input p = landing_[land_h_];
            land_h_ = (land_h_ + 1) % RING; land_n_--;
            land(p.t, p.kind, p.a, p.b, p.lo, p.hi);
            continue;
        }
        Input in = inputs_[in_h_];
        in_h_ = (in_h_ + 1) % RING; in_n_--;
        if (in.kind == IN_RUN) do_run(in.t);
        else if (in.kind == IN_CONFIG) {
            if (running_) FATAL("replayer %u: undefined behavior at %lld: a RUN_CONFIG write while a pass runs (4.19.9; DECISIONS-v1.26 item 13)", me, (long long)in.t);
            run_config_ = in.a & RC_DEFINED; config_writes++;
        } else FATAL("replayer %u: input kind %u", me, in.kind);
    }
    settled_ = T;
    s_running_ = running_;
    s_status0_ = (rows_done_ & 0xFFFF) | (next_offset() & 0xFFFF) << 16;
    s_status1_ = presented_;
    s_rows_ = rows_done_; s_run_config_ = run_config_;
}

unsigned Replayer::next_offset() const {
    if (has_cur_ && !cur_.last_started) return cur_.next_row;
    if (has_nxt_) return nxt_.next_row;
    if (first_instr_) return start_offset_;
    return running_ ? 0 : stopped_offset_;
}

// ================================================================ run
void Replayer::do_run(int64_t t) {
    if (running_) FATAL("replayer %u: undefined behavior at %lld: PULSE_RUN while a pass runs (4.17.2; DECISIONS-v1.25 s8e)", me, (long long)t);
    if (!state_written_ && passes > 0) violate(t, V_NO_FLUSH);
    state_written_ = false;
    uint64_t s_lo = region0[0], s_hi = region0[1];
    pc_ = s_lo; instr_idx_ = uint32_t(s_hi); start_offset_ = unsigned(s_hi >> 32) & 0xFFFF;
    rows_per_pass_ = run_config_ & RC_ROWS; f_store_ = (run_config_ & RC_STORE_LOG) != 0; f_rd_prev_ = (run_config_ & RC_RD_PREV) != 0;
    running_ = true;
    copy_done_at_ = t + 1;   // IMAGE_COPY_CYCLES = 0 (v1.26)
    rows_started_ = 0; rows_done_ = 0; has_cur_ = false; has_nxt_ = false; first_instr_ = true; has_inflight_ = false; busy_until_ = INT64_MIN;
    code_ended_ = false; last_start_at_ = INT64_MIN; exp_discard_ = 0; stopped_offset_ = 0;
    src_of_[0] = src_of_[1] = src_of_[2] = src_of_[3] = -1;
    pass_start_at = t; passes++;
}

void Replayer::flush(int64_t t) {
    code.flush(); rlog.flush(); wlog.flush(); exp.reset();
    latch_sizes();
    for (unsigned b = EV_CODE; b <= EV_EXP; b++) ev.set(t, b, 2);
    for (unsigned q = 0; q < 4; q++) { pushed_[q] = 0; allowance_[q] = 0; }
    state_written_ = true; flushes++;
}
void Replayer::latch_sizes() {
    streamword::Size sz[3];
    streamword::parse(region0[2], region0[3], sz);   // W14's stream_word.h: the layout's one definition
    ReplayerStream* s[3] = {&code, &rlog, &wlog};
    for (unsigned i = 0; i < 3; i++) s[i]->latch(sz[i].words, sz[i].values, sz[i].compressed);
}

void Replayer::stop_core(int64_t t, bool post_done) {
    if (running_) {
        pass_end_at = t; stopped_offset_ = next_offset();
        region0[1] = (region0[1] & ~(uint64_t(0xFFFF) << 32)) | uint64_t(stopped_offset_ & 0xFFFF) << 32;
    }
    running_ = false; has_inflight_ = false; has_cur_ = false; has_nxt_ = false;
    if (post_done) { done_set_at_ = t; ev.post(t, EV_RUN_DONE); }
}

// ================================================================ the write port (4.19.10)
void Replayer::land(int64_t t, unsigned src, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi) {
    unsigned w = addr >> 18;
    if (w >= 5) FATAL("replayer %u: undefined behavior at %lld: a bad write: unmapped address 0x%x", me, (long long)t, addr);
    if (size != 2) FATAL("replayer %u: undefined behavior at %lld: a bad write: size %u at 0x%x: only 128-bit packets", me, (long long)t, 32u << size, addr);
    if (w == 0) {
        if ((addr & 3) != 0) FATAL("replayer %u: undefined behavior at %lld: a 128-bit write to part 0 off a 128-bit boundary: word address 0x%x (item 47)", me, (long long)t, addr);
        unsigned wd = addr >> 2;
        if (wd < PART0_WORDS) {
            if (running_) FATAL("replayer %u: undefined behavior at %lld: a bad write: region 0 written while a pass runs", me, (long long)t);
            region0[2 * wd] = lo; region0[2 * wd + 1] = hi;
            if (wd == 0) flush(t);
            else if (wd == 1) latch_sizes();
        } else if (wd == REC_FILES || wd == REC_BITMAP) {
            if (running_) FATAL("replayer %u: undefined behavior at %lld: a bad write: record word %u written while a pass runs", me, (long long)t, wd);
            ignored_writes++;
        } else if (addr >= 0x400 && addr < 0x800) {
            if (running_) FATAL("replayer %u: undefined behavior at %lld: a bad write: decode table written while a pass runs", me, (long long)t);
            unsigned e0 = (wd - 0x100) * 8;
            for (unsigned k = 0; k < 8; k++) decode_table[e0 + k] = uint16_t((k < 4 ? lo >> (16 * k) : hi >> (16 * (k - 4))) & 0xFFFF);
        } else if (addr >= 0x800 && addr < 0x1000) {
            unsigned j = wd - 0x200; rom_lo[j] = lo; rom_hi[j] = hi;   // the ROM's second port: written even while running
        } else FATAL("replayer %u: undefined behavior at %lld: a bad write: unmapped address 0x%x", me, (long long)t, addr);
        return;
    }
    unsigned qi = w - 1;
    if (running_) {
        if (src_of_[qi] >= 0 && src_of_[qi] != int(src)) violate(t, V_TWO_SOURCES, uint64_t(src_of_[qi]), src);
        src_of_[qi] = int(src);
        if (src != me) violate(t, V_FOREIGN_SRC, src, qi);
        if (++pushed_[qi] > allowance_[qi]) violate(t, V_CREDIT, qi, pushed_[qi]);
    }
    bool ok = qi == 3 ? exp.push(t, lo, hi) : stream(qi).push(t, lo, hi);
    if (!ok) { cond_events_[ERR_OVERRUN]++; record(t, F_OVERRUN, qi, 0); }
}

// ================================================================ the cycle
void Replayer::step(int64_t T) {
    settle(T);
    drain(T);
    code.cycle = rlog.cycle = wlog.cycle = exp.cycle = T;
    if (!running_) return;
    pass_cycles++;
    if (rows_per_pass_ == 0) { check_leftover(T); stop_core(T, true); return; }
    if (has_inflight_ && T == busy_until_) finish_row(T);
    if (!running_) return;
    if (exp_discard_ > 0 && exp.occ > 0) { exp_pop(); exp_discard_--; }
    front_end(T);
    if (!running_) return;
    if (T > busy_until_) {
        if (T < copy_done_at_) { stalls[ST_IMAGE]++; return; }
        if (rows_started_ < rows_per_pass_) start_row(T);
    }
}

void Replayer::drain(int64_t T) {
    if (outq_n_ == 0 || outq_at_[outq_h_] > T || T - last_sink_ < int64_t(sink_interval)) return;
    const uint64_t* r = outq_[outq_h_];
    outq_h_ = (outq_h_ + 1) % OUTQ_ROWS; outq_n_--;
    last_sink_ = T;
    for (unsigned i = 0; i < REC_LONGS; i++) row_hash_ = mix(row_hash_ ^ r[i]);
    if (row_buf_) {                                       // W20: from row_base_ (0 unless the harness drains it)
        if (rows_sunk_ - row_base_ >= row_cap_) FATAL("replayer %u: the row buffer is full: %llu rows", me, (unsigned long long)row_cap_);
        for (unsigned i = 0; i < REC_LONGS; i++) row_buf_[(rows_sunk_ - row_base_) * REC_LONGS + i] = r[i];
    }
    rows_sunk_++;
}

void Replayer::exp_pop() {
    exp.head = (exp.head + 1) % EXP_CAP; exp.occ--;
    if (exp.pop_cycle != exp.cycle) { exp.pop_cycle = exp.cycle; exp.pops = 0; }
    exp.pops++;
    if (++exp.popped_since == EXP_HALF) { exp.popped_since = 0; ev.post(exp.cycle, EV_EXP); }
}
void Replayer::exp_new_expansion() {
    exp.pend = 0; exp.newest_valid = false; exp.popped_since = 0;
    ev.set(exp.cycle, EV_EXP, 2);
    pushed_[3] = 0; allowance_[3] = 0;
}

// ---------------------------------------------------------------- the front end
void Replayer::front_end(int64_t T) {
    if (has_nxt_ || (has_cur_ && !cur_.last_started)) return;
    if (rows_started_ >= rows_per_pass_ || code_ended_) return;
    if (has_cur_ && last_start_at_ == T) return;
    int st = code.peek();
    if (st == ReplayerStream::WAIT) return;
    if (st == ReplayerStream::END) { code_ended_ = true; return; }
    if (st == ReplayerStream::OVER) { underflow(T, 0, UF_CODE_OVER_DECODE); return; }
    uint32_t w = uint32_t(code.peek_value);
    code.commit();
    Instr in;
    in.id = next_instr_id_++;
    in.w = w; in.decoded_at = T; in.code_end = code.consumed;
    in.next_row = in.first_row = first_instr_ ? start_offset_ : 0;
    first_instr_ = false;
    if ((w & 3) == 0) in.kind = rowcore::K_PAD;
    else {
        rowcore::decode(w, decode_table, rom_lo, dec_);
        in.kind = dec_.kind; in.start = dec_.start; in.rd0 = dec_.rd0; in.offset = dec_.offset;
        if (in.kind == rowcore::K_NOP) { in.next_row = in.first_row = 0; }
        else if (in.kind == rowcore::K_CTRL) {
            in.rd0 = 0; presented_ = w; new_exp_set_at_ = T; exp_new_expansion(); ev.post(T, EV_NEWEXP); new_expansions++;
        }
    }
    nxt_ = in; has_nxt_ = true;
}

// ---------------------------------------------------------------- starting a row
void Replayer::start_row(int64_t T) {
    using namespace rowcore;
    if (!has_cur_ || cur_.last_started) {
        if (!has_nxt_ && code_ended_) { underflow(T, 0, UF_CODE_PAST_VALUES); return; }
        if (!has_nxt_ || nxt_.decoded_at >= T) { stalls[ST_CODE]++; return; }
        cur_ = nxt_; has_cur_ = true; has_nxt_ = false;
    }
    Instr& in = cur_;
    if (in.kind == K_BAD) { invalid(T, INV_CLASS3, in.w); return; }
    uint64_t lo, hi;
    unsigned row_idx = in.next_row;
    switch (in.kind) {
        case K_PAD: lo = rom_lo[0]; hi = rom_hi[0]; break;
        case K_NOP: lo = rom_lo[1]; hi = rom_hi[1]; break;
        case K_ROM: {
            unsigned a = in.start + row_idx;
            if (a > 511) { invalid(T, INV_ROM_END, uint64_t(in.start) << 16 | row_idx); return; }
            lo = rom_lo[a]; hi = rom_hi[a];
            break;
        }
        default:
            if (exp_discard_ > 0) { stalls[ST_EXP]++; return; }
            if (exp.occ == 0) { stalls[row_idx == in.first_row ? ST_NEWEXP : ST_EXP]++; return; }
            lo = exp.head_lo(); hi = exp.head_hi();
    }
    const unsigned op = rowcore::op(lo), c_rd = rowcore::c_rd(lo), c_rs1 = rowcore::c_rs1(lo), c_rs2 = rowcore::c_rs2(lo);
    const unsigned seq_rem = seq_remaining(lo);
    const bool nfv = next_from_value(lo), hld = hold(lo), vrt = virt(lo);
    const bool i0p0 = imm0pc0(lo), rep_c = report_c(lo), flog = from_log(lo);
    const bool s_imm = sign_imm(lo), o_imm = offset_imm(lo);
    const unsigned fmt = format(lo), rd0w = rowcore::rd0(lo);
    if (reserved(lo) != 0)
        FATAL("replayer %u: undefined behavior at %lld: an expansion word with reserved bits [61:55] = 0x%x (%s): lo 0x%016llx", me,
              (long long)T, reserved(lo), in.kind == K_CTRL ? "controller-fed" : "ROM", (unsigned long long)lo);
    if (unsigned why = invalid_word(lo)) { invalid(T, INV_WORD, why); return; }
    // N3: the rd = x0 policy applies to every word; for a controller-fed expansion it is read from the first word delivered
    if (in.kind == K_CTRL && !in.ctrl_first_seen) {
        in.ctrl_first_seen = true;
        unsigned rd_field = (in.w >> 7) & 31;
        in.offset = rd_field == 0 && rd0w == 2 ? 1 : 0;
        if (rd_field == 0 && (rd0w == 1 || rd0w == 3) && row_idx == 0) {
            exp_pop();
            exp_discard_ = seq_rem;
            in.kind = K_NOP; in.rd0 = rd0w;
            start_row(T);
            return;
        }
    }
    // operands
    const uint32_t w = in.w;
    const uint64_t src_imm = needs_source_imm(lo) ? source_imm(fmt, w) : 0;
    const uint64_t so = o_imm ? src_imm : 0;
    if (o_imm && !alu::is_shift_data(op)) violate(T, V_OFFSET_IMM, op);
    const int r_rd = rowcore::reg(c_rd, w, in.offset), r_rs1 = rowcore::reg(c_rs1, w, in.offset), r_rs2 = rowcore::reg(c_rs2, w, in.offset);
    const uint64_t v_rd = r_rd >= 0 ? reg(unsigned(r_rd)) : imm_val(c_rd, hi, src_imm);
    const uint64_t v_rs1 = r_rs1 >= 0 ? reg(unsigned(r_rs1)) : imm_val(c_rs1, hi, src_imm);
    const uint64_t v_rs2 = r_rs2 >= 0 ? reg(unsigned(r_rs2)) : imm_val(c_rs2, hi, src_imm);
    const uint64_t imm_sum = (r_rd < 0 ? v_rd : 0) + (r_rs1 < 0 ? v_rs1 : 0) + (r_rs2 < 0 ? v_rs2 : 0);
    // log readiness
    ReplayerStream* log_s = nullptr;
    if (flog) log_s = alu::is_store(op) ? (f_store_ ? &wlog : nullptr) : &rlog;
    if (log_s) {
        int s = log_s->peek();
        if (s == ReplayerStream::WAIT) { stalls[log_s == &wlog ? ST_WLOG : ST_RLOG]++; return; }
        if (s == ReplayerStream::END) { underflow(T, log_s->id, UF_LOG_END); return; }
        if (s == ReplayerStream::OVER) { underflow(T, log_s->id, UF_LOG_OVER); return; }
    }
    if (nfv) {
        if (log_s == &rlog) violate(T, V_LOG_AND_NFV);
        else {
            int s = rlog.peek();
            if (s == ReplayerStream::WAIT) { stalls[ST_RLOG]++; return; }
            if (s == ReplayerStream::OVER) { underflow(T, 1, UF_NFV_OVER); return; }
        }
    }
    if (outq_n_ >= OUTQ_ROWS) { stalls[ST_OUTQ]++; return; }

    // ---- the row starts
    if (in.kind == K_CTRL) exp_pop();
    uint64_t log_word = 0;
    if (log_s) {
        log_word = log_s->peek_value; log_s->commit(); log_s->release_consumed(ev);
        if (alu::is_load(op)) wlog.last_load(log_word);
    }
    const bool last = seq_rem == 0 || in.kind == K_PAD || in.kind == K_NOP;
    const bool compressed = (w & 1) == 0;
    const unsigned pc_inc = !last ? 0 : compressed ? 2 : 4;
    alu::Out o;
    alu::exec(op, v_rs1, v_rs2, v_rd, pc_, pc_inc, i0p0, log_word, so, o);
    if ((op == alu::PEXT || op == alu::PEXT_S) && v_rs2 != 0) {
        uint64_t m = v_rs2 >> alu::tz(v_rs2);
        if ((m & (m + 1)) != 0) pext_non_contiguous++;
    }
    if (o.undefined) violate(T, V_UNDEFINED, op);
    Pending p;
    p.code_end = in.code_end; p.last_of_instr = last; p.instr_id = in.id;
    const bool writes = r_rd >= 0 && op != alu::NOP;
    if (writes && r_rd != 0) { p.rd_reg = r_rd; p.rd_val = o.rd; }
    const uint64_t rd_after = r_rd < 0 ? 0 : r_rd == 0 ? 0 : writes ? o.rd : v_rd;
    // pc and instr_idx (4.19.7, appendix 1.4)
    if (hld) { p.hold = true; p.new_pc = pc_; p.new_idx = instr_idx_; }
    else {
        uint64_t np = o.new_pc;
        if (np == 0) {
            p.new_pc = 0; p.new_idx = 0;
            if (nfv) { uint64_t v = pop_transfer(T); if (v != 0 && running_) violate(T, V_END_WORD, v); }
            if (!running_) return;
        } else if (np != pc_) {
            if (!nfv) violate(T, V_NO_NFV, op);
            p.new_pc = np; p.new_idx = uint32_t(pop_transfer(T));
            if (!running_) return;
        } else { p.new_pc = pc_ + pc_inc; p.new_idx = instr_idx_ + 1; }
    }
    // the record (4.19.12)
    uint64_t* r = p.rec;
    if (in.kind != K_PAD) {
        const bool is_virtual = in.kind == K_NOP ? in.rd0 == 1 : (vrt || seq_rem > 0 || row_idx > 0);
        const bool is_first = is_virtual && (in.kind == K_NOP || row_idx == 0);
        const bool is_comp = compressed && rep_c;
        auto nz = [](int x) { return uint64_t(x < 0 ? 0 : x); };
        r[0] = uint64_t(instr_idx_) | uint64_t(op) << 32 | nz(r_rd) << 40 | nz(r_rs1) << 48 | nz(r_rs2) << 56;
        r[1] = (seq_rem & 0xFFFFull) | uint64_t(is_comp) << 16 | uint64_t(is_virtual) << 17 | uint64_t(is_first) << 18
               | uint64_t(r_rd < 0) << 19 | uint64_t(r_rs1 < 0) << 20 | uint64_t(r_rs2 < 0) << 21 | uint64_t(s_imm) << REC_SIGN_IMM;
        r[2] = pc_; r[3] = alu::is_shift_data(op) ? so : imm_sum;
        r[4] = r_rs1 >= 0 ? v_rs1 : 0; r[5] = r_rs2 >= 0 ? v_rs2 : 0;
        r[6] = r_rd >= 0 && f_rd_prev_ ? v_rd : 0; r[7] = rd_after;
        if (alu::is_load(op)) { r[8] = alu::mem_addr(op, v_rs1, imm_sum); r[9] = rd_after; }
        else if (alu::is_store(op)) { r[8] = alu::mem_addr(op, v_rs1, imm_sum); r[9] = log_s ? log_word : 0; }
        if ((alu::is_load(op) || alu::is_store(op)) && ((v_rs1 + imm_sum) & 7 & ~alu::floor_mask(op)) != 0)
            violate(T, V_MISALIGNED, op, v_rs1 + imm_sum);
    }
    in.next_row++;
    if (last) { in.last_started = true; last_start_at_ = T; }
    rows_started_++; rows_total++;
    if (alu::cls(op) == 'M') rows_m++;
    if (in.kind == K_CTRL) rows_ctrl++;
    inflight_ = p; has_inflight_ = true;
    busy_until_ = T + alu::cycles(op) - 1;
}

uint64_t Replayer::pop_transfer(int64_t T) {
    int s = rlog.peek();
    if (s != ReplayerStream::AVAIL) { underflow(T, 1, UF_TRANSFER); return 0; }
    uint64_t v = rlog.peek_value; rlog.commit(); rlog.release_consumed(ev);
    return v;
}

// ---------------------------------------------------------------- finishing a row
void Replayer::finish_row(int64_t T) {
    has_inflight_ = false;
    const Pending& p = inflight_;
    if (p.rd_reg > 0) region0[4 + p.rd_reg] = p.rd_val;
    pc_ = p.new_pc; instr_idx_ = p.new_idx;
    region0[0] = pc_; region0[1] = (region0[1] & ~uint64_t(0xFFFFFFFF)) | uint64_t(instr_idx_);
    if (outq_n_ == OUTQ_ROWS) FATAL("replayer %u: the output queue overflowed at %lld", me, (long long)T);
    unsigned slot = (outq_h_ + outq_n_++) % OUTQ_ROWS;
    for (unsigned i = 0; i < REC_LONGS; i++) outq_[slot][i] = p.rec[i];
    outq_at_[slot] = T + 1;
    if (p.last_of_instr) code.release_to(p.code_end, ev);
    rows_done_++;
    if (rows_done_ == rows_per_pass_) { check_leftover(T); stop_core(T, true); }
}

void Replayer::check_leftover(int64_t T) {
    uint64_t streams = 0, first_cause = 0;
    for (unsigned s = 0; s < (f_store_ ? 3u : 2u); s++) {
        uint64_t n;
        unsigned c = stream(s).leftover(n);
        if (c) { if (!streams) first_cause = c; streams |= 1u << s; }
    }
    if (streams) { cond_events_[ERR_LEFTOVER]++; record(T, F_LEFTOVER, streams, first_cause); }
}

void Replayer::underflow(int64_t T, unsigned s, uint64_t cause) {
    cond_events_[s == 0 ? ERR_CODE_UNDER : s == 1 ? ERR_RLOG_UNDER : ERR_WLOG_UNDER]++;
    record(T, F_UNDERFLOW, s, cause);
    stop_core(T, true);
}

void Replayer::invalid(int64_t T, uint64_t cause, uint64_t detail) {
    cond_events_[ERR_INVALID]++;
    record(T, F_INVALID, cause, detail);
    stop_core(T, true);
}

}  // namespace units
