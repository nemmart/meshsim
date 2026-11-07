// mesh_fw.h: W6's synthetic busy 8 x 8 mesh (sim/bench/mesh.cpp), rebuilt as units on the framework (WORKER4).
//
// One Router per tile. Mesh links are Links of latency 3 carrying a three-word link word (D17); each router takes a word
// the cycle it arrives into its own input queue (depth 8; the injection queue 16), and returns the queue's credit to the
// sender on a width-0 Link of latency 3 when the word leaves it, as W6's credit wheel. Every cycle a router injects with
// probability `rate` (Bernoulli, its own splitmix stream), routes XY with round-robin per output (one word per output and
// at most one pop per input a cycle), sends with a credit, and ejects into its digest. Not exact to ref; deterministic.
// The edges wrap (a torus of wires) only so every declared port is wired: XY routing never uses them.
#pragma once

#include <cstdint>
#include <string>

#include "config.h"

namespace mesh {

using namespace sim;

constexpr unsigned kSide = 8, kTiles = kSide * kSide, kLatency = 3, kDepth = 8, kInjDepth = 16;
enum { OUT_E = 0, OUT_W = 1, OUT_S = 2, OUT_N = 3, EJECT = 4 };

inline uint64_t splitmix(uint64_t& s) {
    uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
inline uint64_t fold(uint64_t h, uint64_t w) {
    uint64_t z = h ^ (w + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    return z ^ (z >> 31);
}
inline unsigned neighbour(unsigned tile, unsigned o) {        // through output o (wrapping at the edges)
    unsigned x = tile % kSide, y = tile / kSide;
    switch (o) {
        case OUT_E: x = (x + 1) % kSide; break;
        case OUT_W: x = (x + kSide - 1) % kSide; break;
        case OUT_S: y = (y + 1) % kSide; break;
        default: y = (y + kSide - 1) % kSide; break;
    }
    return y * kSide + x;
}
inline unsigned xy(unsigned tile, unsigned dst) {
    const unsigned x = tile % kSide, y = tile / kSide, dx = dst % kSide, dy = dst / kSide;
    return dx > x ? OUT_E : dx < x ? OUT_W : dy > y ? OUT_S : dy < y ? OUT_N : EJECT;
}

struct Router {
    LinkIn in[4]; LinkOut out[4];     // words: in[o] comes from the neighbour's out[o]
    LinkIn cin[4]; LinkOut cout[4];   // credits: cout[i] returns input i's credit to the neighbour that sent through it
    uint64_t rng, dig = 0, thresh;
    uint64_t sends = 0, ejects = 0, injected = 0, refused = 0;
    uint32_t tile;
    uint8_t credits[4], rr[5], head[5], count[5];
    uint8_t pad[1] = {0};
    Word lq[4][kDepth][3];            // the input queues, fed by the links
    Word iq[kInjDepth][3];            // the injection queue

    uint64_t digest() const {
        return hash_fields(in, out, cin, cout, rng, dig, thresh, sends, ejects, injected, refused, tile, credits, rr, head,
                           count) ^ live_words();
    }
    uint64_t live_words() const {
        uint64_t h = 0;
        for (unsigned i = 0; i < 5; i++)
            for (unsigned k = 0; k < count[i]; k++) {
                const Word* w = at(i, (head[i] + k) % cap(i));
                h = fold(fold(fold(h, w[0]), w[1]), w[2]);
            }
        return h;
    }
    static unsigned cap(unsigned i) { return i == 4 ? kInjDepth : kDepth; }

    Router(Ports& p, unsigned t, uint64_t seed, double rate) : tile(t) {
        for (unsigned d = 0; d < 4; d++) {
            const std::string s = std::to_string(d);
            p.in(in[d], ("in" + s).c_str(), 3); p.out(out[d], ("out" + s).c_str(), 3);
            p.in(cin[d], ("cin" + s).c_str(), 0); p.out(cout[d], ("cout" + s).c_str(), 0);
            credits[d] = kDepth;
        }
        for (unsigned i = 0; i < 5; i++) rr[i] = head[i] = count[i] = 0;
        rng = seed * 0x100000001b3ull + t;
        splitmix(rng);
        thresh = rate >= 1.0 ? ~uint64_t(0) : uint64_t(rate * 18446744073709551616.0);
        for (auto& a : lq) for (auto& b : a) b[0] = b[1] = b[2] = 0;
        for (auto& b : iq) b[0] = b[1] = b[2] = 0;
    }

    Word* at(unsigned i, unsigned k) { return i == 4 ? iq[k] : lq[i][k]; }
    const Word* at(unsigned i, unsigned k) const { return i == 4 ? iq[k] : lq[i][k]; }
    Word* push(unsigned i) { Word* w = at(i, (head[i] + count[i]) % cap(i)); count[i]++; return w; }
    const Word* front(unsigned i) const { return at(i, head[i]); }
    void pop(unsigned i) { head[i] = uint8_t((head[i] + 1) % cap(i)); count[i]--; }

    Wake step(Ctx& c) {
        for (unsigned d = 0; d < 4; d++) {
            if (c.ready(in[d])) { const Word* w = c.peek(in[d]); Word* e = push(d); e[0] = w[0]; e[1] = w[1]; e[2] = w[2]; c.take(in[d]); }
            if (c.ready(cin[d])) { credits[d]++; c.take(cin[d]); }
        }
        if (splitmix(rng) < thresh) {                              // inject
            const uint64_t y = splitmix(rng);
            uint32_t dst = uint32_t(((y & 0xffffffffu) * (kTiles - 1)) >> 32);
            if (dst >= tile) ++dst;
            const uint64_t lo = splitmix(rng), hi = splitmix(rng);
            if (count[4] == kInjDepth) refused++;
            else { Word* w = push(4); w[0] = uint64_t(tile) | uint64_t(dst) << 7 | uint64_t(2) << 20; w[1] = lo; w[2] = hi; injected++; }
        }
        unsigned req[5] = {0, 0, 0, 0, 0}, outs = 0;
        for (unsigned i = 0; i < 5; i++)
            if (count[i]) { const unsigned o = xy(tile, unsigned(front(i)[0] >> 7 & 63)); req[o] |= 1u << i; outs |= 1u << o; }
        for (; outs; outs &= outs - 1) {
            const unsigned o = unsigned(__builtin_ctz(outs));
            const unsigned m = req[o];
            if (o < 4 && credits[o] == 0) continue;
            const unsigned hi = m & (~0u << rr[o]);
            const unsigned i = unsigned(__builtin_ctz(hi ? hi : m));
            rr[o] = uint8_t(i == 4 ? 0 : i + 1);
            const Word* w = front(i);
            if (o < 4) { credits[o]--; c.put(out[o], w[0], w[1], w[2]); sends++; }
            else { dig = fold(fold(fold(dig, w[0]), w[1]), w[2]); ejects++; }
            pop(i);
            if (i < 4) c.put(cout[i]);
        }
        return c.next();                                           // injection is every cycle: always busy
    }
};

// One mesh in `cfg`: routers named prefix + "r<tile>", tile k in domain domain_of(k).
template <class DomainOf>
void build(Config& cfg, uint64_t seed, double rate, DomainOf domain_of, const std::string& prefix = "") {
    Router* r[kTiles];
    for (unsigned k = 0; k < kTiles; k++) {
        cfg.domain(domain_of(k));
        r[k] = &cfg.add<Router>(prefix + "r" + std::to_string(k), k, seed, rate);
    }
    for (unsigned k = 0; k < kTiles; k++)
        for (unsigned o = 0; o < 4; o++) {
            const unsigned n = neighbour(k, o);
            cfg.link(r[k]->out[o], r[n]->in[o], kLatency);
            // input o of n came through k's output o: n's credit for it returns to k
            cfg.link(r[n]->cout[o], r[k]->cin[o], kLatency);
        }
}
inline void build(Config& cfg, uint64_t seed, double rate, const std::string& prefix = "") {
    build(cfg, seed, rate, [](unsigned) { return 0u; }, prefix);
}

}  // namespace mesh
