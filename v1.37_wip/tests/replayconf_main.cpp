// replayconf: the replayer's conformance scripts (tests/replayconf.h).
//   replayconf SCRIPT [MAX_ROWS]    the script (plain or .gz) replayed on units::Replayer; its observations on stdout
//   replayconf --list               the scenarios' names
//   replayconf --gen NAME           the scenario's script on stdout: the ideal controller's inputs (ReplayDriver's,
//                                   every one recorded) on one of the unit tests' windows, some with a mutation for an
//                                   error path. The fixtures' scripts are these; their observations are the RTL's
//                                   (tests/replayer/README.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <algorithm>
#include <memory>
#include <set>

#include "harness.h"
#include "portconf.h"
#include "replay_driver.h"
#include "replay_t.h"
#include "replay_window.h"
#include "replayconf.h"

// The unit tests' windows and encoders, compiled in unchanged (as suite_replayer does).
#undef TEST
#define TEST(name) [[maybe_unused]] static void name()
namespace unit {
#include "test_replayer.cpp"
}

namespace {

using namespace unit;
using rtest::LogEvent;
using rtest::ReplayDriver;
using rtest::ReplayWindow;
using rtest::W;
using units::Replayer;

// One pass of `w` under the ideal controller, recorded. `before`: inputs of the test's own between the load and the
// pass.
void pass(std::string& script, ReplayWindow& w, uint32_t rows, int64_t max_cycles = 20000, const std::function<void(ReplayDriver&)>& before = nullptr,
          int passes = 1) {
    ReplayDriver d(w);
    d.script = &script;
    w.rows = rows;
    d.load();
    if (before) before(d);
    for (int k = 0; k < passes; k++) d.pass(max_cycles);
}

std::unique_ptr<ReplayWindow> one_template(uint32_t ins, std::vector<W> words) {
    auto w = std::make_unique<ReplayWindow>();
    w->fixed_rom();
    w->rom(20, std::move(words));
    w->decode_rom(ins, 20);
    w->pc = 0x1000; w->instr_idx = 1;
    w->set_logs({}, false, false);
    return w;
}
const W kAddi = W::op(alu::ADDI).rd(129).rs1(130).rs2(132).fmt(1).report_c();
const uint32_t kIns = i_(5, 0, 0, A0, OP_IMM), kInl = r_(0, 2, 1, 0, 3, CUSTOM0);

std::unique_ptr<ReplayWindow> core_fed(int n, uint32_t offset) {
    auto w = std::make_unique<ReplayWindow>();
    w->fixed_rom();
    w->decode_class(kInl, 1);
    std::vector<W> words;
    words.resize(size_t(n));
    for (int k = 0; k < n; k++) words[size_t(k)] = W::op(alu::ADDI).rd(unsigned(137 + (k % 8))).rs1(0).rs2(128).cnst(uint64_t(k + 1)).fmt(0);
    words[size_t(n) - 1].report_c();
    w->template_(kInl, words);
    w->pc = 0x2000; w->instr_idx = 100; w->next_offset = offset;
    w->set_code({kInl, kInl}, false);
    w->set_logs({}, false, false);
    return w;
}

// A core-fed template with rd = x0 policy `pol`, with rd x0 and without, between ROM instructions; the window starts at
// the first expansion's row `offset`.
void nop_policy(std::string& s, unsigned pol, uint32_t offset) {
    uint32_t divx0 = r_(1, A1, A0, 4, 0, OP), div = r_(1, A1, A0, 4, A2, OP), addi = prog()[0];
    ReplayWindow v;
    templates(v);
    v.decode_class(divx0, 1); v.decode_class(div, 1); v.decode_rom(addi, T_ADDI);
    std::vector<W> dw(12);
    for (unsigned k = 0; k < 12; k++) dw[k] = W::op(alu::ADDI).rd(137 + k % 6).rs1(0).rs2(128).cnst(k).fmt(0);
    dw[0].rd0(pol); dw[11].rd(129).report_c();
    v.template_(divx0, dw); v.template_(div, dw);
    v.pc = 0x2000; v.instr_idx = 1; v.next_offset = offset;
    v.set_code({divx0, addi, div, divx0, addi}, false);
    v.set_logs({}, false, false);
    pass(s, v, 16);
}

struct Scenario { const char* name; std::function<void(std::string&)> gen; };

const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> all = {
        // ---- the hand-checked program: ROM templates, the NOP rows, M rows, a transfer, the program's end, padding
        {"program-raw", [](std::string& s) { auto w = program_window(false, false, false, true); pass(s, *w, 25); }},
        {"program-lru", [](std::string& s) { auto w = program_window(true, true, true, true); pass(s, *w, 25); }},
        {"program-no-store-log", [](std::string& s) { auto w = program_window(false, true, false, false); pass(s, *w, 25); }},
        {"program-twice", [](std::string& s) { auto w = program_window(true, false, true, true); pass(s, *w, 25, 20000, nullptr, 2); }},
        {"program-short", [](std::string& s) { auto w = program_window(false, false, false, true); pass(s, *w, 10); }},
        {"rows-0", [](std::string& s) { auto w = program_window(false, false, false, true); pass(s, *w, 0, 200); }},
        {"rows-0-empty", [](std::string& s) {
             ReplayWindow w;
             w.fixed_rom(); w.set_code({}, false); w.set_logs({}, false, false);
             pass(s, w, 0, 200);
         }},
        {"padding-lru", [](std::string& s) {
             ReplayWindow w;
             w.fixed_rom();
             w.set_code(std::vector<uint32_t>(3000, 0), true); w.set_logs({}, false, false);
             pass(s, w, 3000, 40000);
         }},
        // ---- core-fed expansions: the post, the expansion queue's credits, a row offset, the rd = x0 policies
        {"core-fed", [](std::string& s) { auto w = core_fed(300, 0); pass(s, *w, 600); }},
        {"core-fed-offset", [](std::string& s) { auto w = core_fed(300, 250); pass(s, *w, 350); }},
        {"core-fed-cut", [](std::string& s) { auto w = core_fed(20, 0); w->set_code({kInl}, false); pass(s, *w, 10); }},
        {"core-fed-nop-1", [](std::string& s) { nop_policy(s, 1, 0); }},
        {"core-fed-nop-3", [](std::string& s) { nop_policy(s, 3, 0); }},
        {"core-fed-nop-offset", [](std::string& s) { nop_policy(s, 3, 3); }},
        {"core-fed-remap"
, [](std::string& s) {
             ReplayWindow w;
             templates(w);
             uint32_t lbx0 = prog()[2], lba5 = prog()[1], jal = prog()[12];
             w.decode_class(lbx0, 1); w.decode_class(lba5, 1); w.decode_rom(jal, T_JAL);
             std::vector<W> lb(3);
             for (unsigned k = 0; k < 3; k++) { lb[k].lo = w.rom_lo[T_LB + k]; lb[k].hi = w.rom_hi[T_LB + k]; }
             w.template_(lbx0, lb); w.template_(lba5, lb);
             w.pc = 0x1004; w.instr_idx = 2; w.regs[A0] = 0x100; w.regs[40] = 0x4040; w.regs[A5] = 0x55;
             w.set_code({lba5, lbx0, jal, 0}, false);
             w.set_logs({{true, V1, true, false, 0}, {true, V2, true, false, 0}, {true, 0, false, false, 0}}, true, false);
             pass(s, w, 8);
         }},
        // ---- the ALU's extra cycles, the last-row rule, the logs' announcements
        {"pext", [](std::string& s) {
             uint32_t ins = r_(0, A1, A0, 0, A2, OP);
             auto w = one_template(ins, {W::op(alu::PEXT_S).rd(129).rs1(130).rs2(131).fmt(0).report_c()});
             w->regs[A0] = 0x8000000000000021ull; w->regs[A1] = 0x8000000000000029ull;
             w->set_code({ins, ins, ins}, false);
             pass(s, *w, 3);
         }},
        {"not-the-last-row", [](std::string& s) {
             uint32_t ins = b_(8, A0, A0, 0, BRANCH);
             auto w = one_template(ins, {W::op(alu::BEQ).rd(132).rs1(130).rs2(131).fmt(3).nfv().sign(),
                                         W::op(alu::JAL).rd(137).rs1(128).rs2(128).cnst(0).fmt(0).nfv().imm0pc0(),
                                         W::op(alu::ADDI).rd(138).rs1(0).rs2(128).cnst(7).fmt(0).report_c()});
             w->regs[A0] = 1;
             w->set_code({ins, ins}, false);
             pass(s, *w, 6);
         }},
        {"branch-not-taken", [](std::string& s) {
             uint32_t ins = b_(8, A1, A0, 0, BRANCH);
             auto w = one_template(ins, {W::op(alu::BEQ).rd(132).rs1(130).rs2(131).fmt(3).nfv().report_c().sign()});
             w->regs[A0] = 1; w->regs[A1] = 2;
             w->set_code({ins, ins, ins}, false);
             w->set_logs({{true, 5, false, false, 0}}, false, false);
             w->words_adj[1] = -1;
             pass(s, *w, 3);
         }},
        {"transfer-then-advice-2", [](std::string& s) {
             uint32_t jal = j_(8, 0, JAL), adv = i_(0, 0, 0, A0, OP_IMM);
             auto w = one_template(jal, {W::op(alu::JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).report_c()});
             w->rom(24, {W::op(alu::ADVICE).rd(129).rs1(128).rs2(128).cnst(0).from_log().report_c()});
             w->decode_rom(adv, 24);
             w->set_code({jal, adv}, false);
             w->set_logs({{true, 2, false, false, 0}, {true, 77, false, false, 0}}, false, false);
             pass(s, *w, 2);
         }},
        {"transfer-then-advice-3", [](std::string& s) {
             uint32_t jal = j_(8, 0, JAL), adv = i_(0, 0, 0, A0, OP_IMM);
             auto w = one_template(jal, {W::op(alu::JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).report_c()});
             w->rom(24, {W::op(alu::ADVICE).rd(129).rs1(128).rs2(128).cnst(0).from_log().report_c()});
             w->decode_rom(adv, 24);
             w->set_code({jal, adv, adv}, false);
             w->set_logs({{true, 2, false, false, 0}, {true, 77, false, false, 0}, {true, 78, false, false, 0}}, true, false);
             pass(s, *w, 3);
         }},
        // ---- underflows: the stop on the row that needs the value
        {"under-code-values", [](std::string& s) { auto w = one_template(kIns, {kAddi}); w->set_code({kIns, kIns, kIns}, false); pass(s, *w, 4, 400); }},
        {"under-code-words", [](std::string& s) {
             auto w = one_template(kIns, {kAddi});
             w->set_code(std::vector<uint32_t>(5, kIns), false); w->words_adj[0] = -1;
             pass(s, *w, 5, 400);
         }},
        {"under-code-lru", [](std::string& s) {
             auto w = one_template(kIns, {kAddi});
             w->set_code(std::vector<uint32_t>(40, kIns), true); w->values_adj[0] = 2;
             pass(s, *w, 42, 800);
         }},
        {"under-rlog-values", [](std::string& s) {
             auto w = one_template(kIns, {W::op(alu::ADVICE).rd(129).rs1(128).rs2(128).cnst(0).from_log().report_c()});
             w->set_code({kIns, kIns, kIns}, false);
             w->set_logs({{true, 9, false, false, 0}}, false, false);
             pass(s, *w, 3, 400);
         }},
        {"under-rlog-words", [](std::string& s) {
             auto w = one_template(kIns, {W::op(alu::ADVICE).rd(129).rs1(128).rs2(128).cnst(0).from_log().report_c()});
             w->set_code({kIns, kIns, kIns}, false);
             w->set_logs({{true, 9, false, false, 0}, {true, 10, false, false, 0}, {true, 11, false, false, 0}}, false, false);
             w->words_adj[1] = -1;
             pass(s, *w, 3, 400);
         }},
        {"under-wlog-values", [](std::string& s) {
             uint32_t sd = s_(8, A1, A0, 3, STORE);
             auto w = one_template(sd, {W::op(alu::SD).rd(132).rs1(130).rs2(131).fmt(2).from_log().report_c().sign()});
             w->regs[A0] = 0x2000;
             w->set_code({sd, sd, sd}, false);
             w->set_logs({{false, 0, false, true, 7}}, false, true);
             pass(s, *w, 3, 400);
         }},
        {"under-transfer", [](std::string& s) {
             uint32_t jal = j_(8, 0, JAL);
             auto w = one_template(jal, {W::op(alu::JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).report_c()});
             w->set_code({jal, jal, jal}, false);
             w->set_logs({{true, 2, false, false, 0}}, false, false);
             pass(s, *w, 3, 400);
         }},
        {"under-then-pass", [](std::string& s) {
             // an underflow's stop, a row left waiting for its transfer's word, then the next pass at once
             uint32_t jal = j_(8, 0, JAL);
             auto w = one_template(jal, {W::op(alu::JAL).rd(129).rs1(132).rs2(128).cnst(0).fmt(5).nfv().imm0pc0().rd0(2).report_c()});
             w->set_code({jal, jal, jal}, false);
             w->set_logs({{true, 2, false, false, 0}}, false, false);
             ReplayDriver d(*w);
             d.script = &s; d.after_done = 3;
             w->rows = 3;
             d.load();
             d.pass(400);
             w->set_logs({{true, 2, false, false, 0}, {true, 3, false, false, 0}, {true, 4, false, false, 0}}, false, false);
             d.after_done = 40;
             d.pass(400);
         }},
        // ---- leftovers
        {"left-code-value", [](std::string& s) {
             auto w = program_window(false, false, false, true);
             std::vector<uint32_t> code = prog(); code.resize(code.size() + PADDING + 1, 0); w->set_code(code, false);
             pass(s, *w, 25);
         }},
        {"left-rlog-value", [](std::string& s) {
             auto w = program_window(false, false, false, true);
             auto ev = program_logs(); ev.push_back(LogEvent{true, 5, false, false, 0}); w->set_logs(ev, true, false);
             pass(s, *w, 25);
         }},
        {"left-wlog-value", [](std::string& s) {
             auto w = program_window(false, false, false, true);
             auto ev = program_logs(); ev.push_back(LogEvent{false, 0, false, true, 5}); w->set_logs(ev, false, false);
             pass(s, *w, 25);
         }},
        {"left-code-word", [](std::string& s) { auto w = program_window(false, false, false, true); w->code.resize(w->code.size() + 2, 0); pass(s, *w, 25); }},
        {"left-rlog-entry", [](std::string& s) {
             auto w = program_window(false, false, false, true);
             w->rlog.resize(w->rlog.size() + 2, 0); w->words_adj[1] = -1;
             pass(s, *w, 25);
         }},
        // ---- invalid expansions
        {"invalid-class-3", [](std::string& s) {
             auto w = one_template(kIns, {kAddi});
             w->decode_class(kIns, 3);
             w->set_code({kIns, kIns, kIns}, false);
             pass(s, *w, 10, 400);
         }},
        {"invalid-operand", [](std::string& s) {
             uint32_t bad = i_(5, 0, 1, A0, OP_IMM);
             W word = kAddi; word.rs2(146);
             auto w = one_template(kIns, {kAddi});
             w->rom(30, {word}); w->decode_rom(bad, 30);
             w->set_code({kIns, kIns, bad, kIns}, false);
             pass(s, *w, 10, 400);
         }},
        {"invalid-opcode", [](std::string& s) {
             uint32_t bad = i_(5, 0, 1, A0, OP_IMM);
             W word = kAddi; word.lo = (word.lo & ~0xFFull) | 163;
             auto w = one_template(kIns, {kAddi});
             w->rom(30, {kAddi, word}); w->decode_rom(bad, 30);
             w->set_code({kIns, bad, kIns}, false);
             pass(s, *w, 10, 400);
         }},
        {"invalid-rom-511", [](std::string& s) {
             W word = kAddi; word.seq(1);
             auto w = one_template(kIns, {kAddi});
             w->rom_lo[511] = word.lo; w->rom_hi[511] = word.hi;
             w->decode_rom(kIns, 511);
             w->set_code({kIns, kIns, kIns}, false);
             pass(s, *w, 10, 400);
         }},
        // ---- a slow consumer: the output queue fills, and no row starts while it is full (17.8.11)
        {"slow-consumer", [](std::string& s) {
             auto w = core_fed(60, 0);
             ReplayDriver d(*w);
             d.script = &s;
             d.ready = [](int64_t t) { return t % 7 == 0 || (t / 64) % 3 == 1; };   // a row in 7 cycles, and bursts
             w->rows = 120;
             d.load();
             d.pass(20000);
         }},
        {"slow-consumer-program", [](std::string& s) {
             auto w = program_window(true, true, true, true);
             std::vector<uint32_t> code = prog();
             code.resize(code.size() + 80, 0);
             w->set_code(code, true);
             ReplayDriver d(*w);
             d.script = &s;
             d.ready = [](int64_t t) { return t % 11 == 3; };
             d.after_done = 400;
             w->rows = 97;
             d.load();
             d.pass(20000);
         }},
        // ---- a slower core: an entry, or an expansion word, that comes D cycles late, for each D: the pass's wait and
        // its start again, at every phase of the fetch, the decoders and the row engine
        {"late-code", [](std::string& s) {
             auto w = one_template(kIns, {W::op(alu::ADDI).rd(129).rs1(129).rs2(132).fmt(1).report_c()});
             w->set_code(std::vector<uint32_t>(12, kIns), false);
             ReplayDriver d(*w);
             d.script = &s; d.after_done = 6;
             w->rows = 12;
             d.load();
             for (int64_t late = 0; late < 20; late++) {
                 d.hold = [late](unsigned q, size_t index, int64_t t) { return q == 0 && index == 1 && t < 8 + late; };
                 d.pass(2000);
             }
         }},
        {"late-read-log", [](std::string& s) {
             auto w = one_template(kIns, {W::op(alu::LD).rd(129).rs1(130).rs2(128).cnst(0).fmt(0).from_log().report_c()});
             w->set_code(std::vector<uint32_t>(8, kIns), false);
             std::vector<LogEvent> ev;
             for (uint64_t k = 0; k < 8; k++) ev.push_back({true, 100 + k, true, false, 0});
             w->set_logs(ev, false, false);
             ReplayDriver d(*w);
             d.script = &s; d.after_done = 6;
             w->rows = 8;
             d.load();
             for (int64_t late = 0; late < 18; late++) {
                 d.hold = [late](unsigned q, size_t index, int64_t t) { return q == 1 && index == 1 && t < 8 + late; };
                 d.pass(2000);
             }
         }},
        {"late-expansion-word", [](std::string& s) {
             auto w = core_fed(8, 0);
             w->set_code({kInl}, false);
             ReplayDriver d(*w);
             d.script = &s; d.after_done = 6;
             w->rows = 8;
             d.load();
             for (int64_t late = 0; late < 18; late++) {
                 d.hold = [late](unsigned q, size_t index, int64_t t) { return q == 3 && index == 4 && t < 16 + late; };
                 d.pass(2000);
             }
         }},
        // ---- a core that loads EVENTS in every cycle: a load in the cycle of a credit's fill and of its return
        {"eager-loads", [](std::string& s) {
             auto w = core_fed(300, 0);
             ReplayDriver d(*w);
             d.script = &s; d.eager = true;
             w->rows = 600;
             d.load();
             d.pass(20000);
         }},
        // ---- the queues: an overrun, and pushes before the pass
        {"overrun", [](std::string& s) {
             auto w = program_window(false, false, false, true);
             pass(s, *w, 25, 20000, [&](ReplayDriver& d) {
                 for (int k = 0; k < 258; k++) d.write(ReplayDriver::PART_CODE * 2, uint64_t(k), 0);   // 258 pushes into the read-log queue
                 d.resend_state();
             });
         }},
    };
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "--list")) {
        for (const Scenario& sc : scenarios()) std::printf("%s\n", sc.name);
        return 0;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "--gen")) {
        for (const Scenario& sc : scenarios())
            if (!std::strcmp(sc.name, argv[2])) {
                std::string s = std::string("# replayconf script v2: ") + sc.name + "\n";
                sc.gen(s);
                std::fwrite(s.data(), 1, s.size(), stdout);
                return 0;
            }
        std::fprintf(stderr, "replayconf: no scenario %s\n", argv[2]);
        return 2;
    }
    if (argc < 2) { std::fprintf(stderr, "usage: replayconf SCRIPT [MAX_ROWS] | --list | --gen NAME\n"); return 2; }
    std::string path = argv[1];
    bool ok = false;
    std::string script = portconf::read_command((path.size() > 3 && path.substr(path.size() - 3) == ".gz" ? "gzip -dc " : "cat ") + path, &ok);
    if (!ok) { std::fprintf(stderr, "replayconf: cannot read %s\n", argv[1]); return 2; }
    static units::Replayer r(0);
    std::string obs = replayconf::run(script, argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1u << 20, &r);
    std::fwrite(obs.data(), 1, obs.size(), stdout);
    return 0;
}
