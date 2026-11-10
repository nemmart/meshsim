// chain (WORKER17, v1.35; build/<cc>-<mode>/chain): T2's segments back to back on the framework's chip, one workload,
// the memory carried from each segment to the next as `ref`'s T1 carries it (sim/planning/sessions/WORKER17-NOTES.md).
// Run from sim/ (the hops, the boot ROM, the images in tests/m3-images, the replayer tables).
//
//   chain --w26 DIR --delivery DIR --program NAME --segments A-B [--oracle FILE] [--windows N] [--chunk N]
//         [--dump S --dump-dir D]
//
//   --w26 DIR         WORKER26's inputs of the workload ($DATA/w26-jumpmap/NAME)
//   --delivery DIR    the deliveries ($DATA/w9-deliveries): NAME.manifest.json and NAME/window_NNNNN.*.bin
//   --oracle FILE     tests/golden-chain/<run>.starts: every segment's start memory (and some landings) as ref's
//   --chunk N         cycles run between the harness's checks (default 250,000: ref's progress rule's interval)
//   --dump S          segment S's start memory and landings in W13's forms (D/NAME.SSS.files.gz, .sram.gz), for the
//                     t2 tool's checked run on them (--files, --sram, --segment, --fp-points)
//
// Per segment (T1's harness between segments, TracerT2.chip within one):
//   setup    the ownership set (the update list's pages; against seg/NNN.pages); the start memory against the oracle
//            (sha256 of W13's form, nextFree, pages, the files sum); the landings (against the oracle's where given);
//            a fresh chip (one domain, D26) with C&C 3's Tracer on the carried store through a fresh memory system
//            (cold; cold again at every state-word landing, as T2's onStateWrite); the landings poked; the boot staged;
//   boot     to Mach.coresBooted;
//   segment  CC_SEGMENT, then the run in chunks to C&C 3's exit with every tile exited (ref's stop rules: the Tracer
//            stopped, no progress for 3 M cycles, a tile not exited 1 M cycles after C&C 3);
//   checks   everything TracerT2.chip checks: C&C 3 exited, tr_results status 1, the Tracer not stopped; per tile the
//            delivered state word and registers against state.bin, the delivered streams against code/rlog/wlog.bin
//            (value for value, the windows the manifest lists with files), exit 1, the rows against rows.bin (count
//            and the sink hash), the replayer clean; no violation; the request bus's lag_cycles 0;
//   carry    the dirty lines written back (untimed, T1's), then every cell of the update list and every cell updated
//            so far (the cumulative shadow) against the memory (T1's end checks).
// The exit and end cycles are W16's labels (t2's points): the register blocks' sim_exit_at - 1 (C&C 3's; the latest of
// C&C 3's and the tiles'), equal to t2's on segment 10 of both workloads (Quest 173). A segment's cycles are W16's
// (the end's label less the boot's end); its time is the run from CC_SEGMENT, which ends within 10,000 cycles of the end.
#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "chain.h"
#include "sha256.h"
#include "t2_harness.h"
#include "units/big_sram.h"
#include "units/chip.h"
#include "units/mem_system.h"
#include "units/periph_slot.h"
#include "units/replayer.h"
#include "units/reqbus.h"
#include "units/stream_word.h"
#include "units/tile_ctl.h"
#include "units/tracer.h"

using Clock = std::chrono::steady_clock;
using units::tracer::FileStore;

