// W6: a synthetic busy 8 x 8 mesh on W1's structures, to measure the link word (two words against three), where a
// delivery entry's destination goes, runtime against compile-time widths, and what a ready mark costs.
//
// Per thread (its partition: a contiguous share of independent meshes), every cycle, as sim/README.md's core loop:
//   1. deliver: walk the credit bucket for t (credits back to their outputs), then the link bucket for t (words onto the
//      destination's input ring, the router marked ready);
//   2. inject: every tile, Bernoulli at the offered rate, into its local injection ring (refused and counted when full);
//   3. run: the ready routers (or, polling, all of them) step: XY routing, round-robin per output, one word per output and
//      at most one pop per input per cycle; a link send needs a credit and lands at t + 3; a pop returns its credit at t + 3;
//      ejection folds the word into the destination tile's digest (and in (a) reads and frees the 128-bit value).
//   (commit: a router with words left stays marked; buckets are cleared as they are walked.)
// Not exact to ref; deterministic: every variant gives the same digest for the same seed, rate, mix and depths.
#include <thread>
#include <type_traits>

#include "base/alloc_guard.h"
#include "base/arena.h"
#include "base/bitmap.h"
#include "base/bucket.h"
#include "base/ring.h"
#include "base/value_pool.h"
#include "common.h"
#include "fixed.h"

namespace {

using bench::copy_words;
using sim::u128;

enum { LW_A = 0, LW_B = 1 };
enum { E1 = 0, E2 = 1 };
enum { OUT_E = 0, OUT_W = 1, OUT_S = 2, OUT_N = 3, EJECT = 4 };
constexpr int kDelta[4] = {+1, -1, +8, -8};  // the neighbour through output o (and o ^ 1 is the way back)
constexpr unsigned kTiles = 64, kLatency = 3, kWheel = 4;

struct Cfg {
    unsigned depth = 8, injdepth = 16;
    double rate = 0.35;
    int mix = 0;  // 0: all 128-bit; 1: 32/64/128 in equal thirds
    uint64_t seed = 1;
    long cycles = 100000, warmup = 10000;
    bool phases = false, alloc_probe = false;
};

struct Result {
    double secs = 0;
    uint64_t sends = 0, ejects = 0, injected = 0, refused = 0, credits = 0, occ_sum = 0, occ_samples = 0;
    uint64_t ph[4] = {0, 0, 0, 0};  // TSC ticks: credits, links, inject, run
};

template <int LW, int DL, bool FX, bool POLL>
class Partition {
public:
    static constexpr unsigned W = LW == LW_A ? 2 : 3;
    static constexpr unsigned K = FX ? W : 0;  // the transports' width, if compile-time
    using RingT = std::conditional_t<FX, bench::FixedRing<W>, sim::Ring>;
    using LinkBucket = std::conditional_t<DL == E2, bench::SoaBucket<K>,
                                          std::conditional_t<FX, bench::FixedBucket<W + 1>, sim::Bucket>>;
    using CreditBucket = std::conditional_t<FX, bench::FixedBucket<1>, sim::Bucket>;
    static constexpr unsigned kEntryWidth = DL == E2 ? W : W + 1;

    Partition() = default;
    Partition(const Partition&) = delete;
    ~Partition() {
        for (void* q : {(void*)block_, (void*)rings_, (void*)credits_, (void*)rr_, (void*)rng_, (void*)dig_})
            if (q) sim::free_big(q);
    }

