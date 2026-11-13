// test_statefp.cpp: the fingerprint library (statefp.h) against ref's own files (sim/tests/golden-m2) and by hand.
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"
#include "statefp.h"

// Strings: CHECK_EQ is for integers.
#define STR_EQ(a, b)                                                                                        \
    do {                                                                                                    \
        const std::string sa_ = (a), sb_ = (b);                                                             \
        if (sa_ != sb_) { ::test::fail(__FILE__, __LINE__, "%s: '%s' != '%s'", #a, sa_.c_str(), sb_.c_str()); return; } \
    } while (0)

namespace {

// Streams whose hashes the golden shows at cycle 0 of regs.fp: t0.inj and ep are four zero words (two empty holds; two
// empty FIFOs), t0.hist and t0.req five (HIST's sum, an empty pipe, an empty line and FIFO; the request port idle).
uint64_t zeros(unsigned n) { fp::FpOut o; for (unsigned i = 0; i < n; i++) o.w("z", 0); return o.done(); }

TEST(hash_matches_ref_on_known_streams) {
    CHECK_EQ(zeros(4), 0xc1091b265f166cd6ull);
    CHECK_EQ(zeros(5), 0x02d95a98a977510eull);
    fp::FpOut inj;                                  // the injection unit's stream, empty: readReturn, send
    fp::hold_empty(inj, "readReturn"); fp::hold_empty(inj, "send");
    CHECK_EQ(inj.done(), 0xc1091b265f166cd6ull);
    fp::FpOut ep;                                   // two empty FIFOs
    fp::fifo_empty(ep, "memsub.rx"); fp::fifo_empty(ep, "cc3.rx");
    CHECK_EQ(ep.done(), 0xc1091b265f166cd6ull);
}

TEST(golden_regs_loads_and_every_chip_line_combines) {
    fp::FpGolden g = fp::FpGolden::load("tests/golden-m2/regs.fp");
    CHECK(g.error.empty());
    CHECK_EQ(g.points.size(), size_t(872));
    STR_EQ(g.config.at("readyLatency"), "8");
    STR_EQ(g.config.at("K"), "8");
    for (const fp::FpPoint& p : g.points) {
        CHECK_EQ(p.lines.size(), size_t(21));       // tile 0's 16 units, the 4 chip units, chip
        STR_EQ(p.lines.back().unit, "chip");
        std::vector<uint64_t> h;
        for (size_t i = 0; i + 1 < p.lines.size(); i++) h.push_back(p.lines[i].hash);
        CHECK_EQ(fp::combine(h.data(), h.size()), p.lines.back().hash);
    }
    for (const char* f : {"tests/golden-m2/wfi.fp", "tests/golden-m2/a2a.fp"}) {
        fp::FpGolden w = fp::FpGolden::load(f);
        CHECK(w.error.empty());
        for (const fp::FpPoint& p : w.points) {
            std::vector<uint64_t> h;
            for (size_t i = 0; i + 1 < p.lines.size(); i++) h.push_back(p.lines[i].hash);
            CHECK_EQ(fp::combine(h.data(), h.size()), p.lines.back().hash);
        }
    }
}

TEST(memsum_incremental_equals_full_scan) {
    test::Rng r(7);
    std::vector<uint32_t> m(4096, 0);
    fp::MemSum s(fp::R_DMEM);
    for (int k = 0; k < 200000; k++) {
        uint32_t i = uint32_t(r.below(m.size()));
        uint32_t v = r.below(4) == 0 ? 0 : uint32_t(r.next());
        s.write(i, m[i], v);
        m[i] = v;
    }
    CHECK_EQ(s.sum, fp::MemSum::scan(fp::R_DMEM, m.data(), m.size()));
    // banked: word w in bank w & 3, row w >> 2, indexed by word (StateFp.banked)
    uint64_t banked = 0;
    for (unsigned b = 0; b < 4; b++) {
        std::vector<uint32_t> bank;
        for (size_t w = b; w < m.size(); w += 4) bank.push_back(m[w]);
        banked += fp::MemSum::scan(fp::R_DMEM, bank.data(), bank.size(), 4, b);
    }
    CHECK_EQ(banked, s.sum);
    std::vector<uint32_t> z(64, 0);                 // zero-free: an empty memory sums to 0
    CHECK_EQ(fp::MemSum::scan(fp::R_SRAM, z.data(), z.size()), uint64_t(0));
}

TEST(header_check_refuses_naming_the_difference) {
    fp::FpGolden g = fp::FpGolden::load("tests/golden-m2/regs.fp");
    CHECK(g.error.empty());
    std::vector<std::string> ours = g.header;
    STR_EQ(g.check_header(ours), "");
    for (std::string& s : ours) if (s == "# config readyLatency 8") s = "# config readyLatency 1";
    STR_EQ(g.check_header(ours), "refused (D12): config readyLatency: golden 8, ours 1");
    ours = g.header;
    ours.push_back("# config extraKnob 3");
    STR_EQ(g.check_header(ours), "refused (D12): the golden has no config extraKnob (ours: 3)");
    ours = g.header;
    for (size_t i = 0; i < ours.size(); i++) if (ours[i] == "# config K 8") { ours.erase(ours.begin() + long(i)); break; }
    STR_EQ(g.check_header(ours), "refused (D12): config K 8 in the golden, unknown here");
    ours = g.header;
    ours[1] = "# M2Prog wfi seed -1";
    CHECK(g.check_header(ours).find("refused (D12): header line 2: golden '# M2Prog regs seed -1'") == 0);
}

TEST(compare_names_the_first_differing_cycle_and_unit) {
    fp::FpGolden g = fp::FpGolden::load("tests/golden-m2/regs.fp");
    CHECK(g.error.empty());
    const fp::FpPoint& p = g.points[100];
    std::vector<fp::FpLine> ours = p.lines;
    CHECK(!fp::compare(p, ours).any);
    ours[3].hash ^= 1;                              // t0.dir
    fp::FpDiff d = fp::compare(p, ours);
    CHECK(d.any);
    CHECK_EQ(d.cycle, p.cycle);
    STR_EQ(d.unit, "t0.dir");
    CHECK(d.why.find("hash golden") == 0);
    ours = p.lines;
    ours.pop_back();
    d = fp::compare(p, ours);
    CHECK(d.any && d.unit == "chip" && d.why == "missing on our side");
}

// A registry whose writers produce known streams: the point's lines and its chip line as ref writes them.
void w_zeros4(const void*, fp::FpOut& o, const fp::FpCtx&) { for (int i = 0; i < 4; i++) o.w("z", 0); }
void w_zeros5(const void*, fp::FpOut& o, const fp::FpCtx&) { for (int i = 0; i < 5; i++) o.w("z", 0); }
void w_value(const void* u, fp::FpOut& o, const fp::FpCtx& x) {
    o.w("v", *static_cast<const uint64_t*>(u)); o.wi("x", 5, x.cycle);
}

TEST(registry_writes_a_point_as_ref_does) {
    fp::FpRegistry reg;
    uint64_t v = 42;
    for (const char* u : fp::kTileUnits) reg.add(std::string("t3.") + u, std::strcmp(u, "req") == 0 ? w_zeros5 : w_value, &v);
    for (const char* u : fp::kChipUnits) reg.add(u, w_zeros4, nullptr);
    std::vector<std::string> out;
    reg.point(fp::FpCtx{17, nullptr}, {3}, false, out);
    CHECK_EQ(out.size(), size_t(15 + 4 + 1));       // sram.data left out
    STR_EQ(out[14], "17 t3.req 02d95a98a977510e");
    STR_EQ(out[15], "17 cnc c1091b265f166cd6");
    std::vector<fp::FpLine> l = fp::parse_lines(out);
    std::vector<uint64_t> h;
    for (size_t i = 0; i + 1 < l.size(); i++) h.push_back(l[i].hash);
    STR_EQ(l.back().unit, "chip");
    CHECK_EQ(l.back().hash, fp::combine(h.data(), h.size()));
}

TEST(dump_writes_named_words_in_refs_format) {
    fp::FpRegistry reg;
    uint64_t v = 0xabc;
    reg.add("t0.core", w_value, &v);
    char* buf = nullptr; size_t len = 0;
    FILE* f = open_memstream(&buf, &len);
    reg.dump("t0.core", fp::FpCtx{9, nullptr}, f);
    std::fclose(f);
    STR_EQ(std::string(buf), "# fpdump cycle 9 unit t0.core\nt0.core v abc\nt0.core x5 9\n");
    free(buf);
    fp::FpOut o(nullptr, "u");                      // a dump-less stream with names hashes the same as a dumped one
    o.wi("x", 5, 9); o.wp("portB", ".n", 0); o.wpi("ret", 3, ".n", 0);
    fp::FpOut p;
    p.w("a", 9); p.w("b", 0); p.w("c", 0);
    CHECK_EQ(o.done(), p.done());
}

DEATH_TEST(registry_refuses_a_second_writer, "a second writer for t0.core") {
    fp::FpRegistry reg;
    reg.add("t0.core", w_zeros4, nullptr);
    reg.add("t0.core", w_zeros4, nullptr);
}

}  // namespace
