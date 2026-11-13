// suite_recipes.cpp (WORKER23, P5, inventory C12): v1.3's RecipeTest (sim/ref/meshsim/test/RecipeTest.java, with
// SelfTest.java) on W20's harness, group for group, in Java's order and words. run_tests.sh's steps `recipes` (-1) and
// `recipes-shuf` (4); its output equals ref's log (tests/ctlsuites/recipes*.log). Usage: suite_recipes [shuffleSeed]
//
// The recipe book's tests (fw/RECIPES.md, the v1.2 recipes fw/recipes/rcp_*.c). Every recipe runs on all 64 tiles and is
// checked bit-exactly, with no invariant violations (or exactly the expected ones), and its cycles are reported. The
// snippet check makes every code block in RECIPES.md equal to the RECIPE ... END region it names, so the book can't
// drift from the code that runs.
//
// IN PROGRESS (NOTES.md): ported so far: snippets, copy, cmem, status, rules, tables, replay, cc1. The groups that read C&C's phase log
// (goToReady, goToFirstRequest: W20's Cc::phase_log) and the cores' come next; until then main() runs the groups
// ported, so the output is ref's lines for those groups.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "units/ag.h"
#include "units/replayer.h"
#include "../java_random.h"
#include "../suite.h"
#include "ctl.h"

using namespace ctl;
using mach::Chip;
using mach::Mach;

namespace {

int64_t shuffle = -1;
std::vector<std::string> table;
void out(const std::string& s) { std::printf("%s\n", s.c_str()); }
void meas(const char* recipe, const std::string& what, int64_t cycles) { table.push_back(suite::str("  %-14s %-72s %8lld", recipe, what.c_str(), (long long)cycles)); }
void meas(const char* recipe, const std::string& what, const std::string& cycles) { table.push_back(suite::str("  %-14s %-72s %8s", recipe, what.c_str(), cycles.c_str())); }

// ================================================================ helpers
mach::MachConfig cfg() { return Mach::cfg(shuffle); }
void no_violations(Chip& c, const char* what) {
    const uint64_t v = c.violations();
    suite::check(v == 0, "%s: no invariant violations%s", what, v == 0 ? "" : ("\n" + c.violation_report()).c_str());
}
void finish(Mach& m, const char* what, uint64_t max) {
    m.c.cc().command(BROADCAST, 0).go();
    const bool ok = m.run([&] { return m.all_stopped(); }, max);
    suite::check(ok && m.all_exited(), "%s: every tile ran to its exit: %s", what, m.halt_summary().c_str());
}
// Every tile back in wait_for_command() since the last GO (C&C's own state aside: usable inside a C&C step).
bool tiles_idle(Mach& m) {
    if (!m.all_in_wfi()) return false;
    const int64_t go = m.c.cc().last_go_at();
    for (unsigned k = 0; k < 64; k++) { const units::RegBlockState b = m.c.tile(k).block(); if (!b.done || b.last_done_at <= go) return false; }
    return true;
}
bool idle(Mach& m) { return !m.c.cc().busy() && tiles_idle(m); }
int64_t rec(Chip& c, unsigned k, uint32_t chunk) { return int64_t(c.tile(k).sram_peek64(chunk)); }
int64_t rec(int32_t lo, int32_t hi) { return int64_t(uint64_t(uint32_t(lo)) | uint64_t(uint32_t(hi)) << 32); }
void poke64(Chip& c, unsigned k, uint32_t chunk, int64_t v) { c.tile(k).sram_poke64(chunk, uint64_t(v)); }
// the tile's last WFI wake (ref's wfiLog.get(size - 1)[1])
int64_t last_wake(Chip& c, unsigned k) {
    const units::Rv32& r = core(c, k);
    if (r.wfi_n == 0 || r.wfi_n > units::Rv32::kWfiLog) FATAL("recipes: tile %u: %u WFIs (the log keeps %u)", k, r.wfi_n, units::Rv32::kWfiLog);
    return r.wfi_log[r.wfi_n - 1][1];
}


// java.util.Random's nextInt(bound), nextInt(), nextLong()
int32_t next_int(test::JavaRandom& r, int32_t bound) {
    if ((bound & -bound) == bound) return r.next_int_pow2(bound);
    int32_t bits, val;
    do { bits = r.next(31); val = bits % bound; } while (bits - val + (bound - 1) < 0);
    return val;
}
int32_t next_int(test::JavaRandom& r) { return r.next(32); }
int64_t next_long(test::JavaRandom& r) { const int64_t hi = r.next(32); return int64_t(uint64_t(hi) << 32) + int64_t(r.next(32)); }

// C&C's phase log (ref's cc.phaseLog: {all READY seen, GO store} per GO; entry 0 is the boot's GO)
// Cycles from command k's GO (0 = the first command after boot) to all 64 READY after it.
int64_t go_to_ready(Mach& m, unsigned k) { const auto ph = m.c.cc().phase_log(); return ph.at(k + 2)[0] - ph.at(k + 1)[1]; }
// A synchronised start (spec 6.4): from C&C's GO store (phase k) to each tile's start store and first request.
void go_to_first_request(Mach& m, unsigned k, const char* recipe, const char* run) {
    const int64_t go = m.c.cc().phase_log().at(k + 1)[1];                 // v1.2: entry 0 is the boot's GO
    int64_t fmin = INT64_MAX, fmax = 0, smin = INT64_MAX, smax = 0, lead = -1; bool same_lead = true;
    for (unsigned t = 0; t < 64; t++) {
        const units::AddrGen& ag = m.c.unit<units::AddrGen>(unit_name(t, "ag"));
        const int64_t f = ag.first_request_at - go, st = ag.started_at - 1 - go, l = ag.first_request_at - (ag.started_at - 1);
        fmin = std::min(fmin, f); fmax = std::max(fmax, f); smin = std::min(smin, st); smax = std::max(smax, st);
        if (lead < 0) lead = l; else if (lead != l) same_lead = false;
    }
    suite::check(same_lead, "%s: the lead-in is the same on every tile (%lld cycles from the start store)", recipe, (long long)lead);
    suite::check(fmax - fmin <= 1, "%s: every tile's first request within the 0/+1 start skew (%lld..%lld)", recipe, (long long)fmin, (long long)fmax);
    meas(recipe, std::string(run) + ": GO store -> start store, every tile", std::to_string(smin) + (smax > smin ? "-" + std::to_string(smax) : ""));
    meas(recipe, std::string(run) + ": start store -> first request (the lead-in)", lead);
    meas(recipe, std::string(run) + ": GO store -> first request, every tile", std::to_string(fmin) + (fmax > fmin ? "-" + std::to_string(fmax) : ""));
}

// ================================================================ the snippet check (Java's patterns, by hand)
bool jspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' || (c >= 0x1C && c <= 0x1F); }
bool rspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }   // regex \s
bool read_file(const std::string& p, std::string& s) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    std::stringstream b; b << f.rdbuf(); s = b.str(); return true;
}
size_t nonspace_run(const std::string& s, size_t at) { size_t e = at; while (e < s.size() && !rspace(s[e])) e++; return e; }
// Java's norm(): trailing spaces and tabs off each line, stripTrailing, the least indent of the non-blank lines off
// every line (blank ones empty), each line + '\n', then strip
std::string norm(std::string s) {
    std::string t;
    for (size_t i = 0; i < s.size(); i++) {               // replaceAll("[ \t]+\n", "\n")
        if (s[i] == ' ' || s[i] == '\t') {
            size_t j = i; while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
            if (j < s.size() && s[j] == '\n') { i = j - 1; continue; }
            t.append(s, i, j - i); i = j - 1; continue;
        }
        t += s[i];
    }
    while (!t.empty() && jspace(t.back())) t.pop_back();   // stripTrailing
    std::vector<std::string> l; size_t at = 0;
    for (;;) { size_t e = t.find('\n', at); if (e == std::string::npos) { l.push_back(t.substr(at)); break; } l.push_back(t.substr(at, e - at)); at = e + 1; }
    auto blank = [](const std::string& x) { for (char c : x) if (!jspace(c)) return false; return true; };
    size_t ind = std::string::npos;
    for (const std::string& x : l) if (!blank(x)) { size_t k = 0; while (k < x.size() && jspace(x[k])) k++; ind = std::min(ind, k); }
    std::string sb;
    for (const std::string& x : l) { if (!blank(x)) sb += x.substr(ind); sb += '\n'; }
    size_t b = 0, e = sb.size();
    while (b < e && jspace(sb[b])) b++;
    while (e > b && jspace(sb[e - 1])) e--;
    return sb.substr(b, e - b);
}
struct Region { std::string name, body; };
// SRC: "/\* RECIPE (\S+) \*/\n(.*?)\n[ \t]*/\* END \*/", every match left to right
std::vector<Region> regions_of(const std::string& t) {
    std::vector<Region> out;
    const std::string mk = "/* RECIPE ", end = "/* END */";
    size_t i = 0;
    while ((i = t.find(mk, i)) != std::string::npos) {
        const size_t n0 = i + mk.size(), n1 = nonspace_run(t, n0);
        if (n1 > n0 && t.compare(n1, 4, " */\n") == 0) {
            const size_t start = n1 + 4;
            for (size_t p = t.find('\n', start); p != std::string::npos; p = t.find('\n', p + 1)) {
                size_t q = p + 1; while (q < t.size() && (t[q] == ' ' || t[q] == '\t')) q++;
                if (t.compare(q, end.size(), end) == 0) { out.push_back({t.substr(n0, n1 - n0), t.substr(start, p - start)}); i = q + end.size(); goto next; }
            }
        }
        i++;
    next:;
    }
    return out;
}
struct Block { std::string name, file, body; };
// MD: "<!-- recipe: (\S+) \((\S+)\) -->\s*```(?:c|java|asm)\n(.*?)```", every match left to right
std::vector<Block> blocks_of(const std::string& t) {
    std::vector<Block> out;
    const std::string mk = "<!-- recipe: ";
    size_t i = 0;
    while ((i = t.find(mk, i)) != std::string::npos) {
        const size_t n0 = i + mk.size(), n1 = nonspace_run(t, n0);
        if (n1 > n0 && t.compare(n1, 2, " (") == 0) {
            const size_t f0 = n1 + 2, f1 = nonspace_run(t, f0);   // \S+ then "\) -->": the run ends in ")"
            if (f1 > f0 + 1 && t[f1 - 1] == ')' && t.compare(f1, 4, " -->") == 0) {
                size_t q = f1 + 4; while (q < t.size() && rspace(t[q])) q++;
                size_t body = std::string::npos;
                for (const char* lang : {"```c\n", "```java\n", "```asm\n"}) if (t.compare(q, std::char_traits<char>::length(lang), lang) == 0) { body = q + std::char_traits<char>::length(lang); break; }
                if (body != std::string::npos) {
                    const size_t e = t.find("```", body);
                    if (e != std::string::npos) { out.push_back({t.substr(n0, n1 - n0), t.substr(f0, f1 - 1 - f0), t.substr(body, e - body)}); i = e + 3; continue; }
                }
            }
        }
        i++;
    }
    return out;
}
void snippets() {
    suite::group("snippets");
    std::string md;
    if (!read_file("fw/RECIPES.md", md)) { suite::check(false, "snippets: java.nio.file.NoSuchFileException: fw/RECIPES.md"); return; }
    std::vector<std::string> names;
    if (DIR* d = opendir("fw/recipes")) { while (dirent* e = readdir(d)) names.push_back(e->d_name); closedir(d); }
    std::sort(names.begin(), names.end());
    std::map<std::string, std::string> src, file;          // ref's LinkedHashMaps: read by key only
    std::vector<std::string> order;                        // their insertion order
    for (const std::string& n : names) {
        auto ends = [&](const char* x) { const size_t k = std::char_traits<char>::length(x); return n.size() >= k && n.compare(n.size() - k, k, x) == 0; };
        if (!(ends(".c") || ends(".h") || ends(".S"))) continue;
        std::string text; read_file("fw/recipes/" + n, text);
        for (const Region& r : regions_of(text)) {
            suite::check(!src.count(r.name), "recipe %s marked once", r.name.c_str());
            if (!src.count(r.name)) order.push_back(r.name);
            src[r.name] = norm(r.body); file[r.name] = n;
        }
    }
    int n = 0; std::set<std::string> seen;
    for (const Block& b : blocks_of(md)) {
        n++;
        suite::check(src.count(b.name), "RECIPES.md's block %s has a RECIPE region in the sources", b.name.c_str());
        if (!src.count(b.name)) continue;
        suite::eqo(file[b.name], b.file, "block %s: the file named is the one that holds it", b.name.c_str());
        suite::eqo(src[b.name], norm(b.body), "block %s: RECIPES.md's text equals sw/recipes/%s", b.name.c_str(), file[b.name].c_str());
        seen.insert(b.name);
    }
    for (const std::string& s : order) suite::check(seen.count(s), "RECIPE region %s (%s) is quoted in RECIPES.md", s.c_str(), file[s].c_str());
    out(suite::str("snippets: %d code blocks in RECIPES.md, %zu RECIPE regions in wip/sw/recipes/", n, src.size()));
}


