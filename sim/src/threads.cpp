// threads.cpp: one machine over several threads (WORKER4): see threads.h. The configuration regime for the split and
// the build; the run loop (run_thread, publish, drain, clear) is the cycle loop's: no allocation, no STL calls, no locks.
#include "threads.h"

#include <pthread.h>

#include <algorithm>
#include <cstdio>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include "base/alloc_guard.h"
#include "internals.h"
#include "parallel.h"

namespace sim::detail {

// ---- boundary units: build-time placeholders, never a hop -----------------------------------------------------------
// Each holds one port. A sink is the proxy in the producer's domain (nothing is ever delivered to it: the producer's
// route goes to an outbox); a source feeds the consumer in its domain (it never puts: the drain appends for it).
namespace {
struct XLinkSink {
    LinkIn in;
    XLinkSink(Ports& p, unsigned w, unsigned sw, unsigned held) { p.in(in, "in", w); if (sw) p.by_ref(in, sw, held); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XLinkSource {
    LinkOut out;
    XLinkSource(Ports& p, unsigned w, unsigned sw) { p.out(out, "out", w); if (sw) p.by_ref(out, sw); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XFifoSink {
    FifoIn in;
    XFifoSink(Ports& p, unsigned w, unsigned depth, bool credit, unsigned sw, unsigned held) {
        if (credit) p.in_credit(in, "in", w, depth); else p.in(in, "in", w, depth);
        if (sw) p.by_ref(in, sw, held);
    }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XFifoSource {
    FifoOut out;
    XFifoSource(Ports& p, unsigned w, unsigned sw) { p.out(out, "out", w); if (sw) p.by_ref(out, sw); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XCreditSource {
    CreditOut out;
    XCreditSource(Ports& p, unsigned w, unsigned sw) { p.out(out, "out", w); if (sw) p.by_ref(out, sw); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XHoldSink {
    HoldIn in;
    XHoldSink(Ports& p, unsigned w, unsigned sw, unsigned held) { p.in(in, "in", w); if (sw) p.by_ref(in, sw, held); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XHoldSource {
    HoldOut out;
    XHoldSource(Ports& p, unsigned w, unsigned sw) { p.out(out, "out", w); if (sw) p.by_ref(out, sw); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XRegSink {
    RegIn in;
    XRegSink(Ports& p, unsigned w) { p.in(in, "in", w); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XRegSource {
    RegOut out;
    XRegSource(Ports& p, unsigned w) { p.out(out, "out", w); }
    Wake step(Ctx&) { return Wake::none(); }
};
struct XCountMember {     // a domain's member of a Count read across domains, where only a Fifo counts into it
    CountIn in;
    explicit XCountMember(Ports& p) { p.in(in, "in"); }
    Wake step(Ctx&) { return Wake::none(); }
};

unsigned pow2_at_least(unsigned x) { unsigned w = 1; while (w < x) w <<= 1; return w; }
}  // namespace

// W10: the declared domains are units of placement, not engines. They are grouped, in order, into E engines (one per
// thread unless Options::engines says otherwise; W4's spread, domain d of D in group d * E / D); each group is built as
// one engine, so a connection between two domains of a group is an ordinary local port and only connections between
// groups are crossings. What may not cross is judged on the declared domains, so a configuration's validity doesn't
// depend on the thread count.
namespace {
unsigned declared_domains(const Internals::CImpl& I) {
    std::set<unsigned> ds;
    for (const auto& u : I.units) ds.insert(u.domain);
    return unsigned(ds.size());
}
unsigned engines_of(const Internals::CImpl& I) {
    const unsigned D = declared_domains(I);
    const unsigned want = I.opt.engines ? I.opt.engines : std::max(1u, I.opt.threads);
    return std::min(want, D);
}
}  // namespace

bool split_needed(const Internals::CImpl& I) { return declared_domains(I) > 1 && engines_of(I) > 1; }

void check_domains(const Internals::CImpl& I) {
    for (const auto& O : I.decls) {
        if (!O.out || (O.kind != Kind::wire && O.kind != Kind::regfile)) continue;
        const unsigned p = I.units[O.unit].domain;
        for (uint32_t r : O.peers)
            if (I.units[I.decls[r].unit].domain != p)
                FATAL("a %s can't cross thread domains: '%s' (unit '%s', domain %u) drives '%s' (unit '%s', domain %u); put "
                      "both units in one domain, or make the coupling registered (a Reg or a Link)", kind_name(O.kind),
                      O.name.c_str(), I.units[O.unit].name.c_str(), p, I.decls[r].name.c_str(),
                      I.units[I.decls[r].unit].name.c_str(), I.units[I.decls[r].unit].domain);
    }
}

void destroy_threads(Threads* t) { delete t; }

// ---- the split ------------------------------------------------------------------------------------------------------
Sim::Impl* Internals::split(Config::Impl& G) {
    using Decl = Config::Impl::Decl;
    const uint32_t n = uint32_t(G.units.size());

    // W10: the declared domains checked, then grouped into engines (the rest of the split sees the groups as domains)
    check_domains(G);
    const unsigned declared = declared_domains(G), E = engines_of(G);
    {
        std::map<unsigned, uint32_t> dense;
        for (auto& u : G.units) dense[u.domain] = 0;
        uint32_t k = 0;
        for (auto& [key, v] : dense) v = k++;
        for (auto& u : G.units) u.domain = unsigned(uint64_t(dense[u.domain]) * E / declared);
    }

    // domains, dense in increasing order of their numbers
    std::map<unsigned, uint32_t> dmap;
    for (auto& u : G.units) dmap[u.domain] = 0;
    uint32_t D = 0;
    for (auto& [k, v] : dmap) v = D++;
    std::vector<uint32_t> dom(n);
    for (uint32_t u = 0; u < n; u++) dom[u] = dmap[G.units[u].domain];
    auto ddom = [&](uint32_t id) { return dom[G.decls[id].unit]; };
    for (const Decl& d : G.decls)
        if (d.peers.empty() && d.kind != Kind::count) FATAL("port '%s' (%s %s) is not wired", d.name.c_str(), kind_name(d.kind), d.out ? "output" : "input");

    // Counts: connected sets (count declarations and the Fifos counting into them)
    std::vector<uint32_t> parent(G.decls.size());
    for (uint32_t i = 0; i < parent.size(); i++) parent[i] = i;
    auto find = [&](uint32_t x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
    for (uint32_t id = 0; id < G.decls.size(); id++)
        if (G.decls[id].kind == Kind::count)
            for (uint32_t p : G.decls[id].peers) if (G.decls[p].kind == Kind::count) parent[find(id)] = find(p);

    // crossings
    struct Cross { uint32_t o, i, p, q; };
    struct RegCross { uint32_t o, p; std::map<uint32_t, std::vector<uint32_t>> readers; };
    std::vector<Cross> cross;
    std::vector<RegCross> rcross;
    std::vector<bool> reg_crossing(G.decls.size(), false);
    Cycle interval = kNoCycle;
    std::string why;
    auto lower = [&](Cycle L, const std::string& w) { if (L < interval) { interval = L; why = w; } };
    for (uint32_t id = 0; id < G.decls.size(); id++) {
        const Decl& O = G.decls[id];
        if (!O.out || O.kind == Kind::count) continue;
        const uint32_t p = ddom(id);
        if (O.kind == Kind::wire || O.kind == Kind::regfile) {
            for (uint32_t r : O.peers)
                if (ddom(r) != p)
                    FATAL("a %s can't cross thread domains: '%s' (unit '%s', domain %u) drives '%s' (unit '%s', domain %u); put "
                          "both units in one domain, or make the coupling registered (a Reg or a Link)", kind_name(O.kind),
                          O.name.c_str(), G.units[O.unit].name.c_str(), G.units[O.unit].domain, G.decls[r].name.c_str(),
                          G.units[G.decls[r].unit].name.c_str(), G.units[G.decls[r].unit].domain);
            continue;
        }
        if (O.kind == Kind::reg) {
            bool any = false;
            for (uint32_t r : O.peers) any = any || ddom(r) != p;
            if (!any) continue;
            RegCross rc{id, p, {}};
            for (uint32_t r : O.peers) { rc.readers[ddom(r)].push_back(r); reg_crossing[r] = true; }
            reg_crossing[id] = true;
            rcross.push_back(std::move(rc));
            lower(1, std::string("reg '") + O.name + "'");
            continue;
        }
        const uint32_t i = O.peers[0];
        if (ddom(i) == p) continue;
        cross.push_back({id, i, p, ddom(i)});
        lower(O.kind == Kind::link ? O.latency : 1, std::string(kind_name(O.kind)) + " '" + O.name + "' -> '" + G.decls[i].name + "'" +
                                                        (O.kind == Kind::link ? " (latency " + std::to_string(O.latency) + ")" : ""));
    }
    // a Count read across domains: members (count declarations, and Fifos counting into it) in several domains
    std::map<uint32_t, std::set<uint32_t>> comp_doms;
    for (uint32_t id = 0; id < G.decls.size(); id++) {
        const Decl& d = G.decls[id];
        if (d.kind == Kind::count) comp_doms[find(id)].insert(ddom(id));
        if (d.kind == Kind::fifo && !d.out && d.peers.size() > 1) comp_doms[find(d.peers[1])].insert(ddom(id));
    }
    for (auto& [c, ds] : comp_doms)
        if (ds.size() > 1) lower(1, "count '" + G.decls[c].name + "' (in " + std::to_string(ds.size()) + " domains)");

    // ---- one configuration per domain: the units moved over in add order, their declarations renumbered
    std::vector<std::unique_ptr<Config>> subs;
    for (uint32_t d = 0; d < D; d++) {
        Options o = G.opt;
        o.shuffle_seed = G.opt.shuffle_seed ? hash_mix(G.opt.shuffle_seed, d + 1) | 1 : 0;   // each domain its own order
        o.threads = 1;
        o.cpus.clear();
        subs.push_back(std::make_unique<Config>(o));
    }
    auto S = [&](uint32_t d) -> Config::Impl& { return impl(*subs[d]); };
    std::vector<uint32_t> new_unit(n), new_decl(G.decls.size(), kNone);
    for (uint32_t u = 0; u < n; u++) {
        auto& gu = G.units[u];
        Config::Impl& I = S(dom[u]);
        Config::Impl::Staged st;
        st.type = I.type_id(G.types[gu.type]);
        st.form = gu.form; st.name = gu.name; st.chip = gu.chip; st.domain = 0; st.obj = gu.obj;
        st.nbits = gu.nbits; st.edges = gu.edges; st.level = 0;
        gu.obj = nullptr;                                   // moved: the global configuration no longer owns it
        new_unit[u] = uint32_t(I.units.size());
        I.names[gu.name] = new_unit[u];
        for (uint32_t id : gu.decls) {
            Decl dd = G.decls[id];
            dd.unit = new_unit[u];
            dd.peers.clear();
            dd.final = kNone;
            new_decl[id] = uint32_t(I.decls.size());
            st.decls.push_back(new_decl[id]);
            I.decls.push_back(std::move(dd));
        }
        I.units.push_back(std::move(st));
    }
    for (auto& [nm, v] : G.missed) {
        auto it = G.names.find(nm);
        if (it == G.names.end()) FATAL("missed_wake_after: no unit named '%s'", nm.c_str());
        S(dom[it->second]).missed[nm] = v;
    }
    // peers inside a domain (not Counts, not a Fifo's Count, not a crossing Reg: below)
    for (uint32_t id = 0; id < G.decls.size(); id++) {
        const Decl& g = G.decls[id];
        if (g.kind == Kind::count || reg_crossing[id]) continue;
        Decl& s = S(ddom(id)).decls[new_decl[id]];
        const size_t np = g.kind == Kind::fifo && !g.out ? std::min<size_t>(1, g.peers.size()) : g.peers.size();
        for (size_t k = 0; k < np; k++)
            if (ddom(g.peers[k]) == ddom(id)) s.peers.push_back(new_decl[g.peers[k]]);
    }

    // ---- boundary units for every crossing, wired in their domains
    const unsigned held = interval == kNoCycle ? 0 : unsigned(2 * interval + 2);   // outstanding copies at the source
    struct CrossB { uint32_t sink, src; };                 // the sub-declarations of the proxy (in p) and the feed (in q)
    std::vector<CrossB> cb(cross.size());
    for (size_t k = 0; k < cross.size(); k++) {
        const Cross& c = cross[k];
        const Decl& O = G.decls[c.o];
        const Decl& I = G.decls[c.i];
        const unsigned w = O.width, sw = O.slot_words;
        const std::string nm = "~x" + std::to_string(k) + ":" + O.name;
        Config& P = *subs[c.p];
        Config& Q = *subs[c.q];
        switch (O.kind) {
            case Kind::link:
                cb[k].sink = P.add<XLinkSink>(nm, w, sw, held).in.idx;
                cb[k].src = Q.add<XLinkSource>(nm + ">", w, sw).out.route;
                break;
            case Kind::fifo:
                cb[k].sink = P.add<XFifoSink>(nm, w, 1u, false, sw, held).in.idx;
                cb[k].src = Q.add<XFifoSource>(nm + ">", w, sw).out.route;
                break;
            case Kind::credit:
                cb[k].sink = P.add<XFifoSink>(nm, w, I.depth, true, sw, held).in.idx;
                cb[k].src = Q.add<XCreditSource>(nm + ">", w, sw).out.route;
                break;
            case Kind::hold:
                cb[k].sink = P.add<XHoldSink>(nm, w, sw, held).in.idx;
                cb[k].src = Q.add<XHoldSource>(nm + ">", w, sw).out.route;
                break;
            default: FATAL("crossing of kind %s", kind_name(O.kind));
        }
        S(c.p).wire(new_decl[c.o], cb[k].sink, O.kind, O.latency, false, "split");
        S(c.q).wire(cb[k].src, new_decl[c.i], O.kind, O.latency, false, "split");
    }
    struct RegB { uint32_t sink; std::map<uint32_t, uint32_t> src; };   // per reader domain: its feed
    std::vector<RegB> rb(rcross.size());
    for (size_t k = 0; k < rcross.size(); k++) {
        const RegCross& r = rcross[k];
        const Decl& O = G.decls[r.o];
        const std::string nm = "~r" + std::to_string(k) + ":" + O.name;
        rb[k].sink = subs[r.p]->add<XRegSink>(nm, O.width).in.idx;
        S(r.p).wire(new_decl[r.o], rb[k].sink, Kind::reg, 1, true, "split");
        for (auto& [d, readers] : r.readers) {
            const uint32_t src = subs[d]->add<XRegSource>(nm + ">" + std::to_string(d), O.width).out.route;
            rb[k].src[d] = src;
            for (uint32_t rd : readers) S(d).wire(src, new_decl[rd], Kind::reg, 1, true, "split");
        }
    }
    // ---- Counts: one member list per (set, domain); a domain with only a Fifo counting into the set gets an XCount
    std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> members;   // (set, domain) -> sub-declarations
    for (uint32_t id = 0; id < G.decls.size(); id++)
        if (G.decls[id].kind == Kind::count) members[{find(id), ddom(id)}].push_back(new_decl[id]);
    for (uint32_t id = 0; id < G.decls.size(); id++) {
        const Decl& g = G.decls[id];
        if (g.kind != Kind::fifo || g.out || g.peers.size() < 2) continue;
        const uint32_t d = ddom(id);
        auto& m = members[{find(g.peers[1]), d}];
        if (m.empty()) m.push_back(subs[d]->add<XCountMember>("~k" + std::to_string(id) + ":" + g.name).in.idx);
        S(d).decls[new_decl[id]].peers.push_back(m[0]);            // the Fifo's second peer: its Count (config.cpp)
    }
    for (auto& [key, m] : members)
        for (size_t k = 1; k < m.size(); k++) {
            S(key.second).decls[m[k]].peers.push_back(m[0]);
            S(key.second).decls[m[0]].peers.push_back(m[k]);
        }

    // ---- build each domain (W3's build, unchanged)
    auto* T = new Threads;
    T->interval = interval;
    T->doms.resize(D);
    std::vector<Sim> sims;
    for (uint32_t d = 0; d < D; d++) {
        sims.push_back(subs[d]->build());
        T->doms[d].S = impl(sims[d]);
        T->doms[d].e = &T->doms[d].S->e;
    }
    T->sims = std::move(sims);
    auto fin = [&](uint32_t d, uint32_t sub_decl) { return S(d).decls[sub_decl].final; };
    auto eng = [&](uint32_t d) -> Engine& { return *T->doms[d].e; };
    for (uint32_t d = 0; d < D; d++) T->doms[d].e->bind();

    // ---- outboxes
    std::map<std::tuple<uint32_t, uint32_t, unsigned, unsigned, unsigned>, uint32_t> obmap;
    auto outbox = [&](uint32_t src, uint32_t dst, Kind k, unsigned w, unsigned L) -> Outbox& {
        auto key = std::make_tuple(src, dst, unsigned(k), w, L);
        auto it = obmap.find(key);
        if (it == obmap.end()) {
            Outbox ob{};
            ob.src = src; ob.dst = dst; ob.kind = k; ob.width = uint8_t(w); ob.latency = uint16_t(L); ob.routes = 0;
            it = obmap.emplace(key, uint32_t(T->outboxes.size())).first;
            T->outboxes.push_back(std::move(ob));
        }
        return T->outboxes[it->second];
    };
    auto pool_size = [&](uint32_t d, Kind k) -> size_t {
        const Engine& e = eng(d);
        return k == Kind::link ? e.nlinks : k == Kind::hold ? e.nholds : k == Kind::reg ? e.nregs : e.nfifos;
    };
    struct Retarget { uint32_t dom, route, ob; };
    std::vector<Retarget> retarget;
    std::vector<uint64_t> counts_by_kind(kKinds, 0);
    for (size_t k = 0; k < cross.size(); k++) {
        const Cross& c = cross[k];
        const Decl& O = G.decls[c.o];
        const uint32_t route = fin(c.p, new_decl[c.o]), proxy = fin(c.p, cb[k].sink), port = fin(c.q, new_decl[c.i]);
        Outbox& ob = outbox(c.p, c.q, O.kind, O.width, O.latency);
        ob.routes++;
        if (ob.xl.empty()) ob.xl.resize(pool_size(c.p, O.kind));
        Xl x;
        x.dst = port;
        x.scls = eng(c.p).routes[route].cls;
        x.dcls = O.kind == Kind::link ? eng(c.q).links[port].cls : O.kind == Kind::hold ? eng(c.q).holds[port].cls : eng(c.q).fifos[port].cls;
        ob.xl[proxy] = x;
        retarget.push_back({c.p, route, uint32_t(&ob - T->outboxes.data())});
        counts_by_kind[unsigned(O.kind)]++;
        if (O.kind == Kind::link) T->doms[c.p].link_proxies.push_back(proxy);
        if (O.kind == Kind::credit) {                      // the credit's return: the consumer's take, back to the proxy
            const uint32_t ret = eng(c.q).fifos[port].ret;
            Outbox& rb2 = outbox(c.q, c.p, Kind::cret, 0, 1);
            rb2.routes++;
            if (rb2.xl.empty()) rb2.xl.resize(pool_size(c.q, Kind::fifo));
            rb2.xl[port].dst = proxy;
            retarget.push_back({c.q, ret, uint32_t(&rb2 - T->outboxes.data())});
        }
        if (O.kind == Kind::hold) {
            Outbox& ob2 = T->outboxes[retarget[retarget.size() - 1].ob];
            if (ob2.hold_of.empty()) ob2.hold_of.assign(pool_size(c.p, Kind::hold), kNone);
            XHold h;
            h.p = c.p; h.proxy = proxy; h.route = route; h.q = c.q; h.port = port;
            h.slot = 0;
            ob2.hold_of[proxy] = uint32_t(T->holds.size());
            T->doms[c.p].holds_p.push_back(uint32_t(T->holds.size()));
            T->doms[c.q].holds_q.push_back(uint32_t(T->holds.size()));
            T->holds.push_back(h);
        }
    }
    for (size_t k = 0; k < rcross.size(); k++) {
        const RegCross& r = rcross[k];
        const Decl& O = G.decls[r.o];
        const uint32_t route = fin(r.p, new_decl[r.o]);
        const uint32_t preg = eng(r.p).routes[route].dest;
        Outbox& ob = outbox(r.p, kFan, Kind::reg, O.width, 1);
        ob.routes++;
        if (ob.fan.empty()) ob.fan.resize(D);
        for (auto& [d, src] : rb[k].src) {
            if (ob.fan[d].empty()) ob.fan[d].resize(pool_size(r.p, Kind::reg));
            ob.fan[d][preg].dst = eng(d).routes[fin(d, src)].dest;
        }
        retarget.push_back({r.p, route, uint32_t(&ob - T->outboxes.data())});
        counts_by_kind[unsigned(Kind::reg)]++;
    }
    // W10: direct delivery for every Link outbox carrying values (no by-reference class): its destination delivers from
    // its buckets at their cycle. Its window must outlast an entry's flight and the interval it was put in (W >= L + I),
    // so the source never writes a bucket its destination may still be reading in the same interval.
    for (auto& ob : T->outboxes) {
        if (ob.kind != Kind::link || ob.dst == kFan || interval == kNoCycle) continue;
        bool by_value = true;
        for (const Xl& x : ob.xl) by_value = by_value && x.scls == kNoClass;
        if (!by_value || ob.width > 3) continue;
        ob.direct = true;
        ob.xdst.resize(ob.xl.size());
        for (size_t k = 0; k < ob.xl.size(); k++) ob.xdst[k] = ob.xl[k].dst;
    }
    // storage: per source domain, one block; buckets W >= 2 x interval (direct: >= latency + interval), each as deep as
    // its routes
    const unsigned W = interval == kNoCycle ? 2 : pow2_at_least(unsigned(std::max<Cycle>(2, 2 * interval)));
    auto window = [&](const Outbox& ob) {
        return ob.direct ? std::max(W, pow2_at_least(unsigned(ob.latency + interval))) : W;
    };
    std::vector<size_t> words(D, 0);
    for (auto& ob : T->outboxes) {
        ob.mask = window(ob) - 1;
        words[ob.src] += round_to_lines((sizeof(Bucket) * window(ob) + 7) / 8) + window(ob) * Bucket::words_needed(ob.width + 1u, ob.routes);
    }
    std::vector<uint64_t*> cur(D, nullptr);
    T->blocks.resize(D);
    for (uint32_t d = 0; d < D; d++)
        if (words[d]) { T->blocks[d] = std::make_unique<Block>(words[d], "sim: a domain's outboxes"); cur[d] = T->blocks[d]->words(); }
    uint64_t outbox_words = 0;
    for (auto& ob : T->outboxes) {
        T->port_names.push_back("outbox " + std::string(kind_name(ob.kind)) + " w" + std::to_string(ob.width) + " L" +
                                std::to_string(ob.latency) + " " + std::to_string(ob.src) + "->" +
                                (ob.dst == kFan ? std::string("fan") : std::to_string(ob.dst)));
        uint64_t*& p = cur[ob.src];
        const unsigned OW = window(ob);
        ob.buckets = reinterpret_cast<Bucket*>(p);
        p += round_to_lines((sizeof(Bucket) * OW + 7) / 8);
        const size_t per = Bucket::words_needed(ob.width + 1u, ob.routes);
        for (unsigned b = 0; b < OW; b++) {
            new (&ob.buckets[b]) Bucket();
            ob.buckets[b].init(p, ob.width + 1u, ob.routes, T->port_names.back().c_str());
            p += per;
        }
        outbox_words += round_to_lines((sizeof(Bucket) * OW + 7) / 8) + OW * per;
        const uint32_t id = uint32_t(&ob - T->outboxes.data());
        T->doms[ob.src].out.push_back(id);
        if (ob.dst == kFan) { for (uint32_t d = 0; d < D; d++) if (!ob.fan[d].empty()) T->doms[d].in.push_back(id); }
        else T->doms[ob.dst].in.push_back(id);
    }
    // Routes name their wheel by index (W3's performance pass): each source domain's wheel table is copied and its outboxes
    // appended after its own wheels (index >= nwheels: never delivered by the domain's cycle), and its routes retargeted.
    // The due mask (a bit per wheel) is off in a split machine's domains: an outbox's index may pass 63, and the drain
    // appends into a destination's wheels between cycles.
    T->wheel_tables.resize(D);
    for (uint32_t d = 0; d < D; d++) {
        Engine& e = eng(d);
        auto& tab = T->wheel_tables[d];
        tab.assign(e.wheels, e.wheels + e.nwheels);
        for (uint32_t o : T->doms[d].out) {
            Outbox& ob = T->outboxes[o];
            if (tab.size() >= 0xFFFF) FATAL("split: more than 65535 wheels and outboxes in domain %u", d);
            ob.src_wheel = uint16_t(tab.size());
            tab.push_back(Wheel{ob.buckets, ob.mask, ob.kind, ob.width});
        }
        e.wheels = tab.data();                      // nwheels unchanged: the cycle delivers only the domain's own
        e.due_ok = false;
    }
    for (auto& r : retarget) eng(r.dom).routes[r.route].wheel = T->outboxes[r.ob].src_wheel;
    // where each outbox lands: the destination's wheel of its kind and width
    for (auto& ob : T->outboxes) {
        ob.wheel.assign(D, kNone);
        if (ob.direct) continue;                     // W10: delivered from the outbox itself
        for (uint32_t d = 0; d < D; d++) {
            if (ob.dst != kFan ? d != ob.dst : ob.fan[d].empty()) continue;
            const Engine& e = eng(d);
            for (uint32_t w = 0; w < e.nwheels; w++)
                if (e.wheels[w].kind == ob.kind && e.wheels[w].width == ob.width) ob.wheel[d] = w;
            if (ob.wheel[d] == kNone) FATAL("split: no %s wheel of width %u in domain %u", kind_name(ob.kind), ob.width, d);
        }
    }
    // W10: each domain's incoming direct outboxes
    T->xins.resize(D);
    for (auto& ob : T->outboxes) {
        if (!ob.direct) continue;
        T->xins[ob.dst].push_back(Engine::XIn{ob.buckets, ob.xdst.data(), ob.mask, ob.width});
        if (ob.latency + interval > 64) eng(ob.dst).xdue_all = true;
    }
    for (uint32_t d = 0; d < D; d++) { eng(d).xin = T->xins[d].data(); eng(d).nxin = uint32_t(T->xins[d].size()); }
    // by reference: each class's slot storage, by domain (the drain reads a source's slot without its pool's checks)
    for (uint32_t d = 0; d < D; d++) {
        Engine& e = eng(d);
        T->doms[d].slot_base.resize(e.npools);
        for (uint32_t c = 0; c < e.npools; c++) {
            const uint32_t i = e.pools[c].alloc();
            T->doms[d].slot_base[c] = e.pools[c].slot(i) - size_t(i) * e.pools[c].slot_words();
            e.pools[c].free(i);
        }
    }

    // ---- status words: one per crossing Hold, one per Count replica, two per domain (next event, words sent)
    uint32_t nslots = 0;
    for (auto& h : T->holds) h.slot = nslots++;
    std::map<uint32_t, std::vector<uint32_t>> reps_of;
    for (auto& [key, m] : members) {
        if (comp_doms[key.first].size() < 2) continue;
        XCount x;
        x.dom = key.second; x.idx = fin(key.second, m[0]); x.slot = nslots++; x.comp = key.first;
        reps_of[key.first].push_back(uint32_t(T->counts.size()));
        T->doms[key.second].counts.push_back(uint32_t(T->counts.size()));
        T->counts.push_back(x);
        counts_by_kind[unsigned(Kind::count)]++;
    }
    for (auto& x : T->counts) {
        x.reps = reps_of[x.comp];
        // a Count read across domains: its `now` would miss the others' increments (A3: caught in debug builds by
        // Ctx::now's level check, which no level passes)
        bool remote_producer = false;
        for (uint32_t id = 0; id < G.decls.size(); id++)
            if (G.decls[id].kind == Kind::count && G.decls[id].out && find(id) == x.comp && ddom(id) != x.dom) remote_producer = true;
        if (remote_producer) eng(x.dom).counts[x.idx].max_producer_level = 0xFFFFFFFEu;
    }
    for (uint32_t d = 0; d < D; d++) {
        T->doms[d].next_slot = nslots++; T->doms[d].sent_slot = nslots++; T->doms[d].stop_slot = nslots++;
        T->doms[d].e->stop_breaks = false;       // W10: a stop runs on to the interval's barrier, where every thread ends
    }
    // the packing rule (base/words.h): each status word is written by one domain's thread and read by others, so each has
    // its own cache line; the two banks too
    T->status_block = std::make_unique<Block>(std::max<size_t>(1, 2 * size_t(nslots)) * kLineWords, "sim: the status words");
    T->status[0] = T->status_block->words();
    T->status[1] = T->status_block->words() + size_t(nslots) * kLineWords;

    // ---- the digest view, in the global add order; names, types
    for (uint32_t u = 0; u < n; u++) {
        const uint32_t d = dom[u];
        DigestUnit du = T->doms[d].S->view.units[new_unit[u]];
        du.eng = d;
        for (auto& p : du.ports) p.eng = d;
        T->view.units.push_back(std::move(du));
        T->dom_of[G.units[u].name] = d;
    }
    for (size_t k = 0; k < cross.size(); k++)
        if (G.decls[cross[k].o].kind == Kind::credit) {
            const uint32_t port = fin(cross[k].q, new_decl[cross[k].i]), proxy = fin(cross[k].p, cb[k].sink);
            for (auto& p : T->view.units[G.decls[cross[k].i].unit].ports)
                if (p.kind == Kind::credit && p.idx == port) { p.ceng = cross[k].p; p.cidx = proxy; }
        }
    T->types = G.types;

    // E6, the missed-wake detector across domains: a word put in another domain sits in an outbox until the drain, so the
    // consumer's detector can't see it on its way for up to an interval. The drain marks it on its way (below, as
    // on_its_way would have each cycle), and a consumer of a crossing waits an interval longer before it is judged (a real
    // missed wake is still caught, at most `interval` cycles later)
    if (kDebug && interval != kNoCycle)              // (release builds have no detector: its arrays are empty)
        for (size_t k = 0; k < cross.size(); k++) {
            const uint32_t q = cross[k].q, inst = T->doms[q].S->view.units[new_unit[G.decls[cross[k].i].unit]].inst;
            Engine& e = eng(q);
            if (e.missed_after[inst]) e.missed_after[inst] += uint32_t(interval);
        }

    // ---- threads
    T->nthreads = std::max(1u, std::min(G.opt.threads, D));
    T->timing = G.opt.time_threads;
    T->ph.assign(T->nthreads, Threads::PhaseTicks{});
    for (uint32_t d = 0; d < D; d++) T->doms[d].thread = unsigned(uint64_t(d) * T->nthreads / D);
    T->cpus = G.opt.cpus;
    const bool flags = G.opt.barrier == Options::Barrier::flags ||
                       (G.opt.barrier == Options::Barrier::automatic && T->nthreads >= 3 && !G.opt.barrier_block);   // W10
    T->barrier.init(T->nthreads, G.opt.barrier_block, G.opt.barrier_spins, flags);
    T->where = std::make_unique<Threads::Where[]>(T->nthreads);
    T->barrier.watchdog(10.0, &Threads::report_where, T);

    // ---- sizes (D15) and the report
    std::map<std::string, SizeLine> by_s, by_t;
    std::map<unsigned, uint64_t> by_chip;
    uint64_t total = 0;
    for (uint32_t d = 0; d < D; d++) {
        const Sizes& z = T->doms[d].S->sizes;
        for (auto& l : z.by_structure) { auto& t = by_s[l.what]; t.what = l.what; t.words += l.words; t.count += l.count; }
        for (auto& l : z.by_type) { auto& t = by_t[l.what]; t.what = l.what; t.words += l.words; t.count += l.count; }
        total += z.total_words;
        T->sz.wheel_buckets = std::max(T->sz.wheel_buckets, z.wheel_buckets);
    }
    for (uint32_t u = 0; u < n; u++) by_chip[G.units[u].chip] += 0;
    by_s["outboxes"] = SizeLine{"outboxes", outbox_words, uint32_t(T->outboxes.size())};
    for (auto& [k, v] : by_s) T->sz.by_structure.push_back(v);
    for (auto& [k, v] : by_t) T->sz.by_type.push_back(v);
    T->sz.total_words = total + outbox_words;
    T->sz.chips = unsigned(std::max<size_t>(1, by_chip.size()));
    T->sz.words_per_chip = T->sz.total_words / T->sz.chips;
    std::string rep = (G.placement.empty() ? std::to_string(declared) + " domains in " : G.placement + "; ") +
                      std::to_string(D) + " engines on " + std::to_string(T->nthreads) + " threads; crossings:";
    const Kind kinds[] = {Kind::link, Kind::fifo, Kind::credit, Kind::hold, Kind::reg, Kind::count};
    bool any = false;
    for (Kind k : kinds)
        if (counts_by_kind[unsigned(k)]) { rep += " " + std::to_string(counts_by_kind[unsigned(k)]) + " " + kind_name(k); any = true; }
    if (!any) rep += " none";
    {
        uint64_t direct = 0;
        for (const auto& ob : T->outboxes) if (ob.direct) direct += ob.routes;
        if (direct) rep += " (" + std::to_string(direct) + " link delivered direct)";   // W10
    }
    rep += interval == kNoCycle ? "; no barrier until the end of a run"
                                : "; barrier every " + std::to_string(interval) + " cycles, set by " + why;
    T->report = rep;
    if (T->nthreads > 1) T->report += flags ? "; the flags barrier" : "; the counter barrier";   // W10

    auto* SI = new Sim::Impl;
    SI->threads = T;
    return SI;
}

// ---- observers ------------------------------------------------------------------------------------------------------
Threads::~Threads() {
    if (!workers.empty()) {
        { std::lock_guard<std::mutex> g(mu); stop = true; go++; }
        cv.notify_all();
        for (auto& w : workers) w.join();
    }
}
void Threads::report_where(void* self) {
    const Threads& T = *static_cast<const Threads*>(self);
    for (unsigned t = 0; t < T.nthreads; t++)
        std::fprintf(stderr, "thread %u: interval from cycle %llu, domain %u, %s\n", t, (unsigned long long)T.where[t].cycle.load(),
                     T.where[t].dom.load(), T.where[t].phase.load());
}
uint32_t Threads::dom_index(const std::string& name) const {
    auto it = dom_of.find(name);
    if (it == dom_of.end()) FATAL("no unit named '%s'", name.c_str());
    return it->second;
}
uint16_t Threads::type_of(const std::string& name) const {
    const TypeInfo* t = doms[dom_index(name)].S->types[sims[dom_index(name)].type_of(name)];
    for (size_t k = 0; k < types.size(); k++) if (types[k] == t) return uint16_t(k);
    FATAL("type_of '%s': not a type of this machine", name.c_str());
}
void* Threads::unit_ptr(const std::string& name, const TypeInfo* type) const {
    return Internals::unit_ptr(sims[dom_index(name)], name, type);
}
uint64_t Threads::steps(const std::string& name) const { return sims[dom_index(name)].steps(name); }
void Threads::wake(const std::string& name) { sims[dom_index(name)].wake(name); }   // W10: in its own domain
void Threads::wake_all() { for (auto& x : sims) x.wake_all(); }                                   // W20 (harness-only)
bool Threads::in_flight(const std::vector<uint16_t>& shifted, const std::vector<uint16_t>& ignored) const { for (auto& x : sims) if (x.in_flight(shifted, ignored)) return true; return false; }   // W20
std::string Threads::sleepers() const {
    std::string s;
    for (auto& x : sims) s += x.sleepers();
    return s;
}
uint64_t Threads::digest() const {
    std::vector<const Internals::SImpl*> v;
    for (auto& d : doms) v.push_back(d.S);
    return Internals::digest(view, v.data(), now());
}

// ---- the run --------------------------------------------------------------------------------------------------------
void Threads::run_until(Cycle end) {
    if (end <= now()) return;
    if (workers.empty() && nthreads > 1)
        for (unsigned t = 1; t < nthreads; t++) workers.emplace_back([this, t] { thread_main(t); });
    if (nthreads > 1) {
        { std::lock_guard<std::mutex> g(mu); target = end; guard = in_run(); go++; }
        cv.notify_all();
    }
    if (!cpus.empty()) {                       // the caller is thread 0 for the run; its own affinity is put back after
        cpu_set_t old;
        pthread_getaffinity_np(pthread_self(), sizeof old, &old);
        pin_to_cpu(cpus[0]);
        run_thread(0, end);
        pthread_setaffinity_np(pthread_self(), sizeof old, &old);
    } else {
        run_thread(0, end);
    }
}

void Threads::thread_main(unsigned t) {
    if (!cpus.empty()) pin_to_cpu(cpus[t % cpus.size()]);
    uint64_t seen = 0;
    for (;;) {
        bool g_run;
        Cycle end;
        {
            std::unique_lock<std::mutex> g(mu);
            cv.wait(g, [&] { return go != seen; });
            seen = go;
            if (stop) return;
            g_run = guard;                    // this run's: the caller may start the next before this thread looks again
            end = target;
        }
        if (g_run) begin_run();
        run_thread(t, end);
        if (g_run) end_run();
    }
}

namespace {
// A domain's cycles from its now to `stop` (exclusive): W3's run_until, with its idle jumps (nothing can arrive from
// another domain before the next drain, so a jump inside an interval is exact) and its debug checks.
void run_domain(Engine& e, Cycle stop) {
    if (stop > e.now) e.run_until(stop - 1);
}
}  // namespace

void Threads::run_thread(unsigned t, Cycle end) {
    Cycle now = 0;                            // every domain is at the same cycle between runs: read one of its own
    for (auto& d : doms) if (d.thread == t) { now = d.e->now; break; }
    Span prev{now, 0};
    unsigned par = 0;
    PhaseTicks* P = timing ? &ph[t] : nullptr;   // W10: Options::time_threads only
    uint64_t c0 = P ? sim_ticks() : 0, c1 = 0;
    const uint64_t start = c0;
    while (now < end) {
        const Cycle n = interval == kNoCycle ? end - now : std::min(interval, end - now);
        for (uint32_t di = 0; di < doms.size(); di++)
            if (doms[di].thread == t) { at(t, now, di, "running"); run_domain(*doms[di].e, now + n); }
        if (P) { c1 = sim_ticks(); P->run += c1 - c0; P->last_run[par] = c1 - c0; c0 = c1; }
        for (auto& d : doms) if (d.thread == t) publish(d, now, n, par);
        if (P) { c1 = sim_ticks(); P->publish += c1 - c0; c0 = c1; }
        at(t, now, kNone, "at the barrier after its interval");
        barrier.wait(t);
        if (P) {
            c1 = sim_ticks(); P->wait += c1 - c0; c0 = c1;
            if (t == 0) {                     // every thread's last_run was written before the barrier
                uint64_t mx = 0, sum = 0;
                for (unsigned k = 0; k < nthreads; k++) { mx = std::max(mx, ph[k].last_run[par]); sum += ph[k].last_run[par]; }
                ph_intervals++; ph_max_run += mx; ph_sum_run += sum;
            }
        }
        // the whole machine idle and nothing sent: every thread jumps to the same next event
        Cycle next = kNoCycle;
        uint64_t sent = 0;
        Cycle stop = kNoCycle;                // W10: a stop asked in any domain ends the run at this barrier, on every thread
        for (auto& d : doms) {
            next = std::min<Cycle>(next, status[par][d.next_slot * kLineWords]); sent += status[par][d.sent_slot * kLineWords];
            stop = std::min<Cycle>(stop, status[par][d.stop_slot * kLineWords]);
        }
        for (uint32_t di = 0; di < doms.size(); di++) if (doms[di].thread == t) { at(t, now, di, "draining"); drain(di, now, n, par, sent != 0); }
        if (P) { c1 = sim_ticks(); P->drain += c1 - c0; c0 = c1; }
        for (auto& d : doms) if (d.thread == t) clear(d, prev);
        if (P) { c1 = sim_ticks(); P->clear += c1 - c0; c0 = c1; }
        prev = Span{now, n};
        now += n;
        if (stop != kNoCycle) { par ^= 1; break; }
        if (sent == 0 && next > now && now < end) {
            const Cycle j = std::min(next, end);
            for (auto& d : doms) if (d.thread == t) d.e->now = j;
            now = j;
            prev = Span{now, 0};              // nothing was sent: nothing to clear (and its buckets may alias the next)
        }
        par ^= 1;
    }
    at(t, now, kNone, "at the barrier ending the run");
    barrier.wait(t);                          // every drain done: the last interval's outbox entries can go
    for (auto& d : doms) if (d.thread == t) {
        clear(d, prev);
        if constexpr (kDebug) if (d.e->npools) d.e->check_leaks();
    }
    if (P) { c1 = sim_ticks(); P->end += c1 - c0; P->total += c1 - start; }   // before the barrier: the caller reads them
    at(t, now, kNone, "at the last barrier of the run");
    barrier.wait(t);                          // the machine is still: the caller may read it
    at(t, now, kNone, "idle");
}

// W10: the phase report (Options::time_threads): each phase as a share of the threads' time over the runs, the mean
// over threads; "imbalance" is the slowest thread's compute less the mean, per interval, summed; "barrier_wait" is the mean
// wait less that (the barrier's own latency, and differences in publishing).
std::string Threads::thread_times() const {
    if (!timing) return "time_threads off";
    double run = 0, pub = 0, wait = 0, drn = 0, clr = 0, endp = 0, tot = 0;
    for (const auto& p : ph) { run += p.run; pub += p.publish; wait += p.wait; drn += p.drain; clr += p.clear; endp += p.end; tot += p.total; }
    const double T = double(ph.size());
    run /= T; pub /= T; wait /= T; drn /= T; clr /= T; endp /= T; tot /= T;
    const double imb = ph_intervals ? double(ph_max_run) - double(ph_sum_run) / T : 0;
    char b[400];
    std::snprintf(b, sizeof b, "intervals=%llu total=%.0f compute=%.4f imbalance=%.4f barrier_wait=%.4f drain=%.4f publish=%.4f clear=%.4f "
                  "end=%.4f other=%.4f", (unsigned long long)ph_intervals, tot, run / tot, imb / tot, (wait - imb) / tot,
                  drn / tot, pub / tot, clr / tot, endp / tot, (tot - run - wait - drn - pub - clr - endp) / tot);
    return b;
}

// Before the barrier: what this domain sent this interval leaves its count in flight; its Holds' occupancy, its Counts'
// increments, its next event go to the status words.
void Threads::publish(Dom& d, Cycle t0, Cycle n, unsigned par) {
    Engine& e = *d.e;
    uint64_t sent = 0;
    for (uint32_t o : d.out) {
        const Outbox& ob = outboxes[o];
        for (Cycle t = t0; t < t0 + n; t++) sent += ob.buckets[(t + ob.latency) & ob.mask].count();
    }
    e.pending -= sent;
    uint64_t* st = status[par];
    for (uint32_t h : d.holds_q) st[holds[h].slot * kLineWords] = e.holds[holds[h].port].occ;
    for (uint32_t c : d.counts) st[counts[c].slot * kLineWords] = e.counts[counts[c].idx].total - counts[c].base;
    st[d.sent_slot * kLineWords] = sent;
    st[d.stop_slot * kLineWords] = e.stop_at;
    Cycle next = t0 + n;
    if (!e.step_all && e.idle()) {
        next = e.next_wake();
        // W3's cap (debug): a missed-wake suspect is judged at its deadline, so no jump passes it; every thread caps the
        // agreed jump at the same cycle, the earliest deadline of any domain
        if constexpr (kDebug) if (e.detector && e.nsuspects) next = std::min(next, e.suspect_deadline());
    }
    st[d.next_slot * kLineWords] = next;
}

// After the barrier: the outboxes addressed to this domain into its wheels, at the bucket each entry was put for (so it
// arrives at t + L exactly as in one domain); then the status words.
void Threads::drain(uint32_t di, Cycle t0, Cycle n, unsigned par, bool any) {
    Dom& d = doms[di];
    Engine& e = *d.e;
    // Nothing sent anywhere: the outboxes aren't read at all (after an idle jump the next interval's buckets may alias
    // this one's, and their source may already be writing them).
    // W10: the buckets are another thread's, just written: their lines are misses. Ask for every bucket's header first,
    // then every non-empty bucket's entries, so the misses overlap instead of following one another.
    if (any) {
        for (uint32_t o : d.in) {
            const Outbox& ob = outboxes[o];
            for (Cycle t = t0; t < t0 + n; t++) __builtin_prefetch(&ob.buckets[(t + ob.latency) & ob.mask]);
        }
        for (uint32_t o : d.in) {
            const Outbox& ob = outboxes[o];
            for (Cycle t = t0; t < t0 + n; t++) {
                const Bucket& b = ob.buckets[(t + ob.latency) & ob.mask];
                const uint32_t c = b.count();
                if (!c) continue;
                const char* p = reinterpret_cast<const char*>(b.begin());
                const size_t bytes = size_t(c) * (ob.width + 1u) * sizeof(Word);
                for (size_t k = 0; k < bytes; k += 64) __builtin_prefetch(p + k);
            }
        }
    }
    if (any) for (uint32_t o : d.in) {
        const Outbox& ob = outboxes[o];
        if (ob.direct) {                                            // W10: counted and marked due; delivered at its cycle
            for (Cycle t = t0; t < t0 + n; t++) {
                const Cycle at = t + ob.latency;
                const Bucket& b = ob.buckets[at & ob.mask];
                const uint32_t c = b.count();
                if (!c) continue;
                e.pending += c;
                e.xdue |= uint64_t(1) << (at & 63);
                if constexpr (kDebug) {                             // the exact capacity check, and the detector (E6)
                    const Word* p = b.begin();
                    for (uint32_t k = 0; k < c; k++, p += ob.width + 1u) {
                        const uint32_t q = ob.xdst[p[0]];
                        LinkPort& l = e.links[q];
                        if (l.ring.count() + l.inflight >= l.cap) e.die_link_full(q);
                        l.inflight++;
                        if (e.detector && e.suspect_since[l.owner] != kNoCycle) e.suspect_since[l.owner] = e.now;
                    }
                }
            }
            continue;
        }
        const Wheel& wh = e.wheels[ob.wheel[di]];
        const unsigned ew = ob.width + 1u;
        const std::vector<Xl>& xl = ob.dst == kFan ? ob.fan[di] : ob.xl;
        const Dom& src = doms[ob.src];
        for (Cycle t = t0; t < t0 + n; t++) {
            const Bucket& b = ob.buckets[(t + ob.latency) & ob.mask];
            if (b.empty()) continue;
            Bucket& db = wh.buckets[(t + ob.latency) & wh.mask];
            const Word* p = b.begin();
            for (uint32_t k = 0; k < b.count(); k++, p += ew) {
                const Xl& x = xl[p[0]];
                if (x.dst == kNone) continue;                       // a fanned Reg not read here
                Word* q = db.append();
                q[0] = x.dst;
                if constexpr (kDebug) if (e.detector) {    // E6: on its way now, as the detector would have seen it locally
                    const uint32_t owner = ob.kind == Kind::link ? e.links[x.dst].owner : ob.kind == Kind::hold ? e.holds[x.dst].owner
                                         : ob.kind == Kind::cret ? e.fifos[x.dst].producer : ob.kind == Kind::reg ? kNone : e.fifos[x.dst].owner;
                    if (owner != kNone && e.suspect_since[owner] != kNoCycle) e.suspect_since[owner] = e.now;
                }
                for (unsigned w = 1; w < ew; w++) q[w] = p[w];
                e.pending++;
                if (ob.kind == Kind::link) {
                    LinkPort& l = e.links[x.dst];
                    if (__builtin_expect(l.ring.count() + l.inflight >= l.cap, 0)) e.die_link_full(x.dst);
                    l.inflight++;
                }
                if (x.scls != kNoClass) {                           // by reference: a copy in this domain's pool
                    if (ob.kind == Kind::hold && e.holds[x.dst].occ) continue;   // will be refused: the producer keeps it
                    SlotPool& pool = e.pools[x.dcls];
                    const uint32_t i = pool.alloc();
                    std::memcpy(pool.slot(i), src.slot_base[x.scls] + size_t(p[1]) * pool.slot_words(), pool.slot_words() * sizeof(Word));
                    q[1] = i;
                }
            }
        }
    }
    const uint64_t* st = status[par];
    for (uint32_t h : d.holds_p) {                                  // the producer's view of a Hold across
        XHold& x = holds[h];
        HoldPort& px = e.holds[x.proxy];
        const bool occ = st[x.slot * kLineWords] != 0, put = e.routes[x.route].last_put == e.now - 1;
        px.occ = occ || put;
        px.last_take = kNoCycle;
        if (put && occ) {
            px.refused_at = e.now - 1;
            px.refusals++;
            x.refused_at = e.now - 1;
            e.mark_bit(px.producer, px.pbit);
        }
    }
    for (uint32_t l : d.link_proxies) e.links[l].inflight = 0;
    for (uint32_t c : d.counts) {
        XCount& x = counts[c];
        uint64_t sum = 0;
        for (uint32_t r : x.reps) sum += st[counts[r].slot * kLineWords];
        x.base += sum;
        e.counts[x.idx].total = x.base;
    }
}

// The source empties an interval's outbox buckets once every drain of them is done (after the next barrier), freeing
// the slots its by-reference entries copied from (a refused Hold put keeps its slot for the retry, as in one domain).
void Threads::clear(Dom& d, Span s) {
    Engine& e = *d.e;
    for (uint32_t o : d.out) {
        const Outbox& ob = outboxes[o];
        if (ob.direct) continue;                     // W10: its destination empties each bucket as it delivers it
        const unsigned ew = ob.width + 1u;
        for (Cycle t = s.t0; t < s.t0 + s.n; t++) {
            Bucket& b = ob.buckets[(t + ob.latency) & ob.mask];
            if (b.empty()) continue;
            if (ob.width && ob.dst != kFan && ob.kind != Kind::cret) {
                const Word* p = b.begin();
                for (uint32_t k = 0; k < b.count(); k++, p += ew) {
                    const Xl& x = ob.xl[p[0]];
                    if (x.scls == kNoClass) continue;
                    if (ob.kind == Kind::hold && holds[ob.hold_of[p[0]]].refused_at == t) continue;
                    e.pools[x.scls].free(uint32_t(p[1]));
                }
            }
            b.clear();
        }
    }
}

}  // namespace sim::detail
