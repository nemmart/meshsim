// replayer.cpp: the replayer's cycle, as the RTL's replayer_unit runs it (replayer.h).
#include "units/replayer.h"

#include "check.h"

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

Replayer::Replayer(unsigned m, unsigned log_entries, unsigned list_capacity) : me(m) {
    list_cap_ = list_capacity;
    list_text_.reset(new char[size_t(N_LISTS) * list_cap_ * LIST_TEXT + 1]());
    if (log_entries != LOG_QUEUE_ENTRIES) FATAL("replayer %u: log queues of %u entries: every input queue is REPLAY_QUEUE_ENTRIES = %u (19)", m, log_entries, LOG_QUEUE_ENTRIES);
    const uint8_t depth[] = {1, 0, 2, 2, 2, 2, 1};
    const bool flags[] = {false, false, true, true, true, true, false};
    ev.init(7, depth, flags);
    code_d.init(32, false); rlog_d.init(64, false); wlog_d.init(64, true);
    for (unsigned b = 0; b < 8; b++) { ev_fill_at_[b] = INT64_MIN; ev_full_at_[b] = INT64_MIN; }
}

// ================================================================ facts (D24)
void Replayer::record(int64_t t, uint16_t c, uint64_t a, uint64_t b, uint64_t x) {
    list_add(t, c, a, b, x);
    ReplayerFact& f = facts_[fact_n_ % FACT_RING];
    f.t = t; f.code = c; f.a = a; f.b = b;
    fact_n_++;
    if (fact_shown_ < FACT_RING) fact_shown_++;
    fact_counts_[c]++;
}
void Replayer::violate(int64_t t, uint16_t c, uint64_t a, uint64_t b, uint64_t x) {
    record(t, c, a, b, x);
    // reported to the checker at t + 1 (the slot passes it to the block, whose counter the
    // checker sees at the end of the next cycle: v1.3 reports 23808 for a violation at 23807)
    sim::check::record(uint64_t(t) + 1, int(me), sim::check::Kind::PERIPH_VIOLATION, c, a, b, x);
}

