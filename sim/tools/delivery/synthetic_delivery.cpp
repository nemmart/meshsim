// synthetic_delivery (WORKER19, P1, D42): v1.3's SyntheticDelivery (sim/ref/meshsim/test/SyntheticDelivery.java) in C++,
// its files byte for byte, with one deliberate difference that makes it reproducible per seed (D42; DECISIONS):
//   - the inlines' advice (Func.advice()'s value for every advice word that isn't a division's) comes from
//     SplittableRandom(seed ^ K), K = 0xD42. v1.3's came from an unseeded SplittableRandom, so every window from the
//     first inline's advice on differed run to run (WORKER19 NOTES, D42);
//   - the manifest's small maps (each file's bytes and sha256; a window's counts) are written in the source's order:
//     bytes, sha256; instructions, read_log_words, write_log_words, padding_rows. v1.3's Map.of iterated in an order
//     salted per JVM run.
// Checked against a copy of v1.3's patched in exactly those two places (oracle/synthetic_crosscheck.sh): every file of
// every window equal, for every seed ref's tests use; and against v1.3 itself: equal with no inlines, equal up to the
// first inline otherwise, the manifest equal as JSON.
//
// A synthetic replay delivery (REPLAY-INPUT-REQUIREMENTS.md, v1.25) from the universal tables: a random RV64IMAC program
// expanded through the tables by a functional replay (Func), each log word supplied as a tracer would; every ALU, M,
// memory, branch, jump and atomic instruction also run by a plain RV64 interpreter (Ref) and the replay's x1-x31, pc and
// memory checked against it after each instruction. The windows are consecutive slices of the one program; the last
// ends the program (JAL x0, 0) and pads. Files per window: the three streams raw and LRU-coded, the state records state,
// state-lru and state-pick, rows.bin, end.bin; manifest.json with sizes and SHA-256; the tables under tables/.
//
//   synthetic_delivery OUT [--program NAME] [--windows N] [--rows R] [--first K] [--seed S] [--inline-weight W]
//                      [--no-end] [--tables DIR] [--w9-layout]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../src/units/replay_tables.h"
#include "../../src/units/replayer.h"
#include "../../src/units/replayer_alu.h"
#include "../../src/units/row_core.h"
#include "../../tests/replay_window.h"
#include "../../tests/sha256.h"

namespace fs = std::filesystem;
namespace alu = units::alu;
namespace {

constexpr uint64_t K_ADVICE = 0xD42;   // the deliberate difference (D42): the inlines' advice from SplittableRandom(seed ^ K)

[[noreturn]] void die(const std::string& m) { fprintf(stderr, "synthetic_delivery: %s\n", m.c_str()); exit(1); }
std::string hexs(uint64_t v) { char s[24]; snprintf(s, sizeof s, "%llx", (unsigned long long)v); return s; }

// ---- java.util.SplittableRandom (JDK 17-21), exactly
struct SplittableRandom {
    uint64_t seed;
    static constexpr uint64_t GAMMA = 0x9e3779b97f4a7c15ull;
    explicit SplittableRandom(int64_t s) : seed(uint64_t(s)) {}
    uint64_t next_seed() { return seed += GAMMA; }
    static uint64_t mix64(uint64_t z) { z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; return z ^ (z >> 31); }
    static int32_t mix32(uint64_t z) { z = (z ^ (z >> 33)) * 0x62a9d9ed799705f5ull; return int32_t(uint32_t(((z ^ (z >> 28)) * 0xcb24d0a5c88c35b3ull) >> 32)); }
    int64_t next_long() { return int64_t(mix64(next_seed())); }
    int32_t next_int() { return mix32(next_seed()); }
    int32_t next_int(int32_t bound) {
        int32_t r = next_int();
        const int32_t m = bound - 1;
        if ((bound & m) == 0) r &= m;
        else {
            for (int32_t u = int32_t(uint32_t(r) >> 1); int32_t(uint32_t(u) + uint32_t(m) - uint32_t(r = u % bound)) < 0;
                 u = int32_t(uint32_t(next_int()) >> 1)) {}
        }
        return r;
    }
    bool next_boolean() { return next_int() < 0; }
};

uint64_t mix(uint64_t z) { z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }

// ---- memory: doublewords; unwritten ones are a hash of the address
struct Mem {
    std::unordered_map<uint64_t, uint64_t> m;
    uint64_t rd(uint64_t a) const { a &= ~7ull; auto it = m.find(a); return it != m.end() ? it->second : mix(a * 0x9E3779B97F4A7C15ull + 12345); }
    void wr(uint64_t a, uint64_t v) { m[a & ~7ull] = v; }
    uint64_t rd_n(uint64_t a, int bytes) const { const uint64_t d = rd(a); const int sh = int(a & 7) * 8; return bytes == 8 ? d : (d >> sh) & ((1ull << (8 * bytes)) - 1); }
    void wr_n(uint64_t a, int bytes, uint64_t v) {
        if (bytes == 8) { wr(a, v); return; }
        const uint64_t d = rd(a); const int sh = int(a & 7) * 8; const uint64_t mask = ((1ull << (8 * bytes)) - 1) << sh;
        wr(a, (d & ~mask) | ((v << sh) & mask));
    }
};

struct Log { bool has_read; uint64_t read; bool load, has_write; uint64_t write; };

uint64_t source_imm(unsigned format, uint32_t w) { return units::rowcore::source_imm(format, w); }

struct Gen;

// ---- the functional replay (a replayer without queues or timing)
struct Func {
    Gen* g;
    const units::tracer::ReplayTables* tb;
    uint64_t regs[128] = {}; uint64_t pc = 0; int32_t idx = 0;
    Mem mem;
    uint32_t w = 0; int kind = 0, offset = 0, next_row = 0, tnum = -1; std::vector<uint64_t> words; uint64_t src_a = 0, src_b = 0;
    bool active = false;
    std::vector<uint64_t> written;
    enum { PAD = 0, ROM = 1, NOP = 2, CTRL = 3 };

