// test_tracer_slot: the Tracer as C&C 3's peripheral 1 in the slot unit (AUDIT-M3 3.1, 3.3), with
// test_periph_slot's harness pattern: a stand-in for the core and the director puts window stores, consuming EVENTS loads
// (S2/S3, Link(1)) and region writes (S1, Link(3)), reads the window RegFile, and takes the DMA's packets from the credit
// Fifo. The stand is a small controller (TracerRig's, reduced): the tables, the record, the hash words, RUN_CONFIG and
// PULSE_RUN, then the drain (queue 0 in one transfer, the streams by 128-word credits, in order 1-3) until run done.
//
// The reference is the same controller driving SlotCore directly with ref's timing (the slot's calls made at once, as
// ref's PeripheralSlot under its Tile: the SlotConf holds SlotCore equal to ref's slot; TracerConf and Quest 163 hold
// the Tracer equal to ref's). The two runs must make the same decisions at the same cycles, send the same DMA packets
// (the unit's a cycle later, through the Fifo), and leave the Tracer with the same fingerprint (AUDIT-M3 6.6, all five
// units). Plain, step-all and shuffled; the debug builds' missed-wake detector on throughout.
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "harness.h"
#include "sim.h"
#include "statefp.h"
#include "units/file_store.h"
#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/replay_tables.h"
#include "units/tracer.h"

using namespace units;
using units::tracer::FileStore;
using units::tracer::Tracer;

namespace {

constexpr uint64_t LOWEST = 0x7FF00000, CODE = 0x80000000, DATA = 0x80001000;
constexpr int LOG2K = 20;
constexpr unsigned TILE = 66;  // C&C 3

// ---------------------------------------------------------------- the guest: straight-line code ending in END
uint32_t addi(int rd, int rs, int imm) { return uint32_t(imm & 0xFFF) << 20 | uint32_t(rs) << 15 | uint32_t(rd) << 7 | 0x13; }
uint32_t add(int rd, int a, int b) { return uint32_t(b) << 20 | uint32_t(a) << 15 | uint32_t(rd) << 7 | 0x33; }
uint32_t mul(int rd, int a, int b) { return 1u << 25 | uint32_t(b) << 20 | uint32_t(a) << 15 | uint32_t(rd) << 7 | 0x33; }
uint32_t sd(int rs2, int rs1, int off) {
    return uint32_t((off >> 5) & 0x7F) << 25 | uint32_t(rs2) << 20 | uint32_t(rs1) << 15 | 3u << 12 | uint32_t(off & 31) << 7 | 0x23;
}
uint32_t ld(int rd, int rs1, int off) { return uint32_t(off & 0xFFF) << 20 | uint32_t(rs1) << 15 | 3u << 12 | uint32_t(rd) << 7 | 0x03; }
constexpr uint32_t END = 0x6F;

std::vector<uint32_t> program() {
    std::vector<uint32_t> p;
    for (int k = 0; k < 12; k++) {
        p.push_back(addi(5 + k % 6, 5 + (k + 1) % 6, 17 * k - 100));
        p.push_back(add(11, 5 + k % 6, 11));
        p.push_back(sd(11, 2, 8 * ((37 * k) % 256)));                 // two data pages through x2 and x3
        p.push_back(ld(12, 3, 8 * ((53 * k) % 256) - 2048));
        p.push_back(mul(13, 12, 11));
    }
    p.push_back(END);
    return p;
}

// The store: the code page, two owned data pages, an empty map (no jumps), the copy the Tracer runs on.
struct Image {
    std::unique_ptr<FileStore> fs;
    FileStore::File copy, map;
};
Image image() {
    Image im;
    im.fs = std::make_unique<FileStore>();
    FileStore::File img = im.fs->create(2);
    std::vector<uint64_t> page(512, 0);
    const auto prog = program();
    for (size_t k = 0; k < prog.size(); k++) {
        uint64_t& w = page[k / 2];
        w |= uint64_t(prog[k]) << (32 * (k % 2));
    }
    im.fs->put_page(img, int64_t((CODE - LOWEST) / 4096), page.data());
    for (int d = 0; d < 2; d++) {
        for (int i = 0; i < 512; i++) page[size_t(i)] = 0x1111111111111111ull * uint64_t(d + 1) + uint64_t(i);
        im.fs->put_page(img, int64_t((DATA - LOWEST) / 4096) + d, page.data());
    }
    im.map = im.fs->create(1);
    std::fill(page.begin(), page.end(), ~uint64_t(0));
    im.fs->put_page(im.map, 0, page.data());
    im.copy = im.fs->cow_open(img);
    for (int d = 0; d < 2; d++) im.fs->make_private(im.copy, int64_t((DATA - LOWEST) / 4096) + d);
    return im;
}

// ---------------------------------------------------------------- the controller (one decision a cycle)
struct Packet { uint64_t t, lo, h, hi; };
struct Ctl {
    // what the controller sees at t: window words as a load at t reads them
    struct View { uint32_t status, peek, mask, len[4]; };
    enum : uint8_t { NONE, STORE, CONSUME };
    struct Act { uint8_t core = NONE; uint32_t w = 0, v = 0; bool region = false; uint32_t addr = 0; uint64_t lo = 0, hi = 0; };

