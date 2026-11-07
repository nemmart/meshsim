// tile_ctl.h: the tile's control in an M2 tile (WORKER8, G1): the register block as a unit with the M2 ports
// (TileControl.java; AUDIT-M2 3.7), the core's M2 ports, and the fragment of the tile builder that adds G1's units and
// returns the handles W9's builder wires. DECLARATIONS: the ports are the agreed interface (sessions/WORKER8/NOTES.md);
// the steps come with the implementation. W5's M1 units (controller.h: Core, RegBlock, TestBench) stay for M1's goldens.
//
// Levels (from the wires): 0 the block (TileControl.endCycle(t - 1) from what reached it at t, then the view for t);
// 1 the core (Controller.runCycle(t)). The director has no wire: it runs at any level.
//
// The register block's inputs, each registered (so endCycle(t) runs at level 0 of t + 1 from inputs delivered at t + 1):
//   bus         Link(1), width 2   the core's store, consuming load or bus error of t (W5's encoding)
//   go          Link(go_latency + skew[k] + 1), width 0, from W9's C&C model
//   landed      Link(cm_latency + 1), width 0, from the director: a port-B write covering command slot 0 landed at t
//   kline       Link(K + 1), width 1, from the director: counter id, a decrement due at t (judged without the same
//               cycle's store, D27; then the store's add)
//   send_busy   Reg, width 1, from W9's injection unit: the send hold occupied at the end of t; STATUS [3] is that OR
//               the core's own put at t (a send store or a retry: ref's `!sendHold.isEmpty(t)`, WordHold.isEmpty =
//               !occ && !pendPut), which the block sees on `bus`; none landed yet reads as not busy
//   router_ovf, we_in_ovf, gather_ovf, bad: Counts read through_last (endCycle(t) sees the increments of t)
// Its outputs:
//   regs        RegFile, read by the core at level 1: the view for t (see TileCtl::kView)
//   wake, irq   Wires to the core
//   dma         RegFile (2: DMA_CONFIG, the record base), read by the write engine, the AG and the gather engine
//               (write_engine.h): their configuration, from t + 1
//   line        Link(ready_latency - 2), width 1, to W9's row collector: bit 0 READY rose at the end of t, bit 1 the
//               error line's level after endCycle(t); put only in a cycle where READY rises or the error line changes
// Added with G3/G4: the AG's start pulse (a Wire written at level 0 of t + 1), the AG's and the gather's status (Regs),
// the read port (a Fifo(64) from the core, its free count), and the condition Counts of the table FIFOs, AG drift, the
// read port.
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "config.h"
#include "sim.h"
#include "units/chip.h"
#include "units/director.h"
#include "units/linkword.h"
#include "units/table_store.h"
#include "units/write_engine.h"
#include "units/regblock.h"
#include "units/regmap.h"
#include "units/rv32.h"

namespace units {

// ------------------------------------------------------------------------------------------------- the register block
struct TileCtl {
    // ---- inputs ----
    sim::LinkIn bus, go, landed, kline;
    sim::RegIn send_busy;
    sim::CountIn router_ovf, we_in_ovf, gather_ovf, bad;
    // ---- outputs ----
    sim::RegFileOut regs, dma;
    sim::WireOut wake, irq;
    sim::LinkOut line;
    RegBlockState s;

    // The registers the core reads through `regs` (indices: the register numbers): PARAMS .. SRAM_RD_DEST_ADDR, the
    // counters (their value at the start of t), ROUTER_HDR/ADDR and ROUTER_SET_W0-W2 (a send store at t builds its packet
    // from them as they stand at t).
    static constexpr unsigned kRegs = SRAM_RD_DEST_ADDR + 1;

    bool error_out = false;            // the error line's level as last put on `line`

