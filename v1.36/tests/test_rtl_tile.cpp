// test_rtl_tile.cpp: acceptance A of v1.36's platform: the RTL's tile programs (Snarky One's
// rtl/tile/base/test/programs/base_tile, vendored unmodified as ELF files in fw/rtl/tile/base_tile/) on one simulated compute tile,
// booted through the RTL's boot ROM (fw/rtl/boot/), with the stimulus and the verdicts of the RTL's own test of its tile,
// rtl/tile/base/test/src/base_tile_test.rs. The bench here plays what that test plays: the master tile (the load, the
// barrier's release), the request tree's row node that takes the tile's reports, and the tiles at the ends of the router's
// links.
//
// The expectations are the RTL test's, which come from the base tile specification (machine-spec, the same numbers): the
// map (5.1), the packet (4.1), a request's word 0 (11.2, 11.5: the requester and target 0 as software leaves them, op
// REPORT 1 at [17:10] and the number at [25:18], or op LOAD 3 and words 1-3 0), PARAMS (7.1) from the tile's straps, the
// routes of 16.2 (`first_hop`), the boot ROM's reports 24 and 25 (6.5), the errors' bits (7.4) and the causes (6.2); and
// each program's results as its header gives them. None comes from the simulator.
//
// A run (base_tile_test.rs's): the tile's reset at a tile id (here: a new machine); the boot (6.5): the boot ROM's LOAD
// request, then the load on one of the router's inputs, at random gaps: the program's segments as uncounted 128-bit
// WRITEs from the master tile, a row each, then the arguments' two rows likewise (argument 0 the kind, 1-7 the run's),
// then a COUNT on counter 7; BARRIER_ARRIVE, which may pulse only once the COUNT has been driven; the release, after a
// random wait. Then the program's part, and its reports (each run's function says which). A program that passes ends by
// jumping to the reset PC, and the boot ROM sends its next LOAD request, which the test awaits; then quiet cycles with
// no request and no packet out.
//
// Every cycle: no packet leaves the tile but those a run expects; BARRIER_ARRIVE only as above; the error line low
// until an errors run's error and then high to its end.
//
// What differs from the RTL's test, and why:
//   - The RTL's model with attach ports has all three at every tile id (it is built as tile 0 at 1 x 1). A simulated
//     tile has the attach port its place in the chip gives it (2.1: tile 0 the master tile's, 7 the memory tile's, 15
//     the tracer tile's), and PARAMS.attach says so: the runs at an attach tile use its one attach port, and a run
//     whose input the RTL test picks among seven picks among the tile's.
//   - The RTL's request link is 16-bit beats with ready and valid; the simulator's request unit hands over whole
//     requests. The bench takes a request's eight beats one a cycle, at the RTL test's random ready, and frees the
//     queue's entry at the last, so the request queue fills and stalls the core as there.
//   - The RTL's handshake monitors have no counterpart: the framework's ports have no ready or valid to watch.
//   - The RTL's test takes the replayer's rows from the row port at a random ready; the simulator's row port is the
//     replayer's sink, always ready, and the rows are read from its buffer after the pass.
//
// The boot ROM's cycles. The RTL states them on its block `core` (Snarky One's rtl/blocks/core/README.md, "The boot
// ROM's program", lines 204-208: boot_rom_program_test's figures, cycle 0 the first after the reset, each ending at the
// cycle its store is raised in, the cycle its instruction leaves execute), and the resume after a wake in its core
// specification (rtl/blocks/core/spec.md, "Resume timing", lines 608-611). Here the core, the register bus, the tile
// control unit and the request unit are the simulated tile's, so every run checks them on it (`Run::probe`):
//   - from the reset to the LOAD request's REQUEST_SEND, 26; with boot_local, to the image's first instruction, 27;
//   - from the release (the cycle of BARRIER_RELEASE's pulse, at whose end the tile sets the event) to the image's
//     first instruction, 13;
//   - the wake: MEIP is high from R, the cycle after the release; the instruction after the WFI moves into execute
//     at the end of R + 1 and, nothing stalling it, leaves it in R + 2 and retires in R + 3. The ROM's is a load of
//     EVENTS, a register window's: raised in R + 2, answered and retired two cycles later, in R + 4 (6.4).
// PENDING-v1.36.md, section 1, lists what of the RTL's test is not here (the special tile's program).
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/fatal.h"
#include "harness.h"
#include "rtl_images.h"
#include "units/chip.h"
#include "units/net.h"
#include "units/periph_slot.h"
#include "units/regmap.h"
#include "units/tile_ctl.h"

using namespace units;

