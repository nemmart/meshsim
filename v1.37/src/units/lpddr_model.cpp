// lpddr_model.cpp: LpddrModel (lpddr_model.h).
#include "units/lpddr_model.h"

#include <cstring>

#include "base/fatal.h"

namespace units {

using tracer::FileStore;

LpddrModel::LpddrModel(FileStore* s, const axi::LpddrTiming& t, unsigned bits) : store(s), timing(t), address_bits(bits) {
    if (!store) FATAL("lpddr_model: no file store");
    if (t.max_reads < 1 || t.max_reads > MAX_BURSTS || t.max_writes < 1 || t.max_writes > MAX_BURSTS)
        FATAL("lpddr_model: max_reads and max_writes must be 1 to %u", MAX_BURSTS);
    if (bits < 24 || bits > 36) FATAL("lpddr_model: LPDDR_ADDRESS_BITS must be 24 to 36");
    if (t.jitter && (t.jitter_lo < 1 || t.jitter_hi < t.jitter_lo)) FATAL("lpddr_model: jitter's range");
    for (Write& w : writes_) w.data = new uint64_t[2 * axi::MAX_BURST_BEATS]();
    reset();
}

LpddrModel::~LpddrModel() {
    for (Write& w : writes_) delete[] w.data;
}

void LpddrModel::reset() {
    for (Read& r : reads_) r.used = false;
    for (Write& w : writes_) { w.used = false; w.complete = false; }
    n_reads_ = n_writes_ = 0;
    r_cur_ = b_cur_ = -1;
    r_rr_ = axi::CLIENTS - 1;
    w_head_ = w_n_ = 0;
    seq_ = 0;
    rng_ = timing.seed ? timing.seed : 1;
    out_ = axi::SlaveOut{};
    out_.arready = out_.awready = true;
    prev_m_ = axi::MasterOut{};
    prev_ar_wait_ = prev_aw_wait_ = prev_w_wait_ = false;
    for (unsigned k = 0; k < n_faults_; k++) fault_left_[k] = faults_[k].bursts;
}

void LpddrModel::add_fault(const axi::LpddrFault& f) {
    if (n_faults_ == MAX_FAULTS) FATAL("lpddr_model: more than %u faults", MAX_FAULTS);
    faults_[n_faults_] = f;
    fault_left_[n_faults_] = f.bursts;
    n_faults_++;
}

// ---- helpers
void LpddrModel::check_burst(const char* ch, uint64_t addr, unsigned len, unsigned id) const {
    const uint64_t bytes = uint64_t(len + 1) * axi::BEAT_BYTES;
    if (addr % axi::BEAT_BYTES) FATAL("lpddr_model: %s: an address not a multiple of 16: 0x%llx", ch, (unsigned long long)addr);
    if (addr % axi::BOUNDARY_BYTES + bytes > axi::BOUNDARY_BYTES)
        FATAL("lpddr_model: %s: a burst across a 4 KB boundary: 0x%llx, %u beats", ch, (unsigned long long)addr, len + 1);
    if (id >= axi::CLIENTS) FATAL("lpddr_model: %s: ID %u is no client's", ch, id);
}

uint8_t LpddrModel::fault_for(uint64_t addr, unsigned beats, bool write) {
    const uint64_t last = addr + uint64_t(beats) * axi::BEAT_BYTES - 1;
    if (last >> address_bits) return axi::DECERR;                      // past LPDDR's end
    for (unsigned k = 0; k < n_faults_; k++) {
        const axi::LpddrFault& f = faults_[k];
        if (write ? !f.writes : !f.reads) continue;
        if (last < f.first || addr > f.last) continue;
        if (f.bursts != 0 && fault_left_[k] == 0) continue;
        if (f.bursts != 0) fault_left_[k]--;
        return f.resp;
    }
    return axi::OKAY;
}

// xorshift32 (the RTL tests' generator, tests/axi4_slave.h)
static uint32_t next_random(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

unsigned LpddrModel::latency(unsigned fixed) {
    if (!timing.jitter) return fixed;
    return timing.jitter_lo + next_random(rng_) % (timing.jitter_hi - timing.jitter_lo + 1);
}
bool LpddrModel::drop() { return timing.jitter && next_random(rng_) % 4 == 0; }

void LpddrModel::read_beat(uint64_t addr, uint64_t* w) {
    const int32_t page = int32_t((addr >> 12) << 5);                     // the page's address in 128-byte units
    if (!store->has_page(page)) { w[0] = w[1] = 0; unwritten_reads++; return; }
    const uint64_t* p = store->raw_page(page);
    const unsigned at = unsigned(addr & 4095) / 8;
    w[0] = p[at]; w[1] = p[at + 1];
}

// A write's beats into the memory: one page (a burst never crosses 4 KB), read, changed and written whole, so the file
// store's own bookkeeping (its fingerprint sum) sees it.
void LpddrModel::land(const Write& w) {
    if (w.resp != axi::OKAY) return;                                     // a faulty write writes nothing
    const int32_t page = int32_t((w.addr >> 12) << 5);
    uint64_t buf[FileStore::PAGE_WORDS];
    store->read_page(page, buf);
    std::memcpy(buf + (w.addr & 4095) / 8, w.data, size_t(w.beats) * 16);
    store->write_page(page, buf);
}

int LpddrModel::oldest_read(unsigned id) const {
    int best = -1;
    for (unsigned k = 0; k < MAX_BURSTS; k++)
        if (reads_[k].used && reads_[k].id == id && (best < 0 || reads_[k].seq < reads_[best].seq)) best = int(k);
    return best;
}
int LpddrModel::oldest_write(unsigned id) const {
    int best = -1;
    for (unsigned k = 0; k < MAX_BURSTS; k++)
        if (writes_[k].used && writes_[k].id == id && (best < 0 || writes_[k].seq < writes_[best].seq)) best = int(k);
    return best;
}

// ---- the edge that ends cycle t
void LpddrModel::edge(int64_t t, const axi::MasterOut& m) {
    const axi::SlaveOut o = out_;                                        // the outputs of t

    // The handshake rules (AXI section 1): a request that waited is raised again, unchanged.
    if (prev_ar_wait_ && (!m.arvalid || m.araddr != prev_m_.araddr || m.arlen != prev_m_.arlen || m.arid != prev_m_.arid))
        FATAL("lpddr_model: AR withdrawn or changed while it waited, at %lld", (long long)t);
    if (prev_aw_wait_ && (!m.awvalid || m.awaddr != prev_m_.awaddr || m.awlen != prev_m_.awlen || m.awid != prev_m_.awid))
        FATAL("lpddr_model: AW withdrawn or changed while it waited, at %lld", (long long)t);
    if (prev_w_wait_ && (!m.wvalid || m.wdata[0] != prev_m_.wdata[0] || m.wdata[1] != prev_m_.wdata[1] || m.wlast != prev_m_.wlast))
        FATAL("lpddr_model: a W beat withdrawn or changed while it waited, at %lld", (long long)t);
    prev_ar_wait_ = m.arvalid && !o.arready;
    prev_aw_wait_ = m.awvalid && !o.awready;
    prev_w_wait_ = m.wvalid && !o.wready;
    prev_m_ = m;

    // ---- what left in t: an R beat, a B response (RREADY and BREADY are 1)
    if (o.rvalid) {
        Read& r = reads_[r_cur_];
        beats_read++; read_busy_cycles++;
        if (++r.sent == r.beats) { r.used = false; n_reads_--; r_cur_ = -1; }
    }
    if (o.bvalid) { writes_[b_cur_].used = false; writes_[b_cur_].complete = false; n_writes_--; b_cur_ = -1; }

    // ---- what came in t
    if (m.arvalid && o.arready) {
        check_burst("AR", m.araddr, m.arlen, m.arid);
        int k = 0;
        while (reads_[k].used) k++;
        Read& r = reads_[k];
        r.used = true; r.id = m.arid; r.addr = m.araddr; r.beats = m.arlen + 1u; r.sent = 0; r.seq = seq_++;
        r.due = t + latency(timing.read_latency);
        r.resp = fault_for(r.addr, r.beats, false);
        if (r.resp != axi::OKAY) error_bursts++;
        n_reads_++; read_bursts++;
    }
    if (m.awvalid && o.awready) {
        check_burst("AW", m.awaddr, m.awlen, m.awid);
        int k = 0;
        while (writes_[k].used) k++;
        Write& w = writes_[k];
        w.used = true; w.complete = false; w.id = m.awid; w.addr = m.awaddr; w.beats = m.awlen + 1u; w.got = 0;
        w.seq = seq_++; w.aw_t = t;
        w.resp = fault_for(w.addr, w.beats, true);
        if (w.resp != axi::OKAY) error_bursts++;
        n_writes_++; write_bursts++;
        w_order_[(w_head_ + w_n_) % MAX_BURSTS] = k; w_n_++;
    }
    if (m.wvalid && o.wready) {
        if (w_n_ == 0) FATAL("lpddr_model: a W beat taken with no write waiting for it, at %lld", (long long)t);
        Write& w = writes_[w_order_[w_head_]];
        w.data[2 * w.got] = m.wdata[0]; w.data[2 * w.got + 1] = m.wdata[1];
        w.got++; beats_written++; write_busy_cycles++;
        if (m.wlast != (w.got == w.beats))
            FATAL("lpddr_model: wlast %s on beat %u of %u, at %lld", m.wlast ? "set" : "clear", w.got, w.beats, (long long)t);
        if (w.got == w.beats) {
            w.complete = true;
            w.due = (t > w.aw_t ? t : w.aw_t) + latency(timing.write_latency);
            w_head_ = (w_head_ + 1) % MAX_BURSTS; w_n_--;
        }
    }

    // ---- the outputs of t + 1
    axi::SlaveOut n;
    const int64_t next = t + 1;
    // B first, so that a write lands before a read beat of the same cycle takes its data
    {
        int best = -1;
        for (unsigned id = 0; id < axi::CLIENTS; id++) {
            const int k = oldest_write(id);
            if (k < 0 || !writes_[k].complete || writes_[k].due > next) continue;
            if (best < 0 || writes_[k].due < writes_[best].due || (writes_[k].due == writes_[best].due && writes_[k].seq < writes_[best].seq))
                best = k;
        }
        if (best >= 0) {
            b_cur_ = best;
            land(writes_[best]);
            n.bvalid = true; n.bresp = writes_[best].resp; n.bid = writes_[best].id;
        }
    }
    // R: the burst going on, or the next ID round robin (jitter: at random) among those whose oldest read is due
    if (r_cur_ < 0) {
        int due_ids[axi::CLIENTS], n_due = 0;
        for (unsigned k = 1; k <= axi::CLIENTS; k++) {
            const unsigned id = (r_rr_ + k) % axi::CLIENTS;
            const int r = oldest_read(id);
            if (r >= 0 && reads_[r].due <= next) due_ids[n_due++] = r;
        }
        if (n_due > 0) {
            r_cur_ = due_ids[timing.jitter ? next_random(rng_) % unsigned(n_due) : 0];
            r_rr_ = reads_[r_cur_].id;
        }
    }
    if (r_cur_ >= 0) {
        const Read& r = reads_[r_cur_];
        n.rvalid = true; n.rid = r.id; n.rresp = r.resp; n.rlast = r.sent + 1 == r.beats;
        if (r.resp != axi::OKAY) n.rdata[0] = n.rdata[1] = axi::FAULT_DATA;
        else read_beat(r.addr + uint64_t(r.sent) * axi::BEAT_BYTES, n.rdata);
    }
    n.arready = n_reads_ < timing.max_reads && !drop();
    n.awready = n_writes_ < timing.max_writes && !drop();
    n.wready = w_n_ > 0 && !drop();
    out_ = n;
}

}  // namespace units
