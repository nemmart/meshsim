// test_updater_ctl: the Updater's controller (src/units/updater_ctl.h, updater_top.h; updater_wip, WORKER6), with the
// test LPDDR and the stand-in cache (tests/test_lpddr.h, standin_cache.h). NOTES section 9's checks:
//   - alone against gold: gold's frame files of the guests (tests/updater/) offered on the word port; every image
//     against gold's reference, PAGE_COUNT 0 at every done; the timing fixed, then varied;
//   - against the black box: the same runs on both, each fed on its word port (WORKER9: the shim is gone); at each
//     run's end the registers, the errors, the events, the stores byte for byte, the stream not yet decoded and the
//     page counts equal;
//   - random streams (holes, shared pages at every level, runs of the Updater ending inside a run of values, continue
//     across runs, raw and LRU, the Tracer's copy), against the black box and against a model of the files;
//   - the errors, each against the black box where it has the case.
// In every run the test pieces' checks: no read of a page never written, no update before its page's write is answered,
// no write to a page that is neither new, nor a root being made, nor an owned data page of the image.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "ctl_bench.h"
#include "harness.h"
#include "sha256.h"

namespace {

using ctl_bench::Bench;
using ctl_bench::Options;
using ctl_bench::Run;
using ctl_bench::Word;
using units::Updater;
using units::UpdaterTop;
using units::tracer::FileStore;

std::map<std::string, std::string> gold_digests(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string name, digest;
        if (words >> name >> digest) out[name] = digest;
    }
    return out;
}
std::string sha(const std::vector<uint8_t>& bytes) { return test::sha256_hex(bytes.data(), bytes.size()); }

