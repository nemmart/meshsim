// test_mem.cpp: the scratchpad and the inject unit (machine-spec 14, 15), each rule from the specification or, where it
// leaves the figure to the implementation, from the RTL's blocks (rtl/blocks/scratchpad/README.md,
// rtl/blocks/inject_unit/README.md): READ_LATENCY = 3, a read at the router's local input four cycles after its take.
// No expectation here comes from the simulator's output.
//
// THE RIG. A stand plays the inject unit's surroundings from a script: the three read clients (the gather unit's and
// the address generator's Holds, the read unit's Fifo), the write unit's holding register, the write engine's write
// with its note, the director's GATHER note, the tokens' returns; and it records every packet the inject unit sends
// toward the router with the cycle it reaches the router's local input. Cycles count from 1.
//   - a read PUT in cycle p is OFFERED in p + 1 (the Hold is the client's output register: inject.h);
//   - a read TAKEN in cycle T is injected in T + READ_LATENCY and at the router's local input in T + READ_LATENCY + 1
//     (14.3; the RTL: "a read taken in cycle t reaches the router in t + 4");
//   - a write PUT in cycle p is PRESENTED to the scratchpad in p + 1, the write engine's `scratchpad_write` cycle;
//   - a token return put in p frees its token from p + 1 (the rig's link is 1; the chip's counts from the router).
// The runner's steps `mem`, `mem-shuf` and `mem-shuf2` run this test in the suite build, the last two with the units of
// a level stepped in a seeded random order (MEM_SHUFFLE): no result may depend on the order of steps within a cycle.
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "config.h"
#include "harness.h"
#include "units/big_sram.h"
#include "units/inject.h"
#include "units/mem.h"

using namespace units;

static uint32_t init_value(uint32_t i) { uint32_t x = i * 0x9E3779B1u + 1; return x ^ (x >> 15); }

namespace {

constexpr unsigned RL = 3;                               // READ_LATENCY: the RTL's scratchpad_pkg
constexpr unsigned ME = 5;                               // the tile

struct Stand {
    enum Kind { GATHER, AG, RP, SEND, WRITE, GNOTE, TOKEN };
    struct Ev { uint64_t at; Kind kind; uint64_t a, b, c; };
    sim::HoldOut gather, ag, send, wr;
    sim::FifoOut rp;
    sim::LinkOut wr_note, gather_note, loop_left;
    sim::LinkIn to_router;
    sim::RegIn send_busy, gather_srq_busy;
    sim::CountIn rp_injected;
    std::vector<Ev> script;
    struct Pkt { uint64_t at, lo, h, hi; };
    std::vector<Pkt> out;
    // each Hold's producer: the word offered, put again while refused, and the next from the script in order
    struct Client { std::vector<Ev> q; size_t next = 0; uint64_t held[3] = {}; };
    Client cl[3];                                        // gather, AG, send
    Stand(sim::Ports& p, std::vector<Ev> s) : script(std::move(s)) {
        p.out(gather, "gather", 2); p.out(ag, "ag", 2); p.out(send, "send", 3); p.out(wr, "wr", 3);
        p.out(rp, "rp", 2); p.out(wr_note, "wr_note", 1); p.out(gather_note, "gather_note", 0); p.out(loop_left, "loop_left", 0);
        p.in(to_router, "to_router", 3); p.in(send_busy, "send_busy", 1); p.in(gather_srq_busy, "gather_srq_busy", 1);
        p.in(rp_injected, "rp_injected");
        for (const Ev& e : script) if (e.kind == GATHER || e.kind == AG) cl[e.kind].q.push_back(e); else if (e.kind == SEND) cl[2].q.push_back(e);
    }
    void client(sim::Ctx& c, unsigned k, sim::HoldOut h, unsigned width) {
        Client& x = cl[k];
        if (c.refused(h)) { if (width == 2) c.put(h, x.held[0], x.held[1]); else c.put(h, x.held[0], x.held[1], x.held[2]); return; }
        if (x.next < x.q.size() && x.q[x.next].at <= c.now()) {
            const Ev& e = x.q[x.next++];
            x.held[0] = e.a; x.held[1] = e.b; x.held[2] = e.c;
            if (width == 2) c.put(h, e.a, e.b); else c.put(h, e.a, e.b, e.c);
        }
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(to_router)) { const sim::Word* w = c.peek(to_router); out.push_back({c.now(), w[0], w[1], w[2]}); c.take(to_router); }
        client(c, 0, gather, 2); client(c, 1, ag, 2); client(c, 2, send, 3);
        for (const Ev& e : script) {
            if (e.at != c.now()) continue;
            if (e.kind == RP) c.put(rp, e.a, e.b);
            else if (e.kind == WRITE) { c.put(wr, e.a, e.b, e.c); c.put(wr_note, e.a & 0x1FFFFF); }
            else if (e.kind == GNOTE) c.put(gather_note);
            else if (e.kind == TOKEN) c.put(loop_left);
        }
        return c.next();
    }
    uint64_t digest() const { return 0; }
};

