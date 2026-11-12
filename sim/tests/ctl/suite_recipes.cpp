// suite_recipes.cpp (WORKER23, P5, inventory C12): v1.3's RecipeTest (sim/ref/meshsim/test/RecipeTest.java, with
// SelfTest.java) on W20's harness, group for group, in Java's order and words. run_tests.sh's steps `recipes` (-1) and
// `recipes-shuf` (4); its output equals ref's log (tests/ctlsuites/recipes*.log). Usage: suite_recipes [shuffleSeed]
//
// The recipe book's tests (fw/RECIPES.md, the v1.2 recipes fw/recipes/rcp_*.c). Every recipe runs on all 64 tiles and is
// checked bit-exactly, with no invariant violations (or exactly the expected ones), and its cycles are reported. The
// snippet check makes every code block in RECIPES.md equal to the RECIPE ... END region it names, so the book can't
// drift from the code that runs.
//
// IN PROGRESS (NOTES.md): ported so far: snippets, copy, cmem, status, rules. The groups that read C&C's phase log
// (goToReady, goToFirstRequest: W20's Cc::phase_log) and the cores' come next; until then main() runs the groups
// ported, so the output is ref's lines for those groups.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "units/ag.h"
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
[[maybe_unused]] void meas(const char* recipe, const std::string& what, const std::string& cycles) { table.push_back(suite::str("  %-14s %-72s %8s", recipe, what.c_str(), cycles.c_str())); }

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

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) shuffle = std::strtoll(argv[1], nullptr, 10);
    if (shuffle >= 0) std::printf("shuffled child order, seed %lld\n", (long long)shuffle);
    snippets();
    // cmdloop(): W20 (Cc::phase_log)
    copy(); cmem();
    // bucket(), aug(), gather(), xchg(), xvec(), a2a(), rdport(): W20 (Cc::phase_log)
    status(); rules();
    // sync(), blocks(), selftest(), boot(), signal(), tables(), replay(), cc1(): next (the phase log, wait_until, the cores)
    out("recipe cycles [meas: RecipeTest; tile 0 unless stated]:");
    for (const std::string& s : table) out(s);
    return suite::done(shuffle >= 0 ? "RecipeTest (shuffled, seed " + std::to_string(shuffle) + ")" : "RecipeTest");
}
