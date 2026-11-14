// tracer_block.h: the Tracer at the block level (v1.36): the unit alone, with its neighbours each from its contract at
// fixed timing, as the RTL's acceptance runs it (Snarky One's rtl/blocks/tracer_unit/test/src/tracer_acceptance_test.rs,
// whose bench this is, rule for rule, so that a run's cycles here are that test's):
//   - the core, an ideal controller: exactly one register access a cycle or none; the tables through part 0, once; the
//     hash's constants; a segment's record, words 0-67 in order; RUN_CONFIG; then a run: START, and a consuming load of
//     EVENTS in every cycle it makes no other access. An advice_credit releases the next 128 entries of the segment's
//     advice; a new_expansion is answered by loads of EXPANSION_OPCODE and EXPANSION_OFFSET, for an inline an
//     EXPANSION_TEMPLATE store, and its words from the offset, 128 an expansion_credit; once state_ready is up, the
//     length words and output queue 0 in 17.9.10's four transfers (1, 1, 65 and 1 entries), then output queues 1-3 a
//     credit at a time, each stream in turn; done; then twelve loads: STATUS, ERRORS, EVENTS_PEEK, ROWS_RUN and the
//     eight statistics counters;
//   - the receive unit's write port: one WRITE a cycle, an expansion word first;
//   - the peripheral's DMA (Snarky One's peripheral_dma.veryl): a transfer started by DMA_START in cycle p takes an
//     entry a cycle from p + 1 while its queue holds one, ends the cycle after its last, and posts dma_done then;
//   - the router: always ready;
//   - the memory system on the fill port: the Tracer's MemSystem, at fixed latencies or its own model's (tracer.h).
// A cycle here is the RTL bench's: cycle n is the n-th after the reset, cycle 0 the EVENTS_MASK store.
//
// The control block's words (17.1) and the DMA's (17.4) are the bench's own here, by name: the Tracer's place in the
// peripheral slot is periph_slot's.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "units/replay_tables.h"
#include "units/tracer.h"

namespace tracer_block {

using units::tracer::ReplayTables;
using units::tracer::Tracer;

// What a run gave, and when.
struct RunOut {
    bool done = false;                 // false: the Tracer stopped on an error, or the cycle limit
    int64_t start_store = 0;           // the cycle of START's store
    int64_t first_row_start = -1, last_row_done = -1;
    int64_t state_ready_load = -1;     // the cycle of the EVENTS load that first showed state_ready
    int64_t done_load = -1;            // the cycle of the EVENTS load that showed done
    int64_t rows_cycles = 0;           // state_ready_load - start_store - 1: "from START to the rows' end"
    int64_t drain_cycles = 0;          // done_load - state_ready_load + 1
    uint32_t status = 0, errors = 0, events_peek = 0, rows_run = 0;
    uint32_t stat[Tracer::STAT_N] = {};   // words 44-51, loaded after done: cumulative over the segment
    uint32_t lengths[3] = {};
    std::vector<uint64_t> queue[4];    // each output queue's entries as the DMA sent them, {lo, hi} an entry
    std::vector<uint32_t> post_opcode, post_offset;   // each new expansion's words 35 and 34
};

class Bench {
public:
    static constexpr int SRC = 66;
    // block 0, the control block's words (17.1); block 1, the DMA's (17.4): the bench's own registers
    enum : int { B_CONTROL = 0, B_DMA = 1, B_TWO = 2, B_THREE = 3 };
    enum : int { C_STATUS = 1, C_ERRORS = 2, C_EVENTS = 3, C_EVENTS_PEEK = 4, C_EVENTS_MASK = 5, C_START = 6 };
    enum : int { D_HDR = 0, D_DEST_ADDR = 1, D_START = 2 };
    static constexpr uint32_t EVENTS_COUNTED = 0x1F;

    Tracer& tr;
    const ReplayTables& tb;
    int64_t cycle = 0;
    int64_t limit_per_run = 0;         // 0: the RTL bench's limit

    Bench(Tracer& t, const ReplayTables& tables) : tr(t), tb(tables) {}

    // The reset's first cycles: EVENTS_MASK, the tables, the hash's constants.
    void setup(uint32_t hash_mul, uint32_t hash_step, uint32_t hash_lines);
    // A segment's start: the record's 68 words (136 uint64_t, lo then hi), RUN_CONFIG, the advice the state write's
    // credits release.
    void segment(const uint64_t* record, const std::vector<uint64_t>& advice, uint32_t run_config);
    // A run: START, the controller's loop to done, the twelve loads.
    void run(RunOut& out);

    // one cycle with a load, a store, or no access
    uint32_t load(int block, int word);
    void store(int block, int word, uint32_t wdata);
    void idle_cycle();
    void flush();
    // a WRITE for the port, behind those queued
    void write(uint32_t offset, uint64_t lo, uint64_t hi) { writes_.push_back({offset, lo, hi}); }

private:
    struct Write { uint32_t offset; uint64_t lo, hi; };
    struct Access { int kind = 0, block = 0, word = 0; uint32_t wdata = 0; };   // kind: 0 none, 1 load, 2 store
    uint32_t step(const Access& a);
    void release_advice();
    void release_expansion();
    void start_transfer(int queue, uint32_t count);

    std::deque<Write> writes_, expansion_writes_;
    std::deque<uint64_t> advice_;
    std::deque<Write> expansion_;
    uint32_t events_mask_ = 0;
    int dma_done_ = 0;                 // EVENTS.dma_done, counted to 1
    // the DMA (peripheral_dma.veryl)
    uint32_t dma_hdr_ = 0;
    bool dma_busy_ = false;
    int dma_queue_ = 0;
    uint32_t dma_pending_ = 0;
    bool transfer_ = false;            // the controller's: a transfer started and its dma_done not yet seen
    std::vector<uint64_t>* received_ = nullptr;
};

}  // namespace tracer_block