// ================================================================================================ gold
// A frame file's plan for s, executed on the controller alone (no black box): every image against gold's.
using Setup = std::function<void(Bench<UpdaterTop>&)>;
void gold_alone(const char* name, const Options& o, const Setup& setup = nullptr, const Setup& after = nullptr) {
    frames::File file;
    std::string err;
    if (!file.read(std::string("tests/updater/") + name + ".frames", err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
    for (unsigned s : {0u, 20u, 60u, 63u}) {
        const auto gold = gold_digests(std::string("tests/updater/") + name + "-s" + std::to_string(s) + ".images.txt");
        CHECK_EQ(gold.size(), size_t(6));
        std::vector<ctl_bench::Step> steps;
        if (!ctl_bench::plan(file, 64, s, steps, err)) { ::test::fail(__FILE__, __LINE__, "%s: %s", name, err.c_str()); return; }
        Bench<UpdaterTop> b(o);
        if (setup) setup(b);
        ctl_bench::Images im;
        for (size_t k = 0; k < steps.size(); k++)
            if (!ctl_bench::execute(b, file, steps, k, im)) {
                ::test::fail(__FILE__, __LINE__, "%s, s %u, %s: the run did not end (ERRORS 0x%x)%s", name, s, steps[k].name.c_str(), unsigned(b.u.errors(b.t)),
                             b.violations().c_str());
                return;
            }
        if (b.stream_left()) ::test::fail(__FILE__, __LINE__, "%s, s %u: frame data left unsent", name, s);
        std::map<std::string, std::string> got;
        for (size_t k = 0; k < im.names.size(); k++) got[im.names[k]] = sha(im.images[k]);
        CHECK(got["program image"] == gold.at("program"));
        CHECK(got["jump table"] == gold.at("table"));
        CHECK(got["data image"] == gold.at("cycle0"));
        CHECK(got["segments before s"] == gold.at("start-" + std::to_string(s)));
        CHECK(got["tracer"] == gold.at("tracer"));
        CHECK(got["segments after s"] == gold.at("final"));
        for (const auto& a : b.accounts) CHECK_EQ(a.pages_left, 0u);
        CHECK_EQ(b.u.errors(b.t), 0);
        const std::string v = b.violations();
        if (!v.empty()) ::test::fail(__FILE__, __LINE__, "%s, s %u:%s", name, s, v.c_str());
        CHECK_EQ(b.u.free_page_entries(), 0u);
        if (after) after(b);
    }
}
// The test pieces' timing and the port's valid: fixed, or with a seed all three varied.
Options timing(uint64_t seed = 0) {
    Options o;
    if (seed) { o.lpddr.jitter = true; o.lpddr.seed = uint32_t(seed); o.cache_seed = seed * 7 + 1; o.valid_seed = seed * 13 + 5; }
    return o;
}

// The same plan on the black box and on the controller, each on its word port, compared after every run.
void gold_against(const char* name, uint64_t seed, bool joined = false) {
    frames::File file;
    std::string err;
    if (!file.read(std::string("tests/updater/") + name + ".frames", err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
    for (unsigned s : {0u, 20u, 63u}) {
        std::vector<ctl_bench::Step> steps;
        if (!ctl_bench::plan(file, 64, s, steps, err)) { ::test::fail(__FILE__, __LINE__, "%s: %s", name, err.c_str()); return; }
        Options o;
        if (seed) { o.lpddr.jitter = true; o.lpddr.seed = uint32_t(seed); o.cache_seed = seed * 3 + 1; o.valid_seed = seed * 13 + 5; }
        o.joined = joined;
        Bench<Updater> a;
        Bench<UpdaterTop> b(o);
        ctl_bench::Images ia, ib;
        for (size_t k = 0; k < steps.size(); k++) {
            const bool oka = ctl_bench::execute(a, file, steps, k, ia), okb = ctl_bench::execute(b, file, steps, k, ib);
            CHECK(oka && okb);
            const std::string d = ctl_bench::compare(a, b);
            if (!d.empty()) { ::test::fail(__FILE__, __LINE__, "%s, s %u, after %s:%s", name, s, steps[k].name.c_str(), d.c_str()); return; }
            if (!oka || !okb) return;
        }
        CHECK(ia.images == ib.images);
        const std::string v = b.violations();
        if (!v.empty()) ::test::fail(__FILE__, __LINE__, "%s, s %u:%s", name, s, v.c_str());
    }
}

// ================================================================================================ random streams
// A file as the tests expect it: its words by 64-bit-word index (a copy has its source's words).
struct Model {
    std::map<uint64_t, uint64_t> words;
    void apply(const std::vector<uint64_t>& u) {
        for (size_t i = 0; i < u.size();) {
            const uint64_t start = u[i] & 0xFFFFFFFFu, n = u[i] >> 32;
            for (uint64_t k = 0; k < n; k++) words[start + k] = u[i + 1 + k];
            i += 1 + n;
        }
    }
    // gold's form: the pages holding a non-zero byte, ascending, each a u64 byte offset then its 4,096 bytes
    std::vector<uint8_t> image() const {
        std::vector<uint8_t> out;
        for (auto it = words.begin(); it != words.end();) {
            const uint64_t page = it->first >> 9;
            uint64_t pg[512] = {};
            bool any = false;
            for (; it != words.end() && it->first >> 9 == page; ++it) { pg[it->first & 511] = it->second; any |= it->second != 0; }
            if (!any) continue;
            for (unsigned b = 0; b < 8; b++) out.push_back(uint8_t((page * 4096) >> (8 * b)));
            for (uint64_t w : pg) for (unsigned b = 0; b < 8; b++) out.push_back(uint8_t(w >> (8 * b)));
        }
        return out;
    }
};

// Update words as a frame's data, raw or LRU-coded (HOST-FRAMES.md), padded to 128-bit words.
std::vector<Word> encode(const std::vector<uint64_t>& u, bool lru) {
    std::vector<uint8_t> bytes;
    size_t bits = 0;
    auto put = [&](uint64_t v, unsigned n) {
        for (unsigned i = 0; i < n; i++, bits++) {
            if (bits / 8 == bytes.size()) bytes.push_back(0);
            if (v >> i & 1) bytes[bits / 8] |= uint8_t(1u << (bits % 8));
        }
    };
    if (!lru) for (uint64_t x : u) put(x, 64);
    else {
        uint64_t table[32] = {};
        for (uint64_t x : u) {
            int hit = -1;
            for (int i = 0; i < 32 && hit < 0; i++) if (table[i] == x) hit = i;
            if (hit >= 0) { put(1, 1); put(uint64_t(hit), 5); }
            else { put(0, 1); if (x >> 32) { put(1, 1); put(x, 64); } else { put(0, 1); put(x, 32); } }
            for (int k = hit >= 0 ? hit : 31; k > 0; k--) table[k] = table[k - 1];
            table[0] = x;
        }
    }
    while (bytes.size() % 16) bytes.push_back(0);
    std::vector<Word> w(bytes.size() / 16);
    for (size_t k = 0; k < w.size(); k++)
        for (unsigned b = 0; b < 16; b++) w[k][b / 8] |= uint64_t(bytes[16 * k + b]) << (8 * (b % 8));
    return w;
}

// Random update words over a pool of pages, in address order: whole pages, halves, runs across a half or a page, a few
// words; values often repeated (for the LRU) and often 32-bit.
std::vector<uint64_t> random_updates(test::Rng& rng, const std::vector<uint64_t>& pool, unsigned runs) {
    std::vector<uint64_t> u;
    std::vector<uint64_t> pages;
    for (unsigned r = 0; r < runs; r++) pages.push_back(pool[rng.below(pool.size())]);
    std::sort(pages.begin(), pages.end());
    uint64_t at = 0;
    for (uint64_t page : pages) {
        const unsigned kind = unsigned(rng.below(6));
        uint64_t start, n;
        if (kind == 0) { start = page << 9; n = 512; }
        else if (kind == 1) { start = (page << 9) + 256 * rng.below(2); n = 256; }
        else if (kind == 2) { start = (page << 9) + 100 + rng.below(300); n = 200 + rng.below(500); }
        else { start = (page << 9) + rng.below(512); n = 1 + rng.below(12); }
        if (start < at) continue;
        u.push_back(start | n << 32);
        for (uint64_t k = 0; k < n; k++) u.push_back(rng.below(3) == 0 ? rng.below(5) : rng.below(2) ? rng.next() : rng.next() & 0xFFFFFFFFu);
        at = start + n;
    }
    return u;
}

// One frame of a scenario: its update words cut into runs of the Updater at random points (inside runs of values too).
struct Frame {
    std::vector<uint64_t> updates;
    std::vector<uint32_t> cuts;                    // each run's WORD_COUNT
    bool lru = false;
};
Frame cut(test::Rng& rng, std::vector<uint64_t> u, bool lru, unsigned runs) {
    Frame f;
    f.updates = std::move(u);
    f.lru = lru;
    std::vector<uint32_t> at;
    for (unsigned k = 1; k < runs; k++) at.push_back(uint32_t(rng.below(f.updates.size() + 1)));
    std::sort(at.begin(), at.end());
    uint32_t last = 0;
    for (uint32_t a : at) { f.cuts.push_back(a - last); last = a; }
    f.cuts.push_back(uint32_t(f.updates.size()) - last);
    return f;
}

// A scenario played on a bench: four files, each a frame of runs.
//   A: an empty file R0 and its updates;                  B: R1, a copy of R0, and its updates (shared at level 0);
//   C: R2, a copy of R1, with the Tracer's copy R3, its runs with tracer_copy but the last (shared pages at every level);
//   D: R4, a copy of R1 after B (shared with R0 and R1 both).
struct Scenario {
    int depth = 1;
    Frame a, b, c, d;
    static constexpr uint32_t R0 = 0x80000 * 32, R1 = R0 + 32, R2 = R0 + 64, R3 = R0 + 96, R4 = R0 + 128;
    static constexpr uint32_t PAGES = 300;
};
Scenario make_scenario(uint64_t seed) {
    test::Rng rng(seed);
    Scenario x;
    x.depth = 1 + int(seed % 3);
    const uint64_t reach = x.depth == 1 ? 1024 : x.depth == 2 ? (uint64_t(1) << 20) : (uint64_t(1) << 23);
    std::vector<uint64_t> pool;
    for (int k = 0; k < 6; k++) {                  // clusters, so that index pages are shared and new ones are made
        const uint64_t base = rng.below(reach - 8);
        for (int j = 0; j < 3; j++) pool.push_back(base + rng.below(8));
    }
    const bool lru = seed & 1;
    x.a = cut(rng, random_updates(rng, pool, 10), lru, 1 + unsigned(rng.below(3)));
    x.b = cut(rng, random_updates(rng, pool, 8), !lru, 1 + unsigned(rng.below(3)));
    x.c = cut(rng, random_updates(rng, pool, 8), lru, 2 + unsigned(rng.below(3)));
    x.d = cut(rng, random_updates(rng, pool, 6), !lru, 1 + unsigned(rng.below(2)));
    return x;
}
// The runs of each frame: the first makes the root, the rest carry on (continue). In C every run but the last has
// tracer_copy.
struct ScenarioRun { Run run; size_t frame; bool first; };
std::vector<ScenarioRun> runs_of(const Scenario& x) {
    std::vector<ScenarioRun> out;
    const Frame* f[4] = {&x.a, &x.b, &x.c, &x.d};
    const uint32_t image[4] = {Scenario::R0, Scenario::R1, Scenario::R2, Scenario::R4};
    const uint32_t source[4] = {0, Scenario::R0, Scenario::R1, Scenario::R1};
    for (size_t k = 0; k < 4; k++)
        for (size_t r = 0; r < f[k]->cuts.size(); r++) {
            const bool first = r == 0, tc = k == 2 && r + 1 < f[k]->cuts.size();
            const unsigned root = !first ? 0u : k == 0 ? 1u : 2u;
            Run run{ctl_bench::config(f[k]->lru, !first, tc, x.depth, root), f[k]->cuts[r], Scenario::PAGES, int32_t(image[k]),
                    int32_t(tc ? Scenario::R3 : 0), int32_t(source[k])};
            out.push_back({run, k, first});
        }
    return out;
}
// The expected images after each frame.
std::vector<std::vector<uint8_t>> expected(const Scenario& x) {
    Model r0, r1, r2, r4;
    r0.apply(x.a.updates);
    r1 = r0; r1.apply(x.b.updates);
    r2 = r1; r2.apply(x.c.updates);
    r4 = r1; r4.apply(x.d.updates);
    return {r0.image(), r1.image(), r2.image(), r1.image() /* R3, the Tracer's copy: R1 as it was */, r4.image()};
}

template <class U>
void stream_frame(Bench<U>& b, const Scenario& x, size_t k) {
    const Frame* f[4] = {&x.a, &x.b, &x.c, &x.d};
    b.stream(encode(f[k]->updates, f[k]->lru));
}
template <class U>
std::string check_images(Bench<U>& b, const Scenario& x) {
    const auto e = expected(x);
    const uint32_t roots[5] = {Scenario::R0, Scenario::R1, Scenario::R2, Scenario::R3, Scenario::R4};
    const char* names[5] = {"R0", "R1", "R2", "the Tracer's copy R3", "R4"};
    std::string d;
    for (int k = 0; k < 5; k++)
        if (updater_bench::image(b.store, int32_t(roots[k]), x.depth) != e[size_t(k)]) d += std::string(" ") + names[k] + ";";
    return d;
}

// The scenario on the black box and on the controller, each on its word port, compared after every run; both against the
// model at the end.
void random_against(uint64_t seed, uint64_t timing_seed, bool joined = false) {
    const Scenario x = make_scenario(seed);
    Options o;
    if (timing_seed) { o.lpddr.jitter = true; o.lpddr.seed = uint32_t(timing_seed); o.cache_seed = timing_seed * 5 + 3; o.valid_seed = timing_seed * 11 + 7; }
    o.joined = joined;
    Bench<Updater> a;
    Bench<UpdaterTop> b(o);
    const auto runs = runs_of(x);
    for (size_t k = 0; k < runs.size(); k++) {
        if (runs[k].first) { stream_frame(a, x, runs[k].frame); stream_frame(b, x, runs[k].frame); }
        const bool oka = a.run(runs[k].run), okb = b.run(runs[k].run);
        const std::string d = ctl_bench::compare(a, b);
        if (!oka || !okb || !d.empty()) {
            ::test::fail(__FILE__, __LINE__, "seed %llu (timing %llu), run %zu (frame %zu, %u words): ended %d and %d;%s%s", (unsigned long long)seed,
                         (unsigned long long)timing_seed, k, runs[k].frame, runs[k].run.words, oka, okb, d.c_str(), b.violations().c_str());
            return;
        }
    }
    const std::string ia = check_images(a, x), ib = check_images(b, x), v = b.violations();
    if (!ia.empty() || !ib.empty() || !v.empty())
        ::test::fail(__FILE__, __LINE__, "seed %llu: images against the model: the black box's%s; the controller's%s;%s", (unsigned long long)seed,
                     ia.c_str(), ib.c_str(), v.c_str());
}
// The scenario on the controller alone against the model.
void random_alone(uint64_t seed, uint64_t timing_seed) {
    const Scenario x = make_scenario(seed);
    Bench<UpdaterTop> b(timing(timing_seed));
    const auto runs = runs_of(x);
    for (size_t k = 0; k < runs.size(); k++) {
        if (runs[k].first) stream_frame(b, x, runs[k].frame);
        if (!b.run(runs[k].run)) {
            ::test::fail(__FILE__, __LINE__, "seed %llu, run %zu: did not end (ERRORS 0x%x)%s", (unsigned long long)seed, k, unsigned(b.u.errors(b.t)),
                         b.violations().c_str());
            return;
        }
    }
    const std::string d = check_images(b, x), v = b.violations();
    if (!d.empty() || !v.empty() || b.stream_left())
        ::test::fail(__FILE__, __LINE__, "seed %llu (timing %llu): images%s;%s%s", (unsigned long long)seed, (unsigned long long)timing_seed, d.c_str(), v.c_str(),
                     b.stream_left() ? " stream left" : "");
}

}  // namespace

TEST(ctl_gold_kin_raw) { gold_alone("kin-raw", timing()); }
TEST(ctl_gold_kin_lru) { gold_alone("kin-lru", timing()); }
TEST(ctl_gold_jumps_base) { gold_alone("jumps-base", timing()); }
TEST(ctl_gold_divs) { gold_alone("divs", timing()); }
TEST(ctl_gold_kin_big) { gold_alone("kin-big", timing()); }
TEST(ctl_gold_varied) {
    for (uint64_t seed : {1, 2, 3}) { gold_alone("kin-lru", timing(seed)); gold_alone("divs", timing(seed)); gold_alone("jumps-base", timing(seed)); }
}
TEST(ctl_gold_against_black_box) {
    for (const char* n : {"kin-raw", "kin-lru", "jumps-base", "divs", "kin-big"}) gold_against(n, 0);
}
TEST(ctl_gold_against_black_box_varied) {
    for (const char* n : {"kin-lru", "divs"}) for (uint64_t seed : {4, 5}) gold_against(n, seed);
}

TEST(ctl_random_against_black_box) { for (uint64_t seed = 1; seed <= 60; seed++) random_against(seed, 0); }
TEST(ctl_random_against_black_box_varied) { for (uint64_t seed = 61; seed <= 90; seed++) random_against(seed, seed); }
TEST(ctl_random_alone) { for (uint64_t seed = 1; seed <= 60; seed++) random_alone(seed, 0); }
TEST(ctl_random_alone_varied) { for (uint64_t seed = 1; seed <= 60; seed++) random_alone(seed, seed + 1000); }
// ---- v1.37 stage 1 (the INTEGRATOR): THE JOINED BENCH. The same runs with the controller on the memory system: the real
// cache, arbiter and LPDDR model behind its client port 1 and look-up port 1 (units/cache_system.h; WORKER9's piece 2), the
// joint's checks on, the timing fixed and jittered. Against gold's images and against the black box, the stores byte for
// byte (with no Tracer the cache holds no dirty line, so LPDDR is the whole truth).
Options joined(uint64_t seed = 0) {
    Options o;
    o.joined = true;
    if (seed) { o.lpddr.jitter = true; o.lpddr.seed = uint32_t(seed); o.valid_seed = seed * 13 + 5; }
    return o;
}
TEST(joined_gold_alone) { for (const char* n : {"kin-raw", "kin-lru", "jumps-base", "divs", "kin-big"}) gold_alone(n, joined()); }
TEST(joined_gold_alone_jittered) {
    for (uint64_t seed : {1, 2, 3}) for (const char* n : {"kin-raw", "kin-lru", "jumps-base", "divs", "kin-big"}) gold_alone(n, joined(seed));
}
TEST(joined_gold_against_black_box) { for (const char* n : {"kin-raw", "kin-lru", "jumps-base", "divs", "kin-big"}) gold_against(n, 0, true); }
TEST(joined_gold_against_black_box_jittered) { for (const char* n : {"kin-lru", "divs", "kin-big"}) for (uint64_t seed : {4, 5}) gold_against(n, seed, true); }
TEST(joined_random_against_black_box) { for (uint64_t seed = 1; seed <= 60; seed++) random_against(seed, 0, true); }
TEST(joined_random_against_black_box_jittered) { for (uint64_t seed = 61; seed <= 90; seed++) random_against(seed, seed, true); }

// A SECOND CLIENT AT ONCE (WORKER9's section 4): while the Updater builds a fixture's images, a client on the fill port,
// as the Tracer is, reads a frozen file of its own and writes and reads back a scratch file of its own, every reply
// against what it must be. Both files are depth 1, far from the Updater's pages. The Updater's images against gold's,
// the joint's checks, and the overlap counted.
struct FillClient {
    static constexpr int32_t BASE = 100000 * 32;   // the files' pages: far above the bench's
    units::CacheSystem* sys = nullptr;
    FileStore* store = nullptr;
    int32_t frozen = 0, scratch = 0;               // the two roots
    uint64_t rnd = 0x9E3779B97F4A7C15ull, reads = 0, writes = 0, checked = 0, bad = 0;
    bool out = false, out_write = false;
    int32_t out_root = 0;
    int64_t out_line = 0;
    std::map<int64_t, std::array<uint64_t, 16>> written;   // the scratch file's lines as last written
    uint64_t next() { rnd ^= rnd << 13; rnd ^= rnd >> 7; rnd ^= rnd << 17; return rnd; }
    void make(int32_t root, int32_t first_data, unsigned pages, bool owned, uint64_t tag) {
        uint64_t* idx = store->page_at(root);
        for (int i = 0; i < 1024; i++) FileStore::set_entry(idx, i, units::lookup::NO_PAGE);
        for (unsigned p = 0; p < pages; p++) {
            const int32_t a = first_data + int32_t(32 * p);
            uint64_t* d = store->page_at(a);
            for (unsigned w = 0; w < 512; w++) d[w] = tag ^ (uint64_t(p) << 32) ^ w;
            FileStore::set_entry(idx, int(p), uint32_t(a) | (owned ? units::lookup::OWNED : 0));
        }
    }
    void attach(Bench<UpdaterTop>& b) {
        sys = b.sys.get(); store = &b.store; written.clear(); out = false;
        frozen = BASE; scratch = BASE + 32;
        make(frozen, BASE + 64, 8, false, 0xF0F0000000000000ull);
        make(scratch, BASE + 64 + 32 * 8, 4, true, 0x5C5C000000000000ull);
        b.other_client = [this](int64_t t) { cycle(t); };
    }
    std::array<uint64_t, 16> expect(int32_t root, int64_t line) const {
        if (root == scratch) { auto it = written.find(line); if (it != written.end()) return it->second; }
        const uint64_t* idx = store->raw_page(root);
        const uint32_t e = FileStore::entry_of(idx, int(line >> 5));
        const uint64_t* d = store->raw_page(int32_t(e & units::lookup::ADDRESS));
        std::array<uint64_t, 16> x;
        for (unsigned w = 0; w < 16; w++) x[w] = d[(line & 31) * 16 + w];
        return x;
    }
    void cycle(int64_t t) {
        sys->begin(t);
        const units::cache::Outputs& o = sys->ctl.out();
        if (out && !out_write && o.frr_valid) {
            const std::array<uint64_t, 16> x = expect(out_root, out_line);
            const unsigned want = out_root == frozen ? 2u : 0u;   // 2: the page is not the file's own (17.9.5)
            if (o.frr_status != want || std::memcmp(o.frr_data, x.data(), 128) != 0) {
                bad++;
                if (bad <= 5) std::printf("wrong at %lld: root %s line %lld status %u, got %llx want %llx (written %d)\n", (long long)t, out_root == frozen ? "frozen" : "scratch",
                                          (long long)out_line, unsigned(o.frr_status), (unsigned long long)o.frr_data[0], (unsigned long long)x[0], int(written.count(out_line)));
            }
            checked++; out = false;
        } else if (out && out_write && o.fwr_valid) {
            if (o.fwr_status != 0) bad++;
            out = false;
        }
        if (out || (next() & 3) != 0) return;      // a request one cycle in four, when none is outstanding
        const unsigned k = unsigned(next() % 3);
        if (k == 2 && o.fw_ready) {                // a write of a scratch line
            const int64_t line = int64_t(next() % (4 * 32));
            std::array<uint64_t, 16> d;
            for (auto& w : d) w = next();
            units::cache::FillWrite& fw = sys->in.fw;
            fw.valid = true; fw.root = scratch; fw.depth = 1; fw.line = line;
            std::memcpy(fw.data, d.data(), 128);
            written[line] = d;
            out = true; out_write = true; out_root = scratch; out_line = line; writes++;
        } else if (k < 2 && o.fr_ready) {          // a read of a frozen line, or of a scratch line
            const int32_t root = k == 0 ? frozen : scratch;
            const int64_t line = int64_t(next() % ((k == 0 ? 8 : 4) * 32));
            units::cache::FillRead& fr = sys->in.fr;
            fr.valid = true; fr.root = root; fr.depth = 1; fr.line = line;
            out = true; out_write = false; out_root = root; out_line = line; reads++;
        }
    }
};
TEST(joined_second_client_at_once) {
    for (uint64_t seed : {0, 7, 8}) {
        FillClient f;
        uint64_t requests = 0, both = 0, under = 0, replays = 0;
        gold_alone("kin-big", joined(seed), [&](Bench<UpdaterTop>& b) { f.attach(b); }, [&](Bench<UpdaterTop>& b) {
            requests += b.sys->lookups + b.sys->updates; both += b.sys->both_outstanding_cycles;
            under += b.sys->ctl.served_under_miss; replays += b.sys->ctl.replays;
        });
        std::printf("the second client, timing %llu: %llu reads (%llu checked), %llu writes, %llu wrong; the Updater's look-ups and updates %llu; "
                    "served under a miss %llu, replays %llu\n", (unsigned long long)seed, (unsigned long long)f.reads, (unsigned long long)f.checked,
                    (unsigned long long)f.writes, (unsigned long long)f.bad, (unsigned long long)requests, (unsigned long long)under, (unsigned long long)replays);
        CHECK(under + replays > 0);                // the two clients' requests met in the cache
        CHECK_EQ(f.bad, 0u);
        CHECK(f.checked > 100);
        CHECK(f.writes > 50);
    }
}

// The random scenarios reach what they are for: a look-up stopping at a hole and at a shared page at every level, a
// half read that a run of values ending inside the run would have covered (the owner's note at the go-ahead), and pages
// written in runs with the Tracer's copy. Prints the counts.
TEST(ctl_random_reaches_its_cases) {
    uint64_t by[2][8][4] = {}, cut_reads = 0, tracer_writes = 0, depths[4] = {};
    for (uint64_t seed = 1; seed <= 90; seed++) {
        const Scenario x = make_scenario(seed);
        depths[x.depth]++;
        Bench<UpdaterTop> b(timing());
        for (const auto& r : runs_of(x)) {
            if (r.first) stream_frame(b, x, r.frame);
            CHECK(b.run(r.run));
            cut_reads += b.u.ctl.cut_half_reads_run;
            if (r.run.config & 4) tracer_writes += b.accounts.back().pages_written;
        }
        for (int o = 0; o < 2; o++) for (int s = 0; s < 8; s++) for (int l = 0; l < 4; l++) by[o][s][l] += b.cache->replies_by[o][s][l];
    }
    const char* st[] = {"ok", "hole", "not_owned", "beyond", "lpddr_error"};
    for (int o = 0; o < 2; o++)
        for (int s = 0; s < 5; s++)
            for (int l = 0; l < 4; l++)
                if (by[o][s][l]) std::printf("  %s %s at level %d: %llu\n", o ? "update" : "look-up", st[s], l, (unsigned long long)by[o][s][l]);
    std::printf("  halves read that the black box's test would skip: %llu; pages written with tracer_copy: %llu; depths 1, 2, 3: %llu, %llu, %llu\n",
                (unsigned long long)cut_reads, (unsigned long long)tracer_writes, (unsigned long long)depths[1], (unsigned long long)depths[2],
                (unsigned long long)depths[3]);
    for (int l = 0; l < 3; l++) { CHECK(by[0][units::lookup::HOLE][l] > 0); CHECK(by[0][units::lookup::NOT_OWNED][l] > 0); }
    CHECK(cut_reads > 0);
    CHECK(tracer_writes > 0);
}
// A run of the Updater ending inside a run of values that covers a page or a half (the owner's note at the go-ahead):
// cut 1, 255, 256, 257 and 511 values into a page-aligned run of 512, and 1 and 255 into a half-aligned run of 256; on
// a page the file owns and on one it shares. Against the black box and the model; the halves the black box's test
// skips are the controller's reads, counted.
TEST(ctl_run_ends_inside_a_covered_half) {
    std::vector<uint64_t> full;                    // pages 0 to 3 of a depth-1 file, whole
    for (uint64_t p = 0; p < 4; p++) { full.push_back((p << 9) | uint64_t(512) << 32); for (int k = 0; k < 512; k++) full.push_back(p * 1000 + uint64_t(k) + 1); }
    struct Case { uint64_t start, n; uint32_t k; };
    const Case cases[] = {{2 * 512, 512, 1}, {2 * 512, 512, 255}, {2 * 512, 512, 256}, {2 * 512, 512, 257}, {2 * 512, 512, 511},
                          {2 * 512 + 256, 256, 1}, {2 * 512 + 256, 256, 255}};
    for (bool shared : {false, true})
        for (const Case& c : cases) {
            std::vector<uint64_t> u{c.start | c.n << 32};
            for (uint64_t k = 0; k < c.n; k++) u.push_back(0xABCD0000u + k);
            const uint32_t R0 = 0x80000 * 32, R1 = R0 + 32, image = shared ? R1 : R0;
            Bench<Updater> a;
            Bench<UpdaterTop> b;
            uint64_t cut_reads = 0;
            for (int side = 0; side < 2; side++) {
                auto go = [&](auto& x) {
                    x.stream(encode(full, false));
                    bool ok = x.run({ctl_bench::config(false, false, false, 1, 1), uint32_t(full.size()), 20, int32_t(R0), 0, 0});
                    x.stream(encode(u, side == 1));
                    ok &= x.run({ctl_bench::config(side == 1, false, false, 1, shared ? 2u : 0u), 1 + c.k, 20, int32_t(image), 0, int32_t(R0)});
                    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Bench<UpdaterTop>>) cut_reads += x.u.ctl.cut_half_reads_run;
                    ok &= x.run({ctl_bench::config(side == 1, true, false, 1, 0), uint32_t(c.n) - c.k, 20, int32_t(image), 0, 0});
                    if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Bench<UpdaterTop>>) cut_reads += x.u.ctl.cut_half_reads_run;
                    return ok;
                };
                const bool oka = go(a), okb = go(b);
                const std::string d = ctl_bench::compare(a, b);
                Model m0, m1;
                m0.apply(full); m1 = m0; m1.apply(u);
                CHECK(updater_bench::image(b.store, int32_t(image), 1) == m1.image());
                CHECK(updater_bench::image(a.store, int32_t(image), 1) == m1.image());
                if (shared) CHECK(updater_bench::image(b.store, int32_t(R0), 1) == m0.image());
                if (!oka || !okb || !d.empty() || !b.violations().empty())
                    ::test::fail(__FILE__, __LINE__, "%s page, run of %llu cut at %u (%s):%s%s", shared ? "a shared" : "an owned", (unsigned long long)c.n, c.k,
                                 side ? "LRU" : "raw", d.c_str(), b.violations().c_str());
            }
            // a cut inside a half that the run would have covered is read: one half each time, raw and LRU
            const bool inside = (c.k % 256) != 0;
            CHECK_EQ(cut_reads, inside ? 2u : 0u);
        }
}

// ================================================================================================ errors
namespace {
constexpr uint32_t E0 = 0x80000 * 32, E1 = E0 + 32;
// A depth-1 file E0 with pages 0 to 3 written; E1 its copy, the next run's image. Runs: the build, then `u` into E1.
std::vector<uint64_t> four_pages() {
    std::vector<uint64_t> full;
    for (uint64_t p = 0; p < 4; p++) { full.push_back((p << 9) | uint64_t(512) << 32); for (int k = 0; k < 512; k++) full.push_back(p * 1000 + uint64_t(k) + 1); }
    return full;
}
template <class B>
bool build(B& x) {
    const auto full = four_pages();
    x.stream(encode(full, false));
    return x.run({ctl_bench::config(false, false, false, 1, 1), uint32_t(full.size()), 20, int32_t(E0), 0, 0});
}
const std::vector<uint64_t> PATCH = {(uint64_t(2 * 512 + 7)) | uint64_t(3) << 32, 11, 12, 13, (uint64_t(9 * 512)) | uint64_t(1) << 32, 99};
// The stopped run: busy stays 1, no done, the bit set; the controller against the black box where it has the case.
void stopped_like(Bench<Updater>* a, Bench<UpdaterTop>& b, bool ok_b, int bit, const char* what) {
    if (ok_b) ::test::fail(__FILE__, __LINE__, "%s: the run ended", what);
    CHECK(b.u.running(b.t));
    CHECK_EQ(b.u.errors(b.t) & bit, bit);
    if (!a) return;
    CHECK_EQ(a->u.errors(a->t), b.u.errors(b.t));
    CHECK_EQ(a->word(33), b.word(33));   // the value that stopped it decoded in both, and nothing after it
    CHECK_EQ(a->word(34), b.word(34));
    CHECK_EQ(a->dones, b.dones);
    std::vector<std::pair<int32_t, std::vector<uint64_t>>> pa, pb;
    a->store.for_each_stored([&](int32_t at, const uint64_t* w) { pa.push_back({at, std::vector<uint64_t>(w, w + 512)}); });
    b.store.for_each_stored([&](int32_t at, const uint64_t* w) { pb.push_back({at, std::vector<uint64_t>(w, w + 512)}); });
    if (pa != pb) ::test::fail(__FILE__, __LINE__, "%s: the stores differ", what);
}
}  // namespace

// An error answer to a read: the source root's (root 2), and a half's.
TEST(ctl_error_on_a_read) {
    {
        Bench<UpdaterTop> b;
        CHECK(build(b));
        b.lpddr->faults.push_back({uint64_t(E0) * 128, uint64_t(E0) * 128 + 4095, true, false, units::axi::SLVERR, 1});
        b.stream(encode(PATCH, false));
        stopped_like(nullptr, b, b.run({ctl_bench::config(false, false, false, 1, 2), uint32_t(PATCH.size()), 20, int32_t(E1), 0, int32_t(E0)}),
                     units::updctl::Ctl::ERR_LPDDR, "the root's read");
    }
    {
        Bench<UpdaterTop> b;
        CHECK(build(b));
        // E0's data page 2, as the copy shares it: the half the patch touches
        uint32_t page2 = 0;
        { const uint64_t* r = b.store.raw_page(int32_t(E0)); page2 = FileStore::entry_of(r, 2) & 0x7FFFFFFFu; }
        b.lpddr->faults.push_back({uint64_t(page2) * 128, uint64_t(page2) * 128 + 4095, true, false, units::axi::DECERR, 0});
        b.stream(encode(PATCH, false));
        stopped_like(nullptr, b, b.run({ctl_bench::config(false, false, false, 1, 2), uint32_t(PATCH.size()), 20, int32_t(E1), 0, int32_t(E0)}),
                     units::updctl::Ctl::ERR_LPDDR, "a half's read");
    }
}
// An error answer to a write: the empty root's, and a new page's.
TEST(ctl_error_on_a_write) {
    {
        Bench<UpdaterTop> b;
        b.lpddr->faults.push_back({uint64_t(E0) * 128, uint64_t(E0) * 128 + 4095, false, true, units::axi::SLVERR, 0});
        const auto full = four_pages();
        b.stream(encode(full, false));
        stopped_like(nullptr, b, b.run({ctl_bench::config(false, false, false, 1, 1), uint32_t(full.size()), 20, int32_t(E0), 0, 0}),
                     units::updctl::Ctl::ERR_LPDDR, "the root's write");
    }
    {
        Bench<UpdaterTop> b;
        CHECK(build(b));
        // the first free page the next run takes: the head of the queue the build left
        const uint64_t p = uint64_t(b.u.ctl.q.fp[b.u.ctl.q.fp_head]) * 128;
        b.lpddr->faults.push_back({p, p + 4095, false, true, units::axi::SLVERR, 0});
        b.stream(encode(PATCH, false));
        stopped_like(nullptr, b, b.run({ctl_bench::config(false, false, false, 1, 2), uint32_t(PATCH.size()), 20, int32_t(E1), 0, int32_t(E0)}),
                     units::updctl::Ctl::ERR_LPDDR, "a new page's write");
    }
}
// A reply of lpddr_error and of not_owned to an update; lpddr_error to a look-up.
TEST(ctl_error_replies) {
    struct C { uint8_t op, status; int bit; const char* what; };
    const C cs[] = {{units::lookup::UPDATE, units::lookup::LPDDR_ERROR, units::updctl::Ctl::ERR_LPDDR, "lpddr_error to an update"},
                    {units::lookup::UPDATE, units::lookup::NOT_OWNED, units::updctl::Ctl::ERR_NOT_OWNED, "not_owned to an update"},
                    {units::lookup::LOOK_UP, units::lookup::LPDDR_ERROR, units::updctl::Ctl::ERR_LPDDR, "lpddr_error to a look-up"}};
    for (const C& c : cs) {
        Bench<UpdaterTop> b;
        CHECK(build(b));
        b.cache->inject = {c.op, c.op == units::lookup::UPDATE ? unsigned(b.cache->updates + 1) : unsigned(b.cache->lookups + 1), c.status};
        b.stream(encode(PATCH, false));
        stopped_like(nullptr, b, b.run({ctl_bench::config(false, false, false, 1, 2), uint32_t(PATCH.size()), 20, int32_t(E1), 0, int32_t(E0)}), c.bit, c.what);
    }
}
// A word beyond the depth's reach; a page needed with PAGE_COUNT 0; a root that is no page of LPDDR: against the
// black box.
TEST(ctl_error_beyond_pages_root) {
    {
        Bench<Updater> a;
        Bench<UpdaterTop> b;
        CHECK(build(a)); CHECK(build(b));
        const std::vector<uint64_t> u = {(uint64_t(2 * 512)) | uint64_t(2) << 32, 5, 6, (uint64_t(1024 * 512)) | uint64_t(1) << 32, 7};
        a.stream(encode(u, false)); b.stream(encode(u, false));
        const Run r{ctl_bench::config(false, false, false, 1, 2), uint32_t(u.size()), 20, int32_t(E1), 0, int32_t(E0)};
        a.run(r);
        stopped_like(&a, b, b.run(r), units::updctl::Ctl::ERR_BEYOND, "beyond");
    }
    {
        Bench<Updater> a;
        Bench<UpdaterTop> b;
        CHECK(build(a)); CHECK(build(b));
        a.stream(encode(PATCH, false)); b.stream(encode(PATCH, false));
        const Run r{ctl_bench::config(false, false, false, 1, 2), uint32_t(PATCH.size()), 0, int32_t(E1), 0, int32_t(E0)};
        a.run(r);
        stopped_like(&a, b, b.run(r), units::updctl::Ctl::ERR_OUT_OF_PAGES, "out of pages");
    }
}
// A root off a page is UB (17.10.1, the owner's S11): the simulator stops, and ERRORS.lpddr_error is not set for it.
DEATH_TEST(ctl_root_off_a_page_is_ub, "a run's root that is no page of LPDDR") {
    Bench<UpdaterTop> b;
    b.stream(encode(PATCH, false));
    b.run(Run{ctl_bench::config(false, false, false, 1, 1), uint32_t(PATCH.size()), 20, int32_t(E0 + 5), 0, 0});
}
DEATH_TEST(ctl_root_bit_31_is_ub, "with bit 31 set") {
    Bench<UpdaterTop> b;
    b.stream(encode(PATCH, false));
    b.run(Run{ctl_bench::config(false, false, false, 1, 0), uint32_t(PATCH.size()), 20, int32_t(0x80000000u | uint32_t(E0)), 0, 0});
}
DEATH_TEST(ctl_run_past_index_2_32_is_ub, "a run of values past word index 2") {
    Bench<UpdaterTop> b;
    CHECK(build(b));
    const std::vector<uint64_t> u = {uint64_t(0xFFFFFFF0u) | uint64_t(32) << 32, 1};
    b.stream(encode(u, false));
    b.run(Run{ctl_bench::config(false, false, false, 3, 0), uint32_t(u.size()), 20, int32_t(E0), 0, 0});
}
// A push into a full free page queue: the bit set, the push dropped, nothing stopped. (The segment queue's overrun
// went with the queue: the word port cannot overrun, its `ready` holds the source.)
TEST(ctl_error_full_queue) {
    Bench<Updater> a;
    Bench<UpdaterTop> b;
    for (unsigned k = 0; k < 257; k++) {
        a.u.accept(a.t - 3, 64, Updater::PART_FREE_PAGES << 20, int(units::lw::SIZE_128), 32 * (k + 1), 0);
        b.u.accept(b.t - 3, 64, Updater::PART_FREE_PAGES << 20, int(units::lw::SIZE_128), 32 * (k + 1), 0);
        a.cycle(); a.t++; b.cycle(); b.t++;
    }
    CHECK_EQ(a.u.errors(a.t), int(Updater::ERR_QUEUE_OVERRUN));
    CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_QUEUE_OVERRUN));
    CHECK_EQ(a.u.free_page_entries(), 256u);
    CHECK_EQ(b.u.free_page_entries(), 256u);
    CHECK(!b.u.running(b.t));
}

