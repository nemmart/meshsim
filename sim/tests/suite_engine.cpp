// suite_engine.cpp (WORKER22, P4, inventory C5): v1.3's EngineTest (sim/ref/meshsim/test/EngineTest.java): the address
// generator, the write engine, the table and HIST stores, augmenting and count-on-arrival, the 16-group all-to-all, on
// W20's harness (tests/mach.h's Net, the coreless machine; A2A). Every assertion and printed line is Java's, in Java's
// order, so the output equals run_tests.sh's logs engine (-1), eng-shuf (4), engine-full (-1 full); fixtures
// tests/p4suites/.
//
//   suite_engine [seed] [quick|full] [--only=GROUP,...]
//
// --only (not Java's) runs the named groups alone: writeengine localcopy streamruns exchange radix lategroups alltoall
// drift inv6 fence.
//
// The statistics it reads that the timing builds don't keep (the Fifo peaks, big SRAM's queue peaks) exist under
// SIM_CHECKS: run the suite and debug builds.
//
// STATUS. Every group (writeengine, alltoall, drift, inv6, localcopy, streamruns, exchange, radix, lategroups, fence):
// the log equals v1.3's byte for byte (engine, eng-shuf, engine-full).
//
//
// Test-side code (the configuration regime, D6).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "mach.h"
#include "p4_debug.h"
#include "p4_java.h"
#include "suite.h"
#include "units/ag.h"
#include "units/big_sram.h"
#include "units/director.h"
#include "units/inject.h"
#include "units/linkword.h"
#include "units/router.h"
#include "units/table_store.h"
#include "units/write_engine.h"

#ifdef SIM_CHECKS
using namespace mach;
namespace lw = units::lw;
using suite::check;
using suite::eq;

