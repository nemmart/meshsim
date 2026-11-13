// replay_shard: ref's test/ReplayShard.java, the acceptance kit's replayer. Every window
// of a delivery's program (or every S-th of a range, from the K-th), in Stage A (the replayer model fed by the ideal
// controller: tests/stage_a.h) and Stage B (replay_ctl.c on a tile: B tile 0 alone, F the full chip), each compared
// row for row with the reference rows, the streams' consumption and the end state. Before a window is replayed, every
// file its replay reads is checked against the manifest's SHA-256 (CORRUPT, not a mismatch); before any, the tables
// against the tables' manifest. Appends one CSV line per window and stage to CSV, ref's line exactly but for wall_ms:
//   program,window,first_row,stage:variant,verdict,rows,cycles,wall_ms,"detail"
// and prints Stage A's line per window (ref's stdout, the kit's jvm-K.log).
//
//   replay_shard DELIVERY PROGRAM CSV [--stages ABF] [--variant V] [--first F --count N] [--manifest M]
//                [--stride S --phase K] [--full-every K] [--tables DIR] [--replay-build DIR] [--norow N] [--logq Q]
// --tables: ref's -Dreplay.tables (the default tables, whose ../manifest.json the tables are checked against);
// --replay-build, --norow, --logq: ref's -Dreplay.build, -Dstageb.norow, -Dreplayer.logq (Stage B's).
// Exit 0 if every line is OK and there was at least one.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "sha256.h"
#include "stage_a.h"
#include "stageb.h"

