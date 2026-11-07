// RegBlockState: TileControl.endCycle's M1 subset (units/regblock.h): its order, the done gate, the consume, the request
// FIFO's count and edge, the view.
#include <cstdint>

#include "harness.h"
#include "units/regblock.h"

using namespace units;
using In = RegBlockState::In;

static In store(uint32_t r, uint32_t v) { In i; i.op = BUS_STORE; i.arg = r; i.v = v; return i; }

TEST(regblock_reset_view) {
    RegBlockState b;
    b.params = params_value(1, 9, 1u << 20);
    CHECK_EQ(b.reg(PARAMS), 0x78900001u);
    CHECK_EQ(b.reg(EVENTS_MASK), 0x180u);
    CHECK_EQ(b.reg(STATUS), 0u);
    CHECK(!b.wake() && !b.irq() && !b.busy());
}

TEST(regblock_go_and_landing) {
    RegBlockState b;
    In land; land.landed = true;
    b.end_cycle(1, land);
    CHECK_EQ(b.reg(STATUS), 1u << ST_LANDED);
    CHECK_EQ(b.reg(EVENTS), 1u << EV_COMMAND);
    CHECK(!b.wake());                                  // [9] not under the reset mask
    In both; both.go = true; both.landed = true;       // a landing survives a GO of the same cycle
    b.end_cycle(2, both);
    CHECK_EQ(b.reg(STATUS), 1u << ST_LANDED);
    CHECK_EQ(b.reg(EVENTS), 1u << EV_COMMAND | 1u << EV_GO);
    CHECK(b.wake());
    In go; go.go = true;                               // GO clears landed, not EVENTS [9]
    b.end_cycle(3, go);
    CHECK_EQ(b.reg(STATUS), 0u);
    CHECK_EQ(b.reg(EVENTS), 1u << EV_COMMAND | 1u << EV_GO);
}

TEST(regblock_done_gate) {
    RegBlockState b;
    b.end_cycle(1, store(EVENTS_MASK, 0x380));
    In land; land.landed = true;
    b.end_cycle(2, land);
    CHECK(b.wake());                                   // [9] under the mask
    b.end_cycle(3, store(PULSE_DONE, 1));
    CHECK(b.gate && b.done);
    CHECK(!b.wake());                                  // the gate: only GO wakes
    CHECK_EQ(b.reg(STATUS), 1u << ST_DONE | 1u << ST_LANDED);
    In go = store(PULSE_DONE, 1); go.go = true;        // GO and a done pulse at once: done and the gate stay set
    b.end_cycle(4, go);
    CHECK(b.gate && b.done);
    CHECK(b.wake());                                   // GO is pending
    In g2; g2.go = true;
    b.end_cycle(5, g2);
    CHECK(!b.gate && !b.done);
    CHECK_EQ(b.dones, 2u);
}

TEST(regblock_consume_clears_only_what_it_returned) {
    RegBlockState b;
    In go; go.go = true;
    b.end_cycle(1, go);
    In c; c.op = BUS_CONSUME; c.v = 1u << EV_GO; c.go = true;   // a GO at the consume's cycle: the clear beats the set
    b.end_cycle(2, c);
    CHECK_EQ(b.reg(EVENTS), 0u);
    b.end_cycle(3, go);
    In l; l.op = BUS_CONSUME; l.v = 1u << EV_GO; l.landed = true;
    b.end_cycle(4, l);
    CHECK_EQ(b.reg(EVENTS), 1u << EV_COMMAND);         // set during the cycle, not returned: survives
}

TEST(regblock_errors_and_interrupt) {
    RegBlockState b;
    In e; e.op = BUS_ERROR; e.arg = COND_BUS_ERROR;
    b.end_cycle(1, e);
    CHECK_EQ(b.reg(ERRORS), 1u << COND_BUS_ERROR);
    CHECK(b.irq());
    e.arg = COND_ILLEGAL;
    b.end_cycle(2, e);
    CHECK_EQ(b.reg(ERRORS), 1u << COND_BUS_ERROR | 1u << COND_ILLEGAL);
}

TEST(regblock_events_required) {
    RegBlockState b;
    b.end_cycle(1, store(EVENTS_MASK, 0));
    b.end_cycle(2, store(EVENTS_REQUIRED, 1u << EV_GO | 1u << EV_COMMAND));
    In go; go.go = true;
    b.end_cycle(3, go);
    CHECK(!b.wake());                                  // one of two required
    In l; l.landed = true;
    b.end_cycle(4, l);
    CHECK(b.wake());
}

TEST(regblock_request_fifo) {
    RegBlockState b;
    b.request_fifo_depth = 4;
    for (int i = 0; i < 4; i++) b.end_cycle(1 + i, store(REQUEST_SEND, 0));
    CHECK(b.busy());
    CHECK_EQ(b.reg(STATUS), 1u << ST_REQUEST_BUSY);
    In fp = store(REQUEST_SEND, 0); fp.freed = true;   // a free and a push in one cycle: still full, no room event
    b.end_cycle(5, fp);
    CHECK(b.busy());
    CHECK_EQ(b.reg(EVENTS), 0u);
    In f; f.freed = true;
    b.end_cycle(6, f);
    CHECK(!b.busy());
    CHECK_EQ(b.reg(EVENTS), 1u << EV_REQUEST_ROOM);    // busy's fall
    CHECK_EQ(b.sends, 5u);
    b.end_cycle(7, store(REQUEST_SET_W1, 9));
    CHECK_EQ(b.set_w[1], 9u);
    b.end_cycle(8, store(REQUEST_SEND, 0));
    CHECK_EQ(b.set_w[1], 0u);                          // cleared by the push
}

// D29 (W8): an overflow the router counted at t reaches the block as an event: ERRORS [0] at endCycle(t), the error line
// raised, with nothing else arriving; the Count alone is not read while the event is offered.
TEST(router_overflow_event_sets_errors_at_its_end_of_cycle) {
    RegBlockState b;
    RegBlockState::In none;
    b.end_cycle(1, none);
    CHECK_EQ(b.conditions, 0u);
    RegBlockState::In ev;
    ev.router_ovf_ev = true;
    b.end_cycle(2, ev);
    CHECK_EQ(b.conditions, 1u);
    CHECK(b.error_line);
    CHECK(b.irq());
}