namespace {

int64_t shuffle = -1;
bool full = false;
std::string report;
std::set<std::string> only;
void out(const std::string& s) { report += s; report += '\n'; }
bool want(const char* g) { return only.empty() || only.count(g); }

MachConfig cfg(bool skew) { MachConfig c = MachConfig::defaults(); c.shuffle_seed = shuffle; c.skew = skew; c.check_errors = false; return c; }
void no_violations(Chip& c, const std::string& what) {
    c.scan_overflows();
    if (c.violations() == 0) check(true, "%s", what.c_str());
    else check(false, "%s: no violations %s", what.c_str(), c.violation_report().c_str());
}
units::AddrGen& ag(TileRef p) { return p.unit<units::AddrGen>("ag"); }
units::WriteEngine& writer(TileRef p) { return p.unit<units::WriteEngine>("we"); }
units::Router& router(TileRef p) { return p.unit<units::Router>("router"); }
uint32_t we_in_peak(Chip& c, unsigned k) { return c.sim().fifo_peak(writer(c.tile(k)).in); }
std::string join_messages(Chip& c) { std::string s; for (const auto& v : c.violation_messages()) { if (!s.empty()) s += '\n'; s += v; } return s; }

// ref's EngineTest.runLocal: start the tiles' runs, run until each is done (TileRef::phase_done, ref's phaseDone)
int64_t run_local(Net& n, std::initializer_list<unsigned> tiles) {
    Chip& c = n.c;
    const int64_t t0 = c.cycle();
    for (unsigned i : tiles) c.tile(i).pulse(fields::PULSE_START);
    const bool ok = c.run_until([&] { for (unsigned i : tiles) if (!c.tile(i).phase_done()) return false; return true; }, 2000000);
    check(ok, "local run finished");
    return c.cycle() - t0;
}
void stream_run(TileRef p, uint32_t start, uint32_t count, uint32_t bucket_digit, uint32_t hist_digit) {
    p.set_master_config(fields::master_config(fields::RUN_STREAMS, start, count, bucket_digit, hist_digit, 1));
}
uint32_t imem(TileRef p, uint32_t w) { return p.peek_local(0x01000000u + 4 * w); }

// ================================================================ the write engine
void write_engine() {
    suite::group("write engine");
    Net n(p4::dbg(cfg(false)));
    TileRef r = n.c.tile(10);
    r.set_ptr(9, 0x20000); r.set_master_config(fields::master_config(0, 0, 0, 0, 1, 1)); r.set_remaining(1, 20);
    for (int j = 0; j < 20; j++) n.at(9, 2 + j, 10, lw::M_SRC, 0, 0xABCD000000000000ull | uint64_t(j % 5));
    n.run_all(1000);
    bool ok = true; for (int j = 0; j < 20; j++) if (r.sram_peek64(0x20000 + 2 * j) != (0xABCD000000000000ull | uint64_t(j % 5))) ok = false;
    check(ok, "mode 2 lands at PTR[src], += 2");
    eq(0x20000 + 40, r.ptr(9), "PTR[9] advanced by 2 per packet");
    for (int k = 0; k < 5; k++) eq(4, r.hist_peek(k), "count-on-arrival HIST[%d]", k);
    for (int b = 0; b < 256; b++) r.set_ptr(b, 0x60000 + 64 * b);
    r.set_master_config(fields::master_config(0, 0, 0, 2, 0, 1)); r.set_remaining(1, 30);
    uint64_t keys[30]; p4::JRandom rnd(3);
    for (int j = 0; j < 30; j++) { keys[j] = uint64_t(uint32_t(rnd.next_int())); n.at(9, n.c.cycle() + 2 + j, 10, lw::M_BUCKET, 0, keys[j]); }
    n.run_all(1000);
    int seen[256] = {}; ok = true;
    for (uint64_t k : keys) { const int b = int((k >> 8) & 0xFF); if (r.sram_peek64(0x60000 + 64 * b + 2 * seen[b]++) != k) ok = false; }
    check(ok, "mode 3 lands at PTR[digit 2], in arrival order per bucket");
    r.set_ptr(7, 0x70000); r.set_master_config(fields::master_config(0, 0, 0, 1, 0, 1)); r.set_remaining(1, 50);
    for (int j = 0; j < 50; j++) n.at(9, n.c.cycle() + 2 + j, 10, lw::M_BUCKET, 0, uint64_t(j) << 32 | 7);
    n.run_all(1000);
    ok = true; for (int j = 0; j < 50; j++) if (r.sram_peek64(0x70000 + 2 * j) != (uint64_t(j) << 32 | 7)) ok = false;
    check(ok, "50 packets to one bucket, one a cycle: consecutive (pointer bypass)");
    r.set_ptr(9, 0x30010); r.set_record_base(0x30000); r.set_master_config(fields::master_config(0, 0, 0, 0, 0, 1)); r.set_remaining(1, 10);
    for (int j = 0; j < 10; j++) n.at(9, n.c.cycle() + 2 + j, 10, lw::M_AUG, 0, 0xFFFFFFFF00000000ull | uint64_t(1000 + j));
    n.run_all(1000);
    ok = true; for (int j = 0; j < 10; j++) if (r.sram_peek64(0x30010 + 2 * j) != (uint64_t(8 + j) << 32 | uint64_t(1000 + j))) ok = false;
    check(ok, "mode 4: ordinals 8, 9, ... from AUGMENT_BASE_ADDRESS, key kept");
    r.set_ptr(9, 0x34000); r.set_ptr(11, 0x34100); r.set_record_base(0x34000); r.set_master_config(fields::master_config(0, 0, 0, 0, 1, 1));
    r.set_remaining(1, 40); const int64_t hist0 = r.hist_peek(0x5A);
    bool aug[2][20];
    for (int s = 0; s < 2; s++) for (int j = 0; j < 20; j++) {
        aug[s][j] = ((j * 7 + s * 3) % 5) < 2;
        n.at(s == 0 ? 9 : 11, n.c.cycle() + 2 + j, 10, aug[s][j] ? lw::M_AUG : lw::M_SRC, 0, 0xEEEE000000000000ull | uint64_t(s) << 40 | uint64_t(j) << 8 | 0x5A);
    }
    n.run_all(1000);
    ok = true; int n_aug = 0;
    for (int s = 0; s < 2; s++) for (int j = 0; j < 20; j++) {
        const uint32_t at = uint32_t((s == 0 ? 0x34000 : 0x34100) + 2 * j);
        const uint64_t sent = 0xEEEE000000000000ull | uint64_t(s) << 40 | uint64_t(j) << 8 | 0x5A;
        const uint64_t want_v = aug[s][j] ? (uint64_t((at - 0x34000) >> 1) << 32 | (sent & 0xFFFFFFFFull)) : sent;
        if (aug[s][j]) n_aug++;
        if (r.sram_peek64(at) != want_v) ok = false;
    }
    check(ok, "modes 2 and 4 mixed in one phase: arrival order at PTR[src] kept, ordinals only on the %d mode-4 payloads", n_aug);
    eq(0x34000 + 40, r.ptr(9), "PTR[9] advanced by both modes"); eq(0x34100 + 40, r.ptr(11), "PTR[11] advanced by both modes");
    eq(hist0 + 40, int64_t(r.hist_peek(0x5A)), "count-on-arrival counts both modes");
    eq(0, r.remaining(1), "SRAM_REMAINING counts both modes");
    no_violations(n.c, "write engine");
    r.set_ptr(9, 0x50007); r.set_master_config(0); r.set_remaining(1, 2); const uint64_t v0 = n.c.violations();
    for (int j = 0; j < 2; j++) n.at(9, n.c.cycle() + 2 + j, 10, lw::M_SRC, 1, 0, lw::SIZE_32, uint64_t(0x7700 + j), 0);
    n.run_all(1000);
    eq(0x7700, r.sram_peek(0x50007), "a 32-bit write at an odd chunk lands"); eq(0x7701, r.sram_peek(0x50008), "and the next");
    eq(0, r.remaining(1), "both counted");
    eq(0, int64_t(n.c.violations() - v0), "no violation");
    eq(0x50009, r.ptr(9), "PTR[9] odd");
    r.set_remaining(1, 1);
    n.at(9, n.c.cycle() + 2, 10, lw::M_SRC, 0, 0x1111111100000000ull);
    suite::ub([&] { n.run_all(1000); }, {"write engine", "PTR[src 9]", "chunk 0x50009", "64-bit"}, "a 64-bit write through an odd PTR stops meshsim");
    std::string sb = "write engine pointer stage, 200 packets at one a cycle [meas: EngineTest write engine]:";
    for (unsigned L = 1; L <= 4; L++) {
        MachConfig k = cfg(false); k.with_ts_latency(L);
        Net m(p4::dbg(k)); TileRef q = m.c.tile(10); q.set_ptr(9, 0x20000); q.set_remaining(1, 200);
        for (int j = 0; j < 200; j++) m.at(9, 2 + j, 10, lw::M_SRC, 0, uint64_t(j));
        m.run_all(5000);
        bool good = true; for (int j = 0; j < 200; j++) if (q.sram_peek64(0x20000 + 2 * j) != uint64_t(j)) good = false;
        check(good, "L = %u: every write lands", L);
        const unsigned stage_peak = writer(q).stage_peak;   // ref's writer.stageHighWater (SIM_CHECKS)
        check(stage_peak <= 2 * L + 2, "L = %u: stage within 2L + 2", L);
        eq(0, we_in_peak(m.c, 10) > 1 ? 1 : 0, "L = %u: the input FIFO never holds more than one (one a cycle sustained)", L);
        no_violations(m.c, suite::str("L = %u", L));
        sb += suite::str(" L=%u: peak %u of %u;", L, stage_peak, 2 * L + 2);
    }
    out(sb + " (v3.7 measured 3, 5, 7, 9)");
}

// ================================================================ local copies
void local_copy() {
    suite::group("local copy");
    std::string sb = "local copies, one stream run on one tile, area 0 -> area 1 [meas: EngineTest local copy], cycles from the arm+start store to the local-wake condition:\n";
    const uint32_t N = 4096;
    const uint32_t cases[3][2] = {{fields::SIZE_64, N}, {fields::SIZE_128, N / 2}, {fields::SIZE_32, N}};
    const char* name[] = {"64-bit records", "128-bit values (one packet each)", "32-bit keys (one per packet)"};
    for (int k = 0; k < 3; k++) {
        Net n(p4::dbg(cfg(false))); TileRef p = n.c.tile(20);
        for (uint32_t i = 0; i < 2 * N; i++) p.sram_poke(i, 0x50000000u + i);
        const uint32_t size = cases[k][0], count = cases[k][1], packets = count;
        p.set_stream(0, fields::stream_src(0), 0, fields::stream_count(count, size, 1, lw::M_SRC, 0));
        p.set_ptr(20, 0x20000); p.set_remaining(1, packets); stream_run(p, 0, 1, 0, 0);
        const int64_t cyc = run_local(n, {20});
        bool ok = true;
        const uint32_t chunks = count * (1u << size);
        for (uint32_t j = 0; j < chunks; j++) if (p.sram_peek(0x20000 + j) != 0x50000000u + j) { ok = false; break; }
        check(ok, "%s: copied, %u chunks", name[k], chunks);
        eq(0x20000 + chunks, p.ptr(20), "%s: PTR[me] advanced by the chunks written", name[k]);
        no_violations(n.c, name[k]);
        sb += suite::str("  %-34s %5u packets: %5lld cycles (%lld over the packet count)\n", name[k], packets, (long long)cyc, (long long)(cyc - packets));
    }
    for (uint32_t size : {fields::SIZE_128, fields::SIZE_64, fields::SIZE_32}) {
        Net n(p4::dbg(cfg(false))); TileRef p = n.c.tile(21);
        for (uint32_t i = 0; i < 1024; i++) p.sram_poke(0x10000 + i, 0x70000000u + i);
        const uint32_t pk = 1024u >> size;
        p.set_stream(5, fields::stream_src(0x10000), Net::cm(0x000), fields::stream_count(pk, size, 2, lw::M_WRITE, 0));
        p.set_remaining(2, pk); stream_run(p, 5, 1, 0, 0);
        const int64_t cyc = run_local(n, {21});
        bool ok = true; for (uint32_t i = 0; i < 1024; i++) if (imem(p, i) != 0x70000000u + i) ok = false;
        check(ok, "mode-5 block copy, %u-bit packets: 1K words in instruction memory", 32u << size);
        no_violations(n.c, "block copy");
        sb += suite::str("  %-34s %5u packets: %5lld cycles%s\n", suite::str("1K-word block copy (mode 5, %u)", 32u << size).c_str(), pk, (long long)cyc,
                         size == fields::SIZE_128 ? " (spec 4.2 estimate: about 256 plus the stream's start)" : size == fields::SIZE_64 ? " (v1.0: 526)" : "");
    }
    out(sb);
}

// ================================================================ the fence (3.1, D43)
void fence() {
    suite::group("fence");
    Net n(p4::dbg(cfg(false))); TileRef p = n.c.tile(12), q = n.c.tile(50);
    const int N = 300;
    for (int i = 0; i < N; i++) p.sram_poke64(0x8000 + 2 * i, 0xF00000ull + uint64_t(i));
    p.set_stream(0, fields::stream_src(0x8000), 0, fields::stream_count(N, fields::SIZE_64, 1, lw::M_SRC, 38));
    p.set_stream(1, fields::stream_src(0x8000), 0, fields::stream_count(1, fields::SIZE_32, 7, lw::M_COUNT, 0));
    q.set_ptr(12, 0x40000); q.set_remaining(1, N); p.set_remaining(7, 1);
    stream_run(p, 0, 2, 0, 0);
    const units::Injection& in = p.unit<units::Injection>("inj");
    const uint64_t inj0 = in.from_read; const uint32_t ptr0 = p.ptr(12);
    p.pulse(fields::PULSE_START);
    int64_t fence_at = -1, inj_at_fence = -1;
    for (int i = 0; i < 5000 && fence_at < 0; i++) { n.step(); if (p.remaining(7) == 0) { fence_at = n.c.cycle(); inj_at_fence = int64_t(in.from_read - inj0); } }
    check(fence_at > 0, "the fence's counter reached 0");
    eq(N + 1, inj_at_fence, "when it did, every packet of the run had been injected, the fence included");
    n.c.run_until([&] { return q.phase_done() && p.phase_done(); }, 5000);
    eq(0, q.remaining(1), "the run's packets all landed at tile 50");
    eq(ptr0, p.ptr(12), "the count-only packet wrote nothing at the tile itself (no PTR moved)");
    eq(1, int64_t(p.unit<units::Director>("dir").count_only), "one count-only packet accepted");
    eq(0, int64_t(in.tok.reads + in.tok.loop_held), "its read token was returned");
    no_violations(n.c, "fence");
    out(suite::str("fence: a %d-packet stream run ending with a count-only packet to the tile itself on counter 7: when counter 7 reached 0, all %d packets had been injected [meas: EngineTest fence]", N, N + 1));
}

// ================================================================ stream runs: timing, count 0, gap-after, relative destinations, wrap
void stream_runs() {
    suite::group("stream runs");
    Net n(p4::dbg(cfg(false))); TileRef p = n.c.tile(30);
    for (int i = 0; i < 64; i++) p.sram_poke64(2 * i, uint64_t(i));
    const uint32_t counts[] = {3, 0, 2, 0, 1}, gaps[] = {2, 3, 0, 1, 0}; uint32_t src = 0;
    for (unsigned s = 0; s < 5; s++) { p.set_stream(250 + s, fields::stream_src(2 * src, gaps[s]), 0, fields::stream_count(counts[s], fields::SIZE_64, 1, lw::M_SRC, 0)); src += counts[s]; }
    p.set_ptr(30, 0x20000); p.set_remaining(1, 6); stream_run(p, 250, 5, 0, 0);
    run_local(n, {30});
    const auto l = n.log(30);
    eq(6, int64_t(l.size()), "six packets");
    std::vector<int64_t> rel; for (const auto& e : l) rel.push_back(int64_t(e[0]) - int64_t(l.at(0)[0]));
    suite::eqo("[0, 1, 2, 8, 9, 11]", suite::jlist(rel.data(), rel.size()), "issue cycles: 3 elements + gap 2, count 0 with gap 3, 2 elements, count 0 with gap 1, 1 element (streams 250-254)");
    for (int j = 0; j < 6; j++) eq(j, int64_t(p.sram_peek64(0x20000 + 2 * j)), "data %d", j);
    p.set_stream(10, fields::stream_src(0), 0, fields::stream_count(2, fields::SIZE_64, 1, lw::M_SRC, 5));
    p.set_stream(11, fields::stream_src(4), 0, fields::stream_count(2, fields::SIZE_64, 1, lw::M_SRC, 40));
    n.c.tile(35).set_ptr(30, 0x30000); n.c.tile(6).set_ptr(30, 0x30000);
    n.c.tile(35).set_remaining(1, 2); n.c.tile(6).set_remaining(1, 2);
    stream_run(p, 10, 2, 0, 0);
    run_local(n, {30});
    n.c.run_until([&] { return n.c.tile(35).phase_done() && n.c.tile(6).phase_done(); }, 1000);
    eq(1, int64_t(n.c.tile(35).sram_peek64(0x30002)), "d = 5 lands at tile 35"); eq(3, int64_t(n.c.tile(6).sram_peek64(0x30002)), "d = 40 lands at tile 6 (30 + 40 mod 64)");
    eq(0, int64_t(n.c.violations()), "no violation: %s", n.c.violation_report().c_str());
    out("stream runs: a count-0 stream takes exactly its gap-after cycles and a stream its elements plus its gap (issue offsets 0, 1, 2, 8, 9, 11 for counts 3, 0, 2, 0, 1 and gaps 2, 3, 0, 1, 0, streams 254 to 2: the numbers wrap, flagged); relative destinations wrap mod 64 [meas: EngineTest stream runs]");
}

// ================================================================ one-word exchanges (63 streams of count 1, 16-group order)
void one_word_exchange() {
    suite::group("one-word exchange");
    std::string sb = "one-word exchange, every tile sends one record to each of the other 63 in the 16-group offset order, stream runs, 0/+1 start skew [meas: EngineTest one-word exchange]:\n";
    int order[63]; { int i = 0; for (const auto& g : A2A::G4) for (int k : g) if (k != 0) order[i++] = k; }
    for (uint32_t gap : {4u, 2u, 1u, 0u}) {
        Net n(p4::dbg(cfg(true)));
        for (unsigned k = 0; k < 64; k++) {
            TileRef p = n.c.tile(k);
            for (unsigned s = 0; s < 63; s++) {
                const unsigned d = (k + unsigned(order[s])) & 63;
                p.sram_poke64(2 * s, uint64_t(k) << 32 | d);
                p.set_stream(s, fields::stream_src(2 * s, gap), 0, fields::stream_count(1, fields::SIZE_64, 1, lw::M_SRC, uint32_t(order[s])));
            }
            for (unsigned s = 0; s < 64; s++) p.set_ptr(s, 0x20000 + 2 * s);
            p.set_remaining(1, 63); stream_run(p, 0, 63, 0, 0);
        }
        const int64_t t0 = n.c.cycle();
        const std::vector<int> sk = n.c.start_skew();
        bool st[64] = {};
        for (int i = 0; i < 2000; i++) {
            for (unsigned k = 0; k < 64; k++) if (!st[k] && n.c.cycle() >= t0 + sk[k]) { n.c.tile(k).pulse(fields::PULSE_START); st[k] = true; }
            n.step();
            bool all = true; for (unsigned k = 0; k < 64; k++) if (!n.c.tile(k).phase_done() || !st[k]) { all = false; break; }
            if (all) break;
        }
        const int64_t cyc = n.c.cycle() - t0;
        bool ok = true;
        for (unsigned k = 0; k < 64; k++) for (unsigned s = 0; s < 64; s++) if (s != k && n.c.tile(k).sram_peek64(0x20000 + 2 * s) != (uint64_t(s) << 32 | k)) ok = false;
        check(ok, "gap %u: every word landed", gap);
        n.c.scan_overflows();
        uint64_t ag_reads = 0; uint32_t ptr_q = 0;
        unsigned link = 0, ej = 0;
        for (unsigned k = 0; k < 64; k++) {
            const TileRef tr = n.c.tile(k);
            const units::TableStore& ts = tr.unit<units::TableStore>("ts");
            ag_reads = std::max(ag_reads, ts.table_reads); ptr_q = std::max(ptr_q, ts.q_we.peak);
            const units::Router& r = router(n.c.tile(k));
            for (unsigned o = 0; o < 4; o++) for (unsigned f = 0; f < units::Router::kNFeeders[o]; f++) link = std::max(link, unsigned(r.q[r.fifo_index(o, f)].peak));
            for (unsigned f = 0; f < units::Router::kNFeeders[units::Router::EJECT]; f++) ej = std::max(ej, unsigned(r.q[r.fifo_index(units::Router::EJECT, f)].peak));
        }
        no_violations(n.c, suite::str("gap %u (separate table macros: no we-in overflow)", gap));
        std::string ovf = "no overflow";
        if (n.c.violations() != 0) { std::string m = n.c.violation_messages().at(0); const size_t sp = m.find(' ', 6); if (m.rfind("cycle ", 0) == 0 && sp != std::string::npos) m = m.substr(sp + 1); ovf = "OVERFLOW " + m; }
        sb += suite::str("  gap after %u: %4lld cycles; link FIFO peak %2u of 16, eject peak %2u of 16; STREAM reads %llu, PTR request FIFO peak %u of 16; %s\n",
                         gap, (long long)cyc, link, ej, (unsigned long long)ag_reads, ptr_q, ovf.c_str());
    }
    std::string t = sb; while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
    out(t);
}

// ================================================================ a 4-pass local radix sort
int64_t run_all_tiles(Net& n) {   // EngineTest.runAll: arm + start every tile's run (+ skew), run until every tile is done
    Chip& c = n.c; const int64_t t0 = c.cycle(); bool st[64] = {};
    const std::vector<int> sk = c.start_skew();
    for (int i = 0; i < 5000000; i++) {
        for (unsigned k = 0; k < 64; k++) if (!st[k] && c.cycle() >= t0 + sk[k]) { c.tile(k).pulse(fields::PULSE_START); st[k] = true; }
        c.step();
        bool all = true; for (unsigned k = 0; k < 64; k++) if (!st[k] || !c.tile(k).phase_done()) { all = false; break; }
        if (all) return c.cycle() - t0;
    }
    check(false, "run finished"); return -1;
}

void radix_sort() {
    suite::group("radix sort");
    const uint32_t N = full ? 16384 : 4096;
    MachConfig k0 = cfg(true); k0.log_capacity = 5 * N + 64;   // ref's director log is a list: every packet of the 5 passes
    Net n(p4::dbg(k0));
    const uint32_t A = 0x00000, B = 0x20000, C = 0x40000;
    std::vector<std::vector<uint64_t>> keys(64, std::vector<uint64_t>(N));
    p4::JRandom rnd(11);
    for (unsigned k = 0; k < 64; k++) for (uint32_t j = 0; j < N; j++) { keys[k][j] = uint32_t(rnd.next_int()); n.c.tile(k).sram_poke(A + j, uint32_t(keys[k][j])); }
    int64_t pass[5] = {};
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = n.c.tile(k);
        p.set_stream(0, fields::stream_src(A), 0, fields::stream_count(N, fields::SIZE_32, 1, lw::M_AUG, 0));
        p.set_ptr(k, B); p.set_record_base(B); p.set_remaining(1, N); stream_run(p, 0, 1, 0, 1);
    }
    pass[0] = run_all_tiles(n);
    uint32_t src = B, dst = C;
    for (uint32_t d = 1; d <= 4; d++) {
        for (unsigned k = 0; k < 64; k++) {
            TileRef p = n.c.tile(k);
            uint32_t pre = 0;
            for (unsigned b = 0; b < 256; b++) { const uint32_t c = p.hist_peek(b); p.set_ptr(b, dst + 2 * pre); pre += c; p.hist_poke(b, 0); }
            eq(N, pre, "tile %u pass %u: counts sum to N", k, d);
            p.set_stream(0, fields::stream_src(src), 0, fields::stream_count(N, fields::SIZE_64, 1, lw::M_BUCKET, 0));
            p.set_remaining(1, N); stream_run(p, 0, 1, d, d < 4 ? d + 1 : 0);
        }
        n.c.new_phase();      // ref's newPhase
        pass[d] = run_all_tiles(n);
        std::swap(src, dst);
    }
    int bad = 0;
    for (unsigned k = 0; k < 64; k++) {
        int64_t prev_key = -1, prev_ord = -1;
        for (uint32_t j = 0; j < N; j++) {
            const uint64_t r = n.c.tile(k).sram_peek64(src + 2 * j); const int64_t key = int64_t(r & 0xFFFFFFFFull), ord = int64_t(r >> 32);
            if (key < prev_key || (key == prev_key && ord < prev_ord) || ord >= int64_t(N) || int64_t(keys[k][size_t(ord)]) != key) bad++;
            prev_key = key; prev_ord = ord;
        }
    }
    eq(0, bad, "64 tiles sorted, stable, each (key, ordinal) matching the input");
    no_violations(n.c, "radix sort");
    const int64_t mn = std::min(std::min(pass[1], pass[2]), std::min(pass[3], pass[4])), mx = std::max(std::max(pass[1], pass[2]), std::max(pass[3], pass[4]));
    out(suite::str("local radix sort, 64 tiles x %u keys, 0/+1 skew [meas: EngineTest radix sort]: augmenting pass %lld cycles, bucket passes %lld, %lld, %lld, %lld (N + %lld to N + %lld each); total %lld",
                   N, (long long)pass[0], (long long)pass[1], (long long)pass[2], (long long)pass[3], (long long)pass[4], (long long)(mn - N), (long long)(mx - N),
                   (long long)(pass[0] + pass[1] + pass[2] + pass[3] + pass[4])));
}