// A read of `size` at `word`, to tile `dst`, in `mode` with `counter` (the client's request words: mem.h).
Stand::Ev read(uint64_t at, Stand::Kind k, uint32_t word, unsigned size, unsigned dst = 9, unsigned mode = lw::M_SRC, unsigned counter = 1, uint32_t addr = 0) {
    return {at, k, mem::req_a(word, size, mode, counter, dst), mem::req_b(addr), 0};
}
// A write of `words` words at `word` (the write engine's request: big_sram.h).
Stand::Ev write(uint64_t at, uint32_t word, unsigned words, uint64_t lo, uint64_t hi = 0) {
    return {at, Stand::WRITE, uint64_t(word) | uint64_t(words) << 21, lo, hi};
}

struct Rig {
    SramBlocks blocks;
    sim::Sim m;
    Rig(std::vector<Stand::Ev> script, uint32_t mem_chunks = 1u << 20) : m(build(blocks, std::move(script), mem_chunks)) {}
    static sim::Sim build(SramBlocks& blocks, std::vector<Stand::Ev> script, uint32_t mem_chunks) {
        NetConfig nc;
        ReadSideConfig rc;
        sim::Options opt;                                    // the runner's shuffled steps: MEM_SHUFFLE=<seed>
        if (const char* s = std::getenv("MEM_SHUFFLE")) opt.shuffle_seed = uint64_t(std::strtoull(s, nullptr, 10)) + 1;
        rc.present = rc.gather = rc.ag = rc.wr_note = rc.gather_note = true;
        rc.read_latency = RL; rc.mem_chunks = mem_chunks;
        sim::Config cfg(opt);
        auto& st = cfg.add<Stand>("stand", std::move(script));
        auto& in = cfg.add<Injection>("inj", ME, nc, rc);
        SramConfig sc; sc.mem_chunks = mem_chunks;
        auto& sr = cfg.add<BigSram>("sram", sc, blocks.block(ME, mem_chunks), true);
        cfg.connect(st.gather, in.rs.client_hold[InjReadSide::GATHER]);
        cfg.connect(st.ag, in.rs.client_hold[InjReadSide::AG]);
        cfg.connect(st.rp, in.rs.rp_q);
        cfg.connect(st.send, in.send);
        cfg.connect(st.wr, sr.wr);
        cfg.link(st.wr_note, in.rs.wr_note, 1);
        cfg.link(st.gather_note, in.rs.gather_note, 1);
        cfg.link(st.loop_left, in.loop_left, 1);
        cfg.link(in.to_router, st.to_router, nc.intra_tile_latency);
        cfg.connect(in.send_busy, st.send_busy);
        cfg.connect(in.rs.gather_srq_busy, st.gather_srq_busy);
        cfg.connect(in.rs.rp_injected, st.rp_injected);
        cfg.connect(in.rs.sram_rd, sr.rd);
        cfg.link(sr.ret, in.rs.ret, RL - 1);
        return cfg.build();
    }
    BigSram& sram() { return m.unit<BigSram>("sram"); }
    const std::vector<Stand::Pkt>& out() { return m.unit<Stand>("stand").out; }
    const Injection& inj() { return m.unit<Injection>("inj"); }
};

// The cycle a read put at `put` and taken the cycle it is offered reaches the router's local input.
constexpr uint64_t at_router(uint64_t put) { return put + 1 + RL + 1; }

}  // namespace

