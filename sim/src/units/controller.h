// controller.h: one tile's controller on the framework (sim.h, config.h): sketch 2's three units, wrapping the plain
// logic of rv32.h and regblock.h, and the bench that replays a golden trace's MEMW, GO, CMD and REQ-FREED lines
// (sim/tests/AUDIT-M1.md section 5). Everything else in the trace comes out of the core and the block.
//
//   level 0  TestBench  acts at the cycles the golden's lines imply (MEMW, GO, CMD at C - 1; REQ-FREED at C)
//   level 0  RegBlock   TileControl.endCycle(t - 1) from what reached it at t (the shifted end of cycle), then the view
//                       for t: the register file, the wake term, the interrupt term, request busy
//   level 1  Core       Controller.runCycle(t), reading those at t; its store, consume or error reaches the block at t + 1
#pragma once

#include <cstdint>
#include <string>

#include "config.h"
#include "sim.h"
#include "units/regblock.h"
#include "units/rv32.h"
#include "units/trace_log.h"

namespace units {

using sim::Ctx;
using sim::Ports;
using sim::Wake;
using sim::Word;

// The configuration the controller's behaviour depends on (the D12 header's keys that reach it, AUDIT-M1 section 7).
struct TileConfig {
    unsigned tile = 0;
    Rv32Timing timing;                 // reg_load_latency, branch_penalty, mul_latency, ref_cycle
    bool done_gate = true;
    uint32_t request_fifo_depth = 128;
    uint32_t mem_chunks = 1u << 20;
    uint32_t periph_present = 1;       // PARAMS [2:0] (peripheral 1 on every M1 tile; its window not modelled)
};

// ---------------------------------------------------------------------------------------------------------------- core
struct Core {
    sim::RegFileIn regs;               // the block's view: indices are the register numbers (PARAMS .. EVENTS_REQUIRED)
    sim::WireIn wake, irq, req_busy;   // from the block
    sim::WireIn req_freed;             // from the bench: REQ-FREED at this cycle
    sim::LinkIn memw;                  // port-B writes of t - 1: {addr | n << 32, v0 | v1 << 32, v2 | v3 << 32}
    sim::LinkOut bus;                  // {op << 32 | arg, v}: a store, a consume or an error, applied by the block at t + 1
    Rv32 rv;

    Core(Ports& p, const Rv32& initial) : rv(initial) {
        p.in(regs, "regs"); p.in(wake, "wake"); p.in(irq, "irq"); p.in(req_busy, "req_busy"); p.in(req_freed, "req_freed");
        p.in(memw, "memw", 3);
        p.out(bus, "bus", 2);
    }

    struct Env {
        Ctx& c; Core& u;
        uint32_t reg(unsigned r) const { return c.read(u.regs, r); }
        bool wake() const { return c.read(u.wake) != 0; }
        bool irq() const { return c.read(u.irq) != 0; }
        bool can_push() const { return !c.read(u.req_busy) || c.read(u.req_freed); }
        void post(uint32_t op, uint32_t arg, uint32_t v) { c.put(u.bus, Word(op) << 32 | arg, Word(v)); }
    };

    Wake step(Ctx& c) {
        if (c.ready(memw)) {                                        // written at t - 1: seen by a load at t
            if (rv.stop == Stop::running) apply_memw(c.peek(memw));
            c.take(memw);
        }
        Env e{c, *this};
        uint64_t next = rv.step(e, c.now());
        return next == Rv32::kNever ? Wake::none() : Wake::at_cycle(next);
    }
    uint64_t digest() const { return rv.digest(); }
    void apply_memw(const Word* w) {
        uint32_t a = uint32_t(w[0]), n = uint32_t(w[0] >> 32);
        uint32_t* m = region(a) == R_PMEM ? rv.imem : rv.dmem;
        uint32_t w0 = (offset(a) & (MEM_BYTES - 1)) >> 2;
        for (uint32_t k = 0; k < n; k++) m[(w0 + k) & (MEM_WORDS - 1)] = uint32_t(w[1 + k / 2] >> (32 * (k & 1)));
    }
};

// ------------------------------------------------------------------------------------------------- the register block
struct RegBlock {
    sim::LinkIn bus, go, landed, freed;   // what happened at t - 1, delivered at t
    sim::RegFileOut regs;
    sim::WireOut wake, irq, req_busy;
    RegBlockState s;

