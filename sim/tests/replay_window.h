// replay_window.h: one window's replayer inputs and the builders the unit tests use (WORKER15): ref's
// test/ReplayWindow.java, method for method, and java.util.Random (the tests' seeded inputs must be ref's).
// Test code: the STL is fine here (configuration-phase rules).
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "units/replayer.h"
#include "units/replay_tables.h"
#include "units/stream_word.h"

namespace rtest {

// java.util.Random, exactly.
struct JRandom {
    uint64_t seed;
    explicit JRandom(int64_t s) : seed((uint64_t(s) ^ 0x5DEECE66Dull) & ((1ull << 48) - 1)) {}
    int32_t next(int bits) {
        seed = (seed * 0x5DEECE66Dull + 0xB) & ((1ull << 48) - 1);
        return int32_t(int64_t(seed) >> (48 - bits));
    }
    int32_t next_int() { return next(32); }
    int32_t next_int(int32_t bound) {
        if ((bound & -bound) == bound) return int32_t((int64_t(bound) * int64_t(next(31))) >> 31);
        int32_t bits, val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (int32_t(uint32_t(bits) - uint32_t(val) + uint32_t(bound - 1)) < 0);
        return val;
    }
    int64_t next_long() { return int64_t(uint64_t(int64_t(next(32))) << 32) + int64_t(next(32)); }
    bool next_boolean() { return next(1) != 0; }
};

// A 128-bit expansion word (ReplayWindow.W).
struct W {
    uint64_t lo = 0, hi = 0;
    static W op(unsigned o) { W w; w.lo = o & 0xFF; return w.rd(128).rs1(128).rs2(128); }
    W& rd(unsigned c) { lo = (lo & ~(0xFFull << 8)) | uint64_t(c & 0xFF) << 8; return *this; }
    W& rs1(unsigned c) { lo = (lo & ~(0xFFull << 16)) | uint64_t(c & 0xFF) << 16; return *this; }
    W& rs2(unsigned c) { lo = (lo & ~(0xFFull << 24)) | uint64_t(c & 0xFF) << 24; return *this; }
    W& seq(unsigned n) { lo = (lo & ~(0xFFFull << 32)) | uint64_t(n & 0xFFF) << 32; return *this; }
    W& nfv() { lo |= 1ull << 44; return *this; }
    W& hold() { lo |= 1ull << 45; return *this; }
    W& rd0(unsigned p) { lo = (lo & ~(3ull << 46)) | uint64_t(p & 3) << 46; return *this; }
    W& fmt(unsigned f) { lo = (lo & ~(7ull << 48)) | uint64_t(f & 7) << 48; return *this; }
    W& virt() { lo |= 1ull << 51; return *this; }
    W& imm0pc0() { lo |= 1ull << 52; return *this; }
    W& report_c() { lo |= 1ull << 53; return *this; }
    W& from_log() { lo |= 1ull << 54; return *this; }
    W& reserved(unsigned bit) { lo |= 1ull << bit; return *this; }
    W& sign() { lo |= 1ull << 62; return *this; }
    W& off_imm() { lo |= 1ull << 63; return *this; }
    W& cnst(uint64_t c) { hi = c; return *this; }
    unsigned seq_rem() const { return unsigned(lo >> 32) & 0xFFF; }
};

// A bit writer: LSB first within each 128-bit entry, entries in order (ReplayWindow.Bits).
struct Bits {
    std::vector<uint64_t> words;
    int64_t n = 0;
    void put(uint64_t v, int bits) {
        for (int k = 0; k < bits; k++) {
            int64_t pos = n + k;
            size_t wi = size_t(pos >> 6);
            while (words.size() <= wi) words.push_back(0);
            if ((v >> (k & 63)) & 1) words[wi] |= 1ull << (pos & 63);   // Java: a shift count mod 64
        }
        n += bits;
    }
    std::vector<uint64_t> entries(int64_t seed = 0) const {
        int64_t e = (n + 127) >> 7;
        std::vector<uint64_t> r(size_t(2 * e), 0);
        for (size_t i = 0; i < r.size() && i < words.size(); i++) r[i] = words[i];
        if (seed != 0 && (n & 127) != 0) {
            JRandom g(seed ^ n);
            for (int64_t pos = n; pos < 128 * e; pos++)
                if (g.next_boolean()) r[size_t(pos >> 6)] |= 1ull << (pos & 63);
        }
        return r;
    }
};

// The encoder's table: true LRU over 16 entries (or 15 plus the last-load entry for the write log).
struct Lru {
    uint64_t E[16] = {};
    bool write;
    explicit Lru(bool w) : write(w) {}
    int code(uint64_t v) {
        if (write && E[15] == v) return 15;
        int top = write ? 15 : 16;
        for (int i = 0; i < top; i++)
            if (E[i] == v) { for (int j = i; j > 0; j--) E[j] = E[j - 1]; E[0] = v; return i; }
        for (int j = top - 1; j > 0; j--) E[j] = E[j - 1];
        E[0] = v;
        return -1;
    }
};
inline void put_value(Bits& b, int hit, uint64_t v) {
    if (hit >= 0) { b.put(1, 1); b.put(uint64_t(hit), 4); }
    else if ((v >> 32) == 0) { b.put(0, 1); b.put(0, 1); b.put(v, 32); }
    else { b.put(0, 1); b.put(1, 1); b.put(v, 64); }
}

struct LogEvent {
    bool has_read = false;
    uint64_t read = 0;
    bool load = false, has_write = false;
    uint64_t write = 0;
};

// ref's Replayer.runConfig and stateHi.
inline uint32_t run_config(uint32_t rows, bool store_log, bool rd_prev) {
    if (rows > units::Replayer::RC_ROWS) FATAL("rows %u: 0 to 16,777,215", rows);
    return rows | (store_log ? units::Replayer::RC_STORE_LOG : 0) | (rd_prev ? units::Replayer::RC_RD_PREV : 0);
}
inline uint64_t state_hi(uint32_t instr_idx, uint32_t next_offset) { return uint64_t(instr_idx) | uint64_t(next_offset & 0xFFFF) << 32; }
// ref's Replayer.streamWord.
inline void stream_word(const units::streamword::Size s[3], uint32_t advice, uint64_t out[2]) {
    using namespace units::streamword;
    out[0] = out[1] = 0;
    if (advice > (1u << 22)) FATAL("advice values %u", advice);
    put128(out, SW_ADVICE, SW_ADVICE_BITS, advice);
    for (int k = 0; k < 3; k++) {
        put128(out, sw_words(k), 16, s[k].words);
        put128(out, sw_values(k), 17, s[k].values);
        put128(out, sw_flag(k), 1, s[k].compressed ? 1 : 0);
    }
}

struct ReplayWindow {
    uint32_t rows = 65536;
    uint64_t pc = 0;
    uint32_t instr_idx = 0, next_offset = 0;
    bool code_comp = false, rlog_comp = false, wlog_comp = false;
    bool store_log = true, rd_prev = true;
    uint32_t advice_values = 0;
    int64_t code_len = 0, rlog_len = 0, wlog_len = 0;
    uint32_t code_vals = 0, rlog_vals = 0, wlog_vals = 0;
    int64_t pad_seed = 0;
    uint64_t regs[128] = {};
    uint16_t table[2048];
    uint64_t rom_lo[512] = {}, rom_hi[512] = {};
    std::vector<uint64_t> code, rlog, wlog;
    std::map<uint32_t, std::vector<uint64_t>> templates;   // by ReplayTables.key
    std::map<uint32_t, uint32_t> template_rd0;
    std::vector<uint64_t> region0;                          // a delivered record's words 0-65, or empty
    int words_adj[3] = {}, values_adj[3] = {};

