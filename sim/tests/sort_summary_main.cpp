// sort_summary (WORKER24, P6, row D6): v1.3's SortSummary (sim/ref/meshsim/test/SortSummary.java) in C++, output for
// output: the per-run CSVs of a kit's results directory into SUMMARY.md (the Inputs section, P4, the shuffled identity
// check, each sweep with its band, the runs with no result) and all.csv (every CSV row).
//
//   sort_summary DIR [OUT.md] [--hashes FILE]       (Java: java SortSummary DIR [OUT.md], -Dsort.hashes=FILE)
//
// The reference set is HASHES in the working directory unless --hashes names another (SortSummary's -Dsort.hashes).
// Labels: p4/<set>, shuf/<set> (must equal p4's step by step), sweep/<set>/<point>.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sort_kit.h"

using namespace sortkit;

namespace {

struct Run {
    std::string label, fmt, path, params; int ready = 0; int64_t seed = 0;
    Ordered steps;          // step -> cycles (summed over repeats), in first-seen order
    int64_t total = -1; int failed = 0; std::string file;
    int64_t step(const std::string& k) const { const std::string* v = steps.get(k); return v ? std::stoll(*v) : 0; }
};
bool steps_equal(const Run& a, const Run& b) {          // Map.equals: the same keys and values, order aside
    if (a.steps.v.size() != b.steps.v.size()) return false;
    for (auto& e : a.steps.v) { const std::string* o = b.steps.get(e.first); if (!o || *o != e.second) return false; }
    return true;
}
// SortSummary's String.matches patterns (whole-string): "L1|SPLIT|XB|RED|L2", "XC|TH|SWEEP|DESC|K|V|..\\..", "XC|TH|SWEEP|DESC"
bool one_of(const std::string& s, std::initializer_list<const char*> v) { for (const char* x : v) if (s == x) return true; return false; }
bool main_step(const std::string& n) { return one_of(n, {"L1", "SPLIT", "XB", "RED", "L2"}); }
bool planning(const std::string& n) { return one_of(n, {"XC", "TH", "SWEEP", "DESC"}); }
bool skew_cmd(const std::string& n) { return planning(n) || one_of(n, {"K", "V"}) || (n.size() == 4 && n[2] == '.'); }

// The Inputs section; returns the number of runs flagged.
int inputs(const std::string& dir, const std::vector<Run>& runs, const std::string& hf, std::string& md) {
    const bool have_ref = is_file(hf) || exists(hf);
    Ordered ref; if (have_ref) ref = read_hashes(hf);
    std::vector<std::string> no_line, differ;
    std::map<std::string, std::vector<std::string>> kits;
    int keys_checked = 0;
    for (const Run& r : runs) {
        const std::string id = r.file.substr(0, r.file.size() - 4);
        const std::string log = resolve(dir, id + ".log");
        Ordered h; bool found = false;
        if (exists(log)) for (const std::string& l : read_lines(log)) if (parse_inputs(l, h)) { found = true; break; }
        if (!found) { no_line.push_back(id); continue; }
        std::string kit;
        for (auto& e : h.v) if (e.first == "meshsim.jar" || starts_with(e.first, "image/") || e.first == "hops.txt")
            kit += e.first + " " + abbrev(e.second) + ", ";
        kits[kit].push_back(id);
        if (have_ref) {
            for (auto& e : h.v) if (ends_with(e.first, ".keys") && ref.contains(e.first)) keys_checked++;
            std::vector<std::string> d = differences(h, ref);
            if (!d.empty()) differ.push_back(id + ": " + join(d, "; "));
        }
    }
    md += "## Inputs\n\n";
    const int with_line = int(runs.size() - no_line.size());
    if (!have_ref) md += "No reference set (" + java_path(hf) + " not found): the runs are compared only with each other.\n";
    else if (differ.empty() && with_line > 0)
        md += "All " + std::to_string(with_line) + " runs with an inputs line read the reference inputs (" + java_path(hf) + "): jar, image and hops.txt, and "
            + std::to_string(keys_checked) + " key files.\n";
    if (!differ.empty()) {
        md += "**" + std::to_string(differ.size()) + " runs read files that differ from the reference set (" + java_path(hf)
            + "); their results are not comparable with the reference's:**\n\n";
        for (auto& d : differ) md += "- " + d + "\n";
    }
    if (kits.size() > 1) {
        md += "\n**The runs used " + std::to_string(kits.size()) + " different kits (jar, image, hops.txt):**\n\n";
        for (auto& e : kits) {
            std::string k = e.first; if (ends_with(k, ", ")) k.resize(k.size() - 2);
            md += "- " + std::to_string(e.second.size()) + " runs (" + e.second[0] + (e.second.size() > 1 ? ", ..." : "") + "): " + k + "\n";
        }
    }
    if (!no_line.empty()) {
        std::vector<std::string> shown(no_line.begin(), no_line.begin() + std::min<size_t>(no_line.size(), 8));
        md += "\n**" + std::to_string(no_line.size()) + " runs have no inputs line (a kit before M9, or no log); their inputs are unknown:** "
            + join(shown, ", ") + (no_line.size() > 8 ? ", ..." : "") + "\n";
    }
    md += "\n";
    std::set<std::string> f;
    for (auto& d : differ) f.insert(d.substr(0, d.find(": ")));
    if (kits.size() > 1) for (const Run& r : runs) f.insert(r.file.substr(0, r.file.size() - 4));
    for (auto& n : no_line) f.insert(n);
    return int(f.size());
}

}  // namespace

