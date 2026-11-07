// Sketch 2 (WORKER3, stage 1): the outline of the controller's surroundings against sim.h, compiled, not run. Revised
// against W2's coupling audit (sim/tests/AUDIT-M1.md, "AUDIT" below) and sim/tests/TRACE-FORMAT.md ("TRACE"). The
// interpreter (Controller.execute) is elided; what is here is every coupling between the core and the rest of its tile in
// M1, with the cycle each takes effect, which is what tests the phases and the Wires.
//
// Three units, two levels:
//   level 0  TestBench  replays the golden trace's MEMW, GO, CMD and REQ-FREED lines, nothing else (AUDIT 5)
//   level 0  RegBlock   TileControl's M1 subset and the request FIFO's count (RequestPort), as the shifted end of cycle
//                       (sim.h): its step at t does endCycle(t - 1) from what reached it at t, then drives the register
//                       view, the wake term, the interrupt term and request busy for t
//   level 1  Core       Controller.runCycle; reads those Wires and the RegFile at t; its stores, consuming loads and
//                       errors reach the block at t + 1
//
// `ref`'s couplings (AUDIT 0-5) and what each becomes:
//   a block store at t       effect at the end of t (endCycle(t), step 2)
//                            -> Link(1) `bus` to the block, applied by its step at t + 1 before the core runs
//   a block load at t        samples at t + 1, returns the state at the end of t (+ live bits, 0 in M1); EVENTS consumes
//                            at the end of t + 1 -> the core waits one cycle, reads `regs` at t + 1 (the block did
//                            endCycle(t) at level 0 of t + 1); the consume goes on `bus` at t + 1
//   ctl.errorsAt(t)          conditions from endCycle(t - 1): an error the core raises at t is seen at t + 1 (AUDIT 1)
//                            -> the error goes on `bus` at t (an event, not a Count: it must step the block, PORTS.md 8);
//                            the block sets the condition in endCycle(t) at t + 1 and drives `irq`; the core sees it at t + 1
//   ctl.takeWake(t)          EVENTS' sticky bits at the end of t - 1, the mask, the gate -> Wire `wake` from the block
//   tile.req.canPush(t)      size < 128 || freedAt == t, the bus running first (AUDIT 4)
//                            -> Wires `req_busy` (the block: the count at the end of t - 1 >= depth, STATUS [6]) and
//                            `req_freed` (the bench: REQ-FREED at t); canPush = !req_busy || req_freed. The push at t
//                            reaches the block on `bus` (the store) and counts at the end of t, after t's free
//   a port-B write at C - 1  (MEMW C) seen by a load at C: memories return the contents as at the start of a cycle and
//                            commit writes at its end -> the bench puts at C - 1 on Link(1) `memw`; delivered at C, the
//                            core applies it at the start of its step at C, before it executes (AUDIT 0, TRACE)
//   GO, CMD at C             reached the tile at C - 1, latched by endCycle(C - 1) -> the bench puts at C - 1 on Link(1)
//                            `go` / `landed`; the block latches them at C, in endCycle's order (go, the store, the landing)
//   REQ-REFUSED              not replayed: the core writes its own, and the comparison checks it line for line
#include <cstdint>

#include "base/fatal.h"
#include "config.h"
#include "sim.h"

