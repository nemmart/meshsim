// stage_a.h (WORKER25, P7b; inventory E4): Stage A's window as ref's ReplayDelivery runs it, shared by
// `replay_delivery` (W15's driver; its manifest mode) and `replay_shard` (the acceptance kit). Everything a window's
// result says is ref's text: the line (ReplayDelivery.Result.line), the summary (the streams, newexp, the stalls) and
// every detail line of a failing window (the row count, the first mismatching row with its fields, ref's describe and
// diag.bin lines and the two rows before it, the streams' consumption, the end state, the replayer's errorReport and
// the ideal controller's missing-template log). Also the delivery's reading (ref's tablesFor, the manifest's window
// entries) and consumedExactly, which Stage B shares. Configuration-regime test code (D6).
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../tools/delivery/json.h"
#include "replay_driver.h"
#include "replay_report.h"
#include "units/replay_tables.h"
#include "units/replayer.h"
#include "units/replayer_alu.h"
#include "units/stream_word.h"

namespace stagea {

using units::Replayer;

// ---------------------------------------------------------------- small helpers (Java's formats)
inline std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
inline std::string fmt(const char* f, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}
inline bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
inline bool exists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}
inline uint64_t le64(const std::vector<uint8_t>& b, size_t at) {
    uint64_t v = 0;
    for (int k = 7; k >= 0; k--) v = v << 8 | b[at + size_t(k)];
    return v;
}
inline uint32_t le32(const std::vector<uint8_t>& b, size_t at) { return uint32_t(le64(b, at) & 0xFFFFFFFF); }
inline std::vector<uint64_t> words(const std::vector<uint8_t>& b) {
    std::vector<uint64_t> r(b.size() / 8);
    for (size_t i = 0; i < r.size(); i++) r[i] = le64(b, 8 * i);
    return r;
}
// ReplayDelivery.streamFile: raw, or its LRU form.
inline std::string stream_file(const char* s, bool compressed) { return std::string(s) + (compressed ? "-lru" : "") + ".bin"; }
inline std::string window_base(long long index) { return fmt("window_%05lld.", index); }
// ReplayerAlu.name: the name, or "op" + op.
inline std::string op_name(long long op) {
    const char* n = op >= 0 && op < 256 ? units::alu::name_or_null(unsigned(op)) : nullptr;
    return n ? n : "op" + std::to_string(op);
}
// List.toString of strings.
inline std::string java_list(const std::vector<std::string>& l) {
    std::string s = "[";
    for (size_t i = 0; i < l.size(); i++) { if (i) s += ", "; s += l[i]; }
    return s + "]";
}
// Arrays.toString of the stalls.
inline std::string stalls_string(const Replayer& r) {
    std::string s = "[";
    for (unsigned k = 0; k < Replayer::N_STALL; k++) s += fmt("%s%llu", k ? ", " : "", (unsigned long long)r.stalls[k]);
    return s + "]";
}

// ---------------------------------------------------------------- the delivery
// ReplayDelivery.tablesFor: the program directory's own tables, else the delivery's tables/, else the default
// (ReplayTables::default_dir: $SIM_REPLAY_TABLES, else data/tables/replayer; ref's -Dreplay.tables).
inline std::string tables_dir(const std::string& delivery, const std::string& pd) {
    if (exists(pd + "/decode_table.bin")) return pd;
    if (!delivery.empty() && exists(delivery + "/tables/decode_table.bin")) return delivery + "/tables";
    return units::tracer::ReplayTables::default_dir();
}

