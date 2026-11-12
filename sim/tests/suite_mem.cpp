// suite_mem.cpp (WORKER22, P4, inventory C4): v1.3's MemTest (sim/ref/meshsim/test/MemTest.java): the big SRAM, the
// SRAM read and write queues, mem_idle and the disjoint-buffer check, on W20's harness (tests/mach.h's Net, the coreless
// machine with the harness's read clients) and, for the unit level, the big SRAM alone at its holds (ref's `U`, here
// `U` too: a sim of BigSram and a driver). Every assertion and printed line is Java's, in Java's order, so the output
// equals run_tests.sh's logs mem (-1), mem-shuf (3), mem-shuf2 (8); fixtures tests/p4suites/.
//
//   suite_mem [seed] [--only=GROUP,...]
//
// --only (not Java's) runs the named groups alone: decode partial stall combining wrap latency throughput priority
// packets returnorder tokenwindow disjoint memidle oversubscription.
//
// The statistics MemTest reads that the timing builds don't keep (big SRAM per area, the read queue per client, the
// Fifo peaks) exist under SIM_CHECKS: run the suite and debug builds.
//
// STATUS. Every group but `disjoint` (Chip::new_phase and big SRAM's disjoint-buffer facts: W22-14, W20 and W26).
//
// Test-side code (the configuration regime, D6); the unit level's driver keeps its returns in a fixed array (nothing
// allocates in the cycle loop).
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "config.h"
#include "mach.h"
#include "p4_debug.h"
#include "suite.h"
#include "units/big_sram.h"
#include "units/inject.h"
#include "units/linkword.h"
#include "units/router.h"
#include "units/write_engine.h"

#ifdef SIM_CHECKS
using namespace mach;
namespace lw = units::lw;
using units::BigSram;
using suite::check;
using suite::eq;

namespace {

int64_t shuffle = -1;
std::string report;
std::set<std::string> only;
void out(const std::string& s) { report += s; report += '\n'; }
bool want(const char* g) { return only.empty() || only.count(g); }
// Java's Double.toString for the few values printed here (1.0, 1.25, 1.5, 2.0)
std::string jdouble(double d) { std::string s = suite::str("%.17g", d); if (s.find('.') == std::string::npos) s += ".0"; return s; }
std::string strip_trailing(std::string s) { while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back(); return s; }

enum : unsigned { GATHER = 0, AG = 1, READPORT = 2 };     // SRAMReadQueue's clients
const char* const CLIENT[] = {"gather", "address generator", "read port"};

// Java's int arithmetic
int32_t pat(int tile, uint32_t chunk) { return int32_t(uint32_t(uint64_t(chunk) * 0x9E3779B1ull)) ^ int32_t(uint32_t(tile) * 0x01000193u); }
int64_t pat64(int tile, uint32_t chunk) { return int64_t((uint64_t(uint32_t(pat(tile, chunk)))) | uint64_t(uint32_t(pat(tile, (chunk + 1) & 0xFFFFF))) << 32); }
void fill(TileRef p, uint32_t from, uint32_t n) { for (uint32_t i = 0; i < n; i++) p.sram_poke((from + i) & 0xFFFFF, uint32_t(pat(int(p.idx()), (from + i) & 0xFFFFF))); }

// True if fn() dies by a FATAL in a forked child (Java's `try { ... } catch (IllegalArgumentException e) { threw = true; }`
// around a constructor; suite::throws would count it as an assertion, which MemTest doesn't).
template <class F> bool dies(F&& fn) {
    std::fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) { int fd = open("/dev/null", O_WRONLY); if (fd >= 0) dup2(fd, 2); fn(); _exit(0); }
    int st = 0; waitpid(pid, &st, 0);
    return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

// ======================================================== unit level: BigSRAM at its bus ends (ref's U)
struct UDrv {
    sim::HoldOut rd, wr;
    sim::LinkIn ret[BigSram::AREAS];
    static constexpr unsigned kRets = 256;
    std::array<int64_t, 5> rets[kRets] = {};     // {t, token | part << 16, lo, hi, area}
    unsigned nrets = 0;
    explicit UDrv(sim::Ports& p) {
        p.out(rd, "rd", 1); p.out(wr, "wr", 3);
        for (auto& r : ret) p.in(r, "ret", 3);
    }
    sim::Wake step(sim::Ctx& c) {
        for (unsigned a = 0; a < BigSram::AREAS; a++)
            if (c.ready(ret[a])) {
                const sim::Word* w = c.peek(ret[a]);
                if (nrets == kRets) { std::fprintf(stderr, "FATAL suite_mem: U's return list is full\n"); std::abort(); }
                rets[nrets++] = {int64_t(c.now()), int64_t(w[0]), int64_t(w[1]), int64_t(w[2]), int64_t(a)};
                c.take(ret[a]);
            }
        return sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};

struct U {
    std::unique_ptr<units::SramBlocks> blocks{new units::SramBlocks};
    std::optional<sim::Sim> m;
    BigSram* s = nullptr;
    UDrv* d = nullptr;
    int64_t t = 0;
    sim::Word last_rd = 0, last_wr[3] = {};
    uint64_t ref_rd = 0, ref_wr = 0;       // refusals seen so far
    bool rd_refused = false, wr_refused = false;

