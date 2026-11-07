// W6: the cost of Fp128 arithmetic (p = 2^128 - 159, reduced by folding; D14). Forms: fp128.h.
//
//   fp128 --check N                       both forms against the slow reference: edge cases, then N random pairs
//   fp128 --dump N                        "a b a*b mod p" in hex (edge cases, then N random), for check_fp128.py
//   fp128 --micro --form u128|acc|limbs --n N latency (one dependent chain) and throughput (8 independent chains)
//   fp128 --unit --form u128|acc|limbs|none --hold pool|inline --units K [--threads T --cpus L --cycles N --warmup N]
//       K toy multiplier units (split over T threads), each with an input Ring of operand pairs: a unit with input pops
//       a pair (x, y), multiplies, and puts the result paired with y back to its own input through the delivery wheel
//       (latency 1); two pairs per unit, so every unit multiplies every cycle. --hold pool: the pair is two ValuePool
//       indices (the result takes x's freed index); --hold inline: the pair is the two values, 4 words. --form none:
//       an addition instead of the multiply (the data movement alone).
#include <thread>

#include "base/alloc_guard.h"
#include "base/arena.h"
#include "base/bitmap.h"
#include "base/bucket.h"
#include "base/ring.h"
#include "base/value_pool.h"
#include "common.h"
#include "fp128.h"

