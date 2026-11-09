// The T2 harness's pieces (WORKER16; tests/t2_harness.h), on what `make check` has without the runner's data:
//   - the two dump formats (M3Inputs.java), written here in their format and read back; their refusals;
//   - the boot's staging (Mach.bootCores): C&C 1's host queue and big SRAM equal to ref's (tests/m3-images/bootstage.ref,
//     from tests/ref/bootstage.sh: counts and FNV-1a hashes), and C&C 1's image and descriptor landed through the host
//     port into its memories;
//   - CC_SEGMENT's packets; the rows' reference (rows.bin's count and the replayer's sink hash); the partition.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"
#include "t2_harness.h"
#include "units/chip.h"
#include "units/host_port.h"

using namespace units;

namespace {

std::string tmp(const char* name) { return std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/w16_t2_" + std::to_string(getpid()) + "_" + name; }

void write_gz(const std::string& path, const std::vector<uint8_t>& b) {
    FILE* p = popen(("gzip -c > '" + path + "'").c_str(), "w");
    std::fwrite(b.data(), 1, b.size(), p);
    pclose(p);
}
void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); }
void put64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; i++) b.push_back(uint8_t(v >> (8 * i))); }

uint64_t fnv(uint64_t h, uint64_t v) { for (int i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 0x100000001b3ull; } return h; }

}  // namespace

TEST(t2_files_dump_round_trip) {
    // two pages (addresses 64 and 128 in 128 B units), nextFree 160: pages 32 and 96 come back zero
    std::vector<uint8_t> b(std::begin("M3FILES1"), std::begin("M3FILES1") + 8);
    put32(b, 160); put32(b, 2);
    for (uint32_t a : {64u, 128u}) { put32(b, a); for (unsigned i = 0; i < 512; i++) put64(b, uint64_t(a) << 32 | i); }
    const std::string p = tmp("files.gz");
    write_gz(p, b);
    tracer::FileStore fs;
    std::string err;
    uint64_t pages = 0;
    CHECK(t2::load_files(p, fs, err, &pages));
    CHECK(err.empty());
    CHECK_EQ(pages, 2u);
    CHECK_EQ(fs.next_free(), 160);
    uint64_t line[16];
    fs.read_line(64 + 3, line);                                // page 64, line 3: words 48-63
    CHECK_EQ(line[0], uint64_t(64) << 32 | 48);
    fs.read_line(128, line);
    CHECK_EQ(line[15], uint64_t(128) << 32 | 15);
    fs.read_line(96, line);
    CHECK_EQ(line[5], 0u);
    // refusals: the magic, a truncated dump
    b[0] = 'X'; write_gz(p, b);
    tracer::FileStore f2;
    CHECK(!t2::load_files(p, f2, err));
    b[0] = 'M'; b.resize(b.size() - 8); write_gz(p, b);
    tracer::FileStore f3;
    CHECK(!t2::load_files(p, f3, err));
    std::remove(p.c_str());
}

TEST(t2_sram_dump_round_trip) {
    std::vector<uint8_t> b(std::begin("M3SRAM01"), std::begin("M3SRAM01") + 8);
    put32(b, 5); put32(b, 2); put32(b, 7); put32(b, 0x11); put32(b, (3u << 17) | 9); put32(b, 0x22);
    put32(b, 66); put32(b, 1); put32(b, 0x1FFFFF); put32(b, 0x33);
    const std::string p = tmp("sram.gz");
    write_gz(p, b);
    std::vector<t2::SramLanding> l;
    std::string err;
    CHECK(t2::load_sram(p, l, err));
    CHECK_EQ(l.size(), 3u);
    if (l.size() == 3) {
        CHECK_EQ(l[0].unit, 5u); CHECK_EQ(l[0].chunk, 7u); CHECK_EQ(l[0].value, 0x11u);
        CHECK_EQ(l[1].chunk, (3u << 17) | 9);
        CHECK_EQ(l[2].unit, 66u); CHECK_EQ(l[2].chunk, 0x1FFFFFu);
    }
    put32(b, 3); put32(b, 0);                                  // a unit out of order
    write_gz(p, b);
    CHECK(!t2::load_sram(p, l, err));
    std::remove(p.c_str());
}

