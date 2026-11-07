// config.cpp: building a machine (WORKER3, stage 2). The configuration regime (D6): any C++.
//
// Increment 2: Link, Fifo and Hold are built; the other kinds are declared and wired, and build() refuses them by name
// until increment 3.
#include "config.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>

#include "base/alloc_guard.h"
#include "base/arena.h"

namespace sim {

// ---- the type registry (process-wide; ids are small integers, no RTTI) ---------------------------------------------
namespace detail {
static std::vector<TypeInfo>& registry() {
    static std::vector<TypeInfo> r;
    return r;
}
uint16_t register_type(TypeInfo&& t) {
    auto& r = registry();
    if (r.size() >= 0xFFFF) FATAL("too many unit types");
    r.push_back(std::move(t));
    return uint16_t(r.size() - 1);
}
const TypeInfo& type_info(uint16_t id) { return registry()[id]; }
size_t registry_size() { return registry().size(); }
}  // namespace detail

namespace {
const char* kind_name(Kind k) {
    static const char* n[] = { "link", "fifo", "credit fifo", "hold", "reg", "wire", "regfile", "count" };
    return n[unsigned(k)];
}
enum class Shape : uint8_t { in, out, out_bit, idx };
}  // namespace

// ---- the configuration's state ---------------------------------------------------------------------------------------
struct Config::Impl {
    struct Decl {
        uint32_t unit;
        Kind kind;
        bool out;
        Shape shape;
        uint8_t bit = 0xFF;
        std::string name;               // "unit.port"
        unsigned width = 0, depth = 0, latency = 0, regs = 0;
        Word reset = 0;
        size_t offset = 0;
        std::vector<uint32_t> peers;
        uint32_t final = kNone;         // the pool index (inputs) or the route (outputs)
    };
    struct Staged {
        uint16_t type;
        Form form;
        std::string name;
        unsigned chip = 0, domain = 0;
        unsigned char* obj = nullptr;
        std::vector<uint32_t> decls;
        unsigned nbits = 0;
        uint64_t edges = 0;
        uint32_t level = 0;
    };

    Options opt;
    std::vector<Staged> units;
    std::vector<Decl> decls;
    std::unordered_map<std::string, uint32_t> names;
    unsigned chip = 0, domain = 0;
    int cur = -1;
    Ports* ports = nullptr;
    bool built = false;

