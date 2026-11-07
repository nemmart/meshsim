// cnc.h (WORKER9, G2): C&C 1's Java model (ref's CnC.java and Cnc1Peripheral.java: the sequencer, the endpoint's send,
// READY, GO's net, the error bit, the receive FIFO), the row collectors that bring the tiles' READY and error lines to
// it, and C&C 2 and 3's endpoints (Endpoint.java). AUDIT-M2 sections 0 and 4. Declarations; the steps come with stage 3.
//
// EVENT-DRIVEN (D23). Nothing here scans the tiles. C&C steps only when a step of its program acts, a wait ends, a READY
// or error-line event arrives, its send was refused, or a packet reaches its receive FIFO.
//
// THE ROW COLLECTORS. A unit has at most 64 ready bits (sim.h), so 64 READY lines plus 64 error lines can't be one unit's
// inputs: one collector per mesh row takes its 8 tiles' lines and forwards a word per cycle to C&C. (The mask's width
// forces this; M3's larger fan-ins will meet the same limit.) Latencies (agreed with W8): the register block puts a line
// event at level 0 of t + 1 (its endCycle(t)) with latency ready_latency - 2; the collector forwards it at once with
// latency 1; C&C sees it at t + ready_latency, as ref's `readyAt = edge + ready_latency`. The error line uses the same
// links, so M2 requires error_latency == ready_latency (fatal otherwise: not modelled in M2).
//
//   line word (register block -> collector, width 1):  bit 0 READY's edge at t; bit 1 the error line changed at t;
//                                                      bit 2 its new level.
//   row word (collector -> C&C, width 1):              [7:0] READY edges, [15:8] error changes, [23:16] levels, of the
//                                                      row's tiles 8r .. 8r + 7.
//
// GO'S NET: C&C's GO store at t reaches tile k's register block by a width-0 Link of latency go_latency + skew[k] + 1
// (the tile sees GO at t + go_latency + skew[k]; the block handles that cycle's endCycle one cycle later).
//
// THE PROGRAM. ref's harness queues CnC steps (`loadWords`, `command`, `go`, `runWhenReady`, `atAllReady`, `waitCycles`,
// `call`); the C++ harness builds the same list at configuration with `CncProgram`, keeping ref's step granularity (the
// queue's length is in the fingerprint: a `send` of many packets is one step; `command` is an instant mark and one send;
// `go` is a step and, with cnc_go_settle, a settle step; `atAllReady` a wait for all READY and an instant call). A
// harness that queues more steps later (SortTest: the sort's commands after the boot has run) marks a release point;
// steps past it are not yet in the queue (nor in the fingerprint) until the driver releases them between cycles. While C&C
// has nothing released and something is left to release, it steps every cycle doing nothing (a few cycles at the boot's
// end), so no external wake is needed.
#pragma once

#include <cstdint>
#include <vector>

#include "sim.h"
#include "units/net.h"

namespace units {

// ---- the program (configuration) ----------------------------------------------------------------------------------
struct CncPacket { uint8_t dst, mode, counter, size; uint32_t addr; uint64_t lo, hi; };
struct CncStep {
    enum Kind : uint8_t {
        SEND,        // packets [a, b), one per cycle (Endpoint.send: src 64, the payload masked to its size)
        MARK_CMD,    // instant: lastCommandWord = a (the phase names)
        GO,          // all READY seen at t0 (recorded), then at t0 + cnc_reaction: clear READY, PULSE_GO
        SETTLE,      // until lastGoAt + go_latency + 1
        WAIT,        // a cycles (waitCycles)
        WAIT_READY,  // until all READY (SortTest.atAllReady's waitUntil; records allReadyAt)
        CALL,        // instant: harness bookkeeping, id a (the driver reads the cycle it ran)
    };
    Kind kind;
    uint32_t a = 0, b = 0;
};
struct CncRelease { uint32_t steps; uint64_t parity; };   // a release point: steps queued so far, commandsTo's parity bits

class CncProgram {
public:
    // ref's CnC API, as the harness calls it.
    CncProgram& load_words(unsigned dst, uint32_t byte_addr, const uint32_t* words, size_t n);   // 128-bit mode-0 writes
    CncProgram& command(unsigned dst, uint32_t word, const uint32_t* args = nullptr, size_t nargs = 0);
    CncProgram& send(const CncPacket* p, size_t n);
    CncProgram& go();
    CncProgram& run_when_ready(uint32_t call_id);         // go().call(id): Mach.boot's `runAt`
    CncProgram& at_all_ready(uint32_t call_id);           // SortTest.Machine.atAllReady
    CncProgram& wait_cycles(uint32_t n);
    CncProgram& call(uint32_t id);
    CncProgram& release();                                // the harness queues the next steps here (between cycles)

