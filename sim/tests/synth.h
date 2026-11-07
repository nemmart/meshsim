// synth.h: a seeded synthetic machine with every port kind, for the thread tests (WORKER4).
//
// `Node`s (level 0) each have one input and one output of every registered kind (Link, Fifo, credit Fifo, Hold, Reg),
// a second Reg input (fan-out), an event line, two Count outputs and a Count input; outputs are paired to inputs at random over the whole machine,
// so any partition makes crossings of every kind. Wires, RegFiles and Count `now` reads stay inside a **cluster**: each
// cluster's `Leaf` (level 1) reads its nodes' Wire and RegFile and its cluster's Count. A partition assigns whole
// clusters to domains (those kinds can't cross); `Synth::cluster_of` says which.
//
// Behaviour is a hash of everything seen, so any difference in what arrives, or when, changes the digest. A node acts
// (puts, increments, re-schedules) only at cycles it chose itself, and decides takes from the cycle number, so a step it
// didn't ask for changes nothing (step-all safe, sim.h). Links and Fifos are taken whenever ready (a Link must be: its
// capacity is L + 2); a credit Fifo and a Hold only at some cycles, so credits run out and Holds refuse; Fifos overflow
// when a burst outruns a slow consumer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"

namespace synth {

using namespace sim;

inline uint64_t mix(uint64_t a, uint64_t b) { return hash_mix(a, b); }

// splitmix64 (as the tests' Rng), so the bench can use this header without the test harness.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    uint64_t below(uint64_t n) { return next() % n; }
};

struct Node {
    LinkIn lin; LinkOut lout;
    LinkIn ein; LinkOut eout;    // an event line (width 0), as GO and READY
    FifoIn fin; FifoOut fout;
    FifoIn cin; CreditOut cout;
    HoldIn hin; HoldOut hout;
    RegIn rin0, rin1; RegOut rout;
    CountIn kin; CountOut kglob, kloc;
    WireOut wout; RegFileOut rfout;
    uint64_t h, seed;
    Cycle next_act;
    uint32_t gap;                // the mean distance between acts: busy (1) to quiet
    uint32_t events = 0;
    uint32_t refusals = 0, no_credit = 0;   // seen by the tests: the synthetic chip exercises both
    Word held[3] = {};           // the Hold put to retry after a refusal