namespace {

// ---- The specification's numbers, written here (never read from the simulator's headers) ----
constexpr uint32_t kMeshSize = 8, kTileCount = 64, kMasterTile = kTileCount;
// The reports' numbers (tile_map.h; the boot ROM's, 6.5).
constexpr uint32_t REPORT_PASS = 0, REPORT_FAIL = 1, REPORT_WAIT = 2, REPORT_HANDLER_FIRST = 24, REPORT_HANDLER_SECOND = 25;
constexpr uint32_t OP_REPORT = 1, OP_LOAD = 3;
// The errors program's errors, by their bit in ERRORS (7.4).
constexpr uint32_t ERROR_ROUTER_OVERFLOW = 0, ERROR_ACCESS_FAULT = 7, ERROR_ILLEGAL_INSTRUCTION = 8, ERROR_BITS = 11;
// The causes (6.2; the core specification).
constexpr uint32_t CAUSE_NMI = 0x80000000, CAUSE_ILLEGAL_INSTRUCTION = 2, CAUSE_LOAD_ACCESS_FAULT = 5;
constexpr uint32_t LOAD_COUNTER = 7;                       // the load's counter, the boot ROM's (6.5)
// The packet's modes and sizes (4.1, 4.2).
constexpr uint32_t MODE_WRITE = 0, MODE_COUNT = 5, SIZE_32 = 0, SIZE_128 = 2;
// The programs' kinds (programs.txt; tile_programs.rs).
constexpr uint32_t KIND_REGISTERS = 0, KIND_NOC = 1, KIND_BARRIER = 2, KIND_ERRORS = 3, KIND_REPLAYER = 5, KIND_COUNT = 6;   // (4: special)
constexpr unsigned ROW_LONGS = 10;                         // a replayer row's 640 bits, as 64-bit words (17.8.11)
// The arguments (tile_map.h): DMEM's 8 words past its reserved 16 bytes.
constexpr uint32_t ARGUMENTS = 16;
// The router's ports, as inputs and outputs: the links, then the attach ports of the master, memory and tracer tiles.
constexpr unsigned WEST = 0, EAST = 1, NORTH = 2, SOUTH = 3, ATTACH = 4, PORTS = 7;
const char* const PORT_NAMES[PORTS] = {"west", "east", "north", "south", "master", "memory", "tracer"};
constexpr unsigned REQUEST_BEATS = 8;                      // a request's beats on the request link (11.3)
// The test's own bounds and waits (base_tile_test.rs's).
constexpr uint32_t BOOT_TIMEOUT = 20000, REPORT_TIMEOUT = 100000, QUIET_CYCLES = 300, RELEASE_WAIT_MOST = 40, FLOOD_CYCLES = 64;
constexpr uint64_t ARRIVAL_DELAY_LEAST = 2000;             // the barrier program's delay loop (barrier.S)
// The boot ROM's cycles (the header): the RTL's figures, and the words they end at (5.5).
constexpr uint64_t RESET_TO_LOAD_REQUEST = 26, RESET_TO_IMAGE_BOOT_LOCAL = 27, RELEASE_TO_IMAGE = 13;
constexpr uint64_t WAKE_TO_EXECUTE_END = 2, WAKE_TO_RETIRE = 3, WINDOW_LOAD_CYCLES = 2;
constexpr uint32_t ADDRESS_EVENTS = 0xFFFFF80C, ADDRESS_REQUEST_SEND = 0xFFFFF90C, ADDRESS_IMEM = 0x01000000;

// The attach tile of special tile `special` (2.1).
uint32_t attach_tile(uint32_t special) { const uint32_t t[3] = {0, kMeshSize - 1, (2 * kMeshSize - 1) % kTileCount}; return t[special]; }
// The special tile attached at compute tile `tile`, or -1.
int attached_at(uint32_t tile) { for (uint32_t s = 0; s < 3; s++) if (attach_tile(s) == tile) return int(s); return -1; }

// The output a unicast from compute tile `me` to `dst` leaves by, by the specification's route (16.2): a special tile
// routed as its attach tile, which sends it by its attach port; otherwise the offset (a, b), M a + b = dst - me modulo
// the tile count, each within half a side and fewest in all, a column move of exactly half a side going north when
// b > 0 and south otherwise; |b| hops east (b > 0) or west first, then |a| south (a > 0) or north. -1 for dst = me.
int first_hop(uint32_t me, uint32_t dst) {
    uint32_t target = dst;
    if (dst >= kTileCount) {
        const uint32_t special = dst - kTileCount;
        if (attach_tile(special) == me) return int(ATTACH + special);
        target = attach_tile(special);
    }
    if (target == me) return -1;
    const int side = int(kMeshSize), count = int(kTileCount), half = side / 2;
    const int offset = ((int(target) - int(me)) % count + count) % count;
    int best_cost = 1 << 30, best_a = 0, best_b = 0;
    bool tie = false;
    for (int a = -half; a <= half; a++)
        for (int b = -half; b <= half; b++) {
            if ((((side * a + b - offset) % count) + count) % count != 0) continue;
            const int cost = (a < 0 ? -a : a) + (b < 0 ? -b : b);
            if (cost < best_cost) { best_cost = cost; best_a = a; best_b = b; tie = false; }
            else if (cost == best_cost) {
                // Only a column move of exactly half a side ties: north (a < 0) when b > 0, south otherwise.
                if (b != best_b || (a != half && a != -half)) FATAL("first_hop: an unexpected tie at (%d, %d)", a, b);
                tie = true;
                const bool want_north = b > 0;
                if ((a < 0) == want_north) { best_a = a; best_b = b; }
            }
        }
    (void)tie;
    return best_b > 0 ? int(EAST) : best_b < 0 ? int(WEST) : best_a > 0 ? int(SOUTH) : int(NORTH);
}

// A packet's defined fields (4.1).
struct Packet {
    uint32_t dst = 0, src = 0, mode = 0, counter = 0, size = 0, address = 0;
    uint64_t lo = 0, hi = 0;                               // the payload
    bool operator==(const Packet& o) const {
        return dst == o.dst && src == o.src && mode == o.mode && counter == o.counter && size == o.size && address == o.address
               && lo == o.lo && hi == o.hi;
    }
};

// ---- The bench's unit: the tile's surroundings, cycle by cycle. The test sets a cycle's inputs before it runs and
// reads what the unit saw in it afterwards; the unit itself keeps only the request link's state. ----
struct Bench {
    enum : unsigned { kIn = 5 };                           // west, east, north, south, the tile's attach port
    sim::LinkOut in[kIn];                                  // into the router's inputs
    sim::LinkIn out[kIn];                                  // out of its outputs
    sim::LinkOut release, freed_ev;
    sim::WireOut freed;
    sim::LinkIn line, request;
    bool attach = false;
    // ---- this cycle's inputs (the test's) ----
    bool drive[kIn] = {};
    uint64_t drive_w[kIn][3] = {};
    bool release_now = false, request_ready = false;
    // ---- what this cycle showed ----
    bool out_valid[kIn] = {};
    uint64_t out_w[kIn][3] = {};
    bool line_valid = false; uint64_t line_word = 0;
    bool request_done = false, request_stalled = false;
    uint32_t request_w[4] = {};
    // ---- the request link: the tile's request queue's entries on their way, the head's beats taken ----
    uint32_t rq[8][4] = {};
    unsigned rq_head = 0, rq_n = 0, beats = 0;
    bool freed_on = false;

