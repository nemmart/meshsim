// Sketch 2 (WORKER3, stage 1): the outline of the controller's surroundings against sim.h, compiled, not run. From
// PLAN-M1 item 8 and `ref` (Controller.java, TileControl.java, RequestPort.java); to be revised against W2's AUDIT.md
// when it lands. The interpreter (Controller.execute) is elided; what is here is every coupling between the core and the
// rest of its tile in M1, which is what tests the phases and the Wires.
//
// Three units, two levels:
//   level 0  TestBench  replays `ref`'s trace: command landed, GO, and the request FIFO's refusals (PLAN-M1 item 8)
//   level 0  RegBlock   TileControl's M1 subset, as the shifted end of cycle (sim.h): at t it does endCycle(t - 1) from
//                       what reached it at t, then drives the register view, the wake term and the interrupt term for t
//   level 1  Core       the RV32 core; reads those three Wires/RegFile at t; its stores and consuming loads reach the
//                       block at t + 1
//
// `ref`'s same-cycle couplings, and what each becomes:
//   ctl.read(t, a)        a block load issued at t samples at t + 1 (regLoadLatency 2) and returns the state at the end of t
//                         -> the core stalls at t, reads `regs` (RegFile) at t + 1: the block wrote it at level 0 of t + 1
//                         after doing endCycle(t)                                              (TileControl.java:8-13)
//   ctl.write(t, a, v)    a store at t acts at the end of t (endCycle step 2), seen from t + 1
//                         -> Link(1) `bus` to the block, applied in its step at t + 1, before the core reads
//                         (read-only and absent-window bus errors are decided by the core from the static map)
//   ctl.takeWake(t)       the wake term from the sticky events at the end of t - 1 and the live events at t
//                         -> Wire `wake`, driven by the block at level 0 (M1 has no live events on a mesh tile)
//   ctl.errorsAt(t) != 0  the interrupt term: conditions from endCycle(t - 1) | the slot's live errors at t
//                         -> Wire `irq`, driven by the block at level 0 (no slot in M1)
//   tile.req.canPush(t)   room, or an entry freed by the request bus in t (the bus runs first, Chip.java:215)
//                         -> Wire `req_free`, driven by the test bench from the trace in M1 (the bus itself in M2)
//   GO at goAt            visible to the tile at goAt only; endCycle(goAt) latches EVENTS [8]
//                         -> the bench puts an event on Link(1) `go` at goAt; the block latches it in endCycle(goAt) at goAt + 1
//   command landed at t   a director write covering slot 0 at t sets it in endCycle(t) step 3 -> the same, Link(1) `landed`
#include <cstdint>

#include "config.h"
#include "sim.h"

namespace sketch {
using namespace sim;

// The register map is static configuration data, shared read-only (Registers.java): which word is which register.
enum Reg : unsigned { PARAMS, STATUS, EVENTS, EVENTS_MASK, PULSE_DONE, COMMAND, ERRORS, REQUEST_SEND, SIM_EXIT, TRACE, N_REGS };
enum : unsigned { EV_GO = 8, EV_LANDED = 9 };
bool reg_of(uint32_t addr, Reg& r);                    // the block's decode (0xFFFFF800-0xFFFFF9FF); false: unmapped
bool reg_writable(Reg r);
bool reg_consuming(Reg r);                             // a load with a side effect at the end of its sampling cycle

// The core's messages to the block on `bus` (one per cycle at most: a core does one load or store a cycle).
enum : Word { BUS_STORE = 1, BUS_CONSUME = 2 };

// ---------------------------------------------------------------------------------------------------------------- core
struct Core {
    // ports
    RegFileIn regs;       // the block's register view for this cycle
    WireIn wake, irq;     // the wake term, the interrupt term
    WireIn req_free;      // a REQUEST_SEND store may complete this cycle
    LinkOut bus;          // stores and consuming loads to the block, latency 1
    // architectural state (Controller.java)
    uint32_t x[32] = {}, pc = 0x02000000, mstatus = 0, mie = 0;
    uint64_t mcycle = 0, last = ~0ull;               // `last`: the cycle this core last ran (catch-up); ~0: never
    uint64_t stall_until = 0;                        // a branch or multiply stall: the core sleeps until then
    bool in_wfi = false, halted = false;
    uint64_t load_wait_t = ~0ull; uint32_t load_wait_pc = 0;
    // statistics
    uint64_t instructions = 0, cycles_stalled = 0, cycles_wfi = 0, cycles_halted = 0, stall_request = 0, stall_reg_load = 0;

