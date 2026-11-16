// ie_far_end.h: the far ends of Ingress/Egress's pins and ports (machine-spec 17.6, 18, 3), for the tests: the serial
// line's other end and the LVDS IO block, which are not chip hardware. A test attaches one to the master tile's
// Ingress/Egress (units/ingress_egress.h, THE PINS AND THE PORTS) with attach(): Ingress/Egress then calls cycle() once
// in every cycle t, with its outputs' levels in t, and takes the inputs' levels for t that the far end gives.
//
//   the trace     every output's level in every cycle from the first one stepped: at(t). Checked contiguous.
//   uart_rx       `rx`, the pin's level by cycle (1, the idle line, past its end); frame() and needle_frame() write
//                 frames into it.
//   lvds_in       `offers`, the beats the block offers in order: each from its cycle `from` (or from the cycle after
//                 the beat before it was taken, if that is later), TVALID, TDATA and TLAST held until the handshake
//                 (AXI4-Stream); `taken_at` has each handshake's cycle.
//   lvds_out      TREADY in cycle t is `ready(t)` (none: 0); `got` has each handshake's cycle, TDATA and TLAST.
//   `drive`       if set, gives a cycle's inputs itself, after the scripts above (a test whose far end is its own).
//
// decode_tx() reads the frames off the trace of uart_tx: for each falling edge after an idle line or a stop bit, the
// 10 bits of `divisor` cycles each, every bit's level checked in every one of its cycles.
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "units/ingress_egress.h"

struct IeFarEnd {
    using Pins = units::IngressEgress::Pins;

    // ---- the trace ----
    int64_t first = -1;
    std::vector<Pins::Out> out;
    bool traced(int64_t t) const { return first >= 0 && t >= first && t < first + int64_t(out.size()); }
    const Pins::Out& at(int64_t t) const {
        if (!traced(t)) FATAL("IeFarEnd: no trace of cycle %lld (from %lld, %zu cycles)", (long long)t, (long long)first, out.size());
        return out[size_t(t - first)];
    }

    // ---- uart_rx ----
    std::vector<uint8_t> rx;
    void rx_level(int64_t t, bool level) {
        if (size_t(t) >= rx.size()) rx.resize(size_t(t) + 1, 1);
        rx[size_t(t)] = level;
    }
    // A frame whose start bit's falling edge is on the pin in cycle c: the start bit, `byte`'s bits, the least
    // significant first, and the stop bit (`stop`: 1, or 0 for a bad one), each `bit_time` cycles.
    void frame(int64_t c, uint8_t byte, unsigned bit_time, bool stop = true) {
        for (unsigned k = 0; k < 10; k++) {
            const bool level = k == 0 ? false : k == 9 ? stop : (byte >> (k - 1) & 1);
            for (unsigned j = 0; j < bit_time; j++) rx_level(c + int64_t(k * bit_time + j), level);
        }
    }
    // A frame that only a receiver sampling in the specified cycles reads as `byte`: a start bit of one cycle, in c,
    // and each data bit's level on the pin in the one cycle c + k D + floor(D / 2) alone (k = 1-8), with the opposite
    // level in the cycle before and the cycle after; the line is 1 elsewhere, the stop bit's cycle included. D >= 4.
    void needle_frame(int64_t c, uint8_t byte, unsigned d) {
        rx_level(c, false);
        for (unsigned k = 1; k <= 8; k++) {
            const bool level = byte >> (k - 1) & 1;
            const int64_t s = c + int64_t(k * d + d / 2);
            rx_level(s - 1, !level); rx_level(s, level); rx_level(s + 1, !level);
        }
    }

    // ---- lvds_in ----
    struct Offer { int64_t from; uint32_t w[4]; bool last; };
    std::vector<Offer> offers;
    size_t next_offer = 0;
    std::vector<int64_t> taken_at;
    int64_t held_valid = 0;            // cycles a beat was offered and not taken

    // ---- lvds_out ----
    std::function<bool(int64_t)> ready;
    struct Got { int64_t t; uint32_t w[4]; bool last; };
    std::vector<Got> got;
    // AXI4-Stream's rule on lvds_out: once TVALID is 1 it stays 1, with TDATA and TLAST unchanged, until the handshake
    uint64_t out_violations = 0;
    bool out_pending = false;
    Pins::Out out_was;

