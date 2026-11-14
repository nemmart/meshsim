// replayconf.h: the replayer's conformance with the RTL, cycle by cycle. A script gives the replayer's inputs, a line
// a cycle; the observations are what its ports show in each cycle. The same script on the RTL's `replayer_unit` (Snarky
// One; tests/replayer/README.md says how) gives the reference observations, which the fixtures in tests/replayer/
// hold: the model's must equal them character for character.
//
// A script: `# ...` comment lines, then a line for each cycle from 0, `<cycle>[ <op>]...`, at most one write and one
// access a cycle, the numbers hex but the cycle:
//   W<offset>,<lo>,<hi>   the write port: a 128-bit WRITE at the region's offset, presented in the cycle (17.8.9)
//   S                     a store to START with bit 0 set (17.2)
//   C<value>              a store to RUN_CONFIG (17.8.1)
//   K                     a consuming load of EVENTS, every event in EVENTS_MASK (17.2)
// The row port is always ready. The replayer starts from its reset, EVENTS_MASK then every event.
//
// The observations: `<cycle>[ <field>=<value>]...[ row=<hash>]`, a line for each cycle in which something changed: the
// fields as a load presented in the cycle reads them (the state at the end of the cycle before), each printed when it
// differs from the cycle before's (all in cycle 0):
//   busy  STATUS.busy          err  ERRORS          ev  EVENTS, as EVENTS_PEEK reads it
//   w32   RUN_CONFIG           w33  ROWS_RUN        w34 EXPANSION_OFFSET       w35  EXPANSION_OPCODE
// and `row=`, the row leaving the row port in the cycle: h = mix(h ^ word) over its ten 64-bit words, from 0.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "units/replayer.h"

namespace replayconf {

inline uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

constexpr const char* FIELD[] = {"busy", "err", "ev", "w32", "w33", "w34", "w35"};
constexpr unsigned N_FIELD = sizeof(FIELD) / sizeof(FIELD[0]);

inline std::string hex(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }

// Replay a script (its text) on r, fresh from its reset; returns the observations. max_rows bounds the row buffer.
inline std::string run(const std::string& script, uint64_t max_rows, units::Replayer* r) {
    using R = units::Replayer;
    std::vector<uint64_t> buf(max_rows * R::REC_LONGS);
    r->set_row_buffer(buf.data(), max_rows);
    std::string obs = "# replayconf observations v2\n";
    uint64_t last[N_FIELD] = {}, rows_seen = 0;
    bool first = true;
    int64_t expect_t = 0;
    size_t p = 0;
    while (p < script.size()) {
        size_t e = script.find('\n', p);
        if (e == std::string::npos) e = script.size();
        std::string line = script.substr(p, e - p);
        p = e + 1;
        if (line.empty() || line[0] == '#') continue;
        char* q;
        const int64_t t = std::strtoll(line.c_str(), &q, 10);
        if (t != expect_t++) FATAL("replayconf: cycle %lld out of order in line %s", (long long)t, line.c_str());
        // the observations of this cycle
        uint64_t v[N_FIELD];
        uint32_t x = 0;
        v[0] = r->running(t); v[1] = uint64_t(r->errors(t));
        r->sync(t);
        const uint32_t events = r->events().value(t);
        v[2] = events;
        for (unsigned w = 0; w < 4; w++) { r->word(t, int(R::W_RUN_CONFIG + w), x); v[3 + w] = x; }
        // its inputs
        bool consume = false;
        while (*q) {
            while (*q == ' ') q++;
            if (!*q) break;
            const char op = *q++;
            switch (op) {
                case 'W': {
                    const uint32_t offset = uint32_t(std::strtoul(q, &q, 16));
                    const uint64_t lo = std::strtoull(q + 1, &q, 16), hi = std::strtoull(q + 1, &q, 16);
                    r->write(t, int(r->me), offset, lo, hi);
                    break;
                }
                case 'S': r->run(t); break;
                case 'C':
                    if (r->write_word(t, R::W_RUN_CONFIG, uint32_t(std::strtoul(q, &q, 16))) != 1) FATAL("replayconf: RUN_CONFIG refused");
                    break;
                case 'K': consume = true; break;
                default: FATAL("replayconf: op '%c' in line %s", op, line.c_str());
            }
        }
        r->step(t);
        if (consume) r->consume_events(events);
        std::string b;
        for (unsigned i = 0; i < N_FIELD; i++)
            if (first || v[i] != last[i]) { b += ' '; b += FIELD[i]; b += '='; b += hex(v[i]); last[i] = v[i]; }
        for (; rows_seen < r->rows_sunk(); rows_seen++) {
            uint64_t h = 0;
            for (unsigned i = 0; i < R::REC_LONGS; i++) h = mix(h ^ buf[rows_seen * R::REC_LONGS + i]);
            b += " row=" + hex(h);
        }
        if (!b.empty()) obs += std::to_string(t) + b + "\n";
        first = false;
    }
    return obs + "# end " + std::to_string(expect_t) + " cycles, " + std::to_string(rows_seen) + " rows\n";
}

}  // namespace replayconf
