// chip.cpp: the chip's assembly (chip.h). Configuration only.
#include "units/chip.h"

#include "base/fatal.h"
#include "units/ingress_egress.h"
#include "units/inject.h"
#include "units/periph_slot.h"
#include "units/router.h"

namespace units {

std::string tile_unit(unsigned k, const char* unit) { return unit_prefix(k) + unit; }

namespace {

// The network-only checks' stand-in for a tile's control part: sinks the eject bus and the release, never sends, never
// arrives, its error line low.
struct ControlStandIn {
    sim::HoldOut send;
    sim::RegIn send_busy;
    sim::LinkIn eject, release;
    sim::LinkOut arrive, error;
    sim::CountIn router_ovf;
    sim::LinkIn router_ovf_ev;
    explicit ControlStandIn(sim::Ports& p) {
        p.out(send, "send", 3); p.in(send_busy, "send_busy", 1); p.in(eject, "eject", 3); p.in(release, "release", 0);
        p.out(arrive, "arrive", 0); p.out(error, "error", 1); p.in(router_ovf, "router_ovf"); p.in(router_ovf_ev, "ovf", 0);
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(eject)) c.take(eject);
        if (c.ready(release)) c.take(release);
        if (c.ready(router_ovf_ev)) c.take(router_ovf_ev);
        return sim::Wake::none();
    }
    uint64_t digest() const { return 0; }
};

ControlIO stand_in(sim::Config& cfg, unsigned k) {
    auto& s = cfg.add<ControlStandIn>(tile_unit(k, "stand"));
    ControlIO io;
    io.send = s.send; io.send_busy = s.send_busy; io.eject = s.eject;
    io.barrier = true; io.release = s.release; io.arrive = s.arrive; io.error = s.error;
    io.router_ovf = s.router_ovf; io.router_ovf_ev = s.router_ovf_ev;
    return io;
}

// A unit's injection unit and its wiring to the router and the control part (tile_cycle's and special_cycle's).
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
    if (io.mem.present) {                                 // the read queue's clients, the big SRAM's ends
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
    if (!io.barrier) FATAL("tile_cycle: tile %u's control has no barrier: a compute tile's nets are wired (7.1)", k);
    auto& r = cfg.add<Router>(tile_unit(k, "router"), k, nc, spec.hops, true,
                              spec.bench_attach ? Router::FIFTH_HOLD : Router::FIFTH_LINK);
    wire_unit(cfg, nc, k, io, r);
    TileIO t;
    for (unsigned d = 0; d < 4; d++) { t.link_in[d] = r.in[d]; t.link_out[d] = r.out[d]; }
    t.release = io.release; t.arrive = io.arrive; t.error = io.error;
    t.attached = nc.attached_special(k) >= 0;
    if (t.attached && spec.bench_attach) { t.from_bench = r.from_cnc; t.to_bench = r.to_cnc; }
    if (t.attached && !spec.bench_attach) { t.attach_in = r.from_cc; t.attach_out = r.to_cc; }
    t.req = io.req;                                       // wired to the request tree by its builder
    return t;
}

SpecialIO special_cycle(sim::Config& cfg, const ChipSpec& spec, unsigned s) {
    const NetConfig& nc = spec.net;
    if (s > 2) FATAL("special_cycle: special tile %u (0-2)", s);
    if (!spec.control) FATAL("special_cycle: a special tile needs its control part (ChipSpec::control)");
    const unsigned k = lw::CC1 + s;
    ControlIO io = spec.control(cfg, k, spec.control_ctx);
    if (io.barrier) FATAL("special_cycle: unit %u's control has the barrier's nets: a special tile's are not wired (7.3)", k);
    auto& r = cfg.add<Router>(tile_unit(k, "router"), k, nc, spec.hops, true, Router::FIFTH_LINK, io.has_dma);
    wire_unit(cfg, nc, k, io, r);
    if (io.has_dma) cfg.connect(io.dma, r.dma);           // the peripheral's DMA, on an input of its own (16.4)
    SpecialIO o;
    o.attach_in = r.in[Router::CC_LINK_IN]; o.attach_out = r.out[Router::CC_LINK_OUT];
    o.error = io.error; o.slot = io.slot;
    o.req = io.req;                                       // its request unit, both sides
    return o;
}

void add_mesh(sim::Config& cfg, const ChipSpec& spec, TileIO io[64]) {
    const NetConfig& nc = spec.net;
    if (!spec.hops) FATAL("add_mesh: no hops table");
    if (nc.link_latency < 1) FATAL("add_mesh: link_latency %u", nc.link_latency);
    for (unsigned k = 0; k < 64; k++) io[k] = tile_cycle(cfg, spec, k);
    for (unsigned k = 0; k < 64; k++)
        for (unsigned d = 0; d < 4; d++) cfg.link(io[k].link_out[d], io[neighbour(k, d)].link_in[arrival_port(d)], nc.link_latency);
}

// Each leaf in its unit's domain, each receive queue in its special tile's, the nodes with tile 0's.
void add_request_tree(sim::Config& cfg, const ChipSpec& spec, TileIO* io, SpecialIO* special) {
    uint64_t tiles = 0;
    unsigned specials = 0;
    units::reqbus::ReqBusPlacement pl;
    for (unsigned k = 0; k < 64; k++) {
        if (io[k].req.present) tiles |= uint64_t(1) << k;
        pl.leaf[k] = spec.domain ? spec.domain[k] : 0;
    }
    for (unsigned s = 0; special && s < 3; s++) {
        if (special[s].req.present) specials |= 1u << s;
        pl.leaf[64 + s] = pl.recv[s] = spec.domain ? spec.domain[spec.net.attach[s]] : 0;
    }
    pl.bus = spec.domain ? spec.domain[0] : 0;
    if (!tiles && !specials) return;
    const units::reqbus::ReqBusIO rb = units::reqbus::add_reqbus(cfg, spec.reqbus, tiles, specials, spec.domain ? &pl : nullptr);
    for (unsigned k = 0; k < 64; k++) if (io[k].req.present) units::reqbus::wire_sender(cfg, rb, k, io[k].req);
    for (unsigned s = 0; special && s < 3; s++) if (special[s].req.present) units::reqbus::wire_sender(cfg, rb, 64 + s, special[s].req);
}

void build_chip(sim::Config& cfg, const ChipSpec& spec) {
    const NetConfig& nc = spec.net;
    if (spec.bench_attach) FATAL("build_chip: the chip's attach ports are the special tiles' (ChipSpec::bench_attach is a test bench's)");
    if (nc.release_latency < 1)
        FATAL("build_chip: RELEASE_LATENCY 0: Ingress/Egress's flip-flop's cycle is the first of BARRIER_RELEASE's (machine-spec 17.6, 18)");
    TileIO io[64];
    add_mesh(cfg, spec, io);
    // the special tiles, each with its attach tile: its attach link is a link like the others (16.1)
    SpecialIO sp[3];
    for (unsigned s = 0; s < 3; s++) {
        const unsigned at = nc.attach[s];
        if (spec.domain) cfg.domain(spec.domain[at]);
        sp[s] = special_cycle(cfg, spec, s);
        cfg.link(io[at].attach_out, sp[s].attach_in, nc.link_latency);
        cfg.link(sp[s].attach_out, io[at].attach_in, nc.link_latency);
    }
    add_request_tree(cfg, spec, io, sp);
    // the global nets (18), into and out of the master tile's Ingress/Egress through its slot's own ports
    if (spec.domain) cfg.domain(spec.domain[nc.attach[0]]);
    PeriphSlot* ms = sp[0].slot;
    if (!ms || ms->s.per[1].type != PT_INGRESS) FATAL("build_chip: the master tile's slot holds no Ingress/Egress (17.6)");
    const IngressEgress& ie = *static_cast<const IngressEgress*>(ms->s.per[1].p);
    for (unsigned r = 0; r < 8; r++) {
        auto& row = cfg.add<NetRow>("nets.row" + std::to_string(r), r);
        cfg.connect(row.out, ms->own.wire_in[ie.row0 + r]);
        for (unsigned i = 0; i < 8; i++) {
            cfg.link(io[8 * r + i].arrive, row.arrive[i], 1 + nc.arrive_latency);
            cfg.link(io[8 * r + i].error, row.error[i], 1 + nc.error_latency);
        }
    }
    for (unsigned k = 0; k < 64; k++) cfg.link(ms->own.link_out[ie.release0 + k], io[k].release, nc.release_latency);
    cfg.link(sp[0].error, ms->own.link_in[ie.line0], 1);  // the master tile's own line: the toast pin (17.6)
    for (unsigned s = 1; s < 3; s++) cfg.link(sp[s].error, ms->own.link_in[ie.line0 + s], 1 + nc.error_latency);
}

void row_bands(unsigned parts, uint8_t domain[64]) {
    if (parts != 1 && parts != 2 && parts != 4 && parts != 8) FATAL("row_bands: %u parts (1, 2, 4 or 8)", parts);
    for (unsigned k = 0; k < 64; k++) domain[k] = uint8_t((k / 8) * parts / 8);
}

}  // namespace units
