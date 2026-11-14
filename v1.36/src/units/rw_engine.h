// rw_engine.h: the RW engine, the memory tile's peripheral 1, type 2, version 0 (machine-spec 17.7, with 3 for the
// `lpddr` port and 19 for its parameters): the RTL's rw_engine_unit (Snarky One's rtl/blocks/rw_engine_unit), register
// for register and cycle for cycle. It is a peripheral of the slot (units/peripheral.h's method set): the control block
// and the DMA block (17.1-17.4; the RTL's peripheral_control and peripheral_dma) are the slot's, and this file holds
// what rw_engine_unit.veryl holds itself: block 2's words, the job, the bursts, the write data, the write buffer, the
// two output queues' entries (the RTL's output_queue, which the slot's DMA pops) and the `lpddr` port.
//
// ITS WORDS (17.7; window words 32-36, block 2's words 0-4):
//   32 RW_CONFIG       R/W  [0] kind: 0 a read, 1 a write; [10:1] beats, 0-1023
//   33 RW_LPDDR_ADDR   R/W  [LPDDR_ADDRESS_BITS - 5:0] the first beat's LPDDR beat address (a byte address over 16)
//   34 RW_BUFFER_ADDR  R/W  [log2 RW_BUFFER_BYTES - 5:0] a write's first beat in the write buffer, its offset over 16
//   35 RW_REPLY_W0     W    the next reply's bits [31:0]; 0 at the reset and after each send
//   36 RW_REPLY_SEND   W    the reply's bits [63:32]; pushes {0, RW_REPLY_SEND, RW_REPLY_W0} into output queue 1
// The bits above a register's fields read 0 and a store ignores them (5.4); a W word reads 0. The first three are not
// reset (17.7: undefined until written; here 0 before the first store).
//
// TIMING (the slot's rule, periph_slot.h, THE CONTRACT; p, c, a are cycles, and step(t) is the edge that ends t):
//   - a store presented in p (write_word(p, ...), run(p)) is in effect at the end of p; a load in p reads the end of
//     p - 1. START in p takes RW_CONFIG, RW_LPDDR_ADDR and RW_BUFFER_ADDR as they stand at the end of p; STATUS.busy is
//     1 from p + 1 until the job ends, and EVENTS.done is posted in the cycle it ends. A job of 0 beats ends in p + 1.
//   - THE BURSTS. A job moves its beats by INCR bursts of 16-byte beats, each from its next LPDDR beat to the end of
//     that beat's aligned group of BURST_BEATS (4) beats, 64 bytes, or to the job's last beat: no burst crosses a
//     4 KiB boundary. The address channel (AR for a read, AW for a write) takes the next burst at the edge that ends a
//     cycle in which beats are left to raise, the channel is free (its valid 0, or its burst accepted in the cycle)
//     and, for a read, output queue 0 has room for every beat in flight: with F its free entries at the start of the
//     cycle and I the read's beats raised and not yet received, a burst of L beats is raised only if I + L <= F. So a
//     job's first burst is raised at the end of p + 1 and its address is on AR or AW from p + 2.
//   - A READ. Each R beat (RREADY is 1) enters output queue 0 at the end of its cycle; the read ends in the cycle its
//     last beat comes.
//   - A WRITE. The engine reads the buffer's rows into the W channel's register: a row read in cycle c (the buffer's
//     row at the end of c - 1) is pending in c + 1, and the register takes it at the end of c + 1 if it is empty or its
//     beat leaves (WVALID and WREADY); if not, the row is read again. So the first beat is on W from p + 3 and a beat
//     follows a cycle while WREADY is 1; WLAST marks a burst's last beat, by the burst rule; the data may run ahead of
//     its address. The write ends in the cycle LPDDR answers its last burst (BVALID), every burst raised.
//   - THE WRITE BUFFER, the engine's region: a WRITE the director accepted at a is presented on the write port in
//     a + 2 (the slot's accept(a, ...), called after step(a + 2)) and lands at the edge that ends that cycle. It
//     writes, by its size, the 32-bit word at its address's bits [3:2], the 64-bit half at bit 3, or the whole row,
//     at row [log2 RW_BUFFER_BYTES - 1:4].
//   - THE REPLY. RW_REPLY_SEND presented in p pushes its entry at the end of p if output queue 1 had room at the start
//     of p, and clears RW_REPLY_W0 either way.
//   - LPDDR ERRORS. An R beat or a B response that is not OKAY sets ERRORS.lpddr_error at the end of its cycle,
//     sticky; the beat or the write completes all the same.
//   - THE OUTPUT QUEUES (output_queue.veryl): a push is accepted with room at the start of its cycle and its entry is
//     at the head from the next; the DMA's take of cycle t (out_take, called before step(t)) removes the head at the
//     end of t, so the room "at the start of t" still counts that entry.
//
// THE LPDDR PORT is the model's boundary: LPDDR and its controller are not chip hardware and are not modelled here. The
// port is AXI4, the engine the master (3): AR, R, AW, W, B; 128-bit data; no ID; INCR bursts (AxBURST 1) of 16-byte
// beats (AxSIZE 4), AxLEN a burst's beats less one; WSTRB all ones; RREADY and BREADY 1; AR and AW share the address
// and length register. A harness places a slave on it (attach): in every cycle the engine is not idle it gives the
// slave its outputs of the cycle, all from registers (LpddrOut), and takes the slave's (LpddrIn); a slave that keeps
// time by itself counts the cycles it was not called in from `t`. Without a slave, a job that raises a burst stops the
// simulator. The reset is not the port's: a harness that resets the engine resets its slave.
//
// UNDEFINED BEHAVIOUR (1.3). A START while busy stops the simulator (17.2). The rest behaves as the RTL does and is
// counted as a violation (violation_count, the slot's flag): a reply pushed into a full queue 1 is lost; a job past
// LPDDR's end or the buffer's wraps; a WRITE past the buffer's end lands at its row within the buffer, a misaligned one
// lands aligned, one of the reserved size writes nothing, one into a row a running write reads lands.
#pragma once