    TileCtl(sim::Ports& p, const RegBlockState& initial) : s(initial) {
        p.in(bus, "bus", 2); p.in(go, "go", 0); p.in(landed, "landed", 0); p.in(kline, "kline", 1);
        p.in(send_busy, "send_busy", 1);
        p.in(router_ovf, "router_ovf"); p.in(we_in_ovf, "we_in_ovf"); p.in(gather_ovf, "gather_ovf"); p.in(bad, "bad");
        p.out(regs, "regs", kRegs); p.out(dma, "dma", DMA_REGS); p.out(wake, "wake"); p.out(irq, "irq");
        p.out(line, "line", 1);
    }
    // Stepped when something reaches it (and at the first cycle): endCycle(t - 1) from what reached it at t (the
    // shifted end of cycle, PORTS.md 7), then the view for t. A Count wakes nobody: a condition count that rises with
    // nothing else reaching the block is seen at its next step (never in M2's programs: no overflow, no bad packet).
    sim::Wake step(sim::Ctx& c) {
        RegBlockState::In in;
        if (c.ready(go)) { c.take(go); in.go = true; }
        if (c.ready(landed)) { c.take(landed); in.landed = true; }
        if (c.ready(kline)) { in.kline = uint32_t(c.peek(kline)[0]); c.take(kline); }
        bool send_put = false;
        if (c.ready(bus)) {
            const sim::Word* m = c.peek(bus);
            in.op = uint32_t(m[0] >> 32); in.arg = uint32_t(m[0]); in.v = uint32_t(m[1]);
            send_put = in.op == BUS_STORE && in.arg >= ROUTER_SEND_32 && in.arg <= ROUTER_SEND_128;
            c.take(bus);
        }
        in.send_busy = send_put || (c.valid(send_busy) && c.read(send_busy)[0] != 0);   // ref: !sendHold.isEmpty(t)
        in.router_ovf = c.through_last(router_ovf); in.bad = c.through_last(bad);
        in.we_in_ovf = c.through_last(we_in_ovf); in.gather_ovf = c.through_last(gather_ovf);
        s.end_cycle(c.now() - 1, in);
        for (unsigned r = 0; r < kRegs; r++) c.write(regs, r, s.reg(r));
        c.write(dma, DMA_R_CONFIG, s.dma_config);
        c.write(dma, DMA_R_RECORD_BASE, s.record_base);
        c.drive(wake, s.wake());
        c.drive(irq, s.irq());
        if (s.ready_pulse || s.error_line != error_out) {
            c.put(line, sim::Word(s.ready_pulse) | sim::Word(s.error_line) << 1);
            error_out = s.error_line;
        }
        return sim::Wake::none();
    }
    uint64_t digest() const { return digest_mix(s.digest(), error_out); }
};

// ------------------------------------------------------------------------------------------------ the core, M2 tile
// The core of an M2 tile (W5's Rv32 with M2's ports; W5's `Core` stays for M1's bench). Its memories live outside the
// unit (`dmem`, `imem` in Rv32: a unit is moved into an arena at build); port B's link words are applied before its
// step (ref: written at t + cm_latency, seen by a load at t + cm_latency + 1).
struct TileCore {
    sim::RegFileIn regs, hist;
    sim::WireIn wake, irq;
    sim::LinkIn port_b;                // width 3: link words from the director, Link(cm_latency + 1)
    sim::LinkOut bus;                  // width 2: the core's store, consume or bus error of t -> the block, Link(1)
    sim::HoldOut send;                 // width 3: the send holding register -> W9's injection unit
    sim::LinkOut tables, hist_w;       // width 2: table-window stores -> the table store, HIST (Link(1))
    Rv32 rv;

