// test_tracer_parts.cpp (WORKER14): the Tracer's pure pieces against `ref`.
//   - RVC: all 2^16 encodings against `ref`'s Rvc (tests/tracerparts/rvc.txt.gz);
//   - LruCoder and its packer: seeded streams, every coded value's bits and every packed word against `ref`'s
//     (lru-sN.txt.gz);
//   - FileStore and MemSystem: an image built by FileStore's operations, then seeded fill-bus reads and writes, every
//     status, ready cycle, data hash, acknowledgement and statistic against `ref`'s (mem-sN.txt.gz);
//   - MemorySystemTest's own cases, ported with their expected values;
//   - the replay tables: sizes, the manifest's hashes, the index's keys.
// The fixtures come from tests/ref/TracerParts.java (tests/ref/tracerparts.sh --fixtures; --check regenerates them).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "harness.h"
#include "portconf.h"
#include "sha256.h"
#include "units/file_store.h"
#include "units/lru_coder.h"
#include "units/mem_system.h"
#include "units/replay_tables.h"
#include "units/rvc.h"

using units::tracer::Bits;
using units::tracer::FileStore;
using units::tracer::LruCoder;
using units::tracer::MemSystem;
using units::tracer::ReplayTables;

namespace {

const std::string kDir = "tests/tracerparts/";

std::vector<std::string> fixture_lines(const std::string& name, bool* ok) {
    std::string text = portconf::read_command("gzip -dc " + kDir + name, ok);
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l))
        if (!l.empty() && l[0] != '#') lines.push_back(l);
    return lines;
}
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> v;
    std::istringstream in(s);
    std::string w;
    while (in >> w) v.push_back(w);
    return v;
}
uint64_t hx(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }
int64_t dec(const std::string& s) { return std::strtoll(s.c_str(), nullptr, 10); }
std::string hex(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%llx", (unsigned long long)v);
    return b;
}

// ---------------------------------------------------------------- RVC
void check_rvc() {
    bool ok = false;
    auto lines = fixture_lines("rvc.txt.gz", &ok);
    CHECK(ok);
    CHECK_EQ(lines.size(), size_t(65536));
    for (const auto& l : lines) {
        auto f = split(l);
        CHECK_EQ(f.size(), size_t(3));
        const uint32_t h = uint32_t(hx(f[0]));
        if (rvc::uncompress(h) != uint32_t(hx(f[1])) || rvc::code_word(h) != uint32_t(hx(f[2]))) {
            ::test::fail(__FILE__, __LINE__, "rvc %04x: uncompress %08x code_word %08x, ref %s %s", h, rvc::uncompress(h),
                         rvc::code_word(h), f[1].c_str(), f[2].c_str());
            return;
        }
    }
}

