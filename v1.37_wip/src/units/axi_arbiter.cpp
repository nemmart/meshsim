// axi_arbiter.cpp: AxiArbiter (axi_arbiter.h).
#include "units/axi_arbiter.h"

#include "base/fatal.h"

namespace units {

AxiArbiter::AxiArbiter(const axi::ArbiterConfig& c) : cfg(c) {
    if (c.turn_beats < 1 || c.turn_beats > axi::MAX_BURST_BEATS) FATAL("axi_arbiter: turn_beats must be 1 to 256");
    if (c.client_bursts < 1 || c.client_bursts > MAX_LIST) FATAL("axi_arbiter: client_bursts must be 1 to %u", MAX_LIST);
    reset();
}

void AxiArbiter::reset() {
    for (unsigned c = 0; c < C; c++) {
        ar_[c] = Slot{}; aw_[c] = Slot{}; rl_[c] = List{}; wl_[c] = List{};
        cout_[c] = axi::SlaveOut{};
        cout_[c].arready = cout_[c].awready = true;
    }
    mout_ = axi::MasterOut{};
    ar_rr_ = aw_rr_ = C - 1;
    w_cur_ = w_cur_last_piece_ = false; w_cur_client_ = 0; w_cur_left_ = 0;
    wq_head_ = wq_n_ = 0;
}

bool AxiArbiter::idle() const {
    for (unsigned c = 0; c < C; c++)
        if (ar_[c].valid || aw_[c].valid || rl_[c].n || wl_[c].n || cout_[c].rvalid || cout_[c].bvalid) return false;
    return !mout_.arvalid && !mout_.awvalid && wq_n_ == 0 && !w_cur_;
}

void AxiArbiter::check(const char* ch, unsigned c, uint64_t addr, unsigned len) {
    if (addr % axi::BEAT_BYTES) FATAL("axi_arbiter: client %u's %s: an address not a multiple of 16: 0x%llx", c, ch, (unsigned long long)addr);
    if (addr % axi::BOUNDARY_BYTES + uint64_t(len + 1) * axi::BEAT_BYTES > axi::BOUNDARY_BYTES)
        FATAL("axi_arbiter: client %u's %s: a burst across a 4 KB boundary: 0x%llx, %u beats", c, ch, (unsigned long long)addr, len + 1);
}

void AxiArbiter::edge(int64_t t, const axi::MasterOut* in, const axi::SlaveOut& ctl) {
    axi::SlaveOut next_c[C];                     // the clients' outputs of t + 1: R and B set below, readies at the end
    const axi::MasterOut m = mout_;              // the arbiter's outputs of t to the controller

    // ---- the controller's side in t
    bool ar_busy = m.arvalid && !ctl.arready;    // an address still waiting at the end of t
    bool aw_busy = m.awvalid && !ctl.awready;
    if (m.wvalid && ctl.wready) { wq_head_ = (wq_head_ + 1) % 2; wq_n_--; }
    if (ctl.rvalid) {
        const unsigned c = ctl.rid;
        List& l = rl_[c];
        if (c >= C || l.n == 0) FATAL("axi_arbiter: an R beat for ID %u, which has no read outstanding, at %lld", c, (long long)t);
        bool last = false;
        if (ctl.rlast && --l.pieces[l.head] == 0) { last = true; l.head = (l.head + 1) % MAX_LIST; l.n--; }
        axi::SlaveOut& o = next_c[c];
        o.rvalid = true; o.rdata[0] = ctl.rdata[0]; o.rdata[1] = ctl.rdata[1]; o.rresp = ctl.rresp; o.rlast = last;
    }
    if (ctl.bvalid) {
        const unsigned c = ctl.bid;
        List& l = wl_[c];
        if (c >= C || l.n == 0) FATAL("axi_arbiter: a B response for ID %u, which has no write outstanding, at %lld", c, (long long)t);
        if (l.resp[l.head] == axi::OKAY) l.resp[l.head] = ctl.bresp;
        if (--l.pieces[l.head] == 0) {
            next_c[c].bvalid = true; next_c[c].bresp = l.resp[l.head];
            l.head = (l.head + 1) % MAX_LIST; l.n--;
        }
    }

    // ---- the clients' side in t
    for (unsigned c = 0; c < C; c++) {
        const axi::MasterOut& x = in[c];
        const axi::SlaveOut& o = cout_[c];
        if (x.arvalid && o.arready) {
            check("AR", c, x.araddr, x.arlen);
            ar_[c] = Slot{true, x.araddr, x.arlen + 1u};
            List& l = rl_[c];
            const unsigned k = (l.head + l.n) % MAX_LIST;
            l.pieces[k] = pieces_of(x.arlen + 1u); l.resp[k] = axi::OKAY; l.n++;
            read_bursts[c]++;
        }
        if (x.awvalid && o.awready) {
            check("AW", c, x.awaddr, x.awlen);
            aw_[c] = Slot{true, x.awaddr, x.awlen + 1u};
            List& l = wl_[c];
            const unsigned k = (l.head + l.n) % MAX_LIST;
            l.pieces[k] = pieces_of(x.awlen + 1u); l.resp[k] = axi::OKAY; l.n++;
            write_bursts[c]++;
        }
        if (x.wvalid && o.wready) {               // wready is only ever 1 for the client whose piece is in turn
            const bool burst_last = w_cur_last_piece_ && w_cur_left_ == 1;
            if (x.wlast != burst_last)
                FATAL("axi_arbiter: client %u's W beat at %lld: wlast %s, and it is %sthe burst's last", c, (long long)t,
                      x.wlast ? "set" : "clear", burst_last ? "" : "not ");
            WBeat& b = wq_[(wq_head_ + wq_n_) % 2];
            b.d[0] = x.wdata[0]; b.d[1] = x.wdata[1]; b.last = w_cur_left_ == 1;
            wq_n_++;
            if (--w_cur_left_ == 0) w_cur_ = false;
        }
    }

    // ---- the turns
    axi::MasterOut n = m;
    n.wvalid = false;
    int ar_served = -1, aw_served = -1;
    if (!ar_busy) {
        n.arvalid = false;
        for (unsigned k = 1; k <= C; k++) {
            const unsigned c = (ar_rr_ + k) % C;
            if (!ar_[c].valid) continue;
            Slot& s = ar_[c];
            const unsigned beats = s.left < cfg.turn_beats ? s.left : cfg.turn_beats;
            n.arvalid = true; n.araddr = s.next; n.arlen = uint8_t(beats - 1); n.arid = uint8_t(c);
            s.next += uint64_t(beats) * axi::BEAT_BYTES; s.left -= beats;
            if (s.left == 0) s.valid = false;
            ar_rr_ = c; ar_served = int(c); read_pieces[c]++;
            break;
        }
    }
    if (!aw_busy) n.awvalid = false;
    if (!aw_busy && !w_cur_) {
        for (unsigned k = 1; k <= C; k++) {
            const unsigned c = (aw_rr_ + k) % C;
            if (!aw_[c].valid) continue;
            Slot& s = aw_[c];
            const unsigned beats = s.left < cfg.turn_beats ? s.left : cfg.turn_beats;
            n.awvalid = true; n.awaddr = s.next; n.awlen = uint8_t(beats - 1); n.awid = uint8_t(c);
            s.next += uint64_t(beats) * axi::BEAT_BYTES; s.left -= beats;
            if (s.left == 0) s.valid = false;
            w_cur_ = true; w_cur_client_ = c; w_cur_left_ = beats; w_cur_last_piece_ = !s.valid;
            aw_rr_ = c; aw_served = int(c); write_pieces[c]++;
            break;
        }
    }
    for (unsigned c = 0; c < C; c++) {
        if (ar_[c].valid && ar_served != int(c)) read_waits[c]++;
        if (aw_[c].valid && aw_served != int(c) && !(w_cur_ && w_cur_client_ == c)) write_waits[c]++;
    }
    if (wq_n_ > 0) {
        const WBeat& b = wq_[wq_head_];
        n.wvalid = true; n.wdata[0] = b.d[0]; n.wdata[1] = b.d[1]; n.wlast = b.last;
    }
    mout_ = n;

    // ---- the readies of t + 1
    for (unsigned c = 0; c < C; c++) {
        next_c[c].arready = !ar_[c].valid && rl_[c].n < cfg.client_bursts;
        next_c[c].awready = !aw_[c].valid && wl_[c].n < cfg.client_bursts;
        next_c[c].wready = w_cur_ && w_cur_client_ == c && wq_n_ <= 1;
        cout_[c] = next_c[c];
    }
}

}  // namespace units