    Bench(sim::Ports& p, bool attach_) : attach(attach_) {
        static const char* kNames[kIn] = {"west", "east", "north", "south", "attach"};
        for (unsigned i = 0; i < (attach ? kIn : 4u); i++) { p.out(in[i], (std::string("in-") + kNames[i]).c_str(), 3); p.in(out[i], (std::string("out-") + kNames[i]).c_str(), 3); }
        p.out(release, "release", 0); p.out(freed_ev, "freed_ev", 0); p.out(freed, "freed");
        p.in(line, "line", 1); p.in(request, "request", 2);
    }
    sim::Wake step(sim::Ctx& c) {
        const unsigned n = attach ? kIn : 4u;
        for (unsigned i = 0; i < n; i++) {
            if (drive[i]) c.put(in[i], drive_w[i][0], drive_w[i][1], drive_w[i][2]);
            out_valid[i] = c.ready(out[i]);
            if (out_valid[i]) { const sim::Word* w = c.peek(out[i]); out_w[i][0] = w[0]; out_w[i][1] = w[1]; out_w[i][2] = w[2]; c.take(out[i]); }
        }
        if (release_now) c.put(release);
        line_valid = c.ready(line);
        if (line_valid) { line_word = c.peek(line)[0]; c.take(line); }
        if (freed_on) { c.drive(freed, 0); freed_on = false; }
        if (c.ready(request)) {                            // a REQUEST_SEND's request, into the tile's queue
            const sim::Word* w = c.peek(request);
            if (rq_n == 8) FATAL("bench: the tile's request queue holds more than its depth");
            uint32_t* e = rq[(rq_head + rq_n++) % 8];
            e[0] = uint32_t(w[0]); e[1] = uint32_t(w[0] >> 32); e[2] = uint32_t(w[1]); e[3] = uint32_t(w[1] >> 32);
            c.take(request);
        }
        request_done = false; request_stalled = false;
        if (rq_n) {                                        // the head's beats, one a cycle while the link is ready
            if (!request_ready) request_stalled = true;
            else if (++beats == REQUEST_BEATS) {
                beats = 0; request_done = true;
                for (unsigned i = 0; i < 4; i++) request_w[i] = rq[rq_head][i];
                rq_head = (rq_head + 1) % 8; rq_n--;
                c.drive(freed, 1); freed_on = true; c.put(freed_ev);
            }
        }
        return c.next();
    }
    uint64_t digest() const { return rq_n * 8 + beats; }
};

// The tile's ports of the simulator's router, by the RTL's port: inputs by arrival (from the west: FROM_LEFT, ...),
// outputs by direction (to the west: TO_LEFT, ...).
constexpr unsigned kLinkIn[4] = {0, 1, 2, 3};              // west FROM_LEFT, east FROM_RIGHT, north FROM_UP, south FROM_DOWN
constexpr unsigned kLinkOut[4] = {1, 0, 3, 2};             // west TO_LEFT, east TO_RIGHT, north TO_UP, south TO_DOWN

// One compute tile at `tile_id`, with the bench around it.
struct Machine {
    std::vector<uint32_t> mem;
    SramBlocks sram;
    Hops hops;
    rtl_images::BootRom rom;
    G1Ctx g;
    ChipSpec spec;
    sim::Sim m;
    // `placed`: null, or the image placed in the tile's IMEM and DMEM before the reset ends, with the strap boot_local 1
    // (6.5).
    Machine(uint32_t tile_id, const Rv32Timing* timing, const std::vector<rtl_images::Row>* placed = nullptr)
        : mem(size_t(64) * 2 * MEM_WORDS, 0), m(build(tile_id, timing, placed)) {}
    static Straps local_straps(unsigned k, const G1Config& cfg) {
        Straps s = default_straps(k, cfg.periph_present);
        s.boot_local = true;
        return s;
    }
    sim::Sim build(uint32_t tile_id, const Rv32Timing* timing, const std::vector<rtl_images::Row>* placed) {
        if (placed) {
            g.straps = local_straps;
            for (const rtl_images::Row& r : *placed) {
                const uint32_t region = r.address >> 24, word = (r.address & 0xFFFFFF) >> 2;
                if (region > 1 || word + 4 > MEM_WORDS) FATAL("a placed row at 0x%x: neither DMEM nor IMEM", r.address);
                uint32_t* at = mem.data() + size_t(tile_id) * 2 * MEM_WORDS + (region == 1 ? size_t(MEM_WORDS) : size_t(0)) + word;
                at[0] = uint32_t(r.lo); at[1] = uint32_t(r.lo >> 32); at[2] = uint32_t(r.hi); at[3] = uint32_t(r.hi >> 32);
            }
        }
        hops = load_hops("data/hops.txt");
        rom = rtl_images::boot_rom();
        g.bootrom = rom.words.data(); g.bootrom_words = uint32_t(rom.words.size()); g.bootrom_handler = rom.handler;
        g.mem = mem.data(); g.sram = &sram;
        g.cfg.periph_slot = true;                          // peripheral 1: the replayer (2.2)
        if (timing) g.cfg.timing = *timing;
        spec.hops = &hops; spec.control = add_g1; spec.control_ctx = &g;
        spec.cc_cores = true;                              // the attach port as a link, not v1.35's endpoint model
        sim::Config cfg;
        const bool attach = attached_at(tile_id) >= 0;
        TileIO io = tile_cycle(cfg, spec, tile_id);
        if (io.attached != attach) FATAL("the simulator's tile %u: attach port %d, the specification's %d (2.1)", tile_id, int(io.attached), int(attach));
        auto& b = cfg.add<Bench>("bench", attach);
        for (unsigned i = 0; i < 4; i++) { cfg.link(b.in[i], io.link_in[kLinkIn[i]], 1); cfg.link(io.link_out[kLinkOut[i]], b.out[i], 1); }
        if (attach) { cfg.link(b.in[4], io.from_cc, 1); cfg.link(io.to_cc, b.out[4], 1); }
        cfg.link(b.release, io.release, 1);
        cfg.link(io.line, b.line, 1);
        cfg.link(io.req.push, b.request, 1);
        cfg.connect(b.freed, io.req.freed);
        cfg.link(b.freed_ev, io.req.freed_ev, 1);
        return cfg.build();
    }
};

using Report = std::pair<uint32_t, std::array<uint32_t, 3>>;

struct Counts {
    uint32_t runs[KIND_COUNT] = {}, inputs_driven[PORTS] = {}, outputs_seen[PORTS] = {}, reports = 0, loads = 0, request_stalls = 0,
             errors_seen[ERROR_BITS] = {}, rows = 0;
};

// The bench (base_tile_test.rs's Bench).
struct Run {
    std::unique_ptr<Machine> mach;
    Bench* bench = nullptr;
    test::Rng random;
    std::vector<std::string> files;
    const Rv32Timing* timing = nullptr;                    // the core's timing knobs, when a test sets them
    uint64_t cycle = 0;
    uint64_t cycle_of_reset = 0;                           // the first cycle after the machine's reset
    uint32_t tile_id = 0;
    std::vector<unsigned> inputs;                          // the inputs the tile has: the links, and its attach port
    std::deque<Packet> queued[PORTS];
    bool flood = false;
    bool reachable[PORTS] = {};
    std::vector<std::pair<unsigned, Packet>> expected_out;
    std::deque<Report> reports;
    uint32_t loads = 0;
    bool release = false, rom_release = false;             // the release to drive; it is the boot ROM's wait's
    bool arrive_allowed = false;
    uint32_t arrivals = 0;
    uint64_t arrived_at = 0;
    bool error_allowed = false, error_risen = false, error_line = false;
    Counts counts;
    // ---- The boot ROM's cycles (the header): the core's data port and stages, read after each cycle ----
    TileCore* core = nullptr;
    bool boot_local = false;                               // the machine's strap
    bool was_outstanding = false;                          // the data port held an access in the cycle before
    bool was_image = false, request_sent = false, image_run = false, events_load_seen = false;
    uint32_t last_address = 0; uint64_t last_make_at = 0;
    int64_t released_at = -1;                              // the boot ROM's release: BARRIER_RELEASE's pulse; -1 none awaited
    uint64_t retired_at_release = 0;                       // the instructions retired by the release's cycle
    uint32_t figures[4] = {};                              // how many times each figure was checked
    bool dead = false;                                     // a check failed: the run stops