    // Configuration time: sizes from the declarations, one block carved, every structure on its own lines.
    void build(unsigned meshes, unsigned first_mesh, const Cfg& c) {
        cfg_ = c;
        nr_ = meshes * kTiles;
        const uint32_t pool_cap = nr_ * (4 * c.depth + c.injdepth);  // credits bound a link's ring + flight to depth
        const uint32_t bucket_cap = 4 * nr_;                          // one put per output per cycle
        size_t words = 0;
        for (unsigned r = 0; r < nr_; ++r)
            words += 4 * RingT::words_needed(W, c.depth) + RingT::words_needed(W, c.injdepth);
        words += kWheel * (LinkBucket::words_needed(kEntryWidth, bucket_cap) + CreditBucket::words_needed(1, bucket_cap));
        words += sim::Bitmap::words_needed(nr_);
        if (LW == LW_A) words += sim::ValuePool::words_needed(pool_cap);
        block_ = static_cast<uint64_t*>(sim::alloc_big(words * 8, "mesh partition"));
        uint64_t* p = block_;
        rings_ = static_cast<RingT*>(sim::alloc_big(sizeof(RingT) * nr_ * 5, "rings"));
        for (unsigned k = 0; k < nr_ * 5; ++k) new (&rings_[k]) RingT();
        for (unsigned r = 0; r < nr_; ++r)
            for (unsigned i = 0; i < 5; ++i) {
                const unsigned d = i == 4 ? c.injdepth : c.depth;
                rings_[r * 5 + i].init(p, W, d, i == 4 ? "inject" : "link in");
                p += RingT::words_needed(W, d);
            }
        for (unsigned k = 0; k < kWheel; ++k) {
            wheel_[k].init(p, kEntryWidth, bucket_cap, "link wheel");
            p += LinkBucket::words_needed(kEntryWidth, bucket_cap);
            cwheel_[k].init(p, 1, bucket_cap, "credit wheel");
            p += CreditBucket::words_needed(1, bucket_cap);
        }
        ready_.init(p, nr_, "ready");
        p += sim::Bitmap::words_needed(nr_);
        if (LW == LW_A) {
            pool_.init(p, pool_cap, "values");
            p += sim::ValuePool::words_needed(pool_cap);
        }
        if (size_t(p - block_) != words) FATAL("carving: %zu words used of %zu", size_t(p - block_), words);
        credits_ = static_cast<uint8_t*>(sim::alloc_big(nr_ * 4, "credits"));
        rr_ = static_cast<uint8_t*>(sim::alloc_big(nr_ * 5, "rr"));
        rng_ = static_cast<uint64_t*>(sim::alloc_big(nr_ * 8, "rng"));
        dig_ = static_cast<uint64_t*>(sim::alloc_big(nr_ * 8, "digests"));
        for (unsigned r = 0; r < nr_; ++r) {
            for (unsigned o = 0; o < 4; ++o) credits_[r * 4 + o] = uint8_t(c.depth);
            uint64_t s = c.seed * 0x100000001b3ull + (uint64_t(first_mesh + r / kTiles) << 6) + (r % kTiles);
            rng_[r] = bench::splitmix(s);
            dig_[r] = 0;
        }
        thresh_ = c.rate >= 1.0 ? ~uint64_t(0) : uint64_t(c.rate * 18446744073709551616.0);
    }

    // The cycle loop: no allocation, no STL, no virtual calls.
    void run(uint64_t from, uint64_t to, Result& res) {
        const bool ph = cfg_.phases;
        for (uint64_t t = from; t < to; ++t) {
            uint64_t a = ph ? __rdtsc() : 0, b = 0;
            deliver_credits(t);
            if (ph) b = __rdtsc(), res.ph[0] += b - a, a = b;
            deliver_links(t);
            if (ph) b = __rdtsc(), res.ph[1] += b - a, a = b;
            inject(t);
            if (ph) b = __rdtsc(), res.ph[2] += b - a, a = b;
            step_all(t);
            if (ph) b = __rdtsc(), res.ph[3] += b - a;
            if (__builtin_expect((t & 1023) == 0, 0)) sample();
            if (__builtin_expect(cfg_.alloc_probe && t == from + 5, 0)) {
                void* volatile q = std::malloc(16);  // the hook must abort here
                std::free(q);
            }
        }
    }

    void reset_counts() { sends_ = ejects_ = injected_ = refused_ = credits_sent_ = occ_sum_ = occ_n_ = 0; }
    void counts(Result& r) const {
        r.sends = sends_, r.ejects = ejects_, r.injected = injected_, r.refused = refused_, r.credits = credits_sent_;
        r.occ_sum = occ_sum_, r.occ_samples = occ_n_;
    }
    uint64_t mesh_digest(unsigned m) const {
        uint64_t h = 0;
        for (unsigned i = 0; i < kTiles; ++i) h = bench::fold(h, dig_[m * kTiles + i]);
        return h;
    }

private:
    __attribute__((noinline)) void deliver_credits(uint64_t t) {
        CreditBucket& b = cwheel_[t & (kWheel - 1)];
        const uint64_t* e = b.begin();
        const uint32_t n = b.count();
        for (uint32_t k = 0; k < n; ++k) ++credits_[e[k]];
        b.clear();
    }

    __attribute__((noinline)) void deliver_links(uint64_t t) {
        LinkBucket& b = wheel_[t & (kWheel - 1)];
        const uint64_t* e = b.begin();
        const uint32_t n = b.count();
        if constexpr (DL == E1) {
            const unsigned ew = b.width();
            for (uint32_t k = 0; k < n; ++k, e += ew) {
                const uint64_t port = e[0];
                rings_[(port >> 3) * 5 + (port & 7)].push(e + 1);
                if (!POLL) ready_.set(port >> 3);
            }
        } else {
            const unsigned ew = b.width();
            const uint32_t* d = b.dests();
            for (uint32_t k = 0; k < n; ++k, e += ew) {
                const uint32_t port = d[k];
                rings_[(port >> 3) * 5 + (port & 7)].push(e);
                if (!POLL) ready_.set(port >> 3);
            }
        }
        b.clear();
    }

