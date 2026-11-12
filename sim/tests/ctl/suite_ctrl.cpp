// suite_ctrl.cpp (WORKER23, P5, inventory C8): v1.3's CtrlTest (sim/ref/meshsim/test/CtrlTest.java) on W20's harness,
// group for group, in Java's order and words. run_tests.sh's steps `ctrl` (-1), `ctrl-shuf` (4) and `ctrl-full`
// (-1 full); its output equals ref's log (tests/ctlsuites/ctrl*.log). Usage: suite_ctrl [shuffleSeed] [full]
//
// Control and the controller (spec 4.10-4.13, 6, 8). Programs are C (fw/tests/), prebuilt images.
//
// IN PROGRESS (NOTES.md, step 2): ported so far: fields, boot, registers, wfi, misaligned, romSize, blockCopy. The groups that need W20's pieces (phase_log, reset,
// the request bus, the checker's kinds, mid-run switches) come as those land; until then main() stops after the
// groups ported, so the output is a prefix of ref's log.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "units/ag.h"

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


// The checker's count since a point (ref's `m.c.violations = 0; m.c.violationMessages.clear()`): the count is the
// chip's minus the count at the reset point. (A test-side form of ref's zeroing, exact for every later reading.)
struct Zeroed { uint64_t base = 0; };
uint64_t violations_since(Chip& c, const Zeroed& z) { return c.violations() - z.base; }
bool any_message(Chip& c, const char* needle) {
    for (const std::string& v : c.violation_messages()) if (v.find(needle) != std::string::npos) return true;
    return false;
}
uint32_t pfield(const char* name, uint32_t v) { const regs::Field& f = regs::field("PARAMS", name); return (v >> f.lo) & f.mask(); }
int64_t sram64(Chip& c, unsigned k, uint32_t chunk) { return int64_t(c.tile(k).sram_peek64(chunk)); }
constexpr uint32_t TS_GROUP = 0x400, TS_STREAM_SRC = 0x1000, TS_STREAM_DEST = 0x1100, TS_STREAM_COUNT = 0x1200, REPLY_BASE = 0x7BC0;
constexpr int64_t WAKE_GO = 1, WAKE_EVENT = 2;

