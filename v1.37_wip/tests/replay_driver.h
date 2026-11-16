// replay_driver.h: Stage A's standalone harness: one replayer and an ideal controller, through the replayer's ports
// alone (the write port, its words, its events and the row port), exactly as the RTL's acceptance drives
// `replayer_unit` (Snarky One, rtl/blocks/replayer_unit/test/src/replayer_acceptance_test.rs, `pass`), so that a pass
// takes the same cycles on both.
//
// The controller (17.8.13), cycle by cycle from the cycle after START's:
//   - the write port takes one packet a cycle, of those released before this cycle: an expansion word first, else the
//     streams' entries, a stream in turn (code, read log, write log, from the one after the last served);
//   - a load, one a cycle: EXPANSION_OPCODE, then EXPANSION_OFFSET, in the two cycles after a new expansion is seen;
//     else, in a cycle whose level (EVENTS & EVENTS_MASK, every event in the mask) is up, a consuming load of EVENTS;
//   - a credit taken releases the stream's next min(128, entries left) entries; `new_expansion` drops what is left
//     of the expansion before it and asks for the two loads; the expansion queue's credit releases the next
//     min(128, words left) words of the expansion, or is kept until its words are known (the load of
//     EXPANSION_OFFSET, which releases min(128 x the credits kept, the words));
//   - the expansion's words: the template of EXPANSION_OPCODE's key, from EXPANSION_OFFSET, the template's rd0 in the
//     first word sent; under a NOP policy with rd = x0 the shared NOP row alone, carrying the template's rd0;
//   - the row port is always ready: the replayer's sink, a row a cycle.
// Before a pass: the record's words 0-65 through part 0, the state word first, a packet a cycle; RUN_CONFIG if it
// changes; then START. The rows come out through the replayer's sink (its row buffer), each with the cycle it left.
#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "replay_window.h"

namespace rtest {

struct ReplayDriver {
    static constexpr unsigned SRC = 0;
    static constexpr unsigned HALF = units::ReplayerQueue::HALF;
    // The region's parts (17.8.9).
    static constexpr uint32_t PART_CODE = 1u << 20, PART_EXP = 4u << 20, DECODE_TABLE = 0x1000, ROM = 0x2000;
    units::Replayer* r;
    std::unique_ptr<units::Replayer> owned;
    ReplayWindow& w;
    int64_t t = 0;
    std::vector<uint64_t> buf;                   // the sink's records (10 words each)
    std::vector<int64_t> times;                  // the cycle each left the row port
    uint64_t pass_first_row = 0;
    // each stream's next entry and the entries its credits released to the port; the stream whose turn is next
    size_t next[3] = {}, released[3] = {};
    unsigned turn = 0;
    // the expansion being fed: its words, the next, those a credit released, and the credits taken before its words
    // are known
    std::vector<uint64_t> exp_words;
    size_t exp_next = 0, exp_released = 0, exp_credits = 0;
    std::deque<unsigned> due;                    // the loads due for a new expansion
    uint32_t opcode = 0;
    bool done = false;
    int64_t run_at = 0, done_at = 0;
    uint64_t missing_templates = 0, posts = 0, expansion_words_sent = 0, cut_short = 0;
    std::vector<std::string> log;                // one line per missing template
    int64_t run_config = -1;                     // -1: the window's
    uint32_t last_config = 0;
    uint64_t config_stores = 0;
    int passes_run = 0;
    int after_done = 40;                         // the cycles after the pass's end in which the output queue drains
    // A recording of every input it gives the replayer, a line a cycle, as a replayconf script (tests/replayconf.h):
    // the same inputs replayed on the RTL's replayer_unit give the RTL's observations of them.
    std::string* script = nullptr;
    std::string ops;                             // this cycle's inputs
    // The row's consumer: whether the row port is ready in cycle t (always, when null).
    std::function<bool(int64_t)> ready;
    // A slower core: whether the port must not yet take entry `index` of queue q (0-2 the streams, 3 the expansion's
    // words) in cycle t of the pass, START's being 0 (never, when null); and whether it loads EVENTS in every cycle,
    // its level up or not.
    std::function<bool(unsigned, size_t, int64_t)> hold;
    bool eager = false;

