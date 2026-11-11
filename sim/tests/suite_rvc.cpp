// suite_rvc.cpp (WORKER21; P7a, inventory C22): v1.3's RvcTest (sim/ref/meshsim/test/RvcTest.java) on the C++ RVC
// decompressor (src/units/rvc.h), on W20's suite.h. Its output must equal v1.3's log of run_tests.sh's step `rvc`
// (tests/tracersuites/rvc.log, from oracle/ref/tracersuites.sh).
//
// The Tracer's RVC decompressor (spec 4.21.4 step 2) against gold's (gold.Decoder.uncompress, a16z's rules with D12) on
// all 2^16 16-bit encodings, and the code-stream word's rules: bit 0 cleared as the compressed flag, 0 (an invalid
// expansion) for the encodings with no 32-bit form and the reserved forms (c.unimp among them).
// Gold stays Java in v1.35's pipeline/ (D40): its expansion of every compressed encoding is committed once as
// tests/tracersuites/rvc-gold.txt.gz (oracle/ref/GoldRvc.java). Without the file the comparison is PENDING, as v1.3's
// RvcTest without gold's classes.
// Usage: suite_rvc (from sim/)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "suite.h"
#include "units/rvc.h"

namespace {

const char* const kGold = "tests/tracersuites/rvc-gold.txt.gz";

// gold's expansion by encoding (index h; only compressed forms present); false if the fixture can't be read whole.
bool load_gold(std::vector<uint32_t>& gold, std::vector<bool>& have) {
    gold.assign(65536, 0);
    have.assign(65536, false);
    FILE* f = std::fopen(kGold, "rb");
    if (!f) return false;
    std::fclose(f);
    FILE* p = popen((std::string("gzip -dc ") + kGold).c_str(), "r");
    if (!p) return false;
    unsigned h = 0, x = 0;
    int n = 0;
    while (std::fscanf(p, "%x %x", &h, &x) == 2) {
        if (h > 0xFFFF || (h & 3) == 3) { pclose(p); return false; }
        gold[h] = uint32_t(x);
        have[h] = true;
        n++;
    }
    return pclose(p) == 0 && n == 49152;
}

}  // namespace

int main() {
    suite::group("Rvc against gold");
    std::vector<uint32_t> gold;
    std::vector<bool> have;
    const bool g = load_gold(gold, have);
    if (!g) suite::pending("Rvc against gold, every 16-bit encoding", "gold's expansions in tests/tracersuites/rvc-gold.txt.gz");
    int same = 0, compressed_forms = 0, invalid = 0;
    for (uint32_t h = 0; h < 65536; h++) {
        if ((h & 3) == 3) continue;  // a 32-bit instruction's first half
        compressed_forms++;
        const uint32_t mine = rvc::uncompress(h);
        if (g) {
            if (!have[h]) suite::check(false, "0x%04x: no gold expansion in the fixture", h);
            else if (mine == gold[h]) same++;
            else suite::check(false, "0x%04x: Rvc 0x%08x, gold 0x%08x", h, mine, gold[h]);
        }
        const uint32_t cw = rvc::code_word(h);
        if (mine == 0xffffffffu || mine == 0) {
            invalid++;
            suite::check(cw == 0, "0x%04x: no 32-bit form gives code word 0", h);
        } else
            suite::check(cw == (mine & ~1u) && (cw & 3) == 2, "0x%04x: the code word is the expansion with bit 0 cleared", h);
    }
    if (g) suite::eq(compressed_forms, same, "every compressed encoding equals gold's expansion");
    suite::eq(0, rvc::code_word(0x0000), "c.unimp (0x0000) has no 32-bit form: an invalid expansion");
    suite::eq(0x00101012, rvc::code_word(0x0002 | 0 << 7 | 1 << 2), "C.SLLI x0, 1 (a HINT) is SLLI x0, x0, 1 (D12), bit 0 cleared");
    suite::eq(0x0000006E, rvc::code_word(0xA001), "C.J 0 is JAL x0, 0 (the program's end), bit 0 cleared");
    suite::eq(0x00100072, rvc::code_word(0x9002), "C.EBREAK is EBREAK, bit 0 cleared");
    std::printf("compressed encodings %d, with no valid form %d\n", compressed_forms, invalid);
    return suite::done("RvcTest");
}
