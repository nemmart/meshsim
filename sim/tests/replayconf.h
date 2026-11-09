// replayconf.h: the replayer's conformance, the C++ side (WORKER15): a ReplayConf script (tests/ref/ReplayConf.java)
// replayed on units::Replayer, with the observations ReplayConf.observe writes, character for character.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "statefp.h"
#include "units/replayer.h"

namespace replayconf {

inline uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

constexpr const char* FIELD[] = {"run", "err", "ev", "w32", "w33", "w34", "w35", "outq", "done", "land",
                                 "stI", "stC", "stN", "stE", "stR", "stW", "stO",
                                 "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "inv", "ovr", "und", "lft", "pc", "idx", "fp"};
constexpr unsigned N_FIELD = sizeof(FIELD) / sizeof(FIELD[0]);

struct Rec {
    units::Replayer* r;
    std::string obs, y;
    uint64_t last[N_FIELD] = {};
    bool first = true;
    uint64_t rows_seen = 0;
    std::vector<uint64_t> buf;

    explicit Rec(units::Replayer* rp, uint64_t max_rows) : r(rp), buf(max_rows * units::Replayer::REC_LONGS) {
        r->set_row_buffer(buf.data(), max_rows);
    }
    static std::string hex(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }

    void read(int64_t t) {
        r->sync(t);
        uint32_t l = r->events().value(t), w34 = 0, w35 = 0;
        if (l != 0) { r->word(t, units::Replayer::W_EXP_OFFSET, w34); r->word(t, units::Replayer::W_EXP_OPCODE, w35); }
        if (l != 0) y += " y=" + hex(l) + "," + hex(w34) + "," + hex(w35);
    }
    void step(int64_t t) {
        r->step(t);
        fp::FpOut f;                                 // the canonical fingerprint (AUDIT-M3 6.8) at the end of t
        r->fingerprint(f, t);
        const uint64_t fpv = f.done();
        int64_t u = t + 1;
        uint64_t v[N_FIELD];
        unsigned k = 0;
        uint32_t x;
        v[k++] = r->running(u);
        v[k++] = r->errors(u);
        r->sync(u);
        v[k++] = r->events().value(u);
        for (unsigned w = 32; w <= 35; w++) { r->word(u, w, x); v[k++] = x; }
        v[k++] = r->out_queue_rows(); v[k++] = r->rows_done(); v[k++] = r->landed(t);
        for (unsigned s = 0; s < units::Replayer::N_STALL; s++) v[k++] = r->stalls[s];
        for (unsigned c = 1; c < units::Replayer::N_VIOLATION; c++) v[k++] = r->fact_count(uint16_t(c));
        v[k++] = r->invalids(); v[k++] = r->overruns(); v[k++] = r->underflows(); v[k++] = r->leftovers();
        v[k++] = r->pc(); v[k++] = r->instr_idx();
        v[k++] = fpv;
        std::string b;
        for (unsigned i = 0; i < k; i++)
            if (first || v[i] != last[i]) { b += ' '; b += FIELD[i]; b += '='; b += hex(v[i]); last[i] = v[i]; }
        b += y;
        for (; rows_seen < r->rows_sunk(); rows_seen++) {
            uint64_t h = 0;
            for (unsigned i = 0; i < units::Replayer::REC_LONGS; i++) h = mix(h ^ buf[rows_seen * units::Replayer::REC_LONGS + i]);
            b += " row=" + hex(h);
        }
        if (!b.empty()) obs += std::to_string(t) + b + "\n";
        first = false;
        y.clear();
    }
};

// Replay a script (its text); returns the observations as ReplayConf run prints them. max_rows bounds the row buffer.
inline std::string run(const std::string& script, uint64_t max_rows, units::Replayer* r) {
    Rec rc(r, max_rows);
    size_t p = 0;
    while (p < script.size()) {
        size_t e = script.find('\n', p);
        if (e == std::string::npos) e = script.size();
        std::string line = script.substr(p, e - p);
        p = e + 1;
        if (line.empty() || line[0] == '#') continue;
        const char* s = line.c_str();
        char* q;
        int64_t t = std::strtoll(s, &q, 10);
        while (*q) {
            while (*q == ' ') q++;
            if (!*q) break;
            char op = *q++;
            switch (op) {
                case 'A': {
                    uint32_t addr = uint32_t(std::strtoul(q + 1, &q, 16));
                    uint64_t lo = std::strtoull(q + 1, &q, 16), hi = std::strtoull(q + 1, &q, 16);
                    r->accept(t, 0, addr << 2, 2, lo, hi);
                    break;
                }
                case 'R': r->run(t); break;
                case 'C': {
                    uint32_t v = uint32_t(std::strtoul(q + 1, &q, 16));
                    if (r->write_word(t, units::Replayer::W_RUN_CONFIG, v) != 1) FATAL("replayconf: RUN_CONFIG refused");
                    break;
                }
                case 'Y': rc.read(t); break;
                case 'K': r->consume_events(uint32_t(std::strtoul(q + 1, &q, 16))); break;
                case 'S': rc.step(t); break;
                default: FATAL("replayconf: op '%c' in line %s", op, line.c_str());
            }
        }
    }
    char tail[160];
    std::snprintf(tail, sizeof tail, "# end rows %llu stalls [", (unsigned long long)r->rows_total);
    std::string out = "# replayconf observations v1\n" + rc.obs + tail;
    for (unsigned s = 0; s < units::Replayer::N_STALL; s++) out += (s ? ", " : "") + std::to_string(r->stalls[s]);
    return out + "]\n";
}

}  // namespace replayconf
