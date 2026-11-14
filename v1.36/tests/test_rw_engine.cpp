// test_rw_engine: the RW engine, the memory tile's peripheral 1 (machine-spec 17.7, with 3, 4.1, 5.4, 6.4, 11.4, 11.5,
// 17.1-17.5 and 18), cycle by cycle. It is the RTL's own test of its unit, Snarky One's
// rtl/blocks/rw_engine_unit/test/src/rw_engine_unit_test.rs, ported test for test, on the simulator's model
// (src/units/rw_engine.h in the slot, src/units/periph_slot.h), at the RTL test's three configurations: the defaults
// (32 bits of LPDDR address, a write buffer of 16 KiB, output queue 0 of 16 beats), the least (24 bits, 4 KiB, 4 beats)
// and the most (36 bits, 16 MiB, 5 beats).
//
// WHERE THE EXPECTATIONS COME FROM. Never from the simulator:
//   - the directed tests write each expected value out, from 17.7 and from the RTL unit's README (Timing): START in p,
//     busy from p + 1, AR or AW from p + 2, a write's first beat on W in p + 3 and a beat a cycle after it, the bursts
//     by the 4-beat groups, WLAST at each burst's end, the room rule, the reply, the errors, the reset;
//   - every test also compares every output, every cycle, with `Reference`: the RTL test's model of 17.7 and the
//     README's timing, ported here; it shares no code with rw_engine.h (it keeps a job by its addresses, the queues as
//     lists and the buffer as words known or not);
//   - THE RTL ITSELF: tests/rwengine/unit.fp holds, for each bench of the RTL's test (named by its seed), a fingerprint
//     of every output of the RTL's unit in every cycle, taken from the RTL's simulation (tests/rwengine/README.md: the
//     instrumented run and its command). The bench here is the RTL's, with its random generator and its seeds, so its
//     stimulus is the RTL test's as long as the model answers as the RTL does, and the fingerprint of the model's
//     outputs must equal the RTL's at every checkpoint and at the end: the model's ports are the RTL's in every cycle
//     of the RTL's whole test, the random runs' 950 jobs included.
//
// THE BENCH plays the engine's neighbours as the RTL's test does: register_bus's reg interfaces (at most one access a
// cycle), the program (which keeps every rule 17.7 and 17.4 give it), the receive unit's write port, the router input
// (its ready drawn at random every cycle), and the LPDDR controller, tests/axi4_slave.h's UnitLpddr. `Dut` puts the
// slot's calls under the RTL's ports, by the slot's contract (periph_slot.h, THE CONTRACT):
//   a load presented in c       SlotCore::peek(c) (EVENTS: read(c), consuming at the end of c), before the edge
//   the edge that ends c        run_cycle(c, room), then the store of c (write(c)), end_cycle(c), and the write port's
//                               write of c: accept(c - 2), which the director accepted two cycles before (17.3)
//   packet_valid, packet        the DMA's packet register: the slot hands a packet over in the cycle it takes it,
//                               when the register is empty or its packet leaves (`room`), and the bench holds it until
//                               packet_ready, as the RTL's register does; its src is the tile's id here (16.4: the
//                               router's stamp, which the slot gives), where the RTL's port carries none
//   the lpddr port              RwEngine::lpddr_out(), its registers before the edge, and the slave's outputs of the
//                               cycle, which the engine asks its slave for
//
// The framework's side (the engine in a PeriphSlot unit behind its Links: the write port's three cycles, the wake) is
// the last tests'. PLANTED BUGS: tests/rw_engine_teeth.sh.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "axi4_slave.h"
#include "harness.h"
#include "config.h"
#include "sim.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/regmap.h"
#include "units/rw_engine.h"

using namespace units;
using axi4::AddressBeat;
using axi4::Random;
using axi4::u128;

namespace {

// ---------------------------------------------------------------- the window's blocks and words (17.1, 17.7)
enum : uint8_t { CONTROL = 0, DMA = 1, RW = 2 };
enum : uint8_t { W_PARAMS = 0, W_STATUS = 1, W_ERRORS = 2, W_EVENTS = 3, W_EVENTS_PEEK = 4, W_EVENTS_MASK = 5, W_START = 6 };
enum : uint8_t { D_HDR = 0, D_DEST_ADDR = 1, D_START = 2 };
enum : uint8_t { RW_CONFIG = 0, RW_LPDDR_ADDR = 1, RW_BUFFER_ADDR = 2, RW_REPLY_W0 = 3, RW_REPLY_SEND = 4 };
constexpr uint32_t BUSY = 1, DONE = 1, DMA_DONE = 2, LPDDR_ERROR = 1;
constexpr uint32_t EXPECTED_PARAMS = 2;                 // 17.7: type 2, version 0, no constants
constexpr uint32_t KIND_READ = 0, KIND_WRITE = 1;
constexpr unsigned SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2;   // 4.1
constexpr unsigned MODE_WRITE = 0;
constexpr unsigned TILE = 65;                           // the memory tile's id (2.1): its packets' src (16.4)
constexpr unsigned TILE_ID_MASK = 0x7F;
constexpr unsigned RESET_CYCLES = 2;
constexpr uint64_t GROUP_BEATS = 4;                     // a burst's group of beats (the README): 64 bytes, aligned

// DMA_HDR's value (17.1, 5.4): counter [4:2], mode [7:5], dst [14:8], count [26:15], queue [28:27].
uint32_t dma_hdr(uint32_t counter, uint32_t mode, uint32_t dst, uint32_t count, uint32_t queue) {
    return counter << 2 | mode << 5 | dst << 8 | count << 15 | queue << 27;
}
// RW_CONFIG's value (17.7): kind [0], beats [10:1].
uint32_t rw_config(uint32_t kind, uint32_t beats) { return kind | beats << 1; }

// ---------------------------------------------------------------- the configurations, written here
struct Config {
    unsigned lpddr_bits;
    uint32_t buffer_bytes;
    unsigned queue_beats;
    uint64_t lpddr_beats() const { return uint64_t(1) << (lpddr_bits - 4); }
    uint32_t rows() const { return buffer_bytes / 16; }
};
constexpr Config DEFAULTS{32, 16384, 16};               // 19
constexpr Config LEAST{24, 4096, 4};
constexpr Config MOST{36, 16u << 20, 5};

// ---------------------------------------------------------------- the engine's ports
struct Acc { bool some = false; uint8_t block = 0; bool write = false; uint8_t word = 0; uint32_t wdata = 0; };
Acc load(uint8_t block, uint8_t word) { return {true, block, false, word, 0}; }
Acc store(uint8_t block, uint8_t word, uint32_t wdata) { return {true, block, true, word, wdata}; }
const Acc NO_ACCESS{};
struct PortWrite { bool some = false; uint32_t address = 0; unsigned size = 0; u128 data = 0; };
const PortWrite NO_PORT{};
struct Inputs { bool reset; Acc access; PortWrite port; bool ready; LpddrIn axi; };

struct Packet {
    uint32_t dst = 0, src = 0, mode = 0, counter = 0, size = 0, address = 0;
    u128 payload = 0;
    bool operator==(const Packet&) const = default;
};
AddressBeat address_beat(uint64_t beat, uint32_t beats) { return {beat << 4, beats - 1, axi4::SIZE_16_BYTES, axi4::BURST_INCR}; }

struct Outputs {
    uint32_t rdata = 0;              // the accessed block's read data
    bool event = false, error = false;
    bool packet_valid = false;
    Packet packet;
    bool arvalid = false, awvalid = false;
    AddressBeat ar, aw;
    bool wvalid = false, wlast = false;
    u128 wdata = 0;
    unsigned wstrb = 0;
    LpddrOut lpddr;                  // the port's outputs as the slave takes them
};

// The first failure of a bench (a CHECK returns from the test function only, so the bench's own checks record); with
// RW_ENGINE_VERBOSE set, the first four are printed as they come.
struct Failures {
    unsigned n = 0;
    std::string first;
    void fail(const std::string& what) { if (n++ == 0) first = what; if (n <= 4 && std::getenv("RW_ENGINE_VERBOSE")) std::printf("  failure %u: %s\n", n, what.c_str()); }
    void expect(bool ok, const char* what, uint64_t cycle) { if (!ok) fail(std::string(what) + " (cycle " + std::to_string(cycle) + ")"); }
};

// ---------------------------------------------------------------- the model under test, under the RTL's ports
struct Dut {
    // The LPDDR controller as the engine asks for it: the bench drives UnitLpddr itself, every cycle, as the RTL's
    // bench does, and leaves the cycle's outputs here.
    struct Latch final : LpddrSlave {
        LpddrIn in;
        uint64_t asked = 0;
        LpddrIn cycle(int64_t, const LpddrOut&) override { asked++; return in; }
    } latch;
    SlotCore s;
    RwEngine* e = nullptr;
    int64_t t = 0;
    bool offered = false;            // the packet register
    Packet packet;