namespace {

using fp::u128;
using fp::u64;

enum Form { F_U128 = 0, F_LIMBS = 1, F_NONE = 2, F_ACC = 3 };

u128 rand128(uint64_t& s) {
    const u64 hi = bench::splitmix(s);
    return fp::make(hi, bench::splitmix(s));
}

int edge_cases(u128* v) {
    int n = 0;
    const u128 p = fp::kP, two64 = u128(1) << 64;
    const u128 list[] = {0,
                         1,
                         2,
                         158,
                         159,
                         160,
                         p - 2,
                         p - 1,
                         p,
                         p + 1,
                         p + 158,
                         ~u128(0),
                         ~u128(0) - 1,
                         two64 - 1,
                         two64,
                         two64 + 1,
                         u128(1) << 127,
                         (u128(1) << 127) - 1,
                         p >> 1,
                         (p >> 1) + 1,
                         fp::make(~0ull, 0),
                         fp::make(0, ~0ull),
                         fp::make(1, 0),
                         fp::make(~0ull - 1, ~0ull - 158)};
    for (u128 x : list) v[n++] = x;
    return n;
}

void hex(char* b, u128 x) { snprintf(b, 40, "%016llx%016llx", (unsigned long long)(x >> 64), (unsigned long long)x); }

int check(long n) {
    u128 e[64];
    const int ne = edge_cases(e);
    long bad = 0, tested = 0;
    auto one = [&](u128 a, u128 b) {
        const u128 r = fp::mul_ref(a, b), x = fp::mul_u128(a, b), y = fp::mul_limbs(a, b), z = fp::mul_acc(a, b);
        ++tested;
        if (x != r || y != r || z != r) {
            char ha[40], hb[40], hr[40], hx[40], hy[40], hz[40];
            hex(ha, a), hex(hb, b), hex(hr, r), hex(hx, x), hex(hy, y), hex(hz, z);
            if (++bad <= 10)
                fprintf(stderr, "MISMATCH a=%s b=%s ref=%s u128=%s limbs=%s acc=%s\n", ha, hb, hr, hx, hy, hz);
        }
    };
    for (int i = 0; i < ne; ++i)
        for (int j = 0; j < ne; ++j) one(e[i], e[j]);
    uint64_t s = 12345;
    for (long k = 0; k < n; ++k) {
        u128 a = rand128(s), b = rand128(s);
        const uint64_t sel = bench::splitmix(s);
        if ((sel & 7) == 0) a = fp::kP - 1 - (a & 0xffff);  // near p
        if ((sel & 56) == 8) b = ~u128(0) - (b & 0xffff);   // near 2^128 (unreduced)
        if ((sel & 448) == 64) a &= ~u128(0) >> 64;         // small
        one(a, b);
    }
    printf("fp128 check: %ld pairs (%d edge cases squared, %ld random), %ld mismatches\n", tested, ne, n, bad);
    return bad ? 1 : 0;
}

int dump(long n) {
    u128 e[64];
    const int ne = edge_cases(e);
    char ha[40], hb[40], hr[40];
    auto one = [&](u128 a, u128 b) {
        hex(ha, a), hex(hb, b), hex(hr, fp::mul_u128(a, b));
        printf("%s %s %s\n", ha, hb, hr);
        hex(hr, fp::mul_limbs(a, b));
        printf("%s %s %s\n", ha, hb, hr);
        hex(hr, fp::mul_acc(a, b));
        printf("%s %s %s\n", ha, hb, hr);
    };
    for (int i = 0; i < ne; ++i)
        for (int j = 0; j < ne; ++j) one(e[i], e[j]);
    uint64_t s = 777;
    for (long k = 0; k < n; ++k) {
        const u128 a = rand128(s), b = rand128(s);
        one(a, b);
    }
    return 0;
}

template <int F>
inline u128 op(u128 a, u128 b) {
    if constexpr (F == F_U128) return fp::mul_u128(a, b);
    else if constexpr (F == F_LIMBS) return fp::mul_limbs(a, b);
    else if constexpr (F == F_ACC) return fp::mul_acc(a, b);
    else return a + b;
}

template <int F>
int micro(long n, const char* name) {
    uint64_t s = 99;
    u128 y = fp::reduce_once(rand128(s)), x = fp::reduce_once(rand128(s));
    u128 xs[8], ys[8];
    for (int k = 0; k < 8; ++k) xs[k] = fp::reduce_once(rand128(s)), ys[k] = fp::reduce_once(rand128(s));
    sim::begin_run();
    for (long i = 0; i < n / 10; ++i) x = op<F>(x, y);  // warm-up
    const double t0 = bench::now_s();
    const uint64_t c0 = __rdtsc();
    for (long i = 0; i < n; ++i) x = op<F>(x, y);
    const uint64_t c1 = __rdtsc();
    const double t1 = bench::now_s();
    for (long i = 0; i < n / 8; ++i)
        for (int k = 0; k < 8; ++k) xs[k] = op<F>(xs[k], ys[k]);
    const uint64_t c2 = __rdtsc();
    const double t2 = bench::now_s();
    sim::end_run();
    u128 acc = x;
    for (int k = 0; k < 8; ++k) acc ^= xs[k];
    const double nt = double(n / 8 * 8);
    printf("fp128 micro form=%s n=%ld latency_ns=%.3f throughput_ns=%.3f latency_tsc=%.2f throughput_tsc=%.2f"
           " sink=%016llx\n",
           name, n, 1e9 * (t1 - t0) / double(n), 1e9 * (t2 - t1) / nt, double(c1 - c0) / double(n),
           double(c2 - c1) / nt, (unsigned long long)(acc ^ (acc >> 64)));
    return 0;
}

// --- the toy multiplier unit ---------------------------------------------------------------------------------------

constexpr unsigned kPairs = 2, kDepth = 4, kWheel = 2;

template <int F, bool POOL>
class Units {
public:
    static constexpr unsigned W = POOL ? 2 : 4;  // a pair: two indices, or two values

    Units() = default;
    Units(const Units&) = delete;
    ~Units() {
        if (block_) sim::free_big(block_);
        if (rings_) sim::free_big(rings_);
    }

    void build(uint32_t k, uint32_t first) {
        k_ = k;
        first_ = first;
        const uint32_t cap = k * (kPairs + 1);  // each unit: its pairs' x and its one y
        const size_t words = size_t(k) * sim::Ring::words_needed(W, kDepth) +
                             kWheel * sim::Bucket::words_needed(W + 1, k) + sim::Bitmap::words_needed(k) +
                             (POOL ? sim::ValuePool::words_needed(cap) : 0);
        uint64_t* p = block_ = static_cast<uint64_t*>(sim::alloc_big(words * 8, "units"));
        rings_ = static_cast<sim::Ring*>(sim::alloc_big(sizeof(sim::Ring) * k, "unit rings"));
        for (uint32_t u = 0; u < k; ++u) {
            new (&rings_[u]) sim::Ring();
            rings_[u].init(p, W, kDepth, "operands");
            p += sim::Ring::words_needed(W, kDepth);
        }
        for (unsigned b = 0; b < kWheel; ++b) {
            wheel_[b].init(p, W + 1, k, "unit wheel");
            p += sim::Bucket::words_needed(W + 1, k);
        }
        ready_.init(p, k, "unit ready");
        p += sim::Bitmap::words_needed(k);
        if (POOL) pool_.init(p, cap, "values");
        for (uint32_t u = 0; u < k; ++u) {
            uint64_t s = 0xabcdef + uint64_t(first + u) * 1000003;
            const u128 y = fp::reduce_once(rand128(s));
            uint32_t yi = 0;
            if (POOL) yi = pool_.alloc(), pool_[yi] = y;
            for (unsigned j = 0; j < kPairs; ++j) {
                const u128 x = fp::reduce_once(rand128(s));
                uint64_t* w = rings_[u].push();
                if constexpr (POOL) {
                    const uint32_t xi = pool_.alloc();
                    pool_[xi] = x;
                    w[0] = xi, w[1] = yi;
                } else {
                    w[0] = u64(x), w[1] = u64(x >> 64), w[2] = u64(y), w[3] = u64(y >> 64);
                }
            }
            ready_.set(u);
        }
    }

