// tracer_rig.cpp (WORKER21): TracerRig.java, member for member. See tracer_rig.h.
#include "tracer_rig.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>

#include "base/alloc_guard.h"
#include "base/fatal.h"
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
// HashMap: a table of 16 bins growing by doubling when size > 0.75 n, or when a bin reaches 9 entries while n < 64
// (treeifyBin); a bin's entries in insertion order (put appends; a resize splits a bin keeping relative order). So the
// order is the entries sorted by their bin in the final table, insertion order within a bin. A treeified bin (9 entries
// at n >= 64) iterates otherwise: refused.
std::vector<int64_t> java_hash_order(const std::vector<int64_t>& keys) {
    std::vector<int64_t> in;
    for (int64_t k : keys)
        if (std::find(in.begin(), in.end(), k) == in.end()) in.push_back(k);
    auto spread = [](int64_t k) {
        const uint64_t v = uint64_t(k);
        uint32_t h = uint32_t(v ^ (v >> 32));  // Long.hashCode
        return h ^ (h >> 16);                  // HashMap.hash
    };
    size_t n = 16;
    std::vector<int64_t> so_far;
    for (int64_t k : in) {
        // the bin's size with k appended, in the current table
        size_t bin = 0;
        for (int64_t x : so_far) bin += (spread(x) & (n - 1)) == (spread(k) & (n - 1));
        so_far.push_back(k);
        if (bin + 1 >= 9) {
            if (n < 64) n *= 2;
            else FATAL("java_hash_order: a HashMap bin of %zu entries is treeified (order not reproduced)", bin + 1);
        }
        if (so_far.size() > n * 3 / 4) n *= 2;
    }
    std::vector<int64_t> out = in;
    std::stable_sort(out.begin(), out.end(),
                     [&](int64_t a, int64_t c) { return (spread(a) & (n - 1)) < (spread(c) & (n - 1)); });
    return out;
}

// ================================================================ Pages
std::vector<uint64_t>& Pages::at(int64_t key) {
    for (size_t k = 0; k < keys.size(); k++)
        if (keys[k] == key) return pages[k];
    keys.push_back(key);
    pages.emplace_back(512, 0);
    return pages.back();
}
void Pages::put(int64_t key, std::vector<uint64_t> page) {
    if (page.size() != 512) FATAL("Pages::put: a page of %zu words", page.size());
    at(key) = std::move(page);
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

Rig::Rig() : tables(shared_tables()) {
    tr.mem = &ms;
    tr.cold_mem_on_state_write = true;  // ref: tr.onStateWrite = ms::cold
    port_.cap = 1024;
    port_.e = new Pkt[size_t(port_.cap)];
    port_exp_.cap = 512;
    port_exp_.e = new Pkt[size_t(port_exp_.cap)];
    for (auto& g : got_) g = new uint64_t[2 * size_t(GOT_CAP)];
    for (const auto& w : tables.words) exp_buf_cap_ = std::max(exp_buf_cap_, int(w.size()));
    exp_buf_ = new uint64_t[size_t(exp_buf_cap_ > 0 ? exp_buf_cap_ : 2)];
}
Rig::~Rig() {
    delete[] port_.e;
    delete[] port_exp_.e;
    for (auto& g : got_) delete[] g;
    delete[] exp_buf_;
}

void Rig::Ring::push(const Pkt& p) {
    if (n == cap) FATAL("TracerRig: the port's queue is full (%d packets)", cap);
    e[(head + n) % cap] = p;
    n++;
}
Rig::Pkt Rig::Ring::pop() {
    const Pkt p = e[head];
    head = (head + 1) % cap;
    n--;
    return p;
}
void Rig::port_add(uint32_t offset, uint64_t lo, uint64_t hi) { port_.push({offset, lo, hi}); }

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
            if (m < 0) FATAL("TracerRig: no template for 0x%08x (ref: an index out of bounds)", w);
            return int(tables.length[size_t(m)]);
        }
        default: return 1;
    }
}

