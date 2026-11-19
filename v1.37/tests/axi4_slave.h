// axi4_slave.h: the LPDDR controller behind the RW engine's `lpddr` port, for the tests: AXI4 slave models (ARM IHI
// 0022; machine-spec 3). LPDDR and its controller are not chip hardware: the port is the simulator's boundary
// (src/units/rw_engine.h, THE LPDDR PORT), and a test places one of these on it. They are the RTL's tests' own slave
// models, ported line for line, with the RTL tests' random generator (Snarky One's rtl/verif: xorshift32), so that a
// test here gives the engine the timing the RTL's test gives the RTL's unit:
//   UnitLpddr  rtl/blocks/rw_engine_unit/test/src/rw_engine_unit_test.rs's `Lpddr`: random ready and valid timing on
//              every channel, a random latency, the reads answered in order and the writes in order, a write's data
//              taken before or after its address, no response before both its handshakes, random SLVERR and DECERR
//              responses, the reset dropping all it holds; it checks every burst it takes. Driven by the bench in
//              every cycle (drive, then advance), as the RTL's bench drives it.
//   ChipLpddr  rtl/chip/test/src/chip_memory_test.rs's `Lpddr` and `cycle`: one ID, answering in order, over a memory
//              whose beats are a function of their index until written; each ready 1 three cycles in four and a
//              latency below 8, at random, or, `fixed`, every ready 1 and one latency. It is an LpddrSlave: the engine
//              calls it in the cycles it is not idle, and it draws for the cycles between as the RTL's test does in
//              every cycle, so its random sequence is the RTL test's.
// And `Handshake`, rtl/verif's handshake monitor: the rules of a valid/ready channel, watched from outside.
#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "units/rw_engine.h"

namespace axi4 {

using u128 = unsigned __int128;
inline u128 make128(uint64_t hi, uint64_t lo) { return u128(hi) << 64 | lo; }
inline uint64_t lo64(u128 x) { return uint64_t(x); }
inline uint64_t hi64(u128 x) { return uint64_t(x >> 64); }

enum : unsigned { OKAY = 0, SLVERR = 2, DECERR = 3 };
enum : unsigned { SIZE_16_BYTES = 4, BURST_INCR = 1 };

// rtl/verif's Random: xorshift32 from a non-zero state.
inline uint32_t xorshift32(uint32_t v) { v ^= v << 13; v ^= v >> 17; return v ^ v << 5; }
struct Random {
    uint32_t s;
    explicit Random(uint32_t seed) : s(seed) {}
    uint32_t next() { s = xorshift32(s); return s; }
    uint32_t below(uint32_t bound) { return next() % bound; }
    bool one_in(uint32_t period) { return below(period) == 0; }
};

// rtl/verif's Handshake: one valid/ready port watched from outside; sample once a cycle, before the edge.
//   withdrawn  a request that waited (valid without ready) was not raised again in the next cycle
//   changed    a request that waited was raised again with a different payload
//   early      a request was raised while max_outstanding were outstanding, other than in a response's cycle
//   stray      a response arrived with no request outstanding
template <class P>
struct Handshake {
    uint32_t max_outstanding, outstanding = 0;
    bool waiting = false;
    P waited{};
    bool broken[4] = {};
    explicit Handshake(uint32_t max = UINT32_MAX) : max_outstanding(max) {}
    void sample(bool reset, bool valid, bool ready, const P& payload, bool response) {
        if (waiting) { broken[0] |= !valid; broken[1] |= valid && !(payload == waited); }
        broken[2] |= valid && outstanding == max_outstanding && !response;
        broken[3] |= response && outstanding == 0;
        if (reset) { outstanding = 0; waiting = false; return; }
        const bool accepted = valid && ready, answered = response && outstanding != 0;
        if (accepted && !answered) { if (outstanding < max_outstanding) outstanding++; }
        else if (answered && !accepted) outstanding--;
        waiting = valid && !ready;
        if (waiting) waited = payload;
    }
    bool ok() const { return !broken[0] && !broken[1] && !broken[2] && !broken[3]; }
};

// A burst on AR or AW: its LPDDR byte address, AxLEN, AxSIZE and AxBURST.
struct AddressBeat {
    uint64_t address = 0;
    unsigned len = 0, size = 0, burst = 0;
    bool operator==(const AddressBeat&) const = default;
};
inline AddressBeat address_of(const units::LpddrOut& o) { return {o.addr, o.len, units::LpddrOut::SIZE, units::LpddrOut::BURST}; }

// ================================================================ the RTL unit test's controller
struct Timing {
    uint32_t ready, valid;      // the readies' and valids' rates, out of 256
    uint32_t latency;           // the bound of its latency
    uint32_t error_period;      // one SLVERR or DECERR in this many responses (0: none)
};
constexpr Timing PROMPT{256, 256, 1, 0};
constexpr Timing SLOW{96, 128, 24, 0};

// LPDDR's contents before any write: a pattern of the row.
inline u128 initial_row(uint64_t row) {
    u128 value = 0;
    uint32_t state = (uint32_t(row) ^ uint32_t(row >> 32)) * 0x9E3779B9u | 1;
    for (int i = 0; i < 4; i++) { state = xorshift32(state); value = value << 32 | state; }
    return value;
}

struct UnitLpddr {
    struct ReadBurst { uint64_t row; uint32_t beats; uint64_t due; };
    struct WriteBeat { u128 data; unsigned strobe; bool last; };
    unsigned lpddr_bits;
    Timing timing;
    std::unordered_map<uint64_t, u128> memory;
    std::deque<ReadBurst> reads;
    std::deque<AddressBeat> write_bursts;
    std::deque<WriteBeat> write_beats;
    std::deque<bool> write_known;                         // each beat taken: every word of it defined (the bench says)
    std::unordered_set<uint64_t> unknown;                 // the rows written with a beat that was not
    std::deque<uint64_t> responses;                       // each write's response: the cycle from which it may come
    std::vector<std::pair<bool, AddressBeat>> bursts;     // every burst taken, a write's flagged, in order
    unsigned failures = 0;                                // a burst or a beat that broke a rule
    std::string first_failure;