// ================================================================ 1 boot and the command loop
void cmdloop() {
    suite::group("cmdloop");
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_cmdloop");
    std::vector<uint32_t> a31(7); int32_t s31 = 0; for (unsigned i = 0; i < a31.size(); i++) { a31[i] = 1000 + i; s31 += int32_t(a31[i]); }   // a full block: 7 arguments
    m.c.cc().command(BROADCAST, 1 << 24 | 3, {10, 20, 30}).go();
    for (unsigned t = 0; t < 64; t++) m.c.cc().command(t, 1 << 24 | 2, {t * 100, 5});
    m.c.cc().go().command(BROADCAST, 1 << 24 | uint32_t(a31.size()), a31).go();
    m.c.cc().command(BROADCAST, 2 << 24).go();
    const bool ok = m.run([&] { return m.all_stopped(); }, 20000);
    suite::check(ok && m.all_exited(), "every tile ran the command loop to CMD_EXIT: %s", m.halt_summary().c_str());
    for (unsigned me = 0; me < 64; me++) {
        suite::eq(60 + me, m.word(me, im, "res", 0), "tile %u: broadcast, even block, 3 arguments", me);
        suite::eq(me * 100 + 5 + me, m.word(me, im, "res", 1), "tile %u: unicast, odd block, its own arguments", me);
        suite::eq(s31 + int32_t(me), int32_t(m.word(me, im, "res", 2)), "tile %u: broadcast, even block, 7 arguments (a full block)", me);
        suite::eq(3, m.word(me, im, "ncmd", 0), "tile %u: three sums", me);
    }
    no_violations(m.c, "cmdloop");
    meas("cmdloop", "a 3-argument command: GO -> all 64 READY", go_to_ready(m, 0));
    meas("cmdloop", "a 7-argument command: GO -> all 64 READY", go_to_ready(m, 2));
}

// ================================================================ 4 histogram digit, prefix sum, bucket passes
void bucket() {
    suite::group("bucket");
    Mach m(cfg());
    m.boot("rcp_bucket");
    std::vector<int64_t> in[64];
    for (unsigned k = 0; k < 64; k++) {
        test::JavaRandom r(1000 + k);
        const int n = 200;                                    // v1.2: the command's n on every tile
        in[k].resize(n);
        for (int i = 0; i < n; i++) {
            const int32_t key = k % 8 == 0 ? int32_t(uint32_t(next_int(r, 16)) * 0x01010101u) : next_int(r);
            in[k][size_t(i)] = rec(key, i); poke64(m.c, k, 2 * uint32_t(i), in[k][size_t(i)]);
        }
    }
    m.c.cc().command(BROADCAST, 1 << 24 | 200).go();
    finish(m, "bucket", 200000);
    for (unsigned k = 0; k < 64; k++) {
        std::vector<int64_t> b = in[k];
        std::stable_sort(b.begin(), b.end(), [](int64_t x, int64_t y) { return uint32_t(x) < uint32_t(y); });   // stable: ties keep input order
        int bad = 0;
        for (size_t i = 0; i < b.size(); i++) if (rec(m.c, k, 0x40000 + 2 * uint32_t(i)) != b[i]) bad++;
        suite::eq(0, bad, "tile %u: %zu records sorted by the 32-bit key, stable (values are input positions)", k, b.size());
        int32_t h = 0; for (unsigned t = 0; t < 256; t++) h += int32_t(m.c.tile(k).hist_peek(t));
        suite::eq(0, h, "tile %u: HIST clear at the end", k);
    }
    no_violations(m.c, "bucket");
    meas("bucket", "200 records: count copy + 4 bucket passes: GO -> all READY", go_to_ready(m, 0));
}