// The port's sink, as the black box's (test_updater's): four words held behind it, idle or busy; ready 0 while four
// are held; emptied by the reset; EVENTS [2] reads 0; region part 1 is unmapped. (That a START keeps them is every
// run of ctl_frames_back_to_back.)
TEST(ctl_port_holds_four_words) {
    Bench<Updater> a;
    Bench<UpdaterTop> b;
    std::vector<Word> w;
    for (uint64_t k = 0; k < 9; k++) w.push_back({0x1000 + k, k});
    a.stream(w); b.stream(w);
    for (int k = 0; k < 12; k++) { a.cycle(); a.t++; b.cycle(); b.t++; }
    CHECK_EQ(a.u.input_words(), 4u);                      // idle: both take four and hold the source
    CHECK_EQ(b.u.input_words(), 4u);
    CHECK_EQ(a.sent, 4u);
    CHECK_EQ(b.sent, 4u);
    CHECK(!a.u.port_sink().ready);
    CHECK(!b.u.port_sink().ready);
    CHECK(b.u.ev.value(b.t) & 1u << UpdaterTop::EV_FREE_PAGE_CREDIT);   // EVENTS [2]: free_page_credit, 2 at the reset (17.10.1)
    b.u.reset(b.t);
    CHECK_EQ(b.u.input_words(), 0u);                      // only the reset empties them
    CHECK(b.u.port_sink().ready);
}
DEATH_TEST(ctl_region_part_1_is_unmapped, "a WRITE to part 1 of its region, which maps part 2 alone") {
    Bench<UpdaterTop> b;
    b.u.accept(b.t - 3, 64, 1u << 20, int(units::lw::SIZE_128), 1, 0);
}

