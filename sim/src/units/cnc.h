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
//   line word (register block -> collector, width 1; W8's tile_ctl.h): bit 0 READY rose at the end of t; bit 1 the error
//                                                      line's level after endCycle(t). Put only in a cycle where READY
//                                                      rises or the error line changes.
//   row word (collector -> C&C, width 1):              [7:0] READY edges, [15:8] which tiles sent a line word, [23:16]
//                                                      their error levels, of the row's tiles 8r .. 8r + 7.
// C&C applies a row word at its arrival, the cycle ref's READY and error changes become visible: READY bits
// (Cnc1Peripheral.readyAt), the tiles' error lines (ERROR_LINES), their OR (CnC.errLine) and the error bit.
//
// GO'S NET: C&C's GO store at t reaches tile k's register block by a width-0 Link of latency go_latency + skew[k] + 1
// (the tile sees GO at t + go_latency + skew[k]; the block handles that cycle's endCycle one cycle later).
//
// THE PROGRAM. ref's harness queues CnC steps (`loadWords`, `command`, `go`, `runWhenReady`, `atAllReady`, `waitCycles`,
// `call`); the C++ harness builds the same list at configuration with `CncProgram`, keeping ref's step granularity (the
// queue's length is in the fingerprint: a `send` of many packets is one step; `command` is an instant mark and one send;
// `go` is a step and, with cnc_go_settle, a settle step; `atAllReady` a wait for all READY and an instant call). A
// harness that queues more steps later (SortTest: the sort's commands after the boot has run) marks a release point;
// steps past it are not yet in the queue (nor in the fingerprint) until the driver releases them between cycles. ref's
// release cycle is decided by the harness from state (SortTest's boot ends at the first cycle with C&C idle and every
// core in WFI, which no line reports), so the driver polls that between cycles, as ref does, then releases and wakes C&C
// for the next cycle through the framework's between-cycles wake (asked of W10, 2026-11-07). C&C never steps while it
// waits for a release (D23).
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
        WAIT_HOST,   // W20 (harness-only): ref's waitUntil in W23's form: until the harness, checking its condition between
                     // cycles, releases it (host_released); instant once released, so C&C moves on the next cycle
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
    CncProgram& wait_host();                              // W20 (harness-only): WAIT_HOST
    CncProgram& release();                                // the harness queues the next steps here (between cycles)

    CncProgram();
    std::vector<CncStep> steps;
    std::vector<CncPacket> packets;
    std::vector<CncRelease> releases;                     // the release points; the program's end is the last
    uint32_t commands_to[64] = {};                        // commands sent to each tile so far (the argument block's parity)
    uint64_t parity() const;                              // commandsTo's parity bits as queued so far
    // ref's constants (Registers, ControllerMemories)
    static constexpr unsigned kCmdParityBit = 23, kArgsEven = 1, kArgsOdd = 8, kArgsPerBlock = 7;
    static constexpr uint32_t kCommand = 0x7FC0;          // command slot 0's byte address (ControllerMemories.COMMAND)
    static constexpr uint32_t kCalls = 256;               // CALL ids 0 .. kCalls - 1
};

// W20 (harness-only; P2): ref's Net harness at a controller's endpoint (sim/tests/mach.h): its sends (Net.cncSends,
// Endpoint.send) and its receive trace (Endpoint.received). Null on every machine but the harness's Net machine.
//   sends   {T, packet}, in order: the endpoint puts the head on its send Hold in its step at t when T <= t + 1 (ref's
//           feed() put it at t, between cycles t and t + 1: visible at t + 1); a refusal is presented again, as the
//           endpoint's own sends are (ref's feed() waits for the Hold to empty: the same cycles)
//   log     {t, payload [63:0], header, payload [127:64]} of every packet taken from the receive FIFO (fatal when full)
// The rings are the harness's (grown between cycles; the pointers refreshed then).
struct HarnessSends {
    struct E { int64_t T; CncPacket p; };
    E* e = nullptr;
    uint32_t cap = 0, head = 0, n = 0;
    uint64_t (*log)[4] = nullptr;
    uint32_t log_cap = 0, log_n = 0;
    bool due(int64_t t) const { return n && e[head].T <= t + 1; }
    const CncPacket& pop() { const CncPacket& p = e[head].p; head = (head + 1) % cap; n--; return p; }
    sim::Wake wake(sim::Ctx& c) const {                       // the step that puts the next send
        if (!n) return sim::Wake::none();
        const int64_t at = e[head].T - 1, t = int64_t(c.now());
        return at <= t + 1 ? c.next() : sim::Wake::at_cycle(uint64_t(at));
    }
    void record(uint64_t t, const sim::Word* w, uint8_t who) {
        if (!log) return;
        if (log_n == log_cap) FATAL("endpoint %u: the harness's receive log is full (%u entries)", who, log_cap);
        uint64_t* x = log[log_n++];
        x[0] = t; x[1] = w[0]; x[2] = w[1]; x[3] = w[2];
    }
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
    unsigned cnc_reaction = 1, go_latency = 8;
    bool go_settle = true;
    uint32_t rx_depth = 64;
    const CncStep* prog = nullptr;    // the program (owned by the harness, read-only, outlives the machine)
    const CncPacket* packets = nullptr;
    const CncRelease* releases = nullptr;
    uint32_t n_releases = 0;

