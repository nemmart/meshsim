// proof_bench.cpp: proof_bench.h's run: segment_bench.h's `Proof` for the master tile's program (fw/cc/master.c).
#include "proof_bench.h"

#include "cache_figures.h"

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "base/fatal.h"
#include "packet_writer.h"
#include "rtl_images.h"
#include "sha256.h"
#include "units/big_sram.h"
#include "units/file_store.h"
#include "units/ingress_egress.h"
#include "units/periph_slot.h"
#include "units/regmap.h"
#include "units/tile_ctl.h"
#include "units/updater.h"
#include "updater_bench.h"

namespace proof {

using segment::U128;
using units::Updater;
using units::tracer::FileStore;

namespace {

// ---- fw/cc/master_layout.h, mirrored: the master's scratchpad, what it is told, its results
constexpr uint32_t ML_MAP = 0x40000, ML_MAP_ROWS = 8192;
constexpr uint32_t MD_MAGIC = 0x4D313337, MS_MAGIC = 0x4E415353;
enum : unsigned { MD_H_MAGIC = 0, MD_H_TILES = 1, MD_H_TRACER = 6, MD_H_TR_RECORD = 11, MD_H_TR_EXPECT, MD_H_TR_ADVICE, MD_H_TR_ADVICE_ROWS,
                  MD_H_GO_LATENCY, MD_H_WORDS };
enum : unsigned { MR_STATUS = 0, MR_DETAIL, MR_FRAMES, MR_RUNS, MR_PAGES_TAKEN, MR_LOADS, MR_RESULTS, MR_OTHERS, MR_ROOT_PROGRAM, MR_ROOT_BASE,
                  MR_ROOT_TABLE, MR_ROOT_CYCLE0, MR_ROOT_START, MR_ROOT_FINAL, MR_ROOT_TRACER, MR_DEPTHS, MR_TILE_ERRORS, MR_INGRESS_ERRORS,
                  MR_UPDATER_ERRORS, MR_ERROR_LINES, MR_MCAUSE = 22, MR_MEPC, MR_BOOT, MR_HANDED, MR_RELEASED, MR_END, MR_PAGES_QUEUED,
                  MR_LINK_WORDS, MR_PAGE_COUNT = 32, MR_RUN_START = 40, MR_RUN_DONE = 48, MR_FRAME_TYPE = 56, MR_FRAME_START = 72,
                  MR_WORDS = 88 };
constexpr uint32_t MR_OK = 1, MR_TRAP = 2;
// ---- fw/tracer/tracer_layout.h's header words the master writes
enum : unsigned { TL_H_EXPECT = 8, TL_H_ADVICE_N = 10, TL_H_HASH_MUL = 12, TL_H_HASH_STEP = 13, TL_H_HASH_LINES = 14 };
constexpr unsigned T = segment::T, RECORD_WORDS = segment::RECORD_WORDS;

std::string sha(const std::vector<uint8_t>& bytes) { return test::sha256_hex(bytes.data(), bytes.size()); }

// Every page of a file, its index pages and its root with them: by address, in 128-byte units.
void reach(const FileStore& store, int32_t at, int level, std::set<int32_t>& out) {
    out.insert(at);
    if (level == 0) return;
    uint64_t page[FileStore::PAGE_WORDS];
    store.read_page(at, page);
    for (int i = 0; i < 1024; i++) {
        const uint32_t e = FileStore::entry_of(page, i);
        if (e != FileStore::NONE) reach(store, FileStore::addr(e), level - 1, out);
    }
}

struct ChipProof final : segment::Proof {
    const Case& c;
    const char* name;
    Numbers* numbers;
    segment::MasterPlaces places{};
    std::vector<uint32_t> R;                      // master_results
    int failures = 0, shown = 0;
    // what the frames say, for this N and s
    frames::ProgramHeader program;
    frames::ProofHeader proof;
    frames::TableHeader table;                    // the proof's own table's header
    bool parsed = false;
    std::string parse_error;
    std::vector<std::string> run_names;           // the runs the frames call for, in order
    const frames::Frame* registers = nullptr;
    const frames::Frame* advice = nullptr;
    // each run as its done was posted
    struct Done { int64_t t; uint32_t config, page_count, image_root, tracer_root, stat[6]; std::string image, tracer; };
    std::vector<Done> done;
    uint32_t stat_before[6] = {};

    ChipProof(const Case& c_, const char* name_, Numbers* numbers_) : c(c_), name(name_), numbers(numbers_) { parse(); }