// One window entry of a manifest's program (only those with "files" are delivered windows).
struct Entry {
    long long index = 0, rows = 0, first_row = 0;
    json::Ptr e;   // the whole entry (its files, rows_sha256)
};
struct Program {
    std::string name;
    std::vector<Entry> windows;   // the manifest's order, delivered windows only
};
inline long long num(const json::Ptr& v) { return !v ? 0 : v->kind == json::Value::DOUBLE ? (long long)v->d : (long long)v->l; }
// The manifest's programs, in its order. Returns false (and err) if it can't be read.
inline bool read_manifest(const std::string& path, std::vector<Program>& out, std::string& err) {
    std::vector<uint8_t> b;
    if (!read_file(path, b)) { err = "cannot read " + path; return false; }
    std::string text(b.begin(), b.end());
    json::Ptr m = json::parse(text, err);
    if (!m || !err.empty()) { if (err.empty()) err = "bad JSON"; return false; }
    json::Ptr ps = m->get("programs");
    if (!ps) { err = path + ": no programs"; return false; }
    for (auto& p : ps->list) {
        Program pr;
        json::Ptr n = p->get("name");
        pr.name = n ? n->s : "";
        json::Ptr we = p->get("window_entries");
        if (we)
            for (auto& e : we->list) {
                if (!e->has("files")) continue;
                pr.windows.push_back(Entry{num(e->get("index")), num(e->get("rows")), num(e->get("first_row")), e});
            }
        out.push_back(std::move(pr));
    }
    return true;
}

// ---------------------------------------------------------------- one window's result
struct Result {
    bool ok = true;
    long long rows = 0, index = 0, cycles = 0;
    std::string summary;
    std::vector<std::string> detail;
    // ReplayDelivery.Result.line.
    std::string line(const std::string& prog, const std::string& variant) const {
        return fmt("%-12s window %5lld  %-10s %s  %6lld rows  %8lld cycles  %s", prog.c_str(), index, variant.c_str(), ok ? "OK  " : "FAIL", rows,
                   cycles, summary.c_str());
    }
};

// ReplayDelivery.consumedExactly: every stream's values popped and its words reached exactly (the write log only with
// the store log), the files' entries equal to the stream word's words; "" if so.
inline std::string consumed_exactly(Replayer& r, const units::streamword::Size* sz, uint64_t code_e, uint64_t rlog_e, uint64_t wlog_e) {
    std::string b;
    uint64_t files[3] = {code_e, rlog_e, wlog_e};
    const char* names[3] = {"code", "read log", "write log"};
    const units::ReplayerStream* st[3] = {&r.code, &r.rlog, &r.wlog};
    for (int k = 0; k < 3; k++) {
        if (files[k] != sz[k].words) b += fmt("%s: the file has %llu entries, the stream word %u words; ", names[k], (unsigned long long)files[k], sz[k].words);
        if (k == 2 && !r.store_log_latched()) continue;
        if (st[k]->taken != sz[k].values) b += fmt("%s: %u of %u values; ", names[k], st[k]->taken, sz[k].values);
        long long used = (long long)(uint64_t(st[k]->end_bits + 127) >> 7);
        if (used != (long long)sz[k].words) b += fmt("%s: ended in word %lld of %u; ", names[k], used, sz[k].words);
    }
    return b.empty() ? b : "streams not consumed exactly: " + b;
}

// ReplayDelivery.fields and describe: a row's 21 fields and ref's one-line form.
inline void fields(const uint64_t* g, uint64_t* f) {
    f[0] = g[0] & 0xFFFFFFFF; f[1] = (g[0] >> 32) & 0xFF; f[2] = (g[0] >> 40) & 0xFF; f[3] = (g[0] >> 48) & 0xFF; f[4] = g[0] >> 56;
    f[5] = g[1] & 0xFFFF; f[6] = (g[1] >> 16) & 1; f[7] = (g[1] >> 17) & 1; f[8] = (g[1] >> 18) & 1; f[9] = (g[1] >> 19) & 1;
    f[10] = (g[1] >> 20) & 1; f[11] = (g[1] >> 21) & 1; f[12] = g[1] >> 22;
    for (int k = 2; k < 10; k++) f[11 + k] = g[k];
}
inline std::string describe(const uint64_t* f) {
    auto u = [](uint64_t x) { return (unsigned long long)x; };
    return fmt("idx %lld %s rd %lld rs1 %lld rs2 %lld seq %lld c%lld v%lld f%lld imm%lld%lld%lld pc 0x%llx imm 0x%llx rs1 0x%llx rs2 0x%llx rd_prev 0x%llx rd 0x%llx mem 0x%llx/0x%llx",
               (long long)f[0], op_name((long long)f[1]).c_str(), (long long)f[2], (long long)f[3], (long long)f[4], (long long)f[5], (long long)f[6],
               (long long)f[7], (long long)f[8], (long long)f[9], (long long)f[10], (long long)f[11], u(f[13]), u(f[14]), u(f[15]), u(f[16]), u(f[17]),
               u(f[18]), u(f[19]), u(f[20]));
}
inline const char* const kFields[21] = {"instr_idx", "opcode", "rd_reg", "rs1_reg", "rs2_reg", "seq_remaining", "is_compressed", "is_virtual",
                                         "is_first", "rd_is_imm", "rs1_is_imm", "rs2_is_imm", "spare", "pc", "imm", "rs1", "rs2", "rd_prev",
                                         "rd", "mem_addr", "mem_value"};