// ================================================================ the images
void Rig::build_image(const Pages& pages, const std::vector<int64_t>& owned) {
    image = fs.create(2);
    for (int64_t key : java_hash_order(pages.keys)) {
        const size_t k = size_t(std::find(pages.keys.begin(), pages.keys.end(), key) - pages.keys.begin());
        fs.put_page(image, key / 4096, pages.pages[k].data());
    }
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
void Rig::dma_start(int q, int64_t n) {
    tr.out_start(t, q, n);
    dma_q_ = q;
    dma_left_ = n;
}

void Rig::step() {
    if (dma_q_ >= 0 && dma_left_ > 0 && tr.out_ready(t, dma_q_)) {
        if (got_n_[dma_q_] == GOT_CAP) FATAL("TracerRig: queue %d delivered more than %d words", dma_q_, GOT_CAP);
        uint64_t* w = got_[dma_q_] + 2 * size_t(got_n_[dma_q_]);
        tr.out_take(t, dma_q_, w);
        got_n_[dma_q_]++;
        dma_left_--;
        dma_packets++;
        if (dma_left_ == 0) dma_q_ = -1;
    }
    if (port_.n > 0 || port_exp_.n > 0) {
        const Pkt p = port_exp_.n > 0 ? port_exp_.pop() : port_.pop();
        tr.accept(t, SRC, p.off, 2 /* LinkWord.SIZE_128 */, p.lo, p.hi);
    }
    tr.step(t);
    if (on_step) on_step(*this, on_step_ctx);
    t++;
}

void Rig::drain_port() {
    while (port_.n > 0 || port_exp_.n > 0) step();
    for (int k = 0; k < Tracer::RP_LATENCY + 1; k++) step();
}

void Rig::load_tables() {
    for (int p = 0; p < 256; p++) {
        uint64_t lo = 0, hi = 0;
        for (int k = 0; k < 4; k++) lo |= uint64_t(tables.table[8 * p + k]) << (16 * k);
        for (int k = 0; k < 4; k++) hi |= uint64_t(tables.table[8 * p + 4 + k]) << (16 * k);
        port_.push({uint32_t(0x1000 + 16 * p), lo, hi});
    }
    for (int q = 0; q < 512; q++) port_.push({uint32_t(0x2000 + 16 * q), tables.rom_lo[q], tables.rom_hi[q]});
    sim::begin_run();
    drain_port();
    word(Tracer::W_HASH_MUL, map_mul);
    word(Tracer::W_HASH_STEP, map_step);
    word(Tracer::W_HASH_LINES, uint32_t(map_k));
    sim::end_run();
}

void Rig::word(int w, uint32_t v) {
    if (tr.write_word(t, w, v) != 1) FATAL("TracerRig: word %d refused", w);
    step();
}

void Rig::load_record(int64_t pc, int32_t instr_idx, int offset, const uint64_t regs[128],
                      const std::vector<uint64_t>& advice) {
    advice_ = advice;
    advice_next_ = 0;
    // a new segment: the last segment's expansion is abandoned (the word-0 write empties the queue and sets the
    // credits to 2); answering those credits with the old template's next chunk, before this segment's first new
    // expansion, would put stale words ahead of the new ones (found at 2^29, Cyclone segment 16, Quest 110)
    exp_words_ = nullptr;
    exp_next_ = 0;
    port_.push({0x000, uint64_t(pc), (uint64_t(uint32_t(instr_idx))) | uint64_t(offset & 0xFFFF) << 32});
    port_.push({0x010, 0, uint64_t(advice.size()) << 38});
    for (int k = 0; k < 64; k++) port_.push({uint32_t(0x020 + 16 * k), regs[2 * k], regs[2 * k + 1]});
    uint64_t fw[2];
    Tracer::files_word(copy.root, copy.depth, map.root, map.depth, log2k, lowest, fw);
    port_.push({0x420, fw[0], fw[1]});
    port_.push({0x430, 0, 0});
    sim::begin_run();
    drain_port();
    send_advice();
    send_advice();  // preload: two half-queue refills on the word-0 write's credits
    tr.events().consume(1u << Tracer::EV_ADVICE);
    tr.events().consume(1u << Tracer::EV_ADVICE);
    drain_port();
    sim::end_run();
}

void Rig::send_advice() {
    const int len = int(advice_.size());
    const int n = std::min(256, len - advice_next_);
    for (int k = 0; k < n; k += 2, advice_next_ += 2)
        port_.push({0x200000, advice_[size_t(advice_next_)],
                    advice_next_ + 1 < len ? advice_[size_t(advice_next_ + 1)] : 0});
    advice_next_ = std::min(advice_next_, len);
}

void Rig::send_exp() {
    if (!exp_words_) return;
    const int n = std::min(128, (exp_len_ - exp_next_) / 2);
    for (int k = 0; k < n; k++, exp_next_ += 2)
        port_exp_.push({0x400000, exp_words_[exp_next_], exp_words_[exp_next_ + 1]});
}

bool Rig::window(uint32_t run_config, int64_t max_cycles, WindowOut& out) {
    sim::begin_run();
    word(Tracer::W_RUN_CONFIG, run_config);
    tr.run(t);
    step();
    const uint32_t mask = 1u << Tracer::EV_RUN_DONE | 1u << Tracer::EV_ADVICE | 1u << Tracer::EV_EXP |
                          1u << Tracer::EV_NEWEXP;
    for (int q = 0; q < 4; q++) got_n_[q] = 0;
    int q0step = 0, rr = 1;
    int64_t sent[4] = {0, 0, 0, 0};
    static constexpr int q0sizes[4] = {1, 1, 65, 1};  // word 0 to tile n+1, word 1 to tile n, words 2-66 to n+1, 67 to n
    const int64_t end = t + max_cycles;
    while (t < end) {
        tr.sync(t);
        const uint32_t v = tr.events().value(t);
        // the service requests (4.21.6): queue 0 in its four transfers first, then the streams' credits, round-robin
        if (dma_q_ < 0 && (hold == 0 || t % hold == 0)) {
            const bool streams = (v & 0b111u << (Tracer::EV_SVC_STATE + 1)) != 0;
            if ((v & 1u << Tracer::EV_SVC_STATE) != 0 && q0step < 4 && !(state_last && streams)) {
                if (q0_one) {
                    dma_start(0, 68);
                    q0step = 4;
                } else dma_start(0, q0sizes[q0step++]);
            } else {
                for (int k = 0; k < 3; k++) {
                    const int q = has_order ? order[k] : 1 + (rr + k - 1) % 3;
                    if ((v & 1u << (Tracer::EV_SVC_STATE + q)) == 0) continue;
                    uint32_t len = 0;
                    if (!tr.word(t, Tracer::W_CODE_LENGTH + q - 1, len)) FATAL("TracerRig: a length word reads null");
                    const int64_t n = std::min<int64_t>(128, int64_t(int32_t(len)) - sent[q]);
                    dma_start(q, n);
                    sent[q] += n;
                    rr = q % 3 + 1;
                    break;
                }
            }
        }
        uint32_t c = v & mask;
        if (c != 0) {
            tr.events().consume(c);
            if ((c & 1u << Tracer::EV_NEWEXP) != 0) {
                uint32_t w = 0, offw = 0;
                tr.word(t, Tracer::W_EXP_OPCODE, w);
                tr.word(t, Tracer::W_EXP_OFFSET, offw);
                const int off = int(int32_t(offw));
                const int m = tables.lookup(w);
                if (m < 0) {
                    no_template++;
                    no_template_w = w;
                    exp_words_ = exp_buf_;
                    exp_len_ = 0;
                } else {
                    if (tables.cls[size_t(m)] == 1) word(Tracer::W_EXP_TEMPLATE, 0x80000000u | uint32_t(m));
                    const std::vector<uint64_t>& tpl = tables.words[size_t(m)];
                    const uint64_t rd0 = tables.rd0[size_t(m)];
                    if (((w >> 7) & 31) == 0 && (rd0 == 1 || rd0 == 3)) {
                        nop_words_[0] = (tables.rom_lo[1] & ~(uint64_t(3) << 46)) | rd0 << 46;
                        nop_words_[1] = tables.rom_hi[1];
                        exp_words_ = nop_words_;
                        exp_len_ = 2;
                        exp_next_ = 0;
                    } else if (off > 0) {
                        std::copy(tpl.begin(), tpl.end(), exp_buf_);
                        exp_buf_[2 * off] = (exp_buf_[2 * off] & ~(uint64_t(3) << 46)) | rd0 << 46;
                        exp_words_ = exp_buf_;
                        exp_len_ = int(tpl.size());
                        exp_next_ = 2 * off;
                    } else {
                        exp_words_ = tpl.data();
                        exp_len_ = int(tpl.size());
                        exp_next_ = 0;
                    }
                }
                if ((c & 1u << Tracer::EV_EXP) == 0) tr.events().consume(1u << Tracer::EV_EXP);
                c &= ~(1u << Tracer::EV_EXP);
                send_exp();
            }
            if ((c & 1u << Tracer::EV_EXP) != 0) send_exp();
            if ((c & 1u << Tracer::EV_ADVICE) != 0) send_advice();
            if ((c & 1u << Tracer::EV_RUN_DONE) != 0) {
                step();
                sim::end_run();
                collect(out);
                return true;
            }
        }
        if (tr.stopped()) {
            for (int k = 0; k < 3; k++) step();
            sim::end_run();
            return false;
        }
        step();
    }
    sim::end_run();
    return false;
}

// The window's output as the DMA delivered it: queue 0's 68 words, the three streams.
void Rig::collect(WindowOut& o) const {
    auto flat = [this](int q) { return std::vector<uint64_t>(got_[q], got_[q] + 2 * size_t(got_n_[q])); };
    o.record = flat(0);
    o.code = flat(1);
    o.rlog = flat(2);
    o.wlog = flat(3);
    if (o.record.size() != 2 * size_t(Tracer::REC_WORDS))
        FATAL("TracerRig: queue 0 delivered %zu words", o.record.size() / 2);
    units::streamword::parse(o.record[2], o.record[3], o.sizes);
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
// Java's %.2f: the double's decimal digits (shortest round trip, as Java's FloatingDecimal gives for these values)
// rounded HALF_UP. printf's %.2f rounds the binary value instead, which differs on decimal ties such as 0.125.
std::string java_2f(double x) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed);
    std::string s(buf, res.ptr);
    const size_t dot = s.find('.');
    std::string ip = dot == std::string::npos ? s : s.substr(0, dot);
    std::string fp = dot == std::string::npos ? "" : s.substr(dot + 1);
    while (fp.size() < 3) fp += '0';
    std::string digits = ip + fp.substr(0, 2);
    if (fp[2] >= '5') {  // HALF_UP: carry through the digits
        int k = int(digits.size()) - 1;
        while (k >= 0) {
            if (digits[size_t(k)] == '9') digits[size_t(k)] = '0', k--;
            else { digits[size_t(k)]++; break; }
        }
        if (k < 0) digits.insert(digits.begin(), '1');
    }
    return digits.substr(0, digits.size() - 2) + "." + digits.substr(digits.size() - 2);
}
double pct(int64_t a, int64_t b) { return b == 0 ? 0 : 100.0 * double(a) / double(b); }
}  // namespace

std::string Rig::mem_summary(const MemSystem& m) {
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "reads %lld, writes %lld; index %lld (%s%% hit), data %lld (%s%% hit), LPDDR reads %lld; walk %lld "
                  "cycles, data %lld; early evictions %lld, write-allocates %lld, read waits %lld (%lld cycles)",
                  (long long)m.reads, (long long)m.writes, (long long)m.index_accesses,
                  java_2f(pct(m.index_hits, m.index_accesses)).c_str(), (long long)m.data_accesses,
                  java_2f(pct(m.data_hits, m.data_accesses)).c_str(), (long long)m.lpddr_reads,
                  (long long)m.walk_cycles, (long long)m.data_cycles, (long long)m.early_evictions,
                  (long long)m.write_allocates, (long long)m.read_waits, (long long)m.read_wait_cycles);
    return buf;
}

}  // namespace tracer_rig