    void run(uint64_t from, uint64_t to) {
        for (uint64_t t = from; t < to; ++t) {
            deliver(t);
            step_all(t);
        }
    }

    // Per unit, its values in order (ring, then the entry in flight), folded; then in unit order.
    void digests(uint64_t* out, uint64_t t_end) const {
        for (uint32_t u = 0; u < k_; ++u) {
            uint64_t h = 0;
            for (unsigned i = 0; i < rings_[u].count(); ++i) h = fold_pair(h, rings_[u].peek(i));
            out[u] = h;
        }
        const sim::Bucket& b = wheel_[t_end & (kWheel - 1)];
        for (uint32_t i = 0; i < b.count(); ++i) {
            const uint64_t* e = b.entry(i);
            out[e[0] - first_] = fold_pair(out[e[0] - first_], e + 1);
        }
    }

private:
    uint64_t fold_pair(uint64_t h, const uint64_t* w) const {
        u128 x, y;
        if constexpr (POOL) x = pool_[uint32_t(w[0])], y = pool_[uint32_t(w[1])];
        else x = fp::make(w[1], w[0]), y = fp::make(w[3], w[2]);
        h = bench::fold(h, u64(x));
        h = bench::fold(h, u64(x >> 64));
        return bench::fold(h, u64(y) ^ u64(y >> 64));
    }

    __attribute__((noinline)) void deliver(uint64_t t) {
        sim::Bucket& b = wheel_[t & (kWheel - 1)];
        const uint64_t* e = b.begin();
        const unsigned ew = b.width();
        for (uint32_t i = 0, n = b.count(); i < n; ++i, e += ew) {
            const uint32_t u = uint32_t(e[0] - first_);
            rings_[u].push(e + 1);
            ready_.set(u);
        }
        b.clear();
    }

    __attribute__((noinline)) void step_all(uint64_t t) {
        sim::Bucket& out = wheel_[(t + 1) & (kWheel - 1)];
        ready_.for_each_set([&](size_t uu) {
            const uint32_t u = uint32_t(uu);
            sim::Ring& in = rings_[u];
            const uint64_t* w = in.peek();
            uint64_t* e = out.append();
            e[0] = first_ + u;
            if constexpr (POOL) {
                const uint32_t xi = uint32_t(w[0]), yi = uint32_t(w[1]);
                const u128 r = op<F>(pool_[xi], pool_[yi]);
                pool_.free(xi);
                const uint32_t ri = pool_.alloc();
                pool_[ri] = r;
                e[1] = ri, e[2] = yi;
            } else {
                const u128 r = op<F>(fp::make(w[1], w[0]), fp::make(w[3], w[2]));
                e[1] = u64(r), e[2] = u64(r >> 64), e[3] = w[2], e[4] = w[3];
            }
            in.pop();
            if (in.empty()) ready_.clear(u);
        });
    }