// ================================================================ 5 augmented transfers
int n_aug(unsigned t) { return 20 + int((t * 3) & 15); }
void aug() {
    suite::group("aug");
    Mach m(cfg());
    m.boot("rcp_aug");
    for (unsigned k = 0; k < 64; k++) {
        for (unsigned i = 0; i < 64 + k; i++) m.c.tile(k).sram_poke(i, 0x70000000u + k * 0x1000 + i * 3);
        for (int i = 0; i < n_aug(k); i++) poke64(m.c, k, 0x20000 + 2 * uint32_t(i), rec(int32_t(k << 16 | unsigned(i)), int32_t(0xDEAD0000u | unsigned(i))));
    }
    m.c.cc().command(BROADCAST, 1 << 24 | 64).go().command(BROADCAST, 2 << 24).go().command(BROADCAST, 3 << 24).go();
    finish(m, "aug", 30000);
    for (unsigned me = 0; me < 64; me++) {
        int bad = 0;
        for (unsigned i = 0; i < 64; i++) if (rec(m.c, me, 0x40000 + 2 * i) != rec(int32_t(0x70000000u + me * 0x1000 + i * 3), int32_t(i))) bad++;
        suite::eq(0, bad, "tile %u: local form: 64 raw keys -> (key, j), j = 0, 1, 2, ... (v1.2: 64 on every tile)", me);
        const unsigned l = (me + 63) & 63, r = (me + 1) & 63; const int nl = n_aug(l), nr = n_aug(r); bad = 0;
        for (int j = 0; j < nl; j++) if (rec(m.c, me, 0x80000 + 2 * uint32_t(j)) != rec(int32_t(l << 16 | unsigned(j)), j)) bad++;
        for (int j = 0; j < nr; j++) if (rec(m.c, me, 0x80000 + 2 * uint32_t(nl + j)) != rec(int32_t(r << 16 | unsigned(j)), nl + j)) bad++;
        suite::eq(0, bad, "tile %u: remote form: the left's %d then the right's %d records, high word = position in the buffer", me, nl, nr);
    }
    no_violations(m.c, "aug");
    meas("aug", "local form, 64 keys: GO -> all READY", go_to_ready(m, 0));
    meas("aug", "remote form, start command (two neighbours, 20-35 records each): GO -> all READY", go_to_ready(m, 2));
}

// ================================================================ 6 gather
void gather() {
    suite::group("gather");
    Mach m(cfg());
    m.boot("rcp_gather");
    std::vector<int> perm[64];
    for (unsigned k = 1; k <= 2; k++) {
        for (unsigned t = 0; t < 64; t++) {
            const int n = 50 + int(t);
            test::JavaRandom r(77 * k + t);
            perm[t].resize(size_t(n));
            for (int i = 0; i < n; i++) perm[t][size_t(i)] = i;
            for (int i = n - 1; i > 0; i--) { const int j = next_int(r, i + 1); std::swap(perm[t][size_t(i)], perm[t][size_t(j)]); }
            for (int i = 0; i < n; i++) poke64(m.c, t, 2 * uint32_t(i), rec(i * 11, perm[t][size_t(i)]));
            for (int j = 0; j < n; j++) for (unsigned q = 0; q < k; q++)
                poke64(m.c, t, 0x40000 + 2 * k * uint32_t(j) + 2 * q, rec(int32_t(t << 24 | unsigned(j) << 4 | q), int32_t(0xBEEF0000u | k)));
        }
        m.c.cc().command(BROADCAST, 1 << 24 | k).go();
        suite::check(m.run([&] { return idle(m); }, 40000), "k = %u: every tile gathered", k);
        for (unsigned t = 0; t < 64; t++) {
            int bad = 0; const int n = 50 + int(t);
            for (int i = 0; i < n; i++) for (unsigned q = 0; q < k; q++)
                if (rec(m.c, t, 0x80000 + 2 * k * uint32_t(i) + 2 * q) != rec(int32_t(t << 24 | unsigned(perm[t][size_t(i)]) << 4 | q), int32_t(0xBEEF0000u | k))) bad++;
            suite::eq(0, bad, "tile %u, k = %u: %d values in index order", t, k, n);
        }
    }
    finish(m, "gather", 10000);
    no_violations(m.c, "gather");
    meas("gather", "k = 1, 50-113 values per tile: GO -> all READY", go_to_ready(m, 0));
    meas("gather", "k = 2, 50-113 values per tile: GO -> all READY", go_to_ready(m, 1));
}

// ================================================================ 7 one-word exchange, words to one tile
void xchg() {
    suite::group("xchg");
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_xchg");
    m.c.cc().command(BROADCAST, 1 << 24).go().command(BROADCAST, 2 << 24).go();
    finish(m, "xchg", 20000);
    for (unsigned me = 0; me < 64; me++) {
        int bad = 0;
        for (unsigned s2 = 0; s2 < 64; s2++) if (m.word(me, im, "in", s2) != s2 * 64 + me) bad++;
        suite::eq(0, bad, "tile %u: in[s] = tile s's word to this tile (s x 64 + me), all 64", me);
    }
    int bad4 = 0; for (unsigned i = 0; i < 4; i++) if (m.word(0, im, "box", i) != 64 + i) bad4++;
    suite::eq(0, bad4, "tile 0: tile 1's four words in box, ROUTER_ADDR auto-incremented");
    no_violations(m.c, "xchg");
    meas("xchg", "one-word exchange, 64 words per tile: GO -> all READY", go_to_ready(m, 0));
    meas("xchg", "four words to one tile: GO -> all READY", go_to_ready(m, 1));
}

// ================================================================ 8 vector exchange with the neighbours
void xvec() {
    suite::group("xvec");
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_xvec");
    m.c.cc().command(BROADCAST, 1 << 24).go();
    finish(m, "xvec", 20000);
    const uint32_t base = im.sym("nb");
    for (unsigned me = 0; me < 64; me++) {
        int bad = 0;
        const unsigned from[4] = {(me + 63) & 63, (me + 1) & 63, (me + 56) & 63, (me + 8) & 63};   // left, right, above, below
        for (unsigned k = 0; k < 4; k++) for (unsigned i = 0; i < 6; i++) if (m.word(me, base + 4 * (6 * k + i)) != from[k] * 16 + i) bad++;
        suite::eq(0, bad, "tile %u: nb[0..3] from left, right, above, below", me);
    }
    no_violations(m.c, "xvec");
    meas("xvec", "6 words to each of 4 neighbours: GO -> all READY", go_to_ready(m, 0));
}

// ================================================================ 9 all-to-all
int cnt_a(unsigned s2, unsigned d) { return int((s2 * 7 + d * 3) & 7); }
void a2a() {
    suite::group("a2a");
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_a2a");
    for (unsigned t = 0; t < 64; t++) {
        uint32_t at = 0;
        for (unsigned d = 0; d < 64; d++) for (int j = 0; j < cnt_a(t, d); j++) { poke64(m.c, t, 2 * at, rec(int32_t(t << 24 | d << 16 | unsigned(j)), ~j)); at++; }
    }
    m.c.cc().command(BROADCAST, 1 << 24).go().command(BROADCAST, 2 << 24).go().command(BROADCAST, 3 << 24).go();
    finish(m, "a2a", 50000);
    int64_t bound = 0; int R[16] = {};
    for (unsigned g = 0; g < 16; g++) { for (unsigned s2 = 0; s2 < 64; s2++) for (int off : mach::A2A::G4[g]) R[g] = std::max(R[g], cnt_a(s2, (s2 + unsigned(off)) & 63)); bound += 4LL * R[g]; }
    for (unsigned me = 0; me < 64; me++) {
        uint32_t at = 0; int bad = 0;
        for (unsigned s2 = 0; s2 < 64; s2++) for (int j = 0; j < cnt_a(s2, me); j++) { if (rec(m.c, me, 0x40000 + 2 * at) != rec(int32_t(s2 << 24 | me << 16 | unsigned(j)), ~j)) bad++; at++; }
        suite::eq(0, bad, "tile %u: %u records, each source's run in source order", me, at);
        int rb = 0; for (unsigned g = 0; g < 16; g++) if (int(m.word(me, im, "R", g)) != R[g]) rb++;
        suite::eq(0, rb, "tile %u: R[g] from the reduction = the global group maxima", me);
    }
    no_violations(m.c, "a2a");
    meas("a2a", "counts exchange + group maxima: GO -> all READY", go_to_ready(m, 0));
    meas("a2a", "reduction of R + configure: GO -> all READY", go_to_ready(m, 1));
    meas("a2a", "start: GO -> all READY (timetable sum R_g * 4 = " + std::to_string(bound) + ")", go_to_ready(m, 2));
    go_to_first_request(m, 2, "a2a", "start (a group run)");
}

