// cache_ctl.h: the cache's controller (../v1.4_wip/planning/CACHE-INTERFACE.md, "CI" below;
// DESIGN-NOTES-2026-11-16.md sections 1 to 4), with the cache memory (units/cache_memory.h) inside it. updater_wip,
// WORKER5. What the cache does seen from its ports is CI; this file is how it does it, in the shape of hardware.
//
// ITS PORTS (a cycle's signals: Inputs and Outputs below; every output a register; the cycle convention is
// units/axi_port.h's):
//   - four look-up ports and the shared reply bus (CI section 1; units/lookup_port.h), port c the client c;
//   - the fill port, v1.36's `fill` (CI section 5): a read and a write channel, each with its response;
//   - its AXI port to the arbiter, ID 0 (AXI-INTERFACE.md);
//   - the error flag (CI section 6).
//
// ITS PARTS (the third note's section 1), each a block of registers and of edge():
//   THE FRONT END. A request taken at its port goes into that port's register (`held`): a look-up port's until its
//     reply, so its `ready` is 0 meanwhile (one request outstanding, owner); the fill port's read and write likewise.
//     When the walker is free it picks the next request, round robin among the sources waiting (CI, Open 1: †), the
//     request a miss was fetched for first. An update waits while the update slot is full (one update in flight).
//   THE WALKER. One request at a time, a level a step: the tag memory read (cycle 1), the compare and the data memory
//     read (cycle 2), the line (cycle 3, which also reads the next level's tags): two cycles a level on hits. It
//     answers as CI section 2's tables say, and for the fill port as 17.9.5 says. A request is a walk that changes
//     nothing until its last step, so a request that cannot go on (its line is not in the cache, and the miss handler
//     is busy or must fetch it) is put back in its register, waiting, and walked again from the top when it can be:
//     a replay. A fill write's last step writes the line, dirty; an update's changes its entry and hands the line to
//     the AXI writer (CI section 3, steps 4 to 6).
//   THE MISS HANDLER. One line at a time: the victim (chosen by the walker, never the update slot's line), written
//     back first if dirty, in its place, and its response waited for (no list of pending writes, owner); then the line
//     fetched (8 beats) into a line register, or for a fill write the line itself; then the data row and the tag row
//     written in one cycle. A fetch answered with an error installs nothing, and the request it was for is answered
//     with the error at its replay.
//   THE AXI WRITER. One line's write at a time, its address and its 8 beats from a line register; a short FIFO of what
//     each write was (an update's, a write-back), whose responses come in that order (one ID).
//   THE UPDATE SLOT. The one update in flight: its client, line and the entry before. At its write's response the
//     reply goes out (CI section 3, steps 7 and 8). Its line is never evicted and never discarded meanwhile.
//   THE PAGE SEQUENCER is the walker's discard: the page's 32 lines, a tag read and compare each, invalidated.
//
// TWO STEPS (the third note, section 1). hits_under_miss false: the front end takes nothing while the miss handler
// works (step 1; the update slot is parked all the same, as CI section 3 needs). true (step 2): while it works the
// walker serves other requests, as long as they hit and touch no line of the miss handler's set.
//
// "Write in flight" and "being filled" are registers (the slot, the miss handler), compared with a set and way; the
// tag memory holds valid, dirty, the tag and the ages.
//
// A CYCLE: out() is the controller's outputs of the cycle; edge(t, in) takes the other side's outputs of t and ends the
// cycle: the front end's handshakes, the AXI channels, the slot's and the miss handler's work, the walker's one step,
// the memories' ports, then the next outputs. No allocation in edge.
//
// THE STALE-LINE CHECK (a simulator's check, not hardware; WORKER5's NOTES section 7): with `check_store` set, every
// clean line the walker reads, but the update slot's, is compared with LPDDR (the file store): a difference stops the
// simulator. Every dirty line written back must be one a fill write made.
#pragma once

