// updater_bench.cpp: updater_bench.h's controller and the frame file's play.
#include "updater_bench.h"

#include <cstring>

#include "units/net.h"

namespace updater_bench {

std::vector<uint8_t> image(const FileStore& store, int32_t root, int depth) {
    std::vector<uint8_t> out;
    FileStore::File f;
    f.root = root; f.depth = depth;
    store.for_each_page(f, [&](int64_t page, const uint64_t* words) {
        bool any = false;
        for (int i = 0; i < FileStore::PAGE_WORDS && !any; i++) any = words[i] != 0;
        if (!any) return;
        const uint64_t offset = uint64_t(page) * 4096;
        for (unsigned b = 0; b < 8; b++) out.push_back(uint8_t(offset >> (8 * b)));
        for (int i = 0; i < FileStore::PAGE_WORDS; i++) for (unsigned b = 0; b < 8; b++) out.push_back(uint8_t(words[i] >> (8 * b)));
    });
    return out;
}

bool Bench::run(const Run& r) {
    // block 2, then START: each store in a cycle of its own, in effect at its end
    const uint32_t words[6] = {r.config, r.words, r.pages, uint32_t(r.image), uint32_t(r.tracer), uint32_t(r.source)};
    for (int w = 0; w < 6; w++) { u.step(t); u.write_word(t, Updater::W_RUN_CONFIG + w, words[w]); t++; }
    uint32_t stat0[6];
    for (int k = 0; k < 6; k++) u.word(t, Updater::W_STAT_CYCLES + k, stat0[k]);
    u.step(t); u.run(t); t++;
    uint32_t pages_to_queue = r.pages;
    bool done = false;
    for (int64_t limit = t + cycle_limit; !done && t < limit; t++) {
        // a load of EVENTS under a mask of done and both credits: one count from each that is set
        const uint32_t ev = u.ev.value(t) & (1u << Updater::EV_DONE | 1u << Updater::EV_SEGMENT_CREDIT | 1u << Updater::EV_FREE_PAGE_CREDIT);
        // one WRITE a cycle into the region: a free page first, else the stream's next word
        if (pages_to_queue > 0 && page_allowance > 0) {
            u.accept(t - 3, 64, Updater::PART_FREE_PAGES << 20, int(units::lw::SIZE_128), uint32_t(new_page()), 0);
            pages_to_queue--; page_allowance--;
        } else if (stream_left() && allowance > 0) {
            u.accept(t - 3, 64, Updater::PART_SEGMENT << 20, int(units::lw::SIZE_128), file->lo(*frame, sent), file->hi(*frame, sent));
            sent++; allowance--;
        }
        u.step(t);
        u.ev.consume_late(t, ev);
        if (ev & 1u << Updater::EV_SEGMENT_CREDIT) allowance += Updater::HALF;
        if (ev & 1u << Updater::EV_FREE_PAGE_CREDIT) page_allowance += Updater::HALF;
        if (ev & 1u << Updater::EV_DONE) done = true;
        if (u.errors(t) & ~Updater::ERR_QUEUE_OVERRUN) break;
    }
    Account a;
    a.pages_asked = r.pages;
    u.word(t, Updater::W_PAGE_COUNT, a.pages_left);
    uint32_t stat[6];
    for (int k = 0; k < 6; k++) { u.word(t, Updater::W_STAT_CYCLES + k, stat[k]); stat[k] -= stat0[k]; }
    a.cycles = stat[0]; a.stall_data = stat[1]; a.stall_pages = stat[2]; a.stall_memory = stat[3]; a.pages_read = stat[4]; a.pages_written = stat[5];
    accounts.push_back(a);
    return done;
}

bool play(Bench& b, const frames::File& file, unsigned asics, unsigned s, Proof& p, std::string& err) {
    using namespace frames;
    auto fail = [&](const std::string& what) { err = what; return false; };
    auto run = [&](const char* name, const Bench::Run& r) {
        p.run_names.push_back(name);
        if (b.run(r)) return true;
        err = std::string(name) + ": the run did not end (ERRORS " + std::to_string(b.u.errors(b.t)) + ")";
        return false;
    };
    if (s >= asics) return fail("s is not below N");
    const Frame* header = nullptr;                    // the last header frame (1 or 4) seen: its data frame follows
    for (const Frame& f : file.frame) {
        switch (f.type) {
            case PROGRAM_HEADER:
                if (!file.program_header(f, p.program)) return fail("frame 1 is not as HOST-FRAMES.md says");
                p.image_depth = depth_of(p.program.file_pages);
                header = &f;
                break;
            case PROGRAM_DATA: {
                if (!header || header->type != PROGRAM_HEADER || f.length != p.program.words128) return fail("frame 2 against frame 1");
                p.program_root = b.new_page();
                b.stream(file, f);
                if (!run("program image", {Bench::config(f.compression, false, false, p.image_depth, Updater::ROOT_EMPTY), p.program.words64,
                                           p.program.pages, p.program_root, 0, 0})) return false;
                p.program_image = image(b.store, p.program_root, p.image_depth);
                break;
            }
            case PROOF_HEADER:
                if (!file.proof_header(f, asics, p.proof)) return fail("frame 3 is not as HOST-FRAMES.md says for this N");
                break;
            case TABLE_HEADER: {
                TableHeader h;
                if (!file.table_header(f, h)) return fail("frame 4 is not as HOST-FRAMES.md says");
                if (!h.path.empty()) { p.base = h; p.has_base = true; } else p.table = h;
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
                if (from_base && !p.has_base) return fail("frame 4 names a base table the file did not establish");
                const int32_t root = b.new_page();
                b.stream(file, f);
                if (!run(is_base ? "base table" : "jump table",
                         {Bench::config(f.compression, false, false, depth, from_base ? Updater::ROOT_COPY : Updater::ROOT_EMPTY), h.words64, h.pages,
                          root, 0, from_base ? p.base_root : 0})) return false;
                if (is_base) { p.base_root = root; p.base_image = image(b.store, root, depth); }
                else { p.table_root = root; p.table_depth = depth; p.table_image = image(b.store, root, depth); }
                break;
            }
            case DATA_IMAGE: {
                if (f.length != p.proof.data_words128) return fail("frame 6 against frame 3");
                p.cycle0_root = b.new_page();
                b.stream(file, f);
                if (!run("data image", {Bench::config(f.compression, false, false, p.image_depth, Updater::ROOT_COPY), p.proof.data_words64,
                                        p.proof.data_pages, p.cycle0_root, 0, p.program_root})) return false;
                p.cycle0_image = image(b.store, p.cycle0_root, p.image_depth);
                break;
            }
            case REGISTERS:
                if (f.length != RECORD_WORDS * asics) return fail("frame 7 does not hold N records");
                break;
            case ADVICE:
                break;
            case SEGMENTS: {
                if (f.length != p.proof.segments_words128) return fail("frame 9 against frame 3");
                uint32_t before = 0, after = 0;
                for (unsigned k = 0; k < s; k++) before += p.proof.segment_words64[k];
                for (unsigned k = s + 1; k < asics; k++) after += p.proof.segment_words64[k];
                const uint32_t mine = p.proof.segment_words64[s];
                const std::array<uint32_t, 3>& pages = p.proof.per_asic[s];   // start, final, the Tracer's copy
                b.stream(file, f);
                p.start_root = b.new_page();
                if (!run("segments before s", {Bench::config(f.compression, false, false, p.image_depth, Updater::ROOT_COPY), before, pages[0],
                                               p.start_root, 0, p.cycle0_root})) return false;
                p.start_image = image(b.store, p.start_root, p.image_depth);
                p.final_root = b.new_page(); p.tracer_root = b.new_page();
                if (!run("segment s", {Bench::config(f.compression, true, true, p.image_depth, Updater::ROOT_COPY), mine, 2 * pages[2],
                                       p.final_root, p.tracer_root, p.start_root})) return false;
                p.tracer_image = image(b.store, p.tracer_root, p.image_depth);
                if (!run("segments after s", {Bench::config(f.compression, true, false, p.image_depth, Updater::ROOT_EXISTS), after,
                                              pages[1] - pages[2], p.final_root, 0, 0})) return false;
                p.final_image = image(b.store, p.final_root, p.image_depth);
                break;
            }
            default: return fail("a frame of no type");
        }
    }
    if (b.stream_left()) return fail("frame data left unsent");
    return true;
}

}  // namespace updater_bench
