// controller.cpp: wiring one tile's controller (configuration regime).
#include "units/controller.h"

namespace units {

TileHandles configure_tile(sim::Config& cfg, const std::string& prefix, const Rv32& core, const RegBlockState& block,
                           const BenchEvent* events, uint32_t n_events, TraceLog* trace) {
    auto& b = cfg.add<TestBench>(prefix + ".bench", events, n_events, trace);
    auto& r = cfg.add<RegBlock>(prefix + ".ctl", block);
    auto& c = cfg.add<Core>(prefix + ".core", core);
    cfg.link(c.bus, r.bus, 1);
    cfg.link(b.memw, c.memw, 1);
    cfg.link(b.go, r.go, 1);
    cfg.link(b.landed, r.landed, 1);
    cfg.link(b.freed, r.freed, 1);
    cfg.connect(r.regs, c.regs);
    cfg.connect(r.wake, c.wake);
    cfg.connect(r.irq, c.irq);
    cfg.connect(r.req_busy, c.req_busy);
    cfg.connect(b.req_freed, c.req_freed);
    return {&c, &r, &b};
}

}  // namespace units