    explicit Dut(const Config& c, bool with_slave = true) {
        PeriphSlotConfig pc;
        pc.tile = TILE;
        pc.rw.lpddr_bits = c.lpddr_bits; pc.rw.buffer_bytes = c.buffer_bytes; pc.rw.queue_beats = c.queue_beats;
        s.tile = TILE;
        s.dma_port = true;
        s.install(1, PT_RW, pc);
        e = &s.rw_engine(1);
        if (with_slave) e->attach(&latch);
    }
    static unsigned window_word(const Acc& a) { return a.block * 16u + a.word; }
    // The outputs of cycle t, before its edge.
    Outputs outputs(const Inputs& i) {
        Outputs o;
        if (i.access.some && !i.access.write) {
            const unsigned w = window_word(i.access);
            if (w == P_EVENTS) s.read(t, 1, w, o.rdata);        // the consuming load
            else s.peek(t, 1, w, o.rdata);
        }
        o.event = s.live_events(t) & 1;
        o.error = s.live_errors(t) & 1;
        o.packet_valid = offered;
        o.packet = packet;
        o.lpddr = e->lpddr_out();
        o.arvalid = o.lpddr.arvalid; o.awvalid = o.lpddr.awvalid;
        o.ar = o.aw = axi4::address_of(o.lpddr);
        o.wvalid = o.lpddr.wvalid; o.wlast = o.lpddr.wlast;
        o.wdata = axi4::make128(o.lpddr.wdata_hi, o.lpddr.wdata_lo);
        o.wstrb = LpddrOut::WSTRB;
        return o;
    }
    // The edge that ends cycle t.
    void edge(const Inputs& i) {
        if (i.reset) { s.reset(t); offered = false; t++; return; }
        latch.in = i.axi;
        const bool leaving = offered && i.ready;
        s.run_cycle(t, !offered || leaving ? 1 : 0);
        if (leaving) offered = false;
        if (s.n_out > 1) FATAL("test_rw_engine: two DMA packets in a cycle");
        if (s.n_out == 1) {
            if (offered) FATAL("test_rw_engine: the DMA took a packet with its register full");
            const DmaPacket& p = s.out[0];
            packet = Packet{uint32_t(lw::dst(p.h)), uint32_t(lw::src(p.h)), uint32_t(lw::mode(p.h)), uint32_t(lw::counter(p.h)),
                            uint32_t(lw::size(p.h)), lw::addr(p.h), axi4::make128(p.hi, p.lo)};
            offered = true;
        }
        if (i.access.some && i.access.write && !s.write(t, 1, window_word(i.access), i.access.wdata))
            FATAL("test_rw_engine: a store to window word %u is a bus error", window_word(i.access));
        s.end_cycle(t);
        if (i.port.some)
            s.accept(t - 2, 1, axi4::lo64(i.port.data), lw::hdr(0, TILE, lw::M_WRITE, 0, i.port.address, i.port.size), axi4::hi64(i.port.data));
        t++;
    }
};

// ---------------------------------------------------------------- the reference: 17.7, 17.1-17.4 and the READMEs' timing
// A buffer row as known: each 32-bit word, unknown while never written.
struct Row { uint32_t w[4] = {}; bool known[4] = {}; };
void row_value(const Row& row, u128& data, u128& mask) {
    data = 0; mask = 0;
    for (unsigned lane = 0; lane < 4; lane++)
        if (row.known[lane]) { data |= u128(row.w[lane]) << (32 * lane); mask |= u128(0xFFFFFFFF) << (32 * lane); }
}
struct Header {
    uint32_t counter, mode, dst, count, queue;
    static Header of(uint32_t v) { return {v >> 2 & 7, v >> 5 & 7, v >> 8 & TILE_ID_MASK, v >> 15 & 0xFFF, v >> 27 & 3}; }
};
struct Transfer { Header header; uint32_t pending; };
// A job as the README runs it: its kind; the beats still to raise and the next burst's LPDDR beat; a read's beats still
// to come or a write's rows still to read; a write's next row and its LPDDR beat; its bursts raised and not answered.
struct Job { uint32_t kind, raise_left; uint64_t raise_address; uint32_t moving_left, fetch_row; uint64_t fetch_address; uint32_t outstanding; };
struct WriteBeat { Row row; bool last; };

struct Reference {
    Config config;
    bool busy = false, done = false, dma_done = false;
    uint32_t mask = 0;
    bool lpddr_error = false;
    std::deque<u128> queues[2];
    std::optional<Header> header;
    std::optional<uint32_t> dest_addr;
    std::optional<Transfer> transfer;
    std::optional<Packet> offered;
    std::optional<uint32_t> rw_config_reg, rw_buffer_addr;
    std::optional<uint64_t> rw_lpddr_addr;
    uint32_t reply_w0 = 0;
    std::optional<Job> job;
    std::optional<AddressBeat> ar, aw;
    std::optional<WriteBeat> w;
    bool pending = false;
    std::vector<Row> buffer;
    Failures* f = nullptr;

    Reference(const Config& c, Failures* fails) : config(c), buffer(c.rows()), f(fails) {}
    // The reset (17.7, 18): the job ended, the queues, the packet register and the channels empty, RW_REPLY_W0, STATUS,
    // ERRORS, EVENTS and EVENTS_MASK 0, the other registers undefined; the buffer kept.
    void reset() {
        Reference fresh(config, f);
        fresh.buffer = std::move(buffer);
        *this = std::move(fresh);
    }
    bool dma_busy() const { return transfer.has_value(); }
    uint32_t events() const { return uint32_t(done) | uint32_t(dma_done) << 1; }
    // A load's value, none while undefined.
    std::optional<uint32_t> read(uint8_t block, uint8_t word) const {
        if (block == CONTROL) switch (word) {
            case W_PARAMS: return EXPECTED_PARAMS;
            case W_STATUS: return uint32_t(busy) | uint32_t(dma_busy()) << 1;
            case W_ERRORS: return uint32_t(lpddr_error);
            case W_EVENTS: case W_EVENTS_PEEK: return events();
            case W_EVENTS_MASK: return mask;
        }
        if (block == DMA && word == D_HDR) { if (!header) return {}; return dma_hdr(header->counter, header->mode, header->dst, header->count, header->queue); }
        if (block == DMA && word == D_DEST_ADDR) return dest_addr;
        if (block == RW && word == RW_CONFIG) return rw_config_reg;
        if (block == RW && word == RW_LPDDR_ADDR) { if (!rw_lpddr_addr) return {}; return uint32_t(*rw_lpddr_addr); }
        if (block == RW && word == RW_BUFFER_ADDR) return rw_buffer_addr;
        FATAL("register_bus presents no load of block %u, word %u", block, word);
    }
    uint32_t queue_free_beats() const { return config.queue_beats - uint32_t(queues[0].size()); }
    static uint32_t burst_beats(const Job& j) {
        const uint32_t group_rest = uint32_t(GROUP_BEATS - j.raise_address % GROUP_BEATS);
        return j.raise_left < group_rest ? j.raise_left : group_rest;
    }
    bool raises(const LpddrIn& axi) const {
        if (!job || job->raise_left == 0) return false;
        if (job->kind == KIND_WRITE) return !aw || axi.awready;
        const uint32_t in_flight = job->moving_left - job->raise_left;
        return (!ar || axi.arready) && in_flight + burst_beats(*job) <= queue_free_beats();
    }
    bool job_ends(const LpddrIn& axi) const {
        if (!job) return false;
        if (job->kind == KIND_WRITE) return job->raise_left == 0 && job->outstanding == uint32_t(axi.bvalid);
        return job->moving_left == uint32_t(axi.rvalid);
    }
    bool takes(const LpddrIn& axi) const { return pending && (!w || axi.wready); }
    bool dma_ends(bool ready) const { return transfer && transfer->pending == 0 && (!offered || ready); }

    // Checks the outputs for these inputs.
    void check(const Outputs& o, const Inputs& i, uint64_t cycle) const {
        if (i.access.some && !i.access.write) {
            const std::optional<uint32_t> expected = read(i.access.block, i.access.word);
            if (expected && o.rdata != *expected) {
                char b[160];
                std::snprintf(b, sizeof b, "block %u, word %u's read data is 0x%x, expected 0x%x (cycle %llu)", i.access.block, i.access.word,
                              o.rdata, *expected, (unsigned long long)cycle);
                f->fail(b);
            }
        }
        f->expect(o.event == ((events() & mask) != 0), "event_peripheral", cycle);
        f->expect(o.error == lpddr_error, "error_peripheral", cycle);
        f->expect(o.packet_valid == offered.has_value(), "packet_valid", cycle);
        if (offered) f->expect(o.packet == *offered, "the packet offered", cycle);
        f->expect(o.arvalid == ar.has_value(), "ARVALID", cycle);
        if (ar) f->expect(o.ar == *ar, "the AR channel", cycle);
        f->expect(o.awvalid == aw.has_value(), "AWVALID", cycle);
        if (aw) f->expect(o.aw == *aw, "the AW channel", cycle);
        f->expect(o.wvalid == w.has_value(), "WVALID", cycle);
        if (w) {
            u128 data, m;
            row_value(w->row, data, m);
            f->expect((o.wdata & m) == data, "WDATA", cycle);
            f->expect(o.wlast == w->last, "WLAST", cycle);
            f->expect(o.wstrb == 0xFFFF, "WSTRB", cycle);
        }
    }

    // The edge of a cycle with inputs `i`: returns the packet that left.
    std::optional<Packet> step(const Inputs& i) {
        std::optional<Packet> left = i.ready ? offered : std::optional<Packet>{};
        if (i.reset) { reset(); return left; }
        const LpddrIn axi = i.axi;
        const Acc& access = i.access;
        auto is_store = [&](uint8_t block, uint8_t word) { return access.some && access.block == block && access.write && access.word == word; };
        const uint32_t wdata = access.some ? access.wdata : 0;

        // This cycle's decisions, from the state at its start.
        const bool start = is_store(CONTROL, W_START) && (wdata & 1) != 0;
        const bool ends = job_ends(axi);
        const bool raise = raises(axi);
        const bool take = takes(axi);
        const bool dma_end = dma_ends(i.ready);
        const bool w_leaving = w.has_value() && axi.wready;
        const bool events_load = access.some && access.block == CONTROL && !access.write && access.word == W_EVENTS;

        // ---- The DMA (17.4, its README's timing) ----
        const uint32_t free[2] = {queue_free_beats(), 1 - uint32_t(queues[1].size())};
        const bool register_free = !offered || i.ready;
        if (left) offered.reset();
        // A packet an entry: a 128-bit WRITE at DMA_DEST_ADDR, which advances by 16.
        if (transfer && transfer->pending != 0 && register_free && !queues[transfer->header.queue].empty()) {
            Transfer& t = *transfer;
            const u128 entry = queues[t.header.queue].front();
            queues[t.header.queue].pop_front();
            const bool writes = t.header.mode == MODE_WRITE;
            if (!dest_addr) FATAL("a transfer with D_DEST_ADDR undefined");
            const uint32_t address = *dest_addr;
            offered = Packet{t.header.dst, TILE, t.header.mode, t.header.counter, SIZE_128, writes ? address : 0, entry};
            if (writes) dest_addr = address + 16;
            t.pending--;
        }
        // The pushes, into room at the start of the cycle: a read beat, the reply {0, RW_REPLY_SEND, RW_REPLY_W0}, which
        // clears RW_REPLY_W0.
        if (axi.rvalid) {
            if (free[0] < 1) f->fail("the reference: a read beat with no room in queue 0");
            queues[0].push_back(axi4::make128(axi.rdata_hi, axi.rdata_lo));
        }
        if (is_store(RW, RW_REPLY_SEND)) {
            if (free[1] >= 1) queues[1].push_back(u128(wdata) << 32 | u128(reply_w0));
            reply_w0 = 0;
        }
        if (is_store(RW, RW_REPLY_W0)) reply_w0 = wdata;
        if (is_store(DMA, D_HDR)) header = Header::of(wdata);
        if (is_store(DMA, D_DEST_ADDR)) dest_addr = wdata;
        if (is_store(DMA, D_START) && (wdata & 1) != 0) {
            if (!header) FATAL("a D_START with D_HDR undefined");
            transfer = Transfer{*header, header->count};
        } else if (dma_end) transfer.reset();

        // ---- The control block (17.1, 17.2, 7.2) ----
        if (events_load) {
            done = done && (mask & DONE) == 0;
            dma_done = dma_done && (mask & DMA_DONE) == 0;
        }
        done |= ends;
        dma_done |= dma_end;
        if (is_store(CONTROL, W_EVENTS_MASK)) mask = wdata & 3;
        lpddr_error |= (axi.rvalid && axi.rresp != axi4::OKAY) || (axi.bvalid && axi.bresp != axi4::OKAY);

        // ---- The write data: the beat leaves, the pending row taken, a row read ----
        if (w_leaving) w.reset();
        const uint64_t lpddr_mask = config.lpddr_beats() - 1;
        if (take) {
            Job& j = *job;
            const bool last = j.moving_left == 1 || j.fetch_address % GROUP_BEATS == GROUP_BEATS - 1;
            w = WriteBeat{buffer[j.fetch_row], last};
            j.moving_left--;
            j.fetch_row = (j.fetch_row + 1) % config.rows();
            j.fetch_address = (j.fetch_address + 1) & lpddr_mask;
        }
        // A write reads its next row in every cycle with rows left to take.
        pending = job && job->kind == KIND_WRITE && job->moving_left != 0;

        // ---- The job and its bursts ----
        if (ar && axi.arready) ar.reset();
        if (aw && axi.awready) aw.reset();
        if (job) {
            Job& j = *job;
            if (raise) {
                const uint32_t beats = burst_beats(j);
                const AddressBeat burst = address_beat(j.raise_address, beats);
                if (j.kind == KIND_WRITE) { aw = burst; j.outstanding++; }
                else ar = burst;
                j.raise_left -= beats;
                j.raise_address = (j.raise_address + beats) & lpddr_mask;
            }
            if (j.kind == KIND_READ && axi.rvalid) j.moving_left--;
            if (j.kind == KIND_WRITE && axi.bvalid) j.outstanding--;
        }
        if (start) {
            if (busy && !ends) f->fail("the bench: a START while busy");
            if (!rw_config_reg || !rw_lpddr_addr || !rw_buffer_addr) FATAL("a START with a job register undefined");
            const uint32_t beats = *rw_config_reg >> 1 & 0x3FF;
            job = Job{*rw_config_reg & 1, beats, *rw_lpddr_addr, beats, *rw_buffer_addr, *rw_lpddr_addr, 0};
            busy = true;
        } else if (ends) {
            job.reset();
            busy = false;
        }

        // ---- Block 2's stores (17.7) ----
        if (is_store(RW, RW_CONFIG)) rw_config_reg = wdata & 0x7FF;
        if (is_store(RW, RW_LPDDR_ADDR)) rw_lpddr_addr = uint64_t(wdata) & lpddr_mask;
        if (is_store(RW, RW_BUFFER_ADDR)) rw_buffer_addr = wdata & (config.rows() - 1);

        // ---- The write port (17.3, 17.7): the write lands at the edge ----
        if (i.port.some) {
            const PortWrite& p = i.port;
            Row& row = buffer[(p.address >> 4) % config.rows()];
            const unsigned lane = p.address >> 2 & 3;
            auto set = [&](unsigned at, unsigned from) { row.w[at] = uint32_t(p.data >> (32 * from)); row.known[at] = true; };
            switch (p.size) {
                case SIZE_32: set(lane, 0); break;
                case SIZE_64: set(lane & 2, 0); set((lane & 2) + 1, 1); break;
                case SIZE_128: for (unsigned k = 0; k < 4; k++) set(k, k); break;
                default: break;
            }
        }
        return left;
    }
};

// ---------------------------------------------------------------- the RTL's fingerprints (tests/rwengine/unit.fp)
// One line a bench of the RTL's test, by its seed: `<seed> end <cycles> <fingerprint>`, and `<seed> at <cycle>
// <fingerprint>` every 16384 cycles; the fingerprint is of the RTL's outputs in every cycle up to there (below).
struct RtlFingerprints {
    std::map<uint32_t, std::map<uint64_t, uint64_t>> at;
    std::map<uint32_t, std::pair<uint64_t, uint64_t>> end;
    bool loaded = false;
    RtlFingerprints() {
        std::ifstream in("tests/rwengine/unit.fp");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream s(line);
            std::string seed, kind, cycle, fp;
            s >> seed >> kind >> cycle >> fp;
            const uint32_t k = uint32_t(std::stoul(seed, nullptr, 16));
            const uint64_t c = std::stoull(cycle), h = std::stoull(fp, nullptr, 16);
            if (kind == "end") end[k] = {c, h}; else at[k][c] = h;
            loaded = true;
        }
    }
};
const RtlFingerprints& rtl_fingerprints() { static RtlFingerprints f; return f; }
constexpr uint64_t CHECKPOINT = 16384;

