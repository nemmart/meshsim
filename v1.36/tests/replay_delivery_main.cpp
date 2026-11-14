// replay_delivery: Stage A on the C++ replayer (the manifest mode and ref's detail lines): ref's
// test/ReplayDelivery.java, window for window. For each window of a (v1.26) delivery: the program's tables, one of the
// window's state records and the streams its stream word names, one pass through ReplayDriver (the ideal controller,
// the peripheral interface only), every record compared with rows.bin byte for byte, every stream consumed exactly,
// the end state against end.bin, the replayer clean. Every line is ref's (tests/stage_a.h), so the two drivers' outputs
// compare with `cmp`.
//
//   replay_delivery DELIVERY VARIANT [PROGRAM ...] [--keep-going]
//     ref's `java ReplayDelivery DELIVERY VARIANT [PROGRAM ...] [--keep-going]`: DELIVERY/manifest.json read here, every
//     delivered window of each program (all, or those named) in the manifest's order; stops at the first failing
//     window unless --keep-going ("STOPPED at the first failing window", exit 1)
//   replay_delivery LIST VARIANT [--keep-going] [--every K]
//     the form: LIST is ReplayConf windows' output ("PROGRAM_DIR TABLES_DIR INDEX ROWS FIRST_ROW" per line);
//     --every K: every K-th window only (and ref with the same list)
// VARIANT: state, state-lru or state-pick.
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "stage_a.h"

