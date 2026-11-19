// test_ag.cpp: the DMA unit's address generator (machine-spec 8.2-8.5), every expectation from the specification or,
// where it leaves the cycle to the implementation, from the RTL's unit (rtl/blocks/dma_unit/README.md, "A transfer";
// address_generator.veryl's header). No expectation here comes from the simulator's output.
//
// THE BENCH: stand-ins around the unit: the tile control unit's `dma` RegFile and `start` Wire, the table store's
// STREAM, GROUP and PTR RegFiles, the read arbiter's client 1 (taking the read offered, or refusing it on the cycles a
// case names), and the tile control unit's input (status). ERRORS.drift's pulse is the read arbiter's, for a read the
// generator marks a group transfer's (mem::A_GROUP): the mark is checked here, the pulse's cycle on the whole tile
// (test_mach).
//
// THE CYCLES. E is the cycle DMA_START's store is presented in: the cycle before the tile control unit drives `start`
// (it drives it at the edge that ends E: tile_ctl.h). A store issued in t is presented in E = t + 1 (6.4), so with
// L = TABLE_LATENCY:
//     F, the transfer's first cycle  = E + 2L + 1 in a stream transfer (2L + 2 after the start store: 8.2)
//                                    = E + 4L + 2 in a group transfer (4L + 3 after it), whatever its first entry holds
//     a read is TAKEN in the cycle it is offered (14.1), so the arbiter's take cycles are the timetable's (8.3, 8.4)
//     w, the walk's last cycle; EVENTS.dma_done is set in w + 1 and STATUS.dma_busy falls at its end (8.2): the
//     generator's `done` reaches the tile control unit at w + 2, the edge that ends w + 1
//     an entry is in 2L + 1 cycles after its read's issue, the reads one a cycle from E while a place is free (8.5)
#include <cstdlib>
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/ag.h"
#include "units/net.h"

using namespace sim;
using units::AddrGen;
namespace lw = units::lw;
namespace mem = units::mem;

