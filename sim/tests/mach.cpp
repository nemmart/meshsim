// mach.cpp (WORKER20, P2): the machine harness (mach.h). Configuration and between-cycle code only (D6 doesn't apply).
#include "mach.h"

#include <sys/stat.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "base/fatal.h"
#include "checker.h"
#include "units/ag.h"
#include "units/big_sram.h"
#include "units/director.h"
#include "units/gather.h"
#include "units/inject.h"
#include "units/periph_slot.h"
#include "units/replayer.h"
#include "units/router.h"
#include "units/write_engine.h"
#include "units/cnc.h"
#include "units/host_port.h"
#include "units/linkword.h"
#include "units/net.h"
#include "units/table_store.h"
#include "units/tile_ctl.h"

namespace mach {

namespace {

bool exists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }

// A data file by its path in v1.35 (W19's tree; nothing here reads sim/ref).
std::string data_path(std::initializer_list<const char*> candidates) {
    for (const char* c : candidates) if (exists(c)) return c;
    std::string all;
    for (const char* c : candidates) { all += all.empty() ? "" : ", "; all += c; }
    FATAL("mach: none of %s found (run from sim/)", all.c_str());
}

bool read_file(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[1 << 16];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

std::vector<uint32_t> words_of(const std::string& path) {   // Mach.words: little-endian, zero-padded to a word
    std::string b;
    if (!read_file(path, b)) FATAL("mach: cannot read %s", path.c_str());
    std::vector<uint32_t> w((b.size() + 3) / 4, 0);
    for (size_t i = 0; i < b.size(); i++) w[i >> 2] |= uint32_t(uint8_t(b[i])) << (8 * (i & 3));
    return w;
}

std::vector<uint32_t> bootrom() {
    std::string rom;
    const std::string p = data_path({"fw/boot/bootrom.hex"});
    if (!read_file(p, rom)) FATAL("mach: cannot read %s", p.c_str());
    std::vector<uint32_t> w;
    for (size_t at = 0; at < rom.size();) {
        size_t e = rom.find('\n', at);
        if (e == std::string::npos) e = rom.size();
        if (e > at) w.push_back(uint32_t(std::stoul(rom.substr(at, e - at), nullptr, 16)));
        at = e + 1;
    }
    return w;
}

constexpr uint32_t PMEM_BASE = 0x01000000, CMD_JUMP = 0x4A;

}  // namespace

// ================================================================================================= the configuration
MachConfig MachConfig::defaults() { return MachConfig{}; }
MachConfig MachConfig::machine() {
    MachConfig c;
    c.coreless = false;
    c.real_responder = true;
    return c;
}
unsigned MachConfig::k_min() const {
    const unsigned L = ts_engine_latency;
    return std::max(std::max(2 * L + 4, L + 4), std::max(cm_latency, rp_latency));
}
MachConfig& MachConfig::with_ts_latency(unsigned L) {
    ts_engine_latency = L;
    K = std::max(K, k_min());
    return *this;
}
bool MachConfig::set(const std::string& field, const std::string&, std::string& err) {
    err = "cfg." + field + "=: the Config front end comes with W20's step 3";
    return false;
}

// ===================================================================================================== the chip
struct Chip::Impl {
    units::Hops hops;
    std::vector<uint32_t> rom, mem;
    units::SramBlocks sram;
    units::G1Ctx gx;
    units::CncProgram prog;
    std::optional<sim::Sim> sim;
    unsigned nunits = 64;
    uint8_t skew[64] = {};
    std::unique_ptr<Cc> cc;
    std::unique_ptr<Checker> checker;                    // ref's violations (src/check.h; on with SIM_CHECKS)
    // ref's hardware FIFOs for invariant 1 (Tile.hardwareFifos, then C&C's receive FIFOs), in ref's scan order
    struct Hw { int tile; units::Router* router; unsigned q; const sim::FifoIn* in; bool reported; };
    std::vector<Hw> hw;
    void collect_overflows();
    int64_t last_run_at = -1;
    // C&C's CALL steps run between cycles (Cc::call): id -> the harness's function, run after the cycle it ran in
    struct Call { uint32_t id; std::function<void(int64_t)> fn; };
    std::vector<Call> calls;
    std::vector<uint32_t> free_ids;
    struct Wait { uint32_t step; std::function<bool()> cond; };   // Cc::wait_until's, in order
    std::vector<Wait> waits;
    void check_waits();
    // Chip::on_row's hooks: tile, its replayer, the buffer it sinks into, the function
    struct RowHook { unsigned tile; units::Replayer* r; std::vector<uint64_t> buf; std::function<void(const uint64_t*, int64_t)> fn; };
    std::vector<RowHook> row_hooks;
    void drain_rows(int64_t cycle);
    // Net (the coreless machine fed from the harness): each tile's feed (inject.h's HarnessFeed, harness-only) and the
    // rings' storage (grown between cycles, the feed's pointers refreshed), and the directors' logs (fixed size)
    using E4 = std::array<uint64_t, 4>;
    std::vector<units::HarnessFeed> feeds;
    std::vector<std::vector<E4>> ring_store;            // tile k's ring r (0: ret, 1-3: clients 0-2) at 4k + r
    std::vector<std::vector<E4>> logs;
    std::vector<E4> writer_logs[64], gather_logs[64];     // TileRef::record_writer, record_gather
    // the endpoints' harness parts (cnc.h's HarnessSends): C&C 1 (the Java model), C&C 2, C&C 3
    units::HarnessSends sends[3];
    std::vector<units::HarnessSends::E> send_store[3];
    std::vector<E4> recv[3];
    void push_send(unsigned cc, const units::HarnessSends::E& e);
    bool net = false;
    std::vector<uint16_t> shifted;                      // the register block's and the core's types (Sim::in_flight)
    std::vector<uint16_t> lines;                        // C&C's row collectors and C&C: ref's READY and error lines
    void enable_net(const MachConfig& c);
    void push(unsigned tile, unsigned ring, const E4& e);

    bool has_cnc = false;
    units::Cnc* cnc() { return has_cnc ? &sim->unit<units::Cnc>("cnc") : nullptr; }
    uint32_t* dmem(unsigned u) { return mem.data() + size_t(u) * 2 * units::MEM_WORDS; }
    uint32_t* imem(unsigned u) { return dmem(u) + units::MEM_WORDS; }
    void run_calls();
};

Chip::Chip(const MachConfig& c) : cfg_(c), impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    if (!c.start_skew.empty()) FATAL("mach: a per-tile start_skew comes with W20's step 3");
    if (c.K < c.k_min())                                   // TrafficDirector's constructor (ref: IllegalArgumentException)
        FATAL("K = %u is below meshsim's max(2L + 4, L + 4, cm_latency, rp_latency) = %u at L = %u (4.7, D49; invariant 24)",
              c.K, c.k_min(), c.ts_engine_latency);
    if (c.cc3_tracer && !c.cc_cores) FATAL("mach: cc3_tracer needs cc_cores (ref's Config)");
    m.hops = units::load_hops(data_path({"data/hops.txt"}).c_str());
    m.rom = bootrom();
    m.nunits = c.cc_cores ? 67 : 64;
    m.mem.assign(size_t(m.nunits) * 2 * units::MEM_WORDS, 0);

    units::G1Config& g = m.gx.cfg;
    g.timing.reg_load_latency = c.reg_load_latency;
    g.timing.ref_cycle = c.ref_cycle;
    g.done_gate = c.done_gate;
    g.mem_chunks = c.mem_chunks;
    g.periph_present = c.periph_type != 0 ? 1 : 0;
    g.periph_slot = c.periph_type != 0 || c.cc_cores;     // ref: every tile's slot holds its peripheral 1 (type 4)
    if (c.periph_type != 0 && c.periph_type != 4) FATAL("mach: periph_type %d (4 the replayer, or 0)", c.periph_type);
    if (c.test_periph_tile >= 64) FATAL("mach: test_periph_tile %d (a mesh tile)", c.test_periph_tile);
    if (c.test_periph_tile >= 0) {                        // ref's testPeripheral: the replayer on that tile only
        if (c.periph_type != 0) FATAL("mach: test_periph_tile needs periph_type 0 (ref's StageB: periphType 0)");
        g.periph_present = 1; g.periph_slot = true; g.periph_only = c.test_periph_tile;
    }
    g.cc3_tracer = c.cc3_tracer;
    g.stray_check = sim::check::kOn;                      // the director's stray check (a checker fact: on with SIM_CHECKS)
    g.K = c.K; g.cm_latency = c.cm_latency;
    g.ts_engine_latency = c.ts_engine_latency;
    g.ag_lookahead = c.ag_lookahead > 0 ? unsigned(c.ag_lookahead) : 0;
    g.area_queue_depth = c.area_queue_depth; g.sram_read_latency = c.sram_read_latency;
    g.read_port_depth = c.read_port_depth;
    g.write_combining = c.write_combining; g.fifo_count_before_pop = c.fifo_count_before_pop;
    m.gx.bootrom = m.rom.data(); m.gx.bootrom_words = uint32_t(m.rom.size());
    m.gx.mem = m.mem.data(); m.gx.units = m.nunits; m.gx.sram = &m.sram;
    m.gx.we_in_depth = c.we_in_depth; m.gx.gather_depth = c.gather_depth;