    UnitLpddr(unsigned bits, Timing t) : lpddr_bits(bits), timing(t) {}
    u128 row(uint64_t r) const { auto it = memory.find(r); return it == memory.end() ? initial_row(r) : it->second; }
    bool idle() const { return reads.empty() && write_bursts.empty() && write_beats.empty() && responses.empty(); }
    void fail(const std::string& what) { if (failures++ == 0) first_failure = what; }

    // The controller's outputs in this cycle.
    units::LpddrIn drive(uint64_t cycle, Random& random) const {
        const Timing t = timing;
        units::LpddrIn a;
        a.awready = random.below(256) < t.ready;
        a.wready = random.below(256) < t.ready;
        a.arready = random.below(256) < t.ready;
        auto error = [&]() -> unsigned {
            if (t.error_period == 0 || !random.one_in(t.error_period)) return OKAY;
            return random.one_in(2) ? SLVERR : DECERR;
        };
        a.bvalid = !responses.empty() && responses.front() <= cycle && random.below(256) < t.valid;
        a.bresp = a.bvalid ? error() : random.below(4);
        a.rvalid = !reads.empty() && reads.front().due <= cycle && random.below(256) < t.valid;
        a.rresp = a.rvalid ? error() : random.below(4);
        if (a.rvalid) {
            const u128 d = row(reads.front().row);
            a.rdata_lo = lo64(d); a.rdata_hi = hi64(d);
            a.rlast = reads.front().beats == 1;
        } else {
            const uint64_t top = random.next();           // u128(next) << 96 | u128(next)
            const uint64_t low = random.next();
            a.rdata_hi = top << 32; a.rdata_lo = low;
            a.rlast = random.one_in(2);
        }
        return a;
    }

    // A burst's checks (3, AXI4): INCR, 16-byte beats, aligned, within one 4 KiB page and within LPDDR.
    void check_burst(const AddressBeat& b, const char* channel) {
        if (b.size != SIZE_16_BYTES) fail(std::string(channel) + ": AxSIZE");
        if (b.burst != BURST_INCR) fail(std::string(channel) + ": AxBURST");
        if (b.address % 16 != 0) fail(std::string(channel) + ": an address not 16-byte aligned");
        const uint64_t bytes = 16 * (uint64_t(b.len) + 1);
        if (b.address % 4096 + bytes > 4096) fail(std::string(channel) + ": a burst across a 4 KiB boundary");
        if (b.address + bytes > uint64_t(1) << lpddr_bits) fail(std::string(channel) + ": a burst past LPDDR's end");
    }

