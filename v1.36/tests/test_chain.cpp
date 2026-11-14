// The chain's pieces (tests/chain.h), on what `make check` has without the runner's data:
//   - the JSON reader (proof.json, the delivery's manifest);
//   - the segment file: segments written in frames 2-5 (raw runs and the LRU coding) and read back;
//   - the inputs: a small set written in their formats, loaded, the store's pages and the checks' refusals;
//   - the landings on the real replayer tables: every header word, the record, the advice, the expected end state, the
//     tiles' headers; M3SRAM01's and M3FILES1's streams;
//   - the oracle's files (tests/golden-chain) parsed; stream_values against packed values.
// The landings' and the start memory's equality with `ref` (golden-chain's sha256s) is checked on the runner.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "chain.h"
#include "harness.h"
#include "units/tracer.h"

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
uint32_t le32(const std::vector<uint8_t>& b, size_t at) { return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24; }

uint64_t rng = 0x9E3779B97F4A7C15ull;
uint64_t next() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

// The landings as a lookup: (unit, chunk) -> value (0 when absent).
uint32_t at(const std::vector<t2::SramLanding>& l, uint32_t u, uint32_t c) {
    for (const auto& x : l) if (x.unit == u && x.chunk == c) return x.value;
    return 0;
}

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
        // M3FILES1: the header, then every page with its address
        const std::vector<uint8_t> s = files_stream(fs);
        CHECK(std::memcmp(s.data(), "M3FILES1", 8) == 0);
        CHECK_EQ(le32(s, 8), uint32_t(fs.next_free()));
        CHECK_EQ(le32(s, 12), uint32_t(fs.pages_allocated()));
        CHECK_EQ(s.size(), 16 + fs.pages_allocated() * (4 + 4096));
        CHECK_EQ(le32(s, 16), 32u);
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

TEST(chain_landings) {
    const units::tracer::ReplayTables tb = units::tracer::ReplayTables::load(units::tracer::ReplayTables::default_dir());
    W26 w;
    w.log2k = 23; w.lowest = 0x7e000000; w.hash_k = 13; w.hash_p1 = int32_t(0x9e3779b1u); w.hash_p3 = 10368899;
    w.img = {32, 2}; w.map = {16032, 1};
    Seg sg, nx;
    for (int i = 0; i < 131; i++) { sg.state[i] = next(); nx.state[i] = next(); }
    sg.advice = {11, 22, 33};
    std::vector<t2::SramLanding> l;
    landings(tb, w, sg, &nx, 64, l);
    for (size_t i = 1; i < l.size(); i++) CHECK(l[i - 1].unit < l[i].unit || (l[i - 1].unit == l[i].unit && l[i - 1].chunk < l[i].chunk));
    for (const auto& x : l) CHECK(x.value != 0 && (x.unit < 64 || x.unit == 66));
    // C&C 3's header
    CHECK_EQ(at(l, 66, C_HDR + 0), TL_MAGIC); CHECK_EQ(at(l, 66, C_HDR + 1), TL_VERSION);
    CHECK_EQ(at(l, 66, C_HDR + 4), uint32_t(tb.index_bytes.size() / 16));
    CHECK_EQ(at(l, 66, C_HDR + 8), C_EXPECT); CHECK_EQ(at(l, 66, C_HDR + 10), 3u); CHECK_EQ(at(l, 66, C_HDR + 11), 65536u);
    CHECK_EQ(at(l, 66, C_HDR + 12), 0x9e3779b1u); CHECK_EQ(at(l, 66, C_HDR + 13), 10368899u); CHECK_EQ(at(l, 66, C_HDR + 14), 13u);
    CHECK_EQ(at(l, 66, C_HDR + 15), 64u); CHECK_EQ(at(l, 66, C_HDR + 19), S_WLOG);
    // the record: state, idx | off << 32, advice count << 38, the registers, the files word
    CHECK_EQ(at(l, 66, C_RECORD + 0), uint32_t(sg.state[0])); CHECK_EQ(at(l, 66, C_RECORD + 1), uint32_t(sg.state[0] >> 32));
    CHECK_EQ(at(l, 66, C_RECORD + 2), uint32_t(sg.state[1])); CHECK_EQ(at(l, 66, C_RECORD + 3), uint32_t(sg.state[2] & 0xFFFF));
    CHECK_EQ(at(l, 66, C_RECORD + 7), uint32_t((uint64_t(3) << 38) >> 32));
    CHECK_EQ(at(l, 66, C_RECORD + 8 + 2 * 127), uint32_t(sg.state[130]));
    uint64_t fw[2];
    units::tracer::Tracer::files_word(32, 2, 16032, 1, 23, 0x7e000000, fw);
    CHECK_EQ(at(l, 66, C_RECORD + 264), uint32_t(fw[0])); CHECK_EQ(at(l, 66, C_RECORD + 267), uint32_t(fw[1] >> 32));
    // the advice (padded to even) and the expected end state
    CHECK_EQ(at(l, 66, C_ADVICE + 4), 33u); CHECK_EQ(at(l, 66, C_ADVICE + 6), 0u);
    CHECK_EQ(at(l, 66, C_EXPECT + 1), uint32_t(nx.state[0] >> 32));
    CHECK_EQ(at(l, 66, C_EXPECT + 8 + 2 * 127 + 1), uint32_t(nx.state[130] >> 32));
    // the tables in C&C 3's layout and every tile's
    const std::vector<uint8_t> t0(tb.decode_bytes);
    CHECK_EQ(at(l, 66, C_TABLES), le32(t0, 0));
    CHECK_EQ(at(l, 17, S_TABLES + 5), le32(t0, 20));
    CHECK_EQ(at(l, 63, S_TEMPLATES + 3), le32(tb.template_bytes, 12));
    for (uint32_t t : {0u, 31u, 63u}) {
        CHECK_EQ(at(l, t, S_HDR + 0), RL_MAGIC); CHECK_EQ(at(l, t, S_HDR + 1), 5u); CHECK_EQ(at(l, t, S_HDR + 8), ~0u);
        CHECK_EQ(at(l, t, S_HDR + 13), 1u); CHECK_EQ(at(l, t, S_HDR + 14), 65536u | 3u << 24); CHECK_EQ(at(l, t, S_HDR + 15), 2u);
    }
    // the last segment: no expected end state; fewer windows: the idle tiles wait on the barriers
    landings(tb, w, sg, nullptr, 64, l);
    CHECK_EQ(at(l, 66, C_HDR + 8), 0u);
    CHECK_EQ(at(l, 66, C_EXPECT + 1), 0u);
    landings(tb, w, sg, &nx, 8, l);
    CHECK_EQ(at(l, 66, C_HDR + 8), 0u); CHECK_EQ(at(l, 66, C_HDR + 15), 8u);
    CHECK_EQ(at(l, 7, S_HDR + 13), 1u); CHECK_EQ(at(l, 8, S_HDR + 13), 0u); CHECK_EQ(at(l, 8, S_HDR + 15), 1u);
    // M3SRAM01: unit, count, then (chunk, value), unit by unit
    const std::vector<uint8_t> s = sram_stream(l);
    CHECK(std::memcmp(s.data(), "M3SRAM01", 8) == 0);
    size_t p = 8, words = 0;
    uint32_t last = 0;
    while (p < s.size()) {
        const uint32_t u = le32(s, p), n = le32(s, p + 4);
        CHECK(p == 8 || u > last);
        last = u; p += 8 + 8 * size_t(n); words += n;
    }
    CHECK_EQ(p, s.size());
    CHECK_EQ(words, l.size());
}