    uint32_t k_ = 0, first_ = 0;
    uint64_t* block_ = nullptr;
    sim::Ring* rings_ = nullptr;
    sim::Bucket wheel_[kWheel];
    sim::Bitmap ready_;
    sim::ValuePool pool_;
};

template <int F, bool POOL>
int unit_bench(uint32_t units, int nthreads, const int* cpus, long cycles, long warmup, const char* name) {
    if (units % uint32_t(nthreads)) FATAL("units %u not divisible by threads %d", units, nthreads);
    if (nthreads > 64) FATAL("too many threads");
    const uint32_t per = units / uint32_t(nthreads);
    double secs[64];
    uint64_t* digs = static_cast<uint64_t*>(sim::alloc_big(size_t(units) * 8, "digests"));
    bench::StartLine line;
    auto body = [&](int tid) {
        bench::pin_to(cpus[tid]);
        Units<F, POOL> us;
        us.build(per, uint32_t(tid) * per);
        line.arrive_and_wait();
        sim::begin_run();
        us.run(0, uint64_t(warmup));
        const double t0 = bench::now_s();
        us.run(uint64_t(warmup), uint64_t(warmup + cycles));
        const double t1 = bench::now_s();
        sim::end_run();
        secs[tid] = t1 - t0;
        us.digests(digs + size_t(tid) * per, uint64_t(warmup + cycles));
    };
    std::thread th[64];
    for (int i = 1; i < nthreads; ++i) th[i] = std::thread(body, i);
    std::thread rel([&] { line.release_when(nthreads); });
    body(0);
    rel.join();
    for (int i = 1; i < nthreads; ++i) th[i].join();
    double maxs = 0, sums = 0;
    for (int i = 0; i < nthreads; ++i) maxs = secs[i] > maxs ? secs[i] : maxs, sums += secs[i];
    uint64_t d = 0;
    for (uint32_t u = 0; u < units; ++u) d = bench::fold(d, digs[u]);
    const double muls = double(units) * double(cycles);
    printf("fp128 unit %s units=%u threads=%d cycles=%ld secs=%.4f thread_ns_per_mul=%.3f muls_per_s=%.4g"
           " ns_per_unit_cycle=%.3f digest=%016llx\n",
           name, units, nthreads, cycles, maxs, 1e9 * sums / muls, muls / maxs, 1e9 * maxs * nthreads / muls,
           (unsigned long long)d);
    sim::free_big(digs);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bench::Args a{argc, argv};
    if (a.flag("--check")) return check(a.num("--check", 1000000));
    if (a.flag("--dump")) return dump(a.num("--dump", 1000));
    const char* form = a.get("--form", "u128");
    const int f = !strcmp(form, "u128") ? F_U128 : !strcmp(form, "limbs") ? F_LIMBS : !strcmp(form, "none") ? F_NONE
                                                                            : !strcmp(form, "acc") ? F_ACC : -1;
    if (f < 0) FATAL("unknown form '%s'", form);
    if (a.flag("--micro")) {
        const long n = a.num("--n", 100000000);
        if (f == F_U128) return micro<F_U128>(n, form);
        if (f == F_LIMBS) return micro<F_LIMBS>(n, form);
        if (f == F_ACC) return micro<F_ACC>(n, form);
        return micro<F_NONE>(n, form);
    }
    if (a.flag("--unit")) {
        const bool pool = strcmp(a.get("--hold", "pool"), "inline") != 0;
        int cpus[64];
        const int ncpu = bench::parse_cpus(a.get("--cpus", "0"), cpus, 64);
        const int nt = int(a.num("--threads", ncpu));
        if (nt > ncpu) FATAL("threads %d but %d cpus listed", nt, ncpu);
        if (a.flag("--nopin"))
            for (int i = 0; i < ncpu; ++i) cpus[i] = -1;
        const uint32_t units = uint32_t(a.num("--units", 64));
        const long cycles = a.num("--cycles", 100000), warmup = a.num("--warmup", 10000);
        char name[64];
        snprintf(name, sizeof name, "form=%s hold=%s", form, pool ? "pool" : "inline");
#define U(FF, P) return unit_bench<FF, P>(units, nt, cpus, cycles, warmup, name)
        if (f == F_U128) {
            if (pool) U(F_U128, true);
            U(F_U128, false);
        }
        if (f == F_LIMBS) {
            if (pool) U(F_LIMBS, true);
            U(F_LIMBS, false);
        }
        if (f == F_ACC) {
            if (pool) U(F_ACC, true);
            U(F_ACC, false);
        }
        if (pool) U(F_NONE, true);
        U(F_NONE, false);
#undef U
    }
    fprintf(stderr, "usage: see the header of fp128.cpp\n");
    return 2;
}
