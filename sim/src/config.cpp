// config.cpp: building a machine (WORKER3, stage 2). The configuration regime (D6): any C++.
//
#include "config_impl.h"
#include "digest.h"
#include "threads.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "base/alloc_guard.h"

namespace sim {

// ---- Ports: a unit's declarations ------------------------------------------------------------------------------------
#define IMPL (*static_cast<Config::Impl*>(impl_))
void Ports::in(LinkIn& h, const char* name, unsigned width) {
    h.idx = IMPL.declare(&h, Kind::link, false, Shape::in, name, width, true, false);
    h.bit = IMPL.decls[h.idx].bit;
}
// W10: a Link whose consumer may hold up to `depth` words (capacity L + depth: ref's WordDelayLine(name, L, L + depth));
// the plain declaration is depth 2 (WordDelayLine's default, L + 2).
void Ports::in(LinkIn& h, const char* name, unsigned width, unsigned depth) {
    if (depth < 1 || depth > 0x7FFF) FATAL("link '%s': depth %u (1-32767)", name, depth);
    in(h, name, width);
    IMPL.decls[h.idx].depth = depth;
}
void Ports::in(FifoIn& h, const char* name, unsigned width, unsigned depth) {
    if (width < 1 || depth < 1 || depth > 0xFFFF) FATAL("fifo '%s': width %u and depth %u (1-3, 1-65535)", name, width, depth);
    h.idx = IMPL.declare(&h, Kind::fifo, false, Shape::in, name, width, true, false);
    h.bit = IMPL.decls[h.idx].bit;
    IMPL.decls[h.idx].depth = depth;
}
void Ports::overflow_steps(FifoIn& h) {
    if (h.idx == kNone || IMPL.decls[h.idx].kind != Kind::fifo) FATAL("overflow_steps: declare a plain Fifo input first (in(FifoIn&, ...))");
    IMPL.decls[h.idx].ovf_step = true;
}
void Ports::in_credit(FifoIn& h, const char* name, unsigned width, unsigned depth) {
    if (width < 1 || depth < 1 || depth > 0xFFFF) FATAL("credit fifo '%s': width %u and depth %u", name, width, depth);
    h.idx = IMPL.declare(&h, Kind::credit, false, Shape::in, name, width, true, false);
    h.bit = IMPL.decls[h.idx].bit;
    IMPL.decls[h.idx].depth = depth;
}
void Ports::in(HoldIn& h, const char* name, unsigned width) {
    if (width < 1) FATAL("hold '%s': width 0", name);
    h.idx = IMPL.declare(&h, Kind::hold, false, Shape::in, name, width, true, false);
    h.bit = IMPL.decls[h.idx].bit;
}
void Ports::in(RegIn& h, const char* name, unsigned width) {
    h.idx = IMPL.declare(&h, Kind::reg, false, Shape::in, name, width, true, true);
    h.bit = IMPL.decls[h.idx].bit;
}
void Ports::in(WireIn& h, const char* name) {
    h.idx = IMPL.declare(&h, Kind::wire, false, Shape::in, name, 1, true, true);
    h.bit = IMPL.decls[h.idx].bit;
}
void Ports::in(RegFileIn& h, const char* name) {
    h.idx = IMPL.declare(&h, Kind::regfile, false, Shape::in, name, 0, true, true);
    h.bit = IMPL.decls[h.idx].bit;
}
void Ports::in(CountIn& h, const char* name) { h.idx = IMPL.declare(&h, Kind::count, false, Shape::idx, name, 0, false, false); }
void Ports::out(LinkOut& h, const char* name, unsigned width) {
    h.route = IMPL.declare(&h, Kind::link, true, Shape::out, name, width, false, false);
}
void Ports::out(FifoOut& h, const char* name, unsigned width) {
    if (width < 1) FATAL("fifo output '%s': width 0", name);
    h.route = IMPL.declare(&h, Kind::fifo, true, Shape::out, name, width, false, false);
}
void Ports::out(CreditOut& h, const char* name, unsigned width) {
    h.route = IMPL.declare(&h, Kind::credit, true, Shape::out_bit, name, width, true, true);
    h.bit = IMPL.decls[h.route].bit;
}
void Ports::out(HoldOut& h, const char* name, unsigned width) {
    if (width < 1) FATAL("hold output '%s': width 0", name);
    h.route = IMPL.declare(&h, Kind::hold, true, Shape::out_bit, name, width, true, true);
    h.bit = IMPL.decls[h.route].bit;
}
void Ports::out(RegOut& h, const char* name, unsigned width) {
    h.route = IMPL.declare(&h, Kind::reg, true, Shape::out, name, width, false, false);
}
void Ports::out(WireOut& h, const char* name, Word reset_value) {
    h.idx = IMPL.declare(&h, Kind::wire, true, Shape::idx, name, 1, false, false);
    IMPL.decls[h.idx].reset = reset_value;
}
void Ports::out(RegFileOut& h, const char* name, unsigned registers) {
    h.idx = IMPL.declare(&h, Kind::regfile, true, Shape::idx, name, 0, false, false);
    IMPL.decls[h.idx].regs = registers;
}
void Ports::out(CountOut& h, const char* name) { h.idx = IMPL.declare(&h, Kind::count, true, Shape::idx, name, 0, false, false); }
void Ports::by_ref_decl(uint32_t id, unsigned slot_words, unsigned held) {
    Config::Impl::Decl& d = IMPL.decl(id, "by_ref");
    if (slot_words < 1) FATAL("by_ref: '%s' with slots of 0 words", d.name.c_str());
    if (d.width < 1) FATAL("by_ref: '%s' has no payload word to carry the index", d.name.c_str());
    if (d.slot_words) FATAL("by_ref: '%s' declared twice", d.name.c_str());
    d.slot_words = slot_words;
    d.held = held;
}
void Ports::by_ref(LinkIn& h, unsigned w, unsigned held) { by_ref_decl(h.idx, w, held); }
void Ports::by_ref(LinkOut& h, unsigned w, unsigned held) { by_ref_decl(h.route, w, held); }
void Ports::by_ref(FifoIn& h, unsigned w, unsigned held) { by_ref_decl(h.idx, w, held); }
void Ports::by_ref(FifoOut& h, unsigned w, unsigned held) { by_ref_decl(h.route, w, held); }
void Ports::by_ref(CreditOut& h, unsigned w, unsigned held) { by_ref_decl(h.route, w, held); }
void Ports::by_ref(HoldIn& h, unsigned w, unsigned held) { by_ref_decl(h.idx, w, held); }
void Ports::by_ref(HoldOut& h, unsigned w, unsigned held) { by_ref_decl(h.route, w, held); }
#undef IMPL
// ---- Config ----------------------------------------------------------------------------------------------------------
Config::Config(Options o) : impl_(new Impl) {
    impl_->opt = o;
    impl_->ports = new Ports();
    impl_->ports->impl_ = impl_;
}
Config::~Config() {
    for (auto& u : impl_->units)
        if (u.obj) { (*impl_->types[u.type]).destroy(u.obj); ::operator delete(u.obj, std::align_val_t((*impl_->types[u.type]).align)); }
    delete impl_->ports;
    delete impl_;
}
void Config::domain(unsigned d) { impl_->domain = d; }
void Config::chip(unsigned c) { impl_->chip = c; }

void* Config::stage(const detail::TypeInfo* type, Form f, const std::string& name) {
    Impl& I = *impl_;
    if (I.built) FATAL("unit '%s' added after build()", name.c_str());
    if (I.cur >= 0) FATAL("unit '%s' added inside another unit's constructor", name.c_str());
    if (name.empty()) FATAL("a unit needs a name");
    if (!I.names.emplace(name, uint32_t(I.units.size())).second) FATAL("two units named '%s'", name.c_str());
    if (f == Form::result && !type->run[1]) FATAL("unit '%s': its type has no result form", name.c_str());
    Impl::Staged u;
    u.type = I.type_id(type);
    const size_t size = type->size, align = type->align;
    u.form = f;
    u.name = name;
    u.chip = I.chip;
    u.domain = I.domain;
    u.obj = static_cast<unsigned char*>(::operator new(size, std::align_val_t(align < 8 ? 8 : align)));
    std::memset(u.obj, 0, size);
    I.units.push_back(std::move(u));
    return I.units.back().obj;
}
Ports& Config::begin_ports(void* unit) {
    (void)unit;
    impl_->cur = int(impl_->units.size() - 1);
    return *impl_->ports;
}
void Config::end_ports() { impl_->cur = -1; }

void Config::link(LinkOut o, LinkIn i, unsigned latency) { impl_->wire(o.route, i.idx, Kind::link, latency, false, "link"); }
void Config::connect(FifoOut o, FifoIn i) { impl_->wire(o.route, i.idx, Kind::fifo, 1, false, "connect"); }
void Config::connect(CreditOut o, FifoIn i) { impl_->wire(o.route, i.idx, Kind::credit, 1, false, "connect"); }
void Config::connect(HoldOut o, HoldIn i) { impl_->wire(o.route, i.idx, Kind::hold, 1, false, "connect"); }
void Config::connect(RegOut o, RegIn i) { impl_->wire(o.route, i.idx, Kind::reg, 1, true, "connect"); }
void Config::connect(WireOut o, WireIn i) { impl_->wire(o.idx, i.idx, Kind::wire, 1, true, "connect"); }
void Config::connect(RegFileOut o, RegFileIn i) { impl_->wire(o.idx, i.idx, Kind::regfile, 1, true, "connect"); }
void Config::connect(CountOut o, CountIn i) {
    Impl::Decl& O = impl_->decl(o.idx, "connect");
    Impl::Decl& I = impl_->decl(i.idx, "connect");
    if (O.kind != Kind::count || !O.out || I.kind != Kind::count || I.out) FATAL("connect: '%s' to '%s' is not a count", O.name.c_str(), I.name.c_str());
    O.peers.push_back(i.idx);
    I.peers.push_back(o.idx);
}
void Config::missed_wake_after(const std::string& unit, unsigned cycles) { impl_->missed[unit] = cycles; }
void Config::weight(const std::string& unit, double w) {
    if (!(w >= 0)) FATAL("weight of '%s': %g (a load is >= 0)", unit.c_str(), w);
    impl_->weights[unit] = w;
}
void Config::count_overflows(FifoIn f, CountIn c) {
    Impl::Decl& F = impl_->decl(f.idx, "count_overflows");
    Impl::Decl& C = impl_->decl(c.idx, "count_overflows");
    if (F.kind != Kind::fifo || F.out || C.kind != Kind::count || C.out) FATAL("count_overflows: '%s', '%s'", F.name.c_str(), C.name.c_str());
    if (F.peers.size() > 1) FATAL("count_overflows: '%s' already counts into a Count", F.name.c_str());
    F.peers.push_back(c.idx);   // a fifo input's second peer: its Count
    C.peers.push_back(f.idx);
}

// ---- Sim --------------------------------------------------------------------------------------------------------------
namespace {
// The carving: every structure's words, named, then one block and the pointers.
struct Carver {
    struct Item { std::string what; size_t words; uint32_t count; int type; int chip; size_t off; };
    std::vector<Item> items;
    size_t total = 0;
    size_t add(std::string what, size_t words, uint32_t count = 1, int type = -1, int chip = -1) {
        if (round_to_lines(total) != total) {           // a structure on its own lines (after packed ones): the gap counted
            items.push_back({"padding (to whole lines)", round_to_lines(total) - total, 1, -1, -1, total});
            total = round_to_lines(total);
        }
        words = round_to_lines(words);
        items.push_back({std::move(what), words, count, type, chip, total});
        total += words;
        return total - words;
    }
    // Packed: no rounding, so small structures this thread alone writes share lines (base/words.h). Word-aligned.
    size_t add_packed(std::string what, size_t words, uint32_t count = 1, int type = -1, int chip = -1) {
        items.push_back({std::move(what), words, count, type, chip, total});
        total += words;
        return total - words;
    }
    template <class T> size_t add_array(std::string what, size_t n, int type = -1, int chip = -1) {
        return add(std::move(what), (sizeof(T) * n + 7) / 8, uint32_t(n), type, chip);
    }
};
unsigned pow2_above(unsigned x) { unsigned w = 2; while (w <= x) w <<= 1; return w; }
}  // namespace

Sim Config::build() {
    Impl& I = *impl_;
    if (I.built) FATAL("build() called twice");
    bool domains = false;                     // W10: several domains in one engine: checked as if split, then built whole
    for (const auto& u : I.units) domains = domains || u.domain != I.units[0].domain;
    bool placed_whole = false;                // W10: by_load found one group: one engine, whatever the threads
    if (I.opt.placement == Options::Placement::by_load) {
        if (domains) detail::check_domains(I);   // on the author's domains, before they are replaced by the groups
        unsigned groups = 1;
        if (I.opt.threads > 1 && I.units.size() > 1) {
            Cycle interval = 0;
            const std::vector<unsigned> g = detail::place_by_load(I, I.opt.threads, groups, interval, I.placement);
            if (groups > 1) for (size_t u = 0; u < I.units.size(); u++) I.units[u].domain = g[u];
        }
        if (groups <= 1) { placed_whole = true; for (auto& u : I.units) u.domain = 0; domains = false; }
    }
    if (detail::split_needed(I)) { I.built = true; return Sim(detail::Internals::split(I)); }   // W4 (E3): several engines
    I.built = true;
    if (domains) detail::check_domains(I);
    using Decl = Impl::Decl;
    const uint32_t n = uint32_t(I.units.size());

    // 0. every option is implemented, or refused: an option set must never be silently ignored (integrator, 2026-11-07)
    if (I.opt.threads != 1 && !domains && !placed_whole)
        FATAL("option threads = %u not implemented for one domain: split the machine with Config::domain, or run independent "
              "chips with run_independent (W4)", I.opt.threads);

    // 1. every port wired (a Count may stand alone: a counter no one reads, or read but never incremented)
    for (const Decl& d : I.decls)
        if (d.peers.empty() && d.kind != Kind::count)
            FATAL("port '%s' (%s %s) is not wired", d.name.c_str(), kind_name(d.kind), d.out ? "output" : "input");

    // 1b. by-reference payloads: both ends declare the same size class; a class per slot size; its capacity the most
    //     payloads its ports hold at once (config.h's sizing) plus what the ends declare they hold
    std::map<unsigned, uint32_t> class_of;          // slot words -> class
    std::vector<unsigned> class_words;
    std::vector<uint64_t> class_cap, class_allow;
    std::vector<uint16_t> decl_cls(I.decls.size(), kNoClass);
    for (uint32_t id = 0; id < I.decls.size(); id++) {
        const Decl& d = I.decls[id];
        if (d.out || d.peers.empty() || d.kind == Kind::count || d.kind == Kind::reg || d.kind == Kind::wire || d.kind == Kind::regfile) continue;
        const Decl& o = I.decls[d.peers[0]];
        if (d.slot_words != o.slot_words)
            FATAL("by_ref: '%s' (slots of %u words) and '%s' (slots of %u words) must declare the same size class (0: by value)",
                  o.name.c_str(), o.slot_words, d.name.c_str(), d.slot_words);
        if (!d.slot_words) continue;
        auto it = class_of.find(d.slot_words);
        if (it == class_of.end()) {
            it = class_of.emplace(d.slot_words, uint32_t(class_words.size())).first;
            class_words.push_back(d.slot_words);
            class_cap.push_back(0);
            class_allow.push_back(0);
        }
        unsigned per = d.kind == Kind::link ? Config::Impl::link_capacity(d) : d.kind == Kind::fifo ? d.depth + 1 : d.kind == Kind::credit ? d.depth : 2;
        class_cap[it->second] += per + d.held + o.held;
        class_allow[it->second] += d.held + o.held + (d.kind == Kind::hold ? 1 : 0);   // a refused word awaiting its retry
        decl_cls[id] = decl_cls[d.peers[0]] = uint16_t(it->second);
    }
    for (uint64_t cap : class_cap) if (cap > 0xFFFFFFFFull) FATAL("a size class of more than 2^32 slots");

    // 2. levels, from the Wires and RegFiles (a producer below each reader); a loop is fatal with its units named
    std::vector<std::vector<uint32_t>> succ(n);
    std::vector<uint32_t> indeg(n, 0);
    for (const Decl& d : I.decls)
        if (d.out && (d.kind == Kind::wire || d.kind == Kind::regfile))
            for (uint32_t r : d.peers) { succ[d.unit].push_back(I.decls[r].unit); indeg[I.decls[r].unit]++; }
    {
        std::vector<uint32_t> q, deg = indeg;
        for (uint32_t u = 0; u < n; u++) if (!deg[u]) q.push_back(u);
        for (size_t k = 0; k < q.size(); k++)
            for (uint32_t v : succ[q[k]]) {
                I.units[v].level = std::max(I.units[v].level, I.units[q[k]].level + 1);
                if (--deg[v] == 0) q.push_back(v);
            }
        if (q.size() != n) {                         // a loop: walk back from a unit left over until one repeats
            std::vector<std::vector<uint32_t>> pred(n);
            for (uint32_t u = 0; u < n; u++) for (uint32_t v : succ[u]) if (deg[u] && deg[v]) pred[v].push_back(u);
            uint32_t u = 0;
            while (!deg[u]) u++;
            std::vector<int> seen(n, -1);
            std::vector<uint32_t> path;
            while (seen[u] < 0) { seen[u] = int(path.size()); path.push_back(u); u = pred[u][0]; }
            std::string loop;
            for (size_t k = path.size(); k-- > size_t(seen[u]);) loop += I.units[path[k]].name + " -> ";   // forward order
            loop += I.units[path.back()].name;
            FATAL("a combinational loop through wires: %s", loop.c_str());
        }
    }
    // one level per (type, form): the largest of its instances (sim.h), then every wire checked again
    std::map<std::pair<uint16_t, int>, uint32_t> tf_level;
    for (auto& u : I.units) { auto& l = tf_level[{u.type, int(u.form)}]; l = std::max(l, u.level); }
    for (auto& u : I.units) u.level = tf_level[{u.type, int(u.form)}];
    for (uint32_t u = 0; u < n; u++)
        for (uint32_t v : succ[u])
            if (I.units[v].level <= I.units[u].level)
                FATAL("levels: '%s' (type %s, level %u) drives a wire read by '%s' (type %s, level %u): one level per type "
                      "can't order them", I.units[u].name.c_str(), I.types[I.units[u].type]->name.c_str(),
                      I.units[u].level, I.units[v].name.c_str(), I.types[I.units[v].type]->name.c_str(), I.units[v].level);

    // 3. the instances' order: (level, type in first-add order, form), then add order; runs are its contiguous groups
    std::vector<uint16_t> type_rank(I.types.size(), 0xFFFF);
    uint16_t nt = 0;
    for (auto& u : I.units) if (type_rank[u.type] == 0xFFFF) type_rank[u.type] = nt++;
    std::vector<uint32_t> order(n);
    for (uint32_t i = 0; i < n; i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const auto &A = I.units[a], &B = I.units[b];
        if (A.level != B.level) return A.level < B.level;
        if (type_rank[A.type] != type_rank[B.type]) return type_rank[A.type] < type_rank[B.type];
        return A.form < B.form;
    });
    std::vector<uint32_t> inst_of(n);
    for (uint32_t k = 0; k < n; k++) inst_of[order[k]] = k;
    struct RunDef { uint32_t first, count, level; uint16_t type; Form form; };
    std::vector<RunDef> runs;
    for (uint32_t k = 0; k < n; k++) {
        const auto& u = I.units[order[k]];
        if (runs.empty() || runs.back().type != u.type || runs.back().form != u.form || runs.back().level != u.level)
            runs.push_back({k, 0, u.level, u.type, u.form});
        runs.back().count++;
    }

