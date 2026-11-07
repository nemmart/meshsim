// The golden traces (tests/golden, TRACE-FORMAT.md): every run in RUNS matches, line for line, header and closing block
// included; the harness refuses a golden made under another configuration (D12), naming the difference; and it catches a
// difference. Run from sim/.
#include <string>

#include "golden_run.h"
#include "harness.h"
#include "java_random.h"

using namespace test;

TEST(golden_runs_listed) {
    CHECK_EQ(read_runs().size(), 67u);
}

TEST(golden_all_match_direct) {
    unsigned bad = 0;
    for (const RunSpec& r : read_runs()) {
        std::string d = check_direct(r);
        if (!d.empty()) { fprintf(stderr, "%s\n", d.c_str()); bad++; }
    }
    CHECK_EQ(bad, 0u);
}

TEST(golden_all_match_engine) {
    unsigned bad = 0;
    std::vector<sim::Options> ready(1);
    for (const RunSpec& r : read_runs()) {
        std::string d = check_engine(r, ready);
        if (!d.empty()) { fprintf(stderr, "%s\n", d.c_str()); bad++; }
    }
    CHECK_EQ(bad, 0u);
}

// Shuffle seeds 1-3, instance-major and step-all: each trace equal to the golden, each digest to the ready-driven run's.
// Enforced once the engine implements them (W3's increment 5); until then this says so and passes.
TEST(golden_engine_debug_checks) {
    if (!engine_has_debug_checks()) { printf("  (pending: the engine ignores shuffle, order and step-all until W3's increment 5)\n"); return; }
    unsigned bad = 0;
    auto opts = debug_check_options();
    for (const RunSpec& r : read_runs()) {
        std::string d = check_engine(r, opts);
        if (!d.empty()) { fprintf(stderr, "%s\n", d.c_str()); bad++; }
    }
    CHECK_EQ(bad, 0u);
}

static bool load(const char* name, RunSpec& r, Golden& g, Images& im) {
    auto runs = read_runs();
    const RunSpec* p = find_run(runs, name);
    if (!p) return false;
    r = *p;
    std::string err;
    return read_golden("tests/golden/" + r.name + ".trace", g, err) && load_images(r, im, err);
}

TEST(golden_refuses_a_config_difference) {
    RunSpec r; Golden g; Images im;
    CHECK(load("hello", r, g, im));
    for (std::string& l : g.lines) if (l == "# config mul_latency 3") l = "# config mul_latency 4";
    std::string why = check_header(g, header_lines(r, im, tile_config(r.tile)));
    CHECK(why == "refused (D12): config mul_latency: golden 4, ours 3");
}

TEST(golden_refuses_an_image_or_tile_difference) {
    RunSpec r; Golden g; Images im;
    CHECK(load("faults-t3", r, g, im));
    CHECK(check_header(g, header_lines(r, im, tile_config(r.tile))).empty());
    RunSpec r2 = r; r2.tile = 4;
    std::string tile = check_header(g, header_lines(r2, im, tile_config(3)));
    CHECK(tile.find("header line 3") != std::string::npos);
    im.code[0] ^= 1; im.code_sha = "0";
    CHECK(check_header(g, header_lines(r, im, tile_config(r.tile))).find("header line 4") != std::string::npos);
}

TEST(golden_catches_a_difference) {
    RunSpec r; Golden g; Images im;
    CHECK(load("riscv-mul", r, g, im));
    units::TileConfig c = tile_config(r.tile);
    auto header = header_lines(r, im, c);
    c.timing.mul_latency = 4;                          // a core that differs from ref
    Tile t(im, c, uint32_t(g.body_lines + 64));
    std::string d = compare(run_direct(t, g, header), g.lines);
    CHECK(d.find("first difference at line") != std::string::npos);
}

TEST(golden_start_skew_is_refs) {
    // Tiles 0-7, 9 and 12 as the goldens record them (Chip: Random(1).nextInt(2)).
    for (const RunSpec& r : read_runs()) {
        Golden g; std::string err;
        CHECK(read_golden("tests/golden/" + r.name + ".trace", g, err));
        for (auto& kv : g.config)
            if (kv.first == "start_skew") CHECK_EQ(std::stoi(kv.second), start_skew(r.tile));
    }
}
