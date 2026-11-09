// test_replayer_alu: the replayer's ALU and row rules (WORKER15), against ref.
//
// 1. ReplayerTest.opcodeTable and .vectors, ported: the model's opcode set against sim/ref/tables/opcode_table.txt, and
//    the 252 rows of sim/ref/meshsim/data/vec-821e1bd.tsv (the tracer's vectors, jolt-cpp 821e1bd), read in place. The
//    report (summary lines and every flagged or n/a row) must equal ref's, line for line: tests/replayer/alu-report.txt
//    is ReplayerTest's output, lines 1-29 (tests/ref/replayconf.sh --check regenerates it).
// 2. A differential sweep (tests/replayer/alu-sweep.txt.gz, from ReplayConf alu): every opcode on 100 seeded inputs,
//    exec's rd, new pc and undefined flag equal to ReplayerAlu.exec's; the decode index, source immediates and immediate
//    codes on seeded words equal RowCore's.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"
#include "units/event_counts.h"
#include "units/replayer_alu.h"
#include "units/row_core.h"

using namespace units;

namespace {

const std::string kVec = "ref/meshsim/data/vec-821e1bd.tsv";
const std::string kOpTable = "ref/tables/opcode_table.txt";
const std::string kDir = "tests/replayer/";

bool read_lines(const std::string& path, std::vector<std::string>& out) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    std::string cur;
    int ch;
    while ((ch = std::fgetc(f)) != EOF) {
        if (ch == '\n') { out.push_back(cur); cur.clear(); }
        else cur += char(ch);
    }
    if (!cur.empty()) out.push_back(cur);
    std::fclose(f);
    return true;
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> r;
    size_t p = 0;
    for (;;) {
        size_t q = s.find(sep, p);
        r.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos) return r;
        p = q + 1;
    }
}
std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> r;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') j++;
        if (j > i) r.push_back(s.substr(i, j - i));
        i = j;
    }
    return r;
}
bool blank(const std::string& s) { return s.find_first_not_of(" \t\r") == std::string::npos; }
bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
uint64_t hex_after_0x(const std::string& s) { return std::strtoull(s.c_str() + s.find("0x") + 2, nullptr, 16); }
std::string hx(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "0x%016llx", (unsigned long long)v); return b; }

constexpr uint64_t PC = 0x80000100ull;
// vec.cpp (821e1bd) sets ShiftData's shift_offset per vector without printing it: its S(...) calls, in order.
constexpr int64_t SHIFT_OFFSETS[] = {0, 7, -2, 0, 2, -4, 0, 4, -4};

