// sort_compare (D41 Q3: kept as the generic run-comparison tool): v1.3's SortCompare
// (sim/ref/meshsim/test/SortCompare.java) in C++, output for output. Two kit results directories compared run by run:
// for every <id>.csv in both, the steps (every column), then every C&C phase of <id>.phases.csv (every column but go_at
// and end_at, which move with any earlier difference). One table line per run, then the details. M10's column
// peak_cntReq reads as M11's peak_histReq.
//
//   sort_compare NEW_DIR REF_DIR [out.md]
#include <cstdio>
#include <string>
#include <vector>

#include "sort_kit.h"

using namespace sortkit;

namespace {

using Row = Ordered;   // column -> value, in the header's order
std::string canon(const std::string& c) { return c == "peak_cntReq" ? "peak_histReq" : c; }
std::vector<Row> read(const std::string& p) {
    std::vector<std::string> l = read_lines(p);
    std::vector<Row> out;
    if (l.empty()) return out;
    std::vector<std::string> h = split_all(l[0], ',');
    for (size_t i = 1; i < l.size(); i++) {
        if (strip(l[i]).empty()) continue;                     // isBlank
        std::vector<std::string> v = split_all(l[i], ',');
        Row m;
        for (size_t k = 0; k < h.size(); k++) m.put(canon(h[k]), k < v.size() ? v[k] : "");
        out.push_back(m);
    }
    return out;
}
// Objects.equals on two possibly absent values
bool same(const std::string* a, const std::string* b) { return a && b ? *a == *b : a == b; }
std::string val(const std::string* v) { return v ? *v : "null"; }
bool parse_long(const std::string& s, int64_t& v) {
    if (s.empty()) return false;
    size_t i = s[0] == '-' || s[0] == '+' ? 1 : 0;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); k++) if (s[k] < '0' || s[k] > '9') return false;
    v = std::stoll(s);
    return true;
}

}  // namespace

int sort_compare_main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: sort_compare NEW_DIR REF_DIR [out.md]\n"); return 2; }
    const std::string n = java_path(argv[1]), r = java_path(argv[2]);
    std::string sum, det;
    sum += "# " + n + " against " + r + "\n\n";
    sum += "| run | path | TOTAL new | TOTAL ref | diff | steps that differ |\n|---|---|---|---|---|---|\n";
    std::vector<std::string> files;
    for (const std::string& f : list_sorted(n))
        if (ends_with(f, ".csv") && !ends_with(f, ".phases.csv") && f != "all.csv") files.push_back(f);
    int nsame = 0, ndiff = 0, missing = 0;
    for (const std::string& f : files) {
        const std::string g = resolve(r, f);
        const std::string id = remove_all(f, ".csv");
        if (!exists(g)) { missing++; continue; }
        std::vector<Row> A = read(resolve(n, f)), B = read(g);
        Ordered keys; std::vector<const Row*> bs;              // step -> its last row in B (LinkedHashMap.put)
        for (const Row& m : B) {
            const std::string* st = m.get("step"); const std::string k = val(st);
            bool found = false;
            for (size_t i = 0; i < keys.v.size(); i++) if (keys.v[i].first == k) { bs[i] = &m; found = true; }
            if (!found) { keys.v.emplace_back(k, ""); bs.push_back(&m); }
        }
        auto lookup = [&](const std::string& k) -> const Row* {
            for (size_t i = 0; i < keys.v.size(); i++) if (keys.v[i].first == k) return bs[i];
            return nullptr;
        };
        std::vector<std::string> steps; std::string tn, tr, path;
        for (const Row& m : A) {
            const std::string st = val(m.get("step"));
            const Row* o = lookup(st);
            if (path.empty() && m.get("path")) path = *m.get("path");
            if (st == "TOTAL") { tn = val(m.get("cycles")); tr = o == nullptr ? "?" : val(o->get("cycles")); }
            if (!o) { steps.push_back(st + " (missing in ref)"); continue; }
            std::vector<std::string> cols;
            for (auto& c : m.v) if (!same(&c.second, o->get(c.first))) cols.push_back(c.first + " " + val(o->get(c.first)) + " -> " + c.second);
            if (!cols.empty()) { steps.push_back(st); det += "- " + id + " " + st + ": " + join(cols, "; ") + "\n"; }
        }
        const std::string pn = resolve(n, id + ".phases.csv"), pr = resolve(r, id + ".phases.csv");
        if (exists(pn) && exists(pr)) {
            std::vector<Row> P = read(pn), Q = read(pr);
            if (P.size() != Q.size()) det += "- " + id + ": " + std::to_string(Q.size()) + " phases -> " + std::to_string(P.size()) + "\n";
            for (size_t i = 0; i < std::min(P.size(), Q.size()); i++) {
                const Row& x = P[i]; const Row& y = Q[i]; std::vector<std::string> cols;
                for (auto& c : x.v)
                    if (c.first != "go_at" && c.first != "end_at" && !same(&c.second, y.get(c.first)))
                        cols.push_back(c.first + " " + val(y.get(c.first)) + " -> " + c.second);
                if (!cols.empty()) det += "  - phase " + val(x.get("phase")) + " " + val(x.get("name")) + ": " + join(cols, "; ") + "\n";
            }
        }
        int64_t d = 0, a = 0, b = 0;
        if (parse_long(tn, a) && parse_long(tr, b)) d = a - b;
        if (steps.empty()) nsame++; else ndiff++;
        sum += "| " + id + " | " + path + " | " + tn + " | " + tr + " | " + std::to_string(d) + " | " + (steps.empty() ? "identical" : join(steps, ", ")) + " |\n";
    }
    sum += "\n" + std::to_string(nsame) + " identical, " + std::to_string(ndiff) + " differ, " + std::to_string(missing) + " not in the reference.\n\n## Differences\n\n" + det;
    std::fwrite(sum.data(), 1, sum.size(), stdout);
    if (argc > 3 && !write_file(argv[3], sum)) { std::fprintf(stderr, "sort_compare: cannot write %s\n", argv[3]); return 1; }
    return 0;
}

#ifndef SORT_KIT_NO_MAIN
int main(int argc, char** argv) { return sort_compare_main(argc, argv); }
#endif