// ---------------------------------------------------------------- LruCoder
void check_lru(const char* name) {
    bool ok = false;
    auto lines = fixture_lines(name, &ok);
    CHECK(ok);
    CHECK(lines.size() > 1000);
    LruCoder c[3] = {LruCoder(false), LruCoder(false), LruCoder(true)};
    LruCoder count_only[3] = {LruCoder(false), LruCoder(false), LruCoder(true)};  // set 1: the same bits, no packer
    Bits pk[3];
    std::vector<std::string> emitted;  // the w lines our packers produced, compared with the fixture's in order
    size_t next_w = 0;
    auto sink = [&](int q) {
        return [&emitted, q](uint64_t lo, uint64_t hi) {
            emitted.push_back("w " + std::to_string(q) + " " + hex(lo) + " " + hex(hi));
        };
    };
    size_t n_e = 0, n_w = 0, hits = 0, e15 = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        auto f = split(lines[i]);
        const std::string& op = f[0];
        if (op == "e") {
            const int q = int(dec(f[1]));
            const uint64_t v = hx(f[2]);
            const int peek = c[q].peek_bits(v);
            if (q == 2 && c[q].table()[15] == v) e15++;
            const int b = c[q].encode(v, pk[q], sink(q));
            const int b1 = count_only[q].encode(v);
            if (peek != dec(f[3]) || b != dec(f[4]) || b1 != b) {
                ::test::fail(__FILE__, __LINE__, "%s line %zu (%s): peek %d bits %d (set 1: %d)", name, i + 1,
                             lines[i].c_str(), peek, b, b1);
                return;
            }
            if (b == 5) hits++;
            n_e++;
        } else if (op == "l") {
            c[2].last_load(hx(f[1]));
            count_only[2].last_load(hx(f[1]));
        } else if (op == "w") {
            n_w++;
            if (next_w >= emitted.size() || emitted[next_w] != lines[i]) {
                ::test::fail(__FILE__, __LINE__, "%s line %zu: ref %s, ours %s", name, i + 1, lines[i].c_str(),
                             next_w < emitted.size() ? emitted[next_w].c_str() : "(none)");
                return;
            }
            next_w++;
        } else if (op == "f") {
            const int q = int(dec(f[1]));
            pk[q].finish(sink(q));
        } else if (op == "r") {
            for (int q = 0; q < 3; q++) {
                c[q].reset();
                count_only[q].reset();
                pk[q].reset();
            }
        } else if (op == "s") {
            const int q = int(dec(f[1]));
            const std::string ours = "s " + std::to_string(q) + " " + std::to_string(c[q].bits) + " " +
                                     std::to_string(c[q].hits) + " " + std::to_string(c[q].misses) + " " +
                                     std::to_string(c[q].values) + " " + std::to_string(pk[q].words);
            if (ours != lines[i]) {
                ::test::fail(__FILE__, __LINE__, "%s line %zu: ref %s, ours %s", name, i + 1, lines[i].c_str(),
                             ours.c_str());
                return;
            }
        } else {
            ::test::fail(__FILE__, __LINE__, "%s line %zu: unknown event %s", name, i + 1, op.c_str());
            return;
        }
    }
    CHECK_EQ(next_w, emitted.size());
    // the stream reaches what it should
    CHECK(n_e > 10000);
    CHECK(n_w > 1000);
    CHECK(hits > 1000);
    CHECK(e15 > 10);
}

// ---------------------------------------------------------------- FileStore and MemSystem
uint64_t data_hash(const uint64_t* d) {
    uint64_t h = 17;
    for (int i = 0; i < 16; i++) h = h * 31 + d[i];
    return h;
}
std::vector<uint64_t> page_of(int64_t seed) {
    std::vector<uint64_t> p(512);
    for (int i = 0; i < 512; i++) p[i] = uint64_t(seed) * 1000003ull + uint64_t(i);
    return p;
}
void line_of(int64_t seed, uint64_t* p) {
    for (int i = 0; i < 16; i++) p[i] = uint64_t(seed) * 31ull + uint64_t(i) * 0x9E3779B97F4A7C15ull;
}
std::string stats_line(const MemSystem& m) {
    std::string s = "S";
    const int64_t v[] = {m.reads,        m.writes,      m.beyond,          m.index_accesses, m.index_hits,
                         m.data_accesses, m.data_hits,  m.lpddr_reads,     m.walk_cycles,    m.data_cycles,
                         m.early_evictions, m.read_waits, m.read_wait_cycles, m.write_allocates};
    for (int64_t x : v) s += " " + std::to_string(x);
    for (const int64_t* arr : {m.acc, m.hits, m.cycles})
        for (int k = 0; k < 4; k++) s += " " + std::to_string(arr[k]);
    return s;
}