TEST(t2_boot_staging_equal_to_refs) {
    std::string err;
    auto m = t2::build_machine(nullptr, {}, err);
    CHECK(m != nullptr);
    if (!m) { std::fprintf(stderr, "%s\n", err.c_str()); return; }
    t2::Image cc1, cc23, tile, cc3;
    for (auto [im, n] : {std::pair<t2::Image*, const char*>{&cc1, "cc1"}, {&cc23, "cc23"}, {&tile, "replay_ctl"}, {&cc3, "tracer_ctl"}})
        if (!t2::load_image("tests/m3-images", n, *im, err)) { test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
    sim::Sim& s = *m->sim;
    t2::boot_cores(s, cc1, cc23, tile, cc3, 8, 0);
    // ref's: the host queue and C&C 1's big SRAM (bootstage.ref)
    const HostPort& h = s.unit<HostPort>("cc1.host");
    uint64_t hq = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < h.count; i++) { const uint64_t* e = h.q + size_t((h.head + i) % h.capacity) * 3; hq = fnv(fnv(fnv(hq, e[0]), e[1]), e[2]); }
    const uint32_t* blk = m->sram.block(64, 1u << 20);
    uint64_t sh = 0xcbf29ce484222325ull, sn = 0;
    for (uint32_t c = 0; c < (1u << 20); c++) if (blk[c]) { sh = fnv(fnv(sh, c), blk[c]); sn++; }
    char got[128];
    std::snprintf(got, sizeof got, "hostq %u %016" PRIx64 "\nsram %" PRIu64 " %016" PRIx64 "\n", h.count, hq, sn, sh);
    std::string ref;
    {
        FILE* f = std::fopen("tests/m3-images/bootstage.ref", "r");
        CHECK(f != nullptr);
        if (!f) return;
        char line[256];
        while (std::fgets(line, sizeof line, f)) if (line[0] != '#') ref += line;
        std::fclose(f);
    }
    CHECK(ref == got);
    if (ref != got) std::fprintf(stderr, "ref:\n%sc++:\n%s", ref.c_str(), got);
    // the packets land through the host port: run until the last (the command) has gone, then a few cycles
    uint64_t c = 0;
    while (h.count > 1 && c < 1000) { s.run(1); c++; }
    s.run(8);
    const uint32_t* dm = m->dmem(64);
    const uint32_t* pm = dm + MEM_WORDS;
    CHECK_EQ(pm[0], cc1.code[0]);
    CHECK_EQ(pm[cc1.code.size() - 1], cc1.code.back());
    const uint32_t desc = (cc1.at("boot_desc")) / 4;
    CHECK_EQ(dm[desc], 0xB007DE5Cu);
    CHECK_EQ(dm[desc + 19], 8u);                                // go_latency
    CHECK(!t2::cores_booted(dm, cc1));
}

// The boot to Mach.coresBooted without the landings, against ref's (BootStage trace, boottrace.ref.gz): C&C 1-3's and
// tile 0's pc between every cycle, and the boot-end point 7,222; on one domain, on T2's split, under the debug options.
std::string boot_trace(const uint8_t* dom, sim::Options opt, int64_t* end) {
    std::string err;
    auto m = t2::build_machine(dom, opt, err);
    if (!m) return "build: " + err;
    t2::Image cc1, cc23, tile, cc3;
    for (auto [im, n] : {std::pair<t2::Image*, const char*>{&cc1, "cc1"}, {&cc23, "cc23"}, {&tile, "replay_ctl"}, {&cc3, "tracer_ctl"}})
        if (!t2::load_image("tests/m3-images", n, *im, err)) return err;
    t2::boot_cores(*m->sim, cc1, cc23, tile, cc3, 8, 0);
    std::string tr;
    *end = t2::run_boot(*m, cc1, 20000, &tr);
    return tr;
}

void check_boot(const uint8_t* dom, sim::Options opt) {
    std::vector<uint8_t> raw;
    std::string err;
    if (!t2::read_gz("tests/m3-images/boottrace.ref.gz", raw, err)) { test::fail(__FILE__, __LINE__, "%s", err.c_str()); return; }
    const std::string ref(raw.begin(), raw.end());
    int64_t end = -1;
    const std::string got = boot_trace(dom, opt, &end);
    CHECK_EQ(end, 7222);
    if (got != ref) {
        size_t p = 0, line = 1;
        while (p < got.size() && p < ref.size() && got[p] == ref[p]) { if (got[p] == '\n') line++; p++; }
        test::fail(__FILE__, __LINE__, "the boot's trace differs from ref's at line %zu", line);
    }
}