namespace sketch {
using namespace sim;

// The register map is static configuration data, shared read-only (Registers.java).
enum Reg : unsigned { PARAMS, STATUS, ERRORS, PULSE_DONE, EVENTS, EVENTS_PEEK, EVENTS_MASK, REQUEST_SET_W0, REQUEST_SET_W1,
                      REQUEST_SET_W2, REQUEST_SEND, SIM_EXIT, N_REGS };
enum : unsigned { EV_COUNTERS_DONE = 7, EV_GO = 8, EV_LANDED = 9, EV_REQUEST_ROOM = 11 };
enum : unsigned { ST_DONE = 4, ST_LANDED = 5, ST_REQUEST_BUSY = 6 };
enum : unsigned { COND_BUS_ERROR = 9, COND_ILLEGAL = 10 };    // ERRORS bits
bool reg_of(uint32_t addr, Reg& r);                    // the decode of 0xFFFFF800-0xFFFFF9FF; false: unmapped or reserved
bool reg_writable(Reg r);                              // false: a store is a bus error (STATUS, ERRORS, EVENTS, PARAMS)

// What the core tells the block on `bus`: at most one a cycle (one instruction a cycle; a faulting access has no effect).
enum : Word { BUS_STORE = 1, BUS_CONSUME = 2, BUS_ERROR = 3 };
inline Word bus_op(Word op, unsigned arg) { return op << 32 | arg; }

// ---------------------------------------------------------------------------------------------------------------- core
struct Core {
    // ports
    RegFileIn regs;                  // the block's register view for this cycle
    WireIn wake, irq, req_busy;      // the wake term, the interrupt term, STATUS [6] (from the block)
    WireIn req_freed;                // REQ-FREED at this cycle (from the bench)
    LinkIn memw;                     // port-B writes of t - 1: {addr | n << 32, v0 | v1 << 32, v2 | v3 << 32}
    LinkOut bus;                     // stores, consuming loads and errors to the block, latency 1
    // architectural state (Controller.java)
    uint32_t x[32] = {}, pc = 0x02000000, mstatus = 0, mie = 0;
    uint32_t* dmem; uint32_t* imem;                  // the memories (permanent state, alloc_big, loaded before cycle 1)
    uint64_t mcycle = 0, last = 0;                   // `last`: the cycle this core last ran (the first cycle is 1)
    uint64_t stall_until = 0;                        // a branch or multiply stall: the core sleeps until then
    bool in_wfi = false, halted = false;
    uint64_t load_wait_t = 0; uint32_t load_wait_pc = 0;
    // statistics
    uint64_t instructions = 0, cycles_stalled = 0, cycles_wfi = 0, cycles_halted = 0, stall_request = 0, stall_reg_load = 0;

    Core(Ports& p, uint32_t* data_memory, uint32_t* program_memory) : dmem(data_memory), imem(program_memory) {
        p.in(regs, "regs"); p.in(wake, "wake"); p.in(irq, "irq"); p.in(req_busy, "req_busy"); p.in(req_freed, "req_freed");
        p.in(memw, "memw", 3);
        p.out(bus, "bus", 2);
    }

    // Controller.runCycle in AUDIT 0's order. A core with nothing to do (halted, in WFI without a wake, in a branch or
    // multiply stall) is not stepped; the cycles it slept through are counted from `last`, in the state it slept in, so a
    // 10^9-cycle WFI costs one wake. A step it didn't ask for (a port-B write, a wire waking it early) changes nothing
    // else: the step-all check holds.
    Wake step(Ctx& c) {
        uint64_t t = c.now(), gap = t - last - 1; last = t;
        if (c.ready(memw)) { apply_memw(c.peek(memw)); c.take(memw); }          // written at t - 1: seen by a load at t
        mcycle += gap + 1;                                                          // 1. every cycle, halted included
        if (halted) { cycles_halted += gap + 1; return Wake::none(); }             // 2.
        if (in_wfi) cycles_wfi += gap; else cycles_stalled += gap;
        if (t < stall_until) { cycles_stalled++; return Wake::at_cycle(stall_until); }   // 6. (4. waits for the stall)
        if (interrupt_pending(c)) { in_wfi = false; trap_external(); return c.next(); }   // 4. WFI included, no WAKE line
        if (in_wfi) {                                                               // 5.
            if (!c.read(wake)) { cycles_wfi++; return Wake::none(); }              // woken again by `wake` or `irq`
            in_wfi = false;                                                         // the WAKE line; go on in this cycle
        }
        unsigned stall = execute(c, t);                                             // 7. returns ref's `stall`
        if (halted) return Wake::none();
        if (stall) { stall_until = t + 1 + stall; return Wake::at_cycle(stall_until); }
        return c.next();
    }
    // A register-block load's first cycle and a refused REQUEST_SEND are not `stall`: the instruction is re-executed at
    // t + 1 after step 4, so an interrupt pending then preempts it with mepc = that instruction (AUDIT 1). Here: execute
    // returns 0 without retiring, pc unchanged, and the core steps at t + 1.

    bool interrupt_pending(Ctx& c) { return (mstatus & 1u << 3) && (mie & 1u << 11) && c.read(irq) != 0; }
    void trap_external();                                // Controller.trap(CAUSE_EXTERNAL): CSRs, pc = mtvec & ~3
    unsigned execute(Ctx& c, uint64_t t);                // the interpreter; register accesses call the three below