// ---------------------------------------------------------------- the bench
struct Counts {
    uint32_t reads = 0, writes = 0, empty_jobs = 0, raise_held_for_room = 0, rows_read_again = 0, write_data_before_address = 0,
             four_kib_crossed = 0, replies = 0, errors = 0, resets_in_jobs = 0;
};
struct WBeat { u128 data = 0; bool last = false; bool operator==(const WBeat&) const = default; };

struct Bench {
    Failures fails;
    Dut dut;
    Reference reference;
    axi4::UnitLpddr lpddr;
    Random random;
    uint32_t seed;
    uint64_t cycle = 0;
    uint32_t ready_rate = 256;       // the router's ready rate, out of 256
    std::vector<Packet> sent;        // the packets that left, in order
    axi4::Handshake<AddressBeat> mon_ar, mon_aw;
    axi4::Handshake<WBeat> mon_w;
    axi4::Handshake<Packet> mon_packet;
    Counts counts;
    bool owned = false;              // the rows a running write reads, which no port write may touch (17.7)
    uint32_t owned_first = 0, owned_rows = 0;
    uint64_t fp = 0xcbf29ce484222325ull;   // the fingerprint of every output so far
    std::deque<bool> beat_known;     // queue 0's beats: each one's data defined (LPDDR's row written from a buffer row
    bool offered_known = true;       //   written in full, or never written); and the packet register's
    unsigned checkpoints = 0;

    Bench(const Config& c, axi4::Timing timing, uint32_t s)
        : dut(c), reference(c, &fails), lpddr(c.lpddr_bits, timing), random(s), seed(s) { reset(); }
    const Config& config() const { return reference.config; }
    void reset() {
        if (reference.busy) counts.resets_in_jobs++;
        for (unsigned k = 0; k < RESET_CYCLES; k++) step(true, NO_ACCESS, NO_PORT);
        owned = false;
    }

    void mix(uint64_t x) { fp = (fp ^ x) * 0x100000001b3ull; fp ^= fp >> 29; }
    // A cycle's outputs into the fingerprint: what the specification defines of them (the reference says which).
    void fingerprint(const Outputs& o, const Inputs& i) {
        if (i.access.some && !i.access.write && reference.read(i.access.block, i.access.word)) mix(o.rdata);
        mix(uint64_t(o.event) | uint64_t(o.error) << 1 | uint64_t(o.packet_valid) << 2 | uint64_t(o.arvalid) << 3 | uint64_t(o.awvalid) << 4 |
            uint64_t(o.wvalid) << 5);
        if (o.packet_valid) {
            mix(o.packet.dst); mix(o.packet.mode | o.packet.counter << 8 | o.packet.size << 16); mix(o.packet.address);
            if (offered_known) { mix(axi4::lo64(o.packet.payload)); mix(axi4::hi64(o.packet.payload)); }
        }
        if (o.arvalid) { mix(o.ar.address); mix(o.ar.len | o.ar.size << 8 | o.ar.burst << 16); }
        if (o.awvalid) { mix(o.aw.address); mix(o.aw.len | o.aw.size << 8 | o.aw.burst << 16); }
        if (o.wvalid) {
            u128 data = 0, m = 0;
            if (reference.w) row_value(reference.w->row, data, m);
            mix(axi4::lo64(o.wdata & m)); mix(axi4::hi64(o.wdata & m)); mix(uint64_t(o.wlast) | uint64_t(o.wstrb) << 1);
        }
    }
    void checkpoint() {
        const RtlFingerprints& r = rtl_fingerprints();
        auto it = r.at.find(seed);
        if (it == r.at.end() || !it->second.count(cycle)) { fails.fail("no RTL fingerprint at cycle " + std::to_string(cycle)); return; }
        checkpoints++;
        if (it->second.at(cycle) != fp) fails.fail("the outputs differ from the RTL's before cycle " + std::to_string(cycle) + " (and not before the checkpoint before)");
    }

    // One cycle: `access` and `port`, the router's ready at its rate and the controller's outputs. Checks every output
    // against the reference, then takes the edge.
    Outputs step(bool rst, Acc access, PortWrite port) {
        const bool ready = random.below(256) < ready_rate;
        const LpddrIn axi = lpddr.drive(cycle, random);
        if (rst) { access = NO_ACCESS; port = NO_PORT; }
        if (port.some && owned) {
            const uint32_t row = (port.address >> 4) % config().rows();
            if ((row - owned_first) % config().rows() < owned_rows) fails.fail("the bench wrote a row a write reads");
        }
        const Inputs inputs{rst, access, port, ready, axi};
        const Outputs o = dut.outputs(inputs);
        if (!rst) {
            reference.check(o, inputs, cycle);
            count(inputs, o);
            fingerprint(o, inputs);
        }
        mon_ar.sample(rst, o.arvalid, axi.arready, o.ar, axi.rvalid && axi.rlast);
        mon_aw.sample(rst, o.awvalid, axi.awready, o.aw, axi.bvalid);
        mon_w.sample(rst, o.wvalid, axi.wready, WBeat{o.wdata, o.wlast}, false);
        mon_packet.sample(rst, o.packet_valid, ready, o.packet, false);
        dut.edge(inputs);
        // whether the W beat, the R beat and the packet taken hold defined data alone: an unwritten buffer row's words are
        // undefined (18), and what is undefined is left out of the fingerprint, on the RTL's side as here
        bool w_known = true;
        if (reference.w) { u128 data, m; row_value(reference.w->row, data, m); w_known = m == ~u128(0); }
        const bool r_known = lpddr.reads.empty() || !lpddr.unknown.count(lpddr.reads.front().row);
        const size_t held[2] = {reference.queues[0].size(), reference.queues[1].size()};
        lpddr.advance(rst, axi, o.lpddr, cycle, random, w_known);
        if (const std::optional<Packet> p = reference.step(inputs)) sent.push_back(*p);
        if (rst) beat_known.clear();
        else {
            const bool reply = access.some && access.block == RW && access.write && access.word == RW_REPLY_SEND && held[1] == 0;
            if (held[0] + axi.rvalid > reference.queues[0].size()) { offered_known = beat_known.front(); beat_known.pop_front(); }
            if (held[1] + reply > reference.queues[1].size()) offered_known = true;
            if (axi.rvalid) beat_known.push_back(r_known);
        }
        if (!reference.busy) owned = false;
        cycle++;
        if (cycle % CHECKPOINT == 0) checkpoint();
        return o;
    }
    void count(const Inputs& i, const Outputs& o) {
        const Reference& r = reference;
        if (r.job && r.job->kind == KIND_READ && r.job->raise_left != 0 && (!r.ar || i.axi.arready) && !r.raises(i.axi)) counts.raise_held_for_room++;
        counts.rows_read_again += r.pending && !r.takes(i.axi);
        if (o.wvalid && i.axi.wready && lpddr.write_bursts.empty() && !(o.awvalid && i.axi.awready)) counts.write_data_before_address++;
        counts.errors += (i.axi.rvalid && i.axi.rresp != axi4::OKAY) || (i.axi.bvalid && i.axi.bresp != axi4::OKAY);
    }
    Outputs cyc(Acc access) { return step(false, access, NO_PORT); }
    Outputs idle() { return cyc(NO_ACCESS); }
    uint32_t ld(uint8_t block, uint8_t word) { return cyc(load(block, word)).rdata; }
    Outputs st(uint8_t block, uint8_t word, uint32_t wdata) { return cyc(store(block, word, wdata)); }
    // A write on the write port at `offset` in the region.
    Outputs port(uint32_t offset, unsigned size, u128 data) { return step(false, NO_ACCESS, PortWrite{true, 0x04000000 | offset, size, data}); }
    // Cycles until the job and the DMA's transfer have ended and LPDDR is idle; returns the cycles taken.
    uint32_t finish() {
        for (uint32_t cycles = 0; cycles < 200000; cycles++) {
            if (!reference.busy && !reference.dma_busy() && lpddr.idle()) return cycles;
            idle();
        }
        fails.fail("a job did not end in 200000 cycles");
        return 200000;
    }
    std::vector<u128> lpddr_rows(uint64_t beat, uint32_t beats) const {
        std::vector<u128> v;
        for (uint64_t i = 0; i < beats; i++) v.push_back(lpddr.row(beat + i));
        return v;
    }

