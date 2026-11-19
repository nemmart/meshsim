// axi_test_client.h: a test's AXI master for a client's port or the controller's (units/axi_port.h;
// ../v1.4_wip/planning/AXI-INTERFACE.md): a list of reads and a list of writes, each raised in order, the reads with up
// to `max_reads` outstanding, the writes one at a time with their W beats offered with the address and back to back
// (AXI section 2). It keeps AXI's rules (a valid held, unchanged, until its ready), and records what came back: each
// read's beats and responses and where RLAST was, each write's response, and the cycles. Its outputs are registers;
// edge(t, in) takes the slave's outputs of t. For the tests of the LPDDR model, the arbiter and the cache.
#pragma once

#include <cstdint>
#include <vector>

#include "harness.h"
#include "units/axi_port.h"

namespace axitest {
namespace axi = units::axi;

struct Op {
    bool write = false;
    uint64_t addr = 0;
    unsigned beats = 1;
    uint8_t id = 0;                         // ARID or AWID: on the controller's port only
    std::vector<uint64_t> data;             // a write's beats, 2 words each
    // results
    std::vector<uint64_t> got;              // a read's beats
    std::vector<uint8_t> resp;              // a read's each beat's RRESP; a write's BRESP in [0]
    unsigned rlasts = 0;                    // RLASTs seen
    bool rlast_on_last = false;
    bool done = false;
    int64_t t_raised = -1, t_taken = -1, t_first = -1, t_done = -1;
};

struct Client {
    std::vector<Op> reads, writes;
    unsigned max_reads = 4;
    unsigned gap_one_in = 0;                // > 0: a new request waits a cycle at random, one in this many
    uint64_t seed = 1;

    axi::MasterOut out;                     // the outputs of the cycle to come
    size_t ar_next = 0, ar_done = 0, aw_next = 0, aw_done = 0;
    unsigned w_beat = 0;
    bool aw_taken = false;
    std::vector<size_t> r_out, w_out;       // outstanding, oldest first

    bool finished() const { return ar_done == reads.size() && aw_done == writes.size(); }

    void edge(int64_t t, const axi::SlaveOut& in) {
        test::Rng rng(seed ^ uint64_t(t) * 0x9E3779B97F4A7C15ull);
        // what moved in t
        if (out.arvalid && in.arready) { reads[ar_next].t_taken = t; r_out.push_back(ar_next); ar_next++; }
        if (out.awvalid && in.awready) { writes[aw_next].t_taken = t; aw_taken = true; }
        if (out.wvalid && in.wready) w_beat++;
        if (aw_taken && w_beat == writes[aw_next].beats) { w_out.push_back(aw_next); aw_next++; aw_taken = false; w_beat = 0; }
        if (in.rvalid) {
            size_t at = 0;                  // the oldest read outstanding with the beat's ID
            while (at < r_out.size() && reads[r_out[at]].id != in.rid) at++;
            if (at == r_out.size()) test::fail(__FILE__, __LINE__, "an R beat with no read outstanding at %lld", (long long)t);
            else {
                Op& o = reads[r_out[at]];
                if (o.t_first < 0) o.t_first = t;
                o.got.push_back(in.rdata[0]); o.got.push_back(in.rdata[1]); o.resp.push_back(in.rresp);
                if (in.rlast) o.rlasts++;
                if (o.got.size() == 2 * size_t(o.beats)) {
                    o.rlast_on_last = in.rlast; o.done = true; o.t_done = t; ar_done++;
                    r_out.erase(r_out.begin() + long(at));
                }
            }
        }
        if (in.bvalid) {
            size_t at = 0;
            while (at < w_out.size() && writes[w_out[at]].id != in.bid) at++;
            if (at == w_out.size()) test::fail(__FILE__, __LINE__, "a B response with no write outstanding at %lld", (long long)t);
            else { Op& o = writes[w_out[at]]; o.resp.assign(1, in.bresp); o.done = true; o.t_done = t; aw_done++; w_out.erase(w_out.begin() + long(at)); }
        }
        // the outputs of t + 1
        axi::MasterOut n;
        const bool hold_ar = out.arvalid && !in.arready, hold_aw = out.awvalid && !in.awready;
        if (hold_ar) { n.arvalid = true; n.araddr = out.araddr; n.arlen = out.arlen; n.arid = out.arid; }
        else if (ar_next < reads.size() && r_out.size() < max_reads && !(gap_one_in && rng.below(gap_one_in) == 0)) {
            Op& o = reads[ar_next];
            n.arvalid = true; n.araddr = o.addr; n.arlen = uint8_t(o.beats - 1); n.arid = o.id;
            if (o.t_raised < 0) o.t_raised = t + 1;
        }
        if (hold_aw) { n.awvalid = true; n.awaddr = out.awaddr; n.awlen = out.awlen; n.awid = out.awid; }
        else if (aw_next < writes.size() && !aw_taken && !(gap_one_in && rng.below(gap_one_in) == 0) && (w_beat == 0)) {
            Op& o = writes[aw_next];
            n.awvalid = true; n.awaddr = o.addr; n.awlen = uint8_t(o.beats - 1); n.awid = o.id;
            if (o.t_raised < 0) o.t_raised = t + 1;
        }
        // W: the current write's beats, from its address's raising, back to back
        const bool w_going = aw_next < writes.size() && (n.awvalid || aw_taken) && w_beat < writes[aw_next].beats;
        if (w_going) {
            const Op& o = writes[aw_next];
            n.wvalid = true; n.wdata[0] = o.data[2 * w_beat]; n.wdata[1] = o.data[2 * w_beat + 1]; n.wlast = w_beat + 1 == o.beats;
        }
        out = n;
    }
};

// A read or a write of `beats` beats at `addr`; a write's data a pattern of its address and `tag`.
inline Op read_op(uint64_t addr, unsigned beats, uint8_t id = 0) { Op o; o.addr = addr; o.beats = beats; o.id = id; return o; }
inline Op write_op(uint64_t addr, unsigned beats, uint64_t tag, uint8_t id = 0) {
    Op o; o.write = true; o.addr = addr; o.beats = beats; o.id = id;
    for (unsigned k = 0; k < 2 * beats; k++) o.data.push_back((addr + 8 * k) * 0x100000001ull ^ tag);
    return o;
}

}  // namespace axitest