    void apply_memw(const Word* w) {
        uint32_t a = uint32_t(w[0]); unsigned n = unsigned(w[0] >> 32);
        for (unsigned k = 0; k < n; k++) {
            uint32_t v = uint32_t(w[1 + k / 2] >> (32 * (k & 1)));
            uint32_t* m = (a >> 24) == 0x01 ? imem : dmem;
            m[((a & 0x7FFF) >> 2) + k] = v;
        }
    }
    // A word load from the block: false while it waits for its sampling cycle (the first returns with nothing done).
    bool load_reg(Ctx& c, uint64_t t, uint32_t ipc, uint32_t ea, uint32_t& v, bool& bus_error) {
        Reg r;
        if (!reg_of(ea, r)) { bus_error = true; return true; }
        if (!(load_wait_t == t - 1 && load_wait_pc == ipc)) { load_wait_t = t; load_wait_pc = ipc; stall_reg_load++; return false; }
        v = c.read(regs, r);                              // the state at the end of t - 1
        if (r == EVENTS) c.put(bus, bus_op(BUS_CONSUME, r), v);   // the clear at the end of t: the bits it returned
        return true;
    }
    // A word store to the block: false while a REQUEST_SEND is refused (REQ-REFUSED at t; re-executed at t + 1).
    bool store_reg(Ctx& c, uint32_t ea, uint32_t v, bool& bus_error) {
        Reg r;
        if (!reg_of(ea, r) || !reg_writable(r)) { bus_error = true; return true; }
        if (r == REQUEST_SEND && c.read(req_busy) && !c.read(req_freed)) { stall_request++; return false; }   // !canPush
        if (r == SIM_EXIT) halted = true;                 // at t, inside the store (HALT 5 after its retire line)
        c.put(bus, bus_op(BUS_STORE, r), v);              // effect at the end of t: the block applies it at t + 1
        return true;
    }
    // A bus error or an illegal instruction at t (the core traps at t itself): ERRORS from t + 1.
    void raise(Ctx& c, unsigned cond) { c.put(bus, bus_op(BUS_ERROR, cond), 0); }
};

// ------------------------------------------------------------------------------------------------- the register block
struct RegBlock {
    LinkIn bus, go, landed, freed;   // what happened at t - 1, delivered at t
    RegFileOut regs; WireOut wake, irq, req_busy;
    // TileControl's state (M1 subset) and the request FIFO's count (RequestPort)
    bool done = false, landed_ = false, gate = false, busy_prev = false;
    uint32_t events = 0, mask = 0x180, conditions = 0, params;
    uint32_t req_n = 0, req_depth;

    RegBlock(Ports& p, uint32_t params_value, uint32_t request_fifo_depth) : params(params_value), req_depth(request_fifo_depth) {
        p.in(bus, "bus", 2); p.in(go, "go", 0); p.in(landed, "landed", 0); p.in(freed, "freed", 0);
        p.out(regs, "regs", N_REGS); p.out(wake, "wake"); p.out(irq, "irq"); p.out(req_busy, "req_busy");
    }

