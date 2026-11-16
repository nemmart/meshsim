// mach.cpp: the machine harness (mach.h). Configuration and between-cycle code only (D6 doesn't apply).
#include "mach.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>

#include "base/fatal.h"
#include "checker.h"
#include "rtl_images.h"
#include "units/ag.h"
#include "units/big_sram.h"
#include "units/director.h"
#include "units/gather.h"
#include "units/inject.h"
#include "units/reqbus.h"
#include "units/router.h"
#include "units/write_engine.h"
#include "bench_master.h"
#include "units/ingress_egress.h"
#include "units/linkword.h"
#include "units/net.h"
#include "units/table_store.h"
#include "units/tile_ctl.h"
#include "units/updater.h"

namespace mach {

namespace {

bool exists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }

// A data file by its path in the version's folder.
std::string data_path(std::initializer_list<const char*> candidates) {
    for (const char* c : candidates) if (exists(c)) return c;
    std::string all;
    for (const char* c : candidates) { all += all.empty() ? "" : ", "; all += c; }
    FATAL("mach: none of %s found (run from the version's folder)", all.c_str());
}

// A compute tile's straps with boot_local (MachConfig::boot_local; 7.1, 6.5): the builder's, and that bit on tiles 0-63.
units::Straps boot_local_straps(unsigned k, const units::G1Config& cfg) {
    units::Straps s = units::default_straps(k, cfg.periph_present);
    if (k < units::TILE_COUNT) s.boot_local = true;
    return s;
}

}  // namespace