    explicit Run(uint64_t seed) : random(seed), files(rtl_images::program_list("fw/rtl/tile/base_tile", KIND_COUNT)) {}

    void fail(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (dead) return;
        dead = true;
        char text[512];
        va_list ap; va_start(ap, fmt); std::vsnprintf(text, sizeof text, fmt, ap); va_end(ap);
        test::fail(__FILE__, __LINE__, "cycle %llu, tile %u: %s", (unsigned long long)cycle, tile_id, text);
    }
    // A port of the RTL's seven as the bench's index, or -1 when the tile has no such port.
    int bench_port(unsigned port) const {
        if (port < ATTACH) return int(port);
        return attached_at(tile_id) == int(port - ATTACH) ? 4 : -1;
    }
    static void encode(const Packet& p, uint64_t w[3]) {
        w[0] = p.lo; w[1] = lw::hdr(p.src, p.dst, p.mode, p.counter, p.address, p.size); w[2] = p.hi;
    }
    static Packet decode(const uint64_t w[3]) {
        Packet p;
        p.lo = w[0]; p.hi = w[2];
        p.src = lw::src(w[1]); p.dst = lw::dst(w[1]); p.mode = lw::mode(w[1]); p.counter = lw::counter(w[1]);
        p.address = lw::addr(w[1]); p.size = lw::size(w[1]);
        return p;
    }

    // The tile's reset at `tile`, its inputs idle: a new machine.
    void reset(uint32_t tile, const std::vector<rtl_images::Row>* placed = nullptr) {
        if (dead) return;
        tile_id = tile;
        for (auto& q : queued) q.clear();
        flood = false; expected_out.clear(); reports.clear(); loads = 0;
        arrive_allowed = false; arrivals = 0; error_allowed = false; error_risen = false; error_line = false;
        mach = std::make_unique<Machine>(tile, timing, placed);
        core = &mach->m.unit<TileCore>(unit_prefix(tile) + "core");
        boot_local = placed != nullptr;
        cycle_of_reset = cycle; was_outstanding = was_image = request_sent = image_run = false; released_at = -1;
        bench = &mach->m.unit<Bench>("bench");
        inputs = {WEST, EAST, NORTH, SOUTH};
        if (attached_at(tile) >= 0) inputs.push_back(ATTACH + unsigned(attached_at(tile)));
    }

    // One cycle: the inputs driven, the outputs checked and taken.
    void step() {
        if (dead) return;
        Bench& b = *bench;
        // ---- Inputs ----
        for (unsigned i = 0; i < Bench::kIn; i++) b.drive[i] = false;
        for (unsigned port : inputs) {
            const bool drive = !queued[port].empty() && (flood || random.below(2) == 0);
            if (!drive) continue;
            const int i = bench_port(port);
            encode(queued[port].front(), b.drive_w[i]); b.drive[i] = true;
            queued[port].pop_front();
            counts.inputs_driven[port]++;
        }
        b.request_ready = random.below(3) != 0;
        b.release_now = release;
        release = false;
        mach->m.run(1);
        probe(b.release_now);
        // ---- The request link: a report or a LOAD at a request's last beat ----
        if (b.request_stalled) counts.request_stalls++;
        if (b.request_done) {
            const uint32_t* words = b.request_w;
            if ((words[0] & 0x3FF) != 0) { fail("a request's requester and target, as software wrote them: 0x%x", words[0]); return; }
            const uint32_t op = words[0] >> 10 & 0xFF;
            if (op == OP_REPORT) {
                if (words[0] >> 26 != 0) { fail("a report's word 0: 0x%x", words[0]); return; }
                reports.push_back({words[0] >> 18 & 0xFF, {words[1], words[2], words[3]}});
                counts.reports++;
            } else if (op == OP_LOAD) {
                if (words[0] != OP_LOAD << 10 || words[1] || words[2] || words[3]) { fail("a LOAD request: %x %x %x %x", words[0], words[1], words[2], words[3]); return; }
                loads++; counts.loads++;
            } else { fail("a request's op %u: %x %x %x %x", op, words[0], words[1], words[2], words[3]); return; }
        }
        // ---- The outputs: each packet one the run expects ----
        for (unsigned port = 0; port < PORTS; port++) {
            const int i = bench_port(port);
            if (i < 0 || !b.out_valid[i]) continue;
            const Packet packet = decode(b.out_w[i]);
            size_t at = 0;
            while (at < expected_out.size() && !(expected_out[at].first == port && expected_out[at].second == packet)) at++;
            if (at == expected_out.size()) {
                fail("an unexpected packet out of %s: dst %u src %u mode %u counter %u size %u address 0x%x payload 0x%llx",
                     PORT_NAMES[port], packet.dst, packet.src, packet.mode, packet.counter, packet.size, packet.address,
                     (unsigned long long)packet.lo);
                return;
            }
            expected_out.erase(expected_out.begin() + long(at));
            counts.outputs_seen[port]++;
        }
        // ---- The nets ----
        if (b.line_valid) {
            if (b.line_word & 1) {
                if (!arrive_allowed) { fail("BARRIER_ARRIVE before the load's COUNT"); return; }
                arrivals++; arrived_at = cycle;
            }
            error_line = (b.line_word >> 1 & 1) != 0;
        }
        if (error_risen && !error_line) { fail("the error line fell"); return; }
        if (error_line && !error_allowed) { fail("the error line rose"); return; }
        error_risen = error_line;
        cycle++;
    }
    // The boot ROM's cycles (the header), after the cycle just run. `since` counts from the reset, 0 its first cycle.
    void probe(bool released) {
        if (dead) return;
        const Rv32& rv = core->rv;
        const int64_t since = int64_t(cycle - cycle_of_reset);
        // The data port's access was raised in this cycle: one outstanding now that was not in the cycle before (the
        // next may be raised as the last is answered: another address, or another cycle to be made in).
        const bool raised = rv.lsu_out && (!was_outstanding || rv.lsu_addr != last_address || rv.lsu_make_at != last_make_at);
        was_outstanding = rv.lsu_out; last_address = rv.lsu_addr; last_make_at = rv.lsu_make_at;
        // The image's first instruction left execute in this cycle: it is in writeback, where it was not before.
        const bool image = rv.w_valid && rv.w_pc == ADDRESS_IMEM;
        const bool image_entered = image && !was_image;
        was_image = image;
        if (raised && rv.lsu_store && rv.lsu_addr == ADDRESS_REQUEST_SEND && !request_sent) {
            request_sent = true;
            if (!boot_local && !image_run) {                      // the boot ROM's LOAD request, the first after the reset
                if (uint64_t(since) != RESET_TO_LOAD_REQUEST) { fail("the LOAD request's REQUEST_SEND is raised %lld cycles after the reset, the RTL's %llu", (long long)since, (unsigned long long)RESET_TO_LOAD_REQUEST); return; }
                figures[0]++;
            }
        }
        if (image_entered && !image_run) {
            image_run = true;
            if (boot_local) {
                if (uint64_t(since) != RESET_TO_IMAGE_BOOT_LOCAL) { fail("the image's first instruction leaves execute %lld cycles after the reset, the RTL's %llu", (long long)since, (unsigned long long)RESET_TO_IMAGE_BOOT_LOCAL); return; }
                figures[1]++;
            }
        }
        if (released && rom_release) {                            // the release the boot ROM waits for, in this cycle
            rom_release = false;
            released_at = int64_t(cycle); events_load_seen = false; retired_at_release = rv.instructions;
            if (!rv.in_wfi || !rv.core_status_canonical_wait) fail("the release finds the core out of the canonical wait");
            return;
        }
        if (released_at < 0) return;
        const uint64_t after = cycle - uint64_t(released_at);     // R, MEIP's first cycle, is the release's + 1
        if (raised && !rv.lsu_store && rv.lsu_addr == ADDRESS_EVENTS && !events_load_seen) {
            events_load_seen = true;
            if (after != 1 + WAKE_TO_EXECUTE_END) { fail("the load after the wake is raised in R + %llu, the RTL's R + %llu", (unsigned long long)(after - 1), (unsigned long long)WAKE_TO_EXECUTE_END); return; }
        }
        if (events_load_seen && rv.instructions == retired_at_release + 1) {   // the load retires: the first since the wake
            retired_at_release = ~uint64_t(0) - 1;
            if (after != 1 + WAKE_TO_RETIRE + WINDOW_LOAD_CYCLES - 1) { fail("the load after the wake retires in R + %llu, the RTL's R + %llu", (unsigned long long)(after - 1), (unsigned long long)(WAKE_TO_RETIRE + WINDOW_LOAD_CYCLES - 1)); return; }
            figures[2]++;
        }
        if (image_entered) {
            if (after != RELEASE_TO_IMAGE) { fail("the image's first instruction leaves execute %llu cycles after the release, the RTL's %llu", (unsigned long long)after, (unsigned long long)RELEASE_TO_IMAGE); return; }
            figures[3]++;
            released_at = -1;
        }
    }
    template <class Done>
    void step_until(uint32_t timeout, const char* what, Done done) {
        for (uint32_t i = 0; i < timeout && !dead; i++) {
            if (done()) return;
            step();
        }
        if (!dead && !done()) fail("no %s in %u cycles", what, timeout);
    }
    void steps(uint32_t count) { for (uint32_t i = 0; i < count && !dead; i++) step(); }

