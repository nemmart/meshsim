// partition.cpp: placement by load (Options::Placement::by_load). The configuration regime: any C++.
//
// 1. Granules: units that must share a thread. Joined by a Wire or RegFile (can't cross), a Count (its members), a Fifo,
//    credit Fifo, Hold or Reg (each would make the barrier interval 1), a Link below the interval, and one declared
//    domain (Config::domain, if the author used it). The interval: Options::min_interval, or the largest Link latency
//    that still leaves at least 4 granules per thread; failing that, interval 1 (only Wires and RegFiles joined) if it
//    leaves more.
// 2. Loads: per unit, from the profile (Options::load, by name), else Config::weight, else 1; a granule's is the sum.
//    Edges: between granules, the words a crossing would move per put (a Link of width w: w + 1), summed.
// 3. Assignment: recursive bisection in proportion to the threads on each side (6 = 3 + 3, 5 = 2 + 3), each side grown
//    from a peripheral granule by breadth-first search until its share of the load, then refined by moving boundary
//    granules: first toward balance (within 1% of the side's load where granules allow), then to lower the cut without
//    worsening the balance. Last, a k-way polish: a granule moves to a neighbouring group when that lowers the larger
//    of the two loads.
//    Deterministic: every order is by index.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <queue>
#include <set>

#include "config_impl.h"
#include "internals.h"
#include "threads.h"