    void start(uint32_t w_, int start_row) {
        w = w_; next_row = start_row; offset = 0; tnum = -1; active = true; written.clear();
        src_a = regs[(w >> 15) & 31]; src_b = regs[(w >> 20) & 31];
        if ((w & 3) == 0) { kind = PAD; words = {tb->rom_lo[0], tb->rom_hi[0]}; return; }
        const unsigned e = tb->table[units::rowcore::decode_index(w)] & 0xFFFF, cls = e & 3, rdf = (w >> 7) & 31;
        unsigned rd0;
        if (cls == 3) die("generated an invalid instruction 0x" + hexs(w));
        if (cls == 0) {
            const unsigned s = (e >> 2) & 0x1FF; rd0 = unsigned((tb->rom_lo[s] >> 46) & 3);
            words.clear();
            for (unsigned a = s; ; a++) { words.push_back(tb->rom_lo[a]); words.push_back(tb->rom_hi[a]); if (((tb->rom_lo[a] >> 32) & 0xFFF) == 0) break; }
            kind = ROM;
        } else {
            tnum = tb->lookup(w);
            if (tnum < 0) die("no template for 0x" + hexs(w));
            words = tb->words[size_t(tnum)]; rd0 = tb->rd0[size_t(tnum)]; kind = CTRL;
        }
        if (rdf == 0 && (rd0 == 1 || rd0 == 3)) {          // the shared NOP row, carrying the template's rd0 (N3)
            words = {(tb->rom_lo[1] & ~(3ull << 46)) | uint64_t(rd0) << 46, tb->rom_hi[1]};
            kind = NOP; next_row = 0;
        } else offset = rdf == 0 && rd0 == 2 ? 1 : 0;
    }
    int reg(int c) const {
        if (c < 128) return c;
        if (c == 129) return offset == 1 ? 40 : int((w >> 7) & 31);
        if (c == 130) return int((w >> 15) & 31);
        if (c == 131) return int((w >> 20) & 31);
        if (c >= 137 && c <= 144) return 40 + (c - 137) + offset;
        return -1;
    }
    static uint64_t imm(int c, uint64_t cnst, uint64_t si) {
        switch (c) {
            case 128: return cnst; case 145: return 0; case 132: return si; case 133: return uint64_t(-(int64_t(1) << (si & 63)));
            case 134: return 1ull << (si & 63); case 135: return 1ull << (si & 31); case 136: return (1ull << 32) - (1ull << (si & 31));
            default: die("code " + std::to_string(c));
        }
    }
    uint64_t rv(int r) const { return r == 0 ? 0 : regs[r]; }
    uint64_t advice();
    void step(std::vector<Log>& logs, uint64_t r[10]);
};

// ---- the RV64 reference (checked instructions only)
struct Ref {
    uint64_t x[32] = {}; uint64_t pc = 0; Mem mem; int64_t resv = -1;
    static int64_t mulhi(int64_t a, int64_t b) { return int64_t((__int128)a * b >> 64); }
    bool exec(uint32_t w) {
        const unsigned op = (w | 1) & 0x7F, rd = (w >> 7) & 31, rs1 = (w >> 15) & 31, rs2 = (w >> 20) & 31, f3 = (w >> 12) & 7, f7 = w >> 25;
        const uint64_t a = x[rs1], b = x[rs2], inc = (w & 1) == 0 ? 2 : 4;
        const int64_t sa = int64_t(a), sb = int64_t(b);
        uint64_t npc = pc + inc, v = 0; bool wr = true;
        const uint64_t iI = source_imm(1, w), iS = source_imm(2, w), iB = source_imm(3, w), iU = source_imm(4, w), iJ = source_imm(5, w);
        switch (op) {
            case 0x37: v = iU; break;
            case 0x17: v = pc + iU; break;
            case 0x6F: if (iJ == 0) return false; v = pc + inc; npc = pc + iJ; break;
            case 0x67: v = pc + inc; npc = (a + iI) & ~1ull; break;
            case 0x63: { wr = false; bool t;
                switch (f3) { case 0: t = a == b; break; case 1: t = a != b; break; case 4: t = sa < sb; break; case 5: t = sa >= sb; break;
                              case 6: t = a < b; break; case 7: t = a >= b; break; default: die("branch f3"); }
                if (t) npc = pc + iB;
                break; }
            case 0x03: { const uint64_t ad = a + iI;
                switch (f3) { case 0: v = uint64_t(int64_t(int8_t(mem.rd_n(ad, 1)))); break; case 1: v = uint64_t(int64_t(int16_t(mem.rd_n(ad, 2)))); break;
                              case 2: v = uint64_t(int64_t(int32_t(mem.rd_n(ad, 4)))); break; case 3: v = mem.rd_n(ad, 8); break;
                              case 4: v = mem.rd_n(ad, 1); break; case 5: v = mem.rd_n(ad, 2); break; case 6: v = mem.rd_n(ad, 4); break; default: die("load f3"); }
                break; }
            case 0x23: wr = false; mem.wr_n(a + iS, 1 << f3, b); break;
            case 0x13: { const int sh = int(iI & 63);
                switch (f3) { case 0: v = a + iI; break; case 1: v = a << sh; break; case 2: v = sa < int64_t(iI) ? 1 : 0; break; case 3: v = a < iI ? 1 : 0; break;
                              case 4: v = a ^ iI; break; case 5: v = (w & 1u << 30) != 0 ? uint64_t(sa >> sh) : a >> sh; break; case 6: v = a | iI; break; default: v = a & iI; }
                break; }
            case 0x1B: { const int sh = int(iI & 31);
                switch (f3) { case 0: v = uint64_t(int64_t(int32_t(uint32_t(a + iI)))); break; case 1: v = uint64_t(int64_t(int32_t(uint32_t(a) << sh))); break;
                              case 5: v = (w & 1u << 30) != 0 ? uint64_t(int64_t(int32_t(a) >> sh)) : uint64_t(int64_t(int32_t(uint32_t(a) >> sh))); break;
                              default: die("op-imm-32 f3"); }
                break; }
            case 0x33:
                if (f7 == 1) { switch (f3) {
                    case 0: v = a * b; break; case 1: v = uint64_t(mulhi(sa, sb)); break; case 2: v = uint64_t(mulhi(sa, sb)) + ((uint64_t(sb >> 63)) & a); break;
                    case 3: v = alu::mulhu(a, b); break;
                    case 4: v = b == 0 ? ~0ull : (sa == INT64_MIN && sb == -1) ? a : uint64_t(sa / sb); break;
                    case 5: v = b == 0 ? ~0ull : a / b; break;
                    case 6: v = b == 0 ? a : (sa == INT64_MIN && sb == -1) ? 0 : uint64_t(sa % sb); break;
                    default: v = b == 0 ? a : a % b;
                } } else { switch (f3) {
                    case 0: v = f7 == 0x20 ? a - b : a + b; break; case 1: v = a << (b & 63); break; case 2: v = sa < sb ? 1 : 0; break; case 3: v = a < b ? 1 : 0; break;
                    case 4: v = a ^ b; break; case 5: v = f7 == 0x20 ? uint64_t(sa >> (b & 63)) : a >> (b & 63); break; case 6: v = a | b; break; default: v = a & b;
                } }
                break;
            case 0x3B: { const int32_t p = int32_t(a), q = int32_t(b); int32_t r = 0;
                if (f7 == 1) { switch (f3) {
                    case 0: r = int32_t(uint32_t(p) * uint32_t(q)); break;
                    case 4: r = q == 0 ? -1 : (p == INT32_MIN && q == -1) ? p : p / q; break;
                    case 5: r = q == 0 ? -1 : int32_t(uint32_t(p) / uint32_t(q)); break;
                    case 6: r = q == 0 ? p : (p == INT32_MIN && q == -1) ? 0 : p % q; break;
                    case 7: r = q == 0 ? p : int32_t(uint32_t(p) % uint32_t(q)); break;
                    default: die("op-32 M f3");
                } } else { switch (f3) {
                    case 0: r = int32_t(f7 == 0x20 ? uint32_t(p) - uint32_t(q) : uint32_t(p) + uint32_t(q)); break;
                    case 1: r = int32_t(uint32_t(p) << (q & 31)); break;
                    case 5: r = f7 == 0x20 ? p >> (q & 31) : int32_t(uint32_t(p) >> (q & 31)); break;
                    default: die("op-32 f3");
                } }
                v = uint64_t(int64_t(r)); break; }
            case 0x2F: {
                const unsigned f5 = w >> 27; const bool dw = f3 == 3; const int n = dw ? 8 : 4;
                const uint64_t old = dw ? mem.rd_n(a, 8) : uint64_t(int64_t(int32_t(mem.rd_n(a, 4))));
                if (f5 == 2) { v = old; resv = int64_t(a); }
                else if (f5 == 3) { if (resv == int64_t(a)) { mem.wr_n(a, n, b); v = 0; } else v = 1; resv = -1; }
                else {
                    const uint64_t bb = dw ? b : uint64_t(int64_t(int32_t(b))); uint64_t nv;
                    switch (f5) {
                        case 1: nv = bb; break; case 0: nv = old + bb; break; case 4: nv = old ^ bb; break; case 12: nv = old & bb; break; case 8: nv = old | bb; break;
                        case 16: nv = int64_t(old) < int64_t(bb) ? old : bb; break; case 20: nv = int64_t(old) > int64_t(bb) ? old : bb; break;
                        case 24: nv = dw ? (old < bb ? old : bb) : (uint32_t(old) < uint32_t(bb) ? old : bb); break;
                        case 28: nv = dw ? (old > bb ? old : bb) : (uint32_t(old) > uint32_t(bb) ? old : bb); break;
                        default: die("amo " + std::to_string(f5));
                    }
                    mem.wr_n(a, n, nv); v = old;
                }
                break; }
            case 0x0F: wr = false; break;
            default: return false;
        }
        if (wr && rd != 0) x[rd] = v;
        if (npc == pc) npc = pc + inc;      // jolt-cpp's rule (appendix 1.4): a transfer to its own pc is sequential
        pc = npc;
        return true;
    }
};

// ---- the generator
struct Gen {
    fs::path out; std::string program = "synthetic"; int windows = 4, rows = 65536, first = 0; int64_t seed = 1;
    int inline_weight = 2; bool end = true; std::string tables_dir = units::tracer::ReplayTables::default_dir(); bool w9 = false;
    units::tracer::ReplayTables tb;
    SplittableRandom rnd{0}, gen{0};
    int32_t lr_pending = -1;
    long long checked = 0; long long row_count = 0; bool ref_check = false;
    uint32_t hist_w[8] = {}; uint64_t hist_pc[8] = {}; int hist_n = 0;
    static constexpr uint64_t BASE2 = 0x80100000ull, BASE3 = 0x80200000ull, BASE4 = 0x80300000ull, MTVEC = 0x80001000ull;

