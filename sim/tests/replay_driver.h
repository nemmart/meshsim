// replay_driver.h: Stage A's standalone harness (WORKER15): ref's test/ReplayDriver.java line for line, on
// units::Replayer. One replayer fed by an ideal controller through the peripheral interface only: it loads part 0, the
// decode table and the ROM with mode-0 packets into region 0x04, starts a pass, then every cycle consumes every event
// it sees and answers: a queue's credit with min(half, entries left) packets of that stream, new expansion with the
// template of the presented instruction from the next offset, the expansion's credit with its next words. One packet
// per cycle, expansion words first. The rows come out through the replayer's sink (its row buffer).
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "replay_window.h"

namespace rtest {

struct ReplayDriver {
    struct Pkt { uint32_t addr; uint64_t lo, hi; };
    static constexpr unsigned SRC = 0;
    units::Replayer* r;
    std::unique_ptr<units::Replayer> owned;
    ReplayWindow& w;
    int64_t t = 0;
    std::vector<uint64_t> buf;                   // the sink's records (10 words each)
    uint64_t pass_first_row = 0;
    std::deque<Pkt> port, port_exp;
    int code_next = 0, rlog_next = 0, wlog_next = 0;
    std::vector<uint64_t> exp_words;
    bool has_exp = false;
    size_t exp_next = 0;
    bool done = false;
    int64_t run_at = 0, done_at = 0;
    uint64_t missing_templates = 0;
    int64_t run_config = -1;                     // -1: the window's
    uint32_t last_config = 0;
    uint64_t config_stores = 0;
    int passes_run = 0;

    explicit ReplayDriver(ReplayWindow& win, uint64_t max_rows = 1u << 17, units::Replayer* rp = nullptr)
        : r(rp), w(win), buf(max_rows * units::Replayer::REC_LONGS) {
        if (!r) { owned = std::make_unique<units::Replayer>(0); r = owned.get(); }
        r->set_row_buffer(buf.data(), max_rows);
    }

    // The rows of the current (or last) pass.
    uint64_t rows() const { return r->rows_sunk() - pass_first_row; }
    const uint64_t* row(uint64_t i) const { return &buf[(pass_first_row + i) * units::Replayer::REC_LONGS]; }