namespace {

// The entries' layouts (8.1, 8.3, 8.4): STREAM_HDR is the header block (size [1:0], counter [4:2], mode [7:5]), dst
// [14:8] and count [31:15]; GROUP is word 0 (rotations [17:0], first_stream [25:18], streams - 1 [29:26]) and word 1, the
// gap [7:0]; DMA_CONFIG is kind [0], start [8:1], count [17:9] and gap [20:18].
uint32_t stream_hdr(uint32_t count, uint32_t size, uint32_t counter, uint32_t mode, uint32_t d) {
    return (size & 3) | (counter & 7) << 2 | (mode & 7) << 5 | (d & 0x7F) << 8 | (count & 0x1FFFF) << 15;
}
uint32_t dma_config(uint32_t kind, uint32_t start, uint32_t count, uint32_t gap = 0) {
    return (kind & 1) | (start & 0xFF) << 1 | (count & 0x1FF) << 9 | (gap & 7) << 18;
}

struct Tables {
    uint32_t src[256] = {}, dest[256] = {}, hdr[256] = {}, group0[64] = {}, group1[64] = {}, ptr[256] = {};
    void group(unsigned g, uint32_t R, uint32_t first, uint32_t T, uint32_t gap) {
        group0[g] = (R & 0x3FFFF) | (first & 0xFF) << 18 | ((T - 1) & 0xF) << 26; group1[g] = gap & 0xFF;
    }
    // stream i: `count` elements of 32 bits from word 0x1000 * i, APPENDs on counter 1 to the tile itself (dst 0)
    void stream(unsigned i, uint32_t count) { src[i] = 0x1000 * i; hdr[i] = stream_hdr(count, lw::SIZE_32, 1, lw::M_SRC, 0); }
};

// The tile control unit's side: DMA_CONFIG at cycle 1 (and `cfg2` from `at2`), a start driven at each cycle of `starts`.
struct Blk {
    RegFileOut dma; WireOut start;
    uint32_t cfg, base; std::vector<Cycle> starts; size_t n = 0; Word count = 0;
    uint32_t cfg2 = 0; Cycle at2 = 0;
    Blk(Ports& p, uint32_t c, uint32_t b, std::vector<Cycle> s) : cfg(c), base(b), starts(std::move(s)) {
        p.out(dma, "dma", units::DMA_REGS); p.out(start, "start");
    }
    uint64_t digest() const { return hash_fields(n, count); }
    Wake step(Ctx& c) {
        if (c.now() == 1) { c.write(dma, units::DMA_R_CONFIG, cfg); c.write(dma, units::DMA_R_GATHER, base & 0x1FFFFF); }
        if (at2 && c.now() == at2) c.write(dma, units::DMA_R_CONFIG, cfg2);
        while (n < starts.size() && starts[n] == c.now()) { n++; c.drive(start, ++count); }
        return c.next();
    }
};
// The table store's RegFiles: STREAM and GROUP (`view`) and PTR.
struct Ts {
    RegFileOut view, ptr;
    Tables tb;
    Ts(Ports& p, const Tables& t) : tb(t) { p.out(view, "view", units::TV_REGS); p.out(ptr, "ptr", units::PTR_REGS); }
    uint64_t digest() const { return 0; }
    Wake step(Ctx& c) {
        if (c.now() != 1) return Wake::none();
        for (unsigned i = 0; i < 256; i++) {
            c.write(view, units::TV_STREAM_SRC + i, tb.src[i]); c.write(view, units::TV_STREAM_DEST + i, tb.dest[i]);
            c.write(view, units::TV_STREAM_HDR + i, tb.hdr[i]); c.write(ptr, i, tb.ptr[i]);
        }
        for (unsigned g = 0; g < 64; g++) { c.write(view, units::TV_GROUP + 2 * g, tb.group0[g]); c.write(view, units::TV_GROUP + 2 * g + 1, tb.group1[g]); }
        return Wake::none();
    }
};
// The read arbiter's client 1: takes the read offered, but on the cycles in `refuse`.
struct Arb {
    HoldIn in;
    std::vector<Cycle> refuse;
    struct Got { Cycle at; Word a, b; };
    std::vector<Got> got;
    Arb(Ports& p, std::vector<Cycle> r) : refuse(std::move(r)) { p.in(in, "in", mem::kReadReqWidth); got.reserve(1024); }
    uint64_t digest() const { return hash_fields(uint64_t(got.size())); }
    Wake step(Ctx& c) {
        if (!c.ready(in)) return Wake::none();
        for (Cycle r : refuse) if (r == c.now()) return c.next();
        const Word* w = c.peek(in); got.push_back({c.now(), w[0], w[1]}); c.take(in);
        return Wake::none();
    }
};
// The tile control unit's side of the generator's outputs: each new status with the cycle it is seen.
struct Mon {
    RegIn status;
    std::vector<std::pair<Cycle, Word>> st;
    explicit Mon(Ports& p) { p.in(status, "status", 1); }
    uint64_t digest() const { return hash_fields(uint64_t(st.size())); }
    Wake step(Ctx& c) {
        if (c.valid(status) && (st.empty() || st.back().second != c.read(status)[0])) st.push_back({c.now(), c.read(status)[0]});
        return Wake::none();
    }
};

struct Bench {
    Sim sim;
    Bench(const Tables& tb, uint32_t cfg, unsigned L, std::vector<Cycle> starts, std::vector<Cycle> refuse = {}, uint32_t base = 0, unsigned tile = 5,
          uint32_t cfg2 = 0, Cycle at2 = 0)
        : sim(build(tb, cfg, L, std::move(starts), std::move(refuse), base, tile, cfg2, at2)) {}
    static Sim build(const Tables& tb, uint32_t cfg, unsigned L, std::vector<Cycle> starts, std::vector<Cycle> refuse, uint32_t base, unsigned tile,
                     uint32_t cfg2, Cycle at2) {
        Options opt;                                  // the runner's shuffled steps: SIM_SHUFFLE=<seed>
        if (const char* s = std::getenv("SIM_SHUFFLE")) opt.shuffle_seed = uint64_t(std::strtoull(s, nullptr, 10)) + 1;
        Config c(opt);
        auto& b = c.add<Blk>("blk", cfg, base, std::move(starts));
        b.cfg2 = cfg2; b.at2 = at2;
        auto& a = c.add<AddrGen>("ag", tile, L, true);
        auto& t = c.add<Ts>("ts", tb);
        auto& s = c.add<Arb>("arb", std::move(refuse));
        auto& m = c.add<Mon>("mon");
        c.connect(b.dma, a.dma); c.connect(b.start, a.start);
        c.connect(t.view, a.view); c.connect(t.ptr, a.ptr);
        c.connect(a.rd, s.in);
        c.connect(a.status, m.status);
        c.missed_wake_after("arb", 0);
        return c.build();
    }
    const std::vector<Arb::Got>& got() { return sim.unit<Arb>("arb").got; }
    const Mon& mon() { return sim.unit<Mon>("mon"); }
};

constexpr Cycle S = 10;                           // the cycle `start` is driven in: E = S - 1
constexpr Cycle E = S - 1;
Cycle first_stream_cycle(unsigned L) { return E + 2 * L + 1; }
Cycle first_group_cycle(unsigned L) { return E + 4 * L + 2; }

// `running` is seen by the tile control unit at E + 2 and `done` at w + 2, and nothing else.
bool status_is(const Mon& m, Cycle w) {
    return m.st.size() == 2 && m.st[0].first == E + 2 && m.st[0].second == Word(AddrGen::ST_RUNNING) && m.st[1].first == w + 2 &&
           m.st[1].second == Word(AddrGen::ST_DONE);
}
// The reads taken, at the cycles given, in order.
bool taken_at(const std::vector<Arb::Got>& got, std::vector<Cycle> at) {
    if (got.size() != at.size()) return false;
    for (size_t i = 0; i < at.size(); i++) if (got[i].at != at[i]) return false;
    return true;
}

}  // namespace