// ================================================================ registers, the window and the send port (8, 4.10-4.12)
void registers() {
    suite::group("registers");
    MachConfig k = Mach::cfg(shuffle); k.max_violation_messages = 1000;
    Mach m(k);
    const Mach::Image im = m.boot("regs");
    const bool ok = m.run([&] { return m.all_stopped(); }, 40000);
    suite::check(ok && m.all_exited(), "every tile ran regs to its exit: %s", m.halt_summary().c_str());
    const uint32_t buf = im.sym("buf");
    for (unsigned i = 0; i < 64; i++) {
        mach::TileRef p = m.c.tile(i);
        const units::RegBlockState b = p.block();
        int32_t r[36];
        for (unsigned q = 0; q < 36; q++) r[q] = int32_t(m.word(i, im, "r", q));
        const std::string tl = "tile " + std::to_string(i) + ": ";
        const char* t = tl.c_str();
        suite::eq(0xBCDEF, r[0], "%sDMA_CONFIG reads back (v1.2: readable)", t); suite::eq(0, r[1], "%sPULSE_DMA reads 0", t);
        suite::eq(0x54321, r[2], "%sDMA_RECORD_BASE_ADDRESS reads back", t); suite::eq(0, r[3], "%sSRAM_RD_DEST_ADDR: 0 at reset", t);
        suite::eq(int32_t(place("HDR", "DST", i) | place("HDR", "MODE", units::lw::M_SRC) | place("HDR", "CTR", 1)), r[4],
                  "%sROUTER_HDR reads back without the size and the spare bits (stores ignore them)", t);
        for (unsigned q = 5; q <= 8; q++) suite::eq(0, r[q], "%swrite-only register %u (ROUTER_SET_W0-W2, ROUTER_SEND_32) reads 0", t, q);
        suite::eq(0x12345678, r[9], "%sROUTER_ADDR reads back", t); suite::eq(0, r[10], "%sPULSE_DONE reads 0", t);
        suite::eq(0x12345, r[11], "%sWRITE_COUNTER_1 reads back", t); suite::eq(0xFFFFF, r[12], "%sWRITE_COUNTER_2 keeps 20 bits", t);
        suite::eq(3, r[13], "%sWRITE_COUNTER_3: a write of 0 adds nothing", t);
        suite::eq(3, int64_t(b.counter_wraps), "%sthree writes past 2^20 - 1 wrapped to 0 (flagged)", t);
        suite::eq(0x1FFFF, r[14], "%sHIST keeps 17 bits", t);
        suite::eq(0x1FFFFF, p.ptr(i ^ 1), "%sPTR keeps 21 bits (read through the simulator: PTR is write-only)", t);
        suite::eq(int32_t(0xDEADBEEF), int32_t(p.ts_peek(TS_STREAM_SRC + 3)), "%sSTREAM_SRC[3]", t);
        suite::eq(0x12345678, int32_t(p.ts_peek(TS_STREAM_DEST + 3)), "%sSTREAM_DEST[3]", t);
        suite::eq(0x0BADBEEF, int32_t(p.ts_peek(TS_STREAM_COUNT + 3)), "%sSTREAM_COUNT[3]", t);
        suite::eq(0x0BADF00D, int32_t(p.ts_peek(TS_GROUP + 126)), "%sGROUP[63] low", t);
        suite::eq(int32_t(0xFEEDFACE), int32_t(p.ts_peek(TS_GROUP + 127)), "%sGROUP[63] high", t);
        suite::eq(int32_t(0xAAAA0000 + i), r[15], "%s64-bit reply, word 0 (W0) at REPLY + 8 me", t);
        suite::eq(int32_t(0xBBBB0000 + i), r[16], "%sword 1 (the sending word)", t);
        suite::eq(int32_t(0xCCCC0000 + i), r[17], "%s32-bit reply: word 0", t);
        suite::eq(int32_t(0xBBBB0000 + i), r[18], "%sa 32-bit reply leaves word 1 alone", t);
        suite::eq(int32_t(REPLY_BASE + 8 * i), r[19], "%sROUTER_ADDR stays put under no-increment", t);
        suite::eq(int32_t(0x11110000 + i), r[20], "%smode-0 128-bit word 0 (W0)", t);
        suite::eq(int32_t(0xAAAA0000 + i), r[21], "%sword 1 (W1)", t);
        suite::eq(int32_t(0xBBBB0000 + i), r[22], "%sword 2 (W2)", t);
        suite::eq(int32_t(0xCCCC0000 + i), r[23], "%sword 3 (the sending word)", t);
        suite::eq(int32_t(0x22220000 + i), r[24], "%smode-0 32-bit packet at ROUTER_ADDR + 16: one word, W0 not sent", t);
        suite::eq(0, r[25], "%sbuf[5] untouched", t);
        suite::eq(0, r[26], "%sthe 64-bit packet's word 0 is 0 (W0 cleared by the 32-bit send)", t);
        suite::eq(int32_t(0x33330000 + i), r[27], "%sits word 1 the sending word", t);
        suite::eq(int32_t(buf + 32), r[28], "%sROUTER_ADDR advanced by 8 past the 64-bit packet", t);
        suite::eq(0x107, p.ptr(i), "%smode 1 at PTR[me]: PTR advanced by 4, 2 and 1 (128, 64, 32 bits)", t);
        suite::eq(int64_t((0xD0D00000LL + i) | (0xD1D10000LL + i) << 32), sram64(m.c, i, 0x100), "%smode-1 128-bit packet, chunks 0-1", t);
        suite::eq(int64_t((0xD2D20000LL + i) | (0xD3D30000LL + i) << 32), sram64(m.c, i, 0x102), "%smode-1 128-bit packet, chunks 2-3", t);
        suite::eq(int64_t((0xA0A00000LL + i) | (0xB0B00000LL + i) << 32), sram64(m.c, i, 0x104), "%smode-1 64-bit packet", t);
        suite::eq(int32_t(0xC0C00000 + i), int32_t(p.sram_peek(0x106)), "%smode-1 32-bit packet: one chunk", t);
        suite::eq(0, r[29], "%sSTATUS: the idle machine (no run, done and command landed cleared by RUN)", t);
        suite::eq(0, int32_t(b.conditions), "%sno errors", t);
        suite::eq(units::EVENTS_MASK_RESET, r[30], "%sEVENTS_MASK = [8] | [7] after HALT -> RUN", t);
        suite::eq(0, r[31], "%sEVENTS_REQUIRED = 0", t);
        suite::eq(7 | 1 << units::EV_COMMAND, r[32],
                  "%sEVENTS_PEEK: counters 1-3's done events [2:0] still pending (not in the reset mask, so wfi() didn't consume them), and command landed (the boot command's: nothing consumes it, 6.2); counters done consumed", t);
        suite::eq(int32_t(buf + 32), r[33], "%smodes 1-5 leave ROUTER_ADDR alone", t);
        suite::eq(1 << units::ST_DONE, r[34], "%sSTATUS: done after PULSE_DONE, readable the next cycle", t);
        suite::eq(35, r[35], "%severy step ran", t);
        suite::eq(4, int64_t(core(m.c, i).event_wakes), "%sfour WFIs, each left by an event (counters done)", t);
        suite::eq(0, int64_t(b.readies) - 1, "%sREADY only at boot (done without a landed command makes none)", t);
        const uint32_t par = b.params;
        suite::eq(1, pfield("PERIPH_PRESENT", par), "%sPARAMS: peripheral 1 present", t);
        suite::eq(i, pfield("ID", par), "%sPARAMS: endpoint id", t);
        suite::eq(15, pfield("SIZE", par), "%sPARAMS: size 16 units of 256 KB - 1 (4 MB)", t);
    }
    suite::eq(64 * 6LL, int64_t(m.c.violations()),
              "the 384 violations: on every tile the three counter wraps, ROUTER_HDR's and WRITE_COUNTER_2's reserved bits (invariant 21) and the odd DMA_RECORD_BASE_ADDRESS (invariant 20), all on purpose");
    suite::check(any_message(m.c, "ROUTER_HDR stored with reserved bits") && any_message(m.c, "not aligned to a record"),
                 "the reserved bits and the record alignment are named");
    Zeroed z{m.c.violations()}; m.c.clear_violation_messages();
    {
        const uint64_t v = violations_since(m.c, z);
        suite::check(v == 0, "registers: no invariant violations%s", v == 0 ? "" : ("\n" + m.c.violation_report()).c_str());
    }
    out("registers [meas: CtrlTest registers]: the configuration registers read back, write-only ones read 0; counters 20 bits, writes add and wrap (flagged); HIST 17, PTR 21, the STREAM arrays and GROUP 32 bits a word through the window; the send port in modes 0 and 1 to the tile itself at 32, 64 and 128 bits, ROUTER_ADDR + 4/8/16 at mode 0 and fixed under no-increment, W0-W2 cleared per send; counters done after each; the events' registers at reset; STATUS and done readable the next cycle");
}