    // Lands `rows` on input `port` as 128-bit WRITEs from the master tile, uncounted.
    void land(unsigned port, const std::vector<rtl_images::Row>& rows) {
        for (const rtl_images::Row& r : rows) {
            Packet p;
            p.dst = tile_id; p.src = kMasterTile; p.mode = MODE_WRITE; p.counter = 0; p.size = SIZE_128;
            p.address = r.address; p.lo = r.lo; p.hi = r.hi;
            queued[port].push_back(p);
        }
    }
    // Answers the boot ROM's LOAD request (6.5): once it has come, the load on input `port`, `rows` then a COUNT on
    // counter 7, all from the master tile; then waits for the BARRIER_ARRIVE and releases the tile.
    void load_and_release(unsigned port, const std::vector<rtl_images::Row>& rows) {
        step_until(BOOT_TIMEOUT, "LOAD request", [&] { return loads > 0; });
        if (dead) return;
        loads--;
        land(port, rows);
        Packet count;
        count.dst = tile_id; count.src = kMasterTile; count.mode = MODE_COUNT; count.counter = LOAD_COUNTER;
        queued[port].push_back(count);
        step_until(BOOT_TIMEOUT, "landing", [&] { return queued[port].empty(); });
        // The COUNT has been driven: it is ejected and counted within a few cycles, and the tile may arrive from then.
        arrive_allowed = true;
        wait_and_release(true);
    }
    // Waits for the BARRIER_ARRIVE, then releases the tile after a random wait.
    void wait_and_release(bool from_rom = false) {
        const uint32_t before = arrivals;
        step_until(BOOT_TIMEOUT, "BARRIER_ARRIVE", [&] { return arrivals > before; });
        steps(uint32_t(random.below(RELEASE_WAIT_MOST)));
        if (dead) return;
        if (arrivals != before + 1) { fail("BARRIER_ARRIVE is one cycle's pulse: %u pulses", arrivals - before); return; }
        arrive_allowed = false;
        release = true;
        rom_release = from_rom;
        step();
    }
    // The arguments' two rows (tile_map.h): argument 0 the kind, then the run's, the rest 0.
    static std::vector<rtl_images::Row> argument_rows(uint32_t kind, const std::vector<uint32_t>& arguments) {
        uint32_t words[8] = {};
        words[0] = kind;
        for (size_t i = 0; i < arguments.size(); i++) words[1 + i] = arguments[i];
        std::vector<rtl_images::Row> rows;
        for (unsigned index = 0; index < 2; index++)
            rows.push_back({ARGUMENTS + 16 * index, uint64_t(words[4 * index]) | uint64_t(words[4 * index + 1]) << 32,
                            uint64_t(words[4 * index + 2]) | uint64_t(words[4 * index + 3]) << 32});
        return rows;
    }
    // The load of program `kind` with `arguments` on input `port`, once the boot ROM asks for it, up to its release.
    void load(uint32_t kind, unsigned port, const std::vector<uint32_t>& arguments) {
        steps(uint32_t(random.below(50)));
        if (dead) return;
        std::vector<rtl_images::Row> rows = rtl_images::program_rows(files[kind]);
        for (const rtl_images::Row& r : argument_rows(kind, arguments)) rows.push_back(r);
        load_and_release(port, rows);
        counts.runs[kind]++;
    }
    void boot(uint32_t kind, uint32_t tile, unsigned port, const std::vector<uint32_t>& arguments) {
        reset(tile);
        load(kind, port, arguments);
    }
    // A passing program's end (6.5): its jump to the reset PC, after which the boot ROM sends its next LOAD request.
    void end() {
        step_until(REPORT_TIMEOUT, "LOAD request after the pass", [&] { return loads > 0; });
        if (!dead) loads--;
    }
    // The next report, which the run has waited at most REPORT_TIMEOUT cycles for.
    Report report() {
        step_until(REPORT_TIMEOUT, "report", [&] { return !reports.empty(); });
        if (dead) return {};
        const Report r = reports.front();
        reports.pop_front();
        if (r.first == REPORT_FAIL) fail("check %u failed: expected 0x%x, read 0x%x", r.second[0], r.second[1], r.second[2]);
        return r;
    }
    void expect_report(const Report& got, uint32_t number, uint32_t w1, uint32_t w2, uint32_t w3, const char* what) {
        if (dead) return;
        if (got.first != number || got.second[0] != w1 || got.second[1] != w2 || got.second[2] != w3)
            fail("%s: report %u (%x, %x, %x), expected %u (%x, %x, %x)", what, got.first, got.second[0], got.second[1], got.second[2],
                 number, w1, w2, w3);
    }
    // The end of a run: quiet cycles with no request and no packet still expected.
    void quiet() {
        steps(QUIET_CYCLES);
        if (dead) return;
        if (!reports.empty()) { fail("a report after the last: %u", reports.front().first); return; }
        if (loads != 0) { fail("a LOAD request after the last"); return; }
        if (!expected_out.empty()) fail("packets never out: %zu", expected_out.size());
    }
    // PARAMS as the straps give it (7.1): the tile id, and for a compute tile peripheral 1, the replayer, no receive
    // side, the special tile attached at it, the barrier wired and boot_local 0.
    static uint32_t params(uint32_t tile) {
        const int special = attached_at(tile);
        return tile | 1u << 7 | (special >= 0 ? 1u << special : 0u) << 14 | 1u << 17;
    }