// 8.2, 8.3: a stream transfer. The lead-in 2L + 2 after the start store; one element a cycle; DMA_CONFIG.gap idle cycles
// after each stream but the last; element e at source + e words and, for WRITE, destination + e x the size's bytes; dst
// relative on a compute tile; the end the cycle after the last element is taken.
TEST(a_stream_transfer_lead_in_elements_gap_and_end) {
    for (unsigned L = 1; L <= 3; L++) {
        Tables tb;
        // stream 7: 3 elements of 64 bits, APPEND, counter 2, dst 1 (tile 5: tile 6); stream 8: 2 of 32 bits, WRITE to
        // 0x40, dst 66 (absolute); the transfer's gap 2
        tb.src[7] = 0x100; tb.hdr[7] = stream_hdr(3, lw::SIZE_64, 2, lw::M_SRC, 1);
        tb.src[8] = 0x200; tb.dest[8] = 0x40; tb.hdr[8] = stream_hdr(2, lw::SIZE_32, 0, lw::M_WRITE, 66);
        Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 7, 2, 2), L, {S});
        b.sim.run(60);
        const Cycle F = first_stream_cycle(L);
        CHECK(taken_at(b.got(), {F, F + 1, F + 2, F + 5, F + 6}));
        const Word a0[] = {mem::req_a(0x100, lw::SIZE_64, lw::M_SRC, 2, 6), mem::req_a(0x102, lw::SIZE_64, lw::M_SRC, 2, 6),
                           mem::req_a(0x104, lw::SIZE_64, lw::M_SRC, 2, 6), mem::req_a(0x200, lw::SIZE_32, lw::M_WRITE, 0, 66),
                           mem::req_a(0x201, lw::SIZE_32, lw::M_WRITE, 0, 66)};
        const Word b0[] = {0, 0, 0, 0x40, 0x44};
        for (size_t j = 0; j < 5; j++) { CHECK_EQ(b.got()[j].a, a0[j]); CHECK_EQ(b.got()[j].b, b0[j]); }
        CHECK(status_is(b.mon(), F + 6));
        CHECK_EQ(b.sim.unit<AddrGen>("ag").violations(), 0u);
        CHECK_EQ(b.sim.unit<AddrGen>("ag").drift_events, uint64_t(0));   // (a0: a stream transfer's reads are not marked)
    }
}

