// updater_bench.h: the Updater black box on a bench, with no firmware (v1.37): a file store, the Updater, and a small
// controller in C++ that does what the master tile's program does (../v1.4_wip/planning/UPDATER-INTERFACE.md, "The
// runs"): it writes block 2, queues free pages on the free page queue's credits, offers a frame's data on the Updater's
// word port (word_port.h; the forward count's part, WORKER7), and starts each run. It calls the Updater as the slot
// does (peripheral.h, periph_slot.h): one accept a cycle at most, a store's effect at the end of its cycle, the port's
// outputs of t taken before step(t), the word that moved in t given after it.
//
// `Proof` plays a whole frame file for segment s of N: the program image; a base table if the file has one; the run's
// table; the cycle-0 image; the memory segments as three runs. After each run it keeps the file's image in gold's form
// (the pages holding a non-zero byte, ascending, each a u64 byte offset then its 4,096 bytes), which gold's
// `frames-images --out` writes for the same file and s.
//
// Test-side code (the configuration regime): the STL throughout.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "frames.h"
#include "units/file_store.h"
#include "units/updater.h"

namespace updater_bench {

using units::Updater;
using units::tracer::FileStore;

// A file's image in gold's form.
std::vector<uint8_t> image(const FileStore& store, int32_t root, int depth);

struct Bench {
    FileStore store;
    Updater u;
    int64_t t = 1;
    uint32_t next_page = 1;                // the page map's stand-in: pages in order from page 1 (page 0 is not given out)
    uint64_t valid_seed = 0;               // not 0: the port's valid comes and goes at random (a xorshift from this seed)
    unsigned page_allowance = 0;           // the free page queue's entries
    // the stream being sent: a frame's data, 128-bit words
    const frames::File* file = nullptr;
    const frames::Frame* frame = nullptr;
    size_t sent = 0;
    // each run's account
    struct Account { uint32_t pages_asked, pages_left, cycles, stall_data, stall_pages, stall_memory, pages_read, pages_written; };
    std::vector<Account> accounts;
    std::vector<int32_t> pages_given;      // every page handed out, in order (roots and queue entries)
    int64_t cycle_limit = int64_t(1) << 32;

    Bench() { u.store = &store; }
    int32_t new_page() { const int32_t a = int32_t(next_page++ * 32u); pages_given.push_back(a); return a; }
    void stream(const frames::File& f, const frames::Frame& fr) { file = &f; frame = &fr; sent = 0; }
    bool stream_left() const { return frame && sent < frame->length; }

    struct Run {
        uint32_t config = 0, words = 0, pages = 0;
        int32_t image = 0, tracer = 0, source = 0;
    };
    static uint32_t config(bool lru, bool cont, bool tracer_copy, int depth, unsigned root) {
        return (lru ? 1u : 0) | (cont ? 2u : 0) | (tracer_copy ? 4u : 0) | uint32_t(depth) << 3 | root << 5;
    }
    // One run, to its done: true; false if the Updater stopped on an error or the limit passed.
    bool run(const Run& r);
};

// What a frame file makes, for segment s.
struct Proof {
    frames::ProgramHeader program;
    frames::ProofHeader proof;
    frames::TableHeader base, table;
    bool has_base = false;
    int image_depth = 0, table_depth = 0;
    int32_t program_root = 0, base_root = 0, table_root = 0, cycle0_root = 0, start_root = 0, tracer_root = 0, final_root = 0;
    std::vector<uint8_t> program_image, base_image, table_image, cycle0_image, start_image, tracer_image, final_image;
    std::vector<std::string> run_names;    // one for each of the bench's accounts
};
bool play(Bench& b, const frames::File& file, unsigned asics, unsigned s, Proof& out, std::string& err);

}  // namespace updater_bench