#include <cstdint>

#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/linkword.h"
#include "units/peripheral.h"

namespace units {

// The engine's outputs on the lpddr port in a cycle: registers and constants.
struct LpddrOut {
    bool arvalid = false, awvalid = false;
    uint64_t addr = 0;                    // ARADDR and AWADDR: the last burst raised, a byte address
    unsigned len = 0;                     // ARLEN and AWLEN: its beats less one
    static constexpr unsigned SIZE = 4, BURST = 1, WSTRB = 0xFFFF;   // AxSIZE, AxBURST, WSTRB; RREADY and BREADY are 1
    bool wvalid = false, wlast = false;
    uint64_t wdata_lo = 0, wdata_hi = 0;
};
// The slave's outputs in a cycle.
struct LpddrIn {
    bool arready = false, awready = false, wready = false;
    bool rvalid = false, rlast = false, bvalid = false;
    unsigned rresp = 0, bresp = 0;        // OKAY is 0
    uint64_t rdata_lo = 0, rdata_hi = 0;
};
// The LPDDR controller as the engine sees it: cycle t's outputs for the engine's outputs of t, and its own edge. Given
// by a harness (tests/axi4_slave.h); called once in each cycle the engine is not idle.
struct LpddrSlave {
    virtual LpddrIn cycle(int64_t t, const LpddrOut& out) = 0;
protected:
    ~LpddrSlave() = default;
};

struct RwEngineConfig {
    unsigned lpddr_bits = 32;             // LPDDR_ADDRESS_BITS, 24 to 36 (19)
    uint32_t buffer_bytes = 16384;        // RW_BUFFER_BYTES, a power of two from 4 KiB to 16 MiB
    unsigned queue_beats = 16;            // RW_QUEUE_BEATS, at least a burst's 4
};

struct RwEngine : PeripheralBase<RwEngine> {
    enum : int { RW_CONFIG = 32, RW_LPDDR_ADDR = 33, RW_BUFFER_ADDR = 34, RW_REPLY_W0 = 35, RW_REPLY_SEND = 36 };
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1 };
    enum : unsigned { KIND_READ = 0, KIND_WRITE = 1 };
    static constexpr unsigned BURST_BEATS = 4;   // a burst's group of beats, 64 bytes aligned
    // the violations, by kind (header, UNDEFINED BEHAVIOUR)
    enum : unsigned { V_REPLY_FULL, V_JOB_PAST_END, V_WRITE_PAST_END, V_WRITE_MISALIGNED, V_WRITE_SIZE, V_WRITE_OWNED, V_KINDS };

    RwEngineConfig cfg;
    unsigned tile = 0;
    LpddrSlave* slave = nullptr;
    EventCounts ev;

    // ---- block 2's registers ----
    unsigned config_kind = 0, config_beats = 0;
    uint64_t config_lpddr = 0;            // an LPDDR beat address, LPDDR_ADDRESS_BITS - 4 bits
    uint32_t config_buffer = 0;           // a buffer row
    uint32_t reply_w0 = 0;