    units::NetConfig nc;
    nc.link_latency = c.link_latency; nc.queue_depth = c.queue_depth; nc.intra_tile_latency = c.intra_tile_latency;
    nc.read_tokens = c.read_tokens;
    nc.go_latency = c.go_latency; nc.ready_latency = c.ready_latency; nc.cnc_reaction = c.cnc_reaction;
    nc.cnc_rx_depth = c.cnc_rx_depth; nc.skew = c.skew; nc.skew_seed = c.skew_seed;
    nc.cc_link_latency = c.cc_link_latency; nc.cc_dma_depth = c.cc_dma_depth;
    units::start_skews(nc, m.skew);

    m.prog.release();                                     // C&C's queue starts empty; the harness appends (Cc)
    sim::Options opt;
    if (c.shuffle_seed >= 0) opt.shuffle_seed = uint64_t(c.shuffle_seed) + 1;   // 0 is "none" in the framework
    opt.step_all = c.step_all;
    opt.order = c.instance_major ? sim::Options::Order::instance_major : sim::Options::Order::type_major;
    opt.threads = c.threads;
    opt.observe_arrivals = true;                          // the register block as ref has it at the end of a cycle
    sim::Config sc(opt);
    uint8_t dom[64];
    units::row_bands(c.parts, dom);
    units::ChipSpec spec;
    spec.net = nc; spec.hops = &m.hops; spec.control = units::add_g1; spec.control_ctx = &m.gx;
    spec.domain = c.parts > 1 ? dom : nullptr;
    spec.cc_cores = c.cc_cores; spec.cc3_dma = c.cc3_tracer; spec.host_capacity = c.host_capacity;
    if (!c.cc_cores) spec.program = &m.prog;
    units::build_chip(sc, spec);
    m.sim.emplace(sc.build());
    // The harness's machine (W20, harness-only flags in the units): the coreless core (ref's Config.coreless), ref's Drain
    // in the GATHER engine's place (realResponder off), and the table stores' republish of a harness poke.
    for (unsigned k = 0; k < 64; k++) {
        if (c.coreless) m.sim->unit<units::TileCore>(units::tile_unit(k, "core")).coreless = true;
        if (!c.real_responder) m.sim->unit<units::Gather>(units::tile_unit(k, "gather")).drain = true;
        m.sim->unit<units::TableStore>(units::tile_unit(k, "ts")).host_poke = true;
        m.sim->unit<units::HistStore>(units::tile_unit(k, "hist")).host_poke = true;
        m.sim->unit<units::TileCtl>(units::tile_unit(k, "ctl")).host_on = true;
    }
    m.shifted = {m.sim->type_of(units::tile_unit(0, "ctl")), m.sim->type_of(units::tile_unit(0, "core"))};
    if (!c.cc_cores) m.lines = {m.sim->type_of("row0"), m.sim->type_of("cnc")};
    CheckSwitches sw;
    sw.errors = c.check_errors; sw.unexpected = c.check_unexpected; sw.read_port_overlap = c.check_read_port_overlap;
    sw.command_read = c.check_command_read; sw.strict_replies = c.strict_replies; sw.K = c.K;
    m.checker = std::make_unique<Checker>(sim::check::kOn ? c.check_capacity : 0, c.max_violation_messages, sw);
    for (unsigned k = 0; k < m.nunits; k++) {             // the units that record facts under their tile (src/check.h)
        units::TileCtl& b = m.sim->unit<units::TileCtl>(units::unit_prefix(k) + "ctl");
        b.ck_tile = int(k); b.s.ck_tile = int(k);
        m.sim->unit<units::TileCore>(units::unit_prefix(k) + "core").rv.ck_tile = int(k);
        if (k < 64) m.sim->unit<units::WriteEngine>(units::tile_unit(k, "we")).ck_tile = int(k);
    }
    if (sim::check::kOn) {                                // ref's FIFO names (Router, Tile, Endpoint)
        static const char* const kOutName[6] = {"to-right", "to-left", "to-down", "to-up", "eject", "to-cnc"};
        static const char* const kPortName[6] = {"from-left", "from-right", "from-up", "from-down", "loopback", "from-cnc"};
        for (unsigned k = 0; k < 64; k++) {
            units::Router& r = m.sim->unit<units::Router>(units::tile_unit(k, "router"));
            const std::string t = "t" + std::to_string(k) + ".";
            for (unsigned o = 0; o < units::Router::kOut; o++)
                for (unsigned f = 0; f < r.nfeed[o]; f++) {
                    m.hw.push_back({int(k), &r, r.first[o] + f, nullptr, false});
                    m.checker->fifo_names.push_back(t + kOutName[o] + "<" + kPortName[r.feed[o][f]]);
                }
            m.hw.push_back({int(k), nullptr, 0, &m.sim->unit<units::WriteEngine>(units::tile_unit(k, "we")).in, false});
            m.checker->fifo_names.push_back(t + "we-in");
            m.hw.push_back({int(k), nullptr, 0, &m.sim->unit<units::Gather>(units::tile_unit(k, "gather")).in, false});
            m.checker->fifo_names.push_back(t + "gather-q");
        }
        if (!c.cc_cores) {
            m.hw.push_back({-1, nullptr, 0, &m.sim->unit<units::Cnc>("cnc").rx, false}); m.checker->fifo_names.push_back("cc1.rx");
            m.hw.push_back({-1, nullptr, 0, &m.sim->unit<units::Endpoint>("ep2").rx, false}); m.checker->fifo_names.push_back("cc2.rx");
            m.hw.push_back({-1, nullptr, 0, &m.sim->unit<units::Endpoint>("ep3").rx, false}); m.checker->fifo_names.push_back("cc3.rx");
        }
    }
    m.has_cnc = !c.cc_cores;
    if (!c.cc_cores)                                      // the endpoints' receive traces (ref's received), every machine
        for (unsigned k = 0; k < 3; k++) {
            m.recv[k].assign(c.log_capacity, {});
            m.sends[k].log = reinterpret_cast<uint64_t(*)[4]>(m.recv[k].data());
            m.sends[k].log_cap = c.log_capacity;
            if (k == 0) m.sim->unit<units::Cnc>("cnc").hs = &m.sends[0];
            else m.sim->unit<units::Endpoint>("ep" + std::to_string(k + 1)).hs = &m.sends[k];
        }
    if (!c.cc_cores) m.cc = std::make_unique<Cc>(*this);
    for (uint32_t id = units::CncProgram::kCalls; id-- > 0;) m.free_ids.push_back(id);
}

Chip::~Chip() { if (impl_ && impl_->checker) impl_->checker->uninstall(); }

sim::Sim& Chip::sim() { return *impl_->sim; }
void Chip::touched(const std::string& name) { impl_->sim->wake(name); }
int64_t Chip::cycle() const { return int64_t(impl_->sim->now()) - 1; }

// Invariant 1's facts: every hardware FIFO that has overflowed since the last look, at its first overflow's put cycle
// (ref's overflowCycle), once per FIFO (ref's reportedOverflows), before the facts are processed.
void Chip::Impl::collect_overflows() {
    if (!sim::check::kOn) return;
    for (size_t i = 0; i < hw.size(); i++) {
        Hw& h = hw[i];
        if (h.reported) continue;
        uint64_t n = 0, first = 0;
        if (h.router) { n = h.router->q[h.q].overflows; first = h.router->q[h.q].first_overflow; }
        else { sim::Cycle f = 0; n = sim->fifo_overflows(*h.in, &f); first = f; }
        if (!n) continue;
        h.reported = true;
        checker->add({first, h.tile, sim::check::Kind::FIFO_OVERFLOW, 0, {uint64_t(i), 0, 0, 0}});
    }
}

void Chip::Impl::run_calls() {
    units::Cnc* c = cnc();
    if (!c || calls.empty()) return;
    for (size_t i = 0; i < calls.size();) {
        const int64_t at = c->call_at[calls[i].id];
        if (at < 0) { i++; continue; }
        Call k = std::move(calls[i]);
        calls.erase(calls.begin() + long(i));
        c->call_at[k.id] = -1;
        free_ids.push_back(k.id);
        k.fn(at);
    }
}

// Cc::wait_until: the first pending wait, once C&C is at its step, released when its condition holds (between cycles)
void Chip::Impl::check_waits() {
    if (waits.empty()) return;
    units::Cnc* c = cnc();
    if (!c || c->cur != waits.front().step || !waits.front().cond()) return;
    c->host_released++;
    waits.erase(waits.begin());
    sim->wake("cnc");
}

