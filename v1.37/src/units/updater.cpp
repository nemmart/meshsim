// updater.cpp: the Updater black box (updater.h).
#include "units/updater.h"

#include "units/net.h"

namespace units {

namespace {
// Entry `level`'s index within its index page, for page number pg of a file of this depth: level 0 is the root's.
int digit(int64_t pg, int level, int depth) { return int(uint64_t(pg) >> (10 * (depth - 1 - level))) & 1023; }
}  // namespace

Updater::Updater() {
    static const uint8_t depth[EV_BITS] = {1, 1, 2};                  // [2] free_page_credit (17.10.1)
    static const bool flags[EV_BITS] = {false, false, true};         // a credit returned past its depth is a fault
    ev.init(EV_BITS, depth, flags);
    reset(0);
}

// The reset, and nothing else, empties the free page queue and the data input, and fills the credit to 2 (UI).
void Updater::reset(int64_t t) {
    errors_ = 0; busy_ = stopped_ = false;
    run_config_ = word_count_ = page_count_ = image_root_ = tracer_root_ = source_root_ = 0;
    std::memset(stat_, 0, sizeof stat_);
    free_pages_.clear(); in_head_ = in_n_ = 0;
    std::memset(bits_, 0, sizeof bits_); nbits_ = 0;
    std::memset(lru_table_, 0, sizeof lru_table_);
    run_addr_ = run_left_ = 0; have_value_ = false;
    held_ = false; wait_ = 0; root_pending_ = false;
    ev.clear_all();
    ev.set(t, EV_FREE_PAGE_CREDIT, 2);
}

// ================================================================================================ the queues
// A push into a full queue is dropped and sets ERRORS.queue_overrun.
void Updater::push(int64_t, Queue& q, unsigned, uint64_t lo, uint64_t hi) {
    if (q.n == q.size) { q.dropped++; errors_ |= ERR_QUEUE_OVERRUN; return; }
    const unsigned at = (q.head + q.n++) % q.size;
    q.lo[at] = lo; q.hi[at] = hi;
    q.pushed++;
}
// A credit is returned each time half the queue's entries, 128, have left it since the last return.
void Updater::pop(int64_t t, Queue& q, unsigned credit, uint64_t& lo, uint64_t& hi) {
    lo = q.lo[q.head]; hi = q.hi[q.head];
    q.head = (q.head + 1) % q.size; q.n--;
    q.popped++;
    if (++q.left == q.half) { q.left = 0; ev.post(t, credit); }
}

void Updater::accept(int64_t t, int src, uint32_t offset, int size, uint64_t lo, uint64_t hi) {
    const unsigned part = offset >> 20 & 7;
    if (size != int(lw::SIZE_128))
        FATAL("the Updater: undefined behavior at %lld: a WRITE of another size than 128 bits into its region (offset 0x%x, from %d)",
              (long long)t, offset, src);
    if (part == PART_FREE_PAGES) push(t, free_pages_, EV_FREE_PAGE_CREDIT, lo, hi);
    else FATAL("the Updater: undefined behavior at %lld: a WRITE to part %u of its region, which maps part 2 alone (offset 0x%x, from %d)",
               (long long)t, part, offset, src);
}

// The word that moved on the data input in t, held from the end of t (updater.h, THE DATA INPUT).
void Updater::port_take(int64_t t, const wordport::SourceOut& w) {
    if (in_n_ >= INPUT_WORDS) FATAL("the Updater: a word on its data input at %lld with %u held: ready was 0", (long long)t, in_n_);
    const unsigned at = (in_head_ + in_n_++) % INPUT_WORDS;
    in_lo_[at] = w.data[0]; in_hi_[at] = w.data[1];
    words_in++;
}

// ================================================================================================ the window
bool Updater::word(int64_t, int w, uint32_t& v) {
    switch (w) {
        case W_RUN_CONFIG: v = run_config_; return true;
        case W_WORD_COUNT: v = word_count_; return true;
        case W_PAGE_COUNT: v = page_count_; return true;
        case W_IMAGE_ROOT: v = image_root_; return true;
        case W_TRACER_ROOT: v = tracer_root_; return true;
        case W_SOURCE_ROOT: v = source_root_; return true;
        default:
            if (w >= W_STAT_CYCLES && w <= W_STAT_PAGES_WRITTEN) { v = stat_[w - W_STAT_CYCLES]; return true; }
            return false;
    }
}

int Updater::write_word(int64_t t, int w, uint32_t v) {
    if (w >= W_STAT_CYCLES && w <= W_STAT_PAGES_WRITTEN) return 0;   // read-only
    if (w < W_RUN_CONFIG || w > W_SOURCE_ROOT) return -1;
    if (busy_) FATAL("the Updater: undefined behavior at %lld: a store to word %d of block 2 during a run (UI: written while idle)", (long long)t, w);
    switch (w) {
        case W_RUN_CONFIG: run_config_ = v & RC_BITS; break;
        case W_WORD_COUNT: word_count_ = v; break;
        case W_PAGE_COUNT: page_count_ = v; break;
        case W_IMAGE_ROOT: image_root_ = v; break;
        case W_TRACER_ROOT: tracer_root_ = v; break;
        default: source_root_ = v; break;
    }
    return 1;
}

// START: the run block 2 describes.
void Updater::run(int64_t t) {
    if (busy_) FATAL("the Updater: undefined behavior at %lld: a START during a run (17.2)", (long long)t);
    if (!store) FATAL("the Updater: no file store behind it (a harness sets Updater::store)");
    busy_ = true; runs++;
    lru_ = run_config_ & RC_LRU;
    tracer_copy_ = run_config_ & RC_TRACER_COPY;
    depth_ = int(run_config_ >> RC_DEPTH_LO & 3);
    root_mode_ = run_config_ >> RC_ROOT_LO & 3;
    if (!(run_config_ & RC_CONTINUE)) {                    // a new frame: the decoder starts clear
        std::memset(bits_, 0, sizeof bits_); nbits_ = 0;
        std::memset(lru_table_, 0, sizeof lru_table_);
        run_addr_ = run_left_ = 0; have_value_ = false;
    }
    held_ = false; wait_ = 0;
    if (!lpddr_page(image_root_) || (tracer_copy_ && !lpddr_page(tracer_root_)) || (root_mode_ == ROOT_COPY && !lpddr_page(source_root_)) ||
        root_mode_ == 3) {                                 // UB (17.10.1; S11), as the controller has it: the simulator's stop
        FATAL("the Updater: undefined behavior: a run's root that is no page of LPDDR, or RUN_CONFIG.root 3 (17.10.1)");
    }
    image_ = int32_t(image_root_); tracer_ = int32_t(tracer_root_);
    root_pending_ = root_mode_ != ROOT_EXISTS;
}

// ================================================================================================ the decoder
// The stream's next words into the bit buffer, until it holds `need` bits; false while the data input is empty.
bool Updater::fill(int64_t, unsigned need) {
    while (nbits_ < need) {
        if (in_n_ == 0) return false;
        const uint64_t lo = in_lo_[in_head_], hi = in_hi_[in_head_];
        in_head_ = (in_head_ + 1) % INPUT_WORDS; in_n_--;
        const unsigned word = nbits_ / 64, shift = nbits_ % 64;   // fewer than `need` <= 66 bits are held: room for 128 more
        bits_[word] |= lo << shift;
        if (shift) { bits_[word + 1] |= lo >> (64 - shift) | hi << shift; bits_[word + 2] |= hi >> (64 - shift); }
        else bits_[word + 1] |= hi;
        nbits_ += 128;
    }
    return true;
}
// The buffer's lowest n bits (n <= 64), taken.
uint64_t Updater::take_bits(unsigned n) {
    const uint64_t v = n == 64 ? bits_[0] : bits_[0] & ((uint64_t(1) << n) - 1);
    for (unsigned k = 0; k < 4; k++) {
        const uint64_t next = k + 1 < 4 ? bits_[k + 1] : 0;
        bits_[k] = n == 64 ? next : bits_[k] >> n | next << (64 - n);
    }
    nbits_ -= n;
    return v;
}
// The next update word; false while its bits are not all in (the stall on data).
bool Updater::decode(int64_t t, uint64_t& v) {
    if (!lru_) {
        if (!fill(t, 64)) return false;
        v = take_bits(64);
        return true;
    }
    if (!fill(t, 1)) return false;
    if (bits_[0] & 1) {                                    // a hit: 1, then the entry's index
        if (!fill(t, 6)) return false;
        const unsigned i = unsigned(take_bits(6) >> 1);
        v = lru_table_[i];
        std::memmove(lru_table_ + 1, lru_table_, i * sizeof(uint64_t));
    } else {                                               // a miss: 0, then 0 and 32 bits, or 1 and 64
        if (!fill(t, 2)) return false;
        const bool wide = bits_[0] >> 1 & 1;
        if (!fill(t, wide ? 66 : 34)) return false;
        take_bits(2);
        v = take_bits(wide ? 64 : 32);
        std::memmove(lru_table_ + 1, lru_table_, (LRU_ENTRIES - 1) * sizeof(uint64_t));
    }
    lru_table_[0] = v;                                     // move to front
    return true;
}

// ================================================================================================ LPDDR
void Updater::read_page(int32_t a, uint64_t* out) {
    if (!store->read_page(a, out)) unwritten_reads++;     // a page nothing ever wrote: the master's program is wrong
    wait_ += timing.page; stat_[W_STAT_PAGES_READ - W_STAT_CYCLES]++;
}
void Updater::write_page(int32_t a, const uint64_t* in) {
    store->write_page(a, in);
    wait_ += timing.page; stat_[W_STAT_PAGES_WRITTEN - W_STAT_CYCLES]++;
}
// An index page's entry i, written through.
void Updater::set_entry(int32_t index_page, int i, uint32_t e) {
    uint64_t pg[PAGE_WORDS];
    store->read_page(index_page, pg);
    Store::set_entry(pg, i, e);
    store->write_page(index_page, pg);
    wait_ += timing.entry;
}
// The next free page: the free page queue's next entry, its address in [31:0]. The caller has checked there is one.
bool Updater::take_page(int64_t t, int32_t& a) {
    uint64_t lo, hi;
    pop(t, free_pages_, EV_FREE_PAGE_CREDIT, lo, hi);
    page_count_--; pages_taken++;
    if (!lpddr_page(uint32_t(lo)))                        // UB (17.10.1; S11), as the controller has it
        FATAL("the Updater: undefined behavior at %lld: a free page 0x%x that is no page of LPDDR (17.10.1)", (long long)t, uint32_t(lo));
    a = int32_t(uint32_t(lo));
    return true;
}

// ================================================================================================ the root (v1.37)
void Updater::make_root(int64_t) {
    uint64_t pg[PAGE_WORDS];
    if (root_mode_ == ROOT_EMPTY) {                        // every entry "no page"; at depth 0 the root is the data page: zeros
        std::memset(pg, depth_ > 0 ? 0xFF : 0, sizeof pg);
    } else {                                               // the source's root, its entries no longer owned
        read_page(int32_t(source_root_), pg);
        if (depth_ > 0)
            for (int i = 0; i < 1024; i++) {
                const uint32_t e = Store::entry_of(pg, i);
                if (e != Store::NONE) Store::set_entry(pg, i, e & ~Store::OWNED);
            }
    }
    write_page(image_, pg);
    if (root_mode_ == ROOT_COPY && tracer_copy_) write_page(tracer_, pg);
}

// ================================================================================================ a page
// The pages a file takes to make page `page` its own: one for each level from the first entry on its path that is not
// its own (a shared page or a hole) down to the data page.
unsigned Updater::pages_needed(int32_t root, int64_t page) const {
    int32_t a = root;
    for (int level = 0; level < depth_; level++) {
        uint64_t pg[PAGE_WORDS];
        store->read_page(a, pg);
        const uint32_t e = Store::entry_of(pg, digit(page, level, depth_));
        if (e == Store::NONE || !(e & Store::OWNED)) return unsigned(depth_ - level);
        a = Store::addr(e);
    }
    return 0;
}

// Brings page `page` of the image into the page memory, making every level above it the image's own, and with
// tracer_copy the Tracer's copy's too, its page first at each level.
Updater::Bring Updater::bring(int64_t t, int64_t page) {
    const unsigned needed = pages_needed(image_, page) + (tracer_copy_ ? pages_needed(tracer_, page) : 0);
    if (needed > page_count_) { stop(ERR_OUT_OF_PAGES); return Bring::STOP; }
    if (needed > free_pages_.n) return Bring::STALL;       // the pages are still to be queued

    int32_t a = image_, b = tracer_;
    uint64_t pg[PAGE_WORDS], src[PAGE_WORDS];
    held_page_ = page; held_at_ = image_; held_owned_ = true; held_hole_ = false; held_entry_page_ = -1;   // depth 0: the root itself
    for (int level = 0; level < depth_; level++) {
        const bool last = level == depth_ - 1;
        const int i = digit(page, level, depth_);
        wait_ += timing.lookup;
        store->read_page(a, pg);
        const uint32_t e = Store::entry_of(pg, i);
        // what the level below holds, as it stands: read once, for both files (they are copies of one file)
        uint32_t f = e;
        if (tracer_copy_) { store->read_page(b, pg); f = Store::entry_of(pg, i); }
        const bool image_takes = e == Store::NONE || !(e & Store::OWNED);
        const bool tracer_takes = tracer_copy_ && (f == Store::NONE || !(f & Store::OWNED));
        const uint32_t from = tracer_takes ? f : e;
        const bool read_now = (tracer_takes || (image_takes && !last)) && from != Store::NONE;
        if (tracer_takes || image_takes) {
            if (from == Store::NONE) std::memset(src, last ? 0 : 0xFF, sizeof src);   // a hole: zeros, or a page of holes
            else if (read_now) read_page(Store::addr(from), src);
            if (!last && from != Store::NONE)
                for (int k = 0; k < 1024; k++) {
                    const uint32_t x = Store::entry_of(src, k);
                    if (x != Store::NONE) Store::set_entry(src, k, x & ~Store::OWNED);
                }
        }
        if (tracer_takes) {                                // the Tracer's copy: the page as it stands, its own
            int32_t q;
            if (!take_page(t, q)) return Bring::STOP;
            write_page(q, src);
            set_entry(b, i, uint32_t(q) | Store::OWNED);
            b = q;
        } else if (tracer_copy_) {
            b = Store::addr(f);
        }
        if (!image_takes) {
            a = Store::addr(e);
            if (last) { held_at_ = a; store->read_page(a, held_mem_); held_have_[0] = held_have_[1] = false; }
            continue;
        }
        int32_t p;
        if (!take_page(t, p)) return Bring::STOP;
        if (!last) {                                       // an index page: written now, then the entry above it
            write_page(p, src);
            set_entry(a, i, uint32_t(p) | Store::OWNED);
            a = p;
        } else {                                           // the data page: patched in the page memory, written later
            held_at_ = p; held_owned_ = false; held_hole_ = e == Store::NONE;
            held_entry_page_ = a; held_entry_ = i;
            if (e == Store::NONE) std::memset(held_mem_, 0, sizeof held_mem_);
            else if (tracer_takes) std::memcpy(held_mem_, src, sizeof held_mem_);
            else store->read_page(Store::addr(e), held_mem_);
            held_have_[0] = held_have_[1] = tracer_takes && from != Store::NONE;   // read whole for the Tracer's copy
        }
    }
    if (depth_ == 0) { store->read_page(image_, held_mem_); held_have_[0] = held_have_[1] = false; }
    held_dirty_[0] = held_dirty_[1] = false;
    held_ = true;
    return Bring::OK;
}

// The pending value into its word of the page memory. A half of the page is read when a value first needs it, unless
// the run of values covers that half whole from here, or the page is a hole (UI's table).
void Updater::patch() {
    const unsigned at = run_addr_ & 511, half = at >> 8;
    if (!held_have_[half]) {
        const bool covered = (at & 255) == 0 && run_left_ >= 256;
        if (!covered && !held_hole_) { wait_ += timing.half; stat_[W_STAT_PAGES_READ - W_STAT_CYCLES]++; }
        held_have_[half] = true;
    }
    held_mem_[at] = value_;
    held_dirty_[half] = true;
    run_addr_++; run_left_--; have_value_ = false;
    words_applied++;
}

// The page memory's page written: in place, the halves that changed; or, a page the image did not own, whole to its
// new page (the halves not yet in the page memory read first), and then the entry that names it.
void Updater::write_held() {
    if (held_owned_) {
        if (held_dirty_[0] || held_dirty_[1]) {
            store->write_page(held_at_, held_mem_);
            wait_ += held_dirty_[0] && held_dirty_[1] ? timing.page : timing.half;
            stat_[W_STAT_PAGES_WRITTEN - W_STAT_CYCLES]++;
        }
    } else {
        if (!held_hole_) {
            const unsigned missing = unsigned(!held_have_[0]) + unsigned(!held_have_[1]);
            if (missing) { wait_ += missing == 2 ? timing.page : timing.half; stat_[W_STAT_PAGES_READ - W_STAT_CYCLES]++; }
        }
        write_page(held_at_, held_mem_);
        set_entry(held_entry_page_, held_entry_, uint32_t(held_at_) | Store::OWNED);
    }
    held_ = false;
}

void Updater::finish(int64_t t) {
    busy_ = false;
    ev.post(t, EV_DONE);
    if (on_done) on_done(on_done_ctx, *this, t);
}

// ================================================================================================ the cycle
void Updater::step(int64_t t) {
    if (!busy_ || stopped_) return;
    stat_[0]++;                                            // STAT_CYCLES
    if (wait_ > 0) { wait_--; stat_[W_STAT_STALL_MEMORY - W_STAT_CYCLES]++; return; }
    if (root_pending_) { root_pending_ = false; make_root(t); return; }
    if (!have_value_) {
        if (word_count_ == 0) {                            // the run's end: the page memory's page, then done
            if (held_) { write_held(); return; }
            finish(t);
            return;
        }
        uint64_t v;
        if (!decode(t, v)) { stat_[W_STAT_STALL_DATA - W_STAT_CYCLES]++; return; }
        word_count_--;
        if (run_left_ == 0) {                              // a header word: the run's start and its count
            run_addr_ = uint32_t(v); run_left_ = uint32_t(v >> 32);
            return;
        }
        value_ = v; have_value_ = true;
    }
    const int64_t page = run_addr_ >> 9;
    if (page >= (int64_t(1) << (10 * depth_))) { stop(ERR_BEYOND); return; }
    // The value's page into the page memory, the page that was there written first, and the value into its word, all
    // in the word's cycle: what they cost is waited out before the next word.
    if (held_ && held_page_ != page) write_held();
    if (!held_) {
        const Bring r = bring(t, page);
        if (r == Bring::STALL) stat_[W_STAT_STALL_PAGES - W_STAT_CYCLES]++;
        if (r != Bring::OK) return;
    }
    patch();
}

void Updater::digest_words(int64_t* out) const {
    out[0] = int64_t(sim::hash_fields(errors_, busy_, stopped_, run_config_, word_count_, page_count_, image_root_, tracer_root_, source_root_, stat_));
    out[1] = int64_t(sim::hash_fields(in_head_, in_n_, in_lo_, in_hi_, free_pages_.head, free_pages_.n, free_pages_.left,
                                      ev.count, ev.last_post_at));
    out[2] = int64_t(sim::hash_fields(bits_, nbits_, lru_table_, run_addr_, run_left_, have_value_, value_, held_, held_page_, held_at_, wait_));
}

}  // namespace units