    // The edge: beats and responses given leave, bursts and data taken come in flight, answered no earlier than the
    // next cycle; the reset drops them all.
    // `w_known`: the W beat offered holds defined data alone (a buffer row written in full), by the bench.
    void advance(bool reset, const units::LpddrIn& a, const units::LpddrOut& o, uint64_t cycle, Random& random, bool w_known = true) {
        if (reset) { reads.clear(); write_bursts.clear(); write_beats.clear(); write_known.clear(); responses.clear(); return; }
        if (a.rvalid) {
            ReadBurst& r = reads.front();
            r.beats--; r.row++;
            if (r.beats == 0) reads.pop_front();
        }
        if (a.bvalid) responses.pop_front();
        const uint32_t latency = timing.latency < 1 ? 1 : timing.latency;
        auto due = [&]() { return cycle + 1 + random.below(latency); };
        if (o.arvalid && a.arready) {
            const AddressBeat ar = address_of(o);
            check_burst(ar, "AR");
            bursts.emplace_back(false, ar);
            const uint64_t after = reads.empty() ? 0 : reads.back().due;
            const uint64_t d = due();
            reads.push_back({ar.address >> 4, ar.len + 1, d > after ? d : after});
        }
        if (o.awvalid && a.awready) {
            const AddressBeat aw = address_of(o);
            check_burst(aw, "AW");
            bursts.emplace_back(true, aw);
            write_bursts.push_back(aw);
        }
        if (o.wvalid && a.wready) { write_beats.push_back({make128(o.wdata_hi, o.wdata_lo), units::LpddrOut::WSTRB, o.wlast}); write_known.push_back(w_known); }
        // A burst whose address and every beat have come is written, and answered from the next cycle at the earliest.
        while (!write_bursts.empty()) {
            const AddressBeat burst = write_bursts.front();
            const size_t beats = burst.len + 1;
            if (write_beats.size() < beats) break;
            write_bursts.pop_front();
            for (size_t i = 0; i < beats; i++) {
                const WriteBeat b = write_beats.front();
                write_beats.pop_front();
                if (b.strobe != 0xFFFF) fail("WSTRB: a write's beats enable every byte");
                if (b.last != (i == beats - 1)) fail("WLAST on beat " + std::to_string(i) + " of " + std::to_string(beats));
                memory[(burst.address >> 4) + i] = b.data;
                if (write_known.front()) unknown.erase((burst.address >> 4) + i); else unknown.insert((burst.address >> 4) + i);
                write_known.pop_front();
            }
            const uint64_t after = responses.empty() ? 0 : responses.back();
            const uint64_t d = due();
            responses.push_back(d > after ? d : after);
        }
    }
};

// ================================================================ the RTL chip test's controller
// The model's memory: a beat a function of its index until written (chip_memory_test.rs's lpddr_word).
inline u128 chip_lpddr_word(uint64_t index) {
    const u128 i = index;
    return (i ^ 0x01234567) << 96 | (i * 3) << 64 | (i ^ 0x89ABCDEF) << 32 | i;
}

struct ChipLpddr final : units::LpddrSlave {
    struct Burst {
        uint64_t beat = 0; uint32_t beats = 0; unsigned size = 0, kind = 0;
        bool operator==(const Burst&) const = default;
    };
    struct Read { Burst burst; int64_t due; uint32_t sent; };
    struct Beat { u128 data = 0; unsigned strobes = 0; bool last = false; bool operator==(const Beat&) const = default; };
    struct Written { uint64_t index; u128 data; unsigned strobes; bool last; };
    struct Event { int64_t t; char channel; uint64_t a, b; };   // a handshake: 'A' AR, 'W' AW, 'D' a W beat, 'R', 'B'

    // ---- the timing ----
    static constexpr uint32_t LATENCY = 8;       // the RTL test's bound
    bool fixed = false;                          // every ready 1 and each latency `fixed_latency`, no draw
    uint32_t fixed_latency = 0;                  // a response due fixed_latency + 1 cycles after its last handshake
    Random random;
    uint64_t lpddr_beats;
    int64_t next_t;                              // the next cycle to draw for: the cycles the engine did not call in
                                                 //   draw their three readies here, as the RTL's test does every cycle