// ReplayerTest.opcodeTable and .vectors; appends the report lines; returns the number of failed checks.
int alu_report(std::vector<std::string>& report) {
    int failed = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) { failed++; std::fprintf(stderr, "  FAIL %s\n", what.c_str()); }
    };
    std::vector<std::string> lines;
    if (!read_lines(kOpTable, lines)) { std::fprintf(stderr, "cannot read %s\n", kOpTable.c_str()); return 1; }
    int rows = 0, executed = 0;
    for (const std::string& line : lines) {
        if (blank(line)) continue;
        std::vector<std::string> f = split_ws(line);
        int c = std::atoi(f[0].c_str());
        rows++;
        if (f[2] == "-") { check(!alu::executes(c), f[1] + " is not executed by the replayer"); continue; }
        executed++;
        const char* nm = alu::name_or_null(c);
        check(nm && f[1] == nm, "opcode " + f[0] + ": table " + f[1]);
        check(alu::cls(c) == f[2][0], f[1] + ": class " + f[2]);
    }
    int model = 0;
    for (unsigned i = 0; i < 256; i++) model += alu::executes(i);
    check(executed == model, "the model executes exactly the table's row opcodes");
    char buf[512];
    std::snprintf(buf, sizeof buf, "opcode table: %d opcodes, %d row opcodes, all matching the model", rows, executed);
    report.push_back(buf);

    lines.clear();
    if (!read_lines(kVec, lines)) { std::fprintf(stderr, "cannot read %s\n", kVec.c_str()); return 1; }
    std::vector<std::string> rep;
    int pass = 0, na = 0, flagged = 0, shift_k = 0;
    bool covered[256] = {};
    for (const std::string& line : lines) {
        if (blank(line)) continue;
        std::vector<std::string> f = split(line, '\t');
        const std::string& name = f[0];
        std::string note = f.size() > 4 ? f[4] : "", rd_imm_s = f.size() > 5 ? f[5] : "";
        std::vector<std::string> res = split(f[3], '|');
        int op = alu::code(name.c_str());
        std::string what = name + " " + f[1] + " " + f[2] + (note.empty() ? "" : " (" + note + ")");
        if (res[0] == "ABORT") { na++; rep.push_back("n/a  " + what + ": the tracer aborts (a failing assertion never reaches a replayer)"); continue; }
        if (op < 0 || !alu::executes(unsigned(op))) {
            na++;
            rep.push_back("n/a  " + what + ": not a replayer opcode (advice is written as VirtualAdvice and taken from the log, 4.19.7)");
            continue;
        }
        uint64_t a = hex_after_0x(f[1]), b = hex_after_0x(f[2]);
        bool rd_imm = !rd_imm_s.empty();
        uint64_t rd_imm_v = rd_imm ? hex_after_0x(rd_imm_s) : 0;
        unsigned inc = has(note, "not the last row") ? 0 : has(note, "compressed") ? 2 : 4;
        int rd_reg = has(note, "rd = x0") ? 0 : has(note, "rd = rs1") ? 6 : has(note, "EBREAK") ? 40 : 5;
        uint64_t rd_init = name == "VirtualHostIO" ? 0x1234 : name == "JAL" && rd_reg == 40 ? 0 : 0xdead;
        unsigned u = unsigned(op);
        uint64_t log = alu::is_load(u) ? (has(note, "rd = x0") ? 0x99 : 0x1122334455667788ull) : alu::is_store(u) ? 0x99
                       : name == "VirtualAdvice" ? 0x42 : 0;
        uint64_t so = alu::is_shift_data(u) ? uint64_t(SHIFT_OFFSETS[shift_k++]) : 0;
        uint64_t c = rd_imm ? rd_imm_v : rd_reg == 0 ? 0 : rd_reg == 6 ? a : rd_init;
        alu::Out o;
        alu::exec(u, a, b, c, PC, inc, true, log, so, o);
        uint64_t rd_after = rd_imm ? 0 : rd_reg == 0 ? 0 : o.rd;
        uint64_t final_pc;
        const char* next;
        if (o.new_pc == 0) { final_pc = 0; next = "end: pc 0, instr_idx 0"; }
        else if (o.new_pc != PC) { final_pc = o.new_pc; next = "transfer"; }
        else { final_pc = PC + inc; next = "sequential"; }
        int logs = alu::cls(u) == 'L' ? 1 : 0;
        std::string got = (rd_imm ? std::string("\u2014") : hx(rd_after)) + "|" + hx(final_pc) + "|" + next + "|" + std::to_string(logs);
        if (o.undefined) {
            flagged++;
            check(has(note, "UB"), what + ": flagged undefined but the vector is defined");
            rep.push_back("flag " + what + ": undefined for the replayer (" + o.undefined + "), flagged; tracer " + f[3] + ", model " + got);
            continue;
        }
        check(!has(note, "UB"), what + ": an undefined input was not flagged");
        bool ok = got == f[3];
        check(ok, what + ": tracer " + f[3] + ", model " + got);
        if (ok) { pass++; covered[u] = true; }
    }
    // ref meant this check (ReplayerTest.java line 105; its append is lost in a comment): every executed opcode but NOP
    // (the padding row, unit-tested) has a matching vector
    for (unsigned i = 1; i < 256; i++)
        if (alu::executes(i)) check(covered[i], std::string("no matching vector for ") + alu::name_or_null(i));
    std::snprintf(buf, sizeof buf,
                  "ALU vectors: %zu rows; %d match the tracer; %d undefined for the replayer and flagged (invariant 16); %d not applicable",
                  lines.size(), pass, flagged, na);
    report.push_back(buf);
    for (const std::string& r : rep) report.push_back("  " + r);
    return failed;
}

}  // namespace

TEST(alu_vectors_and_opcode_table_equal_ref) {
    std::vector<std::string> report;
    CHECK_EQ(alu_report(report), 0);
    std::vector<std::string> want;
    CHECK(read_lines(kDir + "alu-report.txt", want));
    CHECK_EQ(report.size(), want.size());
    for (size_t i = 0; i < want.size(); i++) {
        if (report[i] != want[i]) {
            std::fprintf(stderr, "  line %zu\n    ref:  %s\n    ours: %s\n", i + 1, want[i].c_str(), report[i].c_str());
            CHECK(report[i] == want[i]);
        }
    }
}