// 15, 14.3: each client alone, every size at every aligned word of a row: the read is taken the cycle it is offered,
// its packet is at the router READ_LATENCY + 1 cycles later, its payload the size's words of the row from its word (the
// bits above its size 0), its header the read's.
TEST(a_read_returns_its_words_at_the_fixed_latency) {
    const Stand::Kind kinds[3] = {Stand::GATHER, Stand::AG, Stand::RP};
    for (unsigned k = 0; k < 3; k++)
        for (unsigned size = 0; size < 3; size++)
            for (unsigned o = 0; o < 4; o += 1u << size) {
                const uint32_t row = 0x1230 + 4 * k;
                Rig r({read(3, kinds[k], row + o, size, 9, lw::M_SRC, 3)});
                const uint32_t w[4] = {0xA0A0A0A0, 0xB1B1B1B1, 0xC2C2C2C2, 0xD3D3D3D3};
                for (unsigned j = 0; j < 4; j++) r.sram().poke(row + j, w[j]);
                r.m.run(12);
                CHECK_EQ(r.out().size(), size_t(1));
                const Stand::Pkt& p = r.out()[0];
                CHECK_EQ(p.at, at_router(3));
                uint64_t lo = 0, hi = 0;
                for (unsigned j = 0; j < (1u << size); j++) (j < 2 ? lo : hi) |= uint64_t(w[o + j]) << (32 * (j & 1));
                CHECK_EQ(p.lo, lo);
                CHECK_EQ(p.hi, hi);
                CHECK_EQ(lw::dst(p.h), 9u);
                CHECK_EQ(lw::mode(p.h), unsigned(lw::M_SRC));
                CHECK_EQ(lw::counter(p.h), 3u);
                CHECK_EQ(lw::size(p.h), size);
                CHECK_EQ(r.inj().tok.reads, 0u);         // its token freed at its injection (14.2)
                CHECK_EQ(r.inj().tok.loop_held, 0u);
            }
}

// 15: reads come back in the order taken, one a cycle: three back-to-back reads of one client.
TEST(reads_return_in_take_order_one_a_cycle) {
    Rig r({read(1, Stand::AG, 0x100, lw::SIZE_32, 9, lw::M_WRITE, 1, 0x03000000), read(2, Stand::AG, 0x104, lw::SIZE_32, 9, lw::M_WRITE, 1, 0x03000004),
           read(3, Stand::AG, 0x108, lw::SIZE_32, 9, lw::M_WRITE, 1, 0x03000008)});
    for (uint32_t i = 0; i < 3; i++) r.sram().poke(0x100 + 4 * i, 0x5000 + i);
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(3));
    for (uint32_t i = 0; i < 3; i++) {
        CHECK_EQ(r.out()[i].at, at_router(1 + i));
        CHECK_EQ(r.out()[i].lo, uint64_t(0x5000 + i));
        CHECK_EQ(lw::addr(r.out()[i].h), 0x03000000u + 4 * i);   // the read's address field passed on (4.1)
    }
}

// 14.1: the arbiter's fixed priority, the gather unit, then the address generator, then the read unit: three reads
// offered in one cycle are taken in three cycles in that order.
TEST(the_arbiter_serves_gather_then_the_address_generator_then_the_read_unit) {
    Rig r({read(1, Stand::RP, 0x40, lw::SIZE_32, 9, lw::M_WRITE, 3), read(1, Stand::AG, 0x44, lw::SIZE_32, 9, lw::M_SRC, 2),
           read(1, Stand::GATHER, 0x48, lw::SIZE_64, ME, lw::M_SRC, 1)});
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(3));
    for (unsigned i = 0; i < 3; i++) {
        CHECK_EQ(r.out()[i].at, at_router(1) + i);
        CHECK_EQ(lw::counter(r.out()[i].h), i + 1);      // 1 the gather unit's, 2 the address generator's, 3 the read unit's
    }
}