    // 4. numbering. Inputs of Link, Fifo (credit too), Hold: their own pools, consumer type-major then instance. Reg, Wire
    //    and RegFile: one port per output, its readers pointing at it. Outputs that put (Link, Fifo, credit, Hold, Reg):
    //    routes, in the same order, then one return route per credit Fifo. Counts: one per connected set.
    uint32_t nlink = 0, nfifo = 0, nhold = 0, nroute = 0, nreg = 0, nwire = 0, nrf = 0, ncount = 0;
    std::vector<uint32_t> regidx(I.decls.size(), kNone);
    std::vector<std::string> route_names;
    std::vector<uint32_t> route_decl;
    for (uint32_t k = 0; k < n; k++)
        for (uint32_t id : I.units[order[k]].decls) {
            Decl& d = I.decls[id];
            if (d.out) {
                switch (d.kind) {
                    case Kind::wire: d.final = nwire++; break;
                    case Kind::regfile: d.final = nrf++; break;
                    case Kind::count: break;
                    case Kind::reg: regidx[id] = nreg++; [[fallthrough]];
                    default: d.final = nroute++; route_names.push_back(d.name); route_decl.push_back(id); break;
                }
                continue;
            }
            if (d.kind == Kind::link) d.final = nlink++;
            else if (d.kind == Kind::fifo || d.kind == Kind::credit) d.final = nfifo++;
            else if (d.kind == Kind::hold) d.final = nhold++;
        }
    for (Decl& d : I.decls) {
        if (d.out || d.peers.empty()) continue;
        const uint32_t prod = d.peers[0];
        if (d.kind == Kind::reg) d.final = regidx[prod];
        else if (d.kind == Kind::wire || d.kind == Kind::regfile) d.final = I.decls[prod].final;
    }
    std::vector<uint32_t> ret_route(I.decls.size(), kNone);
    for (uint32_t k = 0; k < n; k++)
        for (uint32_t id : I.units[order[k]].decls)
            if (!I.decls[id].out && I.decls[id].kind == Kind::credit) {
                ret_route[id] = nroute++;
                route_names.push_back(I.decls[id].name + " (credit return)");
                route_decl.push_back(kNone);
            }
    {   // counts: union-find over the count declarations and the Fifos that count their overflows into one
        std::vector<uint32_t> parent(I.decls.size());
        for (uint32_t i = 0; i < parent.size(); i++) parent[i] = i;
        auto find = [&](uint32_t x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
        for (uint32_t id = 0; id < I.decls.size(); id++)
            if (I.decls[id].kind == Kind::count) for (uint32_t p : I.decls[id].peers) parent[find(id)] = find(p);
        std::map<uint32_t, uint32_t> comp;
        for (uint32_t k = 0; k < n; k++)
            for (uint32_t id : I.units[order[k]].decls)
                if (I.decls[id].kind == Kind::count) {
                    auto it = comp.find(find(id));
                    if (it == comp.end()) it = comp.emplace(find(id), ncount++).first;
                    I.decls[id].final = it->second;
                }
    }

    // 5. wheels: one per (kind, width) with routes on it; W above its largest latency; a bucket holds one put per route
    struct WheelDef { Kind kind; unsigned width, W, cap; };
    std::vector<WheelDef> wheels;
    std::map<std::pair<unsigned, unsigned>, uint32_t> wheel_of;
    std::vector<uint32_t> route_wheel(nroute);
    for (uint32_t r = 0; r < nroute; r++) {
        Kind kd = route_decl[r] == kNone ? Kind::cret : I.decls[route_decl[r]].kind;
        unsigned w = route_decl[r] == kNone ? 0 : I.decls[route_decl[r]].width;
        unsigned lat = route_decl[r] == kNone ? 1 : I.decls[route_decl[r]].latency;
        auto key = std::make_pair(unsigned(kd), w);
        auto it = wheel_of.find(key);
        if (it == wheel_of.end()) { it = wheel_of.emplace(key, uint32_t(wheels.size())).first; wheels.push_back({kd, w, 2, 0}); }
        WheelDef& wd = wheels[it->second];
        wd.W = std::max(wd.W, pow2_above(lat));
        wd.cap++;
        route_wheel[r] = it->second;
    }

    // 6. sizing: every structure's words, by structure, type and chip (D15)
    uint32_t nreaders = 0;
    for (const Decl& d : I.decls)
        if (d.out && (d.kind == Kind::reg || d.kind == Kind::wire || d.kind == Kind::regfile)) nreaders += uint32_t(d.peers.size());
    Carver c;
    auto tname = [&](uint16_t t) { return I.types[t]->name; };
    const size_t o_mask = c.add("instances: ready masks", n), o_edges = c.add("instances: edge bits", n),
                 o_wake = c.add("instances: wake cycles", n), o_steps = c.add("instances: step counts", n),
                 o_runof = c.add("instances: run of", (n + 1) / 2), o_hpos = c.add("instances: heap places", (n + 1) / 2),
                 o_heap = c.add("far wakes (heap)", (n + 1) / 2);
    const size_t o_links = c.add_array<LinkPort>("link ports", nlink), o_fifos = c.add_array<FifoPort>("fifo ports", nfifo),
                 o_holds = c.add_array<HoldPort>("hold ports", nhold), o_regs = c.add_array<RegPort>("reg ports", nreg),
                 o_wires = c.add_array<WirePort>("wire ports", nwire), o_rfs = c.add_array<RegFilePort>("regfile ports", nrf),
                 o_counts = c.add_array<CountPort>("counts", ncount), o_rinst = c.add_array<uint32_t>("readers", nreaders),
                 o_rbit = c.add_array<uint8_t>("readers: bits", nreaders), o_routes = c.add_array<Route>("routes", nroute);
    std::vector<size_t> o_ring(I.decls.size(), 0);
    for (uint32_t id = 0; id < I.decls.size(); id++) {
        const Decl& d = I.decls[id];
        const auto& u = I.units[d.unit];
        if (d.out && d.kind == Kind::regfile) { o_ring[id] = c.add("regfile registers", (d.regs + 1) / 2, 1, u.type, int(u.chip)); continue; }
        if (d.out) continue;
        if (d.kind == Kind::link)            // width 0 (an event line): no storage, the count only
            o_ring[id] = c.add_packed("ring link w" + std::to_string(d.width), size_t(d.width) * Config::Impl::link_capacity(d), 1, u.type, int(u.chip));
        else if (d.kind == Kind::fifo || d.kind == Kind::credit)
            o_ring[id] = c.add_packed(std::string("ring ") + (d.kind == Kind::credit ? "credit" : "fifo") + " w" + std::to_string(d.width),
                                      size_t(d.width) * d.depth, 1, u.type, int(u.chip));
    }
    const size_t o_wheels = c.add_array<Wheel>("wheels: headers", wheels.size());
    std::vector<size_t> o_bheads(wheels.size()), o_bstore(wheels.size());
    for (size_t w = 0; w < wheels.size(); w++) {
        std::string nm = std::string("wheel ") + kind_name(wheels[w].kind) + " w" + std::to_string(wheels[w].width);
        o_bheads[w] = c.add_array<Bucket>(nm + ": buckets", wheels[w].W);
        o_bstore[w] = c.add(nm, wheels[w].W * Bucket::words_needed(wheels[w].width + 1, wheels[w].cap), wheels[w].W);
    }
    const size_t o_pools = c.add_array<SlotPool>("slot pools: headers", class_words.size());
    std::vector<size_t> o_pool(class_words.size());
    for (size_t k = 0; k < class_words.size(); k++)
        o_pool[k] = c.add("slot pool w" + std::to_string(class_words[k]), SlotPool::words_needed(class_words[k], uint32_t(class_cap[k])),
                          uint32_t(class_cap[k]));
    const size_t o_allow = c.add_array<uint64_t>("slot pools: allowances", class_words.size());
    const size_t o_runs = c.add_array<RunSet>("runs", runs.size());
    std::vector<size_t> o_bits(runs.size());
    for (size_t r = 0; r < runs.size(); r++) o_bits[r] = c.add_packed("run sets", 2 * size_t((runs[r].count + 63) / 64), runs[r].count);
    // the debug orders and the missed-wake detector: add order, a level's scratch, watches, thresholds, suspects
    uint32_t nwatch = 0;
    for (const Decl& d : I.decls)
        if ((!d.out && (d.kind == Kind::link || d.kind == Kind::fifo || d.kind == Kind::credit || d.kind == Kind::hold)) ||
            (d.out && (d.kind == Kind::hold || d.kind == Kind::credit)))
            nwatch++;
    // carved only when they are used: the debug orders' when an order is set; the detector's in debug builds
    const bool orders = I.opt.shuffle_seed || I.opt.order == Options::Order::instance_major;
    const size_t nd = kDebug ? n : 0, no = orders ? n : 0;
    const size_t o_addord = c.add_array<uint32_t>("debug: add order", no), o_scratch = c.add_array<uint32_t>("debug: a level's order", no),
                 o_watch = c.add_array<Engine::Watch>("debug: detector watches", kDebug ? nwatch : 0),
                 o_wfirst = c.add_array<uint32_t>("debug: detector watch index", kDebug ? n + 1 : 0),
                 o_mwafter = c.add_array<uint32_t>("debug: detector thresholds", nd),
                 o_since = c.add_array<Cycle>("debug: detector suspects since", nd), o_susp = c.add_array<uint32_t>("debug: detector suspects", nd);
    // the run masks (one bit per run) and the near wakes (16 slots of an instance bitmap and its summary)
    const uint32_t nrw = uint32_t((runs.size() + 63) / 64), niw = (n + 63) / 64, nsw = (niw + 63) / 64;
    const size_t o_rmask = c.add_array<uint64_t>("run masks", 2 * size_t(nrw));
    const size_t o_near = c.add_array<uint64_t>("near wakes", Engine::kNear * size_t(niw + nsw));

    auto* S = new Sim::Impl;
    S->types = I.types;
    S->block = std::make_unique<Block>(c.total ? c.total : 8, "sim: the thread's block");
    uint64_t* B = S->block->words();
    Engine& e = S->e;
    e.ninst = n;
    e.mask = B + o_mask; e.edges = B + o_edges; e.wake_at = B + o_wake; e.steps = B + o_steps;
    e.run_of = reinterpret_cast<uint32_t*>(B + o_runof); e.heap_pos = reinterpret_cast<uint32_t*>(B + o_hpos);
    e.heap = reinterpret_cast<uint32_t*>(B + o_heap);
    e.links = reinterpret_cast<LinkPort*>(B + o_links); e.fifos = reinterpret_cast<FifoPort*>(B + o_fifos);
    e.holds = reinterpret_cast<HoldPort*>(B + o_holds); e.regs = reinterpret_cast<RegPort*>(B + o_regs);
    e.wires = reinterpret_cast<WirePort*>(B + o_wires); e.regfiles = reinterpret_cast<RegFilePort*>(B + o_rfs);
    e.counts = reinterpret_cast<CountPort*>(B + o_counts);
    e.reader_inst = reinterpret_cast<uint32_t*>(B + o_rinst); e.reader_bit = reinterpret_cast<uint8_t*>(B + o_rbit);
    e.routes = reinterpret_cast<Route*>(B + o_routes);
    e.wheels = reinterpret_cast<Wheel*>(B + o_wheels); e.nwheels = uint32_t(wheels.size());
    e.runs = reinterpret_cast<RunSet*>(B + o_runs); e.nruns = uint32_t(runs.size());
    e.nlinks = nlink; e.nfifos = nfifo; e.nholds = nhold; e.nroutes = nroute;
    e.nregs = nreg; e.nwires = nwire; e.nregfiles = nrf; e.ncounts = ncount;
    e.pools = reinterpret_cast<SlotPool*>(B + o_pools); e.npools = uint32_t(class_words.size());
    e.pool_allow = B + o_allow;
    for (size_t k = 0; k < class_words.size(); k++) e.pool_allow[k] = class_allow[k];
    e.leak_every = I.opt.leak_check_every;
    for (size_t k = 0; k < class_words.size(); k++) {
        S->misc_names.push_back("w" + std::to_string(class_words[k]));
        new (&e.pools[k]) SlotPool();
        e.pools[k].init(B + o_pool[k], class_words[k], uint32_t(class_cap[k]), S->misc_names.back().c_str());
    }

    // 7. the units: one arena per type (moved from staging, in the instances' order); handles rewritten in place
    S->inst.resize(n);
    std::map<uint16_t, uint32_t> store_of;
    for (uint32_t k = 0; k < n; k++) {
        auto& u = I.units[order[k]];
        auto it = store_of.find(u.type);
        if (it == store_of.end()) { it = store_of.emplace(u.type, uint32_t(S->stores.size())).first; S->stores.push_back({u.type, nullptr, 0}); }
        S->inst[k] = {it->second, S->stores[it->second].count++, u.name, u.type, u.chip};
    }
    for (auto& st : S->stores) {
        const auto& ti = *I.types[st.type];
        st.base = static_cast<unsigned char*>(alloc_big(std::max<size_t>(1, ti.size * st.count), "sim: a type's units"));
    }
    for (uint32_t k = 0; k < n; k++) {
        auto& u = I.units[order[k]];
        unsigned char* dst = S->obj(k);
        (*I.types[u.type]).move(dst, u.obj);
        ::operator delete(u.obj, std::align_val_t((*I.types[u.type]).align));
        u.obj = nullptr;
        for (uint32_t id : u.decls) {
            const Decl& d = I.decls[id];
            unsigned char* h = dst + d.offset;
            switch (d.shape) {
                case Shape::in: { InPort v{d.final, d.bit}; std::memcpy(h, &v, sizeof v); break; }
                case Shape::out: { OutPort v{d.final}; std::memcpy(h, &v, sizeof v); break; }
                case Shape::out_bit: { HoldOut v; v.route = d.final; v.bit = d.bit; std::memcpy(h, &v, sizeof v); break; }
                case Shape::idx: { WireOut v{d.final}; std::memcpy(h, &v, sizeof v); break; }
            }
        }
        S->by_name[u.name] = k;
    }

    // 8. initialise every structure
    S->unit_names.resize(n);
    for (uint32_t k = 0; k < n; k++) S->unit_names[k] = S->inst[k].name.c_str();
    for (const Decl& d : I.decls) S->port_names.push_back(d.name);
    S->route_name_s = route_names;
    S->route_names.resize(nroute);
    for (uint32_t r = 0; r < nroute; r++) S->route_names[r] = S->route_name_s[r].c_str();
    e.unit_names = S->unit_names.data();
    e.route_names = S->route_names.data();
    S->level_ports.resize(n);
    S->missed_after.assign(n, I.opt.missed_wake_after);
    for (uint32_t k = 0; k < n; k++) {
        const auto& u = I.units[order[k]];
        e.mask[k] = 0; e.edges[k] = u.edges; e.wake_at[k] = kNoCycle; e.steps[k] = 0; e.heap_pos[k] = kNone;
        auto mw = I.missed.find(u.name);
        if (mw != I.missed.end()) S->missed_after[k] = mw->second;
        for (uint32_t id : u.decls) {
            const Decl& d = I.decls[id];
            if (!d.out && (d.kind == Kind::link || d.kind == Kind::fifo || d.kind == Kind::credit || d.kind == Kind::hold))
                S->level_ports[k].push_back({d.bit, d.name});
        }
    }
    for (const auto& [nm, v] : I.missed) if (!I.names.count(nm)) FATAL("missed_wake_after: no unit named '%s'", nm.c_str());
    uint32_t next_reader = 0;
    auto add_readers = [&](const Decl& d, uint32_t& first, uint32_t& cnt) {
        first = next_reader;
        cnt = uint32_t(d.peers.size());
        for (uint32_t r : d.peers) {
            e.reader_inst[next_reader] = inst_of[I.decls[r].unit];
            e.reader_bit[next_reader] = I.decls[r].bit;
            next_reader++;
        }
    };
    for (uint32_t r = 0; r < ncount; r++) e.counts[r] = CountPort{0, 0, kNoCycle, kNone};
    for (uint32_t id = 0; id < I.decls.size(); id++) {
        const Decl& d = I.decls[id];
        const uint32_t owner = inst_of[d.unit];
        if (d.kind == Kind::count) {
            if (d.out) {
                CountPort& cp = e.counts[d.final];
                const uint32_t lv = I.units[d.unit].level;
                cp.max_producer_level = cp.max_producer_level == kNone ? lv : std::max(cp.max_producer_level, lv);
            }
            continue;
        }
        if (d.out && d.kind == Kind::wire) {
            WirePort& w = e.wires[d.final];
            w = WirePort{};
            w.v = d.reset; w.written_at = kNoCycle; w.read_at = kNoCycle; w.producer = owner; w.level = I.units[d.unit].level;
            add_readers(d, w.readers, w.nreaders);
            continue;
        }
        if (d.out && d.kind == Kind::regfile) {
            RegFilePort& r = e.regfiles[d.final];
            r = RegFilePort{};
            r.regs = reinterpret_cast<uint32_t*>(B + o_ring[id]); r.read_at = kNoCycle; r.owner = owner; r.n = d.regs;
            add_readers(d, r.readers, r.nreaders);
            continue;
        }
        if (d.out) {
            const Decl& in = I.decls[d.peers[0]];
            const WheelDef& wd = wheels[route_wheel[d.final]];
            Route& r = e.routes[d.final];
            r = Route{};
            r.wheel = uint16_t(route_wheel[d.final]);
            r.last_put = kNoCycle;
#ifdef SIM_DEBUG
            r.owner = owner;
#endif
            (void)wd;
            r.dest = d.kind == Kind::reg ? regidx[id] : in.final;
            r.latency = uint16_t(d.latency); r.width = uint8_t(d.width); r.kind = d.kind; r.cls = decl_cls[id];
            if (d.kind == Kind::reg) {
                RegPort& rp = e.regs[regidx[id]];
                rp = RegPort{};
                rp.width = uint8_t(d.width); rp.valid = false;
                add_readers(d, rp.readers, rp.nreaders);
            }
            continue;
        }
        if (d.kind == Kind::link) {
            LinkPort& l = *new (&e.links[d.final]) LinkPort{};
            l.ring.init(B + o_ring[id], d.width, Config::Impl::link_capacity(d));
            l.owner = owner; l.inflight = 0; l.cap = uint16_t(Config::Impl::link_capacity(d)); l.bit = d.bit; l.width = uint8_t(d.width);
#ifdef SIM_DEBUG
            l.last_take = kNoCycle;
#endif
            l.cls = decl_cls[id];
        } else if (d.kind == Kind::fifo || d.kind == Kind::credit) {
            FifoPort& f = *new (&e.fifos[d.final]) FifoPort{};
            f.ring.init(B + o_ring[id], d.width, d.depth);
            f.last_take = kNoCycle; f.first_overflow = kNoCycle; f.overflows = 0; f.owner = owner; f.count_idx = kNone;
            f.ovf_step = d.ovf_step ? 1 : 0;
            f.ret = kNone; f.credits = 0; f.producer = kNone; f.pbit = 0;
            f.depth = uint16_t(d.depth); f.bit = d.bit; f.width = uint8_t(d.width); f.cls = decl_cls[id];
            for (size_t k = 1; k < d.peers.size(); k++) f.count_idx = I.decls[d.peers[k]].final;   // count_overflows
            if (d.kind == Kind::credit) {
                const Decl& o = I.decls[d.peers[0]];
                f.ret = ret_route[id]; f.credits = d.depth; f.producer = inst_of[o.unit]; f.pbit = o.bit;
                Route& r = e.routes[f.ret];
                r = Route{};
                r.wheel = uint16_t(route_wheel[f.ret]);
                r.last_put = kNoCycle; r.dest = d.final;
#ifdef SIM_DEBUG
                r.owner = owner;
#endif
                r.latency = 1; r.width = 0; r.kind = Kind::cret; r.cls = kNoClass;
            }
        } else if (d.kind == Kind::hold) {
            const Decl& o = I.decls[d.peers[0]];
            HoldPort& h = *new (&e.holds[d.final]) HoldPort{};
            h.last_take = kNoCycle; h.refused_at = kNoCycle; h.refusals = 0; h.owner = owner; h.producer = inst_of[o.unit];
            h.bit = d.bit; h.pbit = o.bit; h.width = uint8_t(d.width); h.occ = false; h.cls = decl_cls[id];
        }
    }
    for (size_t w = 0; w < wheels.size(); w++) {
        Bucket* heads = reinterpret_cast<Bucket*>(B + o_bheads[w]);
        std::string nm = std::string("wheel ") + kind_name(wheels[w].kind) + " w" + std::to_string(wheels[w].width);
        S->misc_names.push_back(nm);
        const size_t per = Bucket::words_needed(wheels[w].width + 1, wheels[w].cap);
        for (unsigned b = 0; b < wheels[w].W; b++) {
            new (&heads[b]) Bucket();
            heads[b].init(B + o_bstore[w] + b * per, wheels[w].width + 1, wheels[w].cap, S->misc_names.back().c_str());
        }
        e.wheels[w] = Wheel{heads, wheels[w].W - 1, wheels[w].kind, uint8_t(wheels[w].width)};
    }
    for (size_t r = 0; r < runs.size(); r++) {
        RunSet& rs = *new (&e.runs[r]) RunSet{};
        const RunDef& rd = runs[r];
        const auto& ti = *I.types[rd.type];
        rs.fn = (I.opt.calibrate ? ti.run_cal : ti.run)[rd.form == Form::result ? 1 : 0];
        rs.one = ti.one[rd.form == Form::result ? 1 : 0];
        rs.units = S->obj(rd.first);
        rs.first = rd.first; rs.count = rd.count; rs.level = rd.level; rs.type = rd.type; rs.form = rd.form;
        S->misc_names.push_back("run set " + ti.name);
        rs.nwords = (rd.count + 63) / 64;
        rs.cur = 0;
        rs.bits[0] = B + o_bits[r];
        rs.bits[1] = B + o_bits[r] + rs.nwords;
        for (uint32_t k = rd.first; k < rd.first + rd.count; k++) e.run_of[k] = uint32_t(r);
    }
    // the run masks, the near wakes, whether delivery can go by the due masks (every latency below 64, at most 64 wheels)
    e.nrw = nrw;
    e.run_ready = B + o_rmask;
    e.run_next = B + o_rmask + nrw;
    e.niw = niw; e.nsw = nsw;
    e.near_bits = B + o_near;
    e.near_sum = B + o_near + Engine::kNear * size_t(niw);
    {
        unsigned maxlat = 0;
        for (uint32_t r = 0; r < nroute; r++) maxlat = std::max<unsigned>(maxlat, e.routes[r].latency);
        e.due_ok = maxlat < 64 && wheels.size() <= 64;
    }
    S->link_name_ptrs.resize(nlink);
    for (const Decl& d : I.decls) if (!d.out && d.kind == Kind::link) S->link_name_ptrs[d.final] = S->port_names[&d - I.decls.data()].c_str();
    e.link_names = S->link_name_ptrs.data();
    // the debug orders, step-all, the detector
    e.add_order = reinterpret_cast<uint32_t*>(B + o_addord);
    e.scratch = reinterpret_cast<uint32_t*>(B + o_scratch);
    if (orders) for (uint32_t k = 0; k < n; k++) e.add_order[k] = order[k];
    e.step_all = I.opt.step_all;
    e.instance_major = I.opt.order == Options::Order::instance_major;
    e.shuffle = I.opt.shuffle_seed ? hash_mix(I.opt.shuffle_seed, 0x5eed) | 1 : 0;
    e.watch = reinterpret_cast<Engine::Watch*>(B + o_watch);
    e.watch_first = reinterpret_cast<uint32_t*>(B + o_wfirst);
    e.missed_after = reinterpret_cast<uint32_t*>(B + o_mwafter);
    e.suspect_since = reinterpret_cast<Cycle*>(B + o_since);
    e.suspects = reinterpret_cast<uint32_t*>(B + o_susp);
    e.nsuspects = 0;
    e.missed_fatal = I.opt.missed_wake_fatal;
    if (kDebug) {
        uint32_t w = 0;
        bool any = false;
        for (uint32_t k = 0; k < n; k++) {
            e.watch_first[k] = w;
            e.missed_after[k] = S->missed_after[k];
            e.suspect_since[k] = kNoCycle;
            any = any || e.missed_after[k];
            for (uint32_t id : I.units[order[k]].decls) {
                const Decl& d = I.decls[id];
                if (!d.out && (d.kind == Kind::link || d.kind == Kind::fifo || d.kind == Kind::credit || d.kind == Kind::hold))
                    e.watch[w++] = Engine::Watch{d.kind, d.final, S->port_names[id].c_str()};
                else if (d.out && d.kind == Kind::hold)
                    e.watch[w++] = Engine::Watch{Kind::hold, d.final, nullptr};                     // its route: a put this cycle
                else if (d.out && d.kind == Kind::credit)
                    e.watch[w++] = Engine::Watch{Kind::cret, I.decls[d.peers[0]].final, nullptr};    // its fifo's returns
            }
        }
        e.watch_first[n] = w;
        e.detector = kDebug && !I.opt.step_all && any;
    }
    e.bind();
    for (uint32_t k = 0; k < n; k++) e.mark(k);   // every unit is stepped at the first cycle (sim.h)

    // 9. sizes, by structure, by type, per chip
    Sizes& z = S->sizes;
    std::map<std::string, SizeLine> by_s, by_t;
    std::map<unsigned, uint64_t> by_chip;
    for (auto& it : c.items) S->carved.push_back({it.what, it.off, it.words, it.type});   // W10 (Internals::regions)
    for (auto& it : c.items) {
        SizeLine& l = by_s[it.what];
        l.what = it.what; l.words += it.words; l.count += it.count;
        if (it.type >= 0) { SizeLine& t = by_t["ports of " + tname(uint16_t(it.type))]; t.what = "ports of " + tname(uint16_t(it.type)); t.words += it.words; t.count += it.count; }
        if (it.chip >= 0) by_chip[unsigned(it.chip)] += it.words;
    }
    uint64_t unit_words = 0;
    for (auto& st : S->stores) {
        const auto& ti = *I.types[st.type];
        uint64_t w = (ti.size * st.count + 7) / 8;
        SizeLine& t = by_t["unit " + ti.name];
        t.what = "unit " + ti.name; t.words += w; t.count += st.count;
        unit_words += w;
    }
    for (uint32_t k = 0; k < n; k++) by_chip[S->inst[k].chip] += (I.types[S->inst[k].type]->size + 7) / 8;
    for (auto& [k, v] : by_s) z.by_structure.push_back(v);
    for (auto& [k, v] : by_t) z.by_type.push_back(v);
    z.total_words = c.total + unit_words;
    z.chips = unsigned(by_chip.size() ? by_chip.size() : 1);
    for (auto& [k, v] : by_chip) z.per_chip.push_back(v);
    z.words_per_chip = z.total_words / z.chips;
    z.wheel_buckets = 0;
    for (auto& w : wheels) z.wheel_buckets = std::max(z.wheel_buckets, w.W);
    detail::Internals::record_view(I, inst_of, S->view);       // W4 (E5): the digest, unit by unit
    if (I.opt.calibrate) {                                     // W10: time per instance, for Sim::load_profile
        S->run_ticks_v.assign(e.ninst, 0);
        e.inst_ticks = S->run_ticks_v.data();
        S->link_words_v.assign(e.nlinks, 0);
        e.link_words = S->link_words_v.data();
        S->cal_ticks0 = sim_ticks();
        S->cal_ns0 = double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    if (I.opt.observe_arrivals && e.nlinks) {                  // W10: one arrival cycle per Link ring slot
        S->link_at_off_v.resize(e.nlinks);
        size_t tot = 0;
        for (uint32_t k = 0; k < e.nlinks; k++) { S->link_at_off_v[k] = uint32_t(tot); tot += e.links[k].ring.depth; }
        if (tot > 0xFFFFFFFFull) FATAL("observe_arrivals: more than 2^32 link slots");
        S->link_at_v.assign(tot, 0);
        e.link_at = S->link_at_v.data();
        e.link_at_off = S->link_at_off_v.data();
    }
    return Sim(S);
}

Sim::Sim(Sim&& o) noexcept : impl_(o.impl_) {
    o.impl_ = nullptr;
    if (impl_) impl_->e.bind();
}
Sim& Sim::operator=(Sim&& o) noexcept {
    if (this != &o) { delete impl_; impl_ = o.impl_; o.impl_ = nullptr; }
    return *this;
}
Sim::~Sim() { delete impl_; }

// W4 (E3): a machine split over domains forwards each operation to its threads (threads.h; its run_until(end) runs the
// cycles before `end`, so these keep W3's meaning: run(n) runs n cycles, run_until(t) runs through t inclusive).
// W10: a run clears the stop request and marks itself running (the observer refuses while it is).
namespace {
struct Running {
    detail::Internals::SImpl* s;
    explicit Running(detail::Internals::SImpl* i) : s(i) {
        if (s->running) FATAL("Sim::run inside a run");
        s->running = true;
        if (s->threads) for (auto& d : s->threads->doms) d.e->stop_at = kNoCycle;
        else s->e.stop_at = kNoCycle;
    }
    ~Running() { s->running = false; }
};
}  // namespace
void Sim::run(Cycle n) {
    Running r(impl_);
    if (impl_->threads) return impl_->threads->run_until(impl_->threads->now() + n);
    if (n) impl_->e.run_until(impl_->e.now + n - 1);
}
void Sim::run_until(Cycle t) {
    Running r(impl_);
    if (impl_->threads) return impl_->threads->run_until(t + 1);
    impl_->e.run_until(t);
}
// W10: each instance's load: the time its steps took in calibrating runs.
namespace {
void profile_of(const detail::Internals::SImpl& S, LoadProfile& p) {
    const Engine& e = S.e;
    if (!e.inst_ticks) FATAL("Sim::load_profile: the machine wasn't built with Options::calibrate");
    for (uint32_t i = 0; i < e.ninst; i++) {
        if (e.unit_names[i][0] == '~') continue;           // a split's boundary units are the framework's, not the machine's
        p.unit.push_back(e.unit_names[i]);
        p.load.push_back(double(e.inst_ticks[i]));
    }
    for (uint32_t k = 0; k < e.nlinks; k++) {
        if (!e.link_words[k] || e.link_names[k][0] == '~') continue;
        p.link.push_back(e.link_names[k]);
        p.words.push_back(double(e.link_words[k]));
    }
    const double ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) - S.cal_ns0;
    if (ns > 0) p.ticks_per_ns = double(sim_ticks() - S.cal_ticks0) / ns;
}
}  // namespace
LoadProfile Sim::load_profile() const {
    LoadProfile p;
    p.cycles = now() - 1;
    if (impl_->threads) { for (auto& d : impl_->threads->doms) profile_of(*d.S, p); }
    else profile_of(*impl_, p);
    return p;
}
std::string LoadProfile::to_text() const {
    char b[64];
    std::snprintf(b, sizeof b, "%.17g", ticks_per_ns);
    std::string t = "loadprofile v2\ncycles " + std::to_string(cycles) + "\nticks_per_ns " + b + "\n";
    for (size_t k = 0; k < unit.size(); k++) { std::snprintf(b, sizeof b, " %.17g\n", load[k]); t += "unit " + unit[k] + b; }
    for (size_t k = 0; k < link.size(); k++) { std::snprintf(b, sizeof b, " %.17g\n", words[k]); t += "link " + link[k] + b; }
    return t;
}
LoadProfile LoadProfile::from_text(const std::string& text) {
    LoadProfile p;
    size_t pos = 0, line = 0;
    int version = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string l = text.substr(pos, end - pos);
        pos = end + 1;
        line++;
        if (l.empty()) continue;
        if (line == 1) {
            if (l == "loadprofile v1") version = 1;
            else if (l == "loadprofile v2") version = 2;
            else FATAL("load profile line 1: '%s', expected 'loadprofile v1' or 'v2'", l.c_str());
            continue;
        }
        if (line == 2) {
            if (l.rfind("cycles ", 0) != 0) FATAL("load profile line 2: '%s', expected 'cycles N'", l.c_str());
            p.cycles = std::strtoull(l.c_str() + 7, nullptr, 10);
            continue;
        }
        if (version == 2 && l.rfind("ticks_per_ns ", 0) == 0) { p.ticks_per_ns = std::strtod(l.c_str() + 13, nullptr); continue; }
        std::string rest = l;
        bool is_link = false;
        if (version == 2) {
            if (l.rfind("unit ", 0) == 0) rest = l.substr(5);
            else if (l.rfind("link ", 0) == 0) { rest = l.substr(5); is_link = true; }
            else FATAL("load profile line %zu: '%s', expected 'unit NAME LOAD' or 'link NAME WORDS'", line, l.c_str());
        }
        const size_t sp = rest.rfind(' ');
        char* e = nullptr;
        const double v = sp == std::string::npos ? 0 : std::strtod(rest.c_str() + sp + 1, &e);
        if (sp == std::string::npos || sp == 0 || !e || *e) FATAL("load profile line %zu: '%s', expected 'name load'", line, l.c_str());
        if (is_link) { p.link.push_back(rest.substr(0, sp)); p.words.push_back(v); }
        else { p.unit.push_back(rest.substr(0, sp)); p.load.push_back(v); }
    }
    if (line < 2) FATAL("load profile: no header");
    return p;
}
std::string Sim::thread_times() const { return impl_->threads ? impl_->threads->thread_times() : std::string("one domain"); }
void Sim::wake(const std::string& name) {
    if (impl_->running) FATAL("Sim::wake('%s') during a run: between runs only", name.c_str());
    if (impl_->threads) return impl_->threads->wake(name);
    auto it = impl_->by_name.find(name);
    if (it == impl_->by_name.end()) FATAL("Sim::wake: no unit named '%s'", name.c_str());
    impl_->e.mark(it->second);       // ready for the next cycle's run phase (nothing clears it before)
}
// W20 (harness-only; P2): Sim::wake_all, Sim::in_flight (config.h).
bool Sim::in_flight_into(const std::string& prefix) const {
    if (impl_->running) FATAL("Sim::in_flight_into during a run: between runs only");
    if (impl_->threads) FATAL("Sim::in_flight_into: one domain only (the harness's machines run one thread)");
    const Engine& e = impl_->e;
    auto ours = [&](uint32_t k) { return impl_->inst[k].name.compare(0, prefix.size(), prefix) == 0; };
    for (uint32_t k = 0; k < e.ninst; k++) if ((e.mask[k] & ~e.edges[k]) && ours(k)) return true;
    for (uint32_t wi = 0; wi < e.nwheels; wi++) {
        const Wheel& wh = e.wheels[wi];
        if (wh.kind != Kind::link && wh.kind != Kind::fifo && wh.kind != Kind::hold) continue;   // (credit returns: state)
        for (uint32_t b = 0; b <= wh.mask; b++) {
            const Bucket& bk = wh.buckets[b];
            const Word* p = bk.begin();
            for (uint32_t m = bk.count(); m; m--, p += wh.width + 1) {
                const uint32_t o = wh.kind == Kind::link ? e.links[p[0]].owner : wh.kind == Kind::fifo ? e.fifos[p[0]].owner : e.holds[p[0]].owner;
                if (ours(o)) return true;
            }
        }
    }
    return false;
}
uint64_t Sim::hold_refusals(HoldIn p) const {
    if (impl_->threads) FATAL("Sim::hold_refusals: one domain only (the harness's machines run one thread)");
    return impl_->e.holds[p.idx].refusals;
}
uint64_t Sim::fifo_overflows(FifoIn p, Cycle* first) const {
    if (impl_->threads) FATAL("Sim::fifo_overflows: one domain only (the harness's machines run one thread)");
    const auto& f = impl_->e.fifos[p.idx];
    if (first) *first = f.first_overflow;
    return f.overflows;
}
uint32_t Sim::fifo_peak(FifoIn p) const {
#ifdef SIM_CHECKS
    if (impl_->threads) FATAL("Sim::fifo_peak: one domain only (the harness's machines run one thread)");
    return impl_->e.fifos[p.idx].peak;
#else
    (void)p;
    FATAL("Sim::fifo_peak: Fifo peaks are kept only under SIM_CHECKS (the debug and suite builds)");
#endif
}
void Sim::fifo_restart_peak(FifoIn p) {
#ifdef SIM_CHECKS
    if (impl_->threads) FATAL("Sim::fifo_restart_peak: one domain only (the harness's machines run one thread)");
    auto& f = impl_->e.fifos[p.idx];
    f.peak = f.ring.count();
#else
    (void)p;
    FATAL("Sim::fifo_restart_peak: Fifo peaks are kept only under SIM_CHECKS (the debug and suite builds)");
#endif
}
void Sim::put_late(const std::string& unit, HoldOut p, const Word* w, unsigned n) {
    if (impl_->running) FATAL("Sim::put_late('%s') during a run: between runs only", unit.c_str());
    if (impl_->threads) FATAL("Sim::put_late('%s'): one domain only (the harness's machines run one thread)", unit.c_str());
    auto it = impl_->by_name.find(unit);
    if (it == impl_->by_name.end()) FATAL("Sim::put_late: no unit named '%s'", unit.c_str());
    Engine& e = impl_->e;
    if (e.now == 0) FATAL("Sim::put_late('%s'): before the machine's first cycle", unit.c_str());
    Route& r = e.routes[p.route];
#ifdef SIM_DEBUG
    if (r.owner != it->second) FATAL("Sim::put_late: the Hold isn't '%s''s", unit.c_str());
#endif
    if (r.kind != Kind::hold || r.cls != kNoClass || r.width != n)
        FATAL("Sim::put_late('%s'): a Hold of width %u by value (this one: width %u)", unit.c_str(), n, unsigned(r.width));
    const Cycle t = e.now - 1;                          // the cycle just run: the put is that cycle's
    if (r.last_put == t) FATAL("Sim::put_late('%s'): the unit put on this Hold in cycle %llu already (two puts)", unit.c_str(),
                               (unsigned long long)t);
    r.last_put = t;
    const Wheel& wh = e.wheels[r.wheel];
    const Cycle at = t + r.latency;
    Bucket& b = wh.buckets[at & wh.mask];
    if (b.empty() && e.due_ok) e.due[at & 63] |= uint64_t(1) << r.wheel;
    Word* x = b.append();
    x[0] = r.dest;
    for (unsigned i = 0; i < n; i++) x[1 + i] = w[i];
    e.pending++;
}
void Sim::put_late(const std::string& unit, LinkOut p, const Word* w, unsigned n) {
    if (impl_->running) FATAL("Sim::put_late('%s') during a run: between runs only", unit.c_str());
    if (impl_->threads) FATAL("Sim::put_late('%s'): one domain only (the harness's machines run one thread)", unit.c_str());
    auto it = impl_->by_name.find(unit);
    if (it == impl_->by_name.end()) FATAL("Sim::put_late: no unit named '%s'", unit.c_str());
    Engine& e = impl_->e;
    if (e.now == 0) FATAL("Sim::put_late('%s'): before the machine's first cycle", unit.c_str());
    Route& r = e.routes[p.route];
#ifdef SIM_DEBUG
    if (r.owner != it->second) FATAL("Sim::put_late: the Hold isn't '%s''s", unit.c_str());
#endif
    if (r.kind != Kind::link || r.cls != kNoClass || r.width != n)
        FATAL("Sim::put_late('%s'): a Link of width %u by value (this one: width %u)", unit.c_str(), n, unsigned(r.width));
    const Cycle t = e.now - 1;                          // the cycle just run: the put is that cycle's
    if (r.last_put == t) FATAL("Sim::put_late('%s'): the unit put on this Hold in cycle %llu already (two puts)", unit.c_str(),
                               (unsigned long long)t);
    r.last_put = t;
    const Wheel& wh = e.wheels[r.wheel];
    const Cycle at = t + r.latency;
    Bucket& b = wh.buckets[at & wh.mask];
    if (b.empty() && e.due_ok) e.due[at & 63] |= uint64_t(1) << r.wheel;
    Word* x = b.append();
    x[0] = r.dest;
    for (unsigned i = 0; i < n; i++) x[1 + i] = w[i];
    e.pending++;
}
void Sim::wake_all() {
    if (impl_->running) FATAL("Sim::wake_all during a run: between runs only");
    if (impl_->threads) return impl_->threads->wake_all();
    for (uint32_t k = 0; k < impl_->e.ninst; k++) impl_->e.mark(k);
}
bool Sim::in_flight(const std::vector<uint16_t>& shifted, const std::vector<uint16_t>& ignored) const {
    if (impl_->running) FATAL("Sim::in_flight during a run: between runs only");
    if (impl_->threads) return impl_->threads->in_flight(shifted, ignored);
    const Engine& e = impl_->e;
    auto in = [](const std::vector<uint16_t>& v, uint16_t ty) { for (uint16_t x : v) if (x == ty) return true; return false; };
    for (uint32_t k = 0; k < e.ninst; k++)
        if ((e.mask[k] & ~e.edges[k]) && !in(ignored, impl_->inst[k].type)) return true;
    for (uint32_t wi = 0; wi < e.nwheels; wi++) {
        const Wheel& wh = e.wheels[wi];
        if (wh.kind != Kind::link && wh.kind != Kind::fifo && wh.kind != Kind::credit && wh.kind != Kind::hold) continue;
        for (uint32_t b = 0; b <= wh.mask; b++) {
            const Bucket& bk = wh.buckets[b];
            if (!bk.count()) continue;
            if (wh.kind != Kind::link || (shifted.empty() && ignored.empty())) return true;
            const bool now = b == (e.now & wh.mask);
            const Word* p = bk.begin();                  // a Link's bucket: words into ignored units, and into shifted
            for (uint32_t m = bk.count(); m; m--, p += wh.width + 1) {   // units at now(), don't count
                const uint16_t ty = impl_->inst[e.links[p[0]].owner].type;
                if (!in(ignored, ty) && !(now && in(shifted, ty))) return true;
            }
        }
    }
    return false;
}
Cycle Sim::stop_cycle() const {
    if (!impl_->threads) return impl_->e.stop_at;
    Cycle c = kNoCycle;
    for (auto& d : impl_->threads->doms) c = std::min(c, d.e->stop_at);
    return c;
}
Cycle Sim::now() const { return impl_->threads ? impl_->threads->now() : impl_->e.now; }
const Sizes& Sim::sizes() const { return impl_->threads ? impl_->threads->sizes() : impl_->sizes; }
std::string Sim::partition() const { return impl_->threads ? impl_->threads->partition() : std::string("one domain"); }