    int reg() { return gen.next_int(32); }
    int rd_reg() { int r; do r = gen.next_int(32); while (r >= 2 && r <= 4); return gen.next_int(10) == 0 ? 0 : r; }
    static int32_t R(int f7, int rs2, int rs1, int f3, int rd, int op) { return int32_t(uint32_t(f7) << 25 | uint32_t(rs2) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(rd) << 7 | uint32_t(op)); }
    static int32_t I(int32_t imm, int rs1, int f3, int rd, int op) { return int32_t(uint32_t(imm & 0xFFF) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(rd) << 7 | uint32_t(op)); }
    static int32_t S(int32_t imm, int rs2, int rs1, int f3, int op) { return int32_t(uint32_t(imm >> 5 & 0x7F) << 25 | uint32_t(rs2) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(imm & 31) << 7 | uint32_t(op)); }
    static int32_t B(int32_t imm, int rs2, int rs1, int f3) { return int32_t(uint32_t(imm >> 12 & 1) << 31 | uint32_t(imm >> 5 & 0x3F) << 25 | uint32_t(rs2) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(imm >> 1 & 15) << 8 | uint32_t(imm >> 11 & 1) << 7 | 0x63u); }
    static int32_t J(int32_t imm, int rd) { return int32_t(uint32_t(imm >> 20 & 1) << 31 | uint32_t(imm >> 1 & 0x3FF) << 21 | uint32_t(imm >> 11 & 1) << 20 | uint32_t(imm >> 12 & 0xFF) << 12 | uint32_t(rd) << 7 | 0x6Fu); }