    std::function<void(int64_t, const Pins::Out&, Pins::In&)> drive;

    void attach(units::IngressEgress& ie) { ie.boundary = &IeFarEnd::cycle; ie.far_end = this; }

    // The far end's cycle 0 is the simulator's cycle `origin` (the framework numbers a machine's cycles from 1: a chip
    // test whose cycle 0 is the first after the reset, as the RTL benches count, sets it to 1).
    int64_t origin = 0;
    static void cycle(void* self, int64_t t, const Pins::Out& o, Pins::In& in) {
        IeFarEnd* f = static_cast<IeFarEnd*>(self);
        f->step(t - f->origin, o, in);
    }
    void step(int64_t t, const Pins::Out& o, Pins::In& in) {
        if (first < 0) first = t;
        if (t != first + int64_t(out.size())) FATAL("IeFarEnd: cycle %lld after %lld: Ingress/Egress skipped a cycle", (long long)t, (long long)(first + int64_t(out.size()) - 1));
        out.push_back(o);
        in.uart_rx = size_t(t) < rx.size() ? rx[size_t(t)] != 0 : true;
        if (next_offer < offers.size() && t >= offers[next_offer].from) {
            const Offer& b = offers[next_offer];
            in.lvds_in_tvalid = true; in.lvds_in_tlast = b.last;
            for (unsigned k = 0; k < 4; k++) in.lvds_in_tdata[k] = b.w[k];
        }
        in.lvds_out_tready = ready ? ready(t) : false;
        if (drive) drive(t, o, in);
        // the handshakes of this cycle
        if (in.lvds_in_tvalid && o.lvds_in_tready) { taken_at.push_back(t); if (!drive) next_offer++; }
        else if (in.lvds_in_tvalid) held_valid++;
        if (out_pending) {
            bool same = o.lvds_out_tvalid && o.lvds_out_tlast == out_was.lvds_out_tlast;
            for (unsigned k = 0; k < 4; k++) same = same && o.lvds_out_tdata[k] == out_was.lvds_out_tdata[k];
            if (!same) out_violations++;
        }
        out_pending = o.lvds_out_tvalid && !in.lvds_out_tready;
        out_was = o;
        if (o.lvds_out_tvalid && in.lvds_out_tready) {
            Got g{t, {}, o.lvds_out_tlast};
            for (unsigned k = 0; k < 4; k++) g.w[k] = o.lvds_out_tdata[k];
            got.push_back(g);
        }
    }

    // ---- the UART's line, decoded ----
    struct TxFrame { int64_t start; uint8_t byte; };   // the first cycle of its start bit
    // The frames on uart_tx over the trace's cycles [from, to), each of 10 bits of `divisor(start)` cycles; `error`
    // names the first bit that is not held for its cycles, a stop bit of 0, or a frame cut by the trace's end.
    std::vector<TxFrame> decode_tx(int64_t from, int64_t to, const std::function<unsigned(int64_t)>& divisor, std::string& error) const {
        std::vector<TxFrame> frames;
        error.clear();
        int64_t t = from;
        while (t < to) {
            if (at(t).uart_tx) { t++; continue; }
            const unsigned d = divisor(t);
            if (t + int64_t(10 * d) > to) { error = "a frame from cycle " + std::to_string(t) + " is cut by the trace's end"; return frames; }
            uint8_t byte = 0;
            for (unsigned k = 0; k < 10; k++) {
                const bool level = at(t + int64_t(k * d)).uart_tx;
                for (unsigned j = 1; j < d; j++)
                    if (at(t + int64_t(k * d + j)).uart_tx != level) {
                        error = "the frame from cycle " + std::to_string(t) + ": bit " + std::to_string(k) + " changes in its cycle " + std::to_string(j);
                        return frames;
                    }
                if (k == 0 && level) { error = "the frame from cycle " + std::to_string(t) + ": no start bit"; return frames; }
                if (k == 9 && !level) { error = "the frame from cycle " + std::to_string(t) + ": a stop bit of 0"; return frames; }
                if (k >= 1 && k <= 8 && level) byte |= uint8_t(1u << (k - 1));
            }
            frames.push_back({t, byte});
            t += int64_t(10 * d);
        }
        return frames;
    }
};