    // ---- The program's steps, the memory tile's core's ----
    void mask_all() { st(CONTROL, W_EVENTS_MASK, DONE | DMA_DONE); }
    void dma_setup(uint32_t queue, uint32_t count, uint32_t counter, uint32_t dst, uint32_t address) {
        st(DMA, D_HDR, dma_hdr(counter, MODE_WRITE, dst, count, queue));
        st(DMA, D_DEST_ADDR, address);
    }
    void job_setup(uint32_t kind, uint32_t beats, uint64_t lpddr_beat, uint32_t buffer) {
        st(RW, RW_CONFIG, rw_config(kind, beats));
        st(RW, RW_LPDDR_ADDR, uint32_t(lpddr_beat));
        st(RW, RW_BUFFER_ADDR, buffer);
    }
    // The packets a read's answer sends (17.7, 11.4): 128-bit WRITEs of the rows, to `dst` at `address` and up by 16.
    std::vector<Packet> read_packets(const std::vector<u128>& rows, uint32_t counter, uint32_t dst, uint32_t address) const {
        std::vector<Packet> v;
        for (size_t k = 0; k < rows.size(); k++) v.push_back(Packet{dst, TILE, MODE_WRITE, counter, SIZE_128, address + 16 * uint32_t(k), rows[k]});
        return v;
    }
    void count_job(uint32_t beats, uint64_t lpddr_beat) {
        counts.empty_jobs += beats == 0;
        counts.four_kib_crossed += beats > 1 && lpddr_beat / 256 != (lpddr_beat + beats - 1) / 256;
    }
    bool sent_from(size_t before, const std::vector<Packet>& expected) const {
        return sent.size() == before + expected.size() && std::equal(expected.begin(), expected.end(), sent.begin() + long(before));
    }
    // A read job of `beats` from `lpddr_beat` with its transfer, DMA_START before or after START (`dma_first`), to the
    // end; checks its packets.
    void read_job(uint32_t beats, uint64_t lpddr_beat, bool dma_first) {
        const uint32_t dst = random.below(TILE_ID_MASK);
        const uint32_t counter = 1 + random.below(7);
        const uint32_t address = random.next() & ~15u;
        const std::vector<u128> rows = lpddr_rows(lpddr_beat, beats);
        const std::vector<Packet> expected = read_packets(rows, counter, dst, address);
        const uint32_t buffer = random.below(config().rows());
        job_setup(KIND_READ, beats, lpddr_beat, buffer);
        dma_setup(0, beats, counter, dst, address);
        const size_t sent_before = sent.size();
        if (dma_first) { st(DMA, D_START, 1); gap(); st(CONTROL, W_START, 1); }
        else { st(CONTROL, W_START, 1); gap(); st(DMA, D_START, 1); }
        store_during_job();
        finish();
        if (!sent_from(sent_before, expected)) fails.fail("a read of " + std::to_string(beats) + " beats: its packets");
        counts.reads++;
        count_job(beats, lpddr_beat);
    }
    u128 random_row() {
        const u128 a = random.next(), b = random.next(), c = random.next(), d = random.next();
        return a << 96 | b << 64 | c << 32 | d;
    }
    // Fills buffer rows `first` up with random words by the write port's three sizes, junk above each payload's size.
    std::vector<u128> fill_rows(uint32_t first, uint32_t rows) {
        std::vector<u128> values;
        for (uint32_t index = 0; index < rows; index++) {
            const uint32_t row = (first + index) % config().rows();
            const u128 value = random_row();
            const u128 junk = random_row();
            switch (random.below(3)) {
                case 0: port(row << 4, SIZE_128, value); break;
                case 1:
                    for (uint32_t half = 0; half < 2; half++)
                        port(row << 4 | half << 3, SIZE_64, (value >> (64 * half) & u128(UINT64_MAX)) | junk >> 64 << 64);
                    break;
                default:
                    for (uint32_t lane = 0; lane < 4; lane++)
                        port(row << 4 | lane << 2, SIZE_32, (value >> (32 * lane) & 0xFFFFFFFF) | junk >> 32 << 32);
                    break;
            }
            values.push_back(value);
        }
        return values;
    }
    // A write job of `beats` from buffer row `buffer` to `lpddr_beat`, its rows filled first, to the end; checks LPDDR.
    void write_job(uint32_t beats, uint64_t lpddr_beat, uint32_t buffer) {
        const std::vector<u128> values = fill_rows(buffer, beats);
        job_setup(KIND_WRITE, beats, lpddr_beat, buffer);
        st(CONTROL, W_START, 1);
        owned = true; owned_first = buffer; owned_rows = beats;
        store_during_job();
        finish();
        if (lpddr_rows(lpddr_beat, beats) != values) fails.fail("a write of " + std::to_string(beats) + " beats: LPDDR's rows");
        counts.writes++;
        count_job(beats, lpddr_beat);
    }
    // A reply (17.7): a store of `w0` to RW_REPLY_W0, or none, then of `w1` to RW_REPLY_SEND, and its transfer from
    // queue 1, of count 1; checks its one packet, 128 bits, its value {w1, w0} in its low 64 bits, w0 0 without its
    // store, and its bits [127:64] 0.
    void reply(std::optional<uint32_t> w0, uint32_t w1, uint32_t counter, uint32_t dst, uint32_t address) {
        if (w0) st(RW, RW_REPLY_W0, *w0);
        st(RW, RW_REPLY_SEND, w1);
        dma_setup(1, 1, counter, dst, address);
        const size_t sent_before = sent.size();
        st(DMA, D_START, 1);
        finish();
        const u128 payload = u128(w1) << 32 | u128(w0.value_or(0));
        if (!sent_from(sent_before, {Packet{dst, TILE, MODE_WRITE, counter, SIZE_128, address, payload}})) fails.fail("the reply's packet");
        counts.replies++;
    }
    // Stores to RW_CONFIG, RW_LPDDR_ADDR and RW_BUFFER_ADDR during a job, at random: the job took them at its START.
    void store_during_job() {
        if (random.one_in(2)) {
            const uint32_t values[3] = {random.next(), random.next(), random.next()};
            const uint8_t words[3] = {RW_CONFIG, RW_LPDDR_ADDR, RW_BUFFER_ADDR};
            for (unsigned k = 0; k < 3; k++) st(RW, words[k], values[k]);
        }
    }
    void gap() { for (uint32_t n = random.below(4); n > 0; n--) idle(); }

    // The bench's end: no failure of its own checks, the monitors' records, the slave's checks, no flag of the model's,
    // and the RTL's fingerprint of the whole run.
    bool ok() {
        if (!(mon_ar.ok() && mon_aw.ok() && mon_w.ok() && mon_packet.ok())) fails.fail("a handshake rule broken on AR, AW, W or the packet interface");
        if (lpddr.failures) fails.fail("the controller: " + lpddr.first_failure);
        if (dut.s.violation_total() != 0) fails.fail("the model flagged " + std::to_string(dut.s.violation_total()) + " violations");
        const RtlFingerprints& r = rtl_fingerprints();
        auto it = r.end.find(seed);
        if (it == r.end.end()) fails.fail("tests/rwengine/unit.fp has no line for this bench");
        else if (it->second.first != cycle) fails.fail("the bench ran " + std::to_string(cycle) + " cycles, the RTL's " + std::to_string(it->second.first));
        else if (it->second.second != fp) fails.fail("the outputs differ from the RTL's (the fingerprint at the end)");
        if (fails.n) std::printf("test_rw_engine: bench 0x%x: %u failures, the first: %s\n", seed, fails.n, fails.first.c_str());
        else std::printf("test_rw_engine: bench 0x%x: %llu cycles, %zu packets, every output the reference's, the fingerprint the RTL's (%u checkpoints)\n",
                         seed, (unsigned long long)cycle, sent.size(), checkpoints);
        return fails.n == 0;
    }
};

std::vector<AddressBeat> burst_list_of(const axi4::UnitLpddr& l) {
    std::vector<AddressBeat> v;
    for (const auto& x : l.bursts) v.push_back(x.second);
    return v;
}
std::vector<AddressBeat> burst_list(const Bench& b) { return burst_list_of(b.lpddr); }

}  // namespace

// ================================================================ the directed tests (rw_engine_unit_test.rs's)

// The registers (17.1, 17.7, 5.4): PARAMS type 2, version 0, no constants; after the reset STATUS, ERRORS, EVENTS and
// EVENTS_MASK 0 and nothing on the lpddr port or the packet interface; RW_CONFIG's kind and beats, RW_LPDDR_ADDR's
// LPDDR_ADDRESS_BITS - 4 bits and RW_BUFFER_ADDR's log2 RW_BUFFER_BYTES - 4 bits, each kept, the bits above reading 0; a
// store's effect seen by the next load; the DMA's block reached; a START with bit 0 clear starting nothing.
TEST(rw_engine_registers) {
    const struct { Config config; uint32_t seed; } runs[] = {{DEFAULTS, 0x5EED0701}, {LEAST, 0x5EED0702}, {MOST, 0x5EED0703}};
    for (const auto& run : runs) {
        const Config config = run.config;
        Bench bench(config, axi4::PROMPT, run.seed);
        const Outputs o = bench.idle();
        CHECK(!o.arvalid && !o.awvalid && !o.wvalid && !o.packet_valid);
        CHECK(!o.event && !o.error);
        CHECK_EQ(bench.ld(CONTROL, W_PARAMS), EXPECTED_PARAMS);
        for (uint8_t word : {W_STATUS, W_ERRORS, W_EVENTS, W_EVENTS_PEEK, W_EVENTS_MASK}) CHECK_EQ(bench.ld(CONTROL, word), 0u);
        const uint64_t beats_mask = config.lpddr_beats() - 1;
        const uint32_t lpddr_mask = beats_mask > UINT32_MAX ? UINT32_MAX : uint32_t(beats_mask);
        const uint32_t row_mask = config.rows() - 1;
        for (uint32_t wdata : {0xFFFFFFFFu, 0u, 0x000007FFu, 0x00000001u, 0x000007FEu, 0xA5A55A5Au}) {
            bench.st(RW, RW_CONFIG, wdata);
            bench.st(RW, RW_LPDDR_ADDR, wdata);
            bench.st(RW, RW_BUFFER_ADDR, wdata);
            CHECK_EQ(bench.ld(RW, RW_CONFIG), wdata & 0x7FF);
            CHECK_EQ(bench.ld(RW, RW_LPDDR_ADDR), wdata & lpddr_mask);
            CHECK_EQ(bench.ld(RW, RW_BUFFER_ADDR), wdata & row_mask);
        }
        // Back to back: each store seen by the next load.
        bench.st(RW, RW_CONFIG, rw_config(KIND_WRITE, 3));
        CHECK_EQ(bench.ld(RW, RW_CONFIG), 7u);
        bench.st(DMA, D_DEST_ADDR, 0x12345670);
        CHECK_EQ(bench.ld(DMA, D_DEST_ADDR), 0x12345670u);
        bench.st(DMA, D_HDR, dma_hdr(3, 0, 5, 12, 1));
        CHECK_EQ(bench.ld(DMA, D_HDR), dma_hdr(3, 0, 5, 12, 1));
        // EVENTS_MASK keeps its two bits.
        bench.st(CONTROL, W_EVENTS_MASK, 0xFFFFFFFF);
        CHECK_EQ(bench.ld(CONTROL, W_EVENTS_MASK), 3u);
        // A START with bit 0 clear starts nothing.
        bench.job_setup(KIND_READ, 1, 0, 0);
        bench.st(CONTROL, W_START, 0xFFFFFFFE);
        CHECK_EQ(bench.ld(CONTROL, W_STATUS), 0u);
        CHECK(!bench.idle().arvalid);
        CHECK(bench.ok());
    }
}

