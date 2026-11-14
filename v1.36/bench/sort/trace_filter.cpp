// trace_filter: footprints from a memory trace. Reads bench_chip --trace-map's region map (argv[1]) and
// valgrind --tool=lackey --trace-mem=yes's trace (stdin), windows marked by stores to the map's marker; reports, per
// structure (the map's names), what is allocated against the cache lines touched over all windows, and per domain
// (thread) the lines touched per window (one barrier interval when the window is the interval).
//   valgrind --tool=lackey --trace-mem=yes --log-fd=1 ./bench_chip ... --trace-map M | ./trace_filter M
// Stand-alone (no sim/ headers): a measurement tool, not part of the framework.
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

struct Region { uint64_t lo, hi; int dom; std::string what; };

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: trace_filter MAP < lackey-trace\n"); return 2; }
    FILE* f = std::fopen(argv[1], "r");
    if (!f) { std::fprintf(stderr, "trace_filter: cannot read %s\n", argv[1]); return 2; }
    uint64_t marker = 0;
    long nwin = -1;                                          // the windows the run marks: what follows the last is not counted
    std::vector<Region> regs;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        if (!std::strncmp(line, "marker ", 7)) { marker = std::strtoull(line + 7, nullptr, 16); continue; }
        if (!std::strncmp(line, "windows ", 8)) { nwin = std::strtol(line + 8, nullptr, 10); continue; }
        if (std::strncmp(line, "region ", 7)) continue;
        char* p = line + 7;
        const uint64_t lo = std::strtoull(p, &p, 16);
        const uint64_t bytes = std::strtoull(p, &p, 10);
        const int dom = int(std::strtol(p, &p, 10));
        while (*p == ' ') p++;
        std::string what(p);
        while (!what.empty() && (what.back() == '\n' || what.back() == ' ')) what.pop_back();
        regs.push_back({lo, lo + bytes, dom, what});
    }
    std::fclose(f);
    std::sort(regs.begin(), regs.end(), [](const Region& a, const Region& b) { return a.lo < b.lo; });
    int ndom = 0;
    std::map<std::string, uint64_t> alloc;
    for (auto& r : regs) { ndom = std::max(ndom, r.dom + 1); alloc[r.what] += r.hi - r.lo; }
    auto find = [&](uint64_t a) -> const Region* {
        auto it = std::upper_bound(regs.begin(), regs.end(), a, [](uint64_t x, const Region& r) { return x < r.lo; });
        if (it == regs.begin()) return nullptr;
        --it;
        return a < it->hi ? &*it : nullptr;
    };
    // per window: lines by domain (and "other": outside every region); over all windows: lines by structure name
    std::vector<std::unordered_set<uint64_t>> win(ndom + 1);
    std::map<std::string, std::unordered_set<uint64_t>> ever;
    std::map<std::string, uint64_t> win_sum;                 // per name: lines touched per window, summed over windows
    std::map<std::string, std::unordered_set<uint64_t>> win_name;
    std::vector<uint64_t> dom_sum(ndom + 1, 0), dom_max(ndom + 1, 0);
    long window = -1, windows = 0;
    auto close_window = [&]() {
        if (window < 0 || (nwin >= 0 && window >= nwin)) return;
        for (int d = 0; d <= ndom; d++) {
            dom_sum[d] += win[d].size();
            dom_max[d] = std::max<uint64_t>(dom_max[d], win[d].size());
            win[d].clear();
        }
        for (auto& [nm, s] : win_name) { win_sum[nm] += s.size(); s.clear(); }
        windows++;
    };
    while (std::fgets(line, sizeof line, stdin)) {
        // lackey: " L addr,size", " S addr,size", " M addr,size" (data); "I  addr,size" (instructions: skipped)
        if (line[0] != ' ' || (line[1] != 'L' && line[1] != 'S' && line[1] != 'M')) continue;
        char* p = line + 3;
        const uint64_t a = std::strtoull(p, &p, 16);
        const uint64_t sz = *p == ',' ? std::strtoull(p + 1, nullptr, 10) : 1;
        if (a == marker && line[1] != 'L') {               // a window's start (the last store ends the run)
            close_window();
            window++;
            continue;
        }
        if (window < 0 || (nwin >= 0 && window >= nwin)) continue;   // before the first window, after the last (teardown)
        const Region* r = find(a);
        for (uint64_t l = a >> 6; l <= (a + (sz ? sz : 1) - 1) >> 6; l++) {
            if (r) { win[r->dom].insert(l); ever[r->what].insert(l); win_name[r->what].insert(l); }
            else { const char* o = "(outside the map: stacks, the bench, units' own heap)"; win[ndom].insert(l); ever[o].insert(l); win_name[o].insert(l); }
        }
    }
    // the last marker store ended the run: the window it opened is not one (nothing after it was counted)
    std::printf("footprint windows=%ld domains=%d\n", windows, ndom);
    std::printf("per domain (thread), cache lines touched per window: mean, max (64-byte lines; KB)\n");
    for (int d = 0; d <= ndom; d++)
        std::printf("  %s mean=%.0f lines (%.1f KB) max=%llu lines (%.1f KB)\n", d < ndom ? ("domain " + std::to_string(d)).c_str() : "outside",
                    windows ? double(dom_sum[d]) / windows : 0.0, windows ? double(dom_sum[d]) / windows * 64 / 1024 : 0.0,
                    (unsigned long long)dom_max[d], double(dom_max[d]) * 64 / 1024);
    std::printf("per structure: allocated KB, touched over all windows KB (%%), touched per window KB (mean, all domains)\n");
    std::vector<std::pair<uint64_t, std::string>> order;
    for (auto& [nm, b] : alloc) order.push_back({b, nm});
    for (auto& [nm, s] : ever) if (!alloc.count(nm)) order.push_back({0, nm});
    std::sort(order.rbegin(), order.rend());
    for (auto& [b, nm] : order) {
        const double t = ever.count(nm) ? double(ever[nm].size()) * 64 : 0.0;
        const double pw = windows && win_sum.count(nm) ? double(win_sum[nm]) / windows * 64 : 0.0;
        std::printf("  %-58s alloc=%10.1f KB touched=%9.1f KB (%5.1f%%) per_window=%8.1f KB\n", nm.c_str(), double(b) / 1024, t / 1024,
                    b ? 100.0 * t / double(b) : 0.0, pw / 1024);
    }
    return 0;
}