// 8.3: a special tile's stream dst below TILE_COUNT is absolute (on a compute tile it is relative, above).
TEST(a_special_tiles_stream_dst_is_absolute) {
    Tables tb;
    tb.src[0] = 0x100; tb.hdr[0] = stream_hdr(1, lw::SIZE_32, 1, lw::M_SRC, 9);
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 1), 1, {S}, {}, 0, 64);
    b.sim.run(40);
    CHECK_EQ(b.got().size(), size_t(1));
    CHECK_EQ(mem::a_dst(b.got()[0].a), 9u);
}

// 8.5: one read a cycle from E and an entry in 2L + 1 cycles later: streams of one element each run back to back, from
// the first cycle, for as long as the transfer lasts; READ_AHEAD's 2L + 2 places are just enough.
TEST(one_element_streams_run_back_to_back) {
    for (unsigned L = 1; L <= 3; L++) {
        Tables tb;
        for (unsigned i = 0; i < 40; i++) tb.stream(i, 1);
        Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 40), L, {S});
        b.sim.run(100);
        const Cycle F = first_stream_cycle(L);
        std::vector<Cycle> at;
        for (unsigned i = 0; i < 40; i++) at.push_back(F + i);
        CHECK(taken_at(b.got(), at));
        CHECK(status_is(b.mon(), F + 39));
    }
}

// 8.3: a count-0 stream sends nothing and takes only the gap. With a gap of 0, or as the transfer's last stream, it takes
// no cycle; its table read still takes its turn (8.5: the wait is part of the timetable).
TEST(count_zero_streams) {
    for (unsigned L = 1; L <= 3; L++) {
        const Cycle F = first_stream_cycle(L);
        {   // gap 3: A (2 elements), Z1, Z2 (count 0: the gap alone, 3 cycles each), B (1 element)
            Tables tb; tb.stream(0, 2); tb.stream(1, 0); tb.stream(2, 0); tb.stream(3, 1);
            Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 4, 3), L, {S});
            b.sim.run(80);
            CHECK(taken_at(b.got(), {F, F + 1, F + 11}));           // S[3] = F + 2 + 3 + 3 + 3
            CHECK(status_is(b.mon(), F + 11));
        }
        {   // gap 0: Z1 and Z2 take no cycle. B is the fourth entry read: in at F + 3 (one read a cycle), so B is then
            Tables tb; tb.stream(0, 2); tb.stream(1, 0); tb.stream(2, 0); tb.stream(3, 1);
            Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 4, 0), L, {S});
            b.sim.run(80);
            CHECK(taken_at(b.got(), {F, F + 1, F + 3}));
            CHECK(status_is(b.mon(), F + 3));
        }
        {   // gap 3, the last stream count 0: A's gap is waited (A is not the last stream), then the last passes: no cycle
            Tables tb; tb.stream(0, 2); tb.stream(1, 0);
            Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 2, 3), L, {S});
            b.sim.run(80);
            CHECK(taken_at(b.got(), {F, F + 1}));
            CHECK(status_is(b.mon(), F + 5));                       // its pass, the cycle after the gap's three
        }
        {   // ten count-0 streams, gap 0, then one element: each takes no cycle of the timetable, and one table read
            Tables tb; for (unsigned i = 0; i < 10; i++) tb.stream(i, 0);
            tb.stream(10, 1);
            Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 11, 0), L, {S});
            b.sim.run(80);
            CHECK(taken_at(b.got(), {F + 10}));
            CHECK(status_is(b.mon(), F + 10));
        }
    }
}

