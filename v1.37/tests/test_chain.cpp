// The readers of the pipeline's host-side files (tests/chain.h), on what `make check` has without the runner's data:
//   - the JSON reader (proof.json);
//   - the segment file: segments written in frames 2-5 (raw runs and the LRU coding) and read back;
//   - the inputs: a small set written in their formats, loaded, the store's pages and the checks' refusals.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "chain.h"
#include "harness.h"

using namespace chain;

namespace {

std::string tmpdir(const char* name) {
    const std::string d = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/w17_chain_" + std::to_string(getpid()) + "_" + name;
    mkdir(d.c_str(), 0700);
    return d;
}
void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); }
void put64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; i++) b.push_back(uint8_t(v >> (8 * i))); }
void write(const std::string& p, const std::vector<uint8_t>& b) { FILE* f = std::fopen(p.c_str(), "wb"); if (!b.empty()) std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
void write(const std::string& p, const std::string& s) { write(p, std::vector<uint8_t>(s.begin(), s.end())); }

uint64_t rng = 0x9E3779B97F4A7C15ull;
uint64_t next() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

}  // namespace

TEST(chain_json) {
    Json j;
    std::string err;
    CHECK(parse_json(R"({"a": 1, "b": [true, false, null, "x\"y"], "c": {"d": "0x7e000000", "e": -2.5e1}, "f": 18446744073709551615})", j, err));
    CHECK(j.kind == Json::OBJ);
    CHECK_EQ(j.get("a")->integer(), 1);
    CHECK_EQ(j.get("b")->arr.size(), 4u);
    CHECK(j.get("b")->arr[0].b && !j.get("b")->arr[1].b && j.get("b")->arr[2].kind == Json::NUL);
    CHECK(j.get("b")->arr[3].str == "x\"y");
    CHECK_EQ(j.get("c")->get("d")->integer(), 0x7e000000);
    CHECK(j.get("c")->get("e")->num == -25.0);
    CHECK(j.get("nope") == nullptr);
    CHECK(!parse_json("{\"a\": }", j, err));
    CHECK(!parse_json("[1, 2", j, err));
    CHECK(!parse_json("{} x", j, err));
}

TEST(chain_segments_round_trip) {
    for (int lru = 0; lru < 2; lru++) {
        std::vector<Seg> segs(4);
        for (size_t s = 0; s < segs.size(); s++) {
            for (int i = 0; i < 131; i++) segs[s].state[i] = next();
            if (s != 2) for (uint64_t k = 0, n = next() % 9 + 1; k < n; k++) segs[s].advice.push_back(next());
            if (s == 3) continue;                       // a segment that writes nothing (as 127)
            uint32_t cell = uint32_t(next() % 100000);
            for (int r = 0; r < 20; r++) {
                const unsigned run = unsigned(next() % 12) + 1;
                for (unsigned k = 0; k < run; k++) {
                    // repeated values (the LRU's hits), small (32-bit) and wide (64-bit) ones
                    const uint64_t v = next() % 4 == 0 ? 7 : next() % 3 == 0 ? next() & 0xFFFFFFFF : next();
                    segs[s].updates.push_back({cell + k, v});
                }
                cell += run + 1 + uint32_t(next() % 3000);
            }
        }
        const std::string d = tmpdir(lru ? "seglru" : "segraw");
        write(d + "/segments.seg", write_segments(segs, lru));
        std::vector<Seg> back;
        std::string err;
        CHECK(read_segments(d + "/segments.seg", back, err));
        CHECK_EQ(back.size(), segs.size());
        for (size_t s = 0; s < segs.size(); s++) {
            CHECK(std::memcmp(back[s].state, segs[s].state, sizeof segs[s].state) == 0);
            CHECK(back[s].advice == segs[s].advice);
            CHECK(back[s].updates == segs[s].updates);
            std::set<int64_t> p;
            for (auto& u : segs[s].updates) p.insert(int64_t(u.first) / 512);
            CHECK(back[s].written_pages() == std::vector<int64_t>(p.begin(), p.end()));
        }
        // a frame running past the end is refused
        std::vector<uint8_t> b = write_segments(segs, lru);
        b.resize(b.size() - 9);
        write(d + "/bad.seg", b);
        CHECK(!read_segments(d + "/bad.seg", back, err));
    }
}