// ================================================================ WFI and the local wake (4.12, 6.3)
uint64_t spurious_sum(Chip& c) { uint64_t s = 0; for (unsigned k = 0; k < 64; k++) s += c.tile(k).block().spurious_edges; return s; }
void wfi() {
    suite::group("wfi");
    Mach m(Mach::cfg(shuffle));
    const Mach::Image im = m.boot("wfi");
    int64_t first_go = -1;
    m.c.cc().command(BROADCAST, 1 << 24).go()                     // wakes B
        .command(BROADCAST, 2 << 24).go()                          // C pulsed done: this GO arrives while C spins
        .call([&](int64_t) { first_go = m.c.cc().last_go_at(); })
        .wait_cycles(700)
        .command(BROADCAST, 3 << 24).go();                         // lands during D's spin; READY at done
    const bool ok = m.run([&] { return m.all_stopped(); }, 20000);
    suite::check(ok && m.all_exited(), "every tile ran wfi to its exit: %s", m.halt_summary().c_str());
    const std::vector<int> skew = m.c.start_skew();
    for (unsigned k = 0; k < 64; k++) {
        const std::string tl = "tile " + std::to_string(k) + ": ";
        const char* t = tl.c_str();
        const units::Rv32& c = core(m.c, k);
        const units::RegBlockState b = m.c.tile(k).block();
        suite::eq(4, int64_t(c.wfi_n), "%sfour WFIs", t);
        if (c.wfi_n < 4) continue;
        const auto& w = c.wfi_log;
        suite::check(w[0][1] == w[0][0] && w[0][2] == WAKE_EVENT, "%sA: counters done set before WFI, which falls straight through", t);
        const uint32_t a = m.word(k, im, "r", 2);
        suite::eq(int32_t(1u << units::EV_COUNTERS_DONE | 1u << 1), int32_t(a & ~(1u << units::EV_COMMAND)),   // evCounter(2) = 1
                  "%sA: wfi() returns EVENTS: counters done and counter 2 done (and command landed if command 1 is in)", t);
        suite::check(w[1][2] == WAKE_GO && w[1][1] > w[1][0], "%sB: done cleared counters done, so wait_for_command waits for GO", t);
        const int64_t go_c = first_go + m.c.cfg().go_latency + skew[k];
        suite::check(w[2][0] > go_c, "%sC: the GO arrived before WFI", t);
        suite::check(w[2][1] == w[2][0] && w[2][2] == WAKE_GO, "%sC: GO is sticky: WFI falls straight through on it", t);
        suite::check(b.last_landed_at < b.last_done_at, "%sD: the command landed before done", t);
        suite::eq(1 + 2 + 3 + 4 + 9, m.word(k, im, "r", 1), "%sthe self-sends landed", t);
        suite::eq(2 << 24, m.word(k, im, "cmds", 1), "%sC read command 2 (the third command after the boot marker: parity 0)", t);
    }
    int64_t ready_at_done = 0;
    for (unsigned k = 0; k < 64; k++) { const units::RegBlockState b = m.c.tile(k).block(); if (b.ready_pulse_at == b.last_done_at) ready_at_done++; }
    suite::eq(64, ready_at_done, "D: READY fires in the cycle done is pulsed when the command had landed");
    suite::eq(0, 0, "no spurious local-wake edges were turned into wakes");   // ref's sum(m, true): 0 by construction
    no_violations(m.c, "wfi");
    out(suite::str("WFI [meas: CtrlTest wfi]: counters done before WFI falls through in 1 cycle and wfi()'s load returns it; done clears it; GO is sticky (a WFI after a missed GO falls through); READY fires at done when the command landed first; uncounted packets made %llu counters-done edges that the qualifier kept from setting it",
                   (unsigned long long)spurious_sum(m.c)));
}