    std::vector<std::pair<uint32_t, std::pair<uint64_t, uint64_t>>> packets;  // region writes: (address, lo, hi)
    std::vector<std::pair<uint32_t, uint32_t>> stores;                         // the set-up stores, in order
    size_t np = 0, ns = 0;
    int phase = 0;     // 0 set-up, 1 running, 2 done
    int64_t wait_until = 0;
    int dma = 0;       // 0 idle, 1-3: HDR, ADDR, PULSE to store next; 4: waiting for done
    unsigned dq = 0;
    uint32_t dwords = 0, sent[4] = {}, addr_next = 0x1000;
    bool q0_sent = false;
    std::string log;   // every decision, "t:what", for the two runs to be compared
    int rows;

    explicit Ctl(int rows_) : rows(rows_) {
        const auto tab = tracer::ReplayTables::load(tracer::ReplayTables::default_dir());
        for (int p = 0; p < 256; p++) {
            uint64_t lo = 0, hi = 0;
            for (int k = 0; k < 4; k++) lo |= uint64_t(tab.table[8 * p + k]) << (16 * k);
            for (int k = 0; k < 4; k++) hi |= uint64_t(tab.table[8 * p + 4 + k]) << (16 * k);
            packets.push_back({0x1000u + 16u * unsigned(p), {lo, hi}});
        }
        for (int q = 0; q < 512; q++) packets.push_back({0x2000u + 16u * unsigned(q), {tab.rom_lo[q], tab.rom_hi[q]}});
    }
    void record(const Image& im) {
        packets.push_back({0x000, {CODE, 0}});                    // word 0 first: pc, instr_idx 0, offset 0
        packets.push_back({0x010, {0, 0}});                       // no advice
        for (int k = 0; k < 64; k++) {
            uint64_t lo = 0, hi = 0;
            const int r0 = 2 * k, r1 = 2 * k + 1;                // register image: rec[4 + r] = x_r (lo of word 2+k = x(2k))
            auto val = [](int r) -> uint64_t { return r == 2 ? DATA : r == 3 ? DATA + 4096 : r == 0 ? 0 : 0x0101010101ull * uint64_t(r); };
            lo = val(r0); hi = val(r1);
            packets.push_back({0x020u + 16u * unsigned(k), {lo, hi}});
        }
        uint64_t fw[2];
        Tracer::files_word(im.copy.root, im.copy.depth, im.map.root, im.map.depth, LOG2K, int64_t(LOWEST), fw);
        packets.push_back({0x420, {fw[0], fw[1]}});
        packets.push_back({0x430, {0, 0}});
        stores = {{40, 0x9E3779B1u}, {41, 1000003u}, {42, 5u}, {7, 0x7FFu}};
    }

