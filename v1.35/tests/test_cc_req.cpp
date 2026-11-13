// test_cc_req: the request bus end to end on the chip with C&C 1-3 as cores and C&C 1's slot (T2's machine,
// tests/t2_harness.h): a tile's REQUEST_SEND through its leaf, the row node and the root into C&C 1's queue and its
// REQUEST_RECEIVE, against ref's figures.
//   tests/m3-images/cc_req.c (C&C 1, booted through the host port): CMD_JUMP to tile 5, GO from its peripheral, 6,000
//   cycles without draining (its queue fills, the root runs out of credit, tile 5's FIFO fills and its sends stall: the
//   same-cycle free), then 300 receives through EVENTS_PEEK [12], REQUEST_GET_W0-W2 and REQUEST_RECEIVE (each logged
//   with mcycle), one receive on the empty queue (reads 0, ERRORS [14]), SIM_EXIT.
//   tests/m3-images/tile_req.c (tile 5, in its program memory before the boot): 300 REQUEST_SENDs back to back, mcycle
//   after each.
// ref's figures: tests/m3-images/cc_req.ref (oracle/ref/ccreq.sh, CcReq.java). Every figure is a stamp the programs took
// (mcycle) or ref's SIM_EXIT cycle (TileCtl's sim_exit_at, which counts as ref does: AUDIT-M3 7's mapping is inside it).
//   - D34 mode 0 (exact) on one domain: every figure equal to ref's.
//   - mode 3 (the spec, the default) on one domain and on T2's split: the same words (the requests as sent, stamped with
//     requestor 5), its lag_cycles reported; where it is 0, every cycle equal too. This test fills the queue and the
//     FIFO on purpose, so its lag is positive and the cycles differ: reported, as D29 asks.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "harness.h"
#include "t2_harness.h"
#include "units/host_port.h"
#include "units/reqbus.h"
#include "units/tile_ctl.h"

namespace {

constexpr unsigned N = 300;

struct Figures {
    std::string err;
    uint64_t exit_at = 0, beats = 0, lag = 0;
    uint32_t exit = 0, extra[2] = {};
    uint32_t sent[N] = {}, words[4 * N] = {}, got[N] = {};
};

bool load_ref(Figures& f) {
    FILE* in = std::fopen("tests/m3-images/cc_req.ref", "r");
    if (!in) return false;
    char line[512];
    unsigned rows = 0;
    while (std::fgets(line, sizeof line, in)) {
        unsigned long long a;
        unsigned i, s, w0, w1, w2, w3, g, x, y, e;
        if (std::sscanf(line, "exit %llu %x", &a, &e) == 2) { f.exit_at = a; f.exit = e; }
        else if (std::sscanf(line, "r %u %u %x %x %x %x %u", &i, &s, &w0, &w1, &w2, &w3, &g) == 7 && i < N) {
            f.sent[i] = s; f.words[4 * i] = w0; f.words[4 * i + 1] = w1; f.words[4 * i + 2] = w2; f.words[4 * i + 3] = w3;
            f.got[i] = g; rows++;
        } else if (std::sscanf(line, "extra %x %x", &x, &y) == 2) { f.extra[0] = x; f.extra[1] = y; }
        else if (std::sscanf(line, "beats %llu", &a) == 1) f.beats = a;
    }
    std::fclose(in);
    return rows == N;
}

Figures run(unsigned credit_latency, unsigned parts, sim::Options opt = {}) {
    Figures f;
    uint8_t dom[64];
    t2::partition(parts, dom);
    opt.threads = parts;
    units::reqbus::ReqBusConfig rc;
    rc.credit_latency = credit_latency;
    auto m = t2::build_machine(parts > 1 ? dom : nullptr, opt, f.err, &rc);
    if (!m) return f;
    sim::Sim& s = *m->sim;
    t2::Image cc, tl;
    if (!t2::load_image("tests/m3-images", "cc_req", cc, f.err) || !t2::load_image("tests/m3-images", "tile_req", tl, f.err)) return f;
    // tile 5's program memory holds tile_req (C&C 1's CMD_JUMP and GO start it), as CcReq pokes it
    uint32_t* pm5 = m->dmem(5) + units::MEM_WORDS;
    for (size_t i = 0; i < tl.code.size(); i++) pm5[i] = tl.code[i];
    // Mach.hostBoot: C&C 1's program (and data), then CMD_JUMP to _start, through the host port
    units::HostPort& h = s.unit<units::HostPort>("cc1.host");
    h.load(0x01000000, cc.code.data(), cc.code.size());
    if (!cc.data.empty()) h.load(cc.at("_data_start"), cc.data.data(), cc.data.size());
    const uint32_t entry = cc.at("_start");
    h.command(0x4Au << 24, &entry, 1);
    s.wake("cc1.host");
    const units::TileCore& core = s.unit<units::TileCore>("cc1.core");
    for (uint64_t c = 0; c < 60000 && core.rv.halt == units::Rv32::RUN; c += 100) s.run(100);
    if (core.rv.halt != units::Rv32::HALT_EXIT) { f.err = "C&C 1 did not exit (halt " + std::to_string(core.rv.halt) + ")"; return f; }
    const units::TileCtl& ctl = s.unit<units::TileCtl>("cc1.ctl");
    f.exit = ctl.s.sim_exit_value;
    f.exit_at = ctl.s.sim_exit_at;
    const uint32_t* d5 = m->dmem(5);
    const uint32_t* d64 = m->dmem(64);
    const uint32_t ts = tl.at("stamp"), cs = cc.at("stamp"), cw = cc.at("words"), ce = cc.at("extra");
    for (unsigned i = 0; i < N; i++) {
        f.sent[i] = d5[ts / 4 + i];
        for (unsigned k = 0; k < 4; k++) f.words[4 * i + k] = d64[cw / 4 + 4 * i + k];
        f.got[i] = d64[cs / 4 + i];
    }
    f.extra[0] = d64[ce / 4];
    f.extra[1] = d64[ce / 4 + 1];
    f.beats = s.unit<units::reqbus::ReqRoot>("reqbus.root").beats_at_root;
    const units::reqbus::ReqBusUnits u = units::reqbus::resolve(s, ~uint64_t(0), 7, rc);
    f.lag = units::reqbus::lag_cycles(u);
    return f;
}

// The figures against ref's: the words always; the cycles when `cycles`. Returns how many cycle figures differ.
unsigned compare(const Figures& r, const Figures& o, bool cycles, const char* what) {
    if (!o.err.empty()) { ::test::fail(__FILE__, __LINE__, "%s: %s", what, o.err.c_str()); return 1; }
    if (o.exit != r.exit || o.beats != r.beats || o.extra[0] != r.extra[0] || o.extra[1] != r.extra[1]) {
        ::test::fail(__FILE__, __LINE__, "%s: exit value %x (ref %x), beats %" PRIu64 " (ref %" PRIu64 "), the empty receive %x and ERRORS %x (ref %x, %x)",
                     what, o.exit, r.exit, o.beats, r.beats, o.extra[0], o.extra[1], r.extra[0], r.extra[1]);
        return 1;
    }
    for (unsigned i = 0; i < 4 * N; i++)
        if (o.words[i] != r.words[i]) {
            ::test::fail(__FILE__, __LINE__, "%s: request %u word %u: %08x, ref %08x", what, i / 4, i % 4, o.words[i], r.words[i]);
            return 1;
        }
    unsigned differ = (o.exit_at != r.exit_at);
    for (unsigned i = 0; i < N; i++) differ += (o.sent[i] != r.sent[i]) + (o.got[i] != r.got[i]);
    if (cycles && differ) {
        for (unsigned i = 0; i < N; i++)
            if (o.sent[i] != r.sent[i] || o.got[i] != r.got[i]) {
                ::test::fail(__FILE__, __LINE__, "%s: request %u: sent at %u (ref %u), received at %u (ref %u)", what, i,
                             o.sent[i], r.sent[i], o.got[i], r.got[i]);
                break;
            }
        if (o.exit_at != r.exit_at)
            ::test::fail(__FILE__, __LINE__, "%s: exit at %" PRIu64 ", ref %" PRIu64, what, o.exit_at, r.exit_at);
    }
    return differ;
}

}  // namespace