#include <cstdint>

#include "units/axi_port.h"
#include "units/cache_memory.h"
#include "units/file_store.h"
#include "units/lookup_port.h"

namespace units {
namespace cache {

// ---- the fill port's signals (machine-spec 3, `fill`): the Tracer's requests, the cache's responses
struct FillRead {
    bool valid = false;
    int32_t root = 0;
    uint8_t depth = 0;
    int64_t line = 0;
};
struct FillWrite {
    bool valid = false;
    int32_t root = 0;
    uint8_t depth = 0;
    int64_t line = 0;
    uint64_t data[LINE_WORDS] = {};
};
enum : uint8_t { FILL_WRITE_OK = 0, FILL_WRITE_ERROR = 1 };

struct Inputs {
    lookup::Request req[lookup::CLIENTS];
    FillRead fr;
    FillWrite fw;
    axi::SlaveOut axi;
};
struct Outputs {
    bool ready[lookup::CLIENTS] = {};
    lookup::Reply reply;
    bool fr_ready = false, fw_ready = false;
    bool frr_valid = false, fwr_valid = false;   // the fill port's responses
    uint8_t frr_status = 0, fwr_status = 0;
    uint64_t frr_data[LINE_WORDS] = {};
    axi::MasterOut axi;
    bool error = false;
};

struct Config {
    bool hits_under_miss = true;   // step 2
    int32_t map_root_for_stats = -1;   // the statistics' classes: the jump table's file's root, if known
};

class CacheCtl {
public:
    static constexpr unsigned PORTS = lookup::CLIENTS, S_FREAD = PORTS, S_FWRITE = PORTS + 1, SOURCES = PORTS + 2;
    enum Class : unsigned { MEM_INDEX = 0, MEM_DATA, MAP_INDEX, MAP_DATA, LOOKUP_INDEX, CLASSES };

    explicit CacheCtl(const Config& c = {});
    const Outputs& out() const { return out_; }
    void edge(int64_t t, const Inputs& in);
    bool idle() const;
    void reset();

    Config cfg;
    const tracer::FileStore* check_store = nullptr;   // the stale-line check's LPDDR (null: off)
    bool trace = false;                                 // a line a cycle on stderr

    // ---- back doors for tests (not hardware): every dirty line written into `store` (a test's sweep before comparing
    // LPDDR with a reference), and a line's presence
    void write_dirty_to(tracer::FileStore& store) const;
    bool holds(int64_t line) const;
    bool holds_dirty(int64_t line) const;

    // ---- statistics
    uint64_t hits[CLASSES] = {}, misses[CLASSES] = {};
    uint64_t evictions = 0, write_backs = 0, write_back_errors = 0, fetch_errors = 0, allocations = 0;
    uint64_t updates = 0, update_writes = 0, discards = 0, discarded_lines = 0, discarded_dirty = 0;
    uint64_t replays = 0, walk_lines[5] = {};          // the index lines a finished request walked, 0 to 3, and beyond
    uint64_t served_under_miss = 0;
    // latencies, from a request's handshake to its answer: count, sum, max; a histogram by powers of two
    struct Latency {
        uint64_t n = 0, sum = 0, max = 0, bucket[16] = {};
        void add(uint64_t c);
    } fill_read_latency, fill_write_latency, lookup_latency, update_latency;
    uint64_t fill_reads = 0, fill_writes = 0, lookups = 0;

private:
    enum Op : uint8_t { J_LOOK_UP = 0, J_UPDATE = 1, J_DISCARD = 2, J_FREAD = 3, J_FWRITE = 4 };
    struct Held {
        bool valid = false, parked = false, wait_mh = false, wait_slot = false;
        uint8_t op = 0, depth = 0, level = 0;
        uint32_t root = 0, entry = 0;
        uint64_t pos = 0;                      // a look-up port's page number; the fill port's line
        uint64_t data[LINE_WORDS] = {};
        int64_t failed_line = -1, missed_line = -1, t_accept = 0;
    };
    enum WState : uint8_t { W_IDLE, W_CMP, W_DATA, W_REPLY };
    enum MState : uint8_t { M_IDLE, M_WB_READ, M_WB_HAND, M_WB_WAIT, M_FETCH, M_FILL, M_INSTALL, M_FAIL };
    enum Kind : uint8_t { K_UPDATE, K_WRITE_BACK };

