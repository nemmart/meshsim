// ctl_bench.h: a bench for the Updater's controller (updater_wip, WORKER6), the same for the black box, so that every
// check drives both the same way (NOTES section 9). Test-side code: the STL throughout.
//
// `Bench<U>` is updater_bench.h's bench made generic: U is units::Updater (the black box) or units::UpdaterTop (the
// controller, with the test LPDDR, the stand-in cache and, in port mode, the word feeder in place of the shim). It
// writes block 2, queues free pages on the free page queue's credits, sends the stream (into the segment queue on its
// credits, or offered on the word port), and starts each run, calling the unit as the slot does: one accept a cycle at
// most, a store's effect at the end of its cycle, step(t) for cycle t. Pages are handed out in order from page 1, so
// two benches given the same runs hand out the same pages.
//
// `play` is updater_bench's play over a frame file (a copy: that file is the black box's and WORKER7 changes it).
// `Compare` checks two benches against each other at a run's end (NOTES section 9).
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "frames.h"
#include "standin_cache.h"
#include "test_lpddr.h"
#include "units/file_store.h"
#include "units/net.h"
#include "units/updater.h"
#include "units/updater_top.h"
#include "updater_bench.h"

namespace ctl_bench {

using units::Updater;
using units::UpdaterTop;
using units::tracer::FileStore;
using Word = std::array<uint64_t, 2>;

constexpr uint32_t config(bool lru, bool cont, bool tracer_copy, int depth, unsigned root) {
    return (lru ? 1u : 0) | (cont ? 2u : 0) | (tracer_copy ? 4u : 0) | uint32_t(depth) << 3 | root << 5;
}

struct Options {
    bool port = false;                             // the controller fed on its port by the feeder, no shim
    units::axi::LpddrTiming lpddr;                 // the test LPDDR's timing (jitter, seed)
    uint64_t cache_seed = 0;                       // the stand-in at random (0: 80 a line, 80 the write)
    uint64_t feeder_seed = 0;                      // the feeder's valid at random (0: always)
    bool rules = true;                             // the write rule checked (NOTES section 8)
};

struct Run {
    uint32_t config = 0, words = 0, pages = 0;
    int32_t image = 0, tracer = 0, source = 0;
};

template <class U>
struct Bench {
    static constexpr bool CTL = std::is_same_v<U, UpdaterTop>;
    FileStore store;
    U u;
    Options opt;
    int64_t t = 1;
    uint32_t next_page = 1;
    unsigned allowance = 0, page_allowance = 0;
    std::vector<Word> words;                       // the stream being sent
    size_t sent = 0;                               // shim mode: words sent into the segment queue
    struct Account { uint32_t pages_asked, pages_left, cycles, stall_data, stall_pages, stall_memory, pages_read, pages_written; bool done; };
    std::vector<Account> accounts;
    uint64_t dones = 0, page_credits = 0, segment_credits = 0;
    int64_t cycle_limit = int64_t(1) << 32;
    // the controller's other side
    std::unique_ptr<test_pieces::TestLpddr> lpddr;
    std::unique_ptr<test_pieces::StandInCache> cache;
    std::unique_ptr<test_pieces::WordFeeder> feeder;
    // the write rule's state: pages queued and named by no entry; roots being made; owned data pages of the image
    std::set<uint32_t> fresh, roots, owned_data;
    uint32_t cur_image = 0;
    unsigned cur_depth = 0;

