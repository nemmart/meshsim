// suite_ag.cpp (WORKER22, P4, inventory C6): v1.3's AgTest (sim/ref/meshsim/test/AgTest.java): the address generator
// without stream working registers, its computed fields, the read-ahead queue bounds, mixed read sizes in a group,
// figure 12's PTR and HIST ports and the v1.0 rulings, on W20's harness (tests/mach.h; ref's runLocal on
// TileRef::phase_done). AgTest prints no lines: its log is the summary (agtest -1, agtest-shuf 4; tests/p4suites/).
//
//   suite_ag [seed] [--only=GROUP,...]      groups: worked computed bounds groupsize fig12 rulings
//
// STATUS. figure 12 (PTR and HIST, L = 1 and 3) waits on W22-16 (the write engine's RMW puts from the harness); the
// stream-entry flags in `computed` and "dst 70" in `rulings` on W26's kinds.
//
// Test-side code (the configuration regime, D6).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "mach.h"
#include "p4_debug.h"
#include "suite.h"
#include "units/ag.h"
#include "units/linkword.h"
#include "units/write_engine.h"

using namespace mach;
namespace lw = units::lw;
using suite::check;
using suite::eq;

namespace {

int64_t shuffle = -1;
std::set<std::string> only;
bool want(const char* g) { return only.empty() || only.count(g); }

MachConfig cfg() { return Net::cfg(shuffle); }
units::AddrGen& ag(TileRef p) { return p.unit<units::AddrGen>("ag"); }
void no_violations(Chip& c, const std::string& what) {   // EngineTest.noViolations
    c.scan_overflows();
    if (c.violations() == 0) check(true, "%s", what.c_str());
    else check(false, "%s: no violations %s", what.c_str(), c.violation_report().c_str());
}
int64_t run_local(Net& n, unsigned tile) {               // EngineTest.runLocal
    const int64_t t0 = n.c.cycle();
    n.c.tile(tile).pulse(fields::PULSE_START);
    const bool ok = n.c.run_until([&] { return n.c.tile(tile).phase_done(); }, 2000000);
    check(ok, "local run finished");
    return n.c.cycle() - t0;
}
// ref's Chip.newPhase: W20's Chip::new_phase (W22-14) isn't on main yet; nothing records the disjoint-buffer check yet
// (W26), so this changes nothing meanwhile.
void new_phase(Chip&) {}
bool any_message(Chip& c, const std::string& a, const std::string& b = "") {
    for (const auto& v : c.violation_messages()) if (v.find(a) != std::string::npos && (b.empty() || v.find(b) != std::string::npos)) return true;
    return false;
}
// ref's TableStore indexes (TileRef::ts_peek takes ref's)
enum : uint32_t { STREAM_SRC = 0x1000, STREAM_DEST = 0x1100, STREAM_COUNT = 0x1200 };
std::string stream_words(TileRef p, unsigned i) {
    return std::to_string(int32_t(p.ts_peek(STREAM_SRC + i))) + "/" + std::to_string(int32_t(p.ts_peek(STREAM_DEST + i))) + "/" +
           std::to_string(int32_t(p.ts_peek(STREAM_COUNT + i)));
}
const char* size_name(unsigned s) { static const char* const n[] = {"32", "64", "128", "11"}; return n[s & 3]; }

// ================================================================ v1.0 rulings
void rulings() {
    suite::group("v1.0 rulings");
    Chip c(p4::dbg(cfg()));
    TileRef p = c.tile(3);
    p.set_master_config(fields::master_config(fields::RUN_STREAMS, 0, 0, 0, 0, 1)); p.pulse(fields::PULSE_START); c.run(3);
    check(ag(p).done && !ag(p).running && ag(p).requests == 0, "a stream run of count 0 ends at once, with no requests");
    p.set_master_config(fields::master_config(fields::RUN_GROUPS, 0, 0, 0, 0, 1)); p.pulse(fields::PULSE_START); c.run(3);
    check(ag(p).done && !ag(p).running && ag(p).requests == 0, "a group run of count 0 ends at once, with no requests");
    eq(0, int64_t(c.violations()), "count 0 is not flagged");
    const uint32_t runs[2][3] = {{fields::RUN_STREAMS, 255, 2}, {fields::RUN_GROUPS, 63, 2}};
    for (const auto& run : runs) {
        Chip cw(p4::dbg(cfg())); TileRef pw = cw.tile(3);
        pw.set_master_config(fields::master_config(run[0], run[1], run[2], 0, 0, 1)); pw.pulse(fields::PULSE_START);
        suite::ub([&] { cw.run(20); }, {"tile 3", "address generator", "past the table's end"}, "a %s run %u + %u: past the table's end",
                  run[0] == fields::RUN_STREAMS ? "stream" : "group", run[1], run[2]);
    }
    const uint32_t dgs[3][2] = {{5, 0}, {0, 7}, {6, 6}};
    for (const auto& dg : dgs)
        suite::ub([&] { p.set_master_config(fields::master_config(fields::RUN_STREAMS, 0, 1, dg[0], dg[1], 1)); }, {"tile 3", "reserved digit selector"},
                  "digit selectors %u, %u", dg[0], dg[1]);
    p.set_master_config(fields::master_config(fields::RUN_STREAMS, 0, 1, 4, 1, 1));
    {   // the write engine's digits as it takes them (ref's writer.bucketDigit / histDigit): DMA_CONFIG as the block holds it
        const uint32_t dc = p.block().dma_config;
        check(units::mc_bucket_digit(dc) == 4 && units::mc_hist_digit(dc) == 1, "digit selectors 1-4 are taken");
    }
    {
        Chip cs(p4::dbg(cfg())); TileRef ps = cs.tile(3);
        ps.set_stream(10, fields::stream_src(0x100), 0, fields::stream_count(200, fields::SIZE_64, 1, lw::M_SRC, 0));
        ps.set_ptr(3, 0x60000); ps.set_remaining(1, 200);
        ps.set_master_config(fields::master_config(fields::RUN_STREAMS, 10, 1, 0, 0, 1)); ps.pulse(fields::PULSE_START); cs.run(2);
        ps.pulse(fields::PULSE_START);
        suite::ub([&] { cs.run(2); }, {"tile 3", "address generator", "a start while a run is running"}, "a start in the lead-in");
    }
    p.set_stream(10, fields::stream_src(0x100), 0, fields::stream_count(200, fields::SIZE_64, 1, lw::M_SRC, 0));
    p.set_ptr(3, 0x60000); p.set_remaining(1, 200); new_phase(c);
    p.set_master_config(fields::master_config(fields::RUN_STREAMS, 10, 1, 0, 0, 1)); p.pulse(fields::PULSE_START); c.run(2);
    c.run_until([&] { return ag(p).done && p.remaining(1) == 0 && p.mem_idle(); }, 5000);
    p.set_stream(11, fields::stream_src(0x100), 0, fields::stream_count(50, fields::SIZE_64, 1, lw::M_SRC, 0));
    p.set_ptr(3, 0x60000); p.set_remaining(1, 50); new_phase(c);
    p.set_master_config(fields::master_config(fields::RUN_STREAMS, 11, 1, 0, 0, 1));
    const int64_t ts = c.cycle() + 1;
    p.store(ts, 0xFFFFF800u + 4 * 4, 1);                    // PULSE_DMA (Registers.PULSE_DMA)
    c.step();
    {   // ref's ag.runningForStatus() / doneForStatus(): TILE_STATUS as computed at the end of this cycle, read at + 1
        const std::optional<uint32_t> st = p.load(c.cycle() + 1, 0xFFFFF800u + 4 * 1, 4);   // STATUS (Registers.STATUS)
        const uint32_t s = st ? *st : 0;
        check(!ag(p).running && (s >> 0 & 1), "at the end of the start store's cycle the run has not begun, but TILE_STATUS's rd_running (computed then, read at + 1) is 1");
        check(!(s >> 1 & 1), "... and rd_done is 0");
    }
    c.run_until([&] { return ag(p).done && p.remaining(1) == 0 && p.mem_idle(); }, 5000);
    const uint64_t v0 = c.violations();
    p.send_put(c.cycle(), 1, lw::hdr(0, 70, lw::M_COUNT, 1, 0, lw::SIZE_32), 0); c.run(20);
    check(c.violations() > v0 && any_message(c, "dst 70"), "a packet sent to dst 70 is flagged");
}

// ================================================================ the proposal's worked example (section 4)
void worked_example() {
    suite::group("worked example");
    Net n(p4::dbg(cfg())); TileRef p = n.c.tile(12);
    const uint32_t count[] = {2, 0, 3}, src[] = {0x100, 0x200, 0x400};
    for (unsigned s = 0; s < 3; s++) {
        p.set_stream(40 + s, fields::stream_src(src[s]), 0, fields::stream_count(count[s], fields::SIZE_64, 1, lw::M_SRC, 0));
        for (unsigned e = 0; e < 4; e++) p.sram_poke64(src[s] + 2 * e, uint64_t(s + 0xA) << 8 | e);
    }
    p.set_group(7, fields::group_lo(3), fields::group_hi(40, 3, 0));
    std::string before[3]; for (unsigned s = 0; s < 3; s++) before[s] = stream_words(p, 40 + s);
    for (int rep = 0; rep < 2; rep++) {
        n.clear_logs();
        p.set_ptr(12, 0x40000); p.set_remaining(1, 5);
        p.set_master_config(fields::master_config(fields::RUN_GROUPS, 7, 1, 0, 0, 1));
        run_local(n, 12);
        const auto l = n.log(12);
        eq(5, int64_t(l.size()), "five packets (run %d)", rep);
        const uint64_t want_v[] = {0xA00, 0xC00, 0xA01, 0xC01, 0xC02};
        for (int j = 0; j < 5; j++) eq(int64_t(want_v[j]), int64_t(p.sram_peek64(0x40000 + 2 * j)), "element %d in rotation order (run %d)", j, rep);
        std::vector<int64_t> rel; for (const auto& e : l) rel.push_back(int64_t(e[0]) - int64_t(l.at(0)[0]));
        suite::eqo("[0, 2, 3, 5, 8]", suite::jlist(rel.data(), rel.size()), "issue cycles: period 3, idle visits keep their time (run %d)", rep);
        eq(9, int64_t(ag(p).stream_reads), "nine STREAM reads (three per rotation)");
        eq(1, int64_t(ag(p).group_reads), "one GROUP read");
        eq(5, int64_t(ag(p).requests), "five element reads");
    }
    for (unsigned s = 0; s < 3; s++) suite::eqo(before[s], stream_words(p, 40 + s), "STREAM[%u] as the controller wrote it (its three words)", 40 + s);
    no_violations(n.c, "worked example");
}

// ================================================================ element addresses and mode-5 words, computed
void computed_fields() {
    suite::group("computed fields");
    const uint32_t sizes[] = {fields::SIZE_32, fields::SIZE_64, fields::SIZE_128};
    for (uint32_t region : {uint32_t(lw::R_DMEM), uint32_t(lw::R_SRAM)})
        for (uint32_t kind : {fields::RUN_STREAMS, fields::RUN_GROUPS})
            for (uint32_t size : sizes) {
                Net n(p4::dbg(cfg())); TileRef p = n.c.tile(3);
                const uint32_t chunks = 1u << size, N = 5, base = 0x800;
                const uint32_t dest0 = region == lw::R_DMEM ? 0x400 : lw::sram_addr(0x40000);
                for (uint32_t i = 0; i < N * chunks; i++) p.sram_poke(base + i, 0x5000 + i);
                p.set_stream(9, fields::stream_src(base), dest0, fields::stream_count(N, size, 2, lw::M_WRITE, 0));
                p.set_group(2, fields::group_lo(N), fields::group_hi(9, 1, 0));
                p.set_remaining(2, N);
                p.set_master_config(fields::master_config(kind, kind == fields::RUN_STREAMS ? 9 : 2, 1, 0, 0, 1));
                run_local(n, 3);
                const auto l = n.log(3);
                const std::string what = suite::str("mode 0 to region 0x0%u, %s, %u-bit", region, kind == fields::RUN_STREAMS ? "stream run" : "group run", 32u << size);
                eq(N, int64_t(l.size()), "%s: one packet per element", what.c_str());
                bool ok = true, one_cycle = true;
                for (size_t k = 0; k < l.size(); k++) {
                    const auto& e = l[k];
                    if (lw::addr(e[2]) != dest0 + uint32_t(k) * 4 * chunks) ok = false;
                    if (lw::size(e[2]) != size || lw::counter(e[2]) != 2) ok = false;
                    uint64_t lo = 0, hi = 0;
                    for (uint32_t c = 0; c < chunks; c++) { const uint64_t v = (0x5000 + k * chunks + c) & 0xFFFFFFFFull; if (c < 2) lo |= v << (32 * c); else hi |= v << (32 * (c - 2)); }
                    if (e[1] != lo || e[3] != hi) ok = false;
                    if (k > 0 && int64_t(e[0]) - int64_t(l[k - 1][0]) != 1) one_cycle = false;
                }
                check(ok, "%s: element e at source + (e << size), address = STREAM_DEST + e * %u, the stream's size and counter", what.c_str(), 4 * chunks);
                check(one_cycle, "%s: one element per cycle", what.c_str());
                bool landed = true;
                for (uint32_t i = 0; i < N * chunks; i++) if ((region == lw::R_DMEM ? p.dmem(0x100 + i) : p.sram_peek(0x40000 + i)) != 0x5000 + i) landed = false;
                check(landed, "%s: %u words landed", what.c_str(), N * chunks);
                eq(0, p.remaining(2), "%s: each counted once", what.c_str());
                no_violations(n.c, what);
            }
    // the stream-entry checks (4.2; meshsim only, the elements still issue as written)
    const char* bad[][2] = {{"read size 11", "11"}, {"mode 6 (v1.25: UB, below)", "reserved mode"}, {"d = 70", "d = 70"}, {"d = 126 on a mesh tile", "d = 126"},
                            {"mode 4 with d = 5", "mode-4 stream"}, {"mode 3 at 128", "mode-3 stream"}, {"a misaligned destination", "destination address"},
                            {"a misaligned source", "source chunk"}, {"count-only on counter 0", "counter 0"}};
    const uint32_t e[][7] = {{0x800, 0, 1, fields::SIZE_UNDEF, 1, lw::M_SRC, 1}, {0x800, 0, 1, fields::SIZE_32, 1, 6, 1}, {0x800, 0, 1, fields::SIZE_32, 1, lw::M_SRC, 70},
                             {0x800, 0, 1, fields::SIZE_32, 1, lw::M_COUNT, 126}, {0x800, 0, 1, fields::SIZE_64, 1, lw::M_GATHER, 5}, {0x800, 0, 1, fields::SIZE_128, 1, lw::M_AUG, 1},
                             {0x800, 0x404, 1, fields::SIZE_64, 2, lw::M_WRITE, 0}, {0x801, 0, 1, fields::SIZE_64, 1, lw::M_SRC, 1}, {0x800, 0, 1, fields::SIZE_32, 0, lw::M_COUNT, 1}};
    for (int b = 0; b < 9; b++) {
        MachConfig kc = cfg(); kc.max_violation_messages = 100;
        Net n(p4::dbg(kc)); TileRef p = n.c.tile(3);
        const uint32_t* x = e[b];
        p.set_stream(0, fields::stream_src(x[0]), x[1], fields::stream_count(x[2], x[3], x[4], x[5], x[6]));
        p.set_master_config(fields::master_config(fields::RUN_STREAMS, 0, 1, 0, 0, 1));
        p.pulse(fields::PULSE_START);
        // A UB stop runs in suite::ub's forked child; ref's facts recorded before its stop stay in the machine. So the
        // parent runs to the cycle before the stop's (named in its message), for the flags this group checks after it.
        auto run_to_stop = [&](const std::string& msg) {
            const size_t at = msg.find("cycle ");
            if (at == std::string::npos) return;
            const int64_t stop = std::atoll(msg.c_str() + at + 6);
            if (stop > n.c.cycle() + 1) n.c.run(uint64_t(stop - n.c.cycle() - 1));
        };
        if (b == 1) suite::ub([&] { n.c.run(100); }, {"address generator", "reserved mode 6"}, "a stream in the reserved mode 6 stops meshsim (v1.25, s8e)");
        else if (b == 6) run_to_stop(suite::ub([&] { n.c.run(100); }, {"traffic director", "0x404"}, "a stream with a misaligned destination: its packets stop meshsim"));
        else if (b == 7) run_to_stop(suite::ub([&] { n.c.run(100); }, {"address generator", "chunk 0x801"}, "a stream with a misaligned source: its reads stop meshsim"));
        else n.c.run(100);
        if (b != 1) check(any_message(n.c, bad[b][1], "stream"), "a stream with %s is flagged: %s", bad[b][0], n.c.violation_report().c_str());
    }
}

// ================================================================ the queues hold at most ag_lookahead entries
void queue_bounds() {
    suite::group("queue bounds");
    for (unsigned L = 1; L <= 4; L++)
        for (int la : {1, 2, int(2 * L + 2), 9}) {
            MachConfig k = cfg(); k.with_ts_latency(L); k.ag_lookahead = la;
            Net n(p4::dbg(k)); TileRef p = n.c.tile(0);
            uint32_t stream = 0, g = 0, total = 0;
            for (uint32_t t = 1; t <= 16; t += 3)
                for (uint32_t R = 0; R <= 3; R++) {
                    for (uint32_t s = 0; s < t; s++) p.set_stream(stream + s, fields::stream_src(0x100 * g + 2 * s), 0, fields::stream_count(R, fields::SIZE_64, 1, lw::M_SRC, 0));
                    p.set_group(g, fields::group_lo(R), fields::group_hi(stream, t, 0));
                    stream += t; g++; total += R * t;
                }
            p.set_ptr(0, 0x40000); p.set_remaining(1, total);
            p.set_master_config(fields::master_config(fields::RUN_GROUPS, 0, g, 0, 0, 1));
            run_local(n, 0);
            const units::AddrGen& a = ag(p);
            check(int(a.stream_q_peak) <= la && int(a.group_q_peak) <= la, "L %u, ag_lookahead %d: peaks %u, %u", L, la, a.stream_q_peak, a.group_q_peak);
            eq(total, int64_t(a.requests), "L %u, ag_lookahead %d: every element sent", L, la);
            no_violations(n.c, suite::str("queue bounds L %u la %d", L, la));
        }
}

// ================================================================ mixed read sizes in a group (proposed item 27, accepted)
void group_size() {
    suite::group("mixed read sizes in a group");
    Net n(p4::dbg(cfg())); TileRef p = n.c.tile(5);
    const uint32_t size[] = {fields::SIZE_128, fields::SIZE_64, fields::SIZE_32, fields::SIZE_32}, src[] = {0x200, 0x300, 0x400, 0x500};
    for (unsigned s = 0; s < 4; s++) {
        p.set_stream(60 + s, fields::stream_src(src[s]), 0, fields::stream_count(2, size[s], 1, lw::M_SRC, 0));
        for (unsigned c = 0; c < 8; c++) p.sram_poke(src[s] + c, 0x1000 * (s + 1) + c);
    }
    p.set_group(1, fields::group_lo(2), fields::group_hi(60, 4, 0));
    p.set_ptr(5, 0x40000); p.set_remaining(1, 8);
    p.set_master_config(fields::master_config(fields::RUN_GROUPS, 1, 1, 0, 0, 1));
    run_local(n, 5);
    const auto l = n.log(5);
    eq(8, int64_t(l.size()), "eight packets: one per element whatever its size");
    std::vector<int64_t> rel; for (const auto& e : l) rel.push_back(int64_t(e[0]) - int64_t(l.at(0)[0]));
    suite::eqo("[0, 1, 2, 3, 4, 5, 6, 7]", suite::jlist(rel.data(), rel.size()), "period t = 4: every visit one cycle, 128-bit included");
    std::string sz; for (const auto& e : l) { sz += size_name(lw::size(e[2])); sz += ' '; }
    suite::eqo("128 64 32 32 128 64 32 32 ", sz, "each packet stamped with its stream's read size");
    const uint32_t want_v[] = {0x1000, 0x1001, 0x1002, 0x1003, 0x2000, 0x2001, 0x3000, 0x4000, 0x1004, 0x1005, 0x1006, 0x1007, 0x2002, 0x2003, 0x3001, 0x4001};
    bool ok = true; for (unsigned c = 0; c < 16; c++) if (p.sram_peek(0x40000 + c) != want_v[c]) ok = false;
    check(ok, "landed contiguously at PTR[me], each at its size (16 chunks)");
    eq(0x40000 + 16, p.ptr(5), "PTR[me] advanced by 4 + 2 + 1 + 1, twice");
    no_violations(n.c, "mixed sizes in a group, kept aligned: no report (item 27)");
    Net m(p4::dbg(cfg())); TileRef q = m.c.tile(5);
    const uint32_t size2[] = {fields::SIZE_64, fields::SIZE_32, fields::SIZE_128};
    for (unsigned s = 0; s < 3; s++) q.set_stream(60 + s, fields::stream_src(src[s]), 0, fields::stream_count(2, size2[s], 1, lw::M_SRC, 0));
    q.set_group(1, fields::group_lo(2), fields::group_hi(60, 3, 0));
    q.set_ptr(5, 0x40000); q.set_remaining(1, 6);
    q.set_master_config(fields::master_config(fields::RUN_GROUPS, 1, 1, 0, 0, 1));
    suite::ub([&] { run_local(m, 5); }, {"write engine", "not aligned to its 128-bit write"}, "a mix that misaligns its PTR stops meshsim");
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a;
    for (int i = 1; i < argc; i++) {
        if (p4::debug_flag(argv[i])) continue;
        if (std::strncmp(argv[i], "--only=", 7) == 0) {
            std::string s = argv[i] + 7; size_t p = 0;
            while (p <= s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); only.insert(s.substr(p, q - p)); p = q + 1; }
        } else a.push_back(argv[i]);
    }
    if (!a.empty()) shuffle = std::atoll(a[0].c_str());
    if (want("worked")) worked_example();
    if (want("computed")) computed_fields();
    if (want("bounds")) queue_bounds();
    if (want("groupsize")) group_size();
    // fig12: figure 12's PTR and HIST ports, ts_engine_latency 1 and 3: W22-16 (the write engine's RMW puts)
    if (want("rulings")) rulings();
    return suite::done(std::string("AgTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : ""));
}
