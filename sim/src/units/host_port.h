// host_port.h (WORKER16, G7): the outside world's writes into a controller (C&C as cores, M3; ref's CcCores host port,
// W14-02; AUDIT-M3 3.4): the host's queue (`CcCores.hostQ[c]`) as a harness stand-in, feeding the controller router's
// CC_HOST_IN Fifo(4) one packet a cycle, in order, as uncounted mode-0 writes from src 64 (ref's hostWrite, hostLoad,
// hostCommand). D23: stepped only while its queue holds packets; the harness queues packets between cycles and wakes it
// (Sim::wake), as ref's harness adds to hostQ between cycles.
//
// The queue is fixed at configuration (`capacity` packets, D6); `hostQ` is ref's ArrayDeque: popped at the start of
// CcCores.runCycle(t) into the FIFO (put at t, visible at t + 1); WordFIFO.canPut is always true and the router takes
// one packet a cycle, so the FIFO never overflows.
#pragma once

#include <cstdint>
#include <vector>

#include "sim.h"
#include "base/fatal.h"
#include "units/net.h"

namespace units {

struct HostPort {
    sim::FifoOut out;                  // -> the controller router's `host` (CC_HOST_IN), Fifo(cc_host_depth)

    uint8_t cc = 0;                    // 0-2: C&C 1-3
    uint64_t* q = nullptr;             // capacity x 3 words {lo, header, hi}: ref's hostQ
    uint32_t capacity = 0, head = 0, count = 0;
    uint32_t commands_to = 0;          // commands landed so far (the next one's parity: hostCommand)
    uint64_t packets = 0;              // statistics: packets put (ref's hostPackets, per controller)

    HostPort(sim::Ports& p, unsigned c, uint32_t cap) : cc(uint8_t(c)), capacity(cap), store_(size_t(cap) * 3) {
        if (c > 2) FATAL("host port: controller %u (0-2)", c);
        p.out(out, "host", 3);
        q = store_.data();
    }
    sim::Wake step(sim::Ctx& c) {
        if (count == 0) return sim::Wake::none();
        const uint64_t* e = q + size_t(head) * 3;
        c.put(out, e[0], e[1], e[2]);
        head = head + 1 == capacity ? 0 : head + 1;
        count--; packets++;
        return count ? c.next() : sim::Wake::none();
    }
    uint64_t digest() const {
        uint64_t h = sim::hash_fields(cc, head, count, commands_to);
        for (uint32_t i = 0; i < count; i++) h = sim::hash_mix(h, sim::hash_bytes(q + size_t((head + i) % capacity) * 3, 24));
        return h;
    }

    // ---- the outside world (configuration or between cycles; then Sim::wake this unit) ----
    // A write into controller cc's memory (ref's hostWrite): an uncounted mode-0 packet from src 64, payload masked.
    void write(uint32_t addr, unsigned size, uint64_t lo, uint64_t hi) {
        if (count >= capacity) FATAL("host port cc%u: more than %u packets queued", cc + 1u, capacity);
        uint64_t* e = q + size_t((head + count) % capacity) * 3;
        e[0] = lw::mask_lo(lo, size); e[1] = lw::hdr(lw::CC1, lw::CC1 + cc, lw::M_WRITE, 0, addr, size); e[2] = lw::mask_hi(hi, size);
        count++;
    }
    // Words at a byte address (region 0x00 or 0x01) as 128-bit packets, zero-padded to whole packets (hostLoad).
    void load(uint32_t byte_addr, const uint32_t* w, size_t n) {
        for (size_t i = 0; i < n; i += 4) {
            uint64_t x[4];
            for (size_t k = 0; k < 4; k++) x[k] = i + k < n ? w[i + k] : 0;
            write(byte_addr + uint32_t(4 * i), lw::SIZE_128, x[1] << 32 | x[0], x[3] << 32 | x[2]);
        }
    }
    // A command into the command block (hostCommand, 6.3): the arguments into the parity's block, then the word with
    // the parity in bit 23 into slot 0, last.
    void command(uint32_t word, const uint32_t* args, size_t nargs) {
        static constexpr uint32_t kCommand = 0x7FC0, kArgsEven = 1, kArgsOdd = 8, kParityBit = 23;
        if (nargs > 7) FATAL("host port cc%u: a command with %zu arguments (7 at most)", cc + 1u, nargs);
        const uint32_t par = commands_to++ & 1, base = par == 0 ? kArgsEven : kArgsOdd;
        for (size_t i = 0; i < nargs; i++) write(kCommand + 4 * uint32_t(base + i), lw::SIZE_32, args[i], 0);
        write(kCommand, lw::SIZE_32, (word & ~(1u << kParityBit)) | par << kParityBit, 0);
    }
    bool idle() const { return count == 0; }

private:
    std::vector<uint64_t> store_;      // configuration-time storage (never resized in the run)
};

}  // namespace units
