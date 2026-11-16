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

// ---- the segment's layouts in the scratchpads, v1.35's (fw/tracer/tracer_layout.h, fw/replay/replay_layout.h): their
// headers' words, by index
enum : unsigned {
    TL_MAGIC_W = 0, TL_VERSION_W, TL_TABLES, TL_INDEX, TL_NTEMPLATES, TL_TEMPLATES, TL_RECORD, TL_OWN_RECORD, TL_EXPECT,
    TL_ADVICE, TL_ADVICE_N, TL_RUN_CONFIG, TL_HASH_MUL, TL_HASH_STEP, TL_HASH_LINES, TL_WINDOWS, TL_T_RECORD, TL_T_CODE,
    TL_T_RLOG, TL_T_WLOG, TL_FIRST_TILE, TL_WORDS = 32,
};
enum : unsigned {
    RL_MAGIC_W = 0, RL_VERSION_W, RL_TABLES, RL_INDEX, RL_NTEMPLATES, RL_TEMPLATES, RL_RECORD, RL_CODE, RL_CODE_N, RL_RLOG,
    RL_RLOG_N, RL_WLOG, RL_WLOG_N, RL_PASSES, RL_RUN_CONFIG, RL_WAIT, RL_WORDS = 16,
};
constexpr uint32_t TL_MAGIC = 0x52435254, TL_VERSION = 1, RL_MAGIC = 0x594C5052, RL_VERSION = 5;   // "TRCR", "RPLY"
constexpr uint32_t RL_N_FROM_RECORD = 0xFFFFFFFF, RL_WAIT_BARRIERS = 1, RL_WAIT_HANDSHAKE = 2;
// ---- the scratchpads' layout, as the host would place it: word addresses (5.4)
constexpr uint32_t TABLES = 0x40, INDEX = 0xC40, TEMPLATES = 0xD80;
constexpr uint32_t RECORD = 0x10000, CODE = 0x10400, READ_LOG = 0x20400, WRITE_LOG = 0x40400;
constexpr uint32_t TRACER_RECORD = 0x10000, EXPECTED = 0x10200, FINAL = 0x10400, ADVICE = 0x10800;
// ---- C&C 1's program (fw/cc/cc1.c): its commands, boot_desc's words, its mailbox; the master's image rows' places
// (its three commands each count in its mailbox, CC_EXIT before it exits)
constexpr uint32_t CC_SEGMENT = 0x54, CC_WAIT = 0x52, CC_EXIT = 0x58;
enum : unsigned { BOOT_MAGIC = 0, BOOT_UNIT = 1, IMAGE_WORDS = 6, BOOT_GO_LATENCY = 19, BOOT_SCRIPT = 21 };
enum : unsigned { MBOX_COMMANDS = 0, MBOX_RELEASED = 2, MBOX_BOOT = 3, MBOX_LOADS = 4, MBOX_RESULTS = 6, MBOX_OTHERS = 7 };
constexpr uint32_t BOOT_DESC_MAGIC = 0xB007DE5C, EXIT_DONE = 0x45584954;
constexpr uint32_t REPLAY_PMEM = 0x0000, REPLAY_DMEM = 0x2000, TRACER_PMEM = 0x4000, TRACER_DMEM = 0x6000, COUNT_ROW = 0x8000;
// ---- the programs' results: replay_ctl.c's rp_results and tracer_ctl.c's tr_results, by index
enum : unsigned { RP_STATUS = 0, RP_PASSES = 1, RP_CONDITIONS = 2, RP_MISSING = 5 };
enum : unsigned { TR_STATUS = 0, TR_WINDOWS = 1, TR_MISSING = 5, TR_ERRORS = 6, TR_ADVICE_LEFT = 7, TR_END_DIFF = 8, TR_CONDITIONS = 9, TR_STRAYS = 10 };
constexpr uint64_t POLL_CYCLES = 512;
// The RTL test's cycle 0 is the first after the reset; the simulator's cycles count from 1.
constexpr int64_t CYCLE_OFFSET = 1;

std::vector<uint32_t> file_words(const std::string& path) { return rtl_images::words(rtl_images::read_file(path)); }

