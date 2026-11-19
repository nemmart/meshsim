// axi_arbiter.h: the AXI arbiter between LPDDR's clients and the LPDDR controller (../v1.4_wip/planning/AXI-INTERFACE.md,
// "AXI" below, section 3; types in units/axi_port.h). updater_wip, WORKER5.
//
// WHAT IT DOES. Each client has a port of its own (AXI section 1: `lpddr`'s rules, no ID signals); the arbiter is the
// only thing on the controller's port, and gives client c's transactions the ID c (AXI section 4).
//   - A burst longer than turn_beats is cut into pieces of turn_beats (the last the rest), in order, each the client's
//     ID. A client sees its burst whole: its R beats in order with RLAST on the burst's last beat only; one B, OKAY if
//     every piece's was, else the first piece's that was not.
//   - Round robin, reads and writes apart: the AR arbiter gives the turn to one piece a cycle at most, the first client
//     after the last one served that has a burst waiting; the AW arbiter likewise, but passes one write piece at a time:
//     its address, then its beats on W, and only then the next turn, so W leaves in the order of the addresses.
//   - client_bursts bursts outstanding a client in each direction: beyond, its ARREADY or AWREADY stays 0.
//
// ITS REGISTERS. For each client: an AR slot and an AW slot, each a burst being cut (the slot's ready 1 while it is
// empty and the client has fewer than client_bursts outstanding); the lists of its outstanding bursts in each direction,
// each with the pieces still to be answered. The two round robin pointers; the write piece in turn (its client, the
// beats it has still to take, whether it is its burst's last piece); a FIFO of two W beats to the controller; the
// output registers. Every output is a register: an address, a beat and a response take one cycle through it.
//
// A CYCLE (axi_port.h): client_out(c) and controller_out() are the arbiter's outputs of the cycle; edge(t, clients,
// controller) takes the clients' and the controller's outputs of cycle t and ends it. No allocation in edge.
//
// RULES IT CHECKS (FATAL): a client's burst that is not a multiple of 16 or crosses 4 KB; a W beat whose WLAST is not on
// its burst's last beat; a beat or a response for an ID with nothing outstanding.
//
// THE RW ENGINE (AXI section 6) joins through RwEngineOnArbiter below: its `lpddr` port shares one address and length
// between AR and AW, which the adapter gives to both.
#pragma once

#include <cstdint>

#include "units/axi_port.h"
#include "units/rw_engine.h"

namespace units {

class AxiArbiter {
public:
    static constexpr unsigned C = axi::CLIENTS;
    static constexpr unsigned MAX_LIST = 16;

    explicit AxiArbiter(const axi::ArbiterConfig& cfg = {});

    const axi::SlaveOut& client_out(unsigned c) const { return cout_[c]; }
    const axi::MasterOut& controller_out() const { return mout_; }
    void edge(int64_t t, const axi::MasterOut* clients, const axi::SlaveOut& controller);
    bool idle() const;
    void reset();

    axi::ArbiterConfig cfg;

    // ---- statistics
    uint64_t read_pieces[C] = {}, write_pieces[C] = {}, read_bursts[C] = {}, write_bursts[C] = {};
    uint64_t read_waits[C] = {}, write_waits[C] = {};   // cycles a client's slot held a piece that did not get its turn

private:
    struct Slot {
        bool valid = false;
        uint64_t next = 0;      // the next piece's byte address
        unsigned left = 0;      // the beats still to cut
    };
    struct List {               // a client's outstanding bursts, oldest first: the pieces each has still to be answered
        unsigned pieces[MAX_LIST] = {};
        uint8_t resp[MAX_LIST] = {};
        unsigned head = 0, n = 0;
    };
    unsigned pieces_of(unsigned beats) const { return (beats + cfg.turn_beats - 1) / cfg.turn_beats; }
    static void check(const char* ch, unsigned c, uint64_t addr, unsigned len);

    axi::SlaveOut cout_[C];
    axi::MasterOut mout_;
    Slot ar_[C], aw_[C];
    List rl_[C], wl_[C];
    unsigned ar_rr_ = C - 1, aw_rr_ = C - 1;
    bool w_cur_ = false, w_cur_last_piece_ = false;
    unsigned w_cur_client_ = 0, w_cur_left_ = 0;
    struct WBeat { uint64_t d[2]; bool last; } wq_[2];
    unsigned wq_head_ = 0, wq_n_ = 0;
};

// The RW engine's `lpddr` port on the arbiter (AXI section 6). Each cycle a harness sets `in` to the arbiter's
// client_out for the engine's port before the engine steps; the engine's step calls cycle(), which records its outputs
// in `out`; the harness gives `out` to the arbiter's edge, then clears it with clear() (a cycle the engine did not call
// in, idle, its outputs are all 0).
struct RwEngineOnArbiter final : LpddrSlave {
    axi::SlaveOut in;
    axi::MasterOut out;
    LpddrIn cycle(int64_t, const LpddrOut& o) override {
        out = axi::MasterOut{};
        out.arvalid = o.arvalid; out.araddr = o.addr; out.arlen = uint8_t(o.len);
        out.awvalid = o.awvalid; out.awaddr = o.addr; out.awlen = uint8_t(o.len);
        out.wvalid = o.wvalid; out.wlast = o.wlast; out.wdata[0] = o.wdata_lo; out.wdata[1] = o.wdata_hi;
        LpddrIn r;
        r.arready = in.arready; r.awready = in.awready; r.wready = in.wready;
        r.rvalid = in.rvalid; r.rlast = in.rlast; r.rresp = in.rresp; r.rdata_lo = in.rdata[0]; r.rdata_hi = in.rdata[1];
        r.bvalid = in.bvalid; r.bresp = in.bresp;
        return r;
    }
    void clear() { out = axi::MasterOut{}; }
};

}  // namespace units