    RegBlock(Ports& p, const RegBlockState& initial) : s(initial) {
        p.in(bus, "bus", 2); p.in(go, "go", 0); p.in(landed, "landed", 0); p.in(freed, "freed", 0);
        p.out(regs, "regs", EVENTS_REQUIRED + 1); p.out(wake, "wake"); p.out(irq, "irq"); p.out(req_busy, "req_busy");
    }

    // Stepped when something reaches it (and at the first cycle): endCycle(t - 1), then the view for t.
    Wake step(Ctx& c) {
        RegBlockState::In in;
        if (c.ready(go)) { c.take(go); in.go = true; }
        if (c.ready(landed)) { c.take(landed); in.landed = true; }
        if (c.ready(freed)) { c.take(freed); in.freed = true; }
        if (c.ready(bus)) {
            const Word* m = c.peek(bus);
            in.op = uint32_t(m[0] >> 32); in.arg = uint32_t(m[0]); in.v = uint32_t(m[1]);
            c.take(bus);
        }
        s.end_cycle(c.now() - 1, in);
        static constexpr unsigned kView[] = {PARAMS, STATUS, ERRORS, EVENTS, EVENTS_MASK, EVENTS_REQUIRED};
        for (unsigned r : kView) c.write(regs, r, s.reg(r));
        c.drive(wake, s.wake());
        c.drive(irq, s.irq());
        c.drive(req_busy, s.busy());
        return Wake::none();                                        // nothing changes until something arrives
    }
};

// ------------------------------------------------------------------------------------------------------ the test bench
// The golden's replayed lines as the bench's actions: `t` is the cycle it acts. MEMW, GO, CMD labelled C act at C - 1 (a
// cycle's MEMW words, one packet of consecutive addresses, are one put); REQ-FREED at C acts at C. Each is recorded in the
// trace as it is replayed, under its label.
struct BenchEvent {
    uint64_t t;           // the cycle the bench acts
    uint8_t kind;         // T_MEMW, T_GO, T_CMD, T_FREED
    uint32_t addr, n;     // MEMW: the first word's byte address, the word count (1-4)
    uint32_t v[4];
};

struct TestBench {
    sim::LinkOut memw, go, landed, freed;
    sim::WireOut req_freed;
    const BenchEvent* ev; uint32_t n, i = 0;
    TraceLog* log;
    bool freed_now = false;

    TestBench(Ports& p, const BenchEvent* events, uint32_t count, TraceLog* trace) : ev(events), n(count), log(trace) {
        if (n && ev[0].t < 1) FATAL("test bench: an event acting at cycle %llu, before the first cycle (1)", (unsigned long long)ev[0].t);
        p.out(memw, "memw", 3); p.out(go, "go", 0); p.out(landed, "landed", 0); p.out(freed, "freed", 0);
        p.out(req_freed, "req_freed");
    }
    // Stepped at its events' cycles, and the cycle after a REQ-FREED (to drive req_freed back to 0).
    Wake step(Ctx& c) {
        uint64_t t = c.now();
        if (freed_now) { c.drive(req_freed, 0); freed_now = false; }
        for (; i < n && ev[i].t == t; i++) {
            const BenchEvent& e = ev[i];
            switch (e.kind) {
            case T_MEMW:
                c.put(memw, Word(e.addr) | Word(e.n) << 32, Word(e.v[0]) | Word(e.v[1]) << 32, Word(e.v[2]) | Word(e.v[3]) << 32);
                for (uint32_t k = 0; k < e.n; k++) if (log) log->add(t + 1, T_MEMW, 0, e.addr + 4 * k, e.v[k]);
                break;
            case T_GO: c.put(go); if (log) log->add(t + 1, T_GO); break;
            case T_CMD: c.put(landed); if (log) log->add(t + 1, T_CMD); break;
            default: c.drive(req_freed, 1); c.put(freed); freed_now = true; if (log) log->add(t, T_FREED); break;
            }
        }
        if (freed_now) return c.next();
        return i < n ? Wake::at_cycle(ev[i].t) : Wake::none();
    }
    uint64_t digest() const { return digest_mix(digest_mix(n, i), freed_now); }
};

// ------------------------------------------------------------------------------------------------------ configuration
struct TileHandles { Core* core; RegBlock* block; TestBench* bench; };
// Adds tile `prefix`'s three units and wires them (levels from the wires: bench 0, block 0, core 1).
TileHandles configure_tile(sim::Config& cfg, const std::string& prefix, const Rv32& core, const RegBlockState& block,
                           const BenchEvent* events, uint32_t n_events, TraceLog* trace);

static_assert(sim::UnitType<Core> && sim::UnitType<RegBlock> && sim::UnitType<TestBench>);

}  // namespace units