void check_mem(const char* name) {
    bool ok = false;
    auto lines = fixture_lines(name, &ok);
    CHECK(ok);
    CHECK(lines.size() > 1000);
    FileStore* fs = nullptr;
    MemSystem* ms = nullptr;
    std::vector<FileStore::File> files(8);
    MemSystem::Response r;
    uint64_t line[16];
    size_t reads = 0, writes = 0, early = 0, waits = 0;
    auto bad = [&](size_t i, const std::string& ours) {
        ::test::fail(__FILE__, __LINE__, "%s line %zu: ref %s, ours %s", name, i + 1, lines[i].c_str(), ours.c_str());
    };
    for (size_t i = 0; i < lines.size(); i++) {
        auto f = split(lines[i]);
        const std::string& op = f[0];
        std::string ours;
        if (op == "n") {
            fs = new FileStore(int32_t(dec(f[1])));
            continue;
        } else if (op == "c") {
            FileStore::File fl = fs->create(int(dec(f[2])));
            files[size_t(dec(f[1]))] = fl;
            ours = "c " + f[1] + " " + f[2] + " -> " + std::to_string(fl.root);
        } else if (op == "p") {
            auto pg = page_of(dec(f[3]));
            fs->put_page(files[size_t(dec(f[1]))], dec(f[2]), pg.data());
            continue;
        } else if (op == "o") {
            FileStore::File fl = fs->cow_open(files[size_t(dec(f[2]))]);
            files[size_t(dec(f[1]))] = fl;
            ours = "o " + f[1] + " " + f[2] + " -> " + std::to_string(fl.root);
        } else if (op == "v") {
            fs->make_private(files[size_t(dec(f[1]))], dec(f[2]));
            continue;
        } else if (op == "O") {
            std::vector<int64_t> written;
            for (size_t k = 4; k < f.size(); k++) written.push_back(dec(f[k]));
            const int created = fs->set_ownership(files[size_t(dec(f[1]))], written);
            if (created != dec(f[3])) {
                bad(i, "created " + std::to_string(created));
                return;
            }
            continue;
        } else if (op == "m") {
            ms = new MemSystem(fs);
            ms->map_root_for_stats = int32_t(dec(f[1]));
            continue;
        } else if (op == "C") {
            ms->cold();
            continue;
        } else if (op == "S") {
            ours = stats_line(*ms);
            early = early > size_t(ms->early_evictions) ? early : size_t(ms->early_evictions);
            waits = waits > size_t(ms->read_waits) ? waits : size_t(ms->read_waits);
        } else if (op == "L") {
            ours = "L " + f[1] + " " + f[2] + " -> " + hex(fs->load64(files[size_t(dec(f[1]))], dec(f[2])));
        } else if (op == "R") {
            const FileStore::File& fl = files[size_t(dec(f[2]))];
            ms->read(dec(f[1]), fl.root, fl.depth, dec(f[3]), r);
            ours = "R " + f[1] + " " + f[2] + " " + f[3] + " -> " + std::to_string(r.status) + " " +
                   std::to_string(r.ready_at) + " " + hex(data_hash(r.data));
            reads++;
        } else if (op == "W") {
            const FileStore::File& fl = files[size_t(dec(f[2]))];
            line_of(dec(f[4]), line);
            const int64_t ack = ms->write(dec(f[1]), fl.root, fl.depth, dec(f[3]), line);
            ours = "W " + f[1] + " " + f[2] + " " + f[3] + " " + f[4] + " -> " + std::to_string(ack);
            writes++;
        } else {
            bad(i, "an unknown event");
            return;
        }
        if (ours != lines[i]) {
            bad(i, ours);
            return;
        }
    }
    CHECK(reads > 1000);
    CHECK(writes > 500);
    CHECK(early > 10);  // the free-or-clean policy and the write buffer were reached
    CHECK(waits > 5);
    delete ms;
    delete fs;
}

}  // namespace

TEST(rvc_all_encodings_equal_ref) { check_rvc(); }
TEST(lru_coder_seed1_equals_ref) { check_lru("lru-s1.txt.gz"); }
TEST(lru_coder_seed2_equals_ref) { check_lru("lru-s2.txt.gz"); }
TEST(mem_system_seed1_equals_ref) { check_mem("mem-s1.txt.gz"); }
TEST(mem_system_seed2_equals_ref) { check_mem("mem-s2.txt.gz"); }