    // ---- the sequencer's state (CnC) ----
    uint32_t cur = 0, released = 0, rel = 0;   // the next step; steps queued; release points passed
    uint32_t sub = 0;                 // a SEND step's next packet
    int64_t seen = -1;                // a GO step: the cycle it saw all READY
    int64_t wait_until = -1;          // a WAIT step: the cycle it finishes (fixed the first cycle it runs)
    uint64_t parity = 0;              // commandsTo's parity bits as queued
    int64_t last_go_at = -1, last_all_ready_at = -1;
    uint32_t last_command_word = 0xFFFFFFFFu;
    uint8_t pending_retry = 0;        // a refused send is presented again
    uint64_t retry_w[3] = {};
    HarnessSends* hs = nullptr;       // W20 (harness-only): ref's Net harness's sends and receive trace; null otherwise
    uint32_t host_released = 0, host_waits = 0;   // W20 (harness-only): WAIT_HOST steps released by the harness, passed
    // the receive FIFO (6.6): {payload [63:0], src, arrival, payload [127:64], size}
    uint64_t rxq[kRx][5] = {};
    uint32_t rx_head = 0, rx_count = 0;
    bool rx_overflow = false;
    // the error bit (6.6): the tiles' OR'd error line as visible, the sticky bit
    bool error_bit = false, err_line_seen = false;
    int64_t error_set_at = -1;
    // ---- C&C 1's peripheral (Cnc1Peripheral): READY, READY_EXPECT, the error lines ----
    int64_t ready_at[64];             // the latest READY arrival (INT64_MIN: none)
    int64_t cleared_at = INT64_MIN;
    uint64_t expect = ~0ull;          // READY_EXPECT_0-1: the model waits for all 64
    uint64_t err_line = 0;            // the tiles' error lines as visible (ERROR_LINES_0-1)

    // ---- statistics and the harness's marks ----
    uint64_t gos = 0, packets_sent = 0, rx_lost = 0, rx_taken = 0, refusals = 0, violations = 0;
    // READY pulses arriving while the tile's previous pulse is still visible (not cleared by a GO). ref's READY bit reads 0
    // from such a pulse's edge until its arrival (readyAt is one cycle: max(old, edge + ready_latency)); the spec's bit is
    // sticky, and this model keeps it set (it cannot know of the pulse before it arrives). Counted, so a run can show it
    // never happened (W9 NOTES, stage 3).
    uint64_t ready_repulses = 0;
    uint64_t stepped = 0;             // cycles C&C was stepped (D23: only when something happens)
    uint32_t rx_peak = 0;
    static constexpr unsigned kCalls = 256;
    int64_t call_at[kCalls];          // CALL id -> the cycle it ran (-1: not yet)

    Cnc(sim::Ports& p, const NetConfig& c, const CncProgram& program);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const;
    bool busy() const { return cur < released; }                      // ref's CnC.busy (steps queued)
    uint32_t steps_queued() const { return released - cur; }         // ref's steps.size()
    void release_next();                                              // the driver, between cycles (then wakes C&C)
    bool ready(unsigned k, int64_t t) const {
        return ready_at[k] != INT64_MIN && ready_at[k] >= cleared_at && ready_at[k] <= t;
    }
    bool all_ready(int64_t t) const;
private:
    sim::Wake sequence(sim::Ctx& c, int64_t t);                       // CnC's sequencer at t (after the lines and the FIFO)
    sim::Wake after_step(sim::Ctx& c);                                // the next cycle if a step is queued, else none
    void send_packet(sim::Ctx& c, const CncPacket& p);
};

// ---- a row collector -----------------------------------------------------------------------------------------------
struct RowCollector {
    sim::LinkIn line[8];              // the row's register blocks (width 1, latency ready_latency - 2)
    // the row word's fields
    static constexpr unsigned kEdges = 0, kPresent = 8, kLevels = 16;
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
    // W20 (harness-only; P2): the harness's sends and receive trace (HarnessSends); null otherwise
    HarnessSends* hs = nullptr;
    uint8_t pending_retry = 0;
    uint64_t retry_w[3] = {};
    Endpoint(sim::Ports& p, unsigned address, const NetConfig& c);
    sim::Wake step(sim::Ctx& c);
    uint64_t digest() const { return received; }
};

}  // namespace units