void Chip::Impl::drain_rows(int64_t cycle) {
    for (RowHook& h : row_hooks) {
        const uint64_t n = h.r->rows_sunk() - h.r->row_base();
        for (uint64_t i = 0; i < n; i++) h.fn(h.buf.data() + i * units::Replayer::REC_LONGS, cycle);
        h.r->drained();
    }
}
void Chip::on_row(unsigned tile, std::function<void(const uint64_t*, int64_t)> fn) {
    units::PeriphSlot& sl = unit<units::PeriphSlot>(units::tile_unit(tile, "slot"));
    Impl::RowHook h{tile, &sl.s.replayer(1), std::vector<uint64_t>(size_t(64) * units::Replayer::REC_LONGS), std::move(fn)};
    impl_->row_hooks.push_back(std::move(h));
    Impl::RowHook& r = impl_->row_hooks.back();
    r.r->drained();
    r.r->set_row_buffer(r.buf.data(), 64);                 // at most one row a cycle leaves (the sink): 64 is plenty
}
bool Chip::parked_untouched() const {                      // ref's: each parked tile quiescent, no link into or out of it busy
    if (cfg_.parked.empty()) return true;
    for (int k : cfg_.parked) {
        TileRef p = const_cast<Chip*>(this)->tile(unsigned(k));
        for (const auto& f : p.unit<units::Router>("router").q) if (f.count) return false;
        const units::Injection& in = p.unit<units::Injection>("inj");
        if (p.unit<units::AddrGen>("ag").running || p.ts_busy() || in.tok.reads || in.rs.nxt_v || in.rs.cur_v ||
            p.unit<units::BigSram>("sram").busy || p.unit<units::WriteEngine>("we").count || p.unit<units::Gather>("gather").left)
            return false;
        if (impl_->sim->in_flight_into(units::unit_prefix(unsigned(k)))) return false;   // its ports (the links into it)
    }
    for (unsigned k = 0; k < 64; k++) if (impl_->sim->in_flight_into(units::tile_unit(k, "router"))) return false;
    return true;
}

void Chip::step() {
    impl_->checker->install();
    impl_->sim->run(1);
    impl_->collect_overflows();
    impl_->checker->process(cycle(), false);
    impl_->run_calls();
    impl_->check_waits();
    impl_->drain_rows(cycle());
}
void Chip::run(uint64_t n) {
    impl_->sim->wake_all();
    if (impl_->calls.empty() && impl_->waits.empty() && impl_->row_hooks.empty()) {
        impl_->checker->install();
        impl_->sim->run(n);
        impl_->collect_overflows();
        impl_->checker->process(cycle(), false);
    }
    else for (uint64_t i = 0; i < n; i++) step();
    scan_overflows();
}
bool Chip::run_until(const std::function<bool()>& cond, uint64_t max) {
    impl_->sim->wake_all();
    bool ok = false;
    for (uint64_t i = 0; i < max && !(ok = cond()); i++) step();
    if (!ok) ok = cond();
    scan_overflows();
    return ok;
}
bool Chip::drain(uint64_t max) { return run_until([this] { return quiescent(); }, max); }
// ref's Chip.quiescent: nothing in motion anywhere (the framework's ports: links, FIFOs, holds, the K line, the
// endpoints), and every tile's units idle as ref's Tile.quiescent judges them (the router's FIFOs, the AG, the table
// stores, the read queue, big SRAM's area queues, the write engine's stage, the gather engine, the read port).
bool Chip::quiescent() const {
    sim::Sim& s = *impl_->sim;
    if (s.in_flight(impl_->shifted, impl_->lines)) return false;
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = const_cast<Chip*>(this)->tile(k);
        for (const auto& f : p.unit<units::Router>("router").q) if (f.count) return false;   // the router's own FIFOs
        if (p.unit<units::AddrGen>("ag").running || p.ts_busy()) return false;
        const units::Injection& in = p.unit<units::Injection>("inj");
        if (in.tok.reads || in.rs.nxt_v || in.rs.cur_v) return false;
        if (p.unit<units::BigSram>("sram").busy) return false;
        if (p.unit<units::WriteEngine>("we").count) return false;
        if (p.unit<units::Gather>("gather").left) return false;
        // the read port (ref's readPort.idle): its results stored and not yet injected (none under the test clients)
        if ((!in.rs.feed || !in.rs.feed->clients) && p.unit<units::TileCore>("core").rp_stored != in.rs.rp_injected_n) return false;
    }
    return true;
}
std::vector<int> Chip::start_skew() const { return std::vector<int>(impl_->skew, impl_->skew + 64); }

Cc& Chip::cc() {
    if (!impl_->cc) FATAL("mach: Chip::cc(): C&C 1 is a core (cc_cores): use Mach::cc1_command and phase");
    return *impl_->cc;
}

bool Chip::all_stopped() const {
    for (unsigned k = 0; k < 64; k++) if (impl_->sim->unit<units::TileCore>(units::tile_unit(k, "core")).rv.halt == units::Rv32::RUN) return false;
    return true;
}
bool Chip::all_exited() const {
    for (unsigned k = 0; k < 64; k++) if (impl_->sim->unit<units::TileCore>(units::tile_unit(k, "core")).rv.halt != units::Rv32::HALT_EXIT) return false;
    return true;
}
bool Chip::all_in_wfi() const {
    for (unsigned k = 0; k < 64; k++) if (!impl_->sim->unit<units::TileCore>(units::tile_unit(k, "core")).rv.in_wfi) return false;
    return true;
}

void Chip::host_load(unsigned c, uint32_t byte_addr, const std::vector<uint32_t>& words) {
    if (!cfg_.cc_cores) FATAL("mach: host_load needs cc_cores");
    const std::string n = "cc" + std::to_string(c + 1) + ".host";
    impl_->sim->unit<units::HostPort>(n).load(byte_addr, words.data(), words.size());
    impl_->sim->wake(n);
}
void Chip::host_command(unsigned c, uint32_t word, const std::vector<uint32_t>& args) {
    if (!cfg_.cc_cores) FATAL("mach: host_command needs cc_cores");
    const std::string n = "cc" + std::to_string(c + 1) + ".host";
    impl_->sim->unit<units::HostPort>(n).command(word, args.data(), args.size());
    impl_->sim->wake(n);
}

// ---- the checker (checker.cpp): ref's Chip.violations, violationMessages, violationReport, scanOverflows ----
// The report through the cycle just run: the register blocks' ERRORS and error lines as ref has them at its end (their
// own facts for it come at their next step: block_at_end now; the duplicates later are harmless, the rule is
// idempotent), then every fact through it.
void Chip::finalize_checks() const {
    if (!sim::check::kOn) return;
    Chip& me = const_cast<Chip&>(*this);
    Checker& ck = *impl_->checker;
    for (unsigned k = 0; k < 64; k++) {
        const units::RegBlockState b = me.tile(k).block(false);   // ref's end of the cycle (not the test's bits since)
        const uint32_t v = b.conditions | b.live_errors();
        sim::check::Fact f{uint64_t(cycle()), int32_t(k), sim::check::Kind::ERRORS_SET, 0, {v, 0, 0, 0}};
        ck.add(f);
        f.kind = sim::check::Kind::ERROR_LINE; f.a[0] = b.error_line;
        ck.add(f);
    }
    ck.process(cycle(), false, true);
}
uint64_t Chip::violations() const { finalize_checks(); return impl_->checker->violations(); }
const std::vector<std::string>& Chip::violation_messages() const { finalize_checks(); return impl_->checker->messages(); }
void Chip::clear_violation_messages() { finalize_checks(); impl_->checker->clear_messages(); }
void Chip::set_check_errors(bool on) { finalize_checks(); impl_->checker->switches().errors = on; cfg_.check_errors = on; }
void Chip::set_strict_replies(bool on) { finalize_checks(); impl_->checker->switches().strict_replies = on; cfg_.strict_replies = on; }
void Chip::set_max_violation_messages(unsigned n) { finalize_checks(); impl_->checker->set_max_messages(n); cfg_.max_violation_messages = n; }
void Chip::clear_violations() { finalize_checks(); impl_->checker->clear_count(); impl_->checker->clear_messages(); }
void Chip::scan_overflows() { impl_->collect_overflows(); impl_->checker->process(cycle(), true); }
std::string Chip::violation_report() {
    scan_overflows();
    finalize_checks();
    std::string sb = "violations: " + std::to_string(violations()) + "\n";
    for (const std::string& s : violation_messages()) sb += "  " + s + "\n";
    return sb;
}
void Chip::reset(bool) { FATAL("mach: reset comes with W20's step 2"); }
uint64_t Chip::resets() const { return 0; }
int64_t Chip::last_reset_at() const { return -1; }
// ref's Chip.resetStats (Tile.resetStats, the units' own): statistics only, never state the machine runs on
void Chip::reset_stats() {
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = tile(k);
        units::Router& r = p.unit<units::Router>("router");
        for (auto& f : r.q) f.peak = f.count;
        r.injected = r.routed = r.ejected = r.eject_errors = r.route_errors = r.from_cnc_words = r.to_cnc_words = 0;
        units::Director& d = p.unit<units::Director>("dir");
        for (auto& x : d.per_mode) x = 0;
        d.accepted = d.bad_packets = 0;
        units::Injection& in = p.unit<units::Injection>("inj");
        in.injected = in.from_read = in.from_send = in.send_wait_cycles = 0;
        units::WriteEngine& we = p.unit<units::WriteEngine>("we");
        we.taken = we.written = we.stall_cycles = 0;
        units::TableStore& ts = p.unit<units::TableStore>("ts");
        ts.table_reads = ts.rmws = 0;
        if (sim::check::kOn) {                            // the framework FIFOs' peaks (kept under SIM_CHECKS: W22's)
            impl_->sim->fifo_restart_peak(we.in);
            impl_->sim->fifo_restart_peak(p.unit<units::Gather>("gather").in);
        }
    }
}

