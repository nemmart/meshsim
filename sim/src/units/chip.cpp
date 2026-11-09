// chip.cpp (WORKER9): the chip's assembly (chip.h). Configuration only.
#include "units/chip.h"

#include "base/fatal.h"
#include "units/host_port.h"
#include "units/inject.h"
#include "units/router.h"

namespace units {

std::string unit_prefix(unsigned k) { return k < 64 ? "t" + std::to_string(k) + "." : "cc" + std::to_string(k - 63) + "."; }
std::string tile_unit(unsigned k, const char* unit) { return unit_prefix(k) + unit; }

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

// M3 (cc_cores), until C&C 1's slot is wired (W15's slot body): a stand-in for C&C 1's peripheral's own ports (the
// row collectors' words, the controllers' line words) that sinks them and never drives GO. Also the source of the
// controllers' GO inputs, which nothing drives in ref (CnC.goNet reaches tiles 0-63 only).
struct ChipLinesStandIn {
    sim::LinkIn rows[8], cc_line[3];
    sim::LinkOut go[64], cc_go[3];
    explicit ChipLinesStandIn(sim::Ports& p) {
        for (unsigned r = 0; r < 8; r++) p.in(rows[r], "row", 1);
        for (unsigned c = 0; c < 3; c++) { p.in(cc_line[c], "cc-line", 1); p.out(cc_go[c], "cc-go", 0); }
        for (unsigned k = 0; k < 64; k++) p.out(go[k], "go", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        for (auto& r : rows) if (c.ready(r)) c.take(r);
        for (auto& l : cc_line) if (c.ready(l)) c.take(l);
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

namespace {
// A unit's injection unit and its wiring to the router and the control part (tile_cycle's and cc_cycle's).
void wire_unit(sim::Config& cfg, const NetConfig& nc, unsigned k, const ControlIO& io, Router& r) {
    ReadSideConfig rc;
    rc.present = io.mem.present; rc.gather = io.mem.gather; rc.ag = io.mem.ag;
    rc.read_port_depth = io.mem.read_port_depth; rc.sram_read_latency = io.mem.sram_read_latency; rc.mem_chunks = io.mem.mem_chunks;
    auto& in = cfg.add<Injection>(tile_unit(k, "inj"), k, nc, rc);
    cfg.connect(io.send, in.send);
    cfg.connect(in.send_busy, io.send_busy);
    cfg.link(in.to_router, r.inj, nc.intra_tile_latency);
    cfg.link(r.loop_left, in.loop_left, 1);
    cfg.link(r.eject, io.eject, nc.intra_tile_latency);
    cfg.connect(r.ovf, io.router_ovf);
    cfg.link(r.ovf_ev, io.router_ovf_ev, 1);              // D29: the overflow steps the block (counted at t + 1, D28)
    if (io.mem.present) {                                 // W11: the read queue's clients, the big SRAM's ends
        if (io.mem.gather) cfg.connect(io.mem.rd_gather, in.rs.client_hold[InjReadSide::GATHER]);
        if (io.mem.ag) cfg.connect(io.mem.rd_ag, in.rs.client_hold[InjReadSide::AG]);
        if (io.mem.gather) cfg.connect(in.rs.gather_srq_busy, io.mem.gather_srq_busy);
        cfg.connect(io.mem.rd_port, in.rs.rp_q);
        cfg.connect(in.rs.sram_rd, io.mem.sram_rd);
        for (unsigned a = 0; a < InjReadSide::AREAS; a++) cfg.link(io.mem.sram_ret[a], in.rs.ret[a], io.mem.sram_read_latency);
        cfg.connect(in.rs.rp_injected, io.mem.rp_injected);
    }
    // A send waiting for a loopback token sleeps until the router frees one, which may not be on its way yet (the
    // loopback FIFO drains when the eject selector reaches it): longer than the detector's default.
    cfg.missed_wake_after(tile_unit(k, "inj"), 1u << 20);
}
}  // namespace

TileIO tile_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned k) {
    const NetConfig& nc = spec.net;
    if (spec.domain) cfg.domain(spec.domain[k]);
    ControlIO io = spec.control ? spec.control(cfg, k, spec.control_ctx) : stand_in(cfg, k);
    auto& r = cfg.add<Router>(tile_unit(k, "router"), k, nc, spec.hops, true,
                              spec.cc_cores ? Router::FIFTH_LINK : Router::FIFTH_HOLD);
    wire_unit(cfg, nc, k, io, r);
    TileIO t;
    for (unsigned d = 0; d < 4; d++) { t.link_in[d] = r.in[d]; t.link_out[d] = r.out[d]; }
    t.go = io.go;
    t.line = io.line;
    t.attached = k < 3;
    if (t.attached && !spec.cc_cores) { t.from_cnc = r.from_cnc; t.to_cnc = r.to_cnc; }
    if (t.attached && spec.cc_cores) { t.from_cc = r.from_cc; t.to_cc = r.to_cc; }
    return t;
}

CcIO cc_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned c) {
    const NetConfig& nc = spec.net;
    if (c > 2) FATAL("cc_cycle: controller %u (0-2)", c);
    const unsigned k = lw::CC1 + c;
    ControlIO io = spec.control ? spec.control(cfg, k, spec.control_ctx) : stand_in(cfg, k);
    const bool dma = c == 2 && spec.cc3_dma;
    auto& r = cfg.add<Router>(tile_unit(k, "router"), k, nc, spec.hops, true, Router::FIFTH_HOLD, dma);
    wire_unit(cfg, nc, k, io, r);
    auto& h = cfg.add<HostPort>(tile_unit(k, "host"), c, spec.host_capacity);
    cfg.connect(h.out, r.host);
    CcIO o;
    o.link_in = r.in[Router::CC_LINK_IN]; o.link_out = r.out[Router::CC_LINK_OUT];
    o.has_dma = dma; if (dma) o.dma = r.dma;
    o.go = io.go; o.line = io.line;
    return o;
}

void build_chip(sim::Config& cfg, const ChipSpec& spec) {
    const NetConfig& nc = spec.net;
    if (!spec.hops) FATAL("build_chip: no hops table");
    if (!spec.program && !spec.cc_cores) FATAL("build_chip: no C&C program");
    if (nc.link_latency < 1 || nc.ready_latency < 3) FATAL("build_chip: link_latency %u, ready_latency %u", nc.link_latency, nc.ready_latency);
    uint8_t skew[64];
    start_skews(nc, skew);
    TileIO io[64];
    for (unsigned k = 0; k < 64; k++) io[k] = tile_cycle(cfg, spec, k);
    for (unsigned k = 0; k < 64; k++)
        for (unsigned d = 0; d < 4; d++) cfg.link(io[k].link_out[d], io[neighbour(k, d)].link_in[arrival_port(d)], nc.link_latency);
    if (spec.cc_cores) {
        // C&C c with tile c (the C&C links are Link(cc_link_latency), 1 in ref: one domain, D26)
        CcIO u[3];
        for (unsigned c = 0; c < 3; c++) {
            if (spec.domain) cfg.domain(spec.domain[c]);
            u[c] = cc_cycle(cfg, spec, c);
            cfg.link(io[c].to_cc, u[c].link_in, nc.cc_link_latency);
            cfg.link(u[c].link_out, io[c].from_cc, nc.cc_link_latency);
        }
        // The chip lines (AUDIT-M3 3.4, section 4): the tiles' line words through M2's row collectors, the controllers'
        // directly, all into C&C 1's peripheral (READY, ERROR_LINES, the toast pin), GO out of it; with C&C 1, the
        // collectors in tile 0's domain. Until the slot is wired, a stand-in takes C&C 1's peripheral's place.
        if (spec.domain) cfg.domain(spec.domain[0]);
        auto& cl = cfg.add<ChipLinesStandIn>("cc1.lines");
        for (unsigned r = 0; r < 8; r++) {
            auto& row = cfg.add<RowCollector>("row" + std::to_string(r), r);
            cfg.link(row.out, cl.rows[r], 1);
            for (unsigned i = 0; i < 8; i++) cfg.link(io[8 * r + i].line, row.line[i], nc.ready_latency - 2);
        }
        for (unsigned k = 0; k < 64; k++) cfg.link(cl.go[k], io[k].go, nc.go_latency + skew[k]);
        for (unsigned c = 0; c < 3; c++) {
            cfg.link(u[c].line, cl.cc_line[c], c == 0 ? 1 : nc.error_lat() - 1);
            cfg.link(cl.cc_go[c], u[c].go, nc.go_latency + 1);
        }
        return;
    }
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