    explicit Core(Ports& p) {
        p.in(regs, "regs"); p.in(wake, "wake"); p.in(irq, "irq"); p.in(req_free, "req_free");
        p.out(bus, "bus", 2);
    }

    // Controller.runCycle. A core with nothing to do this cycle (halted, in WFI without a wake, in a branch or multiply
    // stall) is not stepped: the cycles it slept through are counted from `last` when it next runs, in the state it
    // slept in, so a 10^9-cycle WFI costs one wake. `ref` takes no interrupt during a stall (`interruptPending && stall ==
    // 0`), so sleeping through one loses nothing; a wire waking it early finds it still stalled.
    Wake step(Ctx& c) {
        uint64_t t = c.now(), gap = t - last - 1; last = t;
        mcycle += gap + 1;
        if (halted) { cycles_halted += gap + 1; return Wake::none(); }
        if (in_wfi) cycles_wfi += gap; else cycles_stalled += gap;
        if (t < stall_until) { cycles_stalled++; return Wake::at_cycle(stall_until); }
        if (interrupt_pending(c)) { in_wfi = false; trap_external(); return c.next(); }    // WFI included (4.11)
        if (in_wfi) {
            if (!c.read(wake)) { cycles_wfi++; return Wake::none(); }   // woken again by `wake` or `irq` changing
            in_wfi = false;
        }
        unsigned stall = execute(c, t);                  // 1 for a taken branch, 2 more for a multiply (ref's `stall`)
        if (halted) return Wake::none();
        if (stall) { stall_until = t + 1 + stall; return Wake::at_cycle(stall_until); }
        return c.next();
    }

    bool interrupt_pending(Ctx& c) { return (mstatus & 1u << 3) && (mie & 1u << 11) && c.read(irq) != 0; }
    void trap_external();                                // Controller.trap(CAUSE_EXTERNAL): CSRs, pc = mtvec
    unsigned execute(Ctx& c, uint64_t t);                // the interpreter (returns its stall); loads and stores call the two below

    // A word load from the block (Controller.load's register case and the 2-cycle rule, Controller.java:208-211).
    // Returns false while the load waits for its sampling cycle (the instruction is retried at t + 1).
    bool load_reg(Ctx& c, uint64_t t, uint32_t ipc, uint32_t ea, uint32_t& v, bool& bus_error) {
        Reg r;
        if (!reg_of(ea, r)) { bus_error = true; return true; }
        if (!(load_wait_t == t - 1 && load_wait_pc == ipc)) { load_wait_t = t; load_wait_pc = ipc; stall_reg_load++; return false; }
        v = c.read(regs, r);                              // the state at the end of t - 1: the block did endCycle(t - 1) at level 0
        if (reg_consuming(r)) c.put(bus, BUS_CONSUME << 32 | r, v);   // its side effect at the end of t (applied at t + 1)
        return true;
    }
    // A word store to the block (Controller.store's register case, Controller.java:226-229, 366-378). Returns false
    // while a REQUEST_SEND stalls (retried at t + 1).
    bool store_reg(Ctx& c, uint32_t ea, uint32_t v, bool& bus_error) {
        Reg r;
        if (!reg_of(ea, r) || !reg_writable(r)) { bus_error = true; return true; }
        if (r == REQUEST_SEND && !c.read(req_free)) { stall_request++; return false; }
        if (r == SIM_EXIT) halted = true;
        c.put(bus, BUS_STORE << 32 | r, v);              // acts at the end of t: the block applies it at t + 1
        return true;
    }
};

// ------------------------------------------------------------------------------------------------- the register block
struct RegBlock {
    LinkIn bus, go, landed;          // what happened at t - 1, delivered at t
    RegFileOut regs; WireOut wake, irq;
    // TileControl's state (M1 subset)
    bool done = false, landed_ = false, gate = false;
    uint32_t events = 0, wake_mask = 0, wake_required = 0, conditions = 0, command = 0, params = 0;

