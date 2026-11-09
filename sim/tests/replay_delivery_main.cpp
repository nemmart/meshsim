// replay_delivery: Stage A on the C++ replayer (WORKER15): ref's test/ReplayDelivery.java runWindow, window for window.
// For each window of a (v1.26) delivery: the program's tables, one of the window's state records and the streams its
// stream word names, one pass through ReplayDriver (the ideal controller, the peripheral interface only), every record
// compared with rows.bin byte for byte, every stream consumed exactly, the end state against end.bin, the replayer clean.
// Each window's line is ref's line exactly (rows, the pass's cycles, the streams, new expansions, the stalls), so the
// two drivers' outputs compare with `cmp`.
//
//   replay_delivery LIST VARIANT [--keep-going] [--every K]
//     LIST: ReplayConf windows' output ("PROGRAM_DIR TABLES_DIR INDEX ROWS FIRST_ROW" per line); VARIANT state,
//     state-lru or state-pick; --every K: every K-th window only (and ref with the same list).
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "replay_driver.h"

using units::Replayer;

namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
uint64_t le64(const std::vector<uint8_t>& b, size_t at) {
    uint64_t v = 0;
    for (int k = 7; k >= 0; k--) v = v << 8 | b[at + size_t(k)];
    return v;
}
std::vector<uint64_t> words(const std::vector<uint8_t>& b) {
    std::vector<uint64_t> r(b.size() / 8);
    for (size_t i = 0; i < r.size(); i++) r[i] = le64(b, 8 * i);
    return r;
}
std::string stream_file(const char* s, bool comp) { return std::string(s) + (comp ? "-lru" : "") + ".bin"; }