    // the next instruction word (bit 0 clear for a compressed one); every call to gen in Java's (left-to-right) order
    int32_t next() {
        if (lr_pending != -1) { const int32_t sc = lr_pending; lr_pending = -1; return sc; }
        int32_t w; const int pick = gen.next_int(100 + inline_weight);
        bool can_c = false;
        if (pick < 30) {
            const int k = gen.next_int(4);
            if (k == 0) { const int f3 = gen.next_int(8); const int f7 = (f3 == 0 || f3 == 5) && gen.next_boolean() ? 0x20 : 0;
                          const int rs2 = reg(), rs1 = reg(), rd = rd_reg(); w = R(f7, rs2, rs1, f3, rd, 0x33); }
            else if (k == 1) { const int f3 = gen.next_int(8); int imm;
                               if (f3 == 1) imm = gen.next_int(64);
                               else if (f3 == 5) { const int lo = gen.next_int(64); imm = lo | (gen.next_boolean() ? 0x400 : 0); }
                               else imm = gen.next_int(4096) - 2048;
                               const int rs1 = reg(), rd = rd_reg(); w = I(imm, rs1, f3, rd, 0x13); }
            else if (k == 2) { static const int f3s[] = {0, 1, 5}; const int f3 = f3s[gen.next_int(3)];
                               const int f7 = (f3 == 0 || f3 == 5) && gen.next_boolean() ? 0x20 : 0;
                               const int rs2 = reg(), rs1 = reg(), rd = rd_reg(); w = R(f7, rs2, rs1, f3, rd, 0x3B); }
            else { static const int f3s[] = {0, 1, 5}; const int f3 = f3s[gen.next_int(3)]; int imm;
                   if (f3 == 0) imm = gen.next_int(4096) - 2048;
                   else { const int lo = gen.next_int(32); imm = lo | (f3 == 5 && gen.next_boolean() ? 0x400 : 0); }
                   const int rs1 = reg(), rd = rd_reg(); w = I(imm, rs1, f3, rd, 0x1B); }
            can_c = true;
        } else if (pick < 34) { const int32_t hi = gen.next_int(1 << 20); const int rd = rd_reg(); const bool lui = gen.next_boolean();
                                w = int32_t(uint32_t(hi) << 12 | uint32_t(rd) << 7 | (lui ? 0x37u : 0x17u)); }
        else if (pick < 44) { const int f3 = gen.next_int(8); const int rs2 = reg(), rs1 = reg(), rd = rd_reg(); w = R(1, rs2, rs1, f3, rd, 0x33); }
        else if (pick < 47) { static const int f3s[] = {0, 4, 5, 6, 7}; const int f3 = f3s[gen.next_int(5)];
                              const int rs2 = reg(), rs1 = reg(), rd = rd_reg(); w = R(1, rs2, rs1, f3, rd, 0x3B); }
        else if (pick < 60) { const int f3 = gen.next_int(7), n = 1 << (f3 & 3); const int base = 2 + gen.next_int(3);
                              const int32_t imm = (gen.next_int(256) - 128) * n; const int rd = rd_reg(); w = I(imm, base, f3, rd, 0x03); can_c = true; }
        else if (pick < 70) { const int f3 = gen.next_int(4), n = 1 << f3; const int base = 2 + gen.next_int(3);
                              const int32_t imm = (gen.next_int(256) - 128) * n; const int rs2 = reg(); w = S(imm, rs2, base, f3, 0x23); can_c = true; }
        else if (pick < 80) { static const int f3s[] = {0, 1, 4, 5, 6, 7}; const int f3 = f3s[gen.next_int(6)];
                              const int32_t imm = (gen.next_int(2048) - 1024) * 2; const int rs2 = reg(), rs1 = reg(); w = B(imm, rs2, rs1, f3); can_c = true; }
        else if (pick < 83) { int32_t imm; do imm = (gen.next_int(1 << 19) - (1 << 18)) * 2; while (imm == 0); const int rd = rd_reg(); w = J(imm, rd); can_c = true; }
        else if (pick < 85) { const int32_t imm = gen.next_int(4096) - 2048; const int base = 2 + gen.next_int(3); const int rd = rd_reg();
                              w = I(imm, base, 0, rd, 0x67); can_c = true; }
        else if (pick < 93) {
            const bool dw = gen.next_boolean(); const int f3 = dw ? 3 : 2; const int base = 2 + gen.next_int(3); const int rd = rd_reg();
            static const int f5s[] = {2, 1, 0, 4, 12, 8, 16, 20, 24, 28};
            const int f5 = f5s[gen.next_int(10)];
            const int aq = gen.next_int(4);
            if (f5 == 2) {
                w = int32_t(2u << 27 | uint32_t(aq) << 25 | uint32_t(base) << 15 | uint32_t(f3) << 12 | uint32_t(rd) << 7 | 0x2Fu);
                if (gen.next_int(4) == 0) lr_pending = -1;
                else { const int aq2 = gen.next_int(4); const int rs2 = reg(); const int rd2 = rd_reg();
                       lr_pending = int32_t(3u << 27 | uint32_t(aq2) << 25 | uint32_t(rs2) << 20 | uint32_t(base) << 15 | uint32_t(f3) << 12 | uint32_t(rd2) << 7 | 0x2Fu); }
            } else { const int rs2 = reg(); w = int32_t(uint32_t(f5) << 27 | uint32_t(aq) << 25 | uint32_t(rs2) << 20 | uint32_t(base) << 15 | uint32_t(f3) << 12 | uint32_t(rd) << 7 | 0x2Fu); }
        } else if (pick < 94) w = 0x0FF0000F;
        else if (pick < 95) w = 0x0000205B;
        else if (pick < 96) w = 0x00000073;
        else if (pick < 98) {
            static const int CSRS[] = {0x300, 0x305, 0x340, 0x341, 0x342, 0x343};
            const int csr = CSRS[gen.next_int(6)]; int f3 = 1 + gen.next_int(2);
            const int rd = gen.next_int(3) == 0 ? 0 : rd_reg();
            int rs1;
            if (gen.next_int(3) == 0) rs1 = 0;
            else if (gen.next_int(3) == 0 && rd != 0) rs1 = rd;
            else rs1 = reg();
            if (csr == 0x305) { f3 = 2; rs1 = 0; }
            w = int32_t(uint32_t(csr) << 20 | uint32_t(rs1) << 15 | uint32_t(f3) << 12 | uint32_t(rd) << 7 | 0x73u);
        } else if (pick < 100) w = 0x0FF0000F;
        else {
            std::vector<uint32_t> inl; for (int k = 0; k < tb.count(); k++) if (tb.cls[size_t(k)] == 1) inl.push_back(tb.key[size_t(k)]);
            const uint32_t key = inl[size_t(gen.next_int(int32_t(inl.size())))];
            w = int32_t(key | 4u << 7 | 2u << 15 | 3u << 20);
        }
        if (can_c && gen.next_int(5) == 0) w &= ~1;
        return w;
    }

