// lpddr_model.h: the LPDDR controller and LPDDR behind the AXI arbiter, the simulator's model of them
// (../v1.4_wip/planning/AXI-INTERFACE.md, "AXI" below, sections 4 and 5; types in units/axi_port.h). LPDDR and its
// controller are not chip hardware: the controller's port is the simulator's boundary, as `lpddr` is in v1.36
// (rw_engine.h, THE LPDDR PORT). updater_wip, WORKER5.
//
// ONE MEMORY. The model's memory is the file store's pages (units/file_store.h, pages by address), so the images the
// Updater builds and the lines the cache reads are one memory. A read of bytes nothing has written returns zeros and is
// counted (`unwritten_reads`): a test fails on it.
//
// ITS RULES (AXI section 4): every burst answered in bounded time; the reads of one ID answered in the order taken, and
// the writes likewise; no write response before the write's address and its last beat; between IDs any order.
//
// ITS TIMING (AXI section 5; axi::LpddrTiming):
//   - a read's first beat comes read_latency cycles after its address's handshake at the earliest, then a beat a
//     cycle, the burst's beats back to back. When a burst ends, the next is chosen round robin among the IDs whose
//     oldest unanswered read is due (jitter: at random among them);
//   - W beats are taken in the order of the write addresses, a beat a cycle; a write's response is due write_latency
//     cycles after the later of its address's handshake and its last beat's; the responses go one a cycle, the earliest
//     due first, the order within an ID kept;
//   - at most max_reads and max_writes bursts taken and not answered: beyond, ARREADY or AWREADY 0;
//   - jitter: each burst's latency drawn from [jitter_lo, jitter_hi], each ready 0 one cycle in four.
//   - WHEN THE MEMORY CHANGES: a write's beats reach the memory at the edge before the cycle its response is sent, and a
//     read beat takes its data at the edge before the cycle it is sent. So in a cycle, the write lands first; a read and
//     a write of the same bytes that overlap may see either, and only the wait for the response orders them.
//
// ERRORS (AXI, "Errors"): a test's faults (axi::LpddrFault): every burst that touches a fault's bytes, in its
// direction, answers its response until its count is used; a faulty read's beats carry axi::FAULT_DATA and a faulty
// write writes nothing. A burst past LPDDR's end is DECERR, the same way. A burst that breaks a rule (an address not a
// multiple of 16, across 4 KB, an ID no client has, a `wlast` out of place) or a handshake withdrawn or changed while it
// waited stops the simulator (FATAL).
//
// A CYCLE (axi_port.h): out() is the model's outputs of the cycle, all registers; edge(t, m) takes the arbiter's outputs
// of cycle t and ends the cycle. No allocation in edge.
#pragma once

#include <cstdint>

#include "units/axi_port.h"
#include "units/file_store.h"

namespace units {

class LpddrModel {
public:
    static constexpr unsigned MAX_BURSTS = 64;   // the most max_reads or max_writes may be
    static constexpr unsigned MAX_FAULTS = 8;

    LpddrModel(tracer::FileStore* store, const axi::LpddrTiming& timing = {}, unsigned address_bits = axi::LPDDR_ADDRESS_BITS);
    ~LpddrModel();
    LpddrModel(const LpddrModel&) = delete;
    LpddrModel& operator=(const LpddrModel&) = delete;

    void add_fault(const axi::LpddrFault& f);
    void clear_faults() { n_faults_ = 0; }
    const axi::SlaveOut& out() const { return out_; }
    void edge(int64_t t, const axi::MasterOut& m);
    bool idle() const { return n_reads_ == 0 && n_writes_ == 0; }
    void reset();

    tracer::FileStore* store;
    axi::LpddrTiming timing;
    unsigned address_bits;

    // ---- statistics
    uint64_t read_bursts = 0, write_bursts = 0, beats_read = 0, beats_written = 0, error_bursts = 0;
    uint64_t unwritten_reads = 0;            // beats read from a page nothing wrote: a fault of the run
    uint64_t read_busy_cycles = 0, write_busy_cycles = 0;   // cycles with a beat on R, on W

private:
    struct Read {
        bool used = false;
        uint8_t id = 0, resp = 0;
        uint64_t addr = 0, seq = 0;
        unsigned beats = 0, sent = 0;
        int64_t due = 0;
    };
    struct Write {
        bool used = false, complete = false;
        uint8_t id = 0, resp = 0;
        uint64_t addr = 0, seq = 0;
        unsigned beats = 0, got = 0;
        int64_t aw_t = 0, due = 0;
        uint64_t* data = nullptr;            // 2 words a beat
    };

    void check_burst(const char* ch, uint64_t addr, unsigned len, unsigned id) const;
    uint8_t fault_for(uint64_t addr, unsigned beats, bool write);
    unsigned latency(unsigned fixed);
    bool drop();                             // jitter: a ready 0 this cycle
    void read_beat(uint64_t addr, uint64_t* w);
    void land(const Write& w);
    int oldest_read(unsigned id) const;
    int oldest_write(unsigned id) const;

    axi::SlaveOut out_;
    axi::MasterOut prev_m_;                  // the arbiter's outputs of the last cycle, for the handshake checks
    bool prev_ar_wait_ = false, prev_aw_wait_ = false, prev_w_wait_ = false;
    Read reads_[MAX_BURSTS];
    Write writes_[MAX_BURSTS];
    unsigned n_reads_ = 0, n_writes_ = 0;
    int r_cur_ = -1, b_cur_ = -1;            // the read burst on R, the write answered on B, in the cycle out_ is for
    unsigned r_rr_ = axi::CLIENTS - 1;       // the last ID served on R
    int w_order_[MAX_BURSTS];                // the writes still taking beats, in the order of their addresses
    unsigned w_head_ = 0, w_n_ = 0;
    uint64_t seq_ = 0;
    uint32_t rng_;
    axi::LpddrFault faults_[MAX_FAULTS];
    unsigned fault_left_[MAX_FAULTS] = {};
    unsigned n_faults_ = 0;
};

}  // namespace units
