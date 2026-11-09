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
#include <string>

#include "t2_harness.h"
#include "units/tile_ctl.h"

int main(int argc, char** argv) {
    std::string sram, files;
    unsigned parts = 1;
    uint64_t cap = 2'000'000;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { if (i + 1 >= argc) { std::fprintf(stderr, "t2: %s needs a value\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if (a == "--sram") sram = next();
        else if (a == "--files") files = next();
        else if (a == "--parts") parts = unsigned(std::atoi(next()));
        else if (a == "--boot-cycles") cap = std::strtoull(next(), nullptr, 10);
        else { std::fprintf(stderr, "t2: unknown argument %s\n", a.c_str()); return 2; }
    }
    std::string err;
    if (!files.empty()) {
        units::tracer::FileStore fs;
        uint64_t pages = 0;
        if (!t2::load_files(files, fs, err, &pages)) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
        std::printf("files: %" PRIu64 " pages, nextFree %lld, sum %016" PRIx64 "\n", pages, (long long)fs.next_free(), fs.scan_sum());
    }
    uint8_t dom[64];
    t2::partition(parts, dom);
    sim::Options opt;
    opt.threads = parts;
    auto m = t2::build_machine(parts > 1 ? dom : nullptr, opt, err);
    if (!m) { std::fprintf(stderr, "t2: %s\n", err.c_str()); return 1; }
    sim::Sim& s = *m->sim;
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
    uint64_t c = 0;
    while (!t2::cores_booted(m->dmem(64), cc1) && c < cap) { s.run(1); c++; }
    if (!t2::cores_booted(m->dmem(64), cc1)) {
        std::printf("the boot didn't finish in %" PRIu64 " cycles\n", cap);
        return 1;
    }
    std::printf("boot-end %" PRIu64 " (ref: 7222)\n", s.now() - 1);
    return 0;
}
