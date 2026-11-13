// cnc.cpp (WORKER9): C&C 1's model (ref's CnC and Cnc1Peripheral), its program, the row collectors and C&C 2-3's
// endpoints. AUDIT-M2 section 4. C&C is one unit per chip: its step is out of line.
#include "units/cnc.h"

#include <cstring>

#include "base/fatal.h"
#include "check.h"
#include "units/linkword.h"

namespace units {

// ---- the program -----------------------------------------------------------------------------------------------------
CncProgram::CncProgram() {}

uint64_t CncProgram::parity() const {
    uint64_t p = 0;
    for (unsigned k = 0; k < 64; k++) p |= uint64_t(commands_to[k] & 1) << k;
    return p;
}

CncProgram& CncProgram::send(const CncPacket* p, size_t n) {
    if (n == 0) return *this;
    const uint32_t a = uint32_t(packets.size());
    packets.insert(packets.end(), p, p + n);
    steps.push_back({CncStep::SEND, a, uint32_t(packets.size())});
    return *this;
}

// CnC.loadWords: 128-bit mode-0 packets, four words each, addr + 16 per packet, the last padded with zero words.
CncProgram& CncProgram::load_words(unsigned dst, uint32_t byte_addr, const uint32_t* words, size_t n) {
    std::vector<CncPacket> ps;
    for (size_t i = 0; i < n; i += 4) {
        uint64_t w[4];
        for (size_t k = 0; k < 4; k++) w[k] = i + k < n ? words[i + k] : 0;
        ps.push_back({uint8_t(dst), lw::M_WRITE, 0, lw::SIZE_128, uint32_t(byte_addr + 4 * i), w[1] << 32 | w[0], w[3] << 32 | w[2]});
    }
    return send(ps.data(), ps.size());
}

// CnC.command: the arguments into the tile's next argument block (even commands slots 1-7, odd 8-15), then the command
// word with the block's parity in bit 23 into slot 0, last; the parity fixed when queued.
CncProgram& CncProgram::command(unsigned dst, uint32_t word, const uint32_t* args, size_t nargs) {
    if (nargs > kArgsPerBlock) FATAL("C&C program: a command with %zu arguments (at most 7)", nargs);
    unsigned par;
    if (dst == lw::BROADCAST) {
        par = commands_to[0] & 1;
        for (unsigned i = 0; i < 64; i++) {
            if ((commands_to[i] & 1) != par) FATAL("C&C program: broadcast command: tiles at different parities");
            commands_to[i]++;
        }
    } else {
        if (dst > 63) FATAL("C&C program: a command to %u", dst);
        par = commands_to[dst]++ & 1;
    }
    const unsigned base = par == 0 ? kArgsEven : kArgsOdd;
    std::vector<CncPacket> ps;
    for (size_t i = 0; i < nargs; i++)
        ps.push_back({uint8_t(dst), lw::M_WRITE, 0, lw::SIZE_32, uint32_t(kCommand + 4 * (base + i)), args[i], 0});
    const uint32_t w = (word & ~(1u << kCmdParityBit)) | par << kCmdParityBit;
    steps.push_back({CncStep::MARK_CMD, word, 0});
    ps.push_back({uint8_t(dst), lw::M_WRITE, 0, lw::SIZE_32, kCommand, w, 0});
    return send(ps.data(), ps.size());
}

CncProgram& CncProgram::go() {
    steps.push_back({CncStep::GO, 0, 0});
    steps.push_back({CncStep::SETTLE, 0, 0});   // removed by the unit when cnc_go_settle is off (see Cnc's constructor)
    return *this;
}
CncProgram& CncProgram::run_when_ready(uint32_t id) { go(); return call(id); }
CncProgram& CncProgram::at_all_ready(uint32_t id) { steps.push_back({CncStep::WAIT_READY, 0, 0}); return call(id); }
CncProgram& CncProgram::wait_cycles(uint32_t n) { steps.push_back({CncStep::WAIT, n, 0}); return *this; }
CncProgram& CncProgram::call(uint32_t id) {
    if (id >= kCalls) FATAL("C&C program: call id %u (at most %u)", id, kCalls - 1);
    steps.push_back({CncStep::CALL, id, 0});
    return *this;
}
CncProgram& CncProgram::call_step(uint32_t id) {        // harness-only
    if (id >= kCalls) FATAL("C&C program: call id %u (at most %u)", id, kCalls - 1);
    steps.push_back({CncStep::CALL_STEP, id, 0});
    return *this;
}
CncProgram& CncProgram::wait_host() { steps.push_back({CncStep::WAIT_HOST, 0, 0}); return *this; }   // W20 (harness-only)
CncProgram& CncProgram::release() {
    releases.push_back({uint32_t(steps.size()), parity()});
    return *this;
}

// ---- C&C 1 -----------------------------------------------------------------------------------------------------------
Cnc::Cnc(sim::Ports& p, const NetConfig& c, const CncProgram& program)
    : cnc_reaction(c.cnc_reaction), go_latency(c.go_latency), go_settle(c.cnc_go_settle), rx_depth(c.cnc_rx_depth) {
    if (!c.cnc_go_settle) FATAL("C&C: cnc_go_settle off is not modelled in M2");
    if (c.error_lat() != c.ready_latency) FATAL("C&C: error_latency %u != ready_latency %u (one line carries both in M2)", c.error_lat(), c.ready_latency);
    if (c.ready_latency < 3) FATAL("C&C: ready_latency %u (the row collectors need at least 3)", c.ready_latency);
    if (c.cnc_rx_depth > kRx) FATAL("C&C: cnc_rx_depth %u (at most %u modelled)", c.cnc_rx_depth, kRx);
    if (program.releases.empty() || program.releases.back().steps != program.steps.size())
        FATAL("C&C: the program must end with a release point (CncProgram::release)");
    p.out(send, "send", 3);
    p.in(rx, "rx", 3, c.queue_depth);
    for (unsigned r = 0; r < kRows; r++) p.in(rows[r], "row", 1);
    for (unsigned k = 0; k < 64; k++) p.out(go[k], "go", 0);
    prog = program.steps.data();
    packets = program.packets.data();
    releases = program.releases.data();
    n_releases = uint32_t(program.releases.size());
    released = releases[0].steps;
    parity = releases[0].parity;
    rel = 1;
    for (auto& r : ready_at) r = INT64_MIN;
    for (auto& a : call_at) a = -1;
}

void Cnc::release_next() {
    if (rel >= n_releases) FATAL("C&C: nothing left to release");
    released = releases[rel].steps;
    parity = releases[rel].parity;
    rel++;
}

bool Cnc::all_ready(int64_t t) const {
    if (expect == 0) return false;
    for (unsigned k = 0; k < 64; k++)
        if ((expect >> k & 1) && !ready(k, t)) return false;
    return true;
}

// W26: Endpoint.send's checks (invariants 5 and 21) as chip facts, at the send's cycle
void endpoint_checks(uint64_t t, unsigned address, const CncPacket& p) {
    const uint64_t h = lw::hdr(address, p.dst, p.mode, p.counter, p.addr, p.size);
    if (lw::bad_packet(p.mode, p.size, p.addr, 1)) sim::check::record(t, -1, sim::check::Kind::EP_BAD_PACKET, h, address);
    if (p.size == lw::SIZE_UNDEF) sim::check::record(t, -1, sim::check::Kind::EP_SIZE_UNDEF, 0, address);
    if (lw::reserved_mode(p.mode)) sim::check::record(t, -1, sim::check::Kind::EP_RESERVED_MODE, p.mode, address);
}

// Endpoint.send: src 64, the payload masked to the size; invariants 5 and 21 counted.
void Cnc::send_packet(sim::Ctx& c, const CncPacket& p) {
    const uint64_t h = lw::hdr(lw::CC1, p.dst, p.mode, p.counter, p.addr, p.size);
    if (p.size == lw::SIZE_UNDEF || lw::reserved_mode(p.mode)) violations++;
    endpoint_checks(c.now(), lw::CC1, p);              // W26 (ref counts the bad packet too; this counter, as before, doesn't)
    retry_w[0] = lw::mask_lo(p.lo, p.size); retry_w[1] = h; retry_w[2] = lw::mask_hi(p.hi, p.size);
    c.put(send, retry_w[0], retry_w[1], retry_w[2]);
    packets_sent++;
}

sim::Wake Cnc::after_step(sim::Ctx& c) {
    return cur < released ? c.next() : sim::Wake::none();
}

sim::Wake Cnc::step(sim::Ctx& c) {
    const int64_t t = int64_t(c.now());
    stepped++;
    // READY and the error lines, visible from their arrival (Cnc1Peripheral.readyAt, errChanges; CnC.errLine)
    for (unsigned r = 0; r < kRows; r++) {
        if (!c.ready(rows[r])) continue;
        const uint64_t w = c.peek(rows[r])[0];
        c.take(rows[r]);
        for (unsigned i = 0; i < 8; i++) {
            if (!(w >> (RowCollector::kPresent + i) & 1)) continue;
            const unsigned k = 8 * r + i;
            if (w >> (RowCollector::kEdges + i) & 1) {
                if (ready(k, t - 1)) ready_repulses++;
                ready_at[k] = t;
            }
            const uint64_t lvl = w >> (RowCollector::kLevels + i) & 1;
            err_line = (err_line & ~(1ull << k)) | lvl << k;
        }
    }
    err_line_seen = err_line != 0;
    if (err_line_seen && !error_bit) { error_bit = true; error_set_at = t; }
    if (err_line_seen && !toast) { toast = true; toast_at = t; }
    // the receive FIFO: one packet a cycle from tile 0's to-controller output
    bool more_rx = false;
    if (c.ready(rx)) {
        const sim::Word* w = c.peek(rx);
        const uint64_t d = w[0], h = w[1], hi = w[2];
        if (hs) hs->record(uint64_t(t), w, 64);              // W20 (harness-only): Endpoint.received
        c.take(rx);
        if (rx_count >= rx_depth) { rx_overflow = true; rx_lost++; sim::check::record(uint64_t(t), -1, sim::check::Kind::CNC_RX_LOST); }   // (W26: the fact)
        else {
            uint64_t* e = rxq[(rx_head + rx_count) % kRx];
            e[0] = d; e[1] = lw::src(h); e[2] = uint64_t(t); e[3] = hi; e[4] = lw::size(h);
            if (++rx_count > rx_peak) rx_peak = rx_count;
            rx_taken++;
        }
        more_rx = c.ready(rx);
    }
    const sim::Wake w = sequence(c, t);
    return more_rx ? c.next() : w;                            // another packet waits in the receive FIFO
}

sim::Wake Cnc::sequence(sim::Ctx& c, int64_t t) {
    // a refused send is presented again, and nothing else acts this cycle
    if (pending_retry && c.refused(send)) {
        refusals++;
        c.put(send, retry_w[0], retry_w[1], retry_w[2]);
        return c.next();
    }
    pending_retry = 0;
    if (reply_n) {                                            // W20 (harness-only): ref's replies first (drainReports)
        send_packet(c, reply_q[reply_h]);
        reply_h = (reply_h + 1) % 16; reply_n--;
        pending_retry = 1;
        return c.next();
    }
    if (hs && hs->due(t)) {                                   // W20 (harness-only): ref's Net.cncSends
        if (cur < released) FATAL("C&C 1: the harness's send (Net.cncSends) while C&C's queue runs: not modelled");
        send_packet(c, hs->pop());
        pending_retry = 1;
        return c.next();
    }
    // the sequencer: at most one register action a cycle; instant steps take none
    while (cur < released) {
        const CncStep& s = prog[cur];
        switch (s.kind) {
        case CncStep::MARK_CMD: last_command_word = s.a; cur++; continue;
        case CncStep::CALL: call_at[s.a] = t; cur++; continue;
        case CncStep::CALL_STEP: call_at[s.a] = t; cur++; return after_step(c);   // harness-only: takes its cycle
        case CncStep::WAIT_HOST:                              // W20 (harness-only): released between cycles by the harness
            if (host_released == host_waits) return sim::Wake::none();
            host_waits++; cur++; continue;
        case CncStep::SEND: {
            send_packet(c, packets[s.a + sub]);
            pending_retry = 1;                                // a refusal at t + 1 wakes C&C (the Hold's producer)
            if (s.a + ++sub == s.b) { cur++; sub = 0; }
            return after_step(c);
        }
        case CncStep::GO:
            if (seen < 0) {
                if (!all_ready(t)) return sim::Wake::none();  // woken by the READY that completes the set
                seen = t;
                last_all_ready_at = t;
            }
            if (t < seen + int64_t(cnc_reaction)) return sim::Wake::at_cycle(uint64_t(seen + cnc_reaction));
            cleared_at = t + 1;                               // PULSE_GO: clear READY (a pulse arriving at t + 1 is kept), GO
            for (unsigned k = 0; k < 64; k++) c.put(go[k]);
            gos++;
            last_go_at = t;
            if (plog) {                                       // W20 (harness-only): ref's phaseLog.add({seen, t})
                if (plog_n == plog_cap) FATAL("C&C: the harness's phase log is full (%u GOs)", plog_cap);
                plog[plog_n][0] = seen; plog[plog_n][1] = t; plog_n++;
            }
            seen = -1;
            cur++;
            return after_step(c);
        case CncStep::SETTLE: {
            const int64_t at = last_go_at + int64_t(go_latency) + 1;
            if (t < at) return sim::Wake::at_cycle(uint64_t(at));
            cur++;
            return after_step(c);
        }
        case CncStep::WAIT:
            if (wait_until < 0) wait_until = t + int64_t(s.a > 0 ? s.a : 1) - 1;
            if (t < wait_until) return sim::Wake::at_cycle(uint64_t(wait_until));
            wait_until = -1;
            cur++;
            return after_step(c);
        case CncStep::WAIT_READY:                             // SortTest.atAllReady's waitUntil: C&C's own
            if (!all_ready(t)) return sim::Wake::none();      //  lastAllReadyAt is go()'s alone in ref (W24, NOTES)
            cur++;
            return after_step(c);
        }
    }
    return hs ? hs->wake(c) : sim::Wake::none();              // W20 (harness-only): the next harness send
}

uint64_t Cnc::digest() const {
    uint64_t h = sim::hash_fields(cnc_reaction, go_latency, go_settle, rx_depth, n_releases, cur, released, rel, sub, seen,
                                  wait_until, parity, last_go_at, last_all_ready_at, last_command_word);
    h = sim::hash_mix(h, sim::hash_fields(pending_retry, retry_w, rx_head, rx_count, rx_overflow, error_bit, err_line_seen,
                                          error_set_at, ready_at, cleared_at, expect, err_line));
    for (uint32_t i = 0; i < rx_count; i++) h = sim::hash_mix(h, sim::hash_bytes(rxq[(rx_head + i) % kRx], sizeof rxq[0]));
    return h;
}

// ---- a row collector --------------------------------------------------------------------------------------------------
RowCollector::RowCollector(sim::Ports& p, unsigned r) : row(uint8_t(r)) {
    for (unsigned i = 0; i < 8; i++) p.in(line[i], "line", 1);
    p.out(out, "out", 1);
}

sim::Wake RowCollector::step(sim::Ctx& c) {
    uint64_t w = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!c.ready(line[i])) continue;
        const uint64_t v = c.peek(line[i])[0];
        c.take(line[i]);
        w |= (v & 1) << (kEdges + i) | 1ull << (kPresent + i) | (v >> 1 & 1) << (kLevels + i);
    }
    if (w) c.put(out, w);
    return sim::Wake::none();
}