    [[noreturn]] void fail(const Func& f, const std::string& m) {
        std::string h;
        for (int i = 8; i >= 1; i--) if (hist_n - i >= 0) { char s[48]; snprintf(s, sizeof s, " %08x@%llx", hist_w[(hist_n - i) & 7], (unsigned long long)hist_pc[(hist_n - i) & 7]); h += s; }
        char s[96]; snprintf(s, sizeof s, "RV64 check failed after 0x%08x (now pc 0x%llx): ", f.w, (unsigned long long)f.pc);
        die(s + m + "; last instructions (word@pc):" + h);
    }
    void after_instr(Func& f, Ref& ref) {
        if (ref_check) {
            checked++;
            for (int i = 1; i < 32; i++) if (f.regs[i] != ref.x[i]) fail(f, "x" + std::to_string(i) + ": replay 0x" + hexs(f.regs[i]) + ", RV64 0x" + hexs(ref.x[i]));
            if (f.pc != ref.pc) fail(f, "pc: replay 0x" + hexs(f.pc) + ", RV64 0x" + hexs(ref.pc));
            for (uint64_t a : f.written) if (f.mem.rd(a) != ref.mem.rd(a)) fail(f, "memory 0x" + hexs(a));
            for (const auto& e : ref.mem.m) if (f.mem.rd(e.first) != e.second) fail(f, "memory 0x" + hexs(e.first) + " (RV64 wrote it)");
        } else {
            for (int i = 0; i < 32; i++) ref.x[i] = f.regs[i];
            ref.x[0] = 0; ref.pc = f.pc;
            for (uint64_t a : f.written) ref.mem.wr(a, f.mem.rd(a));
            if (f.regs[2] != BASE2 || f.regs[3] != BASE3 || f.regs[4] != BASE4) fail(f, "an unchecked instruction moved a base register (x2-x4)");
        }
        ref_check = false;
    }