namespace {

using stagea::fmt;

std::string sha256_file(const std::string& path, bool& ok) {
    std::vector<uint8_t> b;
    ok = stagea::read_file(path, b);
    return ok ? test::sha256_hex(b.data(), b.size()) : "";
}
// ReplayShard.q: the detail quoted, its quotes made apostrophes and its newlines spaces.
std::string q(std::string s) {
    for (char& c : s) { if (c == '"') c = '\''; else if (c == '\n') c = ' '; }
    return '"' + s + '"';
}
std::string java_string_list(const std::vector<std::string>& l) { return stagea::java_list(l); }

// ReplayTables.checkManifest: the four table files against the tables' manifest ("replayer/" prefix).
std::vector<std::string> check_tables(const std::string& dir, const std::string& manifest) {
    std::vector<std::string> bad;
    std::vector<uint8_t> b;
    if (!stagea::read_file(manifest, b)) FATAL("replay_shard: cannot read the tables' manifest %s", manifest.c_str());
    std::string text(b.begin(), b.end()), err;
    json::Ptr m = json::parse(text, err);
    if (!m || !err.empty()) FATAL("replay_shard: %s: %s", manifest.c_str(), err.c_str());
    json::Ptr files = m->get("files");
    for (const char* f : {"decode_table.bin", "rom.bin", "templates.bin", "template_index.bin"}) {
        json::Ptr e = files ? files->get(std::string("replayer/") + f) : nullptr;
        if (!e) { bad.push_back(std::string(f) + ": not in " + manifest); continue; }
        json::Ptr w = e->get("sha256");
        bool ok;
        std::string got = sha256_file(dir + "/" + f, ok);
        if (!ok) FATAL("replay_shard: cannot read %s/%s", dir.c_str(), f);
        std::string want = w ? w->s : "null";
        if (want != got) bad.push_back(std::string(f) + ": sha256 " + got + ", manifest " + want);
    }
    return bad;
}

// ReplayShard.checkFiles: the files a replay of variant v reads, against the window entry's "files" (and rows.bin also
// against rows_sha256); "" if all match.
std::string check_files(const std::string& pd, const std::string& base, const std::string& v, const stagea::Entry& we) {
    using units::Replayer;
    json::Ptr files = we.e->get("files");
    std::vector<std::string> names = {base + v + ".bin", base + "rows.bin"};
    if (stagea::exists(pd + "/" + base + "end.bin")) names.push_back(base + "end.bin");
    std::vector<uint8_t> rec;
    if (!stagea::read_file(pd + "/" + base + v + ".bin", rec)) FATAL("replay_shard: %s%s.bin: no such file (ref: NoSuchFileException)", base.c_str(), v.c_str());
    if (rec.size() != 16 * Replayer::REC_WORDS)
        return fmt("%s%s.bin: %zu bytes, not a v1.26 record (%u); convert the delivery (ConvertDelivery)", base.c_str(), v.c_str(), rec.size(), 16 * Replayer::REC_WORDS);
    units::streamword::Size sz[3];
    units::streamword::parse(stagea::le64(rec, 16), stagea::le64(rec, 24), sz);
    names.push_back(base + stagea::stream_file("code", sz[0].compressed));
    names.push_back(base + stagea::stream_file("rlog", sz[1].compressed));
    names.push_back(base + stagea::stream_file("wlog", sz[2].compressed));
    std::string bad;
    for (const std::string& f : names) {
        json::Ptr e = files ? files->get(f) : nullptr;
        if (!e && files) e = files->get(f.substr(base.size()));   // delivery_writer: keys without the window prefix
        std::string want;
        bool have = false;
        if (e && e->kind == json::Value::STRING) { want = e->s; have = true; }
        else if (e && e->kind == json::Value::OBJECT) { json::Ptr s = e->get("sha256"); if (s && s->kind == json::Value::STRING) { want = s->s; have = true; } }
        if (!have) { bad += f + ": no SHA-256 in the manifest; "; continue; }
        if (!stagea::exists(pd + "/" + f)) { bad += f + ": missing; "; continue; }
        bool ok;
        std::string got = sha256_file(pd + "/" + f, ok);
        if (want != got) bad += f + ": sha256 " + got.substr(0, 12) + ", manifest " + want.substr(0, std::min<size_t>(12, want.size())) + "; ";
        json::Ptr rs = we.e->get("rows_sha256");
        if (f.size() >= 8 && f.compare(f.size() - 8, 8, "rows.bin") == 0 && rs && !(rs->kind == json::Value::STRING && rs->s == got))
            bad += f + ": differs from rows_sha256; ";
    }
    return bad;
}

long long now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: replay_shard DELIVERY PROGRAM CSV [--stages ABF] [--variant V] [--first F --count N] [--manifest M] [--stride S --phase K]\n"
                             "                    [--full-every K] [--tables DIR] [--replay-build DIR] [--norow N] [--logq Q]\n");
        return 2;
    }
    std::string dir = argv[1], prog = argv[2], csv = argv[3], stages = "AB", variant, manifest, build = "tests/m3-images";
    long long stride = 1, phase = 0, first_w = 0, count_w = INT64_MAX, full_every = 0, norow = 50000, logq = 256;
    for (int i = 4; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) FATAL("replay_shard: %s needs a value", a.c_str()); return argv[++i]; };
        if (a == "--stages") stages = next();
        else if (a == "--variant") variant = next();
        else if (a == "--stride") stride = std::atoll(next().c_str());
        else if (a == "--phase") phase = std::atoll(next().c_str());
        else if (a == "--first") first_w = std::atoll(next().c_str());
        else if (a == "--count") count_w = std::atoll(next().c_str());
        else if (a == "--manifest") manifest = next();
        else if (a == "--full-every") full_every = std::atoll(next().c_str());
        else if (a == "--tables") setenv("SIM_REPLAY_TABLES", next().c_str(), 1);
        else if (a == "--replay-build") build = next();
        else if (a == "--norow") norow = std::atoll(next().c_str());
        else if (a == "--logq") logq = std::atoll(next().c_str());
        else FATAL("replay_shard: unknown argument %s", a.c_str());
    }
    if (manifest.empty()) manifest = stagea::exists(dir + "/manifest.json") ? dir + "/manifest.json" : dir + "/" + prog + ".manifest.json";
    std::vector<stagea::Program> progs;
    std::string err;
    if (!stagea::read_manifest(manifest, progs, err)) FATAL("replay_shard: %s", err.c_str());
    const std::string pd = dir + "/" + prog;
    const std::string tdir = stagea::tables_dir(dir, pd);
    auto tb = std::make_unique<units::tracer::ReplayTables>(units::tracer::ReplayTables::load(tdir));
    std::vector<std::string> bad_tables = check_tables(tdir, units::tracer::ReplayTables::default_dir() + "/../manifest.json");
    const bool want_b = stages.find('B') != std::string::npos || stages.find('F') != std::string::npos || full_every > 0;
    std::unique_ptr<mach::Mach::Image> im;
    if (want_b) im = std::make_unique<mach::Mach::Image>(stageb::image(build));
    stageb::Options bo;
    bo.norow = norow;
    bo.logq = unsigned(logq);

    FILE* out = std::fopen(csv.c_str(), "a");
    if (!out) FATAL("replay_shard: cannot append to %s", csv.c_str());
    long long n = 0, failed = 0;
    if (!bad_tables.empty()) {
        std::fprintf(out, "%s,-,-,tables,FAIL,0,0,0,%s\n", prog.c_str(), q("tables differ from wip/tables/manifest.json: " + java_string_list(bad_tables)).c_str());
        failed++;
    }
    for (const stagea::Program& p : progs) {
        if (p.name != prog) continue;
        long long k = 0;
        for (const stagea::Entry& we : p.windows) {
            if (we.index < first_w || we.index - first_w >= count_w || k++ % stride != phase) continue;
            const std::string base = stagea::window_base(we.index);
            std::string v = !variant.empty() ? variant : stagea::exists(pd + "/" + base + "state-pick.bin") ? "state-pick" : "state";
            std::string corrupt = check_files(pd, base, v, we);
            if (!corrupt.empty()) {
                std::fprintf(out, "%s,%lld,%lld,-,CORRUPT,0,0,0,%s\n", prog.c_str(), we.index, we.first_row, q(corrupt).c_str());
                std::fflush(out);
                failed++; n++;
                continue;
            }
            std::string these = full_every > 0 && we.index % full_every == 0 && stages.find('F') == std::string::npos ? stages + "F" : stages;
            for (char st : these) {
                long long t0 = now_ns();
                stagea::Result r;
                // B: Stage B on tile 0 alone (the 63 idle tiles parked, checked untouched); F: Stage B on the full chip
                bo.lone = st == 'B';
                if (st == 'A') r = stagea::run_window(*tb, pd, we, v, unsigned(logq));
                else r = stageb::run_window(*im, *tb, pd, we, v, prog, bo);
                long long ms = (now_ns() - t0) / 1000000;
                if (st == 'A') std::printf("%s\n", r.line(prog, v).c_str());
                std::string detail;
                if (r.ok) detail = r.summary;
                else for (size_t i = 0; i < r.detail.size(); i++) detail += (i ? " | " : "") + r.detail[i];
                std::fprintf(out, "%s,%lld,%lld,%c:%s,%s,%lld,%lld,%lld,%s\n", prog.c_str(), we.index, we.first_row, st, v.c_str(), r.ok ? "OK" : "FAIL", r.rows,
                             r.cycles, ms, q(detail).c_str());
                std::fflush(out);
                std::fflush(stdout);
                n++;
                if (!r.ok) failed++;
            }
        }
    }
    std::fclose(out);
    std::printf("ReplayShard %s %s phase %lld/%lld: %lld runs, %lld failed\n", prog.c_str(), stages.c_str(), phase, stride, n, failed);
    return failed == 0 && n > 0 ? 0 : 1;
}
