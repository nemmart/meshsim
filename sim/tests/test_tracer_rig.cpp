// test_tracer_rig.cpp (WORKER21): TracerRig against ref's rig.
//  - JavaLongHashMap, the order ref's rig hands its pages to FileStore in (which decides their addresses and so the
//    caches' timing; tracer_rig.h), against java.util.HashMap on every case of tests/tracersuites/hashorder.txt
//    (tests/ref/JavaHashOrder.java: put and computeIfAbsent, growth, a filling bin, high and negative keys, repeats,
//    TracerTest's own page sets).
//  - THE RIG'S CALLS: suite_tracer --calls records every Tracer call the C++ rig and TracerTest's cases make, in
//    TracerConf's format (RecTracer); it must equal ref's rig's recording of the same cases (tests/tracerconf/
//    cases.log.gz, W14's TracerConf, made from ref's TracerRig and TracerTest), Tracer by Tracer: the same Tracers (ids,
//    probes included), the same store dumps (every page, by address: the page order), and the same calls with the same
//    results at the same cycles, token by token. Both logs are expanded to one stream of (cycle, call) per Tracer, so
//    how each side runs equal cycles together doesn't matter. A "U" line (C++) stands for a UB check run in a forked
//    child: ref's calls from there up to the one that stopped ('!') are skipped. With W14's replay (test_tracer: the
//    Tracer's state after every one of these calls equals ref's), the C++ rig is ref's rig, call for call.
// Run from sim/.
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "harness.h"
#include "tracer_rig.h"

using tracer_rig::JavaLongHashMap;

TEST(java_long_hash_map_equals_java) {
    std::ifstream f("tests/tracersuites/hashorder.txt");
    CHECK(f.good());
    std::string ops_line, order_line;
    int cases = 0;
    while (std::getline(f, ops_line) && std::getline(f, order_line)) {
        std::istringstream a(ops_line), b(order_line);
        std::string w;
        a >> w;
        CHECK(w == "ops");
        JavaLongHashMap m;
        while (a >> w) {
            const int64_t k = std::strtoll(w.c_str() + 1, nullptr, 10);
            if (w[0] == 'p') m.put(k);
            else m.compute_if_absent(k);
        }
        b >> w;
        if (w == "treeified") {  // Java made a tree of a bin: the model must know it, and refuse an order
            if (!m.treeified()) { ::test::fail(__FILE__, __LINE__, "case %d: Java treeified, the model didn't", cases); return; }
            cases++;
            continue;
        }
        CHECK(w == "order");
        if (m.treeified()) { ::test::fail(__FILE__, __LINE__, "case %d: the model treeified, Java didn't", cases); return; }
        std::vector<int64_t> want;
        while (b >> w) want.push_back(std::strtoll(w.c_str(), nullptr, 10));
        if (m.order() != want) {
            ::test::fail(__FILE__, __LINE__, "case %d differs from Java: %s", cases, ops_line.c_str());
            return;
        }
        cases++;
    }
    CHECK_EQ(cases, 49);
}

TEST(pages_iterate_as_refs_rig_fills_them) {
    // TracerTest's rig(): poke the code, then the data (computeIfAbsent both): Java iterates the data page first
    tracer_rig::Pages p;
    p.poke(0x100000, std::vector<uint8_t>(32, 1));
    p.poke(0x101000, std::vector<uint8_t>(8));
    const std::vector<int64_t> want = {0x101000, 0x100000};
    CHECK(p.map.order() == want);
}

// ================================================================ the rig's calls against ref's rig's
namespace {

struct Group {
    bool ub = false;  // a "U" line
    int64_t t0 = 0, n = 0;
    std::vector<std::pair<int64_t, std::string>> toks;  // (offset from the cycle's anchor, the call without "@d")
};
struct Stream {
    std::vector<std::string> hdr;  // N, F, P, Z lines in order
    std::vector<Group> groups;
};

// "name@d,rest" -> ("name,rest", d); a plain token: offset 0
std::pair<int64_t, std::string> parse_token(const std::string& tk) {
    const size_t at = tk.find('@');
    if (at == std::string::npos || at > tk.find_first_of(",=")) return {0, tk};
    size_t e = at + 1;
    if (e < tk.size() && tk[e] == '-') e++;
    while (e < tk.size() && tk[e] >= '0' && tk[e] <= '9') e++;
    return {std::strtoll(tk.substr(at + 1, e - at - 1).c_str(), nullptr, 10), tk.substr(0, at) + tk.substr(e)};
}

bool read_log(const std::string& cmd, std::map<int, Stream>& out) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    std::string line;
    char buf[1 << 16];
    while (std::fgets(buf, sizeof buf, p)) {
        line += buf;
        if (line.empty() || line.back() != '\n') continue;
        line.pop_back();
        std::istringstream in(line);
        std::string kind;
        int id = -1;
        in >> kind >> id;
        if (kind == "N" || kind == "F" || kind == "P" || kind == "Z") out[id].hdr.push_back(line);
        else if (kind == "U") { Group g; g.ub = true; out[id].groups.push_back(g); }
        else if (kind == "c") {
            Group g;
            std::string sig;
            in >> g.t0 >> g.n >> sig;
            size_t s = 0;
            while (s <= sig.size()) {
                size_t e = sig.find(';', s);
                if (e == std::string::npos) e = sig.size();
                g.toks.push_back(parse_token(sig.substr(s, e - s)));
                s = e + 1;
            }
            out[id].groups.push_back(g);
        }
        line.clear();
    }
    return pclose(p) == 0;
}