// Frames back to back in one stream, as Ingress's forward count may offer the next frame while the decoder still holds
// the last one's tail (the next count written when the frame's header arrives): each frame a new file (root 1,
// continue 0), raw or LRU, its last codes often short hits. The decoder must take no word it does not need, or the
// next frame's first word is lost when continue 0 clears it. The port offers every frame from the start; the controller
// with its timing varied and fixed, the black box, and the model.
TEST(ctl_frames_back_to_back) {
    for (uint64_t seed = 1; seed <= 40; seed++) {
        test::Rng rng(seed * 977);
        std::vector<Word> stream;
        std::vector<Model> files;
        std::vector<Run> runs;
        for (int f = 0; f < 4; f++) {
            std::vector<uint64_t> u;
            const uint64_t page = rng.below(1000);
            const unsigned n = 1 + unsigned(rng.below(40));
            u.push_back((page << 9) | uint64_t(n) << 32);
            for (unsigned k = 0; k < n; k++) u.push_back(rng.below(4) ? 7 : rng.next());   // mostly one value: LRU hits of 6 bits
            Model m; m.apply(u); files.push_back(m);
            const bool lru = (seed + f) % 3 != 0;
            const auto w = encode(u, lru);
            stream.insert(stream.end(), w.begin(), w.end());
            runs.push_back({ctl_bench::config(lru, false, false, 1, 1), uint32_t(u.size()), 4, int32_t(0x80000 * 32 + 32 * f), 0, 0});
        }
        Bench<UpdaterTop> b(timing(seed % 2 ? seed : 0));
        Bench<Updater> a;
        Bench<UpdaterTop> c;
        b.stream(stream); a.stream(stream); c.stream(stream);
        for (size_t f = 0; f < runs.size(); f++) {
            const bool okb = b.run(runs[f]), oka = a.run(runs[f]), okc = c.run(runs[f]);
            const std::string d = ctl_bench::compare(a, c);
            if (!okb || !oka || !okc || !d.empty()) {
                ::test::fail(__FILE__, __LINE__, "seed %llu, frame %zu: ended %d %d %d;%s", (unsigned long long)seed, f, okb, oka, okc, d.c_str());
                break;
            }
            const auto want = files[f].image();
            CHECK(updater_bench::image(b.store, runs[f].image, 1) == want);
            CHECK(updater_bench::image(a.store, runs[f].image, 1) == want);
        }
        CHECK(b.violations().empty());
    }
}