// 8.5 and the RTL (address_generator.veryl: "a place freed in a cycle takes a read from the next"; a count-0 stream that
// takes no cycle "keeps its place until it passes"). L = 1: READ_AHEAD is 4 places. Stream A (5 elements), then eight
// count-0 streams P1-P8 with no gap, then B (1 element), the last.
//   reads: A in E (in at F), P1 E + 1 (F + 1), P2 E + 2 (F + 2), P3 in F (F + 3): the four places held;
//   A begins in F: its place is free from F + 1, when P4 is read (in at F + 4); A's elements F .. F + 4;
//   F + 5: the generator looks for its next stream: P1-P4 are in and pass together; their places are free from F + 6;
//   P5 is read in F + 6 (in at F + 9), P6 F + 7, P7 F + 8, P8 F + 9; P5 passes in F + 9, so B is read in F + 10: in at
//   F + 13, when P6, P7 and P8 have passed in F + 10, F + 11 and F + 12: B's element is F + 13's.
TEST(passes_keep_their_places_until_they_pass) {
    Tables tb; tb.stream(0, 5);
    for (unsigned i = 1; i <= 8; i++) tb.stream(i, 0);
    tb.stream(9, 1);
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 10, 0), 1, {S});
    b.sim.run(80);
    const Cycle F = first_stream_cycle(1);
    CHECK(taken_at(b.got(), {F, F + 1, F + 2, F + 3, F + 4, F + 13}));
    CHECK(status_is(b.mon(), F + 13));
}

// 8.2: an empty transfer ends in the cycle after its start takes effect: STATUS.dma_busy is 1 for that cycle alone (the
// tile control unit's own, from the start), and EVENTS.dma_done is set at its end: the generator's `done` is at the tile
// control unit at E + 2, the edge that ends E + 1, and it never says `running`.
TEST(an_empty_transfer_is_done_the_cycle_after_its_start) {
    for (uint32_t kind : {uint32_t(AddrGen::RUN_STREAMS), uint32_t(AddrGen::RUN_GROUPS)}) {
        Tables tb;
        Bench b(tb, dma_config(kind, 3, 0), 1, {S});
        b.sim.run(40);
        CHECK(b.got().empty());
        const Mon& m = b.mon();
        CHECK_EQ(m.st.size(), size_t(1));
        CHECK_EQ(m.st[0].first, E + 2);
        CHECK_EQ(m.st[0].second, Word(AddrGen::ST_DONE));
    }
}

// 8.2, 8.4: a group transfer. The lead-in 4L + 3 after the start store; R rotations over the group's t streams, one
// visit a cycle, a stream sending its element r on rotation r; the end the cycle after the last visit.
TEST(a_group_transfer_lead_in_and_rotations) {
    for (unsigned L = 1; L <= 3; L++) {
        Tables tb;
        tb.group(3, 2, 10, 2, 0);                                   // group 3: R = 2 over streams 10 and 11
        for (uint32_t i : {10u, 11u}) { tb.src[i] = 0x1000 * i; tb.hdr[i] = stream_hdr(2, lw::SIZE_64, 1, lw::M_SRC, 0); }
        Bench b(tb, dma_config(AddrGen::RUN_GROUPS, 3, 1), L, {S});
        b.sim.run(60);
        const Cycle F = first_group_cycle(L);
        CHECK(taken_at(b.got(), {F, F + 1, F + 2, F + 3}));
        const uint32_t want[] = {0xA000, 0xB000, 0xA002, 0xB002};   // rotation 0: element 0 of each; rotation 1: element 1
        for (size_t j = 0; j < 4; j++) CHECK_EQ(mem::a_addr(b.got()[j].a), want[j]);
        CHECK_EQ(mem::a_dst(b.got()[0].a), 5u);                     // dst 0: the tile itself
        CHECK(status_is(b.mon(), F + 3));
        for (const auto& r : b.got()) CHECK((r.a & mem::A_GROUP) != 0);   // a group transfer's reads: a refused one is drift
        CHECK_EQ(b.sim.unit<AddrGen>("ag").drift_events, uint64_t(0));
    }
}

// 8.4: a visit is idle when its rotation is not below its stream's count: it takes its cycle and sends nothing.
TEST(an_idle_visit_takes_its_cycle) {
    for (unsigned L = 1; L <= 3; L++) {
        Tables tb;
        tb.group(0, 3, 10, 2, 0);                                   // R = 3 over stream 10 (3 elements) and 11 (1 element)
        tb.stream(10, 3); tb.stream(11, 1);
        Bench b(tb, dma_config(AddrGen::RUN_GROUPS, 0, 1), L, {S});
        b.sim.run(60);
        const Cycle F = first_group_cycle(L);
        CHECK(taken_at(b.got(), {F, F + 1, F + 2, F + 4}));         // F + 3 and F + 5: stream 11's idle visits
        CHECK(status_is(b.mon(), F + 5));                           // the group ends with its last visit, idle
    }
}