    explicit CncProgram(const NetConfig& c) : cfg_(c) {}
    std::vector<CncStep> steps;
    std::vector<CncPacket> packets;
    std::vector<CncRelease> releases;                     // the last is the end of the program
    uint32_t commands_to[64] = {};                        // commands sent to each tile so far (the argument block's parity)
private:
    NetConfig cfg_;
};

// ---- C&C 1: the sequencer and its peripheral ----------------------------------------------------------------------
struct Cnc {
    static constexpr unsigned kRows = 8, kRx = 64;
    // ---- ports ----
    sim::HoldOut send;                // the endpoint's send holding register into tile 0's from-controller route block
    sim::FifoIn rx;                   // tile 0's to-controller output (the endpoint's receive FIFO, queue_depth)
    sim::LinkIn rows[kRows];          // the row collectors (latency 1)
    sim::LinkOut go[64];              // GO's net (width 0), latency go_latency + skew[k] + 1

    // ---- configuration ----
    NetConfig cfg;
    const CncStep* prog = nullptr;    // the program (owned by the harness, read-only, outlives the machine)
    const CncPacket* packets = nullptr;
    const CncRelease* releases = nullptr;
    uint32_t n_releases = 0;

    // ---- the sequencer's state (CnC) ----
    uint32_t cur = 0, released = 0, rel = 0;   // the next step; steps queued; release points passed
    uint32_t sub = 0;                 // a SEND step's next packet; a WAIT's cycles left
    int64_t seen = -1;                // a GO step: the cycle it saw all READY
    uint64_t parity = 0;              // commandsTo's parity bits as queued
    int64_t last_go_at = -1, last_all_ready_at = -1, last_run_at = -1;
    uint32_t last_command_word = 0xFFFFFFFFu;
    uint8_t pending_retry = 0;        // a refused send is presented again
    uint64_t retry_w[3] = {};
    // the receive FIFO (6.6): {payload [63:0], src, arrival, payload [127:64], size}
    uint64_t rxq[kRx][5] = {};
    uint32_t rx_head = 0, rx_count = 0;
    bool rx_overflow = false;
    // the error bit (6.6)
    bool error_bit = false, err_line_seen = false, err_line_last = false;
    // ---- C&C 1's peripheral (Cnc1Peripheral): READY, READY_EXPECT, the error lines ----
    int64_t ready_at[64];             // the latest READY arrival (INT64_MIN: none)
    int64_t cleared_at = INT64_MIN;
    uint64_t expect = ~0ull;          // READY_EXPECT_0-1: the model waits for all 64
    uint64_t err_line = 0;            // the tiles' error lines as visible
    uint32_t err_high = 0;

    // ---- statistics and the harness's marks ----
    uint64_t gos = 0, packets_sent = 0, ready_wait_cycles = 0, rx_lost = 0;
    static constexpr unsigned kCalls = 256;
    int64_t call_at[kCalls];          // CALL id -> the cycle it ran (-1: not yet)

    Cnc(sim::Ports& p, const NetConfig& c, const CncProgram& program);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;
    bool busy() const { return cur < released; }                      // ref's CnC.busy (steps queued)
    void release_next();                                              // the driver, between cycles
    bool all_ready(int64_t t) const;
};

// ---- a row collector -----------------------------------------------------------------------------------------------
struct RowCollector {
    sim::LinkIn line[8];              // the row's register blocks (width 1, latency ready_latency - 2)
    sim::LinkOut out;                 // to C&C (width 1, latency 1)
    uint8_t row = 0;
    RowCollector(sim::Ports& p, unsigned row);
    sim::Wake step(sim::Ctx& c);      // stateless: forwards what arrived this cycle as one row word
    uint64_t digest() const { return row; }
};

// ---- C&C 2 and 3's endpoints (tests drive them; idle in M2) ------------------------------------------------------
struct Endpoint {
    sim::HoldOut send;                // into tile k's from-controller route block (never put in M2)
    sim::FifoIn rx;                   // tile k's to-controller output; one packet taken per cycle
    uint8_t address = 0;              // 65, 66
    uint64_t received = 0;
    Endpoint(sim::Ports& p, unsigned address, const NetConfig& c);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const { return received; }
};

}  // namespace units