void Replayer::fact_text(const ReplayerFact& f, char* buf, size_t n) {
    const unsigned long long a = f.a, b = f.b;
    const long long t = f.t;
    switch (f.code) {
        case V_NO_FLUSH: std::snprintf(buf, n, "@%lld START with no state write since the reset or the previous pass's START", t); break;
        case V_EXP_LEFT: std::snprintf(buf, n, "@%lld %llu words left in the expansion queue at a new expansion", t, a); break;
        case V_CODE_MISS64: std::snprintf(buf, n, "@%lld a code miss with size bit 1", t); break;
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

// ================================================================ the error lists
// Each entry is a text (a violation, an invalid expansion, an underflow, a leftover, an overrun), formatted when
// recorded into the buffer allocated at construction (D24: snprintf, no allocation); beyond the kept entries only the
// count grows. Error paths only: nothing here runs in a clean pass.
namespace {
const char* op_name(uint64_t op, char* tmp, size_t n) {   // ReplayerAlu.name: the name, or "op" + op
    const char* nm = op < 256 ? alu::name_or_null(unsigned(op)) : nullptr;
    if (nm) return nm;
    std::snprintf(tmp, n, "op%lld", (long long)int32_t(uint32_t(op)));
    return tmp;
}
const char* stream_name(unsigned s) { return s == 0 ? "code" : s == 1 ? "read log" : "write log"; }
}  // namespace

// The text of a violation ("t<me> @<t>: <m>"), for the replayer's list and the
// checker (the slot passes it to the tile's register block: a chip violation). Codes below N_VIOLATION only.
void Replayer::violation_text(unsigned me, int64_t t, uint16_t c, uint64_t a, uint64_t b, uint64_t x, char* o, size_t n) {
    const long long T = t;
    char nb[24];
    (void)x;
    switch (c) {
        case V_NO_FLUSH: std::snprintf(o, n, "t%u @%lld: START with no state write since the reset or the previous pass's START: its queues weren't emptied (17.8.10)", me, T); break;
        case V_EXP_LEFT: std::snprintf(o, n, "t%u @%lld: %d words left in the expansion queue at a new expansion (17.8.8)", me, T, int(a)); break;
        case V_CODE_MISS64: std::snprintf(o, n, "t%u @%lld: a code miss with size bit 1 (17.8.12)", me, T); break;
        case V_TWO_SOURCES: std::snprintf(o, n, "t%u @%lld: two sources feed one queue port in a pass (invariant 18): %d and %d", me, T, int(a), int(b)); break;
        case V_FOREIGN_SRC: std::snprintf(o, n, "t%u @%lld: unit %d wrote the replayer's queue %d during a pass: only the tile itself refills it (17.8.13; invariant 16)", me, T, int(a), int(b)); break;
        case V_CREDIT: std::snprintf(o, n, "t%u @%lld: queue %d pushed past the credits taken for it: %d entries, %d credits (17.8.8; invariant 16)", me, T, int(a), int(b), int(x)); break;
        case V_OFFSET_IMM: std::snprintf(o, n, "t%u @%lld: offset_imm set on a %s row (only ShiftData reads shift_offset)", me, T, op_name(a, nb, sizeof nb)); break;
        case V_LOG_AND_NFV: std::snprintf(o, n, "t%u @%lld: a row with both from_log and next_from_value (two read-log words)", me, T); break;
        case V_UNDEFINED: std::snprintf(o, n, "t%u @%lld: undefined result (invariant 16): %s in %s", me, T, reinterpret_cast<const char*>(uintptr_t(b)), op_name(a, nb, sizeof nb)); break;
        case V_END_WORD: std::snprintf(o, n, "t%u @%lld: the program-end row's log word is %lld, not 0", me, T, (long long)a); break;
        case V_NO_NFV: std::snprintf(o, n, "t%u @%lld: a row without next_from_value whose new pc differs from its pc (%s): sequential", me, T, op_name(a, nb, sizeof nb)); break;
        case V_MISALIGNED: std::snprintf(o, n, "t%u @%lld: a misaligned %s at 0x%llx (jolt-cpp aborts the trace; checked_memory_address)", me, T, op_name(a, nb, sizeof nb), (unsigned long long)b); break;
        default: std::snprintf(o, n, "t%u @%lld: violation %u", me, T, unsigned(c)); break;
    }
}

void Replayer::list_add(int64_t t, uint16_t c, uint64_t a, uint64_t b, uint64_t x) {
    unsigned l = c < N_VIOLATION ? L_VIOLATIONS : c == F_INVALID ? L_INVALID : c == F_OVERRUN ? L_OVERRUNS
               : c == F_UNDERFLOW ? L_UNDERFLOWS : c == F_LEFTOVER ? L_LEFTOVER : N_LISTS;
    if (l == N_LISTS) return;
    uint64_t k = list_n_[l]++;
    if (k >= list_cap_) return;
    char* o = &list_text_[(size_t(l) * list_cap_ + k) * LIST_TEXT];
    const size_t n = LIST_TEXT;
    const long long T = t;
    char nb[24];
    switch (c) {
        case V_NO_FLUSH: case V_TWO_SOURCES: case V_FOREIGN_SRC: case V_CREDIT: case V_OFFSET_IMM: case V_LOG_AND_NFV:
        case V_UNDEFINED: case V_END_WORD: case V_NO_NFV: case V_MISALIGNED: case V_EXP_LEFT: case V_CODE_MISS64: violation_text(me, t, c, a, b, x, o, n); break;
        case F_OVERRUN: std::snprintf(o, n, "@%lld queue %d", T, int(a)); break;
        case F_INVALID:
            if (a == INV_CLASS3) std::snprintf(o, n, "@%lld decode-table class 3 for instruction 0x%x", T, unsigned(uint32_t(b)));
            else if (a == INV_ROM_END) std::snprintf(o, n, "@%lld a template runs past ROM word 511 (start %d, row %d)", T, int(b >> 16), int(b & 0xFFFF));
            else if (b == rowcore::INV_OPCODE) std::snprintf(o, n, "@%lld opcode %d (%s) is not executed by the replayer", T, int(rowcore::op(x)), op_name(rowcore::op(x), nb, sizeof nb));
            else if (b == rowcore::INV_OPERAND) std::snprintf(o, n, "@%lld a reserved operand code (146-255)", T);
            else std::snprintf(o, n, "@%lld format %d", T, int(rowcore::format(x)));
            break;
        case F_UNDERFLOW: {
            const streamword::Size& st = sz_[a < 3 ? a : 0];
            switch (b) {
                case UF_CODE_OVER_DECODE: std::snprintf(o, n, "@%lld the code stream's next value runs past its %u words", T, sz_[0].words); break;
                case UF_CODE_PAST_VALUES: std::snprintf(o, n, "@%lld an instruction past the code stream's %u values", T, sz_[0].values); break;
                case UF_LOG_END: std::snprintf(o, n, "@%lld a pop past the %s's %u values", T, stream_name(unsigned(a)), st.values); break;
                case UF_LOG_OVER: std::snprintf(o, n, "@%lld the %s's next value runs past its %u words", T, stream_name(unsigned(a)), st.words); break;
                case UF_TRANSFER_END: std::snprintf(o, n, "@%lld a pop past the read log's %u values (a control transfer's next instr_idx)", T, sz_[1].values); break;
                default: std::snprintf(o, n, "@%lld a pop past the read log's %u words (a control transfer's next instr_idx)", T, sz_[1].words); break;
            }
            break;
        }
        case F_LEFTOVER: {   // checkLeftover: each stream's leftover(), joined with "; "
            size_t at = size_t(std::snprintf(o, n, "@%lld ", T));
            bool first = true;
            for (unsigned si = 0; si < 3 && at < n; si++) {
                if (!(a >> si & 1)) continue;
                const streamword::Size& st = sz_[si];
                uint64_t cnt;
                unsigned kind = leftover_of(si, cnt);
                const char* sep = first ? "" : "; ";
                first = false;
                if (kind == 1) at += size_t(std::snprintf(o + at, n - at, "%s%s: %llu of %u values not popped", sep, stream_name(si), (unsigned long long)cnt, st.values));
                else if (kind == 2) at += size_t(std::snprintf(o + at, n - at, "%s%s: %llu of %u words not taken", sep, stream_name(si), (unsigned long long)cnt, st.words));
                else at += size_t(std::snprintf(o + at, n - at, "%s%s: %llu entries pushed past its %u words", sep, stream_name(si), (unsigned long long)cnt, st.words));
            }
            break;
        }
        default: std::snprintf(o, n, "@%lld fact %u", T, unsigned(c)); break;
    }
}

// ================================================================ inputs, deferred to the end of their cycle
void Replayer::note_input(int64_t t) {
    if (t < last_input_t_) FATAL("replayer %u: time went backwards %lld -> %lld", me, (long long)last_input_t_, (long long)t);
    if (t < settled_) FATAL("replayer %u: an input at %lld after cycle %lld was settled", me, (long long)t, (long long)settled_);
    last_input_t_ = t;
}

void Replayer::write(int64_t t, int src, uint32_t offset, uint64_t lo, uint64_t hi) { present(t, src, offset, 2, lo, hi); }
void Replayer::present(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
    if (t < last_accept_t_) FATAL("replayer %u: the write port's time went backwards %lld -> %lld", me, (long long)last_accept_t_, (long long)t);
    if (t < settled_) FATAL("replayer %u: a write at %lld after cycle %lld was settled", me, (long long)t, (long long)settled_);
    if (accept_n_ > 0 && accept_times_[(accept_n_ - 1) & 7] == t) FATAL("replayer %u: two writes in cycle %lld (the port takes one a cycle, 17.8.9)", me, (long long)t);
    last_accept_t_ = t;
    accept_times_[accept_n_++ & 7] = t;
    if (land_n_ == RING) FATAL("replayer %u: %u writes pending", me, RING);
    landing_[(land_h_ + land_n_++) % RING] = Input{t, unsigned(src), offset, uint32_t(size), lo, hi};
}
void Replayer::accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
    present(t + RP_LATENCY, src, offset, size, lo, hi);
}
void Replayer::run(int64_t t) {
    note_input(t);
    if (in_n_ == RING) FATAL("replayer %u: %u inputs pending", me, RING);
    inputs_[(in_h_ + in_n_++) % RING] = Input{t, IN_RUN, 0, 0, 0, 0};
}

// The reset (17.8.14): ends any pass, empties the five queues, zeroes the events, ERRORS and block 2's words. The state
// word, the stream word, the register file, the decode table and the ROM keep their contents.
void Replayer::reset(int64_t t) {
    in_n_ = 0; land_n_ = 0; settled_ = INT64_MIN;
    if (busy_) pass_end_at = t;
    busy_ = false; started_ = false; check_ = false;
    run_config_ = 0; rows_left_ = 0; rows_run_ = 0; posted_offset_ = 0; posted_opcode_ = 0;
    state_written_ = false;
    ev.clear_all();
    for (unsigned b = 0; b < 8; b++) { ev_fill_at_[b] = INT64_MIN; ev_full_at_[b] = INT64_MIN; }
    code.clear(); rlog.clear(); wlog.clear(); exp.clear();
    code_d.clear(); rlog_d.clear(); wlog_d.clear();
    f_first_ = true; f_entry_pending_ = false; f_active_ = false; f_rom_pending_word_ = false; f_rom_pending_policy_ = false;
    h_valid_ = false;
    e_reads_made_ = false; e_in_flight_ = false; e_in_cycle_2_ = false; e_done_seen_ = false;
    wb_pending_ = false; wb_landed_at_ = INT64_MIN; a_done_at_ = INT64_MIN;
    for (uint64_t& c : cond_events_) c = 0;
    outq_n_ = 0; outq_h_ = 0;
    quiet_ = QUIET;
}

unsigned Replayer::landed(int64_t t) const {
    unsigned n = accept_n_ < 8 ? unsigned(accept_n_) : 8;
    for (unsigned i = 0; i < n; i++)
        if (accept_times_[(accept_n_ - 1 - i) & 7] == t) return 1;
    return 0;
}

int Replayer::errors(int64_t t) {
    settle(t);
    return int(s_errors_);
}
bool Replayer::word(int64_t t, int w, uint32_t& v) {
    settle(t);
    switch (w) {
        case W_RUN_CONFIG: v = s_run_config_; return true;
        case W_ROWS_RUN: v = s_rows_ & RC_ROWS; return true;
        case W_EXP_OFFSET: v = s_offset_; return true;
        case W_EXP_OPCODE: v = s_opcode_; return true;
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

// A set of an event in cycle T: one more, at most its depth (peripheral_control.veryl).
void Replayer::post(int64_t T, unsigned bit) {
    if (ev.count_of(bit) >= ev.depth[bit]) ev_full_at_[bit] = T;
    ev.post(T, bit);
}
uint32_t Replayer::consume_events(uint32_t bits) {
    uint32_t took = 0;
    for (unsigned b = 0; b < ev.n; b++) {
        if (!(bits >> b & 1) || ev.depth[b] == 0 || ev.count_of(b) == 0) continue;
        // the load takes one, then the cycle's set adds one, up to the depth; a fill sets the depth whatever they do
        if (ev_fill_at_[b] != worked_ && ev_full_at_[b] != worked_) ev.consume(1u << b);
        took |= 1u << b;
    }
    for (unsigned b = EV_CODE; b <= EV_EXP; b++)
        if (took >> b & 1) allowance_[b - EV_CODE] += ReplayerQueue::HALF;
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
        quiet_ = 0;
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
            if (busy_) FATAL("replayer %u: undefined behavior at %lld: a RUN_CONFIG store while a pass runs (17.8.1)", me, (long long)in.t);
            run_config_ = in.a & RC_DEFINED; config_writes++;
        } else FATAL("replayer %u: input kind %u", me, in.kind);
    }
    settled_ = T;
    s_running_ = busy_;
    s_rows_ = rows_run_; s_run_config_ = run_config_; s_offset_ = posted_offset_; s_opcode_ = posted_opcode_;
    s_errors_ = 0;
    for (unsigned k = 0; k < N_ERR; k++) if (cond_events_[k] > 0) s_errors_ |= 1u << k;
}

// ================================================================ START (17.8.10)
void Replayer::do_run(int64_t t) {
    if (busy_) FATAL("replayer %u: undefined behavior at %lld: START while a pass runs (17.2)", me, (long long)t);
    if (!state_written_) violate(t, V_NO_FLUSH);
    state_written_ = false;
    busy_ = true; started_ = true;
    rows_left_ = run_config_ & RC_ROWS; rows_run_ = 0;
    src_of_[0] = src_of_[1] = src_of_[2] = src_of_[3] = -1;
    pass_start_at = t; passes++;
}

// The state write as it lands (17.8.8): the queues emptied, the credits filled, the decoders cleared, the fetch and the
// row engine emptied, pc, instr_idx and the row offset loaded.
void Replayer::state_write(int64_t t, uint64_t lo, uint64_t hi) {
    region0[0] = lo; region0[1] = hi;
    pc_ = lo; instr_idx_ = uint32_t(hi); row_offset_ = unsigned(hi >> 32) & 0xFFFF;
    code.clear(); rlog.clear(); wlog.clear(); exp.clear();
    code_d.clear(); rlog_d.clear(); wlog_d.clear();
    f_first_ = true; f_entry_pending_ = false; f_active_ = false; f_rom_pending_word_ = false; f_rom_pending_policy_ = false;
    h_valid_ = false;
    e_reads_made_ = false; e_in_flight_ = false; e_in_cycle_2_ = false; e_done_seen_ = false;
    for (unsigned b = EV_CODE; b <= EV_EXP; b++) { ev.set(t, b, 2); ev_fill_at_[b] = t; }
    for (unsigned q = 0; q < 4; q++) { pushed_[q] = 0; allowance_[q] = 0; }
    state_written_ = true; flushes++;
}

// ================================================================ the write port (17.8.9)
void Replayer::land(int64_t t, unsigned src, uint32_t offset, unsigned size, uint64_t lo, uint64_t hi) {
    offset &= 0xFFFFFF;
    const unsigned part = offset >> 20;
    if (part >= 5) FATAL("replayer %u: undefined behavior at %lld: a bad write: unmapped offset 0x%x", me, (long long)t, offset);
    if (size != 2) FATAL("replayer %u: undefined behavior at %lld: a bad write: size %u at offset 0x%x: only 128-bit packets", me, (long long)t, 32u << size, offset);
    if (part == 0) {
        if ((offset & 0xF) != 0) FATAL("replayer %u: undefined behavior at %lld: a 128-bit write to part 0 off a 128-bit boundary: offset 0x%x", me, (long long)t, offset);
        const unsigned wd = offset >> 4;
        if (wd < PART0_WORDS) {
            if (busy_) FATAL("replayer %u: undefined behavior at %lld: a bad write: the record written while a pass runs", me, (long long)t);
            if (wd == 0) state_write(t, lo, hi);
            else if (wd == 1) { region0[2] = lo; region0[3] = hi; streamword::parse(lo, hi, sz_); }
            else {
                // an image packet wins the register file's ports, and drops a write-back not yet landed
                if (wb_pending_ || wb_landed_at_ == t) { regs_[wb_reg_] = wb_old_; wb_pending_ = false; wb_landed_at_ = INT64_MIN; }
                regs_[2 * (wd - 2)] = lo; regs_[2 * (wd - 2) + 1] = hi;
            }
        } else if (wd == REC_FILES || wd == REC_BITMAP) {
            if (busy_) FATAL("replayer %u: undefined behavior at %lld: a bad write: record word %u written while a pass runs", me, (long long)t, wd);
            ignored_writes++;
        } else if (offset >= 0x1000 && offset < 0x2000) {
            if (busy_) FATAL("replayer %u: undefined behavior at %lld: a bad write: the decode table written while a pass runs", me, (long long)t);
            const unsigned e0 = (wd - 0x100) * 8;
            for (unsigned k = 0; k < 8; k++) decode_table[e0 + k] = uint16_t((k < 4 ? lo >> (16 * k) : hi >> (16 * (k - 4))) & 0xFFFF);
        } else if (offset >= 0x2000 && offset < 0x4000) {
            const unsigned j = wd - 0x200; rom_lo[j] = lo; rom_hi[j] = hi;   // the ROM's write port: during a pass too
        } else FATAL("replayer %u: undefined behavior at %lld: a bad write: unmapped offset 0x%x", me, (long long)t, offset);
        return;
    }
    const unsigned qi = part - 1;
    if (busy_) {
        if (src_of_[qi] >= 0 && src_of_[qi] != int(src)) violate(t, V_TWO_SOURCES, uint64_t(src_of_[qi]), src);
        src_of_[qi] = int(src);
        if (src != me) violate(t, V_FOREIGN_SRC, src, qi);
        if (++pushed_[qi] > allowance_[qi]) violate(t, V_CREDIT, qi, pushed_[qi], allowance_[qi] / ReplayerQueue::HALF);
    }
    if (!queue(qi).push(t, lo, hi)) { cond_events_[ERR_OVERRUN]++; record(t, F_OVERRUN, qi, 0); }
}

// ================================================================ the cycle
void Replayer::step(int64_t T) {
    settle(T);
    work(T);
    worked_ = T;
}

// The row port: the output queue's oldest row leaves, when the sink is ready.
void Replayer::sink(int64_t T) {
    if (outq_n_ == 0 || !row_ready || T - last_sink_ < int64_t(sink_interval)) return;
    const uint64_t* r = outq_[outq_h_];
    outq_h_ = (outq_h_ + 1) % OUTQ_ROWS; outq_n_--;
    last_sink_ = T;
    for (unsigned i = 0; i < REC_LONGS; i++) row_hash_ = mix(row_hash_ ^ r[i]);
    if (row_buf_) {                                       // from row_base_ (0 unless the harness drains it)
        if (rows_sunk_ - row_base_ >= row_cap_) FATAL("replayer %u: the row buffer is full: %llu rows", me, (unsigned long long)row_cap_);
        for (unsigned i = 0; i < REC_LONGS; i++) row_buf_[(rows_sunk_ - row_base_) * REC_LONGS + i] = r[i];
        if (row_times_) row_times_[rows_sunk_ - row_base_] = T;
    }
    rows_sunk_++;
}

// Stream s's input left over at the pass's end: 0 none, 1 values not taken, 2 words not taken, 3 entries past its words.
unsigned Replayer::leftover_of(unsigned s, uint64_t& n) const {
    const StreamDecoder& d = decoder(s);
    const ReplayerQueue& q = s == 0 ? code : s == 1 ? rlog : wlog;
    if (d.values < sz_[s].values) { n = sz_[s].values - d.values; return 1; }
    if (d.taken < sz_[s].words) { n = sz_[s].words - d.taken; return 2; }
    if (q.occ > 0) { n = q.occ; return 3; }
    n = 0;
    return 0;
}

// One cycle, as replayer_unit.veryl's parts run it: every signal of the cycle from the registers, then the clock edge.
void Replayer::work(int64_t T) {
    using namespace rowcore;
    const bool run = busy_;
    const bool store_log = (run_config_ & RC_STORE_LOG) != 0;
    const bool started = started_;
    const bool check = check_;
    started_ = false;
    if (run) { pass_cycles++; quiet_ = 0; } else if (quiet_ < QUIET) quiet_++;

    // ---- the output queue: full is its rows at the start of the cycle; its oldest row leaves
    const bool output_full = outq_n_ == OUTQ_ROWS;
    sink(T);

    // ---- the decoders (stream_decoder.veryl)
    code_d.eval(run, code.valid(), code.head_lo(), code.head_hi(), sz_[0]);
    rlog_d.eval(run, rlog.valid(), rlog.head_lo(), rlog.head_hi(), sz_[1]);
    wlog_d.eval(run && store_log, wlog.valid(), wlog.head_lo(), wlog.head_hi(), sz_[2]);
    const bool rlog_valid = rlog_d.value_valid, wlog_valid = wlog_d.value_valid;

    // ---- the row engine (replayer_row_engine.veryl): the next row's word, completion, the reads and the start
    bool head_invalid = false, head_takes_read = false, head_takes_write = false;
    if (h_valid_) {
        const bool hstore = alu::is_store(op(h_lo_));
        head_invalid = h_invalid_ || invalid_word(h_lo_) != INV_OK;
        head_takes_read = from_log(h_lo_) && !hstore;
        head_takes_write = from_log(h_lo_) && hstore && store_log;
    }
    const bool head_logs_ready = (!head_takes_read || rlog_valid) && (!head_takes_write || wlog_valid);
    const bool alu_done = T == a_done_at_;
    const bool alu_finished = alu_done || e_done_seen_;
    const bool waits_for_transfer = e_in_flight_ && alu_finished && row_.control_log;
    const bool completing = e_in_flight_ && alu_finished && (!row_.control_log || rlog_valid);
    const bool next_row = run && rows_left_ != 0 && h_valid_;
    const bool can_start = next_row && !head_invalid && !output_full;
    const bool make_reads = !e_reads_made_ && (!e_in_flight_ || completing) && can_start && (completing || !wb_pending_);
    const bool start_logs_ready = (!e_reads_take_read_ || rlog_valid || rlog_d.value_soon) && (!e_reads_take_write_ || wlog_valid);
    const bool start = e_reads_made_ && !e_in_flight_ && next_row && start_logs_ready && !output_full;
    const bool invalid_expansion = next_row && !e_in_flight_ && head_invalid;
    const bool want_word = run && rows_left_ != 0 && !e_in_flight_ && !h_valid_;
    const bool waits_for_logs = next_row && !e_in_flight_ && !head_invalid && !head_logs_ready;
    const bool read_log_ready = (e_in_cycle_2_ && row_.takes_read) || waits_for_transfer || (waits_for_logs && head_takes_read && !rlog_valid);
    const bool write_log_ready = (e_in_cycle_2_ && row_.takes_write) || (waits_for_logs && head_takes_write && !wlog_valid);

    // ---- the fetch (replayer_fetch.veryl)
    const bool room = (!h_valid_ || start) && !f_rom_pending_word_;
    const bool word_in_budget = h_valid_ ? rows_left_ > 1 : rows_left_ != 0;
    const unsigned rows_ahead = (h_valid_ ? 1u : 0u) + (f_rom_pending_word_ ? 1u : 0u);
    const bool instruction_in_budget = rows_left_ > rows_ahead;
    const bool needs_instruction = run && !f_active_ && !f_entry_pending_ && !(h_valid_ && h_invalid_);
    const bool take_code = needs_instruction && code_d.value_valid && instruction_in_budget;
    const uint32_t code_value = uint32_t(code_d.value());
    const bool padding_word = (code_value & 3) == 0;
    const bool code_needed = needs_instruction && want_word && !h_valid_ && !f_rom_pending_word_ && rows_left_ != 0;
    const bool code_ready = take_code || (code_needed && !code_d.value_valid);

    // the entry, the cycle after its instruction is taken
    const unsigned entry_class = f_entry_ & 3, entry_start = (f_entry_ >> 2) & 0x1FF;
    const unsigned entry_kind = entry_class == 0 ? unsigned(FK_ROM) : entry_class == 3 ? unsigned(FK_BAD) : unsigned(FK_CORE);
    const bool new_expansion = f_entry_pending_ && entry_kind == FK_CORE;
    const bool current_active = f_active_ || f_entry_pending_;
    const unsigned current_kind = f_entry_pending_ ? entry_kind : f_kind_;
    const unsigned current_start = f_entry_pending_ ? entry_start : f_start_;
    const unsigned rom_word_address = f_start_ + f_index_;
    const bool past_rom = rom_word_address > 511;
    const bool rdz = ((f_instr_ >> 7) & 31) == 0;
    auto nop_policy = [rdz](unsigned p) { return rdz && (p == 1 || p == 3); };
    const uint64_t exp_lo = exp.head_lo(), exp_hi = exp.head_hi();
    const unsigned exp_rd0 = rd0(exp_lo);

    const bool may_send = run && current_active && room && word_in_budget;
    const bool send_padding = may_send && current_kind == FK_PADDING;
    const bool send_nop = may_send && current_kind == FK_NOP;
    const bool send_bad = may_send && (current_kind == FK_BAD || (current_kind == FK_ROM && f_policy_known_ && past_rom));
    const bool send_rom = may_send && current_kind == FK_ROM && f_policy_known_ && !past_rom;
    const bool send_rom_first = may_send && current_kind == FK_ROM && !f_policy_known_ && f_index_ == 0;
    const bool read_policy = run && current_active && current_kind == FK_ROM && !f_policy_known_ && f_index_ != 0 && !f_rom_pending_policy_;
    const bool pop_expansion = may_send && current_kind == FK_CORE && exp.valid();
    const bool pop_first_nop = pop_expansion && !f_policy_known_ && nop_policy(exp_rd0);

    bool rom_read = true;
    unsigned rom_address = 0;
    if (send_padding) rom_address = 0;
    else if (send_nop || pop_first_nop) rom_address = 1;
    else if (send_rom) rom_address = rom_word_address & 511;
    else if (send_rom_first || read_policy) rom_address = current_start;
    else rom_read = false;

    // the data a ROM read brings now
    const unsigned rom_rd0 = rd0(f_rom_lo_);
    const bool arriving_policy = f_rom_pending_policy_;
    const bool arriving_nop = arriving_policy && nop_policy(rom_rd0);
    const bool arriving_word = f_rom_pending_word_ && !arriving_nop;
    const bool arriving_end = arriving_word && seq_remaining(f_rom_lo_) == 0 && f_kind_ == FK_ROM;
    const bool finishing = send_padding || send_nop || send_bad || pop_first_nop || (pop_expansion && seq_remaining(exp_lo) == 0) || arriving_end;

    // ---- the pass (replayer_pass.veryl): the underflows, the end, the stops, the leftover check
    const bool code_uf = code_d.underflow(code_ready);
    const bool rlog_uf = rlog_d.underflow(read_log_ready);
    const bool wlog_uf = store_log && wlog_d.underflow(write_log_ready);
    const bool normal_end = (completing && rows_left_ == 0) || (started && rows_left_ == 0);
    const bool error_stop = run && (invalid_expansion || code_uf || rlog_uf || wlog_uf);
    const bool work_end = normal_end || error_stop;
    if (check) {
        uint64_t streams = 0, first_cause = 0;
        for (unsigned s = 0; s < (store_log ? 3u : 2u); s++) {
            uint64_t n;
            const bool complete = decoder(s).complete(sz_[s]);
            const bool entries_left = (s == 0 ? code : s == 1 ? rlog : wlog).valid();
            if (complete && !entries_left) continue;
            unsigned c = leftover_of(s, n);
            if (!streams) first_cause = c;
            streams |= 1u << s;
        }
        if (streams) { cond_events_[ERR_LEFTOVER]++; record(T, F_LEFTOVER, streams, first_cause); }
    }
    if (run && invalid_expansion) {
        cond_events_[ERR_INVALID]++;
        if (h_invalid_) record(T, F_INVALID, h_bad_cause_, h_bad_detail_);
        else record(T, F_INVALID, INV_WORD, invalid_word(h_lo_), h_lo_);
    }
    if (run && code_uf) { cond_events_[ERR_CODE_UNDER]++; record(T, F_UNDERFLOW, 0, code_d.more_values ? UF_CODE_OVER_DECODE : UF_CODE_PAST_VALUES); }
    if (run && rlog_uf) {
        cond_events_[ERR_RLOG_UNDER]++;
        record(T, F_UNDERFLOW, 1, waits_for_transfer ? (rlog_d.more_values ? UF_TRANSFER : UF_TRANSFER_END) : (rlog_d.more_values ? UF_LOG_OVER : UF_LOG_END));
    }
    if (run && wlog_uf) { cond_events_[ERR_WLOG_UNDER]++; record(T, F_UNDERFLOW, 2, wlog_d.more_values ? UF_LOG_OVER : UF_LOG_END); }

    // ---- the cycles past the rows' schedules, by what the pass waits for
    if (run && !start) {
        if (e_in_flight_) {
            if (T > a_sched_at_) stalls[T > a_done_at_ ? ST_RLOG : ST_ALU]++;   // past its schedule: the ALU's extra cycles, then its transfer's word
        } else if (rows_left_ != 0) {
            unsigned why = ST_PIPE;
            if (!h_valid_) {
                if (current_active && current_kind == FK_CORE) { if (!exp.valid()) why = f_policy_known_ ? ST_EXP : ST_NEWEXP; }
                else if (needs_instruction && !code_d.value_valid) why = ST_CODE;
            } else if (head_invalid) why = N_STALL;
            else if (output_full) why = ST_OUTQ;
            else if (e_reads_made_ && !start_logs_ready) why = e_reads_take_read_ && !(rlog_valid || rlog_d.value_soon) ? ST_RLOG : ST_WLOG;
            if (why != N_STALL) stalls[why]++;
        }
    }

    // ================================================================ the clock edge
    // ---- the row in flight: its cycle 2 (the log word, the ALU), then its completion
    const bool was_in_flight = e_in_flight_;
    const uint64_t read_log_value = rlog_d.value();
    const bool last_load_valid = e_in_cycle_2_ && row_.takes_read && row_.load;
    if (e_in_cycle_2_) compute_row(T, row_.takes_read ? read_log_value : row_.takes_write ? wlog_d.value() : 0);
    if (completing) {
        Row& r = row_;
        if (outq_n_ == OUTQ_ROWS) FATAL("replayer %u: the output queue overflowed at %lld", me, (long long)T);
        const unsigned slot = (outq_h_ + outq_n_++) % OUTQ_ROWS;
        for (unsigned i = 0; i < REC_LONGS; i++) outq_[slot][i] = r.rec[i];
        if (r.writes) { wb_reg_ = unsigned(r.rd_reg); wb_old_ = regs_[wb_reg_]; regs_[wb_reg_] = r.rd_val; wb_pending_ = true; }
        if (r.stop) {
            if (r.control_log && read_log_value != 0) violate(T, V_END_WORD, read_log_value);
            pc_ = 0; instr_idx_ = 0;
        } else if (r.transfer) { pc_ = r.new_pc; instr_idx_ = uint32_t(read_log_value); }
        else if (!r.hold) { pc_ = r.new_pc; instr_idx_++; }
        row_offset_ = r.last ? 0 : r.index + 1;
        rows_run_++;
    }
    if (!(completing && row_.writes) && wb_pending_ && !make_reads) { wb_pending_ = false; wb_landed_at_ = T; }

    // ---- the decoders, then their queues' pops
    code_d.update(code_ready, code.head_lo(), code.head_hi(), false, 0);
    rlog_d.update(read_log_ready, rlog.head_lo(), rlog.head_hi(), false, 0);
    wlog_d.update(write_log_ready, wlog.head_lo(), wlog.head_hi(), last_load_valid, read_log_value);
    if (code_d.miss64 != 0) { violate(T, V_CODE_MISS64); code_d.miss64 = 0; }
    if (code_d.take && code.pop(T, false)) post(T, EV_CODE);
    if (rlog_d.take && rlog.pop(T, false)) post(T, EV_RLOG);
    if (wlog_d.take && wlog.pop(T, false)) post(T, EV_WLOG);

    if (start) start_row(T);

    // ---- the fetch
    const bool load_direct = send_bad || (pop_expansion && !pop_first_nop);
    if (f_rom_pending_word_) {
        if (arriving_word) {
            h_lo_ = f_rom_lo_; h_hi_ = f_rom_hi_; h_code_ = f_instr_;
            const bool one_row = f_rom_pending_padding_ || f_rom_pending_nop_;
            h_temp_offset_ = one_row ? false : arriving_policy ? (rdz && rom_rd0 == 2) : f_temp_offset_;
            h_index_ = one_row ? 0 : f_rom_pending_index_;
            h_padding_ = f_rom_pending_padding_; h_nop_ = f_rom_pending_nop_; h_nop_virtual_ = f_nop_virtual_; h_invalid_ = false; h_core_ = false;
        }
    } else if (load_direct) {
        h_lo_ = exp_lo; h_hi_ = exp_hi; h_code_ = f_instr_;
        h_temp_offset_ = f_policy_known_ ? f_temp_offset_ : (rdz && exp_rd0 == 2);
        h_index_ = f_index_; h_padding_ = false; h_nop_ = false; h_nop_virtual_ = false; h_invalid_ = send_bad; h_core_ = !send_bad;
        if (send_bad) {
            if (current_kind == FK_BAD) { h_bad_cause_ = INV_CLASS3; h_bad_detail_ = f_instr_; }
            else { h_bad_cause_ = INV_ROM_END; h_bad_detail_ = uint64_t(f_start_) << 16 | f_index_; }
        }
    }
    if (arriving_word || load_direct) h_valid_ = true;
    else if (start) h_valid_ = false;

    if (new_expansion) {
        // the expansion queue is empty at a new expansion: a word left in it is UB (17.8.8)
        if (exp.valid()) violate(T, V_EXP_LEFT, exp.occ);
        posted_offset_ = f_index_; posted_opcode_ = f_instr_;
        new_expansions++;
    }
    if (pop_expansion && exp.pop(T, new_expansion)) post(T, EV_EXP);
    if (new_expansion) {
        exp.left = 0;
        ev.set(T, EV_EXP, 2); ev_fill_at_[EV_EXP] = T;
        post(T, EV_NEWEXP);
        pushed_[3] = 0; allowance_[3] = 0;
    }

    const bool was_entry_pending = f_entry_pending_;
    f_rom_pending_word_ = send_padding || send_nop || send_rom || send_rom_first || pop_first_nop;
    f_rom_pending_policy_ = send_rom_first || read_policy;
    f_rom_pending_padding_ = send_padding;
    f_rom_pending_nop_ = send_nop || pop_first_nop;
    f_rom_pending_index_ = f_index_;
    if (rom_read) { f_rom_lo_ = rom_lo[rom_address]; f_rom_hi_ = rom_hi[rom_address]; }
    f_entry_pending_ = take_code && !padding_word;
    if (take_code) {
        const bool first = f_first_;
        f_first_ = false; f_active_ = padding_word;
        f_instr_ = code_value;
        f_entry_ = padding_word ? uint16_t(0) : decode_table[decode_index(code_value)];
        f_index_ = first ? row_offset_ : 0;
        f_kind_ = FK_PADDING; f_policy_known_ = false; f_temp_offset_ = false; f_nop_virtual_ = false;
    } else {
        if (finishing) f_active_ = false;
        else if (was_entry_pending) f_active_ = true;
        if (was_entry_pending) { f_start_ = entry_start; f_kind_ = entry_kind; }
        if (arriving_policy) {
            f_policy_known_ = true;
            if (nop_policy(rom_rd0)) { f_kind_ = FK_NOP; f_nop_virtual_ = rom_rd0 == 1; }
            else f_temp_offset_ = rdz && rom_rd0 == 2;
        }
        if (pop_expansion && !f_policy_known_) {
            f_policy_known_ = true;
            if (nop_policy(exp_rd0)) { f_kind_ = FK_NOP; f_nop_virtual_ = exp_rd0 == 1; }
            else f_temp_offset_ = rdz && exp_rd0 == 2;
        }
        if (send_rom || send_rom_first || (pop_expansion && !pop_first_nop)) f_index_++;
    }

    // ---- the row engine's stages
    if (make_reads) { e_reads_take_read_ = head_takes_read; e_reads_take_write_ = head_takes_write; }
    e_reads_made_ = make_reads;
    e_in_cycle_2_ = start;
    if (completing) { e_in_flight_ = false; e_done_seen_ = false; }
    else if (was_in_flight && alu_done) e_done_seen_ = true;

    // ---- the pass
    check_ = normal_end;
    if (work_end) { busy_ = false; pass_end_at = T; post(T, EV_RUN_DONE); }
}

// A row's start, its cycle 1: the word and its context latched, the operand codes resolved (17.8.4, 17.8.5), rs1's and
// rs2's values read (rd's too: the rows never overlap, so it is the value its cycle 1's read gives), the ALU started.
void Replayer::start_row(int64_t T) {
    using namespace rowcore;
    Row& r = row_;
    r = Row();
    r.lo = h_lo_; r.hi = h_hi_; r.w = h_code_; r.temp_offset = h_temp_offset_; r.index = h_index_;
    r.padding = h_padding_; r.nop = h_nop_; r.nop_virtual = h_nop_virtual_; r.core = h_core_;
    const uint64_t lo = r.lo;
    const unsigned op = rowcore::op(lo);
    if (reserved(lo) != 0)
        FATAL("replayer %u: undefined behavior at %lld: an expansion word with reserved bits [61:55] = 0x%x (%s): lo 0x%016llx", me,
              (long long)T, reserved(lo), r.core ? "core-fed" : "ROM", (unsigned long long)lo);
    r.takes_read = e_reads_take_read_; r.takes_write = e_reads_take_write_; r.load = alu::is_load(op);
    if (r.takes_read && next_from_value(lo)) violate(T, V_LOG_AND_NFV);
    const unsigned off = r.temp_offset ? 1 : 0;
    r.src_imm = needs_source_imm(lo) ? source_imm(format(lo), r.w) : 0;
    r.r_rd = rowcore::reg(c_rd(lo), r.w, off); r.r_rs1 = rowcore::reg(c_rs1(lo), r.w, off); r.r_rs2 = rowcore::reg(c_rs2(lo), r.w, off);
    r.v_rd = r.r_rd >= 0 ? reg(unsigned(r.r_rd)) : imm_val(c_rd(lo), r.hi, r.src_imm);
    r.v_rs1 = r.r_rs1 >= 0 ? reg(unsigned(r.r_rs1)) : imm_val(c_rs1(lo), r.hi, r.src_imm);
    r.v_rs2 = r.r_rs2 >= 0 ? reg(unsigned(r.r_rs2)) : imm_val(c_rs2(lo), r.hi, r.src_imm);
    // the ALU's schedule (replayer_alu.veryl): `done` in the row's cycle 3, 5 for an M row; a VirtualPext whose mask has
    // two or more runs extracts a bit a cycle up to the mask's highest set bit, and is done the cycle after
    a_sched_at_ = T + (alu::cls(op) == 'M' ? 4 : 2);
    a_done_at_ = a_sched_at_;
    if (op == alu::PEXT || op == alu::PEXT_S) {
        const uint64_t m = r.v_rs2, run_starts = m & ~(m << 1);
        if ((run_starts & (run_starts - 1)) != 0) {
            a_done_at_ = T + 3 + (64 - unsigned(__builtin_clzll(m)));
            pext_non_contiguous++; pext_extra_cycles += uint64_t(a_done_at_ - a_sched_at_);
        }
    }
    e_in_flight_ = true;
    rows_left_--;
    rows_total++;
    if (alu::cls(op) == 'M') rows_m++;
    if (r.core) rows_ctrl++;
}

// The row's cycle 2: its log word taken, its values computed (replayer_alu.h), its outcome for pc and instr_idx
// (17.8.7), and the row formed (17.8.11).
void Replayer::compute_row(int64_t T, uint64_t log_word) {
    using namespace rowcore;
    Row& r = row_;
    const uint64_t lo = r.lo;
    const uint32_t w = r.w;
    const unsigned op = rowcore::op(lo), seq_rem = seq_remaining(lo);
    const bool nfv = next_from_value(lo), hld = hold(lo), o_imm = offset_imm(lo);
    const uint64_t so = o_imm ? r.src_imm : 0;
    if (o_imm && !alu::is_shift_data(op)) violate(T, V_OFFSET_IMM, op);
    const int r_rd = r.r_rd, r_rs1 = r.r_rs1, r_rs2 = r.r_rs2;
    const uint64_t v_rd = r.v_rd, v_rs1 = r.v_rs1, v_rs2 = r.v_rs2;
    const uint64_t imm_sum = (r_rd < 0 ? v_rd : 0) + (r_rs1 < 0 ? v_rs1 : 0) + (r_rs2 < 0 ? v_rs2 : 0);
    r.last = seq_rem == 0 || r.padding || r.nop;
    const bool compressed = (w & 1) == 0;
    const unsigned pc_inc = !r.last ? 0 : compressed ? 2 : 4;
    alu::Out o;
    alu::exec(op, v_rs1, v_rs2, v_rd, pc_, pc_inc, imm0pc0(lo), log_word, so, o);
    if (o.undefined) violate(T, V_UNDEFINED, op, uint64_t(reinterpret_cast<uintptr_t>(o.undefined)));
    r.writes = r_rd > 0 && alu::writes_rd(op);
    if (r.writes) { r.rd_reg = r_rd; r.rd_val = o.rd; }
    const uint64_t rd_after = r_rd <= 0 ? 0 : r.writes ? o.rd : v_rd;
    // pc and instr_idx (17.8.7): hold; a new pc of 0; a transfer; otherwise sequential
    const bool moves = r.last && o.new_pc != pc_;
    r.hold = hld; r.stop = false; r.transfer = false; r.control_log = !hld && nfv && moves;
    if (hld) r.new_pc = pc_;
    else if (o.new_pc == 0) { r.stop = true; r.new_pc = 0; }
    else if (moves && nfv) { r.transfer = true; r.new_pc = o.new_pc; }
    else {
        if (moves) violate(T, V_NO_NFV, op);   // UB (17.8.7): sequential, as the RTL
        r.new_pc = pc_ + pc_inc;
    }
    // the row (17.8.11)
    uint64_t* rec = r.rec;
    for (unsigned i = 0; i < REC_LONGS; i++) rec[i] = 0;
    if (!r.padding) {
        const bool is_virtual = r.nop ? r.nop_virtual : (virt(lo) || seq_rem > 0 || r.index > 0);
        const bool is_first = is_virtual && (r.nop || r.index == 0);
        const bool is_comp = compressed && report_c(lo);
        auto nz = [](int x) { return uint64_t(x < 0 ? 0 : x); };
        rec[0] = uint64_t(instr_idx_) | uint64_t(op) << 32 | nz(r_rd) << 40 | nz(r_rs1) << 48 | nz(r_rs2) << 56;
        rec[1] = (seq_rem & 0xFFFFull) | uint64_t(is_comp) << 16 | uint64_t(is_virtual) << 17 | uint64_t(is_first) << 18
                 | uint64_t(r_rd < 0) << 19 | uint64_t(r_rs1 < 0) << 20 | uint64_t(r_rs2 < 0) << 21 | uint64_t(sign_imm(lo)) << REC_SIGN_IMM;
        rec[2] = pc_; rec[3] = alu::is_shift_data(op) ? so : imm_sum;
        rec[4] = r_rs1 >= 0 ? v_rs1 : 0; rec[5] = r_rs2 >= 0 ? v_rs2 : 0;
        rec[6] = r_rd >= 0 && (run_config_ & RC_RD_PREV) != 0 ? v_rd : 0; rec[7] = rd_after;
        if (alu::is_load(op)) { rec[8] = alu::mem_addr(op, v_rs1, imm_sum); rec[9] = rd_after; }
        else if (alu::is_store(op)) { rec[8] = alu::mem_addr(op, v_rs1, imm_sum); rec[9] = r.takes_write ? log_word : 0; }
        if ((alu::is_load(op) || alu::is_store(op)) && ((v_rs1 + imm_sum) & 7 & ~alu::floor_mask(op)) != 0)
            violate(T, V_MISALIGNED, op, v_rs1 + imm_sum);
    }
}

}  // namespace units