// ================================================================ group runs: R = 0, late groups, start before rd_ready
void late_groups() {
    suite::group("late groups");
    Net n(p4::dbg(cfg(false)));
    const std::vector<std::vector<int>> grp = {{1, 2, 3, 5}, {6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}, {23}, {24, 25, 26, 27}};
    const uint32_t R[] = {1, 2, 0, 3}, gap[] = {4, 4, 6, 4};
    uint32_t expect[64] = {};
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = n.c.tile(k);
        uint32_t stream = 0, src = 0;
        for (unsigned g = 0; g < 4; g++) {
            p.set_group(g, fields::group_lo(R[g]), fields::group_hi(stream, uint32_t(grp[g].size()), gap[g]));
            for (int kk : grp[g]) {
                p.set_stream(stream++, fields::stream_src(src), 0, fields::stream_count(R[g], fields::SIZE_64, 1, lw::M_SRC, uint32_t(kk)));
                for (uint32_t j = 0; j < R[g]; j++) p.sram_poke64(src + 2 * j, uint64_t(k) << 32 | uint64_t(kk) << 8 | j);
                src += 2 * R[g]; expect[(k + unsigned(kk)) & 63] += R[g];
            }
        }
        for (unsigned s = 0; s < 64; s++) p.set_ptr(s, 0x40000 + 0x100 * s);
        p.set_master_config(fields::master_config(fields::RUN_GROUPS, 0, 4, 0, 0, 1));
    }
    for (unsigned k = 0; k < 64; k++) n.c.tile(k).set_remaining(1, expect[k]);
    const int64_t cyc = A2A::run(n.c, 10000);
    check(cyc > 0, "finished");
    const units::AddrGen& t5 = ag(n.c.tile(5));
    const int64_t late1 = t5.group_late_by[1];
    eq(0, late1, "group 1 starts on time (M8 read-ahead; M7's bank prefetch made it 13 cycles late)");
    bool same = true;
    for (unsigned k = 0; k < 64; k++) for (unsigned g = 0; g < 4; g++) { const units::AddrGen& a = ag(n.c.tile(k)); if (a.group_late_by[g] != t5.group_late_by[g] || a.group_start[g] != t5.group_start[g]) same = false; }
    check(same, "every tile late by the same amount, every group starting on the same cycle");
    eq(0, int64_t(t5.rotations_late), "no rotation late within a group");
    const int64_t s1 = t5.group_start[1], s2 = t5.group_start[2], s3 = t5.group_start[3];
    eq(s1 + 2 * 16 + 4, s2, "group 2 begins R*t + gap after group 1's actual start");
    eq(s2 + 1 + 6, s3, "group 2 (R = 0) takes 1 cycle plus its gap (spec 4.2, 10 October; M14, was only its gap)");
    no_violations(n.c, "late groups");
    out(suite::str("group run with a 16-stream group after a 4-cycle group: group 1 starts %lld cycles late on every tile (M7: 13, the bank prefetch of GROUP + 16 STREAM reads); R = 0 takes 1 cycle plus its gap; no drift, no invariant-3 violation; ag_lookahead %u, STREAM queue peak %u, GROUP queue peak %u, underrun cycles %llu [meas: EngineTest late groups]",
                   (long long)late1, unsigned(t5.lookahead), t5.stream_q_peak, t5.group_q_peak, (unsigned long long)t5.underrun_cycles));
    for (unsigned k = 0; k < 64; k++) n.c.tile(k).set_remaining(1, expect[k]);
    for (unsigned k = 0; k < 64; k++) for (unsigned s = 0; s < 64; s++) n.c.tile(k).set_ptr(s, 0x60000 + 0x100 * s);
    n.c.new_phase();      // ref's newPhase
    for (unsigned k = 0; k < 64; k++) n.c.tile(k).pulse(fields::PULSE_START);
    n.c.run_until([&] { for (unsigned k = 0; k < 64; k++) if (!n.c.tile(k).phase_done()) return false; return true; }, 10000);
    const units::AddrGen& a0 = ag(n.c.tile(0));
    const int64_t sfr = a0.first_request_at < 0 || a0.started_at < 0 ? -1 : a0.first_request_at - a0.started_at;   // startToFirstRequest()
    eq(0, a0.group_late_by[0], "start: group 0 begins after the lead-in, not counted late");
    eq(int64_t(4 * n.c.cfg().ts_engine_latency + 2), sfr, "start: the first request 4 ts_engine_latency + 2 cycles after the start takes effect");
    out(suite::str("start: the first request %lld cycles after the start takes effect (its GROUP entry, then its first STREAM entry; ts_engine_latency %u) [meas: EngineTest late groups]", (long long)sfr, n.c.cfg().ts_engine_latency));
    no_violations(n.c, "the lead-in");
}

