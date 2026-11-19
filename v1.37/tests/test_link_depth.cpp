// A Link whose consumer may hold up to a declared depth (PORTS.md 2b). Conformance against ref's
// WordDelayLine(name, L, C) is test_portconf (link-L-C fixtures); these test what the scripts can't: peek(k) and size,
// the capacity's death in one domain and across domains, the credit protocol of the mesh across partitions, and no
// allocation in the loop.
#include <string>

#include "base/alloc_guard.h"
#include "config.h"
#include "harness.h"

using namespace sim;

namespace {

// Puts word i (i, ~i, i * 3) on cycles 1 .. n, then stops.
struct Burst {
    LinkOut out;
    uint32_t n, i = 0;
    Burst(Ports& p, uint32_t count) : n(count) { p.out(out, "out", 3); }
    uint64_t digest() const { return hash_fields(n, i); }
    Wake step(Ctx& c) {
        if (i < n) { c.put(out, i, ~Word(i), Word(i) * 3); i++; }
        return i < n ? c.next() : Wake::none();
    }
};

// Sleeps until `look`, then checks every visible word by peek(k) and size, and takes one per cycle.
struct Holder {
    LinkIn in;
    Cycle look;
    uint32_t taken = 0, seen_size = 0, bad = 0;
    Holder(Ports& p, unsigned depth, Cycle at) : look(at) { p.in(in, "in", 3, depth); }
    uint64_t digest() const { return hash_fields(look, taken, seen_size, bad); }
    Wake step(Ctx& c) {
        if (c.now() < look) return Wake::at_cycle(look);
        if (c.now() == look) {
            seen_size = c.size(in);
            for (unsigned k = 0; k < seen_size; k++) {
                const Word* w = c.peek(in, k);
                const Word v = taken + k;
                if (w[0] != v || w[1] != ~v || w[2] != v * 3) bad++;
            }
            if (c.peek(in) != c.peek(in, 0)) bad++;
        }
        if (!c.ready(in)) return Wake::none();
        const Word* w = c.peek(in);
        if (w[0] != taken) bad++;
        c.take(in);
        taken++;
        return c.next();
    }
};

}  // namespace

TEST(a_link_with_depth_holds_its_words_in_order) {
    // L 3, depth 5: capacity 8. Eight puts on cycles 1-8, all visible by 11; the consumer looks at 12.
    Config cfg;
    auto& b = cfg.add<Burst>("b", 8u);
    auto& h = cfg.add<Holder>("h", 5u, Cycle(12));
    cfg.link(b.out, h.in, 3);
    Sim s = cfg.build();
    begin_run();
    s.run(30);
    end_run();
    const Holder& r = s.unit<Holder>("h");
    CHECK_EQ(r.seen_size, 8u);
    CHECK_EQ(r.taken, 8u);
    CHECK_EQ(r.bad, 0u);
}

TEST(the_plain_declaration_is_depth_2) {
    // A Link declared without a depth has capacity L + 2, as before: 5 puts on a latency-3 Link, held, are fine.
    struct Plain {
        LinkIn in; uint32_t n = 0;
        uint64_t digest() const { return hash_fields(n); }
        explicit Plain(Ports& p) { p.in(in, "in", 3); }
        Wake step(Ctx& c) {
            if (c.now() < 20) return Wake::at_cycle(20);
            if (c.now() == 20) n = c.size(in);
            if (!c.ready(in)) return Wake::none();
            c.take(in);
            return c.next();
        }
    };
    Config cfg;
    auto& b = cfg.add<Burst>("b", 5u);
    auto& h = cfg.add<Plain>("h");
    cfg.link(b.out, h.in, 3);
    Sim s = cfg.build();
    s.run(25);
    CHECK_EQ(s.unit<Plain>("h").n, 5u);
}

DEATH_TEST(a_link_with_depth_beyond_its_capacity_is_fatal, "link 'h.in' full at cycle 9: capacity 8") {
    Config cfg;
    auto& b = cfg.add<Burst>("b", 9u);
    auto& h = cfg.add<Holder>("h", 5u, Cycle(40));
    cfg.link(b.out, h.in, 3);
    Sim s = cfg.build();
    s.run(30);
}

