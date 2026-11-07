// statefp.cpp: the canonical state fingerprint's C++ side (statefp.h).
#include "statefp.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace fp {

const char* const kTileUnits[16] = {"core", "ctl", "cmem", "dir", "router", "inj", "srq", "sram", "sram.data", "we",
                                    "ag", "ts", "hist", "gather", "rdport", "req"};
const char* const kChipUnits[4] = {"cnc", "cc1p", "reqbus", "ep"};

// ------------------------------------------------------------------------------------------------------------ FpOut
void FpOut::wi(const char* name, unsigned i, uint64_t v) {
    if (!dump_) { w(name, v); return; }
    char b[96];
    std::snprintf(b, sizeof b, "%s%u", name, i);
    w(b, v);
}
void FpOut::wp(const char* prefix, const char* name, uint64_t v) {
    if (!dump_) { w(name, v); return; }
    char b[96];
    std::snprintf(b, sizeof b, "%s%s", prefix, name);
    w(b, v);
}
void FpOut::wpi(const char* prefix, unsigned i, const char* name, uint64_t v) {
    if (!dump_) { w(name, v); return; }
    char b[96];
    std::snprintf(b, sizeof b, "%s%u%s", prefix, i, name);
    w(b, v);
}

// ------------------------------------------------------------------------------------------------------------ buses
void link(FpOut& o, const char* nm, const BusEntry* e, unsigned n) {
    o.wp(nm, ".n", n);
    for (unsigned k = 0; k < n; k++) {
        o.wp(nm, ".at", e[k].at); o.wp(nm, ".a", e[k].a); o.wp(nm, ".b", e[k].b); o.wp(nm, ".c", e[k].c);
    }
}
void fifo(FpOut& o, const char* nm, const BusEntry* e, unsigned n, const BusEntry* put) {
    o.wp(nm, ".n", n);
    for (unsigned k = 0; k < n; k++) { o.wp(nm, ".a", e[k].a); o.wp(nm, ".b", e[k].b); o.wp(nm, ".c", e[k].c); }
    o.wp(nm, ".put", put ? 1 : 0);
    if (put) { o.wp(nm, ".pa", put->a); o.wp(nm, ".pb", put->b); o.wp(nm, ".pc", put->c); }
}
void hold(FpOut& o, const char* nm, const BusEntry* held, const BusEntry* put) {
    o.wp(nm, ".occ", held ? 1 : 0);
    if (held) { o.wp(nm, ".a", held->a); o.wp(nm, ".b", held->b); o.wp(nm, ".c", held->c); }
    o.wp(nm, ".put", put ? 1 : 0);
    if (put) { o.wp(nm, ".pa", put->a); o.wp(nm, ".pb", put->b); o.wp(nm, ".pc", put->c); }
}

// --------------------------------------------------------------------------------------------------------- registry
void FpRegistry::add(const std::string& name, FpWriter w, const void* unit) {
    if (!w) { std::fprintf(stderr, "statefp: no writer for %s\n", name.c_str()); std::abort(); }
    if (!by_name_.emplace(name, Entry{w, unit}).second) {
        std::fprintf(stderr, "statefp: a second writer for %s\n", name.c_str());
        std::abort();
    }
}
uint64_t FpRegistry::hash(const std::string& name, const FpCtx& x) const {
    auto it = by_name_.find(name);
    if (it == by_name_.end()) { std::fprintf(stderr, "statefp: no unit %s registered\n", name.c_str()); std::abort(); }
    FpOut o;
    it->second.w(it->second.unit, o, x);
    return o.done();
}
void FpRegistry::dump(const std::string& name, const FpCtx& x, FILE* out) const {
    auto it = by_name_.find(name);
    if (it == by_name_.end()) { std::fprintf(stderr, "statefp: no unit %s registered\n", name.c_str()); std::abort(); }
    std::fprintf(out, "# fpdump cycle %llu unit %s\n", (unsigned long long)x.cycle, name.c_str());
    FpOut o(out, name.c_str());
    it->second.w(it->second.unit, o, x);
}
void FpRegistry::point(const FpCtx& x, const std::vector<unsigned>& tiles, bool with_data,
                       std::vector<std::string>& out) const {
    std::vector<uint64_t> hs;
    char b[128];
    auto line = [&](const std::string& name) {
        uint64_t h = hash(name, x);
        hs.push_back(h);
        std::snprintf(b, sizeof b, "%llu %s %016llx", (unsigned long long)x.cycle, name.c_str(), (unsigned long long)h);
        out.push_back(b);
    };
    for (unsigned t : tiles)
        for (const char* u : kTileUnits) {
            if (!with_data && std::strcmp(u, "sram.data") == 0) continue;
            line("t" + std::to_string(t) + "." + u);
        }
    for (const char* u : kChipUnits) line(u);
    std::snprintf(b, sizeof b, "%llu chip %016llx", (unsigned long long)x.cycle,
                  (unsigned long long)combine(hs.data(), hs.size()));
    out.push_back(b);
}