// ================================================================ the 16-group all-to-all
std::string peaks(Chip& c) {
    unsigned link = 0, ej = 0; uint32_t we = 0, aq = 0;
    for (unsigned k = 0; k < 64; k++) {
        const units::Router& r = router(c.tile(k));
        for (unsigned o = 0; o < 4; o++) for (unsigned f = 0; f < units::Router::kNFeeders[o]; f++) link = std::max(link, unsigned(r.q[r.fifo_index(o, f)].peak));
        for (unsigned f = 0; f < units::Router::kNFeeders[units::Router::EJECT]; f++) ej = std::max(ej, unsigned(r.q[r.fifo_index(units::Router::EJECT, f)].peak));
        we = std::max(we, we_in_peak(c, k));
    }
    for (unsigned k = 0; k < 64; k++) for (uint32_t q : c.tile(k).unit<units::BigSram>("sram").queue_peak) aq = std::max(aq, q);
    return suite::str("link %u, eject %u, we-in %u, area queue %u", link, ej, we, aq);
}

// Chip.agReport's figures
struct AgFig { uint64_t stalls = 0, rot_late = 0, max_rot = 0, grp_late = 0, max_grp = 0; int late_tiles = 0, max_drift = 0, drift_group = -1; };
AgFig ag_fig(Chip& c) {
    AgFig f;
    for (unsigned k = 0; k < 64; k++) {
        const units::AddrGen& a = ag(c.tile(k));
        f.rot_late += a.rotations_late; f.max_rot = std::max(f.max_rot, a.max_rot_late); f.grp_late += a.groups_late_prefetch;
        f.max_grp = std::max(f.max_grp, a.max_group_late); f.stalls += a.stall_cycles; if (a.rotations_late > 0) f.late_tiles++;
    }
    for (unsigned g = 0; g < units::AddrGen::MAXGROUPS; g++) {
        int64_t lo = INT64_MAX, hi = INT64_MIN;
        for (unsigned k = 0; k < 64; k++) { const int64_t v = ag(c.tile(k)).group_start[g]; if (v < 0) continue; lo = std::min(lo, v); hi = std::max(hi, v); }
        if (hi >= lo && hi - lo > f.max_drift) { f.max_drift = int(hi - lo); f.drift_group = int(g); }
    }
    return f;
}
std::string ag_report(Chip& c) {   // Chip.agReport().trim()
    const AgFig f = ag_fig(c);
    return suite::str("address generator: %llu request stall cycles; rotations late %llu on %d tiles (max %llu cycles); group starts waiting for read-ahead %llu (max %llu, timetable); drift between tiles at a group start %d%s",
                      (unsigned long long)f.stalls, (unsigned long long)f.rot_late, f.late_tiles, (unsigned long long)f.max_rot, (unsigned long long)f.grp_late,
                      (unsigned long long)f.max_grp, f.max_drift, f.drift_group >= 0 ? suite::str(" (group %d)", f.drift_group).c_str() : "");
}

