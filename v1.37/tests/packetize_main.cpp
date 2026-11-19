// packetize: a frame file wrapped in the link's packets by the tests' provisional writer (tests/packet_writer.h), and a
// packet file read back by Ingress's reader (src/units/host_link.h).
//
//   packetize FRAMES PACKETS [--idle N]     write PACKETS from FRAMES
//   packetize --read PACKETS [FRAMES]       check every packet; with FRAMES, the data must be that file, byte for byte
//
// PROVISIONAL: the packetizer is WORKER2's (../updater_wip/pipeline/gold), which replaces this when it is written.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "packet_writer.h"
#include "units/host_link.h"

namespace {
bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--read") == 0) {
        std::vector<uint8_t> bytes, frames, data;
        if (!read_file(argv[2], bytes)) { std::printf("FAIL packetize: cannot read %s\n", argv[2]); return 1; }
        units::hostlink::Reader r;
        r.open(bytes.data(), bytes.size());
        units::hostlink::Packet p;
        units::hostlink::Check check;
        size_t taken, packets = 0, words = 0;
        for (;;) {
            const auto next = r.next(p, check, taken);
            if (next == units::hostlink::Reader::Next::END) break;
            if (next == units::hostlink::Reader::Next::CUT) { std::printf("FAIL packetize: %s ends inside a packet\n", argv[2]); return 1; }
            if (check != units::hostlink::Check::OK) {
                std::printf("FAIL packetize: %s, packet %zu: %s\n", argv[2], packets + 1,
                            check == units::hostlink::Check::CRC ? "a check failed (crc_error)" : check == units::hostlink::Check::FORMAT ? "its fields (format_error)" : "its number (sequence_error)");
                return 1;
            }
            packets++; words += p.length;
            for (unsigned w = 0; w < p.length; w++) for (uint64_t half : {p.lo[w], p.hi[w]}) for (unsigned b = 0; b < 8; b++) data.push_back(uint8_t(half >> (8 * b)));
        }
        if (argc >= 4) {
            if (!read_file(argv[3], frames)) { std::printf("FAIL packetize: cannot read %s\n", argv[3]); return 1; }
            if (frames != data) { std::printf("FAIL packetize: the packets' data is not %s\n", argv[3]); return 1; }
        }
        std::printf("PASS packetize: %s: %zu packets, %zu words of data, %zu bytes (%.2f %% over the data)\n", argv[2], packets, words, bytes.size(),
                    words ? 100.0 * (double(bytes.size()) / double(16 * words) - 1.0) : 0.0);
        return 0;
    }
    if (argc < 3) { std::fprintf(stderr, "usage: packetize FRAMES PACKETS [--idle N] | packetize --read PACKETS [FRAMES]\n"); return 2; }
    packet_writer::Options o;
    for (int i = 3; i + 1 < argc; i += 2) if (std::strcmp(argv[i], "--idle") == 0) o.idle_zeros = unsigned(std::atoi(argv[i + 1]));
    std::vector<uint8_t> frames, out;
    std::string err;
    if (!read_file(argv[1], frames)) { std::printf("FAIL packetize: cannot read %s\n", argv[1]); return 1; }
    if (!packet_writer::packetize(frames, out, err, o)) { std::printf("FAIL packetize: %s: %s\n", argv[1], err.c_str()); return 1; }
    FILE* f = std::fopen(argv[2], "wb");
    if (!f || std::fwrite(out.data(), 1, out.size(), f) != out.size() || std::fclose(f) != 0) { std::printf("FAIL packetize: cannot write %s\n", argv[2]); return 1; }
    std::printf("packetize: %s: %zu bytes of frames in %zu bytes of packets\n", argv[2], frames.size(), out.size());
    return 0;
}
