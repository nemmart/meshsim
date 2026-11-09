// t2_harness.cpp (WORKER16): the T2 harness's pieces (t2_harness.h).
#include "t2_harness.h"

#include <cstdio>
#include <cstring>

#include "base/fatal.h"
#include "units/big_sram.h"
#include "units/chip.h"
#include "units/host_port.h"
#include "units/net.h"
#include "units/periph_slot.h"
#include "units/tracer.h"
#include "golden_run.h"

namespace t2 {

using units::tracer::FileStore;

bool read_gz(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) std::fclose(f);
    else { err = "cannot read " + path; return false; }
    FILE* p = popen(("gzip -dcf '" + path + "'").c_str(), "r");
    if (!p) { err = "cannot run gzip on " + path; return false; }
    out.clear();
    uint8_t buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, p)) > 0;) out.insert(out.end(), buf, buf + n);
    if (pclose(p) != 0) { err = "gzip failed on " + path; return false; }
    return true;
}

namespace {
struct Reader {
    const std::vector<uint8_t>& b; size_t at = 0; bool bad = false;
    explicit Reader(const std::vector<uint8_t>& v) : b(v) {}
    bool magic(const char* m) { const size_t n = std::strlen(m); if (b.size() < n || std::memcmp(b.data(), m, n)) return false; at = n; return true; }
    uint32_t i32() { if (at + 4 > b.size()) { bad = true; return 0; } uint32_t v; std::memcpy(&v, &b[at], 4); at += 4; return v; }
    uint64_t i64() { if (at + 8 > b.size()) { bad = true; return 0; } uint64_t v; std::memcpy(&v, &b[at], 8); at += 8; return v; }
    bool end() const { return at == b.size(); }
};
}  // namespace

bool load_files(const std::string& path, FileStore& fs, std::string& err, uint64_t* pages) {
    std::vector<uint8_t> b;
    if (!read_gz(path, b, err)) return false;
    Reader r(b);
    if (!r.magic("M3FILES1")) { err = path + ": not an M3FILES1 dump"; return false; }
    const int32_t next_free = int32_t(r.i32());
    const uint32_t n = r.i32();
    if (fs.next_free() != fs.first() || fs.first() != FileStore::PAGE_UNITS) { err = "load_files: the store isn't fresh"; return false; }
    if (next_free < fs.first() || (next_free - fs.first()) % FileStore::PAGE_UNITS) { err = path + ": nextFree " + std::to_string(next_free); return false; }
    fs.grow_to(next_free);
    int64_t last = -1;
    for (uint32_t k = 0; k < n && !r.bad; k++) {
        const int32_t a = int32_t(r.i32());
        if (a < fs.first() || a >= next_free || (a - fs.first()) % FileStore::PAGE_UNITS || a <= last) {
            err = path + ": page " + std::to_string(k) + " at address " + std::to_string(a); return false;
        }
        last = a;
        uint64_t* pg = fs.raw_page(a);
        for (unsigned i = 0; i < FileStore::PAGE_WORDS; i++) pg[i] = r.i64();
    }
    if (r.bad || !r.end()) { err = path + ": truncated or trailing bytes"; return false; }
    if (pages) *pages = n;
    return true;
}

bool load_sram(const std::string& path, std::vector<SramLanding>& out, std::string& err) {
    std::vector<uint8_t> b;
    if (!read_gz(path, b, err)) return false;
    Reader r(b);
    if (!r.magic("M3SRAM01")) { err = path + ": not an M3SRAM01 dump"; return false; }
    out.clear();
    int64_t last_unit = -1;
    while (!r.end() && !r.bad) {
        const uint32_t u = r.i32(), n = r.i32();
        if (u > 66 || int64_t(u) <= last_unit) { err = path + ": unit " + std::to_string(u); return false; }
        last_unit = u;
        int64_t last = -1;
        for (uint32_t k = 0; k < n && !r.bad; k++) {
            const uint32_t c = r.i32(), v = r.i32();
            if (int64_t(c) <= last || c >= (1u << 21)) { err = path + ": unit " + std::to_string(u) + " chunk " + std::to_string(c); return false; }
            last = c;
            out.push_back({u, c, v});
        }
    }
    if (r.bad) { err = path + ": truncated"; return false; }
    return true;
}

void poke_sram(sim::Sim& m, const std::vector<SramLanding>& l) {
    units::BigSram* s = nullptr;
    uint32_t cur = ~0u;
    for (const SramLanding& x : l) {
        if (x.unit != cur) { cur = x.unit; s = &m.unit<units::BigSram>(units::tile_unit(cur, "sram")); }
        s->poke(x.chunk, x.value);
    }
}

namespace {
bool read_file(const std::string& p, std::string& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    char buf[1 << 14];
    out.clear();
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    std::fclose(f);
    return true;
}
std::vector<uint32_t> words(const std::string& b) {   // Mach.words: little-endian, the last word zero-padded
    std::vector<uint32_t> w((b.size() + 3) / 4, 0);
    for (size_t i = 0; i < b.size(); i++) w[i >> 2] |= uint32_t(uint8_t(b[i])) << (8 * (i & 3));
    return w;
}
}  // namespace