// 8.4: S[g] = S[g - 1] + max(R t, 1) + gap. A group with R = 0 takes one cycle and its gap, and, as the first group,
// begins at the lead-in's 4L + 3 like any other (8.2); the last group's gap is not waited.
TEST(groups_follow_the_timetable_and_an_R_0_group_takes_one_cycle) {
    for (unsigned L = 1; L <= 3; L++) {
        const Cycle F = first_group_cycle(L);
        {   // group 0: R = 1 over 2 streams, gap 3; group 1: R = 1 over 1 stream, gap 9 (not waited)
            Tables tb;
            tb.group(0, 1, 10, 2, 3); tb.group(1, 1, 20, 1, 9);
            tb.stream(10, 1); tb.stream(11, 1); tb.stream(20, 1);
            Bench b(tb, dma_config(AddrGen::RUN_GROUPS, 0, 2), L, {S});
            b.sim.run(80);
            CHECK(taken_at(b.got(), {F, F + 1, F + 5}));            // S[1] = F + 2 + 3
            CHECK(status_is(b.mon(), F + 5));
        }
        {   // group 0: R = 0, gap 2; group 1: one visit
            Tables tb;
            tb.group(0, 0, 10, 2, 2); tb.group(1, 1, 20, 1, 0);
            tb.stream(20, 1);
            Bench b(tb, dma_config(AddrGen::RUN_GROUPS, 0, 2), L, {S});
            b.sim.run(80);
            CHECK(taken_at(b.got(), {F + 3}));                      // S[1] = F + max(0, 1) + 2
            CHECK(status_is(b.mon(), F + 3));
        }
        {   // an R = 0 group alone: one cycle, at the lead-in
            Tables tb;
            tb.group(5, 0, 0, 1, 7);
            Bench b(tb, dma_config(AddrGen::RUN_GROUPS, 5, 1), L, {S});
            b.sim.run(80);
            CHECK(b.got().empty());
            CHECK(status_is(b.mon(), F));
        }
    }
}

// 8.3: a refused read holds its element until taken and shifts every later time; in a stream transfer that is not an
// error. There is no catching up: the gap after the stream is waited in full after the late element.
TEST(a_refused_read_shifts_every_later_time_in_a_stream_transfer) {
    for (unsigned L = 1; L <= 3; L++) {
        const Cycle F = first_stream_cycle(L);
        Tables tb; tb.stream(0, 2); tb.stream(1, 2);
        // stream 0's second element, offered in F + 1, is refused in F + 1, F + 2 and F + 3: taken in F + 4
        Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 2, 2), L, {S}, {F + 1, F + 2, F + 3});
        b.sim.run(80);
        CHECK(taken_at(b.got(), {F, F + 4, F + 7, F + 8}));         // then the gap's two cycles, F + 5 and F + 6
        CHECK(status_is(b.mon(), F + 8));
        for (const auto& r : b.got()) CHECK((r.a & mem::A_GROUP) == 0);   // a stream transfer's refused read is no drift
        CHECK_EQ(b.sim.unit<AddrGen>("ag").drift_events, uint64_t(0));
        CHECK_EQ(b.sim.unit<AddrGen>("ag").stall_cycles, uint64_t(3));
    }
}

