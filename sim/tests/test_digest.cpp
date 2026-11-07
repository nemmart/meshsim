// The digest unit by unit (WORKER4, E5): order-free of the build's numbering, by-reference payloads by their contents.
#include "config.h"
#include "harness.h"
#include "synth.h"

using namespace sim;

namespace {
constexpr unsigned kLine = 8;

// Puts a line every cycle into a Fifo; `skew` allocates a spare slot first and frees it after, so its lines sit at
// other indices than an unskewed source's, with the same contents.
struct Src {
    FifoOut out;
    uint64_t sent = 0;
    uint32_t skew, pad = 0;
    Src(Ports& p, uint32_t s) : skew(s) { p.out(out, "out", 1); p.by_ref(out, kLine, 1); }
    uint64_t digest() const { return hash_fields(sent); }       // the slot index is opaque (R1): not hashed
    Wake step(Ctx& c) {
        uint32_t spare = skew && (c.now() & 1) ? c.alloc(out) : ~0u;
        uint32_t i = c.alloc(out);
        for (unsigned k = 0; k < kLine; k++) c.slot(out, i)[k] = c.now() * 10 + k;
        c.put(out, i);
        if (spare != ~0u) c.free(out, spare);
        sent++;
        return c.next();
    }
};
// Takes every other cycle, so lines queue (and the queue's words are slot indices).
struct Dst {
    FifoIn in;
    uint64_t got = 0;
    Dst(Ports& p) { p.in(in, "in", 1, 4); p.by_ref(in, kLine); }
    uint64_t digest() const { return hash_fields(got); }
    Wake step(Ctx& c) {
        if (c.ready(in) && c.now() % 2 == 0) { c.free(in, uint32_t(c.peek(in)[0])); c.take(in); got++; }
        return c.ready(in) ? c.next() : Wake::none();
    }
};
Sim line_machine(uint32_t skew, bool other_first) {
    Config cfg;
    Dst* d = nullptr;
    if (other_first) d = &cfg.add<Dst>("dst");
    Src& s = cfg.add<Src>("src", skew);
    if (!other_first) d = &cfg.add<Dst>("dst");
    cfg.connect(s.out, d->in);
    return cfg.build();
}

TEST(wide_payloads_are_hashed_by_contents_not_index) {
    Sim a = line_machine(0, false), b = line_machine(1, false);
    for (int k = 0; k < 6; k++) {
        a.run(7); b.run(7);
        CHECK_EQ(a.digest(), b.digest());
    }
}

TEST(the_digest_sees_a_wide_payloads_contents) {
    Sim a = line_machine(0, false), b = line_machine(0, false);
    a.run(9); b.run(9);
    CHECK_EQ(a.digest(), b.digest());
    b.unit<Src>("src").sent++;                    // a unit's state
    CHECK(a.digest() != b.digest());
}

TEST(the_digest_is_stable_and_sensitive_on_the_synthetic_chip) {
    synth::Params p;
    Config c1, c2;
    synth::build(c1, p);
    synth::build(c2, p);
    Sim a = c1.build(), b = c2.build();
    a.run(300); b.run(300);
    CHECK_EQ(a.digest(), b.digest());
    b.run(1);
    CHECK(a.digest() != b.digest());
}
}  // namespace