namespace {
// A small input set: proof.json, three image pages, a 2^k-line map (k 5: one 4 KB page, depth 0), two segments'
// pages.
std::string make_w26(const char* name, int k, bool bad_map) {
    const std::string d = tmpdir(name);
    mkdir((d + "/seg").c_str(), 0700);
    write(d + "/proof.json", "{\"log2_k\": 23, \"lowest_address\": \"0x7e000000\", \"hash_k\": " + std::to_string(k) +
                                 ", \"hash_p1\": \"0x9e3779b1\", \"hash_p3\": 10368899, \"segments\": 2, \"other\": [1, {\"x\": null}]}");
    std::vector<uint8_t> ip;
    for (uint64_t pg : {0ull, 5ull, 1500ull}) {
        put64(ip, pg * 4096); put32(ip, 0); put32(ip, 0);
        for (int i = 0; i < 512; i++) put64(ip, pg * 1000 + uint64_t(i) + 1);
    }
    write(d + "/initial.pages", ip);
    std::vector<uint8_t> f6, lines;
    put32(f6, 5 + 32u * (1u << k)); put32(f6, 6); put32(f6, uint32_t(k)); put32(f6, 0x9e3779b1u); put32(f6, 10368899);
    for (size_t i = 0; i < (size_t(128) << k); i++) lines.push_back(uint8_t(i * 7 + 3));
    f6.insert(f6.end(), lines.begin(), lines.end());
    write(d + "/frame6.bin", f6);
    if (bad_map) lines[5] ^= 1;
    write(d + "/map.lines", lines);
    std::vector<uint8_t> s0, s1;
    put64(s0, 5 * 4096); put64(s0, 9 * 4096);
    write(d + "/seg/000.pages", s0);
    write(d + "/seg/001.pages", s1);
    return d;
}
}  // namespace

TEST(chain_w26_inputs) {
    std::string err;
    {
        FileStore fs;
        W26 w;
        CHECK(load_w26(make_w26("w26", 5, false), fs, w, err));
        CHECK_EQ(w.log2k, 23); CHECK_EQ(w.lowest, 0x7e000000); CHECK_EQ(w.hash_k, 5); CHECK_EQ(w.hash_p3, 10368899);
        CHECK_EQ(uint32_t(w.hash_p1), 0x9e3779b1u);
        CHECK_EQ(w.img_pages, 3u);
        CHECK_EQ(w.img.root, 32); CHECK_EQ(w.img.depth, 2);       // as T1Carry reports ref's (Quest 172): root 32, depth 2
        CHECK_EQ(w.map.depth, 0);
        for (uint64_t pg : {0ull, 5ull, 1500ull})
            for (int i : {0, 1, 511}) CHECK_EQ(fs.load64(w.img, int64_t(pg * 4096 + 8 * uint64_t(i))), pg * 1000 + uint64_t(i) + 1);
        CHECK_EQ(fs.load64(w.img, 7 * 4096), 0u);
        uint64_t x = 0;
        for (int b = 0; b < 8; b++) x |= uint64_t(uint8_t((8 + b) * 7 + 3)) << (8 * b);
        CHECK_EQ(fs.load64(w.map, 8), x);
        CHECK_EQ(w.written.size(), 2u);
        CHECK(w.written[0] == std::vector<int64_t>({5, 9}));
        CHECK(w.written[1].empty());
        // ownership as T1's start: page 9 is created, 5 kept; the image's other pages lose their owned bits
        const int64_t nf = fs.next_free();
        CHECK_EQ(fs.set_ownership(w.img, w.written[0]), 1);
        CHECK_EQ(fs.next_free(), nf + FileStore::PAGE_UNITS);
    }
    {
        FileStore fs;
        W26 w;
        CHECK(!load_w26(make_w26("w26bad", 5, true), fs, w, err));
        CHECK(err.find("map.lines") != std::string::npos);
    }
    {   // a 2^13-line map (as both workloads'): depth 1, 256 pages
        FileStore fs;
        W26 w;
        CHECK(load_w26(make_w26("w26big", 13, false), fs, w, err));
        CHECK_EQ(w.map.depth, 1);
        CHECK_EQ(fs.pages_allocated(), size_t(1 + 2 + 3) + 1 + 256);   // image root, two index pages, three data; map's
    }
}
