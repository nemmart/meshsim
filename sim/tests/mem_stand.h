// mem_stand.h (WORKER11): the read queue's ends for tests that build add_g1 without W9's chip (in a chip they are the
// injection unit's): idle, no read is made; a read-port request is fatal.
#pragma once

#include "base/fatal.h"
#include "config.h"
#include "sim.h"
#include "units/chip.h"

struct MemStand {
    sim::FifoIn rd_port;
    sim::HoldOut sram_rd;
    sim::LinkIn ret[16];
    sim::CountOut rp_injected;
    explicit MemStand(sim::Ports& p) {
        p.in(rd_port, "rd_port", 2, 64); p.out(sram_rd, "sram_rd", 1);
        for (auto& r : ret) p.in(r, "ret", 3);
        p.out(rp_injected, "rp_injected");
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(rd_port)) FATAL("MemStand: a read-port request at cycle %llu", (unsigned long long)c.now());
        return sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};
inline void wire_mem_stand(sim::Config& cfg, const units::ControlIO& io) {
    auto& ms = cfg.add<MemStand>("mem");
    cfg.connect(io.mem.rd_port, ms.rd_port);
    cfg.connect(ms.sram_rd, io.mem.sram_rd);
    for (unsigned a = 0; a < 16; a++) cfg.link(io.mem.sram_ret[a], ms.ret[a], io.mem.sram_read_latency);
    cfg.connect(ms.rp_injected, io.mem.rp_injected);
}