    // Stepped at t when something reached it (and at the first cycle): endCycle(t - 1) in TileControl's order
    // (TileControl.java:227-258; AUDIT 3's sequencing), then the view for t.
    Wake step(Ctx& c) {
        if (c.ready(go)) { c.take(go); done = landed_ = gate = false; events |= 1u << EV_GO; }   // 1. GO
        if (c.ready(freed)) { c.take(freed); req_n--; }                 // the bus freed the head (before the store's push)
        uint32_t consume = 0;
        if (c.ready(bus)) {
            const Word* m = c.peek(bus); unsigned op = unsigned(m[0] >> 32), arg = unsigned(m[0] & 0xFFFF);
            uint32_t v = uint32_t(m[1]);
            if (op == BUS_STORE) store(Reg(arg), v);                    // 2. the store (PULSE_DONE after GO: done and the gate stay set)
            else if (op == BUS_ERROR) conditions |= 1u << arg;          // 4. the new condition
            else consume = v;                                           // 5. below
            c.take(bus);
        }
        if (c.ready(landed)) { c.take(landed); landed_ = true; events |= 1u << EV_LANDED; }   // 3. survives a GO of the same cycle
        // READY's edge (done && landed rising) goes to C&C: not in M1's bench (its effect returns as a replayed GO).
        events &= ~(consume & mask);                                    // 5. only the bits it returned (a set at t survives)
        bool busy = req_n >= req_depth;
        if (!busy && busy_prev) events |= 1u << EV_REQUEST_ROOM;        //    the edge events
        busy_prev = busy;
        // 6. the view for t (STATUS for t; EVENTS as at the end of t - 1, live bits 0 in M1)
        uint32_t status = uint32_t(done) << ST_DONE | uint32_t(landed_) << ST_LANDED | uint32_t(busy) << ST_REQUEST_BUSY;
        c.write(regs, PARAMS, params); c.write(regs, STATUS, status); c.write(regs, ERRORS, conditions);
        c.write(regs, EVENTS, events); c.write(regs, EVENTS_PEEK, events); c.write(regs, EVENTS_MASK, mask);
        c.drive(wake, wake_term());
        c.drive(irq, conditions != 0);
        c.drive(req_busy, busy);
        return Wake::none();                                            // nothing changes until something arrives
    }
    void store(Reg r, uint32_t v) {
        if (r == PULSE_DONE && (v & 1)) { done = true; gate = true; events &= ~(0x7Fu | 1u << EV_COUNTERS_DONE); }
        else if (r == EVENTS_MASK) mask = v & 0xFFFF;
        else if (r == REQUEST_SEND) req_n++;                            // the push, at the end of its cycle (W0-W2 cleared)
    }
    bool wake_term() const {                                            // TileControl.wakeTerm (4.12); EVENTS_REQUIRED 0 in M1
        if (gate) return events >> EV_GO & 1;
        return (events & mask) != 0;
    }
};

// ------------------------------------------------------------------------------------------------------ the test bench
struct TestBench {
    LinkOut memw, go, landed, freed; WireOut req_freed;
    // The golden trace's MEMW, GO, CMD and REQ-FREED lines (configuration data, read-only in the loop), as the bench's own
    // actions: `t` is the cycle the bench acts. A trace line at C for MEMW, GO or CMD ("seen from C") is put at C - 1; a
    // MEMW cycle's words (one packet, consecutive addresses, at most four) are one put. REQ-FREED at C is acted at C.
    // The first cycle is 1 and `ref` latches nothing before endCycle(1), so no event needs a put before it; configuration
    // refuses one that would (fatal, naming the line).
    struct Event { uint64_t t; uint8_t kind; Word w[3]; };   // 0 MEMW (w: the packed words), 1 GO, 2 CMD, 3 REQ-FREED
    const Event* ev; uint32_t n, i = 0;
    bool freed_now = false;

    TestBench(Ports& p, const Event* events, uint32_t count) : ev(events), n(count) {
        if (n && ev[0].t < 1) FATAL("test bench: an event at cycle %llu, before the first cycle (1)", (unsigned long long)ev[0].t);
        p.out(memw, "memw", 3); p.out(go, "go", 0); p.out(landed, "landed", 0); p.out(freed, "freed", 0);
        p.out(req_freed, "req_freed");
    }
    // Stepped at its events' cycles, and the cycle after a REQ-FREED (to drive req_freed back to 0).
    Wake step(Ctx& c) {
        uint64_t t = c.now();
        if (freed_now) { c.drive(req_freed, 0); freed_now = false; }
        for (; i < n && ev[i].t == t; i++) {
            const Event& e = ev[i];
            if (e.kind == 0) c.put(memw, e.w[0], e.w[1], e.w[2]);
            else if (e.kind == 1) c.put(go);
            else if (e.kind == 2) c.put(landed);
            else { c.drive(req_freed, 1); c.put(freed); freed_now = true; }
        }
        if (freed_now) return c.next();
        return i < n ? Wake::at_cycle(ev[i].t) : Wake::none();
    }
};

// ----------------------------------------------------------------------------------------------------- configuration
// `events` is built from the golden trace at configuration (any C++); the header's values are checked first (D12).
void configure_tile(Config& cfg, const TestBench::Event* events, uint32_t n, uint32_t params, uint32_t request_fifo_depth,
                    uint32_t* dmem, uint32_t* imem) {
    auto& bench = cfg.add<TestBench>("t0.bench", events, n);
    auto& block = cfg.add<RegBlock>("t0.ctl", params, request_fifo_depth);
    auto& core = cfg.add<Core>("t0.core", dmem, imem);
    cfg.link(core.bus, block.bus, 1);
    cfg.link(bench.memw, core.memw, 1);
    cfg.link(bench.go, block.go, 1);
    cfg.link(bench.landed, block.landed, 1);
    cfg.link(bench.freed, block.freed, 1);
    cfg.connect(block.regs, core.regs);
    cfg.connect(block.wake, core.wake);
    cfg.connect(block.irq, core.irq);
    cfg.connect(block.req_busy, core.req_busy);
    cfg.connect(bench.req_freed, core.req_freed);
    // levels from the wires: bench 0, block 0, core 1; no loop (everything into the block or the bench is a Link)
}

static_assert(UnitType<Core> && UnitType<RegBlock> && UnitType<TestBench>);

}  // namespace sketch