int sort_summary_main(int argc, char** argv) {
    std::vector<std::string> a;
    std::string hf = "HASHES";
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--hashes" && i + 1 < argc) hf = argv[++i];
        else a.push_back(s);
    }
    const std::string dir = java_path(a.empty() ? "results" : a[0]);
    std::vector<Run> runs;
    std::string all;
    std::vector<std::string> files;
    for (const std::string& n : list_sorted(dir))
        if (ends_with(n, ".csv") && !ends_with(n, ".phases.csv") && n != "all.csv") files.push_back(n);
    std::vector<std::string> missing;
    for (const std::string& f : files) {
        std::vector<std::string> ls = read_lines(resolve(dir, f));
        if (ls.size() < 2) { missing.push_back(f); continue; }
        if (all.empty()) all += "file," + ls[0] + "\n";
        Run r; std::vector<std::string> h;
        for (size_t i = 1; i < ls.size(); i++) {
            all += f + "," + ls[i] + "\n";
            std::vector<std::string> c = split_all(ls[i], ',');
            if (c.size() < 20) { std::fprintf(stderr, "sort_summary: %s: a row with %zu columns\n", f.c_str(), c.size()); return 1; }
            h = c;
            r.failed = std::max(r.failed, c[19].empty() ? 0 : std::atoi(c[19].c_str()));
            if (c[9] == "TOTAL") r.total = std::stoll(c[10]);
            else {
                const std::string* v = r.steps.get(c[9]);
                r.steps.put(c[9], std::to_string((v ? std::stoll(*v) : 0) + std::stoll(c[10])));
            }
        }
        r.label = h[0]; r.fmt = h[1]; r.path = h[6]; r.params = h[5]; r.ready = std::atoi(h[4].c_str()); r.seed = std::stoll(h[3]); r.file = f;
        runs.push_back(r);
    }
    for (const std::string& n : list_sorted(dir)) {          // logs without a CSV
        if (!ends_with(n, ".log")) continue;
        const std::string id = remove_all(n, ".log");
        if (!exists(resolve(dir, id + ".csv"))) missing.push_back(id + " (log, no CSV: crashed, failed to finish or still running)");
    }
    std::string md = "# Sort measurements: summary of " + dir + "\n\n";
    md += "Generated by SortSummary from " + std::to_string(runs.size()) + " runs. Cycles are GO of L1 -> all READY of L2 [meas: SortTest].\n\n";
    const int flagged = inputs(dir, runs, hf, md);

    // ---- P4
    md += "## P4: every data set, both formats\n\n| set | format | path | total | L1 | SPLIT | XB + RED | skew planning (XC TH SWEEP DESC) | keys (K, K1.*, K2.*) | values (V, V1.*, V2.*) | L2 | empty and other | checks failed |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    std::map<std::string, const Run*> p4;
    for (const Run& r : runs) if (starts_with(r.label, "p4/")) p4[r.label.substr(3) + " " + r.fmt] = &r;
    for (auto& e : p4) {
        const Run& r = *e.second;
        int64_t l1 = r.step("L1"), sp = r.step("SPLIT"), xr = r.step("XB") + r.step("RED"), plan = 0, keys = 0, vals = 0, l2 = r.step("L2"), other = 0;
        for (auto& s : r.steps.v) {
            const std::string& n = s.first; const int64_t v = std::stoll(s.second);
            if (main_step(n)) continue;
            if (v <= 150 && skew_cmd(n)) { other += v; continue; }
            if (planning(n)) plan += v;
            else if (n == "K" || starts_with(n, "K1.") || starts_with(n, "K2.")) keys += v;
            else if (n == "V" || starts_with(n, "V1.") || starts_with(n, "V2.")) vals += v;
            else other += v;
        }
        md += "| " + r.label.substr(3) + " | " + r.fmt + " | " + r.path + " | " + grouped(r.total) + " | " + grouped(l1) + " | " + grouped(sp)
            + " | " + grouped(xr) + " | " + grouped(plan) + " | " + grouped(keys) + " | " + (r.fmt == "k128" ? grouped(vals) : std::string("-"))
            + " | " + grouped(l2) + " | " + grouped(other) + " | " + std::to_string(r.failed) + " |\n";
    }

    // ---- shuffled
    md += "\n## Shuffled child order (must equal the fixed-order run step by step)\n\n| set | format | seed | total | fixed-order total | identical |\n|---|---|---|---|---|---|\n";
    for (const Run& r : runs) if (starts_with(r.label, "shuf/")) {
        auto it = p4.find(r.label.substr(5) + " " + r.fmt);
        const Run* f = it == p4.end() ? nullptr : it->second;
        md += "| " + r.label.substr(5) + " | " + r.fmt + " | " + std::to_string(r.seed) + " | " + grouped(r.total) + " | "
            + (f ? grouped(f->total) : std::string("(missing)")) + " | "
            + (f ? (steps_equal(*f, r) && f->total == r.total ? "yes" : "**NO**") : "?") + " |\n";
    }

    // ---- sweep
    std::map<std::string, std::vector<const Run*>> sw;
    for (const Run& r : runs) if (starts_with(r.label, "sweep/")) {
        std::vector<std::string> parts = split_all(r.label, '/');
        sw[parts[1] + " " + r.fmt].push_back(&r);
    }
    for (auto& e : sw) {
        const Run* def = nullptr;
        for (const Run* r : e.second) if (ends_with(r->label, "/default")) { def = r; break; }
        md += "\n## Sweep: " + e.first + "\n\n| point | parameters | ready_latency | path | total | delta | delta % | checks failed |\n|---|---|---|---|---|---|---|---|\n";
        int64_t lo = INT64_MAX, hi = INT64_MIN;
        std::vector<const Run*> rs = e.second;
        std::stable_sort(rs.begin(), rs.end(), [](const Run* x, const Run* y) { return x->label < y->label; });
        for (const Run* r : rs) {
            const int64_t d = def ? r->total - def->total : 0;
            if (r->failed == 0) { lo = std::min(lo, r->total); hi = std::max(hi, r->total); }
            md += "| " + r->label.substr(r->label.rfind('/') + 1) + " | " + r->params + " | " + std::to_string(r->ready) + " | " + r->path + " | "
                + grouped(r->total) + " | " + grouped(d, true) + " | " + fixed(def ? 100.0 * double(d) / double(def->total) : 0.0, 2, true) + " % | "
                + std::to_string(r->failed) + " |\n";
        }
        if (def && lo <= hi)
            md += "\nBand over the points without a failed check: " + grouped(lo) + " .. " + grouped(hi) + " against " + grouped(def->total) + " ("
                + fixed(100.0 * double(lo - def->total) / double(def->total), 2, true) + " % .. "
                + fixed(100.0 * double(hi - def->total) / double(def->total), 2, true) + " %).\n";
    }
    if (!missing.empty()) { md += "\n## Runs with no result\n\n"; for (auto& m : missing) md += "- " + m + "\n"; }
    const std::string all_path = resolve(dir, "all.csv");
    const std::string out = a.size() > 1 ? java_path(a[1]) : resolve(dir, "SUMMARY.md");
    if (!write_file(all_path, all) || !write_file(out, md)) { std::fprintf(stderr, "sort_summary: cannot write %s or %s\n", all_path.c_str(), out.c_str()); return 1; }
    std::printf("wrote %s and %s (%zu runs%s%s)\n", out.c_str(), all_path.c_str(), runs.size(),
                missing.empty() ? "" : (", " + std::to_string(missing.size()) + " without a result").c_str(),
                flagged == 0 ? "" : ("; WARNING: " + std::to_string(flagged) + " runs flagged under Inputs").c_str());
    return 0;
}

#ifndef SORT_KIT_NO_MAIN
int main(int argc, char** argv) { return sort_summary_main(argc, argv); }
#endif
