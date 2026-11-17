// proof_run: the whole chip from a packet file (v1.37; tests/proof_bench.h), for one frame file, N and s.
//
//   proof_run FRAMES --asics N --segment S [--windows W] [--rows R] [--gold IMAGES.txt] [--packets FILE]
//             [--recorded RUN] [--fast-link] [--link-rate B] [--limit CYCLES] [--quiet] [--cache]
//
// FRAMES is gold's frame file (`gold frames`), cut for N ASICs into segments of W windows of R rows (--windows, 64, and
// --rows, 65,536: gold's --segment-rows is their product; no frame carries them). The link's bytes are FRAMES in the
// link's packets: --packets names gold's packet file (`gold packets`), which must hold the same frames; without it the
// tests' writer makes them (the same bytes). --gold is gold's reference for this s (`gold frames-images FRAMES S`,
// its output): each image's SHA-256. --recorded RUN (evm-zone-23, cyclone-22) adds the recorded windows' checks, every
// row and delivery, from $MESHSIM_INPUTS: windows 64 S to 64 S + 63, so W must be 64 and R 65,536. --fast-link gives
// the link's bytes as fast as the master takes them, in place of a byte a cycle; --link-rate B, B bytes a cycle (1, 2,
// 4, 8).
// --cache runs the Tracer on the cache's controller, the AXI arbiter and the LPDDR model (MachConfig::cache_ctl;
// updater_wip, WORKER5) in place of v1.37's memory model, and prints the cache's figures.
//
// Prints the run's lines (segment_bench.h's and proof_bench.h's), then its figures: each of the Updater's runs, the
// link, the page map, the master's core; the forward count's words, the master's wakes, Ingress's DMA transfers and the
// count's stores, in all, to the hand-over, and for each frame; the master's share of the link's time to the hand-over
// (WORKER7). Exit 1 on any failed check.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "base/fatal.h"
#include "proof_bench.h"
#include "segment_real.h"
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
int usage() {
    std::fprintf(stderr, "usage: proof_run FRAMES --asics N --segment S [--windows W] [--rows R] [--gold IMAGES.txt] [--packets FILE] "
                         "[--recorded RUN] [--fast-link] [--link-rate B] [--limit CYCLES] [--quiet] [--cache]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path, gold, packets, recorded;
    unsigned windows = 64;
    uint32_t rows = 65536;
    uint64_t limit = 0;
    bool cache = false;
    bool quiet = false;
    proof::Case c;
    c.asics = 128;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--asics" && more) c.asics = unsigned(std::atoi(argv[++i]));
        else if (a == "--segment" && more) c.segment = unsigned(std::atoi(argv[++i]));
        else if (a == "--windows" && more) windows = unsigned(std::atoi(argv[++i]));
        else if (a == "--rows" && more) rows = uint32_t(std::atol(argv[++i]));
        else if (a == "--gold" && more) gold = argv[++i];
        else if (a == "--packets" && more) packets = argv[++i];
        else if (a == "--recorded" && more) recorded = argv[++i];
        else if (a == "--limit" && more) limit = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--fast-link") c.cycles_per_byte = 0;
        else if (a == "--link-rate" && more) c.link_rate = std::atoi(argv[++i]);
        else if (a == "--cache") cache = true;
        else if (a == "--quiet") quiet = true;
        else if (path.empty() && a[0] != '-') path = a;
        else return usage();
    }
    if (path.empty() || windows == 0 || windows > 64 || rows == 0 || c.link_rate < 1) return usage();
    std::string err;
    if (!c.file.read(path, err)) { std::printf("FAIL proof_run: %s\n", err.c_str()); return 1; }
    if (packets.empty()) c.packets = proof::packets_of(c.file);
    else {
        // gold's packet file: its packets' data, in order, must be the frame file
        if (!read_file(packets, c.packets)) { std::printf("FAIL proof_run: cannot read %s\n", packets.c_str()); return 1; }
        units::hostlink::Reader reader;
        reader.open(c.packets.data(), c.packets.size());
        std::vector<uint8_t> data;
        for (;;) {
            units::hostlink::Packet p;
            units::hostlink::Check check;
            size_t taken = 0;
            const auto next = reader.next(p, check, taken);
            if (next != units::hostlink::Reader::Next::PACKET) break;
            if (check != units::hostlink::Check::OK) { std::printf("FAIL proof_run: %s: a packet fails its checks\n", packets.c_str()); return 1; }
            for (unsigned w = 0; w < p.length; w++)
                for (uint64_t half : {p.lo[w], p.hi[w]}) for (unsigned b = 0; b < 8; b++) data.push_back(uint8_t(half >> (8 * b)));
        }
        if (data != c.file.bytes) { std::printf("FAIL proof_run: %s does not hold the frames of %s\n", packets.c_str(), path.c_str()); return 1; }
    }
    if (!gold.empty()) {
        c.gold = proof::gold_digests(gold);
        if (c.gold.empty()) { std::printf("FAIL proof_run: cannot read gold's reference %s\n", gold.c_str()); return 1; }
    }

    segment::Segment seg;
    if (!recorded.empty()) {
        const std::string inputs = segment::inputs_directory();
        if (inputs.empty()) { std::printf("FAIL proof_run: --recorded needs the inputs' directory (MESHSIM_INPUTS)\n"); return 1; }
        if (windows != 64 || rows != 65536) { std::printf("FAIL proof_run: the recorded segments are 64 windows of 65,536 rows\n"); return 1; }
        if (!segment::load_real(inputs, recorded, 64 * c.segment, 64, seg, err)) { std::printf("FAIL proof_run: %s\n", err.c_str()); return 1; }
    } else {
        seg.windows = windows; seg.rows = rows;
    }
    const char* file_name = std::strrchr(path.c_str(), '/');
    seg.name = std::string(file_name ? file_name + 1 : path.c_str()) + ", N " + std::to_string(c.asics) + ", s " + std::to_string(c.segment);
    // the limit: the link at a byte a cycle, the Updater's work, the segment's rows as segment_real.h bounds them
    seg.cycle_limit = limit ? limit : uint64_t(c.packets.size()) * 3 + uint64_t(windows) * rows * 12 + 8000000;

    segment::Options o;
    o.quiet = quiet;
    o.cache_ctl = cache;
    proof::Numbers n;
    const segment::Run run = proof::run(c, seg, o, &n);

    std::printf("\n%-10s %12s %10s %10s %10s %10s %8s %8s %10s\n", "run", "done at", "cycles", "on data", "on pages", "on memory", "read", "written", "PAGE_COUNT");
    for (const proof::RunAccount& a : n.runs)
        std::printf("%-10s %12lld %10u %10u %10u %10u %8u %8u %10u\n", a.name.c_str(), (long long)a.done_at, a.cycles, a.stall_data, a.stall_pages,
                    a.stall_memory, a.pages_read, a.pages_written, a.page_count);
    std::printf("the link: %llu packets, %llu words, %llu bytes; the page map: %llu pages marked; the master's core: %llu cycles awake of %llu (%.2f%%), %llu instructions\n",
                (unsigned long long)n.link_packets, (unsigned long long)n.link_words, (unsigned long long)n.link_bytes, (unsigned long long)n.pages_marked,
                (unsigned long long)n.master_awake, (unsigned long long)run.cycles, run.cycles ? 100.0 * double(n.master_awake) / double(run.cycles) : 0.0,
                (unsigned long long)n.master_instructions);
    if (n.master.size() > 27)   // fw/cc/master_layout.h: MR_HANDED, MR_RELEASED, MR_END, the master's mcycle at each
        std::printf("the master: the tracer tile's load at its cycle %u, the windows landed and the replay released at %u, its exit at %u\n",
                    n.master[25], n.master[26], n.master[27]);
    if (n.master.size() > 122) {   // fw/cc/master_layout.h: MR_WAKES, MR_LINK_READS, MR_FRAME_WAKES, MR_FRAME_READS, MR_COUNT_STORES, MR_HANDED_*
        const std::vector<uint32_t>& R = n.master;
        std::printf("the forward count: the link at %d bytes a cycle; the master's wakes: %u, %u to the hand-over; Ingress's DMA: %u transfers, %u to the hand-over, "
                    "%u words; %u count stores; %llu words forwarded, %llu dropped; the master awake %llu cycles to the hand-over at %u: %.2f%%; the link: its "
                    "last byte at %lld, %llu cycles waiting for room; awake %u cycles at the first frame\n",
                    c.cycles_per_byte == 0 ? 0 : c.link_rate, R[30], R[121], R[31], R[122], R[29], R[120], (unsigned long long)n.link_forwarded,
                    (unsigned long long)n.link_discarded, (unsigned long long)n.master_awake_handed, R[25],
                    R[25] ? 100.0 * double(n.master_awake_handed) / double(R[25]) : 0.0, (long long)n.link_last_at, (unsigned long long)n.link_room_cycles, R.size() > 123 ? R[123] : 0u);
        std::printf("each frame, its type: wakes/transfers:");
        for (uint32_t k = 0; k < R[2] && k < 16; k++) {
            const bool last = k + 1 == R[2] || k == 15;
            std::printf(" %u: %u/%u", R[56 + k], (last ? R[121] : R[88 + k + 1]) - R[88 + k], (last ? R[122] : R[104 + k + 1]) - R[104 + k]);
        }
        std::printf(" (the last to the hand-over)\n");
    }
    std::printf("%s proof_run %s: %d checks failed\n", run.failures ? "FAIL" : "PASS", seg.name.c_str(), run.failures);
    return run.failures ? 1 : 0;
}