    TileCore(sim::Ports& p, const Rv32& initial) : rv(initial) {
        p.in(regs, "regs"); p.in(hist, "hist"); p.in(wake, "wake"); p.in(irq, "irq"); p.in(port_b, "port_b", 3);
        p.out(bus, "bus", 2); p.out(send, "send", 3); p.out(tables, "tables", 2); p.out(hist_w, "hist_w", 2);
    }
    struct Env {
        sim::Ctx& c; TileCore& u;
        uint32_t reg(unsigned r) const { return c.read(u.regs, r); }
        bool wake() const { return c.read(u.wake) != 0; }
        bool irq() const { return c.read(u.irq) != 0; }
        bool can_push() const { FATAL("tile core: REQUEST_SEND at cycle %llu: not modelled in M2 (AUDIT-M2 1.2 (C))", (unsigned long long)c.now()); }
        void post(uint32_t op, uint32_t arg, uint32_t v) { c.put(u.bus, sim::Word(op) << 32 | arg, sim::Word(v)); }
        void send(uint64_t lo, uint64_t hdr, uint64_t hi) { c.put(u.send, lo, hdr, hi); }
        bool send_refused() const { return c.refused(u.send); }
        void table_store(uint32_t table, uint32_t i, uint32_t v) {
            if (table == TBL_HIST) c.put(u.hist_w, sim::Word(i), sim::Word(v));
            else c.put(u.tables, sim::Word(table) << 32 | i, sim::Word(v));
        }
        uint32_t hist(uint32_t i) const { return c.read(u.hist, i); }
    };
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(port_b)) {
            if (rv.stop == Stop::running) apply_port_b(c.peek(port_b));
            c.take(port_b);
        }
        Env e{c, *this};
        uint64_t next = rv.step(e, c.now());
        return next == Rv32::kNever ? sim::Wake::none() : sim::Wake::at_cycle(next);
    }
    uint64_t digest() const { return digest_mix(rv.digest(), rv.pending_t ^ rv.send_w[0] ^ rv.send_w[1] << 1 ^ rv.send_w[2] << 2); }
    // ControllerMemories.runCycle: one naturally aligned 32-, 64- or 128-bit write at its offset (high bits ignored).
    void apply_port_b(const sim::Word* w) {
        uint64_t h = w[1];
        uint32_t a = lw::addr(h), n = lw::chunks(lw::size(h));
        uint32_t* m = lw::region(a) == lw::R_DMEM ? rv.dmem : rv.imem;
        uint32_t w0 = (lw::offset(a) & (MEM_BYTES - 1)) >> 2;
        for (uint32_t i = 0; i < n; i++) m[(w0 + i) & (MEM_WORDS - 1)] = uint32_t((i < 2 ? w[0] : w[2]) >> (32 * (i & 1)));
    }
};

// ---------------------------------------------------------------------------------------------- the core in an M2 tile
// W5's Core gains, in an M2 tile (TileConfig::m2): `send`, a HoldOut of width 3 to W9's injection unit (a send store at
// t puts the packet at t; refused at t + 1: retried, the core stalls); its `memw` input becomes port B, link words from
// the director (Link(cm_latency + 1)); `tables` and `hist_w`, Link(1)s of width 2 to the table store and HIST
// (table_store.h: a table-window store of t, {table << 32 | index, value}); `hist`, HIST's RegFile (a HIST load is one
// cycle, as ref's). A load of PTR, STREAM or GROUP is a bus error, as in ref. M1's request ports (req_busy, req_freed) are not declared in an M2 tile:
// REQUEST_* is fatal there, not modelled in M2 (AUDIT-M2 1.2 (C)).

// ------------------------------------------------------------------------------------- G1's part of the tile builder
// The configuration G1's units depend on (the D12 header's keys that reach them).
struct G1Config {
    unsigned tile = 0;
    Rv32Timing timing;                 // reg_load_latency, branch_penalty, mul_latency, ref_cycle
    bool done_gate = true;
    uint32_t mem_chunks = 1u << 20;
    uint32_t periph_present = 1;
    unsigned K = 8, cm_latency = 2;    // the write counters' line, director -> port B
};

// G1's part of a tile, as W9's tile builder meets it: chip.h's `ControlIO` (the ports agreed in NOTES.md, plus
// ROUTER_OVF), returned by a `ControlBuilder`.
// The units G1 adds for tile k (names "t<k>.core", "t<k>.ctl", "t<k>.dir", and the configuration parts of G3/G4's
// units it owns until they extend them: "t<k>.we", "t<k>.ag", "t<k>.gather", "t<k>.ts", "t<k>.hist"), wired to each
// other inside; the director's we-in and gather outputs go to the write engine's and the gather engine's inputs. `ctx`
// is a G1Ctx. A ControlBuilder (chip.h).
struct G1Ctx {
    G1Config cfg;                      // per tile: cfg.tile is overwritten with k
    const uint32_t* bootrom = nullptr; uint32_t bootrom_words = 0;
    uint32_t* mem = nullptr;           // 64 x 2 x MEM_WORDS words (data, program per tile), zero at power-on; outlives
                                       // the machine (the boot fills them through port B)
    unsigned we_in_depth = 4, gather_depth = 16;
};
ControlIO add_g1(sim::Config& cfg, unsigned k, void* ctx);
static_assert(std::is_same_v<decltype(&add_g1), ControlBuilder>);

}  // namespace units
