// updater.h: the Updater, the master tile's peripheral 2 in v1.37 (../v1.4_wip/planning/UPDATER-INTERFACE.md, "UI"
// below; HOST-FRAMES.md for the update words; DESIGN-NOTES-2026-11-15.md section 4 for what it does to a page). It is a
// peripheral of the slot (units/peripheral.h's method set); the control block (17.1-17.2) is the slot's.
//
// A BLACK BOX. Behind UI's registers, its data input, its free page queue and its credit, its events and its errors, this is software on
// the file store (units/file_store.h): it is not the Updater's hardware design, and it reaches no cache and no AXI port.
// It is written to be right and to be read. What a master tile's program sees of it is UI's, so that the program stands
// when the real controller takes this one's place.
//
// A RUN (UI): START takes block 2's words; STATUS.busy is 1 while it lasts; EVENTS.done is posted at its end. Every run
// applies WORD_COUNT update words to the file in IMAGE_ROOT:
//   1. THE ROOT (UI, "For v1.37"). With RUN_CONFIG.root 1 it first writes an empty root at IMAGE_ROOT; with 2, a copy of
//      SOURCE_ROOT's root with the owned bits cleared, and with tracer_copy the same copy at TRACER_ROOT.
//   2. THE DECODER takes words from its data input as it needs bits: the update words as they are (two an entry, the low one
//      first), or LRU-coded: bit-packed, least significant bit first, a hit 1 and a 5-bit index, a miss 0, then 0 and 32
//      bits or 1 and 64, against 32 entries, move to front. RUN_CONFIG.continue 0 clears it at START (the table, the
//      bits left over, the run of values it was in); 1 keeps all three, the run carrying on in the last one's frame.
//   3. AN UPDATE WORD counts WORD_COUNT down. A header word (the start, a 64-bit-word index in the file, in [31:0]; the
//      count in [63:32]) opens a run of values; each value goes to the next word.
//   4. A PAGE is brought into the one page memory when a value first needs it: the walk from the root, top down, one
//      page a level. An entry that is the file's own is followed. A shared one is copied to the next free page (an
//      index page with its entries' owned bits cleared) and a hole becomes a page of holes, or of zeros; the entry above
//      then names the new page, owned. Every new page is the next entry of the free page queue, and counts PAGE_COUNT
//      down. Once a level is not the file's own, nothing below it is.
//   5. WITH tracer_copy the file in TRACER_ROOT takes its own page at each level first, unchanged, wherever it does not
//      own one, then the image takes its own: the two trees stay in step, and the Tracer's copy ends up owning every
//      page the run writes, with the contents it had before.
//   6. THE PAGE IS WRITTEN when a value needs another page, or the run ends: the page, then the entry that names it.
// A run's end touches nothing else: what is left in the data input, the free page queue and the decoder stays for the
// next run.
//
// THE DATA INPUT (UI, "The data input"; word_port.h): the sink of the word port from Ingress, which the slot wires
// (periph_slot.h, THE WORD PORT). Behind it the black box keeps INPUT_WORDS = 4 words (WORKER7's C5: the decoder takes
// at most 66 bits a cycle, under a word, so 2 would keep it fed with ready registered; 4 is WORKER6's controller's
// input FIFO, so the two hold the same backlog). `ready` in cycle t: fewer than 4 held at the start of t, whether a run
// is on or not; a word that moves in t is held from the end of t, after this unit's step of t. Only the reset empties
// it; a START does not. There are no credits and no event: EVENTS [2], which was segment_credit, reads 0.
//
// ERRORS (UI). queue_overrun: a push into a full free page queue, dropped. beyond: a word past what the depth reaches.
// out_of_pages: a page needed with PAGE_COUNT at 0. lpddr_error: here, a root or a free page that is not a page's
// address in LPDDR (the black box has no other LPDDR error). not_owned is never set: the black box makes each level the
// file's own before it writes below it. beyond, out_of_pages and lpddr_error stop the run: STATUS.busy stays 1 and
// done is not posted. A START while busy, a store to block 2 while busy, a write of another size than 128 bits and a
// write to a part the region does not map are UB (17.2, 17.3), and stop the simulator.
//
// TIMING: PLAIN, AND NOT THE HARDWARE'S. One update word a cycle. Everything that reaches the memory side is done in
// its word's cycle and then waited out, a fixed number of cycles (`Timing`) before the next word, from the memory model's two figures
// (mem_system.h: a hit 6, a miss 80) and a beat of 16 bytes a cycle: a look-up 6 for each level of a walk; a page read,
// or written and confirmed, 80 + 256; half a page 80 + 128; an entry's update 80. A page is read as UI's table says, by
// halves: a half that the run of values covers whole, or that is a hole, is not read; a page copied for the Tracer is
// read whole. The statistics count these: STAT_PAGES_READ and STAT_PAGES_WRITTEN pages and half pages, STAT_STALL_DATA
// and STAT_STALL_PAGES the cycles spent waiting on an empty data input and free page queue, STAT_STALL_MEMORY the
// cycles of the costs above. No cycle count of a run on this black box is a figure of the design.
//
// THE PAGE MEMORY'S CONTENTS are always the page's true contents here (the file store's, or zeros for a hole): what is
// "read" decides only the cycles and the counts, never the result.
//
// SLOT TIMING (periph_slot.h, THE CONTRACT): a WRITE the director accepted at t is in its queue at the end of t + 2 (the
// slot calls accept at level 0 of t + 3, before step(t + 3)); a store's effect at the end of its cycle; step(t) is cycle
// t's work.
#pragma once

