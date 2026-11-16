// segment_bench.cpp: segment_bench.h's fixture reader, the fill port's model and the run.
#include "segment_bench.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "base/fatal.h"
#include "rtl_images.h"
#include "units/big_sram.h"
#include "units/periph_slot.h"
#include "units/regmap.h"
#include "units/replayer.h"

namespace segment {

using units::tracer::MemSystem;

// ================================================================================================= the files
Line Guest::line(uint64_t index) const {
    Line l;
    if (index >= (uint64_t(1) << (5 + 10 * depth))) { l.status = STATUS_BEYOND; return l; }
    const auto it = lines.find(index);
    if (it == lines.end()) { l.status = STATUS_HOLE; return l; }
    return it->second;
}

// ================================================================================================= the fixture
namespace {

struct Reader {
    const std::string& b;
    size_t at = 0;
    bool ok = true;
    explicit Reader(const std::string& bytes) : b(bytes) {}
    bool need(size_t n) { if (at + n > b.size()) ok = false; return ok; }
    uint8_t u8() { if (!need(1)) return 0; return uint8_t(b[at++]); }
    uint32_t u32() { if (!need(4)) return 0; uint32_t v = 0; for (unsigned i = 0; i < 4; i++) v |= uint32_t(uint8_t(b[at + i])) << (8 * i); at += 4; return v; }
    uint64_t u64() { const uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
    U128 u128() { U128 v; v.lo = u64(); v.hi = u64(); return v; }
    void bytes(uint8_t* out, size_t n) { if (!need(n)) return; std::memcpy(out, b.data() + at, n); at += n; }
};

std::string read_gz(const std::string& path, bool& ok) {
    std::string out;
    const bool gz = path.size() > 3 && path.compare(path.size() - 3, 3, ".gz") == 0;
    FILE* p = popen(((gz ? "gzip -dc '" : "cat '") + path + "'").c_str(), "r");
    ok = p != nullptr;
    if (!p) return out;
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, p)) > 0;) out.append(buf, n);
    ok = pclose(p) == 0;
    return out;
}

}  // namespace

bool read_fixture(const std::string& path, Segment& s, Rtl& rtl, std::string& err) {
    bool ok = false;
    const std::string file = read_gz(path, ok);
    if (!ok) { err = "cannot read " + path; return false; }
    Reader r(file);
    if (r.u32() != 0x31474553) { err = path + ": not a segment fixture (its magic)"; return false; }
    size_t slash = path.rfind('/');
    s.name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    s.name = s.name.substr(0, s.name.find('.'));
    s.windows = r.u32(); s.rows = r.u32(); s.seed = r.u32(); s.cycle_limit = r.u64(); s.misses = r.u32() != 0;
    if (!r.ok || s.windows == 0 || s.windows > T) { err = path + ": its windows"; return false; }
    for (uint32_t& w : rtl.tracer_header) w = r.u32();
    for (uint32_t& w : rtl.replay_header) w = r.u32();
    s.guest.lowest = r.u64(); s.guest.log_k = r.u32(); s.guest.depth = r.u32();
    for (uint32_t n = r.u32(); n > 0 && r.ok; n--) {
        const uint64_t index = r.u64();
        Line l; l.status = r.u8(); r.bytes(l.bytes, LINE_BYTES);
        s.guest.lines[index] = l;
    }
    s.map.k = r.u32(); s.map.p1 = r.u32(); s.map.p3 = r.u32();
    for (uint32_t n = r.u32(); n > 0 && r.ok; n--) { Line l; l.status = r.u8(); r.bytes(l.bytes, LINE_BYTES); s.map.lines.push_back(l); }
    s.map.has_filler = r.u8() != 0;
    if (s.map.has_filler) r.bytes(s.map.filler, LINE_BYTES);
    for (U128& w : s.start) w = r.u128();
    for (uint32_t n = r.u32(); n > 0 && r.ok; n--) s.advice.push_back(r.u64());
    for (U128& w : s.end) w = r.u128();
    s.window.resize(s.windows);
    for (Window& w : s.window) {
        for (U128& x : w.record) x = r.u128();
        w.scans[0] = r.u32(); w.scans[1] = r.u32();
        for (Stream& st : w.streams) {
            st.length = r.u64();
            for (uint32_t n = r.u32(); n > 0 && r.ok; n--) st.entries.push_back(r.u128());
        }
        const uint32_t rows = r.u32();
        if (!r.need(size_t(rows) * 8 * ROW_LONGS)) break;
        w.rows.resize(size_t(rows) * ROW_LONGS);
        for (uint64_t& x : w.rows) x = r.u64();
    }
    rtl.present = true;
    rtl.cycles = r.u64();
    for (uint32_t n = r.u32(); n > 0 && r.ok; n--) rtl.summary.push_back(r.u32());
    for (uint32_t n = r.u32(); n > 0 && r.ok; n--) rtl.report.push_back(r.u32());
    for (uint64_t& c : rtl.fill_counts) c = r.u64();
    for (uint32_t n = r.u32(); n > 0 && r.ok; n--) {
        FillCall c;
        c.cycle = r.u64(); c.kind = r.u8(); c.root = r.u32(); c.depth = r.u8(); c.line = r.u64(); c.due = r.u64(); c.status = r.u8();
        rtl.fill.push_back(c);
    }
    rtl.row_cycles.resize(T);
    for (auto& tile : rtl.row_cycles) for (uint32_t n = r.u32(); n > 0 && r.ok; n--) tile.push_back(r.u32());
    if (r.u32() != 0x444e4553 || !r.ok || r.at != file.size()) { err = path + ": not as the format says (its end)"; return false; }
    return true;
}