    explicit RegBlock(Ports& p, uint32_t params_value) : params(params_value) {
        p.in(bus, "bus", 2); p.in(go, "go", 0); p.in(landed, "landed", 0);
        p.out(regs, "regs", N_REGS); p.out(wake, "wake"); p.out(irq, "irq");
    }

    // Stepped at t when something reached it, and at the first cycle (every unit is): endCycle(t - 1), in TileControl's order, then
    // the view for t.
    Wake step(Ctx& c) {
        if (c.ready(go)) { c.take(go); events |= 1u << EV_GO; done = false; landed_ = false; gate = false; }   // step 1
        if (c.ready(bus)) {                                                                                    // steps 2, 4
            const Word* m = c.peek(bus); unsigned op = unsigned(m[0] >> 32), r = unsigned(m[0] & 0xFFFF); uint32_t v = uint32_t(m[1]);
            if (op == BUS_STORE) store(r, v); else if (r == EVENTS) events &= ~(v & wake_mask);
            c.take(bus);
        }
        if (c.ready(landed)) { c.take(landed); landed_ = true; events |= 1u << EV_LANDED; }                    // step 3
        // step 5 (errors, READY, STATUS) and the view for t:
        c.write(regs, EVENTS, events); c.write(regs, ERRORS, conditions); c.write(regs, PARAMS, params);
        c.write(regs, COMMAND, command); c.write(regs, EVENTS_MASK, wake_mask);
        c.drive(wake, wake_term());
        c.drive(irq, conditions != 0);
        return Wake::none();                                                     // nothing changes until something arrives
    }
    void store(unsigned r, uint32_t v);                  // TileControl.write's M1 registers (PULSE_DONE, EVENTS_MASK, ...)
    bool wake_term() const {                             // TileControl.wakeTerm (4.12)
        if (gate) return events >> EV_GO & 1;
        return (events & wake_mask & ~wake_required) != 0 || (wake_required != 0 && (events & wake_required) == wake_required);
    }
};

// ------------------------------------------------------------------------------------------------------ the test bench
struct TestBench {
    LinkOut go, landed; WireOut req_free;
    // `ref`'s events for this run, from the golden trace (sorted by cycle; configuration data, read-only in the loop)
    struct Event { uint64_t t; uint8_t kind; };          // 0 landed, 1 GO, 2 a REQUEST_SEND refused at t
    const Event* ev; uint32_t n, i = 0;

    TestBench(Ports& p, const Event* events, uint32_t count) : ev(events), n(count) {
        p.out(go, "go", 0); p.out(landed, "landed", 0); p.out(req_free, "req_free", 1);   // reset value 1: the FIFO has room
    }
    // Stepped only at its events' cycles, and the cycle after a refusal to drive req_free back.
    bool refused_now = false;
    Wake step(Ctx& c) {
        uint64_t t = c.now();
        if (refused_now) { c.drive(req_free, 1); refused_now = false; }
        for (; i < n && ev[i].t == t; i++) {
            if (ev[i].kind == 0) c.put(landed);
            else if (ev[i].kind == 1) c.put(go);
            else { c.drive(req_free, 0); refused_now = true; }
        }
        if (refused_now) return c.next();
        return i < n ? Wake::at_cycle(ev[i].t) : Wake::none();
    }
};

// ----------------------------------------------------------------------------------------------------- configuration
void configure_tile(Config& cfg, const TestBench::Event* events, uint32_t n, uint32_t params) {
    auto& bench = cfg.add<TestBench>("t0.bench", events, n);
    auto& block = cfg.add<RegBlock>("t0.ctl", params);
    auto& core = cfg.add<Core>("t0.core");
    cfg.link(core.bus, block.bus, 1);
    cfg.link(bench.go, block.go, 1);
    cfg.link(bench.landed, block.landed, 1);
    cfg.connect(block.regs, core.regs);
    cfg.connect(block.wake, core.wake);
    cfg.connect(block.irq, core.irq);
    cfg.connect(bench.req_free, core.req_free);
    // levels: bench 0, block 0, core 1 (from the wires); no loop: core -> block is a Link
}

}  // namespace sketch