// ================================================================================================= a tile
namespace {
inline std::string un(unsigned k, const char* u) { return units::unit_prefix(k) + u; }
}

uint32_t TileRef::sram_peek(uint32_t chunk) const { return c_->unit<units::BigSram>(un(k_, "sram")).peek(chunk); }
void TileRef::sram_poke(uint32_t chunk, uint32_t v) { c_->unit<units::BigSram>(un(k_, "sram")).poke(chunk, v); c_->touched(un(k_, "sram")); }
uint64_t TileRef::sram_peek64(uint32_t chunk) const { return c_->unit<units::BigSram>(un(k_, "sram")).peek64(chunk); }
void TileRef::sram_poke64(uint32_t chunk, uint64_t v) { c_->unit<units::BigSram>(un(k_, "sram")).poke64(chunk, v); c_->touched(un(k_, "sram")); }

// The controller's memories (Controller.peekLocal / pokeLocal): region 0x00 data, 0x01 program, byte addresses.
namespace {
uint32_t* local(Chip::Impl& m, unsigned k, uint32_t a) {
    const uint32_t region = a >> 24, off = (a & 0xFFFFFF) >> 2;
    if ((region != 0 && region != 1) || off >= units::MEM_WORDS || (a & 3))
        FATAL("mach: tile %u: 0x%08x is not a word of the controller's program or data memory", k, a);
    return (region == 1 ? m.imem(k) : m.dmem(k)) + off;
}
}  // namespace
// A word of the controller's memory as ref has it at the end of the cycle just run: port B's write of that cycle
// reaches the C++ core at its next step (Link(cm_latency + 1)), so it is applied here over what the memory holds.
uint32_t TileRef::peek_local(uint32_t a) const {
    uint32_t v = *local(c_->impl(), k_, a);
    const units::TileCore& core = unit<units::TileCore>("core");
    if (core.rv.stop != units::Stop::running) return v;
    sim::PortEntry e[8];
    const uint32_t n = c_->sim().observe(core.port_b, e, 8).n;
    for (uint32_t i = 0; i < n; i++) {
        if (e[i].at != uint64_t(c_->cycle()) + 1) continue;
        const uint64_t h = e[i].w[1];
        const uint32_t pa = units::lw::addr(h), cnt = units::lw::chunks(units::lw::size(h));
        if ((units::lw::region(pa) == units::lw::R_DMEM) != ((a >> 24) == 0)) continue;
        const uint32_t w0 = (units::lw::offset(pa) & (units::MEM_BYTES - 1)) >> 2, w = (a & 0xFFFFFF) >> 2;
        for (uint32_t j = 0; j < cnt; j++)
            if (((w0 + j) & (units::MEM_WORDS - 1)) == w) v = uint32_t((j < 2 ? e[i].w[0] : e[i].w[2]) >> (32 * (j & 1)));
    }
    return v;
}
void TileRef::poke_local(uint32_t a, uint32_t v) { *local(c_->impl(), k_, a) = v; c_->touched(un(k_, "core")); }
uint32_t TileRef::dmem(uint32_t w) const {
    if (w >= units::MEM_WORDS) FATAL("mach: tile %u: dmem[%u] past its %u words", k_, w, units::MEM_WORDS);
    return peek_local(4 * w);
}

uint32_t TileRef::pc() const { return c_->unit<units::TileCore>(un(k_, "core")).rv.pc; }
uint32_t TileRef::x(unsigned r) const { return c_->unit<units::TileCore>(un(k_, "core")).rv.x[r & 31]; }
bool TileRef::in_wfi() const { return c_->unit<units::TileCore>(un(k_, "core")).rv.in_wfi; }
int TileRef::halt() const { return c_->unit<units::TileCore>(un(k_, "core")).rv.halt; }
int64_t TileRef::exit_value() const { return int32_t(block().sim_exit_value); }
std::string TileRef::halt_report() const {
    static const char* const kName[] = {"running", "illegal instruction", "ecall", "ebreak", "bus error", "SIM_EXIT",
                                        "fetch outside program memory and the boot ROM", "misaligned jump target",
                                        "a trap inside the handler"};
    const units::Rv32& r = c_->unit<units::TileCore>(un(k_, "core")).rv;
    char b[160];
    std::snprintf(b, sizeof b, "tile %u: %s at pc=0x%x", k_, r.halt >= 0 && r.halt <= 8 ? kName[r.halt] : "?", r.pc);
    return b;   // ref's report adds the halting instruction (and a bus error's address): not kept by the C++ core yet
}

// ---- the send port's holding register from a test (ref's Tile.sendHold) ----
void TileRef::send_put(int64_t t, uint64_t lo, uint64_t hdr, uint64_t hi) {
    units::TileCore& core = unit<units::TileCore>("core");
    if (t == c_->cycle()) {                                 // as of the cycle just run
        const sim::Word w[3] = {lo, hdr, hi};
        c_->sim().put_late(un(k_, "core"), core.send, w, 3);
        return;
    }
    if (t != c_->cycle() + 1) FATAL("mach: tile %u: a send-hold put at cycle %lld (the cycle just run or the next)", k_, (long long)t);
    if (core.host_send) FATAL("mach: tile %u: a second send-hold put pending", k_);
    core.host_send = true; core.host_send_t = uint64_t(t);
    core.host_send_w[0] = lo; core.host_send_w[1] = hdr; core.host_send_w[2] = hi;
    c_->touched(un(k_, "core"));
}
bool TileRef::send_empty(int64_t t) const {               // WordHold.isEmpty(t): nothing in it, nothing put for t
    if (t != c_->cycle() + 1) FATAL("mach: tile %u: send_empty(%lld): the next cycle only", k_, (long long)t);
    const units::TileCore& core = unit<units::TileCore>("core");
    if (core.host_send) return false;
    sim::PortEntry e[1];
    const sim::PortView v = c_->sim().observe(unit<units::Injection>("inj").send, e, 1);
    return !v.valid && !v.put && v.n == 0;
}
uint64_t TileRef::send_refusals() const { return c_->sim().hold_refusals(unit<units::Injection>("inj").send); }
void TileRef::record_writer() {
    Chip::Impl& m = c_->impl();
    auto& v = m.writer_logs[k_];
    v.assign(c_->cfg().log_capacity, {});
    units::WriteEngine& we = unit<units::WriteEngine>("we");
    we.log = reinterpret_cast<uint64_t(*)[4]>(v.data()); we.log_cap = uint32_t(v.size()); we.log_n = 0;
}
std::vector<std::array<uint64_t, 4>> TileRef::writer_log() const {
    const units::WriteEngine& we = unit<units::WriteEngine>("we");
    const auto& v = c_->impl().writer_logs[k_];
    return std::vector<std::array<uint64_t, 4>>(v.begin(), v.begin() + we.log_n);
}
void TileRef::record_gather() {
    units::Gather& g = unit<units::Gather>("gather");
    if (!g.drain) FATAL("mach: tile %u: record_gather: ref's gatherStub (the drain, real_responder off) only", k_);
    auto& v = c_->impl().gather_logs[k_];
    v.assign(c_->cfg().log_capacity, {});
    g.log = reinterpret_cast<uint64_t(*)[4]>(v.data()); g.log_cap = uint32_t(v.size()); g.log_n = 0;
}
std::vector<std::array<uint64_t, 4>> TileRef::gather_log() const {
    const units::Gather& g = unit<units::Gather>("gather");
    const auto& v = c_->impl().gather_logs[k_];
    return std::vector<std::array<uint64_t, 4>>(v.begin(), v.begin() + g.log_n);
}