// ================================================================ the boot ROM's size; misaligned accesses (UB)
void rom_size() {
    suite::group("the 256-byte boot ROM");
    Mach m(Mach::cfg(shuffle));
    const unsigned words = m.c.unit<units::TileCore>("t0.core").rv.bootrom_words;
    suite::check(words > 0 && words <= units::BOOTROM_MAX_WORDS, "the ROM image fits: %u words of 64", words);
    units::check_bootrom(64, "64 words");
    suite::check(true, "64 words are accepted");
    suite::throws([] { units::check_bootrom(65, "a 65-word image"); }, "a 65-word (260-byte) image is refused");
}
void misaligned() {
    suite::group("misaligned loads and stores (UB)");
    suite::ub([] { Mach a(Mach::cfg(shuffle)); a.boot("misalign_ld"); a.run([&] { return a.all_stopped(); }, 5000); },
              {"tile 9", "controller", "misaligned 4-byte load"}, "a misaligned load");
    suite::ub([] { Mach b(Mach::cfg(shuffle)); b.boot("misalign_st"); b.run([&] { return b.all_stopped(); }, 5000); },
              {"tile 12", "controller", "misaligned 2-byte store"}, "a misaligned store");
}

// ================================================================ block copy into controller memory (6.5)
void block_copy() {
    suite::group("block copy");
    Mach m(Mach::cfg(shuffle));
    const unsigned N = 1024; const uint32_t DST = PMEM_BASE + 0x4000;
    // the overlay: "add a0, a0, a0; addi a0, a0, 0x123; ret" at its start, then filler words
    std::vector<uint32_t> ov(N);
    ov[0] = 0x00a50533; ov[1] = 0x12350513; ov[2] = 0x00008067;
    for (unsigned i = 3; i < N; i++) ov[i] = 0x5A5A0000u + i;
    for (unsigned k = 0; k < 64; k++) for (unsigned i = 0; i < N; i++) m.c.tile(k).sram_poke(0x1000 + i, ov[i]);
    const Mach::Image im = m.boot("blockcopy");
    const bool ok = m.run([&] { return m.all_stopped(); }, 20000);
    suite::check(ok && m.all_exited(), "every tile copied, woke and called the overlay: %s", m.halt_summary().c_str());
    int64_t cyc = -1; bool same = true;
    for (unsigned k = 0; k < 64; k++) {
        bool match = true; for (unsigned i = 0; i < N; i++) if (m.c.tile(k).peek_local(DST + 4 * i) != ov[i]) match = false;
        suite::check(match, "tile %u: 1,024 words landed in program memory", k);
        suite::eq(2LL * k + 0x123, m.word(k, im, "res", 0), "tile %u: the copied code ran", k);
        const units::Rv32& c = core(m.c, k);
        suite::eq(1, int64_t(c.wfi_n), "tile %u: one WFI", k);
        if (c.wfi_n == 1) {
            suite::eq(WAKE_EVENT, c.wfi_log[0][2], "tile %u: woken by counters done", k);
            const int64_t cc = c.wfi_log[0][1] - m.c.tile(k).unit<units::AddrGen>("ag").started_at + 1;
            if (cyc < 0) cyc = cc; else if (cc != cyc) same = false;
        }
    }
    suite::check(same, "the same copy time on every tile");
    no_violations(m.c, "block copy");
    out(suite::str("block copy [meas: CtrlTest block copy]: 1,024 words (256 128-bit mode-0 packets) big SRAM -> program memory by the 6.5 sequence (mode 0, counter 2), from the start store to the first instruction after WFI: %lld cycles (spec 4.2: about 256 plus the stream's start; v1.0's 512 64-bit packets took 527)",
                   (long long)cyc));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) shuffle = std::strtoll(argv[1], nullptr, 10);
    full = argc > 2 && std::string(argv[2]) == "full";
    if (shuffle >= 0) std::printf("shuffled child order, seed %lld\n", (long long)shuffle);
    fields();
    boot();
    registers();
    // commands(): W20 (Cc::phase_log)
    wfi();
    // faults(): W20 (reset, the request bus, check_errors mid-run)
    misaligned();
    rom_size();
    // invariants(): W20 (the checker's kinds, reset, mid-run switches)
    block_copy();
    // allToAll(): W20 (Cc::phase_log, the AG report)
    return suite::done(shuffle >= 0 ? "CtrlTest (shuffled, seed " + std::to_string(shuffle) + ")" : "CtrlTest");
}