// ================================================================================================= the fill port
// chip_trace_test.rs: each read's latency by its line's kind, a write's, in cycles; each ready 0 one cycle in READY_BUSY.
namespace {
constexpr uint32_t CODE_LATENCY[2] = {2, 24}, DATA_LATENCY[2] = {2, 32}, MAP_LATENCY[2] = {2, 40}, WRITE_LATENCY[2] = {2, 30};
constexpr uint32_t READY_BUSY = 4;
}  // namespace

Fill::Fill(const Guest& g, const Map& map, uint32_t seed, int64_t offset) : guest(g), map_(map), random_(seed), offset_(offset) {
    port.ctx = this;
    port.read_ready = &Fill::read_ready; port.read = &Fill::read;
    port.write_ready = &Fill::write_ready; port.write = &Fill::write;
}

// The RTL test's `drive`, for every cycle up to `cycle`: the two readies, then the read response's 128 junk bytes.
void Fill::draw_to(int64_t cycle) {
    while (drawn_ < cycle) {
        drawn_++;
        read_ready_ = !random_.one_in(READY_BUSY);
        write_ready_ = !random_.one_in(READY_BUSY);
        for (unsigned i = 0; i < LINE_BYTES; i++) random_.next();
    }
}

bool Fill::read_ready(void* ctx, int64_t t) {
    Fill& f = *static_cast<Fill*>(ctx);
    f.draw_to(t - f.offset_);
    return f.read_ready_;
}
bool Fill::write_ready(void* ctx, int64_t t) {
    Fill& f = *static_cast<Fill*>(ctx);
    f.draw_to(t - f.offset_);
    return f.write_ready_;
}

int64_t Fill::read(void* ctx, int64_t t, int32_t root, int depth, int64_t line, MemSystem::Response& r) {
    Fill& f = *static_cast<Fill*>(ctx);
    const int64_t cycle = t - f.offset_;
    f.draw_to(cycle);
    if (f.read_due_ > cycle && f.fault.empty()) f.fault = "a read taken with a read outstanding";
    Line l;
    const uint32_t* latency;
    if (uint32_t(root) == MAP_ROOT) {
        if (uint32_t(depth) != MAP_DEPTH && f.fault.empty()) f.fault = "the map's depth";
        f.counts[2]++;
        if (uint64_t(line) < f.map_.lines.size()) {
            l = f.map_.lines[size_t(line)];
            if (l.status == STATUS_HOLE || l.status == STATUS_BEYOND) std::memset(l.bytes, 0, sizeof l.bytes);
        } else if (f.map_.has_filler) {
            l.status = STATUS_OK; std::memcpy(l.bytes, f.map_.filler, LINE_BYTES);
        } else {
            l.status = STATUS_BEYOND;
        }
        latency = MAP_LATENCY;
    } else {
        if ((uint32_t(root) != MEMORY_ROOT || uint32_t(depth) != f.guest.depth) && f.fault.empty()) f.fault = "a read of no file";
        const auto it = f.guest.lines.find(uint64_t(line));
        const bool code = it != f.guest.lines.end() && it->second.status == STATUS_NOT_OWNED;
        f.counts[code ? 0 : 1]++;
        latency = code ? CODE_LATENCY : DATA_LATENCY;
        l = f.guest.line(uint64_t(line));
    }
    const int64_t due = cycle + int64_t(f.between(latency[0], latency[1]));
    f.read_due_ = due;
    r.status = l.status;
    for (unsigned d = 0; d < 16; d++) { uint64_t v = 0; for (unsigned b = 0; b < 8; b++) v |= uint64_t(l.bytes[8 * d + b]) << (8 * b); r.data[d] = v; }
    r.ready_at = due + f.offset_;
    FillCall c; c.cycle = uint64_t(cycle); c.kind = 0; c.root = uint32_t(root); c.depth = uint8_t(depth); c.line = uint64_t(line); c.due = uint64_t(due); c.status = l.status;
    f.calls.push_back(c);
    return r.ready_at;
}

