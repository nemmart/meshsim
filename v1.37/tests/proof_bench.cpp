// proof_bench.cpp: proof_bench.h's run: segment_bench.h's `Proof` for the master tile's program (fw/cc/master.c).
#include "proof_bench.h"

#include "cache_figures.h"

#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "base/fatal.h"
#include "host_end.h"
#include "packet_writer.h"
#include "rtl_images.h"
#include "sha256.h"
#include "units/big_sram.h"
#include "units/file_store.h"
#include "units/ingress_egress.h"
#include "units/periph_slot.h"
#include "units/regmap.h"
#include "units/router.h"
#include "units/tile_ctl.h"
#include "units/updater.h"
#include "updater_bench.h"
#include "updater_view.h"

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
                  MR_LINK_WORDS, MR_WAKES, MR_LINK_READS, MR_PAGE_COUNT = 32, MR_RUN_START = 40, MR_RUN_DONE = 48, MR_FRAME_TYPE = 56,
                  MR_FRAME_START = 72, MR_FRAME_WAKES = 88, MR_FRAME_READS = 104, MR_COUNT_STORES = 120, MR_HANDED_WAKES, MR_HANDED_READS,
                  MR_FRAME_AWAKE, MR_REPLY_MCYCLE = MR_FRAME_AWAKE + 16, MR_REPLIED = MR_REPLY_MCYCLE + 2,
                  MR_WORDS };