uint32_t Sim::regfile_value(RegFileIn p, unsigned i) const {   // W20 (harness-only)
    if (impl_->running) FATAL("Sim::regfile_value during a run: between runs only");
    if (impl_->threads) FATAL("Sim::regfile_value: one domain only");
    const RegFilePort& r = impl_->e.regfiles[p.idx];
    if (i >= r.n) FATAL("Sim::regfile_value: register %u of %u", i, r.n);
    return r.regs[i];
}
bool Sim::has_unit(const std::string& name) const {      // W20 (harness-only)
    if (impl_->threads) FATAL("Sim::has_unit: one domain only");
    return impl_->by_name.count(name) != 0;
}
uint16_t Sim::type_of(const std::string& name) const {
    if (impl_->threads) return impl_->threads->type_of(name);
    auto it = impl_->by_name.find(name);
    if (it == impl_->by_name.end()) FATAL("no unit named '%s'", name.c_str());
    return impl_->inst[it->second].type;
}
void* Sim::unit_ptr(const std::string& name, const detail::TypeInfo* type) const {
    if (impl_->threads) return impl_->threads->unit_ptr(name, type);
    auto it = impl_->by_name.find(name);
    if (it == impl_->by_name.end()) FATAL("no unit named '%s'", name.c_str());
    if (impl_->types[impl_->inst[it->second].type] != type)
        FATAL("unit '%s' is a %s, not a %s", name.c_str(), impl_->types[impl_->inst[it->second].type]->name.c_str(),
              type->name.c_str());
    return impl_->obj(it->second);
}
uint64_t Sim::steps(const std::string& name) const {
    if (impl_->threads) return impl_->threads->steps(name);
    auto it = impl_->by_name.find(name);
    if (it == impl_->by_name.end()) FATAL("no unit named '%s'", name.c_str());
    return impl_->e.steps[it->second];
}

// W4 (E5): unit by unit in add order, wide payloads by their contents (digest.h).
uint64_t Sim::digest() const {
    if (impl_->threads) return impl_->threads->digest();
    const Sim::Impl* one = impl_;
    return detail::Internals::digest(impl_->view, &one, impl_->e.now);
}

std::string Sim::sleepers() const {
    if (impl_->threads) return impl_->threads->sleepers();
    const Engine& e = impl_->e;
    std::string out;
    for (uint32_t k = 0; k < e.ninst; k++) {
        uint64_t data = e.mask[k] & ~e.edges[k];
        if (!data) continue;
        const RunSet& r = e.runs[e.run_of[k]];
        if (RunSet::test(r.ready(), k - r.first) || e.wake_at[k] != kNoCycle) continue;   // (between cycles: next cycle's set is current)
        out += impl_->inst[k].name + ": asleep with data waiting in";
        for (auto& [bit, nm] : impl_->level_ports[k]) if (data >> bit & 1) out += " " + nm;
        out += "\n";
    }
    return out;
}

}  // namespace sim