// ---- a test's loads and stores through the core (ref's Controller.load, store) ----
namespace {
// The Env Rv32::load reads through, between cycles: the registers as the block publishes them for t (its end of
// t - 1, block_at_end), HIST as stored.
struct LoadEnv {
    const units::RegBlockState& b;
    const units::HistStore& h;
    uint32_t reg(unsigned r) const { return b.reg(r); }
    uint32_t hist(uint32_t i) const { return h.m.hist[i & 255]; }
    void post(uint32_t, uint32_t, uint32_t) const {
        FATAL("mach: a test's load with a side effect (EVENTS, REQUEST_RECEIVE): not modelled yet (ask W20)");
    }
};
}  // namespace
std::optional<uint32_t> TileRef::load(int64_t t, uint32_t ea, unsigned width) const {
    if (t != c_->cycle() + 1) FATAL("mach: tile %u: a test's load at cycle %lld, not the next (%lld)", k_, (long long)t, (long long)c_->cycle() + 1);
    const uint32_t region = units::lw::region(ea & ~3u);
    if (region == units::lw::R_DMEM || region == units::lw::R_PMEM)   // the memories as ref has them (port B's last write)
        return peek_local(region << 24 | (((units::lw::offset(ea & ~3u)) & (units::MEM_BYTES - 1))));
    units::TileCore& core = unit<units::TileCore>("core");
    const units::RegBlockState b = block();
    LoadEnv e{b, unit<units::HistStore>("hist")};
    uint32_t v = 0;
    if (!core.rv.load(e, ea, width, v)) return std::nullopt;
    return v;
}
std::string TileRef::store(int64_t t, uint32_t ea, uint32_t v, uint32_t mask) {
    namespace lw = units::lw;
    if (t <= c_->cycle()) FATAL("mach: tile %u: a test's store at cycle %lld, already run", k_, (long long)t);
    units::TileCore& core = unit<units::TileCore>("core");
    const units::Rv32& r = core.rv;
    const uint32_t a = ea & ~3u;                            // ref's reasons, in ref's order (Controller.store)
    const unsigned region = lw::region(a);
    if (region == lw::R_PMEM) return "store to program memory";
    if (region == lw::R_BOOTROM) return "store to the boot ROM";
    if (units::in_tables(a) && mask != 0xFFFFFFFFu) return "sub-word store to the table window";
    if (units::in_regs(a)) {
        if (mask != 0xFFFFFFFFu) return "sub-word store to a register";
        if (const unsigned n = units::window_of(a)) { if (!r.window_present(n)) return "store to an absent peripheral's window"; }
        else {
            const units::RegInfo& ri = units::reg_info((a - units::REG_BASE) >> 2);
            if (ri.store == units::A_NONE && ri.load != units::A_NONE) return "store to a read-only register";
        }
    } else if (region != lw::R_DMEM && !units::in_tables(a)) return "store to an unmapped address";
    if (core.host_n == units::TileCore::kHostStores) FATAL("mach: tile %u: more than %u test stores pending", k_, units::TileCore::kHostStores);
    core.host_st[core.host_n++] = {uint64_t(t), ea, v, mask};
    c_->touched(un(k_, "core"));
    return "";
}

// ---- the table stores (back doors: ref's TableStore.poke / peek in its index spaces, HIST's) ----
namespace {
constexpr uint32_t PTR_IDX = 0x100, GROUP_IDX = 0x400, SSRC_IDX = 0x1000, SDEST_IDX = 0x1100, SCOUNT_IDX = 0x1200;
void table_of(uint32_t idx, uint32_t& table, uint32_t& i) {
    if (idx >= SCOUNT_IDX) { table = units::TBL_STREAM_COUNT; i = idx - SCOUNT_IDX; }
    else if (idx >= SDEST_IDX) { table = units::TBL_STREAM_DEST; i = idx - SDEST_IDX; }
    else if (idx >= SSRC_IDX) { table = units::TBL_STREAM_SRC; i = idx - SSRC_IDX; }
    else if (idx >= GROUP_IDX) { table = units::TBL_GROUP; i = idx - GROUP_IDX; }
    else if (idx >= PTR_IDX) { table = units::TBL_PTR; i = idx - PTR_IDX; }
    else FATAL("mach: table index 0x%x (ref's index spaces: PTR 0x100, GROUP 0x400, STREAM 0x1000-0x12FF)", idx);
}
uint32_t table_read(const units::TableMems& m, uint32_t table, uint32_t i) {
    switch (table) {
    case units::TBL_STREAM_SRC: return m.stream_src[i & 255];
    case units::TBL_STREAM_DEST: return m.stream_dest[i & 255];
    case units::TBL_STREAM_COUNT: return m.stream_count[i & 255];
    case units::TBL_GROUP: return m.group[i & 127];
    default: return m.ptr[i & 255];
    }
}
}  // namespace
void TileRef::ts_poke(uint32_t idx, uint32_t v) {
    uint32_t table, i;
    table_of(idx, table, i);
    unit<units::TableStore>("ts").poke_host(table, i, v);
    c_->touched(un(k_, "ts"));
}
uint32_t TileRef::ts_peek(uint32_t idx) const {
    uint32_t table, i;
    table_of(idx, table, i);
    return table_read(unit<units::TableStore>("ts").m, table, i);
}
void TileRef::hist_poke(uint32_t i, uint32_t v) { unit<units::HistStore>("hist").poke_host(i, v); c_->touched(un(k_, "hist")); }
uint32_t TileRef::hist_peek(uint32_t i) const { return unit<units::HistStore>("hist").m.hist[i & 255]; }

// ---- Tile's own operations (ref's Tile.set*): the register block's state, published at its next step ----
void TileRef::set_master_config(uint32_t v) {
    const uint32_t bd = v >> 18 & 7, hd = v >> 21 & 7;
    if (bd > 4 || hd > 4)                                   // v1.25 (s8e): ref's UndefinedBehavior, its message
        FATAL("undefined behavior at cycle %lld, tile %u, DMA_CONFIG: a reserved digit selector (bucket %u, hist %u; 5-7 are "
              "reserved: spec 4.15) (DECISIONS-v1.25 s8: meshsim stops; the hardware does nothing special)",
              (long long)c_->cycle(), k_, bd, hd);
    units::TileCtl& b = unit<units::TileCtl>("ctl");
    b.host.dma_config = v; b.host.mask |= units::TileCtl::HostWrites::DMA_CONFIG;
    c_->touched(un(k_, "ctl"));
}
void TileRef::set_errors(uint32_t bits) {
    units::TileCtl& b = unit<units::TileCtl>("ctl");
    b.host_cond |= bits;
    c_->touched(un(k_, "ctl"));
}
void TileRef::set_record_base(uint32_t chunk) {
    units::TileCtl& b = unit<units::TileCtl>("ctl");
    b.host.record_base = chunk & 0x1FFFFF; b.host.mask |= units::TileCtl::HostWrites::RECORD_BASE;
    c_->touched(un(k_, "ctl"));
}
void TileRef::set_ptr(unsigned i, uint32_t chunk) { ts_poke(PTR_IDX + i, chunk & 0x1FFFFF); }
uint32_t TileRef::ptr(unsigned i) const { return ts_peek(PTR_IDX + i); }
void TileRef::set_stream(unsigned i, uint32_t src, uint32_t dest, uint32_t count) {
    ts_poke(SSRC_IDX + i, src); ts_poke(SDEST_IDX + i, dest); ts_poke(SCOUNT_IDX + i, count);
}
void TileRef::set_group(unsigned g, uint32_t lo, uint32_t hi) { ts_poke(GROUP_IDX + 2 * g, lo); ts_poke(GROUP_IDX + 2 * g + 1, hi); }
void TileRef::pulse(uint32_t bits) {                       // TILE_PULSE's start: the AG's start Wire at the next cycle
    if (bits & fields::PULSE_START) {
        units::TileCtl& b = unit<units::TileCtl>("ctl");
        b.host.starts++; b.host.mask |= units::TileCtl::HostWrites::STARTS;
        c_->touched(un(k_, "ctl"));
    }
}
// The register block as ref has it at the end of the cycle just run (its endCycle of that cycle runs at the next step:
// tile_ctl.h's block_at_end), with the harness's writes still pending over it.
units::RegBlockState TileRef::block(bool with_cond) const {
    const units::TileCtl& b = unit<units::TileCtl>("ctl");
    units::RegBlockState r = units::block_at_end(c_->sim(), b, uint64_t(c_->cycle()));
    if (with_cond) r.conditions |= b.host_cond;            // set from the test (request 9): ref's ctl has them now
    if (b.host.mask & units::TileCtl::HostWrites::DMA_CONFIG) r.dma_config = b.host.dma_config;
    if (b.host.mask & units::TileCtl::HostWrites::RECORD_BASE) r.record_base = b.host.record_base;
    if (b.host.mask & units::TileCtl::HostWrites::STARTS) r.dma_starts += b.host.starts;
    for (unsigned k = 0; k < 8; k++) if (b.host.mask & units::TileCtl::HostWrites::REMAINING << k) r.remaining[k] = b.host.remaining[k];
    return r;
}
uint32_t TileRef::remaining(unsigned ctr) const { return block().remaining[ctr & 7]; }
void TileRef::set_remaining(unsigned ctr, uint32_t v) {
    units::TileCtl& b = unit<units::TileCtl>("ctl");
    b.host.remaining[ctr & 7] = v & 0xFFFFF; b.host.mask |= units::TileCtl::HostWrites::REMAINING << (ctr & 7);
    c_->touched(un(k_, "ctl"));
}

namespace {
bool port_empty(sim::Sim& s, const sim::FifoIn& p) { sim::PortEntry e[64]; const sim::PortView v = s.observe(p, e, 64); return v.n == 0 && !v.put; }
bool port_empty(sim::Sim& s, const sim::HoldIn& p) { sim::PortEntry e[1]; const sim::PortView v = s.observe(p, e, 1); return v.n == 0 && !v.put; }
}  // namespace
// mem_idle (4.1; ref's Tile.memIdle): the write engine's input FIFO and its stage empty, nothing on its way into big
// SRAM, no write queued at any area (reads queued don't hold it low)
bool TileRef::mem_idle() const {
    sim::Sim& s = c_->sim();
    const units::WriteEngine& we = unit<units::WriteEngine>("we");
    if (we.count || !port_empty(s, we.in)) return false;
    const units::BigSram& m = unit<units::BigSram>("sram");
    if (!port_empty(s, m.wr)) return false;
    for (unsigned a = 0; a < units::BigSram::AREAS; a++)
        for (unsigned j = 0; j < m.count[a]; j++) if (m.q[a][(m.head[a] + j) % units::BigSram::MAX_DEPTH].write) return false;
    return true;
}
// ts_busy (4.15): either store's pipeline or request FIFOs busy; requests and returns on their way are in the ports
bool TileRef::ts_busy() const {
    const units::TableStore& t = unit<units::TableStore>("ts");
    const units::HistStore& h = unit<units::HistStore>("hist");
    return t.pipe.valid || t.pub || t.q_stream.n || t.q_group.n || t.q_we.n || t.q_stream.pend || t.q_group.pend || t.q_we.pend ||
           h.pipe.valid || h.q.n || h.q.pend;
}
bool TileRef::quiescent() const { FATAL("mach: TileRef::quiescent: use Chip::quiescent (the chip's ports are judged together)"); }