void all_to_all() {
    suite::group("all-to-all");
    std::string sb = "16-group all-to-all as a group run [meas: EngineTest all-to-all]: cycles from the start store to the last tile's local-wake condition; bound = sum 4 R_g + 16 gap; 0/+1 skew\n";
    sb += suite::str("  %-26s %-4s %10s %10s %6s  %-44s %s\n", "data set", "gap", "measured", "bound", "over", "queue peaks (of 16; we-in of 4; area of 8)", "late / drift");
    struct Set { std::string name; A2A d; std::vector<int> gaps; };
    std::vector<Set> sets;
    if (full) {
        sets.push_back({"uniform (Data(7, 65536))", A2A(7, 65536), {4, 0, 8}});
        for (const char* m : {"zipfraw", "trace", "one99"}) sets.push_back({m, A2A(std::string("data/") + m + ".txt"), {4, 0, 8}});
    } else sets.push_back({"uniform (Data(7, 2000))", A2A(7, 2000), {4, 0}});
    for (const Set& st : sets) {
        for (int gap : st.gaps) {
            // ref's `new Net(cfg(true))`, of which only the chip is used: its directors' logs are lists in ref, fixed
            // here, and the full-size data sets (65,536 records a tile and more) don't need them, so the plain chip
            struct { Chip c; } n{Chip(p4::dbg(cfg(true)))};
            st.d.load(n.c, gap);
            const int64_t cyc = A2A::run(n.c, 20000000);
            const int bad = st.d.verify(n.c);
            eq(0, bad, "%s gap %d: every record where it belongs", st.name.c_str(), gap);
            check(cyc > 0, "%s gap %d: finished", st.name.c_str(), gap);
            if (gap > 0) no_violations(n.c, suite::str("%s gap %d", st.name.c_str(), gap));
            const AgFig f = ag_fig(n.c);
            const std::string lr = suite::str("rot %llu, prefetch-late groups %llu (max %llu), drift %d", (unsigned long long)f.rot_late,
                                              (unsigned long long)f.grp_late, (unsigned long long)f.max_grp, f.max_drift);
            const auto& vm = n.c.violation_messages();
            sb += suite::str("  %-26s %-4d %10lld %10lld %6lld  %-44s %s%s\n", st.name.c_str(), gap, (long long)cyc, (long long)st.d.bound(gap),
                             (long long)(cyc - st.d.bound(gap)), peaks(n.c).c_str(), lr.c_str(), n.c.violations() > 0 && !vm.empty() ? ("; " + vm[0]).c_str() : "");
        }
    }
    sb += "  reference, measured on simulator v3.10 (gap 8, AllToAllTest measure, schedule-4 RESULTS 4.1): sixteen groups uniform 71,345, zipfraw 294,580, trace 706,470, one99 434,712; nine groups 73,904, 304,647, 800,218, 575,905";
    out(sb);
}

