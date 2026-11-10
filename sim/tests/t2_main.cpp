// t2 (WORKER16; build/<cc>-<mode>/t2): T2 on the framework's chip, as W13's M3Seg runs ref's (AUDIT-M3 sections 0, 7).
// So far the boot (the first milestone), up to where C&C 1's peripheral is needed in its slot:
//
//   t2 [--sram FILE] [--files FILE] [--parts N] [--boot-cycles N]
//
//   --sram FILE    the harness's big-SRAM landings ($DATA/w13-golden-m3/<run>.sram.gz), poked before the boot
//   --files FILE   the memory at segment 10's start (<run>.files.gz), loaded and checked (the Tracer's memory, once
//                  C&C 3's slot holds the Tracer)
//   --parts N      domains (T2's partition: tile 2 and C&C 3 alone, the rest spread); threads = N
//   --boot-cycles  the cap on the boot (ref: coresBooted at 7,222)
//
// Prints the cycle the boot ends (Mach.coresBooted, read between cycles as ref's harness does), or where it stopped.
// Next (the boot exact): every unit's fingerprint at the boot's end against tests/golden-m3/boot (step 5).
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "statefp_m3.h"
#include "t2_harness.h"
#include "units/tile_ctl.h"
#include "units/periph_slot.h"
#include "units/replayer.h"
#include "units/tracer.h"