// A read's timing (17.7, the README), LPDDR prompt and the router ready: START presented in cycle p, busy from p + 1,
// the first burst raised at the end of p + 1, ARVALID from p + 2, each burst to the end of its 4-beat group; the packets
// out; done and dma_done posted. A read of 0 beats: busy in p + 1 alone, done posted, no burst.
TEST(rw_engine_read_timing) {
    Bench bench(DEFAULTS, axi4::PROMPT, 0x5EED0704);
    bench.mask_all();
    // 6 beats from beat 0x102: bursts of 2 (0x102-0x103) and 4 (0x104-0x107).
    bench.job_setup(KIND_READ, 6, 0x102, 0);
    bench.dma_setup(0, 6, 3, 5, 0x00001000);
    bench.st(DMA, D_START, 1);
    Outputs o = bench.st(CONTROL, W_START, 1);
    const uint64_t p = bench.cycle - 1;
    CHECK(!o.arvalid);                                          // p: no burst
    CHECK_EQ(bench.ld(CONTROL, W_STATUS) & BUSY, BUSY);           // p + 1: busy
    o = bench.idle();
    CHECK(o.arvalid);                                           // p + 2: the first burst
    CHECK(o.ar == address_beat(0x102, 2));
    o = bench.idle();
    CHECK(o.ar == address_beat(0x104, 4));                      // p + 3: the next burst, the first accepted
    // by hand, from the README's timing with the controller prompt (a burst accepted in c answers from c + 1, a beat a
    // cycle): the bursts are accepted in p + 2 and p + 3, the six beats come in p + 3 .. p + 8, each is in the queue
    // from the next cycle and taken by the DMA then, so the packets are offered in p + 5 .. p + 10 and the read ends
    // in p + 8, its done posted at that cycle's end, and the transfer in p + 10
    const uint32_t cycles = bench.finish();
    CHECK(cycles < 20);
    CHECK_EQ(bench.cycle - 1, p + 10);                          // the transfer's last cycle: its last packet leaves
    CHECK(burst_list(bench) == (std::vector<AddressBeat>{address_beat(0x102, 2), address_beat(0x104, 4)}));
    CHECK_EQ(bench.ld(CONTROL, W_EVENTS), DONE | DMA_DONE);
    CHECK_EQ(bench.ld(CONTROL, W_EVENTS), 0u);                    // the load consumed them
    CHECK(bench.sent == bench.read_packets(bench.lpddr_rows(0x102, 6), 3, 5, 0x1000));

    // 0 beats: busy for the cycle after the start alone, done posted at its end.
    bench.job_setup(KIND_READ, 0, 0x55, 0);
    bench.st(CONTROL, W_START, 1);
    CHECK_EQ(bench.ld(CONTROL, W_STATUS) & BUSY, BUSY);
    CHECK_EQ(bench.ld(CONTROL, W_STATUS) & BUSY, 0u);
    CHECK_EQ(bench.ld(CONTROL, W_EVENTS), DONE);
    CHECK_EQ(bench.lpddr.bursts.size(), size_t(2));             // a read of 0 beats raised no burst
    CHECK(bench.ok());
}

// The same read's cycles one by one, from the RTL unit's source and README, with the controller prompt and the router
// ready: STATUS.busy, EVENTS.done, STATUS.dma_busy and EVENTS.dma_done in every cycle (EVENTS_PEEK, EVENTS_MASK 0).
TEST(rw_engine_read_cycles_by_hand) {
    Bench bench(DEFAULTS, axi4::PROMPT, 0x5EED0704);            // the read timing's bench again, read otherwise
    bench.mask_all();
    bench.job_setup(KIND_READ, 6, 0x102, 0);
    bench.dma_setup(0, 6, 3, 5, 0x00001000);
    bench.st(DMA, D_START, 1);
    bench.st(CONTROL, W_START, 1);                                // p
    // loads in p + 1 .. p + 12, STATUS and EVENTS_PEEK in turn
    uint32_t status[13] = {}, events[13] = {};
    for (unsigned k = 1; k <= 12; k++) {
        if (k % 2) status[k] = bench.ld(CONTROL, W_STATUS); else events[k] = bench.ld(CONTROL, W_EVENTS_PEEK);
    }
    // busy through p + 8, the cycle the last beat comes (a load reads the end of the cycle before): 1 in p + 1 .. p + 8
    CHECK_EQ(status[1], 3u); CHECK_EQ(status[3], 3u); CHECK_EQ(status[5], 3u); CHECK_EQ(status[7], 3u);
    CHECK_EQ(status[9], 2u);                                    // p + 9: the read has ended, the transfer runs on
    CHECK_EQ(status[11], 0u);                                   // p + 11: the transfer ended in p + 10
    CHECK_EQ(events[2], 0u); CHECK_EQ(events[4], 0u); CHECK_EQ(events[6], 0u); CHECK_EQ(events[8], 0u);
    CHECK_EQ(events[10], DONE);                                 // done posted at the end of p + 8
    CHECK_EQ(events[12], DONE | DMA_DONE);                      // dma_done at the end of p + 10
    CHECK(bench.fails.n == 0);
}

// A read raises a burst only with room in queue 0 for every beat in flight, and ends only with its last beat in the
// queue (17.7): with the router never ready, a read of 40 beats from beat 0x3FE raises 2 + 4 + 4 + 4 beats, as many
// whole bursts as fit in the queue's 16 beats and the packet register's one, and stays busy; the router ready again,
// the DMA drains the queue and the read ends, every row sent in order. Then the DMA's transfer started 50 cycles after
// the read's START, and a read across two 4 KiB boundaries.
TEST(rw_engine_read_room) {
    Bench bench(DEFAULTS, axi4::PROMPT, 0x5EED0705);
    bench.job_setup(KIND_READ, 40, 0x3FE, 0);
    bench.dma_setup(0, 40, 1, 2, 0x00002000);
    bench.st(CONTROL, W_START, 1);
    bench.st(DMA, D_START, 1);
    bench.ready_rate = 0;
    for (int k = 0; k < 200; k++) bench.idle();
    uint32_t raised = 0;
    for (const auto& b : bench.lpddr.bursts) raised += b.second.len + 1;
    CHECK_EQ(raised, 14u);                                      // beats raised with the router stalled
    CHECK_EQ(bench.ld(CONTROL, W_STATUS) & BUSY, BUSY);           // the read waits for the queue
    bench.ready_rate = 256;
    bench.finish();
    CHECK(bench.sent == bench.read_packets(bench.lpddr_rows(0x3FE, 40), 1, 2, 0x2000));

    const size_t sent_before = bench.sent.size();
    bench.job_setup(KIND_READ, 300, 0xFF0, 0);
    bench.dma_setup(0, 300, 4, 3, 0x00100000);
    bench.st(CONTROL, W_START, 1);
    for (int k = 0; k < 50; k++) bench.idle();
    bench.st(DMA, D_START, 1);
    bench.finish();
    CHECK(bench.sent_from(sent_before, bench.read_packets(bench.lpddr_rows(0xFF0, 300), 4, 3, 0x00100000)));
    // no burst crossed a 4 KiB boundary (3, 17.7): 0xFF0 .. 0x11FB crosses 0x1000 and 0x1100
    for (const AddressBeat& b : burst_list(bench)) CHECK(b.address / 4096 == (b.address + 16 * b.len) / 4096);
    CHECK(bench.ok());
}

// A write's timing (17.7, the README), LPDDR prompt: START presented in cycle p, AW from p + 2, each burst to the end
// of its 4-beat group, the first beat offered in p + 3 and a beat a cycle after it, WLAST at each burst's last beat; the
// write's end with its last answer, done posted; LPDDR holding the rows, read back. A write of 0 beats: busy in p + 1
// alone.
TEST(rw_engine_write_timing) {
    Bench bench(DEFAULTS, axi4::PROMPT, 0x5EED0706);
    bench.mask_all();
    const std::vector<u128> values = bench.fill_rows(0x20, 7);
    bench.job_setup(KIND_WRITE, 7, 0x8001, 0x20);
    Outputs o = bench.st(CONTROL, W_START, 1);
    const uint64_t p = bench.cycle - 1;
    CHECK(!o.awvalid && !o.wvalid);                             // p: nothing raised
    o = bench.idle();
    CHECK(!o.awvalid && !o.wvalid);                             // p + 1: nothing raised
    o = bench.idle();
    CHECK(o.awvalid && !o.wvalid);                              // p + 2: AW alone
    CHECK(o.aw == address_beat(0x8001, 3));
    o = bench.idle();
    CHECK(o.wvalid);                                            // p + 3: the first beat
    CHECK(o.wdata == values[0]);
    CHECK(o.aw == address_beat(0x8004, 4));
    std::vector<bool> lasts = {o.wlast};
    for (size_t index = 1; index < values.size(); index++) {
        o = bench.idle();
        CHECK(o.wvalid);                                        // a beat a cycle after the last
        CHECK(o.wdata == values[index]);
        lasts.push_back(o.wlast);
    }
    CHECK(lasts == (std::vector<bool>{false, false, true, false, false, false, true}));
    // by hand: the seventh beat is taken in p + 9, so the second burst, whose address was taken in p + 3, is complete at
    // that edge and the prompt controller answers it in p + 10: the write ends there
    bench.finish();
    CHECK_EQ(bench.cycle - 1, p + 10);
    CHECK(bench.lpddr_rows(0x8001, 7) == values);
    CHECK_EQ(bench.ld(CONTROL, W_EVENTS), DONE);
    bench.read_job(7, 0x8001, false);
    CHECK_EQ(bench.ld(CONTROL, W_EVENTS), DONE | DMA_DONE);       // the read and its transfer

    // 0 beats.
    bench.job_setup(KIND_WRITE, 0, 0x8001, 0x20);
    bench.st(CONTROL, W_START, 1);
    CHECK_EQ(bench.ld(CONTROL, W_STATUS) & BUSY, BUSY);
    CHECK_EQ(bench.ld(CONTROL, W_STATUS) & BUSY, 0u);
    CHECK_EQ(bench.ld(CONTROL, W_EVENTS), DONE);
    CHECK(bench.ok());
}