constexpr uint32_t MR_OK = 1, MR_TRAP = 2, MR_FRAME = 4;
// ---- fw/tracer/tracer_layout.h's header words the master writes
enum : unsigned { TL_H_EXPECT = 8, TL_H_ADVICE_N = 10, TL_H_HASH_MUL = 12, TL_H_HASH_STEP = 13, TL_H_HASH_LINES = 14 };
constexpr unsigned T = segment::T, RECORD_WORDS = segment::RECORD_WORDS, REPLY_WORDS = 73;   // 29.14: the header and 72

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
    const units::Rv32* master_rv = nullptr;       // the master's core, for its cycles awake at the hand-over
    uint64_t awake_handed = 0;

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

    static void on_done(void* ctx, updater_view::UpdaterView& u, int64_t t) { static_cast<ChipProof*>(ctx)->run_done(u, t); }
    void run_done(updater_view::UpdaterView& u, int64_t t) {
        Done d;
        d.t = t; d.config = u.run_config(); d.page_count = u.page_count(); d.image_root = u.image_root(); d.tracer_root = u.tracer_root();
        for (int k = 0; k < 6; k++) { d.stat[k] = u.stat(k) - stat_before[k]; stat_before[k] = u.stat(k); }
        const int depth = int(d.config >> Updater::RC_DEPTH_LO & 3);
        const size_t i = done.size();
        const bool partial = i < run_names.size() && run_names[i] == "tracer";      // the final image, half made: not compared
        if (partial && master_rv) awake_handed = master_rv->mcycle - master_rv->cycles_wfi;   // the hand-over follows this done
        if (!partial) d.image = sha(updater_bench::image(*u.store(), int32_t(d.image_root), depth));
        if (d.config & Updater::RC_TRACER_COPY) d.tracer = sha(updater_bench::image(*u.store(), int32_t(d.tracer_root), depth));
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
        m.place(units::ID_MASTER, rtl_images::elf_symbol(master_elf, "system_config"), {MS_MAGIC, c.asics, c.segment, 0});
        results_at = rtl_images::elf_symbol(master_elf, "master_results");
        // v1.37 stage 4: the host's end of the link (host_end.h) sends the packet file's link packets, link_rate link
        // cycles a chip cycle, or a link cycle every link_every chip cycles
        if (c.wire) m.attach_wire(*c.wire);                // v1.37 stage 6: the chain of processes
        else {
            HostEnd& host = m.attach_host(c.packets, unsigned(c.link_rate), unsigned(c.link_every));
            host.noise = c.noise; host.hook = c.host_hook;
        }
        view.attach(m);
        if (c.updater_lookup) view.set_lookup(c.updater_lookup);
        master_rv = &m.c.unit<units::TileCore>(units::unit_prefix(units::ID_MASTER) + "core").rv;
        view.on_done(&on_done, this);
    }
    uint32_t results_at = 0;
    updater_view::UpdaterView view;              // the black box or the controller (WORKER6)

    void finished(mach::Mach& m, segment::Run& run) override {
        R.resize(MR_WORDS);
        for (unsigned i = 0; i < MR_WORDS; i++) R[i] = m.word(units::ID_MASTER, results_at + 4 * i);
        run.summary = R;
        run.released = R[MR_RELEASED];
        std::printf("%s: the master: status %u (detail 0x%x), %u frames, %u runs, %u pages taken, %u LOADs, %u results, %u others, boot %u; "
                    "ERRORS: the tile's 0x%x, Ingress's 0x%x, the Updater's 0x%x\n", name, R[MR_STATUS], R[MR_DETAIL], R[MR_FRAMES], R[MR_RUNS],
                    R[MR_PAGES_TAKEN], R[MR_LOADS], R[MR_RESULTS], R[MR_OTHERS], R[MR_BOOT], R[MR_TILE_ERRORS], R[MR_INGRESS_ERRORS],
                    R[MR_UPDATER_ERRORS]);
        m.cache_system().finish();
        cachefig::print(name, m.cache_system());   // updater_wip, WORKER5; the joint's figures and checks (the INTEGRATOR)
        if (R[MR_STATUS] != MR_OK) {                 // where it stopped
            updater_view::UpdaterView& u = view;
            units::IngressEgress& link = m.ingress();
            std::printf("%s: stopped with: the Updater busy %d, WORD_COUNT %u, PAGE_COUNT %u, %u words on its input and %u free pages queued, %u dones; the link: "
                        "%llu packets and %llu words arrived, %u words read out by the DMA, %llu forwarded, %llu discarded, DMA_FORWARD_COUNT 0x%x, %u and %u words "
                        "waiting; %u pages queued by the master\n", name, int(u.busy()), u.word_count(), u.page_count(), u.input_words(), u.free_page_entries(),
                        unsigned(done.size()), (unsigned long long)link.link_packets, (unsigned long long)link.link_words, R[MR_LINK_WORDS],
                        (unsigned long long)link.link_forwarded, (unsigned long long)link.link_discarded, link.forward, link.queue[0].held,
                        link.queue[1].held, R[MR_PAGES_QUEUED]);
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

    bool ends_early() const override { return c.expect.trap || c.expect.cut || c.expect.stop; }

    // ---- v1.37 (30.7, 29.14): the reply, at the host's end: its foreground words as they come, with their hops, and
    //      the link packets they came in. The run goes on past the master's exit until all 72 are in.
    std::vector<::hostlink::Word> reply;
    std::vector<unsigned> reply_hops;
    std::vector<size_t> reply_ends;                  // the words after which a link packet ended
    int64_t wire_sent_at = -1;                        // the poll at which the replies above were all first sent
    uint64_t settle_limit() const override { return c.wire ? ~uint64_t(0) : uint64_t(1) << 20; }
    bool settled(mach::Mach& m) override {
        if (m.word(units::ID_MASTER, results_at + 4 * MR_STATUS) != MR_OK) return true;
        if (c.wire) {                                  // this reply and those from below, first sent towards the driver
            const uint64_t sent = m.ingress().node->counts_above().sent_data, want = 3ull * (c.clients - c.client);
            const int64_t now = int64_t(c.wire->cycles);
            if (sent < want) return false;
            if (wire_sent_at < 0) wire_sent_at = now;
            return uint64_t(now - wire_sent_at) >= c.wire_margin;
        }
        units::IngressEgress::LinkAllocs a;
        ::hostlink::Word w;
        unsigned hops = 0;
        bool last = false;
        while (m.host().end.take(::hostlink::FOREGROUND, w, &hops, &last)) {
            reply.push_back(w); reply_hops.push_back(hops);
            if (last) reply_ends.push_back(reply.size());
        }
        return reply.size() >= REPLY_WORDS || m.word(units::ID_MASTER, results_at + 4 * MR_STATUS) != MR_OK;
    }
    static U128 word_of(const ::hostlink::Word& w) {
        U128 v;
        std::memcpy(&v.lo, w.byte, 8); std::memcpy(&v.hi, w.byte + 8, 8);
        return v;
    }
    void check_reply(mach::Mach& m, const segment::Run& run, updater_view::UpdaterView& u) {
        if (reply.size() != REPLY_WORDS) { fail("the reply: %zu words at the host's end, not %u (%llu cycles run after the exit)", reply.size(), REPLY_WORDS, (unsigned long long)run.after_exit); return; }
        for (unsigned h : reply_hops) if (h != 0) { fail("the reply: a link packet with hops %u from the first ASIC", h); break; }
        if (reply_ends != std::vector<size_t>{32, 64, 73}) fail("the reply: its link packets end after %zu words; 32, 64 and 73 expected (17.6.8)", reply_ends.empty() ? size_t(0) : reply_ends[0]);
        auto w32 = [&](unsigned word, unsigned k) { uint32_t v; std::memcpy(&v, reply[word].byte + 4 * k, 4); return v; };
        if (w32(0, 0) != 0 || w32(0, 1) != 0 || w32(0, 2) != 16 || w32(0, 3) != 72)
            fail("the reply's header word: 0x%08x 0x%08x type %u length %u; compression 0, type 16, length 72", w32(0, 0), w32(0, 1), w32(0, 2), w32(0, 3));
        uint64_t rows = 0;
        for (const auto& r : run.rows) rows += r.size() / segment::ROW_LONGS;
        if (w32(1, 0) != c.segment || w32(1, 1) != c.asics) fail("the reply's word 1: s %u and N %u; the run's %u and %u", w32(1, 0), w32(1, 1), c.segment, c.asics);
        if (w32(1, 2) != 0) fail("the reply's word 1 [95:64] is 0x%x, not 0", w32(1, 2));
        if (w32(4, 0) != rows) fail("the reply: %u rows traced; the replayers sank %llu", w32(4, 0), (unsigned long long)rows);
        if (w32(4, 1) != 0 || w32(4, 2) != 1 || w32(4, 3) != 0)
            fail("the reply's word 4: the Tracer's ERRORS 0x%x, the tracer tile's status %u, [127:96] 0x%x; 0, 1 and 0", w32(4, 1), w32(4, 2), w32(4, 3));
        if (w32(1, 3) != u.page_count()) fail("the reply: PAGE_COUNT %u, the Updater's %u", w32(1, 3), u.page_count());
        const uint64_t mcycle = uint64_t(w32(2, 0)) | uint64_t(w32(2, 1)) << 32;
        if (mcycle != (uint64_t(R[MR_REPLY_MCYCLE]) | uint64_t(R[MR_REPLY_MCYCLE + 1]) << 32) || mcycle == 0 || mcycle > run.cycles + run.after_exit)
            fail("the reply's mcycle %llu: the master's results say %u", (unsigned long long)mcycle, R[MR_REPLY_MCYCLE]);
        if (w32(2, 2) != u.stat(0) || w32(2, 3) != u.stat(1)) fail("the reply: STAT_CYCLES %u and STAT_STALL_DATA %u, the Updater's %u and %u", w32(2, 2), w32(2, 3), u.stat(0), u.stat(1));
        for (unsigned k = 0; k < 4; k++) if (w32(3, k) != 0) { fail("the reply's error word %u is 0x%x: a reply is sent only when the segment has run", k, w32(3, k)); break; }
        // words 5-72: the Tracer's record at the segment's end, as the tracer tile holds it; and the state, words 0 and
        // 2-65, record s + 1's, which the host checks (29.14)
        const bool last = c.segment + 1 == c.asics;
        for (unsigned w = 0; w < RECORD_WORDS; w++) {
            const U128 got = word_of(reply[5 + w]), want = row(m, units::ID_TRACER, places.tr_final + 4 * w);
            if (got != want) { fail("the reply's record, word %u: %016llx%016llx, the tracer tile's %016llx%016llx", w, (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)want.hi, (unsigned long long)want.lo); break; }
            if (!last && registers && w != 1 && w < 66) {
                const U128 r = frame_word(*registers, size_t(RECORD_WORDS) * (c.segment + 1) + w);
                if (got != r) { fail("the reply's record, word %u: %016llx%016llx, record s + 1's %016llx%016llx", w, (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)r.hi, (unsigned long long)r.lo); break; }
            }
        }
        if (R[MR_REPLIED] != 1) fail("the master's results of the reply: replied %u", R[MR_REPLIED]);
    }
    void check(mach::Mach& m, segment::Run& run) override {
        check_all(m, run);
        run.failures += cachefig::faults(name, m.cache_system());
        run.failures += failures;
    }
    void check_all(mach::Mach& m, const segment::Run& run) {
        updater_view::UpdaterView& u = view;
        units::IngressEgress& link = m.ingress();
        const FileStore& store = *u.store();
        if (numbers) {
            numbers->master = R;
            numbers->link_packets = link.link_packets; numbers->link_words = link.link_words; numbers->link_bytes = c.wire ? c.wire->cycles * c.link_rate : m.host().link_cycles;
            const auto& core = m.c.unit<units::TileCore>(units::unit_prefix(units::ID_MASTER) + "core").rv;
            numbers->master_awake = core.mcycle - core.cycles_wfi;
            numbers->master_instructions = core.instructions;
            numbers->link_room_cycles = c.wire ? 0 : m.host().stop_cycles; numbers->link_last_at = link.link_last_at;
            numbers->link_forwarded = link.link_forwarded; numbers->link_discarded = link.link_discarded;
            numbers->link_first_sendings = c.wire ? 0 : m.host().capture.data_packets;
            numbers->link_sent_again = c.wire ? 0 : m.host().capture.again;
            numbers->master_awake_handed = awake_handed;
            for (size_t i = 0; i < done.size(); i++) {
                RunAccount a;
                a.name = i < run_names.size() ? run_names[i] : "?";
                a.done_at = done[i].t; a.page_count = done[i].page_count;
                a.cycles = done[i].stat[0]; a.stall_data = done[i].stat[1]; a.stall_pages = done[i].stat[2]; a.stall_memory = done[i].stat[3];
                a.pages_read = done[i].stat[4]; a.pages_written = done[i].stat[5];
                numbers->runs.push_back(a);
            }
        }
        if (c.expect.stop) {                         // 30.8: a stop of the master's own on what a frame says
            if (!run.finished) fail("no exit: a frame that fails a check must stop the master");
            if (R[MR_STATUS] != MR_FRAME || R[MR_DETAIL] != c.expect.detail)
                fail("the master's status %u, detail 0x%x; a stop on a frame (%u), detail 0x%x expected", R[MR_STATUS], R[MR_DETAIL], MR_FRAME, c.expect.detail);
            if (R[MR_TILE_ERRORS] || R[MR_INGRESS_ERRORS] || R[MR_UPDATER_ERRORS] || link.errors(0) != 0 || u.errors(0) != 0)
                fail("an error bit with a stop of the master's own: the tile's 0x%x, Ingress's 0x%x, the Updater's 0x%x", R[MR_TILE_ERRORS], R[MR_INGRESS_ERRORS], R[MR_UPDATER_ERRORS]);
            if (m.c.toast()) fail("the toast pin with a stop of the master's own");
            if (!reply.empty()) fail("a reply (%zu words) after a stop", reply.size());
            return;
        }
        if (!parsed) { fail("the frame file is not as HOST-FRAMES.md says: %s", parse_error.c_str()); return; }
        if (c.noise) {                               // the noise must have reached the link (stage 4)
            const ::hostlink::LinkCounts n = link.node->counts_above();
            if (n.bad_header + n.bad_data + n.ahead == 0) fail("noise on the line, and the chip's link end saw no damaged link packet");
            else std::printf("%s: the chip's link end: %llu bad headers, %llu bad data, %llu ahead, %llu restarts\n", name, (unsigned long long)n.bad_header,
                             (unsigned long long)n.bad_data, (unsigned long long)n.ahead, (unsigned long long)n.restarts);
        }

        // ---- a run that must end early: that it did, as expected, and nothing else
        if (c.expect.trap) {
            const uint32_t tile_bits = (c.expect.ingress_errors ? 1u << 10 : 0) | (c.expect.updater_errors ? 1u << 11 : 0);   // ERRORS.peripheral_1, _2
            if (!run.finished) fail("no exit: the error path must end the run");
            if (R[MR_STATUS] != MR_TRAP) fail("the master's status is %u: an error bit must end in its error path (%u)", R[MR_STATUS], MR_TRAP);
            if (R[MR_INGRESS_ERRORS] != uint32_t(c.expect.ingress_errors) || link.errors(0) != c.expect.ingress_errors)
                fail("Ingress's ERRORS: 0x%x as the master read it, 0x%x in the end; expected 0x%x", R[MR_INGRESS_ERRORS], unsigned(link.errors(0)), unsigned(c.expect.ingress_errors));
            if (R[MR_UPDATER_ERRORS] != uint32_t(c.expect.updater_errors) || u.errors(0) != c.expect.updater_errors)
                fail("the Updater's ERRORS: 0x%x as the master read it, 0x%x in the end; expected 0x%x", R[MR_UPDATER_ERRORS], unsigned(u.errors(0)), unsigned(c.expect.updater_errors));
            if (R[MR_TILE_ERRORS] != tile_bits) fail("the tile's ERRORS 0x%x, expected its peripherals' bits alone, 0x%x", R[MR_TILE_ERRORS], tile_bits);
            return;
        }
        if (c.expect.cut) {
            if (run.finished) fail("the run ended: with the host's link packets ended inside a frame the master must wait");
            if (!m.host().file_cut) fail("the file did not end inside a packet");
            if (R[MR_STATUS] != 0) fail("the master's status is %u, not 0 (waiting)", R[MR_STATUS]);
            if (link.errors(0) != 0 || u.errors(0) != 0) fail("an error bit: Ingress's 0x%x, the Updater's 0x%x; silence is no error", unsigned(link.errors(0)), unsigned(u.errors(0)));
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
        if (u.errors(0) != 0 || link.errors(0) != 0) fail("ERRORS at the end: the Updater's 0x%x, the link's 0x%x", unsigned(u.errors(0)), unsigned(link.errors(0)));
        // the joint's checks (cache_system.h) are cachefig::faults'; the Updater's bursts on its port, client 1, alone
        if (u.sys->arb.read_bursts[1] + u.sys->arb.write_bursts[1] == 0) fail("no burst of the Updater's on the arbiter's client 1");
        if (u.sys->arb.read_bursts[2] + u.sys->arb.write_bursts[2] != 0) fail("bursts on the arbiter's client 2, the RW engine's, in a proof");
        if (u.sys->lookups + u.sys->updates == 0) fail("no request of the Updater's on the cache's look-up port 1");
        {   // v1.37 (16.1, 16.2): the master tile's loads into the tracer tile ride the line, through the memory tile's router
            const units::Router& rm = m.c.unit<units::Router>("cc1.router");
            const units::Router& rt = m.c.unit<units::Router>("cc3.router");
            if (rm.line_out == 0 || rt.line_in < rm.line_out) fail("the line: %llu packets from the master tile, %llu at the tracer tile",
                                                                   (unsigned long long)rm.line_out, (unsigned long long)rt.line_in);
            if (rm.route_errors || rt.route_errors) fail("the line: route errors at the special tiles' routers");
        }
        if (u.input_words() != 0 || u.free_page_entries() != 0) fail("left in the Updater: %u words on its input, %u free pages", u.input_words(), u.free_page_entries());
        const uint64_t words = c.file.bytes.size() / 16, out = R[MR_LINK_WORDS] + link.link_forwarded + link.link_discarded;
        if (link.link_words != words || out != words || (link.forward & units::IngressEgress::FWD_COUNT) != 0)
            fail("the link: %llu words arrived, %u read out by the DMA, %llu forwarded, %llu discarded, of %llu; DMA_FORWARD_COUNT 0x%x at the end%s",
                 (unsigned long long)link.link_words, R[MR_LINK_WORDS], (unsigned long long)link.link_forwarded, (unsigned long long)link.link_discarded,
                 (unsigned long long)words, link.forward, !c.wire && m.host().file_cut ? "; the file ends inside a packet" : "");

        // ---- the host End's data link packets as first sent, against the packet file's, byte for byte (host_end.h,
        //      THE CAPTURE; NOTES.md C5): on a clean line, the End sends the file's link packets
        if (!c.noise && !c.host_hook && !c.wire) {
            const HostEnd::Capture& cap = m.host().capture;
            const std::vector<uint8_t> want = HostEnd::data_link_packets(c.packets);
            size_t at = 0;
            while (at < want.size() && at < cap.sent.size() && want[at] == cap.sent[at]) at++;
            if (cap.restarts_after_data) fail("the host End started its link again after data: %llu restart link packets", (unsigned long long)cap.restarts_after_data);
            else if (cap.sent != want)
                fail("the host End's data link packets as first sent: %zu bytes, the packet file's %zu; the first difference at byte %zu", cap.sent.size(), want.size(), at);
        }

        // ---- the reply at the host's end (30.7, 29.14)
        if (!c.wire) check_reply(m, run, u);            // on the chain the driver checks it
        else {                                         // the wire is lossless: no link packet may come damaged, either way
            const ::hostlink::LinkCounts a = link.node->counts_above(), b = link.node->counts_below();
            const uint64_t damaged = a.bad_header + a.bad_data + a.bad_fields + a.ahead + b.bad_header + b.bad_data + b.bad_fields + b.ahead;
            std::printf("%s: the chip's link ends: above %llu taken, %llu sent (%llu again), %llu restarts; below %llu taken, %llu sent (%llu again), "
                        "%llu restarts; %llu damaged or ahead\n", name, (unsigned long long)a.taken, (unsigned long long)a.sent_data, (unsigned long long)a.sent_again,
                        (unsigned long long)a.restarts, (unsigned long long)b.taken, (unsigned long long)b.sent_data, (unsigned long long)b.sent_again,
                        (unsigned long long)b.restarts, (unsigned long long)damaged);
            if (damaged) fail("a damaged link packet, or one ahead, on the wire, which loses nothing: %llu", (unsigned long long)damaged);
        }

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
    if (c.lpddr_seed) with.lpddr_seed = c.lpddr_seed;
    return segment::run_chip(seg, nullptr, with);
}

}  // namespace proof