namespace sim::detail {

namespace {

struct Graph {
    std::vector<double> load;                                  // per granule
    std::vector<std::vector<std::pair<uint32_t, double>>> adj; // per granule: (neighbour, weight), merged
};

struct UF {
    std::vector<uint32_t> p;
    explicit UF(size_t n) : p(n) { std::iota(p.begin(), p.end(), 0u); }
    uint32_t find(uint32_t x) { while (p[x] != x) x = p[x] = p[p[x]]; return x; }
    void join(uint32_t a, uint32_t b) { a = find(a); b = find(b); if (a != b) p[std::max(a, b)] = std::min(a, b); }
};

// Units joined into granules for a given interval; returns the granule of each unit (dense, by first unit).
std::vector<uint32_t> granules(const Internals::CImpl& G, Cycle interval, bool domains, uint32_t& ng) {
    using Decl = std::decay_t<decltype(G.decls[0])>;
    const uint32_t n = uint32_t(G.units.size());
    UF uf(n);
    std::map<unsigned, uint32_t> first_of_domain;
    if (domains)
        for (uint32_t u = 0; u < n; u++) {
            auto [it, fresh] = first_of_domain.emplace(G.units[u].domain, u);
            if (!fresh) uf.join(it->second, u);
        }
    // Counts: every member of a connected set together (count declarations, and Fifos counting into them), except at
    // interval 1, where a Count may cross (its replicas' deltas meet at every barrier)
    std::vector<uint32_t> cp(G.decls.size());
    std::iota(cp.begin(), cp.end(), 0u);
    auto cfind = [&](uint32_t x) { while (cp[x] != x) x = cp[x] = cp[cp[x]]; return x; };
    for (uint32_t id = 0; id < G.decls.size(); id++)
        if (G.decls[id].kind == Kind::count)
            for (uint32_t p : G.decls[id].peers) if (G.decls[p].kind == Kind::count) cp[cfind(id)] = cfind(p);
    std::map<uint32_t, uint32_t> count_unit;
    auto count_member = [&](uint32_t comp, uint32_t unit) {
        auto [it, fresh] = count_unit.emplace(comp, unit);
        if (!fresh) uf.join(it->second, unit);
    };
    for (uint32_t id = 0; interval > 1 && id < G.decls.size(); id++) {
        const Decl& d = G.decls[id];
        if (d.kind == Kind::count) count_member(cfind(id), d.unit);
        if (d.kind == Kind::fifo && !d.out && d.peers.size() > 1) count_member(cfind(d.peers[1]), d.unit);
    }
    for (const Decl& O : G.decls) {
        if (!O.out || O.kind == Kind::count) continue;
        // interval 1: only what can't cross at all stays together (a Wire, a RegFile); else also every kind that would
        // force interval 1 (Fifo, credit Fifo, Hold, Reg) and the Links below the interval
        const bool keep = interval <= 1 ? (O.kind == Kind::wire || O.kind == Kind::regfile)
                                        : O.kind != Kind::link || O.latency < interval;
        if (!keep) continue;
        for (uint32_t r : O.peers) uf.join(O.unit, G.decls[r].unit);
    }
    std::vector<uint32_t> g(n);
    std::map<uint32_t, uint32_t> dense;
    for (uint32_t u = 0; u < n; u++) {
        auto [it, fresh] = dense.emplace(uf.find(u), uint32_t(dense.size()));
        g[u] = it->second;
    }
    ng = uint32_t(dense.size());
    return g;
}

// Breadth-first order of `part` (granules), from a pseudo-peripheral one: two sweeps, each from the last reached.
std::vector<uint32_t> bfs_order(const Graph& gr, const std::vector<uint32_t>& part, const std::vector<int>& side, int me) {
    auto sweep = [&](uint32_t start) {
        std::vector<uint32_t> order;
        std::set<uint32_t> seen{start};
        std::queue<uint32_t> q;
        q.push(start);
        auto push_all = [&]() {
            while (!q.empty()) {
                const uint32_t x = q.front(); q.pop();
                order.push_back(x);
                for (auto [y, w] : gr.adj[x]) if (side[y] == me && seen.insert(y).second) q.push(y);
            }
        };
        push_all();
        for (uint32_t x : part) if (!seen.count(x)) { seen.insert(x); q.push(x); push_all(); }   // other components after
        return order;
    };
    std::vector<uint32_t> o = sweep(part[0]);
    o = sweep(o.back());
    return sweep(o.back());
}

// Splits `part` (all with side me) into sides a and b, a taking `frac` of the load; refines the cut.
void bisect(const Graph& gr, const std::vector<uint32_t>& part, std::vector<int>& side, int me, int a, int b, double frac) {
    double total = 0, biggest = 0;
    for (uint32_t x : part) { total += gr.load[x]; biggest = std::max(biggest, gr.load[x]); }
    const double target = total * frac;
    (void)biggest;
    const std::vector<uint32_t> order = bfs_order(gr, part, side, me);
    double la = 0;
    for (uint32_t x : order) {
        // grow a while it is below its share, taking the granule if that lands nearer the target
        if (la < target && std::fabs(la + gr.load[x] - target) <= std::fabs(la - target)) { side[x] = a; la += gr.load[x]; }
        else side[x] = b;
    }
    // balance: while one side is over its share by more than 1% of the load, move the granule from it (on the boundary
    // first) that brings the sides nearest their shares, the cut's growth breaking ties; then refinement: move a boundary
    // granule when it lowers the cut and leaves the balance no worse than max(1%, what it is); passes until nothing moves
    const double tight = 0.01 * total;
    auto ext = [&](uint32_t x) {   // the cut's change if x moves
        double in = 0, out = 0;
        for (auto [y, w] : gr.adj[x]) { if (side[y] == side[x]) in += w; else if (side[y] == a || side[y] == b) out += w; }
        return in - out;
    };
    for (int it = 0; it < 4 * int(part.size()) && std::fabs(la - target) > tight; it++) {
        const int from = la > target ? a : b;
        uint32_t best = kNone;
        double best_err = std::fabs(la - target), best_cut = 0;
        for (uint32_t x : part) {
            if (side[x] != from) continue;
            const double nla = from == a ? la - gr.load[x] : la + gr.load[x];
            const double err = std::fabs(nla - target), dc = ext(x);
            if (err < best_err - 1e-9 * total || (best != kNone && err <= best_err + 1e-9 * total && dc < best_cut)) {
                best = x; best_err = err; best_cut = dc;
            }
        }
        if (best == kNone) break;
        la = from == a ? la - gr.load[best] : la + gr.load[best];
        side[best] = from == a ? b : a;
    }
    for (int pass = 0; pass < 32; pass++) {
        bool moved = false;
        for (uint32_t x : part) {
            double in = 0, out = 0;
            for (auto [y, w] : gr.adj[x]) { if (side[y] == side[x]) in += w; else if (side[y] == a || side[y] == b) out += w; }
            if (out == 0) continue;
            const double gain = out - in;
            const double nla = side[x] == a ? la - gr.load[x] : la + gr.load[x];
            const bool ok = std::fabs(nla - target) <= std::max(tight, std::fabs(la - target));
            if (gain > 0 && ok) {
                side[x] = side[x] == a ? b : a;
                la = nla;
                moved = true;
            }
        }
        if (!moved) break;
    }
}

}  // namespace

std::vector<unsigned> place_by_load(const Internals::CImpl& G, unsigned threads, unsigned& groups, Cycle& interval,
                                    std::string& report) {
    const uint32_t n = uint32_t(G.units.size());
    std::set<unsigned> declared;
    for (auto& u : G.units) declared.insert(u.domain);
    const bool domains = declared.size() > 1;
    // the interval: the largest Link latency leaving at least 4 granules per thread (Options::min_interval: at least it)
    std::set<Cycle> lats;
    for (const auto& d : G.decls) if (d.out && d.kind == Kind::link) lats.insert(d.latency);
    uint32_t ng = 0;
    std::vector<uint32_t> g;
    interval = 0;
    if (G.opt.min_interval) { interval = G.opt.min_interval; g = granules(G, interval, domains, ng); }
    else {
        uint32_t best = 0;
        for (auto it = lats.rbegin(); it != lats.rend(); ++it) {
            uint32_t k = 0;
            auto gg = granules(G, *it, domains, k);
            if (k > best) { best = k; g = gg; ng = k; interval = *it; }
            if (k >= 4 * threads) break;
        }
        if (best < 4 * threads) {                       // not enough at any Link latency: interval 1, if that gives more
            uint32_t k = 0;
            auto gg = granules(G, 1, domains, k);
            if (k > best) { g = gg; ng = k; interval = 1; }
        }
    }
    // loads
    std::map<std::string, double> prof;
    if (G.opt.load) for (size_t k = 0; k < G.opt.load->unit.size(); k++) prof[G.opt.load->unit[k]] = G.opt.load->load[k];
    Graph gr;
    gr.load.assign(ng, 0);
    gr.adj.resize(ng);
    for (uint32_t u = 0; u < n; u++) {
        double w = 1;
        if (G.opt.load) { auto it = prof.find(G.units[u].name); w = it == prof.end() ? 0 : it->second; }
        else if (auto it = G.weights.find(G.units[u].name); it != G.weights.end()) w = it->second;
        gr.load[g[u]] += w;
    }
    // edges: with a profile that has the Links' traffic, what a crossing would cost in load units (the words delivered
    // over the calibration, times Options::crossing_ns in the profile's ticks); else the words per put
    std::map<std::string, double> traffic;
    if (G.opt.load) for (size_t k = 0; k < G.opt.load->link.size(); k++) traffic[G.opt.load->link[k]] = G.opt.load->words[k];
    const bool priced = !traffic.empty();
    const double alpha = priced ? G.opt.crossing_ns * G.opt.load->ticks_per_ns : 0;
    std::vector<std::map<uint32_t, double>> e(ng);
    for (const auto& O : G.decls) {
        if (!O.out || O.kind == Kind::count) continue;
        for (uint32_t r : O.peers) {
            const uint32_t x = g[O.unit], y = g[G.decls[r].unit];
            if (x == y) continue;
            double w = O.width + 1;
            if (priced) { auto it = traffic.find(G.decls[r].name); w = alpha * (it == traffic.end() ? 0 : it->second); }
            e[x][y] += w; e[y][x] += w;
        }
    }
    for (uint32_t x = 0; x < ng; x++) for (auto [y, w] : e[x]) gr.adj[x].push_back({y, w});
    // recursive bisection: side[] is the group, assigned threads per group in `want`
    groups = std::min<unsigned>(threads, ng);
    std::vector<int> side(ng, 0);
    struct Job { int id; unsigned k, first; };
    std::vector<Job> jobs{{0, groups, 0}};
    std::vector<unsigned> group_of_side(1, 0);
    int next_id = 1;
    std::map<int, unsigned> final_group;
    while (!jobs.empty()) {
        Job j = jobs.back(); jobs.pop_back();
        std::vector<uint32_t> part;
        for (uint32_t x = 0; x < ng; x++) if (side[x] == j.id) part.push_back(x);
        if (j.k == 1 || part.size() <= 1) { final_group[j.id] = j.first; continue; }
        const unsigned ka = j.k / 2, kb = j.k - ka;
        const int a = next_id++, b = next_id++;
        bisect(gr, part, side, j.id, a, b, double(ka) / double(j.k));
        jobs.push_back({b, kb, j.first + ka});
        jobs.push_back({a, ka, j.first});
    }
    // k-way polish: a granule moves to a neighbouring group when that lowers the larger of the two groups' costs (so the
    // largest never grows), the cut's change breaking ties toward fewer crossings; passes until nothing moves. A group's
    // cost is its load, plus (priced, with a profile's traffic) every crossing edge's price, paid on both sides.
    std::vector<unsigned> grp(ng);
    for (uint32_t x = 0; x < ng; x++) grp[x] = final_group[side[x]];
    {
        std::vector<double> L(groups, 0);
        double total = 0;
        for (uint32_t x = 0; x < ng; x++) { L[grp[x]] += gr.load[x]; total += gr.load[x]; }
        if (priced)
            for (uint32_t x = 0; x < ng; x++) for (auto [y, w] : gr.adj[x]) if (grp[y] != grp[x]) L[grp[x]] += w;
        const double eps = 1e-9 * (total > 0 ? total : 1);
        for (int pass = 0; pass < 64; pass++) {
            bool moved = false;
            for (uint32_t x = 0; x < ng; x++) {
                const unsigned X = grp[x];
                std::map<unsigned, double> link;   // neighbouring group -> weight to it
                double own = 0;
                for (auto [y, w] : gr.adj[x]) { if (grp[y] == X) own += w; else link[grp[y]] += w; }
                double out_all = 0;
                for (auto [Y, w] : link) out_all += w;
                unsigned best = X;
                double best_max = 0, best_cut = 0, bx = 0, by = 0;
                for (auto [Y, w] : link) {
                    // after the move: X loses x's load; its edges to X now cross (both pay), to Y no longer cross (both
                    // stop paying), to a third group Z move from X's bill to Y's
                    double nx = L[X] - gr.load[x], ny = L[Y] + gr.load[x];
                    if (priced) { nx += own - (out_all - w) - w; ny += own - w + (out_all - w); }
                    const double m = std::max(nx, ny), dc = own - w;
                    if (m < std::max(L[X], L[Y]) - eps && (best == X || m < best_max - eps || (m <= best_max + eps && dc < best_cut))) {
                        best = Y; best_max = m; best_cut = dc; bx = nx; by = ny;
                    }
                }
                if (best != X) { L[X] = bx; L[best] = by; grp[x] = best; moved = true; }
            }
            if (!moved) break;
        }
    }
    std::vector<unsigned> out(n);
    for (uint32_t u = 0; u < n; u++) out[u] = grp[g[u]];
    // groups actually used, renumbered densely in order
    std::map<unsigned, unsigned> used;
    for (unsigned v : out) used.emplace(v, 0);
    unsigned k = 0;
    for (auto& [key, v] : used) v = k++;
    for (unsigned& v : out) v = used[v];
    groups = k;
    // the report: granules, interval, loads per group, crossing words
    std::vector<double> gl(groups, 0);
    std::vector<int> gseen(ng, -1);
    for (uint32_t u = 0; u < n; u++) if (gseen[g[u]] < 0) { gseen[g[u]] = int(out[u]); gl[out[u]] += gr.load[g[u]]; }
    double cut = 0, tot = 0, mx = 0;
    std::vector<double> gc(gl);
    for (uint32_t x = 0; x < ng; x++)
        for (auto [y, w] : gr.adj[x]) if (gseen[x] != gseen[y]) { if (x < y) cut += w; gc[gseen[x]] += w; }
    for (double v : gl) { tot += v; mx = std::max(mx, v); }
    double ctot = 0, cmx = 0;
    for (double v : gc) { ctot += v; cmx = std::max(cmx, v); }
    char b[220];
    if (priced)
        std::snprintf(b, sizeof b, "by load: %u granules (interval %llu), %u groups; load max/mean %.3f; crossings priced at "
                      "%.0f ns a word: %.1f%% of the load; cost max/mean %.3f",
                      ng, (unsigned long long)interval, groups, groups && tot > 0 ? mx / (tot / groups) : 1.0, G.opt.crossing_ns,
                      tot > 0 ? 100.0 * cut / tot : 0.0, groups && ctot > 0 ? cmx / (ctot / groups) : 1.0);
    else
        std::snprintf(b, sizeof b, "by load: %u granules (interval %llu), %u groups; load max/mean %.3f; %.0f crossing words per put round",
                      ng, (unsigned long long)interval, groups, groups && tot > 0 ? mx / (tot / groups) : 1.0, cut);
    report = b;
    return out;
}

}  // namespace sim::detail