// One Tracer's calls in order: (cycle, call), or a U mark.
struct Walk {
    const std::vector<Group>& g;
    size_t gi = 0;
    int64_t ci = 0;
    size_t ti = 0;
    bool at_end() const { return gi >= g.size(); }
    bool at_ub() const { return !at_end() && g[gi].ub; }
    int64_t cycle() const { return g[gi].t0 + ci + g[gi].toks[ti].first; }
    const std::string& call() const { return g[gi].toks[ti].second; }
    void next() {
        if (g[gi].ub) { gi++; ci = 0; ti = 0; return; }
        if (++ti < g[gi].toks.size()) return;
        ti = 0;
        if (++ci < g[gi].n) return;
        ci = 0;
        gi++;
    }
};

std::string self_dir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, size_t(n));
    return p.substr(0, p.rfind('/'));
}

}  // namespace

TEST(rig_calls_equal_refs_rig) {
    const std::string mine = "/tmp/test_tracer_rig_calls." + std::to_string(getpid());
    const std::string cmd = self_dir() + "/suite_tracer --calls " + mine + " > /dev/null 2>&1";
    CHECK_EQ(std::system(cmd.c_str()), 0);
    std::map<int, Stream> ref, cpp;
    CHECK(read_log("gzip -dc tests/tracerconf/cases.log.gz", ref));
    CHECK(read_log("cat " + mine, cpp));
    std::remove(mine.c_str());
    CHECK_EQ(ref.size(), size_t(37));
    CHECK_EQ(cpp.size(), ref.size());
    int64_t calls = 0, skipped = 0;
    for (const auto& [id, r] : ref) {
        auto it = cpp.find(id);
        if (it == cpp.end()) { ::test::fail(__FILE__, __LINE__, "Tracer %d: ref's, not ours", id); return; }
        const Stream& c = it->second;
        if (r.hdr != c.hdr) {
            size_t k = 0;
            while (k < r.hdr.size() && k < c.hdr.size() && r.hdr[k] == c.hdr[k]) k++;
            ::test::fail(__FILE__, __LINE__, "Tracer %d: store dump line %zu differs:\n ref  %.200s\n ours %.200s", id, k,
                         k < r.hdr.size() ? r.hdr[k].c_str() : "(none)", k < c.hdr.size() ? c.hdr[k].c_str() : "(none)");
            return;
        }
        Walk a{r.groups}, b{c.groups};
        while (!a.at_end() || !b.at_end()) {
            if (b.at_ub()) {  // ref's calls in the child, up to the one that stopped
                b.next();
                while (!a.at_end() && !a.at_ub()) {
                    const std::string& x = a.call();
                    const bool stop = !x.empty() && (x.back() == '!' || x.back() == '%');
                    a.next();
                    skipped++;
                    if (stop) break;
                }
                continue;
            }
            if (a.at_end() || b.at_end() || a.cycle() != b.cycle() || a.call() != b.call()) {
                ::test::fail(__FILE__, __LINE__, "Tracer %d, call %lld: ref %s at %lld, ours %s at %lld", id, (long long)calls,
                             a.at_end() ? "(end)" : a.call().c_str(), a.at_end() ? -1LL : (long long)a.cycle(),
                             b.at_end() ? "(end)" : b.call().c_str(), b.at_end() ? -1LL : (long long)b.cycle());
                return;
            }
            a.next();
            b.next();
            calls++;
        }
    }
    std::printf("  %lld calls equal to ref's rig's, %lld skipped (in the UB checks' children)\n", (long long)calls,
                (long long)skipped);
    CHECK(calls > 0);
    CHECK_EQ(skipped, int64_t(5));
}