int64_t Fill::write(void* ctx, int64_t t, int32_t root, int depth, int64_t line, const uint64_t* data, int& status) {
    Fill& f = *static_cast<Fill*>(ctx);
    const int64_t cycle = t - f.offset_;
    f.draw_to(cycle);
    if (f.write_due_ > cycle && f.fault.empty()) f.fault = "a write taken with a write outstanding";
    if ((uint32_t(root) != MEMORY_ROOT || uint32_t(depth) != f.guest.depth) && f.fault.empty()) f.fault = "a write of no file";
    const auto it = f.guest.lines.find(uint64_t(line));
    if (it == f.guest.lines.end()) { if (f.fault.empty()) f.fault = "a write-back of a line never read"; }
    else if (it->second.status != STATUS_OK) { if (f.fault.empty()) f.fault = "a write-back of a line not writable"; }
    else for (unsigned d = 0; d < 16; d++) for (unsigned b = 0; b < 8; b++) it->second.bytes[8 * d + b] = uint8_t(data[d] >> (8 * b));
    f.counts[3]++;
    const int64_t due = cycle + int64_t(f.between(WRITE_LATENCY[0], WRITE_LATENCY[1]));
    f.write_due_ = due;
    status = MemSystem::WRITE_OK;
    FillCall c; c.cycle = uint64_t(cycle); c.kind = 1; c.root = uint32_t(root); c.depth = uint8_t(depth); c.line = uint64_t(line); c.due = uint64_t(due); c.status = 0;
    f.calls.push_back(c);
    return due + f.offset_;
}

// ================================================================================================= the run
namespace {

// ---- the segment's headers (the firmware's src/lib.rs, `segment`): their words, by index
enum : unsigned {
    HEADER_MAGIC = 0, HEADER_WINDOWS, HEADER_ROWS, HEADER_RUN_CONFIG, HEADER_TABLES, HEADER_INDEX, HEADER_TEMPLATES_COUNT,
    HEADER_TEMPLATES, HEADER_RECORD, HEADER_CODE, HEADER_READ_LOG, HEADER_WRITE_LOG, HEADER_HASH_MUL, HEADER_HASH_STEP,
    HEADER_HASH_LINES, HEADER_TRACER_RECORD, HEADER_ADVICE, HEADER_ADVICE_ENTRIES, HEADER_EXPECTED, HEADER_FINAL,
};
constexpr uint32_t REPLAY_MAGIC = 0x52504C59, TRACER_MAGIC = 0x54524345;   // "RPLY", "TRCE"
// ---- the scratchpads' layout, the RTL test's, "as the host would": word addresses (5.4)
constexpr uint32_t TABLES = 0x40, INDEX = 0xC40, TEMPLATES = 0xD80;
constexpr uint32_t RECORD = 0x10000, CODE = 0x10400, READ_LOG = 0x20400, WRITE_LOG = 0x40400;
constexpr uint32_t TRACER_RECORD = 0x10000, EXPECTED = 0x10200, FINAL = 0x10400, ADVICE = 0x10800;
// ---- the summary (the master's src/bin/master.rs, Summary and TraceSummary), by word
enum : unsigned {
    SUMMARY_DONE = 0, SUMMARY_FAILURE = 1, SUMMARY_FAILED_PHASE = 2, SUMMARY_KIND = 3, SUMMARY_PHASES = 4, SUMMARY_EXITS = 5,
    SUMMARY_LOADS = 6, SUMMARY_STRAYS = 7, SUMMARY_REPORTS = 8, SUMMARY_REPORT = 9, SUMMARY_TRACER_EXIT = 13, SUMMARY_EXIT = 17,
    SUMMARY_PHASE = SUMMARY_EXIT + 4 * T, TRACE_PHASES = 3, SUMMARY_ERROR_LINES = SUMMARY_PHASE + 3 * TRACE_PHASES,
    SUMMARY_WORDS = SUMMARY_ERROR_LINES + 3,
};
constexpr uint32_t DONE = 0x444F4E45, KIND_TRACE = 2, OP_EXIT = 2;
// ---- the tracer tile's report (its src/bin/tracer.rs, `report`, in its DMEM's .summary), by word: mcycle at its main, at its
// setup's end, at tile 0's ARMED and at its end's checks' end; the expansions served the general way; the Tracer's eight
// statistics counters (17.9.8); each run's START, its rows' end and its done
enum : unsigned { REPORT_STARTED = 0, REPORT_READY = 1, REPORT_ARMED = 2, REPORT_ENDED = 3, REPORT_GENERAL = 4, REPORT_STATS = 8, REPORT_WINDOW = 16 };
constexpr uint64_t POLL_CYCLES = 512;
// The RTL test's cycle 0 is the first after the reset; the simulator's cycles count from 1.
constexpr int64_t CYCLE_OFFSET = 1;

std::vector<uint32_t> file_words(const std::string& path) { return rtl_images::words(rtl_images::read_file(path)); }

void place(mach::Mach& m, unsigned unit, uint32_t word, const std::vector<uint32_t>& words) {
    const std::string name = units::unit_prefix(unit) + "sram";
    units::BigSram& s = m.c.unit<units::BigSram>(name);
    for (size_t i = 0; i < words.size(); i++) s.poke(word + uint32_t(i), words[i]);
    m.c.touched(name);
}
std::vector<uint32_t> words_of(const U128* w, size_t n) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < n; i++) for (uint64_t half : {w[i].lo, w[i].hi}) { out.push_back(uint32_t(half)); out.push_back(uint32_t(half >> 32)); }
    return out;
}
U128 scratchpad_row(mach::Mach& m, unsigned unit, uint32_t word) {
    const units::BigSram& s = m.c.unit<units::BigSram>(units::unit_prefix(unit) + "sram");
    U128 v;
    v.lo = s.peek64(word); v.hi = s.peek64(word + 2);
    return v;
}