    void fail(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        failures++;
        if (shown++ >= 40) return;
        char text[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(text, sizeof text, fmt, ap);
        va_end(ap);
        std::printf("%s: FAILED: %s\n", name, text);
    }

    // The frames' own account of the proof: the runs they call for, in order, and the header frames' words.
    void parse() {
        using namespace frames;
        const Frame* header = nullptr;
        for (const Frame& f : c.file.frame) {
            switch (f.type) {
                case PROGRAM_HEADER: if (!c.file.program_header(f, program)) { parse_error = "frame 1"; return; } break;
                case PROGRAM_DATA: run_names.push_back("program"); break;
                case PROOF_HEADER: if (!c.file.proof_header(f, c.asics, proof)) { parse_error = "frame 3, for this N"; return; } break;
                case TABLE_HEADER: header = &f; break;
                case TABLE_DATA: {
                    TableHeader h;
                    if (!header || !c.file.table_header(*header, h)) { parse_error = "frame 4"; return; }
                    if (h.path.empty()) { table = h; run_names.push_back("table"); } else run_names.push_back("base");
                    break;
                }
                case DATA_IMAGE: run_names.push_back("cycle0"); break;
                case REGISTERS: registers = &f; break;
                case ADVICE: advice = &f; break;
                case SEGMENTS:
                    run_names.push_back("start-" + std::to_string(c.segment));
                    run_names.push_back("tracer");            // the run of segment s: the Tracer's copy is whole at its done
                    run_names.push_back("final");
                    break;
                default: parse_error = "a frame of no type"; return;
            }
        }
        if (!registers || !advice || registers->length != RECORD_WORDS * c.asics) { parse_error = "frames 7 and 8"; return; }
        parsed = true;
    }
    int image_depth() const { return frames::depth_of(program.file_pages); }
    int table_depth() const { return frames::depth_of(frames::table_pages(table.k)); }

    static void on_done(void* ctx, Updater& u, int64_t t) { static_cast<ChipProof*>(ctx)->run_done(u, t); }
    void run_done(Updater& u, int64_t t) {
        Done d;
        d.t = t; d.config = u.run_config_; d.page_count = u.page_count_; d.image_root = u.image_root_; d.tracer_root = u.tracer_root_;
        for (int k = 0; k < 6; k++) { d.stat[k] = u.stat_[k] - stat_before[k]; stat_before[k] = u.stat_[k]; }
        const int depth = int(d.config >> Updater::RC_DEPTH_LO & 3);
        const size_t i = done.size();
        const bool partial = i < run_names.size() && run_names[i] == "tracer";      // the final image, half made: not compared
        if (!partial) d.image = sha(updater_bench::image(*u.store, int32_t(d.image_root), depth));
        if (d.config & Updater::RC_TRACER_COPY) d.tracer = sha(updater_bench::image(*u.store, int32_t(d.tracer_root), depth));
        done.push_back(d);
    }

    void place(mach::Mach& m, const std::string& master_elf, const segment::MasterPlaces& p) override {
        places = p;
        std::vector<uint32_t> desc(MD_H_WORDS, 0);
        desc[MD_H_MAGIC] = MD_MAGIC;
        for (unsigned i = 0; i < 5; i++) { desc[MD_H_TILES + i] = p.tiles[i]; desc[MD_H_TRACER + i] = p.tracer[i]; }
        desc[MD_H_TR_RECORD] = p.tr_record; desc[MD_H_TR_EXPECT] = p.tr_expect; desc[MD_H_TR_ADVICE] = p.tr_advice;
        desc[MD_H_TR_ADVICE_ROWS] = p.tr_advice_rows; desc[MD_H_GO_LATENCY] = 1;
        m.place(units::ID_MASTER, rtl_images::elf_symbol(master_elf, "boot_desc"), desc);
        m.place(units::ID_MASTER, rtl_images::elf_symbol(master_elf, "system_config"), {MS_MAGIC, c.asics, c.segment});
        results_at = rtl_images::elf_symbol(master_elf, "master_results");
        m.set_link(c.packets);
        m.ingress().link_cycles_per_byte = c.cycles_per_byte;
        if (c.updater_lookup) m.updater().timing.lookup = c.updater_lookup;
        m.updater().on_done = &on_done;
        m.updater().on_done_ctx = this;
    }
    uint32_t results_at = 0;

    void finished(mach::Mach& m, segment::Run& run) override {
        R.resize(MR_WORDS);
        for (unsigned i = 0; i < MR_WORDS; i++) R[i] = m.word(units::ID_MASTER, results_at + 4 * i);
        run.summary = R;
        run.released = R[MR_RELEASED];
        std::printf("%s: the master: status %u (detail 0x%x), %u frames, %u runs, %u pages taken, %u LOADs, %u results, %u others, boot %u; "
                    "ERRORS: the tile's 0x%x, Ingress's 0x%x, the Updater's 0x%x\n", name, R[MR_STATUS], R[MR_DETAIL], R[MR_FRAMES], R[MR_RUNS],
                    R[MR_PAGES_TAKEN], R[MR_LOADS], R[MR_RESULTS], R[MR_OTHERS], R[MR_BOOT], R[MR_TILE_ERRORS], R[MR_INGRESS_ERRORS],
                    R[MR_UPDATER_ERRORS]);
        if (m.c.cfg().cache_ctl) cachefig::print(name, m.cache_system());   // updater_wip, WORKER5
        if (R[MR_STATUS] != MR_OK) {                 // where it stopped
            Updater& u = m.updater();
            units::IngressEgress& link = m.ingress();
            std::printf("%s: stopped with: the Updater busy %d, WORD_COUNT %u, PAGE_COUNT %u, %u segment words and %u free pages queued, %u dones; the link: %llu packets "
                        "and %llu words arrived, %u words read out, %u and %u packets waiting; %u pages queued by the master\n", name, int(u.busy_), u.word_count_,
                        u.page_count_, u.segment_entries(), u.free_page_entries(), unsigned(done.size()), (unsigned long long)link.link_packets,
                        (unsigned long long)link.link_words, R[MR_LINK_WORDS], unsigned(link.link_stream[0].n), unsigned(link.link_stream[1].n), R[MR_PAGES_QUEUED]);
        }
    }

    U128 files_word(const U128& recorded) const override {
        U128 w;
        const uint64_t depths = R.empty() ? 0 : R[MR_DEPTHS];
        w.lo = uint64_t(R.empty() ? 0 : R[MR_ROOT_TRACER]) | (depths & 3) << 31 | uint64_t(R.empty() ? 0 : R[MR_ROOT_TABLE]) << 33;
        w.hi = (recorded.hi & ~uint64_t(3)) | (depths >> 8 & 3);
        return w;
    }
    U128 frame_word(const frames::Frame& f, size_t w) const { U128 v; v.lo = c.file.lo(f, w); v.hi = c.file.hi(f, w); return v; }
    static U128 row(mach::Mach& m, unsigned unit, uint32_t word) {
        const units::BigSram& s = m.c.unit<units::BigSram>(units::unit_prefix(unit) + "sram");
        U128 v;
        v.lo = s.peek64(word); v.hi = s.peek64(word + 2);
        return v;
    }

    bool ends_early() const override { return c.expect.trap || c.expect.cut; }
    void check(mach::Mach& m, segment::Run& run) override {
        check_all(m, run);
        if (m.c.cfg().cache_ctl) run.failures += cachefig::faults(name, m.cache_system());
        run.failures += failures;
    }
    void check_all(mach::Mach& m, const segment::Run& run) {
        Updater& u = m.updater();
        units::IngressEgress& link = m.ingress();
        const FileStore& store = m.store();
        if (numbers) {
            numbers->master = R;
            numbers->link_packets = link.link_packets; numbers->link_words = link.link_words; numbers->link_bytes = link.link_bytes;
            const auto& core = m.c.unit<units::TileCore>(units::unit_prefix(units::ID_MASTER) + "core").rv;
            numbers->master_awake = core.mcycle - core.cycles_wfi;
            numbers->master_instructions = core.instructions;
            for (size_t i = 0; i < done.size(); i++) {
                RunAccount a;
                a.name = i < run_names.size() ? run_names[i] : "?";
                a.done_at = done[i].t; a.page_count = done[i].page_count;
                a.cycles = done[i].stat[0]; a.stall_data = done[i].stat[1]; a.stall_pages = done[i].stat[2]; a.stall_memory = done[i].stat[3];
                a.pages_read = done[i].stat[4]; a.pages_written = done[i].stat[5];
                numbers->runs.push_back(a);
            }
        }
        if (!parsed) { fail("the frame file is not as HOST-FRAMES.md says: %s", parse_error.c_str()); return; }

        // ---- a run that must end early: that it did, as expected, and nothing else
        if (c.expect.trap) {
            const uint32_t tile_bits = (c.expect.ingress_errors ? 1u << 10 : 0) | (c.expect.updater_errors ? 1u << 11 : 0);   // ERRORS.peripheral_1, _2
            if (!run.finished) fail("no exit: the error path must end the run");
            if (R[MR_STATUS] != MR_TRAP) fail("the master's status is %u: an error bit must end in its error path (%u)", R[MR_STATUS], MR_TRAP);
            if (R[MR_INGRESS_ERRORS] != uint32_t(c.expect.ingress_errors) || link.link_errors != c.expect.ingress_errors)
                fail("Ingress's ERRORS: 0x%x as the master read it, 0x%x in the end; expected 0x%x", R[MR_INGRESS_ERRORS], unsigned(link.link_errors), unsigned(c.expect.ingress_errors));
            if (R[MR_UPDATER_ERRORS] != uint32_t(c.expect.updater_errors) || u.errors(0) != c.expect.updater_errors)
                fail("the Updater's ERRORS: 0x%x as the master read it, 0x%x in the end; expected 0x%x", R[MR_UPDATER_ERRORS], unsigned(u.errors(0)), unsigned(c.expect.updater_errors));
            if (R[MR_TILE_ERRORS] != tile_bits) fail("the tile's ERRORS 0x%x, expected its peripherals' bits alone, 0x%x", R[MR_TILE_ERRORS], tile_bits);
            return;
        }
        if (c.expect.cut) {
            if (run.finished) fail("the run ended: with the link quiet inside a packet the master must wait");
            if (!link.link_cut) fail("the file did not end inside a packet");
            if (R[MR_STATUS] != 0) fail("the master's status is %u, not 0 (waiting)", R[MR_STATUS]);
            if (link.link_errors != 0 || u.errors(0) != 0) fail("an error bit: Ingress's 0x%x, the Updater's 0x%x; silence is no error", unsigned(link.link_errors), unsigned(u.errors(0)));
            return;
        }

        // ---- the master's results
        if (R[MR_STATUS] != MR_OK) {                 // the rest would be checks of a proof that was not made
            fail("the master's status is %u, detail 0x%x (fw/cc/master_layout.h); mcause 0x%x, mepc 0x%x", R[MR_STATUS], R[MR_DETAIL], R[MR_MCAUSE], R[MR_MEPC]);
            return;
        }
        if (R[MR_FRAMES] != c.file.frame.size()) fail("the master took %u frames of %zu", R[MR_FRAMES], c.file.frame.size());
        for (size_t i = 0; i < c.file.frame.size() && i < 16; i++)
            if (R[MR_FRAME_TYPE + i] != c.file.frame[i].type) { fail("frame %zu: the master saw type %u, the file has %u", i, R[MR_FRAME_TYPE + i], c.file.frame[i].type); break; }
        if (R[MR_RUNS] != run_names.size() || done.size() != run_names.size()) fail("%u runs in the master's results, %zu dones posted; the frames call for %zu", R[MR_RUNS], done.size(), run_names.size());
        if (R[MR_LOADS] != 2 * (T + 1) || R[MR_RESULTS] != T + 1 || R[MR_OTHERS] != 0) fail("%u LOADs, %u results, %u other requests (130, 65, 0)", R[MR_LOADS], R[MR_RESULTS], R[MR_OTHERS]);
        if (R[MR_BOOT] != 5) fail("the master's sequence stopped at %u", R[MR_BOOT]);
        if (R[MR_DEPTHS] != uint32_t(image_depth() | table_depth() << 8)) fail("the depths the master took: 0x%x; the frames' %d and %d", R[MR_DEPTHS], image_depth(), table_depth());

        // ---- PAGE_COUNT 0 at every done; no error bit; the link read to its end; the queues empty
        for (size_t i = 0; i < done.size(); i++) {
            const char* run_name = i < run_names.size() ? run_names[i].c_str() : "?";
            if (done[i].page_count != 0) fail("run %zu (%s): PAGE_COUNT %u at its done", i, run_name, done[i].page_count);
            if (i < 8 && R[MR_PAGE_COUNT + i] != 0) fail("run %zu (%s): PAGE_COUNT %u as the master read it", i, run_name, R[MR_PAGE_COUNT + i]);
        }
        if (R[MR_TILE_ERRORS] || R[MR_INGRESS_ERRORS] || R[MR_UPDATER_ERRORS] || R[MR_ERROR_LINES] || R[MR_ERROR_LINES + 1] || R[MR_ERROR_LINES + 2])
            fail("an error bit: the tile's 0x%x, Ingress's 0x%x, the Updater's 0x%x, the error lines 0x%x 0x%x 0x%x", R[MR_TILE_ERRORS], R[MR_INGRESS_ERRORS],
                 R[MR_UPDATER_ERRORS], R[MR_ERROR_LINES], R[MR_ERROR_LINES + 1], R[MR_ERROR_LINES + 2]);
        if (u.errors(0) != 0 || link.link_errors != 0) fail("ERRORS at the end: the Updater's 0x%x, the link's 0x%x", unsigned(u.errors(0)), unsigned(link.link_errors));
        if (u.unwritten_reads != 0) fail("%llu reads of a page nothing wrote", (unsigned long long)u.unwritten_reads);
        if (u.segment_entries() != 0 || u.free_page_entries() != 0) fail("entries left in the Updater's queues: %u segment words, %u free pages", u.segment_entries(), u.free_page_entries());
        const uint64_t words = c.file.bytes.size() / 16;
        if (link.link_cut || link.link_words != words || R[MR_LINK_WORDS] != words)
            fail("the link: %llu words arrived, %u read out by the master, of %llu%s", (unsigned long long)link.link_words, R[MR_LINK_WORDS], (unsigned long long)words, link.link_cut ? "; the file ends inside a packet" : "");

        // ---- each image as its run ended, against gold's; the frozen ones again at the end
        for (size_t i = 0; i < done.size() && i < run_names.size(); i++) {
            const std::string& n = run_names[i];
            const std::string& got = n == "tracer" ? done[i].tracer : done[i].image;
            if (n == "base") continue;                // gold's reference has no base table of its own: the table made from it is compared
            const auto g = c.gold.find(n);
            if (g == c.gold.end()) { if (!c.gold.empty()) fail("gold's reference has no image %s", n.c_str()); continue; }
            if (got != g->second) fail("the %s image as its run ended: SHA-256 %s, gold's %s", n.c_str(), got.c_str(), g->second.c_str());
        }
        if (c.gold.empty()) fail("no reference: gold's images for this s were not given");
        const struct { const char* image; unsigned root; int depth; } frozen[] = {
            {"program", MR_ROOT_PROGRAM, image_depth()}, {"table", MR_ROOT_TABLE, table_depth()}, {"cycle0", MR_ROOT_CYCLE0, image_depth()},
            {"start", MR_ROOT_START, image_depth()}, {"final", MR_ROOT_FINAL, image_depth()}};
        for (const auto& f : frozen) {
            const std::string n = std::string(f.image) == "start" ? "start-" + std::to_string(c.segment) : f.image;
            const auto g = c.gold.find(n);
            if (g == c.gold.end()) continue;
            const std::string got = sha(updater_bench::image(store, int32_t(R[f.root]), f.depth));
            if (got != g->second) fail("the %s image at the end of the run: SHA-256 %s, gold's %s", n.c_str(), got.c_str(), g->second.c_str());
        }

        // ---- the page map: page 0 and exactly the pages the files hold
        std::set<int32_t> marked, held, stored;
        const units::BigSram& sram = m.c.unit<units::BigSram>(units::unit_prefix(units::ID_MASTER) + "sram");
        for (uint32_t word = 0; word < 4 * ML_MAP_ROWS; word++) {
            const uint32_t bits = sram.peek(ML_MAP + word);
            for (unsigned b = 0; b < 32; b++) if (bits >> b & 1) marked.insert(int32_t((word * 32 + b) * 32));
        }
        const struct { unsigned root; int depth; } files[] = {{MR_ROOT_PROGRAM, image_depth()}, {MR_ROOT_BASE, table_depth()}, {MR_ROOT_TABLE, table_depth()},
            {MR_ROOT_CYCLE0, image_depth()}, {MR_ROOT_START, image_depth()}, {MR_ROOT_FINAL, image_depth()}, {MR_ROOT_TRACER, image_depth()}};
        held.insert(0);
        for (const auto& f : files) if (R[f.root]) reach(store, int32_t(R[f.root]), f.depth, held);
        stored.insert(0);
        store.for_each_stored([&](int32_t at, const uint64_t*) { stored.insert(at); });
        if (numbers) numbers->pages_marked = marked.size();
        if (marked != held) {
            size_t extra = 0, missing = 0;
            for (int32_t a : marked) extra += !held.count(a);
            for (int32_t a : held) missing += !marked.count(a);
            fail("the page map marks %zu pages, the files hold %zu (with page 0): %zu marked and in no file, %zu in a file and not marked", marked.size(), held.size(), extra, missing);
        }
        if (stored != marked) fail("LPDDR holds %zu pages (with page 0), the page map marks %zu", stored.size(), marked.size());
        if (R[MR_PAGES_TAKEN] + 1 != marked.size()) fail("the master took %u pages, the map marks %zu with page 0", R[MR_PAGES_TAKEN], marked.size());

        // ---- the hand-over, in the tracer tile's scratchpad
        const unsigned s = c.segment;
        for (unsigned w = 0; w < RECORD_WORDS; w++) {
            U128 want = frame_word(*registers, size_t(RECORD_WORDS) * s + w);
            if (w == 66) want = files_word(want);
            const U128 got = row(m, units::ID_TRACER, places.tr_record + 4 * w);
            if (got != want) { fail("the record handed over, word %u: %016llx%016llx, frame 7's %016llx%016llx", w, (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)want.hi, (unsigned long long)want.lo); break; }
        }
        const units::BigSram& tr = m.c.unit<units::BigSram>(units::unit_prefix(units::ID_TRACER) + "sram");
        const bool last = s + 1 == c.asics;
        if (last) { if (tr.peek(TL_H_EXPECT) != 0) fail("the last segment: the tracer tile's header still names an expected end state"); }
        else for (unsigned w = 0; w < RECORD_WORDS; w++) {
            const U128 want = frame_word(*registers, size_t(RECORD_WORDS) * (s + 1) + w), got = row(m, units::ID_TRACER, places.tr_expect + 4 * w);
            if (got != want) { fail("the expected end state handed over, word %u: %016llx%016llx, frame 7's %016llx%016llx", w, (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)want.hi, (unsigned long long)want.lo); break; }
        }
        size_t log_at = c.asics / 4;                 // frame 8: N counts, then the logs
        uint32_t values = 0;
        for (unsigned k = 0; k <= s; k++) {
            values = c.file.u32(advice->data() + 4 * k);
            if (k < s) log_at += (size_t(values) + 1) / 2;
        }
        for (size_t w = 0; w < (size_t(values) + 1) / 2; w++) {
            const U128 want = frame_word(*advice, log_at + w), got = row(m, units::ID_TRACER, places.tr_advice + uint32_t(4 * w));
            if (got != want) { fail("the advice handed over, word %zu: %016llx%016llx, frame 8's %016llx%016llx", w, (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)want.hi, (unsigned long long)want.lo); break; }
        }
        if (tr.peek(TL_H_ADVICE_N) != values) fail("the tracer tile's header: %u advice values, frame 8's count is %u", tr.peek(TL_H_ADVICE_N), values);
        if (tr.peek(TL_H_HASH_MUL) != table.p1 || tr.peek(TL_H_HASH_STEP) != table.p3 || tr.peek(TL_H_HASH_LINES) != table.k)
            fail("the tracer tile's header: the hash constants 0x%x, %u, %u; frame 4's 0x%x, %u, %u", tr.peek(TL_H_HASH_MUL), tr.peek(TL_H_HASH_STEP), tr.peek(TL_H_HASH_LINES), table.p1, table.p3, table.k);

        // ---- the Tracer's last record against record s + 1: words 0 and 2-65, the state (word 1 is the window's own)
        if (!last) for (unsigned w = 0; w < 66; w++) {
            if (w == 1) continue;
            const U128 want = frame_word(*registers, size_t(RECORD_WORDS) * (s + 1) + w), got = row(m, units::ID_TRACER, places.tr_final + 4 * w);
            if (got != want) { fail("the Tracer's last record, word %u: %016llx%016llx, record s + 1's %016llx%016llx", w, (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)want.hi, (unsigned long long)want.lo); break; }
        }
    }
};

}  // namespace

std::map<std::string, std::string> gold_digests(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string image, digest;
        if (words >> image >> digest) out[image] = digest;
    }
    return out;
}

std::vector<uint8_t> packets_of(const frames::File& file) {
    std::vector<uint8_t> out;
    std::string err;
    if (!packet_writer::packetize(file.bytes, out, err)) FATAL("proof: %s", err.c_str());
    return out;
}

segment::Run run(const Case& c, const segment::Segment& seg, const segment::Options& o, Numbers* numbers) {
    ChipProof p(c, seg.name.c_str(), numbers);
    segment::Options with = o;
    with.proof = &p;
    return segment::run_chip(seg, nullptr, with);
}

}  // namespace proof