    explicit ReplayDriver(ReplayWindow& win, uint64_t max_rows = 1u << 17, units::Replayer* rp = nullptr)
        : r(rp), w(win), buf(max_rows * units::Replayer::REC_LONGS), times(max_rows) {
        if (!r) { owned = std::make_unique<units::Replayer>(0); r = owned.get(); }
        r->set_row_buffer(buf.data(), max_rows);
        r->set_row_times(times.data());
    }

    // The rows of the current (or last) pass, and the cycle each left the row port, START's cycle being 0.
    uint64_t rows() const { return r->rows_sunk() - pass_first_row; }
    const uint64_t* row(uint64_t i) const { return &buf[(pass_first_row + i) * units::Replayer::REC_LONGS]; }
    int64_t left(uint64_t i) const { return times[pass_first_row + i] - run_at; }
    // The pass's cycles, as the RTL's acceptance counts them: from START's cycle to the last row leaving the row port
    // (to the pass's end, for a pass with no row).
    int64_t cycles() const { return rows() ? left(rows() - 1) : r->pass_end_at - run_at; }

    const std::vector<uint64_t>& stream(unsigned s) const { return s == 0 ? w.code : s == 1 ? w.rlog : w.wlog; }

    // The cycle: the replayer's step, then the consuming load's effect, if this cycle had one.
    bool consume = false;
    uint32_t consume_bits = 0;
    void step() {
        r->row_ready = !ready || ready(t);
        if (!r->row_ready) note(" N");
        r->step(t);
        r->row_ready = true;
        if (consume) r->consume_events(consume_bits);
        consume = false;
        if (script) { *script += std::to_string(t) + ops + "\n"; ops.clear(); }
        t++;
    }
    void note(const char* f, ...) __attribute__((format(printf, 2, 3))) {
        if (!script) return;
        char b[96];
        va_list ap;
        va_start(ap, f);
        std::vsnprintf(b, sizeof b, f, ap);
        va_end(ap);
        ops += b;
    }
    // This cycle's inputs: a WRITE of 128 bits into the region; a store to RUN_CONFIG; START; a consuming load of
    // EVENTS, which read `events`.
    void present(uint32_t offset, uint64_t lo, uint64_t hi) {
        r->write(t, SRC, offset, lo, hi);
        note(" W%x,%llx,%llx", offset, (unsigned long long)lo, (unsigned long long)hi);
    }
    void store_config(uint32_t c) {
        if (r->write_word(t, units::Replayer::W_RUN_CONFIG, c) != 1) FATAL("RUN_CONFIG refused");
        note(" C%x", c);
    }
    void start() { r->run(t); note(" S"); }
    void load_events(uint32_t events) { consume = true; consume_bits = events; note(" K"); }
    void write(uint32_t offset, uint64_t lo, uint64_t hi) { present(offset, lo, hi); step(); }
    void write_config() {
        uint32_t c = run_config >= 0 ? uint32_t(run_config) : w.run_config_word();
        if (c != last_config) { store_config(c); last_config = c; config_stores++; step(); }
    }
    void load() {
        add_part0();
        for (unsigned p = 0; p < 256; p++) {
            uint64_t lo = 0, hi = 0;
            for (unsigned k = 0; k < 4; k++) lo |= uint64_t(w.table[8 * p + k]) << (16 * k);
            for (unsigned k = 0; k < 4; k++) hi |= uint64_t(w.table[8 * p + 4 + k]) << (16 * k);
            write(DECODE_TABLE + 16 * p, lo, hi);
        }
        for (unsigned j = 0; j < 512; j++) write(ROM + 16 * j, w.rom_lo[j], w.rom_hi[j]);
    }
    void resend_state() { add_part0(); }
    void add_part0() {
        if (!w.region0.empty()) {
            for (unsigned i = 0; i < units::Replayer::PART0_WORDS; i++) write(16 * i, w.region0[2 * i], w.region0[2 * i + 1]);
        } else {
            write(0x00, w.state_lo(), w.state_hi_word());
            write(0x10, w.stream_lo(), w.stream_hi());
            for (unsigned i = 0; i < 64; i++) write(0x20 + 16 * i, w.regs[2 * i], w.regs[2 * i + 1]);
        }
    }
    // The words the core pushes for the core-fed instruction `op` from row offset `off` (17.8.13).
    std::vector<uint64_t> expansion(uint32_t op, uint32_t off) {
        const uint32_t key = units::tracer::ReplayTables::key_of(op);
        auto it = w.templates.find(key);
        if (it == w.templates.end()) {
            missing_templates++;
            char m[96];
            std::snprintf(m, sizeof m, "@%lld no template for 0x%x (key 0x%x)", (long long)t, op, key);
            log.push_back(m);
            return {};
        }
        const std::vector<uint64_t>& tpl = it->second;
        auto ir = w.template_rd0.find(key);
        const uint64_t rd0 = ir != w.template_rd0.end() ? ir->second : uint64_t(tpl.size() > 0 ? (tpl[0] >> 46) & 3 : 0);
        if (((op >> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) return {(w.rom_lo[1] & ~(3ull << 46)) | rd0 << 46, w.rom_hi[1]};
        if (2 * size_t(off) >= tpl.size()) return {};
        std::vector<uint64_t> words(tpl.begin() + 2 * off, tpl.end());
        words[0] = (words[0] & ~(3ull << 46)) | rd0 << 46;
        return words;
    }
    // One pass; stops after_done cycles after done, or after max_cycles. A pass after the first resends the record.
    // Returns the rows.
    uint64_t pass(int64_t max_cycles) {
        if (passes_run++ > 0) resend_state();
        return pass_from(max_cycles, 0);
    }
    // code_sent: the code stream's entries a test pushed itself before the pass.
    uint64_t pass_from(int64_t max_cycles, int code_sent) {
        using R = units::Replayer;
        pass_first_row = r->rows_sunk();
        done = false;
        next[0] = size_t(code_sent); next[1] = next[2] = 0;
        released[0] = released[1] = released[2] = 0; turn = 0;
        exp_words.clear(); exp_next = exp_released = exp_credits = 0; due.clear();
        write_config();
        start(); run_at = t;
        step();
        int left_after = after_done;
        const int64_t end = t + max_cycles;
        while (t < end && left_after > 0) {
            // the level, of this cycle: from the block's registers
            r->sync(t);
            const uint32_t events = r->events().value(t);
            const bool level = !done && events != 0;
            // the write port: an expansion word first, else the streams' in turn
            if (exp_released > 0 && !(hold && hold(3, exp_next / 2, t - run_at))) {
                present(PART_EXP, exp_words[exp_next], exp_words[exp_next + 1]);
                exp_next += 2; exp_released--; expansion_words_sent++;
            } else if (exp_released == 0) {
                for (unsigned ahead = 0; ahead < 3; ahead++) {
                    const unsigned s = (turn + ahead) % 3;
                    if (released[s] == 0 || (hold && hold(s, next[s], t - run_at))) continue;
                    const std::vector<uint64_t>& e = stream(s);
                    present(PART_CODE * (s + 1), e[2 * next[s]], e[2 * next[s] + 1]);
                    next[s]++; released[s]--; turn = (s + 1) % 3;
                    break;
                }
            }
            // the controller's load
            if (!due.empty()) {
                const unsigned word = due.front();
                due.pop_front();
                uint32_t v = 0;
                r->word(t, int(word), v);
                if (word == R::W_EXP_OPCODE) opcode = v;
                else {
                    exp_words = expansion(opcode, v);
                    exp_next = 0;
                    exp_released = std::min(size_t(HALF) * exp_credits, exp_words.size() / 2);
                    exp_credits = 0;
                }
            } else if (level || (eager && !done)) {
                load_events(events);
                for (unsigned s = 0; s < 3; s++)
                    if (events & 1u << (R::EV_CODE + s)) released[s] += std::min(size_t(HALF), stream(s).size() / 2 - next[s] - released[s]);
                if (events & 1u << R::EV_NEWEXP) {
                    posts++;
                    cut_short += (exp_words.size() - exp_next) / 2;
                    exp_words.clear(); exp_next = 0; exp_released = 0; exp_credits = 0;
                    due.push_back(R::W_EXP_OPCODE); due.push_back(R::W_EXP_OFFSET);
                }
                if (events & 1u << R::EV_EXP) {
                    if (due.empty()) exp_released += std::min(size_t(HALF), (exp_words.size() - exp_next) / 2 - exp_released);
                    else exp_credits++;
                }
                if (events & 1u << R::EV_RUN_DONE) { done = true; done_at = t; }
            }
            step();
            if (done) left_after--;
        }
        return rows();
    }
};

}  // namespace rtest
