// suite_reqbus.cpp (WORKER22, P4, inventory C17): v1.3's RequestBusTest (sim/ref/meshsim/test/RequestBusTest.java):
// the request port, the request bus and the receive queues in the tree, from a test (W20's ReqBus: MachConfig::
// reqbus_host, Chip::reqbus(); the machine as specified) and from C (fw/tests/req.c, C&C 2 draining its queue from the
// test). Every assertion and printed line is Java's, printed as Java prints them (at once), so the output equals
// run_tests.sh's logs reqbus (-1), reqbus-shuf (4); fixtures tests/p4suites/.
//
//   suite_reqbus [seed] [--only=GROUP,...]      groups: basics fairness credits backpressure receive drops fromc
//
// STATUS. Ported: fromc. Waiting on W20: a tile's request port from the test (ref's req.setW, req.send, canPush: W22-11)
// for fairness, credits, backpressure, receive, drops; the delivered requests' records (ref's delivered list with
// deliveredAt and srcTile: W22-19) for basics, fairness, credits.
//
// Test-side code (the configuration regime, D6).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "mach.h"
#include "suite.h"
#include "units/linkword.h"
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

void from_c() {
    suite::group("from C");
    MachConfig k = Mach::cfg(shuffle); k.reqbus_host = true;
    Mach m(k);
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
        if (std::strncmp(argv[i], "--only=", 7) == 0) {
            std::string s = argv[i] + 7; size_t p = 0;
            while (p <= s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); only.insert(s.substr(p, q - p)); p = q + 1; }
        } else a.push_back(argv[i]);
    }
    if (!a.empty()) shuffle = std::atoll(a[0].c_str());
    if (shuffle >= 0) { std::printf("shuffled child order, seed %lld\n", (long long)shuffle); std::fflush(stdout); }
    // basics, fairness, credits, backpressure, receive, drops: W22-11, W22-19
    if (want("fromc")) from_c();
    return suite::done(std::string("RequestBusTest") + (shuffle >= 0 ? " (shuffled, seed " + std::to_string(shuffle) + ")" : ""));
}