#include <cstdint>
#include <cstring>

#include "sim.h"
#include "base/fatal.h"
#include "units/event_counts.h"
#include "units/file_store.h"
#include "units/peripheral.h"
#include "units/word_port.h"

namespace units {

struct Updater : PeripheralBase<Updater> {
    // ---- the window's words (UI, blocks 2 and 3)
    enum : int { W_RUN_CONFIG = 32, W_WORD_COUNT = 33, W_PAGE_COUNT = 34, W_IMAGE_ROOT = 35, W_TRACER_ROOT = 36, W_SOURCE_ROOT = 37,
                 W_STAT_CYCLES = 48, W_STAT_STALL_DATA = 49, W_STAT_STALL_PAGES = 50, W_STAT_STALL_MEMORY = 51,
                 W_STAT_PAGES_READ = 52, W_STAT_PAGES_WRITTEN = 53 };
    // RUN_CONFIG's fields
    enum : uint32_t { RC_LRU = 1u << 0, RC_CONTINUE = 1u << 1, RC_TRACER_COPY = 1u << 2, RC_DEPTH_LO = 3, RC_ROOT_LO = 5, RC_BITS = 0x7F };
    enum : unsigned { ROOT_EXISTS = 0, ROOT_EMPTY = 1, ROOT_COPY = 2 };
    enum : unsigned { EV_DONE = 0, EV_DMA_DONE = 1, EV_UNUSED = 2, EV_FREE_PAGE_CREDIT = 3, EV_BITS = 4 };   // [2]: segment_credit until WORKER7
    enum : int { ERR_QUEUE_OVERRUN = 1 << 0, ERR_BEYOND = 1 << 1, ERR_LPDDR = 1 << 2, ERR_OUT_OF_PAGES = 1 << 3, ERR_NOT_OWNED = 1 << 4 };
    // the region's parts (offset bits [22:20]): part 2 alone (part 1 was the segment queue's, until WORKER7)
    enum : unsigned { PART_FREE_PAGES = 2 };
    // the free page queue's entries, and its credit: half the queue (UI, "The region")
    static constexpr unsigned FREE_PAGE_QUEUE = 256, FREE_PAGE_HALF = 128;
    static constexpr unsigned QUEUE_MAX = FREE_PAGE_QUEUE;
    static constexpr unsigned INPUT_WORDS = 4;            // the words behind the data input (header, THE DATA INPUT)
    static constexpr unsigned LRU_ENTRIES = 32;
    static constexpr int PAGE_WORDS = tracer::FileStore::PAGE_WORDS;
    static constexpr int DIGEST_N = 3;
    static constexpr int TYPE = 5;

    // ---- configuration (set before the run)
    tracer::FileStore* store = nullptr;        // LPDDR as files: the chip's one store, the Tracer's memory system's too
    int64_t lpddr_units = int64_t(1) << 25;    // LPDDR's end in 128 B units: 2^20 pages of 4 KB, the page map's reach
    struct Timing { int lookup = 6, page = 80 + 256, half = 80 + 128, entry = 80; } timing;
    // A harness's observer of a run's end (null: none), called in the cycle done is posted: the images are whole then.
    void (*on_done)(void* ctx, Updater& u, int64_t t) = nullptr;
    void* on_done_ctx = nullptr;

    // ---- a queue of 128-bit entries with the replayer's credits (17.8.8)
    struct Queue {
        unsigned size = 0, half = 0;           // its entries; a credit (fixed at construction)
        uint64_t lo[QUEUE_MAX] = {}, hi[QUEUE_MAX] = {};
        unsigned head = 0, n = 0;
        unsigned left = 0;                     // entries that have left since the last credit was returned
        uint64_t pushed = 0, popped = 0, dropped = 0;
        void clear() { head = n = left = 0; }
    };