TEST(chain_oracle_files) {
    std::string err;
    for (const char* f : {"tests/golden-chain/evm.starts", "tests/golden-chain/cyclone.starts"}) {
        std::vector<Start> st;
        CHECK(load_starts(f, st, err));
        CHECK_EQ(st.size(), 128u);
        unsigned with_sram = 0;
        for (const Start& x : st) { CHECK(x.present && x.sha.size() == 64 && x.pages > 0); if (!x.sram_sha.empty()) with_sram++; }
        CHECK_EQ(with_sram, 5u);
        CHECK(!st[10].sram_sha.empty() && !st[127].sram_sha.empty());
    }
}

TEST(chain_manifest) {
    const std::string d = tmpdir("dl");
    write(d + "/p.manifest.json", R"({"programs": [{"name": "p", "window_entries": [{"index": 0, "files": {"x": 1}}, {"index": 1},
        {"index": 2, "files": {}}]}, {"window_entries": [{"index": 9, "files": 1}]}]})");
    std::set<unsigned> w;
    std::string err;
    CHECK(manifest_windows(d, "p", w, err));
    CHECK(w == std::set<unsigned>({0, 2}));
    CHECK(!manifest_windows(d, "q", w, err));
}

TEST(chain_stream_values) {
    for (unsigned bits : {32u, 64u}) {
        const size_t n = 37;
        std::vector<uint64_t> v(n), words((n * bits + 127) / 128 * 2, 0);
        for (size_t k = 0; k < n; k++) {
            v[k] = bits == 64 ? next() : next() & 0xFFFFFFFF;
            for (unsigned b = 0; b < bits; b++) {
                const uint64_t pos = k * bits + b;
                words[2 * (pos / 128) + (pos % 128) / 64] |= ((v[k] >> b) & 1) << (pos % 64);
            }
        }
        bool sh = true;
        CHECK(stream_values(words, n, bits, sh) == v);
        CHECK(!sh);
        words.resize(words.size() - 2);
        stream_values(words, n, bits, sh);
        CHECK(sh);
    }
}