    void write_config() {
        uint32_t c = run_config >= 0 ? uint32_t(run_config) : w.run_config_word();
        if (c != last_config) {
            if (r->write_word(t, units::Replayer::W_RUN_CONFIG, c) != 1) FATAL("RUN_CONFIG refused");
            last_config = c; config_stores++; step();
        }
    }
    void step() {
        if (!port.empty() || !port_exp.empty()) {
            Pkt p;
            if (!port_exp.empty()) { p = port_exp.front(); port_exp.pop_front(); }
            else { p = port.front(); port.pop_front(); }
            r->accept(t, SRC, p.addr << 2, 2, p.lo, p.hi);
        }
        r->step(t);
        t++;
    }
    void load() {
        add_part0();
        for (unsigned p = 0; p < 256; p++) {
            uint64_t lo = 0, hi = 0;
            for (unsigned k = 0; k < 4; k++) lo |= uint64_t(w.table[8 * p + k]) << (16 * k);
            for (unsigned k = 0; k < 4; k++) hi |= uint64_t(w.table[8 * p + 4 + k]) << (16 * k);
            port.push_back({0x400 + 4 * p, lo, hi});
        }
        for (unsigned j = 0; j < 512; j++) port.push_back({0x800 + 4 * j, w.rom_lo[j], w.rom_hi[j]});
        while (!port.empty()) step();
        for (int i = 0; i < units::Replayer::RP_LATENCY + 1; i++) step();
    }
    void resend_state() {
        add_part0();
        while (!port.empty()) step();
        for (int i = 0; i < units::Replayer::RP_LATENCY + 1; i++) step();
    }
    void add_part0() {
        if (!w.region0.empty()) {
            for (unsigned i = 0; i < units::Replayer::PART0_WORDS; i++) port.push_back({4 * i, w.region0[2 * i], w.region0[2 * i + 1]});
        } else {
            port.push_back({0x0, w.state_lo(), w.state_hi_word()});
            port.push_back({0x4, w.stream_lo(), w.stream_hi()});
            for (unsigned i = 0; i < 64; i++) port.push_back({0x8 + 4 * i, w.regs[2 * i], w.regs[2 * i + 1]});
        }
    }
    // One pass; stops at done or after max_cycles. A pass after the first resends the state word. Returns the rows.
    uint64_t pass(int64_t max_cycles) {
        if (passes_run++ > 0) resend_state();
        return pass_from(max_cycles, 0);
    }
    uint64_t pass_from(int64_t max_cycles, int code_sent) {
        pass_first_row = r->rows_sunk();
        done = false; code_next = code_sent; rlog_next = wlog_next = 0; has_exp = false; exp_words.clear();
        write_config();
        r->run(t); run_at = t;
        step();
        int64_t end = t + max_cycles;
        while (t < end) {
            r->sync(t);
            uint32_t l = r->events().value(t);
            if (l != 0) {
                uint32_t st1 = 0, off = 0;
                r->word(t, units::Replayer::W_EXP_OPCODE, st1);
                r->consume_events(l);
                if (l & 1u << units::Replayer::EV_CODE) code_next = refill(w.code, code_next, 0x40000, 128);
                if (l & 1u << units::Replayer::EV_RLOG) rlog_next = refill(w.rlog, rlog_next, 0x80000, r->rlog.half);
                if (l & 1u << units::Replayer::EV_WLOG) wlog_next = refill(w.wlog, wlog_next, 0xC0000, r->wlog.half);
                if (l & 1u << units::Replayer::EV_NEWEXP) {
                    uint32_t key = units::tracer::ReplayTables::key_of(st1);
                    auto it = w.templates.find(key);
                    std::vector<uint64_t> tpl;
                    if (it == w.templates.end()) missing_templates++;
                    else tpl = it->second;
                    r->word(t, units::Replayer::W_EXP_OFFSET, off);
                    auto ir = w.template_rd0.find(key);
                    uint32_t rd0 = ir != w.template_rd0.end() ? ir->second : uint32_t(tpl.size() > 0 ? (tpl[0] >> 46) & 3 : 0);
                    if (((st1 >> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) {
                        exp_words = {(w.rom_lo[1] & ~(3ull << 46)) | uint64_t(rd0) << 46, w.rom_hi[1]}; exp_next = 0;
                    } else if (off > 0 && 2 * off < tpl.size()) {
                        exp_words = tpl; exp_words[2 * off] = (exp_words[2 * off] & ~(3ull << 46)) | uint64_t(rd0) << 46; exp_next = 2 * off;
                    } else { exp_words = tpl; exp_next = 2 * off; }
                    has_exp = true;
                    if ((l & 1u << units::Replayer::EV_EXP) == 0) r->consume_events(1u << units::Replayer::EV_EXP);
                    l &= ~(1u << units::Replayer::EV_EXP);
                    send_exp();
                }
                if (l & 1u << units::Replayer::EV_EXP) send_exp();
                if (l & 1u << units::Replayer::EV_RUN_DONE) { done = true; done_at = t; }
            }
            step();
            if (done && r->out_queue_rows() == 0) break;
        }
        for (int i = 0; i < 4; i++) step();
        return rows();
    }
    int refill(const std::vector<uint64_t>& s, int next, uint32_t base, unsigned half) {
        int n = int(half) < int(s.size() / 2) - next ? int(half) : int(s.size() / 2) - next;
        for (int i = 0; i < n; i++, next++) port.push_back({base, s[2 * size_t(next)], s[2 * size_t(next) + 1]});
        return next;
    }
    void send_exp() {
        if (!has_exp) return;
        size_t avail = (exp_words.size() - exp_next) / 2, n = avail < 128 ? avail : 128;
        for (size_t i = 0; i < n; i++, exp_next += 2) port_exp.push_back({0x100000, exp_words[exp_next], exp_words[exp_next + 1]});
    }
};

}  // namespace rtest
