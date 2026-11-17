// cache_system.h: the path to LPDDR as one piece of the simulator: the cache's controller (units/cache_ctl.h), the AXI
// arbiter (units/axi_arbiter.h) and the LPDDR model on the file store (units/lpddr_model.h), wired as
// AXI-INTERFACE.md and CACHE-INTERFACE.md have them, with the cache on the arbiter's port 0. updater_wip, WORKER5.
//
// A CYCLE. A harness (or the Tracer's fill port, below) sets `in`'s requests for cycle t, then calls tick(t): every
// unit's outputs of t are taken first, then each unit's edge with the others' outputs (axi_port.h's convention). The
// fill port's two requests in `in` are cleared after each tick (the Tracer hands one over at its handshake); a look-up
// port's request is the harness's to hold. The arbiter's other ports carry `clients[c]`, a harness's AXI masters'
// outputs of the cycle (zeros: none).
//
// THE TRACER'S FILL PORT (the setting `cache_ctl`; CHANGES-v1.37.md): attach(tracer) gives the Tracer a FillPort whose
// readies are the cache's, whose read and write hand the cache the request at its handshake, whose responses are
// polled from the cache's response registers, and whose tick is this system's edge, called at the end of the
// Tracer's every step.
#pragma once

#include <cstdint>
#include <cstring>

#include "units/axi_arbiter.h"
#include "units/cache_ctl.h"
#include "units/file_store.h"
#include "units/lpddr_model.h"
#include "units/tracer.h"

namespace units {

struct CacheSystemConfig {
    cache::Config cache;
    axi::ArbiterConfig arbiter;
    axi::LpddrTiming lpddr;
    bool stale_check = true;           // the cache's stale-line check against the store
};

class CacheSystem {
public:
    CacheSystem(tracer::FileStore* store, const CacheSystemConfig& c = {}) : ctl(c.cache), arb(c.arbiter), lpddr(store, c.lpddr) {
        if (c.stale_check) ctl.check_store = store;
        port.ctx = this;
        port.read_ready = [](void* x, int64_t) { return static_cast<CacheSystem*>(x)->ctl.out().fr_ready; };
        port.write_ready = [](void* x, int64_t) { return static_cast<CacheSystem*>(x)->ctl.out().fw_ready; };
        port.read = [](void* x, int64_t, int32_t root, int depth, int64_t line, tracer::MemSystem::Response&) -> int64_t {
            cache::FillRead& r = static_cast<CacheSystem*>(x)->in.fr;
            r.valid = true; r.root = root; r.depth = uint8_t(depth); r.line = line;
            return INT64_MAX;
        };
        port.write = [](void* x, int64_t, int32_t root, int depth, int64_t line, const uint64_t* data, int&) -> int64_t {
            cache::FillWrite& w = static_cast<CacheSystem*>(x)->in.fw;
            w.valid = true; w.root = root; w.depth = uint8_t(depth); w.line = line;
            std::memcpy(w.data, data, sizeof w.data);
            return INT64_MAX;
        };
        port.read_response = [](void* x, int64_t, tracer::MemSystem::Response& r) {
            const cache::Outputs& o = static_cast<CacheSystem*>(x)->ctl.out();
            if (!o.frr_valid) return false;
            std::memcpy(r.data, o.frr_data, sizeof r.data);
            r.status = o.frr_status;
            return true;
        };
        port.write_response = [](void* x, int64_t, int& status) {
            const cache::Outputs& o = static_cast<CacheSystem*>(x)->ctl.out();
            if (!o.fwr_valid) return false;
            status = o.fwr_status;
            return true;
        };
        port.tick = [](void* x, int64_t t) { static_cast<CacheSystem*>(x)->tick(t); };
    }

    void attach(tracer::Tracer& tr) { tr.fill_port = &port; }

    void tick(int64_t t) {
        if (last_t_ != INT64_MIN && t != last_t_ + 1 && !idle())
            FATAL("cache system: cycles %lld to %lld skipped while it works", (long long)(last_t_ + 1), (long long)(t - 1));
        last_t_ = t;
        clients[axi::CACHE] = ctl.out().axi;
        const axi::MasterOut m = arb.controller_out();
        const axi::SlaveOut s = lpddr.out();
        in.axi = arb.client_out(axi::CACHE);
        ctl.edge(t, in);
        lpddr.edge(t, m);
        arb.edge(t, clients, s);
        in.fr.valid = false;
        in.fw.valid = false;
    }
    bool idle() const { return ctl.idle() && arb.idle() && lpddr.idle(); }

    cache::CacheCtl ctl;
    AxiArbiter arb;
    LpddrModel lpddr;
    cache::Inputs in;
    axi::MasterOut clients[axi::CLIENTS];
    tracer::Tracer::FillPort port;

private:
    int64_t last_t_ = INT64_MIN;
};

}  // namespace units