    std::unordered_map<uint64_t, u128> words;
    std::deque<Read> reads;                      // the read bursts in flight
    std::deque<int64_t> responses;               // the write responses due
    std::deque<Burst> writes;                    // the write bursts taken
    uint32_t written = 0;                        // the beats written of the first
    std::deque<Beat> pending;                    // the beats taken before their burst's address
    std::vector<std::pair<bool, Burst>> bursts;  // every burst in the order taken, a write's flagged
    std::vector<Written> beats;                  // every written beat
    std::vector<Event> events;                   // every handshake with its cycle
    Handshake<AddressBeat> ar_monitor, aw_monitor;
    Handshake<Beat> w_monitor;
    uint64_t calls = 0;

    // `first_cycle`: the cycle the slave's time starts at, the RTL test's first cycle (its reset's first), which the
    // random sequence is counted from.
    ChipLpddr(uint32_t seed, unsigned lpddr_bits, int64_t first_cycle)
        : random(seed), lpddr_beats(uint64_t(1) << (lpddr_bits - 4)), next_t(first_cycle) {}
    u128 word(uint64_t index) const { auto it = words.find(index); return it == words.end() ? chip_lpddr_word(index) : it->second; }
    bool idle() const { return reads.empty() && responses.empty() && writes.empty() && pending.empty(); }

    units::LpddrIn cycle(int64_t now, const units::LpddrOut& o) override {
        calls++;
        bool ready[3] = {true, true, true};      // AW, W, AR
        if (!fixed) {
            for (; next_t < now; next_t++) for (int k = 0; k < 3; k++) random.below(4);
            for (int k = 0; k < 3; k++) ready[k] = random.below(4) != 0;
            next_t = now + 1;
        }
        units::LpddrIn in;
        in.awready = ready[0]; in.wready = ready[1]; in.arready = ready[2];
        const bool beat = !reads.empty() && reads.front().due <= now;
        const bool response = !responses.empty() && responses.front() <= now;
        if (beat) {
            const Read& r = reads.front();
            const u128 d = word((r.burst.beat + r.sent) % lpddr_beats);
            in.rvalid = true; in.rdata_lo = lo64(d); in.rdata_hi = hi64(d); in.rlast = r.sent + 1 == r.burst.beats;
        }
        in.bvalid = response;
        const AddressBeat a = address_of(o);
        const Beat w{make128(o.wdata_hi, o.wdata_lo), units::LpddrOut::WSTRB, o.wlast};
        ar_monitor.sample(false, o.arvalid, ready[2], a, false);
        aw_monitor.sample(false, o.awvalid, ready[0], a, false);
        w_monitor.sample(false, o.wvalid, ready[1], w, false);
        auto latency = [&]() { return fixed ? fixed_latency : random.below(LATENCY); };
        if (beat) {                                              // RREADY is 1
            Read& r = reads.front();
            events.push_back({now, 'R', r.burst.beat + r.sent, in.rlast});
            if (++r.sent == r.burst.beats) reads.pop_front();
        }
        if (response) { events.push_back({now, 'B', 0, 0}); responses.pop_front(); }   // BREADY is 1
        if (o.arvalid && ready[2]) {
            const Burst b{a.address >> 4, a.len + 1, a.size, a.burst};
            bursts.emplace_back(false, b);
            events.push_back({now, 'A', b.beat, b.beats});
            int64_t due = now + 1 + latency();
            if (!reads.empty() && reads.back().due > due) due = reads.back().due;
            reads.push_back({b, due, 0});
        }
        if (o.awvalid && ready[0]) {
            const Burst b{a.address >> 4, a.len + 1, a.size, a.burst};
            bursts.emplace_back(true, b);
            events.push_back({now, 'W', b.beat, b.beats});
            writes.push_back(b);
        }
        if (o.wvalid && ready[1]) { pending.push_back(w); events.push_back({now, 'D', lo64(w.data), w.last}); }
        // Each beat taken, once its burst's address is in (AXI4 lets a beat come before it).
        while (!writes.empty() && !pending.empty()) {
            const Burst burst = writes.front();
            const Beat b = pending.front();
            pending.pop_front();
            const uint64_t index = (burst.beat + written) % lpddr_beats;
            u128 value = word(index);
            for (unsigned byte = 0; byte < 16; byte++)
                if (b.strobes >> byte & 1) { const u128 mask = u128(0xFF) << (8 * byte); value = (value & ~mask) | (b.data & mask); }
            words[index] = value;
            beats.push_back({index, b.data, b.strobes, b.last});
            if (++written == burst.beats) {
                writes.pop_front();
                written = 0;
                responses.push_back(now + 1 + latency());
            }
        }
        return in;
    }
};

}  // namespace axi4
