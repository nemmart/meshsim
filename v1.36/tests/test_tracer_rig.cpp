// test_tracer_rig.cpp: the rig's builders (tests/tracer_rig.h).
//  - JavaLongHashMap, the order v1.3's rig handed its pages to FileStore in (which decides their addresses, and so the
//    memory system's model's sets; tracer_rig.h), against java.util.HashMap on every case of
//    tests/tracersuites/hashorder.txt (JavaHashOrder.java: put and computeIfAbsent, growth, a filling bin, high and
//    negative keys, repeats, TracerTest's own page sets).
//  - Pages: the order the rig's own guests come out in.
//  - The rig on the RTL bench's controller: a window's timing against the bench's rules (tests/tracer_block.h).
// Run from v1.36/.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "harness.h"
#include "tracer_rig.h"

using tracer_rig::JavaLongHashMap;

TEST(java_long_hash_map_equals_java) {
    std::ifstream f("tests/tracersuites/hashorder.txt");
    CHECK(f.good());
    std::string ops_line, order_line;
    int cases = 0;
    while (std::getline(f, ops_line) && std::getline(f, order_line)) {
        std::istringstream a(ops_line), b(order_line);
        std::string w;
        a >> w;
        CHECK(w == "ops");
        JavaLongHashMap m;
        while (a >> w) {
            const int64_t k = std::strtoll(w.c_str() + 1, nullptr, 10);
            if (w[0] == 'p') m.put(k);
            else m.compute_if_absent(k);
        }
        b >> w;
        if (w == "treeified") {  // Java made a tree of a bin: the model must know it, and refuse an order
            if (!m.treeified()) { ::test::fail(__FILE__, __LINE__, "case %d: Java treeified, the model didn't", cases); return; }
            cases++;
            continue;
        }
        CHECK(w == "order");
        if (m.treeified()) { ::test::fail(__FILE__, __LINE__, "case %d: the model treeified, Java didn't", cases); return; }
        std::vector<int64_t> want;
        while (b >> w) want.push_back(std::strtoll(w.c_str(), nullptr, 10));
        if (m.order() != want) {
            ::test::fail(__FILE__, __LINE__, "case %d differs from Java: %s", cases, ops_line.c_str());
            return;
        }
        cases++;
    }
    CHECK_EQ(cases, 49);
}

TEST(pages_iterate_as_refs_rig_fills_them) {
    // TracerTest's rig(): poke the code, then the data (computeIfAbsent both): Java iterates the data page first
    tracer_rig::Pages p;
    p.poke(0x100000, std::vector<uint8_t>(32, 1));
    p.poke(0x101000, std::vector<uint8_t>(8));
    const std::vector<int64_t> want = {0x101000, 0x100000};
    CHECK(p.map.order() == want);
}


// ================================================================ the rig on the RTL bench's controller
// The bench's first cycles (tests/tracer_block.h; Snarky One's tracer_acceptance_test): cycle 0 the EVENTS_MASK store,
// 768 WRITEs of the tables one a cycle, two idle cycles, the three stores of the hash's constants: 774 cycles.
TEST(load_tables_takes_the_benchs_cycles) {
    tracer_rig::Rig g;
    CHECK_EQ(g.t, int64_t(0));
    g.load_tables();
    CHECK_EQ(g.t, int64_t(1 + 256 + 512 + 2 + 3));
    // the tables are in the Tracer's part 0, the constants in its words
    CHECK_EQ(g.tr.decode_table[5], g.tables.table[5]);
    CHECK_EQ(g.tr.rom_lo[300], g.tables.rom_lo[300]);
    CHECK_EQ(g.load_word(tracer_rig::Tracer::W_HASH_MUL), g.map_mul);
    CHECK_EQ(g.load_word(tracer_rig::Tracer::W_HASH_STEP), g.map_step);
    CHECK_EQ(g.load_word(tracer_rig::Tracer::W_HASH_LINES), uint32_t(g.map_k));
}

// A zero-row window on the bench: START's store, the rows' end the cycle after its effect, output queue 0 in the
// bench's four transfers of 1, 1, 65 and 1 entries, done; its record is the one written, the stream word's fields 0.
TEST(a_zero_row_window_returns_the_record) {
    using namespace tracer_rig;
    Rig g;
    Pages pages;
    pages.poke(0x100000, std::vector<uint8_t>(8, 0x13));
    g.build_image(pages, {});
    g.build_map({});
    g.load_tables();
    uint64_t regs[128];
    for (int k = 0; k < 128; k++) regs[k] = 0x1000 + uint64_t(k);
    g.load_record(0x80000000LL, 7, 0, regs, {});
    uint64_t want[2 * Tracer::REC_WORDS];
    g.record_words(0x80000000LL, 7, 0, regs, 0, want);
    want[4] = 0;   // register 0 reads 0
    WindowOut o;
    CHECK(g.window(0, 2000, o));
    CHECK_EQ(o.record.size(), size_t(2 * Tracer::REC_WORDS));
    for (size_t k = 0; k < o.record.size(); k++) CHECK_EQ(o.record[k], want[k]);
    CHECK_EQ(g.bench.dma_packets, int64_t(68));
    CHECK(o.code.empty() && o.rlog.empty() && o.wlog.empty());
    CHECK_EQ(g.last.rows_run, 0u);
    CHECK_EQ(g.last.status, 0u);
}
