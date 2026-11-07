// By-reference payloads (WORKER3): a 16-word size class (a 1024-bit cache line) over a Fifo and a Link.
#include <string>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"

using namespace sim;

namespace {
constexpr unsigned kLine = 16;   // words: a 1024-bit cache line

// Fills a fresh slot with the cycle and its word number, puts the index; `extra` allocates one more each cycle and keeps it
// (more than it declared: the class overflows).
struct LineSource {
    FifoOut out;
    uint64_t n, sent = 0;
    bool extra;
    LineSource(Ports& p, uint64_t count, bool keep_extra) : n(count), extra(keep_extra) { p.out(out, "out", 1); p.by_ref(out, kLine); }
    uint64_t digest() const { return hash_fields(out, n, sent) ^ extra; }
    Wake step(Ctx& c) {
        if (sent < n) {
            uint32_t i = c.alloc(out);
            Word* s = c.slot(out, i);
            for (unsigned k = 0; k < kLine; k++) s[k] = c.now() * 100 + k;
            c.put(out, i);
            sent++;
            if (extra) c.alloc(out);
        }
        return sent < n ? c.next() : Wake::none();
    }
};
struct LineSink {
    FifoIn in;
    uint64_t got = 0, bad = 0;
    bool takes;
    LineSink(Ports& p, unsigned depth, bool take) : takes(take) { p.in(in, "in", 1, depth); p.by_ref(in, kLine); }
    uint64_t digest() const { return hash_fields(in, got, bad) ^ takes; }
    Wake step(Ctx& c) {
        if (takes && c.ready(in)) {
            uint32_t i = uint32_t(c.peek(in)[0]);
            const Word* s = c.slot(in, i);
            for (unsigned k = 1; k < kLine; k++) if (s[k] != s[0] + k) bad++;
            c.take(in);
            c.free(in, i);
            got++;
        }
        return takes && c.ready(in) ? c.next() : Wake::none();
    }
};
// Over a Link of latency 3; the consumer keeps the last two lines (declared: held 2) before freeing them.
struct LinkLineSource {
    LinkOut out;
    uint64_t n, sent = 0;
    LinkLineSource(Ports& p, uint64_t count) : n(count) { p.out(out, "out", 1); p.by_ref(out, kLine); }
    uint64_t digest() const { return hash_fields(out, n, sent); }
    Wake step(Ctx& c) {
        if (sent < n) { uint32_t i = c.alloc(out); c.slot(out, i)[0] = ++sent; c.put(out, i); }
        return sent < n ? c.next() : Wake::none();
    }
};
struct LinkLineSink {
    LinkIn in;
    uint32_t kept[2] = { ~0u, ~0u };
    uint64_t sum = 0;
    explicit LinkLineSink(Ports& p) { p.in(in, "in", 1); p.by_ref(in, kLine, 2); }
    uint64_t digest() const { return hash_fields(in, kept, sum); }
    Wake step(Ctx& c) {
        if (c.ready(in)) {
            uint32_t i = uint32_t(c.peek(in)[0]);
            c.take(in);
            sum += c.slot(in, i)[0];
            if (kept[1] != ~0u) c.free(in, kept[1]);
            kept[1] = kept[0]; kept[0] = i;
        }
        return Wake::none();
    }
};
uint64_t words_of(const Sizes& z, const std::string& what, uint32_t* count = nullptr) {
    for (auto& l : z.by_structure) if (l.what == what) { if (count) *count = l.count; return l.words; }
    return 0;
}
}  // namespace

TEST(a_wide_class_sizes_allocates_and_frees_over_a_fifo) {
    Config cfg;
    auto& s = cfg.add<LineSource>("src", uint64_t(1000), false);
    auto& d = cfg.add<LineSink>("dst", 4u, true);
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    uint32_t slots = 0;
    CHECK_EQ(words_of(sim.sizes(), "slot pool w16", &slots), SlotPool::words_needed(16, 5));
    CHECK_EQ(slots, 5u);                                  // depth 4 + one in flight
    begin_run();
    sim.run(1100);
    end_run();
    CHECK_EQ(sim.unit<LineSink>("dst").got, 1000u);
    CHECK_EQ(sim.unit<LineSink>("dst").bad, 0u);
}

TEST(a_fifo_overflow_frees_the_dropped_slot) {
    // the consumer never takes: after 4 puts every one overflows; without the free the class (5 slots) would be exhausted
    Config cfg;
    auto& s = cfg.add<LineSource>("src", uint64_t(200), false);
    auto& d = cfg.add<LineSink>("dst", 4u, false);
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    sim.run(300);
    CHECK_EQ(sim.unit<LineSource>("src").sent, 200u);
}

TEST(a_wide_class_over_a_link_with_held_slots) {
    Config cfg;
    auto& s = cfg.add<LinkLineSource>("src", uint64_t(500));
    auto& d = cfg.add<LinkLineSink>("dst");
    cfg.link(s.out, d.in, 3);
    Sim sim = cfg.build();
    uint32_t slots = 0;
    words_of(sim.sizes(), "slot pool w16", &slots);
    CHECK_EQ(slots, 7u);                                  // L + 2, and the two the consumer declared it holds
    sim.run(600);
    CHECK_EQ(sim.unit<LinkLineSink>("dst").sum, 500u * 501u / 2);
}

DEATH_TEST(allocating_beyond_the_declared_size_is_fatal, "slot pool 'w16' full: capacity 5 \\(slots of 16 words\\)") {
    Config cfg;
    auto& s = cfg.add<LineSource>("src", uint64_t(100), true);
    auto& d = cfg.add<LineSink>("dst", 4u, true);
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
    sim.run(50);
}

DEATH_TEST(both_ends_declare_the_same_class, "by_ref: 'src.out' \\(slots of 16 words\\) and 'dst.in' \\(slots of 0 words\\)") {
    struct Plain { FifoIn in; explicit Plain(Ports& p) { p.in(in, "in", 1, 4); } Wake step(Ctx&) { return Wake::none(); } };
    Config cfg;
    auto& s = cfg.add<LineSource>("src", uint64_t(1), false);
    auto& d = cfg.add<Plain>("dst");
    cfg.connect(s.out, d.in);
    Sim sim = cfg.build();
}