// ---------------------------------------------------------------- MemorySystemTest's cases, ported
namespace {
std::vector<uint64_t> page(int64_t seed) { return page_of(seed); }
}

TEST(file_store_statuses) {
    FileStore fs;
    FileStore::File img = fs.create(2);
    const int64_t pgs[] = {0, 1, 5000, (int64_t(1) << 20) - 1};
    for (int64_t p : pgs) fs.put_page(img, p, page(p + 7).data());
    FileStore::Walk w;
    fs.walk(img.root, 2, 0, w);
    CHECK_EQ(w.status, FileStore::ST_OK);
    CHECK_EQ(w.n_lines, 3);
    CHECK_EQ(w.index_lines, 2);
    fs.walk(img.root, 2, 2 * 32, w);
    CHECK_EQ(w.status, FileStore::ST_HOLE);
    fs.walk(img.root, 2, 3000LL * 32, w);
    CHECK_EQ(w.status, FileStore::ST_HOLE);
    CHECK_EQ(w.n_lines, 1);
    CHECK_EQ(fs.load64(img, 2 * 4096 + 8), uint64_t(0));
    CHECK_EQ(fs.load64(img, 5000LL * 4096 + 17 * 8), page(5007)[17]);
    fs.walk(img.root, 2, int64_t(1) << 25, w);
    CHECK_EQ(w.status, FileStore::ST_BEYOND);
    CHECK_EQ(w.n_lines, 0);
    fs.walk(img.root, 2, (int64_t(1) << 25) - 1, w);
    CHECK_EQ(w.status, FileStore::ST_OK);

    FileStore::File cp = fs.cow_open(img);
    fs.make_private(cp, 1);
    fs.walk(cp.root, 2, 1 * 32 + 5, w);
    CHECK_EQ(w.status, FileStore::ST_OK);
    fs.walk(cp.root, 2, 0, w);
    CHECK_EQ(w.status, FileStore::ST_NOT_OWNED);
    fs.walk(cp.root, 2, 5000LL * 32, w);
    CHECK_EQ(w.status, FileStore::ST_NOT_OWNED);
    fs.walk(cp.root, 2, 2 * 32, w);
    CHECK_EQ(w.status, FileStore::ST_HOLE);
    CHECK_EQ(fs.load64(cp, 4096 + 24), page(8)[3]);
    uint64_t line[16];
    for (auto& x : line) x = 0x5A5A;
    fs.walk(cp.root, 2, 32, w);
    fs.write_line(w.data_line, line);
    CHECK_EQ(fs.load64(cp, 4096), uint64_t(0x5A5A));
    CHECK_EQ(fs.load64(img, 4096), page(8)[0]);

    FileStore::File map = fs.create(FileStore::depth_for(int64_t(4) << 20));
    CHECK_EQ(map.depth, 1);
    for (int p = 0; p < 1024; p++) fs.put_page(map, p, page(p).data());
    fs.walk(map.root, 1, 1023LL * 32 + 31, w);
    CHECK_EQ(w.status, FileStore::ST_OK);
    fs.walk(map.root, 1, 1024LL * 32, w);
    CHECK_EQ(w.status, FileStore::ST_BEYOND);
    CHECK_EQ(w.n_lines, 0);
    FileStore::File d0 = fs.create(0);
    fs.put_page(d0, 0, page(9).data());
    fs.walk(d0.root, 0, 31, w);
    CHECK_EQ(w.status, FileStore::ST_OK);
    CHECK_EQ(w.n_lines, 1);
    fs.walk(d0.root, 0, 32, w);
    CHECK_EQ(w.status, FileStore::ST_BEYOND);
    CHECK_EQ(FileStore::depth_for(4096), 0);
    CHECK_EQ(FileStore::depth_for(int64_t(4) << 30), 2);
    CHECK_EQ(FileStore::depth_for((int64_t(4) << 30) + 1), 3);
}

