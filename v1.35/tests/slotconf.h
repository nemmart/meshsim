// slotconf.h: the peripheral slot's conformance, the C++ side: a SlotConf script (oracle/ref/SlotConf.java)
// replayed on two units::SlotCore (tile 5: the test peripheral; tile 6: the replayer), with SlotConf's observations,
// character for character.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "units/linkword.h"
#include "units/periph_slot.h"
#include "units/replayer.h"
#include "units/test_peripheral.h"

namespace slotconf {

inline std::string hex(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "%llx", (unsigned long long)v); return b; }

struct M {
    std::unique_ptr<units::SlotCore> slot[2];
    unsigned dma_seen[2] = {};
    uint64_t last[64] = {};
    bool first = true;
    std::string obs, line;
    std::vector<units::DmaPacket> dma_out[2];   // every packet so far (ref's dmaOut)

    M() {
        for (int s = 0; s < 2; s++) { slot[s] = std::make_unique<units::SlotCore>(); slot[s]->tile = 5 + unsigned(s); }
        units::PeriphSlotConfig pc;
        slot[0]->install(1, units::PT_TEST, pc);
        slot[1]->install(1, units::PT_REPLAYER, pc);
    }
    units::TestPeripheral& x() { return slot[0]->test_peripheral(1); }
    units::Replayer& r() { return slot[1]->replayer(1); }

    void op(int64_t t, const std::string& o) {
        std::vector<std::string> f;
        size_t p = 0;
        for (;;) { size_t q = o.find(':', p); f.push_back(o.substr(p, q == std::string::npos ? q : q - p)); if (q == std::string::npos) break; p = q + 1; }
        const std::string& k = f[0];
        auto num = [](const std::string& s, int base) { return std::strtoull(s.c_str(), nullptr, base); };
        if (k == "P") x().post(t);
        else if (k == "C") x().credit(t);
        else if (k == "L1") x().level(true);
        else if (k == "L0") x().level(false);
        else if (k == "F") x().fault();
        else if (k == "B1") x().burst = true;
        else if (k == "B0") x().burst = false;
        else if (k == "A") {
            unsigned s = unsigned(num(f[1], 10));
            uint32_t off = uint32_t(num(f[2], 16));
            uint64_t h = units::lw::hdr(7, 5 + s, 0, 0, 0x04u << 24 | off, units::lw::SIZE_128);
            slot[s]->accept(t, 1, num(f[3], 16), h, num(f[4], 16));
        } else if (k == "R") {
            uint32_t v;
            bool ok = slot[num(f[1], 10)]->read(t, 1, unsigned(num(f[2], 10)), v);
            line += ' ';
            line += ok ? hex(v) : std::string("b");
        } else if (k == "W") {
            bool ok = slot[num(f[1], 10)]->write(t, 1, unsigned(num(f[2], 10)), uint32_t(num(f[3], 16)));
            line += ok ? " 1" : " 0";
        } else if (k == "T") line += slot[num(f[1], 10)]->store_stalls(t, 1, unsigned(num(f[2], 10))) ? " s" : " -";
        else if (k == "S") {
            for (int s = 0; s < 2; s++) {
                slot[s]->run_cycle(t);
                for (unsigned i = 0; i < slot[s]->n_out; i++) dma_out[s].push_back(slot[s]->out[i]);
            }
            for (int s = 0; s < 2; s++) slot[s]->end_cycle(t);
            observe(t);
        } else FATAL("slotconf: op %s", o.c_str());
    }

    void observe(int64_t t) {
        const int64_t u = t + 1;
        uint64_t v[64];
        unsigned k = 0;
        for (int s = 0; s < 2; s++) {
            units::SlotCore& p = *slot[s];
            v[k++] = p.live_events(u); v[k++] = p.live_errors(u);
            v[k++] = p.accepted; v[k++] = p.reserved_accesses; v[k++] = p.violation_total();
            const units::SlotCore::Dma& d = p.dma[1];
            v[k++] = (d.busy_at(u) ? 1 : 0) | d.done_bit(u) | d.transfers << 8 | d.packets << 24;
        }
        units::TestPeripheral& tp = x();
        v[k++] = uint64_t(tp.runs); v[k++] = uint64_t(int64_t(int32_t(tp.scratch))); /* ref: an int, sign-extended */ v[k++] = uint64_t(tp.errors_); v[k++] = tp.running_ ? 1 : 0; v[k++] = tp.n_accepts;
        for (unsigned b = 0; b < 5; b++) v[k++] = tp.ev.count_of(b);
        v[k++] = tp.ev.full_posts;
        units::Replayer& rp = r();
        v[k++] = rp.running(u) ? 1 : 0; v[k++] = uint64_t(rp.errors(u)); rp.sync(u); v[k++] = rp.events().value(u);
        v[k++] = rp.code.occ | rp.rlog.occ << 9 | rp.wlog.occ << 18 | uint64_t(rp.exp.occ) << 27;
        v[k++] = rp.rows_total;
        std::string b;
        for (unsigned i = 0; i < k; i++)
            if (first || v[i] != last[i]) { b += ' ' + std::to_string(i) + '=' + hex(v[i]); last[i] = v[i]; }
        for (int s = 0; s < 2; s++)
            for (; dma_seen[s] < dma_out[s].size(); dma_seen[s]++) {
                const units::DmaPacket& d = dma_out[s][dma_seen[s]];
                b += " d" + std::to_string(s) + '=' + hex(uint64_t(d.t)) + ',' + hex(units::lw::dst(d.h)) + ',' + hex(units::lw::mode(d.h)) + ',' +
                     hex(units::lw::counter(d.h)) + ',' + hex(units::lw::addr(d.h)) + ',' + hex(d.lo) + ',' + hex(d.hi);
            }
        first = false;
        if (!line.empty() || !b.empty()) obs += std::to_string(t) + line + " |" + b + "\n";
        line.clear();
    }
};

inline std::string run(const std::string& script) {
    M m;
    size_t p = 0;
    while (p < script.size()) {
        size_t e = script.find('\n', p);
        if (e == std::string::npos) e = script.size();
        std::string l = script.substr(p, e - p);
        p = e + 1;
        if (l.empty() || l[0] == '#') continue;
        size_t q = l.find(' ');
        int64_t t = std::strtoll(l.c_str(), nullptr, 10);
        while (q != std::string::npos) {
            size_t r = l.find(' ', q + 1);
            m.op(t, l.substr(q + 1, r == std::string::npos ? r : r - q - 1));
            q = r;
        }
    }
    return "# slotconf observations v1\n" + m.obs;
}

}  // namespace slotconf