// ================================================================================================= the configuration
MachConfig MachConfig::defaults() { return MachConfig{}; }
MachConfig MachConfig::machine() {
    MachConfig c;
    c.coreless = false;
    c.real_responder = true;
    return c;
}
MachConfig MachConfig::chip() {
    MachConfig c = machine();
    c.special_tiles = true;
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
namespace {
// v1.3's Config fields by name: an int, a long or a boolean member, or dropped (dropped()).
struct Field { const char* name; char kind; size_t off; };   // kind: 'u' unsigned, 'i' int, 'l' int64_t, 'b' bool
#define F_U(n, m) {n, 'u', offsetof(MachConfig, m)}
#define F_I(n, m) {n, 'i', offsetof(MachConfig, m)}
#define F_L(n, m) {n, 'l', offsetof(MachConfig, m)}
#define F_B(n, m) {n, 'b', offsetof(MachConfig, m)}
const Field kFields[] = {
    F_U("linkLatency", link_latency), F_U("queueDepth", queue_depth), F_U("intraTileLatency", intra_tile_latency),
    F_B("skew", skew), F_L("skewSeed", skew_seed), F_U("readPortDepth", read_port_depth),
    F_U("gatherDepth", gather_depth), F_U("cmLatency", cm_latency), F_U("refCycle", ref_cycle),
    F_U("rpLatency", rp_latency), F_U("K", K), F_U("memChunks", mem_chunks),
    F_U("readLatency", read_latency),
    F_U("readTokens", read_tokens), F_B("loopbackLast", loopback_last), F_B("loopbackTokens", loopback_tokens),
    F_I("loopbackDepth", loopback_depth), F_U("tsEngineLatency", ts_engine_latency), 
    F_B("testReadClients", test_read_clients), F_U("regLoadLatency", reg_load_latency),
    F_B("ccCores", special_tiles),                                     // (ccCores: v1.3's name of the machine with units 64-66)
    F_U("arriveLatency", arrive_latency), F_U("releaseLatency", release_latency), F_U("errorLatency", error_latency),   // (v1.36's: 18)
    F_U("requestFifoDepth", request_queue_depth), F_U("reqHopLatency", req_hop_latency), F_I("periphType", periph_type),
    F_B("realResponder", real_responder), F_B("coreless", coreless), F_U("goLatency", go_latency),
    F_U("readyLatency", ready_latency), F_B("cncGoSettle", cnc_go_settle), F_U("cncReaction", cnc_reaction),
    F_B("checkReadPortOverlap", check_read_port_overlap), F_B("bootLocal", boot_local),   // (bootLocal: v1.36's, not ref's)
    F_B("checkDisjoint", check_disjoint), F_L("shuffleSeed", shuffle_seed), F_U("maxViolationMessages", max_violation_messages),
    F_B("measureS1", measure_s1), F_B("checkErrors", check_errors), F_B("hostLink", host_link),   // (hostLink: v1.37's)
    F_U("cncRxDepth", cnc_rx_depth),
    F_U("replayerLogEntries", replayer_log_entries),   // (not ref's Config: its -Dreplayer.logq; the kit's -q)
};
#undef F_U
#undef F_I
#undef F_L
#undef F_B
}  // namespace

const std::vector<std::pair<std::string, std::string>>& MachConfig::dropped() {
    static const std::vector<std::pair<std::string, std::string>> d = {
        {"idleSkip", "ref's own speed option (Chip's sleep/skip); the framework is event-driven"},
        {"startSkew", "set by ref's Chip from skewSeed; no driver sets it (skew, skewSeed do)"},
        {"receiveQueueDepth", "an array; no driver, kit or sweep sets it (the request bus's default {128, 256, 128})"},
        {"periphTypes", "an array; no driver sets it"},
        {"testPeripheral", "a Java function; v1.36: no machine with the replayer on one tile only"},
        {"checkUnexpected", "v1.36: a counter underflow is the hardware's error (12.2), reported with ERRORS (checkErrors)"},
        {"hops", "the hop table: data/hops.txt"},
        {"cc3Tracer", "v1.36: the tracer tile's peripheral 1 is the Tracer (2.1)"},
        {"ccLinkLatency", "v1.36: an attach link is a link like the others (16.1: linkLatency)"},
        // v1.36: parameters of hardware, or of checks of hardware, that the RTL's tile does not have
        {"runLatency", "v1.36: no RUN state (the barrier: machine-spec 7.3)"},
        {"stageDelay", "v1.36: v1.35's boot staging is gone (the boot ROM pulls its image with LOAD: 6.5)"},
        {"counterStoreClearsLatch", "v1.36: the counter events' priority is the RTL's (7.2), not a choice"},
        {"doneGate", "v1.36: no DONE and no gate (ARRIVE and the arrival gate: 7.3)"},
        {"strictReplies", "v1.36: no reply block, so no check of it"},
        {"checkCommandRead", "v1.36: no command block, so no check of its reads"},
        {"agLookahead", "v1.36: READ_AHEAD is 2 TABLE_LATENCY + 2 (machine-spec 8.5), not a setting"},
        {"weInDepth", "v1.36: the write engine is a fixed pipeline with no input FIFO (machine-spec 12.4)"},
        {"areaQueueDepth", "v1.36: the scratchpad has no bank request queues (a write wins its bank: machine-spec 15)"},
        {"writeCombining", "v1.36: no bank request queues, so no write to combine with (15)"},
        {"fifoCountBeforePop", "v1.36: it chose how a bank request queue judged its room; there is none (15)"},
        {"sramReadLatency", "v1.36: READ_LATENCY, take to row (15, 19): readLatency"},
        {"ccDmaDepth", "v1.36: no FIFO in front of a DMA's router input: its ready is room in the queue its packet enters (16.4)"},
        {"logTrace", "v1.36: no TRACE word (a program reports with a REPORT request: 11.5)"},
    };
    return d;
}

bool MachConfig::set(const std::string& field, const std::string& value, std::string& err) {
    for (const auto& d : dropped())
        if (d.first == field) { err = "Config." + field + ": dropped (" + d.second + ")"; return false; }
    for (const Field& f : kFields) {
        if (field != f.name) continue;
        char* base = reinterpret_cast<char*>(this) + f.off;
        if (f.kind == 'b') {                              // Boolean.parseBoolean: "true" in any case, else false
            std::string v = value;
            for (char& ch : v) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            *reinterpret_cast<bool*>(base) = v == "true";
            return true;
        }
        char* end = nullptr;
        errno = 0;
        const long long v = std::strtoll(value.c_str(), &end, 10);
        if (value.empty() || *end || errno) { err = "Config." + field + ": not a number: " + value; return false; }
        if (f.kind == 'l') *reinterpret_cast<int64_t*>(base) = v;
        else if (f.kind == 'i') *reinterpret_cast<int*>(base) = int(v);
        else {
            if (v < 0) { err = "Config." + field + ": " + value + " (not negative here)"; return false; }
            *reinterpret_cast<unsigned*>(base) = unsigned(v);
        }
        return true;
    }
    err = "no Config field " + field;
    return false;
}

bool MachConfig::set_args(const std::vector<std::string>& args, std::string& err) {
    for (const std::string& a : args) {
        if (a.rfind("cfg.", 0) != 0) continue;
        const size_t eq = a.find('=');
        if (eq == std::string::npos) { err = a + ": cfg.<field>=<value>"; return false; }
        if (!set(a.substr(4, eq - 4), a.substr(eq + 1), err)) return false;
    }
    return true;
}

// ===================================================================================================== the chip
struct Chip::Impl {
    std::vector<uint8_t> link_bytes;   // v1.37: the packet file Ingress reads (Mach::set_link)
    units::Hops hops;
    std::vector<uint32_t> rom, mem;
    units::SramBlocks sram;
    units::G1Ctx gx;
    units::CncProgram prog;
    std::optional<sim::Sim> sim;
    unsigned nunits = 64;
    uint8_t skew[64] = {};
    std::unique_ptr<Cc> cc;
    std::unique_ptr<ReqBus> reqbus;
    bool drain = false;                                   // Cc::drain_reports
    std::vector<std::array<int64_t, 6>> dlog[3];          // ReqBus::delivered_log
    std::vector<std::array<int64_t, 5>> reports, requests;
    std::unique_ptr<Checker> checker;                    // the violations (src/check.h; on with SIM_CHECKS)
    // the hardware FIFOs for invariant 1 (each tile's, then the master side's receive FIFOs), in scan order
    // (dir: the gather queue's traffic director, which loses a request for the full queue: director.h)
    struct Hw { int tile; units::Router* router; unsigned q; const sim::FifoIn* in; bool reported; const units::Director* dir = nullptr; };
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
    // Net (the coreless machine fed from the harness): each tile's feed (inject.h's HarnessFeed, harness-only) and the
    // rings' storage (grown between cycles, the feed's pointers refreshed), and the directors' logs (fixed size)
    using E4 = std::array<uint64_t, 4>;
    std::vector<units::HarnessFeed> feeds;
    std::vector<std::vector<E4>> ring_store;            // tile k's ring r (0: ret, 1-3: clients 0-2) at 4k + r
    std::vector<std::vector<E4>> logs;
    std::vector<E4> writer_logs[64], gather_logs[64];     // TileRef::record_writer, record_gather
    // the endpoints' harness parts (cnc.h's HarnessSends): C&C 1 (the Java model), C&C 2, C&C 3
    units::HarnessSends sends[3];
    std::vector<std::array<int64_t, 2>> plog;             // C&C's phase log (Cc::phase_log)
    std::vector<units::HarnessSends::E> send_store[3];
    std::vector<E4> recv[3];
    void push_send(unsigned cc, const units::HarnessSends::E& e);
    bool net = false;
    std::vector<uint16_t> shifted;                      // the register block's and the core's types (Sim::in_flight)
    std::vector<uint16_t> lines;                        // C&C's row collectors and C&C: ref's READY and error lines
    void enable_net(const MachConfig& c);
    void push(unsigned tile, unsigned ring, const E4& e);

    bool has_cnc = false;
    bool split = false;                                   // more than one thread domain
    units::Cnc* cnc() { return has_cnc ? &sim->unit<units::Cnc>("cnc") : nullptr; }
    uint32_t* dmem(unsigned u) { return mem.data() + size_t(u) * 2 * units::MEM_WORDS; }
    uint32_t* imem(unsigned u) { return dmem(u) + units::MEM_WORDS; }
    void run_calls();
};

Chip::Chip(const MachConfig& c) : cfg_(c), impl_(std::make_unique<Impl>()) {
    Impl& m = *impl_;
    if (c.K < c.k_min())                                   // TrafficDirector's constructor (ref: IllegalArgumentException)
        FATAL("K = %u is below meshsim's max(2L + 4, L + 4, cm_latency, rp_latency) = %u at L = %u (4.7, D49; invariant 24)",
              c.K, c.k_min(), c.ts_engine_latency);
    if (c.special_tiles && (c.coreless || c.reqbus_host || c.test_read_clients))
        FATAL("mach: the chip with its special tiles is the machine as specified (no coreless, reqbus_host or test_read_clients)");
    m.hops = units::load_hops(data_path({"data/hops.txt"}).c_str());
    const rtl_images::BootRom rom = rtl_images::boot_rom();   // the RTL's boot ROM (6.5)
    m.rom = rom.words;
    m.nunits = c.special_tiles ? 67 : 64;
    m.mem.assign(size_t(m.nunits) * 2 * units::MEM_WORDS, 0);

    units::G1Config& g = m.gx.cfg;
    g.timing.reg_load_latency = c.reg_load_latency;
    g.timing.ref_cycle = c.ref_cycle;
    g.mem_chunks = c.mem_chunks;
    g.periph_present = c.periph_type != 0 ? 1 : 0;
    if (c.host_link && !c.special_tiles) FATAL("mach: host_link is the chip's (the master tile's Ingress/Egress and Updater)");
    if (c.host_link && c.parts > 1) FATAL("mach: host_link with the chip split over threads: the Updater and the Tracer share one file store");
    g.host_link = c.host_link;
    g.periph_slot = c.periph_type != 0 || c.special_tiles;     // ref: every tile's slot holds its peripheral 1 (type 4)
    if (c.periph_type != 0 && c.periph_type != 4) FATAL("mach: periph_type %d (4 the replayer, or 0)", c.periph_type);
    if (c.test_peripheral_tile >= 64) FATAL("mach: test_peripheral_tile %d (a mesh tile)", c.test_peripheral_tile);
    g.test_periph_tile = c.test_peripheral_tile;
    g.replayer_log_entries = c.replayer_log_entries;
    g.request_queue_depth = c.request_queue_depth;
    g.K = c.K; g.cm_latency = c.cm_latency;
    g.ts_engine_latency = c.ts_engine_latency;
    g.read_latency = c.read_latency;
    g.read_port_depth = c.read_port_depth;
    m.gx.bootrom = m.rom.data(); m.gx.bootrom_words = uint32_t(m.rom.size()); m.gx.bootrom_handler = rom.handler;
    if (c.boot_local) {
        if (c.test_peripheral_tile >= 0)
            FATAL("mach: boot_local with the test peripheral on one tile: the straps are per machine here");
        m.gx.straps = boot_local_straps;
    }
    m.gx.mem = m.mem.data(); m.gx.units = m.nunits; m.gx.sram = &m.sram;
    m.gx.gather_depth = c.gather_depth;

    units::NetConfig nc;
    nc.link_latency = c.link_latency; nc.queue_depth = c.queue_depth; nc.intra_tile_latency = c.intra_tile_latency;
    nc.read_tokens = c.read_tokens;
    nc.go_latency = c.go_latency; nc.ready_latency = c.ready_latency; nc.cnc_reaction = c.cnc_reaction;
    nc.cnc_rx_depth = c.cnc_rx_depth; nc.skew = c.skew; nc.skew_seed = c.skew_seed;
    nc.loopback_last = c.loopback_last; nc.loopback_tokens = c.loopback_tokens; nc.loopback_depth = c.loopback_depth;
    nc.cnc_go_settle = c.cnc_go_settle;
    nc.arrive_latency = c.arrive_latency; nc.release_latency = c.release_latency; nc.error_latency = c.error_latency;
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
    spec.bench_attach = !c.special_tiles;
    spec.reqbus.fifo_depth = c.request_queue_depth;
    // the request tree: the chip's is the RTL's (a cycle a node, the nodes' room as registers: reqbus.h); the bench
    // machine's v1.35's (req_hop_latency; exact as ref with the harness's host, D34)
    spec.reqbus.hop_latency = c.special_tiles ? 1 : c.req_hop_latency;
    spec.reqbus.credit_latency = c.special_tiles ? 1 : c.reqbus_host ? 0 : 3;
    for (unsigned q = 0; q < 3; q++) spec.reqbus.queue_depth[q] = c.receive_queue_depth[q];
    if (c.special_tiles) units::build_chip(sc, spec);
    else units::build_bench_chip(sc, spec, m.prog, c.reqbus_host);
    m.sim.emplace(sc.build());
    // The harness's machine (harness-only flags in the units): the coreless core, the drain in the gather unit's place
    // (real_responder off), and the table stores' republish of a harness poke.
    for (unsigned k = 0; k < 64; k++) {
        if (c.coreless) m.sim->unit<units::TileCore>(units::tile_unit(k, "core")).coreless = true;
        if (!c.real_responder) m.sim->unit<units::Gather>(units::tile_unit(k, "gather")).drain = true;
        m.sim->unit<units::TableStore>(units::tile_unit(k, "ts")).host_poke = true;
        m.sim->unit<units::HistStore>(units::tile_unit(k, "hist")).host_poke = true;
    }
    if (c.host_link) {   // v1.37: one file store, the Tracer's memory system on it (cold at a state write), the Updater's too
        units::tracer::Tracer& tr = m.sim->unit<units::PeriphSlot>(units::unit_prefix(units::ID_TRACER) + "slot").s.tracer(1);
        tr.own_memory(std::make_unique<units::tracer::FileStore>());
        m.sim->unit<units::PeriphSlot>(units::unit_prefix(units::ID_MASTER) + "slot").s.updater(2).store = tr.store();
    }
    m.shifted = {m.sim->type_of(units::tile_unit(0, "ctl")), m.sim->type_of(units::tile_unit(0, "core"))};
    if (!c.special_tiles) m.lines = {m.sim->type_of("row0"), m.sim->type_of("cnc")};
    // ref's quiescent leaves the request bus out (Tile.quiescent, Chip.quiescent: no request port, node or queue)
    if (c.threads <= 1)
        for (const char* u : {"t0.reqleaf", "reqbus.row0", "reqbus.root", "cc1.reqrecv", "reqbus.host"})
            if (m.sim->has_unit(u)) m.lines.push_back(m.sim->type_of(u));
    CheckSwitches sw;
    sw.errors = c.check_errors; sw.read_port_overlap = c.check_read_port_overlap;
    sw.K = c.K;
    sw.disjoint = c.check_disjoint;                       // the switch (ref's checkDisjoint)
    m.checker = std::make_unique<Checker>(sim::check::kOn ? c.check_capacity : 0, c.max_violation_messages, sw);
    for (unsigned k = 0; k < m.nunits; k++) {             // the units that record facts under their tile (src/check.h)
        units::TileCtl& b = m.sim->unit<units::TileCtl>(units::unit_prefix(k) + "ctl");
        b.ck_tile = int(k); b.s.ck_tile = int(k);
        m.sim->unit<units::TileCore>(units::unit_prefix(k) + "core").rv.ck_tile = int(k);
        if (k < 64) m.sim->unit<units::WriteEngine>(units::tile_unit(k, "we")).ck_tile = int(k);
        if (k < 64)                                       // ref's measureS1 (the check builds only)
            if (auto& s1 = m.sim->unit<units::Director>(units::tile_unit(k, "dir")).s1) s1->on = c.measure_s1;
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
            m.hw.push_back({int(k), nullptr, 0, nullptr, false, &m.sim->unit<units::Director>(units::tile_unit(k, "dir"))});
            m.checker->fifo_names.push_back(t + "gather-q");
        }
        if (!c.special_tiles) {
            m.hw.push_back({-1, nullptr, 0, &m.sim->unit<units::Cnc>("cnc").rx, false}); m.checker->fifo_names.push_back("cc1.rx");
            m.hw.push_back({-1, nullptr, 0, &m.sim->unit<units::Endpoint>("ep2").rx, false}); m.checker->fifo_names.push_back("cc2.rx");
            m.hw.push_back({-1, nullptr, 0, &m.sim->unit<units::Endpoint>("ep3").rx, false}); m.checker->fifo_names.push_back("cc3.rx");
        }
    }
    m.has_cnc = !c.special_tiles;
    m.split = c.parts > 1;
    if (c.reqbus_host && !c.special_tiles)                     // the receive queues' delivery logs (ReqBus::delivered_log)
        for (unsigned q = 0; q < 3; q++) {
            static const char* kRecv[3] = {"cc1.reqrecv", "cc2.reqrecv", "cc3.reqrecv"};
            m.dlog[q].reserve(c.log_capacity);
            m.sim->unit<units::reqbus::ReqRecv>(kRecv[q]).dlog = &m.dlog[q];
        }
    if (!c.special_tiles) {                                    // ref's CnC.phaseLog
        m.plog.assign(c.phase_log_capacity, {});
        units::Cnc& cn = m.sim->unit<units::Cnc>("cnc");
        cn.plog = reinterpret_cast<int64_t(*)[2]>(m.plog.data()); cn.plog_cap = c.phase_log_capacity;
    }
    if (!c.special_tiles)                                      // the endpoints' receive traces (ref's received), every machine
        for (unsigned k = 0; k < 3; k++) {
            m.recv[k].assign(c.log_capacity, {});
            m.sends[k].log = reinterpret_cast<uint64_t(*)[4]>(m.recv[k].data());
            m.sends[k].log_cap = c.log_capacity;
            if (k == 0) m.sim->unit<units::Cnc>("cnc").hs = &m.sends[0];
            else m.sim->unit<units::Endpoint>("ep" + std::to_string(k + 1)).hs = &m.sends[k];
        }
    if (!c.special_tiles) m.cc = std::make_unique<Cc>(*this);
    for (uint32_t id = units::CncProgram::kCalls; id-- > 0;) m.free_ids.push_back(id);
}