// An image's rows, as cc1.c's boot streams them and v1.35's harness took them (its .code.bin and .data.bin): its .text
// from IMEM's base, its .data from DMEM_FIRST, each padded to whole rows (.bss is not loaded: crt0.S zeroes it).
struct ImageRows { std::vector<uint32_t> pmem, dmem; };
ImageRows image_rows(const std::string& elf) {
    ImageRows r;
    const rtl_images::Section text = rtl_images::section(elf, ".text"), data = rtl_images::section(elf, ".data");
    if (!text.found || text.address != units::IMEM_BASE || (data.found && data.address != 0x10)) FATAL("%s: not an image of fw/app.ld", elf.c_str());
    r.pmem = rtl_images::words(text.data);
    if (data.found) r.dmem = rtl_images::words(data.data);
    r.pmem.resize((r.pmem.size() + 3) / 4 * 4, 0);
    r.dmem.resize((r.dmem.size() + 3) / 4 * 4, 0);
    return r;
}

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
    (void)rtl;                                                         // the RTL's figures: no longer compared (header)
    Run run;
    const char* name = s.name.c_str();
    Fails fail{run, name};
    const auto wall0 = std::chrono::steady_clock::now();
    const std::string fw = o.fw ? o.fw : rtl_images::fw_images(), tables_dir = o.tables;
    const std::string cc1_elf = fw + "cc/cc1.elf", memory_elf = fw + "cc/memory.elf";
    const std::string tracer_elf = fw + "tracer/tracer_ctl.elf", replay_elf = fw + "replay/replay_ctl.elf";
    const uint32_t boot_desc = rtl_images::elf_symbol(cc1_elf, "boot_desc"), mbox = rtl_images::elf_symbol(cc1_elf, "mbox");
    const uint32_t exit_word = rtl_images::elf_symbol(cc1_elf, "sim_exit_word");
    const uint32_t tr_results = rtl_images::elf_symbol(tracer_elf, "tr_results"), rp_results = rtl_images::elf_symbol(replay_elf, "rp_results");

    // ---- the test data, as the host places it
    std::vector<uint32_t> table_rows = file_words(tables_dir + "decode_table.bin");
    const std::vector<uint32_t> rom = file_words(tables_dir + "rom.bin");
    table_rows.insert(table_rows.end(), rom.begin(), rom.end());
    const std::vector<uint32_t> index = file_words(tables_dir + "template_index.bin");
    const std::vector<uint32_t> templates = file_words(tables_dir + "templates.bin");
    const uint32_t template_count = uint32_t(index.size() / 4);
    // The Tracer's RUN_CONFIG: every stream may go LRU-coded (17.9.1, 17.9.6); the replayer's: the store log and rd_prev.
    std::vector<uint32_t> tracer_header(TL_WORDS, 0);
    tracer_header[TL_MAGIC_W] = TL_MAGIC; tracer_header[TL_VERSION_W] = TL_VERSION;
    tracer_header[TL_TABLES] = TABLES; tracer_header[TL_INDEX] = INDEX; tracer_header[TL_NTEMPLATES] = template_count;
    tracer_header[TL_TEMPLATES] = TEMPLATES; tracer_header[TL_RECORD] = TRACER_RECORD; tracer_header[TL_OWN_RECORD] = FINAL;
    tracer_header[TL_EXPECT] = EXPECTED; tracer_header[TL_ADVICE] = ADVICE; tracer_header[TL_ADVICE_N] = uint32_t(s.advice.size());
    tracer_header[TL_RUN_CONFIG] = s.rows | 7u << 26;
    tracer_header[TL_HASH_MUL] = s.map.p1; tracer_header[TL_HASH_STEP] = s.map.p3; tracer_header[TL_HASH_LINES] = s.map.k;
    tracer_header[TL_WINDOWS] = s.windows; tracer_header[TL_T_RECORD] = RECORD; tracer_header[TL_T_CODE] = CODE;
    tracer_header[TL_T_RLOG] = READ_LOG; tracer_header[TL_T_WLOG] = WRITE_LOG; tracer_header[TL_FIRST_TILE] = 0;
    std::vector<uint32_t> replay_header(RL_WORDS, 0);
    replay_header[RL_MAGIC_W] = RL_MAGIC; replay_header[RL_VERSION_W] = RL_VERSION;
    replay_header[RL_TABLES] = TABLES; replay_header[RL_INDEX] = INDEX; replay_header[RL_NTEMPLATES] = template_count;
    replay_header[RL_TEMPLATES] = TEMPLATES; replay_header[RL_RECORD] = RECORD;
    replay_header[RL_CODE] = CODE; replay_header[RL_RLOG] = READ_LOG; replay_header[RL_WLOG] = WRITE_LOG;
    replay_header[RL_CODE_N] = replay_header[RL_RLOG_N] = replay_header[RL_WLOG_N] = RL_N_FROM_RECORD;
    replay_header[RL_PASSES] = 1; replay_header[RL_RUN_CONFIG] = s.rows | 1u << 24 | 1u << 25;

    // ---- the chip, its images and the segment, before the first cycle
    mach::MachConfig mc = mach::Mach::chip_cfg(-1);
    mc.parts = o.parts; mc.threads = o.parts;
    mach::Mach m(mc);
    m.place_elf(units::ID_MASTER, cc1_elf);
    m.place_elf(units::ID_MEMORY, memory_elf);
    // C&C 1's images in its scratchpad, its boot_desc and its commands: CC_SEGMENT, the segment's two barriers; CC_WAIT,
    // the 64 compute tiles' results and the tracer tile's; CC_EXIT
    const ImageRows replay_rows = image_rows(replay_elf), tracer_rows = image_rows(tracer_elf);
    place(m, units::ID_MASTER, REPLAY_PMEM, replay_rows.pmem); place(m, units::ID_MASTER, REPLAY_DMEM, replay_rows.dmem);
    place(m, units::ID_MASTER, TRACER_PMEM, tracer_rows.pmem); place(m, units::ID_MASTER, TRACER_DMEM, tracer_rows.dmem);
    place(m, units::ID_MASTER, COUNT_ROW, {0, 0, 0, 0});
    std::vector<uint32_t> desc(BOOT_SCRIPT + 4 * 3, 0);
    desc[BOOT_MAGIC] = BOOT_DESC_MAGIC;
    const uint32_t units_images[2][IMAGE_WORDS] = {
        {TRACER_PMEM, uint32_t(tracer_rows.pmem.size() / 4), TRACER_DMEM, uint32_t(tracer_rows.dmem.size() / 4), COUNT_ROW, 0},   // C&C 3's: the tracer tile
        {REPLAY_PMEM, uint32_t(replay_rows.pmem.size() / 4), REPLAY_DMEM, uint32_t(replay_rows.dmem.size() / 4), COUNT_ROW, 0},   // the tiles
    };
    for (unsigned u = 0; u < 2; u++) for (unsigned i = 0; i < IMAGE_WORDS; i++) desc[BOOT_UNIT + (u + 1) * IMAGE_WORDS + i] = units_images[u][i];
    desc[BOOT_GO_LATENCY] = 1;
    desc[BOOT_SCRIPT] = CC_SEGMENT << 24; desc[BOOT_SCRIPT + 4] = CC_WAIT << 24 | (T + 1); desc[BOOT_SCRIPT + 8] = CC_EXIT << 24;
    m.place(units::ID_MASTER, boot_desc, desc);
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
        replay_header[RL_WAIT] = tile < s.windows ? RL_WAIT_HANDSHAKE : RL_WAIT_BARRIERS;
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

    // ---- the run: to C&C 1's exit word
    while (!run.finished && run.cycles < s.cycle_limit) {
        m.c.run(POLL_CYCLES);
        run.cycles += POLL_CYCLES;
        run.finished = m.word(units::ID_MASTER, exit_word) == EXIT_DONE;
    }
    for (unsigned tile = 0; tile < T; tile++) {
        const size_t sunk = replayer[tile]->rows_sunk();
        run.rows[tile].resize(sunk * ROW_LONGS);
        run.row_cycles[tile].resize(sunk);
        for (int64_t& c : run.row_cycles[tile]) c -= CYCLE_OFFSET;
    }
    auto mb = [&](unsigned i) { return m.word(units::ID_MASTER, mbox + 4 * i); };
    auto tr = [&](unsigned i) { return m.word(units::ID_TRACER, tr_results + 4 * i); };
    run.summary.resize(8);
    for (unsigned i = 0; i < 8; i++) run.summary[i] = mb(i);
    run.report.resize(16);
    for (unsigned i = 0; i < 16; i++) run.report[i] = tr(i);
    run.released = mb(MBOX_RELEASED);                                   // the last GO: the windows' landing's release
    std::printf("%s: C&C 1: %u commands, boot %u, %u LOADs, %u results, %u others, exit 0x%x; the tracer tile: status %u, %u windows, "
                "ERRORS 0x%x, advice left %u, end differs at %u, %u strays; %llu cycles from the reset\n", name, mb(MBOX_COMMANDS),
                mb(MBOX_BOOT), mb(MBOX_LOADS), mb(MBOX_RESULTS), mb(MBOX_OTHERS), m.word(units::ID_MASTER, exit_word + 4), tr(TR_STATUS),
                tr(TR_WINDOWS), tr(TR_ERRORS), tr(TR_ADVICE_LEFT), tr(TR_END_DIFF), tr(TR_STRAYS), (unsigned long long)run.cycles);
    if (!o.quiet) {
        std::printf("%s: the rows each tile sent:", name);
        for (unsigned tile = 0; tile < s.windows; tile++) std::printf(" %zu", run.rows[tile].size() / ROW_LONGS);
        std::printf("\n");
    }

    // ---- C&C 1's, the tracer tile's and every compute tile's results
    if (!run.finished) fail("no exit in %llu cycles", (unsigned long long)s.cycle_limit);
    if (mb(MBOX_COMMANDS) != 3 || mb(MBOX_BOOT) != 2) fail("C&C 1 completed %u commands, its boot %u", mb(MBOX_COMMANDS), mb(MBOX_BOOT));
    if (mb(MBOX_RESULTS) != T + 1) fail("%u results, of 64 compute tiles and the tracer tile", mb(MBOX_RESULTS));
    if (mb(MBOX_LOADS) != 2 * (T + 1)) fail("%u LOADs: the boot's 65 and one after each result", mb(MBOX_LOADS));
    if (mb(MBOX_OTHERS) != 0) fail("%u other requests at C&C 1", mb(MBOX_OTHERS));
    if (tr(TR_STATUS) != 1) fail("the tracer tile's status %u (tracer_layout.h; the word with no template 0x%x)", tr(TR_STATUS), tr(TR_MISSING));
    if (tr(TR_WINDOWS) != s.windows) fail("the tracer tile ran %u windows of %u", tr(TR_WINDOWS), s.windows);
    if (tr(TR_ERRORS) != 0 || tr(TR_CONDITIONS) != 0) fail("the Tracer's ERRORS 0x%x, the tracer tile's 0x%x", tr(TR_ERRORS), tr(TR_CONDITIONS));
    if (tr(TR_ADVICE_LEFT) != 0 || tr(TR_END_DIFF) != 0) fail("the advice left %u; the end state differs at word %u", tr(TR_ADVICE_LEFT), tr(TR_END_DIFF));
    if (tr(TR_STRAYS) != 0) fail("%u requests at the tracer tile that are no handshake", tr(TR_STRAYS));
    for (unsigned tile = 0; tile < T; tile++) {
        const uint32_t status = m.word(tile, rp_results + 4 * RP_STATUS), passes = m.word(tile, rp_results + 4 * RP_PASSES);
        const uint32_t conditions = m.word(tile, rp_results + 4 * RP_CONDITIONS), rows_run = replayer[tile]->rows_done();
        const uint32_t want_passes = tile < s.windows ? 1 : 0, want_rows = tile < s.windows ? s.rows : 0;
        if (status != 1 || passes != want_passes || conditions != 0 || rows_run != want_rows)
            fail("tile %u's status %u (the word with no template 0x%x), passes %u, ERRORS 0x%x, ROWS_RUN %u (its window's: %u)", tile, status,
                 m.word(tile, rp_results + 4 * RP_MISSING), passes, conditions, rows_run, want_rows);
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
    if (m.c.toast()) fail("the toast pin: the master's error line rose");
    if (s.misses) {
        if (fill.counts[0] <= 128) fail("the code lines read: the instruction cache never missed past its 128 lines");
        if (fill.counts[2] <= 3 * uint64_t(s.windows)) fail("the map lines read: the jump LRU seldom missed");
        if (fill.counts[3] == 0) fail("no line written back");
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