    // ---- The runs ----

    // registers: the PASS report, with PARAMS, mhartid (the tile id) and STATUS.
    void registers(uint32_t tile, unsigned port) {
        const uint32_t p = params(tile);
        boot(KIND_REGISTERS, tile, port, {p, 0, 0});
        expect_report(report(), REPORT_PASS, p, tile, 0, "registers' pass");
        end();
        quiet();
    }

    // noc: the program's packets out, each on the router output its route takes (16.2), with its fields, a tile for
    // each output its candidates reach; REPORT_WAIT once it has armed counter 6; then a COUNT packet back on every
    // input, and the PASS report.
    void noc(uint32_t tile, unsigned port) {
        const bool has_attach[3] = {attached_at(tile) == 0, attached_at(tile) == 1, attached_at(tile) == 2};
        auto usable = [&](uint32_t dst) {
            const int out = first_hop(tile, dst);
            return out >= 0 && (unsigned(out) < ATTACH || has_attach[unsigned(out) - ATTACH]);
        };
        // The tiles to send to: a tile one step along each link and the special tiles, each with the output its route
        // takes, those whose route ends here or needs an attach port the tile has not left out. One tile for each
        // output they reach, up to four, the attach ports first, then the outputs no run has sent on yet; the rest at
        // random.
        std::vector<uint32_t> candidates;
        for (uint32_t s : {1u, kTileCount - 1, kMeshSize, kTileCount - kMeshSize}) if (usable((tile + s) % kTileCount)) candidates.push_back((tile + s) % kTileCount);
        for (uint32_t special = 0; special < 3; special++) if (usable(kTileCount + special)) candidates.push_back(kTileCount + special);
        std::vector<unsigned> ports;
        for (uint32_t dst : candidates) ports.push_back(unsigned(first_hop(tile, dst)));
        auto key = [&](unsigned p) { return (p < ATTACH ? 4u : 0u) + (reachable[p] ? 2u : 0u); };
        std::stable_sort(ports.begin(), ports.end(), [&](unsigned a, unsigned b) { return key(a) != key(b) ? key(a) < key(b) : a < b; });
        ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
        std::vector<uint32_t> destinations;
        for (size_t i = 0; i < ports.size() && i < 4; i++) {
            std::vector<uint32_t> on_port;
            for (uint32_t dst : candidates) if (first_hop(tile, dst) == int(ports[i])) on_port.push_back(dst);
            destinations.push_back(on_port[random.below(on_port.size())]);
        }
        for (unsigned p : ports) reachable[p] = true;
        std::vector<uint32_t> rest;
        for (uint32_t dst : candidates) if (std::find(destinations.begin(), destinations.end(), dst) == destinations.end()) rest.push_back(dst);
        while (destinations.size() < 4 && !rest.empty()) {
            const size_t pick = random.below(rest.size());
            destinations.push_back(rest[pick]);
            rest.erase(rest.begin() + long(pick));
        }
        std::vector<uint32_t> arguments(5, 0xFFFFFFFF);
        for (size_t i = 0; i < destinations.size(); i++) arguments[i] = destinations[i];
        const size_t input_count = (attached_at(tile) >= 0 ? 5 : 4);
        arguments[4] = uint32_t(input_count);
        boot(KIND_NOC, tile, port, arguments);
        if (dead) return;
        for (unsigned index = 0; index < 4; index++) {
            if (arguments[index] == 0xFFFFFFFF) continue;
            Packet p;
            p.dst = arguments[index]; p.src = tile; p.mode = MODE_WRITE; p.counter = 0; p.size = SIZE_32;
            p.address = 0x100 + 4 * index; p.lo = 0x5EED0000 | index;
            expected_out.push_back({unsigned(first_hop(tile, arguments[index])), p});
        }
        // The program's wait, once it has armed counter 6 and sent its packets; then a COUNT packet on counter 6 back
        // on every input.
        const Report wait = report();
        if (!dead && wait.first != REPORT_WAIT) fail("noc's wait for the packets back: report %u", wait.first);
        step_until(REPORT_TIMEOUT, "packets out", [&] { return expected_out.empty(); });
        for (unsigned in : inputs) {
            Packet p;
            p.dst = tile; p.src = kMasterTile; p.mode = MODE_COUNT; p.counter = 6;
            queued[in].push_back(p);
        }
        expect_report(report(), REPORT_PASS, tile, 0, 0, "noc's pass");
        end();
        quiet();
    }