DEATH_TEST(file_store_pre_copying_a_hole_is_fatal, "pre-copying a hole") {
    FileStore fs;
    FileStore::File img = fs.create(2);
    fs.put_page(img, 0, page(1).data());
    FileStore::File cp = fs.cow_open(img);
    fs.make_private(cp, 2);
}

TEST(mem_system_timing) {
    FileStore fs;
    FileStore::File img = fs.create(2);
    for (int64_t p = 0; p < 64; p++) fs.put_page(img, p, page(p).data());
    MemSystem ms(&fs);
    MemSystem::Response r;
    ms.read(100, img.root, 2, 0, r);
    CHECK_EQ(r.ready_at, int64_t(100 + 3 * 80));
    CHECK_EQ(r.data[0], page(0)[0]);
    CHECK_EQ(r.status, FileStore::ST_OK);
    CHECK(!ms.idle(r.ready_at - 1));
    int64_t t = r.ready_at;
    ms.read(t, img.root, 2, 0, r);
    CHECK_EQ(r.ready_at, t + 3 * 6);
    t = r.ready_at;
    ms.read(t, img.root, 2, 1, r);
    CHECK_EQ(r.ready_at, t + 6 + 6 + 80);
    t = r.ready_at;
    ms.read(t, img.root, 2, 31LL * 32, r);
    CHECK_EQ(r.ready_at, t + 6 + 6 + 80);
    t = r.ready_at;
    ms.read(t, img.root, 2, 40LL * 32, r);
    CHECK_EQ(r.ready_at, t + 6 + 80 + 80);
    CHECK_EQ(ms.walk_cycles, int64_t(160 + 12 + 12 + 12 + 86));
    ms.read(r.ready_at, img.root, 2, 3LL * 32, r);
    CHECK_EQ(r.status, FileStore::ST_OK);
    ms.read(r.ready_at, img.root, 2, 70LL * 32, r);
    CHECK_EQ(r.status, FileStore::ST_HOLE);
    CHECK_EQ(r.data[5], uint64_t(0));
    const int64_t b0 = r.ready_at;
    ms.read(b0, img.root, 2, int64_t(1) << 25, r);
    CHECK_EQ(r.status, FileStore::ST_BEYOND);
    CHECK_EQ(r.ready_at, b0 + 1);
    ms.cold();
    ms.read(1000, img.root, 2, 0, r);
    CHECK_EQ(r.ready_at, int64_t(1000 + 240));
    CHECK_EQ(ms.reads, int64_t(1));
}

DEATH_TEST(mem_system_a_second_request_while_busy_is_fatal, "request outstanding") {
    FileStore fs;
    FileStore::File img = fs.create(2);
    for (int64_t p = 0; p < 4; p++) fs.put_page(img, p, page(p).data());
    MemSystem ms(&fs);
    MemSystem::Response r;
    ms.read(100, img.root, 2, 0, r);
    ms.read(r.ready_at - 1, img.root, 2, 1, r);
}