    Act cycle(int64_t t, const View& v) {
        Act a;
        char b[96];
        if (np < packets.size()) {
            a.region = true;
            a.addr = 0x04000000u | packets[np].first;
            a.lo = packets[np].second.first;
            a.hi = packets[np].second.second;
            np++;
        }
        if (phase == 0) {
            if (ns < stores.size()) { a.core = STORE; a.w = stores[ns].first; a.v = stores[ns].second; ns++; }
            else if (np == packets.size() && wait_until == 0) wait_until = t + 1;   // PULSE_RUN at the landing's cycle: tight
            else if (wait_until && t == wait_until) { a.core = STORE; a.w = 32; a.v = uint32_t(rows); }
            else if (wait_until && t == wait_until + 1) { a.core = STORE; a.w = 3; a.v = 1; phase = 1; }
        } else if (phase == 1) {
            if (dma >= 1 && dma <= 3) {
                a.core = STORE;
                if (dma == 1) { a.w = 8; a.v = 1u << 25 | 1u << 22 | dq << 12 | dwords; }
                else if (dma == 2) { a.w = 9; a.v = addr_next; addr_next += 16 * dwords; }
                else { a.w = 4; a.v = 1; }
                dma++;
            } else if (dma == 4 && (v.peek & 2)) {
                a.core = CONSUME; a.v = v.peek & v.mask; dma = 0;
            } else if ((v.peek & 0x69) != 0) {     // run done, the advice and expansion credits, a new expansion
                a.core = CONSUME; a.v = v.peek & v.mask;
                if (v.peek & 1) phase = 2;
            } else if (dma == 0 && !(v.status & 2)) {
                if ((v.peek & 1u << Tracer::EV_SVC_STATE) && !q0_sent) { dq = 0; dwords = Tracer::REC_WORDS; q0_sent = true; dma = 1; }
                else for (unsigned q = 1; q <= 3; q++) {
                    if (!(v.peek & 1u << (Tracer::EV_SVC_STATE + q))) continue;
                    dq = q; dwords = std::min(128u, v.len[q] - sent[q]); sent[q] += dwords; dma = 1;
                    break;
                }
            }
        }
        if (a.core != NONE || a.region) {
            std::snprintf(b, sizeof b, "%lld:%c%u,%x%s ", (long long)t, a.core == STORE ? 'S' : a.core == CONSUME ? 'C' : '-', a.w, a.v, a.region ? "R" : "");
            log += b;
        }
        return a;
    }
};

// ---------------------------------------------------------------- the framework run
struct Stand {
    sim::LinkOut core, region;
    sim::RegFileIn window;
    sim::FifoIn dma;
    Ctl* ctl = nullptr;
    std::vector<Packet>* got = nullptr;
    Stand(sim::Ports& p, Ctl* c, std::vector<Packet>* g) : ctl(c), got(g) {
        p.out(core, "core", 1);
        p.out(region, "region", 3);
        p.in(window, "window");
        p.in_credit(dma, "dma", 3, 4);
    }
    sim::Wake step(sim::Ctx& c) {
        while (c.ready(dma)) {
            const sim::Word* w = c.peek(dma);
            got->push_back({c.now(), w[0], w[1], w[2]});
            c.take(dma);
        }
        if (ctl->phase == 2) return c.next();
        Ctl::View v;
        v.status = uint32_t(c.read(window, Win::reg(1, Win::R_STD + 1)));
        v.peek = uint32_t(c.read(window, Win::reg(1, Win::R_STD + 6)));
        v.mask = uint32_t(c.read(window, Win::reg(1, Win::R_STD + 7)));
        for (unsigned q = 1; q <= 3; q++) v.len[q] = uint32_t(c.read(window, Win::reg(1, Win::R_OWN + (Tracer::W_CODE_LENGTH + q - 1 - 32))));
        const Ctl::Act a = ctl->cycle(int64_t(c.now()), v);
        if (a.region) c.put(region, a.lo, lw::hdr(7, 0, 0, 0, a.addr, lw::SIZE_128), a.hi);
        if (a.core == Ctl::STORE) c.put(core, CoreOp::store(1, a.w, a.v));
        if (a.core == Ctl::CONSUME)   // a consuming load of EVENTS: its value under EVENTS_MASK (as ref's slot.read)
            c.put(core, CoreOp::consume(1, uint32_t(c.read(window, Win::reg(1, Win::R_STD + 5))) & v.mask));
        return sim::Wake::at_cycle(c.now() + 1);
    }
    uint64_t digest() const { return got->size() * 131 + uint64_t(ctl->np) * 7 + uint64_t(ctl->phase); }
};

struct FwRun {
    Ctl ctl;
    std::vector<Packet> got;
    sim::Sim m;
    FwRun(int rows, const sim::Options& o) : ctl(rows), m(build(o)) {
        Image im = image();
        ctl.record(im);
        m.unit<PeriphSlot>("slot").s.tracer(1).own_memory(std::move(im.fs));
    }
    sim::Sim build(const sim::Options& o) {
        sim::Config cfg(o);
        PeriphSlotConfig pc;
        pc.tile = TILE; pc.controller = true; pc.type[1] = PT_TRACER;
        auto& st = cfg.add<Stand>("stand", &ctl, &got);
        auto& sl = cfg.add<PeriphSlot>("slot", pc);
        cfg.link(st.core, sl.core, 1);
        cfg.link(st.region, sl.region, 3);
        cfg.connect(sl.window, st.window);
        cfg.connect(sl.dma, st.dma);
        return cfg.build();
    }
    Tracer& tracer() { return m.unit<PeriphSlot>("slot").s.tracer(1); }
};

// ---------------------------------------------------------------- the reference: SlotCore driven directly
struct DirectRun {
    Ctl ctl;
    SlotCore sc;
    std::vector<Packet> got;
    int64_t end_t = 0;
    explicit DirectRun(int rows, int64_t cycles) : ctl(rows) {
        sc.tile = TILE;
        sc.dma_port = true;
        sc.install(1, PT_TRACER);
        Image im = image();
        ctl.record(im);
        sc.tracer(1).own_memory(std::move(im.fs));
        for (int64_t t = 0; t < cycles; t++) cycle(t);
    }
    // One cycle t at ref's timing (the slot's calls at once).
    void cycle(int64_t t) {
        {
            if (ctl.phase != 2 && t >= 1) {   // the framework steps units from cycle 1 (the stand's first decision)
                Ctl::View v;
                sc.peek(t, 1, 1, v.status);
                sc.peek(t, 1, 6, v.peek);
                sc.peek(t, 1, 7, v.mask);
                for (unsigned q = 1; q <= 3; q++) sc.peek(t, 1, Tracer::W_CODE_LENGTH + q - 1, v.len[q]);
                const Ctl::Act a = ctl.cycle(t, v);
                if (a.region) sc.accept(t, 1, a.lo, lw::hdr(7, 0, 0, 0, a.addr, lw::SIZE_128), a.hi);
                if (a.core == Ctl::STORE) sc.write(t, 1, a.w, a.v);
                if (a.core == Ctl::CONSUME) { uint32_t x; sc.read(t, 1, 5, x); }
            }
            sc.run_cycle(t);
            for (unsigned k = 0; k < sc.n_out; k++) got.push_back({uint64_t(sc.out[k].t) + 1, sc.out[k].lo, sc.out[k].h, sc.out[k].hi});
            sc.end_cycle(t);
            end_t = t;
        }
    }
};

std::string fps(Tracer& t, int64_t c) {
    std::string s;
    for (int u = 0; u < Tracer::FP_UNITS; u++) {
        fp::FpOut o;
        t.fingerprint(u, o, c);
        char b[24];
        std::snprintf(b, sizeof b, "%016llx ", (unsigned long long)o.done());
        s += b;
    }
    return s;
}

sim::Options opts(int variant) {
    sim::Options o;
    if (variant == 1) o.step_all = true;
    if (variant == 2) o.shuffle_seed = 7;
    return o;
}

}  // namespace