    // ---- the control block's own: STATUS.busy and ERRORS.lpddr_error ----
    bool busy = false, lpddr_error = false;

    // ---- the job, taken at START (rw_engine_unit.veryl's names) ----
    unsigned job_kind = 0;
    unsigned raise_left = 0;              // the beats still to raise
    uint64_t raise_group = 0;             // the next burst's group: its LPDDR beat address over BURST_BEATS
    unsigned raise_offset = 0;            // and its first beat's place in the group
    unsigned in_flight = 0;               // raised and not answered: a read's beats, a write's bursts
    unsigned rows_left = 0;               // a write's rows still to take
    uint32_t fetch_row = 0;               // the next one
    unsigned fetch_offset = 0;            // and its LPDDR beat's place in its group
    uint32_t job_row = 0, job_rows = 0;   // a write's rows, for the ownership flag

    // ---- the lpddr port's registers ----
    bool ar_valid = false, aw_valid = false;
    uint64_t burst_address = 0;           // a beat address
    unsigned burst_length = 0;            // beats less one
    bool pending = false;                 // a row was read in the cycle before
    uint64_t pending_lo = 0, pending_hi = 0;
    bool w_valid = false, w_last = false;
    uint64_t w_lo = 0, w_hi = 0;

    // ---- the write buffer: rows of 16 bytes ----
    uint32_t rows = 0;
    uint64_t* buffer = nullptr;           // [2 * row]: bits [63:0], [2 * row + 1]: bits [127:64]

    // ---- the output queues' entries: 0 the reads' beats, 1 the reply ----
    struct Queue {
        unsigned cap = 0, head = 0, count = 0;
        uint64_t* e = nullptr;            // [2 * place]
        int64_t taken_t = INT64_MIN;      // the cycle of the DMA's last take
        unsigned free_at_start(int64_t t) const { return cap - count - (taken_t == t ? 1 : 0); }
        void push(uint64_t lo, uint64_t hi) { const unsigned at = (head + count) % cap; e[2 * at] = lo; e[2 * at + 1] = hi; count++; }
    } q[2];

    // ---- statistics and flags ----
    uint64_t jobs = 0, bursts = 0, beats_read = 0, beats_written = 0, replies = 0, port_writes = 0;
    uint64_t n_violations = 0, violations[V_KINDS] = {};

    RwEngine(unsigned tile_id, const RwEngineConfig& c) : cfg(c), tile(tile_id) {
        if (c.lpddr_bits < 24 || c.lpddr_bits > 36) FATAL("rw_engine: LPDDR_ADDRESS_BITS must be 24 to 36");
        if (c.buffer_bytes < 4096 || c.buffer_bytes > 16777216 || (c.buffer_bytes & (c.buffer_bytes - 1)) != 0)
            FATAL("rw_engine: RW_BUFFER_BYTES must be a power of two from 4 KiB to 16 MiB");
        if (c.queue_beats < BURST_BEATS) FATAL("rw_engine: RW_QUEUE_BEATS must be at least a burst's 4 beats");
        static const uint8_t depth[2] = {1, 1};
        static const bool flags[2] = {};
        ev.init(2, depth, flags);
        rows = c.buffer_bytes / 16;
        buffer = new uint64_t[2 * size_t(rows)]();
        q[0].cap = c.queue_beats; q[0].e = new uint64_t[2 * size_t(c.queue_beats)]();
        q[1].cap = 1; q[1].e = new uint64_t[2]();
    }
    ~RwEngine() { delete[] buffer; delete[] q[0].e; delete[] q[1].e; }
    RwEngine(const RwEngine&) = delete;
    RwEngine& operator=(const RwEngine&) = delete;

    // The harness's: the LPDDR controller on the port (null: none).
    void attach(LpddrSlave* s) { slave = s; }
    // The port's outputs in the cycle that comes: the registers as they stand.
    LpddrOut lpddr_out() const {
        LpddrOut o;
        o.arvalid = ar_valid; o.awvalid = aw_valid;
        o.addr = burst_address << 4; o.len = burst_length;
        o.wvalid = w_valid; o.wlast = w_last; o.wdata_lo = w_lo; o.wdata_hi = w_hi;
        return o;
    }

    // ---- identity ----
    int type() const { return 2; }
    int params() const { return 2; }                  // type 2, version 0, no constants (17.7)

