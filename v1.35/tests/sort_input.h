// sort_input.h: the sort's input and its expected result, ported from SortTest.java (readKeys, value32,
// valueHalf, loadInput, Oracle, mismatches) so the C++ driver checks the sort as M2Sort does.
//
//   read_keys(path)        KeyGen's file: "KEYS" (0x4B455953), 64 lengths, then each tile's keys (little-endian)
//   value32, value_half    the values of input record (tile p, position j): 32 bits (kv32), 2 x 64 (k128)
//   load_input(...)        SortTest.loadInput between cycles: big SRAM (BUF_IN, BUF_VIN) and S.n_in in data memory
//   Oracle                 the final contents of each owner's BUF_Y (key | hi << 32) and each record's source
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sortin {

enum : int { FMT_KV32 = 0, FMT_K128 = 1 };
enum : uint32_t { CMD_L1 = 0x10, CMD_XB = 0x11, CMD_RED = 0x12, CMD_K = 0x13, CMD_SPLIT = 0x14, CMD_L2 = 0x15, CMD_V = 0x16,
                  CMD_XC = 0x20, CMD_TH = 0x21, CMD_SWEEP = 0x22, CMD_DESC = 0x23, CMD_K1 = 0x24, CMD_K2 = 0x25, CMD_V1 = 0x26,
                  CMD_V2 = 0x27, CMD_BARRIER = 0x30, CMD_EXIT = 0x7F };
constexpr uint32_t AREA = 0x20000, BUF_IN = 0, BUF_X = 3 * AREA, BUF_Y = 4 * AREA, BUF_VIN = AREA, BUF_VS = 5 * AREA;
enum : unsigned { S_NIN = 1, S_NRECV = 2, S_PATH = 4, S_PLAN = 5, S_BT = 8 };

using Keys = std::vector<std::vector<uint32_t>>;   // 64 tiles

// KeyGen's file; an empty result and `err` set on failure.
inline Keys read_keys(const std::string& path, std::string& err) {
    Keys k;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = path + ": cannot open"; return k; }
    auto rd = [&](uint32_t& v) { uint8_t b[4]; if (std::fread(b, 1, 4, f) != 4) return false; v = b[0] | b[1] << 8 | b[2] << 16 | uint32_t(b[3]) << 24; return true; };
    uint32_t magic = 0;
    if (!rd(magic) || magic != 0x4B455953) { err = path + ": not a key file"; std::fclose(f); return k; }
    k.resize(64);
    for (auto& t : k) { uint32_t n = 0; if (!rd(n)) { err = path + ": short"; std::fclose(f); return {}; } t.resize(n); }
    for (auto& t : k) for (auto& x : t) if (!rd(x)) { err = path + ": short"; std::fclose(f); return {}; }
    std::fclose(f);
    return k;
}

inline uint64_t value32(uint32_t p, uint32_t j) { return uint64_t(uint32_t(p * 0x9E3779B1u + j * 0x85EBCA77u)); }
inline uint64_t value_half(uint32_t p, uint32_t j, uint32_t h) {
    uint64_t z = uint64_t(p) << 40 ^ uint64_t(j) << 8 ^ uint64_t(h) * 0x9E3779B97F4A7C15ull;
    z = (z ^ z >> 31) * 0xBF58476D1CE4E5B9ull;
    return z ^ z >> 29;
}

// SortTest.loadInput, through the two callbacks (a tile's big-SRAM 32-bit poke; its data-memory word poke by byte address).
template <class SramPoke, class DmemPoke>
void load_input(const Keys& k, int fmt, uint32_t s_addr, SramPoke sram_poke, DmemPoke dmem_poke) {
    for (uint32_t i = 0; i < 64; i++) {
        for (uint32_t j = 0; j < k[i].size(); j++) {
            if (fmt == FMT_K128) {
                sram_poke(i, BUF_IN + j, k[i][j]);
                const uint64_t v0 = value_half(i, j, 0), v1 = value_half(i, j, 1);
                sram_poke(i, BUF_VIN + 4 * j, uint32_t(v0)); sram_poke(i, BUF_VIN + 4 * j + 1, uint32_t(v0 >> 32));
                sram_poke(i, BUF_VIN + 4 * j + 2, uint32_t(v1)); sram_poke(i, BUF_VIN + 4 * j + 3, uint32_t(v1 >> 32));
            } else {
                const uint64_t w = uint64_t(k[i][j]) | value32(i, j) << 32;
                sram_poke(i, BUF_IN + 2 * j, uint32_t(w)); sram_poke(i, BUF_IN + 2 * j + 1, uint32_t(w >> 32));
            }
        }
        dmem_poke(i, s_addr + 4 * S_NIN, uint32_t(k[i].size()));
    }
}