// ------------------------------------------------------------------------------------------------------ golden files
static bool parse_line(const std::string& s, FpLine& l) {
    std::istringstream in(s);
    std::string c, u, h;
    if (!(in >> c >> u >> h) || h.size() != 16) return false;
    char* end = nullptr;
    l.cycle = std::strtoull(c.c_str(), &end, 10);
    if (*end) return false;
    l.hash = std::strtoull(h.c_str(), &end, 16);
    if (*end) return false;
    l.unit = u;
    return true;
}

FpGolden FpGolden::load(const std::string& path) {
    FpGolden g;
    g.path = path;
    std::ifstream f(path);
    if (!f) { g.error = path + ": cannot open"; return g; }
    std::string s;
    unsigned n = 0;
    bool body = false;
    FpPoint* cur = nullptr;
    while (std::getline(f, s)) {
        n++;
        if (s.empty()) continue;
        if (s[0] == '#') {
            if (body) { g.notes.push_back(s); continue; }
            g.header.push_back(s);
            std::istringstream in(s);
            std::string hash, kw, key, rest;
            in >> hash >> kw;
            if (kw == "config" && (in >> key)) {
                std::getline(in, rest);
                size_t p = rest.find_first_not_of(' ');
                g.config[key] = p == std::string::npos ? "" : rest.substr(p);
            }
            continue;
        }
        body = true;
        FpLine l;
        if (!parse_line(s, l)) { g.error = path + ":" + std::to_string(n) + ": not 'cycle unit hash': " + s; return g; }
        if (!cur || cur->lines.back().unit == "chip") {
            g.points.push_back(FpPoint{l.cycle, {}});
            cur = &g.points.back();
        } else if (l.cycle != cur->cycle) {
            g.error = path + ":" + std::to_string(n) + ": cycle " + std::to_string(l.cycle) + " inside the point at cycle " +
                      std::to_string(cur->cycle) + " (no chip line before it)";
            return g;
        }
        cur->lines.push_back(l);
    }
    if (cur && cur->lines.back().unit != "chip") g.error = path + ": the last point has no chip line";
    return g;
}

std::string FpGolden::check_header(const std::vector<std::string>& ours) const {
    std::map<std::string, std::string> oc;
    for (const std::string& s : ours) {
        std::istringstream in(s);
        std::string hash, kw, key, rest;
        in >> hash >> kw;
        if (kw == "config" && (in >> key)) {
            std::getline(in, rest);
            size_t p = rest.find_first_not_of(' ');
            oc[key] = p == std::string::npos ? "" : rest.substr(p);
        }
    }
    for (auto& [k, v] : oc) {
        auto it = config.find(k);
        if (it == config.end()) return "refused (D12): the golden has no config " + k + " (ours: " + v + ")";
        if (it->second != v) return "refused (D12): config " + k + ": golden " + it->second + ", ours " + v;
    }
    for (auto& [k, v] : config)
        if (!oc.count(k)) return "refused (D12): config " + k + " " + v + " in the golden, unknown here";
    size_t n = header.size() > ours.size() ? header.size() : ours.size();
    for (size_t i = 0; i < n; i++) {
        std::string a = i < header.size() ? header[i] : "(none)", b = i < ours.size() ? ours[i] : "(none)";
        if (a != b) return "refused (D12): header line " + std::to_string(i + 1) + ": golden '" + a + "', ours '" + b + "'";
    }
    return "";
}

std::vector<const FpPoint*> FpGolden::at(uint64_t c) const {
    std::vector<const FpPoint*> r;
    for (const FpPoint& p : points) if (p.cycle == c) r.push_back(&p);
    return r;
}

FpDiff compare(const FpPoint& g, const std::vector<FpLine>& ours) {
    FpDiff d;
    size_t n = g.lines.size() > ours.size() ? g.lines.size() : ours.size();
    char b[96];
    for (size_t i = 0; i < n; i++) {
        if (i >= g.lines.size() || i >= ours.size()) {
            d.any = true; d.cycle = g.cycle;
            d.unit = i < g.lines.size() ? g.lines[i].unit : ours[i].unit;
            d.why = i < g.lines.size() ? "missing on our side" : "not in the golden";
            return d;
        }
        const FpLine &a = g.lines[i], &o = ours[i];
        if (a.unit != o.unit || a.cycle != o.cycle) {
            d.any = true; d.cycle = g.cycle; d.unit = a.unit;
            d.why = "line " + std::to_string(i + 1) + ": golden '" + std::to_string(a.cycle) + " " + a.unit + "', ours '" +
                    std::to_string(o.cycle) + " " + o.unit + "'";
            return d;
        }
        if (a.hash != o.hash) {
            d.any = true; d.cycle = g.cycle; d.unit = a.unit;
            std::snprintf(b, sizeof b, "hash golden %016llx, ours %016llx", (unsigned long long)a.hash,
                          (unsigned long long)o.hash);
            d.why = b;
            return d;
        }
    }
    return d;
}

std::vector<FpLine> parse_lines(const std::vector<std::string>& lines) {
    std::vector<FpLine> r;
    for (const std::string& s : lines) {
        FpLine l;
        if (!parse_line(s, l)) { std::fprintf(stderr, "statefp: not 'cycle unit hash': %s\n", s.c_str()); std::abort(); }
        r.push_back(l);
    }
    return r;
}

}  // namespace fp
