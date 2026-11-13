// shard_summary: the acceptance kit's summary step, v1.3's run_shard.sh inline Python in
// C++ (D40: no Python), line for line: OUT/results.csv read back, OUT/SUMMARY.txt (rows and time per stage, the
// stage-B/F stall averages, the throughput) and OUT/VERDICT.txt (each stage's windows matched, the first 50 failures,
// PASS or FAIL); exit 0 only if every window of the range matched in every stage. Python's formats kept: "{:.1f}" and
// "{:.0f}" (correctly rounded, as printf), a list as "[1, 2]", dicts in insertion order, sorted() where it sorted.
//
//   shard_summary OUT PROGRAM FIRST N STAGES WALL_SECONDS JOBS FULL_EVERY MANIFEST
//     (run_shard.sh's arguments to its Python, in that order; N a count or "all")
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../stage_a.h"

namespace {

using stagea::fmt;

// csv.reader on one line (no newlines inside fields: ReplayShard's q() removes them).
std::vector<std::string> csv_fields(const std::string& line) {
    std::vector<std::string> r;
    std::string f;
    bool quoted = false, any = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        any = true;
        if (quoted) {
            if (c == '"') { if (i + 1 < line.size() && line[i + 1] == '"') { f += '"'; i++; } else quoted = false; }
            else f += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') { r.push_back(f); f.clear(); }
        else f += c;
    }
    if (any) r.push_back(f);
    return r;
}
std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); i++) { if (i) s += sep; s += v[i]; }
    return s;
}
std::string py_list(const std::vector<long long>& v, size_t max) {
    std::string s = "[";
    for (size_t i = 0; i < v.size() && i < max; i++) s += fmt("%s%lld", i ? ", " : "", v[i]);
    return s + "]";
}
long long py_int(const std::string& s) {   // int(s): a malformed number stops the step, as Python's ValueError would
    char* e;
    long long v = std::strtoll(s.c_str(), &e, 10);
    if (s.empty() || *e) { std::fprintf(stderr, "shard_summary: invalid literal for int(): '%s'\n", s.c_str()); std::exit(1); }
    return v;
}
// re.search(r"waits image/code/newexp/exp/rlog/wlog/outq ([0-9/]+) logq (\d+)", s): the seven stall counts and logq.
bool waits(const std::string& s, std::vector<long long>& out) {
    const std::string key = "waits image/code/newexp/exp/rlog/wlog/outq ";
    for (size_t at = s.find(key); at != std::string::npos; at = s.find(key, at + 1)) {
        size_t p = at + key.size(), q0 = p;
        while (p < s.size() && ((s[p] >= '0' && s[p] <= '9') || s[p] == '/')) p++;
        if (p == q0 || s.compare(p, 6, " logq ") != 0) continue;
        size_t d = p + 6, d0 = d;
        while (d < s.size() && s[d] >= '0' && s[d] <= '9') d++;
        if (d == d0) continue;
        std::vector<long long> v;
        std::string g = s.substr(q0, p - q0), cur;
        for (char c : g + "/") { if (c == '/') { if (cur.empty()) { std::fprintf(stderr, "shard_summary: invalid literal for int(): ''\n"); std::exit(1); } v.push_back(std::atoll(cur.c_str())); cur.clear(); } else cur += c; }
        v.push_back(std::atoll(s.substr(d0, d - d0).c_str()));
        out = v;
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 10) { std::fprintf(stderr, "usage: shard_summary OUT PROGRAM FIRST N STAGES WALL_SECONDS JOBS FULL_EVERY MANIFEST\n"); return 2; }
    std::string out = argv[1], prog = argv[2], nn = argv[4], stages = argv[5], mf = argv[9];
    long long f = py_int(argv[3]), tot = py_int(argv[6]), j = py_int(argv[7]), x = py_int(argv[8]), n;
    std::set<long long> want;
    if (nn == "all") {
        std::vector<stagea::Program> progs;
        std::string err;
        if (!stagea::read_manifest(mf, progs, err)) { std::fprintf(stderr, "shard_summary: %s\n", err.c_str()); return 1; }
        for (auto& p : progs) if (p.name == prog) for (auto& w : p.windows) want.insert(w.index);
        n = (long long)want.size();
        f = want.empty() ? 0 : *want.begin();
    } else {
        n = py_int(nn);
        for (long long w = f; w < f + n; w++) want.insert(w);
    }
    // ---- results.csv
    std::map<std::string, std::set<long long>> seen;
    std::vector<std::pair<std::string, std::string>> bad;
    std::map<std::string, std::vector<long long>> per;               // sorted() by stage, as Python sorts the items
    std::vector<std::pair<std::pair<std::string, long long>, std::string>> cyc;   // insertion order (a dict)
    std::map<std::string, std::vector<std::vector<long long>>> wt;
    FILE* in = std::fopen((out + "/results.csv").c_str(), "r");
    if (!in) { std::fprintf(stderr, "shard_summary: no %s/results.csv\n", out.c_str()); return 1; }
    std::string line;
    for (int c; (c = std::fgetc(in)) != EOF || !line.empty();) {
        if (c != EOF && c != '\n') { line += char(c); continue; }
        std::string l = line;
        line.clear();
        if (!l.empty() && l.back() == '\r') l.pop_back();
        std::vector<std::string> r = csv_fields(l);
        // (a blank line: csv.reader yields [], which is malformed below)
        if (r.size() < 9) { bad.push_back({"?", "malformed line " + join(r, ",")}); if (c == EOF) break; continue; }
        std::string st = r[3].substr(0, r[3].find(':'));
        if (r[4] != "OK") { bad.push_back({r[1], r[3] + " " + r[4] + " " + r[8].substr(0, 300)}); if (c == EOF) break; continue; }
        long long w = py_int(r[1]);
        seen[st].insert(w);
        bool found = false;
        for (auto& e : cyc) if (e.first.first == st && e.first.second == w) { e.second = r[6]; found = true; }
        if (!found) cyc.push_back({{st, w}, r[6]});
        std::vector<long long> mw;
        if (waits(r[8], mw)) wt[st].push_back(mw);
        auto& p = per[st];
        if (p.empty()) p = {0, 0, 0};
        p[0] += py_int(r[5]); p[1] += py_int(r[7]); p[2] += 1;
        if (c == EOF) break;
    }
    std::fclose(in);
    for (auto& e : cyc) {
        if (e.first.first != "B") continue;
        for (auto& g : cyc)
            if (g.first.first == "F" && g.first.second == e.first.second && g.second != e.second)
                bad.push_back({std::to_string(e.first.second), "stage B (tile 0 alone) " + e.second + " cycles, stage F (full chip) " + g.second});
    }
    std::map<std::string, std::vector<long long>> missing;
    auto missing_of = [&](const std::string& st, const std::set<long long>& from) {
        std::vector<long long> m;
        const std::set<long long>& s = seen[st];
        for (long long w : from) if (!s.count(w)) m.push_back(w);
        return m;
    };
    for (char c : stages) missing[std::string(1, c)] = missing_of(std::string(1, c), want);
    std::set<long long> every_x;
    if (x > 0) for (long long w : want) if (w % x == 0) every_x.insert(w);
    if (x > 0 && stages.find('F') == std::string::npos) { stages += "F"; missing["F"] = missing_of("F", every_x); }
    // ---- SUMMARY.txt
    std::string s = fmt("program %s, windows %lld..%lld (%lld%s), stages %s, %lld JVMs, wall %lld s\n", prog.c_str(), f, f + n - 1, n,
                        nn != "all" ? "" : ", all the manifest lists", stages.c_str(), j, tot);
    for (auto& [st, p] : per) {
        double rws = double(p[0]), ms = double(p[1]), k = double(p[2]);
        s += fmt("stage %s: %lld windows OK, %lld rows, %.1f JVM-s, %.0f rows per JVM-second, %.2f s per window\n", st.c_str(), p[2], p[0], ms / 1000,
                 rws / std::max(ms, 1.0) * 1000, ms / std::max(k, 1.0) / 1000);
    }
    const char* names[7] = {"image", "code", "newexp", "exp", "rlog", "wlog", "outq"};
    for (auto& [st, ws] : wt) {
        size_t k = ws.size();
        double avg[7];
        long long mx[7];
        for (int i = 0; i < 7; i++) {
            long long sum = 0; mx[i] = ws[0][size_t(i)];
            for (auto& w : ws) { sum += w[size_t(i)]; mx[i] = std::max(mx[i], w[size_t(i)]); }
            avg[i] = double(sum) / double(k);
        }
        s += fmt("stage %s stalls, cycles per window over %zu windows (log queues %lld entries): ", st.c_str(), k, ws[0][7]);
        for (int i = 0; i < 7; i++) s += fmt("%s%s %.1f (max %lld)", i ? ", " : "", names[i], avg[i], mx[i]);
        s += "\n";
        long long rd = 0, wr = 0;
        for (auto& w : ws) { rd += w[4] != 0; wr += w[5] != 0; }
        s += fmt("stage %s log waits: read log %.1f, write log %.1f cycles per window; windows with any: read %lld, write %lld\n", st.c_str(), avg[4], avg[5], rd, wr);
    }
    if (tot) {
        long long sum = 0;
        for (char c : stages) { auto it = seen.find(std::string(1, c)); if (it != seen.end()) sum += (long long)it->second.size(); }
        s += fmt("throughput: %.0f windows per hour (all stages)\n", double(sum) / double(std::max<size_t>(stages.size(), 1)) / double(tot) * 3600);
    }
    FILE* so = std::fopen((out + "/SUMMARY.txt").c_str(), "w");
    std::fputs(s.c_str(), so);
    std::fclose(so);
    // ---- VERDICT.txt
    bool ok = bad.empty();
    for (auto& [st, m] : missing) ok = ok && m.empty();
    std::string v;
    for (char c : stages) {
        std::string st(1, c);
        long long tot_st = !(c == 'F' && x > 0) ? n : (long long)every_x.size();
        const std::vector<long long>& m = missing[st];
        v += fmt("%s stage %s: %lld of %lld windows matched", m.empty() ? "ok  " : "FAIL", st.c_str(), tot_st - (long long)m.size(), tot_st);
        if (!m.empty()) v += "; first missing " + py_list(m, 20);
        v += "\n";
    }
    for (size_t i = 0; i < bad.size() && i < 50; i++) v += "FAIL window " + bad[i].first + ": " + bad[i].second + "\n";
    v += std::string(ok && n > 0 ? "PASS" : "FAIL") + ": " + prog + " windows " + (nn != "all" ? fmt("%lld..%lld", f, f + n - 1) : fmt("(all %lld listed)", n)) + "\n";
    FILE* vo = std::fopen((out + "/VERDICT.txt").c_str(), "w");
    std::fputs(v.c_str(), vo);
    std::fclose(vo);
    return ok && n > 0 ? 0 : 1;
}
