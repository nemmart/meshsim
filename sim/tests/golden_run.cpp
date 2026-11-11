// golden_run.cpp: the golden-trace harness and the direct driver (configuration regime throughout, except the driver's
// loop, which allocates nothing).
#include "golden_run.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <map>
#include <memory>
#include <sstream>

#include "base/alloc_guard.h"
#include "parallel.h"
#include "java_random.h"
#include "sha256.h"

namespace test {

using namespace units;

static std::string hex8(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%08x", v); return b; }

static bool read_lines(const std::string& path, std::vector<std::string>& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string l;
    while (std::getline(f, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
    }
    return true;
}
static bool read_bytes(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}
static std::vector<std::string> fields(const std::string& l) {
    std::istringstream s(l);
    std::vector<std::string> f;
    std::string w;
    while (s >> w) f.push_back(w);
    return f;
}
// Little-endian words, the last one zero-padded (Mach.words).
static std::vector<uint32_t> words(const std::string& b) {
    std::vector<uint32_t> w((b.size() + 3) / 4, 0);
    for (size_t i = 0; i < b.size(); i++) w[i / 4] |= uint32_t(uint8_t(b[i])) << (8 * (i % 4));
    return w;
}
static std::string words_sha(const uint32_t* w, size_t n) {
    std::string b(4 * n, '\0');
    for (size_t i = 0; i < n; i++) for (int k = 0; k < 4; k++) b[4 * i + k] = char(w[i] >> (8 * k));
    return sha256_hex(b.data(), b.size());
}

// ---------------------------------------------------------------------------------------------------------- runs
std::vector<RunSpec> read_runs(const char* runs_file) {
    std::vector<std::string> lines;
    std::vector<RunSpec> runs;
    if (!read_lines(runs_file, lines)) return runs;
    for (const std::string& l : lines) {
        if (l.empty() || l[0] == '#') continue;
        auto f = fields(l);
        if (f.size() < 4) continue;
        RunSpec r;
        r.name = f[0]; r.tile = unsigned(std::stoul(f[1]));
        r.dir = f[2] == "riscv" ? "fw/riscv-tests/bin" : "tests/golden/images";
        r.image = f[3];
        if (f.size() > 4) r.schedule = f[4];
        runs.push_back(r);
    }
    return runs;
}
const RunSpec* find_run(const std::vector<RunSpec>& runs, const std::string& name) {
    for (const RunSpec& r : runs) if (r.name == name) return &r;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------- the golden
bool read_golden(const std::string& path, Golden& g, std::string& err) {
    if (!read_lines(path, g.lines)) { err = "cannot read " + path; return false; }
    for (size_t i = 0; i < g.lines.size(); i++) {
        const std::string& l = g.lines[i];
        auto f = fields(l);
        if (f.empty()) continue;
        if (f[0] == "#") {
            if (f.size() >= 3 && f[1] == "program") g.program = f[2];
            else if (f.size() >= 3 && f[1] == "tile") g.tile = unsigned(std::stoul(f[2]));
            else if (f.size() >= 2 && f[1] == "image") g.image_line = l;
            else if (f.size() >= 3 && f[1] == "bootrom") g.bootrom_sha = f[2];
            else if (f.size() >= 4 && f[1] == "config") g.config.emplace_back(f[2], f[3]);
            else if (f.size() >= 2 && f[1] == "tb") g.tb.push_back(l.substr(l.find("tb") + 3));
            continue;
        }
        if (f[0] == "END") { if (f.size() >= 2) g.end_cycle = std::stoull(f.back()); continue; }
        if (f[0] == "REGS" || f[0] == "CSRS" || f[0] == "DMEM") continue;
        g.body_lines++;
        uint64_t c = std::stoull(f[0]);
        if (f.size() < 2) continue;
        if (f[1] == "MEMW") {
            uint32_t a = uint32_t(std::stoul(f[2], nullptr, 16)), v = uint32_t(std::stoul(f[3], nullptr, 16));
            BenchEvent* last = g.events.empty() ? nullptr : &g.events.back();
            if (last && last->kind == T_MEMW && last->t == c - 1 && last->n < 4 && last->addr + 4 * last->n == a) {
                last->v[last->n++] = v;
            } else {
                if (c < 2) { err = "a MEMW at cycle " + f[0] + ": before the first cycle the bench can act"; return false; }
                g.events.push_back(BenchEvent{c - 1, T_MEMW, a, 1, {v, 0, 0, 0}});
            }
        } else if (f[1] == "GO" || f[1] == "CMD") {
            if (c < 2) { err = "a " + f[1] + " at cycle " + f[0] + ": before the first cycle the bench can act"; return false; }
            g.events.push_back(BenchEvent{c - 1, uint8_t(f[1] == "GO" ? T_GO : T_CMD), 0, 0, {}});
        } else if (f[1] == "REQ-FREED") {
            g.events.push_back(BenchEvent{c, T_FREED, 0, 0, {}});
        }
    }
    // The bench acts in cycle order (a REQ-FREED at C acts after a MEMW labelled C + 1, both at C: keep file order
    // within a cycle, which is the bench's).
    for (size_t i = 1; i < g.events.size(); i++)
        if (g.events[i].t < g.events[i - 1].t) {
            // stable insertion: events are nearly sorted (only REQ-FREED at C precedes MEMW/GO/CMD labelled C + 1)
            for (size_t j = i; j > 0 && g.events[j].t < g.events[j - 1].t; j--) std::swap(g.events[j], g.events[j - 1]);
        }
    if (g.end_cycle == 0) { err = path + ": no END line"; return false; }
    return true;
}

// ---------------------------------------------------------------------------------------------------------- images
bool load_images(const RunSpec& r, Images& im, std::string& err) {
    std::string code, data, base = r.dir + "/" + r.image;
    if (!read_bytes(base + ".code.bin", code)) { err = "cannot read " + base + ".code.bin"; return false; }
    im.code = words(code);
    im.code_sha = sha256_hex(code.data(), code.size());
    if (read_bytes(base + ".data.bin", data)) { im.data = words(data); im.data_sha = sha256_hex(data.data(), data.size()); }
    else im.data_sha = "none";
    std::vector<std::string> sym;
    if (read_lines(base + ".sym", sym)) {                                // TraceRun: _data_start and _start
        std::map<std::string, uint32_t> s;
        for (const std::string& l : sym) { auto f = fields(l); if (f.size() == 3) s[f[2]] = uint32_t(std::stoul(f[0], nullptr, 16)); }
        if (s.count("_data_start")) im.data_at = s["_data_start"];
        if (!s.count("_start")) { err = base + ".sym: no _start"; return false; }
        im.entry = s["_start"];
    }
    std::vector<std::string> rom;
    if (!read_lines("fw/boot/bootrom.hex", rom)) { err = "cannot read fw/boot/bootrom.hex"; return false; }
    for (const std::string& l : rom) { auto f = fields(l); if (!f.empty()) im.bootrom.push_back(uint32_t(std::stoul(f[0], nullptr, 16))); }
    if (im.bootrom.size() > BOOTROM_MAX_WORDS) { err = "the boot ROM image is larger than 64 words"; return false; }
    im.bootrom_sha = words_sha(im.bootrom.data(), im.bootrom.size());
    if (!r.schedule.empty()) {
        std::vector<std::string> tb;
        if (!read_lines("fw/m1/" + r.schedule, tb)) { err = "cannot read fw/m1/" + r.schedule; return false; }
        for (std::string l : tb) {
            size_t h = l.find('#');
            if (h != std::string::npos) l = l.substr(0, h);
            size_t a = l.find_first_not_of(" \t"), b = l.find_last_not_of(" \t");
            if (a != std::string::npos) im.tb.push_back(l.substr(a, b - a + 1));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------- the header
TileConfig tile_config(unsigned tile) { TileConfig c; c.tile = tile; return c; }

KeyValues config_keys(const TileConfig& c) {
    auto n = [](uint64_t v) { return std::to_string(v); };
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    // The keys the controller doesn't use (they set the cycles of what the bench replays, or nothing in M1) carry ref's
    // M1 values: the run is refused if a golden was made under others.
    return {
        {"reg_load_latency", n(c.timing.reg_load_latency)}, {"branch_penalty", n(c.timing.branch_penalty)},
        {"mul_latency", n(c.timing.mul_latency)}, {"ref_cycle", n(c.timing.ref_cycle)},
        {"go_latency", "8"}, {"ready_latency", "8"}, {"error_latency", "8"},
        {"start_skew", n(uint64_t(start_skew(c.tile)))}, {"stage_delay", "64"}, {"cnc_reaction", "1"},
        {"cnc_drain_reports", "false"}, {"cnc_go_settle", "true"}, {"done_gate", b(c.done_gate)},
        {"request_fifo_depth", n(c.request_fifo_depth)}, {"req_hop_latency", "3"}, {"link_latency", "3"},
        {"intra_tile_latency", "1"}, {"cm_latency", "2"}, {"queue_depth", "16"}, {"K", "8"},
        {"mem_chunks", n(c.mem_chunks)}, {"periph_type", "4"}, {"cc_cores", "false"}, {"idle_skip", "true"},
        {"shuffle_seed", "-1"},
    };
}

std::vector<std::string> header_lines(const RunSpec& r, const Images& im, const TileConfig& c) {
    std::vector<std::string> h;
    h.push_back("# meshsim retire trace v1 (sim/tests/TRACE-FORMAT.md)");
    h.push_back("# program " + r.image);
    h.push_back("# tile " + std::to_string(r.tile));
    h.push_back("# image code " + im.code_sha + " data " + im.data_sha + " data_at " + hex8(im.data_at) + " entry " + hex8(im.entry));
    h.push_back("# bootrom " + im.bootrom_sha);
    for (auto& kv : config_keys(c)) h.push_back("# config " + kv.first + " " + kv.second);
    for (auto& s : im.tb) h.push_back("# tb " + s);
    return h;
}

std::string check_header(const Golden& g, const std::vector<std::string>& ours) {
    std::vector<std::string> theirs;
    for (const std::string& l : g.lines) { if (l.empty() || l[0] != '#') break; theirs.push_back(l); }
    // Name the difference: a config key by name, any other line by what it is.
    std::map<std::string, std::string> tk, ok;
    for (auto& l : theirs) { auto f = fields(l); if (f.size() >= 4 && f[1] == "config") tk[f[2]] = f[3]; }
    for (auto& l : ours) { auto f = fields(l); if (f.size() >= 4 && f[1] == "config") ok[f[2]] = f[3]; }
    for (auto& kv : ok) {
        auto it = tk.find(kv.first);
        if (it == tk.end()) return "refused (D12): the golden has no config " + kv.first + " (ours: " + kv.second + ")";
        if (it->second != kv.second) return "refused (D12): config " + kv.first + ": golden " + it->second + ", ours " + kv.second;
    }
    for (auto& kv : tk)
        if (!ok.count(kv.first)) return "refused (D12): config " + kv.first + " " + kv.second + " in the golden, unknown here";
    size_t n = theirs.size() > ours.size() ? theirs.size() : ours.size();
    for (size_t i = 0; i < n; i++) {
        std::string a = i < theirs.size() ? theirs[i] : "(none)", b = i < ours.size() ? ours[i] : "(none)";
        if (a != b) return "refused (D12): header line " + std::to_string(i + 1) + ": golden '" + a + "', ours '" + b + "'";
    }
    return "";
}

// ---------------------------------------------------------------------------------------------------------- the tile
Tile::Tile(const Images& im, const TileConfig& c, uint32_t trace_capacity)
    : dmem(MEM_WORDS, 0), imem(MEM_WORDS, 0), trace(trace_capacity) {
    for (size_t i = 0; i < im.code.size(); i++) imem[i & (MEM_WORDS - 1)] = im.code[i];
    for (size_t i = 0; i < im.data.size(); i++) dmem[((offset(im.data_at) + 4 * i) & (MEM_BYTES - 1)) >> 2] = im.data[i];
    core.timing = c.timing;
    core.periph_present = c.periph_present;
    core.dmem = dmem.data(); core.imem = imem.data();
    core.bootrom = im.bootrom.data(); core.bootrom_words = uint32_t(im.bootrom.size());
    core.log = &trace.log;
    block.params = params_value(c.periph_present, c.tile, c.mem_chunks);
    block.request_fifo_depth = c.request_fifo_depth;
    block.done_gate = c.done_gate;
}

std::vector<std::string> closing_lines(const Rv32& c, const uint32_t* dmem, uint64_t end_cycle) {
    std::vector<std::string> out;
    std::string how = c.stop == Stop::halt ? "halt " + std::to_string(c.halt)
                    : c.stop == Stop::parked ? "parked" : c.stop == Stop::ub ? "ub" : "timeout";
    out.push_back("END " + how + " cycle " + std::to_string(end_cycle));
    std::string r = "REGS";
    for (int i = 1; i < 32; i++) r += " " + hex8(c.x[i]);
    out.push_back(r);
    char b[256];
    snprintf(b, sizeof b, "CSRS pc %08x mstatus %08x mie %08x mtvec %08x mscratch %08x mepc %08x mcause %08x mtval %08x mcycle %016llx",
             c.pc, c.mstatus, c.mie, c.mtvec, c.mscratch, c.mepc, c.mcause, c.mtval, (unsigned long long)c.mcycle);
    out.push_back(b);
    out.push_back("DMEM " + words_sha(dmem, MEM_WORDS));
    return out;
}

// ---------------------------------------------------------------------------------------------------------- direct
// ref's cycle, as Chip and Tile run it for one tile: the request bus first (REQ-FREED), the port-B write of t - 1
// committed (MEMW), the core, then TileControl.endCycle(t) with GO and a landing seen at t (labelled t + 1).
std::vector<std::string> run_direct(Tile& tile, const Golden& g, const std::vector<std::string>& header) {
    struct Env {
        RegBlockState& b; bool freed; RegBlockState::In& in;
        uint32_t reg(unsigned r) const { return b.reg(r); }
        bool wake() const { return b.wake(); }
        bool irq() const { return b.irq(); }
        bool can_push() const { return !b.busy() || freed; }
        void post(uint32_t op, uint32_t arg, uint32_t v) {
            if (in.op) FATAL("direct driver: two bus operations in one cycle");
            in.op = op; in.arg = arg; in.v = v;
        }
    };
    TraceLog& log = tile.trace.log;
    const auto& ev = g.events;
    size_t i = 0;
    uint64_t t = 1;
    sim::begin_run();                                  // the loop allocates nothing (the hook aborts if it does)
    for (; t <= g.end_cycle; t++) {
        RegBlockState::In in;
        bool freed = false;
        for (; i < ev.size() && ev[i].t == t - 1 && ev[i].kind != T_FREED; i++) {   // acted at t - 1: seen from t
            const BenchEvent& e = ev[i];
            if (e.kind == T_MEMW) {
                uint32_t* m = region(e.addr) == R_PMEM ? tile.imem.data() : tile.dmem.data();
                for (uint32_t k = 0; k < e.n; k++) {
                    m[(((offset(e.addr) & (MEM_BYTES - 1)) >> 2) + k) & (MEM_WORDS - 1)] = e.v[k];
                    log.add(t, T_MEMW, 0, e.addr + 4 * k, e.v[k]);
                }
            } else log.add(t, e.kind);                 // GO, CMD: latched by endCycle(t - 1)
        }
        for (; i < ev.size() && ev[i].t == t && ev[i].kind == T_FREED; i++) { freed = true; log.add(t, T_FREED); }
        Env env{tile.block, freed, in};
        tile.core.step(env, t);
        if (tile.core.stop == Stop::ub) break;         // ref's UndefinedBehavior: endCycle(t) never runs
        in.freed = freed;
        for (size_t j = i; j < ev.size() && ev[j].t == t; j++) {
            if (ev[j].kind == T_GO) in.go = true;
            if (ev[j].kind == T_CMD) in.landed = true;
        }
        tile.block.end_cycle(t, in);
        if (tile.core.stop != Stop::running) {
            // ref wrote the lines labelled t + 1 at the end of t (Observer.endCycle)
            for (; i < ev.size() && ev[i].t == t && ev[i].kind != T_FREED; i++)
                for (uint32_t k = 0; k < (ev[i].kind == T_MEMW ? ev[i].n : 1); k++)
                    log.add(t + 1, ev[i].kind, 0, ev[i].addr + 4 * k, ev[i].v[k]);
            break;
        }
    }
    sim::end_run();
    uint64_t end = tile.core.stop != Stop::running ? tile.core.stop_cycle : g.end_cycle;
    std::vector<std::string> out = header;
    for (std::string& l : format_body(log)) out.push_back(l);
    if (log.full) out.push_back("(the trace log is full: more lines than the golden's)");
    for (std::string& l : closing_lines(tile.core, tile.dmem.data(), end)) out.push_back(l);
    return out;
}

// ---------------------------------------------------------------------------------------------------------- engine
sim::Sim build_engine(Tile& tile, const Golden& g, const sim::Options& opt) {
    sim::Config cfg(opt);
    configure_tile(cfg, "t", tile.core, tile.block, g.events.data(), uint32_t(g.events.size()), &tile.trace.log);
    return cfg.build();
}

EngineRun run_built(sim::Sim& s, Tile& tile, const Golden& g, const std::vector<std::string>& header, bool alloc_guard) {
    auto t0 = std::chrono::steady_clock::now();
    if (alloc_guard) sim::begin_run();                 // the units and the engine allocate nothing
    s.run(g.end_cycle);                                // cycles 1 .. END from a fresh machine (= run_until(END))
    if (alloc_guard) sim::end_run();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const Rv32& core = s.unit<Core>("t.core").rv;
    EngineRun out;
    uint64_t end = core.stop != Stop::running ? core.stop_cycle : g.end_cycle;
    out.lines = header;
    TraceLog& log = tile.trace.log;
    for (std::string& l : format_body(log)) out.lines.push_back(l);
    if (log.full) out.lines.push_back("(the trace log is full: more lines than the golden's)");
    for (std::string& l : closing_lines(core, core.dmem, end)) out.lines.push_back(l);
    out.digest = s.digest();
    out.sleepers = s.sleepers();
    out.seconds = seconds;
    out.instructions = core.instructions;
    out.steps[0] = s.steps("t.core"); out.steps[1] = s.steps("t.ctl"); out.steps[2] = s.steps("t.bench");
    return out;
}

EngineRun run_engine(Tile& tile, const Golden& g, const std::vector<std::string>& header, const sim::Options& opt) {
    sim::Sim s = build_engine(tile, g, opt);
    return run_built(s, tile, g, header);
}

static std::string describe(const sim::Options& o) {
    std::string s = o.step_all ? "step-all" : o.order == sim::Options::Order::instance_major ? "instance-major" : "ready-driven";
    if (o.shuffle_seed) s += ", shuffle seed " + std::to_string(o.shuffle_seed);
    return s;
}

// ---------------------------------------------------------------------------------------------------------- threads
std::vector<RunSpec> mixed_order(const std::vector<RunSpec>& runs) {
    std::vector<RunSpec> out;
    std::vector<bool> seen(runs.size());
    for (size_t k = 0, i = 0; k < runs.size(); k++, i = (i + 7) % runs.size()) {
        while (seen[i]) i = (i + 1) % runs.size();
        seen[i] = true; out.push_back(runs[i]);
    }
    return out;
}
std::string check_parallel(const std::vector<RunSpec>& runs, unsigned n, const std::vector<int>& cpus,
                           const std::vector<sim::Options>& opts) {
    // Everything read-only is prepared here, on the calling thread; each chip's tile and machine are built, run and
    // compared on its own thread (W4's run_independent), touching only its own slot.
    struct Prep { Golden g; Images im; TileConfig c; std::vector<std::string> header; uint64_t digest = 0; };
    std::vector<Prep> prep(runs.size());
    for (size_t i = 0; i < runs.size(); i++) {
        std::string err;
        Prep& p = prep[i];
        if (!read_golden("tests/golden/" + runs[i].name + ".trace", p.g, err) || !load_images(runs[i], p.im, err)) return err;
        p.c = tile_config(runs[i].tile);
        p.header = header_lines(runs[i], p.im, p.c);
        std::string refused = check_header(p.g, p.header);
        if (!refused.empty()) return runs[i].name + ": " + refused;
        Tile t(p.im, p.c, uint32_t(p.g.body_lines + 64));
        p.digest = run_engine(t, p.g, p.header, sim::Options{}).digest;        // the same golden alone, for the digest
    }
    struct Slot { std::unique_ptr<Tile> tile; std::string result; };
    std::string report;
    for (const sim::Options& opt : opts)
    for (size_t first = 0; first < runs.size(); first += n) {                // rounds of n chips, each a different golden
        unsigned m = unsigned(std::min<size_t>(n, runs.size() - first));
        std::vector<Slot> slot(m);
        sim::run_independent(m,
            [&](unsigned k) {
                const Prep& p = prep[first + k];
                slot[k].tile = std::make_unique<Tile>(p.im, p.c, uint32_t(p.g.body_lines + 64));
                return build_engine(*slot[k].tile, p.g, opt);
            },
            [&](unsigned k, sim::Sim& s) {
                const Prep& p = prep[first + k];
                EngineRun e = run_built(s, *slot[k].tile, p.g, p.header);
                std::string who = runs[first + k].name + " (thread " + std::to_string(k) + ", " + describe(opt) + "): ";
                std::string d = compare(e.lines, p.g.lines);
                if (!d.empty()) slot[k].result = who + d;
                else if (opt.step_all && e.steps[1] != p.g.end_cycle) slot[k].result = who + "step-all not in effect";
                else if (e.digest != p.digest) slot[k].result = who + "digest differs from the same golden run alone, ready-driven";
                else if (!e.sleepers.empty()) slot[k].result = runs[first + k].name + ": asleep with data waiting:\n" + e.sleepers;
            },
            cpus);
        for (Slot& sl : slot) if (!sl.result.empty()) report += sl.result + "\n";
    }
    return report;
}

BenchResult bench_engine(const RunSpec& r, unsigned reps, bool trace) {
    BenchResult b;
    Golden g; Images im; std::string err;
    if (!read_golden("tests/golden/" + r.name + ".trace", g, err) || !load_images(r, im, err)) return b;
    TileConfig c = tile_config(r.tile);
    auto header = header_lines(r, im, c);
    for (unsigned k = 0; k < reps; k++) {
        Tile tile(im, c, uint32_t(g.body_lines + 64));
        if (!trace) tile.core.log = nullptr;           // the core writes no trace (the bench's few lines remain)
        EngineRun e = run_engine(tile, g, header, sim::Options{});
        b.seconds += e.seconds; b.instructions += e.instructions; b.cycles += g.end_cycle;
        b.steps += e.steps[0] + e.steps[1] + e.steps[2];
    }
    return b;
}

std::vector<sim::Options> debug_check_options() {
    std::vector<sim::Options> o(6);
    o[1].shuffle_seed = 1; o[2].shuffle_seed = 2; o[3].shuffle_seed = 3;
    o[4].order = sim::Options::Order::instance_major;
    o[5].step_all = true;
    return o;
}

std::string check_engine(const RunSpec& r, const std::vector<sim::Options>& opts) {
    Golden g; Images im; std::string err;
    if (!read_golden("tests/golden/" + r.name + ".trace", g, err)) return err;
    if (!load_images(r, im, err)) return err;
    TileConfig c = tile_config(r.tile);
    std::vector<std::string> header = header_lines(r, im, c);
    std::string refused = check_header(g, header);
    if (!refused.empty()) return refused;
    uint64_t digest0 = 0;
    for (size_t k = 0; k < opts.size(); k++) {
        Tile tile(im, c, uint32_t(g.body_lines + 64));
        EngineRun e = run_engine(tile, g, header, opts[k]);
        std::string d = compare(e.lines, g.lines);
        if (!d.empty()) {
            Tile t2(im, c, uint32_t(g.body_lines + 64));
            std::string dd = compare(e.lines, run_direct(t2, g, header));
            return r.name + " (engine, " + describe(opts[k]) + "): " + d + "against the direct driver: "
                 + (dd.empty() ? "the same (so the unit logic, not the engine)\n" : dd);
        }
        if (!e.sleepers.empty()) return r.name + " (engine, " + describe(opts[k]) + "): asleep with data waiting:\n" + e.sleepers;
        if (opts[k].step_all && e.steps[1] != g.end_cycle)
            return r.name + " (engine, step-all): the block was stepped " + std::to_string(e.steps[1]) + " times in "
                 + std::to_string(g.end_cycle) + " cycles: step-all is not in effect";
        if (k == 0) digest0 = e.digest;
        else if (e.digest != digest0)
            return r.name + " (engine, " + describe(opts[k]) + "): digest " + std::to_string(e.digest) + ", ready-driven "
                 + std::to_string(digest0);
    }
    return "";
}

// ---------------------------------------------------------------------------------------------------------- compare
std::string compare(const std::vector<std::string>& ours, const std::vector<std::string>& golden) {
    size_t n = ours.size() > golden.size() ? ours.size() : golden.size();
    for (size_t i = 0; i < n; i++) {
        std::string a = i < ours.size() ? ours[i] : "(end)", b = i < golden.size() ? golden[i] : "(end)";
        if (a == b) continue;
        std::string s = "first difference at line " + std::to_string(i + 1) + "\n";
        size_t lo = i >= 5 ? i - 5 : 0;
        for (size_t k = lo; k < i; k++) s += "    " + golden[k] + "\n";
        s += "  golden: " + b + "\n  ours:   " + a + "\n";
        for (size_t k = i + 1; k < i + 6 && k < n; k++) {
            s += "  golden: " + (k < golden.size() ? golden[k] : "(end)") + "\n";
            s += "  ours:   " + (k < ours.size() ? ours[k] : "(end)") + "\n";
        }
        return s;
    }
    return "";
}

std::string check_direct(const RunSpec& r) {
    Golden g; Images im; std::string err;
    if (!read_golden("tests/golden/" + r.name + ".trace", g, err)) return err;
    if (!load_images(r, im, err)) return err;
    TileConfig c = tile_config(r.tile);
    std::vector<std::string> header = header_lines(r, im, c);
    std::string refused = check_header(g, header);
    if (!refused.empty()) return refused;
    Tile tile(im, c, uint32_t(g.body_lines + 64));
    std::string d = compare(run_direct(tile, g, header), g.lines);
    return d.empty() ? "" : r.name + ": " + d;
}

}  // namespace test