struct Result {
    bool ok = true;
    uint64_t rows = 0;
    int64_t cycles = 0;
    std::string summary;
    std::vector<std::string> detail;
};

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char b[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

Result run_window(const units::tracer::ReplayTables& tables, const std::string& pd, unsigned index, uint32_t rows, const std::string& variant,
                  std::vector<uint64_t>& rowbuf_owner) {
    (void)rowbuf_owner;
    Result res;
    char base[32];
    std::snprintf(base, sizeof base, "window_%05u.", index);
    std::vector<uint8_t> rec, b;
    if (!read_file(pd + "/" + base + variant + ".bin", rec)) { res.ok = false; res.detail.push_back("no record"); return res; }
    if (rec.size() != 16 * Replayer::REC_WORDS) FATAL("%s%s.bin: %zu bytes, not a v1.26 state record (convert a v1.25 delivery with ConvertDelivery)", base, variant.c_str(), rec.size());
    auto w = std::make_unique<rtest::ReplayWindow>();
    w->rows = rows;
    w->use_tables(tables);
    w->region0.resize(2 * Replayer::PART0_WORDS);
    for (size_t i = 0; i < w->region0.size(); i++) w->region0[i] = le64(rec, 8 * i);
    units::streamword::Size sz[3];
    units::streamword::parse(w->region0[2], w->region0[3], sz);
    const char* names[3] = {"code", "rlog", "wlog"};
    std::vector<uint64_t>* dst[3] = {&w->code, &w->rlog, &w->wlog};
    for (int k = 0; k < 3; k++) {
        if (!read_file(pd + "/" + base + stream_file(names[k], sz[k].compressed), b)) { res.ok = false; res.detail.push_back(std::string("no ") + names[k]); return res; }
        *dst[k] = words(b);
    }
    res.summary = std::string("streams ") + (sz[0].compressed ? "C" : "r") + (sz[1].compressed ? "C" : "r") + (sz[2].compressed ? "C" : "r");

    auto d = std::make_unique<rtest::ReplayDriver>(*w, uint64_t(rows) + 16);
    d->load();
    d->pass(40 * int64_t(rows) + 100000);
    res.cycles = d->r->pass_end_at - d->r->pass_start_at;

    // ---- the rows, byte for byte
    std::vector<uint8_t> ref;
    read_file(pd + "/" + base + "rows.bin", ref);
    uint64_t nref = ref.size() / 80;
    res.rows = std::min<uint64_t>(nref, d->rows());
    if (d->rows() != nref) { res.ok = false; res.detail.push_back(fmt("row count: model %llu, reference %llu", (unsigned long long)d->rows(), (unsigned long long)nref)); }
    for (uint64_t r = 0; r < res.rows; r++) {
        const uint64_t* g = d->row(r);
        bool same = true;
        for (unsigned k = 0; k < 10; k++) if (g[k] != le64(ref, 80 * r + 8 * k)) { same = false; break; }
        if (same) continue;
        res.ok = false;
        res.detail.push_back(fmt("first mismatching row %llu", (unsigned long long)r));
        for (unsigned k = 0; k < 10; k++)
            if (g[k] != le64(ref, 80 * r + 8 * k))
                res.detail.push_back(fmt("  word %u model 0x%llx, reference 0x%llx", k, (unsigned long long)g[k], (unsigned long long)le64(ref, 80 * r + 8 * k)));
        break;
    }
    // ---- the streams consumed exactly, the end state, the replayer clean
    if (res.ok) {
        std::string sc;
        uint64_t files[3] = {w->code.size() / 2, w->rlog.size() / 2, w->wlog.size() / 2};
        const units::ReplayerStream* st[3] = {&d->r->code, &d->r->rlog, &d->r->wlog};
        for (int k = 0; k < 3; k++) {
            if (files[k] != sz[k].words) sc += fmt("%s: the file has %llu entries, the stream word %u words; ", names[k], (unsigned long long)files[k], sz[k].words);
            if (k == 2 && !d->r->store_log_latched()) continue;
            if (st[k]->taken != sz[k].values) sc += fmt("%s: %u of %u values; ", names[k], st[k]->taken, sz[k].values);
            if (uint64_t(st[k]->end_bits + 127) >> 7 != sz[k].words) sc += fmt("%s: ended in word %llu of %u; ", names[k], (unsigned long long)(uint64_t(st[k]->end_bits + 127) >> 7), sz[k].words);
        }
        if (!sc.empty()) { res.ok = false; res.detail.push_back("streams not consumed exactly: " + sc); }
        std::vector<uint8_t> end;
        if (read_file(pd + "/" + base + "end.bin", end)) {
            if (end.size() != 16 * Replayer::REC_WORDS) FATAL("%send.bin: not a v1.26 record", base);
            uint64_t epc = le64(end, 0), ehi = le64(end, 8);
            if (d->r->pc() != epc || d->r->instr_idx() != (ehi & 0xFFFFFFFF) || d->r->next_offset_now() != ((ehi >> 32) & 0xFFFF)) {
                res.ok = false;
                res.detail.push_back(fmt("end state: model pc 0x%llx idx %u offset %u; end.bin pc 0x%llx idx %llu offset %llu", (unsigned long long)d->r->pc(),
                                         d->r->instr_idx(), d->r->next_offset_now(), (unsigned long long)epc, (unsigned long long)(ehi & 0xFFFFFFFF),
                                         (unsigned long long)((ehi >> 32) & 0xFFFF)));
            }
            for (unsigned i = 1; i < 128; i++) {
                uint64_t ev = le64(end, 32 + 8 * i);
                if (d->r->reg(i) != ev) { res.ok = false; res.detail.push_back(fmt("end state: register %u model 0x%llx, end.bin 0x%llx", i, (unsigned long long)d->r->reg(i), (unsigned long long)ev)); break; }
            }
        }
        if (!d->r->clean() || d->missing_templates > 0) {
            res.ok = false;
            std::string facts;
            char t[200];
            for (unsigned i = 0; i < d->r->fact_shown(); i++) { Replayer::fact_text(d->r->fact(i), t, sizeof t); facts += t; facts += "; "; }
            res.detail.push_back(fmt("replayer: %s missing templates %llu", facts.c_str(), (unsigned long long)d->missing_templates));
        }
    }
    res.summary += fmt("  newexp %llu, stalls [", (unsigned long long)d->r->new_expansions);
    for (unsigned k = 0; k < Replayer::N_STALL; k++) res.summary += fmt("%s%llu", k ? ", " : "", (unsigned long long)d->r->stalls[k]);
    res.summary += "]";
    return res;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: replay_delivery LIST VARIANT [--keep-going] [--every K]\n"); return 2; }
    std::string variant = argv[2];
    bool keep_going = false;
    unsigned every = 1;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--keep-going")) keep_going = true;
        else if (!std::strcmp(argv[i], "--every") && i + 1 < argc) every = unsigned(std::atoi(argv[++i]));
        else { std::fprintf(stderr, "replay_delivery: unknown argument %s\n", argv[i]); return 2; }
    }
    FILE* lf = std::fopen(argv[1], "r");
    if (!lf) { std::fprintf(stderr, "replay_delivery: cannot read %s\n", argv[1]); return 2; }
    std::map<std::string, std::unique_ptr<units::tracer::ReplayTables>> tables;
    char pd[4096], td[4096];
    unsigned index, rows;
    unsigned long long first;
    uint64_t windows = 0, failed = 0, compared = 0, seen = 0;
    std::vector<uint64_t> owner;
    while (std::fscanf(lf, "%4095s %4095s %u %u %llu", pd, td, &index, &rows, &first) == 5) {
        if (seen++ % every != 0) continue;
        auto& t = tables[td];
        if (!t) t = std::make_unique<units::tracer::ReplayTables>(units::tracer::ReplayTables::load(td));
        std::string prog = pd;
        prog = prog.substr(prog.rfind('/') + 1);
        windows++;
        Result r = run_window(*t, pd, index, rows, variant, owner);
        compared += r.rows;
        std::printf("%-12s window %5u  %-10s %s  %6llu rows  %8lld cycles  %s\n", prog.c_str(), index, variant.c_str(), r.ok ? "OK  " : "FAIL",
                    (unsigned long long)r.rows, (long long)r.cycles, r.summary.c_str());
        if (!r.ok) {
            failed++;
            for (const std::string& s : r.detail) std::printf("    %s\n", s.c_str());
            if (!keep_going) break;
        }
        std::fflush(stdout);
    }
    std::fclose(lf);
    std::printf("%s: %llu windows, %llu rows compared, %llu failed\n", variant.c_str(), (unsigned long long)windows, (unsigned long long)compared, (unsigned long long)failed);
    return failed == 0 ? 0 : 1;
}
