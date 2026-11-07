// test_statefp_g1.cpp: G1's writers and the configuration parts of G3/G4's units against ref's regs.fp (tile 0, every
// cycle): at reset, and with regs' own configuration and table-window stores applied (regs.c lines 13-25, 50).
#include <string>

#include "harness.h"
#include "statefp_g1.h"

namespace {

const fp::FpGolden& regs() { static fp::FpGolden g = fp::FpGolden::load("tests/golden-m2/regs.fp"); return g; }
uint64_t golden(uint64_t cycle, const std::string& unit) {
    for (const fp::FpPoint* p : regs().at(cycle))
        for (const fp::FpLine& l : p->lines) if (l.unit == unit) return l.hash;
    return 0;
}
uint64_t ours(const fp::FpRegistry& r, const std::string& unit, uint64_t c) { return r.hash(unit, fp::FpCtx{c, nullptr}); }

TEST(reset_streams_equal_refs_at_cycle_0) {
    CHECK(regs().error.empty());
    fp::G1Tile g;                                   // every pointer null: the reset state
    fp::FpRegistry r;
    fp::register_g1(r, g);
    for (const char* u : {"sram", "sram.data", "we", "ag", "ts", "hist", "gather", "rdport", "req"}) {
        std::string n = std::string("t0.") + u;
        if (ours(r, n, 0) != golden(0, n)) test::fail(__FILE__, __LINE__, "%s differs at cycle 0", n.c_str());
    }
}

// regs (tile 0): DMA_CONFIG 0x00BCDEF stored, endCycle at 506 (the AG and the write engine's digits from 507), the record
// base 0x54321 at 509 (augment base and gather base from 510); HIST[5] = 0x1FFFF at 591 and 0 at 614; PTR[1] = 0x1FFFFF,
// STREAM 3 and GROUP 63 at 593-607; PTR[0] = 0x100 at 803 (then the mode-1 sends: G3's, from 825).
TEST(regs_configuration_and_table_writes_equal_refs) {
    units::RegBlockState ctl;
    units::TableMems ts;
    units::HistMem hist;
    fp::G1Tile g{0, &ctl, &ts, &hist};
    fp::FpRegistry r;
    fp::register_g1(r, g);
    auto check = [&](uint64_t c, std::initializer_list<const char*> us) {
        for (const char* u : us) {
            std::string n = std::string("t0.") + u;
            if (ours(r, n, c) != golden(c, n)) test::fail(__FILE__, __LINE__, "%s differs at cycle %llu", n.c_str(), (unsigned long long)c);
        }
    };
    check(506, {"we", "ag", "gather", "ts", "hist"});
    ctl.dma_config = 0x00BCDEF;
    check(507, {"we", "ag", "gather"});
    check(509, {"we", "ag", "gather"});
    ctl.record_base = 0x54321;
    check(510, {"we", "ag", "gather"});
    hist.write(5, 0x1FFFF);
    check(591, {"hist"});
    check(613, {"hist"});
    ts.write(units::TBL_PTR, 1, 0x1FFFFF);
    ts.write(units::TBL_STREAM_SRC, 3, 0xDEADBEEF);
    ts.write(units::TBL_STREAM_DEST, 3, 0x12345678);
    ts.write(units::TBL_STREAM_COUNT, 3, 0x0BADBEEF);
    ts.write(units::TBL_GROUP, 126, 0x0BADF00D);
    ts.write(units::TBL_GROUP, 127, 0xFEEDFACE);
    check(607, {"ts"});
    check(802, {"ts"});
    hist.write(5, 0);
    check(614, {"hist"});
    check(869, {"hist"});
    ts.write(units::TBL_PTR, 0, 0x100);
    check(803, {"ts"});
    check(824, {"we", "ag", "gather", "ts", "hist", "sram", "sram.data", "rdport", "req"});
}

TEST(table_window_decode) {
    using units::table_addr;
    CHECK(table_addr(0xFFFF2000 + 4 * 5).table == units::TBL_HIST && table_addr(0xFFFF2000 + 4 * 5).index == 5);
    CHECK(table_addr(0xFFFF2404).table == units::TBL_PTR && table_addr(0xFFFF2404).index == 1);
    CHECK(table_addr(0xFFFF280C).table == units::TBL_STREAM_SRC && table_addr(0xFFFF280C).index == 3);
    CHECK(table_addr(0xFFFF2C0C).table == units::TBL_STREAM_DEST);
    CHECK(table_addr(0xFFFF300C).table == units::TBL_STREAM_COUNT);
    CHECK(table_addr(0xFFFF3400 + 8 * 63 + 4).table == units::TBL_GROUP && table_addr(0xFFFF3400 + 8 * 63 + 4).index == 127);
}

}  // namespace