// ============================================================================================ C&C 1's model (CnC)
namespace {
// Appends behind a release point and hands the new steps to C&C now (ref: the queue grows between cycles; C&C acts
// from the next cycle). The vectors may have moved: C&C's pointers are refreshed (between cycles, configuration code).
void publish(Chip& ch) {
    Chip::Impl& m = ch.impl();
    m.prog.release();
    units::Cnc& c = ch.unit<units::Cnc>("cnc");
    c.prog = m.prog.steps.data();
    c.packets = m.prog.packets.data();
    c.releases = m.prog.releases.data();
    c.n_releases = uint32_t(m.prog.releases.size());
    while (c.rel < c.n_releases) c.release_next();
    ch.touched("cnc");
}
uint32_t take_id(Chip::Impl& m) {
    if (m.free_ids.empty()) FATAL("mach: more than %u C&C calls pending at once", units::CncProgram::kCalls);
    const uint32_t id = m.free_ids.back();
    m.free_ids.pop_back();
    return id;
}
}  // namespace

Cc& Cc::load_words(unsigned dst, uint32_t a, const std::vector<uint32_t>& w) {
    c_->impl().prog.load_words(dst, a, w.data(), w.size()); publish(*c_); return *this;
}
Cc& Cc::command(unsigned dst, uint32_t word, const std::vector<uint32_t>& args) {
    c_->impl().prog.command(dst, word, args.data(), args.size()); publish(*c_); return *this;
}
Cc& Cc::send(unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi) {
    const units::CncPacket p{uint8_t(dst), uint8_t(mode), uint8_t(counter), uint8_t(size), addr, lo, hi};
    c_->impl().prog.send(&p, 1); publish(*c_); return *this;
}
Cc& Cc::go() { c_->impl().prog.go(); publish(*c_); return *this; }
Cc& Cc::wait_cycles(uint32_t n) { c_->impl().prog.wait_cycles(n); publish(*c_); return *this; }
Cc& Cc::call(std::function<void(int64_t)> fn) {
    Chip::Impl& m = c_->impl();
    const uint32_t id = take_id(m);
    m.prog.call(id);
    m.calls.push_back({id, std::move(fn)});
    publish(*c_);
    return *this;
}
void Cc::set_expect(uint64_t tiles) { c_->unit<units::Cnc>("cnc").expect = tiles; c_->touched("cnc"); }
Cc& Cc::wait_until(std::function<bool()> cond) {
    Chip::Impl& m = c_->impl();
    m.waits.push_back({uint32_t(m.prog.steps.size()), std::move(cond)});
    m.prog.wait_host();
    publish(*c_);
    c_->impl().check_waits();                              // C&C may be there already (between cycles: ref checks at once)
    return *this;
}
Cc& Cc::at_all_ready(std::function<void(int64_t)> fn) {
    Chip::Impl& m = c_->impl();
    const uint32_t id = take_id(m);
    m.prog.at_all_ready(id);
    m.calls.push_back({id, std::move(fn)});
    publish(*c_);
    return *this;
}
Cc& Cc::run_when_ready() {                                 // CnC.runWhenReady: lastRunAt = -1; go(); call(lastRunAt = lastGoAt)
    Chip::Impl& m = c_->impl();
    m.last_run_at = -1;
    go();
    Chip* ch = c_;
    return call([ch](int64_t) { ch->impl().last_run_at = ch->unit<units::Cnc>("cnc").last_go_at; });
}
bool Cc::busy() const { const units::Cnc& c = c_->unit<units::Cnc>("cnc"); return c.cur < c.released; }
int64_t Cc::last_go_at() const { return c_->unit<units::Cnc>("cnc").last_go_at; }
int64_t Cc::last_all_ready_at() const { return c_->unit<units::Cnc>("cnc").last_all_ready_at; }
int64_t Cc::last_run_at() const { return c_->impl().last_run_at; }
std::vector<std::array<int64_t, 2>> Cc::phase_log() const { FATAL("mach: Cc::phase_log comes with W20's step 4 (PhaseLog)"); }
std::vector<std::array<uint64_t, 4>> Cc::received() const { return c_->received(0); }
uint64_t Cc::ready_bits() const { FATAL("mach: Cc::ready_bits comes with the coreless machine"); }

// ================================================================================================= Mach
MachConfig Mach::cfg(int64_t shuffle) { MachConfig k = MachConfig::machine(); k.shuffle_seed = shuffle; return k; }
MachConfig Mach::cores_cfg(int64_t shuffle) { MachConfig k = cfg(shuffle); k.cc_cores = true; return k; }

Mach::Image Mach::Image::load_dir(const std::string& dir, const std::string& name) {
    Image im;
    im.name = name;
    const std::string base = dir + "/" + name;
    if (!exists(base + ".code.bin")) FATAL("mach: image %s: no %s.code.bin", name.c_str(), base.c_str());
    im.code = words_of(base + ".code.bin");
    im.data = words_of(base + ".data.bin");
    std::string s;
    if (!read_file(base + ".sym", s)) FATAL("mach: image %s: cannot read %s.sym", name.c_str(), base.c_str());
    for (size_t at = 0; at < s.size();) {                  // Mach.Image: "VALUE TYPE NAME" lines, value in hex
        size_t e = s.find('\n', at);
        if (e == std::string::npos) e = s.size();
        char v[64], t[16], n[256];
        if (std::sscanf(s.substr(at, e - at).c_str(), "%63s %15s %255s", v, t, n) == 3)
            im.syms[n] = uint32_t(std::stoull(v, nullptr, 16));
        at = e + 1;
    }
    return im;
}
Mach::Image Mach::Image::load(const std::string& name, const MachConfig& cfg) {
    std::vector<std::string> dirs = cfg.image_dirs;
    if (dirs.empty()) {
        if (const char* e = std::getenv("SIM_IMAGES")) {
            std::string s = e;
            for (size_t at = 0; at <= s.size();) {
                size_t c = s.find(':', at);
                if (c == std::string::npos) c = s.size();
                if (c > at) dirs.push_back(s.substr(at, c - at));
                at = c + 1;
            }
        }
        for (const char* d : {"fw/images", "tests/golden-m2/images", "tests/golden/images", "tests/m3-images"}) dirs.push_back(d);
    }
    for (const std::string& d : dirs) if (exists(d + "/" + name + ".code.bin")) return load_dir(d, name);
    FATAL("mach: image %s not found (the search path: MachConfig::image_dirs, $SIM_IMAGES, then the defaults)", name.c_str());
}
uint32_t Mach::Image::sym(const std::string& s) const {
    auto it = syms.find(s);
    if (it == syms.end()) FATAL("%s: no symbol %s", name.c_str(), s.c_str());
    return it->second;
}

Mach::Mach(const MachConfig& cfg) : c(cfg) {}

Mach::Image Mach::boot(const std::string& name) { return boot(Image::load(name, c.cfg())); }
Mach::Image Mach::boot(const Image& im) {
    boot_start = c.cycle();
    c.cc().load_words(units::lw::BROADCAST, PMEM_BASE, im.code)
        .load_words(units::lw::BROADCAST, im.sym("_data_start"), im.data)
        .command(units::lw::BROADCAST, CMD_JUMP << 24, {im.sym("_start")})
        .run_when_ready()
        .call([this](int64_t) { run_at = c.cc().last_run_at(); });
    return im;
}