    __attribute__((noinline)) void inject(uint64_t) {
        const uint64_t th = thresh_;
        const bool mixed = cfg_.mix != 0;
        for (uint32_t r = 0; r < nr_; ++r) {
            uint64_t& s = rng_[r];
            if (bench::splitmix(s) >= th) continue;
            const uint64_t y = bench::splitmix(s);
            const uint32_t tile = r & (kTiles - 1);
            uint32_t dst = uint32_t(((y & 0xffffffffu) * 63) >> 32);
            if (dst >= tile) ++dst;
            const uint32_t size = mixed ? uint32_t(((y >> 32) * 3) >> 32) : 2;
            uint64_t lo = bench::splitmix(s), hi = 0;
            if (size == 2) hi = bench::splitmix(s);
            if (size == 0) lo &= 0xffffffffu;
            RingT& q = rings_[r * 5 + 4];
            if (q.full()) {
                ++refused_;
                continue;
            }
            uint64_t* w = q.push();
            w[0] = uint64_t(tile) | uint64_t(dst) << 7 | uint64_t(size) << 20;
            if constexpr (LW == LW_A) {
                if (size == 2) {
                    const uint32_t idx = pool_.alloc();
                    pool_[idx] = u128(lo) | u128(hi) << 64;
                    w[1] = idx;
                } else {
                    w[1] = lo;
                }
            } else {
                w[1] = lo;
                w[2] = hi;
            }
            ++injected_;
            if (!POLL) ready_.set(r);
        }
    }

    __attribute__((noinline)) void step_all(uint64_t t) {
        if constexpr (POLL) {
            for (uint32_t r = 0; r < nr_; ++r) step(r, t);
        } else {
            ready_.for_each_set([&](size_t r) { step(uint32_t(r), t); });
        }
    }

    // XY routing, tabulated as ref's Router does: the decision is a pure function of (tile, dst).
    struct RouteTable {
        uint8_t out[kTiles * kTiles];
        constexpr RouteTable() : out() {
            for (uint32_t tile = 0; tile < kTiles; ++tile)
                for (uint32_t dst = 0; dst < kTiles; ++dst) {
                    const uint32_t x = tile & 7, y = tile >> 3, dx = dst & 7, dy = dst >> 3;
                    out[tile * kTiles + dst] = uint8_t(dx > x ? OUT_E : dx < x ? OUT_W : dy > y ? OUT_S : dy < y ? OUT_N : EJECT);
                }
        }
    };
    static constexpr RouteTable kRoute{};
    static unsigned route(uint32_t tile, uint64_t hdr) { return kRoute.out[tile * kTiles + ((hdr >> 7) & 63)]; }

    inline void step(uint32_t r, uint64_t t) {
        RingT* in = &rings_[r * 5];
        unsigned req[5] = {0, 0, 0, 0, 0};
        unsigned outs = 0;  // the outputs some input wants
        for (unsigned i = 0; i < 5; ++i)
            if (!in[i].empty()) {
                const unsigned o = route(r & (kTiles - 1), in[i].peek()[0]);
                req[o] |= 1u << i;
                outs |= 1u << o;
            }
        if (!outs) {
            if (!POLL) ready_.clear(r);
            return;
        }
        const uint64_t slot = (t + kLatency) & (kWheel - 1);
        for (; outs; outs &= outs - 1) {
            const unsigned o = unsigned(__builtin_ctz(outs));
            const unsigned m = req[o];
            if (o < 4 && credits_[r * 4 + o] == 0) continue;
            const unsigned hi = m & (~0u << rr_[r * 5 + o]);
            const unsigned i = unsigned(__builtin_ctz(hi ? hi : m));
            rr_[r * 5 + o] = uint8_t(i == 4 ? 0 : i + 1);
            const uint64_t* w = in[i].peek();
            if (o < 4) {
                --credits_[r * 4 + o];
                const uint32_t port = uint32_t(int(r) + kDelta[o]) << 3 | o;  // the neighbour's input o
                if constexpr (DL == E1) {
                    uint64_t* e = wheel_[slot].append();
                    e[0] = port;
                    copy_words<K>(e + 1, w, in[i].width());
                } else {
                    copy_words<K>(wheel_[slot].append(port), w, in[i].width());
                }
                ++sends_;
            } else {
                eject(r, w);
            }
            in[i].pop();
            if (i < 4) {  // input i came through the neighbour's output i: its credit goes back
                *cwheel_[slot].append() = uint64_t(uint32_t(int(r) + kDelta[i ^ 1]) * 4 + i);
                ++credits_sent_;
            }
        }
        if (!POLL) {
            unsigned left = 0;
            for (unsigned i = 0; i < 5; ++i) left |= in[i].count();
            if (!left) ready_.clear(r);
        }
    }

