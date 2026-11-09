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

int main(int argc, char** argv) {
    std::string sram, files;
    unsigned parts = 1;
    uint64_t cap = 2'000'000;
    bool trace = false;                       // --trace: the controllers' and tile 0's pc between cycles (BootStage trace)
    std::string fp_boot;                      // --fp-boot FILE: every unit at the boot's end against golden-m3/boot
    unsigned m2_off = 1;                      // --m2-offset N (diagnosis)
    std::vector<std::string> dumps;           // --fp-dump UNIT: its stream word by word at the boot's end
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
    if (!dumps.empty()) {
        fp::M3Chip mc;
        fp::m3_attach(s, mc, units::reqbus::ReqBusConfig{});
        fp::FpRegistry reg;
        fp::m3_register(reg, mc);
        for (const std::string& u : dumps) {
            fp::FpCtx y{uint64_t(end), &s};
            if (!fp::m3_own_writer(u)) y.cycle += m2_off;
            reg.dump(u, y, stdout);
        }
    }
    return 0;
}
