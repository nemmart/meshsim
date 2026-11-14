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

int manifest_mode(int argc, char** argv) {
    std::string dir = argv[1], variant = argv[2];
    bool keep_going = false;
    std::vector<std::string> only;
    for (int i = 3; i < argc; i++) { if (!std::strcmp(argv[i], "--keep-going")) keep_going = true; else only.push_back(argv[i]); }
    std::vector<stagea::Program> progs;
    std::string err;
    if (!stagea::read_manifest(dir + "/manifest.json", progs, err)) { std::fprintf(stderr, "replay_delivery: %s\n", err.c_str()); return 2; }
    std::map<std::string, std::unique_ptr<Tables>> cache;
    long long windows = 0, failed = 0, compared = 0;
    for (const stagea::Program& p : progs) {
        if (!only.empty() && std::find(only.begin(), only.end(), p.name) == only.end()) continue;
        std::string pd = dir + "/" + p.name;
        const Tables* t = tables_at(cache, stagea::tables_dir(dir, pd));
        for (const stagea::Entry& we : p.windows) {
            windows++;
            stagea::Result r = stagea::run_window(*t, pd, we, variant);
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
        stagea::Result r = stagea::run_window(*t, pd, stagea::Entry{index, rows, (long long)first, nullptr}, variant);
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
