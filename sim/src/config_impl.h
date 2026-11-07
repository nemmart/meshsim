// config_impl.h: the configuration's and the machine's internals (WORKER3), apart from config.cpp so W4 can split a
// configuration into domains. Not for unit authors; nothing here runs in the cycle loop.
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/arena.h"
#include "config.h"
#include "digest_view.h"

namespace sim {

inline const char* kind_name(Kind k) {
    static const char* n[] = { "link", "fifo", "credit fifo", "hold", "reg", "wire", "regfile", "count", "credit return" };
    return n[unsigned(k)];
}
// How a declaration's handle is written back into its unit after the build (config.cpp, step 7).
enum class Shape : uint8_t { in, out, out_bit, idx };

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
        unsigned slot_words = 0, held = 0;  // by reference: the size class (0: by value), and slots held beyond the port
        Word reset = 0;
        size_t offset = 0;
        std::vector<uint32_t> peers;
        uint32_t final = kNone;         // the pool index (inputs) or the route (outputs)
    };
    // A Link's capacity, in flight and visible together (PORTS.md 2): L + its declared depth (W10), L + 2 by default.
    static unsigned link_capacity(const Decl& d) {
        const unsigned c = d.latency + (d.depth ? d.depth : 2);
        if (c > 0xFFFF) FATAL("link '%s': latency %u + depth %u over 65535", d.name.c_str(), d.latency, d.depth);
        return c;
    }
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
    // This configuration's unit types, ids in the order they were first added (E1: per machine, so ids are deterministic
    // whatever other threads build); each entry is the type's pure, process-wide description (detail::type_ops<T>).
    std::vector<const detail::TypeInfo*> types;
    std::unordered_map<const detail::TypeInfo*, uint16_t> type_ids;
    uint16_t type_id(const detail::TypeInfo* t) {
        auto it = type_ids.find(t);
        if (it != type_ids.end()) return it->second;
        if (types.size() >= 0xFFFF) FATAL("too many unit types");
        types.push_back(t);
        return type_ids.emplace(t, uint16_t(types.size() - 1)).first->second;
    }
    std::vector<Staged> units;
    std::vector<Decl> decls;
    std::unordered_map<std::string, uint32_t> names;
    unsigned chip = 0, domain = 0;
    int cur = -1;
    std::map<std::string, unsigned> missed;   // per-unit missed-wake thresholds (D20)
    Ports* ports = nullptr;
    bool built = false;

    Decl& decl(uint32_t id, const char* what) {
        if (id >= decls.size()) FATAL("%s: not a port handle of this configuration (%u)", what, id);
        return decls[id];
    }
    uint32_t declare(const void* h, Kind k, bool out, Shape shape, const char* name, unsigned width, bool bit, bool edge) {
        if (cur < 0) FATAL("a port declared outside a unit's constructor ('%s')", name);
        Staged& u = units[size_t(cur)];
        const auto& ti = *types[u.type];
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

namespace detail {
struct Threads;                                     // threads.h (W4): a machine split over several domains
void destroy_threads(Threads* t);
}  // namespace detail

// ---- Sim ---------------------------------------------------------------------------------------------------------------
struct Sim::Impl {
    Engine e;
    std::vector<const detail::TypeInfo*> types;        // the configuration's, by id
    std::unique_ptr<Block> block;
    struct Store { uint16_t type; unsigned char* base; uint32_t count; };
    std::vector<Store> stores;
    struct Inst { uint32_t store, index; std::string name; uint16_t type; unsigned chip; };
    std::vector<Inst> inst;
    std::unordered_map<std::string, uint32_t> by_name;
    std::vector<std::string> route_name_s;
    std::vector<const char*> unit_names, route_names, link_name_ptrs;
    std::deque<std::string> port_names;                // one per declaration, by declaration id: stable addresses (rings keep them)
    std::deque<std::string> misc_names;                // pools, wheels, run sets (W1's structures keep pointers)
    std::vector<std::vector<std::pair<uint8_t, std::string>>> level_ports;   // per instance: (bit, port name) of its level inputs
    std::vector<unsigned> missed_after;               // per instance: the missed-wake detector's threshold (increment 5)
    Sizes sizes;
    detail::DigestView view;                           // the digest, unit by unit (W4, E5: digest.h)
    detail::Threads* threads = nullptr;                // W4 (E3): set when the machine is split over domains; the Sim's
                                                       // operations go to it, and the fields above are unused
    unsigned char* obj(uint32_t i) const {
        const Store& s = stores[inst[i].store];
        return s.base + size_t(inst[i].index) * (*types[s.type]).size;
    }
    ~Impl() {
        if (threads) detail::destroy_threads(threads);
        for (auto& s : stores) {
            const auto& ti = *types[s.type];
            for (uint32_t k = 0; k < s.count; k++) ti.destroy(s.base + size_t(k) * ti.size);
            free_big(s.base);
        }
    }
};

}  // namespace sim
