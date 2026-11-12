// suite_ctrl.cpp (WORKER23, P5, inventory C8): v1.3's CtrlTest (sim/ref/meshsim/test/CtrlTest.java) on W20's harness,
// group for group, in Java's order and words. run_tests.sh's steps `ctrl` (-1), `ctrl-shuf` (4) and `ctrl-full`
// (-1 full); its output equals ref's log (tests/ctlsuites/ctrl*.log). Usage: suite_ctrl [shuffleSeed] [full]
//
// Control and the controller (spec 4.10-4.13, 6, 8). Programs are C (fw/tests/), prebuilt images.
//
// IN PROGRESS (NOTES.md, step 2): ported so far: fields, boot. The groups that need W20's pieces (phase_log, reset,
// the request bus, the checker's kinds, mid-run switches) come as those land; until then main() stops after the
// groups ported, so the output is a prefix of ref's log.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../java_random.h"
#include "../../tools/regs/regtable.h"
#include "../suite.h"
#include "ctl.h"

using namespace ctl;
using mach::Chip;
using mach::Mach;
using mach::MachConfig;

namespace {

int64_t shuffle = -1;
bool full = false;
void out(const std::string& s) { std::printf("%s\n", s.c_str()); }

void no_violations(Chip& c, const char* what) {
    const uint64_t v = c.violations();
    suite::check(v == 0, "%s: no invariant violations%s", what, v == 0 ? "" : ("\n" + c.violation_report()).c_str());
}

// java.util.Random.nextInt(bound) and nextInt()
int32_t next_int(test::JavaRandom& r, int32_t bound) {
    if ((bound & -bound) == bound) return r.next_int_pow2(bound);
    int32_t bits, val;
    do { bits = r.next(31); val = bits % bound; } while (bits - val + (bound - 1) < 0);
    return val;
}
int32_t next_int(test::JavaRandom& r) { return r.next(32); }

// ref's Registers.<field>.place(v) and Fields' stream decoders
uint32_t place(const char* word, const char* name, uint32_t v) { const regs::Field& f = regs::field(word, name); return (v & f.mask()) << f.lo; }
int64_t stream_e0(uint32_t src, uint32_t dest) { return int64_t(uint64_t(src) | uint64_t(dest) << 32); }
int s_src(int64_t e0) { return int(e0 & 0x1FFFFF); }
int s_gap(int64_t e0) { return int((uint64_t(e0) >> 21) & 7); }
int s_dest(int64_t e0) { return int32_t(uint64_t(e0) >> 32); }
int s_count(int64_t e1) { return int(e1 & mach::fields::COUNT_MAX); }
int s_size(int64_t e1) { return int((e1 >> 17) & 3); }
int s_counter(int64_t e1) { return int((e1 >> 19) & 7); }
int s_mode(int64_t e1) { return int((e1 >> 22) & 7); }
int s_d(int64_t e1) { return int((uint64_t(e1) >> 25) & 0x7F); }
uint32_t addr_of(const char* reg) { for (const regs::Reg& g : regs::ALL) if (std::string(g.name) == reg) return regs::REG_BASE + 4u * uint32_t(g.index); FATAL("no register %s", reg); }
const regs::Reg& reg(const char* n) { for (const regs::Reg& g : regs::ALL) if (std::string(g.name) == n) return g; FATAL("no register %s", n); }

// ================================================================ mesh.h's field table agrees with the engines' encoders
void fields() {
    namespace F = mach::fields;
    suite::group("fields");
    test::JavaRandom r(5);
    for (int k = 0; k < 200; k++) {
        const int src = next_int(r, 1 << 21), dest = next_int(r), cnt = next_int(r, 1 << 17), d = next_int(r, 128), mode = next_int(r, 8),
                  size = next_int(r, 4), gap = next_int(r, 8), ctr = next_int(r, 8);
        suite::eq(int32_t(F::stream_src(uint32_t(src), uint32_t(gap))), int32_t(place("STREAM_SRC", "CHUNK", uint32_t(src)) | place("STREAM_SRC", "GAP", uint32_t(gap))), "STREAM_SRC");
        suite::eq(int32_t(F::stream_count(uint32_t(cnt), uint32_t(size), uint32_t(ctr), uint32_t(mode), uint32_t(d))),
                  int32_t(place("STREAM_COUNT", "N", uint32_t(cnt)) | place("HDR", "SIZE", uint32_t(size)) | place("HDR", "CTR", uint32_t(ctr)) |
                          place("HDR", "MODE", uint32_t(mode)) | place("HDR", "DST", uint32_t(d))),
                  "STREAM_COUNT (the common header layout, 8.1)");
        const int64_t e0 = stream_e0(F::stream_src(uint32_t(src), uint32_t(gap)), uint32_t(dest)),
                      e1 = int64_t(F::stream_count(uint32_t(cnt), uint32_t(size), uint32_t(ctr), uint32_t(mode), uint32_t(d)));
        suite::check(s_src(e0) == src && s_gap(e0) == gap && s_dest(e0) == dest && s_d(e1) == d && s_count(e1) == cnt && s_mode(e1) == mode &&
                         s_size(e1) == size && s_counter(e1) == ctr,
                     "STREAM entry decodes as encoded");
        const int R = next_int(r, 1 << 18), st = next_int(r, 256), t = 1 + next_int(r, 16), g = next_int(r, 256);
        suite::eq(int32_t(F::group_lo(uint32_t(R))), int32_t(place("GROUP_LO", "R", uint32_t(R))), "GROUP low");
        suite::eq(int32_t(F::group_hi(uint32_t(st), uint32_t(t), uint32_t(g))),
                  int32_t(place("GROUP_HI", "START", uint32_t(st)) | place("GROUP_HI", "TM1", uint32_t(t - 1)) | place("GROUP_HI", "GAP", uint32_t(g))), "GROUP high");
        const int kind = next_int(r, 2), s0 = next_int(r, 256), c = 1 + next_int(r, 256), bd = next_int(r, 5), cd = next_int(r, 5), kk = 1 + next_int(r, 8);
        suite::eq(int32_t(F::master_config(uint32_t(kind), uint32_t(s0), uint32_t(c), uint32_t(bd), uint32_t(cd), uint32_t(kk))),
                  int32_t(place("DMA_CFG", "KIND", uint32_t(kind)) | place("DMA_CFG", "START", uint32_t(s0)) | place("DMA_CFG", "COUNT", uint32_t(c)) |
                          place("DMA_CFG", "BUCKETDIG", uint32_t(bd)) | place("DMA_CFG", "HISTDIG", uint32_t(cd)) | place("DMA_CFG", "KM1", uint32_t(kk - 1))),
                  "DMA_CONFIG");
    }
    // the common header layout (8.1): size [18:17], counter [21:19], mode [24:22], dst [31:25]
    uint32_t h_used = 0;
    for (const regs::Field& f : regs::FIELDS)
        if (std::string(f.word) == "HDR") { suite::eq(0, int32_t(h_used & (f.mask() << f.lo)), "header fields do not overlap: %s", f.name); h_used |= f.mask() << f.lo; }
    suite::eq(int32_t(0xFFFE0000u), int32_t(h_used), "the common header block is [31:17]");
    suite::eq(int64_t(units::lw::hdr(0, 5, 3, 6, 0, 1) >> 7 & 0x7F), 5, "LinkWord: dst");
    suite::eq(int32_t(F::hdr_bits(1, 6, 3, 5)), int32_t(place("HDR", "SIZE", 1) | place("HDR", "CTR", 6) | place("HDR", "MODE", 3) | place("HDR", "DST", 5)),
              "Fields.hdrBits agrees");
    // the block (8.1): offsets
    const struct { const char* r; uint32_t a; } off[] = {
        {"PARAMS", 0xFFFFF800}, {"ERRORS", 0xFFFFF808}, {"PULSE_DONE", 0xFFFFF80C}, {"PULSE_DMA", 0xFFFFF810}, {"EVENTS", 0xFFFFF818},
        {"EVENTS_REQUIRED", 0xFFFFF824}, {"ROUTER_HDR", 0xFFFFF828}, {"ROUTER_ADDR", 0xFFFFF82C}, {"ROUTER_SEND_128", 0xFFFFF844},
        {"REQUEST_SEND", 0xFFFFF854}, {"DMA_CONFIG", 0xFFFFF858}, {"WRITE_COUNTER_1", 0xFFFFF860}, {"WRITE_COUNTER_7", 0xFFFFF878},
        {"SRAM_RD_ADDR_QUEUE", 0xFFFFF87C}, {"SRAM_RD_DEST_ADDR", 0xFFFFF880}, {"SIM_EXIT", 0xFFFFF9F8}, {"TRACE", 0xFFFFF9FC}};
    for (const auto& o : off) suite::eq(int32_t(o.a), int32_t(addr_of(o.r)), "register at 0x%x", o.a);
    suite::check(regs::readable(reg("ERRORS")) && !regs::writable(reg("ERRORS")), "ERRORS is read-only");
    suite::eq(0x7FFF, (1 << 15) - 1, "fifteen errors in [14:0]");   // ref's Conditions.MASK: (1 << N) - 1, N = 15
    uint32_t mc_used = 0;
    for (const regs::Field& f : regs::FIELDS)
        if (std::string(f.word) == "DMA_CFG") { suite::eq(0, int32_t(mc_used & (f.mask() << f.lo)), "DMA_CONFIG fields do not overlap: %s", f.name); mc_used |= f.mask() << f.lo; }
    suite::eq((1 << 27) - 1, int32_t(mc_used), "DMA_CONFIG is 27 bits (8.5)");
    suite::check(regs::reserved_word(5) && regs::reserved_word(33) && regs::reserved_word(125) && !regs::reserved_word(126), "reserved words: 0x814, 0x884-0x9F4");
}

// ================================================================ boot (6.2)
void boot() {
    suite::group("boot");
    Mach m(Mach::cfg(shuffle));
    m.c.run(30);
    for (unsigned k = 0; k < 64; k++)
        suite::check(units::region(core(m.c, k).pc) == units::R_BOOTROM && core(m.c, k).in_wfi && m.c.tile(k).block().done,
                     "tile %u waits in the boot ROM after the reset (6.2: done pulsed, WFI for GO)", k);
    suite::eq(0, __builtin_popcountll(model_ready_bits(m.c, m.c.cycle())), "no READY before a command lands");
    const Mach::Image im = m.boot("hello");
    const bool ok = m.run([&] { return m.all_stopped(); }, 20000);
    suite::check(ok && m.all_exited(), "every tile ran hello to its exit: %s", m.halt_summary().c_str());
    for (unsigned i = 0; i < 64; i++) {
        suite::eq(i, m.word(i, im, "result", 0), "tile %u: tile index from PARAMS", i);
        suite::eq(int32_t(0xC0FFEE00u + i), int32_t(m.word(i, im, "result", 1)), "tile %u: .rodata constant + index", i);
        suite::eq(1 << 20, m.word(i, im, "result", 2), "tile %u: big-SRAM size from PARAMS", i);
        suite::eq(1, m.word(i, im, "result", 3), "tile %u: PARAMS: peripheral 1 present", i);
        suite::eq(7, m.exit_value(i), "tile %u: main's return value through SIM_EXIT", i);
    }
    no_violations(m.c, "boot");
    out(suite::str("boot [meas: CtrlTest boot] (6.2: the boot ROM): hello %zu code + %zu data words = %zu 128-bit mode-0 packets and 2 command writes from C&C 1; all 64 READY at cycle %lld, GO at %lld, every tile at main's exit by %lld",
                   im.code.size(), im.data.size(), (im.code.size() + 3) / 4 + (im.data.size() + 3) / 4, (long long)m.c.cc().last_all_ready_at(),
                   (long long)m.run_at, (long long)m.c.cycle()));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) shuffle = std::strtoll(argv[1], nullptr, 10);
    full = argc > 2 && std::string(argv[2]) == "full";
    if (shuffle >= 0) std::printf("shuffled child order, seed %lld\n", (long long)shuffle);
    fields();
    boot();
    // IN PROGRESS: registers, commands, wfi, faults, misaligned, romSize, invariants, blockCopy, allToAll (NOTES.md)
    return suite::done(shuffle >= 0 ? "CtrlTest (shuffled, seed " + std::to_string(shuffle) + ")" : "CtrlTest");
}