    void run();
    std::string write_window(const fs::path& pd, int win, const uint64_t* regs, uint64_t pc, int32_t idx, int off, const std::vector<uint32_t>& code,
                             const std::vector<Log>& logs, const std::vector<uint64_t>& rows_out, const std::set<int>& used, const Func& f);
};

uint64_t Func::advice() {
    const unsigned op = (w | 1) & 0x7F, f3 = (w >> 12) & 7, f7 = w >> 25;
    const uint64_t a = src_a, b = src_b; const int64_t sa = int64_t(a), sb = int64_t(b);
    if (f7 == 1 && op == 0x33) switch (f3) {
        case 4: return b == 0 ? ~0ull : (sa == INT64_MIN && sb == -1) ? a : uint64_t(sa / sb);
        case 5: return b == 0 ? ~0ull : a / b;
        case 6: { if (b == 0) return 0; if (sa == INT64_MIN && sb == -1) return uint64_t(INT64_MIN); const int64_t q = sa / sb; return uint64_t(q < 0 ? -q : q); }
        case 7: return b == 0 ? 0 : a / b;
    }
    if (f7 == 1 && op == 0x3B) {
        const int32_t x = int32_t(a), y = int32_t(b);
        switch (f3) {
            case 4: return y == 0 ? ~0ull : (x == INT32_MIN && y == -1) ? 0x80000000ull : uint64_t(int64_t(x / y));
            case 5: return y == 0 ? 0xFFFFFFFFull : uint64_t(uint32_t(x) / uint32_t(y));
            case 6: { if (y == 0) return 0; if (x == INT32_MIN && y == -1) return 0x80000000ull; const int32_t q = x / y; return uint64_t(uint32_t(q < 0 ? -q : q)); }
            case 7: return y == 0 ? 0 : uint64_t(uint32_t(x) / uint32_t(y));
        }
    }
    return uint64_t(g->rnd.next_long());
}

void Func::step(std::vector<Log>& logs, uint64_t r[10]) {
    const int row = next_row;
    const uint64_t lo = words[size_t(2 * row)], hi = words[size_t(2 * row + 1)];
    const unsigned op = unsigned(lo & 0xFF); const int c_rd = int((lo >> 8) & 0xFF), c_rs1 = int((lo >> 16) & 0xFF), c_rs2 = int((lo >> 24) & 0xFF);
    const unsigned seq = unsigned((lo >> 32) & 0xFFF), format = unsigned((lo >> 48) & 7);
    const bool nfv = ((lo >> 44) & 1) != 0, hold = ((lo >> 45) & 1) != 0, virt = ((lo >> 51) & 1) != 0, imm0pc0 = ((lo >> 52) & 1) != 0;
    const bool report_c = ((lo >> 53) & 1) != 0, from_log = ((lo >> 54) & 1) != 0, sign_imm = ((lo >> 62) & 1) != 0, off_imm = ((lo >> 63) & 1) != 0;
    if (!alu::executes(op) || ((lo >> 55) & 0x7F) != 0) die("bad word in 0x" + hexs(w));
    const uint64_t si = source_imm(format, w), so = off_imm ? si : 0;
    const int r_rd = reg(c_rd), r_rs1 = reg(c_rs1), r_rs2 = reg(c_rs2);
    const uint64_t v_rd = r_rd >= 0 ? rv(r_rd) : imm(c_rd, hi, si), v_rs1 = r_rs1 >= 0 ? rv(r_rs1) : imm(c_rs1, hi, si), v_rs2 = r_rs2 >= 0 ? rv(r_rs2) : imm(c_rs2, hi, si);
    const uint64_t imm_sum = (r_rd < 0 ? v_rd : 0) + (r_rs1 < 0 ? v_rs1 : 0) + (r_rs2 < 0 ? v_rs2 : 0);
    uint64_t log_word = 0; bool rd_ = false, wr_ = false, ld = false; uint64_t addr = 0;
    if (from_log) {
        if (alu::is_load(op)) { addr = alu::mem_addr(op, v_rs1, imm_sum); log_word = mem.rd(addr); rd_ = true; ld = true; }
        else if (alu::is_store(op)) { addr = alu::mem_addr(op, v_rs1, imm_sum); log_word = mem.rd(addr); wr_ = true; }
        else { log_word = advice(); rd_ = true; }
    }
    const bool last = seq == 0 || kind == PAD || kind == NOP, compressed = (w & 1) == 0;
    const unsigned pc_inc = !last ? 0 : compressed ? 2 : 4;
    alu::Out o;
    alu::exec(op, v_rs1, v_rs2, v_rd, pc, pc_inc, imm0pc0, log_word, so, o);
    const bool writes = r_rd >= 0 && op != alu::NOP;
    const uint64_t rd_after = r_rd < 0 ? 0 : r_rd == 0 ? 0 : writes ? o.rd : v_rd;
    uint64_t new_pc; int32_t new_idx; int64_t transfer_word = -1;
    if (hold) { new_pc = pc; new_idx = idx; }
    else if (o.new_pc == 0) { new_pc = 0; new_idx = 0; if (nfv) transfer_word = 0; }
    else if (o.new_pc != pc) { new_pc = o.new_pc; new_idx = int32_t(mix(new_pc) & 0x3FFFFFF) + 1; transfer_word = new_idx; }
    else { new_pc = pc + pc_inc; new_idx = idx + 1; }
    if (transfer_word >= 0) { if (rd_) die("two read-log words in one row"); rd_ = true; log_word = uint64_t(transfer_word); }
    if (rd_ || wr_) logs.push_back(Log{rd_, rd_ ? log_word : 0, ld, wr_, wr_ ? log_word : 0});
    for (int k = 0; k < 10; k++) r[k] = 0;
    if (kind != PAD) {
        const bool is_virtual = kind == NOP ? ((lo >> 46) & 3) == 1 : (virt || seq > 0 || row > 0);
        const bool is_first = is_virtual && (kind == NOP || row == 0);
        r[0] = uint64_t(uint32_t(idx)) | uint64_t(op) << 32 | uint64_t(r_rd > 0 ? r_rd : 0) << 40 | uint64_t(r_rs1 > 0 ? r_rs1 : 0) << 48 | uint64_t(r_rs2 > 0 ? r_rs2 : 0) << 56;
        r[1] = uint64_t(seq & 0xFFFF) | uint64_t(compressed && report_c ? 1 : 0) << 16 | uint64_t(is_virtual ? 1 : 0) << 17 | uint64_t(is_first ? 1 : 0) << 18
             | uint64_t(r_rd < 0 ? 1 : 0) << 19 | uint64_t(r_rs1 < 0 ? 1 : 0) << 20 | uint64_t(r_rs2 < 0 ? 1 : 0) << 21 | uint64_t(sign_imm ? 1 : 0) << units::Replayer::REC_SIGN_IMM;
        r[2] = pc; r[3] = alu::is_shift_data(op) ? so : imm_sum;
        r[4] = r_rs1 >= 0 ? v_rs1 : 0; r[5] = r_rs2 >= 0 ? v_rs2 : 0; r[6] = r_rd >= 0 ? v_rd : 0; r[7] = rd_after;
        if (alu::is_load(op)) { r[8] = addr; r[9] = rd_after; }
        else if (alu::is_store(op)) { r[8] = addr; r[9] = log_word; }
    }
    if (alu::is_store(op) && from_log) { mem.wr(addr, v_rs2); written.push_back(addr & ~7ull); }
    if (writes && r_rd != 0) regs[r_rd] = o.rd;
    pc = new_pc; idx = new_idx;
    next_row++;
    if (last) active = false;
}

// ---- files
std::string bytes_of(const std::vector<uint64_t>& v) {
    std::string b(8 * v.size(), '\0');
    for (size_t i = 0; i < v.size(); i++) for (int k = 0; k < 8; k++) b[8 * i + k] = char(v[i] >> (8 * k));
    return b;
}
void write_file(const fs::path& p, const std::string& s) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f || fwrite(s.data(), 1, s.size(), f) != s.size() || fclose(f) != 0) die("cannot write " + p.string());
}
std::string read_bin(const fs::path& p) {
    FILE* f = fopen(p.c_str(), "rb"); if (!f) die("cannot read " + p.string());
    std::string s; char buf[1 << 16]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n); fclose(f); return s;
}
std::string sha(const std::string& b) { return test::sha256_hex(b.data(), b.size()); }
units::streamword::Size size_of(const std::vector<uint64_t>& entries, uint32_t values, bool comp) {
    const int64_t words = int64_t(entries.size() / 2);
    if (words > 0xFFFF || values > 0x1FFFF) die("stream size " + std::to_string(words) + " words, " + std::to_string(values) + " values");
    units::streamword::Size s; s.words = uint32_t(words); s.values = values; s.compressed = comp; return s;
}
int32_t advice_of(uint64_t pc, int32_t idx) { const uint64_t h = (pc ^ uint64_t(int64_t(idx)) << 17) * 0x9E3779B97F4A7C15ull; return int32_t(h >> 42); }
std::vector<uint64_t> record(const uint64_t bm[2], uint64_t pc, int32_t idx, int off, const units::streamword::Size& c, const units::streamword::Size& r,
                             const units::streamword::Size& w, const uint64_t* regs) {
    std::vector<uint64_t> rec(2 * units::Replayer::REC_WORDS, 0);
    uint64_t sw[2]; units::streamword::build(c, r, w, advice_of(pc, idx), sw);
    rec[0] = pc; rec[1] = rtest::state_hi(uint32_t(idx), uint32_t(off)); rec[2] = sw[0]; rec[3] = sw[1];
    for (int i = 0; i < 128; i++) rec[size_t(4 + i)] = regs[i];
    rec[4] = 0;
    rec[2 * units::Replayer::REC_BITMAP] = bm[0]; rec[2 * units::Replayer::REC_BITMAP + 1] = bm[1];
    return rec;
}