    ReplayWindow() { for (uint16_t& e : table) e = 3; }

    void use_tables(const units::tracer::ReplayTables& t) {
        for (int i = 0; i < 2048; i++) table[i] = t.table[i];
        for (int i = 0; i < 512; i++) { rom_lo[i] = t.rom_lo[i]; rom_hi[i] = t.rom_hi[i]; }
        templates.clear(); template_rd0.clear();
        for (int k = 0; k < t.count(); k++) { templates[t.key[size_t(k)]] = t.words[size_t(k)]; template_rd0[t.key[size_t(k)]] = t.rd0[size_t(k)]; }
    }
    uint64_t state_lo() const { return pc; }
    uint64_t state_hi_word() const { return state_hi(instr_idx, next_offset); }
    uint32_t run_config_word() const { return run_config(rows, store_log, rd_prev); }
    void stream_word_of(uint64_t out[2]) const {
        units::streamword::Size s[3] = {
            units::streamword::make(int64_t(code.size() / 2) + words_adj[0], int64_t(code_vals) + values_adj[0], code_comp),
            units::streamword::make(int64_t(rlog.size() / 2) + words_adj[1], int64_t(rlog_vals) + values_adj[1], rlog_comp),
            units::streamword::make(int64_t(wlog.size() / 2) + words_adj[2], int64_t(wlog_vals) + values_adj[2], wlog_comp)};
        stream_word(s, advice_values, out);
    }
    uint64_t stream_lo() const { uint64_t w[2]; stream_word_of(w); return w[0]; }
    uint64_t stream_hi() const { uint64_t w[2]; stream_word_of(w); return w[1]; }

