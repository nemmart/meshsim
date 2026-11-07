// chip.cpp (WORKER9): the chip's assembly (chip.h). Configuration only.
#include "units/chip.h"

#include "base/fatal.h"
#include "units/inject.h"
#include "units/router.h"

namespace units {

std::string tile_unit(unsigned k, const char* unit) { return "t" + std::to_string(k) + "." + unit; }

namespace {

// The network-only checks' stand-in for a tile's control part: sinks the eject bus and GO, never sends, never READY.
struct ControlStandIn {
    sim::HoldOut send;
    sim::RegIn send_busy;
    sim::LinkIn eject, go;
    sim::LinkOut line;
    sim::CountIn router_ovf;
    sim::LinkIn router_ovf_ev;
    explicit ControlStandIn(sim::Ports& p) {
        p.out(send, "send", 3); p.in(send_busy, "send_busy", 1); p.in(eject, "eject", 3); p.in(go, "go", 0);
        p.out(line, "line", 1); p.in(router_ovf, "router_ovf"); p.in(router_ovf_ev, "ovf", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(eject)) c.take(eject);
        if (c.ready(go)) c.take(go);
        if (c.ready(router_ovf_ev)) c.take(router_ovf_ev);
        return sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};

ControlIO stand_in(sim::Config& cfg, unsigned k) {
    auto& s = cfg.add<ControlStandIn>(tile_unit(k, "stand"));
    ControlIO io;
    io.send = s.send; io.send_busy = s.send_busy; io.eject = s.eject; io.go = s.go; io.line = s.line; io.router_ovf = s.router_ovf;
    io.router_ovf_ev = s.router_ovf_ev;
    return io;
}

}  // namespace

TileIO tile_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned k) {
    const NetConfig& nc = spec.net;
    if (spec.domain) cfg.domain(spec.domain[k]);
    ControlIO io = spec.control ? spec.control(cfg, k, spec.control_ctx) : stand_in(cfg, k);
    auto& r = cfg.add<Router>(tile_unit(k, "router"), k, nc, spec.hops, true);
    auto& in = cfg.add<Injection>(tile_unit(k, "inj"), k, nc);
    cfg.connect(io.send, in.send);
    cfg.connect(in.send_busy, io.send_busy);
    cfg.link(in.to_router, r.inj, nc.intra_tile_latency);
    cfg.link(r.loop_left, in.loop_left, 1);
    cfg.link(r.eject, io.eject, nc.intra_tile_latency);
    cfg.connect(r.ovf, io.router_ovf);
    cfg.link(r.ovf_ev, io.router_ovf_ev, 1);              // D29: the overflow steps the block (counted at t + 1, D28)
    // A send waiting for a loopback token sleeps until the router frees one, which may not be on its way yet (the
    // loopback FIFO drains when the eject selector reaches it): longer than the detector's default.
    cfg.missed_wake_after(tile_unit(k, "inj"), 1u << 20);
    TileIO t;
    for (unsigned d = 0; d < 4; d++) { t.link_in[d] = r.in[d]; t.link_out[d] = r.out[d]; }
    t.go = io.go;
    t.line = io.line;
    t.attached = k < 3;
    if (t.attached) { t.from_cnc = r.from_cnc; t.to_cnc = r.to_cnc; }
    return t;
}

void build_chip(sim::Config& cfg, const ChipSpec& spec) {
    const NetConfig& nc = spec.net;
    if (!spec.hops) FATAL("build_chip: no hops table");
    if (!spec.program) FATAL("build_chip: no C&C program");
    if (nc.link_latency < 1 || nc.ready_latency < 3) FATAL("build_chip: link_latency %u, ready_latency %u", nc.link_latency, nc.ready_latency);
    uint8_t skew[64];
    start_skews(nc, skew);
    TileIO io[64];
    for (unsigned k = 0; k < 64; k++) io[k] = tile_cycle(cfg, spec, k);
    for (unsigned k = 0; k < 64; k++)
        for (unsigned d = 0; d < 4; d++) cfg.link(io[k].link_out[d], io[neighbour(k, d)].link_in[arrival_port(d)], nc.link_latency);
    // C&C and the collectors with tile 0 (C&C's send is a Hold into tile 0's router), C&C 2 and 3 with tiles 1 and 2
    if (spec.domain) cfg.domain(spec.domain[0]);
    auto& cc = cfg.add<Cnc>("cnc", nc, *spec.program);
    RowCollector* rows[8];
    for (unsigned r = 0; r < 8; r++) {
        rows[r] = &cfg.add<RowCollector>("row" + std::to_string(r), r);
        cfg.link(rows[r]->out, cc.rows[r], 1);
    }
    cfg.connect(cc.send, io[0].from_cnc);
    cfg.connect(io[0].to_cnc, cc.rx);
    for (unsigned e = 1; e < 3; e++) {
        if (spec.domain) cfg.domain(spec.domain[e]);
        auto& ep = cfg.add<Endpoint>("ep" + std::to_string(e + 1), lw::CC1 + e, nc);
        cfg.connect(ep.send, io[e].from_cnc);
        cfg.connect(io[e].to_cnc, ep.rx);
    }
    for (unsigned k = 0; k < 64; k++) {
        cfg.link(cc.go[k], io[k].go, nc.go_latency + skew[k] + 1);
        cfg.link(io[k].line, rows[k / 8]->line[k % 8], nc.ready_latency - 2);
    }
}

void row_bands(unsigned parts, uint8_t domain[64]) {
    if (parts != 1 && parts != 2 && parts != 4 && parts != 8) FATAL("row_bands: %u parts (1, 2, 4 or 8)", parts);
    for (unsigned k = 0; k < 64; k++) domain[k] = uint8_t((k / 8) * parts / 8);
}

}  // namespace units