Chip::~Chip() {
    if (impl_ && impl_->checker) impl_->checker->uninstall();
}

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
        uint64_t first = 0;
        bool flag = false;                                // the FIFO has overflowed
        if (h.router) { first = h.router->q[h.q].first_overflow; flag = first != 0; }
        else if (h.dir) { first = h.dir->gather_first_overflow; flag = first != 0; }   // the gather queue
        // (a machine split over threads: the framework reads a FIFO's overflows in one domain only)
        else if (split) continue;
        else { sim::Cycle f = sim::kNoCycle; sim->fifo_overflows(*h.in, &f); first = f; flag = f != sim::kNoCycle; }
        if (!flag) continue;
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

ReqBus& Chip::reqbus() {
    if (!cfg_.reqbus_host) FATAL("mach: reqbus(): MachConfig::reqbus_host (the request bus driven from the test)");
    if (!impl_->reqbus) impl_->reqbus = std::make_unique<ReqBus>(*this);
    return *impl_->reqbus;
}

// ---- ref's RequestBus from a test ----
namespace {
units::reqbus::ReqRecv& recv_of(Chip& c, unsigned target) {
    static const char* kRecv[3] = {"cc1.reqrecv", "cc2.reqrecv", "cc3.reqrecv"};
    if (target > 2) FATAL("mach: reqbus: target %u has no receive queue (0-2)", target);
    return c.unit<units::reqbus::ReqRecv>(kRecv[target]);
}
bool link_empty(sim::Sim& s, const sim::LinkIn& p) { sim::PortEntry e[64]; return s.observe(p, e, 64).n == 0; }
}  // namespace
// ref's RequestBus.idle: no node input holds a request or a beat, or has one on its way (Buf.occ, Buf.reqs), and no
// request is on its way to a receive queue (toReceiver). The request ports' FIFOs are not part of it (TileRef::req_idle).
bool ReqBus::idle() const {
    sim::Sim& s = c_->sim();
    auto node_idle = [&](const units::reqbus::ReqNode& n) {
        for (unsigned i = 0; i < n.n_in; i++)
            if ((n.present >> i & 1) && (n.buf[i].n || !link_empty(s, n.in[i]))) return false;
        return true;
    };
    if (!node_idle(c_->unit<units::reqbus::ReqRoot>("reqbus.root"))) return false;
    for (unsigned r = 0; r < 8; r++) if (!node_idle(c_->unit<units::reqbus::ReqNode>("reqbus.row" + std::to_string(r)))) return false;
    static const char* kRecv[3] = {"cc1.reqrecv", "cc2.reqrecv", "cc3.reqrecv"};
    for (unsigned q = 0; q < 3; q++)
        if (s.has_unit(kRecv[q]) && !link_empty(s, c_->unit<units::reqbus::ReqRecv>(kRecv[q]).deliver)) return false;
    if (s.has_unit("reqbus.host")) {                      // a controller's request not yet all sent (ref's controllerSend)
        const units::reqbus::ReqHost& h = c_->unit<units::reqbus::ReqHost>("reqbus.host");
        for (unsigned k = 0; k < 3; k++) if (h.send[k].on) return false;
    }
    return true;
}
bool Chip::toast() const {
    if (cfg_.special_tiles) return const_cast<Chip*>(this)->unit<units::PeriphSlot>("cc1.slot").s.ingress_egress(1).toast;
    return const_cast<Chip*>(this)->unit<units::Cnc>("cnc").toast;
}
int64_t Chip::toast_at() const {
    if (cfg_.special_tiles) return const_cast<Chip*>(this)->unit<units::PeriphSlot>("cc1.slot").s.ingress_egress(1).toast_at;
    return const_cast<Chip*>(this)->unit<units::Cnc>("cnc").toast_at;
}
bool ReqBus::head(unsigned target, int64_t t, uint32_t w[4]) const {   // ref's head(target, t): delivered before t
    const units::reqbus::ReqRecv& r = recv_of(*c_, target);
    const uint64_t skip = popped_[target] - r.received;                 // the test's pops not yet made
    if (skip >= r.qn) return false;
    const auto& e = r.q[(r.qh + skip) % r.depth];
    if (e.delivered_t >= t) return false;
    for (unsigned i = 0; i < 4; i++) w[i] = e.w[i];
    return true;
}
bool ReqBus::pending(unsigned target, int64_t t) const { uint32_t w[4]; return head(target, t, w); }
uint32_t ReqBus::get(unsigned target, int64_t t, unsigned i) const { uint32_t w[4]; return head(target, t, w) ? w[i & 3] : 0; }
bool ReqBus::peek_request(unsigned target, int64_t t, uint32_t w[4]) const { return head(target, t, w); }
uint32_t ReqBus::receive(unsigned target, int64_t t) {
    uint32_t w[4];
    if (!head(target, t, w)) { underflows_++; return 0; }
    if (t != c_->cycle() + 1) FATAL("mach: reqbus.receive at cycle %lld: the next cycle only (%lld)", (long long)t, (long long)c_->cycle() + 1);
    units::reqbus::ReqHost& h = c_->unit<units::reqbus::ReqHost>("reqbus.host");
    popped_[target]++; received_[target]++;
    if (h.pop_at[target] == t) {                          // a further pop in this cycle: with the first one's Link words
        auto add = [&](units::reqbus::HostExtra* x, const char* where) {
            for (unsigned i = 0; i < units::reqbus::HOST_EXTRA; i++) if (x[i].n && x[i].at == t) { x[i].n++; return; }
            for (unsigned i = 0; i < units::reqbus::HOST_EXTRA; i++) if (!x[i].n) { x[i].at = t; x[i].n = 1; return; }
            FATAL("mach: reqbus.receive: %s holds %u cycles of further pops", where, units::reqbus::HOST_EXTRA);
        };
        add(recv_of(*c_, target).host_extra, "the receive queue");
        add(c_->unit<units::reqbus::ReqRoot>("reqbus.root").host_extra[target], "the root");
        return w[3];
    }
    h.pop_at[target] = t;
    c_->touched("reqbus.host");
    return w[3];
}
void ReqBus::controller_send(int64_t t, unsigned cc, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
    if (t != c_->cycle()) FATAL("mach: reqbus.controller_send at cycle %lld: the cycle just run only (%lld)", (long long)t, (long long)c_->cycle());
    if (cc > 2) FATAL("mach: reqbus.controller_send: C&C %u (0-2)", cc + 1);
    units::reqbus::ReqHost& h = c_->unit<units::reqbus::ReqHost>("reqbus.host");
    auto& s = h.send[cc];
    if (s.on) FATAL("mach: reqbus.controller_send: C&C %u's previous request still entering the root (ref's beats overlap: not modelled)", cc + 1);
    s.w[0] = (w0 & ~0x7Fu) | (64 + cc); s.w[1] = w1; s.w[2] = w2; s.w[3] = w3;
    s.store_t = t; s.next = 1; s.on = true;
    const sim::Word b[3] = {units::reqbus::beat_w0(0, 64 + cc, t), sim::Word(s.w[0]) | sim::Word(s.w[1]) << 32, sim::Word(s.w[2]) | sim::Word(s.w[3]) << 32};
    c_->sim().put_late("reqbus.host", h.beat[cc], b, 3);     // beat 0 as of t: arriving at t + hop, as ref's
    c_->touched("reqbus.host");
}
size_t ReqBus::queue_size(unsigned target) const {
    const units::reqbus::ReqRecv& r = recv_of(*c_, target);
    return r.qn - (popped_[target] - r.received);
}
int64_t ReqBus::credits(unsigned target) const { return c_->unit<units::reqbus::ReqRoot>("reqbus.root").credits[target & 7]; }
uint64_t ReqBus::dropped() const { return c_->unit<units::reqbus::ReqRoot>("reqbus.root").dropped; }
std::vector<std::array<int64_t, 6>> ReqBus::delivered_log() const {
    const Chip::Impl& m = c_->impl_const();
    std::vector<std::array<int64_t, 6>> r;
    for (unsigned q = 0; q < 3; q++) r.insert(r.end(), m.dlog[q].begin(), m.dlog[q].end());
    std::stable_sort(r.begin(), r.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });   // (one delivery a cycle)
    return r;
}
uint64_t ReqBus::delivered() const { return c_->unit<units::reqbus::ReqRoot>("reqbus.root").delivered; }