// 14.1, 13: while the gather unit is busy the address generator takes no read, even in a cycle the gather unit takes
// none; the read unit gets that cycle. A GATHER accepted in cycle 1 is in the queue, the unit busy, in cycle 2, when
// it offers no read yet. The address generator's read is taken in cycle 3.
TEST(the_address_generator_takes_no_read_while_the_gather_unit_is_busy) {
    Rig r({{1, Stand::GNOTE, 0, 0, 0}, read(1, Stand::AG, 0x44, lw::SIZE_32, 9, lw::M_SRC, 2), read(1, Stand::RP, 0x40, lw::SIZE_32, 9, lw::M_WRITE, 3)});
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(2));
    CHECK_EQ(lw::counter(r.out()[0].h), 3u);             // the read unit's, taken in cycle 2
    CHECK_EQ(r.out()[0].at, at_router(1));
    CHECK_EQ(lw::counter(r.out()[1].h), 2u);             // the address generator's, in cycle 3
    CHECK_EQ(r.out()[1].at, at_router(1) + 1);
    CHECK_EQ(r.inj().rs.gather_gate_cycles, uint64_t(1));
}
// The same while the gather unit's own read is offered and not taken (a write at its bank): the address generator's
// read, at another bank, is not taken either, and the gather unit's goes first in the next cycle.
TEST(the_address_generator_waits_for_a_gather_read_that_is_refused) {
    Rig r({write(1, 0x20008, 1, 7), read(1, Stand::GATHER, 0x20100, lw::SIZE_64, ME, lw::M_SRC, 1), read(1, Stand::AG, 0x44, lw::SIZE_32, 9, lw::M_SRC, 2)});
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(2));
    CHECK_EQ(lw::counter(r.out()[0].h), 1u);
    CHECK_EQ(r.out()[0].at, at_router(1) + 1);           // cycle 2: the write at bank 1; taken in 3
    CHECK_EQ(lw::counter(r.out()[1].h), 2u);
    CHECK_EQ(r.out()[1].at, at_router(1) + 2);
}

// 15, 14.1: a write wins its bank. A read that would meet the write at its bank in that cycle is not taken, and a
// lower client, whose read is at another bank, does not get the cycle. Banks are 512 KiB (2^17 words): the write and the
// address generator's read in bank 1, the read unit's in bank 0.
TEST(a_write_wins_its_bank_and_a_lower_client_does_not_get_the_cycle) {
    Rig r({write(1, 0x20008, 1, 7), read(1, Stand::AG, 0x20100, lw::SIZE_32, 9, lw::M_SRC, 2), read(1, Stand::RP, 0x40, lw::SIZE_32, 9, lw::M_WRITE, 3)});
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(2));
    CHECK_EQ(lw::counter(r.out()[0].h), 2u);
    CHECK_EQ(r.out()[0].at, at_router(1) + 1);           // not taken in cycle 2, taken in 3
    CHECK_EQ(lw::counter(r.out()[1].h), 3u);
    CHECK_EQ(r.out()[1].at, at_router(1) + 2);           // the read unit's: not cycle 2 either, then after the generator's
    CHECK_EQ(r.inj().rs.bank_wait_cycles, uint64_t(1));
    CHECK_EQ(r.sram().peek(0x20008), 7u);                // the write was done
}
// A write at another bank takes nothing from a read: both are done in the one cycle.
TEST(a_write_at_another_bank_does_not_hold_a_read) {
    Rig r({write(1, 0x20008, 1, 7), read(1, Stand::AG, 0x1FFFC, lw::SIZE_32, 9, lw::M_SRC, 2)});   // bank 1; bank 0's last word
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(1));
    CHECK_EQ(r.out()[0].at, at_router(1));
    CHECK_EQ(r.inj().rs.bank_wait_cycles, uint64_t(0));
}
// The banks are min(SCRATCHPAD_BYTES, 512 KiB): a scratchpad of 512 bytes is one bank, so any write holds any read;
// at 8 MiB there are 16, and a write in the last bank holds a read there and none in the bank before it.
TEST(the_banks_follow_the_scratchpad_size) {
    {
        Rig r({write(1, 0x08, 1, 7), read(1, Stand::AG, 0x70, lw::SIZE_32, 9, lw::M_SRC, 2)}, 128);
        CHECK_EQ(r.sram().banks(), 1u);
        r.m.run(12);
        CHECK_EQ(r.out().size(), size_t(1));
        CHECK_EQ(r.out()[0].at, at_router(1) + 1);
    }
    {
        Rig r({write(1, 0x1E0000, 1, 7), read(1, Stand::AG, 0x1FFFFC, lw::SIZE_32, 9, lw::M_SRC, 2), write(5, 0x1E0000, 1, 8),
               read(5, Stand::AG, 0x1DFFFC, lw::SIZE_32, 9, lw::M_SRC, 2)}, 1u << 21);
        CHECK_EQ(r.sram().banks(), 16u);
        r.m.run(16);
        CHECK_EQ(r.out().size(), size_t(2));
        CHECK_EQ(r.out()[0].at, at_router(1) + 1);       // bank 15: held a cycle
        CHECK_EQ(r.out()[1].at, at_router(5));           // bank 14: not held
    }
    CHECK_EQ(SramConfig{}.mem_chunks, 1u << 20);         // SCRATCHPAD_BYTES 4 MiB (19): 8 banks
    CHECK(scratchpad_size_ok(128) && scratchpad_size_ok(1u << 20) && scratchpad_size_ok(1u << 21));
    CHECK(!scratchpad_size_ok(64) && !scratchpad_size_ok(1u << 22) && !scratchpad_size_ok(3u << 18) && !scratchpad_size_ok(17u << 16));
}
// 5.1, 15, 19: SCRATCHPAD_BYTES is a power of two; v1.35's 4.25 MiB (17 areas' worth of words) is refused.
DEATH_TEST(a_scratchpad_that_is_not_a_power_of_two_is_refused, "SCRATCHPAD_BYTES is a power of two") {
    SramBlocks blocks;
    sim::Config cfg;
    SramConfig sc; sc.mem_chunks = 17u << 16;
    cfg.add<BigSram>("sram", sc, blocks.block(0, sc.mem_chunks), false);
}