Mach::Image Mach::boot_cores(const std::string& tile) { return boot_cores(Image::load(tile, c.cfg()), nullptr); }
Mach::Image Mach::boot_cores(const Image& t, const Image* cc3) {
    if (!c.cfg().cc_cores) FATAL("bootCores needs Config.ccCores");
    cc1 = Image::load("cc1", c.cfg());
    cc23 = Image::load("cc23", c.cfg());
    boot_start = c.cycle();
    units::BigSram& s = c.unit<units::BigSram>("cc1.sram");
    uint32_t at = STAGE_CHUNK;
    struct Staged { uint32_t chunk, packets; };
    auto stage = [&](const std::vector<uint32_t>& w) {       // 128-bit rows, zero-padded
        const uint32_t ch = at, pk = uint32_t((w.size() + 3) / 4);
        for (uint32_t i = 0; i < 4 * pk; i++) s.poke(ch + i, i < w.size() ? w[i] : 0);
        at += 4 * (pk ? pk : 1);
        return Staged{ch, pk};
    };
    std::vector<uint32_t> desc(1 + 3 * 6 + 2, 0);
    desc[0] = 0xB007DE5Cu;
    const Staged cp = stage(cc23.code), cd = stage(cc23.data), tp = stage(t.code), td = stage(t.data);
    const Staged c3p = cc3 ? stage(cc3->code) : cp, c3d = cc3 ? stage(cc3->data) : cd;
    for (unsigned u = 0; u < 3; u++) {
        const Image& im = u == 0 ? cc23 : u == 1 ? (cc3 ? *cc3 : cc23) : t;
        const Staged p = u == 0 ? cp : u == 1 ? c3p : tp, d = u == 0 ? cd : u == 1 ? c3d : td;
        const uint32_t cmd = at; at += 4;
        const uint32_t e[6] = {p.chunk, p.packets, d.chunk, d.packets, cmd, im.sym("_start")};
        for (unsigned i = 0; i < 6; i++) desc[1 + 6 * u + i] = e[i];
    }
    desc[19] = c.cfg().go_latency; desc[20] = uart_divisor;
    std::vector<uint32_t> data = cc1.data;
    const uint32_t base = (cc1.sym("boot_desc") - cc1.sym("_data_start")) / 4;
    if (base + desc.size() > data.size()) FATAL("mach: cc1's data image ends before boot_desc's %zu words", desc.size());
    for (size_t i = 0; i < desc.size(); i++) data[base + i] = desc[i];
    c.touched("cc1.sram");
    host_boot(cc1, data);
    return t;
}
void Mach::host_boot(const Image& im, const std::vector<uint32_t>& data) {
    c.host_load(0, PMEM_BASE, im.code);
    if (!data.empty()) c.host_load(0, im.sym("_data_start"), data);
    c.host_command(0, CMD_JUMP << 24, {im.sym("_start")});
}
void Mach::stage_cc1(uint32_t at, const std::vector<uint32_t>& w, uint32_t rows) {
    if (w.size() > 4 * size_t(rows)) FATAL("%zu words in %u rows", w.size(), rows);
    units::BigSram& s = c.unit<units::BigSram>("cc1.sram");
    for (uint32_t i = 0; i < 4 * rows; i++) s.poke(at + i, i < w.size() ? w[i] : 0);
    c.touched("cc1.sram");
}
uint32_t Mach::mbox(unsigned i) const { return const_cast<Chip&>(c).tile(64).peek_local(cc1.sym("mbox") + 4 * i); }
uint32_t Mach::toast_info(unsigned i) const { return const_cast<Chip&>(c).tile(64).peek_local(cc1.sym("toast_info") + 4 * i); }
void Mach::cc1_command(uint32_t type, uint32_t arg, const std::vector<uint32_t>& args) { c.host_command(0, type << 24 | arg, args); }
void Mach::phase(uint32_t word, const std::vector<uint32_t>& args) {
    std::vector<uint32_t> a{word};
    a.insert(a.end(), args.begin(), args.end());
    cc1_command(CC_PHASE, uint32_t(args.size()), a);
}
bool Mach::cores_booted() const { return !cc1.name.empty() && mbox(3) == 2; }

int64_t Mach::exit_value(unsigned tile) const { return const_cast<Chip&>(c).tile(tile).exit_value(); }
uint32_t Mach::word(unsigned tile, uint32_t a) const { return const_cast<Chip&>(c).tile(tile).peek_local(a); }
uint32_t Mach::word(unsigned tile, const Image& im, const std::string& sym, unsigned i) const { return word(tile, im.sym(sym) + 4 * i); }
std::string Mach::halt_summary() const {
    std::string sb;
    Chip& ch = const_cast<Chip&>(c);
    for (unsigned k = 0; k < 64; k++) {
        TileRef p = ch.tile(k);
        if (p.halt() == units::Rv32::HALT_EXIT) continue;
        char b[48];
        std::snprintf(b, sizeof b, " pc=0x%x", p.pc());
        sb += p.halt_report() + b + (p.in_wfi() ? " (WFI)" : "") + "; ";
        if (sb.size() > 400) break;
    }
    return sb;
}

}  // namespace mach

// ===================================================================================== the digit all-to-all (A2A)
#include "java_random.h"