// ================================================================ drift: an over-subscribed source area in a group run (invariant 3)
void drift() {
    suite::group("drift");
    A2A d(7, 500);
    Net n(p4::dbg(cfg(false)));
    d.load(n.c, 4);
    TileRef p = n.c.tile(7);
    for (unsigned s = 0; s < 64; s++) p.set_ptr(s, uint32_t(0x10000 + 2 * d.dstoff[s][7]));
    const int64_t cyc = A2A::run(n.c, 1000000);
    check(cyc > 0, "finished");
    check(ag(p).rotations_late > 0, "tile 7's rotations run late");
    check(join_messages(n.c).find("invariant 3") != std::string::npos, "reported as invariant 3");
    out("drift: a tile whose incoming writes share its send buffer's area waits at the area (writes first) and runs late: " + ag_report(n.c) + " [meas: EngineTest drift]");
}

// ================================================================ invariant 6
void counter_overflow() {
    suite::group("invariant 6");
    Net n(p4::dbg(cfg(false))); TileRef r = n.c.tile(3);
    r.hist_poke(5, 0x1FFFF); r.set_master_config(fields::master_config(0, 0, 0, 0, 1, 1)); r.set_ptr(2, 0x20000); r.set_remaining(1, 1);
    n.at(2, 2, 3, lw::M_SRC, 0, 5);
    n.run_all(500);
    check(r.unit<units::HistStore>("hist").hist_overflow, "counter past 17 bits flagged");
    check(join_messages(n.c).find("invariant 6") != std::string::npos, "reported as invariant 6");
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
    full = a.size() > 1 && a[1] == "full";
    if (want("writeengine")) write_engine();
    if (want("localcopy")) local_copy();
    if (want("streamruns")) stream_runs();
    if (want("exchange")) one_word_exchange();
    if (want("radix")) radix_sort();
    if (want("lategroups")) late_groups();
    if (want("alltoall")) all_to_all();
    if (want("drift")) drift();
    if (want("inv6")) counter_overflow();
    if (want("fence")) fence();
    std::fputs(report.c_str(), stdout);
    return suite::done(std::string("EngineTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : "") + (full ? " full" : ""));
}
#else   // the timing builds keep none of the statistics EngineTest reads
int main() {
    std::fprintf(stderr, "suite_engine: needs SIM_CHECKS (the suite build, MODE=suite, or a debug build)\n");
    return 2;
}
#endif