void drain_next(Chip& c);   // Cc::drain_reports' (below)
void Chip::step() {
    impl_->checker->install();
    impl_->sim->run(1);
    impl_->collect_overflows();
    impl_->checker->process(cycle(), false);
    impl_->run_calls();
    impl_->check_waits();
    drain_next(*this);
}
void Chip::run(uint64_t n) {
    impl_->sim->wake_all();
    if (impl_->calls.empty() && impl_->waits.empty() && !impl_->drain) {
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
        if (in.tok.reads) return false;                   // a read taken, not yet injected (its row on its way)
        if (p.unit<units::WriteEngine>("we").count) return false;
        if (p.unit<units::Gather>("gather").left) return false;
        // the read port (ref's readPort.idle): its results stored and not yet injected (none under the test clients)
        if ((!in.rs.feed || !in.rs.feed->clients) && p.unit<units::TileCore>("core").rp_stored != in.rs.rp_injected_n) return false;
    }
    return true;
}
std::vector<int> Chip::start_skew() const { return std::vector<int>(impl_->skew, impl_->skew + 64); }

Cc& Chip::cc() {
    if (!impl_->cc) FATAL("mach: Chip::cc(): no model of the master side with cc_cores");
    return *impl_->cc;
}

bool Chip::all_in_wfi() const {
    for (unsigned k = 0; k < 64; k++) if (!impl_->sim->unit<units::TileCore>(units::tile_unit(k, "core")).rv.in_wfi) return false;
    return true;
}