namespace {

// The golden's unit lines by point: label -> name -> hash (fpchip.gz: "c name hash" lines, `#` lines skipped).
using Points = std::map<uint64_t, std::map<std::string, std::string>>;
unsigned g_tr_skipped = 0;
bool load_points(const std::string& path, Points& p, std::string& err) {
    std::vector<uint8_t> raw;
    if (!t2::read_gz(path, raw, err)) return false;
    const char* s = reinterpret_cast<const char*>(raw.data());
    const size_t n = raw.size();
    for (size_t i = 0; i < n;) {
        size_t e = i; while (e < n && s[e] != '\n') e++;
        if (s[i] != '#') {
            const std::string l(s + i, e - i);
            const size_t a = l.find(' '), b = l.rfind(' ');
            if (a != std::string::npos && b > a) p[std::strtoull(l.c_str(), nullptr, 10)][l.substr(a + 1, b - a - 1)] = l.substr(b + 1);
        }
        i = e + 1;
    }
    return true;
}

// One point: every unit and the chip line against the golden's (the chip and tr skipped, and counted, at a point with
// a PULSE_DMA of t in flight: the Tracer's out_start isn't in its stream yet, W14). Returns the differing
// units' count; prints the first few.
unsigned check_point(const fp::FpRegistry& reg, const fp::M3Chip& mc, sim::Sim& s, uint64_t t, const Points& gold, unsigned m2_off,
                     const char* tag) {
    auto it = gold.find(t);
    if (it == gold.end()) { std::printf("point %llu (%s): not in the golden\n", (unsigned long long)t, tag); return 1; }
    std::vector<std::string> lines;
    fp::g_dma_pulse_at_point = false;
    fp::m3_point(reg, mc, fp::FpCtx{t, &s}, lines, m2_off);
    const bool pulse = fp::g_dma_pulse_at_point;
    unsigned bad = 0, shown = 0, eq = 0;
    for (const std::string& l : lines) {
        const size_t a = l.find(' '), b = l.rfind(' ');
        const std::string n = l.substr(a + 1, b - a - 1), h = l.substr(b + 1);
        if (n == "chip" && pulse) continue;                    // it hashes the skipped tr
        auto g = it->second.find(n);
        if (g != it->second.end() && g->second == h) { eq++; continue; }
        if (pulse && n == "tr") { g_tr_skipped++; std::printf("  point %llu (%s): tr not compared (a PULSE_DMA of t in flight: the Tracer's out_start)\n", (unsigned long long)t, tag); continue; }
        bad++;
        if (shown++ < 8) std::printf("  point %llu (%s): %s differs\n", (unsigned long long)t, tag, n.c_str());
    }
    std::printf("point %llu (%s): %u units equal, %u differ\n", (unsigned long long)t, tag, eq, bad);
    std::fflush(stdout);
    return bad;
}

// M3Seg's segment: CC_SEGMENT (C&C 1: the tiles' command, C&C 3's start), then the run to the Tracer's stop or C&C 3's
// exit with every tile exited; points every 100,000 cycles, at C&C 3's exit and at the end (labels as M3Seg's).
int run_segment(t2::Machine& mach, sim::Sim& s, const std::string& fp_points, const std::vector<std::string>& dumps, int64_t dump_at,
                uint64_t cap, unsigned m2_off, unsigned parts, uint64_t check_from, const std::string& rows_ref,
                const std::string& program, const t2::Image& cc3img) {
    Points gold;
    std::string err;
    if (!fp_points.empty() && !load_points(fp_points, gold, err)) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
    fp::M3Chip mc;
    fp::m3_attach(s, mc, units::reqbus::ReqBusConfig{});
    fp::FpRegistry reg;
    fp::m3_register(reg, mc);
    t2::cc_segment(s);
    const units::TileCore& u3 = s.unit<units::TileCore>("cc3.core");
    const units::tracer::Tracer* tr = mc.tr;
    const uint64_t start = s.now();
    unsigned bad_points = 0, points = 0;
    int64_t cc3_exit = -1;
    const char* why = "cap";
    for (;;) {
        const uint64_t t = s.now() - 2, c = s.now() - 1;     // the last cycle run (the label), ref's c.cycle
        if ((t + 1) % 100000 == 0 || int64_t(t) + 1 == dump_at) fp::m3_note_before(mc, s, t);   // ref's pending mask stores
        if (dump_at >= 0 && int64_t(t) == dump_at) {
            for (const std::string& u : dumps) {
                fp::FpCtx y{t, &s};
                if (!fp::m3_own_writer(u)) { y.cycle += m2_off; y.label_shift = m2_off; }
                fp::g_hide_last_put = y.label_shift != 0;
                reg.dump(u, y, stdout);
                fp::g_hide_last_put = false;
            }
            return 0;
        }
        if (!gold.empty() && t > 0 && t % 100000 == 0 && s.now() > start && t >= check_from) { points++; if (check_point(reg, mc, s, t, gold, m2_off, "")) bad_points++; }
        if (tr && tr->stopped()) { why = "the Tracer stopped"; break; }
        if (u3.rv.halt != units::Rv32::RUN) {
            if (cc3_exit < 0) {
                cc3_exit = int64_t(c);
                std::printf("C&C 3 exited (point %llu)\n", (unsigned long long)t);
                if (!gold.empty()) { points++; if (check_point(reg, mc, s, t, gold, m2_off, "cc3-exit")) bad_points++; }
            }
            bool all = true;
            for (unsigned k = 0; k < 64 && all; k++) if (s.unit<units::TileCore>(units::tile_unit(k, "core")).rv.halt == units::Rv32::RUN) all = false;
            if (all) { why = "C&C 3 and every tile exited"; break; }
            if (c - uint64_t(cc3_exit) >= 1'000'000) { why = "a tile not exited 1 M cycles after C&C 3"; break; }
        }
        if (s.now() - start > cap) break;
        s.run(1);
        if ((s.now() - start) % 1'000'000 == 0) { std::printf("... cycle %llu\n", (unsigned long long)(s.now() - 1)); std::fflush(stdout); }
    }
    const uint64_t t_end = s.now() - 2;
    std::printf("end: %s at cycle %llu (ref's c.cycle %llu)\n", why, (unsigned long long)t_end, (unsigned long long)(s.now() - 1));
    if (!gold.empty()) { points++; if (check_point(reg, mc, s, t_end, gold, m2_off, "end")) bad_points++; }
    std::printf("points: %u checked, %u with a difference, tr skipped at %u; request bus lag_cycles %llu; parts %u\n", points, bad_points, g_tr_skipped,
                (unsigned long long)mc.rbu.root->lag_cycles(), parts);
    // M3Seg's checks at the end: C&C 3's tr_results; every tile's exit value and its rows (count and sink hash against
    // rows.bin's, Quest 169). Two more cycles first: a SIM_EXIT stored in the last cycle reaches the block then.
    s.run(2);
    unsigned bad = 0;
    const uint32_t* d3 = mach.dmem(66);
    const uint32_t R = cc3img.at("tr_results") / 4;
    std::printf("tr_results: status %u, windows %u, transfers %u, ERRORS 0x%x, advice left %u\n", d3[R], d3[R + 1], d3[R + 4], d3[R + 6], d3[R + 7]);
    if (d3[R] != 1) bad++;
    std::map<unsigned, std::pair<uint64_t, uint64_t>> want;
    if (!rows_ref.empty()) {
        FILE* f = std::fopen(rows_ref.c_str(), "r");
        if (!f) { std::printf("rows: cannot read %s\n", rows_ref.c_str()); return 1; }
        char p[64]; unsigned w; unsigned long long n, h;
        while (std::fscanf(f, "%63s %u %llu %llx", p, &w, &n, &h) == 4) if (program == p) want[w] = {n, h};
        std::fclose(f);
        if (want.size() != 64) { std::printf("rows: %zu windows of %s in %s, want 64\n", want.size(), program.c_str(), rows_ref.c_str()); bad++; }
    }
    unsigned rows_ok = 0, exits_ok = 0;
    for (unsigned k = 0; k < 64; k++) {
        const units::TileCtl& ctl = s.unit<units::TileCtl>(units::tile_unit(k, "ctl"));
        if (ctl.s.sim_exit_value == 1) exits_ok++; else { bad++; std::printf("tile %u: exit value %u\n", k, ctl.s.sim_exit_value); }
        const units::Replayer& rp = s.unit<units::PeriphSlot>(units::tile_unit(k, "slot")).s.replayer(1);
        auto it = want.find(640 + k);
        if (it == want.end()) continue;
        if (rp.rows_sunk() == it->second.first && rp.row_hash() == it->second.second) rows_ok++;
        else { bad++; std::printf("tile %u: %llu rows, hash %016llx; rows.bin %llu, %016llx\n", k, (unsigned long long)rp.rows_sunk(),
                                  (unsigned long long)rp.row_hash(), (unsigned long long)it->second.first, (unsigned long long)it->second.second); }
    }
    std::printf("exits: %u of 64 tiles exited 1; rows: %u of %zu tiles equal to rows.bin (count and hash)\n", exits_ok, rows_ok, want.size());
    return bad_points || bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string sram, files;
    unsigned parts = 1;
    uint64_t cap = 2'000'000;
    bool trace = false;                       // --trace: the controllers' and tile 0's pc between cycles (BootStage trace)
    std::string fp_boot;                      // --fp-boot FILE: every unit at the boot's end against golden-m3/boot
    unsigned m2_off = 1;                      // --m2-offset N (diagnosis)
    std::vector<std::string> dumps;           // --fp-dump UNIT: its stream word by word at the boot's end (or at --dump-at)
    bool segment = false;                     // --segment: CC_SEGMENT, then the run to its end (M3Seg's conditions)
    std::string fp_points;                    // --fp-points FILE: every point against the golden's unit lines (fpchip.gz)
    int64_t dump_at = -1;                     // --dump-at T: the --fp-dump units at point T (label), then stop
    uint64_t seg_cap = 64ull * 65536 * 12 + 5'000'000;   // M3Seg's cap for 64 windows
    uint64_t check_from = 0;                  // --check-from T: points before T not compared (a run split in two)
    std::string rows_ref;                     // --rows-ref FILE: per window "program window count hash" (Quest 169)
    std::string program = "evm-zone-29";      // --program P: the rows reference's lines for P
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { if (i + 1 >= argc) { std::fprintf(stderr, "t2: %s needs a value\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if (a == "--sram") sram = next();
        else if (a == "--files") files = next();
        else if (a == "--parts") parts = unsigned(std::atoi(next()));
        else if (a == "--boot-cycles") cap = std::strtoull(next(), nullptr, 10);
        else if (a == "--trace") trace = true;
        else if (a == "--fp-boot") fp_boot = next();
        else if (a == "--m2-offset") m2_off = unsigned(std::atoi(next()));
        else if (a == "--fp-dump") dumps.push_back(next());
        else if (a == "--segment") segment = true;
        else if (a == "--fp-points") fp_points = next();
        else if (a == "--dump-at") dump_at = std::strtoll(next(), nullptr, 10);
        else if (a == "--cap") seg_cap = std::strtoull(next(), nullptr, 10);
        else if (a == "--check-from") check_from = std::strtoull(next(), nullptr, 10);
        else if (a == "--rows-ref") rows_ref = next();
        else if (a == "--program") program = next();
        else { std::fprintf(stderr, "t2: unknown argument %s\n", a.c_str()); return 2; }
    }
    std::string err;
    auto store = std::make_unique<units::tracer::FileStore>();     // the Tracer's memory (empty without --files)
    if (!files.empty()) {
        uint64_t pages = 0;
        if (!t2::load_files(files, *store, err, &pages)) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
        std::printf("files: %" PRIu64 " pages, nextFree %lld, sum %016" PRIx64 "\n", pages, (long long)store->next_free(), store->scan_sum());
    }
    uint8_t dom[64];
    t2::partition(parts, dom);
    sim::Options opt;
    opt.threads = parts;
    auto m = t2::build_machine(parts > 1 ? dom : nullptr, opt, err);
    if (!m) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
    sim::Sim& s = *m->sim;
    t2::attach_store(*m, std::move(store));
    if (!sram.empty()) {
        std::vector<t2::SramLanding> l;
        if (!t2::load_sram(sram, l, err)) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
        t2::poke_sram(s, l);
        std::printf("sram: %zu words poked\n", l.size());
    }
    t2::Image cc1, cc23, tile, cc3;
    for (auto [im, n] : {std::pair<t2::Image*, const char*>{&cc1, "cc1"}, {&cc23, "cc23"}, {&tile, "replay_ctl"}, {&cc3, "tracer_ctl"}})
        if (!t2::load_image("tests/m3-images", n, *im, err)) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
    t2::boot_cores(s, cc1, cc23, tile, cc3, 8, 0);
    // Mach.run(coresBooted): the condition before each cycle; the boot-end point is the cycle before it held (M3Seg)
    std::string tr;
    const int64_t end = t2::run_boot(*m, cc1, cap, trace ? &tr : nullptr);
    std::fputs(tr.c_str(), stdout);
    if (end < 0) { std::printf("the boot didn't finish in %" PRIu64 " cycles\n", cap); return 1; }
    std::printf("boot-end %lld (ref: 7222)\n", (long long)end);
    if (!fp_boot.empty()) {
        std::vector<uint8_t> raw;
        if (!t2::read_gz(fp_boot, raw, err)) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
        std::map<std::string, std::string> want;
        std::string text(raw.begin(), raw.end());
        for (size_t p = 0; p < text.size();) {
            size_t e = text.find('\n', p); if (e == std::string::npos) e = text.size();
            const std::string l = text.substr(p, e - p); p = e + 1;
            const size_t a = l.find(' '), b = l.rfind(' ');
            if (a != std::string::npos && b > a) want[l.substr(a + 1, b - a - 1)] = l.substr(b + 1);
        }
        fp::M3Chip mc;
        fp::m3_attach(s, mc, units::reqbus::ReqBusConfig{});
        fp::FpRegistry reg;
        fp::m3_register(reg, mc);
        std::vector<std::string> lines;
        fp::m3_point(reg, mc, fp::FpCtx{uint64_t(end), &s}, lines, m2_off);
        std::map<std::string, unsigned> bad_kind;
        unsigned bad = 0, good = 0;
        for (const std::string& l : lines) {
            const size_t a = l.find(' '), b = l.rfind(' ');
            const std::string n = l.substr(a + 1, b - a - 1), h = l.substr(b + 1);
            auto it = want.find(n);
            if (it == want.end()) { std::printf("  %s: not in the golden\n", n.c_str()); bad++; continue; }
            if (it->second == h) { good++; continue; }
            bad++;
            const size_t d = n.find('.');
            const std::string kind = n.rfind("t", 0) == 0 && d != std::string::npos && n[1] >= '0' && n[1] <= '9' ? "t*." + n.substr(d + 1)
                                   : n.rfind("cc", 0) == 0 && d != std::string::npos && n[2] >= '1' && n[2] <= '3' ? "cc*." + n.substr(d + 1) : n;
            if (bad_kind[kind]++ < 2) std::printf("  differs: %s\n", n.c_str());
        }
        std::printf("fp-boot: %u units equal, %u differ\n", good, bad);
        for (auto& [k, n] : bad_kind) std::printf("  %-14s %u\n", k.c_str(), n);
    }
    if (segment) return run_segment(*m, s, fp_points, dumps, dump_at, seg_cap, m2_off, parts, check_from, rows_ref, program, cc3);
    if (!dumps.empty()) {
        fp::M3Chip mc;
        fp::m3_attach(s, mc, units::reqbus::ReqBusConfig{});
        fp::FpRegistry reg;
        fp::m3_register(reg, mc);
        for (const std::string& u : dumps) {
            fp::FpCtx y{uint64_t(end), &s};
            if (!fp::m3_own_writer(u)) { y.cycle += m2_off; y.label_shift = m2_off; }
            reg.dump(u, y, stdout);
        }
    }
    return 0;
}