    // barrier: REPORT_WAIT before its own barrier; its delay loop keeps the core out of its WFI, so the BARRIER_ARRIVE
    // comes only after the loop; the release; and the PASS report, from its second phase, with argument 1; then the next
    // LOAD, answered with the registers program, which runs and passes with no reset between.
    void barrier(uint32_t tile, unsigned port) {
        const uint32_t magic = 0x5EC04D00 | uint32_t(random.below(256));
        boot(KIND_BARRIER, tile, port, {magic});
        const Report wait = report();
        if (!dead && wait.first != REPORT_WAIT) fail("barrier's wait: report %u", wait.first);
        const uint64_t reported = cycle;
        // The program's delay loop keeps the core out of the canonical wait: the tile is ready only once the core waits
        // (7.3).
        arrive_allowed = true;
        wait_and_release();
        if (!dead && arrived_at - reported < ARRIVAL_DELAY_LEAST)
            fail("BARRIER_ARRIVE %llu cycles after the wait's report, before the core waits", (unsigned long long)(arrived_at - reported));
        expect_report(report(), REPORT_PASS, magic, 0, 0, "barrier's pass");
        // The image's end and the ROM's next LOAD, answered with the registers program, with no reset between.
        const uint32_t p = params(tile);
        load(KIND_REGISTERS, port, {p, 0, 0});
        expect_report(report(), REPORT_PASS, p, tile, 0, "registers' pass after the barrier");
        end();
        quiet();
    }

    // errors: for one error of ERRORS, the boot ROM handler's reports 24 and 25: ERRORS that error's bit alone, mcause
    // the NMI's (or the exception's for an access fault and an illegal instruction); the router's overflow raised by
    // the test, which floods the eject from every link.
    void errors(uint32_t error, uint32_t tile, unsigned port) {
        boot(KIND_ERRORS, tile, port, {error});
        if (dead) return;
        error_allowed = true;
        if (error == ERROR_ROUTER_OVERFLOW) {
            // A COUNT packet with no counter, which writes and counts nothing, on every link in every cycle: four queues
            // into the eject, which takes one a cycle.
            flood = true;
            for (uint32_t i = 0; i < FLOOD_CYCLES; i++)
                for (unsigned link = WEST; link <= SOUTH; link++) {
                    Packet p;
                    p.dst = tile; p.src = kMasterTile; p.mode = MODE_COUNT;
                    queued[link].push_back(p);
                }
        }
        const Report first = report();
        flood = false;
        for (auto& q : queued) q.clear();
        if (dead) return;
        if (first.first != REPORT_HANDLER_FIRST) { fail("error %u: the handler's first report: %u", error, first.first); return; }
        if (first.second[0] != 1u << error) { fail("error %u: ERRORS 0x%x", error, first.second[0]); return; }
        const uint32_t cause = error == ERROR_ACCESS_FAULT ? CAUSE_LOAD_ACCESS_FAULT : error == ERROR_ILLEGAL_INSTRUCTION ? CAUSE_ILLEGAL_INSTRUCTION : CAUSE_NMI;
        if (first.second[1] != cause) { fail("error %u: mcause 0x%x, expected 0x%x", error, first.second[1], cause); return; }
        const Report second = report();
        if (dead) return;
        if (second.first != REPORT_HANDLER_SECOND) { fail("error %u: the handler's second report: %u", error, second.first); return; }
        if (!error_risen) { fail("error %u: the error line low", error); return; }
        counts.errors_seen[error]++;
        quiet();
    }

    // replayer: a pass of 4 rows through the tile's replayer, its tables, record and stream written by the program
    // through the write unit and the tile's own loopback (replayer.S); the rows out of the row port, written here from
    // 17.8.11 for the program's instructions: ADDI x3, x1, 7; ADDI x4, x3, 17; ADDI x1, x2, 257 (x1 = 100, x2 = 200,
    // x3 = 0x33, x4 = 0x44 before, rd_prev on), from pc 0x1000 and instr_idx 5, each the program's template's (opcode
    // ADDI, rs2 an immediate, sign_imm), and a padding word's row, all 0. The PASS report: ROWS_RUN, the replayer's
    // PARAMS (type 4, version 0) and its EVENTS_PEEK: the four credits the state write filled, still 2, done consumed.
    // The simulator's row port is the replayer's sink, which takes a row in every cycle (the chip ties replay_row_ready
    // to 1: 17.8.11); the rows are read from its buffer.
    void replayer(uint32_t tile, unsigned port) {
        // (instr_idx, rd, rs1, immediate, rs1's value, rd before, rd after)
        const uint64_t rows[3][7] = {{5, 3, 1, 7, 100, 0x33, 107}, {6, 4, 3, 17, 107, 0x44, 124}, {7, 1, 2, 257, 200, 100, 457}};
        std::vector<uint64_t> expected(4 * ROW_LONGS, 0);
        for (unsigned at = 0; at < 3; at++) {
            uint64_t* row = &expected[at * ROW_LONGS];
            // Word 0: instr_idx, opcode ADDI (2), rd_reg, rs1_reg, rs2_reg 0; rs2 an immediate [85], imm_signed [86].
            row[0] = rows[at][0] | uint64_t(2) << 32 | rows[at][1] << 40 | rows[at][2] << 48;
            row[1] = uint64_t(3) << (85 - 64);
            // Word 1: pc, the immediate; word 2: rs1, rs2 0; word 3: rd_prev, rd.
            row[2] = 0x1000 + 4 * at; row[3] = rows[at][3];
            row[4] = rows[at][4];
            row[6] = rows[at][5]; row[7] = rows[at][6];
        }
        reset(tile);
        if (dead) return;
        std::vector<uint64_t> out(8 * ROW_LONGS, ~uint64_t(0));
        Replayer& rp = mach->m.unit<PeriphSlot>(unit_prefix(tile) + "slot").s.replayer(1);
        rp.set_row_buffer(out.data(), 8);
        load(KIND_REPLAYER, port, {});
        expect_report(report(), REPORT_PASS, 4, 4, 0x3C, "replayer's pass");
        if (dead) return;
        if (rp.rows_sunk() != 4) { fail("rows out of the row port: %llu, expected 4", (unsigned long long)rp.rows_sunk()); return; }
        for (unsigned i = 0; i < 4 * ROW_LONGS; i++)
            if (out[i] != expected[i]) { fail("row %u out of the row port: word %u is 0x%llx, expected 0x%llx", i / ROW_LONGS, i % ROW_LONGS, (unsigned long long)out[i], (unsigned long long)expected[i]); return; }
        counts.rows += 4;
        end();
        quiet();
        if (!dead && rp.rows_sunk() != 4) fail("a row after the pass: %llu rows", (unsigned long long)rp.rows_sunk());
        rp.set_row_buffer(nullptr, 0);
    }

