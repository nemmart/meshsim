// proof_bench.h: the whole chip from a packet file (v1.37; CHANGES-v1.37.md). The simulator is given the link's bytes,
// N and s; the master tile's program (fw/cc/master.c) reads the frames out of Ingress, drives the Updater, which builds
// the memory images, and hands the tracer tile segment s; the Tracer traces it on those images, and the compute tiles
// replay it. It is segment_bench.h's run_chip with a `Proof` (segment_bench.h): the same machine but for the master
// tile, the same tracer tile's and compute tiles' programs, the same checks of the tiles, and the proof's own:
//
//   - the master's results (fw/cc/master_layout.h): its status, the frames it took, the runs, every request answered;
//   - PAGE_COUNT 0 at every run's done, in the master's results and in the Updater as its done was posted;
//   - no error bit: the tile's, Ingress's, the Updater's, the error lines; no read of a page nothing wrote;
//   - the link read to its end: every packet, every word; both of the Updater's queues empty;
//   - each image as its run ended, against gold's reference for this s (`frames-images`: the program image, the jump
//     table, the cycle-0 image, the start image, the Tracer's copy, the final image), by SHA-256 of gold's own image
//     form; and the program, cycle-0 and start images again at the end, unchanged by all that was built on them and by
//     the Tracer;
//   - the page map as the master left it in its scratchpad: the pages it marks are page 0 and exactly the pages the
//     files hold;
//   - the hand-over, read from the tracer tile's scratchpad: record s as frame 7 has it with word 66's roots and
//     depths, record s + 1 as the expected end state, advice log s, the header's hash constants and advice count;
//   - the Tracer's last record against record s + 1 (but for the last segment, which has none).
//
// Test-side code (the configuration regime): the STL throughout.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "frames.h"
#include "segment_bench.h"

namespace proof {

struct Case {
    frames::File file;                           // gold's frame file (the expectations are read from it)
    std::vector<uint8_t> packets;                // the link's bytes: the frame file in the link's packets
    unsigned asics = 64, segment = 0;            // N and s, the system's configuration
    std::map<std::string, std::string> gold;     // gold's reference for this s: an image's name to its SHA-256 (hex)
    int cycles_per_byte = 1;                     // the link's rate (0: as fast as taken)
    int updater_lookup = 0;                      // not 0: the Updater's cycles for a look-up, in place of its 6 (a slow memory side)
    // A run that must end early, and how; the proof's checks are then that it did so, and nothing else.
    //   trap  in the master's error path, by an error bit's NMI: with these ERRORS of Ingress and of the Updater;
    //   cut   with the link gone quiet inside a packet (the file ends there): no error bit, the master waiting, the
    //         run ended by its cycle limit.
    struct Expect { bool trap = false, cut = false; int ingress_errors = 0, updater_errors = 0; } expect;
};

// One of the Updater's runs, as its done was posted.
struct RunAccount {
    std::string name;
    int64_t done_at = 0;                         // the cycle
    uint32_t page_count = 0;                     // PAGE_COUNT at done
    uint32_t cycles = 0, stall_data = 0, stall_pages = 0, stall_memory = 0, pages_read = 0, pages_written = 0;   // the run's own (words 48-53)
};
struct Numbers {
    std::vector<RunAccount> runs;
    std::vector<uint32_t> master;                // master_results
    uint64_t link_packets = 0, link_words = 0, link_bytes = 0;
    uint64_t pages_marked = 0;                   // in the page map, with page 0
    uint64_t master_awake = 0;                   // cycles the master's core was not asleep
    uint64_t master_instructions = 0;            // and the instructions it retired
};

// gold's reference file (`gold frames-images FRAMES S`, tests/updater/NAME-sS.images.txt): each line a name and a
// SHA-256. Empty if the file can't be read.
std::map<std::string, std::string> gold_digests(const std::string& path);

// The frame file in the link's packets, by the tests' writer (packet_writer.h); gold's packetizer makes the same bytes.
std::vector<uint8_t> packets_of(const frames::File& file);

// Segment `seg` (its name, windows, rows and cycle limit; its recorded windows if it has them) on the chip, from the
// case's packets. The run's failures are run_chip's and the proof's.
segment::Run run(const Case& c, const segment::Segment& seg, const segment::Options& o = segment::Options(), Numbers* numbers = nullptr);

}  // namespace proof