// The write port (17.3, 17.7): 32-bit writes at each word of a row, 64-bit writes at each half, 128-bit writes, the bits
// above a payload's size ignored, a write of the reserved size writing nothing; each lands by the next cycle (a write's
// START in the cycle after a port write sends its row). Each row seen in LPDDR after a write job.
TEST(rw_engine_write_port) {
    Bench bench(DEFAULTS, axi4::SLOW, 0x5EED0707);
    const uint32_t base = 0x2C0;
    const uint32_t words[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    for (uint32_t lane = 0; lane < 4; lane++) bench.port(base << 4 | lane << 2, SIZE_32, u128(words[lane]) | u128(0xDEAD) << 64);
    bench.port((base + 1) << 4, SIZE_64, u128(0x5555555566666666ull) | u128(0xBEEF) << 100);
    bench.port((base + 1) << 4 | 8, SIZE_64, 0x7777777788888888ull);
    bench.port((base + 2) << 4, SIZE_128, axi4::make128(0x0102030405060708ull, 0x090A0B0C0D0E0F10ull));
    bench.port((base + 2) << 4 | 4, SIZE_32, 0x99999999);
    bench.port((base + 2) << 4 | 8, SIZE_64, 0xAAAAAAAABBBBBBBBull);
    bench.port((base + 3) << 4, SIZE_128, axi4::make128(0xCCCCCCCCDDDDDDDDull, 0xEEEEEEEEFFFFFFFFull));
    bench.dut.e->violations[RwEngine::V_WRITE_SIZE] = 0;
    bench.port((base + 3) << 4, 3, 0x1234);
    // the reserved size is UB (4.1): the model writes nothing, as the RTL, and flags it, once
    CHECK_EQ(bench.dut.e->violations[RwEngine::V_WRITE_SIZE], uint64_t(1));
    CHECK_EQ(bench.dut.s.violation_total(), uint64_t(1));
    bench.dut.e->n_violations = 0;
    bench.job_setup(KIND_WRITE, 4, 0x40, base);
    bench.st(CONTROL, W_START, 1);
    bench.owned = true; bench.owned_first = base; bench.owned_rows = 4;
    bench.finish();
    CHECK(bench.lpddr_rows(0x40, 4) == (std::vector<u128>{
        axi4::make128(0x4444444433333333ull, 0x2222222211111111ull),
        axi4::make128(0x7777777788888888ull, 0x5555555566666666ull),
        axi4::make128(0xAAAAAAAABBBBBBBBull, 0x999999990D0E0F10ull),
        axi4::make128(0xCCCCCCCCDDDDDDDDull, 0xEEEEEEEEFFFFFFFFull)}));
    // A write's START in the cycle after a port write: the row is in place.
    bench.job_setup(KIND_WRITE, 1, 0x80, 0x10);
    bench.port(0x10 << 4, SIZE_128, 0xFACE);
    bench.st(CONTROL, W_START, 1);
    bench.owned = true; bench.owned_first = 0x10; bench.owned_rows = 1;
    bench.finish();
    CHECK(bench.lpddr_rows(0x80, 1) == std::vector<u128>{0xFACE});
    CHECK(bench.ok());
}

// The reply (17.7, 11.4), one 128-bit packet: a 32-bit value, RW_REPLY_W0 with a RW_REPLY_SEND of 0; a 64-bit one,
// RW_REPLY_SEND its upper word; a RW_REPLY_SEND alone after a send, its RW_REPLY_W0 cleared by the send before (0 after
// the reset too); the bits above 64 0 whatever is stored. A reply after a read's data, as an answer, leaves after every
// packet of the data.
TEST(rw_engine_reply) {
    Bench bench(DEFAULTS, axi4::PROMPT, 0x5EED0708);
    bench.reply({}, 0x13579BDF, 3, 2, 0x00000EF0);              // RW_REPLY_W0 0 after the reset
    bench.reply(0xCAFEF00Du, 0, 6, 4, 0x00000F00);
    bench.reply(0x80000042u, 0x00000001, 2, 5, 0x00000F10);
    bench.reply({}, 0xFFFFFFFF, 1, 3, 0x00000F20);              // cleared by the send before
    CHECK_EQ(bench.sent.size(), size_t(4));
    CHECK(bench.sent[0].payload == u128(0x13579BDF) << 32);
    CHECK(bench.sent[1].payload == u128(0xCAFEF00D));
    CHECK(bench.sent[2].payload == (u128(1) << 32 | 0x80000042u));
    CHECK(bench.sent[3].payload == u128(0xFFFFFFFF) << 32);
    bench.read_job(9, 0x777, true);
    const size_t data_end = bench.sent.size();
    bench.reply({}, 0, 7, 6, 0x00000F30);
    CHECK_EQ(bench.sent.size(), data_end + 1);
    CHECK(bench.ok());
}

// LPDDR errors (17.7): a read beat not OKAY sets ERRORS.lpddr_error, the beat delivered all the same; it stays set
// (sticky) through a later OKAY read, and the tile's error bit with it; the reset clears it. A write response not OKAY
// sets it likewise, and the write completes.
TEST(rw_engine_lpddr_error) {
    Bench bench(DEFAULTS, axi4::PROMPT, 0x5EED0709);
    bench.lpddr.timing.error_period = 1;
    bench.read_job(3, 0x10, false);
    CHECK_EQ(bench.ld(CONTROL, W_ERRORS), LPDDR_ERROR);
    CHECK(bench.idle().error);
    bench.lpddr.timing.error_period = 0;
    bench.read_job(3, 0x20, false);
    CHECK_EQ(bench.ld(CONTROL, W_ERRORS), LPDDR_ERROR);           // sticky
    bench.reset();
    CHECK_EQ(bench.ld(CONTROL, W_ERRORS), 0u);                    // the reset clears it
    bench.write_job(5, 0x30, 0);
    CHECK_EQ(bench.ld(CONTROL, W_ERRORS), 0u);                    // OKAY writes
    bench.lpddr.timing.error_period = 1;
    bench.write_job(5, 0x40, 8);
    CHECK_EQ(bench.ld(CONTROL, W_ERRORS), LPDDR_ERROR);           // a write response not OKAY
    CHECK(bench.ok());
}

// The reset (17.7, 18): during a read with bursts in flight and beats queued, and during a write with beats in flight,
// it ends the job, empties the queues and the channels, and zeroes STATUS, ERRORS, EVENTS and EVENTS_MASK, and
// RW_REPLY_W0; the controller drops what it held. The buffer keeps its rows: a write after the reset sends them.
TEST(rw_engine_reset) {
    Bench bench(DEFAULTS, axi4::SLOW, 0x5EED070A);
    bench.ready_rate = 64;
    bench.mask_all();
    bench.job_setup(KIND_READ, 100, 0x123, 0);
    bench.dma_setup(0, 100, 1, 1, 0);
    bench.st(CONTROL, W_START, 1);
    bench.st(DMA, D_START, 1);
    for (int k = 0; k < 60; k++) bench.idle();
    CHECK(bench.reference.busy && !bench.reference.queues[0].empty());
    bench.reset();
    for (uint8_t word : {W_STATUS, W_ERRORS, W_EVENTS, W_EVENTS_MASK}) CHECK_EQ(bench.ld(CONTROL, word), 0u);
    const std::vector<u128> values = bench.fill_rows(0x40, 30);
    bench.job_setup(KIND_WRITE, 30, 0x5000, 0x40);
    bench.st(CONTROL, W_START, 1);
    bench.owned = true; bench.owned_first = 0x40; bench.owned_rows = 30;
    for (int k = 0; k < 20; k++) bench.idle();
    CHECK(bench.reference.busy);
    bench.reset();
    for (int k = 0; k < 50; k++) {
        const Outputs o = bench.idle();
        CHECK(!o.arvalid && !o.awvalid && !o.wvalid && !o.packet_valid);
    }
    bench.job_setup(KIND_WRITE, 30, 0x6000, 0x40);
    bench.st(CONTROL, W_START, 1);
    bench.owned = true; bench.owned_first = 0x40; bench.owned_rows = 30;
    bench.finish();
    CHECK(bench.lpddr_rows(0x6000, 30) == values);              // the buffer kept its rows
    // RW_REPLY_W0 stored, then the reset: a RW_REPLY_SEND alone sends it 0.
    bench.st(RW, RW_REPLY_W0, 0xDEADBEEF);
    bench.reset();
    bench.reply({}, 0x02468ACE, 5, 2, 0x00000FF0);
    CHECK(bench.ok());
}

// Random jobs at each configuration: reads and writes of random lengths at random addresses (across 4 KiB boundaries
// and at LPDDR's top, 0 beats among them), their transfers before or after their STARTs, replies, port writes beside a
// job into rows it does not read, loads of every register, a router stalling at random, LPDDR at random timing with
// errors, and resets in the middle of jobs; every case counted as met.
void random_jobs(const Config& config, uint32_t seed, uint32_t jobs) {
    Bench bench(config, axi4::Timing{160, 160, 12, 97}, seed);
    bench.mask_all();
    Random& random = bench.random;
    for (uint32_t job = 0; job < jobs && bench.fails.n == 0; job++) {
        static const uint32_t ready_rates[4] = {256, 200, 64, 16}, rates[3] = {256, 160, 48};
        bench.ready_rate = ready_rates[random.below(4)];
        bench.lpddr.timing.ready = rates[random.below(3)];
        bench.lpddr.timing.valid = rates[random.below(3)];
        const uint32_t kind = random.below(8);
        uint32_t beats;
        switch (random.below(5)) {
            case 0: beats = 0; break;
            case 1: beats = 1 + random.below(4); break;
            case 2: beats = 1 + random.below(32); break;
            case 3: beats = 1 + random.below(300); break;
            default: beats = 1 + random.below(1023); break;
        }
        if (kind >= 3 && beats > config.rows() - 8) beats = config.rows() - 8;
        const uint64_t top = config.lpddr_beats() - beats;
        const uint64_t wide_hi = random.next();
        const uint64_t wide = wide_hi << 32 | random.next();
        uint64_t lpddr;
        switch (random.below(3)) {
            case 0: lpddr = top; break;
            case 1: {
                const uint64_t page = wide % (config.lpddr_beats() / 256);
                const uint64_t back = random.below(8);
                const uint64_t at = page * 256 > back ? page * 256 - back : 0;
                lpddr = at < top ? at : top;
                break;
            }
            default: lpddr = wide % (top + 1); break;
        }
        if (kind <= 2) {
            const bool dma_first = random.one_in(2);
            bench.read_job(beats, lpddr, dma_first);
        } else if (kind <= 5) {
            const uint32_t buffer = random.below(config.rows() - beats + 1);
            bench.write_job(beats, lpddr, buffer);
        } else if (kind == 6) {
            std::optional<uint32_t> w0;
            if (random.one_in(4)) w0 = random.next();
            const uint32_t w1 = random.one_in(2) ? 0 : random.next();
            const uint32_t counter = random.below(8);
            const uint32_t dst = random.below(TILE_ID_MASK);
            const uint32_t address = random.next() & ~15u;
            bench.reply(w0, w1, counter, dst, address);
        } else {
            // A job cut by the reset, with port writes into rows it does not read beside it.
            const uint32_t job_kind = random.below(2);
            bench.job_setup(job_kind, beats, lpddr, 0);
            bench.dma_setup(0, beats, 1, 1, 0);
            bench.st(CONTROL, W_START, 1);
            if (job_kind == KIND_READ) bench.st(DMA, D_START, 1);
            else { bench.owned = true; bench.owned_first = 0; bench.owned_rows = beats; }
            for (uint32_t n = random.below(80); n > 0; n--) {
                static const uint8_t words[5] = {W_PARAMS, W_STATUS, W_ERRORS, W_EVENTS_PEEK, W_EVENTS_MASK};
                const uint8_t word = words[random.below(5)];
                PortWrite port;
                if (random.one_in(3)) {
                    const uint32_t address = 0x04000000 | (config.rows() - 1 - random.below(4)) << 4;
                    port = PortWrite{true, address, SIZE_128, u128(random.next())};
                }
                bench.step(false, load(CONTROL, word), port);
            }
            bench.reset();
            bench.mask_all();
        }
        // Loads of the registers between jobs.
        static const uint8_t loads[9][2] = {{CONTROL, W_PARAMS}, {CONTROL, W_STATUS}, {CONTROL, W_ERRORS}, {CONTROL, W_EVENTS}, {RW, RW_CONFIG},
                                            {RW, RW_LPDDR_ADDR}, {RW, RW_BUFFER_ADDR}, {DMA, D_HDR}, {DMA, D_DEST_ADDR}};
        for (const auto& l : loads) if (random.one_in(3)) bench.cyc(load(l[0], l[1]));
    }
    const Counts& c = bench.counts;
    std::printf("test_rw_engine: random at %u bits, %u bytes, %u beats: %u reads, %u writes, %u empty jobs, %u replies, %u raises held for room, "
                "%u rows read again, %u beats before their address, %u jobs across 4 KiB, %u errors, %u resets in jobs\n",
                config.lpddr_bits, config.buffer_bytes, config.queue_beats, c.reads, c.writes, c.empty_jobs, c.replies, c.raise_held_for_room,
                c.rows_read_again, c.write_data_before_address, c.four_kib_crossed, c.errors, c.resets_in_jobs);
    CHECK(c.reads > 0 && c.writes > 0 && c.empty_jobs > 0 && c.replies > 0);
    CHECK(c.raise_held_for_room > 0 && c.rows_read_again > 0);
    CHECK(c.write_data_before_address > 0 && c.four_kib_crossed > 0);
    CHECK(c.errors > 0 && c.resets_in_jobs > 0);
    CHECK(bench.ok());
}
TEST(rw_engine_random_defaults) { random_jobs(DEFAULTS, 0x5EED070B, 400); }
TEST(rw_engine_random_least) { random_jobs(LEAST, 0x5EED070C, 400); }
TEST(rw_engine_random_most) { random_jobs(MOST, 0x5EED070D, 150); }

// Each configuration check stops the simulator: LPDDR_ADDRESS_BITS outside 24 to 36, RW_BUFFER_BYTES below 4 KiB or not
// a power of two, RW_QUEUE_BEATS below a burst's 4 (rw_engine_unit_rejects_bad_configurations).
DEATH_TEST(rw_engine_rejects_lpddr_bits_23, "LPDDR_ADDRESS_BITS must be 24 to 36") { Dut d(Config{23, 16384, 16}); }
DEATH_TEST(rw_engine_rejects_lpddr_bits_37, "LPDDR_ADDRESS_BITS must be 24 to 36") { Dut d(Config{37, 16384, 16}); }
DEATH_TEST(rw_engine_rejects_a_buffer_of_2048, "RW_BUFFER_BYTES must be a power of two") { Dut d(Config{32, 2048, 16}); }
DEATH_TEST(rw_engine_rejects_a_buffer_of_12288, "RW_BUFFER_BYTES must be a power of two") { Dut d(Config{32, 12288, 16}); }
DEATH_TEST(rw_engine_rejects_a_queue_of_3, "RW_QUEUE_BEATS must be at least a burst's 4 beats") { Dut d(Config{32, 16384, 3}); }

// ================================================================ the simulator's own rules at the port and for UB

// The lpddr port is the model's boundary: with no controller on it, a job of 0 beats runs (it raises nothing), and a job
// that raises a burst stops the simulator, named.
TEST(rw_engine_a_job_of_no_beats_needs_no_controller) {
    Dut d(DEFAULTS, false);
    Inputs i{false, store(RW, RW_CONFIG, rw_config(KIND_READ, 0)), NO_PORT, true, LpddrIn{}};
    d.edge(i);
    i.access = store(CONTROL, W_START, 1);
    d.edge(i);
    i.access = load(CONTROL, W_STATUS);
    CHECK_EQ(d.outputs(i).rdata & BUSY, BUSY);
    d.edge(i);
    CHECK_EQ(d.outputs(i).rdata & BUSY, 0u);
    i.access = load(CONTROL, W_EVENTS_PEEK);
    CHECK_EQ(d.outputs(i).rdata, DONE);
}
DEATH_TEST(rw_engine_a_burst_without_a_controller_stops_the_simulator, "a job raises a burst at 2 with no LPDDR controller on the lpddr port") {
    Dut d(DEFAULTS, false);
    Inputs i{false, store(RW, RW_CONFIG, rw_config(KIND_READ, 1)), NO_PORT, true, LpddrIn{}};
    d.edge(i);                                                  // cycle 0
    i.access = store(CONTROL, W_START, 1);
    d.edge(i);                                                  // p = 1
    i.access = NO_ACCESS;
    d.edge(i);                                                  // p + 1 = 2: the first burst is raised at its end
}
// A START while busy is UB (17.2): the simulator stops.
DEATH_TEST(rw_engine_a_start_while_busy_stops_the_simulator, "START while a job is in progress") {
    Dut d(DEFAULTS);
    Inputs i{false, store(RW, RW_CONFIG, rw_config(KIND_READ, 8)), NO_PORT, true, LpddrIn{}};
    d.edge(i);
    i.access = store(CONTROL, W_START, 1);
    d.edge(i);
    d.edge(i);
}
// A reply pushed into a full queue 1 is UB (17.7): it is lost, RW_REPLY_W0 cleared all the same (the RTL's README), and
// the model flags it. The first reply leaves alone, and the next send after it finds room.
TEST(rw_engine_a_reply_into_a_full_queue_is_lost_and_flagged) {
    Dut d(DEFAULTS);
    std::vector<Packet> sent;
    auto cycle = [&](Acc a) {
        Inputs i{false, a, NO_PORT, true, LpddrIn{}};
        const Outputs o = d.outputs(i);
        if (o.packet_valid) sent.push_back(o.packet);
        d.edge(i);
    };
    cycle(store(RW, RW_REPLY_W0, 0x11));
    cycle(store(RW, RW_REPLY_SEND, 0x22));                      // into queue 1
    cycle(store(RW, RW_REPLY_W0, 0x33));
    cycle(store(RW, RW_REPLY_SEND, 0x44));                      // queue 1 is full: lost
    CHECK_EQ(d.e->violations[RwEngine::V_REPLY_FULL], uint64_t(1));
    cycle(store(DMA, D_HDR, dma_hdr(1, MODE_WRITE, 3, 2, 1)));   // two packets from queue 1
    cycle(store(DMA, D_DEST_ADDR, 0x100));
    cycle(store(DMA, D_START, 1));
    for (int k = 0; k < 4; k++) cycle(NO_ACCESS);
    CHECK_EQ(sent.size(), size_t(1));                           // the first reply alone; the transfer waits
    CHECK(sent[0].payload == (u128(0x22) << 32 | 0x11));
    cycle(store(RW, RW_REPLY_SEND, 0x55));                      // RW_REPLY_W0 was cleared by the lost send
    for (int k = 0; k < 4; k++) cycle(NO_ACCESS);
    CHECK_EQ(sent.size(), size_t(2));
    CHECK(sent[1].payload == u128(0x55) << 32);
    CHECK_EQ(sent[1].address, 0x110u);
}

// ================================================================ in the framework: the engine in a PeriphSlot unit
// The slot behind its ports (periph_slot.h): the core's stores on S2 (Link(1): a store put in c is presented in c), the
// director's region WRITEs on S1 (Link(3): a WRITE the director accepted in a is on the write port in a + 2 and lands
// at its end), the window RegFile, and the DMA's packets on the credit Fifo (a packet the DMA takes in c is put in c
// and is at the router's input in c + 1). The controller is UnitLpddr at its prompt timing, called by the engine.
namespace {

struct PromptSlave final : LpddrSlave {
    axi4::UnitLpddr lpddr{32, axi4::PROMPT};
    Random random{0x5EED0801};
    std::vector<std::pair<int64_t, char>> seen;                 // each handshake's cycle: 'A' AR, 'W' AW, 'D' a W beat
    LpddrIn cycle(int64_t t, const LpddrOut& o) override {
        const LpddrIn in = lpddr.drive(uint64_t(t), random);
        if (o.arvalid && in.arready) seen.emplace_back(t, 'A');
        if (o.awvalid && in.awready) seen.emplace_back(t, 'W');
        if (o.wvalid && in.wready) seen.emplace_back(t, 'D');
        lpddr.advance(false, in, o, uint64_t(t), random);
        return in;
    }
};

struct Stand {
    enum : uint8_t { STORE, REGION, READ };
    struct Ev { uint64_t t; uint8_t kind; uint32_t a, b; uint64_t lo, hi; };
    sim::LinkOut core, region;
    sim::RegFileIn window;
    sim::FifoIn dma;
    const Ev* ev = nullptr;
    unsigned n = 0, i = 0;
    uint32_t reads[64] = {};
    unsigned n_reads = 0;
    uint64_t dma_t[64] = {}, dma_lo[64] = {}, dma_hi[64] = {}, dma_h[64] = {};
    unsigned n_dma = 0;
    Stand(sim::Ports& p, const Ev* e, unsigned count) : ev(e), n(count) {
        p.out(core, "core", 1);
        p.out(region, "region", 3);
        p.in(window, "window");
        p.in_credit(dma, "dma", 3, 4);
    }
    sim::Wake step(sim::Ctx& c) {
        if (c.ready(dma)) {
            const sim::Word* w = c.peek(dma);
            if (n_dma < 64) { dma_t[n_dma] = c.now(); dma_lo[n_dma] = w[0]; dma_h[n_dma] = w[1]; dma_hi[n_dma] = w[2]; }
            n_dma++;
            c.take(dma);
        }
        for (; i < n && ev[i].t == c.now(); i++) {
            const Ev& e = ev[i];
            if (e.kind == STORE) c.put(core, CoreOp::store(1, e.a, e.b));
            else if (e.kind == REGION) c.put(region, e.lo, lw::hdr(7, TILE, lw::M_WRITE, 0, e.a, e.b), e.hi);
            else reads[n_reads++] = c.read(window, e.a);
        }
        return c.next();
    }
    uint64_t digest() const { return i * 131 + n_reads * 7 + n_dma; }
};

struct Rig {
    sim::Sim m;
    PromptSlave slave;
    Rig(const Stand::Ev* ev, unsigned n, const sim::Options& o) : m(build(ev, n, o)) { slot().s.rw_engine(1).attach(&slave); }
    static sim::Sim build(const Stand::Ev* ev, unsigned n, const sim::Options& o) {
        sim::Config cfg(o);
        PeriphSlotConfig pc;
        pc.tile = TILE; pc.controller = true; pc.type[1] = PT_RW;
        auto& st = cfg.add<Stand>("stand", ev, n);
        auto& sl = cfg.add<PeriphSlot>("slot", pc);
        cfg.link(st.core, sl.core, 1);
        cfg.link(st.region, sl.region, 3);
        cfg.connect(sl.window, st.window);
        cfg.connect(sl.dma, st.dma);
        return cfg.build();
    }
    Stand& stand() { return m.unit<Stand>("stand"); }
    PeriphSlot& slot() { return m.unit<PeriphSlot>("slot"); }
};
sim::Options opts(int variant) {
    sim::Options o;
    if (variant == 1) o.step_all = true;
    if (variant == 2) o.shuffle_seed = 7;
    return o;
}
const unsigned R_STATUS = Win::reg(1, Win::R_STD + P_STATUS), R_PEEK = Win::reg(1, Win::R_STD + P_EVENTS_PEEK),
               R_PARAMS = Win::reg(1, Win::R_STD + P_PARAMS), R_CONFIG = Win::reg(1, Win::R_OWN + 0), R_DEFINED = Win::reg(1, Win::R_DEFINED),
               R_WRITABLE = Win::reg(1, Win::R_WRITABLE), R_ERRORS = Win::reg(1, Win::R_STD + P_ERRORS);

}  // namespace

// A read of 6 beats and its reply through the slot unit, the read timing test's job at the framework's ports. By hand,
// from the README's timing (the controller prompt): START presented in p = 10, STATUS.busy read 1 in p + 1, AR accepted
// in p + 2 and p + 3, the beats in p + 3 .. p + 8, taken by the DMA in p + 4 .. p + 9 and at the router's input a cycle
// later, the read's end in p + 8 (done read from p + 9) and the transfer's in p + 10 (dma_done read from p + 11).
TEST(rw_engine_in_the_slot_unit_a_read_and_its_reply) {
    const Stand::Ev ev[] = {
        {2, Stand::READ, R_PARAMS, 0, 0, 0},                    // 0: W_PARAMS: type 2, version 0
        {2, Stand::READ, R_DEFINED, 0, 0, 0},                   // 1: words 32-36 defined
        {2, Stand::READ, R_WRITABLE, 0, 0, 0},                  // 2: and writable
        {3, Stand::STORE, RwEngine::RW_CONFIG, rw_config(KIND_READ, 6), 0, 0},
        {4, Stand::STORE, RwEngine::RW_LPDDR_ADDR, 0x102, 0, 0},
        {5, Stand::READ, R_CONFIG, 0, 0, 0},                    // 3: RW_CONFIG as stored in 3
        {6, Stand::STORE, P_DMA_HDR, dma_hdr(3, MODE_WRITE, 5, 6, 0), 0, 0},
        {7, Stand::STORE, P_DMA_DEST_ADDR, 0x1000, 0, 0},
        {8, Stand::STORE, P_DMA_START, 1, 0, 0},
        {10, Stand::STORE, P_START, 1, 0, 0},                   // p = 10
        {10, Stand::READ, R_STATUS, 0, 0, 0},                   // 4: p: dma_busy alone
        {11, Stand::READ, R_STATUS, 0, 0, 0},                   // 5: p + 1: busy
        {18, Stand::READ, R_STATUS, 0, 0, 0},                   // 6: p + 8: busy, the last beat's cycle
        {18, Stand::READ, R_PEEK, 0, 0, 0},                     // 7: no event yet
        {19, Stand::READ, R_STATUS, 0, 0, 0},                   // 8: p + 9: the read ended; the transfer runs
        {19, Stand::READ, R_PEEK, 0, 0, 0},                     // 9: done
        {20, Stand::READ, R_STATUS, 0, 0, 0},                   // 10: p + 10: the transfer's last cycle
        {21, Stand::READ, R_STATUS, 0, 0, 0},                   // 11: p + 11: idle
        {21, Stand::READ, R_PEEK, 0, 0, 0},                     // 12: done and dma_done
        {22, Stand::STORE, RwEngine::RW_REPLY_W0, 0xCAFEF00D, 0, 0},
        {23, Stand::STORE, RwEngine::RW_REPLY_SEND, 0x7, 0, 0}, // pushed at the end of 23
        {24, Stand::STORE, P_DMA_HDR, dma_hdr(2, MODE_WRITE, 5, 1, 1), 0, 0},
        {25, Stand::STORE, P_DMA_DEST_ADDR, 0x0EF0, 0, 0},
        {26, Stand::STORE, P_DMA_START, 1, 0, 0},               // the reply taken in 27, at the router's input in 28
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], opts(variant));
        r.m.run(40);
        const Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 13u);
        CHECK_EQ(s.reads[0], 2u);
        CHECK_EQ(s.reads[1], 0x1Fu);
        CHECK_EQ(s.reads[2], 0x1Fu);
        CHECK_EQ(s.reads[3], rw_config(KIND_READ, 6));
        CHECK_EQ(s.reads[4], 2u);
        CHECK_EQ(s.reads[5], 3u);
        CHECK_EQ(s.reads[6], 3u);
        CHECK_EQ(s.reads[7], 0u);
        CHECK_EQ(s.reads[8], 2u);
        CHECK_EQ(s.reads[9], DONE);
        CHECK_EQ(s.reads[10], 2u);
        CHECK_EQ(s.reads[11], 0u);
        CHECK_EQ(s.reads[12], DONE | DMA_DONE);
        CHECK(r.slave.seen == (std::vector<std::pair<int64_t, char>>{{12, 'A'}, {13, 'A'}}));
        CHECK(burst_list_of(r.slave.lpddr) == (std::vector<AddressBeat>{address_beat(0x102, 2), address_beat(0x104, 4)}));
        CHECK_EQ(s.n_dma, 7u);
        for (unsigned k = 0; k < 6; k++) {
            CHECK_EQ(s.dma_t[k], 15u + k);                      // taken in p + 4 + k, at the router's input the cycle after
            const u128 row = axi4::initial_row(0x102 + k);
            CHECK_EQ(s.dma_lo[k], axi4::lo64(row));
            CHECK_EQ(s.dma_hi[k], axi4::hi64(row));
            CHECK_EQ(lw::addr(s.dma_h[k]), 0x1000u + 16 * k);
            CHECK_EQ(lw::dst(s.dma_h[k]), 5u);
            CHECK_EQ(lw::src(s.dma_h[k]), TILE);
            CHECK_EQ(lw::counter(s.dma_h[k]), 3u);
            CHECK_EQ(lw::size(s.dma_h[k]), unsigned(lw::SIZE_128));
        }
        CHECK_EQ(s.dma_t[6], 28u);
        CHECK_EQ(s.dma_lo[6], uint64_t(0x7) << 32 | 0xCAFEF00D);
        CHECK_EQ(s.dma_hi[6], uint64_t(0));
        CHECK_EQ(lw::addr(s.dma_h[6]), 0x0EF0u);
        CHECK_EQ(lw::counter(s.dma_h[6]), 2u);
        CHECK_EQ(r.slot().s.violation_total(), uint64_t(0));
        if (variant == 0) CHECK(r.m.steps("slot") < 36);        // asleep before the first store and after the reply
    }
}

