// suite_reqbus.cpp (WORKER22, P4, inventory C17): v1.3's RequestBusTest (sim/ref/meshsim/test/RequestBusTest.java):
// the request port, the request bus and the receive queues in the tree, from a test (W20's ReqBus: MachConfig::
// reqbus_host, Chip::reqbus(); the machine as specified) and from C (fw/tests/req.c, C&C 2 draining its queue from the
// test). Every assertion and printed line is Java's, printed as Java prints them (at once), so the output equals
// run_tests.sh's logs reqbus (-1), reqbus-shuf (4); fixtures tests/p4suites/.
//
//   suite_reqbus [seed] [--only=GROUP,...]      groups: basics fairness credits backpressure receive drops fromc
//
// STATUS. Ported: backpressure, receive, drops, fromc. Waiting on W20: the delivered requests' records (ref's delivered
// list with deliveredAt and srcTile: W22-19) for basics, fairness, credits.
//
// Test-side code (the configuration regime, D6).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mach.h"
#include "p4_debug.h"
#include "suite.h"
#include "units/linkword.h"
#include "units/regmap.h"
#include "units/tile_ctl.h"

using namespace mach;
namespace lw = units::lw;
using suite::check;
using suite::eq;

namespace {

int64_t shuffle = -1;
std::set<std::string> only;
bool want(const char* g) { return only.empty() || only.count(g); }

// Registers' RQ fields (Registers.java: RQ_REQUESTOR [6:0], RQ_TARGET [9:7])
uint32_t rq_requestor(uint32_t w0) { return w0 & 0x7F; }
uint32_t rq_target(uint32_t w0) { return w0 >> 7 & 7; }

uint32_t reg(unsigned r) { return units::REG_BASE + 4 * r; }
// RequestBusTest.machine(): Net on Config.machine() with the bus driven from the test; settle: every core in the boot
// ROM's WFI (done pulsed, waiting for GO)
MachConfig machine_cfg() { MachConfig k = MachConfig::machine(); k.shuffle_seed = shuffle; k.reqbus_host = true; return k; }
void settle(Net& n) {
    for (int i = 0; i < 200; i++) {
        bool all = true; for (unsigned p = 0; p < 64; p++) if (!n.c.tile(p).in_wfi()) all = false;
        if (all) break;
        n.step();
    }
}
void store(Net& n, TileRef p, uint32_t addr, uint32_t v) {
    const std::string e = p.store(n.c.cycle() + 1, addr, v);
    if (!e.empty()) { std::fprintf(stderr, "FATAL suite_reqbus: store: %s\n", e.c_str()); std::abort(); }
    n.step();
}
std::optional<uint32_t> load(Net& n, TileRef p, uint32_t addr) { const auto v = p.load(n.c.cycle() + 1, addr, 4); n.step(); return v; }
int64_t load_i(Net& n, TileRef p, uint32_t addr) { const auto v = load(n, p, addr); return v ? int64_t(int32_t(*v)) : -1; }
// RequestBusTest.push: a request into p's FIFO directly, as a store at the current cycle would
void push(Net& n, TileRef p, uint32_t target, uint32_t tag) { p.req_set_w(0, target << 7 | tag << 10); p.req_set_w(1, tag); p.req_send(n.c.cycle(), tag); }
void run(Net& n, int k) { for (int i = 0; i < k; i++) n.step(); }
size_t qsize(Net& n, unsigned target) { return n.c.reqbus().queue_size(target); }

void back_pressure() {
    suite::group("back-pressure to the request FIFO");
    Net n(p4::dbg(machine_cfg())); settle(n);
    TileRef s = n.c.tile(30);
    for (uint32_t j = 0; j < 128; j++) push(n, n.c.tile(31), 0, j);
    run(n, 1300);
    eq(128, int64_t(qsize(n, 0)), "target 0 full");
    for (uint32_t j = 0; j < 128; j++) push(n, s, 0, 1000 + j);
    run(n, 100);
    for (uint32_t j = 0; j < 2; j++) push(n, s, 0, 2000 + j);
    const uint32_t room = 1u << units::EV_REQUEST_ROOM;
    store(n, s, reg(units::EVENTS_MASK), room | units::EVENTS_MASK_RESET); load(n, s, reg(units::EVENTS));
    run(n, 50);
    eq(int64_t(1) << units::ST_REQUEST_BUSY, load_i(n, s, reg(units::STATUS)) & (int64_t(1) << units::ST_REQUEST_BUSY), "request_busy: the FIFO is full (128 entries)");
    check(!s.req_can_push(n.c.cycle() + 1), "a REQUEST_SEND store would stall");
    eq(0, load_i(n, s, reg(units::EVENTS_PEEK)) & room, "no [11] yet");
    n.c.reqbus().receive(0, n.c.cycle() + 1);
    run(n, 80);
    eq(0, load_i(n, s, reg(units::STATUS)) & (int64_t(1) << units::ST_REQUEST_BUSY), "a pop at the receiver frees an entry");
    eq(room, load_i(n, s, reg(units::EVENTS_PEEK)) & room, "request FIFO no longer full: [11] posted on request_busy's fall");
}

void receive_side() {
    suite::group("a tile's receive side");
    Net n(p4::dbg(machine_cfg())); settle(n);
    TileRef r = n.c.tile(40);
    const uint32_t pend = 1u << units::EV_REQUEST_PENDING;
    for (uint32_t j = 0; j < 3; j++) push(n, n.c.tile(j), 0, j);
    run(n, 150);
    eq(3, int64_t(qsize(n, 0)), "three requests in target 0's queue");
    eq(0, load_i(n, r, reg(units::EVENTS_PEEK)) & pend, "[12] never rises on a mesh tile");
    eq(0, load_i(n, r, reg(units::REQUEST_GET_W0)), "GET reads 0: no queue");
    eq(0, r.block().conditions, "no underflow from GET");
    eq(0, load_i(n, r, reg(units::REQUEST_RECEIVE)), "RECEIVE reads 0");
    eq(int64_t(1) << units::COND_RECV_UNDERFLOW, r.block().conditions, "and sets receive underflow (ERRORS [14]): every such load on a mesh tile");
    eq(3, int64_t(qsize(n, 0)), "the controllers' queues untouched");
    n.c.reqbus().receive(1, n.c.cycle() + 1);
    eq(1, int64_t(n.c.reqbus().underflows()), "a receiver's RECEIVE on its empty queue is an underflow too");
}

void drops() {
    suite::group("drops at the root");
    Net n(p4::dbg(machine_cfg())); settle(n);
    push(n, n.c.tile(4), 5, 1);
    push(n, n.c.tile(5), 3, 2);
    run(n, 120);
    eq(2, int64_t(n.c.reqbus().dropped()), "both dropped at the root");
    eq(2, int64_t(n.c.violations()), "and flagged (6.7)");
    const auto& vm = n.c.violation_messages();
    check(vm.size() >= 2 && vm[0].find("reserved") != std::string::npos && vm[1].find("no receiver") != std::string::npos, "naming why");
    eq(128, n.c.reqbus().credits(0), "target 0's credits untouched");
}

void from_c() {
    suite::group("from C");
    MachConfig k = Mach::cfg(shuffle); k.reqbus_host = true;
    Mach m(p4::dbg(k));
    const Mach::Image im = m.boot("req");
    ReqBus& b = m.c.reqbus();
    std::vector<std::array<uint32_t, 3>> got;
    // C&C 2 drains its queue slowly (one request every 64 cycles): 600 > 256 (its queue) + 128 (the FIFO) + 2 (the tree),
    // so tile 0's FIFO fills and its stores stall
    const bool ok = m.run([&] {
        const int64_t now = m.c.cycle() + 1;
        if ((m.c.cycle() & 63) == 0 && b.pending(1, now)) { const uint32_t w0 = b.get(1, now, 0), w1 = b.get(1, now, 1); got.push_back({w0, w1, b.receive(1, now)}); }
        return m.all_stopped() && got.size() >= 600;
    }, 400000);
    check(ok && m.all_exited(), "every tile ran req to its exit, and C&C 2 drained 600: %s", m.halt_summary().c_str());
    eq(600, m.word(0, im, "r", 0), "tile 0 sent 600 requests");
    int bad = 0;
    for (size_t i = 0; i < got.size(); i++) {
        const auto& g = got[i];
        if (g[2] != i || rq_requestor(g[0]) != 0 || g[0] >> 10 != i || g[1] != 0x1111 || rq_target(g[0]) != 1) bad++;
    }
    eq(0, bad, "each with requestor 0 and target 1, in order, intact");
    const uint64_t stall = m.c.tile(0).unit<units::TileCore>("core").rv.stall_request;
    check(stall > 0, "tile 0's REQUEST_SEND stores stalled on the full FIFO (%llu cycles)", (unsigned long long)stall);
    eq(0, int64_t(m.c.violations()), "no violations %s", m.c.violation_report().c_str());
    uint32_t c = 0; for (unsigned p = 0; p < 64; p++) c |= m.c.tile(p).block().conditions;
    eq(0, c, "no errors");
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
    if (shuffle >= 0) { std::printf("shuffled child order, seed %lld\n", (long long)shuffle); std::fflush(stdout); }
    // basics, fairness, credits: W22-19 (the delivered requests' records)
    if (want("backpressure")) back_pressure();
    if (want("receive")) receive_side();
    if (want("drops")) drops();
    if (want("fromc")) from_c();
    return suite::done(std::string("RequestBusTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : ""));
}
