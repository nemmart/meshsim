// test_header: HL's four example headers and its CRC values, byte for byte; the restart packet's data and its
// checksum; the numbers' arithmetic; and,
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
    CHECK(u.number == h.number && u.ack == h.ack && u.stop[FG] == h.stop[FG] && u.stop[BG] == h.stop[BG] && u.reserved == h.reserved);
}

// Gold's VECTORS.txt: its crc32c lines. Its packets are of the header before 2026-11-17, and are not read.
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
    // HL's four example headers.
    {
        Header h; h.stream = 2; h.length = 1; h.number = 1; h.dsum = crc32c(zero, 16);
        example(h, "0201010000000000EA9A7042A08C06E1");
        h.hops = 1;
        example(h, "0201010000001000EA9A70429EBC0A21");
        Header g; g.stream = 1; g.flags = F_RESEND; g.length = 1; g.number = 0x2ABCD; g.ack = 0x15555;
        g.hops = 3; g.stop[BG] = true; g.dsum = crc32c(zero, 16);
        example(g, "1101CDAB56553502EA9A7042D2BFBA0A");
        Header z; z.stream = 1; z.number = 5; z.ack = 4; z.stop[FG] = true;
        example(z, "01000500100000010000000057094D4E");
    }
    // HL, "The data is masked on the wire": worked examples, byte for byte (the expected bytes are from a second
    // implementation, in Python), the mask taken off again, and the register's two states that stand still.
    {
        auto masked = [&](const Header& h, const uint8_t* data, unsigned n, const char* want) {
            uint8_t hb[HEADER_BYTES], d[64]; pack(h, hb); memcpy(d, data, n);
            mask_data(hb, d, n);
            char got[200]; for (unsigned i = 0; i < n; i++) snprintf(got + 2 * i, 3, "%02X", d[i]);
            if (strcmp(got, want) != 0) { printf("FAIL mask: got %s, want %s\n", got, want); failures++; }
            mask_data(hb, d, n);
            CHECK(memcmp(d, data, n) == 0);
        };
        Header h; h.stream = 2; h.length = 1; h.number = 1; h.dsum = crc32c(zero, 16);          // HL's first example header
        masked(h, zero, 16, "2A54C074C01C582FA65D273739C9471F");
        uint8_t count[32]; for (unsigned i = 0; i < 16; i++) { count[i] = uint8_t(i); count[16 + i] = 0xFF; }
        Header g; g.stream = 1; g.length = 1; g.number = 7; g.ack = 3; g.dsum = crc32c(count, 16);
        uint8_t gb[HEADER_BYTES]; pack(g, gb);
        CHECK(header_crc(gb) == 0x362A949Eu && mask_start(header_crc(gb)) == 0x362B949Eu && mask_step(0x362B949Eu) == 0x3EE668A4u);
        masked(g, count, 16, "A4D06E3C10A42F89F574219EDE43E4C0");
        g.ack = 4; g.hops = 1;                                 // the same data forwarded, under another header: another mask
        masked(g, count, 16, "79F1F28277D9B4AF918F7C003996A151");
        Header w; w.stream = 2; w.length = 2; w.number = 0x2ABCD; w.ack = 0x15555; w.stop[BG] = true; w.dsum = crc32c(count, 32);
        masked(w, count, 32, "1796CC00CFBA59A933533F30B5C2282DEBF00BACE428925E1502273A5009CE6B");
        CHECK(mask_step(0) == 0 && mask_step(MASK_STILL) == MASK_STILL && uint8_t(MASK_STILL) == 0xAF);
        CHECK(mask_start(0) != 0 && mask_start(MASK_STILL) != MASK_STILL && mask_start(MASK_STILL ^ MASK_SET) != MASK_STILL);
    }
    // HL, "Starting a link again": the restart packet's data, the first 32 primes sixteen times over, and its checksum.
    {
        uint8_t data[RESTART_WORDS * WORD_BYTES];
        for (unsigned i = 0; i < sizeof data; i++) data[i] = restart_byte(i);
        CHECK(sizeof data == 512 && data[0] == 2 && data[31] == 131 && data[32] == 2 && data[511] == 131);
        CHECK(crc32c(data, sizeof data) == RESTART_SUM);
        Header h; h.stream = 1; h.flags = F_RESTART | F_LOST; h.length = 32; h.number = 1; h.dsum = RESTART_SUM;
        uint8_t b[HEADER_BYTES]; pack(h, b);
        CHECK(b[0] == 0xA1 && b[1] == 32 && crc32c(b, HEADER_BYTES) == CRC_GOOD_HEADER);
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
