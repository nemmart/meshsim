// tracer_block.cpp: the block-level bench (tracer_block.h).
#include "tracer_block.h"

#include "base/alloc_guard.h"
#include "base/fatal.h"
#include "units/row_core.h"

namespace tracer_block {

// One cycle (the RTL bench's Bench::cycle): a WRITE queued before it, an expansion word first; the access; the DMA.
uint32_t Bench::step(const Access& a) {
    const int64_t n = cycle;
    if (guard_alloc) { sim::begin_run(); tr.sync(n); sim::end_run(); }
    if (!expansion_writes_.empty() || !writes_.empty()) {
        std::deque<Write>& q = !expansion_writes_.empty() ? expansion_writes_ : writes_;
        const Write w = q.front();
        q.pop_front();
        tr.accept(n - Tracer::RP_LATENCY, SRC, w.offset, 2, w.lo, w.hi);   // the write port presents it in n
    }
    uint32_t rdata = 0;
    bool dma_start = false;
    if (a.kind == 1) {
        if (a.block == B_CONTROL) {
            switch (a.word) {
                case C_STATUS: rdata = (tr.running(n) ? 1u : 0u) | (dma_busy_ ? 2u : 0u); break;
                case C_ERRORS: rdata = uint32_t(tr.errors(n)); break;
                case C_EVENTS: case C_EVENTS_PEEK:
                    tr.sync(n);
                    rdata = tr.events().value(n) | (dma_done_ > 0 ? 2u : 0u);
                    if (a.word == C_EVENTS) {   // the consumption, at the end of n, before n's sets
                        tr.consume_events(rdata & events_mask_ & ~2u);
                        if (rdata & events_mask_ & 2u) dma_done_--;
                    }
                    break;
                case C_EVENTS_MASK: rdata = events_mask_; break;
                default: FATAL("tracer_block: a load of control word %d", a.word);
            }
        } else if (a.block == B_DMA) {
            rdata = a.word == D_HDR ? dma_hdr_ : 0;
        } else {
            const int w = (a.block == B_TWO ? 32 : 48) + a.word;
            if (!tr.word(n, w, rdata)) FATAL("tracer_block: a load of word %d, undefined", w);
        }
    } else if (a.kind == 2) {
        if (a.block == B_CONTROL) {
            if (a.word == C_EVENTS_MASK) events_mask_ = a.wdata;
            else if (a.word == C_START) { if (a.wdata & 1) tr.run(n); }
            else FATAL("tracer_block: a store to control word %d", a.word);
        } else if (a.block == B_DMA) {
            if (a.word == D_HDR) dma_hdr_ = a.wdata;
            else if (a.word == D_START) dma_start = (a.wdata & 1) != 0;
        } else {
            if (tr.write_word(n, 32 + a.word, a.wdata) != 1) FATAL("tracer_block: a store to word %d", 32 + a.word);
        }
    }
    // the DMA: an entry a cycle while one is to send and the queue holds one; the end, the cycle after the last
    const bool taking = dma_busy_ && dma_pending_ != 0 && tr.out_ready(n, dma_queue_);
    const bool ending = dma_busy_ && dma_pending_ == 0;
    if (taking) {
        uint64_t w[2];
        tr.out_take(n, dma_queue_, w);
        if (received_) { received_[dma_queue_].push_back(w[0]); received_[dma_queue_].push_back(w[1]); }
        dma_pending_--;
        dma_packets++;
    }
    if (ending && !tr.stopped() && dma_done_ < 1) dma_done_++;
    dma_busy_ = dma_start || (dma_busy_ && !ending);
    if (dma_start) {
        dma_queue_ = int((dma_hdr_ >> 27) & 3);
        dma_pending_ = (dma_hdr_ >> 15) & 0xFFF;
        tr.out_start(n, dma_queue_, dma_pending_);
    }
    if (skip_idle_steps && !tr.must_step()) {
        steps_skipped++;
    } else {
        if (guard_alloc) sim::begin_run();
        tr.step(n);
        if (guard_alloc) sim::end_run();
    }
    if (on_cycle) on_cycle(on_cycle_ctx, n);
    cycle++;
    return rdata;
}

uint32_t Bench::load(int block, int word) { return step(Access{1, block, word, 0}); }
void Bench::store(int block, int word, uint32_t wdata) { step(Access{2, block, word, wdata}); }
void Bench::idle_cycle() { step(Access{}); }
void Bench::flush() {
    while (!writes_.empty() || !expansion_writes_.empty()) idle_cycle();
    idle_cycle();
    idle_cycle();
}

void Bench::reset() {
    tr.reset(cycle);
    writes_.clear(); expansion_writes_.clear(); advice_.clear(); expansion_.clear();
    events_mask_ = 0; dma_done_ = 0;
    dma_hdr_ = 0; dma_busy_ = false; dma_queue_ = 0; dma_pending_ = 0;
    transfer_ = false; may_start_at_ = 0;
    cycle++;
}

void Bench::setup(uint32_t hash_mul, uint32_t hash_step, uint32_t hash_lines) {
    store(B_CONTROL, C_EVENTS_MASK, EVENTS_COUNTED);
    for (int r = 0; r < 256; r++) {
        uint64_t lo = 0, hi = 0;
        for (int k = 0; k < 4; k++) {
            lo |= uint64_t(tb.table[8 * r + k]) << (16 * k);
            hi |= uint64_t(tb.table[8 * r + 4 + k]) << (16 * k);
        }
        write(0x1000 + 16 * uint32_t(r), lo, hi);
    }
    for (int j = 0; j < 512; j++) write(0x2000 + 16 * uint32_t(j), tb.rom_lo[j], tb.rom_hi[j]);
    flush();
    store(B_TWO, Tracer::W_HASH_MUL - 32, hash_mul);
    store(B_TWO, Tracer::W_HASH_STEP - 32, hash_step);
    store(B_TWO, Tracer::W_HASH_LINES - 32, hash_lines);
}

void Bench::release_advice() {
    for (int k = 0; k < Tracer::QHALF && !advice_.empty(); k++) {
        const uint64_t lo = advice_.front();
        advice_.pop_front();
        uint64_t hi = 0;
        if (!advice_.empty()) { hi = advice_.front(); advice_.pop_front(); }
        writes_.push_back({0x200000, lo, hi});
    }
}
void Bench::release_expansion() {
    for (int k = 0; k < Tracer::QHALF && !expansion_.empty(); k++) {
        expansion_writes_.push_back(expansion_.front());
        expansion_.pop_front();
    }
}

void Bench::segment(const uint64_t* record, const std::vector<uint64_t>& advice, uint32_t run_config) {
    advice_.assign(advice.begin(), advice.end());
    expansion_.clear();
    for (uint32_t w = 0; w < Tracer::REC_WORDS; w++) write(16 * w, record[2 * w], record[2 * w + 1]);
    flush();
    store(B_TWO, Tracer::W_RUN_CONFIG - 32, run_config);
    while (load(B_CONTROL, C_EVENTS) & (1u << Tracer::EV_ADVICE)) release_advice();
    flush();
}

void Bench::start_transfer(int queue, uint32_t count) {
    if (transfer_) FATAL("tracer_block: a transfer started in another");
    store(B_DMA, D_HDR, uint32_t(1 + queue) << 2 | 5u << 8 | count << 15 | uint32_t(queue) << 27);
    store(B_DMA, D_DEST_ADDR, 0x04000000u | uint32_t(queue) << 20);
    transfer_ = true;
    store(B_DMA, D_START, 1);
}

namespace {
struct RowTimes {
    RunOut* out;
    void (*next)(void*, int64_t, bool);   // the harness's own hook, called after
    void* next_ctx;
};
void on_row(void* ctx, int64_t t, bool start) {
    const RowTimes& r = *static_cast<RowTimes*>(ctx);
    if (r.next) r.next(r.next_ctx, t, start);
    RunOut& o = *r.out;
    if (start) { if (o.first_row_start < 0) o.first_row_start = t; }
    else o.last_row_done = t;
}
}  // namespace

void Bench::run(RunOut& out) {
    out = RunOut();
    received_ = out.queue;
    void (*const saved)(void*, int64_t, bool) = tr.on_row;
    void* const saved_ctx = tr.on_row_ctx;
    RowTimes rt{&out, saved, saved_ctx};
    tr.on_row = on_row; tr.on_row_ctx = &rt;
    out.start_store = cycle;
    store(B_CONTROL, C_START, 1);
    const int64_t limit = cycle + (limit_per_run > 0 ? limit_per_run : 40 * 65536 + 2000000 + 4 * 65536 * int64_t(tr.fill_latency[0] > tr.fill_latency[1] ? tr.fill_latency[0] : tr.fill_latency[1]) + 400 * 65536);
    static const uint32_t kPieces[4] = {1, 1, 65, 1};
    int pieces = 0, turn = 0;
    uint32_t sent[3] = {0, 0, 0};
    const uint32_t ST = 1u << Tracer::EV_SVC_STATE;
    for (;;) {
        if (cycle >= limit || tr.errors(cycle) != 0) break;
        const uint32_t events = load(B_CONTROL, C_EVENTS);
        if (events & (1u << Tracer::EV_ADVICE)) release_advice();
        if (events & (1u << Tracer::EV_NEWEXP)) {
            const uint32_t w = load(B_TWO, Tracer::W_EXP_OPCODE - 32);
            const uint32_t offset = load(B_TWO, Tracer::W_EXP_OFFSET - 32);
            out.post_opcode.push_back(w); out.post_offset.push_back(offset);
            const int number = tb.lookup(w);
            expansion_.clear();
            if (number < 0) {   // nothing to push: the Tracer waits
                if (!allow_no_template) FATAL("tracer_block: no core-fed template for 0x%08x", w);
                no_template++; no_template_w = w;
            } else {
                if ((tb.table[units::rowcore::decode_index(w)] & 3) == 1)
                    store(B_TWO, Tracer::W_EXP_TEMPLATE - 32, 1u << 31 | uint32_t(number));
                // the words the core pushes from the offset (17.8.13): rd0 in the first; the shared NOP row alone under
                // a NOP policy with rd = x0
                const uint64_t rd0 = uint64_t(tb.rd0[size_t(number)]) << 46, keep = ~(uint64_t(3) << 46);
                if (((w >> 7) & 31) == 0 && (tb.rd0[size_t(number)] == 1 || tb.rd0[size_t(number)] == 3)) {
                    expansion_.push_back({0x400000, (tb.rom_lo[1] & keep) | rd0, tb.rom_hi[1]});
                } else {
                    const std::vector<uint64_t>& words = tb.words[size_t(number)];
                    if (2 * size_t(offset) >= words.size()) FATAL("tracer_block: a post at row offset %u, past the template of 0x%08x", offset, w);
                    for (size_t k = offset; 2 * k < words.size(); k++)
                        expansion_.push_back({0x400000, k == offset ? (words[2 * k] & keep) | rd0 : words[2 * k], words[2 * k + 1]});
                }
            }
        }
        if (events & (1u << Tracer::EV_EXP)) release_expansion();
        if (events & (1u << Tracer::EV_DMA_DONE)) { transfer_ = false; may_start_at_ = cycle + hold; }
        if (events & (1u << Tracer::EV_RUN_DONE)) { out.done = true; out.done_load = cycle - 1; break; }
        if ((events & ST) && out.state_ready_load < 0) {
            out.state_ready_load = cycle - 1;
            may_start_at_ = cycle + hold;
            load(B_TWO, Tracer::W_STATE_LENGTH - 32);
            for (int s = 0; s < 3; s++) out.lengths[s] = load(B_TWO, Tracer::W_CODE_LENGTH - 32 + s);
        }
        if (!transfer_ && out.state_ready_load >= 0 && cycle >= may_start_at_) {
            // output queue 0's pieces first (the RTL bench); with state_last, only when no stream holds a credit
            const bool streams_ready = (events & (7 * (ST << 1))) != 0;
            if (pieces < 4 && !(state_last && streams_ready)) {
                if (events & ST) {
                    if (q0_one) { start_transfer(0, uint32_t(Tracer::REC_WORDS)); pieces = 4; }
                    else start_transfer(0, kPieces[pieces++]);
                }
            } else {
                for (int k = 0; k < 3; k++) {
                    const int s = has_order ? order[k] - 1 : (turn + k) % 3;
                    const uint32_t left = out.lengths[s] - sent[s];
                    if (left != 0 && (events & (ST << (s + 1)))) {
                        const uint32_t count = left < uint32_t(Tracer::QHALF) ? left : uint32_t(Tracer::QHALF);
                        start_transfer(s + 1, count);
                        sent[s] += count;
                        turn = s + 1;
                        break;
                    }
                }
            }
        }
    }
    if (out.state_ready_load >= 0) out.rows_cycles = out.state_ready_load - out.start_store - 1;
    if (out.done) out.drain_cycles = out.done_load - out.state_ready_load + 1;
    out.status = load(B_CONTROL, C_STATUS);
    out.errors = load(B_CONTROL, C_ERRORS);
    out.events_peek = load(B_CONTROL, C_EVENTS_PEEK);
    out.rows_run = load(B_TWO, Tracer::W_ROWS_RUN - 32);
    for (int k = 0; k < 4; k++) out.stat[k] = load(B_TWO, 12 + k);
    for (int k = 0; k < 4; k++) out.stat[4 + k] = load(B_THREE, k);
    tr.on_row = saved; tr.on_row_ctx = saved_ctx;
    received_ = nullptr;
    transfer_ = false;
}

}  // namespace tracer_block