struct Fails {
    Run& run;
    const char* name;
    int shown = 0;
    void operator()(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        run.failures++;
        if (shown++ >= 40) return;
        char text[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(text, sizeof text, fmt, ap);
        va_end(ap);
        std::printf("%s: FAILED: %s\n", name, text);
    }
};

}  // namespace

Run run_chip(const Segment& s, const Rtl* rtl, const Options& o) {
    Run run;
    const char* name = s.name.c_str();
    Fails fail{run, name};
    const auto wall0 = std::chrono::steady_clock::now();
    const std::string fw = o.fw, tables_dir = o.tables;
    const std::string master_elf = fw + "trace/master.elf", memory_elf = fw + "memory-tile/memory.elf", tracer_elf = fw + "segment/tracer.elf";
    const rtl_images::Section summary_section = rtl_images::section(master_elf, ".summary");
    const rtl_images::Section report_section = rtl_images::section(tracer_elf, ".summary");
    if (!summary_section.found || summary_section.size / 4 < SUMMARY_WORDS) FATAL("%s: no .summary of %u words", master_elf.c_str(), unsigned(SUMMARY_WORDS));
    if (!report_section.found || report_section.size == 0) FATAL("%s: the tracer tile's image: no .summary", tracer_elf.c_str());

    // ---- the test data, as the host places it
    std::vector<uint32_t> table_rows = file_words(tables_dir + "decode_table.bin");
    const std::vector<uint32_t> rom = file_words(tables_dir + "rom.bin");
    table_rows.insert(table_rows.end(), rom.begin(), rom.end());
    const std::vector<uint32_t> index = file_words(tables_dir + "template_index.bin");
    const std::vector<uint32_t> templates = file_words(tables_dir + "templates.bin");
    const uint32_t template_count = uint32_t(index.size() / 4);
    auto header = [&](uint32_t magic, uint32_t run_config) {
        std::vector<uint32_t> w(HEADER_WORDS, 0);
        w[HEADER_MAGIC] = magic; w[HEADER_WINDOWS] = s.windows; w[HEADER_ROWS] = s.rows; w[HEADER_RUN_CONFIG] = run_config;
        w[HEADER_TABLES] = TABLES; w[HEADER_INDEX] = INDEX; w[HEADER_TEMPLATES_COUNT] = template_count; w[HEADER_TEMPLATES] = TEMPLATES;
        w[HEADER_RECORD] = RECORD; w[HEADER_CODE] = CODE; w[HEADER_READ_LOG] = READ_LOG; w[HEADER_WRITE_LOG] = WRITE_LOG;
        return w;
    };
    // The Tracer's RUN_CONFIG: every stream may go LRU-coded (17.9.1, 17.9.6); the replayer's: the store log and rd_prev.
    std::vector<uint32_t> tracer_header = header(TRACER_MAGIC, s.rows | 7u << 26);
    tracer_header[HEADER_HASH_MUL] = s.map.p1; tracer_header[HEADER_HASH_STEP] = s.map.p3; tracer_header[HEADER_HASH_LINES] = s.map.k;
    tracer_header[HEADER_TRACER_RECORD] = TRACER_RECORD; tracer_header[HEADER_ADVICE] = ADVICE;
    tracer_header[HEADER_ADVICE_ENTRIES] = uint32_t((s.advice.size() + 1) / 2);
    tracer_header[HEADER_EXPECTED] = EXPECTED; tracer_header[HEADER_FINAL] = FINAL;
    const std::vector<uint32_t> replay_header = header(REPLAY_MAGIC, s.rows | 1u << 24 | 1u << 25);
    if (rtl && rtl->present)   // this placement is the RTL test's
        for (unsigned i = 0; i < HEADER_WORDS; i++) {
            if (tracer_header[i] != rtl->tracer_header[i]) fail("the tracer tile's header word %u is 0x%x here, the RTL test's 0x%x", i, tracer_header[i], rtl->tracer_header[i]);
            if (replay_header[i] != rtl->replay_header[i]) fail("the replay header word %u is 0x%x here, the RTL test's 0x%x", i, replay_header[i], rtl->replay_header[i]);
        }

    // ---- the chip, its images and the segment, before the first cycle
    mach::MachConfig mc = mach::Mach::chip_cfg(-1);
    mc.parts = o.parts; mc.threads = o.parts;
    mach::Mach m(mc);
    m.place_elf(units::ID_MASTER, master_elf);
    m.place_elf(units::ID_MEMORY, memory_elf);
    auto place_tables = [&](unsigned unit) { place(m, unit, TABLES, table_rows); place(m, unit, INDEX, index); place(m, unit, TEMPLATES, templates); };
    place(m, units::ID_TRACER, 0, tracer_header);
    place_tables(units::ID_TRACER);
    place(m, units::ID_TRACER, TRACER_RECORD, words_of(s.start, RECORD_WORDS));
    std::vector<uint32_t> advice;
    for (uint64_t v : s.advice) { advice.push_back(uint32_t(v)); advice.push_back(uint32_t(v >> 32)); }
    advice.resize((advice.size() + 3) / 4 * 4, 0);
    place(m, units::ID_TRACER, ADVICE, advice);
    place(m, units::ID_TRACER, EXPECTED, words_of(s.end, RECORD_WORDS));
    for (unsigned tile = 0; tile < T; tile++) {
        place(m, tile, 0, replay_header);
        if (tile < s.windows) place_tables(tile);
    }
    // the memory system on the tracer tile's fill port, and every compute tile's row port's consumer
    Fill fill(s.guest, s.map, s.seed + 1, CYCLE_OFFSET);
    m.c.unit<units::PeriphSlot>(units::unit_prefix(units::ID_TRACER) + "slot").s.tracer(1).fill_port = &fill.port;
    run.rows.resize(T); run.row_cycles.resize(T);
    std::vector<units::Replayer*> replayer(T);
    for (unsigned tile = 0; tile < T; tile++) {
        const size_t capacity = tile < s.windows ? size_t(s.rows) + 64 : 64;   // room for a tile's rows past its window's, to report them
        run.rows[tile].assign(capacity * ROW_LONGS, 0);
        run.row_cycles[tile].assign(capacity, 0);
        replayer[tile] = &m.c.unit<units::PeriphSlot>(units::unit_prefix(tile) + "slot").s.replayer(1);
        replayer[tile]->set_row_buffer(run.rows[tile].data(), capacity);
        replayer[tile]->set_row_times(run.row_cycles[tile].data());
    }

    // ---- the run: to the master's done word
    const uint32_t done_address = summary_section.address + 4 * SUMMARY_DONE;
    while (!run.finished && run.cycles < s.cycle_limit) {
        m.c.run(POLL_CYCLES);
        run.cycles += POLL_CYCLES;
        run.finished = m.word(units::ID_MASTER, done_address) == DONE;
    }
    for (unsigned tile = 0; tile < T; tile++) {
        const size_t sunk = replayer[tile]->rows_sunk();
        run.rows[tile].resize(sunk * ROW_LONGS);
        run.row_cycles[tile].resize(sunk);
        for (int64_t& c : run.row_cycles[tile]) c -= CYCLE_OFFSET;
    }
    run.summary.resize(SUMMARY_WORDS);
    for (unsigned i = 0; i < SUMMARY_WORDS; i++) run.summary[i] = m.word(units::ID_MASTER, summary_section.address + 4 * i);
    run.report.resize(report_section.size / 4);
    for (size_t i = 0; i < run.report.size(); i++) run.report[i] = m.word(units::ID_TRACER, report_section.address + uint32_t(4 * i));
    const std::vector<uint32_t>& summary = run.summary;
    auto phase = [&](unsigned i, unsigned k) { return summary[SUMMARY_PHASE + 3 * i + k]; };
    const uint32_t* tracer_exit = &summary[SUMMARY_TRACER_EXIT];
    std::printf("%s: summary: failure 0x%x (in phase %u), %u exits, %u loads, %u stray, %u reports (the first [%x, %x, %x, %x]), "
                "the tracer tile's exit [%x, %x, %x, %x]; %llu cycles from the reset\n", name, summary[SUMMARY_FAILURE],
                summary[SUMMARY_FAILED_PHASE], summary[SUMMARY_EXITS], summary[SUMMARY_LOADS], summary[SUMMARY_STRAYS],
                summary[SUMMARY_REPORTS], summary[SUMMARY_REPORT], summary[SUMMARY_REPORT + 1], summary[SUMMARY_REPORT + 2],
                summary[SUMMARY_REPORT + 3], tracer_exit[0], tracer_exit[1], tracer_exit[2], tracer_exit[3], (unsigned long long)run.cycles);
    const unsigned phases = summary[SUMMARY_PHASES];
    if (phases == TRACE_PHASES) {
        run.load = phase(0, 1); run.trace = phase(1, 1) - phase(0, 2); run.replay = phase(2, 1) - phase(1, 2); run.tracer_own = tracer_exit[3];
        run.released = phase(1, 2); run.ended = phase(2, 1);
        std::printf("%s: the load %u cycles from the master's start; the trace and its windows' delivery, from the load's release to "
                    "the windows' landing, %u cycles; the replay, from that release to the passes' end, %u cycles; the Tracer's "
                    "segment, its first START to its last done, %u cycles\n", name, run.load, run.trace, run.replay, run.tracer_own);
    }
    if (!o.quiet) {
        std::printf("%s: the rows each tile sent:", name);
        for (unsigned tile = 0; tile < s.windows; tile++) std::printf(" %zu", run.rows[tile].size() / ROW_LONGS);
        std::printf("\n");
    }

    // ---- the summary
    if (!run.finished) fail("no summary in %llu cycles", (unsigned long long)s.cycle_limit);
    if (summary[SUMMARY_KIND] != KIND_TRACE) fail("the run's kind %u", summary[SUMMARY_KIND]);
    if (summary[SUMMARY_FAILURE] != 0) fail("the master failed (0x%x) after release %u", summary[SUMMARY_FAILURE], summary[SUMMARY_FAILED_PHASE]);
    if (phases != TRACE_PHASES) fail("%u releases: the load's, the windows' landing's and the passes' end's", phases);
    if (summary[SUMMARY_REPORTS] != 0) fail("%u error reports", summary[SUMMARY_REPORTS]);
    if (summary[SUMMARY_STRAYS] != 0) fail("%u stray requests", summary[SUMMARY_STRAYS]);
    if (summary[SUMMARY_EXITS] != T) fail("the compute tiles' exits: %u", summary[SUMMARY_EXITS]);
    if (summary[SUMMARY_LOADS] != T + 1) fail("the LOADs after the exits, the tracer tile's among them: %u", summary[SUMMARY_LOADS]);
    auto op = [](uint32_t w0) { return (w0 >> 10) & 0xFF; };
    if ((tracer_exit[0] & 0x7F) != units::ID_TRACER || op(tracer_exit[0]) != OP_EXIT) fail("the tracer tile's exit's word 0 0x%x", tracer_exit[0]);
    if (tracer_exit[1] != 0) fail("the tracer tile's status %u (src/bin/tracer.rs, Status)", tracer_exit[1]);
    if (tracer_exit[2] != 0) fail("the Tracer's ERRORS, and the tracer tile's from bit 16: 0x%x", tracer_exit[2]);
    for (unsigned tile = 0; tile < T; tile++) {
        const uint32_t* exit = &summary[SUMMARY_EXIT + 4 * tile];
        const uint32_t rows_run = tile < s.windows ? s.rows : 0;
        if ((exit[0] & 0x7F) != tile || op(exit[0]) != OP_EXIT) fail("tile %u's exit's word 0 0x%x", tile, exit[0]);
        if (exit[1] != 0 || exit[2] != 0 || exit[3] != rows_run)
            fail("tile %u's status %u, its ERRORS (the replayer's, its own) 0x%x and ROWS_RUN %u (its window's: %u)", tile, exit[1], exit[2], exit[3], rows_run);
    }

    // ---- every window's rows, against the segment's
    for (unsigned tile = 0; tile < T; tile++) {
        const size_t got = run.rows[tile].size() / ROW_LONGS;
        if (tile >= s.windows) { if (got != 0) fail("tile %u, with no window, sent %zu rows", tile, got); continue; }
        const Window& w = s.window[tile];
        if (got != w.row_count()) fail("tile %u sent %zu rows, its window has %zu", tile, got, w.row_count());
        for (size_t row = 0; row < got && row < w.row_count(); row++)
            if (std::memcmp(&run.rows[tile][row * ROW_LONGS], &w.rows[row * ROW_LONGS], 8 * ROW_LONGS) != 0) {
                for (unsigned k = 0; k < ROW_LONGS; k++)
                    if (run.rows[tile][row * ROW_LONGS + k] != w.rows[row * ROW_LONGS + k]) {
                        fail("tile %u's row %zu: word %u's %s half is 0x%llx, the segment's 0x%llx", tile, row, k / 2, k % 2 ? "high" : "low",
                             (unsigned long long)run.rows[tile][row * ROW_LONGS + k], (unsigned long long)w.rows[row * ROW_LONGS + k]);
                        break;
                    }
                break;
            }
    }

    // ---- every window's delivery: its record and its streams, in its tile's replay region
    static const char* const kStream[3] = {"code", "read-log", "write-log"};
    for (unsigned tile = 0; tile < s.windows; tile++) {
        const Window& w = s.window[tile];
        for (unsigned word = 0; word < RECORD_WORDS; word++) {
            const U128 got = scratchpad_row(m, tile, RECORD + 4 * word);
            if (got != w.record[word]) fail("tile %u's record word %u is %016llx%016llx, the segment's %016llx%016llx", tile, word,
                                            (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)w.record[word].hi, (unsigned long long)w.record[word].lo);
        }
        const uint32_t base[3] = {CODE, READ_LOG, WRITE_LOG};
        for (unsigned stream = 0; stream < 3; stream++) {
            const Stream& st = w.streams[stream];
            for (size_t i = 0; i < st.entries.size(); i++) {
                U128 got = scratchpad_row(m, tile, base[stream] + uint32_t(4 * i));
                const uint64_t end = st.length - uint64_t(i) * 128;        // the bits past the stream's end are of any value
                if (end < 64) { got.hi = 0; got.lo &= (uint64_t(1) << end) - 1; }
                else if (end < 128) got.hi &= (uint64_t(1) << (end - 64)) - 1;
                if (got != st.entries[i]) { fail("tile %u's %s entry %zu is %016llx%016llx, the segment's %016llx%016llx", tile, kStream[stream], i,
                                                 (unsigned long long)got.hi, (unsigned long long)got.lo, (unsigned long long)st.entries[i].hi, (unsigned long long)st.entries[i].lo); break; }
            }
        }
    }

    // ---- the chip-level self-check: each replayer's end state, the next window's start as the Tracer sent it
    U128 last[RECORD_WORDS];
    for (unsigned word = 0; word < RECORD_WORDS; word++) {
        last[word] = scratchpad_row(m, units::ID_TRACER, FINAL + 4 * word);
        if (last[word] != s.end[word]) fail("the Tracer's last record, word %u: %016llx%016llx, the segment's %016llx%016llx", word, (unsigned long long)last[word].hi,
                                            (unsigned long long)last[word].lo, (unsigned long long)s.end[word].hi, (unsigned long long)s.end[word].lo);
    }
    for (unsigned tile = 0; tile < s.windows; tile++)
        for (unsigned i = 0; i < 64; i++) {
            const U128 next = tile + 1 < s.windows ? scratchpad_row(m, tile + 1, RECORD + 4 * (2 + i)) : last[2 + i];
            U128 image;
            image.lo = replayer[tile]->reg(2 * i); image.hi = replayer[tile]->reg(2 * i + 1);
            if (image != next) { fail("tile %u's registers %u and %u after its pass: %016llx, %016llx; the next window's start %016llx, %016llx", tile, 2 * i, 2 * i + 1,
                                      (unsigned long long)image.lo, (unsigned long long)image.hi, (unsigned long long)next.lo, (unsigned long long)next.hi); break; }
        }

    // ---- the fill port and the error lines
    if (!fill.fault.empty()) fail("the fill port: %s", fill.fault.c_str());
    for (unsigned i = 0; i < 4; i++) run.fill_counts[i] = fill.counts[i];
    std::printf("%s: the fill port: %llu code lines, %llu data lines, %llu map lines read, %llu lines written back\n", name,
                (unsigned long long)fill.counts[0], (unsigned long long)fill.counts[1], (unsigned long long)fill.counts[2], (unsigned long long)fill.counts[3]);
    for (unsigned w = 0; w < 3; w++) if (summary[SUMMARY_ERROR_LINES + w] != 0) fail("ERROR_LINES word %u at the run's end 0x%x: a tile's error line rose", w, summary[SUMMARY_ERROR_LINES + w]);
    if (m.c.toast()) fail("the toast pin: the master's error line rose");
    if (s.misses) {
        if (fill.counts[0] <= 128) fail("the code lines read: the instruction cache never missed past its 128 lines");
        if (fill.counts[2] <= 3 * uint64_t(s.windows)) fail("the map lines read: the jump LRU seldom missed");
        if (fill.counts[3] == 0) fail("no line written back");
    }

    // ---- the cycles, against the RTL chip's
    if (rtl && rtl->present) {
        if (run.cycles != rtl->cycles) fail("%llu cycles from the reset, the RTL chip's %llu", (unsigned long long)run.cycles, (unsigned long long)rtl->cycles);
        if (rtl->summary.size() != summary.size()) fail("the RTL's summary has %zu words", rtl->summary.size());
        else for (unsigned i = 0; i < summary.size(); i++)
            if (summary[i] != rtl->summary[i]) fail("the summary's word %u is %u (0x%x), the RTL chip's %u (0x%x)", i, summary[i], summary[i], rtl->summary[i], rtl->summary[i]);
        if (rtl->report.size() != run.report.size()) fail("the RTL's report has %zu words, the tracer tile's %zu", rtl->report.size(), run.report.size());
        else for (size_t i = 0; i < run.report.size(); i++)   // the words the image writes (tracer.rs, `report`): the others are as the memory started
            if ((i <= REPORT_GENERAL || (i >= REPORT_STATS && i < REPORT_WINDOW + 3 * size_t(s.windows))) && run.report[i] != rtl->report[i]) fail("the tracer tile's report's word %zu is %u, the RTL chip's %u", i, run.report[i], rtl->report[i]);
        for (unsigned i = 0; i < 4; i++) if (fill.counts[i] != rtl->fill_counts[i]) fail("the fill port's count %u is %llu, the RTL chip's %llu", i, (unsigned long long)fill.counts[i], (unsigned long long)rtl->fill_counts[i]);
        if (fill.calls.size() != rtl->fill.size()) fail("%zu fill requests, the RTL chip's %zu", fill.calls.size(), rtl->fill.size());
        for (size_t i = 0; i < fill.calls.size() && i < rtl->fill.size(); i++) {
            const FillCall &a = fill.calls[i], &b = rtl->fill[i];
            if (a.cycle != b.cycle || a.kind != b.kind || a.root != b.root || a.depth != b.depth || a.line != b.line || a.due != b.due || a.status != b.status) {
                fail("fill request %zu: a %s of line %llu (root 0x%x) in cycle %llu, answered in %llu, status %u; the RTL chip's: a %s of line %llu (root 0x%x) in cycle "
                     "%llu, answered in %llu, status %u", i, a.kind ? "write" : "read", (unsigned long long)a.line, a.root, (unsigned long long)a.cycle, (unsigned long long)a.due, a.status,
                     b.kind ? "write" : "read", (unsigned long long)b.line, b.root, (unsigned long long)b.cycle, (unsigned long long)b.due, b.status);
                break;
            }
        }
        uint64_t rows_timed = 0;
        for (unsigned tile = 0; tile < T; tile++) {
            const auto& got = run.row_cycles[tile];
            const auto& want = rtl->row_cycles[tile];
            if (got.size() != want.size()) { fail("tile %u: %zu rows timed, the RTL chip's %zu", tile, got.size(), want.size()); continue; }
            for (size_t row = 0; row < got.size(); row++) {
                if (uint64_t(got[row]) != want[row]) { fail("tile %u's row %zu left its port in cycle %lld, the RTL chip's in %u", tile, row, (long long)got[row], want[row]); break; }
                rows_timed++;
            }
        }
        if (run.failures == 0)
            std::printf("%s: the cycles are the RTL chip's: %llu from the reset, the summary's %zu words, the tracer tile's report's %zu, %zu fill "
                        "requests and %llu rows' cycles out of the row ports\n", name, (unsigned long long)run.cycles, summary.size(),
                        run.report.size(), fill.calls.size(), (unsigned long long)rows_timed);
    }
    run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
    std::printf("%s: %s in %llu cycles, %.1f s of simulation, %.0f cycles a second\n", name, run.failures ? "FAILED" : "passed",
                (unsigned long long)run.cycles, run.seconds, double(run.cycles) / run.seconds);
    return run;
}

ReplayCycles replay_cycles(const Run& r, unsigned windows) {
    ReplayCycles c;
    for (unsigned tile = 0; tile < windows && tile < r.row_cycles.size(); tile++) {
        const auto& cycles = r.row_cycles[tile];
        if (cycles.empty()) continue;
        c.rows += cycles.size();
        c.cycles += uint64_t(cycles.back()) - r.released;
        c.to_first += uint64_t(cycles.front()) - r.released;
    }
    return c;
}

}  // namespace segment