TEST(tracer_in_the_slot_equals_slotcore_driven_directly) {
    const int rows = 400;
    const int64_t cycles = 8000;
    DirectRun d(rows, cycles);
    CHECK_EQ(d.ctl.phase, 2);   // run done reached
    Tracer& dt = d.sc.tracer(1);
    CHECK_EQ(dt.rows, int64_t(rows));
    CHECK_EQ(dt.rec[0], uint64_t(0));          // the program ended (END), then padding
    CHECK(!dt.stopped());
    CHECK_EQ(d.sc.violation_total(), uint64_t(0));
    // the drain: the record (68 words) and the three streams, all sent
    uint64_t want = Tracer::REC_WORDS;
    for (unsigned q = 1; q <= 3; q++) want += d.ctl.sent[q];
    CHECK_EQ(uint64_t(d.got.size()), want);
    CHECK(d.ctl.sent[1] > 0 && d.ctl.sent[2] > 0 && d.ctl.sent[3] > 0);
    CHECK_EQ(d.got[0].lo, uint64_t(0));        // queue 0's word 0: the state word at the window's end (pc 0)
    const std::string dfp = fps(dt, d.end_t);
    std::printf("  direct: %zu decisions' log bytes, %zu DMA packets (streams %u, %u, %u words), run done at cycle %s\n",
                d.ctl.log.size(), d.got.size(), d.ctl.sent[1], d.ctl.sent[2], d.ctl.sent[3],
                d.ctl.log.substr(d.ctl.log.rfind(' ', d.ctl.log.size() - 2) + 1).c_str());
    for (int variant = 0; variant < 3; variant++) {
        FwRun r(rows, opts(variant));
        r.m.run(uint64_t(cycles));
        CHECK_EQ(r.ctl.phase, 2);
        if (r.ctl.log != d.ctl.log) {
            size_t i = 0;
            while (i < r.ctl.log.size() && i < d.ctl.log.size() && r.ctl.log[i] == d.ctl.log[i]) i++;
            ::test::fail(__FILE__, __LINE__, "variant %d: the controller's decisions differ at byte %zu: unit '%.60s', direct '%.60s'",
                         variant, i, r.ctl.log.c_str() + (i > 20 ? i - 20 : 0), d.ctl.log.c_str() + (i > 20 ? i - 20 : 0));
        }
        CHECK_EQ(r.got.size(), d.got.size());
        for (size_t k = 0; k < r.got.size() && k < d.got.size(); k++) {
            if (r.got[k].t != d.got[k].t || r.got[k].lo != d.got[k].lo || r.got[k].h != d.got[k].h || r.got[k].hi != d.got[k].hi) {
                ::test::fail(__FILE__, __LINE__, "variant %d: DMA packet %zu: unit (t %llu, %llx, %llx, %llx), direct (t %llu, %llx, %llx, %llx)",
                             variant, k, (unsigned long long)r.got[k].t, (unsigned long long)r.got[k].lo, (unsigned long long)r.got[k].h,
                             (unsigned long long)r.got[k].hi, (unsigned long long)d.got[k].t, (unsigned long long)d.got[k].lo,
                             (unsigned long long)d.got[k].h, (unsigned long long)d.got[k].hi);
                break;
            }
        }
        // the same Tracer state at the end: every fingerprint unit (both ran to cycle `cycles` - 1)
        const std::string rfp = fps(r.tracer(), d.end_t);
        if (rfp != dfp) ::test::fail(__FILE__, __LINE__, "variant %d: the Tracer's fingerprints at the end: unit %s, direct %s", variant, rfp.c_str(), dfp.c_str());
        CHECK_EQ(r.m.unit<PeriphSlot>("slot").s.violation_total(), uint64_t(0));
    }
}