// ---- the checker (checker.cpp) ----
// Every fact recorded so far, processed. (The tile control units record their ERRORS and error lines for a cycle at their
// next step: a report made between cycles has them through the cycle before the one just run.)
void Chip::finalize_checks() const {
    if (!sim::check::kOn) return;
    impl_->checker->process(cycle(), false, true);
}
uint64_t Chip::violations() const { finalize_checks(); return impl_->checker->violations(); }
const std::vector<std::string>& Chip::violation_messages() const { finalize_checks(); return impl_->checker->messages(); }
void Chip::clear_violation_messages() { finalize_checks(); impl_->checker->clear_messages(); }
void Chip::set_check_errors(bool on) { finalize_checks(); impl_->checker->switches().errors = on; cfg_.check_errors = on; }
void Chip::set_max_violation_messages(unsigned n) { finalize_checks(); impl_->checker->set_max_messages(n); cfg_.max_violation_messages = n; }
void Chip::clear_violations() { finalize_checks(); impl_->checker->clear_count(); impl_->checker->clear_messages(); }
void Chip::new_phase() {
    for (unsigned k = 0; k < 64; k++) {
        unit<units::BigSram>(units::tile_unit(k, "sram")).new_phase();
        touched(units::tile_unit(k, "sram"));
    }
}
void Chip::scan_overflows() { impl_->collect_overflows(); impl_->checker->process(cycle(), true); }
std::string Chip::violation_report() {
    scan_overflows();
    finalize_checks();
    std::string sb = "violations: " + std::to_string(violations()) + "\n";
    for (const std::string& s : violation_messages()) sb += "  " + s + "\n";
    return sb;
}
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
        we.taken = we.written = 0;
        units::TableStore& ts = p.unit<units::TableStore>("ts");
        ts.table_reads = ts.rmws = 0;
        if (sim::check::kOn) {                            // the framework FIFOs' peaks (kept under SIM_CHECKS: the)
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

// The core's memories: region 0x00 DMEM, 0x01 IMEM, byte addresses.
namespace {
uint32_t* local(Chip::Impl& m, unsigned k, uint32_t a) {
    const uint32_t region = a >> 24, off = (a & 0xFFFFFF) >> 2;
    if ((region != 0 && region != 1) || off >= units::MEM_WORDS || (a & 3))
        FATAL("mach: tile %u: 0x%08x is not a word of the core's IMEM or DMEM", k, a);
    return (region == 1 ? m.imem(k) : m.dmem(k)) + off;
}
}  // namespace
// A word of the core's memory as it is at the end of the cycle just run: the NoC port's write of that cycle
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

uint64_t TileRef::send_refusals() const { return c_->sim().hold_refusals(unit<units::Injection>("inj").send); }

// ---- the write unit's holding register from a test ----
void TileRef::send_put(uint64_t lo, uint64_t hdr, uint64_t hi) {
    units::TileCore& core = unit<units::TileCore>("core");
    if (!core.coreless) FATAL("mach: tile %u: send_put: the coreless machine only (a running core's write unit owns the port)", k_);
    const sim::Word w[3] = {lo, hdr, hi};
    c_->sim().put_late(un(k_, "core"), core.send, w, 3);
}
bool TileRef::send_empty() const {                         // nothing in the register, nothing put for the next cycle
    sim::PortEntry e[1];
    const sim::PortView v = c_->sim().observe(unit<units::Injection>("inj").send, e, 1);
    return !v.valid && !v.put && v.n == 0;
}

// ---- the tables (back doors) ----
namespace {
uint32_t table_read(const units::TableMems& m, uint32_t table, uint32_t i) {
    switch (table) {
    case units::TBL_STREAM_SRC: return m.stream_src[i & 255];
    case units::TBL_STREAM_DEST: return m.stream_dest[i & 255];
    case units::TBL_STREAM_HDR: return m.stream_count[i & 255];
    case units::TBL_GROUP: return m.group[i & 127];
    case units::TBL_PTR: return m.ptr[i & 255];
    default: FATAL("mach: table %u (units::TBL_STREAM_SRC, TBL_STREAM_DEST, TBL_STREAM_HDR, TBL_GROUP, TBL_PTR; HIST: hist_peek)", table);
    }
}
}  // namespace
void TileRef::table_poke(uint32_t table, uint32_t i, uint32_t v) {
    if (table > units::TBL_PTR) FATAL("mach: table_poke: table %u (HIST: hist_poke)", table);
    unit<units::TableStore>("ts").poke_host(table, i, v & units::table_mask(table, i));
    c_->touched(un(k_, "ts"));
}
// A table store of the cycle just run (the core's, on `window`, applied by the table store at its next step) is in the
// table at the end of that cycle, unless PTR's write-back took the port then (12.5: then it is lost).
uint32_t TileRef::table_peek(uint32_t table, uint32_t i) const {
    const units::TableStore& ts = unit<units::TableStore>("ts");
    uint32_t v = table_read(ts.m, table, i);
    sim::PortEntry e[2];
    const uint32_t n = c_->sim().observe(ts.window, e, 2).n;
    const uint32_t mask = table == units::TBL_GROUP ? 127 : 255;
    for (uint32_t k = 0; k < n; k++)
        if (e[k].at == uint64_t(c_->cycle()) + 1 && uint32_t(e[k].w[0] >> 32) == table && (uint32_t(e[k].w[0]) & mask) == (i & mask) &&
            !(table == units::TBL_PTR && ts.pipe.last_wb == uint64_t(c_->cycle())))
            v = uint32_t(e[k].w[1]);
    return v;
}
void TileRef::hist_poke(uint32_t i, uint32_t v) {
    unit<units::HistStore>("hist").poke_host(i, v & units::table_mask(units::TBL_HIST, i));
    c_->touched(un(k_, "hist"));
}
uint32_t TileRef::hist_peek(uint32_t i) const {           // (with the store of the cycle just run: table_peek's rule)
    const units::HistStore& h = unit<units::HistStore>("hist");
    uint32_t v = h.m.hist[i & 255];
    sim::PortEntry e[2];
    const uint32_t n = c_->sim().observe(h.window, e, 2).n;
    for (uint32_t k = 0; k < n; k++)
        if (e[k].at == uint64_t(c_->cycle()) + 1 && (uint32_t(e[k].w[0]) & 255) == (i & 255) && h.pipe.last_wb != uint64_t(c_->cycle()))
            v = uint32_t(e[k].w[1]);
    return v;
}
void TileRef::set_ptr(unsigned i, uint32_t word) { table_poke(units::TBL_PTR, i, word); }
uint32_t TileRef::ptr(unsigned i) const { return table_peek(units::TBL_PTR, i); }
void TileRef::set_stream(unsigned i, uint32_t src, uint32_t dest, uint32_t hdr) {
    table_poke(units::TBL_STREAM_SRC, i, src); table_poke(units::TBL_STREAM_DEST, i, dest); table_poke(units::TBL_STREAM_HDR, i, hdr);
}
void TileRef::set_group(unsigned g, uint32_t w0, uint32_t w1) { table_poke(units::TBL_GROUP, 2 * g, w0); table_poke(units::TBL_GROUP, 2 * g + 1, w1); }

// ---- the units' registers (back doors): the tile control unit's state, published at its next step ----
const units::RegBlockState& TileRef::block() const { return unit<units::TileCtl>("ctl").s; }
void TileRef::set_dma_config(uint32_t v) {
    unit<units::TileCtl>("ctl").s.dma_config = v & units::DMA_CONFIG_BITS;
    c_->touched(un(k_, "ctl"));
}
void TileRef::set_receive_config(uint32_t v) {
    const uint32_t bd = units::rc_bucket_digit(v), hd = units::rc_hist_digit(v);
    if (bd > 4 || hd > 4)                                   // 12.5: digits 5-7 are reserved
        FATAL("mach: tile %u, cycle %lld: RECEIVE_CONFIG with a reserved digit (bucket %u, histogram %u; 5-7 are reserved: "
              "machine-spec 12.5)", k_, (long long)c_->cycle(), bd, hd);
    unit<units::TileCtl>("ctl").s.receive_config = v & units::RECEIVE_CONFIG_BITS;
    c_->touched(un(k_, "ctl"));
}
void TileRef::set_gather_config(uint32_t v) {
    unit<units::TileCtl>("ctl").s.gather_config = v & units::GATHER_CONFIG_BITS;
    c_->touched(un(k_, "ctl"));
}
void TileRef::dma_start() {
    if (unit<units::AddrGen>("ag").running)
        FATAL("mach: tile %u, cycle %lld: dma_start while a transfer is in progress: UB (8.2)", k_, (long long)c_->cycle());
    units::RegBlockState& s = unit<units::TileCtl>("ctl").s;
    s.dma_starts++;
    s.events &= ~(1u << units::EV_DMA_DONE);
    c_->touched(un(k_, "ctl"));
}
// COUNTER_n as at the end of the cycle just run: the unit's count with what it applies at its next step, for that
// cycle's edge, counted in (12.2: next = count + N - decrement): the decrement the delay line delivers then, and a
// core's store to the counter.
namespace {
uint32_t counter_at_end(Chip& c, const units::TileCtl& b, unsigned ctr) {
    const uint64_t next = uint64_t(c.cycle()) + 1;
    sim::PortEntry e[8];
    uint32_t n = 0;
    bool dec = false;
    uint32_t k = c.sim().observe(b.kline, e, 8).n;
    for (uint32_t i = 0; i < k; i++) if (e[i].at == next && uint32_t(e[i].w[0]) == ctr) dec = true;
    k = c.sim().observe(b.bus, e, 8).n;
    for (uint32_t i = 0; i < k; i++)
        if (e[i].at == next && uint32_t(e[i].w[0] >> 32) == units::BUS_STORE && uint32_t(e[i].w[0]) == units::COUNTER_1 + ctr - 1)
            n = uint32_t(e[i].w[1]) & units::COUNTER_MASK;
    const uint32_t count = b.s.remaining[ctr];
    return (count + n - (dec && (count || n) ? 1 : 0)) & units::COUNTER_MASK;
}
}  // namespace
uint32_t TileRef::remaining(unsigned ctr) const {
    if (ctr < 1 || ctr > 7) FATAL("mach: tile %u: counter %u (1-7)", k_, ctr);
    return counter_at_end(*c_, unit<units::TileCtl>("ctl"), ctr);
}
void TileRef::set_remaining(unsigned ctr, uint32_t v) {
    if (ctr < 1 || ctr > 7) FATAL("mach: tile %u: counter %u (1-7)", k_, ctr);
    unit<units::TileCtl>("ctl").s.remaining[ctr] = v & units::COUNTER_MASK;
    c_->touched(un(k_, "ctl"));
}
void TileRef::set_errors(uint32_t bits) {
    if (bits & ~((1u << units::ERR_PERIPH_1) - 1)) FATAL("mach: tile %u: set_errors(0x%x): the sticky bits [9:0] (a peripheral's are live)", k_, bits);
    unit<units::TileCtl>("ctl").s.errors |= bits;
    c_->touched(un(k_, "ctl"));
}
void TileRef::set_events(uint32_t bits) {
    if (bits & (units::EVENTS_LIVE | ~uint32_t(units::EVENTS_VALID))) FATAL("mach: tile %u: set_events(0x%x): the counted events (the others are live levels)", k_, bits);
    unit<units::TileCtl>("ctl").s.events |= bits;
    c_->touched(un(k_, "ctl"));
}

// ---- the request unit from a test ----
void TileRef::req_set_w(unsigned i, uint32_t v) {
    if (i > 2) FATAL("mach: req_set_w(%u): W0-W2", i);
    unit<units::TileCore>("core").req_out_w[i] = v;
}
uint32_t TileRef::req_w(unsigned i) const { return unit<units::TileCore>("core").req_out_w[i % 3]; }

// ---- the read unit ----
bool TileRef::rp_idle() const {                             // a return injected in a cycle frees its entry in the next
    const units::TileCore& core = unit<units::TileCore>("core");
    const units::InjReadSide& rs = unit<units::Injection>("inj").rs;
    const uint64_t freed = rs.rp_injected_n - (rs.rp_inj_t == uint64_t(c_->cycle()) ? rs.rp_inj_now : 0);
    return core.rp_stored == freed;
}
bool TileRef::rp_all_accepted() const {
    return unit<units::TileCore>("core").rp_stored == unit<units::Director>("dir").rp_accepted;
}
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
std::string Chip::s1_report() const {                     // ref's S1.report(K), summed over the tiles (units::s1_report)
    if (!sim::check::kOn) FATAL("mach: s1_report: S1 is measured in the debug and suite builds only");
    const units::S1Tile* t[64];
    for (unsigned k = 0; k < 64; k++) {
        const auto& s1 = const_cast<Chip*>(this)->unit<units::Director>(units::tile_unit(k, "dir")).s1;
        if (!s1) FATAL("mach: s1_report: tile %u has no S1", k);
        t[k] = s1.get();
    }
    return units::s1_report(t, 64, cfg_.K);
}
std::string Chip::ag_report() const {                       // ref's Chip.agReport
    uint64_t rot_late = 0, max_rot = 0, grp_late = 0, max_grp = 0, stalls = 0;
    int late_tiles = 0, max_drift = 0, drift_group = -1;
    for (unsigned k = 0; k < 64; k++) {
        const units::AddrGen& a = const_cast<Chip*>(this)->unit<units::AddrGen>(units::tile_unit(k, "ag"));
        rot_late += a.rotations_late; max_rot = std::max(max_rot, a.max_rot_late);
        grp_late += a.groups_late_prefetch; max_grp = std::max(max_grp, a.max_group_late);
        stalls += a.stall_cycles; if (a.rotations_late > 0) late_tiles++;
    }
    for (unsigned g = 0; g < units::AddrGen::MAXGROUPS; g++) {
        int64_t lo = INT64_MAX, hi = INT64_MIN;
        for (unsigned k = 0; k < 64; k++) {
            const int64_t v = const_cast<Chip*>(this)->unit<units::AddrGen>(units::tile_unit(k, "ag")).group_start[g];
            if (v < 0) continue;
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        if (hi >= lo && hi - lo > max_drift) { max_drift = int(hi - lo); drift_group = int(g); }
    }
    char b[400];
    std::snprintf(b, sizeof b, "    address generator: %llu request stall cycles; rotations late %llu on %d tiles (max %llu cycles); group starts "
                  "waiting for read-ahead %llu (max %llu, timetable); drift between tiles at a group start %d%s\n",
                  (unsigned long long)stalls, (unsigned long long)rot_late, late_tiles, (unsigned long long)max_rot, (unsigned long long)grp_late,
                  (unsigned long long)max_grp, max_drift, drift_group >= 0 ? (" (group " + std::to_string(drift_group) + ")").c_str() : "");
    return b;
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


void TileRef::req_send(int64_t t, uint32_t w3) {           // REQUEST_SEND as of the cycle just run
    if (t != c_->cycle()) FATAL("mach: tile %u: req_send at cycle %lld: the cycle just run only (%lld)", k_, (long long)t, (long long)c_->cycle());
    units::TileCore& core = unit<units::TileCore>("core");
    // appended to the request queue (the leaf's FIFO) at once, any number between cycles, as a push of t is there at
    // the start of t + 1; a push into a full queue is flagged (the fact) and dropped
    const std::string leaf = "t" + std::to_string(k_) + ".reqleaf";
    units::reqbus::ReqLeaf& l = c_->unit<units::reqbus::ReqLeaf>(leaf);
    if (l.n >= l.depth) {
        l.overflows++;
        sim::check::record(uint64_t(t), int(k_), sim::check::Kind::CTL_REQ_FULL);
    } else {
        units::reqbus::ReqLeaf::Entry& e = l.fifo[(l.head + l.n) % l.depth];
        e.w[0] = core.req_out_w[0]; e.w[1] = core.req_out_w[1]; e.w[2] = core.req_out_w[2]; e.w[3] = w3;
        e.store_t = t;
        l.n++; l.sends++;
        // the queue's two counts beside the leaf's, as a REQUEST_SEND store of t leaves them: the core's (its stall)
        // and the tile control unit's (STATUS.request_full); each falls when the tree takes the queue's head
        core.req_n++;
        units::RegBlockState& s = unit<units::TileCtl>("ctl").s;
        s.req_n++; s.sends++;
        c_->touched(un(k_, "ctl"));
    }
    c_->touched(leaf);
    core.req_out_w[0] = core.req_out_w[1] = core.req_out_w[2] = 0;
}
bool TileRef::req_can_push(int64_t t) const {               // fifo.size() < depth (the bus frees nothing before t runs)
    if (t != c_->cycle() + 1) FATAL("mach: tile %u: req_can_push(%lld): the next cycle only", k_, (long long)t);
    const units::reqbus::ReqLeaf& l = c_->unit<units::reqbus::ReqLeaf>("t" + std::to_string(k_) + ".reqleaf");
    sim::PortEntry e[4];
    const uint32_t n = c_->sim().observe(l.push, e, 4).n;    // a core's push of the cycle just run is in the FIFO for ref
    uint32_t pending = 0;
    for (uint32_t i = 0; i < n; i++) if (e[i].at <= uint64_t(t)) pending++;
    return l.n + pending < l.depth;
}
bool TileRef::req_idle() const {                            // fifo.isEmpty(), a core's push of the cycle just run included
    const std::string leaf = "t" + std::to_string(k_) + ".reqleaf";
    if (!c_->sim().has_unit(leaf)) FATAL("mach: tile %u: req_idle(): no request port on this machine", k_);
    const units::reqbus::ReqLeaf& l = c_->unit<units::reqbus::ReqLeaf>(leaf);
    sim::PortEntry e[4];
    return l.n == 0 && c_->sim().observe(l.push, e, 4).n == 0;
}


// ---- the write engine's request lines from a test ----
namespace {
void we_req(TileRef& p, Chip& c, unsigned k, unsigned line, int64_t t, uint32_t op, uint32_t idx, uint32_t add) {
    if (t != c.cycle() + 1) FATAL("mach: tile %u: a write-engine request at cycle %lld: the next cycle only", k, (long long)t);
    // the lines are the director's (12.4: it sends PTR's and HIST's requests); PTR's old pointer comes back to the
    // write engine, which drops it
    units::Director& d = p.unit<units::Director>("dir");
    if (d.host_req[line][0]) FATAL("mach: tile %u: a second write-engine request on one line in a cycle", k);
    d.host_req[line][0] = uint64_t(t); d.host_req[line][1] = op; d.host_req[line][2] = idx; d.host_req[line][3] = add;
    if (line == 0) p.unit<units::WriteEngine>("we").host_rets++;
    c.touched(units::tile_unit(k, "dir"));
}
}  // namespace
void TileRef::we_ts_req(int64_t t, uint32_t op, uint32_t idx, uint32_t add) { we_req(*this, *c_, k_, 0, t, op, idx, add); }
void TileRef::we_hist_req(int64_t t, uint32_t op, uint32_t idx, uint32_t add) { we_req(*this, *c_, k_, 1, t, op, idx, add); }




namespace {
bool port_empty(sim::Sim& s, const sim::HoldIn& p) { sim::PortEntry e[1]; const sim::PortView v = s.observe(p, e, 1); return v.n == 0 && !v.put; }
bool port_empty(sim::Sim& s, const sim::LinkIn& p) { sim::PortEntry e[8]; return s.observe(p, e, 8).n == 0; }   // nothing visible or in flight
}  // namespace
// mem_idle: the write engine's pipeline empty and nothing on its way into it or into the scratchpad (12.4, 15)
bool TileRef::mem_idle() const {
    sim::Sim& s = c_->sim();
    const units::WriteEngine& we = unit<units::WriteEngine>("we");
    if (we.count || !port_empty(s, we.in)) return false;
    const units::BigSram& m = unit<units::BigSram>("sram");
    return port_empty(s, m.wr);                          // a write is done in the cycle it is presented (15)
}
// ts_busy (4.15): either store's pipeline or request FIFOs busy; requests and returns on their way are in the ports
bool TileRef::ts_busy() const {
    const units::TableStore& t = unit<units::TableStore>("ts");
    const units::HistStore& h = unit<units::HistStore>("hist");
    return t.pipe.valid || t.pub || h.pipe.valid || h.pub;
}
namespace {
uint32_t link_after(sim::Sim& s, const sim::LinkIn& p, uint64_t c) {   // a Link's words arriving after c (still in flight)
    sim::PortEntry e[64];
    const uint32_t n = s.observe(p, e, 64).n;
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) if (e[i].at > c) k++;
    return k;
}
}  // namespace
bool TileRef::phase_done() const {
    if (unit<units::AddrGen>("ag").running) return false;
    for (unsigned k = 1; k <= 7; k++) if (remaining(k)) return false;
    sim::Sim& s = c_->sim();
    const uint64_t next = uint64_t(c_->cycle()) + 1;      // a word arriving at the next cycle landed at ref's end of this one
    if (link_after(s, unit<units::TileCtl>("ctl").kline, next)) return false;
    if (!mem_idle()) return false;
    if (link_after(s, unit<units::TileCore>("core").port_b, next)) return false;
    return !ts_busy();
}

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
// The drain (Cc::drain_reports), for cycle t = cycle() + 1 (between cycles: the receive pops at t)
void drain_next(Chip& c) {
    Chip::Impl& m = c.impl();
    if (!m.drain) return;
    const int64_t t = c.cycle() + 1;
    ReqBus& b = c.reqbus();
    uint32_t w[4];
    if (!b.peek_request(0, t, w)) return;
    b.receive(0, t);
    const std::array<int64_t, 5> e = {t, int64_t(w[0]), int64_t(w[1]), int64_t(w[2]), int64_t(w[3])};
    const unsigned op = w[0] >> units::REQ_OP_LO & units::REQ_OP_MASK;
    if (op == units::REQ_OP_REPORT) { m.reports.push_back(e); return; }
    m.requests.push_back(e);
}
void Cc::drain_reports(bool on) {
    Chip::Impl& m = c_->impl();
    if (on && !c_->cfg().reqbus_host) FATAL("mach: drain_reports: MachConfig::reqbus_host (target 0's queue)");
    m.drain = on;
    if (on) drain_next(*c_);                               // the next cycle's, as ref's CnC at its runCycle
}
// ref's lists as they stand between cycles: what C&C drained at or before the cycle just run (the harness drains for
// the next cycle ahead of it, so an entry of cycle() + 1 is left out until that cycle has run)
namespace {
template <size_t N> std::vector<std::array<int64_t, N>> drained(const std::vector<std::array<int64_t, N>>& v, int64_t now) {
    std::vector<std::array<int64_t, N>> r;
    for (const auto& e : v) if (e[0] <= now) r.push_back(e);
    return r;
}
}  // namespace
std::vector<std::array<int64_t, 5>> Cc::reports() const { return drained(c_->impl_const().reports, c_->cycle()); }
std::vector<std::array<int64_t, 5>> Cc::requests() const { return drained(c_->impl_const().requests, c_->cycle()); }
std::vector<std::array<int64_t, 5>> Cc::reports_of(unsigned id) const {
    std::vector<std::array<int64_t, 5>> r;
    for (const auto& e : c_->impl_const().reports) if ((e[1] & 0x7F) == int64_t(id) && e[0] <= c_->cycle()) r.push_back(e);
    return r;
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
std::vector<std::array<int64_t, 2>> Cc::phase_log() const {   // ref's CnC.phaseLog: {all READY seen, GO} per GO
    const Chip::Impl& m = c_->impl_const();
    const units::Cnc& cn = const_cast<Chip*>(c_)->unit<units::Cnc>("cnc");
    return std::vector<std::array<int64_t, 2>>(m.plog.begin(), m.plog.begin() + cn.plog_n);
}
size_t Cc::phases_logged() const { return const_cast<Chip*>(c_)->unit<units::Cnc>("cnc").plog_n; }
std::vector<std::array<uint64_t, 4>> Cc::received() const { return c_->received(0); }


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
void A2A::load(Chip& c, int gap) const {
    for (unsigned s = 0; s < 64; s++) {
        TileRef p = c.tile(s);
        for (size_t k = 0; k < rec[s].size(); k++) p.sram_poke64(SRC + 2 * uint32_t(k), uint64_t(rec[s][k]));
        for (unsigned g = 0; g < 16; g++) {
            for (unsigned i = 0; i < 4; i++) {
                const unsigned off = unsigned(G4[g][i]), d = (s + off) & 63;
                p.set_stream(4 * g + i, fields::stream_src(SRC + 2 * uint32_t(first(s, d))), 0,
                             fields::stream_hdr(uint32_t(cnt[s][d]), fields::SIZE_64, 1, units::lw::M_SRC, off));
            }
            p.set_group(g, fields::group_w0(uint32_t(R[g]), 4 * g, 4), fields::group_w1(uint32_t(gap)));
        }
        for (unsigned src = 0; src < 64; src++) p.set_ptr(src, INBOX + 2 * uint32_t(dstoff[src][s]));
        p.set_remaining(1, uint32_t(expected(s)));
        p.set_dma_config(fields::dma_config(fields::RUN_GROUPS, 0, 16));
        p.set_receive_config(fields::receive_config(0, 0));
        p.set_gather_config(fields::gather_config(0, 1));
    }
}
int64_t A2A::run(Chip& c, int64_t max) {
    const int64_t t0 = c.cycle();
    const std::vector<int> skew = c.start_skew();
    bool started[64] = {};
    int64_t end = -1;
    for (int64_t i = 0; i < max; i++) {
        for (unsigned k = 0; k < 64; k++) if (!started[k] && c.cycle() >= t0 + skew[k]) { c.tile(k).dma_start(); started[k] = true; }
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
    if (c.special_tiles) FATAL("Net: the coreless machine has C&C 1's Java model (no cc_cores)");
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
    k.shuffle_seed = shuffle; k.check_errors = false;
    return k;
}
Net::Net(const MachConfig& cfg) : c(cfg) { c.impl().enable_net(cfg); }   // any machine, as ref's (Config.machine())
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
    units::CncPacket p{};                                   // ref's Endpoint.send checks it (the facts)
    p.dst = uint8_t(dst); p.mode = uint8_t(mode); p.counter = uint8_t(counter); p.size = uint8_t(size); p.addr = addr; p.lo = lo; p.hi = hi;
    // ref counts it at the send, between cycles, and its checkInvariants reports it at the next cycle run
    units::endpoint_checks(uint64_t(c.cycle()) + 1, lw::CC1 + cc, p);
    if (cc == 0) c.sim().put_late("cnc", c.unit<units::Cnc>("cnc").send, w, 3);
    else {
        const std::string n = "ep" + std::to_string(cc + 1);
        c.sim().put_late(n, c.unit<units::Endpoint>(n).send, w, 3);
    }
}
std::vector<std::array<uint64_t, 4>> Net::received(unsigned cc) const { return c.received(cc); }
std::vector<std::array<uint64_t, 4>> Chip::received(unsigned cc) const {
    if (cfg_.special_tiles || cc > 2) FATAL("mach: received(%u): C&C 1-3's endpoints of the model (no cc_cores)", cc);
    const Impl& m = *impl_;
    if (m.sends[cc].lost) FATAL("mach: C&C %u's receive trace overflowed (%u kept, %llu lost): raise MachConfig::log_capacity",
                                cc + 1, m.sends[cc].log_cap, (unsigned long long)m.sends[cc].lost);
    return std::vector<std::array<uint64_t, 4>>(m.recv[cc].begin(), m.recv[cc].begin() + m.sends[cc].log_n);
}
int64_t Net::late() const { int64_t n = 0; for (const auto& f : const_cast<Chip&>(c).impl().feeds) n += int64_t(f.late); return n; }
bool Net::pending() const {
    const Chip::Impl& m = c.impl_const();
    for (const auto& f : m.feeds) if (f.pending()) return true;
    for (const auto& h : m.sends) if (h.n) return true;
    return false;
}
// ref's Net.step: feed() then the cycle. Only here (and in run_all) are Net's schedules fed: Chip::run and step leave
// them alone, as ref's Chip.run does. The units with something to feed are woken for this cycle.
void Net::step() {
    Chip::Impl& m = c.impl();
    for (unsigned k = 0; k < 64; k++) {
        m.feeds[k].on = true;
        if (m.feeds[k].pending()) m.sim->wake(units::tile_unit(k, "inj"));
    }
    // C&C 1's cncSends, as ref's feed(): between cycles, at the cycle just run, the head when T <= t + 1 and C&C's send
    // holding register is empty then (a put as of t: Sim::put_late, delivered at t + 1 as ref's)
    units::HarnessSends& h = m.sends[0];
    if (h.n && h.e[h.head].T <= c.cycle() + 1) {
        sim::PortEntry e[1];
        const sim::PortView v = m.sim->observe(c.unit<units::Router>(units::tile_unit(0, "router")).from_cnc, e, 1);
        if (!v.valid && !v.put && v.n == 0) {
            namespace lw = units::lw;
            const units::CncPacket& p = h.pop();
            units::endpoint_checks(uint64_t(c.cycle()) + 1, lw::CC1, p);   // ref's Endpoint.send checks it, reported at the next cycle
            const sim::Word w[3] = {lw::mask_lo(p.lo, p.size), lw::hdr(lw::CC1, p.dst, p.mode, p.counter, p.addr, p.size), lw::mask_hi(p.hi, p.size)};
            m.sim->put_late("cnc", c.unit<units::Cnc>("cnc").send, w, 3);
        }
    }
    c.step();
    for (auto& f : m.feeds) f.on = false;
}
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

// ================================================================================================= Mach
namespace mach {

MachConfig Mach::cfg(int64_t shuffle) { MachConfig k = MachConfig::machine(); k.shuffle_seed = shuffle; return k; }
MachConfig Mach::chip_cfg(int64_t shuffle) { MachConfig k = MachConfig::chip(); k.shuffle_seed = shuffle; return k; }

Mach::Mach(const MachConfig& cfg) : c(cfg) {}

void Mach::place(unsigned unit, uint32_t byte_addr, const std::vector<uint32_t>& words) {
    Chip::Impl& m = c.impl();
    const uint32_t region = units::region(byte_addr), off = units::offset(byte_addr);
    if (unit >= m.nunits) FATAL("mach: place: unit %u of %u", unit, m.nunits);
    if ((region != units::R_DMEM && region != units::R_IMEM) || (byte_addr & 3) || off / 4 + words.size() > units::MEM_WORDS)
        FATAL("mach: place: %zu words at 0x%08x of unit %u: not whole words inside IMEM or DMEM", words.size(), byte_addr, unit);
    uint32_t* mem = (region == units::R_IMEM ? m.imem(unit) : m.dmem(unit)) + off / 4;
    for (size_t i = 0; i < words.size(); i++) mem[i] = words[i];
    c.touched(units::unit_prefix(unit) + "core");
}
void Mach::place_elf(unsigned unit, const std::string& path) {
    const rtl_images::MemoryImages im = rtl_images::memory_images(path, units::IMEM_BASE, 0, units::MEM_BYTES);
    place(unit, units::IMEM_BASE, im.imem);
    place(unit, 0, im.dmem);
}
uint32_t Mach::word(unsigned unit, uint32_t a) const { return const_cast<Chip&>(c).tile(unit).peek_local(a); }

// ---- v1.37's chip
units::IngressEgress& Mach::ingress() {
    if (!c.cfg().host_link) FATAL("mach: no host link on this machine (MachConfig::host_link)");
    return c.unit<units::PeriphSlot>(units::unit_prefix(units::ID_MASTER) + "slot").s.ingress_egress(1);
}
units::Updater& Mach::updater() {
    if (!c.cfg().host_link) FATAL("mach: no Updater on this machine (MachConfig::host_link)");
    return c.unit<units::PeriphSlot>(units::unit_prefix(units::ID_MASTER) + "slot").s.updater(2);
}
units::tracer::FileStore& Mach::store() { return *updater().store; }
void Mach::set_link(std::vector<uint8_t> bytes) {
    Chip::Impl& m = c.impl();
    m.link_bytes = std::move(bytes);
    ingress().set_link(m.link_bytes.data(), m.link_bytes.size());
    c.touched(units::unit_prefix(units::ID_MASTER) + "slot");
}

}  // namespace mach