    Decl& decl(uint32_t id, const char* what) {
        if (id >= decls.size()) FATAL("%s: not a port handle of this configuration (%u)", what, id);
        return decls[id];
    }
    uint32_t declare(const void* h, Kind k, bool out, Shape shape, const char* name, unsigned width, bool bit, bool edge) {
        if (cur < 0) FATAL("a port declared outside a unit's constructor ('%s')", name);
        Staged& u = units[size_t(cur)];
        const auto& ti = detail::type_info(u.type);
        const unsigned char* p = static_cast<const unsigned char*>(h);
        if (p < u.obj || p >= u.obj + ti.size) FATAL("unit '%s': port '%s' is not a member of the unit", u.name.c_str(), name);
        if (width > 3) FATAL("unit '%s': port '%s' of width %u (at most 3 words, D17)", u.name.c_str(), name, width);
        Decl d;
        d.unit = uint32_t(cur);
        d.kind = k;
        d.out = out;
        d.shape = shape;
        d.name = u.name + "." + name;
        d.width = width;
        d.offset = size_t(p - u.obj);
        if (bit) {
            if (u.nbits == 64) FATAL("unit '%s': more than 64 ready bits (port '%s')", u.name.c_str(), name);
            d.bit = uint8_t(u.nbits++);
            if (edge) u.edges |= uint64_t(1) << d.bit;
        }
        decls.push_back(std::move(d));
        u.decls.push_back(uint32_t(decls.size() - 1));
        return uint32_t(decls.size() - 1);
    }
    void wire(uint32_t o, uint32_t i, Kind k, unsigned latency, bool fan_out, const char* what) {
        Decl& O = decl(o, what);
        Decl& I = decl(i, what);
        if (O.kind != k || !O.out) FATAL("%s: '%s' is not a %s output", what, O.name.c_str(), kind_name(k));
        if (I.kind != k || I.out) FATAL("%s: '%s' is not a %s input", what, I.name.c_str(), kind_name(k));
        if (O.width != I.width) FATAL("%s: '%s' (width %u) to '%s' (width %u)", what, O.name.c_str(), O.width, I.name.c_str(), I.width);
        if (!I.peers.empty()) FATAL("%s: '%s' is already wired (from '%s')", what, I.name.c_str(), decls[I.peers[0]].name.c_str());
        if (!fan_out && !O.peers.empty()) FATAL("%s: '%s' is already wired (to '%s')", what, O.name.c_str(), decls[O.peers[0]].name.c_str());
        if (latency < 1 || latency > 0x8000) FATAL("%s: '%s' latency %u (1 to 32768)", what, O.name.c_str(), latency);
        O.peers.push_back(i);
        I.peers.push_back(o);
        O.latency = I.latency = latency;
    }
};

// ---- Ports: a unit's declarations ------------------------------------------------------------------------------------
#define IMPL (*static_cast<Config::Impl*>(impl_))
void Ports::in(LinkIn& h, const char* name, unsigned width) {
    h.idx = IMPL.declare(&h, Kind::link, false, Shape::in, name, width, true, false);
    h.bit = IMPL.decls[h.idx].bit;
}
void Ports::in(FifoIn& h, const char* name, unsigned width, unsigned depth) {
    if (width < 1 || depth < 1 || depth > 0xFFFF) FATAL("fifo '%s': width %u and depth %u (1-3, 1-65535)", name, width, depth);
    h.idx = IMPL.declare(&h, Kind::fifo, false, Shape::in, name, width, true, false);
    h.bit = IMPL.decls[h.idx].bit;
    IMPL.decls[h.idx].depth = depth;
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
#undef IMPL

// ---- Config ----------------------------------------------------------------------------------------------------------
Config::Config(Options o) : impl_(new Impl) {
    impl_->opt = o;
    impl_->ports = new Ports();
    impl_->ports->impl_ = impl_;
}
Config::~Config() {
    for (auto& u : impl_->units)
        if (u.obj) { detail::type_info(u.type).destroy(u.obj); ::operator delete(u.obj, std::align_val_t(detail::type_info(u.type).align)); }
    delete impl_->ports;
    delete impl_;
}
void Config::domain(unsigned d) { impl_->domain = d; }
void Config::chip(unsigned c) { impl_->chip = c; }

void* Config::stage(uint16_t type, size_t size, size_t align, Form f, const std::string& name) {
    Impl& I = *impl_;
    if (I.built) FATAL("unit '%s' added after build()", name.c_str());
    if (I.cur >= 0) FATAL("unit '%s' added inside another unit's constructor", name.c_str());
    if (name.empty()) FATAL("a unit needs a name");
    if (!I.names.emplace(name, uint32_t(I.units.size())).second) FATAL("two units named '%s'", name.c_str());
    if (f == Form::result && !detail::type_info(type).run[1]) FATAL("unit '%s': its type has no result form", name.c_str());
    Impl::Staged u;
    u.type = type;
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
void Config::count_overflows(FifoIn f, CountIn c) {
    Impl::Decl& F = impl_->decl(f.idx, "count_overflows");
    Impl::Decl& C = impl_->decl(c.idx, "count_overflows");
    if (F.kind != Kind::fifo || F.out || C.kind != Kind::count || C.out) FATAL("count_overflows: '%s', '%s'", F.name.c_str(), C.name.c_str());
    F.peers.push_back(c.idx);   // a fifo input's second peer: its count (increment 3)
    C.peers.push_back(f.idx);
}

// ---- Sim ---------------------------------------------------------------------------------------------------------------
struct Sim::Impl {
    Engine e;
    std::unique_ptr<Block> block;
    struct Store { uint16_t type; unsigned char* base; uint32_t count; };
    std::vector<Store> stores;
    struct Inst { uint32_t store, index; std::string name; uint16_t type; unsigned chip; };
    std::vector<Inst> inst;
    std::unordered_map<std::string, uint32_t> by_name;
    std::vector<std::string> route_name_s;
    std::vector<const char*> unit_names, route_names;
    std::deque<std::string> port_names;                // ring, bucket and run names: stable addresses (W1's structures keep pointers)
    std::vector<std::vector<std::pair<uint8_t, std::string>>> level_ports;   // per instance: (bit, port name) of its level inputs
    Sizes sizes;
    unsigned char* obj(uint32_t i) const {
        const Store& s = stores[inst[i].store];
        return s.base + size_t(inst[i].index) * detail::type_info(s.type).size;
    }
    ~Impl() {
        for (auto& s : stores) {
            const auto& ti = detail::type_info(s.type);
            for (uint32_t k = 0; k < s.count; k++) ti.destroy(s.base + size_t(k) * ti.size);
            free_big(s.base);
        }
    }
};

namespace {
// The carving: every structure's words, named, then one block and the pointers.
struct Carver {
    struct Item { std::string what; size_t words; uint32_t count; int type; int chip; size_t off; };
    std::vector<Item> items;
    size_t total = 0;
    size_t add(std::string what, size_t words, uint32_t count = 1, int type = -1, int chip = -1) {
        words = round_to_lines(words);
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
    I.built = true;
    using Decl = Impl::Decl;

    // 1. every port wired; kinds of this increment
    for (const Decl& d : I.decls) {
        if (d.peers.empty() && !(d.kind == Kind::count)) FATAL("port '%s' (%s %s) is not wired", d.name.c_str(), kind_name(d.kind), d.out ? "output" : "input");
        if (d.kind != Kind::link && d.kind != Kind::fifo && d.kind != Kind::hold)
            FATAL("port '%s': %s ports come with increment 3", d.name.c_str(), kind_name(d.kind));
    }

    // 2. the instances' order: (level, type in first-add order, form), then add order; runs are its contiguous groups
    std::vector<uint16_t> type_rank(detail::registry_size(), 0xFFFF);
    uint16_t nt = 0;
    for (auto& u : I.units) if (type_rank[u.type] == 0xFFFF) type_rank[u.type] = nt++;
    std::vector<uint32_t> order(I.units.size());
    for (uint32_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const auto &A = I.units[a], &B = I.units[b];
        if (A.level != B.level) return A.level < B.level;
        if (type_rank[A.type] != type_rank[B.type]) return type_rank[A.type] < type_rank[B.type];
        return A.form < B.form;
    });
    const uint32_t n = uint32_t(order.size());
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

    // 3. numbering: inputs per kind, consumer type-major then instance; outputs as routes in the same order
    uint32_t nlink = 0, nfifo = 0, nhold = 0, nroute = 0;
    for (uint32_t k = 0; k < n; k++)
        for (uint32_t id : I.units[order[k]].decls) {
            Decl& d = I.decls[id];
            if (d.out) { d.final = nroute++; continue; }
            if (d.kind == Kind::link) d.final = nlink++;
            else if (d.kind == Kind::fifo) d.final = nfifo++;
            else if (d.kind == Kind::hold) d.final = nhold++;
        }
    std::vector<uint32_t> route_decl(nroute);
    for (uint32_t id = 0; id < I.decls.size(); id++) if (I.decls[id].out) route_decl[I.decls[id].final] = id;

    // 4. wheels: one per (kind, width) with routes on it; W above its largest latency; a bucket holds one put per route
    struct WheelDef { Kind kind; unsigned width, W, cap; };
    std::vector<WheelDef> wheels;
    std::map<std::pair<unsigned, unsigned>, uint32_t> wheel_of;
    std::vector<uint32_t> route_wheel(nroute);
    for (uint32_t r = 0; r < nroute; r++) {
        const Decl& d = I.decls[route_decl[r]];
        auto key = std::make_pair(unsigned(d.kind), d.width);
        auto it = wheel_of.find(key);
        if (it == wheel_of.end()) { it = wheel_of.emplace(key, uint32_t(wheels.size())).first; wheels.push_back({d.kind, d.width, 2, 0}); }
        WheelDef& w = wheels[it->second];
        w.W = std::max(w.W, pow2_above(d.latency));
        w.cap++;
        route_wheel[r] = it->second;
    }

    // 5. sizing: every structure's words, by structure, type and chip (D15)
    Carver c;
    auto tname = [&](uint16_t t) { return detail::type_info(t).name; };
    const size_t o_mask = c.add("instances: ready masks", n), o_edges = c.add("instances: edge bits", n),
                 o_wake = c.add("instances: wake cycles", n), o_steps = c.add("instances: step counts", n),
                 o_runof = c.add("instances: run of", (n + 1) / 2), o_hpos = c.add("instances: heap places", (n + 1) / 2),
                 o_heap = c.add("far wakes (heap)", (n + 1) / 2);
    const size_t o_links = c.add_array<LinkPort>("link ports", nlink), o_fifos = c.add_array<FifoPort>("fifo ports", nfifo),
                 o_holds = c.add_array<HoldPort>("hold ports", nhold), o_routes = c.add_array<Route>("routes", nroute);
    std::vector<size_t> o_ring(I.decls.size(), 0);
    for (uint32_t id = 0; id < I.decls.size(); id++) {
        const Decl& d = I.decls[id];
        if (d.out) continue;
        const auto& u = I.units[d.unit];
        if (d.kind == Kind::link)
            o_ring[id] = c.add("ring link w" + std::to_string(d.width), Ring::words_needed(std::max(1u, d.width), d.latency + 2), 1, u.type, int(u.chip));
        else if (d.kind == Kind::fifo)
            o_ring[id] = c.add("ring fifo w" + std::to_string(d.width), Ring::words_needed(d.width, d.depth), 1, u.type, int(u.chip));
    }
    const size_t o_wheels = c.add_array<Wheel>("wheels: headers", wheels.size());
    std::vector<size_t> o_bheads(wheels.size()), o_bstore(wheels.size());
    for (size_t w = 0; w < wheels.size(); w++) {
        std::string nm = std::string("wheel ") + kind_name(wheels[w].kind) + " w" + std::to_string(wheels[w].width);
        o_bheads[w] = c.add_array<Bucket>(nm + ": buckets", wheels[w].W);
        o_bstore[w] = c.add(nm, wheels[w].W * Bucket::words_needed(wheels[w].width + 1, wheels[w].cap), wheels[w].W);
    }
    const size_t o_runs = c.add_array<RunSet>("runs", runs.size());
    std::vector<size_t> o_bits(runs.size());
    for (size_t r = 0; r < runs.size(); r++) o_bits[r] = c.add("run sets", 2 * Bitmap::words_needed(runs[r].count), runs[r].count);

    auto* S = new Sim::Impl;
    S->block = std::make_unique<Block>(c.total ? c.total : 8, "sim: the thread's block");
    uint64_t* B = S->block->words();
    Engine& e = S->e;
    e.ninst = n;
    e.mask = B + o_mask; e.edges = B + o_edges; e.wake_at = B + o_wake; e.steps = B + o_steps;
    e.run_of = reinterpret_cast<uint32_t*>(B + o_runof); e.heap_pos = reinterpret_cast<uint32_t*>(B + o_hpos);
    e.heap = reinterpret_cast<uint32_t*>(B + o_heap);
    e.links = reinterpret_cast<LinkPort*>(B + o_links); e.fifos = reinterpret_cast<FifoPort*>(B + o_fifos);
    e.holds = reinterpret_cast<HoldPort*>(B + o_holds); e.routes = reinterpret_cast<Route*>(B + o_routes);
    e.wheels = reinterpret_cast<Wheel*>(B + o_wheels); e.nwheels = uint32_t(wheels.size());
    e.runs = reinterpret_cast<RunSet*>(B + o_runs); e.nruns = uint32_t(runs.size());
    e.nlinks = nlink; e.nfifos = nfifo; e.nholds = nhold; e.nroutes = nroute;

    // 6. the units: one arena per type (moved from staging, in the instances' order); handles rewritten in place
    S->inst.resize(n);
    std::map<uint16_t, uint32_t> store_of;
    for (uint32_t k = 0; k < n; k++) {
        auto& u = I.units[order[k]];
        auto it = store_of.find(u.type);
        if (it == store_of.end()) { it = store_of.emplace(u.type, uint32_t(S->stores.size())).first; S->stores.push_back({u.type, nullptr, 0}); }
        S->inst[k] = {it->second, S->stores[it->second].count++, u.name, u.type, u.chip};
    }
    for (auto& st : S->stores) {
        const auto& ti = detail::type_info(st.type);
        st.base = static_cast<unsigned char*>(alloc_big(std::max<size_t>(1, ti.size * st.count), "sim: a type's units"));
    }
    for (uint32_t k = 0; k < n; k++) {
        auto& u = I.units[order[k]];
        unsigned char* dst = S->obj(k);
        detail::type_info(u.type).move(dst, u.obj);
        ::operator delete(u.obj, std::align_val_t(detail::type_info(u.type).align));
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

    // 7. initialise every structure
    S->unit_names.resize(n);
    for (uint32_t k = 0; k < n; k++) S->unit_names[k] = S->inst[k].name.c_str();
    for (const Decl& d : I.decls) S->port_names.push_back(d.name);
    S->route_names.resize(nroute);
    for (uint32_t r = 0; r < nroute; r++) S->route_names[r] = S->port_names[route_decl[r]].c_str();
    e.unit_names = S->unit_names.data();
    e.route_names = S->route_names.data();
    S->level_ports.resize(n);
    for (uint32_t k = 0; k < n; k++) {
        const auto& u = I.units[order[k]];
        e.mask[k] = 0; e.edges[k] = u.edges; e.wake_at[k] = kNoCycle; e.steps[k] = 0; e.heap_pos[k] = kNone;
        for (uint32_t id : u.decls) {
            const Decl& d = I.decls[id];
            if (!d.out && (d.kind == Kind::link || d.kind == Kind::fifo || d.kind == Kind::hold))
                S->level_ports[k].push_back({d.bit, d.name});
        }
    }
    for (uint32_t id = 0; id < I.decls.size(); id++) {
        const Decl& d = I.decls[id];
        const uint32_t owner = inst_of[d.unit];
        const char* nm = S->port_names[id].c_str();
        if (d.out) {
            const Decl& in = I.decls[d.peers[0]];
            const WheelDef& wd = wheels[route_wheel[d.final]];
            Route& r = e.routes[d.final];
            r = Route{};
            r.wheel = reinterpret_cast<Bucket*>(B + o_bheads[route_wheel[d.final]]);
            r.last_put = kNoCycle;
            r.mask = wd.W - 1;
            r.dest = in.final;
            r.owner = owner;
            r.latency = uint16_t(d.latency);
            r.width = uint8_t(d.width);
            r.kind = d.kind;
            continue;
        }
        if (d.kind == Kind::link) {
            LinkPort& l = *new (&e.links[d.final]) LinkPort{};
            l.ring.init(B + o_ring[id], std::max(1u, d.width), d.latency + 2, nm);
            l.last_take = kNoCycle; l.owner = owner; l.inflight = 0; l.cap = uint16_t(d.latency + 2); l.bit = d.bit; l.width = uint8_t(d.width);
        } else if (d.kind == Kind::fifo) {
            FifoPort& f = *new (&e.fifos[d.final]) FifoPort{};
            f.ring.init(B + o_ring[id], d.width, d.depth, nm);
            f.last_take = kNoCycle; f.first_overflow = kNoCycle; f.overflows = 0; f.owner = owner; f.count_idx = kNone;
            f.depth = uint16_t(d.depth); f.bit = d.bit; f.width = uint8_t(d.width);
        } else if (d.kind == Kind::hold) {
            const Decl& o = I.decls[d.peers[0]];
            HoldPort& h = *new (&e.holds[d.final]) HoldPort{};
            h.last_take = kNoCycle; h.refused_at = kNoCycle; h.refusals = 0; h.owner = owner; h.producer = inst_of[o.unit];
            h.bit = d.bit; h.pbit = o.bit; h.width = uint8_t(d.width); h.occ = false;
        }
    }
    for (size_t w = 0; w < wheels.size(); w++) {
        Bucket* heads = reinterpret_cast<Bucket*>(B + o_bheads[w]);
        std::string nm = std::string("wheel ") + kind_name(wheels[w].kind) + " w" + std::to_string(wheels[w].width);
        S->port_names.push_back(nm);
        const size_t per = Bucket::words_needed(wheels[w].width + 1, wheels[w].cap);
        for (unsigned b = 0; b < wheels[w].W; b++) {
            new (&heads[b]) Bucket();
            heads[b].init(B + o_bstore[w] + b * per, wheels[w].width + 1, wheels[w].cap, S->port_names.back().c_str());
        }
        e.wheels[w] = Wheel{heads, wheels[w].W - 1, wheels[w].kind, uint8_t(wheels[w].width)};
    }
    for (size_t r = 0; r < runs.size(); r++) {
        RunSet& rs = *new (&e.runs[r]) RunSet{};
        const RunDef& rd = runs[r];
        const auto& ti = detail::type_info(rd.type);
        rs.fn = ti.run[rd.form == Form::result ? 1 : 0];
        rs.units = S->obj(rd.first);
        rs.first = rd.first; rs.count = rd.count; rs.level = rd.level; rs.type = rd.type; rs.form = rd.form;
        S->port_names.push_back("run set " + ti.name);
        const size_t bw = Bitmap::words_needed(rd.count);
        rs.ready.init(B + o_bits[r], rd.count, S->port_names.back().c_str());
        rs.next.init(B + o_bits[r] + bw, rd.count, S->port_names.back().c_str());
        for (uint32_t k = rd.first; k < rd.first + rd.count; k++) e.run_of[k] = uint32_t(r);
    }
    e.bind();
    for (uint32_t k = 0; k < n; k++) e.mark(k);   // every unit is stepped at the first cycle (sim.h)

    // 8. sizes, by structure, by type, per chip
    Sizes& z = S->sizes;
    std::map<std::string, SizeLine> by_s, by_t;
    std::map<unsigned, uint64_t> by_chip;
    for (auto& it : c.items) {
        SizeLine& l = by_s[it.what];
        l.what = it.what; l.words += it.words; l.count += it.count;
        if (it.type >= 0) { SizeLine& t = by_t["ports of " + tname(uint16_t(it.type))]; t.what = "ports of " + tname(uint16_t(it.type)); t.words += it.words; t.count += it.count; }
        if (it.chip >= 0) by_chip[unsigned(it.chip)] += it.words;
    }
    uint64_t unit_words = 0;
    for (auto& st : S->stores) {
        const auto& ti = detail::type_info(st.type);
        uint64_t w = (ti.size * st.count + 7) / 8;
        SizeLine& t = by_t["unit " + ti.name];
        t.what = "unit " + ti.name; t.words += w; t.count += st.count;
        unit_words += w;
    }
    for (uint32_t k = 0; k < n; k++) by_chip[S->inst[k].chip] += (detail::type_info(S->inst[k].type).size + 7) / 8;
    for (auto& [k, v] : by_s) z.by_structure.push_back(v);
    for (auto& [k, v] : by_t) z.by_type.push_back(v);
    z.total_words = c.total + unit_words;
    z.chips = unsigned(by_chip.size() ? by_chip.size() : 1);
    for (auto& [k, v] : by_chip) z.per_chip.push_back(v);
    z.words_per_chip = z.total_words / z.chips;
    z.wheel_buckets = 0;
    for (auto& w : wheels) z.wheel_buckets = std::max(z.wheel_buckets, w.W);
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

void Sim::run(Cycle n) { impl_->e.run_until(impl_->e.now + n); }
void Sim::run_until(Cycle t) { impl_->e.run_until(t); }
Cycle Sim::now() const { return impl_->e.now; }
const Sizes& Sim::sizes() const { return impl_->sizes; }

void* Sim::unit_ptr(const std::string& name, uint16_t type) const {
    auto it = impl_->by_name.find(name);
    if (it == impl_->by_name.end()) FATAL("no unit named '%s'", name.c_str());
    if (impl_->inst[it->second].type != type)
        FATAL("unit '%s' is a %s, not a %s", name.c_str(), detail::type_info(impl_->inst[it->second].type).name.c_str(),
              detail::type_info(type).name.c_str());
    return impl_->obj(it->second);
}
uint64_t Sim::steps(const std::string& name) const {
    auto it = impl_->by_name.find(name);
    if (it == impl_->by_name.end()) FATAL("no unit named '%s'", name.c_str());
    return impl_->e.steps[it->second];
}

namespace {
struct Hash {
    uint64_t h = 0x9e3779b97f4a7c15ull;
    void add(uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); h *= 0xff51afd7ed558ccdull; }
    void ring(const Ring& r) { add(r.count()); for (unsigned i = 0; i < r.count(); i++) for (unsigned k = 0; k < r.width(); k++) add(r.peek(i)[k]); }
};
}  // namespace

uint64_t Sim::digest() const {
    const Engine& e = impl_->e;
    Hash h;
    h.add(e.now);
    // ports, in pool order: their visible state (not who is scheduled: step-all and the ready-driven run must agree)
    for (uint32_t i = 0; i < e.nlinks; i++) { h.ring(e.links[i].ring); h.add(e.links[i].inflight); }
    for (uint32_t i = 0; i < e.nfifos; i++) { h.ring(e.fifos[i].ring); h.add(e.fifos[i].overflows); h.add(e.fifos[i].first_overflow); }
    for (uint32_t i = 0; i < e.nholds; i++) {
        const HoldPort& p = e.holds[i];
        h.add(p.occ); h.add(p.refusals); h.add(p.refused_at);
        if (p.occ) for (unsigned k = 0; k < p.width; k++) h.add(p.w[k]);
    }
    for (uint32_t k = 0; k < e.ninst; k++) h.add(detail::type_info(impl_->inst[k].type).digest(impl_->obj(k)));
    return h.h;
}

std::string Sim::sleepers() const {
    const Engine& e = impl_->e;
    std::string out;
    for (uint32_t k = 0; k < e.ninst; k++) {
        uint64_t data = e.mask[k] & ~e.edges[k];
        if (!data) continue;
        const RunSet& r = e.runs[e.run_of[k]];
        if (r.ready.test(k - r.first) || e.heap_pos[k] != kNone) continue;
        out += impl_->inst[k].name + ": asleep with data waiting in";
        for (auto& [bit, nm] : impl_->level_ports[k]) if (data >> bit & 1) out += " " + nm;
        out += "\n";
    }
    return out;
}

}  // namespace sim
