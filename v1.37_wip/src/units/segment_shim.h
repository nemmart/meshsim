// segment_shim.h: v1.37's segment queue as merged (WORKER3, WORKER4), standing in front of the Updater's controller as
// a source on its word port (updater_wip, WORKER6; UPDATER-INTERFACE.md, "In v1.37"), so that the master tile's firmware
// runs on the controller unchanged. **It goes when Ingress's forward count drives the port** (WORKER7): deleting this
// file and its lines in updater_top.h is all it takes. The controller knows nothing of it.
//
// What it is, the black box's segment queue exactly: region part 1, 512 entries of 128 bits (8 KB), filled by WRITEs of
// 128 bits; a credit (EVENTS [2] segment_credit, counted to depth 2 by the control block) returned each time 256 entries
// have left since the last return; a push into a full queue dropped, and ERRORS.queue_overrun set (sticky). Only the
// reset empties it.
//
// Its port, as word_port.h's source: `valid` and `data` are registers, the queue's head; a word leaves in a cycle in
// which `valid` and the controller's `ready` are both 1. The head register is one of the 512.
#pragma once

#include <cstdint>

#include "units/word_port.h"

namespace units {

struct SegmentShim {
    static constexpr unsigned SIZE = 512, HALF = 256;
    uint64_t mem[SIZE][2] = {};
    unsigned head = 0, n = 0, left = 0;
    bool overrun = false;                          // ERRORS.queue_overrun, its part (sticky)
    bool credit = false;                           // the credit pulse: a register, posted by the control block
    wordport::SourceOut o;                         // the port's registers: the head, when there is one
    uint64_t pushed = 0, dropped = 0, popped = 0;

    void reset() { head = n = left = 0; overrun = credit = false; o = wordport::SourceOut{}; }
    // A WRITE into region part 1, as it lands.
    void push(uint64_t lo, uint64_t hi) {
        if (n == SIZE) { overrun = true; dropped++; return; }
        const unsigned at = (head + n) % SIZE;
        mem[at][0] = lo; mem[at][1] = hi;
        n++; pushed++;
    }
    wordport::SourceOut out() const { return o; }
    // The edge that ends a cycle, with the sink's outputs of the cycle.
    void edge(const wordport::SinkOut& k) {
        credit = false;
        if (wordport::moves(o, k)) {
            head = (head + 1) % SIZE; n--; popped++;
            if (++left == HALF) { left = 0; credit = true; }
        }
        o.valid = n > 0;
        if (o.valid) { o.data[0] = mem[head][0]; o.data[1] = mem[head][1]; }
    }
    // Nothing to do at an edge: the head register shows the queue as it stands, and no pulse is out.
    bool quiet() const { return !credit && o.valid == (n > 0) && (n == 0 || (o.data[0] == mem[head][0] && o.data[1] == mem[head][1])); }
};

}  // namespace units
