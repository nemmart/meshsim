// rtl_left: makes tests/acceptance/rtl/<program>.txt, the RTL's reference of a delivery's windows (tests/stage_a.h,
// RtlReference), from the RTL's own dumps: Snarky One's replayer_acceptance_test run with REPLAY_DUMP=DIR writes, per
// window and variant, DIR/<window>.<variant>.left, the cycle each row left `replay_row` as a little-endian 32-bit
// word, START's cycle being 0 (tests/acceptance/README.md says how). Nothing here comes from the simulator.
//
//   rtl_left DIR WINDOWS > tests/acceptance/rtl/<program>.txt
// prints `variant window rows cycles hash` for every window 0..WINDOWS-1 of each variant (state, state-lru,
// state-pick): the rows, the last row's cycle, and h = mix(h ^ cycle) over the rows in order.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: rtl_left DIR WINDOWS\n"); return 2; }
    const std::string dir = argv[1];
    const long windows = std::atol(argv[2]);
    std::printf("# The RTL's replayer_unit under its acceptance's ideal controller: the cycles of each window's pass.\n");
    std::printf("# variant window rows cycles hash (tests/stage_a.h, RtlReference; made by tests/acceptance/rtl_left.cpp)\n");
    for (const char* variant : {"state", "state-lru", "state-pick"})
        for (long w = 0; w < windows; w++) {
            const std::string path = dir + "/" + std::to_string(w) + "." + variant + ".left";
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) { std::fprintf(stderr, "rtl_left: no %s\n", path.c_str()); return 1; }
            uint64_t h = 0, rows = 0, last = 0;
            uint8_t b[4];
            while (std::fread(b, 1, 4, f) == 4) {
                last = uint64_t(b[0]) | uint64_t(b[1]) << 8 | uint64_t(b[2]) << 16 | uint64_t(b[3]) << 24;
                h = mix(h ^ last);
                rows++;
            }
            std::fclose(f);
            std::printf("%s %ld %llu %llu %016llx\n", variant, w, (unsigned long long)rows, (unsigned long long)last, (unsigned long long)h);
        }
    return 0;
}
