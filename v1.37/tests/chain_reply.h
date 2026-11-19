// chain_reply.h: the reply frame on the chain of processes (v1.37 stage 6, the INTEGRATOR): how the driver checks
// an ASIC's reply against gold's files, and how a stand-in ASIC makes one from them (29.14, 30.7; the reply's layout
// as the owner ruled it on 2026-11-19, DESIGN-NOTES section 39: 72 data words, 73 on the link). The comparable words
// are those gold's files give: the header word, s and N, PAGE_COUNT 0 (the frames' counts are exact, 29.7), the error
// words 0; word 4, the tracer tile's: the rows traced (the segment's windows times its rows, a run traces exactly
// RUN_CONFIG.rows, 17.9.3), the Tracer's ERRORS 0 and the status 1; and the state of the record at the segment's end,
// words 0 and 2-65 at the reply's 5 on, record s + 1 of frame 7 (29.11), for every segment but the last. mcycle, the
// Updater's statistics, the record's words 1, 66 and 67 are the chip's own: the simulator's harness checks them
// (proof_bench.cpp, check_reply), the driver only that they are there.
//
// Test-side code: the STL throughout.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "frames.h"
#include "hostlink.h"

namespace chain {

constexpr unsigned REPLY_WORDS = 73, REPLY_TYPE = 16, REPLY_RECORD = 5;

inline uint32_t w32(const ::hostlink::Word& w, unsigned k) { uint32_t v; std::memcpy(&v, w.byte + 4 * k, 4); return v; }
inline void put32(::hostlink::Word& w, unsigned k, uint32_t v) { std::memcpy(w.byte + 4 * k, &v, 4); }
inline void put128(::hostlink::Word& w, uint64_t lo, uint64_t hi) { std::memcpy(w.byte, &lo, 8); std::memcpy(w.byte + 8, &hi, 8); }

// Record r of frame 7, word w, as 16 bytes; false if the file has no such record.
inline bool record_word(const frames::File& f, unsigned r, unsigned w, ::hostlink::Word& out) {
    for (const frames::Frame& fr : f.frame)
        if (fr.type == frames::REGISTERS) {
            const size_t k = size_t(frames::RECORD_WORDS) * r + w;
            if (k >= fr.length) return false;
            put128(out, f.lo(fr, k), f.hi(fr, k));
            return true;
        }
    return false;
}

// A stand-in's reply for segment s of N, `rows` traced, made at cycle `t`: what the chip's would be in the comparable
// words, its own words 0.
inline std::vector<::hostlink::Word> make_reply(const frames::File& f, unsigned s, unsigned N, uint32_t rows, uint64_t t) {
    std::vector<::hostlink::Word> r(REPLY_WORDS);
    for (auto& w : r) std::memset(w.byte, 0, sizeof w.byte);
    put32(r[0], 2, REPLY_TYPE); put32(r[0], 3, REPLY_WORDS - 1);
    put32(r[1], 0, s); put32(r[1], 1, N); put32(r[1], 2, 0); put32(r[1], 3, 0);
    put32(r[2], 0, uint32_t(t)); put32(r[2], 1, uint32_t(t >> 32));
    put32(r[4], 0, rows); put32(r[4], 1, 0); put32(r[4], 2, 1);
    for (unsigned w = 0; w < frames::RECORD_WORDS && s + 1 < N; w++) record_word(f, s + 1, w, r[REPLY_RECORD + w]);
    return r;
}

// The driver's check of one ASIC's reply, its words and the link packets they came in (the word counts at which one
// ended). Each difference printed with `who`; returns how many.
inline int check_reply(const std::string& who, const frames::File& f, unsigned s, unsigned N, uint32_t rows,
                       const std::vector<::hostlink::Word>& r, const std::vector<size_t>& ends) {
    int bad = 0;
    auto fail = [&](const char* what) { std::printf("FAIL %s: %s\n", who.c_str(), what); bad++; };
    if (r.size() != REPLY_WORDS) { char m[96]; std::snprintf(m, sizeof m, "%zu words, not %u", r.size(), REPLY_WORDS); fail(m); return bad; }
    if (ends != std::vector<size_t>{32, 64, 73}) fail("its link packets do not end after 32, 64 and 73 words (17.6.8)");
    if (w32(r[0], 0) != 0 || w32(r[0], 1) != 0 || w32(r[0], 2) != REPLY_TYPE || w32(r[0], 3) != REPLY_WORDS - 1) fail("its header word is not type 16, length 72");
    if (w32(r[1], 0) != s || w32(r[1], 1) != N) {
        char m[96]; std::snprintf(m, sizeof m, "s %u and N %u; %u and %u expected", w32(r[1], 0), w32(r[1], 1), s, N); fail(m);
    }
    if (w32(r[1], 2) != 0) fail("word 1 [95:64] not 0");
    if (w32(r[1], 3) != 0) fail("PAGE_COUNT not 0");
    for (unsigned k = 0; k < 4; k++) if (w32(r[3], k) != 0) { fail("an error word not 0"); break; }
    if (w32(r[4], 0) != rows) { char m[96]; std::snprintf(m, sizeof m, "%u rows traced; %u expected", w32(r[4], 0), rows); fail(m); }
    if (w32(r[4], 1) != 0 || w32(r[4], 2) != 1 || w32(r[4], 3) != 0) fail("word 4: the Tracer's ERRORS not 0, or the tracer tile's status not 1");
    if (s + 1 < N)
        for (unsigned w = 0; w < 66; w++) {
            if (w == 1) continue;                                     // the run's totals, the chip's own
            ::hostlink::Word want;
            if (!record_word(f, s + 1, w, want)) { fail("the frame file has no record s + 1"); break; }
            if (std::memcmp(want.byte, r[REPLY_RECORD + w].byte, 16) != 0) {
                char m[96]; std::snprintf(m, sizeof m, "the record's word %u is not record s + 1's", w); fail(m); break;
            }
        }
    return bad;
}

}  // namespace chain