// ================================================================ 11 the read port
void rdport() {
    suite::group("rdport");
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_rdport");
    std::vector<int> keys[64];
    for (unsigned t = 0; t < 64; t++) {
        test::JavaRandom r(500 + t);
        const int n = 300 + int(t);
        keys[t].resize(size_t(n));
        for (int i = 0; i < n; i++) keys[t][size_t(i)] = 100 + next_int(r, 2000);
        std::sort(keys[t].begin(), keys[t].end());
        for (int i = 0; i < n; i++) m.c.tile(t).sram_poke(0x60000 + uint32_t(i), uint32_t(keys[t][size_t(i)]));
    }
    const std::vector<uint32_t> q = {0, 50, 777, 1234, 2099, 2100, 0x7FFFFFFF};   // 7: a command's argument block (v1.0 had 8)
    // v1.2 (recipe 11): command 1 reads key 0; command 2 searches the first n = 300 keys for the seven arguments
    m.c.cc().command(BROADCAST, 1 << 24).go().command(BROADCAST, 2 << 24 | 300, q).go();
    finish(m, "rdport", 30000);
    for (unsigned t = 0; t < 64; t++) {
        suite::eq(keys[t][0], m.word(t, im, "first", 0), "tile %u: one read", t);
        int bad = 0;
        for (unsigned i = 0; i < q.size(); i++) {
            int lo = 0; while (lo < 300 && int64_t(keys[t][size_t(lo)]) < int64_t(int32_t(q[i]))) lo++;
            if (int(m.word(t, im, "lb", i)) != lo) bad++;
        }
        suite::eq(0, bad, "tile %u: seven lower bounds", t);
    }
    no_violations(m.c, "rdport");
    meas("rdport", "one read: GO -> all READY", go_to_ready(m, 0));
    meas("rdport", "7 searches over 300 keys: GO -> all READY", go_to_ready(m, 1));
}

// ================================================================ 2 a stream run: local copy
void copy() {
    suite::group("copy");
    Mach m(cfg());
    m.boot("rcp_copy");
    for (unsigned k = 0; k < 64; k++) for (int i = 0; i < 120; i++) poke64(m.c, k, 2 * uint32_t(i), rec(int32_t(k << 20 | unsigned(i)), ~i));
    m.c.cc().command(BROADCAST, 1 << 24 | 100).go();
    finish(m, "copy", 30000);
    for (unsigned k = 0; k < 64; k++) {
        int bad = 0;
        for (int i = 0; i < 100; i++) if (rec(m.c, k, 0x40000 + 2 * uint32_t(i)) != rec(int32_t(k << 20 | unsigned(i)), ~i)) bad++;
        suite::eq(0, bad, "tile %u: 100 records copied SRC -> DST", k);
        suite::eq(0, rec(m.c, k, 0x40000 + 2 * 100), "tile %u: nothing past the run", k);
    }
    no_violations(m.c, "copy");
    meas("copy", "100 records (tile 0): start store -> first instruction after WFI",
         last_wake(m.c, 0) - m.c.tile(0).unit<units::AddrGen>("ag").started_at + 1);
}

// ================================================================ 3 block copy into controller memory
void cmem() {
    suite::group("cmem");
    Mach m(cfg());
    std::vector<uint32_t> ov(256);
    ov[0] = 0x00a50533; ov[1] = 0x12350513; ov[2] = 0x00008067;          // add a0, a0, a0; addi a0, a0, 0x123; ret
    for (unsigned i = 3; i < 256; i++) ov[i] = 0x5A5A0000u + i;
    for (unsigned k = 0; k < 64; k++) {
        for (unsigned i = 0; i < 16; i++) m.c.tile(k).sram_poke(0x20000 + i, k * 1000 + i);
        for (unsigned i = 0; i < 256; i++) m.c.tile(k).sram_poke(0x21000 + i, ov[i]);
    }
    const Mach::Image im = m.boot("rcp_cmem");
    const uint32_t OVL = PMEM_BASE + 0x6000;                  // v1.2: the recipe's overlay address (program memory)
    suite::check(im.code.size() * 4 <= 0x6000, "the program's code lies below the overlay at 0x01006000");
    m.c.cc().command(BROADCAST, 1 << 24).go();
    finish(m, "cmem", 20000);
    for (unsigned me = 0; me < 64; me++) {
        int32_t s = 0; for (unsigned i = 0; i < 16; i++) s += int32_t(me * 1000 + i);
        int32_t got = 0; for (unsigned i = 0; i < 16; i++) got += int32_t(m.word(me, im, "params", i));
        suite::eq(s, got, "tile %u: 16 parameter words in data memory (params[])", me);
        bool match = true; for (unsigned i = 0; i < 256; i++) if (m.c.tile(me).peek_local(OVL + 4 * i) != ov[i]) match = false;
        suite::check(match, "tile %u: 256 overlay words in program memory", me);
        suite::eq(2 * (me * 1000) + 0x123, m.word(me, im, "res", 0), "tile %u: the overlay ran on params[0]", me);
    }
    no_violations(m.c, "cmem");
    meas("cmem", "256 words into program memory: start store -> wake", last_wake(m.c, 0) - m.c.tile(0).unit<units::AddrGen>("ag").started_at + 1);
}

// ================================================================ status
void status() {
    suite::group("status");
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_status");
    m.c.cc().command(BROADCAST, 1 << 24).go();
    finish(m, "status", 20000);
    for (unsigned me = 0; me < 64; me++) {
        suite::eq(me, m.word(me, im, "me_out", 0), "tile %u: PARAMS' endpoint id", me);
        suite::eq(0x100000, m.word(me, im, "chunks_out", 0), "tile %u: sram_chunks() = 4 MB", me);
        suite::eq(1, m.word(me, im, "present_out", 0), "tile %u: peripheral 1 present (the replayer)", me);
        suite::eq(0, int32_t(m.word(me, im, "live_out", 0) & ~(1u << units::ST_LANDED)), "tile %u: STATUS: nothing running (the idle machine)", me);
    }
    no_violations(m.c, "status");
    out("status: PARAMS' endpoint id, size and peripherals, and STATUS, read on every tile");
}

// ================================================================ the rules (recipe 15)
void rules() {
    suite::group("rules");
    // v1.2 (recipe 15): the right forms only, each run on every tile; the wrong forms are described in the book
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_rules");
    for (unsigned k = 0; k < 64; k++) {
        for (int i = 0; i < 64; i++) poke64(m.c, k, 2 * uint32_t(i), rec(int32_t(k << 16 | unsigned(i)), 0xA0000 + i));
        for (unsigned i = 0; i < 64; i++) m.c.tile(k).sram_poke(0x40000 + 3 + i, 0xB0000 + i);
    }
    // rule_wait: a self-reply, waited for before done
    m.c.cc().command(BROADCAST, 1 << 24).go();
    suite::check(m.run([&] { return idle(m); }, 40000), "rule_wait ran");
    for (unsigned k = 0; k < 64; k++) suite::eq(7, m.word(k, im, "rbox", 0), "tile %u: rule_wait: the reply was in before done", k);
    // rule_disjoint: shift 64 records up by 8, through B
    m.c.cc().command(BROADCAST, 2 << 24).go();
    suite::check(m.run([&] { return idle(m); }, 80000), "rule_disjoint ran");
    int bad = 0;
    for (unsigned k = 0; k < 64; k++) for (int i = 0; i < 64; i++) if (rec(m.c, k, 2 * uint32_t(8 + i)) != rec(int32_t(k << 16 | unsigned(i)), 0xA0000 + i)) bad++;
    suite::eq(0, bad, "rule_disjoint: every tile's 64 records moved up by 8, through a disjoint buffer");
    for (unsigned k = 0; k < 64; k++) for (int i = 0; i < 64; i++) poke64(m.c, k, 2 * uint32_t(i), rec(int32_t(k << 16 | unsigned(i)), 0xA0000 + i));
    // rule_start: configure (3), then the start by identical code (4): 20 + (me & 7) records to the right-hand tile
    m.c.cc().command(BROADCAST, 3 << 24).go().command(BROADCAST, 4 << 24).go();
    suite::check(m.run([&] { return idle(m); }, 80000), "rule_start ran");
    bad = 0;
    for (unsigned k = 0; k < 64; k++) {
        const unsigned l = (k + 63) & 63;
        for (unsigned i = 0; i < 20 + (l & 7); i++) if (rec(m.c, k, 0x80000 + 2 * i) != rec(int32_t(l << 16 | i), int32_t(0xA0000 + i))) bad++;
    }
    suite::eq(0, bad, "rule_start: every tile received its left neighbour's 20 + (l & 7) records");
    // rule_align: a 64-bit stream to itself and a 32-bit stream to the right-hand tile in one group (B was rule_disjoint's
    // scratch buffer: its words are written again)
    for (unsigned k = 0; k < 64; k++) for (unsigned i = 0; i < 64; i++) m.c.tile(k).sram_poke(0x40000 + 3 + i, 0xB0000 + i);
    m.c.cc().command(BROADCAST, 5 << 24).go();
    suite::check(m.run([&] { return idle(m); }, 80000), "rule_align ran");
    bad = 0;
    for (unsigned k = 0; k < 64; k++) {
        for (int i = 0; i < 32; i++) if (rec(m.c, k, 0x80000 + 2 * uint32_t(i)) != rec(int32_t(k << 16 | unsigned(i)), 0xA0000 + i)) bad++;
        for (unsigned i = 0; i < 32; i++) if (m.c.tile(k).sram_peek(0x88000 + i) != 0xB0000 + i) bad++;
    }
    suite::eq(0, bad, "rule_align: the records to itself and the words from the left, in one group");
    finish(m, "rules", 20000);
    no_violations(m.c, "rules");
    out("rules: the right form of each rule (rule_wait, rule_disjoint, rule_start, rule_align) runs clean on every tile");
}