    // ---- expansion words ----
    void rom(unsigned start, std::vector<W> words) {
        for (size_t i = 0; i < words.size(); i++) {
            words[i].seq(unsigned(words.size() - 1 - i));
            rom_lo[start + i] = words[i].lo; rom_hi[start + i] = words[i].hi;
        }
    }
    void fixed_rom() {
        W pad = W::op(0).rd(0).rs1(0).rs2(0).hold();
        rom_lo[0] = pad.lo; rom_hi[0] = pad.hi;
        W nop = W::op(units::alu::ADDI).rd(0).rs1(0).rs2(128).cnst(0).report_c();
        rom_lo[1] = nop.lo; rom_hi[1] = nop.hi;
    }
    void decode_rom(uint32_t w, unsigned start) { table[units::rowcore::decode_index(w)] = uint16_t(start << 2); }
    void decode_class(uint32_t w, unsigned cls) { table[units::rowcore::decode_index(w)] = uint16_t(cls); }
    void template_(uint32_t w, std::vector<W> words) {
        std::vector<uint64_t> t(2 * words.size());
        for (size_t i = 0; i < words.size(); i++) {
            words[i].seq(unsigned(words.size() - 1 - i));
            t[2 * i] = words[i].lo; t[2 * i + 1] = words[i].hi;
        }
        templates[units::tracer::ReplayTables::key_of(w)] = t;
    }

    // ---- the streams (4.19.15) ----
    void set_code(const std::vector<uint32_t>& words, bool compressed) {
        Bits b; Lru l(false);
        for (uint32_t w : words) { uint64_t v = w; if (compressed) put_value(b, l.code(v), v); else b.put(v, 32); }
        code = b.entries(pad_seed); code_len = b.n; code_comp = compressed; code_vals = uint32_t(words.size());
    }
    void set_logs(const std::vector<LogEvent>& ev, bool r_comp, bool w_comp) {
        Bits rb, wb; Lru rl(false), wl(true);
        rlog_vals = 0; wlog_vals = 0;
        for (const LogEvent& e : ev) {
            if (e.has_read) rlog_vals++;
            if (e.has_write) wlog_vals++;
            if (e.has_read) {
                if (r_comp) put_value(rb, rl.code(e.read), e.read); else rb.put(e.read, 64);
                if (e.load) wl.E[15] = e.read;
            }
            if (e.has_write) { if (w_comp) put_value(wb, wl.code(e.write), e.write); else wb.put(e.write, 64); }
        }
        rlog = rb.entries(pad_seed); rlog_len = rb.n; rlog_comp = r_comp;
        wlog = wb.entries(pad_seed + 1); wlog_len = wb.n; wlog_comp = w_comp;
    }
};

}  // namespace rtest
