// test_tracer_rig.cpp (WORKER21): TracerRig's own pieces against Java. JavaLongHashMap, the order ref's rig hands its
// pages to FileStore in (which decides their addresses and so the caches' timing; tracer_rig.h), against
// java.util.HashMap on every case of tests/tracersuites/hashorder.txt (tests/ref/JavaHashOrder.java: put and
// computeIfAbsent, growth, a filling bin, high and negative keys, repeats, TracerTest's own page sets). Run from sim/.
#include <cstdlib>
#include <fstream>
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