// ================================================================ 14 the synchronised start by identical code
int n_sync(unsigned t) { return 16 + int((t * 5) & 15); }
void sync_() {   // ref's sync() (unistd.h has a sync)
    suite::group("sync");
    Mach m(cfg());
    m.boot("rcp_sync");
    for (unsigned t = 0; t < 64; t++) {
        for (int i = 0; i < n_sync(t); i++) poke64(m.c, t, 2 * uint32_t(i), rec(int32_t(t << 16 | unsigned(i)), int32_t(0xC0DE0000u | unsigned(i))));
        m.c.tile(t).sram_poke(0x20000, uint32_t(n_sync(t))); m.c.tile(t).sram_poke(0x20001, uint32_t(n_sync((t + 63) & 63)));
    }
    m.c.cc().command(BROADCAST, 1 << 24).go().command(BROADCAST, 2 << 24).go();
    suite::check(m.run([&] { return idle(m); }, 40000), "every tile ran the set-up and the start");
    std::set<int64_t> starts;
    const int64_t go_at = m.c.cc().last_go_at();
    const std::vector<int> skew = m.c.start_skew();
    for (unsigned t = 0; t < 64; t++) {
        starts.insert(m.c.unit<units::AddrGen>(unit_name(t, "ag")).started_at - skew[t]);
        const unsigned l = (t + 63) & 63; int bad = 0;
        for (int i = 0; i < n_sync(l); i++) if (rec(m.c, t, 0x40000 + 2 * uint32_t(i)) != rec(int32_t(l << 16 | unsigned(i)), int32_t(0xC0DE0000u | unsigned(i)))) bad++;
        suite::eq(0, bad, "tile %u: the left neighbour's %d records arrived", t, n_sync(l));
    }
    suite::eq(1, int64_t(starts.size()), "all 64 tiles started on the same cycle (after their 0/+1 skew)");
    const int64_t s0 = *starts.begin();
    finish(m, "sync", 10000);
    no_violations(m.c, "sync");
    meas("sync", "set-up command (per-tile lengths): GO -> all READY", go_to_ready(m, 0));
    meas("sync", "start command: GO -> the start taking effect (tile 0, after its skew)", s0 - go_at);
    {
        int64_t lo = INT64_MAX, hi = 0;
        for (unsigned t = 0; t < 64; t++) for (int64_t w : m.c.tile(t).block().counter_write_at) if (w >= go_at) { lo = std::min(lo, w - go_at); hi = std::max(hi, w - go_at); }
        out(suite::str("  sync: the shortest start path's counter store, from C&C's GO store: cycle +%lld..+%lld over the 64 tiles [meas: RecipeTest sync]", (long long)lo, (long long)hi));
    }
    meas("sync", "start command: GO -> all READY", go_to_ready(m, 1));
    go_to_first_request(m, 1, "sync", "start (a stream run)");
}

// ================================================================ 28 a signal between tiles: the ring pipeline
void signal() {
    suite::group("signal");
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_signal");
    const uint32_t vecs = im.sym("vecs"), gots = im.sym("gots");
    m.c.cc().wait_until([&] { return tiles_idle(m); }).call([&](int64_t) {   // after crt0 has zeroed .bss
        for (unsigned t = 0; t < 64; t++) for (unsigned st = 0; st < 4; st++) for (unsigned i = 0; i < 8; i++) m.c.tile(t).poke_local(vecs + 4 * (8 * st + i), t << 16 | st << 8 | i); });
    m.c.cc().command(BROADCAST, 1 << 24).go();
    finish(m, "signal", 40000);
    int bad = 0;
    for (unsigned t = 0; t < 64; t++) {
        const unsigned l = (t + 63) & 63;
        for (unsigned st = 0; st < 4; st++) for (unsigned i = 0; i < 8; i++) if (m.word(t, gots + 4 * (8 * st + i)) != (l << 16 | st << 8 | i)) bad++;
    }
    suite::eq(0, bad, "four steps of the ring: every tile got its left neighbour's vectors, each send released by a count-only signal");
    no_violations(m.c, "signal");
    meas("signal", "a 4-step ring of 8-word vectors, released by signals: GO -> all READY", go_to_ready(m, 0));
}

// ================================================================ SelfTest: the Java side of fw/tests/selftest.h
// Every constant read from the header itself (its ST_ #defines), the algorithm the header's (SelfTest.java).
struct SelfTest {
    int32_t a = 0, b = 0, c = 0, warmup = 0, k0 = 0, k1 = 0, m0 = 0, m1 = 0, r = 0, h0 = 0, h1 = 0;
    static SelfTest load() {
        std::string t;
        if (!read_file("fw/tests/selftest.h", t)) FATAL("recipes: cannot read fw/tests/selftest.h");
        std::map<std::string, int64_t> d;
        // "#define\s+(ST_\w+)\s+(0x[0-9A-Fa-f]+|\d+)u?"
        for (size_t i = t.find("#define"); i != std::string::npos; i = t.find("#define", i + 1)) {
            size_t j = i + 7; if (j >= t.size() || !rspace(t[j])) continue;
            while (j < t.size() && rspace(t[j])) j++;
            size_t n = j; while (n < t.size() && (isalnum(uint8_t(t[n])) || t[n] == '_')) n++;
            const std::string name = t.substr(j, n - j);
            if (name.compare(0, 3, "ST_") != 0 || n >= t.size() || !rspace(t[n])) continue;
            size_t v = n; while (v < t.size() && rspace(t[v])) v++;
            size_t e = v;
            if (t.compare(v, 2, "0x") == 0 && v + 2 < t.size() && isxdigit(uint8_t(t[v + 2]))) { e = v + 2; while (e < t.size() && isxdigit(uint8_t(t[e]))) e++; }
            else while (e < t.size() && isdigit(uint8_t(t[e]))) e++;
            if (e == v) continue;
            d[name] = int64_t(std::stoull(t.substr(v, e - v), nullptr, 0));      // Long.decode
        }
        auto get = [&](const char* k) { auto it = d.find(k); if (it == d.end()) FATAL("selftest.h: %s missing", k); return int32_t(it->second); };
        SelfTest s;
        s.a = get("ST_XS_A"); s.b = get("ST_XS_B"); s.c = get("ST_XS_C"); s.warmup = get("ST_WARMUP");
        s.k0 = get("ST_SEED_K0"); s.k1 = get("ST_SEED_K1"); s.m0 = get("ST_HASH_M0"); s.m1 = get("ST_HASH_M1"); s.r = get("ST_HASH_R");
        s.h0 = get("ST_HASH_H0"); s.h1 = get("ST_HASH_H1");
        return s;
    }
    int64_t next(int64_t x) const { uint64_t u = uint64_t(x); u ^= u << (a & 63); u ^= u >> (b & 63); u ^= u << (c & 63); return int64_t(u); }
    int64_t seed(int64_t s) const { if (s == 0) s = 1; for (int i = 0; i < warmup; i++) s = next(s); return s; }
    int64_t tile_seed(int me, int64_t base) const {
        const uint32_t lo = uint32_t(int32_t(base)) ^ uint32_t(me + 1) * uint32_t(k0), hi = uint32_t(uint64_t(base) >> 32) ^ uint32_t(me + 1) * uint32_t(k1);
        return seed(int64_t(uint64_t(lo) | uint64_t(hi) << 32));
    }
    std::vector<int64_t> fill(int64_t state, int n) const { std::vector<int64_t> v(static_cast<size_t>(n)); for (int i = 0; i < n; i++) { state = next(state); v[size_t(i)] = state; } return v; }
    uint32_t mix(uint32_t h, uint32_t w, uint32_t m) const { h = (h ^ w) * m; const unsigned k = unsigned(r) & 31; return k ? (h << k | h >> (32 - k)) : h; }
    int64_t hash(const std::vector<int64_t>& recs, int from, int n) const {
        uint32_t x0 = uint32_t(h0), x1 = uint32_t(h1);
        for (int i = from; i < from + n; i++) { x0 = mix(x0, uint32_t(recs[size_t(i)]), uint32_t(m0)); x1 = mix(x1, uint32_t(uint64_t(recs[size_t(i)]) >> 32), uint32_t(m1)); }
        return int64_t(uint64_t(x0) | uint64_t(x1) << 32);
    }
};