TEST(mem_system_policy) {
    FileStore fs(32 * 1024);
    FileStore::File img = fs.create(2);
    const int pages_n = 16 * 12;
    for (int64_t p = 0; p < pages_n; p++) fs.put_page(img, p, page(p).data());
    MemSystem ms(&fs);
    MemSystem::Response r;
    FileStore::Walk w;
    fs.walk(img.root, 2, 3, w);
    const int set0 = w.data_line & 511;
    int in_set[12];
    int n = 0;
    for (int64_t p = 0; p < pages_n && n < 12; p++) {
        fs.walk(img.root, 2, p * 32 + 3, w);
        if ((w.data_line & 511) == set0) in_set[n++] = int(p);
    }
    CHECK_EQ(n, 12);
    int64_t t = 0;
    uint64_t line[16] = {};
    for (int k = 0; k < 8; k++) {
        t = ms.read(t, img.root, 2, in_set[k] * 32LL + 3, r).ready_at;
        t = ms.write(t, img.root, 2, in_set[k] * 32LL + 3, line);
    }
    CHECK_EQ(ms.write_allocates, int64_t(0));
    CHECK_EQ(ms.early_evictions, int64_t(1));
    const int64_t before = ms.data_accesses - ms.data_hits;
    t = ms.read(t, img.root, 2, in_set[8] * 32LL + 3, r).ready_at;
    CHECK_EQ(ms.data_accesses - ms.data_hits, before + 1);
    CHECK_EQ(ms.early_evictions, int64_t(2));
    CHECK(ms.read_waits >= 1);
    const int64_t wa = ms.write_allocates;
    const int64_t t0 = t;
    t = ms.write(t, img.root, 2, in_set[9] * 32LL + 3, line);
    CHECK_EQ(ms.write_allocates, wa + 1);
    CHECK_EQ(t, t0 + 6 + 6 + 6);
    MemSystem m2(&fs);
    int64_t u = 0;
    for (int k = 0; k < 8; k++) {
        u = m2.read(u, img.root, 2, in_set[k] * 32LL + 3, r).ready_at;
        u = m2.write(u, img.root, 2, in_set[k] * 32LL + 3, line);
    }
    const int64_t wait0 = m2.read_wait_cycles;
    const int64_t u1 = m2.read(u, img.root, 2, in_set[10] * 32LL + 3, r).ready_at;
    CHECK(m2.read_wait_cycles - wait0 <= 80);
    CHECK_EQ(u1, r.ready_at);
}

// ---------------------------------------------------------------- the replay tables
TEST(replay_tables_load_and_match_the_manifest) {
    const std::string dir = ReplayTables::default_dir();
    ReplayTables t = ReplayTables::load(dir);
    CHECK(t.count() > 0);
    CHECK_EQ(t.words.size(), size_t(t.count()));
    // every file's sha256 as ref/tables/manifest.json records it
    bool ok = false;
    const std::string manifest = portconf::read_command("cat " + dir + "/../manifest.json", &ok);
    CHECK(ok);
    struct F { const char* name; const std::vector<uint8_t>* bytes; };
    const F files[] = {{"decode_table.bin", &t.decode_bytes}, {"rom.bin", &t.rom_bytes},
                       {"templates.bin", &t.template_bytes},  {"template_index.bin", &t.index_bytes}};
    for (const F& f : files) {
        const std::string key = std::string("\"replayer/") + f.name + "\"";
        const size_t at = manifest.find(key);
        CHECK(at != std::string::npos);
        const size_t s = manifest.find("\"sha256\": \"", at);
        CHECK(s != std::string::npos);
        const std::string want = manifest.substr(s + 11, 64);
        const std::string got = ::test::sha256_hex(f.bytes->data(), f.bytes->size());
        if (want != got) {
            ::test::fail(__FILE__, __LINE__, "%s: sha256 %s, manifest %s", f.name, got.c_str(), want.c_str());
            return;
        }
    }
    // every template is found by its own key; keys normalise register fields
    for (int k = 0; k < t.count(); k++) CHECK_EQ(t.lookup(t.key[size_t(k)]), k);
    CHECK_EQ(ReplayTables::key_of(0x0000000Bu | (5u << 7) | (6u << 15) | (7u << 20)), 0x0000000Bu);
    CHECK_EQ(ReplayTables::key_of(0x00001073u | (9u << 7) | (9u << 15)), 0x00001073u | (1u << 7) | (1u << 15));
    CHECK_EQ(ReplayTables::key_of(0x00002073u | (3u << 7) | (4u << 15)), 0x00002073u | (1u << 7) | (2u << 15));
}