    // A tile for a run, and the next input of the tile's in turn (base_tile_test.rs's `pick`).
    std::pair<uint32_t, unsigned> pick(size_t run) {
        const uint32_t tile = uint32_t(random.below(kTileCount));
        std::vector<unsigned> in = {WEST, EAST, NORTH, SOUTH};
        if (attached_at(tile) >= 0) in.push_back(ATTACH + unsigned(attached_at(tile)));
        return {tile, in[run % in.size()]};
    }
    // Every output a route reached has sent, and only those; every link of the mesh but the north one, which no unicast
    // to a tile one step away needs first... (base_tile_test.rs's check_outputs)
    void check_outputs() {
        if (dead) return;
        for (unsigned port = 0; port < PORTS; port++)
            if ((counts.outputs_seen[port] > 0) != reachable[port]) { fail("%s: seen %u, reachable %d", PORT_NAMES[port], counts.outputs_seen[port], int(reachable[port])); return; }
        if (!(reachable[EAST] && reachable[WEST] && reachable[SOUTH])) fail("a link of the mesh unreached");
    }
};

void print_counts(const char* name, const Run& r) {
    std::printf("%s: %llu cycles; runs", name, (unsigned long long)r.cycle);
    for (uint32_t n : r.counts.runs) std::printf(" %u", n);
    std::printf("; inputs driven");
    for (uint32_t n : r.counts.inputs_driven) std::printf(" %u", n);
    std::printf("; outputs seen");
    for (uint32_t n : r.counts.outputs_seen) std::printf(" %u", n);
    std::printf("; reports %u, loads %u, request stalls %u, rows %u; errors seen", r.counts.reports, r.counts.loads, r.counts.request_stalls, r.counts.rows);
    for (uint32_t n : r.counts.errors_seen) std::printf(" %u", n);
    std::printf("; the boot ROM's cycles checked: reset to LOAD %u, reset to image (boot_local) %u, wake to the load's retirement %u, release to image %u\n",
                r.figures[0], r.figures[1], r.figures[2], r.figures[3]);
}

}  // namespace

// The RTL's base_tile_test: every program, the errors one for every error, at tile 0, the last tile and random ones, each
// run's boot on another of its inputs; and the noc program at each special tile's attach tile, so that every attach port
// sends.
TEST(rtl_tile_programs) {
    Run bench(0x5be0cd19);
    bench.registers(0, ATTACH);
    bench.registers(kTileCount - 1, WEST);
    size_t run = 0;
    auto next = [&] { run++; return bench.pick(run); };
    for (int i = 0; i < 2; i++) { auto [tile, port] = next(); bench.noc(tile, port); }
    // At each special tile's attach tile, so that every attach port sends.
    for (uint32_t special = 0; special < 3; special++) bench.noc(attach_tile(special), ATTACH + special);
    for (int i = 0; i < 2; i++) { auto [tile, port] = next(); bench.barrier(tile, port); }
    for (int i = 0; i < 2; i++) { auto [tile, port] = next(); bench.replayer(tile, port); }
    for (uint32_t error = 0; error < ERROR_BITS; error++) {
        auto [tile, port] = next();
        bench.errors(error, tile, port);
    }
    print_counts("rtl_tile_programs", bench);
    CHECK(!bench.dead);
    for (unsigned port = 0; port < PORTS; port++) CHECK(bench.counts.inputs_driven[port] > 0);
    bench.check_outputs();
    CHECK(!bench.dead);
    CHECK(bench.counts.request_stalls > 0);
    for (uint32_t error = 0; error < ERROR_BITS; error++) CHECK_EQ(bench.counts.errors_seen[error], 1u);
    // Every run boots from a reset through the ROM's LOAD and its release: 22 resets, and the barrier runs' second loads.
    CHECK_EQ(bench.figures[0], 22u);
    CHECK_EQ(bench.figures[2], 24u);
    CHECK_EQ(bench.figures[3], 24u);
}

// The boot strap (6.5): with PARAMS.boot_local 1 the boot ROM jumps to IMEM's base at once, to the image placed in the
// tile's IMEM and DMEM before the reset ended: no LOAD request, no load, no arrival. The registers program, placed with
// its arguments, passes with PARAMS as the straps give it (boot_local at [18]: 7.1), STATUS 0 and no event pending, and
// the tile has sent no LOAD request by then. (base_tile_special_test.rs places its program so; a compute tile's image
// then ends by jumping to the reset PC, and the ROM, to the image again: the test stops at the first pass.)
TEST(rtl_tile_boot_local) {
    Run bench(0x510e527f);
    const uint32_t tile = 21;
    const uint32_t params = Run::params(tile) | 1u << 18;
    std::vector<rtl_images::Row> rows = rtl_images::program_rows(bench.files[KIND_REGISTERS]);
    for (const rtl_images::Row& r : Run::argument_rows(KIND_REGISTERS, {params, 0, 0})) rows.push_back(r);
    bench.reset(tile, &rows);
    bench.expect_report(bench.report(), REPORT_PASS, params, tile, 0, "registers' pass, placed");
    CHECK(!bench.dead);
    CHECK_EQ(bench.counts.loads, 0u);
    CHECK_EQ(bench.arrivals, 0u);
    CHECK_EQ(bench.figures[1], 1u);                         // the image's first instruction, 27 cycles after the reset
}

// The RTL's base_tile_without_attach_ports: the programs but the errors one, at random tiles that are not attach tiles.
TEST(rtl_tile_programs_at_plain_tiles) {
    Run bench(0x1f83d9ab);
    auto plain = [&](size_t run) {
        for (;;) { auto p = bench.pick(run); if (attached_at(p.first) < 0) return p; }
    };
    for (size_t run = 0; run < 2; run++) {
        { auto [tile, port] = plain(run); bench.registers(tile, port); }
        { auto [tile, port] = plain(run + 1); bench.noc(tile, port); }
        { auto [tile, port] = plain(run + 2); bench.barrier(tile, port); }
        { auto [tile, port] = plain(run + 3); bench.replayer(tile, port); }
    }
    bench.check_outputs();
    print_counts("rtl_tile_programs_at_plain_tiles", bench);
    CHECK(!bench.dead);
    CHECK_EQ(bench.figures[0], 8u);                         // 8 resets, and the barrier runs' second loads
    CHECK_EQ(bench.figures[2], 10u);
    CHECK_EQ(bench.figures[3], 10u);
}
