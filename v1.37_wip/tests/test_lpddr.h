// test_lpddr.h: a test LPDDR on one client's AXI port, for the Updater's controller (updater_wip, WORKER6; NOTES section
// 8). It is AXI-INTERFACE.md section 5's model cut down to one client with no ID (AXI below): a slave on the file store's
// pages, keeping AXI's rules, with a fixed latency or a jitter mode. **A test piece:** WORKER5's arbiter and LPDDR model
// take its place when the two controllers are joined.
//
//   - fixed: every ready 1 but at a limit; a read's first beat `read_latency` after its address handshake at the
//     earliest, a write's response `write_latency` after the later of its address and last beat; one beat a cycle.
//   - jitter (a seed): each burst's latency from jitter_lo to jitter_hi; each ready 0 one cycle in four.
//   - AXI's order within one ID: reads answered in order, writes in order; a read beat takes its data from the store as
//     it is sent; a write's beats reach the store as its response is sent.
//   - faults (axi::LpddrFault): a burst touching the range answers its response; a faulty read's beats carry FAULT_DATA,
//     a faulty write writes nothing. Past LPDDR's end: DECERR, the same.
//   - checks that stop the simulator: a burst across 4 KB, an address not a multiple of 16, more than 256 beats, wlast
//     out of place, a valid withdrawn or its request changed while it waited, a beat or response the client did not
//     take (it has no rready or bready: it takes all).
//   - counted, for a test to fail on: a read of a page never written (`unwritten`), a write the harness's rule refuses
//     (`allow_write`, with the reason kept).
//
// Every output a register: out() returns them; edge(t, m) takes the client's outputs of t and makes the next.
#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "units/axi_port.h"
#include "units/file_store.h"
#include "units/updater_top.h"

namespace test_pieces {

using units::tracer::FileStore;
namespace axi = units::axi;

struct Rand {                                      // splitmix64, the tests' Rng
    uint64_t s;
    explicit Rand(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }
    uint64_t below(uint64_t n) { return next() % n; }
};

struct TestLpddr final : units::updctl::AxiDev {
    FileStore& store;
    axi::LpddrTiming tm;
    int64_t lpddr_units = int64_t(1) << 25;       // LPDDR's end, 128-byte units
    std::vector<axi::LpddrFault> faults;
    // The harness's rule for writes (null: none): false refuses a write to that page, counted with the reason.
    std::function<bool(uint32_t page_unit, std::string& why)> allow_write;

    // ---- counts
    uint64_t read_bursts = 0, write_bursts = 0, read_beats = 0, write_beats = 0, unwritten = 0, refused = 0, error_bursts = 0;
    uint64_t busy_cycles = 0;
    std::string refused_why;

    TestLpddr(FileStore& s, const axi::LpddrTiming& t = {}) : store(s), tm(t), rnd(t.seed) { o.arready = o.awready = o.wready = true; }

    axi::SlaveOut out() const override { return o; }
    bool quiet() const override { return reads.empty() && writes.empty() && wbeats.empty() && !o.rvalid && !o.bvalid; }
    uint64_t violations() const override { return unwritten + refused; }

    // A write accepted and not yet answered that touches the page at `unit` (128-byte units): the stand-in's check that
    // the data is in LPDDR before the index points to it.
    bool write_in_flight(uint32_t unit) const {
        for (const Write& w : writes)
            if ((w.addr >> 12) == (uint64_t(unit) * 128) >> 12) return true;
        return false;
    }