struct Files { std::vector<std::pair<std::string, std::string>> e; };   // name -> {"bytes": n, "sha256": "..."} as JSON
void put(const fs::path& pd, const std::string& name, const std::vector<uint64_t>& v, Files& files) {
    const std::string b = bytes_of(v); write_file(pd / name, b);
    files.e.emplace_back(name, "{\"bytes\": " + std::to_string(b.size()) + ", \"sha256\": \"" + sha(b) + "\"}");
}

std::string Gen::write_window(const fs::path& pd, int win, const uint64_t* regs, uint64_t pc, int32_t idx, int off, const std::vector<uint32_t>& code,
                              const std::vector<Log>& logs, const std::vector<uint64_t>& rows_out, const std::set<int>& used, const Func& f) {
    char bb[32]; snprintf(bb, sizeof bb, "window_%05d.", win); const std::string base = bb;
    Files files;
    std::vector<rtest::LogEvent> ev;
    for (const Log& l : logs) { rtest::LogEvent e; e.has_read = l.has_read; e.read = l.read; e.load = l.load; e.has_write = l.has_write; e.write = l.write; ev.push_back(e); }
    rtest::ReplayWindow raw, lru;
    raw.pad_seed = lru.pad_seed = 0x26000000LL + win;
    raw.set_code(code, false); raw.set_logs(ev, false, false);
    lru.set_code(code, true); lru.set_logs(ev, true, true);
    put(pd, base + "code.bin", raw.code, files); put(pd, base + "code-lru.bin", lru.code, files);
    put(pd, base + "rlog.bin", raw.rlog, files); put(pd, base + "rlog-lru.bin", lru.rlog, files);
    put(pd, base + "wlog.bin", raw.wlog, files); put(pd, base + "wlog-lru.bin", lru.wlog, files);
    uint64_t bm[2] = {0, 0}; for (int t : used) bm[t >> 6] |= 1ull << (t & 63);
    const bool pc_ = lru.code_len < raw.code_len, pr = lru.rlog_len < raw.rlog_len, pw = lru.wlog_len < raw.wlog_len;
    const auto rc0 = size_of(raw.code, raw.code_vals, false), rr0 = size_of(raw.rlog, raw.rlog_vals, false), rw0 = size_of(raw.wlog, raw.wlog_vals, false);
    const auto rc1 = size_of(lru.code, lru.code_vals, true), rr1 = size_of(lru.rlog, lru.rlog_vals, true), rw1 = size_of(lru.wlog, lru.wlog_vals, true);
    put(pd, base + "state.bin", record(bm, pc, idx, off, rc0, rr0, rw0, regs), files);
    put(pd, base + "state-lru.bin", record(bm, pc, idx, off, rc1, rr1, rw1, regs), files);
    put(pd, base + "state-pick.bin", record(bm, pc, idx, off, pc_ ? rc1 : rc0, pr ? rr1 : rr0, pw ? rw1 : rw0, regs), files);
    const units::streamword::Size z; const uint64_t zbm[2] = {0, 0};
    put(pd, base + "end.bin", record(zbm, f.pc, f.idx, f.active ? f.next_row : 0, z, z, z, f.regs), files);
    const std::string rb = bytes_of(rows_out); write_file(pd / (base + "rows.bin"), rb);
    files.e.emplace_back(base + "rows.bin", "{\"bytes\": " + std::to_string(rb.size()) + ", \"sha256\": \"" + sha(rb) + "\"}");
    if (w9) for (auto& x : files.e) x.first = x.first.substr(base.size());
    std::string s = "{\"index\": " + std::to_string(win) + ", \"first_row\": " + std::to_string(int64_t(win) * rows) + ", \"rows\": " + std::to_string(rows);
    s += ", \"files\": {";
    for (size_t i = 0; i < files.e.size(); i++) s += (i ? ", \"" : "\"") + files.e[i].first + "\": " + files.e[i].second;
    s += "}";
    if (!w9) s += ", \"rows_sha256\": \"" + sha(rb) + "\"";
    s += ", \"templates\": [";
    { bool f1 = true; for (int t : used) { s += (f1 ? "" : ", ") + std::to_string(t); f1 = false; } }
    long long reads = 0, writes = 0, pad = 0;
    for (const Log& l : logs) { reads += l.has_read; writes += l.has_write; }
    for (size_t i = 0; i < rows_out.size() / 10; i++) if (rows_out[10 * i] == 0 && rows_out[10 * i + 1] == 0) pad++;
    s += "], \"counts\": {\"instructions\": " + std::to_string(code.size()) + ", \"read_log_words\": " + std::to_string(reads) +
         ", \"write_log_words\": " + std::to_string(writes) + ", \"padding_rows\": " + std::to_string(pad) + "}";
    s += ", \"next_offset\": " + std::to_string(off) + "}";
    return s;
}