// 15: a write is in place for every read taken in a later cycle, and a read taken before a write of its row returns
// the row as it was. Word enables: a 32-bit or a 64-bit write is one access and keeps the row's other words.
TEST(a_write_is_in_place_for_a_later_read_and_not_for_an_earlier_one) {
    // the row at 0x300 holds 1, 2, 3, 4. Cycle 2: its 128-bit read A taken. Cycle 3: a 64-bit write of words 2-3.
    // Cycle 4: its 128-bit read B taken. Cycle 5: a 32-bit write of word 1. Cycle 6: read C taken.
    Rig r({read(1, Stand::AG, 0x300, lw::SIZE_128), write(2, 0x302, 2, 0x000000BB000000AAull), read(3, Stand::AG, 0x300, lw::SIZE_128),
           write(4, 0x301, 1, 0xCC), read(5, Stand::AG, 0x300, lw::SIZE_128)});
    for (uint32_t j = 0; j < 4; j++) r.sram().poke(0x300 + j, j + 1);
    r.m.run(16);
    CHECK_EQ(r.out().size(), size_t(3));
    CHECK_EQ(r.out()[0].at, at_router(1));
    CHECK_EQ(r.out()[0].lo, 0x0000000200000001ull);
    CHECK_EQ(r.out()[0].hi, 0x0000000400000003ull);      // A: before the write of cycle 3
    CHECK_EQ(r.out()[1].at, at_router(3));
    CHECK_EQ(r.out()[1].lo, 0x0000000200000001ull);      // B: words 0-1 kept by the 64-bit write
    CHECK_EQ(r.out()[1].hi, 0x000000BB000000AAull);
    CHECK_EQ(r.out()[2].at, at_router(5));
    CHECK_EQ(r.out()[2].lo, 0x000000CC00000001ull);      // C: word 1 alone written
    CHECK_EQ(r.out()[2].hi, 0x000000BB000000AAull);
    CHECK_EQ(r.sram().writes_served, uint64_t(2));
}