    void edge(int64_t t, const axi::MasterOut& m) override {
        if (!quiet()) busy_cycles++;
        check_held(m);
        const axi::SlaveOut now = o;                   // the outputs of t, against which m moved
        // ---- the handshakes of t
        if (o.arready && m.arvalid) {
            Read r;
            r.addr = m.araddr; r.beats = unsigned(m.arlen) + 1; r.due = t + latency(tm.read_latency);
            rules(r.addr, r.beats, "a read");
            r.resp = fault(r.addr, r.beats, true);
            reads.push_back(r);
            read_bursts++;
        }
        if (o.awready && m.awvalid) {
            Write w;
            w.addr = m.awaddr; w.beats = unsigned(m.awlen) + 1; w.aw_t = t;
            rules(w.addr, w.beats, "a write");
            w.resp = fault(w.addr, w.beats, false);
            if (allow_write) {
                std::string why;
                if (!allow_write(uint32_t((w.addr >> 12) * 32), why)) {
                    refused++;
                    if (refused_why.empty()) refused_why = why;
                }
            }
            writes.push_back(w);
            write_bursts++;
        }
        if (o.wready && m.wvalid) wbeats.push_back({{m.wdata[0], m.wdata[1]}, m.wlast, t});
        // the W beats to the writes, in the order of their addresses
        for (Write& w : writes) {
            while (w.got.size() < w.beats && !wbeats.empty()) {
                const WBeat b = wbeats.front();
                wbeats.pop_front();
                const bool last = w.got.size() + 1 == w.beats;
                if (b.last != last) FATAL("test LPDDR: wlast %s at beat %zu of a write of %u beats at 0x%llx", b.last ? "set" : "missing", w.got.size(), w.beats,
                                          (unsigned long long)w.addr);
                w.got.push_back({b.data[0], b.data[1]});
                if (last) { w.last_t = b.t; w.due = (w.aw_t > b.t ? w.aw_t : b.t) + latency(tm.write_latency); }
            }
            if (w.got.size() < w.beats) break;
        }
        // ---- R: the beat in flight was taken (rready is 1); the next
        o.rvalid = false; o.rlast = false;
        if (!reads.empty()) {
            Read& r = reads.front();
            if (r.sent > 0 || r.due <= t + 1) {
                const uint64_t a = r.addr + 16 * uint64_t(r.sent);
                o.rvalid = true;
                o.rresp = r.resp;
                if (r.resp == axi::OKAY) beat_of(a, o.rdata);
                else o.rdata[0] = o.rdata[1] = axi::FAULT_DATA;
                o.rlast = ++r.sent == r.beats;
                read_beats++;
                if (o.rlast) reads.pop_front();
            }
        }
        // ---- B: the next response due; its write lands now
        o.bvalid = false;
        if (!writes.empty()) {
            const Write& w = writes.front();
            if (w.got.size() == w.beats && w.due <= t + 1) {
                if (w.resp == axi::OKAY)
                    for (unsigned k = 0; k < w.beats; k++) put_beat(w.addr + 16 * uint64_t(k), w.got[k].v);
                write_beats += w.beats;
                o.bvalid = true; o.bresp = w.resp;
                writes.pop_front();
            }
        }
        // ---- the readies of t + 1
        const bool coin = !tm.jitter || rnd.below(4) != 0;
        o.arready = reads.size() < tm.max_reads && (!tm.jitter || rnd.below(4) != 0);
        o.awready = writes.size() < tm.max_writes && (!tm.jitter || rnd.below(4) != 0);
        o.wready = coin && wbeats.size() < 512;
        prev = m;
        prev_o = now;
    }

private:
    struct Read { uint64_t addr = 0; unsigned beats = 0, sent = 0; int64_t due = 0; uint8_t resp = axi::OKAY; };
    struct Beat { uint64_t v[2]; };
    struct Write { uint64_t addr = 0; unsigned beats = 0; int64_t aw_t = 0, last_t = 0, due = INT64_MAX; uint8_t resp = axi::OKAY; std::vector<Beat> got; };
    struct WBeat { uint64_t data[2]; bool last; int64_t t; };

    axi::SlaveOut o;
    Rand rnd;
    std::deque<Read> reads;
    std::deque<Write> writes;
    std::deque<WBeat> wbeats;
    axi::MasterOut prev;
    axi::SlaveOut prev_o;

    int64_t latency(unsigned fixed) { return tm.jitter ? int64_t(tm.jitter_lo + rnd.below(tm.jitter_hi - tm.jitter_lo + 1)) : int64_t(fixed); }

    // a valid held with its request until it moves (AXI; machine-spec 3, "Handshakes")
    void check_held(const axi::MasterOut& m) {
        if (prev.arvalid && !prev_o.arready && (!m.arvalid || m.araddr != prev.araddr || m.arlen != prev.arlen))
            FATAL("test LPDDR: AR withdrawn or changed while it waited");
        if (prev.awvalid && !prev_o.awready && (!m.awvalid || m.awaddr != prev.awaddr || m.awlen != prev.awlen))
            FATAL("test LPDDR: AW withdrawn or changed while it waited");
        if (prev.wvalid && !prev_o.wready && (!m.wvalid || m.wdata[0] != prev.wdata[0] || m.wdata[1] != prev.wdata[1] || m.wlast != prev.wlast))
            FATAL("test LPDDR: a W beat withdrawn or changed while it waited");
    }
    void rules(uint64_t a, unsigned beats, const char* what) {
        if (a % 16) FATAL("test LPDDR: %s at 0x%llx, not a multiple of 16", what, (unsigned long long)a);
        if (beats < 1 || beats > axi::MAX_BURST_BEATS) FATAL("test LPDDR: %s of %u beats", what, beats);
        if (a / axi::BOUNDARY_BYTES != (a + 16 * uint64_t(beats) - 1) / axi::BOUNDARY_BYTES)
            FATAL("test LPDDR: %s of %u beats at 0x%llx crosses 4 KB", what, beats, (unsigned long long)a);
    }
    uint8_t fault(uint64_t a, unsigned beats, bool read) {
        const uint64_t last = a + 16 * uint64_t(beats) - 1;
        if (last >= uint64_t(lpddr_units) * 128) { error_bursts++; return axi::DECERR; }
        for (axi::LpddrFault& f : faults) {
            if ((read ? !f.reads : !f.writes) || last < f.first || a > f.last) continue;
            if (f.bursts == 1) f.bursts = UINT32_MAX;              // the last one used: retired below
            else if (f.bursts == UINT32_MAX) continue;
            else if (f.bursts > 1) f.bursts--;
            error_bursts++;
            return f.resp;
        }
        return axi::OKAY;
    }
    void beat_of(uint64_t a, uint64_t* out) {
        const int32_t unit = int32_t((a >> 12) * 32);
        if (!store.has_page(unit)) { out[0] = out[1] = 0; unwritten++; return; }
        const uint64_t* pg = store.raw_page(unit);
        const unsigned w = unsigned(a & 4095) / 8;
        out[0] = pg[w]; out[1] = pg[w + 1];
    }
    void put_beat(uint64_t a, const uint64_t* v) {
        uint64_t* pg = store.page_at(int32_t((a >> 12) * 32));
        const unsigned w = unsigned(a & 4095) / 8;
        pg[w] = v[0]; pg[w + 1] = v[1];
    }
};

}  // namespace test_pieces
