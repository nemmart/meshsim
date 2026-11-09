// periph_slot.cpp: the peripheral slot (WORKER15): SlotCore is sim/ref/meshsim/src/PeripheralSlot.java method for method;
// PeriphSlot puts it behind AUDIT-M3 3.1's ports.
#include "units/periph_slot.h"

#include <type_traits>

#include "units/cnc1_periph.h"
#include "units/linkword.h"
#include "units/replayer.h"
#include "units/test_peripheral.h"
#include "units/tracer.h"

namespace units {

// ================================================================ the dispatch: one case per peripheral type
template <class F>
decltype(auto) SlotCore::visit(unsigned n, F&& f) {
    switch (per[n].type) {
        case PT_REPLAYER: return f(*static_cast<Replayer*>(per[n].p));
        case PT_TEST: return f(*static_cast<TestPeripheral*>(per[n].p));
        case PT_TRACER: return f(*static_cast<tracer::Tracer*>(per[n].p));   // W14
        case PT_CNC1: return f(*static_cast<Cnc1Peripheral*>(per[n].p));     // W16
        case PT_CC_STUB: return f(*static_cast<CcStub*>(per[n].p));          // W16
        default: FATAL("slot %u: peripheral %u of type %u has no dispatch case", tile, n, unsigned(per[n].type));
    }
}

SlotCore::Ref::~Ref() {
    if (!p) return;
    switch (type) {
        case PT_REPLAYER: {
            auto* r = static_cast<Replayer*>(p);
            delete[] r->row_buffer();
            delete r;
            break;
        }
        case PT_TEST: delete static_cast<TestPeripheral*>(p); break;
        case PT_TRACER: delete static_cast<tracer::Tracer*>(p); break;   // W14: its memory with it
        case PT_CNC1: delete static_cast<Cnc1Peripheral*>(p); break;         // W16
        case PT_CC_STUB: delete static_cast<CcStub*>(p); break;              // W16
        default: break;
    }
}
SlotCore::Ref& SlotCore::Ref::operator=(Ref&& o) noexcept {
    if (this != &o) { this->~Ref(); type = o.type; p = o.p; o.p = nullptr; o.type = PT_ABSENT; }
    return *this;
}

uint64_t SlotCore::digest() {
    uint64_t h = 0x9e3779b97f4a7c15ull;
    auto mixin = [&](uint64_t x) { h = (h ^ x) * 0x100000001b3ull; h ^= h >> 29; };
    for (unsigned n = 1; n <= 3; n++) {
        mixin(mask[n]); mixin(dma[n].hdr); mixin(dma[n].addr); mixin(dma[n].done); mixin(dma[n].busy); mixin(uint64_t(dma[n].left));
        if (per[n].type == PT_REPLAYER) {
            Replayer& r = replayer(n);
            mixin(r.pc()); mixin(r.instr_idx()); mixin(r.rows_done()); mixin(r.is_running()); mixin(r.row_hash()); mixin(r.rows_sunk());
            mixin(r.code.occ); mixin(r.rlog.occ); mixin(r.wlog.occ); mixin(r.exp.occ);
        } else if (per[n].type == PT_TRACER) {   // W14: the Tracer's progress
            tracer::Tracer& x = tracer(n);
            mixin(x.rec[0]); mixin(x.rec[2]); mixin(uint64_t(x.rows)); mixin(uint64_t(x.code_n)); mixin(uint64_t(x.log_n));
            mixin(uint64_t(x.stat[0])); mixin(uint64_t(x.n_violations)); mixin(uint64_t(x.n_errors_logged));
        } else if (per[n].type == PT_TEST) {
            TestPeripheral& x = test_peripheral(n);
            mixin(uint64_t(x.runs)); mixin(x.scratch); mixin(x.n_accepts); mixin(x.taken[0]); mixin(x.taken[1]);
        }
    }
    mixin(accepted); mixin(reserved_accesses); mixin(violations);
    return h;
}

void SlotCore::install(unsigned n, PeriphType type, const PeriphSlotConfig& c) {
    if (n < 1 || n > 3 || per[n].p) FATAL("slot %u: peripheral %u installed twice or out of range", tile, n);
    per[n].type = type;
    switch (type) {
        case PT_ABSENT: return;
        case PT_REPLAYER: {
            auto* r = new Replayer(tile);
            if (c.replayer_row_buffer) r->set_row_buffer(new uint64_t[c.replayer_row_buffer * Replayer::REC_LONGS], c.replayer_row_buffer);
            per[n].p = r;
            break;
        }
        case PT_TEST: per[n].p = new TestPeripheral(); break;
        // W14: the Tracer, bare (ref's Config.cc3Tracer: new Tracer(me)); its tables come by region packets (tracer_ctl),
        // its memory from the harness after the build (tracer(n).own_memory(store)): a run without one is FATAL
        case PT_TRACER: per[n].p = new tracer::Tracer(int(tile)); break;
        // W16: C&C 1's Ingress/Egress and UART (its own ports: READY, the error lines, GO; ref_cycle set by the chip
        // through cnc1(n)), and the stub engine of C&C 2 (and of C&C 3 without the Tracer: CcCores.Stub(2 + c - 1))
        case PT_CNC1: per[n].p = new Cnc1Peripheral(); break;
        case PT_CC_STUB: per[n].p = new CcStub(tile == lw::CC3 ? 3 : 2); break;
        default: FATAL("slot %u: peripheral type %u can't be created here yet", tile, unsigned(type));
    }
}

Replayer& SlotCore::replayer(unsigned n) {
    if (per[n].type != PT_REPLAYER) FATAL("slot %u: peripheral %u is not the replayer", tile, n);
    return *static_cast<Replayer*>(per[n].p);
}
tracer::Tracer& SlotCore::tracer(unsigned n) {
    if (per[n].type != PT_TRACER) FATAL("slot %u: peripheral %u is not the Tracer", tile, n);
    return *static_cast<tracer::Tracer*>(per[n].p);
}
TestPeripheral& SlotCore::test_peripheral(unsigned n) {
    if (per[n].type != PT_TEST) FATAL("slot %u: peripheral %u is not the test peripheral", tile, n);
    return *static_cast<TestPeripheral*>(per[n].p);
}
// (an incomplete class is enough for a static_cast from void*: these need no include)
Cnc1Peripheral& SlotCore::cnc1(unsigned n) {
    if (per[n].type != PT_CNC1) FATAL("slot %u: peripheral %u is not C&C 1's peripheral", tile, n);
    return *static_cast<Cnc1Peripheral*>(per[n].p);
}
CcStub& SlotCore::cc_stub(unsigned n) {
    if (per[n].type != PT_CC_STUB) FATAL("slot %u: peripheral %u is not the stub", tile, n);
    return *static_cast<CcStub*>(per[n].p);
}

void SlotCore::violate(int64_t t, uint16_t code, uint32_t a, uint32_t b) {
    violations++;
    facts[n_facts % FACTS] = Fact{t, code, a, b};
    n_facts++;
}

uint32_t SlotCore::present() const {
    uint32_t v = 0;
    for (unsigned n = 1; n <= 3; n++) if (per[n].type != PT_ABSENT) v |= 1u << (n - 1);
    return v;
}

// ---------------------------------------------------------------- the region (4.17.5)
void SlotCore::accept(int64_t t, unsigned n, uint64_t lo, uint64_t h, uint64_t hi) {
    accepted++;
    if (per[n].type == PT_ABSENT) return;
    visit(n, [&](auto& p) { p.accept(t, int(lw::src(h)), lw::offset(lw::addr(h)), int(lw::size(h)), lo, hi); });
}

// ---------------------------------------------------------------- the window (4.17.4)
uint32_t SlotCore::reserved(int64_t t, unsigned n, unsigned w, bool store) {
    // W4-06 (D48): a peripheral without a DMA reads 0 at words 8 and 9, unflagged; only a store is flagged
    if (!store && (w == 8 || w == 9)) return 0;
    reserved_accesses++;
    violate(t, F_RESERVED, n << 8 | w, store);
    return 0;
}

bool SlotCore::read(int64_t t, unsigned n, unsigned w, uint32_t& v) {
    if (per[n].type == PT_ABSENT) return false;
    if (w == 5) {
        visit(n, [&](auto& p) { p.sync(t); v = p.events().value(t) | dma[n].done_bit(t); });
        uint32_t c = v & mask[n];
        if (c != 0) { consume_t[n] = t; consume_bits[n] = c; }
        return true;
    }
    if (peek(t, n, w, v)) return true;
    v = reserved(t, n, w, false);
    return true;
}

// The value a load reads, with no effect (no consume, no flag); false: a reserved or undefined word (it reads 0).
bool SlotCore::peek(int64_t t, unsigned n, unsigned w, uint32_t& v) {
    return visit(n, [&](auto& p) -> bool {
        const bool has_dma = p.out_queues() > 0;
        switch (w) {
            case 0: v = uint32_t(p.params()); return true;
            case 1: v = (p.running(t) ? 1u : 0u) | (dma[n].busy_at(t) ? 2u : 0u) | (p.status_bits(t) & ~3u); return true;
            case 2: v = uint32_t(p.errors(t)); return true;
            case 3: case 4: v = 0; return true;
            case 5: case 6: p.sync(t); v = p.events().value(t) | dma[n].done_bit(t); return true;
            case 7: v = mask[n]; return true;
            case 8: v = has_dma ? dma[n].hdr : 0; return has_dma;
            case 9: v = has_dma ? dma[n].addr : 0; return has_dma;
            default:
                if (w < Win::OWN0) { v = 0; return false; }
                v = 0;
                if (p.word(t, int(w), v)) return true;
                v = 0;
                return false;
        }
    });
}

bool SlotCore::defined(unsigned n, unsigned w) {
    if (w < Win::OWN0) return w < Win::STD;
    uint32_t v;
    return visit(n, [&](auto& p) { return p.word(0, int(w), v); });
}

bool SlotCore::store_stalls(int64_t t, unsigned n, unsigned w) {
    return n >= 1 && n <= 3 && per[n].type != PT_ABSENT && w >= Win::OWN0 && visit(n, [&](auto& p) { return p.store_stalls(t, int(w)); });
}

bool SlotCore::write(int64_t t, unsigned n, unsigned w, uint32_t v) {
    if (per[n].type == PT_ABSENT) return false;
    return visit(n, [&](auto& p) -> bool {
        const bool has_dma = p.out_queues() > 0;
        switch (w) {
            case 0: case 1: case 2: case 5: case 6: return false;
            case 3: if (v & 1) p.run(t); return true;
            case 4: if (!has_dma) { reserved(t, n, w, true); return true; } if (v & 1) dma_start(t, n); return true;
            case 7: pend_mask[n] = v; pend_mask_t[n] = t; return true;
            case 8:
                if (!has_dma) { reserved(t, n, w, true); return true; }
                if (dma[n].busy) violate(t, F_DMA_HDR_BUSY, n);
                dma[n].hdr = v;
                return true;
            case 9:
                if (!has_dma) { reserved(t, n, w, true); return true; }
                if (dma[n].busy) violate(t, F_DMA_ADDR_BUSY, n);
                dma[n].addr = v;
                return true;
            default: {
                if (w < Win::OWN0) { reserved(t, n, w, true); return true; }
                int r = p.write_word(t, int(w), v);
                if (r < 0) { reserved(t, n, w, true); return true; }
                return r > 0;
            }
        }
    });
}

uint32_t SlotCore::live_events(int64_t t) {
    uint32_t v = 0;
    for (unsigned n = 1; n <= 3; n++) {
        if (per[n].type == PT_ABSENT || mask[n] == 0) continue;
        uint32_t e = visit(n, [&](auto& p) { p.sync(t); return p.events().value(t); }) | dma[n].done_bit(t);
        if (e & mask[n]) v |= 1u << (n - 1);
    }
    return v;
}
uint32_t SlotCore::live_errors(int64_t t) {
    uint32_t v = 0;
    for (unsigned n = 1; n <= 3; n++)
        if (per[n].type != PT_ABSENT && visit(n, [&](auto& p) { return p.errors(t); }) != 0) v |= 1u << (n - 1);
    return v;
}

// ---------------------------------------------------------------- the cycle
void SlotCore::run_cycle(int64_t t, unsigned dma_room) {
    n_out = 0;
    for (unsigned n = 1; n <= 3; n++) {
        if (per[n].type == PT_ABSENT) continue;
        visit(n, [&](auto& p) { p.step(t); });
        dma_run(t, n, dma_room);
        visit(n, [&](auto& p) {
            const EventCounts& e = p.events();
            if (e.full_posts != full_seen[n]) { full_seen[n] = e.full_posts; violate(t, F_FULL_POST, n, e.last_full_bit); }
        });
    }
}

void SlotCore::end_cycle(int64_t t) {
    for (unsigned n = 1; n <= 3; n++) {
        if (per[n].type == PT_ABSENT) continue;
        if (consume_t[n] == t) {
            visit(n, [&](auto& p) { p.consume_events(consume_bits[n] & ~2u); });
            if (consume_bits[n] & 2) dma[n].done = 0;
        }
        if (pend_mask_t[n] == t) mask[n] = pend_mask[n];
    }
}

void SlotCore::reset(int64_t t) {
    for (unsigned n = 1; n <= 3; n++) {
        if (per[n].type == PT_ABSENT) continue;
        visit(n, [&](auto& p) { p.reset(t); });
        mask[n] = 0; dma[n] = Dma(); consume_t[n] = -1; pend_mask_t[n] = -1;
    }
}

uint64_t SlotCore::violation_total() {
    uint64_t v = violations;
    for (unsigned n = 1; n <= 3; n++) if (per[n].type != PT_ABSENT) v += visit(n, [&](auto& p) { return p.violation_count(); });
    return v;
}

bool SlotCore::idle(int64_t t) {
    for (unsigned n = 1; n <= 3; n++)
        if (per[n].type != PT_ABSENT && (dma[n].busy || !visit(n, [&](auto& p) { return p.idle(t); }))) return false;
    return true;
}
int64_t SlotCore::next_wake_at(int64_t t) {
    int64_t w = INT64_MAX;
    for (unsigned n = 1; n <= 3; n++)
        if (per[n].type != PT_ABSENT) { int64_t x = visit(n, [&](auto& p) { return p.next_wake_at(t); }); if (x < w) w = x; }
    return w;
}
void SlotCore::skip_cycles(int64_t k) {
    for (unsigned n = 1; n <= 3; n++) if (per[n].type != PT_ABSENT) visit(n, [&](auto& p) { p.skip_cycles(k); });
}
unsigned SlotCore::digest_words(int64_t* out, unsigned cap) {
    unsigned m = 0;
    for (unsigned n = 1; n <= 3; n++) {
        if (per[n].type == PT_ABSENT) continue;
        visit(n, [&](auto& p) {
            using P = std::remove_reference_t<decltype(p)>;
            if (m + unsigned(P::DIGEST_N) > cap) FATAL("slot %u: digest words past %u", tile, cap);
            p.digest_words(out + m);
            m += unsigned(P::DIGEST_N);
        });
    }
    return m;
}

// ---------------------------------------------------------------- the DMA (4.17.9)
void SlotCore::dma_start(int64_t t, unsigned n) {
    Dma& d = dma[n];
    const int qq = int((d.hdr >> 12) & 3);   // DMA_HDR.QUEUE [13:12]
    if (d.busy) FATAL("slot %u: undefined behavior at %lld: peripheral %u's DMA: PULSE_DMA while a transfer is in progress (4.17.9)", tile, (long long)t, n);
    visit(n, [&](auto& p) {
        if (qq >= p.out_queues())
            FATAL("slot %u: undefined behavior at %lld: peripheral %u's DMA: a transfer naming output queue %d, which the peripheral doesn't have (4.17.9)", tile, (long long)t, n, qq);
        d.q = qq; d.left = d.hdr & 0xFFF; d.busy = true; d.started_at = t; d.transfers++;
        if (d.left > 0) p.out_start(t, d.q, d.left);
    });
}

void SlotCore::dma_run(int64_t t, unsigned n, unsigned& room) {
    Dma& d = dma[n];
    if (!d.busy || t <= d.started_at) return;
    auto finish = [&] { d.busy = false; if (d.done < 1) d.done = 1; d.done_at = t; };
    if (d.left == 0) { finish(); return; }
    // v1.3 (4.17.9): the count in 128-bit words, every packet one queue entry of 128 bits, DMA_ADDR advancing by 16
    bool ready = visit(n, [&](auto& p) { return p.out_ready(t, d.q); });
    if (!ready) return;
    if (dma_port && room == 0) return;
    uint64_t w[2];
    visit(n, [&](auto& p) { p.out_take(t, d.q, w); });
    const unsigned dst = (d.hdr >> 25) & 0x7F, mode = (d.hdr >> 22) & 7, ctr = (d.hdr >> 19) & 7;
    const unsigned src = lw::controller(tile) ? tile + 8 : 0;   // item 118: the controller's id + 8 (the Tracer's 74)
    if (n_out == 3) FATAL("slot %u: more than three DMA packets in a cycle", tile);
    out[n_out++] = DmaPacket{t, w[0], lw::hdr(src, dst, mode, ctr, d.addr, lw::SIZE_128), w[1]};
    if (room) room--;
    d.packets++;
    d.addr += 16; d.left--;
    if (d.left == 0) finish();
}

// ================================================================ the unit
PeriphSlot::PeriphSlot(sim::Ports& p, const PeriphSlotConfig& c) : cfg(c) {
    s.tile = c.tile;
    s.dma_port = c.controller;
    for (unsigned n = 1; n <= 3; n++) if (c.type[n] != PT_ABSENT) s.install(n, c.type[n], c);
    p.in(region, "region", 3);
    p.in(core, "core", 1);
    p.out(window, "window", Win::REGS);
    if (c.controller) p.out(dma, "dma", 3);
    for (unsigned n = 1; n <= 3; n++) if (s.per[n].type != PT_ABSENT) s.visit(n, [&](auto& q) { q.ports(p); });
}

void PeriphSlot::publish(sim::Ctx& c, int64_t t) {
    auto put = [&](unsigned i, uint32_t v) {
        if (first || published[i] != v) { c.write(window, i, v); published[i] = v; }
    };
    for (unsigned n = 1; n <= 3; n++) {
        if (s.per[n].type == PT_ABSENT) continue;
        uint32_t v;
        for (unsigned w = 0; w < Win::STD; w++) { s.peek(t, n, w, v); put(Win::reg(n, Win::R_STD + w), v); }
        uint32_t def[3] = {}, stall[3] = {};
        for (unsigned w = Win::OWN0; w < Win::WORDS; w++) {
            const unsigned k = w - Win::OWN0;
            if (s.peek(t, n, w, v)) def[k / 32] |= 1u << (k % 32);
            else v = 0;
            put(Win::reg(n, Win::R_OWN + k), v);
            if (s.store_stalls(t, n, w)) stall[k / 32] |= 1u << (k % 32);
        }
        for (unsigned k = 0; k < 3; k++) { put(Win::reg(n, Win::R_DEFINED + k), def[k]); put(Win::reg(n, Win::R_STALL + k), stall[k]); }
    }
    put(Win::R_LIVE, s.live_events(t) | s.live_errors(t) << 3);
    first = false;
}

sim::Wake PeriphSlot::step(sim::Ctx& c) {
    const int64_t t = int64_t(c.now());
    bool active = first;
    // ---- level 0: (1) the peripherals' own inputs
    for (unsigned n = 1; n <= 3; n++) if (s.per[n].type != PT_ABSENT) s.visit(n, [&](auto& q) { q.begin(c, t); });
    // (2) the core's access of t - 1: a consume or the EVENTS_MASK store first (ref's endCycle(t - 1)) ...
    uint64_t op = 0;
    bool have_op = false;
    if (c.ready(core)) { op = c.peek(core)[0]; c.take(core); have_op = true; active = true; }
    if (have_op) {
        const unsigned n = CoreOp::n(op), w = CoreOp::word(op);
        if (CoreOp::kind(op) == CoreOp::CONSUME) {
            const uint32_t bits = CoreOp::value(op);
            s.visit(n, [&](auto& q) { q.consume_events(bits & ~2u); });
            if (bits & 2) s.dma[n].done = 0;
            have_op = false;
        } else if (w == 7) {
            s.mask[n] = CoreOp::value(op);
            have_op = false;
        }
    }
    // (3) the landings due: a packet the director handed over at t - 3 lands at the end of t - 1
    while (c.ready(region)) {
        const sim::Word* x = c.peek(region);
        const uint64_t lo = x[0], h = x[1], hi = x[2];
        c.take(region);
        const unsigned n = lw::region(lw::addr(h)) - 3;
        s.accept(t - 3, n, lo, h, hi);
        active = true;
    }
    // (4) the core's other store of t - 1 (its effect at the end of t - 1)
    if (have_op && !s.write(t - 1, CoreOp::n(op), CoreOp::word(op), CoreOp::value(op)))
        FATAL("slot %u: a store at %lld to peripheral %u's window word %u is a bus error: the core judges those", cfg.tile, (long long)(t - 1),
              CoreOp::n(op), CoreOp::word(op));
    // (5) the window as loads at t read it
    const bool idle_before = s.idle(t);
    publish(c, t);
    // ---- the work of t
    unsigned room = cfg.controller ? c.credits(dma) : ~0u;
    s.run_cycle(t, room);
    for (unsigned k = 0; k < s.n_out; k++) {
        if (k > 0) FATAL("slot %u: two DMA packets in one cycle on one port", cfg.tile);
        if (!cfg.controller) FATAL("slot %u: a DMA packet without a DMA port", cfg.tile);
        c.put(dma, s.out[k].lo, s.out[k].h, s.out[k].hi);
    }
    for (unsigned n = 1; n <= 3; n++) if (s.per[n].type != PT_ABSENT) s.visit(n, [&](auto& q) { q.end(c, t); });
    // ---- the wake: the next cycle while anything is in progress, and once more after it (to publish what it left)
    if (active || !idle_before || !s.idle(t + 1)) return c.next();
    const int64_t w = s.next_wake_at(t);
    return w == INT64_MAX ? sim::Wake::none() : sim::Wake::at_cycle(sim::Cycle(w));
}

}  // namespace units