namespace {

double secs(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }

bool read_file(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
uint64_t le64(const std::vector<uint8_t>& b, size_t at) {
    uint64_t v = 0;
    for (int k = 7; k >= 0; k--) v = v << 8 | b[at + size_t(k)];
    return v;
}
std::vector<uint64_t> words_of(const std::vector<uint8_t>& b) {
    std::vector<uint64_t> w(b.size() / 8);
    for (size_t i = 0; i < w.size(); i++) w[i] = le64(b, 8 * i);
    return w;
}
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

struct Opts {
    std::string w26, delivery, program, oracle, dump_dir;
    unsigned s0 = 0, s1 = 0, windows = 64;
    int dump = -1;
    uint64_t chunk = 250000;
};

struct Totals {
    unsigned segments = 0, failed = 0;
    double setup = 0, boot = 0, segment = 0, checks = 0, carry = 0;
    uint64_t cycles = 0, ran = 0, rows = 0;
};

// TracerT2.getWords: n128 128-bit words from chunk on (two longs each, a 32-bit word per chunk).
std::vector<uint64_t> sram_words(units::BigSram& s, uint32_t chunk, size_t n128) {
    std::vector<uint64_t> w(2 * n128);
    for (size_t i = 0; i < w.size(); i++) w[i] = uint64_t(s.peek(chunk + 2 * uint32_t(i))) | uint64_t(s.peek(chunk + 2 * uint32_t(i) + 1)) << 32;
    return w;
}

}  // namespace

int main(int argc, char** argv) {
    Opts o;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { if (i + 1 >= argc) { std::fprintf(stderr, "chain: %s needs a value\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if (a == "--w26") o.w26 = next();
        else if (a == "--delivery") o.delivery = next();
        else if (a == "--program") o.program = next();
        else if (a == "--oracle") o.oracle = next();
        else if (a == "--windows") o.windows = unsigned(std::atoi(next()));
        else if (a == "--chunk") o.chunk = std::strtoull(next(), nullptr, 10);
        else if (a == "--dump") o.dump = std::atoi(next());
        else if (a == "--dump-dir") o.dump_dir = next();
        else if (a == "--segments") {
            const std::string r = next();
            const size_t d = r.find('-');
            o.s0 = unsigned(std::atoi(r.c_str()));
            o.s1 = d == std::string::npos ? o.s0 : unsigned(std::atoi(r.c_str() + d + 1));
        } else { std::fprintf(stderr, "chain: unknown argument %s\n", a.c_str()); return 2; }
    }
    if (o.w26.empty() || o.delivery.empty() || o.program.empty() || o.chunk == 0 || o.windows == 0 || o.windows > 64 ||
        (o.dump >= 0 && o.dump_dir.empty())) {
        std::fprintf(stderr, "chain: --w26, --delivery, --program and --segments are needed (--dump needs --dump-dir)\n");
        return 2;
    }
    std::string err;
    const auto t_load = Clock::now();
    // ---- the inputs, once
    FileStore store;
    chain::W26 w;
    if (!chain::load_w26(o.w26, store, w, err)) { std::fprintf(stderr, "chain: %s\n", err.c_str()); return 1; }
    std::vector<chain::Seg> segs;
    if (!chain::read_segments(o.w26 + "/segments.seg", segs, err)) { std::fprintf(stderr, "chain: %s\n", err.c_str()); return 1; }
    if (int(segs.size()) != w.segments) { std::fprintf(stderr, "chain: %zu segments in segments.seg, proof.json %d\n", segs.size(), w.segments); return 1; }
    if (o.s1 >= segs.size() || o.s0 > o.s1) { std::fprintf(stderr, "chain: segments %u-%u of %zu\n", o.s0, o.s1, segs.size()); return 2; }
    std::vector<chain::Start> oracle;
    if (!o.oracle.empty() && !chain::load_starts(o.oracle, oracle, err)) { std::fprintf(stderr, "chain: %s\n", err.c_str()); return 1; }
    std::set<unsigned> with_files;
    if (!chain::manifest_windows(o.delivery, o.program, with_files, err)) { std::fprintf(stderr, "chain: %s\n", err.c_str()); return 1; }
    const units::tracer::ReplayTables tables = units::tracer::ReplayTables::load(units::tracer::ReplayTables::default_dir());
    t2::Image cc1, cc23, tile, cc3;
    for (auto [im, n] : {std::pair<t2::Image*, const char*>{&cc1, "cc1"}, {&cc23, "cc23"}, {&tile, "replay_ctl"}, {&cc3, "tracer_ctl"}})
        if (!t2::load_image("tests/m3-images", n, *im, err)) { std::fprintf(stderr, "chain: %s\n", err.c_str()); return 1; }
    const std::string pd = o.delivery + "/" + o.program;
    const chain::Landings builder(tables, o.windows);
    std::printf("%s: W26's inputs: image %u pages (root %d, depth %d), map 2^%d lines (root %d, depth %d), log2 K %d, lowest 0x%llx; "
                "%zu segments; %zu windows with files; loaded in %.2f s\n",
                o.program.c_str(), w.img_pages, w.img.root, w.img.depth, w.hash_k, w.map.root, w.map.depth, w.log2k,
                (unsigned long long)w.lowest, segs.size(), with_files.size(), secs(t_load, Clock::now()));
    std::fflush(stdout);

    std::unordered_map<uint32_t, uint64_t> shadow;   // every cell updated so far: its value (T1's cumulative check)
    Totals tot;
    for (unsigned s = 0; s <= o.s1; s++) {
        const chain::Seg& sg = segs[s];
        const chain::Seg* nx = s + 1 < segs.size() ? &segs[s + 1] : nullptr;
        std::vector<std::string> bad;
        const auto t0 = Clock::now();
        // ---- T1's harness at the segment's start: the ownership set
        const std::vector<int64_t> written = sg.written_pages();
        {
            std::vector<int64_t> w26w = w.written[s];
            std::sort(w26w.begin(), w26w.end());
            w26w.erase(std::unique(w26w.begin(), w26w.end()), w26w.end());
            if (w26w != written) bad.push_back("seg/NNN.pages differs from the update list's pages");
        }
        const int created = store.set_ownership(w.img, written);
        // ---- the start memory against ref's
        std::string start_v = "-";
        {
            const std::vector<uint8_t> fs = chain::files_stream(store);
            const std::string sha = test::sha256_hex(fs.data(), fs.size());
            const uint64_t scan = store.scan_sum();
            if (s < oracle.size() && oracle[s].present) {
                const chain::Start& r = oracle[s];
                const bool eq = r.sha == sha && r.next_free == store.next_free() && r.pages == int64_t(store.pages_allocated()) && r.scan == scan;
                start_v = eq ? "equal" : "DIFFERS";
                if (!eq) bad.push_back(fmt("start memory: nextFree %lld pages %zu scan %016" PRIx64 " sha256 %s; ref's nextFree %lld pages %lld scan %016" PRIx64 " sha256 %s",
                                           (long long)store.next_free(), store.pages_allocated(), scan, sha.c_str(), (long long)r.next_free,
                                           (long long)r.pages, r.scan, r.sha.c_str()));
            } else if (!o.oracle.empty()) start_v = "no-oracle";
            if (int(s) == o.dump && !chain::write_gz(fmt("%s/%s.%03u.files.gz", o.dump_dir.c_str(), o.program.c_str(), s), fs, err))
                bad.push_back(err);
            if (s < o.s0) {
                // before the range: carried by the chain all the same (no shortcut), checked as below, not reported apart
            }
        }
        std::vector<t2::SramLanding> land;
        builder.build(w, sg, nx, land);
        std::string sram_v = "-";
        {
            const std::vector<uint8_t> ss = chain::sram_stream(land);
            if (s < oracle.size() && !oracle[s].sram_sha.empty()) {
                const std::string sha = test::sha256_hex(ss.data(), ss.size());
                sram_v = sha == oracle[s].sram_sha ? "equal" : "DIFFERS";
                if (sha != oracle[s].sram_sha) bad.push_back("landings: sha256 " + sha + ", ref's " + oracle[s].sram_sha);
            }
            if (int(s) == o.dump && !chain::write_gz(fmt("%s/%s.%03u.sram.gz", o.dump_dir.c_str(), o.program.c_str(), s), ss, err))
                bad.push_back(err);
        }
        // ---- the chip on the carried store (one domain)
        sim::Options opt;
        opt.threads = 1;
        auto m = t2::build_machine(nullptr, opt, err);
        if (!m) { std::fprintf(stderr, "chain: %s\n", err.c_str()); return 1; }
        sim::Sim& sm = *m->sim;
        units::tracer::Tracer& tr = sm.unit<units::PeriphSlot>("cc3.slot").s.tracer(1);
        units::tracer::MemSystem ms(&store);              // fresh: cold (T2's new MemorySystem(fs))
        ms.map_root_for_stats = w.map.root;
        tr.mem = &ms;
        tr.map_mem = nullptr;
        tr.cold_mem_on_state_write = true;                // T2's onStateWrite = ms::cold
        t2::poke_sram(sm, land);
        t2::boot_cores(sm, cc1, cc23, tile, cc3, 8, 0);
        const auto t1 = Clock::now();
        const int64_t boot_end = t2::run_boot(*m, cc1, 2'000'000);
        const auto t2_ = Clock::now();
        if (boot_end < 0) bad.push_back("the boot didn't finish in 2,000,000 cycles");
        // ---- the segment
        const units::TileCore& u3 = sm.unit<units::TileCore>("cc3.core");
        units::PeriphSlot& slot3 = sm.unit<units::PeriphSlot>("cc3.slot");
        std::vector<const units::TileCore*> cores(64);
        std::vector<units::Replayer*> rps(64);
        for (unsigned k = 0; k < 64; k++) {
            cores[k] = &sm.unit<units::TileCore>(units::tile_unit(k, "core"));
            rps[k] = &sm.unit<units::PeriphSlot>(units::tile_unit(k, "slot")).s.replayer(1);
        }
        const char* why = "cap";
        uint64_t first = 0;
        if (boot_end >= 0) {
            t2::cc_segment(sm);
            first = sm.now();
            const uint64_t cap = uint64_t(o.windows) * 65536 * 12 + 5'000'000;
            uint64_t prog = ~uint64_t(0), prog_at = sm.now();
            int64_t cc3_seen = -1;
            for (;;) {
                sm.run(cc3_seen < 0 ? o.chunk : std::min<uint64_t>(o.chunk, 10000));   // finer once C&C 3 has exited
                const uint64_t c = sm.now();
                if (tr.stopped()) { why = "the Tracer stopped"; break; }
                uint64_t p = uint64_t(tr.rows) + slot3.s.dma[1].packets;
                for (unsigned k = 0; k < 64; k++) p += rps[k]->rows_sunk();
                if (p != prog) { prog = p; prog_at = c; }
                else if (c - prog_at >= 3'000'000) { why = "no progress for 3 M cycles"; break; }
                if (u3.rv.halt != units::Rv32::RUN) {
                    if (cc3_seen < 0) cc3_seen = int64_t(c);
                    bool all = true;
                    for (unsigned k = 0; k < 64 && all; k++) if (cores[k]->rv.halt == units::Rv32::RUN) all = false;
                    if (all) { why = nullptr; break; }
                    if (c - uint64_t(cc3_seen) >= 1'000'000) { why = "a tile not exited 1 M cycles after C&C 3"; break; }
                }
                if (c - first > cap) break;
            }
        }
        const auto t3 = Clock::now();
        const uint64_t ran = sm.now() - first;
        if (why) bad.push_back(fmt("STOPPED: %s (cycle %llu)", why, (unsigned long long)sm.now()));
        // ---- the checks (TracerT2.chip's)
        const units::TileCtl& ctl3 = sm.unit<units::TileCtl>("cc3.ctl");
        // W16's labels (t2's points; ref's c.cycle - 1): a register block's sim_exit_at is the label + 1 (Quest 173)
        uint64_t end_at = ctl3.s.sim_exit_at;
        for (unsigned k = 0; k < 64; k++) end_at = std::max(end_at, sm.unit<units::TileCtl>(units::tile_unit(k, "ctl")).s.sim_exit_at);
        const uint64_t exit_label = ctl3.s.sim_exit_at ? ctl3.s.sim_exit_at - 1 : 0, end_label = end_at ? end_at - 1 : 0;
        const uint64_t seg_cycles = boot_end >= 0 && end_label > uint64_t(boot_end) ? end_label - uint64_t(boot_end) : 0;   // as W16's timing
        const uint32_t* d3 = m->dmem(66);
        const uint32_t R = cc3.at("tr_results") / 4;
        const uint32_t status = d3[R], wins = d3[R + 1], transfers = d3[R + 4], terr = d3[R + 6], left = d3[R + 7], diff = d3[R + 8];
        if (tr.stopped()) bad.push_back("the Tracer stopped");
        if (u3.rv.halt != units::Rv32::HALT_EXIT) bad.push_back(fmt("C&C 3 didn't exit: halt %d pc 0x%x", u3.rv.halt, u3.rv.pc));
        if (status != 1 && !(status == 5 && o.windows < 64))
            bad.push_back(fmt("tr_results: status %u, windows %u, ERRORS 0x%x, advice left %u, end-state word %d", status, wins, terr, left, int(diff) - 1));
        t2::RowsRef want[64];
        if (!t2::rows_ref(pd, s, o.windows, want, err)) bad.push_back(err);
        uint64_t replay_max = 0, rows = 0;
        unsigned exits = 0, rows_ok = 0, states_ok = 0, streams_ok = 0, streams_n = 0, clean = 0;
        std::vector<uint8_t> b;
        for (unsigned t = 0; t < o.windows; t++) {
            const unsigned wi = 64 * s + t;
            units::BigSram& sr = sm.unit<units::BigSram>(units::tile_unit(t, "sram"));
            const std::vector<uint64_t> got = sram_words(sr, chain::S_RECORD, 68);
            if (!read_file(fmt("%s/window_%05u.state.bin", pd.c_str(), wi), b) || b.size() < 48 + 8 * 128) bad.push_back(fmt("tile %u: cannot read window %u's state.bin", t, wi));
            else {
                bool ok = got[0] == le64(b, 16) && (got[1] & 0xFFFFFFFFFFFFull) == (le64(b, 24) & 0xFFFFFFFFFFFFull);
                if (!ok) bad.push_back(fmt("tile %u: the delivered state word 0x%llx 0x%llx, the delivery's 0x%llx 0x%llx", t, (unsigned long long)got[0],
                                           (unsigned long long)got[1], (unsigned long long)le64(b, 16), (unsigned long long)le64(b, 24)));
                for (unsigned r = 1; r < 128 && ok; r++)
                    if (got[4 + r] != le64(b, 48 + 8 * size_t(r))) { ok = false; bad.push_back(fmt("tile %u: delivered register %u differs from state.bin", t, r)); }
                if (ok) states_ok++;
            }
            if (with_files.count(wi)) {
                streams_n++;
                units::streamword::Size sz[3];
                units::streamword::parse(got[2], got[3], sz);
                static const char* f[3] = {"code.bin", "rlog.bin", "wlog.bin"};
                static const uint32_t at[3] = {chain::S_CODE, chain::S_RLOG, chain::S_WLOG};
                static const unsigned bits[3] = {32, 64, 64};
                bool ok = true;
                for (int q = 0; q < 3 && ok; q++) {
                    bool sh1 = false, sh2 = false;
                    const std::vector<uint64_t> mine = chain::stream_values(sram_words(sr, at[q], sz[q].words), sz[q].values, bits[q], sh1);
                    if (!read_file(fmt("%s/window_%05u.%s", pd.c_str(), wi, f[q]), b)) { ok = false; bad.push_back(fmt("tile %u: cannot read window %u's %s", t, wi, f[q])); break; }
                    const std::vector<uint64_t> ref = chain::stream_values(words_of(b), sz[q].values, bits[q], sh2);
                    if (sh1 || sh2 || mine != ref) { ok = false; bad.push_back(fmt("tile %u: the delivered %s differs from the delivery's%s", t, f[q], sh1 || sh2 ? " (short)" : "")); }
                }
                if (ok) streams_ok++;
            }
            const units::Replayer& rp = *rps[t];
            replay_max = std::max(replay_max, uint64_t(rp.pass_end_at - rp.pass_start_at));
            const units::TileCtl& ctl = sm.unit<units::TileCtl>(units::tile_unit(t, "ctl"));
            if (cores[t]->rv.halt == units::Rv32::HALT_EXIT && ctl.s.sim_exit_value == 1) exits++;
            else { bad.push_back(fmt("tile %u: halt %d, exit %u", t, cores[t]->rv.halt, ctl.s.sim_exit_value)); continue; }
            if (want[t].present && rp.rows_sunk() == want[t].rows && rp.row_hash() == want[t].hash) rows_ok++;
            else bad.push_back(fmt("tile %u (window %u): %llu rows, hash %016llx; rows.bin %llu, %016llx", t, wi, (unsigned long long)rp.rows_sunk(),
                                   (unsigned long long)rp.row_hash(), (unsigned long long)want[t].rows, (unsigned long long)want[t].hash));
            rows += rp.rows_sunk();
            if (rp.clean()) clean++; else bad.push_back(fmt("tile %u: the replayer isn't clean", t));
        }
        uint64_t violations = 0;
        for (unsigned k = 0; k < 67; k++) {
            violations += sm.unit<units::TileCore>(units::tile_unit(k, "core")).rv.violations;
            violations += sm.unit<units::TileCtl>(units::tile_unit(k, "ctl")).s.violations;
        }
        if (violations) bad.push_back(fmt("violations: %llu", (unsigned long long)violations));
        const uint64_t lag = units::reqbus::lag_cycles(units::reqbus::resolve(sm, ~uint64_t(0), 3, units::reqbus::ReqBusConfig{}));
        if (lag) bad.push_back(fmt("request bus lag_cycles %llu", (unsigned long long)lag));
        const auto t4 = Clock::now();
        // ---- T1's harness at the segment's end: the dirty lines written back; the update list and the shadow
        const int dirty = tr.write_back_dirty_untimed();
        const uint64_t packets = slot3.s.dma[1].packets;
        const long long run_cycles = (long long)tr.run_cycles, drain_cycles = (long long)tr.drain_cycles;
        tr.mem = nullptr;
        m.reset();
        unsigned cell_bad = 0;
        for (const auto& u : sg.updates) {
            shadow[u.first] = u.second;
            const uint64_t v = store.load64(w.img, 8 * int64_t(u.first));
            if (v != u.second && cell_bad++ < 3) bad.push_back(fmt("memory cell 0x%llx: 0x%llx, the update list 0x%llx", (unsigned long long)(w.lowest + 8 * int64_t(u.first)),
                                                                   (unsigned long long)v, (unsigned long long)u.second));
        }
        unsigned strays = 0;
        for (const auto& [cell, val] : shadow) {
            const uint64_t v = store.load64(w.img, 8 * int64_t(cell));
            if (v != val && strays++ < 3) bad.push_back(fmt("cumulative: cell 0x%llx is 0x%llx, the updates say 0x%llx", (unsigned long long)(w.lowest + 8 * int64_t(cell)),
                                                            (unsigned long long)v, (unsigned long long)val));
        }
        if (cell_bad) bad.push_back(fmt("%u cells differ from the update list", cell_bad));
        if (strays) bad.push_back(fmt("%u cells differ from the cumulative updates", strays));
        const auto t5 = Clock::now();
        if (s < o.s0) { if (!bad.empty()) { std::printf("%s segment %u (before the range): FAIL\n", o.program.c_str(), s); for (auto& x : bad) std::printf("    %s\n", x.c_str()); return 1; } continue; }
        // ---- the report
        const bool ok = bad.empty();
        tot.segments++; if (!ok) tot.failed++;
        tot.setup += secs(t0, t1); tot.boot += secs(t1, t2_); tot.segment += secs(t2_, t3); tot.checks += secs(t3, t4); tot.carry += secs(t4, t5);
        tot.cycles += seg_cycles; tot.ran += ran; tot.rows += rows;
        std::printf("%s segment %u: %s  start %s, landings %s; boot-end %lld; C&C 3 exit %llu, end %llu; segment %llu cycles (ran %llu); "
                    "tr_results status %u windows %u transfers %u; DMA packets %llu; trace rows %lld, drain %lld cycles; replay max %llu; "
                    "states %u, streams %u of %u, exits %u, rows %u (%llu rows), clean %u; lag 0; owned %zu (created %d), dirty %d, cells %zu (shadow %zu); "
                    "setup %.3f s, boot %.3f s, segment %.3f s (%.4f us/cycle), checks %.3f s, carry %.3f s\n",
                    o.program.c_str(), s, ok ? "OK" : "FAIL", start_v.c_str(), sram_v.c_str(), (long long)boot_end,
                    (unsigned long long)exit_label, (unsigned long long)end_label, (unsigned long long)seg_cycles, (unsigned long long)ran, status, wins, transfers,
                    (unsigned long long)packets, run_cycles, drain_cycles, (unsigned long long)replay_max,
                    states_ok, streams_ok, streams_n, exits, rows_ok, (unsigned long long)rows, clean, written.size(), created, dirty,
                    sg.updates.size(), shadow.size(), secs(t0, t1), secs(t1, t2_), secs(t2_, t3), seg_cycles ? 1e6 * secs(t2_, t3) / double(seg_cycles) : 0.0,
                    secs(t3, t4), secs(t4, t5));
        for (const std::string& x : bad) std::printf("    %s\n", x.c_str());
        std::fflush(stdout);
    }
    std::printf("%s: segments %u-%u: %u of %u OK; %llu segment cycles (ran %llu), %llu rows; setup %.2f s, boot %.2f s, segment %.2f s (%.4f us/cycle), checks %.2f s, carry %.2f s; "
                "total %.2f s\n", o.program.c_str(), o.s0, o.s1, tot.segments - tot.failed, tot.segments, (unsigned long long)tot.cycles, (unsigned long long)tot.ran,
                (unsigned long long)tot.rows, tot.setup, tot.boot, tot.segment, tot.cycles ? 1e6 * tot.segment / double(tot.cycles) : 0.0,
                tot.checks, tot.carry, secs(t_load, Clock::now()));
    return tot.failed ? 1 : 0;
}
