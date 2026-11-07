// Sketch 3 (WORKER3, stage 1): mixed fidelity (config.h, MIXED FIDELITY), compiled, not run. A tile is built by a function
// that adds its units and returns its external port handles; a high-level tile is one unit with the same external ports,
// emitting each result at a calibrated time with an ordinary wake and put. The units are stand-ins (not `ref`'s): what is
// shown is the configuration and the timing mechanism.
#include <cstdint>
#include <string>

#include "base/fatal.h"
#include "config.h"
#include "sim.h"

namespace sketch {
using namespace sim;

enum Dir : unsigned { N, E, S, W, NDIR };

// The tile's external ports: what its neighbours and the chip wire to. Both builders return this.
struct TileIO {
    LinkIn in[NDIR];      // link words from the neighbours (three words, D17)
    LinkOut out[NDIR];
};

// ---- the cycle-level tile: a router and a consumer behind it (stand-ins for the tile's dozens of units) ----
struct Router {
    LinkIn in[NDIR]; LinkOut out[NDIR]; FifoOut eject;
    explicit Router(Ports& p) {
        for (unsigned d = 0; d < NDIR; d++) { p.in(in[d], "in", 3); p.out(out[d], "out", 3); }
        p.out(eject, "eject", 3);
    }
    Wake step(Ctx& c);                                   // route each ready input's head to an output or the ejection
};
struct Engine0 {
    FifoIn in; LinkOut reply;
    Engine0(Ports& p, unsigned depth) { p.in(in, "in", 3, depth); p.out(reply, "reply", 3); }
    Wake step(Ctx& c);                                   // consume a packet, compute, reply through the router (elided)
};

TileIO tile_cycle(Config& cfg, unsigned k) {
    std::string t = "t" + std::to_string(k) + ".";
    auto& r = cfg.add<Router>(t + "router");
    auto& e = cfg.add<Engine0>(t + "engine", 16u);
    cfg.connect(r.eject, e.in);
    TileIO io;
    for (unsigned d = 0; d < NDIR; d++) { io.in[d] = r.in[d]; io.out[d] = r.out[d]; }
    return io;
}

// ---- the high-level tile: one unit, the same external ports ----
// On a packet it computes the reply at once and emits it `latency` cycles later, the latency calibrated from cycle-level
// runs (SIMULATOR-NEXT 1: when data pops out of the tile, as a function of what it was given). Replies in flight wait in a
// small queue in its own state, sized at configuration; its wake is the earliest one due.
struct TileModel {
    LinkIn in[NDIR]; LinkOut out[NDIR];
    struct Pending { uint64_t due; unsigned dir; Word w[3]; };
    static constexpr unsigned kMax = 64;
    Pending q[kMax]; unsigned head = 0, count = 0;       // in due order (latency is per packet kind: a FIFO by due still works
    uint64_t latency;                                    //  if every kind's latency is the same; else a small heap)

    TileModel(Ports& p, uint64_t calibrated_latency) : latency(calibrated_latency) {
        for (unsigned d = 0; d < NDIR; d++) { p.in(in[d], "in", 3); p.out(out[d], "out", 3); }
    }
    Wake step(Ctx& c) {
        uint64_t t = c.now();
        for (unsigned d = 0; d < NDIR; d++) if (c.ready(in[d])) {           // take every arrival (a Link's consumer must)
            const Word* w = c.peek(in[d]);
            if (count == kMax) FATAL("tile model: more than %u replies in flight", kMax);
            Pending& p = q[(head + count++) % kMax];
            p.due = t + latency; p.dir = reply_dir(w); compute(w, p.w);       // the result now, its time later
            c.take(in[d]);
        }
        unsigned sent[NDIR] = {};
        while (count && q[head].due <= t && !sent[q[head].dir]) {           // one put per output per cycle
            Pending& p = q[head]; sent[p.dir] = 1;
            c.put(out[p.dir], p.w[0], p.w[1], p.w[2]);                       // delivered at due + L, like any put
            head = (head + 1) % kMax; count--;
        }
        if (!count) return Wake::none();                                    // the next arrival wakes it
        return Wake::at_cycle(q[head].due > t ? q[head].due : t + 1);       // the calibrated time, or retry a busy output
    }
    unsigned reply_dir(const Word* w) const { return unsigned(w[1] & 3); }   // illustrative
    void compute(const Word* in_words, Word* out_words) const {               // the tile's function, result-only
        out_words[0] = in_words[0]; out_words[1] = in_words[1]; out_words[2] = in_words[2];
    }
};

TileIO tile_high(Config& cfg, unsigned k, uint64_t calibrated_latency) {
    auto& m = cfg.add<TileModel>("t" + std::to_string(k) + ".model", calibrated_latency);
    TileIO io;
    for (unsigned d = 0; d < NDIR; d++) { io.in[d] = m.in[d]; io.out[d] = m.out[d]; }
    return io;
}

// ---- a chip: each tile cycle-level or high-level, per instance; the wiring can't tell them apart ----
unsigned neighbour(unsigned k, unsigned d) {
    unsigned x = k % 8, y = k / 8;
    if (d == N) y = (y + 7) % 8; else if (d == S) y = (y + 1) % 8; else if (d == E) x = (x + 1) % 8; else x = (x + 7) % 8;
    return y * 8 + x;
}

void build_chip(Config& cfg, const bool* high_level, uint64_t calibrated_latency) {
    TileIO io[64];
    for (unsigned k = 0; k < 64; k++) {
        cfg.chip(0);
        io[k] = high_level[k] ? tile_high(cfg, k, calibrated_latency) : tile_cycle(cfg, k);
    }
    for (unsigned k = 0; k < 64; k++)
        for (unsigned d = 0; d < NDIR; d++) cfg.link(io[k].out[d], io[neighbour(k, d)].in[(d + 2) % NDIR], 3);
}

static_assert(UnitType<Router> && UnitType<Engine0> && UnitType<TileModel>);

}  // namespace sketch