TEST(cc_req_mode0_one_domain_equals_ref_in_every_figure) {
    Figures r;
    CHECK(load_ref(r));
    const Figures o = run(0, 1);
    compare(r, o, true, "mode 0, one domain");
    CHECK_EQ(o.lag, uint64_t(0));
    std::printf("  mode 0, one domain: exit at %" PRIu64 " (ref %" PRIu64 "), 300 requests, every send and receive cycle equal to ref's, %" PRIu64 " beats at the root\n",
                o.exit_at, r.exit_at, o.beats);
    // the queue and the FIFO filled: the sends stalled (a gap well over a request's 8 beats), and the 300th was sent
    // only after C&C 1 began to receive
    CHECK(o.sent[N - 1] > o.got[0]);
}

TEST(cc_req_mode0_under_debug_options) {
    Figures r;
    CHECK(load_ref(r));
    sim::Options a; a.step_all = true;
    sim::Options b; b.shuffle_seed = 5;
    compare(r, run(0, 1, a), true, "mode 0, step-all");
    compare(r, run(0, 1, b), true, "mode 0, shuffled");
}

TEST(cc_req_mode3_same_words_lag_reported) {
    Figures r;
    CHECK(load_ref(r));
    Figures one;
    for (unsigned parts : {1u, 4u}) {
        const Figures o = run(3, parts);
        if (parts == 1) one = o;
        else {   // the mode is configuration, never the partition: the split gives one domain's figures exactly
            CHECK_EQ(o.lag, one.lag);
            CHECK_EQ(o.exit_at, one.exit_at);
            bool same = true;
            for (unsigned i = 0; i < N; i++) same &= o.sent[i] == one.sent[i] && o.got[i] == one.got[i];
            CHECK(same);
        }
        const unsigned differ = compare(r, o, o.lag == 0, parts == 1 ? "mode 3, one domain" : "mode 3, T2's split");
        std::printf("  mode 3, %s: the same 300 requests' words as ref; lag_cycles %" PRIu64 "; %u of %u cycle figures differ from ref's (exit at %" PRIu64 ", ref %" PRIu64 ")\n",
                    parts == 1 ? "one domain" : "T2's split (4 domains)", o.lag, differ, 2 * N + 1, o.exit_at, r.exit_at);
        if (o.lag == 0) CHECK_EQ(differ, 0u);
        else CHECK(o.lag > 0);
    }
}
