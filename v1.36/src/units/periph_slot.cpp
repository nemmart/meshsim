// periph_slot.cpp: the peripheral slot (periph_slot.h): SlotCore, the slot without ports, whose calls are made at once
// (v1.35's PeripheralSlot.java, method for method, with v1.36's control and DMA blocks and the RTL's cycles: the
// header's contract); PeriphSlot puts it behind the ports.
#include "units/periph_slot.h"

#include <type_traits>

#include "check.h"
#include "units/ingress_egress.h"
#include "units/linkword.h"
#include "units/replayer.h"
#include "units/rw_engine.h"
#include "units/test_peripheral.h"
#include "units/tracer.h"

namespace units {

// ================================================================ the dispatch: one case per peripheral type
template <class F>
decltype(auto) SlotCore::visit(unsigned n, F&& f) {
    switch (per[n].type) {
        case PT_REPLAYER: return f(*static_cast<Replayer*>(per[n].p));
        case PT_TEST: return f(*static_cast<TestPeripheral*>(per[n].p));
        case PT_TRACER: return f(*static_cast<tracer::Tracer*>(per[n].p));
        case PT_INGRESS: return f(*static_cast<IngressEgress*>(per[n].p));
        case PT_RW: return f(*static_cast<RwEngine*>(per[n].p));
        default: FATAL("slot %u: peripheral %u of type %u has no dispatch case", tile, n, unsigned(per[n].type));
    }
}

SlotCore::Ref::~Ref() {
    if (!p) return;
    switch (type) {
        case PT_REPLAYER: {
            delete[] rows;                                      // the slot's own buffer, not the replayer's now
            delete static_cast<Replayer*>(p);
            break;
        }
        case PT_TEST: delete static_cast<TestPeripheral*>(p); break;
        case PT_TRACER: delete static_cast<tracer::Tracer*>(p); break;   // its memory with it
        case PT_INGRESS: delete static_cast<IngressEgress*>(p); break;
        case PT_RW: delete static_cast<RwEngine*>(p); break;
        default: break;
    }
}
SlotCore::Ref& SlotCore::Ref::operator=(Ref&& o) noexcept {
    if (this != &o) { this->~Ref(); type = o.type; p = o.p; rows = o.rows; o.p = nullptr; o.type = PT_ABSENT; o.rows = nullptr; }
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
        } else if (per[n].type == PT_TRACER) {   // the Tracer's progress
            tracer::Tracer& x = tracer(n);
            mixin(x.rec[0]); mixin(x.rec[2]); mixin(uint64_t(x.rows)); mixin(uint64_t(x.code_n)); mixin(uint64_t(x.log_n));
            mixin(uint64_t(x.stat[0])); mixin(uint64_t(x.n_violations)); mixin(uint64_t(x.n_errors_logged));
        } else if (per[n].type == PT_TEST) {
            TestPeripheral& x = test_peripheral(n);
            mixin(uint64_t(x.runs)); mixin(x.scratch); mixin(x.n_accepts); mixin(x.taken[0]); mixin(x.taken[1]);
        } else if (per[n].type == PT_RW) {
            mixin(rw_engine(n).progress());
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
            auto* r = new Replayer(tile, c.replayer_log_entries);
            if (c.replayer_row_buffer) {
                per[n].rows = new uint64_t[c.replayer_row_buffer * Replayer::REC_LONGS];
                r->set_row_buffer(per[n].rows, c.replayer_row_buffer);
            }
            per[n].p = r;
            break;
        }
        case PT_TEST: per[n].p = new TestPeripheral(); break;
        // the Tracer, bare (ref's Config.cc3Tracer: new Tracer(me)); its tables come by region packets (tracer_ctl),
        // its memory from the harness after the build (tracer(n).own_memory(store)): a run without one is FATAL
        case PT_TRACER: per[n].p = new tracer::Tracer(int(tile)); break;
        // the master tile's Ingress/Egress (its own ports: the nets, wired by the chip) and the memory tile's RW engine
        // (rw_engine.h: its LPDDR controller comes from the harness after the build, rw_engine(n).attach(slave))
        case PT_INGRESS: per[n].p = new IngressEgress(); break;
        case PT_RW: per[n].p = new RwEngine(tile, c.rw); break;
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
IngressEgress& SlotCore::ingress_egress(unsigned n) {
    if (per[n].type != PT_INGRESS) FATAL("slot %u: peripheral %u is not Ingress/Egress", tile, n);
    return *static_cast<IngressEgress*>(per[n].p);
}
RwEngine& SlotCore::rw_engine(unsigned n) {
    if (per[n].type != PT_RW) FATAL("slot %u: peripheral %u is not the RW engine", tile, n);
    return *static_cast<RwEngine*>(per[n].p);
}

void SlotCore::violate(int64_t t, uint16_t code, uint32_t a, uint32_t b) {
    violations++;
    facts[n_facts % FACTS] = Fact{t, code, a, b};
    n_facts++;
    // ref's slot.violate goes to the register block's (tile.ctl.violate): a chip fact (the full post's depth with it)
    uint32_t depth = 0;
    if (code == F_FULL_POST) visit(a, [&](auto& p) { depth = p.events().depth[b]; });
    sim::check::record(uint64_t(t), int(tile), sim::check::Kind::SLOT_VIOLATION, code, a, b, depth);
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
    // (17.1): a peripheral without a DMA reads 0 in its block 1, unflagged; only a store is flagged (UB: the bus faults it)
    if (!store && block_of(w) == 1) return 0;
    reserved_accesses++;
    violate(t, F_RESERVED, n << 8 | w, store);
    return 0;
}

bool SlotCore::read(int64_t t, unsigned n, unsigned w, uint32_t& v) {
    if (per[n].type == PT_ABSENT) return false;
    if (w == P_EVENTS) {
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
            case P_PARAMS: v = uint32_t(p.params()); return true;
            case P_STATUS: v = (p.running(t) ? 1u : 0u) | (dma[n].busy_at(t) ? 2u : 0u) | (p.status_bits(t) & ~3u); return true;
            case P_ERRORS: v = uint32_t(p.errors(t)); return true;
            case P_EVENTS: case P_EVENTS_PEEK: p.sync(t); v = p.events().value(t) | dma[n].done_bit(t); return true;
            case P_EVENTS_MASK: v = mask[n]; return true;
            case P_START: v = 0; return true;                            // W: a load reads 0 (5.4)
            case P_DMA_HDR: v = has_dma ? dma[n].hdr : 0; return has_dma;
            case P_DMA_DEST_ADDR: v = has_dma ? dma[n].addr : 0; return has_dma;
            case P_DMA_START: v = 0; return has_dma;
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
    if (w < Win::OWN0) return w <= P_START || (w >= P_DMA_HDR && w <= P_DMA_START);
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
            case P_PARAMS: case P_STATUS: case P_ERRORS: case P_EVENTS: case P_EVENTS_PEEK: return false;
            case P_START: if (v & 1) p.run(t); return true;
            case P_DMA_START: if (!has_dma) { reserved(t, n, w, true); return true; } if (v & 1) dma_start(t, n); return true;
            case P_EVENTS_MASK: pend_mask[n] = v & event_bits(n); pend_mask_t[n] = t; return true;
            case P_DMA_HDR:
                if (!has_dma) { reserved(t, n, w, true); return true; }
                if (dma[n].busy) violate(t, F_DMA_HDR_BUSY, n);
                dma[n].hdr = v & DMA_HDR_BITS;                 // the size, the DMA's, and the bits above `queue` read 0 (5.4)
                return true;
            case P_DMA_DEST_ADDR:
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

// EVENTS_MASK has one bit per event of the peripheral (17.1; peripheral_control.veryl: EVENT_BITS of them): a store
// ignores the bits above them, and they read 0 (5.4).
uint32_t SlotCore::event_bits(unsigned n) {
    const unsigned bits = visit(n, [](auto& p) { return p.events().n; });
    return bits >= 32 ? ~0u : (1u << bits) - 1;
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

// A consuming load of EVENTS made in cycle t (17.2; peripheral_control.veryl), applied after the peripheral's step of
// t: one count from each bit it returned set under the mask, but a set of t survives the load and a fill of t sets the
// depth. dma_done is the slot's (depth 1): a transfer that ended in t leaves it set.
void SlotCore::consume(int64_t t, unsigned n, uint32_t bits) {
    visit(n, [&](auto& p) { p.consume_events_late(t, bits & ~2u); });
    if ((bits & 2) && dma[n].done_at != t) dma[n].done = 0;
}

// ---------------------------------------------------------------- the cycle
void SlotCore::run_cycle(int64_t t, unsigned dma_room) {
    n_out = 0;
    for (unsigned n = 1; n <= 3; n++) {
        if (per[n].type == PT_ABSENT) continue;
        dma_run(t, n, dma_room);                               // the DMA takes the head as it was at the start of t
        visit(n, [&](auto& p) { p.step(t); });
        visit(n, [&](auto& p) {
            const EventCounts& e = p.events();
            if (e.full_posts != full_seen[n]) { full_seen[n] = e.full_posts; violate(t, F_FULL_POST, n, e.last_full_bit); }
        });
    }
}

void SlotCore::end_cycle(int64_t t) {
    for (unsigned n = 1; n <= 3; n++) {
        if (per[n].type == PT_ABSENT) continue;
        if (consume_t[n] == t) consume(t, n, consume_bits[n]);
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
    const int qq = int(d.hdr >> DMA_HDR_QUEUE_LO & DMA_HDR_QUEUE_MASK);   // DMA_HDR.queue [28:27]
    if (d.busy) FATAL("slot %u: undefined behavior at %lld: peripheral %u's DMA: DMA_START while a transfer is in progress (17.4)", tile, (long long)t, n);
    visit(n, [&](auto& p) {
        if (qq >= p.out_queues())
            FATAL("slot %u: undefined behavior at %lld: peripheral %u's DMA: a transfer naming output queue %d, which the peripheral doesn't have (17.4)", tile, (long long)t, n, qq);
        d.q = qq; d.left = d.hdr >> DMA_HDR_COUNT_LO & DMA_HDR_COUNT_MASK; d.busy = true; d.started_at = t; d.transfers++;
        p.out_start(t, d.q, d.left);                           // a count of 0 too: the peripheral sees every start
    });
}

// The DMA's cycle t (peripheral_dma.veryl), before the peripheral's step of t: from the cycle after DMA_START's, an
// entry a cycle while one is to send, the queue's head is there and the router's input has room; the transfer ends
// the cycle after its last take (the cycle the last packet leaves; with a count of 0, the cycle after the start):
// dma_done is set then, and dma_busy falls at its end. A stopped peripheral's dma_done is withheld (the Tracer after
// an error: tracer_control.veryl's event_dma_done_set && !stopped).
void SlotCore::dma_run(int64_t t, unsigned n, unsigned& room) {
    Dma& d = dma[n];
    if (!d.busy || t <= d.started_at) return;
    // the end waits, as a take does, for the packet register: empty, or its packet leaving (peripheral_dma.veryl:
    // ending = busy && pending == 0 && (!full || packet_ready)); `room` is that, the router input's room for a packet
    if (d.left == 0) {
        if (dma_port && room == 0) return;
        d.busy = false;
        const bool withheld = visit(n, [&](auto& p) -> bool {
            if constexpr (requires { p.stopped(); }) return p.stopped();
            else return false;
        });
        if (!withheld) { d.done = 1; d.done_at = t; }
        return;
    }
    // 17.4: the count in 128-bit packets, every packet one queue entry; a WRITE carries DMA_DEST_ADDR, which then
    // advances by 16; a packet of any other mode carries 0 and leaves it; src is the tile's own id (16.4)
    bool ready = visit(n, [&](auto& p) { return p.out_ready(t, d.q); });
    if (!ready) return;
    if (dma_port && room == 0) return;
    uint64_t w[2];
    visit(n, [&](auto& p) { p.out_take(t, d.q, w); });
    const unsigned dst = hdr_dst(d.hdr), mode = hdr_mode(d.hdr), ctr = hdr_counter(d.hdr);
    const bool write = mode == lw::M_WRITE;
    if (n_out == 3) FATAL("slot %u: more than three DMA packets in a cycle", tile);
    out[n_out++] = DmaPacket{t, w[0], lw::hdr(tile, dst, mode, ctr, write ? d.addr : 0, lw::SIZE_128), w[1]};
    if (room) room--;
    d.packets++;
    if (write) d.addr += 16;
    d.left--;
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
    OwnPortsDecl decl(p, own);
    for (unsigned n = 1; n <= 3; n++) if (s.per[n].type != PT_ABSENT) s.visit(n, [&](auto& q) { q.ports(decl); });
}

// Every register of the window as ref's reads at t give it, computed the plain way (per word, through the slot's peek):
// the debug guard's recompute (D35), and the first publish's full write.
void PeriphSlot::full_values(int64_t t, uint32_t* out) {
    for (unsigned i = 0; i < Win::REGS; i++) out[i] = 0;
    for (unsigned n = 1; n <= 3; n++) {
        if (s.per[n].type == PT_ABSENT) continue;
        uint32_t v;
        for (unsigned w = 0; w <= P_DMA_START; w++) {
            if (w > P_START && w < P_DMA_HDR) continue;
            s.peek(t, n, w, v); out[Win::reg(n, Win::R_STD + Win::std(w))] = v;
        }
        uint32_t def[3] = {}, stall[3] = {}, wr[3] = {}, may[3] = {}, pops[3] = {};
        for (unsigned w = Win::OWN0; w < Win::WORDS; w++) {
            const unsigned k = w - Win::OWN0;
            if (s.peek(t, n, w, v)) def[k / 32] |= 1u << (k % 32);
            else v = 0;
            out[Win::reg(n, Win::R_OWN + k)] = v;
            if (s.store_stalls(t, n, w)) stall[k / 32] |= 1u << (k % 32);
        }
        s.visit(n, [&](auto& q) {
            for (unsigned w = Win::OWN0; w < Win::WORDS; w++) {
                const unsigned k = w - Win::OWN0;
                bool yes;
                if constexpr (requires { q.writable(0); }) yes = q.writable(int(w));
                else yes = (def[k / 32] >> (k % 32)) & 1;
                if (yes) wr[k / 32] |= 1u << (k % 32);
                if (q.may_stall(int(w))) may[k / 32] |= 1u << (k % 32);
                if (q.load_pops(int(w))) pops[k / 32] |= 1u << (k % 32);
            }
        });
        for (unsigned k = 0; k < 3; k++) {
            out[Win::reg(n, Win::R_DEFINED + k)] = def[k]; out[Win::reg(n, Win::R_STALL + k)] = stall[k];
            out[Win::reg(n, Win::R_WRITABLE + k)] = wr[k]; out[Win::reg(n, Win::R_MAYSTALL + k)] = may[k];
            out[Win::reg(n, Win::R_POPS + k)] = pops[k];
        }
    }
    out[Win::R_LIVE] = s.live_events(t) | s.live_errors(t) << 3;
}

namespace {
// A peripheral type with its own store_stalls (C&C 1's UART_TX), rather than PeripheralBase's (never).
template <class P>
constexpr bool kOwnStoreStalls = !std::is_same_v<decltype(&P::store_stalls), decltype(&PeripheralBase<P>::store_stalls)>;
}  // namespace

// D35: only the registers that changed are written, their values computed straight from each peripheral, once per
// peripheral (one dispatch, not one a word): the standard words, its defined own words only (a static list made at the
// first publish), its store stalls only if the type has them, the live bits from the values just computed. The first
// publish writes every register (the static ones: PARAMS' type, R_DEFINED, R_WRITABLE, the undefined words' zeros). The
// debug builds check every register against full_values after each publish (fatal, naming it).
void PeriphSlot::publish(sim::Ctx& c, int64_t t) {
    auto put = [&](unsigned i, uint32_t v) {
        if (published[i] != v) { c.write(window, i, v); published[i] = v; }
    };
    if (first) {
        uint32_t all[Win::REGS];
        full_values(t, all);
        for (unsigned i = 0; i < Win::REGS; i++) { c.write(window, i, all[i]); published[i] = all[i]; }
        for (unsigned n = 1; n <= 3; n++) {
            own_words_n[n] = 0;
            if (s.per[n].type == PT_ABSENT) continue;
            for (unsigned k = 0; k < Win::WORDS - Win::OWN0; k++)
                if (all[Win::reg(n, Win::R_DEFINED + k / 32)] >> (k % 32) & 1) own_words[n][own_words_n[n]++] = uint8_t(k);
        }
        first = false;
        return;
    }
    uint32_t live = 0;
    for (unsigned n = 1; n <= 3; n++) {
        if (s.per[n].type == PT_ABSENT) continue;
        s.visit(n, [&](auto& p) {
            using P = std::remove_reference_t<decltype(p)>;
            const SlotCore::Dma& d = s.dma[n];
            const unsigned base = Win::reg(n, 0);
            const uint32_t status = (p.running(t) ? 1u : 0u) | (d.busy_at(t) ? 2u : 0u) | (p.status_bits(t) & ~3u);
            const uint32_t err = uint32_t(p.errors(t));
            p.sync(t);
            const uint32_t ev = p.events().value(t) | d.done_bit(t);
            put(base + Win::R_STD + P_STATUS, status);
            put(base + Win::R_STD + P_ERRORS, err);
            put(base + Win::R_STD + P_EVENTS, ev);
            put(base + Win::R_STD + P_EVENTS_PEEK, ev);
            put(base + Win::R_STD + P_EVENTS_MASK, s.mask[n]);
            if (p.out_queues() > 0) { put(base + Win::R_STD + Win::std(P_DMA_HDR), d.hdr); put(base + Win::R_STD + Win::std(P_DMA_DEST_ADDR), d.addr); }
            for (unsigned i = 0; i < own_words_n[n]; i++) {
                const unsigned k = own_words[n][i];
                uint32_t v = 0;
                p.word(t, int(Win::OWN0 + k), v);
                put(base + Win::R_OWN + k, v);
            }
            if constexpr (kOwnStoreStalls<P>) {
                uint32_t stall[3] = {};
                for (unsigned i = 0; i < own_words_n[n]; i++) {
                    const unsigned k = own_words[n][i];
                    if (p.store_stalls(t, int(Win::OWN0 + k))) stall[k / 32] |= 1u << (k % 32);
                }
                for (unsigned k = 0; k < 3; k++) put(base + Win::R_STALL + k, stall[k]);
            }
            if (ev & s.mask[n]) live |= 1u << (n - 1);
            if (err != 0) live |= 1u << (n + 2);
        });
    }
    put(Win::R_LIVE, live);
    if constexpr (sim::kDebug) {
        uint32_t all[Win::REGS];
        full_values(t, all);
        for (unsigned i = 0; i < Win::REGS; i++) {
            if (all[i] == published[i]) continue;
            const unsigned n = i / Win::PER + 1, r = i % Win::PER;
            const char* what = i == Win::R_LIVE ? "the live bits" : r < Win::R_OWN ? "standard word" : r < Win::R_DEFINED ? "own word"
                             : r < Win::R_STALL ? "R_DEFINED" : r < Win::R_WRITABLE ? "R_STALL" : r < Win::R_MAYSTALL ? "R_WRITABLE" : r < Win::R_POPS ? "R_MAYSTALL" : "R_POPS";
            const unsigned word = r < Win::R_OWN ? r : r < Win::R_DEFINED ? Win::OWN0 + r - Win::R_OWN : r;
            FATAL("slot %u at %lld: window register %u (peripheral %u, %s %u) is 0x%x, published 0x%x (PeriphSlot::publish misses it)",
                  cfg.tile, (long long)t, i, i == Win::R_LIVE ? 0u : n, what, word, all[i], published[i]);
        }
    }
}

sim::Wake PeriphSlot::step(sim::Ctx& c) {
    const int64_t t = int64_t(c.now());
    bool active = first;
    // ---- level 0: (1) the peripherals' own inputs
    for (unsigned n = 1; n <= 3; n++) if (s.per[n].type != PT_ABSENT) s.visit(n, [&](auto& q) { q.begin(c, t, own); });
    // (2) the core's access of t - 1: a consume or the EVENTS_MASK store first (ref's endCycle(t - 1)) ...
    uint64_t op = 0;
    bool have_op = false;
    if (c.ready(core)) { op = c.peek(core)[0]; c.take(core); have_op = true; active = true; }
    if (have_op) {
        const unsigned n = CoreOp::n(op), w = CoreOp::word(op);
        if (CoreOp::kind(op) == CoreOp::CONSUME) {             // the load made in t - 1, after the step of t - 1
            s.consume(t - 1, n, CoreOp::value(op));
            have_op = false;
        } else if (CoreOp::kind(op) == CoreOp::LOAD) {         // a load made in t - 1 that pops, after the step of t - 1
            s.visit(n, [&](auto& p) { p.load_effect(t - 1, int(w)); });
            have_op = false;
        } else if (w == P_EVENTS_MASK) {
            s.mask[n] = CoreOp::value(op) & s.event_bits(n);
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
        if (!cfg.controller) { dma_record[dma_recorded++ % DMA_RECORD] = s.out[k]; continue; }   /* ref's dmaOut */
        if (k > 0) FATAL("slot %u: two DMA packets in one cycle on one port", cfg.tile);
        c.put(dma, s.out[k].lo, s.out[k].h, s.out[k].hi);
    }
    for (unsigned n = 1; n <= 3; n++) if (s.per[n].type != PT_ABSENT) s.visit(n, [&](auto& q) { q.end(c, t, own); });
    // ---- the wake: the next cycle while anything is in progress, and once more after it (to publish what it left)
    if (active || !idle_before || !s.idle(t + 1)) return c.next();
    const int64_t w = s.next_wake_at(t);
    return w == INT64_MAX ? sim::Wake::none() : sim::Wake::at_cycle(sim::Cycle(w));
}

}  // namespace units