// ================================================================================================ the figures
// What a page costs, by kind, on a depth-2 file with the timing fixed (the test LPDDR's 70 and 70, the stand-in's 80 a
// line and 80 a write): each case one run of the Updater holding one run of values, the data and the free pages already
// there, on the controller (port mode) and on the black box. Prints the table; checks that each run ends, and the
// controller's test pieces' checks.
namespace {
struct PageFigure { uint32_t cycles = 0, stall_data = 0, stall_pages = 0, stall_memory = 0, reads = 0, writes = 0; uint64_t lookups = 0, updates = 0; };
template <class U>
PageFigure page_figure(Bench<U>& x, const std::vector<uint64_t>& base, const std::vector<uint64_t>& u, unsigned root, bool tracer, uint32_t image) {
    const uint32_t F0 = 0x80000 * 32, T2 = F0 + 96;
    x.stream(encode(base, false));
    if (!x.run({ctl_bench::config(false, false, false, 2, 1), uint32_t(base.size()), 20, int32_t(F0), 0, 0})) ::test::fail(__FILE__, __LINE__, "the build did not end");
    for (int k = 0; k < 300; k++) { x.cycle(); x.t++; }
    x.stream(encode(u, false));
    for (int k = 0; k < 20; k++) { x.cycle(); x.t++; }
    uint64_t l0 = 0, u0 = 0;
    if constexpr (std::is_same_v<U, UpdaterTop>) { l0 = x.u.ctl.lookups; u0 = x.u.ctl.updates; }
    if (!x.run({ctl_bench::config(false, false, tracer, 2, root), uint32_t(u.size()), 20, int32_t(image), int32_t(tracer ? T2 : 0), int32_t(F0)}))
        ::test::fail(__FILE__, __LINE__, "the run did not end");
    const auto& a = x.accounts.back();
    PageFigure f;
    f.cycles = a.cycles; f.stall_data = a.stall_data; f.stall_pages = a.stall_pages; f.stall_memory = a.stall_memory; f.reads = a.pages_read; f.writes = a.pages_written;
    if constexpr (std::is_same_v<U, UpdaterTop>) { f.lookups = x.u.ctl.lookups - l0; f.updates = x.u.ctl.updates - u0; }
    return f;
}
}  // namespace
TEST(ctl_figures_a_page) {
    const uint32_t F0 = 0x80000 * 32, F1 = F0 + 32, F2 = F0 + 64;
    std::vector<uint64_t> base;                    // F0: pages 0 to 3, written whole
    for (uint64_t p = 0; p < 4; p++) { base.push_back((p << 9) | uint64_t(512) << 32); for (int k = 0; k < 512; k++) base.push_back(p * 7 + uint64_t(k) + 1); }
    auto run_of = [](uint64_t start, uint64_t n) { std::vector<uint64_t> u{start | n << 32}; for (uint64_t k = 0; k < n; k++) u.push_back(0x5000 + k); return u; };
    struct Case { const char* name; std::vector<uint64_t> u; unsigned root; bool tracer; uint32_t image; };
    const Case cases[] = {
        {"owned: 1 value (a half read, written in place)", run_of(1 * 512 + 3, 1), 0, false, F0},
        {"owned: a whole page (nothing read)", run_of(2 * 512, 512), 0, false, F0},
        {"new, its index page owned: a whole page", run_of(9 * 512, 512), 0, false, F0},
        {"new, its index page owned: 1 value (a half filled)", run_of(10 * 512 + 5, 1), 0, false, F0},
        {"new, a hole at level 0: 1 value (a new index page too)", run_of(2048 * 512 + 5, 1), 0, false, F0},
        {"copied, shared at level 0: 1 value (an index page and the page copied)", run_of(3 * 512 + 5, 1), 2, false, F1},
        {"copied, shared at level 0: a whole page (the index page copied)", run_of(3 * 512, 512), 2, false, F1},
        {"with the Tracer's copy, shared at level 0: 1 value", run_of(3 * 512 + 5, 1), 2, true, F2},
        {"with the Tracer's copy, shared at level 0: a whole page", run_of(3 * 512, 512), 2, true, F2},
    };
    std::printf("  %-72s %7s %7s %5s %5s %5s %6s  %s\n", "the page (the root's copy, with root 2, in the run's figures)", "cycles", "bb's", "looks", "upds",
                "reads", "writes", "waiting: data, pages, memory");
    for (const Case& c : cases) {
        Bench<UpdaterTop> b(timing());
        Bench<Updater> a;
        const PageFigure f = page_figure(b, base, c.u, c.root, c.tracer, c.image), g = page_figure(a, base, c.u, c.root, c.tracer, c.image);
        std::printf("  %-72s %7u %7u %5llu %5llu %5u %6u  %u, %u, %u\n", c.name, f.cycles, g.cycles, (unsigned long long)f.lookups, (unsigned long long)f.updates,
                    f.reads, f.writes, f.stall_data, f.stall_pages, f.stall_memory);
        CHECK(b.violations().empty());
    }
}
// A whole proof's runs on gold's fixtures, the controller against the black box, the timing fixed:
// each run's cycles and where the controller waited. Prints the table; the runs are checked as ctl_gold_against_*.
TEST(ctl_figures_runs) {
    for (const char* name : {"kin-big", "divs", "jumps-base"}) {
        frames::File file;
        std::string err;
        if (!file.read(std::string("tests/updater/") + name + ".frames", err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
        std::vector<ctl_bench::Step> steps;
        if (!ctl_bench::plan(file, 64, 20, steps, err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
        Bench<Updater> a;
        Bench<UpdaterTop> b;
        ctl_bench::Images ia, ib;
        std::printf("  %s, s 20: %-18s %6s %8s %8s %6s %6s  %s\n", name, "run", "words", "cycles", "bb's", "reads", "writes", "the controller waiting: data, pages, memory");
        for (size_t k = 0; k < steps.size(); k++) {
            CHECK(ctl_bench::execute(a, file, steps, k, ia));
            CHECK(ctl_bench::execute(b, file, steps, k, ib));
            const auto& x = a.accounts.back();
            const auto& y = b.accounts.back();
            std::printf("  %*s %-18s %6u %8u %8u %6u %6u  %u, %u, %u\n", int(std::strlen(name) + 6), "", steps[k].name.c_str(), steps[k].run.words, y.cycles,
                        x.cycles, y.pages_read, y.pages_written, y.stall_data, y.stall_pages, y.stall_memory);
        }
    }
}
