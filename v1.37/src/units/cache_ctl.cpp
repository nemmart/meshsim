// cache_ctl.cpp: CacheCtl (cache_ctl.h).
#include "units/cache_ctl.h"

#include <cstdio>
#include <cstring>

#include "base/fatal.h"

namespace units {
namespace cache {

using lookup::NO_PAGE;
using lookup::OWNED;

namespace {
constexpr uint64_t ZEROS[LINE_WORDS] = {};
uint32_t entry_of(const uint64_t* line, unsigned i) { return uint32_t(line[(i & 31) >> 1] >> (32 * (i & 1))); }
void set_entry_of(uint64_t* line, unsigned i, uint32_t e) {
    const unsigned s = 32 * (i & 1);
    uint64_t& w = line[(i & 31) >> 1];
    w = (w & ~(uint64_t(0xFFFFFFFFu) << s)) | uint64_t(e) << s;
}
}  // namespace

void CacheCtl::Latency::add(uint64_t c) {
    n++; sum += c;
    if (c > max) max = c;
    unsigned b = 0;
    while (b < 15 && (uint64_t(1) << (b + 1)) <= c) b++;
    bucket[b]++;
}

CacheCtl::CacheCtl(const Config& c) : cfg(c) { reset(); }

void CacheCtl::reset() {
    tags_.reset();
    for (unsigned s = 0; s < SETS; s++) tags_.poke(s) = TagRow{};
    data_.reset();
    for (Held& h : held_) h = Held{};
    rr_ = SOURCES - 1; prio_ = -1; error_ = false;
    tag_read_t_ = mh_tag_write_t_ = INT64_MIN;
    n_diverged_ = 0;
    wst_ = W_IDLE; mst_ = M_IDLE;
    m_ar_ = false;
    w_busy_ = false; kinds_head_ = kinds_n_ = 0;
    s_valid_ = false;
    out_ = Outputs{};
    for (unsigned p = 0; p < PORTS; p++) out_.ready[p] = true;
    out_.fr_ready = out_.fw_ready = true;
}

bool CacheCtl::idle() const {
    for (const Held& h : held_) if (h.valid) return false;
    return wst_ == W_IDLE && mst_ == M_IDLE && !w_busy_ && kinds_n_ == 0 && !s_valid_ && !out_.reply.valid && !out_.frr_valid &&
           !out_.fwr_valid;
}

// ================================================================ the walker's pieces
unsigned CacheCtl::index_at(unsigned k) const {
    const Held& h = held_[src_];
    const uint64_t p = h.op == J_FREAD || h.op == J_FWRITE ? h.pos >> 5 : h.pos;
    return unsigned(p >> (10 * (h.depth - 1 - k))) & 1023;
}

unsigned CacheCtl::cls() const {
    const Held& h = held_[src_];
    if (h.op != J_FREAD && h.op != J_FWRITE) return LOOKUP_INDEX;
    const bool map = int32_t(h.root) == cfg.map_root_for_stats;
    const bool index = k_ < h.depth;
    return map ? (index ? MAP_INDEX : MAP_DATA) : (index ? MEM_INDEX : MEM_DATA);
}

void CacheCtl::finish(int64_t t, unsigned s) {
    Held& h = held_[s];
    const uint64_t c = uint64_t(t + 1 - h.t_accept);   // the answer is on its port in t + 1
    switch (h.op) {
        case J_LOOK_UP: lookup_latency.add(c); lookups++; break;
        case J_UPDATE: update_latency.add(c); updates++; break;
        case J_FREAD: fill_read_latency.add(c); fill_reads++; break;
        case J_FWRITE: fill_write_latency.add(c); fill_writes++; break;
        default: break;
    }
    h = Held{};
    if (s == S_FWRITE) held_[S_FREAD].behind_write = false;   // the read waited for this write (23.6.4)
}

void CacheCtl::reply(int64_t t, uint8_t status, uint8_t level, uint32_t entry) {
    walk_lines[k_ < 4 ? k_ : 4]++;
    if (bus_taken_) { rstatus_ = status; rlevel_ = level; rentry_ = entry; wst_ = W_REPLY; return; }
    next_.reply.valid = true; next_.reply.client = uint8_t(src_);
    next_.reply.status = status; next_.reply.level = level; next_.reply.entry = entry;
    bus_taken_ = true;
    finish(t, src_);
    wst_ = W_IDLE;
}

void CacheCtl::fill_read_response(int64_t t, uint8_t status, const uint64_t* d) {
    walk_lines[k_ < 4 ? k_ : 4]++;
    next_.frr_valid = true; next_.frr_status = status;
    std::memcpy(next_.frr_data, d, sizeof next_.frr_data);
    finish(t, src_);
    wst_ = W_IDLE;
}

void CacheCtl::fill_write_response(int64_t t, uint8_t status) {
    walk_lines[k_ < 4 ? k_ : 4]++;
    next_.fwr_valid = true; next_.fwr_status = status;
    finish(t, src_);
    wst_ = W_IDLE;
}

void CacheCtl::abandon(bool for_slot) {
    Held& h = held_[src_];
    if (for_slot) h.wait_slot = true; else h.wait_mh = true;
    replays++;
    wst_ = W_IDLE;
}

void CacheCtl::check_clean(int64_t line, const uint64_t* d) const {
    for (unsigned k = 0; k < n_diverged_; k++) if (diverged_[k] == line) return;   // its write failed: LPDDR's is undefined
    const int32_t page = int32_t(line & ~int64_t(31));
    const uint64_t* l = check_store->has_page(page) ? check_store->raw_page(page) + 16 * (line & 31) : ZEROS;
    if (std::memcmp(l, d, 16 * sizeof(uint64_t)) != 0)
        FATAL("cache: a stale line: line 0x%llx is clean in the cache and differs from LPDDR", (unsigned long long)line);
}

void CacheCtl::issue_tag_read(int64_t t) {
    set_ = unsigned(x_) & (SETS - 1);
    tags_.read(t, set_);
    tag_read_t_ = t;
    wst_ = W_CMP;
}

void CacheCtl::start(int64_t t, unsigned s) {
    src_ = s;
    Held& h = held_[s];
    k_ = 0; cur_ = h.root; owned_all_ = true;
    const unsigned d = h.depth;
    switch (h.op) {
        case J_DISCARD:
            dk_ = 0; x_ = int64_t(h.root & ~31u); discards++;
            issue_tag_read(t);
            return;
        case J_LOOK_UP:
        case J_UPDATE:
            if (d == 0 || h.pos >> (10 * d) != 0 || (h.op == J_UPDATE && h.level >= d)) { reply(t, lookup::BEYOND, 0, 0); return; }
            x_ = cur_ + (index_at(0) >> 5);
            issue_tag_read(t);
            return;
        case J_FREAD:
        case J_FWRITE:
            if (h.pos >> (5 + 10 * d) != 0) {
                if (h.op == J_FREAD) fill_read_response(t, lookup::BEYOND, ZEROS);
                else fill_write_response(t, FILL_WRITE_ERROR);
                return;
            }
            x_ = d == 0 ? cur_ + int64_t(h.pos & 31) : cur_ + (index_at(0) >> 5);
            issue_tag_read(t);
            return;
        default:
            FATAL("cache: a request with op %u", unsigned(h.op));
    }
}

void CacheCtl::step_cmp(int64_t t) {
    Held& h = held_[src_];
    // the row read before the miss handler's tag write to the same set landed: read it again (in the cycle after the
    // write, so that the read sees it)
    if (mh_tag_write_set_ == set_ && mh_tag_write_t_ >= tag_read_t_) {
        if (mh_tag_write_t_ < t) { tags_.read(t, set_); tag_read_t_ = t; }
        return;
    }
    row_ = tags_.q();
    const uint32_t tag = uint32_t(x_ >> 9);
    int hit = -1;
    for (unsigned w = 0; w < WAYS; w++)
        if (row_.is_valid(w) && row_.tag[w] == tag) { hit = int(w); break; }
    if (mst_ != M_IDLE && set_ == m_set_) { abandon(false); return; }   // the miss handler's set: wait for it

    if (h.op == J_DISCARD) {
        if (hit >= 0) {
            const unsigned w = unsigned(hit);
            if (s_valid_ && s_set_ == set_ && s_way_ == w) { abandon(true); return; }   // in flight: after its response
            if (mh_writes_) return;                                  // the tag port is the miss handler's this cycle
            TagRow r = row_;
            if (r.is_dirty(w)) discarded_dirty++;
            r.valid &= uint8_t(~(1u << w)); r.dirty &= uint8_t(~(1u << w));
            tags_.write(t, set_, r);
            discarded_lines++;
        }
        if (++dk_ == lookup::LINES_PER_PAGE) { reply(t, lookup::OK, 0, 0); return; }
        x_ = int64_t(h.root & ~31u) + dk_;
        issue_tag_read(t);
        return;
    }

    if (hit >= 0) {
        const unsigned w = unsigned(hit);
        if (h.missed_line == x_) h.missed_line = -1;                // the line this request's miss fetched
        else hits[cls()]++;
        if (mst_ != M_IDLE) served_under_miss++;
        if (h.op == J_FWRITE && k_ == h.depth) {                     // the fill write's line: written, dirty
            if (s_valid_ && s_set_ == set_ && s_way_ == w) { abandon(true); return; }   // a line in flight is not changed
            if (mh_writes_) return;
            data_.write(t, DataMemory::row(set_, w), h.data);
            TagRow r = row_;
            r.dirty |= uint8_t(1u << w);
            r.touch(w);
            tags_.write(t, set_, r);
            fill_write_response(t, FILL_WRITE_OK);
            return;
        }
        if (mh_data_read_ || mh_writes_) return;
        data_.read(t, DataMemory::row(set_, w));
        TagRow r = row_;
        r.touch(w);
        tags_.write(t, set_, r);
        way_ = w; wdirty_ = row_.is_dirty(w); wfresh_ = true;
        wst_ = W_DATA;
        return;
    }

    // a miss
    if (h.failed_line == x_) {                                       // its fetch failed: the error is the answer
        h.failed_line = -1;
        switch (h.op) {
            case J_LOOK_UP: case J_UPDATE: reply(t, lookup::LPDDR_ERROR, uint8_t(k_), 0); return;
            case J_FREAD: error_ = true; fill_read_response(t, lookup::BEYOND, ZEROS); return;   // CI section 5 (†)
            default: fill_write_response(t, FILL_WRITE_ERROR); return;
        }
    }
    if (mst_ != M_IDLE) { abandon(false); return; }
    // the victim: a free way, else the least recently used that may be evicted (not the update slot's line)
    int v = -1;
    for (unsigned w = 0; w < WAYS && v < 0; w++) if (!row_.is_valid(w)) v = int(w);
    if (v < 0)
        for (unsigned w = 0; w < WAYS; w++) {
            if (s_valid_ && s_set_ == set_ && s_way_ == w) continue;
            if (v < 0 || row_.age[w] > row_.age[v]) v = int(w);
        }
    const unsigned vw = unsigned(v);
    misses[cls()]++;
    m_x_ = x_; m_set_ = set_; m_way_ = vw; m_row_ = row_; m_owner_ = src_;
    m_vvalid_ = row_.is_valid(vw); m_vdirty_ = m_vvalid_ && row_.is_dirty(vw);
    m_vline_ = int64_t(row_.tag[vw]) << 9 | set_;
    m_alloc_ = h.op == J_FWRITE && k_ == h.depth;
    if (m_alloc_) { std::memcpy(m_buf_, h.data, sizeof m_buf_); allocations++; }
    if (m_vvalid_) evictions++;
    if (m_vdirty_) mst_ = M_WB_READ;
    else if (m_alloc_) mst_ = M_INSTALL;
    else { mst_ = M_FETCH; m_ar_ = true; }
    h.wait_mh = true;
    wst_ = W_IDLE;
}

void CacheCtl::step_data(int64_t t) {
    Held& h = held_[src_];
    if (wfresh_) {
        std::memcpy(wline_, data_.q(), sizeof wline_);
        wfresh_ = false;
        if (check_store && !wdirty_ && !(s_valid_ && s_x_ == x_)) check_clean(x_, wline_);
    }
    const unsigned d = h.depth;
    if (k_ == d) { fill_read_response(t, owned_all_ ? lookup::OK : lookup::NOT_OWNED, wline_); return; }   // the data line

    const unsigned i = index_at(k_);
    const uint32_t e = entry_of(wline_, i);
    const bool hole = e == NO_PAGE, owned = (e & OWNED) != 0;
    switch (h.op) {
        case J_LOOK_UP:
            if (hole) { reply(t, lookup::HOLE, uint8_t(k_), e); return; }
            if (!owned) { reply(t, lookup::NOT_OWNED, uint8_t(k_), e); return; }
            if (k_ == d - 1) { reply(t, lookup::OK, uint8_t(k_), e); return; }
            break;
        case J_UPDATE:
            if (k_ < h.level) {
                if (hole) { reply(t, lookup::HOLE, uint8_t(k_), e); return; }
                if (!owned) { reply(t, lookup::NOT_OWNED, uint8_t(k_), e); return; }
                break;
            }
            // the target line: the entry changed in the cache, the line handed to the AXI writer, the update parked
            if (w_busy_ || mh_writes_) return;
            set_entry_of(wline_, i, h.entry);
            data_.write(t, DataMemory::row(set_, way_), wline_, 1u << ((i & 31) >> 2));
            w_busy_ = true; w_aw_ = false; w_beat_ = 0; w_addr_ = uint64_t(x_) << 7;
            std::memcpy(w_buf_, wline_, sizeof w_buf_);
            kinds_[(kinds_head_ + kinds_n_) % 4] = K_UPDATE; kinds_n_++;
            update_writes++;
            s_valid_ = true; s_src_ = src_; s_set_ = set_; s_way_ = way_; s_level_ = uint8_t(k_); s_old_ = e; s_x_ = x_;
            walk_lines[k_ < 4 ? k_ : 4]++;
            h.parked = true;
            wst_ = W_IDLE;
            return;
        case J_FREAD:
            if (hole) { fill_read_response(t, lookup::HOLE, ZEROS); return; }
            if (!owned) owned_all_ = false;
            break;
        case J_FWRITE:
            if (hole || !owned) { fill_write_response(t, FILL_WRITE_ERROR); return; }
            break;
        default:
            FATAL("cache: the walker's data step with op %u", unsigned(h.op));
    }
    // the next level
    cur_ = int64_t(e & lookup::ADDRESS);
    k_++;
    x_ = k_ < d ? cur_ + (index_at(k_) >> 5) : cur_ + int64_t(h.pos & 31);
    issue_tag_read(t);
}

// ================================================================ the miss handler: its state's work in the cycle
void CacheCtl::step_mh(int64_t t, const Inputs&) {
    switch (mst_) {
        case M_WB_READ:
            data_.read(t, DataMemory::row(m_set_, m_way_));
            mh_data_read_ = true;
            m_copied_ = false;
            mst_ = M_WB_HAND;
            break;
        case M_WB_HAND:
            if (!m_copied_) { std::memcpy(m_buf_, data_.q(), sizeof m_buf_); m_copied_ = true; }
            if (!w_busy_) {
                w_busy_ = true; w_aw_ = false; w_beat_ = 0; w_addr_ = uint64_t(m_vline_) << 7;
                std::memcpy(w_buf_, m_buf_, sizeof w_buf_);
                kinds_[(kinds_head_ + kinds_n_) % 4] = K_WRITE_BACK; kinds_n_++;
                write_backs++;
                if (m_alloc_) std::memcpy(m_buf_, held_[m_owner_].data, sizeof m_buf_);   // the line to install
                mst_ = M_WB_WAIT;
            }
            break;
        case M_INSTALL: {
            TagRow r = m_row_;
            const uint8_t bit = uint8_t(1u << m_way_);
            r.tag[m_way_] = uint32_t(m_x_ >> 9);
            r.valid |= bit;
            if (m_alloc_) r.dirty |= bit; else r.dirty &= uint8_t(~bit);
            r.touch(m_way_);
            tags_.write(t, m_set_, r);
            data_.write(t, DataMemory::row(m_set_, m_way_), m_buf_);
            mh_writes_ = true; mh_tag_write_t_ = t; mh_tag_write_set_ = m_set_;
            held_[m_owner_].missed_line = m_x_;
            for (Held& h : held_) h.wait_mh = false;
            prio_ = int(m_owner_);
            mst_ = M_IDLE;
            break;
        }
        case M_FAIL: {
            TagRow r = m_row_;
            const uint8_t bit = uint8_t(1u << m_way_);
            r.valid &= uint8_t(~bit); r.dirty &= uint8_t(~bit);
            tags_.write(t, m_set_, r);
            mh_writes_ = true; mh_tag_write_t_ = t; mh_tag_write_set_ = m_set_;
            held_[m_owner_].failed_line = m_x_;
            fetch_errors++;
            for (Held& h : held_) h.wait_mh = false;
            prio_ = int(m_owner_);
            mst_ = M_IDLE;
            break;
        }
        default: break;                                              // M_IDLE, M_WB_WAIT, M_FETCH, M_FILL: their events
    }
}

// ================================================================ the edge that ends cycle t
void CacheCtl::edge(int64_t t, const Inputs& in) {
    const Outputs& o = out_;                                         // the outputs of t
    next_ = Outputs{};
    bus_taken_ = false; mh_data_read_ = mh_writes_ = false;

    // ---- the miss handler's work for the state it is in
    step_mh(t, in);

    // ---- the AXI port's events in t
    if (o.axi.arvalid && in.axi.arready) { m_ar_ = false; mst_ = M_FILL; m_got_ = 0; m_err_ = false; }
    if (o.axi.awvalid && in.axi.awready) w_aw_ = true;
    if (o.axi.wvalid && in.axi.wready) w_beat_++;
    if (w_busy_ && w_aw_ && w_beat_ == LANES) w_busy_ = false;
    if (in.axi.rvalid) {
        if (mst_ != M_FILL || m_got_ >= LANES) FATAL("cache: an R beat it did not ask for, at %lld", (long long)t);
        m_buf_[2 * m_got_] = in.axi.rdata[0]; m_buf_[2 * m_got_ + 1] = in.axi.rdata[1];
        if (in.axi.rresp != axi::OKAY) m_err_ = true;
        m_got_++;
        if (in.axi.rlast) {
            if (m_got_ != LANES) FATAL("cache: a line of %u beats, at %lld", m_got_, (long long)t);
            mst_ = m_err_ ? M_FAIL : M_INSTALL;
        }
    }
    if (in.axi.bvalid) {
        if (kinds_n_ == 0) FATAL("cache: a B response with no write outstanding, at %lld", (long long)t);
        const Kind k = kinds_[kinds_head_];
        kinds_head_ = (kinds_head_ + 1) % 4; kinds_n_--;
        const bool ok = in.axi.bresp == axi::OKAY;
        if (k == K_UPDATE) {                                         // the slot's reply: the bus is the slot's first
            next_.reply.valid = true; next_.reply.client = uint8_t(s_src_);
            next_.reply.status = ok ? lookup::OK : lookup::LPDDR_ERROR;
            if (!ok && n_diverged_ < 16) diverged_[n_diverged_++] = s_x_;
            next_.reply.level = s_level_; next_.reply.entry = s_old_;
            bus_taken_ = true;
            finish(t, s_src_);
            s_valid_ = false;
            for (Held& h : held_) h.wait_slot = false;
        } else {
            if (mst_ != M_WB_WAIT) FATAL("cache: a write-back's response out of turn, at %lld", (long long)t);
            if (!ok) { error_ = true; write_back_errors++; }        // the dirty line is lost (CI section 5)
            if (m_alloc_) mst_ = M_INSTALL;
            else { mst_ = M_FETCH; m_ar_ = true; }
        }
    }

    // ---- the walker's one step
    switch (wst_) {
        case W_IDLE: {
            if (!cfg.hits_under_miss && mst_ != M_IDLE) break;
            auto eligible = [&](unsigned s) {
                const Held& h = held_[s];
                return h.valid && !h.parked && !h.wait_mh && !h.wait_slot && !(h.op == J_UPDATE && s_valid_)
                       && !h.behind_write;                    // fill's read after a write accepted before it (23.6.4)
            };
            int pick = -1;
            if (prio_ >= 0 && eligible(unsigned(prio_))) pick = prio_;
            for (unsigned k = 1; k <= SOURCES && pick < 0; k++) {
                const unsigned s = (rr_ + k) % SOURCES;
                if (eligible(s)) { pick = int(s); rr_ = s; }
            }
            prio_ = -1;
            if (pick >= 0) start(t, unsigned(pick));
            break;
        }
        case W_CMP: step_cmp(t); break;
        case W_DATA: step_data(t); break;
        case W_REPLY:
            if (!bus_taken_) {
                next_.reply.valid = true; next_.reply.client = uint8_t(src_);
                next_.reply.status = rstatus_; next_.reply.level = rlevel_; next_.reply.entry = rentry_;
                bus_taken_ = true;
                finish(t, src_);
                wst_ = W_IDLE;
            }
            break;
    }

    // ---- the requests taken at the ports in t
    for (unsigned p = 0; p < PORTS; p++) {
        const lookup::Request& r = in.req[p];
        if (!(r.valid && o.ready[p])) continue;
        if (r.op > lookup::DISCARD) FATAL("cache: look-up port %u: op %u is reserved, at %lld", p, unsigned(r.op), (long long)t);
        Held& h = held_[p];
        h = Held{};
        h.valid = true; h.op = r.op; h.root = r.root & lookup::ADDRESS; h.depth = r.depth & 3;
        h.pos = r.page & ((1u << lookup::PAGE_BITS) - 1); h.level = r.level & 3; h.entry = r.entry; h.t_accept = t;
    }
    if (in.fr.valid && o.fr_ready) {
        Held& h = held_[S_FREAD];
        h = Held{};
        h.valid = true; h.op = J_FREAD; h.root = uint32_t(in.fr.root) & lookup::ADDRESS; h.depth = in.fr.depth & 3;
        h.pos = uint64_t(in.fr.line); h.t_accept = t;
        h.behind_write = held_[S_FWRITE].valid;                     // a write taken in an earlier cycle, not yet answered
    }
    if (in.fw.valid && o.fw_ready) {
        Held& h = held_[S_FWRITE];
        h = Held{};
        h.valid = true; h.op = J_FWRITE; h.root = uint32_t(in.fw.root) & lookup::ADDRESS; h.depth = in.fw.depth & 3;
        h.pos = uint64_t(in.fw.line); h.t_accept = t;
        std::memcpy(h.data, in.fw.data, sizeof h.data);
    }

    // ---- the outputs of t + 1, from the registers
    // a port's ready: its register empty, and not in the cycle of its reply (CI section 1: from the cycle after it)
    for (unsigned p = 0; p < PORTS; p++) next_.ready[p] = !held_[p].valid && !(next_.reply.valid && next_.reply.client == p);
    next_.fr_ready = !held_[S_FREAD].valid && !next_.frr_valid;
    next_.fw_ready = !held_[S_FWRITE].valid && !next_.fwr_valid;
    next_.axi.arvalid = m_ar_;
    next_.axi.araddr = uint64_t(m_x_) << 7; next_.axi.arlen = LANES - 1;
    next_.axi.awvalid = w_busy_ && !w_aw_;
    next_.axi.awaddr = w_addr_; next_.axi.awlen = LANES - 1;
    next_.axi.wvalid = w_busy_ && w_beat_ < LANES;
    if (next_.axi.wvalid) {
        next_.axi.wdata[0] = w_buf_[2 * w_beat_]; next_.axi.wdata[1] = w_buf_[2 * w_beat_ + 1];
        next_.axi.wlast = w_beat_ == LANES - 1;
    }
    next_.error = error_;
    if (trace)
        std::fprintf(stderr, "cache %lld: walker %u src %u k %u line 0x%llx set %u | mh %u line 0x%llx | writer %d beat %u | slot %d%s%s\n",
                     (long long)t, unsigned(wst_), src_, k_, (unsigned long long)x_, set_, unsigned(mst_), (unsigned long long)m_x_,
                     int(w_busy_), w_beat_, int(s_valid_), next_.reply.valid ? " | reply" : "", next_.frr_valid ? " | fill read answered" : "");
    out_ = next_;
    tags_.clock(t);
    data_.clock(t);
}

// ================================================================ back doors (tests)
void CacheCtl::write_dirty_to(tracer::FileStore& store) const {
    for (unsigned s = 0; s < SETS; s++) {
        const TagRow& r = tags_.peek(s);
        for (unsigned w = 0; w < WAYS; w++) {
            if (!r.is_valid(w) || !r.is_dirty(w)) continue;
            const int64_t line = int64_t(r.tag[w]) << 9 | s;
            uint64_t pg[tracer::FileStore::PAGE_WORDS];
            const int32_t page = int32_t(line & ~int64_t(31));
            store.read_page(page, pg);
            std::memcpy(pg + 16 * (line & 31), data_.peek(DataMemory::row(s, w)), 16 * sizeof(uint64_t));
            store.write_page(page, pg);
        }
    }
}

bool CacheCtl::holds(int64_t line) const {
    const TagRow& r = tags_.peek(unsigned(line) & (SETS - 1));
    for (unsigned w = 0; w < WAYS; w++) if (r.is_valid(w) && r.tag[w] == uint32_t(line >> 9)) return true;
    return false;
}
bool CacheCtl::holds_dirty(int64_t line) const {
    const TagRow& r = tags_.peek(unsigned(line) & (SETS - 1));
    for (unsigned w = 0; w < WAYS; w++) if (r.is_valid(w) && r.is_dirty(w) && r.tag[w] == uint32_t(line >> 9)) return true;
    return false;
}

}  // namespace cache
}  // namespace units