    explicit Bench(const Options& o = {}) : opt(o) {
        if constexpr (CTL) {
            lpddr = std::make_unique<test_pieces::TestLpddr>(store, o.lpddr);
            cache = std::make_unique<test_pieces::StandInCache>(store, o.cache_seed);
            cache->lpddr = lpddr.get();
            feeder = std::make_unique<test_pieces::WordFeeder>(o.feeder_seed);
            u.lpddr = lpddr.get(); u.cache = cache.get(); u.store = &store;
            if (o.port) { u.shim_on = false; u.source = feeder.get(); u.reset(0); }
            if (o.rules) {
                lpddr->allow_write = [this](uint32_t p, std::string& why) {
                    if (fresh.count(p) || roots.count(p) || owned_data.count(p)) return true;
                    why = "a write to page " + std::to_string(p) + ", neither new, nor a root being made, nor an owned data page of the image";
                    return false;
                };
                cache->on_update = [this](uint32_t root, unsigned depth, uint32_t, unsigned level, uint32_t e) {
                    fresh.erase(e & units::lookup::ADDRESS);
                    if (root == cur_image && level == depth - 1) owned_data.insert(e & units::lookup::ADDRESS);
                };
            }
        } else {
            u.store = &store;
        }
    }

    int32_t new_page() { return int32_t(next_page++ * 32u); }
    bool port() const { return CTL && opt.port; }
    void stream(std::vector<Word> w) {
        words = std::move(w); sent = 0;
        if constexpr (CTL) { feeder->words = &words; feeder->restart(); }
    }
    void stream(const frames::File& f, const frames::Frame& fr) {
        std::vector<Word> w(fr.length);
        for (size_t k = 0; k < fr.length; k++) w[k] = {f.lo(fr, k), f.hi(fr, k)};
        stream(std::move(w));
    }
    size_t words_sent() const {
        if constexpr (CTL) if (opt.port) return feeder->next;
        return sent;
    }
    bool stream_left() const { return words_sent() < words.size(); }

    uint32_t word(int w) { uint32_t v = 0; u.word(t, w, v); return v; }

    // the owned data pages of a file, walked from its root (the write rule)
    void walk_owned(uint32_t at, unsigned level, unsigned depth) {
        if (level == depth) { owned_data.insert(at); return; }
        if (!store.has_page(int32_t(at))) return;
        const uint64_t* pg = store.raw_page(int32_t(at));
        for (int i = 0; i < 1024; i++) {
            const uint32_t e = FileStore::entry_of(pg, i);
            if (e != FileStore::NONE && (e & FileStore::OWNED)) walk_owned(e & 0x7FFFFFFFu, level + 1, depth);
        }
    }

    // One run, to its done: true; false if the unit stopped on an error or the limit passed.
    bool run(const Run& r) {
        if constexpr (CTL) if (opt.rules) {
            const unsigned root = r.config >> 5 & 3, depth = r.config >> 3 & 3;
            roots.clear(); owned_data.clear();
            cur_image = uint32_t(r.image); cur_depth = depth;
            if (root != 0) { roots.insert(uint32_t(r.image)); if (root == 2 && (r.config & 4)) roots.insert(uint32_t(r.tracer)); }
            else walk_owned(uint32_t(r.image), 0, depth);
        }
        const uint32_t ws[6] = {r.config, r.words, r.pages, uint32_t(r.image), uint32_t(r.tracer), uint32_t(r.source)};
        for (int w = 0; w < 6; w++) { u.step(t); u.write_word(t, 32 + w, ws[w]); t++; }
        uint32_t stat0[6];
        for (int k = 0; k < 6; k++) stat0[k] = word(48 + k);
        u.step(t); u.run(t); t++;
        uint32_t pages_to_queue = r.pages > u.free_page_entries() ? r.pages - u.free_page_entries() : 0;
        const bool shim = !port();
        bool done = false;
        const uint32_t mask = 1u << Updater::EV_DONE | (shim ? 1u << Updater::EV_SEGMENT_CREDIT : 0) | 1u << Updater::EV_FREE_PAGE_CREDIT;
        for (int64_t limit = t + cycle_limit; !done && t < limit; t++) {
            const uint32_t ev = u.ev.value(t) & mask;
            if (pages_to_queue > 0 && page_allowance > 0) {
                const int32_t p = new_page();
                fresh.insert(uint32_t(p));
                u.accept(t - 3, 64, Updater::PART_FREE_PAGES << 20, int(units::lw::SIZE_128), uint32_t(p), 0);
                pages_to_queue--; page_allowance--;
            } else if (shim && sent < words.size() && allowance > 0) {
                u.accept(t - 3, 64, Updater::PART_SEGMENT << 20, int(units::lw::SIZE_128), words[sent][0], words[sent][1]);
                sent++; allowance--;
            }
            u.step(t);
            u.ev.consume_late(t, ev);
            if (ev & 1u << Updater::EV_SEGMENT_CREDIT) { allowance += 256; segment_credits++; }
            if (ev & 1u << Updater::EV_FREE_PAGE_CREDIT) { page_allowance += 128; page_credits++; }
            if (ev & 1u << Updater::EV_DONE) { done = true; dones++; }
            if (u.errors(t) & ~Updater::ERR_QUEUE_OVERRUN) break;
        }
        // a stopped controller: let what is in flight land (it sinks every beat), for the stores to compare
        if constexpr (CTL) for (int k = 0; k < 2000 && !u.idle(t); k++, t++) u.step(t);
        Account a;
        a.pages_asked = r.pages;
        a.pages_left = word(34);
        uint32_t stat[6];
        for (int k = 0; k < 6; k++) stat[k] = word(48 + k) - stat0[k];
        a.cycles = stat[0]; a.stall_data = stat[1]; a.stall_pages = stat[2]; a.stall_memory = stat[3]; a.pages_read = stat[4]; a.pages_written = stat[5];
        a.done = done;
        accounts.push_back(a);
        return done;
    }