// ---- C&C 2 and 3's endpoints ------------------------------------------------------------------------------------------
Endpoint::Endpoint(sim::Ports& p, unsigned addr, const NetConfig& c) : address(uint8_t(addr)) {
    p.out(send, "send", 3);
    p.in(rx, "rx", 3, c.queue_depth);
}

sim::Wake Endpoint::step(sim::Ctx& c) {
    if (hs) {                                                 // W20 (harness-only): the harness's sends and trace
        const int64_t t = int64_t(c.now());
        bool next = false;
        if (pending_retry && c.refused(send)) { c.put(send, retry_w[0], retry_w[1], retry_w[2]); next = true; }
        else {
            pending_retry = 0;
            if (hs->due(t)) {                                 // Endpoint.send: src its address, the payload masked
                const CncPacket& p = hs->pop();
                endpoint_checks(uint64_t(t), address, p);     // W26: Endpoint.send's checks as chip facts
                retry_w[0] = lw::mask_lo(p.lo, p.size); retry_w[1] = lw::hdr(address, p.dst, p.mode, p.counter, p.addr, p.size);
                retry_w[2] = lw::mask_hi(p.hi, p.size);
                c.put(send, retry_w[0], retry_w[1], retry_w[2]);
                pending_retry = 1; next = true;
            }
        }
        if (c.ready(rx)) { hs->record(uint64_t(t), c.peek(rx), address); c.take(rx); received++; }
        if (next || c.ready(rx)) return c.next();
        return hs->wake(c);
    }
    if (!c.ready(rx)) return sim::Wake::none();
    c.take(rx);
    received++;
    return c.ready(rx) ? c.next() : sim::Wake::none();
}

}  // namespace units