// Stage A's replayer keeps this many entries per error list (ref prints them whole in errorReport).
constexpr unsigned kListCapacity = 4096;

// ReplayDelivery.runWindow: one delivered window through the ideal controller. A record that isn't a v1.26 record is
// FATAL, as ref's checkRecord throws (the kit checks every file's hash and size first).
inline Result run_window(const units::tracer::ReplayTables& tables, const std::string& pd, const Entry& we, const std::string& variant) {
    Result res;
    res.index = we.index;
    const long long rows = we.rows;
    const std::string base = window_base(we.index);
    std::vector<uint8_t> rec, b;
    if (!read_file(pd + "/" + base + variant + ".bin", rec)) FATAL("%s%s.bin: cannot read it", base.c_str(), variant.c_str());
    if (rec.size() != 16 * Replayer::REC_WORDS)
        FATAL("%s%s.bin: %zu bytes, not a v1.26 state record (%u): convert a v1.25 delivery with ConvertDelivery", base.c_str(), variant.c_str(), rec.size(),
              16 * Replayer::REC_WORDS);
    auto w = std::make_unique<rtest::ReplayWindow>();
    w->rows = uint32_t(rows);
    w->use_tables(tables);
    w->region0.resize(2 * Replayer::PART0_WORDS);
    for (size_t i = 0; i < w->region0.size(); i++) w->region0[i] = le64(rec, 8 * i);
    units::streamword::Size sz[3];
    units::streamword::parse(w->region0[2], w->region0[3], sz);
    const char* names[3] = {"code", "rlog", "wlog"};
    std::vector<uint64_t>* dst[3] = {&w->code, &w->rlog, &w->wlog};
    for (int k = 0; k < 3; k++) {
        if (!read_file(pd + "/" + base + stream_file(names[k], sz[k].compressed), b)) FATAL("%s%s: cannot read it", base.c_str(), stream_file(names[k], sz[k].compressed).c_str());
        *dst[k] = words(b);
    }
    res.summary = std::string("streams ") + (sz[0].compressed ? "C" : "r") + (sz[1].compressed ? "C" : "r") + (sz[2].compressed ? "C" : "r");

    auto rp = std::make_unique<Replayer>(0, Replayer::LOG_QUEUE_ENTRIES, kListCapacity);
    auto d = std::make_unique<rtest::ReplayDriver>(*w, uint64_t(rows) + 16, rp.get());
    d->load();
    d->pass(40 * rows + 100000);
    res.cycles = d->r->pass_end_at - d->r->pass_start_at;

    // ---- the rows, byte for byte
    std::vector<uint8_t> ref;
    read_file(pd + "/" + base + "rows.bin", ref);
    const long long nref = (long long)(ref.size() / 80), got = (long long)d->rows();
    res.rows = nref < got ? nref : got;
    if (got != nref) { res.ok = false; res.detail.push_back(fmt("row count: model %lld, reference %lld", got, nref)); }
    std::vector<uint8_t> diag;
    const bool has_diag = read_file(pd + "/" + base + "diag.bin", diag);
    for (long long r = 0; r < res.rows; r++) {
        const uint64_t* g = d->row(uint64_t(r));
        bool same = true;
        for (unsigned k = 0; k < 10; k++) if (g[k] != le64(ref, size_t(80 * r + 8 * k))) { same = false; break; }
        if (same) continue;
        uint64_t e[10], gf[21], ef[21];
        for (unsigned k = 0; k < 10; k++) e[k] = le64(ref, size_t(80 * r + 8 * k));
        res.ok = false;
        res.detail.push_back(fmt("first mismatching row %lld (trace row %lld)", r, we.first_row + r));
        fields(g, gf);
        fields(e, ef);
        for (int f = 0; f < 21; f++)
            if (gf[f] != ef[f]) res.detail.push_back(fmt("  %-14s model 0x%llx, reference 0x%llx", kFields[f], (unsigned long long)gf[f], (unsigned long long)ef[f]));
        res.detail.push_back("  reference row: " + describe(ef));
        res.detail.push_back("  model row:     " + describe(gf));
        if (has_diag) {
            int32_t c = int32_t(le32(diag, size_t(16 * r))), lg = int32_t(le32(diag, size_t(16 * r + 4))), de = int32_t(le32(diag, size_t(16 * r + 8))),
                    fl = int32_t(le32(diag, size_t(16 * r + 12)));
            const char* kinds[4] = {"none", "read", "write", "?"};
            uint32_t ude = uint32_t(de);
            std::string ctl = (ude >> 16) == 0xFFFF ? "-" : std::to_string(ude >> 16);
            res.detail.push_back(fmt("  diag: code word %d, log %s %d, decode entry %d, controller number %s, flags 0x%x (log kind %d)", c, kinds[(uint32_t(lg) >> 30) & 3],
                                     lg & 0x3FFFFFFF, de & 0xFFFF, ctl.c_str(), fl & 0xFF, (fl >> 8) & 0xFF));
        }
        for (long long q = r - 2 < 0 ? 0 : r - 2; q < r; q++) {
            uint64_t qf[21];
            fields(d->row(uint64_t(q)), qf);
            res.detail.push_back(fmt("  row %lld (matched): ", q) + describe(qf));
        }
        break;
    }
    // ---- the streams consumed exactly, the end state, the replayer clean
    if (res.ok) {
        std::string sc = consumed_exactly(*d->r, sz, w->code.size() / 2, w->rlog.size() / 2, w->wlog.size() / 2);
        if (!sc.empty()) { res.ok = false; res.detail.push_back(sc); }
        std::vector<uint8_t> end;
        if (read_file(pd + "/" + base + "end.bin", end)) {
            if (end.size() != 16 * Replayer::REC_WORDS)
                FATAL("%send.bin: %zu bytes, not a v1.26 state record (%u): convert a v1.25 delivery with ConvertDelivery", base.c_str(), end.size(), 16 * Replayer::REC_WORDS);
            uint64_t epc = le64(end, 0), ehi = le64(end, 8);
            if (d->r->pc() != epc || d->r->instr_idx() != (ehi & 0xFFFFFFFF) || d->r->next_offset_now() != ((ehi >> 32) & 0xFFFF)) {
                res.ok = false;
                res.detail.push_back(fmt("end state: model pc 0x%llx idx %d offset %u; end.bin pc 0x%llx idx %llu offset %llu", (unsigned long long)d->r->pc(),
                                         int32_t(d->r->instr_idx()), d->r->next_offset_now(), (unsigned long long)epc, (unsigned long long)(ehi & 0xFFFFFFFF),
                                         (unsigned long long)((ehi >> 32) & 0xFFFF)));
            }
            for (unsigned i = 1; i < 128; i++) {
                uint64_t ev = le64(end, 32 + 8 * i);
                if (d->r->reg(i) != ev) {
                    res.ok = false;
                    res.detail.push_back(fmt("end state: register %u model 0x%llx, end.bin 0x%llx", i, (unsigned long long)d->r->reg(i), (unsigned long long)ev));
                    break;
                }
            }
        }
        if (!d->r->clean() || d->missing_templates > 0) {
            res.ok = false;
            res.detail.push_back("replayer: " + rreport::error_report(*d->r) + ", missing templates " + java_list(d->log));
        }
    }
    res.summary += fmt("  newexp %llu, stalls ", (unsigned long long)d->r->new_expansions) + stalls_string(*d->r);
    return res;
}

}  // namespace stagea