DEATH_TEST(a_link_with_depth_beyond_its_capacity_is_fatal_across_domains, "link 'h.in' full at cycle .*: capacity 8") {
    Options o; o.threads = 2;
    Config cfg(o);
    cfg.domain(0);
    auto& b = cfg.add<Burst>("b", 9u);
    cfg.domain(1);
    auto& h = cfg.add<Holder>("h", 5u, Cycle(40));
    cfg.link(b.out, h.in, 3);
    Sim s = cfg.build();
    s.run(30);
}

TEST(a_link_with_depth_holds_across_domains_as_in_one) {
    for (int split = 0; split < 2; split++) {
        Options o; o.threads = split ? 2 : 1;
        Config cfg(o);
        cfg.domain(0);
        auto& b = cfg.add<Burst>("b", 8u);
        cfg.domain(split ? 1 : 0);
        auto& h = cfg.add<Holder>("h", 5u, Cycle(12));
        cfg.link(b.out, h.in, 3);
        Sim s = cfg.build();
        s.run(30);
        const Holder& r = s.unit<Holder>("h");
        CHECK_EQ(r.seen_size, 8u);
        CHECK_EQ(r.taken, 8u);
        CHECK_EQ(r.bad, 0u);
    }
}

namespace {
// The mesh's protocol on a Link that holds (the words held twice, undone): the producer has `depth` credits; each take
// returns one on a width-0 credit line of the same latency. The consumer takes at a seeded random pace, the producer puts
// whenever it has a credit and words left. The words arrive in order, none lost, and the Link never overflows.
struct CreditProducer {
    LinkOut out; LinkIn cred;
    uint32_t credits, sent = 0, n;
    CreditProducer(Ports& p, uint32_t depth, uint32_t count) : credits(depth), n(count) {
        p.out(out, "out", 3); p.in(cred, "cred", 0);
    }
    uint64_t digest() const { return hash_fields(credits, sent, n); }
    Wake step(Ctx& c) {
        if (c.ready(cred)) { c.take(cred); credits++; }
        if (credits && sent < n) { c.put(out, sent, sent ^ 0x55, sent + 7); sent++; credits--; }
        return c.ready(cred) || (credits && sent < n) ? c.next() : Wake::none();
    }
};
struct SlowConsumer {
    LinkIn in; LinkOut cred;
    uint64_t rng, sum = 0;
    uint32_t got = 0, bad = 0, most = 0;
    SlowConsumer(Ports& p, unsigned depth, uint64_t seed) : rng(seed) { p.in(in, "in", 3, depth); p.out(cred, "cred", 0); }
    uint64_t digest() const { return hash_fields(rng, sum, got, bad, most); }
    Wake step(Ctx& c) {
        if (!c.ready(in)) return Wake::none();
        if (c.size(in) > most) most = c.size(in);
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        if ((rng >> 61) < 2) {                              // take one in four cycles, or so: the Link fills
            const Word* w = c.peek(in);
            if (w[0] != got || w[1] != (got ^ 0x55) || w[2] != got + 7) bad++;
            sum = hash_mix(sum, w[0]);
            c.take(in);
            c.put(cred);
            got++;
        }
        return c.next();
    }
};
}  // namespace

TEST(the_credit_protocol_on_a_link_with_depth_across_partitions) {
    uint64_t want = 0;
    for (int split = 0; split < 3; split++) {
        Options o; o.threads = split ? 2 : 1;
        Config cfg(o);
        cfg.domain(0);
        auto& p = cfg.add<CreditProducer>("p", 8u, 2000u);
        cfg.domain(split ? 1 : 0);
        auto& q = cfg.add<SlowConsumer>("q", 8u, uint64_t(11));
        cfg.link(p.out, q.in, 3);
        cfg.link(q.cred, p.cred, 3);
        Sim s = cfg.build();
        s.run(1);                                           // the first run starts the workers (allocates): before the hook
        begin_run();
        s.run(split == 2 ? 2999 : 11999);
        if (split == 2) s.run(9000);                        // in two runs: the barrier's bookkeeping across a run's end
        end_run();
        const SlowConsumer& r = s.unit<SlowConsumer>("q");
        CHECK_EQ(r.got, 2000u);
        CHECK_EQ(r.bad, 0u);
        CHECK(r.most > 2);                                  // it really held words
        if (split == 0) want = s.digest(); else CHECK_EQ(s.digest(), want);
    }
}