    // the walker's steps
    void start(int64_t t, unsigned s);
    void issue_tag_read(int64_t t);
    void step_cmp(int64_t t);
    void step_data(int64_t t);
    unsigned index_at(unsigned k) const;   // the request's entry index at level k
    unsigned cls() const;
    void finish(int64_t t, unsigned src);  // the request's register freed, its latency counted
    void reply(int64_t t, uint8_t status, uint8_t level, uint32_t entry);
    void fill_read_response(int64_t t, uint8_t status, const uint64_t* data);
    void fill_write_response(int64_t t, uint8_t status);
    void abandon(bool for_slot);
    void check_clean(int64_t line, const uint64_t* data) const;
    // the stale-line check's own record (not hardware): lines an LPDDR error left different from LPDDR on purpose
    int64_t diverged_[16] = {};
    unsigned n_diverged_ = 0;
    // the miss handler
    void step_mh(int64_t t, const Inputs& in);

    Outputs out_, next_;
    TagMemory tags_;
    DataMemory data_;
    Held held_[SOURCES];
    unsigned rr_ = SOURCES - 1;
    int prio_ = -1;                           // the source a miss was just handled for
    bool error_ = false;
    bool bus_taken_ = false;                  // this cycle's reply bus already given (the slot first)
    bool mh_data_read_ = false, mh_writes_ = false;   // the miss handler's use of the memories' ports this cycle
    int64_t tag_read_t_ = INT64_MIN;          // the cycle of the walker's last tag read
    int64_t mh_tag_write_t_ = INT64_MIN;      // the cycle and set of the miss handler's last tag write: a walker's read
    unsigned mh_tag_write_set_ = 0;           //   of that set no later than it is read again

    // the walker
    WState wst_ = W_IDLE;
    unsigned src_ = 0, k_ = 0, dk_ = 0;
    int64_t cur_ = 0, x_ = 0;                 // the page at level k; the line being looked up
    unsigned set_ = 0, way_ = 0;
    bool owned_all_ = true, wfresh_ = false, wdirty_ = false;
    uint64_t wline_[LINE_WORDS] = {};
    TagRow row_;                              // the set's tag row, as read
    uint8_t rstatus_ = 0, rlevel_ = 0;        // a reply waiting for the bus
    uint32_t rentry_ = 0;

    // the miss handler
    MState mst_ = M_IDLE;
    bool m_alloc_ = false, m_vvalid_ = false, m_vdirty_ = false, m_err_ = false, m_ar_ = false, m_copied_ = false;
    int64_t m_x_ = 0, m_vline_ = 0;
    unsigned m_set_ = 0, m_way_ = 0, m_owner_ = 0, m_got_ = 0;
    TagRow m_row_;
    uint64_t m_buf_[LINE_WORDS] = {};

    // the AXI writer
    bool w_busy_ = false, w_aw_ = false;
    unsigned w_beat_ = 0;
    uint64_t w_addr_ = 0, w_buf_[LINE_WORDS] = {};
    Kind kinds_[4] = {};
    unsigned kinds_head_ = 0, kinds_n_ = 0;

    // the update slot
    bool s_valid_ = false;
    unsigned s_src_ = 0, s_set_ = 0, s_way_ = 0;
    uint8_t s_level_ = 0;
    uint32_t s_old_ = 0;
    int64_t s_x_ = 0;
};

}  // namespace cache
}  // namespace units