// 14.2: TOKEN_COUNT = 64. Every read holds a token from its take; a loopback packet's stays held after its injection,
// until the director takes its copy. 64 reads to the tile itself, one a cycle from cycle 2, are all taken. The 65th,
// offered from cycle 66, is not: no token. The write unit's loopback packet, offered from 66 too, waits as well. A
// token returned in cycle 70 is free from 71: the arbiter takes it first, for the read; the write unit's packet goes
// with the next, returned in 72 and free from 73.
TEST(sixty_four_tokens_and_the_arbiter_takes_one_before_the_write_unit) {
    std::vector<Stand::Ev> s;
    for (uint64_t i = 0; i < 65; i++) s.push_back(read(1 + i, Stand::AG, uint32_t(0x400 + 4 * i), lw::SIZE_32, ME, lw::M_SRC, 2));
    s.push_back({65, Stand::SEND, 0x77, lw::hdr(0, ME, lw::M_COUNT, 4, 0, lw::SIZE_32), 0});
    s.push_back({70, Stand::TOKEN, 0, 0, 0});
    s.push_back({72, Stand::TOKEN, 0, 0, 0});
    Rig r(s);
    r.m.run(69);
    CHECK_EQ(r.out().size(), size_t(64));
    CHECK_EQ(r.out()[63].at, at_router(64));
    CHECK_EQ(r.inj().tok.loop_held, 64u);
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(66));
    CHECK_EQ(lw::counter(r.out()[64].h), 4u);            // the write unit's: injected in 73, at the router in 74
    CHECK_EQ(r.out()[64].at, uint64_t(74));
    CHECK_EQ(lw::counter(r.out()[65].h), 2u);            // the 65th read: taken in 71, at the router in 75
    CHECK_EQ(r.out()[65].at, uint64_t(75));
    CHECK_EQ(r.inj().tok.loop_held, 64u);                // 64 - 2 returned + the two new loopback packets
}
// With every token held, the write unit's packet to another tile still goes: only a loopback packet takes a token.
TEST(a_packet_to_another_tile_takes_no_token) {
    std::vector<Stand::Ev> s;
    for (uint64_t i = 0; i < 64; i++) s.push_back(read(1 + i, Stand::AG, uint32_t(0x400 + 4 * i), lw::SIZE_32, ME, lw::M_SRC, 2));
    s.push_back({70, Stand::SEND, 0x77, lw::hdr(0, 6, lw::M_COUNT, 4, 0, lw::SIZE_32), 0});
    Rig r(s);
    r.m.run(80);
    CHECK_EQ(r.out().size(), size_t(65));
    CHECK_EQ(r.out()[64].at, uint64_t(72));              // offered in 71, injected then, at the router in 72
    CHECK_EQ(r.inj().tok.loop_held, 64u);
}

// 14.3: the injection point takes a read return first; the write unit's packet goes in the next cycle with no return.
TEST(a_read_return_goes_before_the_write_units_packet) {
    // reads taken in cycles 2 and 3 are injected in 5 and 6; the write unit's packet, offered from 5, goes in 7.
    Rig r({read(1, Stand::AG, 0x40, lw::SIZE_32, 9, lw::M_SRC, 2), read(2, Stand::AG, 0x44, lw::SIZE_32, 9, lw::M_SRC, 2),
           {4, Stand::SEND, 0x77, lw::hdr(0, 6, lw::M_COUNT, 4, 0, lw::SIZE_32), 0}});
    r.m.run(12);
    CHECK_EQ(r.out().size(), size_t(3));
    CHECK_EQ(r.out()[0].at, uint64_t(6));
    CHECK_EQ(r.out()[1].at, uint64_t(7));
    CHECK_EQ(lw::counter(r.out()[2].h), 4u);
    CHECK_EQ(r.out()[2].at, uint64_t(8));
}

namespace {
// a scratchpad's write port with nobody writing: the contents are the test's pokes
struct Feed {
    sim::HoldOut wr;
    explicit Feed(sim::Ports& p) { p.out(wr, "wr", 3); }
    sim::Wake step(sim::Ctx&) { return sim::Wake::none(); }
    uint64_t digest() const { return 0; }
};
}  // namespace

// D31: without the running sum the store is plain and data_sum() scans the block; both give the same sum, also after
// a value is overwritten and a word goes back to zero.
TEST(scratchpad_sum_running_or_scanned_agree) {
    SramBlocks blocks;
    sim::Config cfg;
    auto& f = cfg.add<Feed>("feed");
    auto& g = cfg.add<Feed>("feed2");
    SramConfig on, off; off.running_sum = false;
    auto& a = cfg.add<BigSram>("on", on, blocks.block(0, on.mem_chunks), false);
    auto& b = cfg.add<BigSram>("off", off, blocks.block(1, off.mem_chunks), false);
    cfg.connect(f.wr, a.wr);
    cfg.connect(g.wr, b.wr);
    sim::Sim m = cfg.build();
    BigSram& x = m.unit<BigSram>("on");
    BigSram& y = m.unit<BigSram>("off");
    for (uint32_t i = 0; i < 1000; i++) {
        const uint32_t c = (i * 7919u) % (1u << 20), v = i % 5 == 0 ? 0 : init_value(i);
        x.poke(c, v); y.poke(c, v);
    }
    x.poke(7919, 0); y.poke(7919, 0);
    CHECK(x.data_sum() != 0);
    CHECK_EQ(x.data_sum(), y.data_sum());
    CHECK_EQ(y.sum.sum, uint64_t(0));                  // nothing kept per store
}