// ================================================================ 17-20: write a block, fill, hash
constexpr uint32_t B_SRC = 0x00000, B_DST = 0x40000, B_WB = 0x80000;   // rcp_selftest.c's SRC, DST, WB
std::vector<int64_t> sram_recs(Chip& c, unsigned k, uint32_t chunk, int n) { std::vector<int64_t> v(static_cast<size_t>(n)); for (int i = 0; i < n; i++) v[size_t(i)] = rec(c, k, chunk + 2 * uint32_t(i)); return v; }
std::string per_rec(int64_t c1, int64_t c2, int n1, int n2) { return java_fixed(double(c2 - c1) / (n2 - n1), 2); }
std::map<std::string, std::string> per_record;              // ref's perRecord (LinkedHashMap; read by key)
void blocks() {
    suite::group("blocks");
    const SelfTest ST = SelfTest::load();
    Mach m(cfg());
    const Mach::Image im = m.boot("rcp_selftest");
    const int W2 = 256, F1 = 256, F2 = 1024;
    int64_t seeds[64];
    test::JavaRandom rnd(14);
    for (int i = 0; i < 64; i++) seeds[i] = i == 5 ? 0 : next_long(rnd);       // tile 5: a zero seed (st_seed makes it 1)
    mach::Cc& cc = m.c.cc();
    const uint32_t blk = im.sym("blk");                                        // v1.2: the program's block in data memory
    cc.wait_until([&] { return tiles_idle(m); }).call([&](int64_t) {           // after crt0 has zeroed .bss
        for (unsigned k = 0; k < 64; k++) for (unsigned i = 0; i < unsigned(W2); i++) {
            m.c.tile(k).poke_local(blk + 8 * i, k << 16 | i); m.c.tile(k).poke_local(blk + 8 * i + 4, 0xB10C0000u | i); } });
    cc.command(BROADCAST, 7 << 24).go();                                       // phase 0: write the block (256 records)
    for (int n : {F1, F2}) {                                                   // phases 2, 3: fill, C&C's seeds
        for (unsigned i = 0; i < 64; i++) cc.command(i, 1 << 24, {uint32_t(seeds[i]), uint32_t(uint64_t(seeds[i]) >> 32), uint32_t(n)});
        cc.go();
    }
    cc.command(BROADCAST, 5 << 24, {B_SRC, uint32_t(F1)}).go().command(BROADCAST, 5 << 24, {B_SRC, uint32_t(F2)}).go();   // phases 4, 5: hash
    const int64_t BASE = 0x0123456789ABCDEFLL;
    cc.command(BROADCAST, 2 << 24, {uint32_t(BASE), uint32_t(uint64_t(BASE) >> 32), uint32_t(F2)}).go();   // phase 6: fill, index seed + base
    cc.command(BROADCAST, 2 << 24, {0, 0, uint32_t(F1)}).go();                // phase 7: fill, index seed alone
    std::vector<int64_t> wrote[64], filled[64]; int64_t sig[64] = {};
    const bool ok = m.run([&] { return cc.phases_logged() >= 6 && tiles_idle(m); }, 400000);   // the boot's GO, then five phases
    suite::check(ok, "blocks: write, fill and hash phases ran");
    for (unsigned me = 0; me < 64; me++) {
        wrote[me] = sram_recs(m.c, me, B_WB, W2);
        filled[me] = sram_recs(m.c, me, B_SRC, F2);
        sig[me] = int64_t(uint64_t(m.word(me, im, "sig", 0)) | uint64_t(m.word(me, im, "sig", 1)) << 32);
    }
    finish(m, "blocks", 400000);
    int bad_w = 0, bad_f = 0, bad_h = 0, bad_s = 0, bad_l = 0, bad_z = 0;
    for (unsigned me = 0; me < 64; me++) {
        for (int i = 0; i < W2; i++) if (wrote[me][size_t(i)] != rec(int32_t(me << 16 | unsigned(i)), int32_t(0xB10C0000u | unsigned(i)))) bad_w++;
        const std::vector<int64_t> want = ST.fill(ST.seed(seeds[me]), F2);
        if (want != filled[me]) bad_f++;
        if (sig[me] != ST.hash(want, 0, F2)) bad_h++;
        if (ST.hash(filled[me], 0, F2) != sig[me]) bad_s++;
        const std::vector<int64_t> idx = ST.fill(ST.tile_seed(int(me), BASE), F2), idx0 = ST.fill(ST.tile_seed(int(me), 0), F1);   // phase 6 (phase 7 overwrote the first F1)
        const std::vector<int64_t> now = sram_recs(m.c, me, B_SRC, F2);
        for (int i = 0; i < F2; i++) if (now[size_t(i)] != (i < F1 ? idx0[size_t(i)] : idx[size_t(i)])) { bad_l++; break; }
        if (me == 5 && filled[me][0] != ST.fill(ST.seed(1), 1)[0]) bad_z++;
    }
    suite::eq(0, bad_w, "write_block: every tile's %d records from data memory are in big SRAM", W2);
    suite::eq(0, bad_f, "fill from C&C's seeds: every tile's %d records equal the Java generator's (selftest.h's constants)", F2);
    suite::eq(0, bad_z, "fill: a zero seed is taken as 1 (st_seed)");
    suite::eq(0, bad_h, "hash: every tile's hash of its block equals Java's hash of the Java-generated block");
    suite::eq(0, bad_s, "hash: ... and Java's hash of what big SRAM holds");
    suite::eq(0, bad_l, "fill from the tile index, with a base seed and without: every tile's records equal Java's");
    std::set<int64_t> firsts; for (unsigned k = 0; k < 64; k++) firsts.insert(rec(m.c, k, B_SRC));
    suite::eq(64, int64_t(firsts.size()), "index seeds: 64 different sequences");
    no_violations(m.c, "blocks");
    const int64_t w2 = go_to_ready(m, 0), f1 = go_to_ready(m, 1), f2 = go_to_ready(m, 2), h1 = go_to_ready(m, 3), h2 = go_to_ready(m, 4);   // v1.2: one write phase
    meas("write_block", std::to_string(W2) + " records: GO -> all READY", w2);
    meas("fill", std::to_string(F1) + " records, C&C's seed: GO -> all READY", f1);
    meas("fill", std::to_string(F2) + " records: GO -> all READY", f2);
    meas("hash", std::to_string(F1) + " records (" + std::to_string(2 * F1) + " words): GO -> all READY", h1);
    meas("hash", std::to_string(F2) + " records: GO -> all READY", h2);
    out("blocks [meas: RecipeTest blocks]: cycles per 64-bit record, marginal (the difference of two sizes over their difference): write_block " +
        java_fixed(double(w2) / 256.0, 2) + " (256 records, not marginal), fill " + per_rec(f1, f2, F1, F2) + ", hash " + per_rec(h1, h2, F1, F2) + " (" +
        java_fixed(double(h2 - h1) / (2 * (F2 - F1)), 2) + " per 32-bit word read)");
    per_record["write_block"] = java_fixed(double(w2) / 256.0, 1); per_record["fill"] = per_rec(f1, f2, F1, F2); per_record["hash"] = per_rec(h1, h2, F1, F2);
}