namespace {

using Tables = units::tracer::ReplayTables;

bool is_dir(const char* p) {
    struct stat s;
    return stat(p, &s) == 0 && S_ISDIR(s.st_mode);
}

const Tables* tables_at(std::map<std::string, std::unique_ptr<Tables>>& cache, const std::string& dir) {
    auto& t = cache[dir];
    if (!t) t = std::make_unique<Tables>(Tables::load(dir));
    return t.get();
}

// The RTL's reference of a program's windows (tests/stage_a.h, RtlReference): $REPLAY_RTL/<program>.txt, by default
// tests/acceptance/rtl/<program>.txt; null where the RTL has not run the program (a synthetic delivery).
const stagea::RtlReference* rtl_for(std::map<std::string, std::unique_ptr<stagea::RtlReference>>& cache, const std::string& prog) {
    auto it = cache.find(prog);
    if (it == cache.end()) {
        const char* dir = std::getenv("REPLAY_RTL");
        auto r = std::make_unique<stagea::RtlReference>();
        if (!r->load(std::string(dir ? dir : "tests/acceptance/rtl") + "/" + prog + ".txt")) r.reset();
        it = cache.emplace(prog, std::move(r)).first;
    }
    return it->second.get();
}

// The run's figures, as the RTL's acceptance prints its own: the cycles, the rows' schedules, and the cycles past
// them by the kind of row.
struct Figures {
    long long windows = 0, rows = 0, cycles = 0, equal = 0, with_reference = 0;
    uint64_t schedule = 0, short_rows = 0, rows_of[stagea::kStallKinds] = {}, stalled[stagea::kStallKinds] = {}, cycles_of[stagea::kStallKinds] = {};
    void add(const stagea::Result& r, const stagea::RtlReference* rtl, const std::string& variant) {
        windows++; rows += r.rows; cycles += r.cycles; schedule += r.schedule; short_rows += r.short_rows;
        for (unsigned k = 0; k < stagea::kStallKinds; k++) { rows_of[k] += r.stall_rows[k]; stalled[k] += r.stalled_rows[k]; cycles_of[k] += r.stall_cycles[k]; }
        const stagea::RtlReference::W* w = rtl ? rtl->find(variant, r.index) : nullptr;
        if (w) { with_reference++; if (w->cycles == r.cycles && w->hash == r.left_hash) equal++; }
    }
    void print(const std::string& variant) const {
        uint64_t past = 0;
        for (unsigned k = 0; k < stagea::kStallKinds; k++) past += cycles_of[k];
        std::printf("%s: %lld cycles, %.4f a row; the rows' schedules %llu; past them %llu (%llu rows took fewer than theirs)\n", variant.c_str(), cycles,
                    rows ? double(cycles) / double(rows) : 0.0, (unsigned long long)schedule, (unsigned long long)past, (unsigned long long)short_rows);
        for (unsigned k = 0; k < stagea::kStallKinds; k++)
            std::printf("%s:   %s: %llu rows, %llu of them past their schedules, %llu cycles\n", variant.c_str(), stagea::kStallKind[k],
                        (unsigned long long)rows_of[k], (unsigned long long)stalled[k], (unsigned long long)cycles_of[k]);
        std::printf("%s: %lld of %lld windows equal to the RTL's in their cycles and in every row's leaving (%lld with an RTL reference)\n", variant.c_str(), equal,
                    windows, with_reference);
    }
};

int manifest_mode(int argc, char** argv) {
    std::string dir = argv[1], variant = argv[2];
    bool keep_going = false;
    std::vector<std::string> only;
    for (int i = 3; i < argc; i++) { if (!std::strcmp(argv[i], "--keep-going")) keep_going = true; else only.push_back(argv[i]); }
    std::vector<stagea::Program> progs;
    std::string err;
    if (!stagea::read_manifest(dir + "/manifest.json", progs, err)) { std::fprintf(stderr, "replay_delivery: %s\n", err.c_str()); return 2; }
    std::map<std::string, std::unique_ptr<Tables>> cache;
    std::map<std::string, std::unique_ptr<stagea::RtlReference>> rtls;
    Figures fig;
    long long windows = 0, failed = 0, compared = 0;
    for (const stagea::Program& p : progs) {
        if (!only.empty() && std::find(only.begin(), only.end(), p.name) == only.end()) continue;
        std::string pd = dir + "/" + p.name;
        const Tables* t = tables_at(cache, stagea::tables_dir(dir, pd));
        for (const stagea::Entry& we : p.windows) {
            windows++;
            const stagea::RtlReference* rtl = rtl_for(rtls, p.name);
            stagea::Result r = stagea::run_window(*t, pd, we, variant, units::Replayer::LOG_QUEUE_ENTRIES, rtl);
            fig.add(r, rtl, variant);
            compared += r.rows;
            std::printf("%s\n", r.line(p.name, variant).c_str());
            if (!r.ok) {
                failed++;
                for (const std::string& s : r.detail) std::printf("    %s\n", s.c_str());
                if (!keep_going) { std::printf("STOPPED at the first failing window\n"); return 1; }
            }
            std::fflush(stdout);
        }
    }
    fig.print(variant);
    std::printf("%s: %lld windows, %lld rows compared, %lld failed\n", variant.c_str(), windows, compared, failed);
    return failed == 0 ? 0 : 1;
}

int list_mode(int argc, char** argv) {
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
    std::map<std::string, std::unique_ptr<Tables>> cache;
    std::map<std::string, std::unique_ptr<stagea::RtlReference>> rtls;
    Figures fig;
    char pd[4096], td[4096];
    unsigned index, rows;
    unsigned long long first;
    uint64_t windows = 0, failed = 0, compared = 0, seen = 0;
    while (std::fscanf(lf, "%4095s %4095s %u %u %llu", pd, td, &index, &rows, &first) == 5) {
        if (seen++ % every != 0) continue;
        const Tables* t = tables_at(cache, td);
        std::string prog = pd;
        prog = prog.substr(prog.rfind('/') + 1);
        windows++;
        const stagea::RtlReference* rtl = rtl_for(rtls, prog);
        stagea::Result r = stagea::run_window(*t, pd, stagea::Entry{index, rows, (long long)first, nullptr}, variant, units::Replayer::LOG_QUEUE_ENTRIES, rtl);
        fig.add(r, rtl, variant);
        compared += uint64_t(r.rows);
        std::printf("%s\n", r.line(prog, variant).c_str());
        if (!r.ok) {
            failed++;
            for (const std::string& s : r.detail) std::printf("    %s\n", s.c_str());
            if (!keep_going) break;
        }
        std::fflush(stdout);
    }
    std::fclose(lf);
    fig.print(variant);
    std::printf("%s: %llu windows, %llu rows compared, %llu failed\n", variant.c_str(), (unsigned long long)windows, (unsigned long long)compared,
                (unsigned long long)failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: replay_delivery DELIVERY VARIANT [PROGRAM ...] [--keep-going]\n       replay_delivery LIST VARIANT [--keep-going] [--every K]\n");
        return 2;
    }
    return is_dir(argv[1]) ? manifest_mode(argc, argv) : list_mode(argc, argv);
}