    // The stream not yet decoded, as bits in order: the decoder's, then the words held behind it, then those not yet
    // sent (NOTES C16).
    std::vector<uint8_t> rest() const {
        std::vector<uint8_t> b;
        auto bits = [&](const uint64_t* w, unsigned n) { for (unsigned k = 0; k < n; k++) b.push_back(uint8_t(w[k / 64] >> (k % 64) & 1)); };
        if constexpr (CTL) {
            const auto& q = u.ctl.q;
            bits(q.bb, q.nb);
            for (unsigned k = 0; k < q.fifo_n; k++) bits(q.fifo[(q.fifo_head + k) % units::updctl::Ctl::FIFO], 128);
            if (u.shim_on) for (unsigned k = 0; k < u.shim.n; k++) bits(u.shim.mem[(u.shim.head + k) % units::SegmentShim::SIZE], 128);
        } else {
            bits(u.bits_, u.nbits_);
            for (unsigned k = 0; k < u.segments_.n; k++) {
                const unsigned at = (u.segments_.head + k) % u.segments_.size;
                const uint64_t w[2] = {u.segments_.lo[at], u.segments_.hi[at]};
                bits(w, 128);
            }
        }
        for (size_t k = sent; k < words.size(); k++) bits(words[k].data(), 128);
        return b;
    }
    // The controller's own checks: no read of an unwritten page, no early update, no refused write.
    std::string violations() const {
        if constexpr (CTL) {
            std::string v;
            if (lpddr->unwritten) v += " reads of unwritten pages: " + std::to_string(lpddr->unwritten) + ";";
            if (lpddr->refused) v += " writes refused: " + std::to_string(lpddr->refused) + " (" + lpddr->refused_why + ");";
            if (cache->early) v += " early updates: " + std::to_string(cache->early) + " (" + cache->early_why + ");";
            if (cache->unwritten) v += " index pages read unwritten: " + std::to_string(cache->unwritten) + ";";
            return v;
        } else {
            return u.unwritten_reads ? " reads of unwritten pages: " + std::to_string(u.unwritten_reads) : "";
        }
    }
};

// Two benches after the same run: "" if they agree, else what differs (NOTES section 9).
inline std::string compare(Bench<Updater>& a, Bench<UpdaterTop>& b) {
    std::string d;
    auto diff = [&](const char* what, uint64_t x, uint64_t y) { if (x != y) d += std::string(" ") + what + " " + std::to_string(x) + " against " + std::to_string(y) + ";"; };
    for (int w = 32; w <= 37; w++) diff(("word " + std::to_string(w)).c_str(), a.word(w), b.word(w));
    diff("ERRORS", uint32_t(a.u.errors(a.t)), uint32_t(b.u.errors(b.t)));
    diff("busy", a.u.running(a.t), b.u.running(b.t));
    diff("dones", a.dones, b.dones);
    diff("free page credits", a.page_credits, b.page_credits);
    diff("free page entries", a.u.free_page_entries(), b.u.free_page_entries());
    diff("pages handed out", a.next_page, b.next_page);
    if (!a.accounts.empty() && !b.accounts.empty()) {
        const auto& x = a.accounts.back();
        const auto& y = b.accounts.back();
        diff("STAT_PAGES_WRITTEN", x.pages_written, y.pages_written);
        // the black box's "covered" test does not count a half the run leaves part-written (NOTES section 5)
        diff("STAT_PAGES_READ (+ the black box's uncounted halves)", x.pages_read + b.u.ctl.cut_half_reads_run, y.pages_read);
    }
    if (a.rest() != b.rest()) d += " the stream not yet decoded;";
    // the stores, byte for byte: every page of each, by address
    std::vector<std::pair<int32_t, std::vector<uint64_t>>> pa, pb;
    a.store.for_each_stored([&](int32_t at, const uint64_t* w) { pa.push_back({at, std::vector<uint64_t>(w, w + 512)}); });
    b.store.for_each_stored([&](int32_t at, const uint64_t* w) { pb.push_back({at, std::vector<uint64_t>(w, w + 512)}); });
    if (pa != pb) {
        d += " the stores (" + std::to_string(pa.size()) + " and " + std::to_string(pb.size()) + " pages";
        for (size_t k = 0; k < pa.size() && k < pb.size(); k++)
            if (pa[k] != pb[k]) { d += ", first at page " + std::to_string(pa[k].first) + " and " + std::to_string(pb[k].first); break; }
        d += ");";
    }
    return d;
}

// A frame file for segment s as a plan of runs (updater_bench::play's sequence): each step a run, the frame whose data
// it takes (a new stream when it differs from the last step's), and the image to keep after it.
struct Step {
    std::string name;
    Run run;
    const frames::Frame* frame = nullptr;
    int depth = 0;
    int32_t keep = 0, keep_tracer = 0;             // roots whose images are kept after the run (0: none)
};
// The roots are pages of their own, from page 2^19 up, apart from the free pages a bench hands out from page 1.
inline bool plan(const frames::File& file, unsigned asics, unsigned s, std::vector<Step>& steps, std::string& err) {
    using namespace frames;
    uint32_t next_root = 1u << 19;
    struct { uint32_t& n; int32_t new_page() { return int32_t(n++ * 32u); } } pages{next_root};
    auto fail = [&](const std::string& what) { err = what; return false; };
    frames::ProgramHeader program;
    frames::ProofHeader proof;
    bool has_base = false;
    int image_depth = 0;
    int32_t program_root = 0, base_root = 0, cycle0_root = 0;
    if (s >= asics) return fail("s is not below N");
    const Frame* header = nullptr;
    auto step = [&](const char* name, const Frame& f, const Run& r, int depth, int32_t keep, int32_t keep_tracer = 0) {
        Step x; x.name = name; x.run = r; x.frame = &f; x.depth = depth; x.keep = keep; x.keep_tracer = keep_tracer;
        steps.push_back(x);
    };
    for (const Frame& f : file.frame) {
        switch (f.type) {
            case PROGRAM_HEADER:
                if (!file.program_header(f, program)) return fail("frame 1");
                image_depth = depth_of(program.file_pages);
                header = &f;
                break;
            case PROGRAM_DATA:
                if (!header || header->type != PROGRAM_HEADER || f.length != program.words128) return fail("frame 2 against frame 1");
                program_root = pages.new_page();
                step("program image", f, {config(f.compression, false, false, image_depth, Updater::ROOT_EMPTY), program.words64, program.pages,
                                          program_root, 0, 0}, image_depth, program_root);
                break;
            case PROOF_HEADER:
                if (!file.proof_header(f, asics, proof)) return fail("frame 3 for this N");
                break;
            case TABLE_HEADER: {
                TableHeader h;
                if (!file.table_header(f, h)) return fail("frame 4");
                if (!h.path.empty()) has_base = true;
                header = &f;
                break;
            }
            case TABLE_DATA: {
                if (!header || header->type != TABLE_HEADER) return fail("frame 5 with no frame 4 before it");
                TableHeader h;
                file.table_header(*header, h);
                if (f.length != h.words128) return fail("frame 5 against frame 4");
                const int depth = depth_of(table_pages(h.k));
                const bool is_base = !h.path.empty(), from_base = !is_base && !h.base.empty();
                if (from_base && !has_base) return fail("frame 4 names a base table the file did not establish");
                const int32_t root = pages.new_page();
                step(is_base ? "base table" : "jump table", f,
                     {config(f.compression, false, false, depth, from_base ? Updater::ROOT_COPY : Updater::ROOT_EMPTY), h.words64, h.pages, root, 0,
                      from_base ? base_root : 0}, depth, is_base ? 0 : root);
                if (is_base) base_root = root;
                break;
            }
            case DATA_IMAGE:
                if (f.length != proof.data_words128) return fail("frame 6 against frame 3");
                cycle0_root = pages.new_page();
                step("data image", f, {config(f.compression, false, false, image_depth, Updater::ROOT_COPY), proof.data_words64, proof.data_pages,
                                       cycle0_root, 0, program_root}, image_depth, cycle0_root);
                break;
            case REGISTERS:
            case ADVICE:
                break;
            case SEGMENTS: {
                if (f.length != proof.segments_words128) return fail("frame 9 against frame 3");
                uint32_t before = 0, after_s = 0;
                for (unsigned k = 0; k < s; k++) before += proof.segment_words64[k];
                for (unsigned k = s + 1; k < asics; k++) after_s += proof.segment_words64[k];
                const uint32_t mine = proof.segment_words64[s];
                const std::array<uint32_t, 3>& pg = proof.per_asic[s];
                const int32_t start_root = pages.new_page();
                step("segments before s", f, {config(f.compression, false, false, image_depth, Updater::ROOT_COPY), before, pg[0], start_root, 0,
                                              cycle0_root}, image_depth, start_root);
                const int32_t final_root = pages.new_page(), tracer_root = pages.new_page();
                step("segment s", f, {config(f.compression, true, true, image_depth, Updater::ROOT_COPY), mine, 2 * pg[2], final_root, tracer_root,
                                      start_root}, image_depth, 0, tracer_root);
                step("segments after s", f, {config(f.compression, true, false, image_depth, Updater::ROOT_EXISTS), after_s, pg[1] - pg[2], final_root, 0,
                                             0}, image_depth, final_root);
                break;
            }
            default: return fail("a frame of no type");
        }
    }
    return true;
}

// The images a plan keeps, in its order.
struct Images { std::vector<std::string> names; std::vector<std::vector<uint8_t>> images; };

// One step on a bench: the stream if the frame is new, the run; the images it keeps. False if the run did not end.
template <class U>
bool execute(Bench<U>& b, const frames::File& file, const std::vector<Step>& steps, size_t k, Images& out) {
    const Step& x = steps[k];
    if (k == 0 || steps[k - 1].frame != x.frame) b.stream(file, *x.frame);
    const bool ok = b.run(x.run);
    if (x.keep) { out.names.push_back(x.name); out.images.push_back(updater_bench::image(b.store, x.keep, x.depth)); }
    if (x.keep_tracer) { out.names.push_back("tracer"); out.images.push_back(updater_bench::image(b.store, x.keep_tracer, x.depth)); }
    return ok;
}

}  // namespace ctl_bench