// The self-test: seed and fill, a uniform all-to-all, hash, and the signature to C&C; the simulator computes every
// tile's expected signature from the generator and the all-to-all's permutation alone. Round 1 corrupts one record of
// tile 17's block after the fill: exactly the tile that receives it reports a different signature.
void selftest() {
    suite::group("selftest");
    const SelfTest ST = SelfTest::load();
    const int N = 1024, M = N / 64;
    const int64_t BASE = 0x5EED5EED00C0FFEELL;
    std::vector<int64_t> want[64];
    for (int me = 0; me < 64; me++) want[me] = ST.fill(ST.tile_seed(me, BASE), N);
    int64_t expect[64];
    for (int t = 0; t < 64; t++) {
        std::vector<int64_t> in(static_cast<size_t>(N));
        for (int s2 = 0; s2 < 64; s2++) for (int i = 0; i < M; i++) in[size_t(s2 * M + i)] = want[s2][size_t(t * M + i)];   // s's block t lands in t's slot s
        expect[t] = ST.hash(in, 0, N);
    }
    for (int round = 0; round < 2; round++) {
        mach::MachConfig k = cfg(); k.reqbus_host = true;          // C&C 1's request service (drain_reports)
        Mach m(k);
        m.boot("rcp_selftest");
        mach::Cc& cc = m.c.cc();
        cc.drain_reports(true);                                    // v1.2: the signatures are requests to C&C 1, which answers each
        const int CORRUPT = 17, REC = 22 * M + 3;                  // tile 17's block for tile 22: a record of it
        cc.command(BROADCAST, 2 << 24, {uint32_t(BASE), uint32_t(uint64_t(BASE) >> 32), uint32_t(N)}).go();
        // ref's then((c, t) -> ...): after the fill, before configure (the block at rest), waiting and poking in one
        // step; here wait_until then call (round 1 prints no figures: only which signatures match)
        if (round == 1) cc.wait_until([&] { return tiles_idle(m); }).call([&](int64_t) { poke64(m.c, CORRUPT, B_SRC + 2 * uint32_t(REC), 0xBAD); });
        cc.command(BROADCAST, 3 << 24).go()
            .command(BROADCAST, 4 << 24).go()
            .command(BROADCAST, 5 << 24, {B_DST, uint32_t(N)}).go()
            .command(BROADCAST, 6 << 24).go();
        finish(m, round == 0 ? "selftest round 0" : "selftest round 1", 400000);
        const auto sigs = cc.results();
        int64_t got[64]; for (int64_t& g : got) g = -1;
        for (const auto& e : sigs) got[e[1]] = int64_t(uint64_t(e[3]) << 32 | (uint64_t(e[2]) & 0xFFFFFFFFull));
        int match = 0;
        for (int t = 0; t < 64; t++) if (got[t] == expect[t]) match++;
        if (round == 0) {
            suite::eq(64, int64_t(sigs.size()), "round 0: 64 signatures reached C&C 1 as requests (and each was answered)");
            suite::eq(64, match, "round 0: every tile's signature equals the one the simulator computed");
            const int64_t first = sigs.at(0)[0], last = sigs.at(sigs.size() - 1)[0];
            no_violations(m.c, "selftest round 0");
            meas("selftest", "fill (" + std::to_string(N) + " records, index seed): GO -> all READY", go_to_ready(m, 0));
            meas("selftest", "configure: GO -> all READY", go_to_ready(m, 1));
            meas("selftest", "start (timetable 16 x 4 x " + std::to_string(M) + " = " + std::to_string(64 * M) + "): GO -> all READY", go_to_ready(m, 2));
            meas("selftest", "hash of the received " + std::to_string(N) + " records: GO -> all READY", go_to_ready(m, 3));
            meas("selftest", "signatures to C&C 1 over the request bus, answered: GO -> all READY", go_to_ready(m, 4));
            meas("selftest", "signatures at C&C 1: first to last", last - first);
        } else {
            suite::eq(63, match, "round 1 (tile %d's record %d corrupted): 63 signatures still match", CORRUPT, REC);
            suite::check(got[22] != expect[22], "round 1: the one that differs is tile 22's, which received the record");
            no_violations(m.c, "selftest round 1");
        }
    }
    out(suite::str("selftest [meas: RecipeTest selftest]: fill from the index seed, a uniform all-to-all (%d records per pair), hash, signatures to C&C 1 as requests (answered): all 64 equal the simulator's; one corrupted record changes exactly its receiver's", M));
}

// ================================================================ tables (recipe 21)
void tables() {
    suite::group("tables");
    // the program's `expected` is 0 (never set: gcc folds it), so command 1 is a run that writes nothing here and
    // a2a_finish waits for DMA run done alone; the run (a 40-record stream to the right-hand tile's... none: count 0
    // streams would end at once) is set up from the test as 8 streams of 0 records, whose run still reads the tables
    namespace F = mach::fields;
    Mach m(cfg());
    m.boot("rcp_tables");
    m.c.cc().wait_until([&] { return tiles_idle(m); }).call([&](int64_t) {
        for (unsigned k = 0; k < 64; k++) {
            mach::TileRef p = m.c.tile(k);
            for (unsigned i = 0; i < 8; i++) p.set_stream(i, F::stream_src(0), 0, F::stream_count(0, F::SIZE_64, 1, units::lw::M_SRC, 0));
            p.set_master_config(F::master_config(F::RUN_STREAMS, 0, 8, 0, 0, 1));   // ref sets ctl.dmaConfig and the AG's alike
        } });
    m.c.cc().command(BROADCAST, 1 << 24).go().command(BROADCAST, 2 << 24).go();
    finish(m, "tables", 40000);
    int woke = 0;
    for (unsigned k = 0; k < 64; k++) {
        const int64_t done_at = m.c.tile(k).unit<units::AddrGen>("ag").done_at;
        if (done_at > 0 && m.c.tile(k).block().last_done_at > done_at) woke++;
    }
    suite::eq(64, woke, "a2a_finish: done followed the run's end (DMA run done), and tables_free_unknown returned, on every tile");
    no_violations(m.c, "tables");
}

// ================================================================ 26 the replayer's refill loop
void replay() {
    suite::group("replay");
    // rcp_replay.c's state[] is never written (gcc folds it to zeros), so the pass it starts has an empty code stream:
    // the loop's shape runs (the state load into part 0, PULSE_RUN, the credits, the masks, run done) but no rows. The
    // replayer's real check is StageB synthetic (fw/replay/replay_ctl.c, row for row against Stage A).
    Mach m(cfg());
    m.c.set_check_errors(false);                                   // the empty code stream's underflow, checked below
    m.boot("rcp_replay");
    units::Replayer& r = slot(m.c, 0).replayer(1);
    m.c.cc().wait_until([&] { return tiles_idle(m); }).call([&](int64_t) {
        m.c.unit<units::Cnc>("cnc").expect = 1;                    // ref's cc1p.expect = 1: C&C 1's model waits for tile 0
        m.c.touched("cnc"); });
    m.c.cc().command(0, 1 << 24).go();                             // tile 0 only
    suite::check(m.run([&] { return !m.c.cc().busy() && m.c.tile(0).block().last_done_at > m.c.cc().last_go_at() && core(m.c, 0).in_wfi; }, 400000),
                 "tile 0 ran recipe 26's pass and came back to wait_for_command()");
    suite::eq(1, int64_t(r.passes), "one pass");
    suite::check(!r.is_running() && r.region0[0] == 0 && r.region0[1] == 0, "it ended; part 0 holds the recipe's (zero) state word");
    suite::eq(0, int64_t(slot(m.c, 0).mask[1]), "the loop cleared the replayer's EVENTS_MASK at the end");
    suite::eq(units::EVENTS_MASK_RESET, int64_t(m.c.tile(0).block().mask), "and restored the tile's");
    suite::eq(1 << units::Replayer::ERR_CODE_UNDER, r.errors(m.c.cycle()),
              "the replayer's only error: code-stream underflow (v1.26 ERRORS [3]), an instruction past the (empty) code stream, as the zero state makes it");
    // ref's badWrites is never written (Replayer.java): the overruns are what this check can see
    suite::check(r.overruns() == 0, "no overrun and no bad write: the loop's refills kept to their credits");
    suite::eq(1 << 11, int32_t(m.c.tile(0).block().conditions | (slot(m.c, 0).live_errors(m.c.cycle()) << 11 & ~0x7FFu)),
              "the tile's ERRORS [11] (peripheral 1), and nothing else");
    no_violations(m.c, "replay");
}