TEST(event_counts_follow_ref) {
    // EventCounts' timing: a post at t is visible from t + 1; set's count also hides at its own cycle (ref's lastPostAt)
    EventCounts e;
    const uint8_t depth[] = {1, 0, 2};
    const bool flags[] = {false, false, true};
    e.init(3, depth, flags);
    e.post(10, 0);
    CHECK_EQ(e.value(10), 0u);
    CHECK_EQ(e.value(11), 1u);
    e.set(11, 2, 5);                  // clamped to the depth
    CHECK_EQ(e.count_of(2), 2u);
    CHECK_EQ(e.value(11), 1u | 4u);   // count 2, minus 1 at its own cycle: still set
    e.post(12, 2);                    // full: flagged
    CHECK_EQ(e.full_posts, 1u);
    e.live(1, true);
    CHECK_EQ(e.value(13), 7u);
    CHECK_EQ(e.consume(7u), 5u);      // the live bit is never consumed
    CHECK_EQ(e.value(13), 2u | 4u);
    CHECK_EQ(e.consume(4u), 4u);
    CHECK_EQ(e.consume(4u), 0u);
    CHECK_EQ(e.value(13), 2u);
}

namespace {

// ReplayConf.Rng and mix, mirrored (tests/ref/ReplayConf.java).
struct SweepRng {
    test::Rng r;
    explicit SweepRng(uint64_t seed) : r(seed) {}
    uint64_t next() { return r.next(); }
    uint64_t value() {
        uint64_t x = next();
        unsigned k = unsigned(x >> 8) & 63, j = unsigned(x >> 16) & 63;
        switch (x & 7) {
            case 0: return next();
            case 1: return (x >> 24) % 71;
            case 2: return 0 - (x >> 24) % 71;
            case 3: return uint64_t(1) << k;
            case 4: return (k == 0 ? 0 : (~uint64_t(0) >> (64 - k))) << j;
            case 5: return 0;
            case 6: return uint64_t(1) << 63 | (x >> 24) % 71;
            default: return next() & 0xFFFFFFFFull;
        }
    }
};
uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
std::string hex(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }

}  // namespace

TEST(alu_and_rowcore_sweeps_equal_ref) {
    std::vector<std::string> want;
    CHECK(read_lines(kDir + "alu-sweep.txt", want));
    CHECK(want.size() > 2);
    uint64_t seed = 0, n = 0;
    CHECK(std::sscanf(want[0].c_str(), "# replayconf alu v1: seed %llu n %llu", (unsigned long long*)&seed, (unsigned long long*)&n) == 2);
    std::vector<std::string> got;
    got.push_back(want[0]);
    for (unsigned op = 0; op < 256; op++) {
        if (!alu::executes(op)) continue;
        SweepRng g(seed * 1000 + op);
        uint64_t h = 0;
        unsigned undef = 0;
        for (uint64_t i = 0; i < n; i++) {
            uint64_t va = g.value(), vb = g.value(), vc = g.value(), pc = g.value(), log = g.value(), so = g.value();
            uint64_t r = g.next();
            const unsigned incs[] = {0, 2, 4};
            alu::Out o;
            alu::exec(op, va, vb, vc, pc, incs[r % 3], (r >> 8) & 1, log, so, o);
            h = mix(h ^ o.rd); h = mix(h ^ o.new_pc); h = mix(h ^ (o.undefined ? 1 : 0));
            undef += o.undefined != nullptr;
        }
        got.push_back(std::to_string(op) + " " + alu::name_or_null(op) + " " + hex(h) + " " + std::to_string(undef));
    }
    SweepRng g(seed);
    uint64_t h = 0;
    for (uint64_t i = 0; i < n * 16; i++) {
        uint32_t w = uint32_t(g.value());
        uint64_t lo = g.value(), cnst = g.value(), imm = g.value();
        h = mix(h ^ rowcore::decode_index(w));
        for (unsigned f = 0; f < 6; f++) h = mix(h ^ rowcore::source_imm(f, w));
        for (unsigned c = 0; c < 146; c++) {
            h = mix(h ^ uint64_t(int64_t(rowcore::reg(c, w, 0))));
            h = mix(h ^ uint64_t(int64_t(rowcore::reg(c, w, 1))));
        }
        for (unsigned c : {128u, 132u, 133u, 134u, 135u, 136u, 145u}) h = mix(h ^ rowcore::imm_val(c, cnst, imm));
        h = mix(h ^ rowcore::invalid_word(lo));
        h = mix(h ^ (rowcore::needs_source_imm(lo) ? 1 : 0));
    }
    got.push_back("rowcore " + hex(h));
    CHECK_EQ(got.size(), want.size());
    int bad = 0;
    for (size_t i = 0; i < want.size(); i++)
        if (got[i] != want[i]) { bad++; std::fprintf(stderr, "  ref:  %s\n  ours: %s\n", want[i].c_str(), got[i].c_str()); }
    CHECK_EQ(bad, 0);
}