void Gen::run() {
    tb = units::tracer::ReplayTables::load(tables_dir);
    gen = SplittableRandom(seed);
    rnd = SplittableRandom(int64_t(uint64_t(seed) ^ K_ADVICE));
    Func f; f.g = this; f.tb = &tb; Ref ref;
    for (int i = 1; i < 32; i++) f.regs[i] = gen.next_int(3) == 0 ? uint64_t(int64_t(gen.next_int(64) - 32)) : uint64_t(gen.next_long());
    f.regs[2] = BASE2; f.regs[3] = BASE3; f.regs[4] = BASE4; f.regs[34] = MTVEC; f.regs[39] = 0x1800;
    f.pc = 0x80000000ull; f.idx = 1;
    for (int i = 0; i < 32; i++) ref.x[i] = f.regs[i];
    ref.pc = f.pc;
    const fs::path pd = out / program; std::error_code ec; fs::create_directories(pd, ec);
    static const char* const tn[] = {"decode_table.bin", "rom.bin", "templates.bin", "template_index.bin"};
    if (w9) for (const char* n : tn) write_file(pd / n, read_bin(fs::path(tables_dir) / n));
    else { fs::create_directories(out / "tables", ec); for (const char* n : tn) write_file(out / "tables" / n, read_bin(fs::path(tables_dir) / n)); }
    const int total = first + windows;
    std::vector<std::string> entries;
    bool ended = false;
    const long long end_at = (long long)total * rows - rows / 2;
    for (int win = 0; win < total; win++) {
        const bool keep = win >= first;
        uint64_t start_regs[128]; for (int i = 0; i < 128; i++) start_regs[i] = f.regs[i];
        const uint64_t start_pc = f.pc; const int32_t start_idx = f.idx; const int start_off = f.active ? f.next_row : 0;
        std::vector<uint32_t> code; std::vector<Log> logs; std::vector<uint64_t> rows_out; rows_out.reserve(size_t(10) * size_t(rows));
        std::set<int> used;
        if (f.active) { code.push_back(f.w); if (f.tnum >= 0) used.insert(f.tnum); }
        for (int r = 0; r < rows; r++) {
            if (!f.active) {
                int32_t w;
                if (ended) w = 0;
                else if (end && row_count >= end_at) { w = J(0, 0); ended = true; }
                else w = next();
                code.push_back(uint32_t(w));
                const bool chk = (w & 3) != 0 && !ended && ref.exec(uint32_t(w));
                hist_w[hist_n & 7] = uint32_t(w); hist_pc[hist_n & 7] = f.pc; hist_n++;
                f.start(uint32_t(w), 0);
                if (f.tnum >= 0) used.insert(f.tnum);
                ref_check = chk;
            }
            uint64_t row[10];
            f.step(logs, row);
            row_count++;
            rows_out.insert(rows_out.end(), row, row + 10);
            if (!f.active) after_instr(f, ref);
        }
        if (keep) entries.push_back(write_window(pd, win, start_regs, start_pc, start_idx, start_off, code, logs, rows_out, used, f));
    }
    std::string s = "{\n \"format\": \"replay-inputs/v1.26 (synthetic: SyntheticDelivery, not jolt-cpp)\",\n";
    s += " \"generator\": {\"seed\": " + std::to_string(seed) + ", \"rows\": " + std::to_string(rows) + ", \"first\": " + std::to_string(first) + "},\n";
    s += " \"tables\": \"tables\",\n \"programs\": [{\"name\": \"" + program + "\", \"window_rows\": " + std::to_string(rows) + ", \"window_entries\": [\n";
    for (size_t i = 0; i < entries.size(); i++) s += "  " + entries[i] + (i + 1 < entries.size() ? ",\n" : "\n");
    s += " ]}]\n}\n";
    write_file(out / (w9 ? program + ".manifest.json" : "manifest.json"), s);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: synthetic_delivery OUT [--program NAME] [--windows N] [--rows R] [--first K] [--seed S] [--inline-weight W] [--no-end] [--tables DIR] [--w9-layout]\n"); return 2; }
    Gen g; g.out = argv[1];
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        auto val = [&]() -> const char* { if (i + 1 >= argc) die("missing value for " + a); return argv[++i]; };
        if (a == "--program") g.program = val();
        else if (a == "--windows") g.windows = atoi(val());
        else if (a == "--rows") g.rows = atoi(val());
        else if (a == "--first") g.first = atoi(val());
        else if (a == "--seed") g.seed = strtoll(val(), nullptr, 10);
        else if (a == "--inline-weight") g.inline_weight = atoi(val());
        else if (a == "--no-end") g.end = false;
        else if (a == "--tables") g.tables_dir = val();
        else if (a == "--w9-layout") g.w9 = true;
        else die("unknown option " + a);
    }
    const auto t0 = std::chrono::steady_clock::now();
    g.run();
    printf("SyntheticDelivery: %d windows of %d rows (from %d) in %s, %.1f s; %lld instructions checked against RV64\n", g.windows, g.rows, g.first,
           g.out.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), g.checked);
    return 0;
}
