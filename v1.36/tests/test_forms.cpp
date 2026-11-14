// The result-only form (D7; sim.h DISPATCH): a toy unit's two forms agree on their results. The cycle form models a
// 4-stage multiplier pipeline (a pair in per cycle, its product out 4 cycles later); the result form computes each product
// at once and puts it the next cycle (no timing kept). A sink hashes what it receives in order: the same results, in the
// same order, whatever the timing. Both forms also run under the debug orders (their per-type step-one functions).
#include "config.h"
#include "harness.h"

using namespace sim;

namespace {
constexpr uint64_t kPairs = 500;

struct Pairs {                       // operand pairs a, b on a Fifo, one a cycle
    FifoOut out;
    uint64_t n = 0;
    explicit Pairs(Ports& p) { p.out(out, "out", 2); }
    uint64_t digest() const { return hash_fields(out, n); }
    Wake step(Ctx& c) {
        if (n < kPairs) { n++; c.put(out, n * 0x9e3779b97f4a7c15ull, n + 12345); }
        return n < kPairs ? c.next() : Wake::none();
    }
};

struct Mul {
    FifoIn in; FifoOut out;
    // the cycle form's pipeline: 4 stages, each a product (or empty)
    Word stage[4] = {}; bool full[4] = {};
    explicit Mul(Ports& p) { p.in(in, "in", 2, 8); p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(in, out, stage, full); }
    static Word mul(const Word* w) { return Word((unsigned __int128)w[0] * w[1] >> 17); }   // the toy's arithmetic
    // Cycle-accurate: shift the pipeline; the last stage's product goes out; a new pair enters the first stage.
    Wake step(Ctx& c) {
        if (full[3]) c.put(out, stage[3]);
        for (int k = 3; k > 0; k--) { stage[k] = stage[k - 1]; full[k] = full[k - 1]; }
        full[0] = false;
        if (c.ready(in)) { stage[0] = mul(c.peek(in)); full[0] = true; c.take(in); }
        return (full[0] || full[1] || full[2] || full[3] || c.ready(in)) ? c.next() : Wake::none();
    }
    // Result-only: the product at once, out on the same step; no pipeline, no timing.
    Wake step_result(Ctx& c) {
        if (c.ready(in)) { c.put(out, mul(c.peek(in))); c.take(in); }
        return c.ready(in) ? c.next() : Wake::none();
    }
};

struct Sink {
    FifoIn in;
    uint64_t got = 0, h = 0;
    explicit Sink(Ports& p) { p.in(in, "in", 1, 8); }
    uint64_t digest() const { return hash_fields(in, got, h); }
    Wake step(Ctx& c) {
        if (c.ready(in)) { h = hash_mix(h, c.peek(in)[0]); got++; c.take(in); }
        return c.ready(in) ? c.next() : Wake::none();
    }
};

struct Result { uint64_t got, h; Cycle last; };
Result run(bool result_form, Options o = {}) {
    Config cfg(o);
    auto& p = cfg.add<Pairs>("pairs");
    auto& m = result_form ? cfg.add_result<Mul>("mul") : cfg.add<Mul>("mul");
    auto& k = cfg.add<Sink>("sink");
    cfg.connect(p.out, m.in);
    cfg.connect(m.out, k.in);
    Sim s = cfg.build();
    s.run(kPairs + 50);
    const Sink& sk = s.unit<Sink>("sink");
    return {sk.got, sk.h, s.now()};
}

struct NoResult { explicit NoResult(Ports&) {} Wake step(Ctx&) { return Wake::none(); } };
static_assert(!HasResultForm<NoResult>, "a type without step_result has no result form (add_result refuses it at compile time)");
static_assert(HasResultForm<Mul>);
}  // namespace

TEST(a_toy_units_two_forms_agree_on_their_results) {
    Result cyc = run(false), res = run(true);
    CHECK_EQ(cyc.got, kPairs);
    CHECK_EQ(res.got, kPairs);
    CHECK_EQ(cyc.h, res.h);                             // the same products, in the same order
}

TEST(both_forms_under_the_debug_orders) {
    Options inst, seed, all;
    inst.order = Options::Order::instance_major;
    seed.shuffle_seed = 5;
    all.step_all = true;
    const Result cyc = run(false), res = run(true);
    for (const Options& o : {inst, seed, all}) {
        CHECK_EQ(run(false, o).h, cyc.h);
        CHECK_EQ(run(true, o).h, res.h);
    }
}
