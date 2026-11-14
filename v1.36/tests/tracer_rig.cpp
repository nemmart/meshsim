// tracer_rig.cpp: the rig (tracer_rig.h): the guest's builders, and the windows on the RTL bench's controller.
#include "tracer_rig.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "base/fatal.h"
#include "java_format.h"
#include "units/row_core.h"
#include "units/rvc.h"

namespace tracer_rig {

using units::tracer::Tracer;
namespace rowcore = units::rowcore;


// ================================================================ Prog
int64_t Prog::pc_of(int k) const {
    int64_t p = base;
    for (int n = 0; n < k; n++) p += parcels[size_t(n)].second;
    return p;
}
std::vector<uint8_t> Prog::bytes() const {
    std::vector<uint8_t> out;
    for (const auto& q : parcels)
        for (int k = 0; k < q.second; k++) out.push_back(uint8_t(q.first >> (8 * k)));
    return out;
}

// ================================================================ java.util.HashMap's order (Long keys)
// JDK 21's HashMap, structure only: DEFAULT_INITIAL_CAPACITY 16, load factor 0.75, TREEIFY_THRESHOLD 8,
// MIN_TREEIFY_CAPACITY 64.
uint32_t JavaLongHashMap::hash(int64_t key) {
    const uint64_t v = uint64_t(key);
    const uint32_t h = uint32_t(v ^ (v >> 32));  // Long.hashCode
    return h ^ (h >> 16);                        // HashMap.hash
}
void JavaLongHashMap::resize() {
    const size_t old_n = tab_.size();
    if (old_n == 0) {
        tab_.assign(16, {});
        threshold_ = 12;
        return;
    }
    std::vector<std::vector<int64_t>> t(2 * old_n);
    for (size_t j = 0; j < old_n; j++)  // split: lo stays at j, hi goes to j + old_n, each in order
        for (int64_t k : tab_[j]) t[(hash(k) & old_n) ? j + old_n : j].push_back(k);
    tab_ = std::move(t);
    threshold_ *= 2;
}
void JavaLongHashMap::treeify_bin(size_t bin) {
    (void)bin;
    if (tab_.size() < 64) resize();
    else treeified_ = true;  // Java builds a red-black tree there: its iteration order is not modelled
}
bool JavaLongHashMap::contains(int64_t key) const {
    if (tab_.empty()) return false;
    const auto& b = tab_[hash(key) & (tab_.size() - 1)];
    return std::find(b.begin(), b.end(), key) != b.end();
}
void JavaLongHashMap::put(int64_t key) {  // putVal
    if (tab_.empty()) resize();
    const size_t i = hash(key) & (tab_.size() - 1);
    auto& b = tab_[i];
    if (std::find(b.begin(), b.end(), key) != b.end()) return;
    b.push_back(key);
    if (b.size() >= 9) treeify_bin(i);  // binCount >= TREEIFY_THRESHOLD - 1: the 9th node appended
    if (++size_ > threshold_) resize();
}
void JavaLongHashMap::compute_if_absent(int64_t key) {
    if (size_ > threshold_ || tab_.empty()) resize();
    const size_t i = hash(key) & (tab_.size() - 1);
    auto& b = tab_[i];
    if (std::find(b.begin(), b.end(), key) != b.end()) return;
    const size_t bin_count = b.size();  // every node traversed
    b.insert(b.begin(), key);           // tab[i] = newNode(hash, key, v, first)
    if (bin_count >= 7) treeify_bin(i);
    ++size_;
}
std::vector<int64_t> JavaLongHashMap::order() const {
    if (treeified_) FATAL("JavaLongHashMap: a treeified bin (9 keys in one bin of %zu): Java's order not reproduced", tab_.size());
    std::vector<int64_t> out;
    for (const auto& b : tab_) out.insert(out.end(), b.begin(), b.end());
    return out;
}
std::vector<int64_t> java_hash_order(const std::vector<int64_t>& keys) {
    JavaLongHashMap m;
    for (int64_t k : keys) m.put(k);
    return m.order();
}

// ================================================================ Pages
std::vector<uint64_t>& Pages::slot(int64_t key) {
    for (size_t k = 0; k < keys.size(); k++)
        if (keys[k] == key) return pages[k];
    keys.push_back(key);
    pages.emplace_back(512, 0);
    return pages.back();
}
const std::vector<uint64_t>& Pages::page(int64_t key) const {
    for (size_t k = 0; k < keys.size(); k++)
        if (keys[k] == key) return pages[k];
    FATAL("Pages: no page at 0x%llx", (unsigned long long)key);
}
std::vector<uint64_t>& Pages::at(int64_t key) {
    map.compute_if_absent(key);
    return slot(key);
}
void Pages::put(int64_t key, std::vector<uint64_t> page) {
    if (page.size() != 512) FATAL("Pages::put: a page of %zu words", page.size());
    map.put(key);
    slot(key) = std::move(page);
}
void Pages::poke(int64_t off, const std::vector<uint8_t>& bs) {
    for (size_t k = 0; k < bs.size(); k++) {
        const int64_t o = off + int64_t(k);
        std::vector<uint64_t>& pg = at(o / 4096 * 4096);
        const int w = int((o % 4096) / 8), sh = int(8 * (o % 8));
        pg[size_t(w)] = (pg[size_t(w)] & ~(uint64_t(0xFF) << sh)) | uint64_t(bs[k]) << sh;
    }
}
void Pages::poke64(int64_t off, uint64_t v) {
    std::vector<uint8_t> bs(8);
    for (int k = 0; k < 8; k++) bs[size_t(k)] = uint8_t(v >> (8 * k));
    poke(off, bs);
}
// ================================================================ the rig
const ReplayTables& Rig::shared_tables() {
    static const ReplayTables t = ReplayTables::load(ReplayTables::default_dir());
    return t;
}

Rig::Rig() : tables(shared_tables()), bench(tr, tables), t(bench.cycle) {
    tr.mem = &ms;
    tr.cold_mem_on_state_write = true;  // the memory system's model starts a segment cold (17.9.14)
    fill(1, 1);
    bench.guard_alloc = true;           // D6: the Tracer's cycle allocates nothing
}

std::vector<int64_t> Rig::bases(const Prog& p) const {
    std::vector<int64_t> out(p.parcels.size());
    int64_t idx = 1;
    for (size_t k = 0; k < out.size(); k++) {
        out[k] = idx;
        const auto& q = p.parcels[k];
        const uint32_t w = q.second == 4 ? q.first : rvc::code_word(q.first);
        idx += expansion_length(w);
    }
    return out;
}
int Rig::expansion_length(uint32_t w) const {
    if (w == 0) return 1;
    rowcore::Decoded d;
    rowcore::decode(w, tables.table, tables.rom_lo, d);
    switch (d.kind) {
        case rowcore::K_ROM: return int(rowcore::seq_remaining(tables.rom_lo[d.start])) + 1;
        case rowcore::K_CTRL: {
            const int m = tables.lookup(w);
            const unsigned rd0 = m >= 0 ? tables.rd0[size_t(m)] : 0;
            if (((w >> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) return 1;
            if (m < 0) FATAL("TracerRig: no template for 0x%08x", w);
            return int(tables.length[size_t(m)]);
        }
        default: return 1;
    }
}

// ================================================================ the images
void Rig::build_image(const Pages& pages, const std::vector<int64_t>& owned) {
    // Java's iteration orders (see tracer_rig.h): they decide where FileStore puts each page
    image = fs.create(2);
    for (int64_t key : pages.map.order()) fs.put_page(image, key / 4096, pages.page(key).data());
    copy = fs.cow_open(image);
    for (int64_t o : java_hash_order(owned)) fs.make_private(copy, o / 4096);
}

void Rig::build_map(const std::vector<std::pair<int64_t, int64_t>>& targets) {
    const int lines = 1 << map_k;
    std::vector<uint64_t> l(size_t(lines) * 16, ~uint64_t(0));
    for (const auto& e : targets) {
        const uint32_t key = uint32_t(int32_t(e.first - lowest));
        int64_t h = map_k == 0 ? 0 : int64_t((uint64_t(key) * uint64_t(map_mul) & 0xFFFFFFFFull) >> (32 - map_k));
        bool put = false;
        for (int n = 0; n < 3 && !put; n++) {
            for (int s2 = 0; s2 < 16; s2++)
                if (uint32_t(l[size_t(h) * 16 + size_t(s2)]) == 0xFFFFFFFFu) {
                    l[size_t(h) * 16 + size_t(s2)] = uint64_t(key) | uint64_t(e.second) << 32;
                    put = true;
                    break;
                }
            h = (h + int64_t(map_step)) & (lines - 1);
        }
        if (!put) FATAL("TracerRig: map too full");
    }
    const int pages_n = std::max(1, lines / 32);
    map = fs.create(FileStore::depth_for(int64_t(lines) * 128));
    for (int p = 0; p < pages_n; p++) {
        std::vector<uint64_t> pg(512, 0);
        for (int q = 0; q < 32 && p * 32 + q < lines; q++)
            std::memcpy(&pg[size_t(16 * q)], &l[size_t(p * 32 + q) * 16], 16 * sizeof(uint64_t));
        fs.put_page(map, map.depth == 0 ? 0 : p, pg.data());
    }
}

// ================================================================ the controller
void Rig::record_words(int64_t pc, int32_t instr_idx, int offset, const uint64_t regs[128], size_t advice_values,
                       uint64_t out[2 * Tracer::REC_WORDS]) const {
    out[0] = uint64_t(pc);
    out[1] = uint64_t(uint32_t(instr_idx)) | uint64_t(offset & 0xFFFF) << 32;
    out[2] = 0;
    out[3] = uint64_t(advice_values) << 38;
    for (int k = 0; k < 128; k++) out[4 + k] = regs[k];
    Tracer::files_word(copy.root, copy.depth, map.root, map.depth, log2k, lowest, out + 2 * Tracer::REC_FILES);
    out[2 * Tracer::REC_BITMAP] = out[2 * Tracer::REC_BITMAP + 1] = 0;
}

void Rig::load_record(int64_t pc, int32_t instr_idx, int offset, const uint64_t regs[128],
                      const std::vector<uint64_t>& advice) {
    uint64_t rec[2 * Tracer::REC_WORDS];
    record_words(pc, instr_idx, offset, regs, advice.size(), rec);
    bench.segment(rec, advice, 0);
}

bool Rig::window(uint32_t run_config, int64_t max_cycles, WindowOut& out) {
    word(Tracer::W_RUN_CONFIG, run_config);
    bench.limit_per_run = max_cycles;
    bench.run(last);
    out = WindowOut();
    out.record = last.queue[0];
    out.code = last.queue[1];
    out.rlog = last.queue[2];
    out.wlog = last.queue[3];
    if (last.done) {
        if (out.record.size() != 2 * size_t(Tracer::REC_WORDS))
            FATAL("TracerRig: output queue 0 delivered %zu entries", out.record.size() / 2);
        units::streamword::parse(out.record[2], out.record[3], out.sizes);
    }
    return last.done;
}

// ================================================================ reading the output
std::vector<uint64_t> Rig::stream_values(const std::vector<uint64_t>& words, int values, int bits) {
    std::vector<uint64_t> v(static_cast<size_t>(values));
    for (int k = 0; k < values; k++) {
        const int64_t pos = int64_t(k) * bits;
        const size_t wd = size_t(pos / 128);
        const int bb = int(pos % 128);
        uint64_t x = bb < 64 ? words[2 * wd] >> bb : words[2 * wd + 1] >> (bb - 64);
        if (bb < 64 && bb + bits > 64) x |= words[2 * wd + 1] << (64 - bb);
        v[size_t(k)] = bits == 64 ? x : x & 0xFFFFFFFFull;
    }
    return v;
}

namespace {
double pct(int64_t a, int64_t b) { return b == 0 ? 0 : 100.0 * double(a) / double(b); }
}  // namespace

std::string Rig::mem_summary(const MemSystem& m) {
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "reads %lld, writes %lld; index %lld (%s%% hit), data %lld (%s%% hit), LPDDR reads %lld; walk %lld "
                  "cycles, data %lld; early evictions %lld, write-allocates %lld, read waits %lld (%lld cycles)",
                  (long long)m.reads, (long long)m.writes, (long long)m.index_accesses,
                  jfmt::fixed(pct(m.index_hits, m.index_accesses), 2).c_str(), (long long)m.data_accesses,
                  jfmt::fixed(pct(m.data_hits, m.data_accesses), 2).c_str(), (long long)m.lpddr_reads,
                  (long long)m.walk_cycles, (long long)m.data_cycles, (long long)m.early_evictions,
                  (long long)m.write_allocates, (long long)m.read_waits, (long long)m.read_wait_cycles);
    return buf;
}

}  // namespace tracer_rig
