// test_checkself.cpp: oracle/ref/CheckSelf.java's sequence (fixture tests/mach/checkself.out, oracle/ref/checkself.sh):
// v1.3's checker provoked kind by kind, its report printed, line for line against ref. Its own binary, apart from
// test_mach (the harness's own sequences), so each stays inside the tool limit in the debug builds.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>

#include "base/fatal.h"
#include "harness.h"
#include "mach.h"
#include "units/ag.h"
#include "units/big_sram.h"
#include "units/gather.h"
#include "units/linkword.h"
#include "units/tile_ctl.h"

using namespace mach::fields;
using mach::Mach;

namespace {
namespace lw = units::lw;
using mach::Net;
std::string out;
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    const int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    std::string b(size_t(n > 0 ? n : 0), '\0');
    std::vsnprintf(b.data(), b.size() + 1, fmt, ap);
    va_end(ap);
    out += b;
}
constexpr uint32_t kCmdJump = 0x4A, kPmemBase = 0x01000000;   // ref's Registers.CMD_JUMP, PMEM_BASE
std::string file(const char* path) {
    std::string s;
    FILE* f = std::fopen(path, "rb");
    if (!f) return "(missing)";
    char b[4096]; size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}

void checkself() {
    out.clear();
    {
        Net n(Net::cfg(-1));
        n.at(3, 10, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x11, 0);
        n.at(4, 11, 9, lw::M_WRITE, 0, Net::cm(0x900), lw::SIZE_UNDEF, 0x22, 0);
        n.at(5, 12, 9, lw::M_COUNT, 0, 0, lw::SIZE_32, 0x33, 0);
        n.at(6, 13, 9, lw::M_GATHER, 0, 0, lw::SIZE_64, 0x44, 0);
        n.at(7, 14, 9, lw::M_COUNT, 0, 0, lw::SIZE_UNDEF, 0x55, 0);
        n.at(8, 14, 9, lw::M_AUG, 1, 0, lw::SIZE_128, 0x66, 0x77);
        n.at(3, 40, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_64, 0x88, 0);
        const bool ok = n.run_all(1000);
        say("packets: %s at cycle %lld\n", ok ? "drained" : "NOT drained", (long long)n.c.cycle());
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.check_errors = true;
        Net n(k);
        n.at(3, 10, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x11, 0);
        n.at(4, 12, 12, lw::M_WRITE, 0, 0xFE000000u, lw::SIZE_32, 0x22, 0);
        n.at(5, 30, 9, lw::M_COUNT, 1, 0, lw::SIZE_32, 0x33, 0);
        n.at(6, 31, 9, lw::M_WRITE, 0, 0xFF000000u, lw::SIZE_32, 0x44, 0);
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.queue_depth = 2; k.we_in_depth = 1;   // S1 on again
        Net n(k);
        for (unsigned s = 20; s < 40; s++) for (unsigned i = 0; i < 4; i++)
            n.at(s, 10 + i, 9, lw::M_WRITE, 1, uint32_t(lw::R_SRAM) << 24 | (s * 16 + 2 * i) << 2, lw::SIZE_64, s * 100 + i, 0);
        n.run_all(2000);
        say("%s", n.c.violation_report().c_str());
#ifdef SIM_CHECKS
        {   // S1.report (s1.h)
            const units::S1Tile* t[64];
            for (unsigned x = 0; x < 64; x++) t[x] = n.c.tile(x).unit<units::Director>("dir").s1.get();
            say("%s\n", units::s1_report(t, 64, k.K).c_str());
        }
#endif
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.check_unexpected = true;
        Net n(k);
        const uint32_t SR = uint32_t(lw::R_SRAM) << 24;
        n.at(3, 10, 9, lw::M_WRITE, 1, SR | 0x100, lw::SIZE_64, 1, 0);
        n.c.tile(12).set_remaining(1, 1);
        n.at(4, 10, 12, lw::M_WRITE, 1, SR | 0x200, lw::SIZE_64, 2, 0);
        n.at(4, 11, 12, lw::M_WRITE, 1, SR | 0x208, lw::SIZE_64, 3, 0);
        n.at(5, 12, 20, lw::M_SRC, 0, 0, lw::SIZE_64, 4, 0);
        n.at(5, 13, 21, lw::M_WRITE, 0, SR | 0x300, lw::SIZE_64, 5, 0);
        n.at(5, 14, 22, lw::M_WRITE, 0, Net::cm(0x900), lw::SIZE_32, 6, 0);
        n.at(6, 15, 23, lw::M_WRITE, 1, 8, lw::SIZE_64, 7, 0);
        n.at(6, 16, 24, lw::M_WRITE, 1, 0x7BC0 + 8 * 7, lw::SIZE_32, 8, 0);
        n.at(7, 17, 25, lw::M_WRITE, 0, 0x7FC0, lw::SIZE_32, 9, 0);
        n.at(7, 18, 26, lw::M_WRITE, 1, 0x7FF0 + 0x8000, lw::SIZE_128, 10, 11);
        n.at(8, 19, 27, lw::M_WRITE, 1, SR | (0x100000u << 2), lw::SIZE_64, 12, 0);
        n.cnc_send({30, lw::BROADCAST, lw::M_WRITE, 0x200, 13, lw::SIZE_32, 0, 0});
        n.cnc_send({31, 30, lw::M_WRITE, 0x7FC0, 14, lw::SIZE_32, 0, 0});
        n.run_all(2000);
        say("%s", n.c.violation_report().c_str());
    }
    {   // the core's checks through the block: terminal traps without an ERRORS bit, mret, aliasing, reserved bytes
        mach::MachConfig k = Mach::cfg(-1);
        k.check_errors = false; k.max_violation_messages = 200;
        Mach m(k);
        const uint32_t P = 0x01000000, LOOP = 0x0000006F;
        const std::vector<std::vector<uint32_t>> prog = {
            {LOOP}, {0x00000073}, {0x00100073}, {0x30200073}, {0x0020006F},
            {0x010000B7, 0x01008093, 0x30509073, 0, 0}, {0x00002223, LOOP}, {0x000080B7, 0x0000A103, 0x0020A023, LOOP}};
        for (unsigned t = 0; t < 64; t++) {
            const auto& w = prog[t % prog.size()];
            for (size_t i = 0; i < w.size(); i++) m.c.tile(t).poke_local(P + 4 * uint32_t(i), w[i]);
        }
        m.c.run(40);
        m.c.cc().command(lw::BROADCAST, 0x4Au << 24, {P}).run_when_ready();
        m.c.run(400);
        say("%s", m.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.check_errors = true;
        Net n(k);
        n.c.run(20);
        n.c.tile(5).set_errors(1u << 9);
        n.c.run(10);
        say("after tile 5's bits: %llu at cycle %lld\n", (unsigned long long)n.c.violations(), (long long)n.c.cycle());
        n.c.set_check_errors(false);
        n.c.tile(6).set_errors(1u << 10);
        n.c.run(10);
        n.c.set_check_errors(true);
        n.c.tile(7).set_errors(1u << 1);
        n.c.run(1);
        say("%s", n.c.violation_report().c_str());
        n.c.clear_violations(); n.c.set_max_violation_messages(1);
        n.c.tile(8).set_errors(1u << 2);
        n.c.tile(9).set_errors(1u << 3);
        n.c.step();
        say("%s", n.c.violation_report().c_str());
    }
    {   // CtrlTest's inv program: invariants 4, 9, the reserved word, the table window while the AG runs
        mach::MachConfig k = Mach::cfg(-1);
        k.check_errors = false; k.max_violation_messages = 1000;
        Mach m(k);
        m.boot("inv");
        m.c.run_until([&] { return m.c.all_stopped(); }, 10000);
        say("%s", m.c.violation_report().c_str());
        uint64_t ow = 0, uc = 0;
        for (unsigned t = 0; t < 64; t++) {
            const auto& tr = m.c.tile(t).unit<units::TileCore>("core").track;
            ow += tr.overwritten_unread; uc += tr.unread_commands;
        }
        say("overwritten unread %llu, unread commands %llu\n", (unsigned long long)ow, (unsigned long long)uc);
    }
    {   // inv with invariant 8 strict
        mach::MachConfig k = Mach::cfg(-1);
        k.check_errors = false; k.max_violation_messages = 1000; k.strict_replies = true;
        Mach m(k);
        m.boot("inv");
        m.c.run_until([&] { return m.c.all_stopped(); }, 10000);
        say("%s", m.c.violation_report().c_str());
    }
    {   // the table window while count-on-arrival uses HIST and the write engine uses PTR (a test's stores)
        mach::MachConfig k = Net::cfg(-1);
        Net n(k);
        n.c.tile(9).set_master_config(master_config(0, 0, 0, 0, 1, 1));
        n.c.tile(9).set_remaining(1, 8);
        for (unsigned i = 0; i < 6; i++) n.at(12 + 2 * i, 10 + i, 9, lw::M_SRC, 1, 0, lw::SIZE_64, 0x0101ull * (i + 1), 0);
        while (n.c.cycle() < 19) n.step();
        for (int64_t t = 20; t < 52; t++) {
            const uint32_t a = 0xFFFF2000u + ((t & 1) ? 0x400u : 0u) + 4 * uint32_t(t % 5);
            n.c.tile(9).store(t, a, uint32_t(t));
            n.step();
        }
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
#ifdef SIM_CHECKS
        {   // S1's PTR and HIST paths
            const units::S1Tile* s1t[64];
            for (unsigned x = 0; x < 64; x++) s1t[x] = n.c.tile(x).unit<units::Director>("dir").s1.get();
            say("%s\n", units::s1_report(s1t, 64, k.K).c_str());
        }
#endif
    }
    {   // the peripheral slot's own checks (window 1: word 8 stored, word 10 loaded, word 22 stored)
        mach::MachConfig k = Mach::cfg(-1);
        k.check_errors = false; k.max_violation_messages = 200;
        Mach m(k);
        const uint32_t P = 0x01000000, LOOP = 0x0000006F, X1 = 0x80000093;
        const std::vector<std::vector<uint32_t>> prog = {{LOOP}, {X1, 0x2200A023, LOOP}, {X1, 0x2280A103, LOOP}, {X1, 0x2400AC23, LOOP}};
        for (unsigned t = 0; t < 64; t++) {
            const auto& w = prog[t % prog.size()];
            for (size_t i = 0; i < w.size(); i++) m.c.tile(t).poke_local(P + 4 * uint32_t(i), w[i]);
        }
        m.c.run(40);
        m.c.cc().command(lw::BROADCAST, 0x4Au << 24, {P}).run_when_ready();
        m.c.run(300);
        say("%s", m.c.violation_report().c_str());
    }
    {   // a WFI woken by another event than GO with done set, the done gate off
        mach::MachConfig k = Mach::cfg(-1);
        k.check_errors = false; k.max_violation_messages = 200; k.done_gate = false;
        Mach m(k);
        const uint32_t P = 0x01000000;
        const uint32_t w[8] = {0x80000093, 0x40000113, 0x0220A023, 0x00100193, 0x0030A623, 0x0030A823, 0x10500073, 0x0000006F};
        for (unsigned t = 0; t < 64; t++) {
            if (t % 5 == 2) for (uint32_t i = 0; i < 8; i++) m.c.tile(t).poke_local(P + 4 * i, w[i]);
            else m.c.tile(t).poke_local(P, 0x0000006F);
        }
        m.c.run(40);
        m.c.cc().command(lw::BROADCAST, 0x4Au << 24, {P}).run_when_ready();
        m.c.run(300);
        say("%s", m.c.violation_report().c_str());
    }
    {   // the request bus's root drops requests for targets 3, 5, 7
        mach::MachConfig k = Mach::cfg(-1);
        k.check_errors = false; k.max_violation_messages = 200;
        Mach m(k);
        const uint32_t P = 0x01000000, LOOP = 0x0000006F, X1 = 0x80000093;
        const std::vector<std::vector<uint32_t>> prog = {{LOOP}, {X1, 0x18000113, 0x0420A423, 0x0400AA23, LOOP},
                                                         {X1, 0x28000113, 0x0420A423, 0x0400AA23, LOOP}, {X1, 0x38000113, 0x0420A423, 0x0400AA23, LOOP}};
        for (unsigned t = 0; t < 64; t++) {
            const auto& w = prog[t % prog.size()];
            for (size_t i = 0; i < w.size(); i++) m.c.tile(t).poke_local(P + 4 * uint32_t(i), w[i]);
        }
        m.c.run(40);
        m.c.cc().command(lw::BROADCAST, 0x4Au << 24, {P}).run_when_ready();
        m.c.run(800);
        say("%s", m.c.violation_report().c_str());
    }
    {   // C&C's receive FIFO full
        mach::MachConfig k = Net::cfg(-1);
        k.cnc_rx_depth = 4;
        Net n(k);
        for (unsigned i = 0; i < 8; i++) n.at(10 + i, 40 + i, lw::CC1, lw::M_WRITE, 0, 0x100, lw::SIZE_32, i, 0);
        n.run_all(2000);
        say("%s", n.c.violation_report().c_str());
    }
    {   // the command slot after the boot (the boot ROM takes the boot command at GO)
        mach::MachConfig k = Mach::cfg(-1);
        k.max_violation_messages = 200;
        Mach m(k);
        m.boot("search");
        m.c.cc().command(lw::BROADCAST, 2u << 24).go();
        m.c.run_until([&] { return m.c.all_stopped(); }, 20000);
        say("%s", m.c.violation_report().c_str());
    }
    {   // the read port: stores into a full queue (invariant 8)
        mach::MachConfig k = Net::cfg(-1);
        k.read_port_depth = 2;
        Net n(k);
        while (n.c.cycle() < 19) n.step();
        for (int64_t t = 20; t < 32; t++) {
            if (t == 20) n.c.tile(9).store(t, 0xFFFFF880u, (1u << 17) | (2u << 19) | 0x100);
            else n.c.tile(9).store(t, 0xFFFFF87Cu, uint32_t(0x40 + 2 * t));
            n.step();
        }
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
    }
    {   // the AG's stream-entry checks (checkEntry): eight streams of count 0 on tile 9
        Net n(Net::cfg(-1));
        n.c.tile(9).set_stream(0, stream_src(0x100), 0x0, stream_count(0, 3, 1, 0, 9));
        n.c.tile(9).set_stream(1, stream_src(0x100), 0x0, stream_count(0, 2, 1, 3, 9));
        n.c.tile(9).set_stream(2, stream_src(0x100), 0x104, stream_count(0, 1, 1, 0, 9));
        n.c.tile(9).set_stream(3, stream_src(0x101), 0x0, stream_count(0, 1, 1, 0, 9));
        n.c.tile(9).set_stream(4, stream_src(0x100), 0x0, stream_count(0, 0, 1, 0, 70));
        n.c.tile(9).set_stream(5, stream_src(0x100), 0x0, stream_count(0, 0, 1, 0, 126));
        n.c.tile(9).set_stream(6, stream_src(0x100), 0x0, stream_count(0, 0, 1, 4, 5));
        n.c.tile(9).set_stream(7, stream_src(0x100), 0x0, stream_count(0, 0, 0, 5, 9));
        n.c.tile(9).set_master_config(master_config(RUN_STREAMS, 0, 8, 0, 0, 1));
        n.c.tile(9).pulse(PULSE_START);
        n.c.run(200);
        say("%s", n.c.violation_report().c_str());
    }
    {   // invariant 6: HIST counts past 17 bits, reported once a tile
        Net n(Net::cfg(-1));
        n.c.tile(9).set_master_config(master_config(0, 0, 0, 0, 1, 1));
        for (unsigned i = 0; i < 256; i++) n.c.tile(9).hist_poke(i, 0x1FFFF);
        n.c.tile(9).set_remaining(1, 8);
        for (unsigned i = 0; i < 4; i++) n.at(12 + i, 10 + 3 * i, 9, lw::M_SRC, 1, 0, lw::SIZE_64, 0x0101ull * (i + 1), 0);
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
    }
    {   // invariant 2, the disjoint buffers: written then read (phase 0), new_phase, read then written (phase 1)
        Net n(Net::cfg(-1));
        const uint32_t SR = uint32_t(lw::R_SRAM) << 24;
        n.c.tile(9).set_remaining(1, 2); n.c.tile(9).set_remaining(2, 4);
        n.at(12, 10, 9, lw::M_WRITE, 1, SR | (0x40u << 2), lw::SIZE_64, 0x11, 0);
        n.at(12, 11, 9, lw::M_WRITE, 1, SR | (0x41u << 2), lw::SIZE_32, 0x12, 0);
        while (n.c.cycle() < 39) n.step();
        n.c.tile(9).store(40, 0xFFFFF880u, (1u << 17) | (2u << 19) | 0x100); n.step();
        n.c.tile(9).store(41, 0xFFFFF87Cu, 0x40); n.step();
        n.c.tile(9).store(42, 0xFFFFF87Cu, 0x40); n.step();
        n.run_all(500);
        n.c.new_phase();
        const int64_t T = n.c.cycle() + 5;
        while (n.c.cycle() < T - 1) n.step();
        n.c.tile(9).store(T, 0xFFFFF87Cu, 0x80); n.step();
        n.at(13, T + 20, 9, lw::M_WRITE, 1, SR | (0x80u << 2), lw::SIZE_64, 0x13, 0);
        n.run_all(500);
        say("%s", n.c.violation_report().c_str());
    }
    {   // invariant 2 across GOs: the a2a
        Mach m(Mach::cfg(-1));
        mach::A2A data(7, 2000);
        for (unsigned s = 0; s < 64; s++) {
            mach::TileRef p = m.c.tile(s);
            for (size_t k = 0; k < data.rec[s].size(); k++) p.sram_poke64(mach::A2A::SRC + 2 * uint32_t(k), uint64_t(data.rec[s][k]));
            for (unsigned d = 0; d < 64; d++) p.sram_poke(0xF0000 + d, uint32_t(data.cnt[s][d]));
            for (unsigned src = 0; src < 64; src++) p.sram_poke(0xF0000 + 64 + src, uint32_t(data.cnt[src][s]));
            for (unsigned g = 0; g < 16; g++) p.sram_poke(0xF0000 + 128 + g, uint32_t(data.R[g]));
            p.sram_poke(0xF0000 + 144, 0);
        }
        m.boot("a2a");
        m.c.cc().command(lw::BROADCAST, 1u << 24).go().command(lw::BROADCAST, 2u << 24).go().command(lw::BROADCAST, 3u << 24).go();
        m.run([&] { return m.all_stopped(); }, 2000000);
        say("%s", m.c.violation_report().c_str());
    }
    {   // invariant 2's phases at GO: tile 5 reads chunk 0x40, GO, C&C writes it (another phase)
        mach::MachConfig k = Mach::cfg(-1);
        k.check_errors = false;
        Mach m(k);
        const uint32_t P = 0x01000000;
        const uint32_t w[5] = {0x80000093, 0x00100193, 0x0030A623, 0x10500073, 0xFF9FF06F};
        for (unsigned t = 0; t < 64; t++) for (uint32_t i = 0; i < 5; i++) m.c.tile(t).poke_local(P + 4 * i, w[i]);
        m.c.run(40);
        m.c.cc().command(lw::BROADCAST, 0x4Au << 24, {P}).run_when_ready();
        m.c.run(300);
        m.c.tile(5).set_remaining(2, 1);
        const int64_t T = m.c.cycle() + 1;
        m.c.tile(5).store(T, 0xFFFFF880u, (1u << 17) | (2u << 19) | 0x100); m.c.run(1);
        m.c.tile(5).store(T + 1, 0xFFFFF87Cu, 0x40); m.c.run(100);
        m.c.cc().command(lw::BROADCAST, 1u << 24).go();
        m.c.run(150);
        m.c.cc().send(5, lw::M_WRITE, 0, uint32_t(lw::R_SRAM) << 24 | 0x40u << 2, lw::SIZE_64, 0x77);
        m.c.run(200);
        say("%s", m.c.violation_report().c_str());
    }
    {   // the slot's DMA_HDR and DMA_ADDR changed mid-transfer (ref's test peripheral on tile 7)
        mach::MachConfig k = mach::MachConfig::machine();
        k.periph_type = 0; k.test_peripheral_tile = 7;
        Net n(k);
        n.c.run(200);
        const uint32_t W1 = 0xFFFFF800u + 0x200, hdr = 7u | 1u << 12 | 65u << 25 | 5u << 19;
        const uint32_t st[5][2] = {{W1 + 32, hdr}, {W1 + 36, 0x400}, {W1 + 16, 1}, {W1 + 32, hdr}, {W1 + 36, 0x500}};
        for (const auto& s : st) { n.c.tile(7).store(n.c.cycle() + 1, s[0], s[1]); n.step(); }
        for (int i = 0; i < 40; i++) n.step();
        say("%s", n.c.violation_report().c_str());
    }
    {   // invariant 2's phases at the counters-done latch: A -> B, latch, B -> A; then one run on one buffer
        Net n(Net::cfg(-1));
        mach::TileRef p = n.c.tile(12);
        const uint32_t A = 0x20000, B = 0x40000, N = 64;
        for (uint32_t i = 0; i < N; i++) p.sram_poke64(A + 2 * i, 0xA000ull + i);
        const uint32_t runs[3][2] = {{A, B}, {B, A}, {A, A + 2 * N - 16}};
        for (uint32_t r = 0; r < 3; r++) {
            p.set_stream(r, stream_src(runs[r][0]), 0, stream_count(N, SIZE_64, 1, lw::M_SRC, 0));
            p.set_ptr(12, runs[r][1]); p.set_remaining(1, N);
            p.set_master_config(master_config(RUN_STREAMS, r, 1, 0, 0, 1));
            p.pulse(PULSE_START);
            n.c.run(600);
        }
        say("%s", n.c.violation_report().c_str());
#ifdef SIM_CHECKS
        say("disjoint-buffer accesses counted: %llu\n", (unsigned long long)p.unit<units::BigSram>("sram").disjoint_violations);
#endif
    }
    {   // the endpoints' send checks (C&C 1's cncSends; C&C 2's and C&C 3's direct sends; ref's text rule)
        Net n(Net::cfg(-1));
        n.cnc_send({20, 9, lw::M_WRITE, 0xFF000000u, 1, lw::SIZE_32, 0, 0});
        n.cnc_send({22, 9, lw::M_WRITE, 0x900, 2, lw::SIZE_UNDEF, 0, 0});
        while (n.c.cycle() < 30) n.step();
        n.send(1, 66, 6, 2, 0x100, lw::SIZE_32, 0x55, 0);
        n.step();
        n.send(2, 65, lw::M_WRITE, 2, 0x104, lw::SIZE_UNDEF, 0x56, 0);
        n.step();
        n.cnc_send({40, 9, lw::M_AUG, 0, 4, lw::SIZE_128, 5, 0});
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
    }
    {
        mach::MachConfig k = Net::cfg(-1);
        k.max_violation_messages = 3;
        Net n(k);
        for (unsigned i = 0; i < 6; i++) n.at(10 + i, 10 + i, 20, lw::M_COUNT, 0, 0, lw::SIZE_32, i, 0);
        n.run_all(1000);
        say("%s", n.c.violation_report().c_str());
    }
    // CtrlTest's regs program: the three counter wraps, ROUTER_HDR's and WRITE_COUNTER_2's reserved bits (invariant 21)
    // and the odd DMA_RECORD_BASE_ADDRESS (invariant 20) on every tile; all 384 messages
    {
        mach::MachConfig k = Mach::cfg(-1);
        k.max_violation_messages = 1000;
        Mach m(k);
        m.boot("regs");
        m.run([&] { return m.all_stopped(); }, 40000);
        say("%s", m.c.violation_report().c_str());
    }
    // CtrlTest's invariants: an overlay landing after a unit's first command and before its start (6.2), from C&C 1
    {
        Mach o(Mach::cfg(-1));
        o.c.run(40);
        o.c.cc().command(9, kCmdJump << 24, {kPmemBase}).wait_cycles(60).send(9, lw::M_WRITE, 0, 0x400, lw::SIZE_64, 0x5);
        o.c.run(200);
        say("%s", o.c.violation_report().c_str());
    }
    // CtrlTest's invariants: unicast overlays, then a broadcast command (invariant 25), from C&C 1
    {
        Mach q(Mach::cfg(-1));
        q.c.run(40);
        q.c.cc().load_words(9, kPmemBase, {1, 2, 3, 4}).command(lw::BROADCAST, kCmdJump << 24, {kPmemBase});
        q.c.run(300);
        say("%s", q.c.violation_report().c_str());
    }
}
}  // namespace

TEST(mach_checker_as_refs) {
    if (!sim::check::kOn) { std::printf("  (the checker's facts are off in this build: MODE=debug or MODE=suite)\n"); return; }
    checkself();
    const std::string want = file("tests/mach/checkself.out");
    if (out != want) std::fprintf(stderr, "---- got\n%s---- want (tests/mach/checkself.out)\n%s", out.c_str(), want.c_str());
    CHECK(out == want);
}