namespace mach {

const int A2A::G4[16][4] = {{1, 29, 35, 63}, {2, 30, 34, 62}, {3, 31, 33, 61}, {5, 23, 41, 59}, {6, 22, 42, 58},
                            {7, 21, 43, 57}, {8, 28, 36, 56}, {9, 27, 37, 55}, {10, 26, 38, 54}, {11, 25, 39, 53},
                            {12, 24, 40, 52}, {13, 15, 49, 51}, {14, 18, 46, 50}, {16, 20, 44, 48}, {17, 19, 45, 47},
                            {0, 4, 32, 60}};

namespace {
void a2a_finish(A2A& a) {
    for (int g = 0; g < 16; g++) for (int s = 0; s < 64; s++) for (int k : A2A::G4[g]) a.R[g] = std::max(a.R[g], a.cnt[s][(s + k) & 63]);
    for (int d = 0; d < 64; d++) for (int s = 1; s < 64; s++) a.dstoff[s][d] = a.dstoff[s - 1][d] + a.cnt[s - 1][d];
}
}  // namespace

A2A::A2A(int64_t seed, int n) {   // v3.10's Data(seed, n, ragged = false): weights all 1, so nextInt(64)
    test::JavaRandom r(seed);
    for (int s = 0; s < 64; s++) {
        std::vector<int> dst(static_cast<size_t>(n));
        for (int k = 0; k < n; k++) { const int d = r.next_int_pow2(64); dst[size_t(k)] = d; cnt[s][d]++; }
        rec[s].assign(size_t(n), 0);
        int pos[64] = {};
        for (int d = 1; d < 64; d++) pos[d] = pos[d - 1] + cnt[s][d - 1];
        for (int k = 0; k < n; k++) rec[s][size_t(pos[dst[size_t(k)]]++)] = int64_t(uint64_t(dst[size_t(k)]) << 58 | uint64_t(s) << 52 | uint64_t(k));
    }
    a2a_finish(*this);
}
A2A::A2A(const std::string& matrix) {
    FILE* f = std::fopen(matrix.c_str(), "r");
    if (!f) FATAL("A2A: cannot read %s", matrix.c_str());
    for (int s = 0; s < 64; s++) {
        int np = 0;
        for (int d = 0; d < 64; d++) { if (std::fscanf(f, "%d", &cnt[s][d]) != 1) FATAL("A2A: %s: row %d short", matrix.c_str(), s); np += cnt[s][d]; }
        rec[s].assign(size_t(np), 0);
        size_t i = 0;
        for (int d = 0; d < 64; d++) for (int k = 0; k < cnt[s][d]; k++, i++) rec[s][i] = int64_t(uint64_t(d) << 58 | uint64_t(s) << 52 | i);
    }
    std::fclose(f);
    a2a_finish(*this);
}
int A2A::expected(unsigned d) const { int e = 0; for (int s = 0; s < 64; s++) e += cnt[s][d]; return e; }
int64_t A2A::bound(int gap) const { int64_t b = 0; for (int r : R) b += 4ll * r + gap; return b; }
int A2A::first(unsigned s, unsigned d) const { int f = 0; for (unsigned x = 0; x < d; x++) f += cnt[s][x]; return f; }
int A2A::verify(Chip& c) const {
    int bad = 0;
    for (unsigned d = 0; d < 64; d++) {
        TileRef p = c.tile(d);
        for (unsigned s = 0; s < 64; s++) {
            const uint32_t base = INBOX + 2 * uint32_t(dstoff[s][d]);
            const int f = first(s, d);
            for (int k = 0; k < cnt[s][d]; k++) if (int64_t(p.sram_peek64(base + 2 * uint32_t(k))) != rec[s][size_t(f + k)]) bad++;
        }
    }
    return bad;
}
void A2A::load(Chip& c, int gap) const {                  // A2A.load
    for (unsigned s = 0; s < 64; s++) {
        TileRef p = c.tile(s);
        for (size_t k = 0; k < rec[s].size(); k++) p.sram_poke64(SRC + 2 * uint32_t(k), uint64_t(rec[s][k]));
        for (unsigned g = 0; g < 16; g++) {
            for (unsigned i = 0; i < 4; i++) {
                const unsigned off = unsigned(G4[g][i]), d = (s + off) & 63;
                p.set_stream(4 * g + i, fields::stream_src(SRC + 2 * uint32_t(first(s, d))), 0,
                             fields::stream_count(uint32_t(cnt[s][d]), fields::SIZE_64, 1, units::lw::M_SRC, off));
            }
            p.set_group(g, fields::group_lo(uint32_t(R[g])), fields::group_hi(4 * g, 4, uint32_t(gap)));
        }
        for (unsigned src = 0; src < 64; src++) p.set_ptr(src, INBOX + 2 * uint32_t(dstoff[src][s]));
        p.set_remaining(1, uint32_t(expected(s)));
        p.set_master_config(fields::master_config(fields::RUN_GROUPS, 0, 16, 0, 0, 1));
    }
}
int64_t A2A::run(Chip& c, int64_t max) {                   // A2A.run
    const int64_t t0 = c.cycle();
    const std::vector<int> skew = c.start_skew();
    bool started[64] = {};
    int64_t end = -1;
    for (int64_t i = 0; i < max; i++) {
        for (unsigned k = 0; k < 64; k++) if (!started[k] && c.cycle() >= t0 + skew[k]) { c.tile(k).pulse(fields::PULSE_START); started[k] = true; }
        c.step();
        bool all = true;
        for (unsigned k = 0; k < 64 && all; k++) {
            TileRef p = c.tile(k);
            all = p.unit<units::AddrGen>("ag").done && p.remaining(1) == 0 && p.mem_idle();
        }
        if (all) { end = c.cycle(); break; }
    }
    c.scan_overflows();
    return end < 0 ? -1 : end - t0;
}

// ================================================================================ the coreless machine fed (Net)
void Chip::Impl::enable_net(const MachConfig& c) {
    net = true;
    feeds.assign(64, units::HarnessFeed{});
    ring_store.assign(64 * 4, {});
    logs.assign(64, std::vector<E4>(c.log_capacity));
    for (unsigned k = 0; k < 64; k++) {
        units::Injection& in = sim->unit<units::Injection>(units::tile_unit(k, "inj"));
        if (!in.rs.cfg.present) FATAL("Net: tile %u has no read side", k);
        feeds[k].clients = c.test_read_clients;
        in.rs.feed = &feeds[k];
        units::Director& d = sim->unit<units::Director>(units::tile_unit(k, "dir"));
        d.log = reinterpret_cast<uint64_t(*)[4]>(logs[k].data());
        d.log_cap = c.log_capacity; d.log_n = 0;
    }
    if (c.cc_cores) FATAL("Net: the coreless machine has C&C 1's Java model (no cc_cores)");
}
void Chip::Impl::push_send(unsigned cc, const units::HarnessSends::E& e) {   // between cycles: grow if full
    units::HarnessSends& h = sends[cc];
    std::vector<units::HarnessSends::E>& v = send_store[cc];
    if (h.n == h.cap) {
        std::vector<units::HarnessSends::E> w(std::max<size_t>(64, 2 * size_t(h.cap)));
        for (uint32_t i = 0; i < h.n; i++) w[i] = h.e[(h.head + i) % h.cap];
        v.swap(w);
        h.e = v.data(); h.cap = uint32_t(v.size()); h.head = 0;
    }
    h.e[(h.head + h.n) % h.cap] = e;
    h.n++;
    sim->wake(cc == 0 ? std::string("cnc") : "ep" + std::to_string(cc + 1));
}
void Chip::Impl::push(unsigned tile, unsigned ring, const E4& e) {   // between cycles: grow if full, refresh the pointer
    units::HarnessFeed::Ring& r = ring == 0 ? feeds[tile].ret : feeds[tile].cl[ring - 1];
    std::vector<E4>& v = ring_store[4 * tile + ring];
    if (r.n == r.cap) {
        std::vector<E4> w(std::max<size_t>(64, 2 * size_t(r.cap)));
        for (uint32_t i = 0; i < r.n; i++) { const uint64_t* x = r.at(i); w[i] = {x[0], x[1], x[2], x[3]}; }
        v.swap(w);
        r.e = reinterpret_cast<uint64_t(*)[4]>(v.data());
        r.cap = uint32_t(v.size()); r.head = 0;
    }
    uint64_t* x = r.e[(r.head + r.n) % r.cap];
    x[0] = e[0]; x[1] = e[1]; x[2] = e[2]; x[3] = e[3];
    r.n++;
    sim->wake(units::tile_unit(tile, "inj"));
}

MachConfig Net::cfg(int64_t shuffle) {
    MachConfig k = MachConfig::defaults();
    k.shuffle_seed = shuffle; k.check_unexpected = false; k.check_errors = false;
    return k;
}
Net::Net(const MachConfig& cfg) : c(cfg) {
    if (!cfg.coreless) FATAL("Net: the coreless test machine only (MachConfig::defaults or Net::cfg)");
    c.impl().enable_net(cfg);
}
void Net::at(unsigned tile, int64_t T, unsigned dst, unsigned mode, uint32_t addr, uint64_t payload) {
    at(tile, T, dst, mode, 1, addr, units::lw::SIZE_64, payload, 0);
}
void Net::at(unsigned tile, int64_t T, unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi) {
    units::HarnessFeed::Ring& r = c.impl().feeds[tile].ret;
    if (r.n && int64_t(r.at(r.n - 1)[0]) > T) FATAL("Net.at: tile %u: T %lld before the last scheduled (nondecreasing per tile)", tile, (long long)T);
    c.impl().push(tile, 0, {uint64_t(T), lo, units::lw::hdr(0, dst, mode, counter, addr, size), hi});
}
void Net::rd(unsigned tile, unsigned cl, int64_t T, uint32_t src_chunk, unsigned width, unsigned mode, unsigned counter,
             unsigned dst, uint32_t dst_addr) {
    if (!c.cfg().test_read_clients) FATAL("Net.rd needs MachConfig::test_read_clients (ref's testReadClients)");
    if (cl > 2) FATAL("Net.rd: client %u (0-2)", cl);
    c.impl().push(tile, 1 + cl, {uint64_t(T), units::mem::req_a(src_chunk, width, mode, counter, dst), units::mem::req_b(dst_addr), 0});
}
void Net::cnc_send(const CncSend& s) {
    const units::CncPacket p{uint8_t(s.dst), uint8_t(s.mode), uint8_t(s.counter), uint8_t(s.size), s.addr, s.lo, s.hi};
    c.impl().push_send(0, {s.T, p});
}
// ref's Endpoint.send(c.cycle, ...): src the endpoint's address, the payload masked to its size, put on the endpoint's
// send Hold as of the cycle just run (Sim::put_late); like ref's, a refused one isn't presented again.
void Net::send(unsigned cc, unsigned dst, unsigned mode, unsigned counter, uint32_t addr, unsigned size, uint64_t lo, uint64_t hi) {
    if (cc > 2) FATAL("Net::send: C&C %u (0-2)", cc + 1);
    namespace lw = units::lw;
    const sim::Word w[3] = {lw::mask_lo(lo, size), lw::hdr(lw::CC1 + cc, dst, mode, counter, addr, size), lw::mask_hi(hi, size)};
    if (cc == 0) c.sim().put_late("cnc", c.unit<units::Cnc>("cnc").send, w, 3);
    else {
        const std::string n = "ep" + std::to_string(cc + 1);
        c.sim().put_late(n, c.unit<units::Endpoint>(n).send, w, 3);
    }
}
std::vector<std::array<uint64_t, 4>> Net::received(unsigned cc) const { return c.received(cc); }
std::vector<std::array<uint64_t, 4>> Chip::received(unsigned cc) const {
    if (cfg_.cc_cores || cc > 2) FATAL("mach: received(%u): C&C 1-3's endpoints of the model (no cc_cores)", cc);
    const Impl& m = *impl_;
    if (m.sends[cc].lost) FATAL("mach: C&C %u's receive trace overflowed (%u kept, %llu lost): raise MachConfig::log_capacity",
                                cc + 1, m.sends[cc].log_cap, (unsigned long long)m.sends[cc].lost);
    return std::vector<std::array<uint64_t, 4>>(m.recv[cc].begin(), m.recv[cc].begin() + m.sends[cc].log_n);
}
uint32_t Net::cm(uint32_t w) { return w < 0x800 ? uint32_t(units::lw::R_PMEM) << 24 | w << 2 : (w - 0x800) << 2; }
int64_t Net::late() const { int64_t n = 0; for (const auto& f : const_cast<Chip&>(c).impl().feeds) n += int64_t(f.late); return n; }
bool Net::pending() const {
    const Chip::Impl& m = c.impl_const();
    for (const auto& f : m.feeds) if (f.pending()) return true;
    for (const auto& h : m.sends) if (h.n) return true;
    return false;
}
void Net::step() { c.step(); }
bool Net::run_all(uint64_t max) {
    for (uint64_t i = 0; i < max; i++) {
        if (!pending() && c.quiescent()) { c.scan_overflows(); return true; }
        step();
    }
    c.scan_overflows();
    return false;
}
std::vector<std::array<uint64_t, 4>> Net::log(unsigned tile) const {
    const units::Director& d = const_cast<Chip&>(c).unit<units::Director>(units::tile_unit(tile, "dir"));
    return std::vector<std::array<uint64_t, 4>>(c.impl_const().logs[tile].begin(), c.impl_const().logs[tile].begin() + d.log_n);
}
void Net::clear_logs() {
    for (unsigned k = 0; k < 64; k++) c.unit<units::Director>(units::tile_unit(k, "dir")).log_n = 0;
    for (auto& h : c.impl().sends) { h.log_n = 0; h.lost = 0; }
}
unsigned Net::max_router_peak() const {
    unsigned m = 0;
    for (unsigned k = 0; k < 64; k++) {
        const units::Router& r = const_cast<Chip&>(c).unit<units::Router>(units::tile_unit(k, "router"));
        for (const auto& f : r.q) m = std::max(m, unsigned(f.peak));
    }
    return m;
}

}  // namespace mach
