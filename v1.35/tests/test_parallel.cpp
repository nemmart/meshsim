// Independent chips on independent threads (form 1): N machines on N threads give the digests they give one at
// a time; built concurrently; nothing allocated in their loops. Run under TSan by `make tsan` (src/threads.mk).
#include <string>
#include <vector>

#include "base/alloc_guard.h"
#include "harness.h"
#include "parallel.h"
#include "synth.h"

using namespace sim;

namespace {

constexpr unsigned kChips = 6;
constexpr Cycle kSpan = 400, kSpans = 3;

synth::Params params(unsigned k) {
    synth::Params p;
    p.seed = 1000 + 17 * k;
    p.clusters = 3 + k % 3;
    p.nodes_per_cluster = 3 + k % 4;
    p.gap = 1 + k % 3;
    return p;
}
Sim make(unsigned k) {
    Config cfg;
    synth::build(cfg, params(k));
    return cfg.build();
}
// The digests of chip k at kSpans checkpoints, one span apart, run alone on this thread.
std::vector<uint64_t> alone(unsigned k) {
    Sim s = make(k);
    std::vector<uint64_t> d;
    for (Cycle i = 0; i < kSpans; i++) { s.run(kSpan); d.push_back(s.digest()); }
    return d;
}

// Until each Config has its own registry, the unit types are registered here, on one thread, before any
// concurrent build: concurrent builds then only read the registry.
void register_types() { (void)make(0); }

TEST(a_synthetic_chip_is_deterministic_and_busy) {
    auto a = alone(1), b = alone(1);
    for (Cycle i = 0; i < kSpans; i++) CHECK_EQ(a[i], b[i]);
    CHECK(a[0] != a[1] && a[1] != a[2]);
    Sim s = make(1);
    s.run(kSpan);
    CHECK(s.steps("n0") > kSpan / 4);
    uint64_t ev = 0, ref = 0, nc = 0;
    for (unsigned i = 0; i < 12; i++) {
        auto& n = s.unit<synth::Node>("n" + std::to_string(i));
        ev += n.events; ref += n.refusals; nc += n.no_credit;
    }
    CHECK(ev > 0 && ref > 0 && nc > 0);                    // events, Hold refusals and exhausted credits all happen
}

TEST(n_chips_on_n_threads_match_one_at_a_time) {
    std::vector<std::vector<uint64_t>> want(kChips);
    for (unsigned k = 0; k < kChips; k++) want[k] = alone(k);
    std::vector<Sim> sims;
    for (unsigned k = 0; k < kChips; k++) sims.push_back(make(k));
    std::vector<Sim*> p;
    for (auto& s : sims) p.push_back(&s);
    for (Cycle i = 0; i < kSpans; i++) {
        run_independent(p, kSpan);
        for (unsigned k = 0; k < kChips; k++) CHECK_EQ(sims[k].digest(), want[k][i]);
    }
}

TEST(n_chips_built_and_run_on_their_own_threads) {
    register_types();
    std::vector<std::vector<uint64_t>> want(kChips), got(kChips);
    for (unsigned k = 0; k < kChips; k++) want[k] = alone(k);
    run_independent(kChips, make, [&](unsigned k, Sim& s) {
        for (Cycle i = 0; i < kSpans; i++) {
            begin_run();                                   // the hook is per thread: this thread's loop allocates nothing
            s.run(kSpan);
            end_run();
            got[k].push_back(s.digest());
        }
    });
    for (unsigned k = 0; k < kChips; k++)
        for (Cycle i = 0; i < kSpans; i++) CHECK_EQ(got[k][i], want[k][i]);
}

TEST(the_same_chip_twice_on_two_threads) {
    register_types();
    std::vector<uint64_t> d(2);
    run_independent(2, [](unsigned) { return make(3); }, [&](unsigned k, Sim& s) { s.run(kSpan * kSpans); d[k] = s.digest(); });
    CHECK_EQ(d[0], d[1]);
    CHECK_EQ(d[0], alone(3).back());
}

DEATH_TEST(the_same_sim_twice_is_refused, "Sim 0 is also Sim 1") {
    Sim s = make(0);
    run_independent({&s, &s}, 1);
}

}  // namespace
