// stageb.cpp (WORKER25, P7b; inventory E3): v1.3's test/StageB.java on the C++ machine, line for line (stageb.h).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"   // libstdc++'s std::regex in gcc 13's debug build (as suite_replayer)
#endif
#include "stageb.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "harness.h"
#include "replay_driver.h"
#include "replay_report.h"
#include "replay_t.h"
#include "replay_window.h"
#include "sha256.h"
#include "units/chip.h"
#include "units/event_counts.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/replayer_alu.h"
#include "units/row_core.h"
#include "units/stream_word.h"

// ReplayerUnitTests.programWindow and r(): W15's port, compiled in unchanged (as suite_replayer does).
#undef TEST
#define TEST(name) [[maybe_unused]] static void name()
namespace w15unit {
#include "test_replayer.cpp"
}

namespace stageb {

using stagea::fmt;
using units::Replayer;

namespace {

constexpr uint32_t HDR = 0x00000, TABLES = 0x00040, INDEX = 0x00C40, RECORD = 0x00E40, TEMPLATES = 0x58000, CODE = 0x08000, RLOG = 0x18000,
                   WLOG = 0x38000, LAYOUT_VERSION = 4;
constexpr uint32_t PMEM_BASE = 0x01000000, CMD_JUMP = 0x4A;   // Registers.PMEM_BASE, CMD_JUMP

// StageB.put: bytes into tile p's big SRAM from chunk c, one 32-bit word per chunk (padded to 16 bytes).
void put(mach::TileRef p, uint32_t c, std::vector<uint8_t> b) {
    b.resize((b.size() + 15) & ~size_t(15), 0);
    for (size_t i = 0; i < b.size() / 4; i++)
        p.sram_poke(c + uint32_t(i), uint32_t(b[4 * i]) | uint32_t(b[4 * i + 1]) << 8 | uint32_t(b[4 * i + 2]) << 16 | uint32_t(b[4 * i + 3]) << 24);
}
std::vector<uint8_t> bytes(const std::vector<uint64_t>& w) {   // StageB.bytes
    std::vector<uint8_t> b(8 * w.size());
    for (size_t i = 0; i < w.size(); i++) for (int k = 0; k < 8; k++) b[8 * i + size_t(k)] = uint8_t(w[i] >> (8 * k));
    return b;
}
std::string hex32(uint32_t v) { return fmt("%x", v); }   // Integer.toHexString

// StageB.head and cut.
std::string head(const Replayer& r, unsigned l) { return rreport::head(r, l); }
std::string cut(const std::string& s) { return s.size() <= 600 ? s : s.substr(0, 600) + " ... (" + std::to_string(s.size()) + " chars)"; }
std::string one_line(std::string s) { for (char& c : s) if (c == '\n') c = '\0'; std::string o; for (char c : s) { if (c) o += c; else o += "; "; } return o; }

// The machine of a Stage B run: Config.machine(), periphType 0, the replayer on tile 0 (testPeripheral).
struct Rig {
    std::unique_ptr<mach::Chip> c;
    Replayer* r = nullptr;
    std::vector<std::vector<uint64_t>> got;   // every row the replayer emits (rowListener)
    // StageB.Watch
    int64_t last_row = -1, max_gap = 0, norow = 50000;
    bool stuck = false;
    Rig(const Options& o, bool lone) {
        mach::MachConfig k = mach::MachConfig::machine();
        k.periph_type = 0;
        k.test_periph_tile = 0;
        k.shuffle_seed = o.shuffle;
        k.step_all = o.step_all;
        k.instance_major = o.instance_major;
        if (lone) for (int t = 1; t < 64; t++) k.parked.push_back(t);
        if (o.logq != Replayer::LOG_QUEUE_ENTRIES)
            FATAL("stageb: log queues of %u entries need the machine's replayer log-queue field (W20's Config fields; not on main yet)", o.logq);
        c = std::make_unique<mach::Chip>(k);
        r = &c->unit<units::PeriphSlot>(units::tile_unit(0, "slot")).s.replayer(1);
        norow = o.norow;
        c->on_row(0, [this](const uint64_t* rec, int64_t cycle) {
            got.emplace_back(rec, rec + Replayer::REC_LONGS);
            row(cycle);
        });
    }
    // W20's Chip::on_row points the replayer's row buffer at the hook's vector, and the slot frees the replayer's
    // buffer at destruction (it allocated it in every other use): a double free. Until W20's fix, the buffer is
    // detached before the machine goes (W25 NOTES, F6).
    ~Rig() { if (r) r->set_row_buffer(nullptr, 0); c.reset(); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    void row(int64_t now) { max_gap = std::max(max_gap, now - (last_row < r->pass_start_at ? r->pass_start_at : last_row)); last_row = now; }
    bool starved(int64_t now) {
        if (r->is_running() && now - std::max(r->pass_start_at, last_row) > norow) stuck = true;
        return stuck;
    }
    std::string report(size_t rows, mach::TileRef p) const {
        return fmt("starved: no row for %lld cycles while a pass ran (pass started at cycle %lld, last row at %s, %zu rows so far): is the controller "
                   "feeding the right queue ports (4.19.10, CL1)? queue occupancy: code %u, read log %u, write log %u, expansion %u; bad writes 0; controller %s",
                   (long long)norow, (long long)r->pass_start_at, last_row < 0 ? "none" : std::to_string(last_row).c_str(), rows, r->code.occ, r->rlog.occ,
                   r->wlog.occ, r->exp.occ, p.in_wfi() ? "in WFI" : ("at pc 0x" + hex32(p.pc())).c_str());
    }
    std::string stop_report(int64_t now, int64_t cap, size_t rows, mach::TileRef p) const {
        std::string where = "controller " + std::to_string(p.halt()) + (p.in_wfi() ? " in WFI" : " at pc 0x" + hex32(p.pc()));
        if (r->passes == 0) return fmt("no pass started by cycle %lld (the cap is %lld): ", (long long)now, (long long)cap) + where;
        if (now > cap) return fmt("cycle cap %lld reached with %zu rows: ", (long long)cap, rows) + where;
        return "tile 0 did not reach its exit: " + where;
    }
    // C&C boots replay_ctl on tile 0 only (6.2: only tile 0 is expected at GO; the others wait in their ROMs).
    void boot(const mach::Mach::Image& im) {
        c->cc().set_expect(1);
        c->cc().load_words(0, PMEM_BASE, im.code);
        if (!im.data.empty()) c->cc().load_words(0, im.sym("_data_start"), im.data);
        c->cc().command(0, CMD_JUMP << 24, {im.sym("_start")}).go();
    }
    std::string replayer_detail(bool synthetic_order) const {
        using R = Replayer;
        if (synthetic_order)
            return "replayer: violations " + head(*r, R::L_VIOLATIONS) + ", overruns " + head(*r, R::L_OVERRUNS) + ", bad writes [], invalid " +
                   head(*r, R::L_INVALID) + ", underflows " + head(*r, R::L_UNDERFLOWS) + ", leftover " + head(*r, R::L_LEFTOVER);
        return "replayer: violations " + head(*r, R::L_VIOLATIONS) + ", invalid " + head(*r, R::L_INVALID) + ", overruns " + head(*r, R::L_OVERRUNS) +
               ", bad writes [], underflows " + head(*r, R::L_UNDERFLOWS) + ", leftover " + head(*r, R::L_LEFTOVER);
    }
};

std::string stalls_string(const Replayer& r) { return stagea::stalls_string(r); }

// Java's HashMap<Integer, Long> iteration order (the profile's ties): bucket (h ^ h >>> 16) & (n - 1) at the final
// capacity, insertion order within a bucket (resizes keep it); capacity 16, doubled when the size passes 3/4 of it.
std::vector<std::pair<int32_t, int64_t>> java_int_map_order(const std::vector<std::pair<int32_t, int64_t>>& inserted) {
    size_t cap = 16;
    while (inserted.size() > cap * 3 / 4) cap *= 2;
    std::vector<std::pair<int32_t, int64_t>> out = inserted;
    auto bucket = [cap](int32_t k) { uint32_t h = uint32_t(k); return (h ^ (h >> 16)) & uint32_t(cap - 1); };
    std::stable_sort(out.begin(), out.end(), [&](const auto& a, const auto& b) { return bucket(a.first) < bucket(b.first); });
    return out;
}

}  // namespace

std::string java_fixed(double v, int digits) {
    char b[512];
    std::snprintf(b, sizeof b, "%.80f", v < 0 ? -v : v);   // the exact binary value's decimal expansion (glibc)
    std::string s = b;
    size_t dot = s.find('.');
    std::string ip = s.substr(0, dot), fp = s.substr(dot + 1);
    bool up = fp[size_t(digits)] >= '5';                    // HALF_UP: the first dropped digit decides
    std::string keep = ip + fp.substr(0, size_t(digits));
    if (up) {
        int i = int(keep.size()) - 1;
        while (i >= 0 && keep[size_t(i)] == '9') keep[size_t(i--)] = '0';
        if (i < 0) keep.insert(keep.begin(), '1'); else keep[size_t(i)]++;
    }
    std::string r = keep.substr(0, keep.size() - size_t(digits));
    if (digits > 0) r += "." + keep.substr(keep.size() - size_t(digits));
    return (v < 0 && r.find_first_not_of("0.") != std::string::npos ? "-" : "") + r;
}

mach::Mach::Image image(const std::string& dir) { return mach::Mach::Image::load_dir(dir, "replay_ctl"); }

stagea::Result run_window(const mach::Mach::Image& im, const units::tracer::ReplayTables& tb, const std::string& pd, const stagea::Entry& we,
                          const std::string& variant, const std::string& prog, const Options& o) {
    const long long index = we.index, rows = we.rows;
    const std::string base = stagea::window_base(index);
    Rig g(o, o.lone);
    mach::TileRef p = g.c->tile(0);
    Replayer& r = *g.r;

    // ---- the loader: the window into tile 0's big SRAM (replay_layout.h)
    std::vector<uint8_t> tables = tb.decode_bytes;
    tables.insert(tables.end(), tb.rom_bytes.begin(), tb.rom_bytes.end());
    put(p, TABLES, tables);
    put(p, INDEX, tb.index_bytes);
    put(p, TEMPLATES, tb.template_bytes);
    std::vector<uint8_t> rec;
    if (!stagea::read_file(pd + "/" + base + variant + ".bin", rec)) FATAL("stageb: %s%s.bin: cannot read it", base.c_str(), variant.c_str());
    put(p, RECORD, rec);
    if (rec.size() != 16 * Replayer::REC_WORDS)
        FATAL("%s%s.bin: %zu bytes, not a v1.26 state record (%u): convert a v1.25 delivery with ConvertDelivery", base.c_str(), variant.c_str(), rec.size(),
              16 * Replayer::REC_WORDS);
    units::streamword::Size sz[3];
    units::streamword::parse(stagea::le64(rec, 16), stagea::le64(rec, 24), sz);
    std::vector<uint8_t> code, rlog, wlog;
    const char* names[3] = {"code", "rlog", "wlog"};
    std::vector<uint8_t>* dst[3] = {&code, &rlog, &wlog};
    for (int k = 0; k < 3; k++)
        if (!stagea::read_file(pd + "/" + base + stagea::stream_file(names[k], sz[k].compressed), *dst[k]))
            FATAL("stageb: %s%s: cannot read it", base.c_str(), stagea::stream_file(names[k], sz[k].compressed).c_str());
    put(p, CODE, code); put(p, RLOG, rlog); put(p, WLOG, wlog);
    uint32_t h[16] = {0x594C5052, LAYOUT_VERSION, TABLES, INDEX, uint32_t(tb.index_bytes.size() / 16), TEMPLATES, RECORD, CODE,
                      uint32_t(code.size() / 16), RLOG, uint32_t(rlog.size() / 16), WLOG, uint32_t(wlog.size() / 16), 1,
                      rtest::run_config(uint32_t(rows), true, true), 0};
    for (uint32_t i = 0; i < 16; i++) p.sram_poke(HDR + i, h[i]);

    // ---- boot, then run (with the profile's sampling and the starvation watch in the condition, as ref)
    g.boot(im);
    std::vector<std::pair<int32_t, int64_t>> pcs;   // insertion order, for Java's HashMap
    uint64_t last_s2 = 0;
    const int64_t cap = 40 * rows + 2000000;
    g.c->run_until([&] {
        if (o.profile) {
            uint64_t s2 = r.stalls[2];
            if (s2 != last_s2) {
                int32_t key = p.in_wfi() ? -1 : int32_t(p.pc());
                auto it = std::find_if(pcs.begin(), pcs.end(), [&](const auto& e) { return e.first == key; });
                if (it == pcs.end()) pcs.push_back({key, 1}); else it->second++;
                last_s2 = s2;
            }
        }
        return (p.halt() != 0) || g.c->cycle() > cap || g.starved(g.c->cycle());
    }, uint64_t(60 * rows + 3000000));
    if (o.profile) {
        std::map<std::string, int64_t> by_fn;
        for (const auto& e : pcs) {
            std::string fn = "WFI";
            if (e.first >= 0) {
                int64_t best = -1;
                for (const auto& sy : im.syms)
                    if (int64_t(sy.second) <= int64_t(uint32_t(e.first)) && int64_t(sy.second) > best && units::lw::region(sy.second) == units::lw::region(PMEM_BASE)) {
                        best = sy.second; fn = sy.first;
                    }
            }
            by_fn[fn] += e.second;
        }
        std::string s = "{";
        for (const auto& [k, v] : by_fn) s += (s.size() > 1 ? ", " : "") + k + "=" + std::to_string(v);
        std::printf("  new-expansion stall cycles by the controller's pc: %s}\n", s.c_str());
        std::vector<std::pair<int32_t, int64_t>> ord = java_int_map_order(pcs);
        std::stable_sort(ord.begin(), ord.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < ord.size() && i < 40; i++) std::printf("    pc 0x%x %lld\n", uint32_t(ord[i].first), (long long)ord[i].second);
    }
    const bool ok = p.halt() == units::Rv32::HALT_EXIT;
    const uint32_t res = im.sym("rp_results");
    const uint32_t status = p.peek_local(res), cond = p.peek_local(res + 8), newexp = p.peek_local(res + 12), refills = p.peek_local(res + 16),
                   missing = p.peek_local(res + 20), slow = p.peek_local(res + 24);

    // ---- the rows, the streams, the end state (as Stage A)
    std::vector<std::string> detail;
    std::vector<uint8_t> ref;
    stagea::read_file(pd + "/" + base + "rows.bin", ref);
    const long long nref = (long long)(ref.size() / 80), ngot = (long long)g.got.size(), same = std::min(nref, ngot);
    if (ngot != nref) detail.push_back(fmt("row count: tile %lld, reference %lld", ngot, nref));
    for (long long i = 0; i < same; i++) {
        bool eq = true;
        for (unsigned f = 0; f < 10; f++) if (g.got[size_t(i)][f] != stagea::le64(ref, size_t(80 * i + 8 * f))) { eq = false; break; }
        if (!eq) { detail.push_back(fmt("first mismatching row %lld", i)); break; }
    }
    std::string sc = stagea::consumed_exactly(r, sz, code.size() / 16, rlog.size() / 16, wlog.size() / 16);
    if (!sc.empty()) detail.push_back(sc);
    std::vector<uint8_t> end;
    if (stagea::read_file(pd + "/" + base + "end.bin", end)) {
        if (end.size() != 16 * Replayer::REC_WORDS)
            FATAL("%send.bin: %zu bytes, not a v1.26 state record (%u): convert a v1.25 delivery with ConvertDelivery", base.c_str(), end.size(), 16 * Replayer::REC_WORDS);
        uint64_t epc = stagea::le64(end, 0), ehi = stagea::le64(end, 8);
        if (r.pc() != epc || r.instr_idx() != (ehi & 0xFFFFFFFF)) detail.push_back("end state differs");
        for (unsigned i = 1; i < 128; i++) if (r.reg(i) != stagea::le64(end, 32 + 8 * i)) { detail.push_back("end state: register " + std::to_string(i)); break; }
    }
    if (!r.clean()) detail.push_back(g.replayer_detail(false));
    if (g.c->violations() > 0) detail.push_back("meshsim: " + cut(one_line(g.c->violation_report())));
    if (g.stuck) detail.push_back(g.report(g.got.size(), p));
    else if (!ok || p.halt() == 0) detail.push_back(g.stop_report(g.c->cycle(), cap, g.got.size(), p));
    if (!g.c->parked_untouched()) detail.push_back("tile 0 only (-Dstageb.lone): something reached a parked tile; rerun on the full chip");
    if (status != 1) detail.push_back("rp_results: status " + std::to_string(int32_t(status)) + " conditions 0x" + hex32(cond) + " missing 0x" + hex32(missing));
    const bool pass = detail.empty();
    const long long cyc = (r.pass_end_at >= r.pass_start_at ? r.pass_end_at : g.c->cycle()) - r.pass_start_at;
    const std::string per_row = java_fixed(double(cyc) / double(std::max(1LL, same)), 3);
    std::printf("%s\n", fmt("%-12s window %5lld  %-10s %s  %6lld rows  pass %8lld cycles  %s cycles/row  newexp %llu (served %d, slow path %d), refills %d, stalls %s  [meas: StageB]",
                            prog.c_str(), index, variant.c_str(), pass ? "OK  " : "FAIL", same, cyc, per_row.c_str(), (unsigned long long)r.new_expansions,
                            int32_t(newexp), int32_t(slow), int32_t(refills), stalls_string(r).c_str()).c_str());
    for (const std::string& d : detail) std::printf("    %s\n", d.c_str());
    std::fflush(stdout);
    stagea::Result out;
    out.ok = pass; out.rows = same; out.cycles = cyc; out.index = index; out.detail = detail;
    std::string waits = "image/code/newexp/exp/rlog/wlog/outq ";
    for (unsigned i = 0; i < Replayer::N_STALL; i++) waits += fmt("%s%llu", i ? "/" : "", (unsigned long long)r.stalls[i]);
    waits += " logq " + std::to_string(r.rlog.cap);
    out.summary = std::string(o.lone ? "tile 0 only, " : "full chip, ") + "max row gap " + std::to_string(g.max_gap) + ", " + per_row +
                  fmt(" cycles/row, newexp %llu (slow %d), refills %d, waits ", (unsigned long long)r.new_expansions, int32_t(slow), int32_t(refills)) + waits;
    return out;
}

namespace {

std::vector<std::string> g_last_detail;   // StageB.lastDetail

// StageB.runSynthetic: Stage A's rows (ReplayDriver), then the tile's (replay_ctl.c) from the same window.
bool run_synthetic(const mach::Mach::Image& im, rtest::ReplayWindow& w, const std::string& name, const Options& o) {
    rtest::ReplayDriver d(w, 1u << 17);
    d.load();
    d.pass(400000);
    std::vector<std::vector<uint64_t>> refrows;
    for (uint64_t i = 0; i < d.rows(); i++) refrows.emplace_back(d.row(i), d.row(i) + Replayer::REC_LONGS);
    Rig g(o, false);
    mach::TileRef p = g.c->tile(0);
    Replayer& r = *g.r;
    // the tables: the decode table (256 x 128 bits, entry e at word e >> 3, bits 16 (e & 7)), then the ROM (512 words)
    std::vector<uint64_t> tab(2 * (256 + 512), 0);
    for (int e = 0; e < 2048; e++) tab[size_t(2 * (e >> 3) + ((e & 7) >> 2))] |= uint64_t(w.table[e] & 0xFFFF) << (16 * (e & 3));
    for (int j = 0; j < 512; j++) { tab[size_t(2 * (256 + j))] = w.rom_lo[j]; tab[size_t(2 * (256 + j) + 1)] = w.rom_hi[j]; }
    put(p, TABLES, bytes(tab));
    // the template index (16 B each: key, offset in 128-bit words, length in words, rd0 | class << 8), sorted by key
    std::vector<uint64_t> ix, tpl;
    uint64_t off = 0, bm[2] = {0, 0};
    unsigned q = 0;
    for (const auto& [key, t] : w.templates) {   // std::map: unsigned key order (ref: Integer::compareUnsigned)
        const uint64_t words = t.size() / 2;
        auto ir = w.template_rd0.find(key);
        const uint64_t rd0 = ir != w.template_rd0.end() ? ir->second : (words > 0 ? (t[0] >> 46) & 3 : 0);
        ix.push_back(uint64_t(key) | off << 32);
        ix.push_back(words | (rd0 | 1u << 8) << 32);
        tpl.insert(tpl.end(), t.begin(), t.end());
        off += words;
        bm[q >> 6] |= 1ull << (q & 63);
        q++;
    }
    put(p, INDEX, bytes(ix)); put(p, TEMPLATES, bytes(tpl));
    put(p, RECORD, bytes(w.record(bm)));
    put(p, CODE, bytes(w.code)); put(p, RLOG, bytes(w.rlog)); put(p, WLOG, bytes(w.wlog));
    uint32_t h[16] = {0x594C5052, LAYOUT_VERSION, TABLES, INDEX, q, TEMPLATES, RECORD, CODE, uint32_t(w.code.size() / 2), RLOG,
                      uint32_t(w.rlog.size() / 2), WLOG, uint32_t(w.wlog.size() / 2), 1, w.run_config_word(), 0};
    for (uint32_t i = 0; i < 16; i++) p.sram_poke(HDR + i, h[i]);
    g.boot(im);
    g.c->run_until([&] { return p.halt() != 0 || g.starved(g.c->cycle()); }, 2000000);
    const uint32_t res = im.sym("rp_results"), status = p.peek_local(res);
    std::vector<std::string>& detail = g_last_detail;
    detail.clear();
    if (g.stuck) detail.push_back(g.report(g.got.size(), p));
    else if (p.halt() != units::Rv32::HALT_EXIT) detail.push_back("tile 0 did not reach its exit: " + p.halt_report());
    if (status != 1) detail.push_back("rp_results status " + std::to_string(int32_t(status)) + " missing 0x" + hex32(p.peek_local(res + 20)));
    if (g.got.size() != refrows.size()) detail.push_back(fmt("rows: tile %zu, Stage A %zu", g.got.size(), refrows.size()));
    for (size_t i = 0; i < std::min(g.got.size(), refrows.size()); i++)
        if (g.got[i] != refrows[i]) { detail.push_back(fmt("first mismatching row %zu", i)); break; }
    if (!r.clean()) detail.push_back(g.replayer_detail(true));
    if (g.c->violations() > 0) detail.push_back("meshsim: " + cut(one_line(g.c->violation_report())));
    const bool pass = detail.empty();
    const long long cyc = (r.pass_end_at >= r.pass_start_at ? r.pass_end_at : g.c->cycle()) - r.pass_start_at;
    const double sa = double(d.r->pass_end_at - d.r->pass_start_at) / double(std::max<size_t>(1, refrows.size()));
    std::printf("synthetic %-30s %s %5zu rows  pass %7lld cycles  %s cycles/row (Stage A %s)  newexp %llu, refills %d  [meas: StageB synthetic]\n", name.c_str(),
                pass ? "OK  " : "FAIL", g.got.size(), cyc, java_fixed(double(cyc) / double(std::max<size_t>(1, g.got.size())), 3).c_str(),
                java_fixed(sa, 3).c_str(), (unsigned long long)r.new_expansions, int32_t(p.peek_local(res + 16)));
    for (const std::string& x : detail) std::printf("    %s\n", x.c_str());
    std::fflush(stdout);
    return pass;
}

// The controller-fed window (StageB.synthetic's second and third kinds).
std::unique_ptr<rtest::ReplayWindow> controller_fed(int mid) {
    using rtest::W;
    const uint32_t inl = w15unit::r_(0, 2, 1, 0, 3, w15unit::CUSTOM0);
    const int len = 300;
    auto w = std::make_unique<rtest::ReplayWindow>();
    w->fixed_rom();
    w->decode_class(inl, 1);
    std::vector<W> words;
    for (int k = 0; k < len; k++) words.push_back(W::op(units::alu::ADDI).rd(unsigned(137 + (k % 8))).rs1(0).rs2(128).cnst(uint64_t(k + 1)).fmt(0));
    words[size_t(len - 1)].report_c();
    w->template_(inl, words);
    w->pc = 0x2000; w->instr_idx = 100; w->next_offset = mid == 1 ? 250 : 0;
    w->set_code({inl, inl}, false);
    w->set_logs({}, false, false);
    w->rows = uint32_t(mid == 1 ? 50 + len : 2 * len);
    return w;
}

}  // namespace

int synthetic(const std::string& image_dir, const std::string& oldmap_dir, const Options& o) {
    mach::Mach::Image im = image(image_dir);
    int failed = 0, n = 0;
    for (int v = 0; v < 16; v++) {
        auto w = w15unit::program_window((v & 1) != 0, (v & 2) != 0, (v & 4) != 0, (v & 8) != 0);
        w->rows = 25; n++;
        if (!run_synthetic(im, *w, "program " + std::to_string(v), o)) failed++;
    }
    for (int mid = 0; mid < 2; mid++) {
        auto w = controller_fed(mid);
        n++;
        if (!run_synthetic(im, *w, mid == 1 ? "controller-fed, mid-sequence" : "controller-fed", o)) failed++;
    }
    {   // CL4: the controller on the queue map before CL1 must fail the window, starved, not hang
        auto w = controller_fed(0);
        n++;
        const auto t0 = std::chrono::steady_clock::now();
        bool passed = run_synthetic(image(oldmap_dir), *w, "old queue map (CL4: must fail)", o);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool ok = !passed && std::any_of(g_last_detail.begin(), g_last_detail.end(), [](const std::string& x) { return x.rfind("starved", 0) == 0; });
        std::string why = ok ? "as starved" : "but not as starved: " + stagea::java_list(g_last_detail);
        std::printf("synthetic %-30s %s the window failed %s in %s s\n", "old queue map (CL4)", ok ? "OK  " : "FAIL", why.c_str(), java_fixed(s, 1).c_str());
        if (!ok) failed++;
    }
    std::printf("StageB synthetic: %d windows, %d failed\n", n, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace stageb