    // ---- the registers ----
    uint64_t beat_mask() const { return (uint64_t(1) << (cfg.lpddr_bits - 4)) - 1; }
    bool word(int64_t, int w, uint32_t& v) const {
        switch (w) {
            case RW_CONFIG: v = config_beats << 1 | config_kind; return true;
            case RW_LPDDR_ADDR: v = uint32_t(config_lpddr); return true;
            case RW_BUFFER_ADDR: v = config_buffer; return true;
            case RW_REPLY_W0: case RW_REPLY_SEND: v = 0; return true;   // W: a load reads 0 (5.4)
            default: v = 0; return false;
        }
    }
    bool writable(int w) const { return w >= RW_CONFIG && w <= RW_REPLY_SEND; }
    int write_word(int64_t p, int w, uint32_t v) {
        switch (w) {
            case RW_CONFIG: config_kind = v & 1; config_beats = v >> 1 & 0x3FF; return 1;
            case RW_LPDDR_ADDR: config_lpddr = v & beat_mask(); return 1;
            case RW_BUFFER_ADDR: config_buffer = v & (rows - 1); return 1;
            case RW_REPLY_W0: reply_w0 = v; return 1;
            case RW_REPLY_SEND:
                // {0, RW_REPLY_SEND, RW_REPLY_W0} into queue 1 if it had room at the start of p; RW_REPLY_W0 cleared
                if (q[1].free_at_start(p) >= 1) { q[1].push(uint64_t(v) << 32 | reply_w0, 0); replies++; }
                else violate(V_REPLY_FULL);
                reply_w0 = 0;
                return 1;
            default: return -1;
        }
    }

    // ---- START presented in p ----
    void run(int64_t p) {
        if (busy)
            FATAL("the RW engine (tile %u): undefined behavior at %lld: START while a job is in progress (17.2)", tile, (long long)p);
        job_kind = config_kind;
        raise_left = config_beats;
        raise_group = config_lpddr >> 2;
        raise_offset = unsigned(config_lpddr & 3);
        in_flight = 0;
        rows_left = config_beats;
        fetch_row = config_buffer;
        fetch_offset = unsigned(config_lpddr & 3);
        job_row = config_buffer; job_rows = job_kind == KIND_WRITE ? config_beats : 0;
        busy = true;
        jobs++;
        if (config_lpddr + config_beats > beat_mask() + 1 || (job_kind == KIND_WRITE && config_buffer + config_beats > rows))
            violate(V_JOB_PAST_END);
    }

    // ---- the write port: a WRITE the director accepted at a, presented in a + 2, landing at that cycle's end ----
    void accept(int64_t, int, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
        port_writes++;
        if (offset >= cfg.buffer_bytes) violate(V_WRITE_PAST_END);
        const uint32_t row = offset >> 4 & (rows - 1);
        if (busy && job_kind == KIND_WRITE && ((row - job_row) & (rows - 1)) < job_rows) violate(V_WRITE_OWNED);
        uint64_t* r = buffer + 2 * size_t(row);
        const unsigned lane = offset >> 2 & 3;
        switch (size) {
            case lw::SIZE_32: {
                if (offset & 3) violate(V_WRITE_MISALIGNED);
                uint64_t& half = r[lane >> 1];
                const unsigned shift = 32 * (lane & 1);
                half = (half & ~(uint64_t(0xFFFFFFFF) << shift)) | (lo & 0xFFFFFFFF) << shift;
                break;
            }
            case lw::SIZE_64:
                if (offset & 7) violate(V_WRITE_MISALIGNED);
                r[lane >> 1] = lo;
                break;
            case lw::SIZE_128:
                if (offset & 15) violate(V_WRITE_MISALIGNED);
                r[0] = lo; r[1] = hi;
                break;
            default: violate(V_WRITE_SIZE); break;   // the reserved size writes nothing
        }
    }