uint32_t Image::at(const char* s) const {
    auto it = sym.find(s);
    if (it == sym.end()) FATAL("image %s: no symbol %s", name.c_str(), s);
    return it->second;
}

bool load_image(const std::string& dir, const std::string& name, Image& im, std::string& err) {
    std::string b;
    const std::string base = dir + "/" + name;
    im.name = name;
    if (!read_file(base + ".code.bin", b)) { err = "cannot read " + base + ".code.bin"; return false; }
    im.code = words(b);
    if (!read_file(base + ".data.bin", b)) { err = "cannot read " + base + ".data.bin"; return false; }
    im.data = words(b);
    if (!read_file(base + ".sym", b)) { err = "cannot read " + base + ".sym"; return false; }
    im.sym.clear();
    size_t p = 0;
    while (p < b.size()) {
        size_t e = b.find('\n', p);
        if (e == std::string::npos) e = b.size();
        char a[64], k[8], s[128];
        if (std::sscanf(b.substr(p, e - p).c_str(), "%63s %7s %127s", a, k, s) == 3) im.sym[s] = uint32_t(std::stoul(a, nullptr, 16));
        p = e + 1;
    }
    if (!im.sym.count("_start")) { err = base + ".sym: no _start"; return false; }
    return true;
}

void boot_cores(sim::Sim& m, const Image& cc1, const Image& cc23, const Image& tile, const Image& cc3, unsigned go_latency,
                uint32_t uart_divisor) {
    constexpr uint32_t STAGE_CHUNK = 0x10000, PMEM_BASE = 0x01000000, CMD_JUMP = 0x4A;
    units::BigSram& s = m.unit<units::BigSram>("cc1.sram");
    uint32_t at = STAGE_CHUNK;
    struct Staged { uint32_t chunk, packets; };
    auto stage = [&](const std::vector<uint32_t>& w) {   // 128-bit rows, zero-padded
        const uint32_t ch = at, pk = uint32_t((w.size() + 3) / 4);
        for (uint32_t i = 0; i < 4 * pk; i++) s.poke(ch + i, i < w.size() ? w[i] : 0);
        at += 4 * (pk ? pk : 1);
        return Staged{ch, pk};
    };
    uint32_t desc[1 + 3 * 6 + 2] = {};
    desc[0] = 0xB007DE5Cu;
    const Staged cp = stage(cc23.code), cd = stage(cc23.data), tp = stage(tile.code), td = stage(tile.data);
    const Staged c3p = stage(cc3.code), c3d = stage(cc3.data);
    for (unsigned u = 0; u < 3; u++) {
        const Image& im = u == 0 ? cc23 : u == 1 ? cc3 : tile;
        const Staged p = u == 0 ? cp : u == 1 ? c3p : tp, d = u == 0 ? cd : u == 1 ? c3d : td;
        const uint32_t cmd = at; at += 4;
        const uint32_t e[6] = {p.chunk, p.packets, d.chunk, d.packets, cmd, im.at("_start")};
        std::memcpy(desc + 1 + 6 * u, e, sizeof e);
    }
    desc[19] = go_latency; desc[20] = uart_divisor;
    std::vector<uint32_t> data = cc1.data;
    const uint32_t base = (cc1.at("boot_desc") - cc1.at("_data_start")) / 4;
    if (base + 21 > data.size()) FATAL("boot_cores: cc1's data image ends before boot_desc's 21 words");
    std::memcpy(data.data() + base, desc, sizeof desc);
    units::HostPort& h = m.unit<units::HostPort>("cc1.host");
    h.load(PMEM_BASE, cc1.code.data(), cc1.code.size());
    if (!data.empty()) h.load(cc1.at("_data_start"), data.data(), data.size());
    const uint32_t entry = cc1.at("_start");
    h.command(CMD_JUMP << 24, &entry, 1);
    m.wake("cc1.host");
}

void cc1_command(sim::Sim& m, uint32_t type, uint32_t arg, const uint32_t* args, size_t nargs) {
    units::HostPort& h = m.unit<units::HostPort>("cc1.host");
    h.command(type << 24 | arg, args, nargs);
    m.wake("cc1.host");
}

void cc_segment(sim::Sim& m) {
    const uint32_t args[2] = {1u << 24, TL_CMD_START << 24};
    cc1_command(m, CC_SEGMENT, 2, args, 2);
}

bool cores_booted(const uint32_t* cc1_dmem, const Image& cc1) { return cc1_dmem[(cc1.at("mbox") + 12) / 4] == 2; }