// 8.4: in a group transfer a refused read holds its visit until taken, shifts every later time, idle visits and later
// rotations included, and sets ERRORS.drift, once for each refused cycle.
TEST(a_refused_visit_shifts_the_rotations_and_is_drift) {
    for (unsigned L = 1; L <= 3; L++) {
        const Cycle F = first_group_cycle(L);
        Tables tb;
        tb.group(0, 2, 10, 2, 2); tb.group(1, 1, 20, 1, 0);         // group 0: R = 2 over stream 10 (2 elements) and 11 (none)
        tb.stream(10, 2); tb.stream(11, 0); tb.stream(20, 1);
        // without a refusal: F (10/0), F + 1 idle, F + 2 (10/1), F + 3 idle, gap F + 4, F + 5, group 1 at F + 6.
        // The first visit is refused in F and F + 1: everything is two cycles later.
        Bench b(tb, dma_config(AddrGen::RUN_GROUPS, 0, 2), L, {S}, {F, F + 1});
        b.sim.run(80);
        CHECK(taken_at(b.got(), {F + 2, F + 4, F + 8}));
        CHECK(status_is(b.mon(), F + 8));
        for (const auto& r : b.got()) CHECK((r.a & mem::A_GROUP) != 0);   // each refused cycle of such a read is drift
        CHECK_EQ(b.sim.unit<AddrGen>("ag").drift_events, uint64_t(2));
    }
}

// 8.2 and the RTL (the DMA unit's README: "A store to DMA_CONFIG during a transfer changes nothing of it"): the transfer
// takes DMA_CONFIG at its start.
TEST(dma_config_is_taken_at_the_start) {
    Tables tb; for (unsigned i = 0; i < 8; i++) tb.stream(i, 1);
    tb.group(0, 1, 0, 1, 0);
    // a stream transfer of streams 0-3, gap 1; from E + 2, DMA_CONFIG names a group transfer of 9 groups with gap 0
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 4, 1), 1, {S}, {}, 0, 5, dma_config(AddrGen::RUN_GROUPS, 0, 9, 0), E + 2);
    b.sim.run(80);
    const Cycle F = first_stream_cycle(1);
    CHECK(taken_at(b.got(), {F, F + 2, F + 4, F + 6}));
    CHECK(status_is(b.mon(), F + 6));
}

// A second transfer after the first has ended starts like the first.
TEST(a_second_transfer_starts_like_the_first) {
    Tables tb; tb.stream(0, 2);
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 1), 1, {S, S + 30});
    b.sim.run(80);
    const Cycle F = first_stream_cycle(1);
    CHECK(taken_at(b.got(), {F, F + 1, F + 30, F + 31}));
    CHECK_EQ(b.sim.unit<AddrGen>("ag").runs, uint64_t(2));
}

// 8.2: a start while a transfer is in progress is UB: meshsim stops.
DEATH_TEST(a_start_while_a_transfer_is_in_progress_is_ub, "a start while a transfer is in progress") {
    Tables tb; tb.stream(0, 50);
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 1), 1, {3, 8});
    b.sim.run(20);
}
DEATH_TEST(a_transfer_past_the_tables_end_is_ub, "a stream transfer past the table's end") {
    Tables tb;
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 250, 10), 1, {3});
    b.sim.run(10);
}
// 13: record_base and PTR[me] are aligned to a row whatever k is.
DEATH_TEST(gather_buffers_misaligned_is_ub, "gather buffers not 128-bit") {
    Tables tb;
    tb.hdr[0] = stream_hdr(1, lw::SIZE_64, 0, lw::M_GATHER, 0);
    tb.ptr[5] = 0x102;                            // PTR[me] (me = 5) not aligned to a row
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 1), 1, {3});
    b.sim.run(20);
}

// The entries' checks (meshsim's: the element is offered as written).
TEST(the_entries_checks_count_violations) {
    Tables tb;
    tb.src[0] = 0x1; tb.hdr[0] = stream_hdr(1, lw::SIZE_64, 0, lw::M_SRC, 0);        // source not aligned to 64 bits
    tb.src[1] = 0; tb.hdr[1] = stream_hdr(1, lw::SIZE_32, 0, lw::M_COUNT, 0);        // COUNT on counter 0
    tb.src[2] = 0; tb.hdr[2] = stream_hdr(1, lw::SIZE_32, 0, lw::M_SRC, 126);        // broadcast from a compute tile
    Bench b(tb, dma_config(AddrGen::RUN_STREAMS, 0, 3), 1, {3});
    b.sim.run(40);
    auto& a = b.sim.unit<AddrGen>("ag");
    CHECK_EQ(a.align_violations, 1u); CHECK_EQ(a.mode_violations, 1u); CHECK_EQ(a.dest_violations, 1u);
    CHECK_EQ(b.got().size(), size_t(3));
}
