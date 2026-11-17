#include "standin.h"

namespace wire {

namespace {

constexpr uint32_t kQueue = 1024;  // words waiting to be forwarded; beyond this they are dropped (and counted)

inline uint64_t digest(uint64_t d, uint64_t t, uint64_t w, uint64_t tag) {
    return mix64(d + w + 0x9e3779b97f4a7c15ull * (t + 1) + (tag << 60));
}

}  // namespace

uint64_t standin_finish(const StandInCfg& c) {
    if (!c.stagger) return c.cycles;
    if (c.wide) return 1 + mix64(c.seed * 0x100000001b3ull + uint64_t(c.process) + 11) % (c.cycles ? c.cycles : 1);
    // Up to a quarter early, different for each process.
    return c.cycles - mix64(c.seed * 0x100000001b3ull + uint64_t(c.process) + 7) % (c.cycles / 4 + 1);
}

StandIn::StandIn(const StandInCfg& c) : c_(c) {
    mask_ = c.lane_bytes >= 8 ? ~0ull : (1ull << (8 * c.lane_bytes)) - 1;
    finish_at_ = standin_finish(c);
    rng_state_ = mix64(c.seed ^ (uint64_t(c.process) << 40) ^ 0x5157ull);
    h_ = mix64(rng_state_ + 1);
    out_[0].present = c.process > 0;
    out_[1].present = c.process < c.clients;
    for (int o = 0; o < 2; ++o) {
        Out& e = out_[o];
        e.len = 1 + uint32_t(rng() % 300);  // forwarded after 1 to 300 cycles
        e.line.assign(e.len, 0);
        e.q.assign(kQueue, 0);
        e.period = 1000 + rng() % 10000;
        e.key = rng() | 1;
    }
}

uint64_t StandIn::rng() {
    rng_state_ += 0x9e3779b97f4a7c15ull;
    return mix64(rng_state_);
}

uint64_t StandIn::pack(const uint8_t* b) const {
    uint64_t w = 0;
    for (uint32_t i = 0; i < c_.lane_bytes; ++i) w |= uint64_t(b[i]) << (8 * i);
    return w;
}

void StandIn::unpack(uint64_t w, uint8_t* b) const {
    for (uint32_t i = 0; i < c_.lane_bytes; ++i) b[i] = uint8_t(w >> (8 * i));
}

void StandIn::outputs(uint64_t t, uint8_t* out_up, uint8_t* out_dn) {
    uint8_t* dst[2] = {out_up, out_dn};
    for (int o = 0; o < 2; ++o) {
        Out& e = out_[o];
        uint64_t w = 0;
        if (e.present) {
            if (e.phase_left == 0) {
                // Active stretches of 2,000 to 100,000 cycles; silences up to three million.
                e.active = rng() % 4 != 0;
                e.phase_left = e.active ? 2000 + rng() % 100000 : 10000 + rng() % 3000000;
            }
            --e.phase_left;
            if (e.burst_left == 0 && e.active && (rng() ^ h_) % e.period == 0) e.burst_left = 1 + (h_ ^ rng()) % 400;
            if (e.burst_left > 0) {
                --e.burst_left;
                w = (rng() ^ h_) & mask_;
            } else if (e.qcount > 0) {
                w = e.q[e.qhead];
                e.qhead = (e.qhead + 1) % kQueue;
                --e.qcount;
            }
        }
        last_out_[o] = w;
        if (e.present) dout_[o] = digest(dout_[o], t, w, uint64_t(o));
        unpack(w, dst[o]);
    }
}

void StandIn::inputs(uint64_t t, const uint8_t* in_up, const uint8_t* in_dn) {
    const uint64_t in[2] = {out_[0].present ? pack(in_up) : 0, out_[1].present ? pack(in_dn) : 0};
    for (int o = 0; o < 2; ++o) {
        if (!out_[o].present) continue;
        din_[o] = digest(din_[o], t, in[o], uint64_t(o) + 2);
        if (in[o]) h_ = mix64(h_ ^ in[o] ^ (t << 8) ^ uint64_t(o));
    }
    for (int o = 0; o < 2; ++o) {
        Out& e = out_[o];
        if (!e.present) continue;
        // What it forwards on o comes from the other side, or, at an end of the chain, from o's own input.
        const int src = out_[1 - o].present ? 1 - o : o;
        const uint64_t old = e.line[e.pos];
        e.line[e.pos] = in[src];
        e.pos = e.pos + 1 == e.len ? 0 : e.pos + 1;
        if (old == 0) continue;
        // One word in four is not passed on, so that echoes die away and silences happen.
        if ((mix64(old ^ e.key) & 3) == 0) continue;
        if (e.qcount == kQueue) {
            ++e.drops;
            continue;
        }
        e.q[(e.qhead + e.qcount) % kQueue] = (old ^ e.key) & mask_;
        ++e.qcount;
    }
}

void StandIn::checkpoint(uint64_t t, FILE* f) const {
    if ((t + 1) % kCheckpoint != 0) return;
    const uint64_t d = mix64(din_[0] ^ mix64(din_[1] ^ mix64(dout_[0] ^ mix64(dout_[1]))));
    fprintf(f, "p %d ck %llu %016llx\n", c_.process, (unsigned long long)(t + 1), (unsigned long long)d);
}

void StandIn::report(FILE* f) const {
    fprintf(f, "p %d end %llu in %016llx %016llx out %016llx %016llx drops %llu %llu\n", c_.process,
            (unsigned long long)finish_at_, (unsigned long long)din_[0], (unsigned long long)din_[1],
            (unsigned long long)dout_[0], (unsigned long long)dout_[1], (unsigned long long)out_[0].drops,
            (unsigned long long)out_[1].drops);
}

}  // namespace wire
