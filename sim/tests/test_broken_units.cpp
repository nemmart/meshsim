// The debug checks catch a broken unit (W5): the controller's tile on `hello` with its register block swapped for a broken
// one, wired exactly as configure_tile wires it.
//   CountingBlock  counts its own steps into its state: a step it didn't ask for changes it. The trace can't show it (the
//                  count reaches no port); step-all's digest differs from the ready-driven run's. The missed-wake detector
//                  has nothing to see (no data is left waiting).
//   LazyBlock      leaves the core's bus word untaken on even cycles and sleeps (hello's PULSE_DONE arrives at 12): a missed wake. The detector names it (debug
//                  builds); step-all, which steps it anyway, applies the word a cycle late and its digest differs too.
#include <string>

#include "golden_run.h"
#include "harness.h"

using namespace test;
using namespace units;

namespace {

struct CountingBlock : RegBlock {
    uint64_t steps = 0;
    using RegBlock::RegBlock;
    Wake step(Ctx& c) { steps++; return RegBlock::step(c); }
    uint64_t digest() const { return digest_mix(RegBlock::digest(), steps); }
};

struct LazyBlock : RegBlock {
    using RegBlock::RegBlock;
    Wake step(Ctx& c) {
        if (c.ready(bus) && !(c.now() & 1)) return Wake::none();      // the bug: the word stays, nothing will wake us
        return RegBlock::step(c);
    }
};

template <class Block>
EngineRun run_with(const sim::Options& o, bool* trace_matches = nullptr, bool alloc_guard = true) {
    auto runs = read_runs();
    const RunSpec* r = find_run(runs, "hello");
    Golden g; Images im; std::string err;
    if (!r || !read_golden("tests/golden/hello.trace", g, err) || !load_images(*r, im, err)) return {};
    TileConfig tc = tile_config(r->tile);
    auto header = header_lines(*r, im, tc);
    Tile t(im, tc, uint32_t(g.body_lines + 64));
    sim::Config cfg(o);
    auto& b = cfg.add<TestBench>("t.bench", g.events.data(), uint32_t(g.events.size()), &t.trace.log);
    auto& k = cfg.add<Block>("t.ctl", t.block);
    auto& c = cfg.add<Core>("t.core", t.core);
    cfg.link(c.bus, k.bus, 1);
    cfg.link(b.memw, c.memw, 1);
    cfg.link(b.go, k.go, 1);
    cfg.link(b.landed, k.landed, 1);
    cfg.link(b.freed, k.freed, 1);
    cfg.connect(k.regs, c.regs);
    cfg.connect(k.wake, c.wake);
    cfg.connect(k.irq, c.irq);
    cfg.connect(k.req_busy, c.req_busy);
    cfg.connect(b.req_freed, c.req_freed);
    sim::Sim s = cfg.build();
    EngineRun e = run_built(s, t, g, header, alloc_guard);
    if (trace_matches) *trace_matches = compare(e.lines, g.lines).empty();
    return e;
}

sim::Options step_all() { sim::Options o; o.step_all = true; return o; }

}  // namespace

TEST(broken_counting_block_caught_by_step_all) {
    bool same_ready = false, same_all = false;
    EngineRun ready = run_with<CountingBlock>(sim::Options{}, &same_ready);
    EngineRun all = run_with<CountingBlock>(step_all(), &same_all);
    CHECK(same_ready && same_all);                     // the traces can't see the bug
    CHECK(ready.digest != all.digest);                 // step-all's digest does
    printf("  CountingBlock: traces equal to the golden both ways; digest ready-driven %016llx, step-all %016llx\n",
           (unsigned long long)ready.digest, (unsigned long long)all.digest);
    bool ok = false;                                   // the healthy block, for contrast: equal
    CHECK_EQ(run_with<RegBlock>(sim::Options{}, &ok).digest, run_with<RegBlock>(step_all()).digest);
    CHECK(ok);
}

TEST(broken_lazy_block_caught_by_step_all) {
    sim::Options quiet; quiet.missed_wake_after = 0;                       // the detector off: step-all alone here
    sim::Options all = step_all();
    EngineRun ready = run_with<LazyBlock>(quiet);
    EngineRun stepped = run_with<LazyBlock>(all);
    CHECK(ready.digest != stepped.digest);
    printf("  LazyBlock: digest ready-driven %016llx, step-all %016llx\n", (unsigned long long)ready.digest,
           (unsigned long long)stepped.digest);
}

// Under the allocation hook: the report, not the hook, stops it (D24: diagnostics don't allocate).
DEBUG_DEATH_TEST(broken_lazy_block_caught_by_the_missed_wake_detector, "missed wake at cycle [0-9]+: .*t\\.ctl") {
    run_with<LazyBlock>(sim::Options{}, nullptr, true);
}