    inline void eject(uint32_t r, const uint64_t* w) {
        const uint64_t hdr = w[0];
        uint64_t lo, hi;
        if constexpr (LW == LW_A) {
            if (((hdr >> 20) & 3) == 2) {
                const u128 v = pool_[uint32_t(w[1])];
                lo = uint64_t(v), hi = uint64_t(v >> 64);
                pool_.free(uint32_t(w[1]));
            } else {
                lo = w[1], hi = 0;
            }
        } else {
            lo = w[1], hi = w[2];
        }
        dig_[r] = bench::fold(dig_[r], (hdr & 0x3fffff) ^ lo * 0x9e3779b97f4a7c15ull ^ hi * 0xc2b2ae3d27d4eb4full);
        ++ejects_;
    }

    void sample() {
        for (uint32_t r = 0; r < nr_; ++r) occ_sum_ += rings_[r * 5 + 4].count();
        occ_n_ += nr_;
    }

    Cfg cfg_;
    uint32_t nr_ = 0;
    uint64_t* block_ = nullptr;
    RingT* rings_ = nullptr;
    LinkBucket wheel_[kWheel];
    CreditBucket cwheel_[kWheel];
    sim::Bitmap ready_;
    sim::ValuePool pool_;
    uint8_t *credits_ = nullptr, *rr_ = nullptr;
    uint64_t *rng_ = nullptr, *dig_ = nullptr;
    uint64_t thresh_ = 0;
    uint64_t sends_ = 0, ejects_ = 0, injected_ = 0, refused_ = 0, credits_sent_ = 0, occ_sum_ = 0, occ_n_ = 0;
};

template <int LW, int DL, bool FX, bool POLL>
int run_variant(const Cfg& cfg, unsigned meshes, int nthreads, const int* cpus, const char* name) {
    using P = Partition<LW, DL, FX, POLL>;
    // thread i owns meshes [i * meshes / T, (i + 1) * meshes / T): uneven when T doesn't divide the count
    auto first_of = [&](int i) { return unsigned(uint64_t(i) * meshes / unsigned(nthreads)); };
    Result res[64];
    uint64_t digs[4096];
    if (nthreads > 64 || meshes > 4096) FATAL("too many threads or meshes");
    bench::StartLine line;
    auto body = [&](int tid) {
        bench::pin_to(cpus[tid]);
        const unsigned first = first_of(tid), per = first_of(tid + 1) - first;
        P part;
        part.build(per, first, cfg);
        line.arrive_and_wait();
        sim::begin_run();
        part.run(0, uint64_t(cfg.warmup), res[tid]);
        part.reset_counts();
        Result r = res[tid];
        const double t0 = bench::now_s();
        part.run(uint64_t(cfg.warmup), uint64_t(cfg.warmup + cfg.cycles), r);
        const double t1 = bench::now_s();
        sim::end_run();
        part.counts(r);
        r.secs = t1 - t0;
        for (int k = 0; k < 4; ++k) r.ph[k] -= res[tid].ph[k];
        res[tid] = r;
        for (unsigned m = 0; m < per; ++m) digs[first + m] = part.mesh_digest(m);
    };
    std::thread th[64];
    for (int i = 1; i < nthreads; ++i) th[i] = std::thread(body, i);
    std::thread main_body([&] { line.release_when(nthreads); });
    body(0);
    main_body.join();
    for (int i = 1; i < nthreads; ++i) th[i].join();

    Result tot;
    double maxs = 0, sums = 0;
    for (int i = 0; i < nthreads; ++i) {
        const Result& r = res[i];
        maxs = r.secs > maxs ? r.secs : maxs;
        sums += r.secs;
        tot.sends += r.sends, tot.ejects += r.ejects, tot.injected += r.injected, tot.refused += r.refused;
        tot.credits += r.credits, tot.occ_sum += r.occ_sum, tot.occ_samples += r.occ_samples;
        for (int k = 0; k < 4; ++k) tot.ph[k] += r.ph[k];
    }
    uint64_t dig = 0;
    for (unsigned m = 0; m < meshes; ++m) dig = bench::fold(dig, digs[m]);
    const double mc = double(meshes) * double(cfg.cycles);       // mesh-cycles in all
    const double tc = double(cfg.cycles) * meshes * kTiles;       // tile-cycles in all
    const double hops = double(tot.sends + tot.ejects);
    printf("mesh %s mix=%s rate=%.3f depth=%u meshes=%u threads=%d cycles=%ld secs=%.4f"
           " ns_per_mesh_cycle=%.2f ns_per_hop=%.3f thread_ns_per_mesh_cycle=%.2f mesh_cycles_per_s=%.4g"
           " hops_per_mesh_cycle=%.2f accepted=%.4f refused_frac=%.4f ejected=%.4f avg_injq=%.3f"
           " credits_per_mesh_cycle=%.2f digest=%016llx",
           name, cfg.mix ? "mixed" : "128", cfg.rate, cfg.depth, meshes, nthreads, cfg.cycles, maxs,
           1e9 * maxs * nthreads / mc, 1e9 * maxs * nthreads / hops, 1e9 * sums / mc, mc / maxs, hops / mc,
           double(tot.injected) / tc, double(tot.refused) / double(tot.injected + tot.refused + 1), double(tot.ejects) / tc,
           tot.occ_samples ? double(tot.occ_sum) / double(tot.occ_samples) : 0.0, double(tot.credits) / mc,
           (unsigned long long)dig);
    if (cfg.phases) {
        const double s = double(tot.ph[0] + tot.ph[1] + tot.ph[2] + tot.ph[3]);
        printf(" phase_credits=%.3f phase_links=%.3f phase_inject=%.3f phase_run=%.3f", tot.ph[0] / s, tot.ph[1] / s,
               tot.ph[2] / s, tot.ph[3] / s);
    }
    printf("\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bench::Args a{argc, argv};
    Cfg c;
    c.depth = unsigned(a.num("--depth", 8));
    c.injdepth = unsigned(a.num("--injdepth", 16));
    c.rate = a.real("--rate", 0.35);
    c.mix = !strcmp(a.get("--mix", "128"), "mixed");
    c.seed = uint64_t(a.num("--seed", 1));
    c.cycles = a.num("--cycles", 100000);
    c.warmup = a.num("--warmup", 10000);
    c.phases = a.flag("--phases");
    c.alloc_probe = a.flag("--alloc-probe");
    if (c.depth < 1 || c.depth > 255) FATAL("depth %u out of range", c.depth);
    const unsigned meshes = unsigned(a.num("--meshes", 1));
    int cpus[64];
    const int ncpu = bench::parse_cpus(a.get("--cpus", "0"), cpus, 64);
    const int nthreads = int(a.num("--threads", ncpu));
    if (nthreads > ncpu) FATAL("threads %d but %d cpus listed", nthreads, ncpu);
    if (a.flag("--nopin"))
        for (int i = 0; i < ncpu; ++i) cpus[i] = -1;

    const bool lwa = !strcmp(a.get("--lw", "a"), "a");
    const bool e2 = !strcmp(a.get("--dl", "e1"), "e2");
    const bool fx = !strcmp(a.get("--width", "rt"), "fx");
    const bool poll = !strcmp(a.get("--ready", "bm"), "poll");
    char name[96];
    snprintf(name, sizeof name, "lw=%s dl=%s width=%s ready=%s", lwa ? "a" : "b", e2 ? "e2" : "e1", fx ? "fx" : "rt",
             poll ? "poll" : "bm");
    const int key = (lwa ? 0 : 8) | (e2 ? 4 : 0) | (fx ? 2 : 0) | (poll ? 1 : 0);
#define V(k, LW, DL, FX, POLL) \
    case k: return run_variant<LW, DL, FX, POLL>(c, meshes, nthreads, cpus, name);
    switch (key) {
        V(0, LW_A, E1, false, false) V(1, LW_A, E1, false, true) V(2, LW_A, E1, true, false) V(3, LW_A, E1, true, true)
        V(4, LW_A, E2, false, false) V(5, LW_A, E2, false, true) V(6, LW_A, E2, true, false) V(7, LW_A, E2, true, true)
        V(8, LW_B, E1, false, false) V(9, LW_B, E1, false, true) V(10, LW_B, E1, true, false) V(11, LW_B, E1, true, true)
        V(12, LW_B, E2, false, false) V(13, LW_B, E2, false, true) V(14, LW_B, E2, true, false) V(15, LW_B, E2, true, true)
    }
#undef V
    return 1;
}