// The slot creates the Tracer bare (ref's Config): a run before its memory is given is FATAL.
DEATH_TEST(tracer_in_the_slot_without_memory_is_fatal, "without a memory system") {
    SlotCore sc;
    sc.tile = TILE;
    sc.dma_port = true;
    sc.install(1, PT_TRACER);
    sc.write(1, 1, 3, 1);   // PULSE_RUN at 1
    for (int64_t t = 0; t < 4; t++) { sc.run_cycle(t); sc.end_cycle(t); }
}

// The Tracer's fingerprint in the slot unit at every cycle's end equals its twin's, driven directly at ref's timing
// (the segment run: a PULSE_DMA store of the point's own cycle starts a transfer in ref at once, here a cycle later
// through the slot's core link): PeriphSlot::fingerprint_tracer overlays what the links still hold (landings, the core's
// PULSE_RUN or window store, the PULSE_DMA's start), all five units. The plain Tracer::fingerprint misses them: checked.
TEST(tracer_fingerprint_in_the_slot_equals_its_twin_every_cycle) {
    const int rows = 400;
    const int64_t cycles = 6000;
    sim::Options o;
    o.observe_arrivals = true;
    FwRun r(rows, o);
    DirectRun d(rows, 0);
    PeriphSlot& slot = r.m.unit<PeriphSlot>("slot");
    uint64_t compared = 0, overlaid = 0, starts = 0, starts_missed_plain = 0;
    for (int64_t t = 0; t < cycles; t++) {
        d.cycle(t);
        if (int64_t(r.m.now()) < t + 1) r.m.run(uint64_t(t + 1) - r.m.now());   // the framework between cycles t and t + 1
        for (int u = 0; u < Tracer::FP_UNITS; u++) {
            fp::FpOut a, b, plain;
            d.sc.tracer(1).fingerprint(u, a, t);
            slot.fingerprint_tracer(r.m, 1, u, b, t);
            if (a.done() != b.done()) {
                if (std::getenv("TRACER_FP_DEBUG")) {   // localising: both streams, named, to diff
                    FILE* fa = std::fopen("/tmp/fp_twin.txt", "w"); FILE* fb = std::fopen("/tmp/fp_slot.txt", "w");
                    fp::FpOut da(fa, "tr"), db(fb, "tr");
                    d.sc.tracer(1).fingerprint(u, da, t);
                    slot.fingerprint_tracer(r.m, 1, u, db, t);
                    std::fclose(fa); std::fclose(fb);
                }
                ::test::fail(__FILE__, __LINE__, "after cycle %lld: unit %s: in the slot %016llx, its twin %016llx", (long long)t,
                             Tracer::FP_NAMES[u], (unsigned long long)b.done(), (unsigned long long)a.done());
                return;
            }
            if (u == 0) {
                r.tracer().fingerprint(0, plain, t);
                if (plain.done() != a.done()) overlaid++;
            }
            compared++;
        }
        // the cycles whose core store is a PULSE_DMA (a case met in practice): the plain fingerprint misses it there
        sim::PortEntry e[8];
        const sim::PortView v = r.m.observe(slot.core, e, 8);
        for (uint32_t k = 0; k < v.n; k++)
            if (CoreOp::kind(e[k].w[0]) == CoreOp::STORE && CoreOp::word(e[k].w[0]) == 4) {
                starts++;
                fp::FpOut a, plain;
                d.sc.tracer(1).fingerprint(0, a, t);
                r.tracer().fingerprint(0, plain, t);
                if (a.done() != plain.done()) starts_missed_plain++;
            }
    }
    std::printf("  %llu unit fingerprints equal to the twin's; the overlay mattered at %llu cycles, %llu of them a PULSE_DMA's\n",
                (unsigned long long)compared, (unsigned long long)overlaid, (unsigned long long)starts);
    CHECK(starts >= 4);       // queue 0 and the three streams' transfers
    CHECK_EQ(starts_missed_plain, starts);   // without the overlay, every one of them differs
    CHECK(overlaid >= starts);
}