int64_t run_boot(Machine& m, const Image& cc1, uint64_t cap, std::string* trace) {
    sim::Sim& s = *m.sim;
    const units::TileCore* cores[4] = {&s.unit<units::TileCore>("cc1.core"), &s.unit<units::TileCore>("cc2.core"),
                                       &s.unit<units::TileCore>("cc3.core"), &s.unit<units::TileCore>("t0.core")};
    char last[96] = "", b[96];
    for (uint64_t c = 0;; c++) {
        if (trace) {
            std::snprintf(b, sizeof b, "%d %d %d %d", int(cores[0]->rv.pc), int(cores[1]->rv.pc), int(cores[2]->rv.pc), int(cores[3]->rv.pc));
            if (std::strcmp(last, b)) {
                char l[128];
                std::snprintf(l, sizeof l, "%llu %s\n", (unsigned long long)(s.now() - 1), b);
                *trace += l;
                std::strcpy(last, b);
            }
        }
        if (cores_booted(m.dmem(64), cc1)) return int64_t(s.now()) - 2;
        if (c >= cap) return -1;
        s.run(1);
    }
}

uint64_t rows_hash(const uint64_t* recs, size_t n, uint64_t h) {
    auto mix = [](uint64_t z) {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    };
    for (size_t r = 0; r < n; r++)
        for (unsigned i = 0; i < 10; i++) h = mix(h ^ recs[10 * r + i]);
    return h;
}

bool rows_ref(const std::string& delivery, unsigned segment, unsigned windows, RowsRef out[64], std::string& err) {
    for (unsigned t = 0; t < 64; t++) out[t] = RowsRef{};
    std::string b;
    std::vector<uint64_t> recs;
    for (unsigned t = 0; t < windows && t < 64; t++) {
        char name[64];
        std::snprintf(name, sizeof name, "/window_%05u.rows.bin", 64 * segment + t);
        if (!read_file(delivery + name, b)) { err = "cannot read " + delivery + name; return false; }
        if (b.size() % 80) { err = delivery + name + ": not whole 80-byte rows"; return false; }
        recs.assign(b.size() / 8, 0);
        std::memcpy(recs.data(), b.data(), b.size());   // little-endian longs, as ref reads them
        out[t].rows = b.size() / 80;
        out[t].hash = rows_hash(recs.data(), out[t].rows);
        out[t].present = true;
    }
    return true;
}

void attach_store(Machine& m, std::unique_ptr<FileStore> store) {
    units::PeriphSlot& s3 = m.sim->unit<units::PeriphSlot>("cc3.slot");
    s3.s.tracer(1).own_memory(std::move(store));
}

std::unique_ptr<Machine> build_machine(const uint8_t* domains, sim::Options opt, std::string& err) {
    auto m = std::make_unique<Machine>();
    m->hops = units::load_hops("ref/meshsim/hops.txt");
    std::string rom;
    if (!read_file("ref/meshsim/sw/bootrom.hex", rom)) { err = "cannot read ref/meshsim/sw/bootrom.hex"; return nullptr; }
    for (size_t p = 0; p < rom.size();) {
        size_t e = rom.find('\n', p);
        if (e == std::string::npos) e = rom.size();
        if (e > p) m->bootrom.push_back(uint32_t(std::stoul(rom.substr(p, e - p), nullptr, 16)));
        p = e + 1;
    }
    m->mem.assign(size_t(67) * 2 * units::MEM_WORDS, 0);
    units::TileConfig tc = test::tile_config(0);
    units::G1Ctx& gx = m->gx;
    gx.cfg.timing = tc.timing; gx.cfg.periph_present = tc.periph_present; gx.cfg.mem_chunks = 1u << 20;
    gx.cfg.done_gate = true; gx.cfg.K = 8; gx.cfg.cm_latency = 2;
    gx.cfg.periph_slot = true;           // every unit's slot: the tiles' replayers, C&C 1's peripheral, C&C 3's Tracer
    gx.cfg.cc3_tracer = true;            // ref's cc3Tracer (TracerT2, M3Seg)
    gx.bootrom = m->bootrom.data(); gx.bootrom_words = uint32_t(m->bootrom.size());
    gx.mem = m->mem.data(); gx.units = 67; gx.sram = &m->sram;
    sim::Config cfg(opt);
    units::ChipSpec spec;
    spec.hops = &m->hops; spec.control = units::add_g1; spec.control_ctx = &gx; spec.domain = domains;
    spec.cc_cores = true;
    spec.cc3_dma = true;                 // C&C 3's DMA into its router (the Tracer's records and streams)
    units::build_chip(cfg, spec);
    m->sim.emplace(cfg.build());
    return m;
}

void partition(unsigned parts, uint8_t dom[64]) {
    if (parts < 1 || parts > 64) FATAL("t2::partition: %u parts (1-64)", parts);
    unsigned next = 0;
    for (unsigned k = 0; k < 64; k++) {
        if (parts == 1 || k == 2) { dom[k] = 0; continue; }
        dom[k] = uint8_t(1 + next % (parts - 1));
        next++;
    }
}

}  // namespace t2
