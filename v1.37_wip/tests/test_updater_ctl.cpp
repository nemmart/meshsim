// test_updater_ctl: the Updater's controller (src/units/updater_ctl.h, updater_top.h; updater_wip, WORKER6), with the
// test LPDDR and the stand-in cache (tests/test_lpddr.h, standin_cache.h). NOTES section 9's checks:
//   - alone against gold: gold's frame files of the guests (tests/updater/) offered on the word port, no shim; every
//     image against gold's reference, PAGE_COUNT 0 at every done; the timing fixed, then varied;
//   - against the black box: the same runs on both, the controller behind the shim; at each run's end the registers,
//     the errors, the events, the stores byte for byte, the stream not yet decoded and the page counts equal;
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
void gold_alone(const char* name, const Options& o) {
    frames::File file;
    std::string err;
    if (!file.read(std::string("tests/updater/") + name + ".frames", err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
    for (unsigned s : {0u, 20u, 60u, 63u}) {
        const auto gold = gold_digests(std::string("tests/updater/") + name + "-s" + std::to_string(s) + ".images.txt");
        CHECK_EQ(gold.size(), size_t(6));
        std::vector<ctl_bench::Step> steps;
        if (!ctl_bench::plan(file, 64, s, steps, err)) { ::test::fail(__FILE__, __LINE__, "%s: %s", name, err.c_str()); return; }
        Bench<UpdaterTop> b(o);
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
    }
}
Options port(uint64_t seed = 0) {
    Options o;
    o.port = true;
    if (seed) { o.lpddr.jitter = true; o.lpddr.seed = uint32_t(seed); o.cache_seed = seed * 7 + 1; o.feeder_seed = seed * 13 + 5; }
    return o;
}

// The same plan on the black box and on the controller behind the shim, compared after every run.
void gold_against(const char* name, uint64_t seed) {
    frames::File file;
    std::string err;
    if (!file.read(std::string("tests/updater/") + name + ".frames", err)) { ::test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
    for (unsigned s : {0u, 20u, 63u}) {
        std::vector<ctl_bench::Step> steps;
        if (!ctl_bench::plan(file, 64, s, steps, err)) { ::test::fail(__FILE__, __LINE__, "%s: %s", name, err.c_str()); return; }
        Options o;
        if (seed) { o.lpddr.jitter = true; o.lpddr.seed = uint32_t(seed); o.cache_seed = seed * 3 + 1; }
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

// The scenario on the black box and on the controller behind the shim, compared after every run; both against the
// model at the end.
void random_against(uint64_t seed, uint64_t timing_seed) {
    const Scenario x = make_scenario(seed);
    Options o;
    if (timing_seed) { o.lpddr.jitter = true; o.lpddr.seed = uint32_t(timing_seed); o.cache_seed = timing_seed * 5 + 3; }
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
// The scenario on the controller alone, on its port with the feeder, against the model.
void random_alone(uint64_t seed, uint64_t timing_seed) {
    const Scenario x = make_scenario(seed);
    Bench<UpdaterTop> b(port(timing_seed));
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

TEST(ctl_gold_kin_raw) { gold_alone("kin-raw", port()); }
TEST(ctl_gold_kin_lru) { gold_alone("kin-lru", port()); }
TEST(ctl_gold_jumps_base) { gold_alone("jumps-base", port()); }
TEST(ctl_gold_divs) { gold_alone("divs", port()); }
TEST(ctl_gold_kin_big) { gold_alone("kin-big", port()); }
TEST(ctl_gold_varied) {
    for (uint64_t seed : {1, 2, 3}) { gold_alone("kin-lru", port(seed)); gold_alone("divs", port(seed)); gold_alone("jumps-base", port(seed)); }
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
// The random scenarios reach what they are for: a look-up stopping at a hole and at a shared page at every level, a
// half read that a run of values ending inside the run would have covered (the owner's note at the go-ahead), and pages
// written in runs with the Tracer's copy. Prints the counts.
TEST(ctl_random_reaches_its_cases) {
    uint64_t by[2][8][4] = {}, cut_reads = 0, tracer_writes = 0, depths[4] = {};
    for (uint64_t seed = 1; seed <= 90; seed++) {
        const Scenario x = make_scenario(seed);
        depths[x.depth]++;
        Bench<UpdaterTop> b(port());
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
    {
        Bench<Updater> a;
        Bench<UpdaterTop> b;
        a.stream(encode(PATCH, false)); b.stream(encode(PATCH, false));
        const Run r{ctl_bench::config(false, false, false, 1, 1), uint32_t(PATCH.size()), 20, int32_t(E0 + 5), 0, 0};
        a.run(r);
        stopped_like(&a, b, b.run(r), units::updctl::Ctl::ERR_LPDDR, "a root off a page");
    }
}
// A push into a full free page queue, and (through the shim) into a full segment queue: the bit set, the push
// dropped, nothing stopped. The segment queue fills four words later than the black box's: the controller's FIFO takes
// them while idle (NOTES C16).
TEST(ctl_error_full_queues) {
    Bench<Updater> a;
    Bench<UpdaterTop> b;
    for (unsigned k = 0; k < 257; k++) {
        a.u.accept(a.t - 3, 64, Updater::PART_FREE_PAGES << 20, int(units::lw::SIZE_128), 32 * (k + 1), 0);
        b.u.accept(b.t - 3, 64, Updater::PART_FREE_PAGES << 20, int(units::lw::SIZE_128), 32 * (k + 1), 0);
        a.u.step(a.t++); b.u.step(b.t++);
    }
    CHECK_EQ(a.u.errors(a.t), int(Updater::ERR_QUEUE_OVERRUN));
    CHECK_EQ(b.u.errors(b.t), int(Updater::ERR_QUEUE_OVERRUN));
    CHECK_EQ(a.u.free_page_entries(), 256u);
    CHECK_EQ(b.u.free_page_entries(), 256u);
    CHECK(!b.u.running(b.t));

    Bench<UpdaterTop> c;
    unsigned k = 0;
    for (; k < 600 && c.u.errors(c.t) == 0; k++) {
        c.u.accept(c.t - 3, 64, Updater::PART_SEGMENT << 20, int(units::lw::SIZE_128), k, 0);
        c.u.step(c.t++);
    }
    CHECK_EQ(k, 512u + units::updctl::Ctl::FIFO + 1);
    CHECK_EQ(c.u.errors(c.t), int(Updater::ERR_QUEUE_OVERRUN));
    CHECK_EQ(c.u.segment_entries(), 512u);
}