    uint64_t digest() const {
        return hash_fields(lin, lout, ein, eout, fin, fout, cin, cout, hin, hout, rin0, rin1, rout, kin, kglob, kloc, wout,
                           rfout, h, seed, next_act, gap, events, refusals, no_credit, held);
    }
    Node(Ports& p, uint64_t s, uint32_t g, unsigned fifo_depth) : h(s), seed(s), next_act(1), gap(g) {
        p.in(lin, "lin", 3); p.out(lout, "lout", 3);
        p.in(ein, "ein", 0); p.out(eout, "eout", 0);
        p.in(fin, "fin", 2, fifo_depth); p.out(fout, "fout", 2);
        p.in_credit(cin, "cin", 1, 3); p.out(cout, "cout", 1);
        p.in(hin, "hin", 3); p.out(hout, "hout", 3);
        p.in(rin0, "rin0", 2); p.in(rin1, "rin1", 2); p.out(rout, "rout", 2);
        p.in(kin, "kin"); p.out(kglob, "kglob"); p.out(kloc, "kloc");
        p.out(wout, "wout"); p.out(rfout, "rfout", 4);
    }
    uint64_t roll(Cycle t, unsigned salt) const {           // splitmix64's finaliser of (seed, salt, t)
        uint64_t z = seed + 0x9e3779b97f4a7c15ull * (t * 64 + salt);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    Wake step(Ctx& c) {
        const Cycle t = c.now();
        // inputs: Links whenever ready (they must be), the rest at some cycles only (by the cycle, never by the step)
        if (c.ready(lin)) { const Word* w = c.peek(lin); h = mix(mix(mix(h, w[0]), w[1]), w[2] ^ t); c.take(lin); }
        if (c.ready(ein)) { events++; h = mix(h, t * 7); c.take(ein); }
        if (c.ready(fin) && roll(t, 1) % 3 != 0) { const Word* w = c.peek(fin); h = mix(mix(h, w[0]), w[1] + t); c.take(fin); }
        if (c.ready(cin) && roll(t, 2) % 2 == 0) { h = mix(h, c.peek(cin)[0] * 3 + t); c.take(cin); }
        if (c.ready(hin) && roll(t, 3) % 3 == 0) { const Word* w = c.peek(hin); h = mix(mix(mix(h, w[0]), w[1]), w[2] ^ t); c.take(hin); }
        if (c.mask() >> rin0.bit & 1) { const Word* w = c.read(rin0); h = mix(h, w[0] ^ w[1] ^ (t << 1)); }
        if (c.mask() >> rin1.bit & 1) { const Word* w = c.read(rin1); h = mix(h, w[1] + t); }
        // a refused Hold put is put again at once, with the same words (WordHold's protocol)
        const bool refused = c.refused(hout);
        if (refused) { refusals++; c.put(hout, held[0], held[1], held[2]); }
        if (t == next_act) {
            h = mix(h, c.through_last(kin));
            if (!refused && (c.can_put(hout) || roll(t, 11) % 4 == 0) && roll(t, 4) % 2 == 0) {   // sometimes blind: refused
                held[0] = h; held[1] = t; held[2] = seed;
                c.put(hout, held[0], held[1], held[2]);
            }
            if (roll(t, 5) % 2 == 0) c.put(lout, h, t, seed);
            if (roll(t, 9) % 4 == 0) c.put(eout);
            if (gap <= 2 || roll(t, 6) % 2 == 0) c.put(fout, h, t);
            if (!c.credits(cout)) no_credit++;
            else if (roll(t, 7) % 4 != 0) c.put(cout, h ^ t);
            if (roll(t, 8) % 3 == 0) c.put(rout, h & 0xff, t & 1);
            c.inc(kglob, 1 + (h & 3));
            c.inc(kloc, 1 + (h >> 5 & 1));
            c.drive(wout, h & 0xf);
            c.write(rfout, unsigned(h & 3), uint32_t(h >> 8));
            next_act = t + 1 + roll(t, 10) % gap;
        }
        const bool more = c.ready(lin) || c.ready(ein) || c.ready(fin) || c.ready(cin) || c.ready(hin);
        return Wake::at_cycle(more ? t + 1 : next_act);
    }
};

// Reads its cluster's Wires, RegFiles and Count in the same cycle (level 1).
struct Leaf {
    static constexpr unsigned kMax = 8;
    WireIn win[kMax]; RegFileIn rfin[kMax]; CountIn kin;
    uint64_t h = 0;
    uint32_t n;
    uint32_t pad = 0;
    uint64_t digest() const { return hash_fields(win, rfin, kin, h, n); }
    Leaf(Ports& p, unsigned k) : n(k) {
        for (unsigned i = 0; i < k; i++) {
            p.in(win[i], ("win" + std::to_string(i)).c_str());
            p.in(rfin[i], ("rfin" + std::to_string(i)).c_str());
        }
        p.in(kin, "kin");
    }
    Wake step(Ctx& c) {
        const uint64_t m = c.mask();
        for (unsigned i = 0; i < n; i++) {
            if (m >> win[i].bit & 1) h = mix(h, c.read(win[i]) + c.now());
            if (m >> rfin[i].bit & 1) h = mix(h, c.read(rfin[i], unsigned(c.now() & 3)));
        }
        if (m) h = mix(h, c.now(kin));
        return Wake::none();
    }
};

struct Params {
    uint64_t seed = 1;
    unsigned clusters = 4, nodes_per_cluster = 4;   // nodes_per_cluster <= Leaf::kMax
    uint32_t gap = 2;                                // 1: every node acts every cycle
    unsigned max_latency = 9;                        // Links: 1 .. max_latency
    unsigned min_latency = 1;
    unsigned fifo_depth = 3;
    bool local_counts = false;                       // true: each node's global count is its own (no Count crosses)
};

// What a build made: names and the cluster of each unit, for partitions and observers.
struct Synth {
    std::vector<std::string> nodes, leaves;
    std::vector<unsigned> node_cluster;
};

// Adds the machine to `cfg`; domain_of(cluster) picks each cluster's domain (all 0 for a single domain). Unit names
// carry `prefix` (several machines in one Config: independent chips as domains).
template <class DomainOf>
Synth build(Config& cfg, const Params& P, DomainOf domain_of, const std::string& prefix = "") {
    Rng r(P.seed);
    Synth s;
    const unsigned n = P.clusters * P.nodes_per_cluster;
    std::vector<Node*> nd(n);
    std::vector<Leaf*> lf(P.clusters);
    for (unsigned k = 0; k < P.clusters; k++) {
        cfg.domain(domain_of(k));
        for (unsigned j = 0; j < P.nodes_per_cluster; j++) {
            const unsigned i = k * P.nodes_per_cluster + j;
            std::string nm = prefix + "n" + std::to_string(i);
            nd[i] = &cfg.add<Node>(nm, r.next() | 1, uint32_t(1 + r.below(P.gap)), P.fifo_depth);
            s.nodes.push_back(nm);
            s.node_cluster.push_back(k);
        }
        std::string nm = prefix + "leaf" + std::to_string(k);
        lf[k] = &cfg.add<Leaf>(nm, P.nodes_per_cluster);
        s.leaves.push_back(nm);
    }
    auto perm = [&] {
        std::vector<unsigned> p(n);
        for (unsigned i = 0; i < n; i++) p[i] = i;
        for (unsigned i = n; i > 1; i--) std::swap(p[i - 1], p[r.below(i)]);
        return p;
    };
    auto pl = perm(), pe = perm(), pf = perm(), pc = perm(), ph = perm(), pr = perm();
    auto lat = [&] { return unsigned(P.min_latency + r.below(P.max_latency - P.min_latency + 1)); };
    for (unsigned i = 0; i < n; i++) {
        cfg.link(nd[i]->lout, nd[pl[i]]->lin, lat());
        cfg.link(nd[i]->eout, nd[pe[i]]->ein, lat());
        cfg.connect(nd[i]->fout, nd[pf[i]]->fin);
        cfg.connect(nd[i]->cout, nd[pc[i]]->cin);
        cfg.connect(nd[i]->hout, nd[ph[i]]->hin);
        cfg.connect(nd[i]->rout, nd[pr[i]]->rin0);
        cfg.connect(nd[r.below(n)]->rout, nd[i]->rin1);
    }
    for (unsigned k = 0; k < P.clusters; k++)
        for (unsigned j = 0; j < P.nodes_per_cluster; j++) {
            const unsigned i = k * P.nodes_per_cluster + j;
            cfg.connect(nd[i]->wout, lf[k]->win[j]);
            cfg.connect(nd[i]->rfout, lf[k]->rfin[j]);
            cfg.connect(nd[i]->kloc, lf[k]->kin);                        // the cluster's count: its leaf reads `now`
            cfg.connect(nd[i]->kglob, nd[P.local_counts ? i : unsigned(r.below(n))]->kin);   // nodes read `through_last`
        }
    return s;
}

inline Synth build(Config& cfg, const Params& P, const std::string& prefix = "") {
    return build(cfg, P, [](unsigned) { return 0u; }, prefix);
}

}  // namespace synth
