// test_header: HL's three example headers and its CRC values, byte for byte; the numbers' arithmetic; and, given
// the path of gold's VECTORS.txt (updater_wip/pipeline/gold/test/packets/, on updater-w7 until merged), every
// crc32c line and every single packet in it.
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

static void example(uint32_t number, uint32_t ack, uint8_t hops, const char* want) {
    const uint8_t zero[16] = {};
    Header h; h.stream = 2; h.length = 1; h.number = number; h.ack = ack; h.hops = hops; h.dsum = crc32c(zero, 16);
    uint8_t b[16]; pack(h, b);
    const auto w = hex(want);
    CHECK(w.size() == 16 && memcmp(b, w.data(), 16) == 0);
    CHECK(crc32c(b, 16) == CRC_GOOD_HEADER);
    const Header u = unpack(b);
    CHECK(u.stream == 2 && u.flags == 0 && u.length == 1 && u.number == number && u.ack == ack && u.hops == hops &&
          u.reserved == 0 && u.dsum == h.dsum);
}

static int vectors(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) { printf("cannot open %s\n", path); return 1; }
    char line[4096];
    int crcs = 0, packets = 0;
    bool in = false; Header want; std::vector<uint8_t> bytes; std::string expect; uint32_t hcrc = 0;
    while (fgets(line, sizeof line, f)) {
        char key[64] = "", a[2048] = "", b[64] = "";
        if (char* hash = strchr(line, '#')) *hash = 0;
        const int n = sscanf(line, "%63s %2047s %63s", key, a, b);
        if (n < 1) continue;
        const std::string k = key;
        if (k == "crc32c" && n == 3) {
            const auto v = std::string(a) == "-" ? std::vector<uint8_t>() : hex(a);
            CHECK(crc32c(v.data(), v.size()) == uint32_t(strtoul(b, nullptr, 0))); crcs++;
        } else if (k == "packet") { in = true; want = Header(); bytes.clear(); expect.clear(); }
        else if (!in) continue;
        else if (k == "stream") want.stream = uint8_t(strtoul(a, nullptr, 0));
        else if (k == "flags") want.flags = uint8_t(strtoul(a, nullptr, 0));
        else if (k == "length") want.length = uint8_t(strtoul(a, nullptr, 0));
        else if (k == "number") want.number = uint32_t(strtoul(a, nullptr, 0));
        else if (k == "acknowledge") want.ack = uint32_t(strtoul(a, nullptr, 0));
        else if (k == "hops") want.hops = uint8_t(strtoul(a, nullptr, 0));
        else if (k == "reserved") want.reserved = uint8_t(strtoul(a, nullptr, 0));
        else if (k == "data-checksum") want.dsum = uint32_t(strtoul(a, nullptr, 0));
        else if (k == "header-crc") hcrc = uint32_t(strtoul(a, nullptr, 0));
        else if (k == "bytes") { const auto v = hex(a); bytes.insert(bytes.end(), v.begin(), v.end()); }
        else if (k == "expect") expect = a;
        else if (k == "end") {
            in = false; packets++;
            if (bytes.size() < 16) { CHECK(bytes.size() >= 16); continue; }
            const Header u = unpack(bytes.data());
            // The fields as the bytes hold them. Lengths over 255 and the like do not occur in the file.
            CHECK(u.stream == want.stream && u.flags == want.flags && u.length == want.length && u.number == want.number &&
                  u.ack == want.ack && u.hops == want.hops && u.reserved == want.reserved && u.dsum == want.dsum);
            const bool header_ok = crc32c(bytes.data(), 16) == CRC_GOOD_HEADER;
            CHECK(header_ok == (crc32c(bytes.data(), 12) == hcrc));
            CHECK(header_ok == (expect != "header-crc"));
            if (header_ok) {                                     // the same bytes back from the fields
                uint8_t again[16]; pack(u, again);
                CHECK(memcmp(again, bytes.data(), 16) == 0);
                if (expect == "ok" || expect == "number") {
                    CHECK(bytes.size() == 16 + 16u * u.length);
                    CHECK(crc32c(bytes.data() + 16, bytes.size() - 16) == u.dsum);
                }
                if (expect == "data-checksum") CHECK(crc32c(bytes.data() + 16, bytes.size() - 16) != u.dsum);
            }
        }
    }
    fclose(f);
    printf("vectors: %d crc32c lines, %d packets\n", crcs, packets);
    CHECK(crcs > 0 && packets > 0);
    return 0;
}

int main(int argc, char** argv) {
    // HL, "The two checks": the table's values.
    CHECK(crc32c(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xE3069283u);
    CHECK(crc32c(nullptr, 0) == 0);
    const uint8_t zero[16] = {};
    CHECK(crc32c(zero, 16) == 0x42709AEAu);
    // HL's three example headers.
    example(1, 0, 0, "0201010000000000EA9A7042A08C06E1");
    example(1, 0, 1, "0201010000001000EA9A70429EBC0A21");
    example(0x2ABCD, 0x15555, 3, "0201CDAB56553500EA9A7042C9C42330");
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