// SortTest.G4: the direct path's schedule-4 groups (each group's four offsets q - p).
inline constexpr unsigned G4[16][4] = {{1, 29, 35, 63}, {2, 30, 34, 62}, {3, 31, 33, 61}, {5, 23, 41, 59}, {6, 22, 42, 58},
    {7, 21, 43, 57}, {8, 28, 36, 56}, {9, 27, 37, 55}, {10, 26, 38, 54}, {11, 25, 39, 53}, {12, 24, 40, 52},
    {13, 15, 49, 51}, {14, 18, 46, 50}, {16, 20, 44, 48}, {17, 19, 45, 47}, {0, 4, 32, 60}};
// SortTest.Oracle (shaped = false: M2Sort's; `shaped` added, the targets R_q = the input's own sizes, and R, the
// direct path's group maxima).
struct Oracle {
    std::vector<uint64_t> expect[64], source[64];
    uint64_t N = 0;
    uint32_t R[16] = {};
    Oracle(const Keys& k, int fmt, bool shaped = false) {
        for (auto& t : k) N += t.size();
        std::vector<uint64_t> g; g.reserve(N);
        for (uint32_t p = 0; p < 64; p++)
            for (uint32_t j = 0; j < k[p].size(); j++) g.push_back(uint64_t(k[p][j]) << 32 | uint64_t(p) << 26 | j);
        std::sort(g.begin(), g.end());                     // (key unsigned, tile, position): Java's key ^ 0x80000000 order
        uint64_t Rq[65];
        for (unsigned q = 0; q <= 64; q++) Rq[q] = shaped ? (q == 0 ? 0 : Rq[q - 1] + k[q - 1].size()) : q * N / 64;
        static uint32_t cnt[64][64], c[64][65], prefix[64][64];
        for (auto& r : cnt) for (auto& x : r) x = 0;
        unsigned q = 0;
        for (uint64_t r = 0; r < N; r++) { while (r >= Rq[q + 1]) q++; cnt[g[r] >> 26 & 63][q]++; }
        for (unsigned p = 0; p < 64; p++) { c[p][0] = 0; for (unsigned qq = 0; qq < 64; qq++) c[p][qq + 1] = c[p][qq] + cnt[p][qq]; }
        for (unsigned qq = 0; qq < 64; qq++) { prefix[qq][0] = 0; for (unsigned p = 1; p < 64; p++) prefix[qq][p] = prefix[qq][p - 1] + cnt[p - 1][qq]; }
        uint32_t pos[64] = {};
        for (unsigned qq = 0; qq < 64; qq++) { expect[qq].resize(Rq[qq + 1] - Rq[qq]); source[qq].resize(expect[qq].size()); }
        q = 0; uint64_t r0 = 0;
        for (uint64_t r = 0; r < N; r++) {
            while (r >= Rq[q + 1]) { q++; r0 = r; }
            const uint32_t p = uint32_t(g[r] >> 26 & 63), j = uint32_t(g[r] & 0x3FFFFFF), s = pos[p]++;
            const uint64_t key = g[r] >> 32;
            const uint64_t hi = fmt == FMT_KV32 ? value32(p, j) : uint64_t(uint32_t(prefix[q][p] + s - c[p][q]));
            expect[q][r - r0] = key | hi << 32;
            source[q][r - r0] = uint64_t(p) << 32 | j;
        }
        for (unsigned gi = 0; gi < 16; gi++)
            for (unsigned p = 0; p < 64; p++)
                for (unsigned i = 0; i < 4; i++) R[gi] = std::max(R[gi], cnt[p][(p + G4[gi][i]) & 63]);
    }
};

}  // namespace sortin