TEST(t2_boot_equal_to_refs_one_domain) { check_boot(nullptr, {}); }

TEST(t2_boot_equal_to_refs_split) {
    uint8_t d[64];
    t2::partition(6, d);
    sim::Options o; o.threads = 6;
    check_boot(d, o);
}

TEST(t2_boot_equal_to_refs_under_debug_options) {
    sim::Options a; a.shuffle_seed = 3; check_boot(nullptr, a);
    sim::Options b; b.order = sim::Options::Order::instance_major; check_boot(nullptr, b);
}

TEST(t2_cc_segment_packets) {
    std::string err;
    auto m = t2::build_machine(nullptr, {}, err);
    CHECK(m != nullptr);
    if (!m) return;
    t2::cc_segment(*m->sim);
    const HostPort& h = m->sim->unit<HostPort>("cc1.host");
    CHECK_EQ(h.count, 3u);                                      // two arguments, then the command word
    const uint64_t* w = h.q + size_t(h.head + 2) * 3;
    CHECK_EQ(uint32_t(w[0]), (0x54u << 24) | 2u);               // parity 0: the first command into C&C 1
    CHECK_EQ(lw::addr(w[1]), 0x7FC0u);
    CHECK_EQ(lw::dst(w[1]), lw::CC1);
    const uint64_t* a0 = h.q + size_t(h.head) * 3;
    CHECK_EQ(uint32_t(a0[0]), 1u << 24);
    CHECK_EQ(lw::addr(a0[1]), 0x7FC0u + 4);                     // ARGS_EVEN = 1
}

TEST(t2_rows_reference_and_hash) {
    const std::string d = tmp("delivery");
    CHECK(std::system(("mkdir -p '" + d + "'").c_str()) == 0);
    std::vector<uint64_t> rows(3 * 10);
    for (size_t i = 0; i < rows.size(); i++) rows[i] = 0x1000 + i * 7;
    for (unsigned w : {640u, 641u}) {                           // segment 10, windows 0 and 1
        char n[64]; std::snprintf(n, sizeof n, "/window_%05u.rows.bin", w);
        FILE* f = std::fopen((d + n).c_str(), "wb");
        std::fwrite(rows.data(), 8, w == 640 ? 30 : 20, f);
        std::fclose(f);
    }
    t2::RowsRef r[64];
    std::string err;
    CHECK(t2::rows_ref(d, 10, 2, r, err));
    CHECK(r[0].present && r[1].present && !r[2].present);
    CHECK_EQ(r[0].rows, 3u); CHECK_EQ(r[1].rows, 2u);
    CHECK_EQ(r[0].hash, t2::rows_hash(rows.data(), 3));
    CHECK_EQ(r[1].hash, t2::rows_hash(rows.data(), 2));
    CHECK_EQ(t2::rows_hash(rows.data() + 20, 1, t2::rows_hash(rows.data(), 2)), r[0].hash);   // running, as the sink's
    CHECK(!t2::rows_ref(d, 10, 3, r, err));                    // window 642 absent
    CHECK(std::system(("rm -rf '" + d + "'").c_str()) == 0);
}

TEST(t2_partition) {
    uint8_t d[64];
    t2::partition(1, d);
    for (unsigned k = 0; k < 64; k++) CHECK_EQ(d[k], 0u);
    t2::partition(6, d);
    CHECK_EQ(d[2], 0u);
    unsigned n[6] = {};
    for (unsigned k = 0; k < 64; k++) n[d[k]]++;
    CHECK_EQ(n[0], 1u);                                         // tile 2 (and C&C 3) alone
    for (unsigned p = 1; p < 6; p++) CHECK(n[p] >= 12 && n[p] <= 13);
    // a split build: C&C c with tile c, every crossing a Link of 3 or more
    std::string err;
    sim::Options o; o.threads = 6;
    auto m = t2::build_machine(d, o, err);
    CHECK(m != nullptr);
    if (m) CHECK(m->sim->partition().find("barrier every 3 cycles") != std::string::npos);
}
