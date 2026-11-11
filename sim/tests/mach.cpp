// mach.cpp (WORKER20, P2): the machine harness (mach.h). Configuration and between-cycle code only (D6 doesn't apply).
#include "mach.h"

#include <sys/stat.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "base/fatal.h"
#include "units/big_sram.h"
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
    int64_t last_run_at = -1;
    // C&C's CALL steps run between cycles (Cc::call): id -> the harness's function, run after the cycle it ran in
    struct Call { uint32_t id; std::function<void(int64_t)> fn; };
    std::vector<Call> calls;
    std::vector<uint32_t> free_ids;

    bool has_cnc = false;
    units::Cnc* cnc() { return has_cnc ? &sim->unit<units::Cnc>("cnc") : nullptr; }
    uint32_t* dmem(unsigned u) { return mem.data() + size_t(u) * 2 * units::MEM_WORDS; }
    uint32_t* imem(unsigned u) { return dmem(u) + units::MEM_WORDS; }
    void run_calls();
};

Chip::Chip(const MachConfig& c) : cfg_(c), impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    if (c.coreless) FATAL("mach: the coreless machine (MachConfig::defaults, Net) comes with W20's next commit");
    if (!c.start_skew.empty()) FATAL("mach: a per-tile start_skew comes with W20's step 3");
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
    g.cc3_tracer = c.cc3_tracer;
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
    opt.threads = c.threads;
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
    m.has_cnc = !c.cc_cores;
    if (!c.cc_cores) m.cc = std::make_unique<Cc>(*this);
    for (uint32_t id = units::CncProgram::kCalls; id-- > 0;) m.free_ids.push_back(id);
}

Chip::~Chip() = default;

sim::Sim& Chip::sim() { return *impl_->sim; }
void Chip::touched(const std::string& name) { impl_->sim->wake(name); }
int64_t Chip::cycle() const { return int64_t(impl_->sim->now()) - 1; }

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

void Chip::step() {
    impl_->sim->run(1);
    impl_->run_calls();
}
void Chip::run(uint64_t n) {
    impl_->sim->wake_all();
    if (impl_->calls.empty()) impl_->sim->run(n);
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
bool Chip::quiescent() const { FATAL("mach: Chip::quiescent comes with the coreless machine (W20's next commit)"); }
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

// ---- the checker and reset: W20's step 2. Until then a run reports no violations; the calls that need it stop. ----
uint64_t Chip::violations() const { return 0; }
const std::vector<std::string>& Chip::violation_messages() const { static const std::vector<std::string> none; return none; }
void Chip::clear_violation_messages() {}
std::string Chip::violation_report() { FATAL("mach: violation_report comes with W20's step 2 (the checker)"); }
void Chip::scan_overflows() {}
void Chip::reset(bool) { FATAL("mach: reset comes with W20's step 2"); }
uint64_t Chip::resets() const { return 0; }
int64_t Chip::last_reset_at() const { return -1; }
void Chip::reset_stats() { FATAL("mach: reset_stats comes with W20's step 4 (PhaseLog)"); }

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
uint32_t TileRef::peek_local(uint32_t a) const { return *local(c_->impl(), k_, a); }
void TileRef::poke_local(uint32_t a, uint32_t v) { *local(c_->impl(), k_, a) = v; c_->touched(un(k_, "core")); }
uint32_t TileRef::dmem(uint32_t w) const {
    if (w >= units::MEM_WORDS) FATAL("mach: tile %u: dmem[%u] past its %u words", k_, w, units::MEM_WORDS);
    return c_->impl().dmem(k_)[w];
}

uint32_t TileRef::pc() const { return c_->unit<units::TileCore>(un(k_, "core")).rv.pc; }
uint32_t TileRef::x(unsigned r) const { return c_->unit<units::TileCore>(un(k_, "core")).rv.x[r & 31]; }
bool TileRef::in_wfi() const { return c_->unit<units::TileCore>(un(k_, "core")).rv.in_wfi; }
int TileRef::halt() const { return c_->unit<units::TileCore>(un(k_, "core")).rv.halt; }
int64_t TileRef::exit_value() const { return int32_t(c_->unit<units::TileCtl>(un(k_, "ctl")).s.sim_exit_value); }
std::string TileRef::halt_report() const {
    static const char* const kName[] = {"running", "illegal instruction", "ecall", "ebreak", "bus error", "SIM_EXIT",
                                        "fetch outside program memory and the boot ROM", "misaligned jump target",
                                        "a trap inside the handler"};
    const units::Rv32& r = c_->unit<units::TileCore>(un(k_, "core")).rv;
    char b[160];
    std::snprintf(b, sizeof b, "tile %u: %s at pc=0x%x", k_, r.halt >= 0 && r.halt <= 8 ? kName[r.halt] : "?", r.pc);
    return b;   // ref's report adds the halting instruction (and a bus error's address): not kept by the C++ core yet
}

// The coreless operations: the next commit (the units' harness-only parts).
void TileRef::ts_poke(uint32_t, uint32_t) { FATAL("mach: ts_poke comes with the coreless machine"); }
uint32_t TileRef::ts_peek(uint32_t) const { FATAL("mach: ts_peek comes with the coreless machine"); }
void TileRef::hist_poke(uint32_t, uint32_t) { FATAL("mach: hist_poke comes with the coreless machine"); }
uint32_t TileRef::hist_peek(uint32_t) const { FATAL("mach: hist_peek comes with the coreless machine"); }

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
std::vector<std::array<uint64_t, 4>> Cc::received() const { FATAL("mach: Cc::received comes with the coreless machine"); }
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
        for (const char* d : {"fw/build", "tests/golden-m2/images", "tests/golden/images", "tests/m3-images"}) dirs.push_back(d);
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
void A2A::load(Chip&, int) const { FATAL("A2A::load comes with the coreless machine"); }
int64_t A2A::run(Chip&, int64_t) { FATAL("A2A::run comes with the coreless machine"); }

}  // namespace mach