// ================================================================ recipes 22 and 27, C&C 1's side, on C&C 1 as a core
// The harness plays the host: it stages the units' image (fw/cc/cc_rcp.c: C&C 2, C&C 3 and the tiles in one) at
// rcp_cc1.c's chunks 0x10000 (64 rows) and 0x11000 (16), and 256 rows of data at 0x20000, then lands rcp_cc1 and its
// CMD_JUMP through the host port. rcp_cc1.c's main puts the units' command at 0x12000, boots C&C 2 and C&C 3, waits for
// their "started", boots the tiles, runs boot part 2 (CMD_STAGE, the data broadcast to every tile's big SRAM at chunk
// 0x30000), boots C&C 3 again by recipe fence, then runs phases.
void cc1() {
    suite::group("rcp_cc1.c on C&C 1 as a core: recipes 22 and 27, C&C 1's side");
    Mach m(Mach::cores_cfg(shuffle));
    const Mach::Image u = Mach::Image::load("cc_rcp", m.c.cfg()), c1 = Mach::Image::load("rcp_cc1", m.c.cfg());
    m.stage_cc1(0x10000, u.code, 64);
    m.stage_cc1(0x11000, u.data, 16);
    std::vector<uint32_t> data(256 * 4);
    for (uint32_t i = 0; i < data.size(); i++) data[i] = 0x5EED0000u ^ i * 0x9E3779B1u;
    m.stage_cc1(0x20000, data, 256);
    m.host_boot(c1, c1.data);
    auto running = [&]() -> int64_t { int64_t r = 0; for (unsigned k = 0; k < 64; k++) { const int64_t f = core(m.c, k).first_image_at; if (f < 0) return -1; r = std::max(r, f); } return r; };
    suite::check(m.run([&] { return running() >= 0; }, 200000), "every tile runs the image (C&C 1 booted C&C 2, C&C 3 and the tiles)");
    suite::check(core(m.c, 65).first_image_at > 0 && core(m.c, 66).first_image_at > 0, "C&C 2 and C&C 3 run the image");
    suite::eq(2, int64_t(req_received(m.c, 0)), "C&C 1 popped two requests: both \"started\"");
    const uint32_t commands = u.sym("commands");
    suite::check(m.run([&] { for (unsigned k = 0; k < 64; k++) if (int32_t(m.c.tile(k).peek_local(commands)) < 1) return false; return true; }, 200000),
                 "every tile ran CMD_STAGE");
    int bad = 0;
    for (unsigned k = 0; k < 64; k++) for (uint32_t i = 0; i < data.size(); i++) if (m.c.tile(k).sram_peek(0x30000 + i) != data[i]) { bad++; break; }
    suite::eq(0, bad, "boot part 2: the 256 rows in every tile's big SRAM at chunk 0x30000");
    const uint32_t COMMAND = 0x7FC0, ARGS_ODD = 8;
    suite::check(m.run([&] { return m.c.tile(66).peek_local(COMMAND) == (CMD_JUMP << 24 | 1u << 23); }, 200000),
                 "recipe fence: C&C 3's second command, CMD_JUMP (odd)");
    suite::eq(0x01000000, m.c.tile(66).peek_local(COMMAND + 4 * ARGS_ODD), "its entry in the odd block");
    // the GOs: ref's cc1p.gos counts a PULSE_GO in its store's cycle; here the peripheral applies it at t + 1 (D33) and
    // keeps the store's cycle (last_go_store), ref's cycle (as CcBench)
    uint64_t g = cc1p(m.c).gos; int64_t at[8] = {}; int ng = 0;
    while (ng < 8 && m.c.cycle() < 400000) {
        m.c.step();
        if (cc1p(m.c).gos > g + 1) FATAL("recipes: two GOs in one cycle");
        if (cc1p(m.c).gos > g && ng < 8) { g++; at[ng++] = int64_t(cc1p(m.c).last_go_store); }
    }
    const bool phases = ng == 8;
    suite::check(phases, "eight phases after the boot (C&C 1's phase loop)");
    if (phases) meas("rcp_cc1", "a phase on C&C 1 as a core (command type 1, nothing to do): GO to GO", at[7] - at[6]);
    if (!phases)
        std::printf("  stopped: C&C 1 pc %08x%s, GOs %llu, READY bits %016llx, tiles' commands %u\n", core(m.c, 64).pc, core(m.c, 64).in_wfi ? " (WFI)" : "",
                    (unsigned long long)cc1p(m.c).gos, (unsigned long long)cc1p(m.c).ready_bits(m.c.cycle()), m.c.tile(0).peek_local(commands));
    suite::eq(0, int64_t(m.c.violations()), "violations");
    if (m.c.violations() != 0) std::fputs(m.c.violation_report().c_str(), stdout);
}

// ================================================================ boot (6.2), the handler (recipe 24), the reset (6.1)
void boot() {
    suite::group("boot");
    mach::MachConfig k = cfg(); k.reqbus_host = true;              // C&C 1's request service (drain_reports)
    Mach m(k);
    const Mach::Image im = m.boot("rcp_boot");
    const unsigned P = 16; const uint32_t AT = 0xE0000;           // 16 rows into area 7 (6.2's staging area)
    mach::Cc& cc = m.c.cc();
    cc.command(BROADCAST, 0x53u << 24 | P).go().wait_cycles(m.c.cfg().stage_delay);   // CMD_STAGE; the data no sooner
    for (unsigned i = 0; i < P; i++)                                                  //   than stage_delay after GO
        cc.send(BROADCAST, units::lw::M_WRITE, 1, 0x03000000u | (AT + 4 * i) << 2, units::lw::SIZE_128, 0x5EED0000ull | i, uint64_t(i) << 32);
    suite::check(m.run([&] { return idle(m); }, 40000), "every tile armed, took the staged data and pulsed done");
    int bad = 0, irq = 0;
    for (unsigned t = 0; t < 64; t++) {
        for (unsigned i = 0; i < P; i++)
            if (m.c.tile(t).sram_peek64(AT + 4 * i) != (0x5EED0000ull | i) || m.c.tile(t).sram_peek64(AT + 4 * i + 2) != uint64_t(i) << 32) bad++;
        const units::Rv32& c = core(m.c, t);
        if (c.mtvec == im.sym("trap_handler") && (c.mstatus & 8) != 0 && (c.mie & 1u << 11) != 0) irq++;
    }
    suite::eq(0, bad, "the 16 staged rows are in every tile's big SRAM (counted, broadcast)");
    suite::eq(64, irq, "boot part 2 set mtvec to the image's handler and enabled the hard interrupt on every tile");
    int64_t running = 0; for (unsigned t = 0; t < 64; t++) running = std::max(running, core(m.c, t).first_image_at);
    meas("boot", "reset release -> all 64 tiles running their image (overlays " + std::to_string((im.code.size() + im.data.size() + 3) / 4) +
         " packets, broadcast)", running);
    // the handler (recipe 24): an error on tile 9 with the hard interrupt enabled -> its 24 reports in C&C 1's queue
    m.c.set_check_errors(false);
    cc.drain_reports(true);
    m.c.tile(9).set_errors(1u << 3);                               // Conditions.DRIFT
    suite::check(m.run([&] { return cc.reports_of(9).size() >= 24; }, 40000), "tile 9's handler sent its 24 reports to C&C 1");
    const units::Rv32& t9 = core(m.c, 9);
    suite::check(t9.trap_cause == CAUSE_EXTERNAL && t9.mtvec == im.sym("trap_handler"), "through the image's handler (the hard interrupt)");
    meas("handler", "trap -> the last of 24 reports in C&C 1's receive queue (tile 9)", cc.reports_of(9).at(23)[0] - t9.trap_at);
    m.c.set_check_errors(true); m.c.clear_violations();
    cc.reset(false).command(BROADCAST, CMD_JUMP << 24, {im.sym("_start")}).run_when_ready();
    suite::check(m.run([&] { return m.c.resets() == 1 && idle(m); }, 40000), "a reset and the boot again");
    finish(m, "boot", 20000);
    no_violations(m.c, "boot");
    meas("boot", "CMD_STAGE with 16 broadcast rows: GO -> all READY", go_to_ready(m, 0));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) shuffle = std::strtoll(argv[1], nullptr, 10);
    if (shuffle >= 0) std::printf("shuffled child order, seed %lld\n", (long long)shuffle);
    snippets();
    cmdloop(); copy(); cmem(); bucket(); aug(); gather(); xchg(); xvec(); a2a(); rdport(); status(); rules();
    sync_(); blocks();
    selftest();
    boot();
    signal();
    tables(); replay(); cc1();
    out("recipe cycles [meas: RecipeTest; tile 0 unless stated]:");
    for (const std::string& s : table) out(s);
    return suite::done(shuffle >= 0 ? "RecipeTest (shuffled, seed " + std::to_string(shuffle) + ")" : "RecipeTest");
}
