// test_header: HL's four example headers of 20 bytes and its CRC values, byte for byte; the numbers' arithmetic; and,
// given the path of gold's VECTORS.txt (updater_wip/pipeline/gold/test/packets/), every crc32c line in it.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "header.h"

using namespace hl;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::vector<uint8_t> hex(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back(uint8_t(strtoul(s.substr(i, 2).c_str(), nullptr, 16)));
    return v;
}

static void example(const Header& h, const char* want) {
    uint8_t b[HEADER_BYTES]; pack(h, b);
    const auto w = hex(want);
    CHECK(w.size() == HEADER_BYTES && memcmp(b, w.data(), HEADER_BYTES) == 0);
    CHECK(crc32c(b, HEADER_BYTES) == CRC_GOOD_HEADER);
    const Header u = unpack(b);
    uint8_t again[HEADER_BYTES]; pack(u, again);
    CHECK(memcmp(again, b, HEADER_BYTES) == 0);
    CHECK(u.stream == h.stream && u.flags == h.flags && u.length == h.length && u.hops == h.hops && u.dsum == h.dsum);
    for (unsigned t = 0; t < STREAMS; t++) CHECK(u.number[t] == h.number[t] && u.ack[t] == h.ack[t] && u.stop[t] == h.stop[t] && u.resend[t] == h.resend[t]);
}

// Gold's VECTORS.txt: its crc32c lines. Its packets are of the header of 16 bytes, before 2026-11-17, and are not read.
static int vectors(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    char line[4096];
    int crcs = 0;
    while (fgets(line, sizeof line, f)) {
        char key[64] = "", a[2048] = "", b[64] = "";
        if (char* hash = strchr(line, '#')) *hash = 0;
        if (sscanf(line, "%63s %2047s %63s", key, a, b) == 3 && std::string(key) == "crc32c") {
            const auto v = std::string(a) == "-" ? std::vector<uint8_t>() : hex(a);
            CHECK(crc32c(v.data(), v.size()) == uint32_t(strtoul(b, nullptr, 0))); crcs++;
        }
    }
    fclose(f);
    printf("vectors: %d crc32c lines\n", crcs);
    CHECK(crcs > 0);
    return 0;
}

int main(int argc, char** argv) {
    // HL, "The two checks": the table's values.
    CHECK(crc32c(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xE3069283u);
    CHECK(crc32c(nullptr, 0) == 0);
    const uint8_t zero[16] = {};
    CHECK(crc32c(zero, 16) == 0x42709AEAu);
    // HL's four example headers of 20 bytes.
    {
        Header h; h.stream = 2; h.length = 1; h.number[FG] = 1; h.number[BG] = 1; h.dsum = crc32c(zero, 16);
        example(h, "020101000000100000000000EA9A704245941AE9");
        h.hops = 1;
        example(h, "020101000000100000000001EA9A7042E9FB0BD1");
        Header g; g.stream = 1; g.length = 1; g.number[FG] = 0x2ABCD; g.ack[FG] = 0x15555; g.number[BG] = 0x123; g.ack[BG] = 0x3FFFE;
        g.hops = 3; g.stop[FG] = true; g.resend[BG] = true; g.dsum = crc32c(zero, 16);
        example(g, "0101CDAB5655351280FFFF93EA9A704257AE7815");
        Header z; z.stream = 1; z.number[FG] = 5; z.ack[FG] = 4; z.number[BG] = 9; z.ack[BG] = 8;
        example(z, "0100050010009000000200000000000000DD0F8F");
    }
    // The numbers, at the real width and at the shrunken model's.
    for (uint32_t last : {0x3FFFEu, 6u, 14u}) {
        const Numbers n{last};
        CHECK(n.succ(0) == 1 && n.succ(1) == 2 && n.succ(last) == 1 && n.succ(last - 1) == last);
        CHECK(n.dist(1, 1) == 0 && n.dist(last, 1) == 1 && n.dist(1, last) == last - 1 && n.dist(last - 1, 2) == 3);
        CHECK(n.dist_ack(0, 3) == 3 && n.dist_ack(last, 2) == 2);
        CHECK(n.ahead(last, 1) && n.ahead(last - 1, 1) && !n.ahead(1, last) && !n.ahead(2, 2) && n.ahead(2, 3));
    }
    if (argc > 1 && vectors(argv[1])) failures++;
    printf(failures ? "test_header: %d FAILED\n" : "test_header: ok\n", failures);
    return failures != 0;
}