// A write of 3 beats through the slot unit: its rows by region WRITEs of the three sizes on S1, the last accepted by the
// director the cycle before START is presented's cycle less one, so that it lands at the end of the cycle before the
// engine's first read of the buffer. By hand: START in p = 20; AW accepted in p + 2 (3 beats from beat 0x8001: one
// burst); W beats taken in p + 3, p + 4, p + 5; the burst complete at the end of p + 5 and answered in p + 6: the
// write ends there (done read from p + 7). LPDDR holds the rows as the WRITEs built them.
TEST(rw_engine_in_the_slot_unit_a_write_from_region_writes) {
    const Stand::Ev ev[] = {
        {3, Stand::STORE, RwEngine::RW_CONFIG, rw_config(KIND_WRITE, 3), 0, 0},
        {4, Stand::STORE, RwEngine::RW_LPDDR_ADDR, 0x8001, 0, 0},
        {5, Stand::STORE, RwEngine::RW_BUFFER_ADDR, 0x20, 0, 0},
        // row 0x21 by a 128-bit WRITE, row 0x22 by two 64-bit WRITEs (the director's accepts, a cycle apart)
        {6, Stand::REGION, 0x04000210, lw::SIZE_128, 0x0102030405060708ull, 0x1112131415161718ull},
        {7, Stand::REGION, 0x04000220, lw::SIZE_64, 0x2122232425262728ull, 0},
        {8, Stand::REGION, 0x04000228, lw::SIZE_64, 0x3132333435363738ull, 0},
        // row 0x20 by four 32-bit WRITEs, the last accepted in 18: on the write port in 20 = p, landing at its end,
        // and the engine's first read of the buffer is in p + 1
        {15, Stand::REGION, 0x04000200, lw::SIZE_32, 0xA0A0A0A0, 0},
        {16, Stand::REGION, 0x04000204, lw::SIZE_32, 0xA1A1A1A1, 0},
        {17, Stand::REGION, 0x04000208, lw::SIZE_32, 0xA2A2A2A2, 0},
        {18, Stand::REGION, 0x0400020C, lw::SIZE_32, 0xA3A3A3A3, 0},
        {20, Stand::STORE, P_START, 1, 0, 0},                   // p = 20
        {26, Stand::READ, R_STATUS, 0, 0, 0},                   // 0: p + 6: busy
        {26, Stand::READ, R_PEEK, 0, 0, 0},                     // 1
        {27, Stand::READ, R_STATUS, 0, 0, 0},                   // 2: p + 7: ended
        {27, Stand::READ, R_PEEK, 0, 0, 0},                     // 3: done
        {27, Stand::READ, R_ERRORS, 0, 0, 0},                   // 4: no error
    };
    for (int variant = 0; variant < 3; variant++) {
        Rig r(ev, sizeof ev / sizeof ev[0], opts(variant));
        r.m.run(40);
        const Stand& s = r.stand();
        CHECK_EQ(s.n_reads, 5u);
        CHECK_EQ(s.reads[0], 1u);
        CHECK_EQ(s.reads[1], 0u);
        CHECK_EQ(s.reads[2], 0u);
        CHECK_EQ(s.reads[3], DONE);
        CHECK_EQ(s.reads[4], 0u);
        CHECK(r.slave.seen == (std::vector<std::pair<int64_t, char>>{{22, 'W'}, {23, 'D'}, {24, 'D'}, {25, 'D'}}));
        CHECK(burst_list_of(r.slave.lpddr) == (std::vector<AddressBeat>{address_beat(0x8001, 3)}));
        CHECK(r.slave.lpddr.row(0x8001) == axi4::make128(0xA3A3A3A3A2A2A2A2ull, 0xA1A1A1A1A0A0A0A0ull));
        CHECK(r.slave.lpddr.row(0x8002) == axi4::make128(0x1112131415161718ull, 0x0102030405060708ull));
        CHECK(r.slave.lpddr.row(0x8003) == axi4::make128(0x3132333435363738ull, 0x2122232425262728ull));
        CHECK_EQ(r.slave.lpddr.failures, 0u);
        CHECK_EQ(r.slot().s.rw_engine(1).port_writes, uint64_t(7));
        CHECK_EQ(r.slot().s.violation_total(), uint64_t(0));
    }
}