    explicit U(const MachConfig& k) {
        sim::Options opt;
        if (k.shuffle_seed >= 0) opt.shuffle_seed = uint64_t(k.shuffle_seed) + 1;
        opt.step_all = p4::g_step_all;                       // the debug options (p4_debug.h), as MachConfig's
        if (p4::g_instance_major) opt.order = sim::Options::Order::instance_major;
        sim::Config cfg(opt);
        units::SramConfig sc;
        sc.mem_chunks = k.mem_chunks; sc.area_queue_depth = k.area_queue_depth; sc.write_combining = k.write_combining;
        sc.fifo_count_before_pop = k.fifo_count_before_pop;
        auto& sr = cfg.add<BigSram>("sram", sc, blocks->block(0, k.mem_chunks), true);
        auto& dr = cfg.add<UDrv>("drv");
        cfg.connect(dr.rd, sr.rd); cfg.connect(dr.wr, sr.wr);
        for (unsigned a = 0; a < BigSram::AREAS; a++) cfg.link(sr.ret[a], dr.ret[a], k.sram_read_latency);
        m.emplace(cfg.build());
        s = &m->unit<BigSram>("sram"); d = &m->unit<UDrv>("drv");
    }
    void step() {
        m->run(1); t++;
        const uint64_t r = m->hold_refusals(s->rd), w = m->hold_refusals(s->wr);
        rd_refused = r != ref_rd; wr_refused = w != ref_wr; ref_rd = r; ref_wr = w;
    }
    void run(int n) { for (int i = 0; i < n; i++) step(); }
    void read(uint32_t addr, unsigned n, unsigned tok) {   // BigSRAM.readReq(addr, n, tok, 0), put at t
        last_rd = uint64_t(addr) | uint64_t(n) << 21 | uint64_t(tok) << 24;
        m->put_late("drv", d->rd, &last_rd, 1);
    }
    void write(uint32_t addr, unsigned n, uint64_t lo, uint64_t hi) {   // BigSRAM.writeReq(addr, n), put at t
        last_wr[0] = uint64_t(addr) | uint64_t(n) << 21; last_wr[1] = lo; last_wr[2] = hi;
        m->put_late("drv", d->wr, last_wr, 3);
    }
    void retry_rd() { m->put_late("drv", d->rd, &last_rd, 1); }
    void retry_wr() { m->put_late("drv", d->wr, last_wr, 3); }
    unsigned nrets() const { return d->nrets; }
    const std::array<int64_t, 5>& ret(unsigned i) const {
        if (i >= d->nrets) { std::fprintf(stderr, "FATAL suite_mem: no return %u (%u)\n", i, d->nrets); std::abort(); }
        return d->rets[i];
    }
};

MachConfig cfg() {
    MachConfig c = MachConfig::defaults();
    c.shuffle_seed = shuffle; c.check_unexpected = false; c.check_errors = false; c.skew = false; c.test_read_clients = true;
    return c;
}

void unit_decode() {
    suite::group("decode");
    eq(0, BigSram::area(0x1FFFF), "area of 0x1FFFF"); eq(1, BigSram::area(0x20000), "area of 0x20000"); eq(7, BigSram::area(0xFFFFF), "area of 0xFFFFF"); eq(15, BigSram::area(0x1FFFFF), "area of 0x1FFFFF (8 MB)");
    eq(0x7FFF, BigSram::row(0x1FFFC), "row of 0x1FFFC"); eq(0, BigSram::row(0x20003), "row of 0x20003");
    eq(0b0011, BigSram::chunk_mask(0x100, 2), "64-bit at offset 0"); eq(0b1100, BigSram::chunk_mask(0x102, 2), "64-bit at offset 2");
    eq(0b1000, BigSram::chunk_mask(0x103, 1), "32-bit at offset 3"); eq(0b1111, BigSram::chunk_mask(0x104, 4), "128-bit");
    U u(cfg());
    u.s->poke64(0x40010, 0x1111111100000000ull); u.s->poke64(0x40012, 0x3333333322222222ull);
    u.read(0x40010, 4, 7); u.run(5);
    eq(1, u.nrets(), "one return"); const auto r = u.ret(0);
    eq(2, r[4], "returned by area 2"); eq(7, r[1] & 0xFFFF, "token"); eq(0x1111111100000000ll, r[2], "row lo"); eq(0x3333333322222222ll, r[3], "row hi");
    eq(1, int64_t(u.s->reads_a[2]), "one access in area 2");
}

void unit_partial_and_tie() {
    suite::group("partial writes, tie");
    U u(cfg());
    u.s->poke64(0x500, 0xAAAAAAAAAAAAAAAAull); u.s->poke64(0x502, 0xBBBBBBBBBBBBBBBBull);
    u.write(0x502, 2, 0x2222222211111111ull, 0); u.step();
    u.write(0x500, 2, 0x4444444433333333ull, 0); u.step();
    u.run(3);
    eq(0x4444444433333333ll, int64_t(u.s->peek64(0x500)), "64-bit write at offset 0 leaves offset 2 alone");
    eq(0x2222222211111111ll, int64_t(u.s->peek64(0x502)), "64-bit write at offset 2");
    eq(2, int64_t(u.s->writes_a[0]), "each 64-bit write is one access (chunk enables)");
    u.read(0x500, 2, 1); u.write(0x500, 2, 0x5555555555555555ull, 0); u.run(6);
    eq(0x5555555555555555ll, u.ret(0)[2], "same-cycle read and write: the write is served first, the read sees it");
    u.s->poke(0x506, 0xBBBBBBBBu);
    u.write(0x507, 1, 0x77777777ull, 0); u.run(3);
    eq(0x77777777, int32_t(u.s->peek(0x507)), "32-bit write"); eq(int32_t(0xBBBBBBBBu), int32_t(u.s->peek(0x506)), "neighbouring chunk kept");
}

void unit_stall() {
    suite::group("queue depth and stalls");
    for (unsigned depth : {4u, 8u, 16u}) {
        MachConfig c = cfg(); c.area_queue_depth = depth;
        U u(c);
        for (int i = 0; i < 40; i++) {
            if (u.rd_refused) u.retry_rd(); else u.read(0x60000 + 4 * i, 4, unsigned(i));
            if (u.wr_refused) u.retry_wr(); else u.write(0x70000 + 2 * i, 2, uint64_t(i), 0);
            u.step();
        }
        eq(depth - 1, u.s->queue_peak[3], "depth %u: the queue holds depth - 1 after each cycle (13a: room by the count before the pop; the area serves every cycle)", depth);
        check(u.s->read_stall_cycles > 0, "depth %u: the SRAM read queue stalls", depth);
        eq(0, int64_t(u.s->write_stall_cycles), "depth %u: the SRAM write queue never stalls: writes take every freed slot (tie order)", depth);
        check(u.s->reads_a[3] <= depth, "depth %u: reads wait once the queue is full (%llu served)", depth, (unsigned long long)u.s->reads_a[3]);
        if (depth == 8) out(suite::str("area under two queues of one request a cycle each (unit test, depth 8): the queue fills to %u after a cycle (room is judged by the count before the pop, 13a); from then on every freed slot goes to the write (writes win the tie, ruled 2 October), so the SRAM read queue waits while the writes continue [meas: MemTest queue depth and stalls]", depth - 1));
    }
}

void unit_combining() {
    suite::group("write combining");
    for (bool on : {false, true}) {
        MachConfig c = cfg(); c.write_combining = on;
        U u(c);
        int w = 0;
        for (int i = 0; i < 6; i++) { u.read(0x21000 + 4 * i, 4, unsigned(i)); u.write(0x30000 + 2 * w, 2, uint64_t(0x100 + w), 0); w++; u.step(); }
        for (int i = 0; i < 20; i++) { if (u.wr_refused) u.retry_wr(); else { u.write(0x30000 + 2 * w, 2, uint64_t(0x100 + w), 0); w++; } u.step(); }
        u.run(20);
        bool ok = true; for (int i = 0; i < w; i++) if (u.s->peek64(0x30000 + 2 * i) != uint64_t(0x100 + i)) ok = false;
        check(ok, "combining %s: every write landed", on ? "true" : "false");
        if (!on) eq(w, int64_t(u.s->writes_a[1]), "off: one access per 64-bit write");
        else { check(u.s->combined_a[1] > 0, "on: writes combined (%llu)", (unsigned long long)u.s->combined_a[1]); eq(w, int64_t(u.s->writes_a[1] + u.s->combined_a[1]), "on: accesses + merges = writes"); }
        if (on) out(suite::str("write combining (param, off by default): after a congested stretch, %llu of %d 64-bit writes merged into a queued write to the same row [meas: MemTest write combining]", (unsigned long long)u.s->combined_a[1], w));
    }
}

void unit_wrap_small() {
    suite::group("wrap and small device");
    MachConfig big = cfg(); big.mem_chunks = 1u << 21;
    U u(big);
    u.s->poke(0x1FFFFE, 0xE); u.s->poke(0x1FFFFF, 0xF); u.s->poke(0, 0x10); u.s->poke(1, 0x11);
    eq(0x10, u.s->peek(0x200000), "address 2^21 is chunk 0 (addresses wrap mod 2^21)");
    u.read(0x1FFFFE, 2, 3); u.run(5);
    eq(0x0000000F0000000Ell, u.ret(0)[3], "the top row's chunks 2-3 from area 15");
    eq(1, int64_t(u.s->reads_a[15]), "8 MB: the top chunk is in area 15");
    U d(cfg());
    d.s->poke(0xFFFFF, 0x99); d.s->poke(0x100000, 0x88);
    eq(0x99, d.s->peek(0xFFFFF), "4 MB: the last real chunk"); eq(0, d.s->peek(0x100000), "4 MB: chunk 0x100000 is past the end: reads 0");
    MachConfig c = cfg(); c.mem_chunks = 0x80000;
    U v(c);
    v.s->poke(0x7FFFF, 0x77); v.s->poke(0x80000, 0x88); v.s->poke(0xFFFFF, 0x99);
    eq(0x77, v.s->peek(0x7FFFF), "last real chunk"); eq(0, v.s->peek(0x80000), "above the device: reads 0"); eq(0, v.s->peek(0xFFFFF), "0xFFFFF reads 0");
    v.write(0x80000, 2, 0x1234, 0); v.step(); v.read(0x80000, 2, 0); v.run(6);
    eq(0, v.ret(0)[2], "a write above the device is dropped; the read returns 0");
    eq(1, int64_t(v.s->writes_a[4]), "the access still took its turn in area 4's queue");
    MachConfig q = cfg(); q.mem_chunks = 0x50000;
    U w(q);
    w.s->poke(0x4FFFF, 0x55); w.s->poke(0x50000, 0x66);
    eq(0x55, w.s->peek(0x4FFFF), "1.25 MB: the last row of the half-height area 2"); eq(0, w.s->peek(0x50000), "1.25 MB: area 2's missing rows read 0");
    MachConfig z = cfg(); z.mem_chunks = 0x50001;
    const bool threw = dies([&] { U bad(z); });
    check(threw, "a size that isn't a multiple of 256 KB is refused");
}

void unit_latency() {
    suite::group("read latency");
    std::string sb = "read latency, request put into the SRAM's holding register at t -> row on the return line [meas: MemTest read latency]:";
    for (unsigned L = 1; L <= 4; L++) {
        MachConfig c = cfg(); c.sram_read_latency = L;
        U u(c); const int64_t t0 = u.t; u.read(0x100, 4, 0); u.run(10);
        const int64_t lat = u.ret(0)[0] - t0;
        eq(2 + L, lat, "unqueued: taken at t + 1, served at t + 2, visible at t + 2 + L");
        sb += suite::str(" L=%u: %lld", L, (long long)lat);
    }
    out(sb + " = 2 + L: taken at t + 1, served at t + 2, row visible at t + 2 + L; the SRAM read queue releases the packet in that cycle");
}

// ======================================================== chip level: the SRAM read queue and the SRAM write queue
units::InjReadSide& srq(TileRef p) { return p.unit<units::Injection>("inj").rs; }
units::Injection& inj(TileRef p) { return p.unit<units::Injection>("inj"); }
units::WriteEngine& writer(TileRef p) { return p.unit<units::WriteEngine>("we"); }
BigSram& sram(TileRef p) { return p.unit<BigSram>("sram"); }
units::Router& router(TileRef p) { return p.unit<units::Router>("router"); }
unsigned eject_peak(TileRef p) {
    const units::Router& r = router(p); unsigned m = 0;
    for (unsigned f = 0; f < units::Router::kNFeeders[units::Router::EJECT]; f++) m = std::max(m, unsigned(r.q[r.fifo_index(units::Router::EJECT, f)].peak));
    return m;
}
uint32_t we_in_peak(Chip& c, unsigned k) { return c.sim().fifo_peak(writer(c.tile(k)).in); }
uint64_t dmem64(TileRef p, uint32_t w) { return uint64_t(p.dmem(w)) | uint64_t(p.dmem(w + 1)) << 32; }
std::string vreport(Chip& c) { return c.violation_report(); }

void throughput() {
    suite::group("throughput");
    std::string sb = "throughput [meas: MemTest throughput], 2,000 requests, cycles from the first packet to the last at the receiver:\n";
    const int N = 2000;
    const char* name[] = {"64-bit reads, one area", "64-bit reads, round-robin over 8 areas", "128-bit reads, one area (1 packet each)", "32-bit reads, one area",
                          "64-bit reads (area 1) + 2,000 incoming writes (area 2), together", "reads every 2nd cycle (area 1) + writes every 2nd cycle (area 1, another buffer)"};
    for (int k = 0; k < 6; k++) {
        Net n(p4::dbg(cfg())); TileRef src = n.c.tile(10), dst = n.c.tile(11);
        fill(src, 0, 0x100000);
        dst.set_ptr(10, 0x40000); src.set_ptr(9, k == 5 ? 0x30000 : 0x50000);
        const unsigned w = k == 2 ? lw::SIZE_128 : k == 3 ? lw::SIZE_32 : lw::SIZE_64;
        const int step = k == 2 ? 4 : k == 3 ? 1 : 2, pace = k == 5 ? 2 : 1;
        auto addr_of = [&](int j) { return uint32_t(k == 1 ? (j % 8) * 0x20000 + (j / 8) * 2 : 0x20000 + j * step); };
        for (int j = 0; j < N; j++) n.rd(10, AG, 2 + int64_t(j) * pace, addr_of(j), w, lw::M_SRC, 1, 11, 0);
        if (k >= 4) for (int j = 0; j < N; j++) n.at(9, 2 + int64_t(j) * pace, 10, lw::M_SRC, 0, uint64_t(j));
        check(n.run_all(50000), "%s: drained", name[k]);
        const auto l = n.log(11);
        const int pk = N;
        eq(pk, int64_t(l.size()), "%s: packets", name[k]);
        bool ok = true;
        for (int j = 0; j < N && j < int(l.size()); j++) {
            const uint32_t addr = addr_of(j);
            const int64_t want = w == lw::SIZE_32 ? int64_t(uint32_t(pat(10, addr))) : pat64(10, addr);
            if (int64_t(l[j][1]) != want) ok = false;
            if (k == 2 && int64_t(l[j][3]) != pat64(10, addr + 2)) ok = false;
            if (lw::size(l[j][2]) != w) ok = false;
        }
        check(ok, "%s: payloads, in request order", name[k]);
        const int64_t span = int64_t(l.at(pk - 1)[0]) - int64_t(l.at(0)[0]) + 1;
        eq(0, int64_t(srq(src).stall_cycles), "%s: no read-queue stall", name[k]);
        if (k < 5) eq(pk, span, "%s: one packet per cycle", name[k]);
        if (k >= 4) { eq(N, int64_t(writer(src).taken), "%s: every incoming write taken", name[k]); eq(0, int64_t(writer(src).stall_cycles), "%s: no write-queue stall", name[k]); }
        n.c.scan_overflows(); eq(0, int64_t(n.c.violations()), "%s: no violations %s", name[k], vreport(n.c).c_str());
        sb += suite::str("  %-70s %5lld cycles for %d packets; read stalls %llu, write stalls %llu\n", name[k], (long long)span, pk,
                         (unsigned long long)srq(src).stall_cycles, (unsigned long long)writer(src).stall_cycles);
    }
    out(strip_trailing(sb));
}

void priority() {
    suite::group("priority");
    Net n(p4::dbg(cfg())); TileRef p = n.c.tile(20);
    fill(p, 0x100, 64);
    n.c.tile(21).record_writer();
    n.c.tile(20).set_remaining(2, 5);
    for (int j = 0; j < 5; j++) {
        n.rd(20, GATHER, 2, 0x100 + 2 * j, lw::SIZE_64, lw::M_SRC, 1, 21, 0);
        n.rd(20, AG, 2, 0x100 + 2 * (5 + j), lw::SIZE_64, lw::M_SRC, 1, 21, 0);
        n.rd(20, READPORT, 2, 0x100 + 2 * (10 + j), lw::SIZE_32, lw::M_WRITE, 2, 20, 0x400 + 4 * j);
    }
    n.run_all(500);
    const auto l = n.c.tile(21).writer_log();
    eq(10, int64_t(l.size()), "10 packets");
    std::string order;
    for (const auto& e : l) for (int id = 0; id < 10; id++) if (int64_t(e[1]) == pat64(20, 0x100 + 2 * id)) order += "GA"[id / 5];
    suite::eqo("GGGGGAAAAA", order, "issue (and return) order by priority");
    for (int j = 0; j < 5; j++) eq(pat(20, 0x100 + 2 * (10 + j)), int32_t(n.c.tile(20).dmem(0x100 + j)), "read-port result %d in data memory", j);
    eq(0, n.c.tile(20).remaining(2), "read-port results counted on their counter (2)");
    const auto& q = srq(p);
    check(q.first_issue_t[READPORT] > q.last_issue_t[AG] && q.first_issue_t[AG] > q.last_issue_t[GATHER],
          "issue order gather, AG, read port (first read-port issue %lld after the last AG issue %lld)", (long long)q.first_issue_t[READPORT], (long long)q.last_issue_t[AG]);
    Net m(p4::dbg(cfg())); fill(m.c.tile(22), 0x200, 512);
    for (int j = 0; j < 50; j++) m.rd(22, GATHER, 2 + j, 0x200 + 2 * j, lw::SIZE_64, lw::M_SRC, 1, 23, 0);
    m.rd(22, AG, 2, 0x300, lw::SIZE_64, lw::M_SRC, 1, 23, 0);
    m.run_all(500);
    const auto l2 = m.log(23);
    eq(pat64(22, 0x300), int64_t(l2.at(l2.size() - 1)[1]), "the AG's request goes last, after 50 gather values");
    out("priority: with requests from all three clients waiting, the SRAM read queue issues gather value, address generator, read port in that order; 50 back-to-back gather values hold the address generator's request back 50 cycles [meas: MemTest priority]");
}

void packets() {
    suite::group("packets");
    Net n(p4::dbg(cfg())); TileRef p = n.c.tile(30);
    fill(p, 0x1000, 64);
    n.rd(30, AG, 2, 0x1000, lw::SIZE_128, lw::M_SRC, 1, 31, 0);
    n.rd(30, AG, 2, 0x1007, lw::SIZE_32, lw::M_SRC, 1, 31, 0);
    n.rd(30, AG, 2, 0x1010, lw::SIZE_128, lw::M_WRITE, 2, 31, 0x400);
    n.rd(30, READPORT, 2, 0x1011, lw::SIZE_32, lw::M_WRITE, 2, 30, 0x3C0);
    n.c.tile(31).set_remaining(2, 1); p.set_remaining(2, 1);
    n.run_all(500);
    const auto l = n.log(31);
    eq(3, int64_t(l.size()), "three packets: one per read, whatever its size");
    eq(pat(30, 0x1011), int32_t(p.dmem(0xF0)), "read-port result: the chunk, at its data-memory word");
    eq(pat64(30, 0x1000), int64_t(l.at(0)[1]), "128-bit: payload [63:0]"); eq(pat64(30, 0x1002), int64_t(l.at(0)[3]), "and [127:64], in the same packet");
    eq(lw::SIZE_128, lw::size(l.at(0)[2]), "stamped 128");
    eq(int64_t(uint32_t(pat(30, 0x1007))), int64_t(l.at(1)[1]), "32 bits in [31:0], the rest 0"); eq(0, int64_t(l.at(1)[3]), "32 bits: [127:64] 0");
    eq(lw::SIZE_32, lw::size(l.at(1)[2]), "stamped 32");
    eq(0x400, int32_t(lw::addr(l.at(2)[2])), "mode 0 128-bit: one packet at its address");
    eq(pat64(30, 0x1010), int64_t(dmem64(n.c.tile(31), 0x100)), "mode 0 landed, words 0, 1"); eq(pat64(30, 0x1012), int64_t(dmem64(n.c.tile(31), 0x102)), "and words 2, 3");
    eq(1, int64_t(srq(p).issued[READPORT]), "one read-port read"); eq(0, int64_t(sram(p).reads_a[0]) - 4, "four SRAM accesses");
    const unsigned client[] = {AG, READPORT, GATHER};
    const uint32_t chunk[] = {0x2002, 0x2006, 0x2007};
    const unsigned size[] = {lw::SIZE_128, lw::SIZE_128, lw::SIZE_64};
    for (int c = 0; c < 3; c++) {
        Net m(p4::dbg(cfg())); fill(m.c.tile(40), 0x2000, 16);
        m.rd(40, client[c], 2, chunk[c], size[c], lw::M_SRC, 1, 41, 0);
        const std::string ch = suite::str("chunk 0x%x", chunk[c]);
        suite::ub([&] { m.run_all(500); }, {"tile 40", CLIENT[client[c]], ch.c_str()}, "%s: a misaligned read stops meshsim", CLIENT[client[c]]);
    }
}

void return_order() {
    suite::group("return order");
    auto addr = [](int j) { return uint32_t(j % 2 == 0 ? 0xA0000 + 2 * j : 0xC0000 + 2 * j); };
    {
        Net n(p4::dbg(cfg())); fill(n.c.tile(44), 0, 0x100000);
        for (int j = 0; j < 40; j++) n.rd(44, AG, 2 + j, addr(j), lw::SIZE_64, lw::M_SRC, 1, 45, 0);
        n.run_all(2000);
        const auto l = n.log(45);
        bool ok = l.size() == 40;
        for (int j = 0; ok && j < 40; j++) if (int64_t(l[j][1]) != pat64(44, addr(j))) ok = false;
        check(ok, "returns leave in request order across areas");
    }
    Net m(p4::dbg(cfg())); TileRef q = m.c.tile(44); fill(q, 0, 0x100000); q.set_ptr(43, 0xB0000);
    for (int j = 0; j < 64; j++) m.at(43, 3 + (j / 8) * 32LL + j % 8, 44, lw::M_SRC, 0, uint64_t(j));
    for (int j = 0; j < 200; j++) m.rd(44, AG, 2 + j, addr(j), lw::SIZE_64, lw::M_SRC, 1, 45, 0);
    m.run_all(5000);
    const auto l2 = m.log(45);
    bool ok2 = l2.size() == 200;
    for (int j = 0; ok2 && j < 200; j++) if (int64_t(l2[j][1]) != pat64(44, addr(j))) ok2 = false;
    check(ok2, "with area 5 congested: still in request order");
    const int64_t lm = int64_t(srq(q).lat_max[AG]);
    check(lm > int64_t(2 + m.c.cfg().sram_read_latency), "the bursts did delay reads (%lld)", (long long)lm);
    eq(0, int64_t(m.c.violations()), "no violations %s", vreport(m.c).c_str());
    out(suite::str("return order: 200 reads alternating a busy area (also taking bursts of 8 writes) and a free one leave in request order; read latency max %lld (unloaded %u), write stalls %llu [meas: MemTest return order]",
                   (long long)lm, 2 + m.c.cfg().sram_read_latency, (unsigned long long)writer(q).stall_cycles));
    Net g(p4::dbg(cfg())); TileRef u = g.c.tile(44); fill(u, 0, 0x100000); u.set_ptr(43, 0xB0000);
    for (int j = 0; j < 8; j++) g.at(43, 3 + j, 44, lw::M_SRC, 0, uint64_t(j));
    for (int j = 0; j < 6; j++) g.rd(44, AG, 8 + j, 0xA0000 + 2 * j, lw::SIZE_64, lw::M_SRC, 1, 45, 0);
    for (int j = 0; j < 3; j++) g.rd(44, GATHER, 9 + j, 0xC0000 + 2 * j, lw::SIZE_64, lw::M_SRC, 1, 45, 0);
    g.run_all(2000);
    const auto l3 = g.log(45);
    int first_g = -1, last_a = -1;
    for (int i = 0; i < int(l3.size()); i++) { const int64_t v = int64_t(l3[i][1]); if (first_g < 0 && v == pat64(44, 0xC0000)) first_g = i; if (v == pat64(44, 0xA0000 + 10)) last_a = i; }
    check(l3.size() == 9 && first_g >= 0 && first_g < last_a, "a GATHER read of a free area passes the AG's queued reads (no order between clients): at %d before %d", first_g, last_a);
    bool ag_order = true; int k = 0;
    for (const auto& e : l3) for (int j = 0; j < 6; j++) if (int64_t(e[1]) == pat64(44, 0xA0000 + 2 * j)) { if (j != k) ag_order = false; k++; }
    check(ag_order && k == 6, "the AG's own returns still leave in its order");
    eq(0, int64_t(g.c.violations()), "no violations %s", vreport(g.c).c_str());
}

void token_window() {
    suite::group("token window");
    MachConfig c = cfg(); c.read_tokens = 64; c.sram_read_latency = 80;
    Net n(p4::dbg(c)); TileRef p = n.c.tile(12); fill(p, 0, 0x40000);
    for (int j = 0; j < 300; j++) n.rd(12, AG, 2 + j, 4 * j, lw::SIZE_128, lw::M_SRC, 1, 13, 0);
    unsigned max_in = 0;
    for (int i = 0; i < 3000 && (n.pending() || !n.c.quiescent()); i++) {
        n.step();
        const units::Tokens& tk = inj(p).tok;
        max_in = std::max(max_in, tk.reads + tk.loop_held);     // SRAMReadQueue.inFlight() = held()
    }
    check(max_in <= 64, "never more than 64 requests in flight (%u)", max_in);
    check(srq(p).window_full_cycles > 0, "the window fills when the read latency exceeds 64 cycles");
    eq(300, int64_t(n.log(13).size()), "every packet: one per 128-bit read (v1.1)");
    bool ok = true;
    const auto l = n.log(13);
    for (int j = 0; j < 300 && j < int(l.size()); j++) { const auto& e = l[j]; if (int64_t(e[1]) != pat64(12, 4 * j) || int64_t(e[3]) != pat64(12, 4 * j + 2) || lw::size(e[2]) != lw::SIZE_128) ok = false; }
    check(ok, "each a 128-bit packet, in request order");
    out(suite::str("token window: 128-bit reads issued at one a cycle, array latency 80: in flight peaks at %u of 64, then issue waits (%llu cycles) [meas: MemTest token window; v1.0's case, 128-bit returns as two packets, no longer exists]",
                   max_in, (unsigned long long)srq(p).window_full_cycles));
}

void mem_idle() {
    suite::group("mem_idle");
    Net n(p4::dbg(cfg())); TileRef p = n.c.tile(60); p.set_ptr(59, 0x20000);
    check(p.mem_idle(), "idle at start");
    for (int j = 0; j < 20; j++) n.at(59, 2 + j, 60, lw::M_SRC, 0, uint64_t(0x1000 + j));
    bool saw_busy = false;
    for (int i = 0; i < 300; i++) { n.step(); if (!p.mem_idle()) saw_busy = true; }
    check(saw_busy, "busy while writes are in the path");
    check(p.mem_idle(), "idle once every write is in the array");
    for (int j = 0; j < 20; j++) eq(0x1000 + j, int64_t(p.sram_peek64(0x20000 + 2 * j)), "write %d in memory when mem_idle", j);
    fill(p, 0, 64);
    for (int j = 0; j < 30; j++) n.rd(60, AG, n.c.cycle() + 2 + j, 2 * j, lw::SIZE_64, lw::M_SRC, 1, 61, 0);
    bool idle_throughout = true;
    for (int i = 0; i < 100; i++) { n.step(); if (!p.mem_idle()) idle_throughout = false; }
    check(idle_throughout, "queued reads do not hold mem_idle low (DEV)");
}

// Chip.memReport
std::string mem_report(Chip& c) {
    std::string sb;
    uint32_t qp = 0; int qp_at = -1; uint64_t srq_st = 0, w_st = 0, lat = 0, acc = 0; uint32_t we_in_pk = 0; unsigned ej_pk = 0;
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = c.tile(k);
        const BigSram& s = sram(p);
        for (unsigned a = 0; a < BigSram::AREAS; a++) if (s.queue_peak[a] > qp) { qp = s.queue_peak[a]; qp_at = int(k); }
        srq_st += srq(p).stall_cycles; w_st += writer(p).stall_cycles; acc += s.reads_served + s.writes_served;
        for (uint64_t m : srq(p).lat_max) lat = std::max(lat, m);
        we_in_pk = std::max(we_in_pk, we_in_peak(c, k));
        ej_pk = std::max(ej_pk, eject_peak(p));
    }
    sb += suite::str("    area queue peak %u of %u (t%d); SRAM accesses %llu; SRAM read queue stalled %llu cycles, SRAM write queue stalled %llu cycles; read latency max %llu\n",
                     qp, c.cfg().area_queue_depth, qp_at, (unsigned long long)acc, (unsigned long long)srq_st, (unsigned long long)w_st, (unsigned long long)lat);
    uint64_t rd_sq = 0; for (unsigned k = 0; k < 64; k++) rd_sq += sram(c.tile(k)).read_stall_cycles;
    if (w_st > 0 || rd_sq > 0) {
        std::string first;
        for (const auto& v : c.violation_messages()) if (v.find("overflow") != std::string::npos) { first = v; break; }
        sb += suite::str("    FINDING: an area was asked for more than one access a cycle: the SRAM read queue waited %llu cycles at a full area queue, the SRAM write queue %llu; write-engine input peak %u of %u, eject FIFO peak %u of %u; %s\n",
                         (unsigned long long)rd_sq, (unsigned long long)w_st, we_in_pk, c.cfg().we_in_depth, ej_pk, c.cfg().queue_depth,
                         first.empty() ? "no FIFO overflowed" : ("first overflow: " + first).c_str());
    }
    return sb;
}

void oversubscription() {
    suite::group("oversubscription");
    std::string sb = "one area shared by the SRAM read queue (reads every Pr cycles) and incoming writes from a neighbour (every Pw cycles), 400 of each; depth 8, read latency 2 [meas: MemTest oversubscription]:\n";
    sb += suite::str("  %-4s %-4s %-7s %-14s %-14s %-14s %-12s %s\n", "Pr", "Pw", "demand", "read lat max", "read waits", "write stalls", "we-in peak", "outcome");
    const int cases[][2] = {{2, 2}, {1, 4}, {2, 1}, {1, 2}, {1, 1}};
    for (const auto& cs : cases) {
        Net n(p4::dbg(cfg())); TileRef p = n.c.tile(36); fill(p, 0x60000, 0x1000); p.set_ptr(35, 0x70000);
        for (int j = 0; j < 400; j++) n.rd(36, AG, 2 + int64_t(j) * cs[0], 0x60000 + 2 * j, lw::SIZE_64, lw::M_SRC, 1, 37, 0);
        for (int j = 0; j < 400; j++) n.at(35, 2 + int64_t(j) * cs[1], 36, lw::M_SRC, 0, uint64_t(j));
        n.run_all(20000);
        const bool ovf = n.c.violations() > 0;
        const double demand = 1.0 / cs[0] + 1.0 / cs[1];
        sim::Cycle oc = 0; n.c.sim().fifo_overflows(writer(p).in, &oc);
        const std::string outcome = ovf ? suite::str("we-in overflows %lld cycles after the queues start", (long long)oc - 2)
                                        : suite::str("all %llu writes landed", (unsigned long long)writer(p).taken);
        const auto l = n.log(37);
        const int64_t last = l.empty() ? 0 : int64_t(l.back()[0]);
        sb += suite::str("  %-4d %-4d %-7.2f %-14llu %-14llu %-14llu %-12s %s; last read packet at cycle %lld (last read issued on schedule at %lld)\n", cs[0], cs[1], demand,
                         (unsigned long long)srq(p).lat_max[AG], (unsigned long long)sram(p).read_stall_cycles, (unsigned long long)writer(p).stall_cycles,
                         (std::to_string(we_in_peak(n.c, 36)) + " of 4").c_str(), outcome.c_str(), (long long)last, 2 + 399LL * cs[0]);
        check(!ovf, "demand %s: no overflow: the SRAM read queue waits instead (writes first)", jdouble(demand).c_str());
        eq(400, int64_t(writer(p).taken), "demand %s: every write lands", jdouble(demand).c_str());
        if (cs[0] == 1 && cs[1] == 1) out("  memReport for the full-rate case:\n" + strip_trailing(mem_report(n.c)));
    }
    out(strip_trailing(sb));
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
    if (want("decode")) unit_decode();
    if (want("partial")) unit_partial_and_tie();
    if (want("stall")) unit_stall();
    if (want("combining")) unit_combining();
    if (want("wrap")) unit_wrap_small();
    if (want("latency")) unit_latency();
    if (want("throughput")) throughput();
    if (want("priority")) priority();
    if (want("packets")) packets();
    if (want("returnorder")) return_order();
    if (want("tokenwindow")) token_window();
    // disjoint: W22-14 (Chip::new_phase: W20; big SRAM's disjoint-buffer facts: W26)
    if (want("memidle")) mem_idle();
    if (want("oversubscription")) oversubscription();
    std::fputs(report.c_str(), stdout);
    return suite::done(std::string("MemTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : ""));
}
#else   // the timing builds keep none of the statistics MemTest reads
int main() {
    std::fprintf(stderr, "suite_mem: needs SIM_CHECKS (the suite build, MODE=suite, or a debug build)\n");
    return 2;
}
#endif
