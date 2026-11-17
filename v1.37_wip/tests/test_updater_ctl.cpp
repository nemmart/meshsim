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
