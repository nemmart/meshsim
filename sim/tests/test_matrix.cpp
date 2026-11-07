// The full matrix (WORKER4, form 2): each port kind crossing on its own, 16 units of that kind wired at random, split
// 2, 4 and 8 ways over two seeded partitions each, on 1, 2, 4 and 8 threads: the digest at every checkpoint equal to the
// same machine in one domain. Every unit is busy (stepped every cycle) and its state a hash of everything it saw.
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"

using namespace sim;

namespace {

uint64_t roll(uint64_t seed, Cycle t, unsigned salt) {
    uint64_t z = seed + 0x9e3779b97f4a7c15ull * (t * 64 + salt);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
inline uint64_t mix(uint64_t a, uint64_t b) { return hash_mix(a, b); }

struct Base {
    uint64_t h, seed;
    explicit Base(uint64_t s) : h(s), seed(s) {}
    uint64_t r(Ctx& c, unsigned salt) const { return roll(seed, c.now(), salt); }
};

// Link: every arrival taken; a put at half the cycles.
struct KLink : Base {
    LinkIn in; LinkOut out;
    KLink(Ports& p, uint64_t s) : Base(s) { p.in(in, "in", 2); p.out(out, "out", 2); }
    uint64_t digest() const { return hash_fields(h, seed); }
    Wake step(Ctx& c) {
        if (c.ready(in)) { h = mix(mix(h, c.peek(in)[0]), c.peek(in)[1] ^ c.now()); c.take(in); }
        if (r(c, 1) % 2 == 0) c.put(out, h, c.now());
        return c.next();
    }
};
// Fifo, depth 2: a put every cycle, a take at half: overflows.
struct KFifo : Base {
    FifoIn in; FifoOut out;
    KFifo(Ports& p, uint64_t s) : Base(s) { p.in(in, "in", 1, 2); p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(h, seed); }
    Wake step(Ctx& c) {
        if (c.ready(in) && r(c, 2) % 2 == 0) { h = mix(h, c.peek(in)[0] + c.now()); c.take(in); }
        h = mix(h, c.size(in));
        c.put(out, h);
        return c.next();
    }
};
// credit Fifo, depth 3: a put whenever there is a credit, a take at a third: the producer waits on credits.
struct KCredit : Base {
    FifoIn in; CreditOut out;
    KCredit(Ports& p, uint64_t s) : Base(s) { p.in_credit(in, "in", 1, 3); p.out(out, "out", 1); }
    uint64_t digest() const { return hash_fields(h, seed); }
    Wake step(Ctx& c) {
        if (c.ready(in) && r(c, 3) % 3 == 0) { h = mix(h, c.peek(in)[0] ^ c.now()); c.take(in); }
        h = mix(h, c.credits(out));
        if (c.credits(out) && r(c, 4) % 4 != 0) c.put(out, h);
        return c.next();
    }
};
// Hold: a take at a third; puts when it can and sometimes blind; a refusal retried with the same words.
struct KHold : Base {
    HoldIn in; HoldOut out;
    Word held[2] = {0, 0};
    KHold(Ports& p, uint64_t s) : Base(s) { p.in(in, "in", 2); p.out(out, "out", 2); }
    uint64_t digest() const { return hash_fields(h, seed, held); }
    Wake step(Ctx& c) {
        if (c.ready(in) && r(c, 5) % 3 == 0) { h = mix(mix(h, c.peek(in)[0]), c.peek(in)[1] + c.now()); c.take(in); }
        const bool refused = c.refused(out);
        h = mix(h, uint64_t(refused) << 1 | c.can_put(out) | uint64_t(c.held_at_start(out)) << 2);
        if (refused) c.put(out, held[0], held[1]);
        else if ((c.can_put(out) || r(c, 6) % 3 == 0) && r(c, 7) % 2 == 0) { held[0] = h; held[1] = c.now(); c.put(out, held[0], held[1]); }
        return c.next();
    }
};
// Reg: three readers each (fan-out across domains), a put at a third.
struct KReg : Base {
    RegIn in[3]; RegOut out;
    KReg(Ports& p, uint64_t s) : Base(s) {
        for (unsigned k = 0; k < 3; k++) p.in(in[k], ("in" + std::to_string(k)).c_str(), 2);
        p.out(out, "out", 2);
    }
    uint64_t digest() const { return hash_fields(h, seed); }
    Wake step(Ctx& c) {
        for (unsigned k = 0; k < 3; k++)
            if (c.valid(in[k])) h = mix(h, c.read(in[k])[0] ^ c.read(in[k])[1] ^ (uint64_t(k) << 60) ^ ((c.mask() >> in[k].bit & 1) << 59));
        if (r(c, 8) % 3 == 0) c.put(out, h & 0xff, c.now() & 3);
        return c.next();
    }
};
// Count: every unit increments one Count and reads it through_last (a sum across every domain).
struct KCount : Base {
    CountIn in; CountOut out;
    KCount(Ports& p, uint64_t s) : Base(s) { p.in(in, "in"); p.out(out, "out"); }
    uint64_t digest() const { return hash_fields(h, seed); }
    Wake step(Ctx& c) {
        h = mix(h, c.through_last(in));
        c.inc(out, 1 + r(c, 9) % 5);
        return c.next();
    }
};
// Wide payloads (16-word slots) over a Fifo and a Link: copied across, freed at both ends.
constexpr unsigned kLine = 16;
struct KWide : Base {
    FifoIn fin; FifoOut fout; LinkIn lin; LinkOut lout;
    KWide(Ports& p, uint64_t s) : Base(s) {
        p.in(fin, "fin", 1, 3); p.by_ref(fin, kLine); p.out(fout, "fout", 1); p.by_ref(fout, kLine);
        p.in(lin, "lin", 1); p.by_ref(lin, kLine); p.out(lout, "lout", 1); p.by_ref(lout, kLine);
    }
    uint64_t digest() const { return hash_fields(h, seed); }
    void line(Ctx& c, auto port, uint32_t i) { for (unsigned k = 0; k < kLine; k++) c.slot(port, i)[k] = mix(h, k + c.now()); }
    Wake step(Ctx& c) {
        if (c.ready(fin) && r(c, 10) % 2 == 0) {
            const uint32_t i = uint32_t(c.peek(fin)[0]);
            for (unsigned k = 0; k < kLine; k++) h = mix(h, c.slot(fin, i)[k]);
            c.free(fin, i); c.take(fin);
        }
        if (c.ready(lin)) {
            const uint32_t i = uint32_t(c.peek(lin)[0]);
            for (unsigned k = 0; k < kLine; k += 5) h = mix(h, c.slot(lin, i)[k]);
            c.free(lin, i); c.take(lin);
        }
        if (r(c, 11) % 3 != 0) { const uint32_t i = c.alloc(fout); line(c, fout, i); c.put(fout, i); }
        if (r(c, 12) % 2 == 0) { const uint32_t i = c.alloc(lout); line(c, lout, i); c.put(lout, i); }
        return c.next();
    }
};

enum class K { link, fifo, credit, hold, reg, count, wide };
const char* kname(K k) {
    static const char* n[] = {"link", "fifo", "credit", "hold", "reg", "count", "wide"};
    return n[int(k)];
}
constexpr unsigned kUnits = 16;

// The machine: 16 units of one kind, outputs paired to inputs by a seeded permutation; unit i in domain dom[i].
Sim machine(K kind, const std::vector<unsigned>& dom, unsigned threads, unsigned engines = 0) {
    Options o;
    o.threads = threads;
    o.engines = engines;   // W10: 0, one engine per thread; else several engines per thread (W4's layout)
    Config cfg(o);
    test::Rng r(77);
    std::vector<unsigned> perm(kUnits);
    for (unsigned i = 0; i < kUnits; i++) perm[i] = i;
    for (unsigned i = kUnits; i > 1; i--) std::swap(perm[i - 1], perm[r.below(i)]);
    std::vector<void*> u(kUnits);
    for (unsigned i = 0; i < kUnits; i++) {
        cfg.domain(dom[i]);
        const std::string nm = "u" + std::to_string(i);
        const uint64_t s = r.next() | 1;
        switch (kind) {
            case K::link: u[i] = &cfg.add<KLink>(nm, s); break;
            case K::fifo: u[i] = &cfg.add<KFifo>(nm, s); break;
            case K::credit: u[i] = &cfg.add<KCredit>(nm, s); break;
            case K::hold: u[i] = &cfg.add<KHold>(nm, s); break;
            case K::reg: u[i] = &cfg.add<KReg>(nm, s); break;
            case K::count: u[i] = &cfg.add<KCount>(nm, s); break;
            case K::wide: u[i] = &cfg.add<KWide>(nm, s); break;
        }
    }
    for (unsigned i = 0; i < kUnits; i++) {
        const unsigned j = perm[i];
        switch (kind) {
            case K::link: cfg.link(static_cast<KLink*>(u[i])->out, static_cast<KLink*>(u[j])->in, unsigned(1 + r.below(6))); break;
            case K::fifo: cfg.connect(static_cast<KFifo*>(u[i])->out, static_cast<KFifo*>(u[j])->in); break;
            case K::credit: cfg.connect(static_cast<KCredit*>(u[i])->out, static_cast<KCredit*>(u[j])->in); break;
            case K::hold: cfg.connect(static_cast<KHold*>(u[i])->out, static_cast<KHold*>(u[j])->in); break;
            case K::reg:
                for (unsigned k = 0; k < 3; k++)   // reader k of unit (i + 5k + 1): readers spread over domains
                    cfg.connect(static_cast<KReg*>(u[i])->out, static_cast<KReg*>(u[(perm[i] + 5 * k) % kUnits])->in[k]);
                break;
            case K::count:
                cfg.connect(static_cast<KCount*>(u[i])->out, static_cast<KCount*>(u[0])->in);   // one set: every unit's
                if (i) cfg.connect(static_cast<KCount*>(u[0])->out, static_cast<KCount*>(u[i])->in);
                break;
            case K::wide:
                cfg.connect(static_cast<KWide*>(u[i])->fout, static_cast<KWide*>(u[j])->fin);
                cfg.link(static_cast<KWide*>(u[i])->lout, static_cast<KWide*>(u[perm[j]])->lin, unsigned(1 + r.below(4)));
                break;
        }
    }
    return cfg.build();
}

std::vector<uint64_t> digests(K kind, const std::vector<unsigned>& dom, unsigned threads, unsigned engines = 0) {
    Sim s = machine(kind, dom, threads, engines);
    std::vector<uint64_t> d;
    for (Cycle c : {Cycle(1), Cycle(2), Cycle(9), Cycle(60), Cycle(330)}) { s.run(c); d.push_back(s.digest()); }
    return d;
}

void matrix(K kind) {
    const auto want = digests(kind, std::vector<unsigned>(kUnits, 0), 1);
    for (unsigned D : {2u, 4u, 8u})
        for (uint64_t pseed : {3ull, 4ull}) {
            test::Rng r(pseed * 1000 + D);
            std::vector<unsigned> dom(kUnits);
            for (unsigned i = 0; i < kUnits; i++) dom[i] = i < D ? i : unsigned(r.below(D));   // every domain used
            for (unsigned threads : {1u, 2u, 4u, 8u})
                for (unsigned engines : {0u, D}) {      // W10: one engine per thread (domains grouped), and one per domain
                    if (threads > D || (engines && threads == D)) continue;
                    const auto got = digests(kind, dom, threads, engines);
                    for (size_t k = 0; k < want.size(); k++)
                        if (got[k] != want[k]) {
                            test::fail(__FILE__, __LINE__, "%s: %u domains (partition %llu) on %u threads, %u engines: checkpoint %zu differs",
                                       kname(kind), D, (unsigned long long)pseed, threads, engines, k);
                            return;
                        }
                }
        }
}

TEST(matrix_link) { matrix(K::link); }
TEST(matrix_fifo) { matrix(K::fifo); }
TEST(matrix_credit_fifo) { matrix(K::credit); }
TEST(matrix_hold) { matrix(K::hold); }
TEST(matrix_reg) { matrix(K::reg); }
TEST(matrix_count) { matrix(K::count); }
TEST(matrix_wide_payloads) { matrix(K::wide); }

// The partition report names the kind that set the interval.
TEST(the_report_names_each_kind) {
    for (K kind : {K::link, K::fifo, K::credit, K::hold, K::reg, K::count}) {
        std::vector<unsigned> dom(kUnits);
        for (unsigned i = 0; i < kUnits; i++) dom[i] = i % 2;
        const std::string rep = machine(kind, dom, 2).partition();
        const std::string want = kind == K::credit ? "credit fifo" : kname(kind);
        if (rep.find("set by " + want) == std::string::npos) {
            test::fail(__FILE__, __LINE__, "%s: report '%s'", kname(kind), rep.c_str());
            return;
        }
    }
}

}  // namespace