    // ---- state
    EventCounts ev;
    int errors_ = 0;
    bool busy_ = false, stopped_ = false;      // stopped: an error ended the work; busy stays 1
    uint32_t run_config_ = 0, word_count_ = 0, page_count_ = 0, image_root_ = 0, tracer_root_ = 0, source_root_ = 0;
    uint32_t stat_[6] = {};                    // words 48-53
    Queue free_pages_{FREE_PAGE_QUEUE, FREE_PAGE_HALF};
    uint64_t in_lo_[INPUT_WORDS] = {}, in_hi_[INPUT_WORDS] = {};   // the data input's words, from in_head_ on
    unsigned in_head_ = 0, in_n_ = 0;
    // the run as START took it
    bool lru_ = false, tracer_copy_ = false;
    int depth_ = 0;
    int32_t image_ = 0, tracer_ = 0;
    bool root_pending_ = false;
    unsigned root_mode_ = 0;
    // the decoder: its bits (bit k of the stream at bit k of bits_), the LRU table, the run of values it is in
    uint64_t bits_[4] = {};
    unsigned nbits_ = 0;
    uint64_t lru_table_[LRU_ENTRIES] = {};
    uint32_t run_addr_ = 0, run_left_ = 0;
    bool have_value_ = false;
    uint64_t value_ = 0;
    // the page memory
    bool held_ = false, held_owned_ = false, held_hole_ = false;
    int64_t held_page_ = 0;                    // the page's number in the file
    int32_t held_at_ = 0;                      // where it is written
    int32_t held_entry_page_ = -1;             // the index page whose entry names it (not owned: set at the write)
    int held_entry_ = 0;
    bool held_have_[2] = {}, held_dirty_[2] = {};
    uint64_t held_mem_[PAGE_WORDS] = {};
    int64_t wait_ = 0;                         // cycles of a cost still to pass
    // ---- statistics of the model (not state)
    uint64_t runs = 0, words_applied = 0, pages_taken = 0, unwritten_reads = 0, words_in = 0;

    Updater();

    // ---- identity
    int type() const { return TYPE; }
    int params() const { return TYPE; }                    // type 5, version 0, no constants
    // ---- inputs
    void accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi);
    void run(int64_t t);
    int write_word(int64_t t, int w, uint32_t v);
    uint32_t consume_events(uint32_t bits) { return ev.consume(bits); }
    // ---- level reads
    bool running(int64_t) const { return busy_; }
    int errors(int64_t) const { return errors_; }
    EventCounts& events() { return ev; }
    bool word(int64_t t, int w, uint32_t& v);
    bool writable(int w) const { return w >= W_RUN_CONFIG && w <= W_SOURCE_ROOT; }
    // ---- work
    void step(int64_t t);
    bool idle(int64_t) const { return !busy_ || stopped_; }
    uint64_t violation_count() const { return unwritten_reads; }
    void digest_words(int64_t* out) const;
    void reset(int64_t t);

    // ---- the data input: the word port's sink (header, THE DATA INPUT)
    wordport::SinkOut port_sink() const { wordport::SinkOut o; o.ready = in_n_ < INPUT_WORDS; return o; }
    void port_take(int64_t t, const wordport::SourceOut& w);

    // ---- for a harness
    unsigned input_words() const { return in_n_; }
    unsigned free_page_entries() const { return free_pages_.n; }

private:
    using Store = tracer::FileStore;
    enum class Bring { OK, STALL, STOP };

    void push(int64_t t, Queue& q, unsigned credit, uint64_t lo, uint64_t hi);
    void pop(int64_t t, Queue& q, unsigned credit, uint64_t& lo, uint64_t& hi);
    void stop(int error) { errors_ |= error; stopped_ = true; }
    bool lpddr_page(uint32_t a) const { return Store::page_address(int64_t(a), lpddr_units); }

    // the decoder
    bool fill(int64_t t, unsigned need);
    uint64_t take_bits(unsigned n);
    bool decode(int64_t t, uint64_t& v);

    // LPDDR, with its costs and counts
    void read_page(int32_t a, uint64_t* out);
    void write_page(int32_t a, const uint64_t* in);
    void set_entry(int32_t index_page, int i, uint32_t e);
    bool take_page(int64_t t, int32_t& a);

    void make_root(int64_t t);
    unsigned pages_needed(int32_t root, int64_t page) const;
    Bring bring(int64_t t, int64_t page);
    void patch();
    void write_held();
    void finish(int64_t t);
};

}  // namespace units