    // ---- level reads ----
    bool running(int64_t) const { return busy; }
    int errors(int64_t) const { return lpddr_error ? 1 : 0; }
    EventCounts& events() { return ev; }
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }

    // ---- the DMA's side: the head is there at the start of t; a take removes it at the end of t ----
    int out_queues() const { return 2; }
    bool out_ready(int64_t, int k) const { return k < 2 && q[k].count > 0; }
    void out_take(int64_t t, int k, uint64_t w[2]) {
        Queue& x = q[k];
        if (k >= 2 || x.count == 0) FATAL("the RW engine (tile %u): out_take at %lld from output queue %d, which holds nothing", tile, (long long)t, k);
        w[0] = x.e[2 * x.head]; w[1] = x.e[2 * x.head + 1];
        x.head = (x.head + 1) % x.cap; x.count--;
        x.taken_t = t;
    }

    // ---- the edge that ends cycle t ----
    void step(int64_t t) {
        if (idle(t)) return;
        const LpddrOut out = lpddr_out();
        const LpddrIn in = slave ? slave->cycle(t, out) : LpddrIn{};
        const bool write = job_kind == KIND_WRITE;

        // this cycle's decisions, from the state at its start
        const unsigned group_rest = BURST_BEATS - raise_offset;
        const unsigned burst_beats = raise_left < group_rest ? raise_left : group_rest;
        const unsigned free0 = q[0].free_at_start(t);
        const bool room = free0 >= in_flight + burst_beats;
        const bool channel_free = write ? !aw_valid || in.awready : !ar_valid || in.arready;
        const bool raise = busy && raise_left != 0 && channel_free && (write || room);
        const unsigned raised = write ? 1 : burst_beats;
        const bool answer = write ? in.bvalid : in.rvalid;
        const bool work_end = busy && raise_left == 0 && in_flight == (answer ? 1u : 0u);
        const bool take = pending && (!w_valid || in.wready);
        const bool take_last = rows_left == 1 || fetch_offset == BURST_BEATS - 1;
        const unsigned rows_after = rows_left - (take ? 1 : 0);
        const bool fetch = busy && write && rows_after != 0;
        const uint32_t fetch_row_after = (fetch_row + (take ? 1 : 0)) & (rows - 1);
        if (raise && !slave)
            FATAL("the RW engine (tile %u): a job raises a burst at %lld with no LPDDR controller on the lpddr port (the port is "
                  "the chip's boundary: a harness places an AXI4 slave on it, RwEngine::attach)", tile, (long long)t);

        // the write data: the W register, then the row read
        if (take) { w_lo = pending_lo; w_hi = pending_hi; w_last = take_last; beats_written++; }
        w_valid = take || (w_valid && !in.wready);
        pending = fetch;
        if (fetch) { pending_lo = buffer[2 * size_t(fetch_row_after)]; pending_hi = buffer[2 * size_t(fetch_row_after) + 1]; }

        // the address channels and the job
        ar_valid = (raise && !write) || (ar_valid && !in.arready);
        aw_valid = (raise && write) || (aw_valid && !in.awready);
        if (raise) {
            burst_address = raise_group << 2 | raise_offset;
            burst_length = burst_beats - 1;
            raise_left -= burst_beats;
            raise_group = (raise_group + 1) & (beat_mask() >> 2);
            raise_offset = 0;
            bursts++;
        }
        in_flight = in_flight + (raise ? raised : 0) - (answer ? 1 : 0);
        rows_left = rows_after;
        fetch_row = fetch_row_after;
        if (take) fetch_offset = (fetch_offset + 1) & (BURST_BEATS - 1);

        // a read beat into queue 0, with room at the start of the cycle; the errors
        if (in.rvalid) {
            if (free0 >= 1) { q[0].push(in.rdata_lo, in.rdata_hi); beats_read++; }
            else FATAL("the RW engine (tile %u): a read beat at %lld with no room in output queue 0", tile, (long long)t);
        }
        if ((in.rvalid && in.rresp != 0) || (in.bvalid && in.bresp != 0)) lpddr_error = true;

        // the job's end: EVENTS.done posted, STATUS.busy 0 from the next cycle
        if (work_end) { busy = false; ev.post(t, EV_DONE); }
    }

    // Nothing in progress: no job, and nothing on the port's channels.
    bool idle(int64_t) const { return !busy && !ar_valid && !aw_valid && !w_valid && !pending; }
    uint64_t violation_count() const { return n_violations; }

    // The reset (17.7, 18): the job ended, both output queues and the port's channels empty, RW_REPLY_W0, STATUS,
    // ERRORS and EVENTS 0. The write buffer keeps its contents; block 2's other registers are not reset.
    void reset(int64_t) {
        busy = false; lpddr_error = false;
        ar_valid = aw_valid = w_valid = pending = false;
        reply_w0 = 0;
        for (Queue& x : q) { x.head = 0; x.count = 0; x.taken_t = INT64_MIN; }
        ev.clear_all();
    }

    // The debug checks' digest (SlotCore::digest).
    uint64_t progress() const {
        return jobs ^ bursts << 8 ^ beats_read << 20 ^ beats_written << 32 ^ replies << 44 ^ port_writes << 52 ^ uint64_t(busy) << 63 ^
               uint64_t(raise_left) << 3 ^ uint64_t(in_flight) << 14 ^ uint64_t(rows_left) << 25 ^ uint64_t(q[0].count) << 36 ^ uint64_t(q[1].count) << 41;
    }

private:
    void violate(unsigned kind) { n_violations++; violations[kind]++; }
};

}  // namespace units
