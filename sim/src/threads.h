// threads.h: one machine over several threads (SIMULATOR-NEXT 4, the second form). Not for unit authors.
//
// The partition is `Config::domain(d)`. A configuration with more than one domain is split, before anything is built,
// into one closed configuration per domain: every connection that crosses domains is cut and each end wired to a
// **boundary unit** (a placeholder with no logic that never steps after cycle 1): in the producer's domain the output
// feeds a boundary input (the *proxy*, a real port in that domain's pools, where the producer's own view of the port
// lives: a Link's count in flight, a credit Fifo's credits, a Hold's occupancy); in the consumer's domain the input is fed
// by a boundary output. Each domain is then built by the ordinary build(), unchanged, and the builds are joined:
//
//   - the producer's route points at an **outbox** for the destination domain (a ring of buckets indexed like a wheel,
//     one per (source, destination, kind, width, latency)), so a put runs the same code either way;
//   - every `interval` cycles all threads meet at a **barrier**; after it each domain **drains** the outboxes addressed to
//     it into its own wheels, at the same bucket, so a word arrives at t + L exactly as in one domain (the boundary is
//     never a hop); a credit's return travels back the same way;
//   - what a producer reads as "at the start of t" and a domain can't see (a Hold's occupancy across, a Count's other
//     domains' increments) comes over the barrier as **status words**, written before it and read after it.
//
// The interval is the smallest latency of any crossing (a Link's L; 1 for a Fifo, credit Fifo, Hold, Reg or Count), so
// nothing put in an interval is due before its drain. A Wire or RegFile across domains is fatal at build; a Count read
// with `now` across is caught in debug builds (A3).
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "barrier.h"
#include "config_impl.h"
#include "digest_view.h"
#include "internals.h"

namespace sim::detail {

bool split_needed(const Internals::CImpl& I);        // more than one domain declared, and more than one engine
void check_domains(const Internals::CImpl& I);       // no Wire or RegFile across declared domains (fatal)
// (partition.cpp): Placement::by_load: each unit's group in [0, groups), the interval it assumed, a report line.
std::vector<unsigned> place_by_load(const Internals::CImpl& G, unsigned threads, unsigned& groups, Cycle& interval,
                                    std::string& report);

struct Xl {                                      // an outbox entry's translation, by its source-side index
    uint32_t dst = kNone;                        // the destination port's index in the destination domain
    uint16_t scls = kNoClass, dcls = kNoClass;   // by reference: the size class at each end
};

struct Outbox {
    uint32_t src, dst;                           // domains; dst == kFan: a Reg read in several domains (`fan` below)
    Kind kind;
    uint8_t width;
    uint16_t latency;
    uint32_t mask, routes;                       // W - 1 (W >= 2 x interval), the routes appending to it
    Bucket* buckets;
    std::vector<Xl> xl;                          // dst != kFan: by source index
    std::vector<std::vector<Xl>> fan;            // dst == kFan: by destination domain, then source index
    std::vector<uint32_t> hold_of;               // a Hold outbox: the crossing (XHold) of each source proxy, else empty
    std::vector<uint32_t> wheel;                 // by domain: the wheel it drains into there (kNone: not drained there)
    uint16_t src_wheel = 0;                      // its index in the source domain's wheel table (Route::wheel)
    bool direct = false;                         // a Link outbox by value: delivered from here by its destination at
                                                 // its cycle (Engine::deliver_xin), not copied at the barrier
    std::vector<uint32_t> xdst;                  //   its translation, by source index (xl's dst)
};
inline constexpr uint32_t kFan = 0xFFFFFFFEu;

struct alignas(64) XHold {                       // a Hold across domains (its own line: written by its producer's thread)
    uint32_t p, proxy, route;                    // the producer's domain, its proxy HoldPort, the producer's route
    uint32_t q, port;                            // the consumer's domain and HoldPort
    uint32_t slot;                               // its status word
    Cycle refused_at = kNoCycle;                 // the put cycle of the last refusal (a by-reference slot is kept)
};
struct alignas(64) XCount {                      // one domain's replica of a Count read across domains (its own line)
    uint32_t dom, idx, slot;                     // its domain, its CountPort, its status word (the delta)
    uint32_t comp;
    uint64_t base = 0;                           // the machine's total at the last barrier
    std::vector<uint32_t> reps;                  // every replica of its set (XCount ids), this one included
};

struct Dom {
    Internals::SImpl* S = nullptr;                      // the domain's own machine (the build)
    Engine* e = nullptr;
    unsigned thread = 0;
    std::vector<uint32_t> out, in;               // outboxes it fills, outboxes it drains
    std::vector<uint32_t> link_proxies;          // proxy LinkPorts: their count in flight restarts each interval
    std::vector<uint32_t> holds_p, holds_q;      // XHolds it produces, consumes
    std::vector<uint32_t> counts;                // its XCount replicas
    std::vector<const Word*> slot_base;          // by size class: its pool's slot storage (the drain copies from it)
    uint32_t next_slot = 0, sent_slot = 0;       // its status words: its next event when idle, the words it sent
    uint32_t stop_slot = 0;                      // the cycle a unit asked to stop in, kNoCycle if none
};

struct Threads {
    std::vector<Sim> sims;                       // the domains' machines (the build), owned here
    std::vector<Dom> doms;
    std::vector<Outbox> outboxes;
    std::vector<XHold> holds;
    std::vector<XCount> counts;
    std::unique_ptr<Block> status_block;
    uint64_t* status[2] = {nullptr, nullptr};    // status words, by interval parity, one per cache line (slot x kLineWords):
                                                 // each written by one domain's thread
    std::vector<std::vector<Wheel>> wheel_tables;   // per domain: its wheels, then its outboxes (Route::wheel indexes it)
    std::vector<std::unique_ptr<Block>> blocks;  // the outboxes' storage, per source domain
    DigestView view;
    std::unordered_map<std::string, uint32_t> dom_of;   // unit name -> domain
    std::vector<std::string> port_names;         // outbox names (the buckets keep the pointer)
    std::vector<const TypeInfo*> types;          // the configuration's unit types, ids in its first-add order
    Sizes sz;
    std::string report;
    Cycle interval = 0;                          // kNoCycle: no crossing, no barrier until the end of a run
    unsigned nthreads = 1;
    // Options::time_threads (a measurement option): per thread, time stamp ticks summed over the runs, by phase; and
    // per interval, the slowest and the mean of the threads' compute (thread 0 sums them after the barrier).
    struct alignas(64) PhaseTicks { uint64_t run = 0, publish = 0, wait = 0, drain = 0, clear = 0, end = 0, total = 0; uint64_t last_run[2] = {0, 0}; };
    // last_run by the interval's parity: thread 0 reads [par] after the barrier while the others may already write [par ^ 1]
    std::vector<PhaseTicks> ph;
    std::vector<std::vector<Engine::XIn>> xins;  // per domain, its incoming direct outboxes
    uint64_t ph_intervals = 0, ph_max_run = 0, ph_sum_run = 0;
    bool timing = false;
    std::string thread_times() const;
    std::vector<int> cpus;
    Barrier barrier;
    // the run: thread 0 is the caller's; the workers wait between runs
    struct Span { Cycle t0, n; };
    std::vector<std::thread> workers;
    std::mutex mu;
    std::condition_variable cv;
    uint64_t go = 0;                             // the run number (under mu)
    Cycle target = 0;
    bool stop = false, guard = false;
    // what each thread is doing, for the barrier's watchdog (debug): its cycle, its domain, its phase
    struct alignas(64) Where { std::atomic<uint64_t> cycle{0}; std::atomic<uint32_t> dom{0}; std::atomic<const char*> phase{"idle"}; };
    std::unique_ptr<Where[]> where;
    void at(unsigned t, Cycle c, uint32_t d, const char* ph) {
        if constexpr (kDebug) {
            where[t].cycle.store(c, std::memory_order_relaxed);
            where[t].dom.store(d, std::memory_order_relaxed);
            where[t].phase.store(ph, std::memory_order_relaxed);
        }
    }
    static void report_where(void* self);

    ~Threads();
    void run_until(Cycle end);
    Cycle now() const { return doms[0].e->now; }
    const Sizes& sizes() const { return sz; }
    std::string partition() const { return report; }
    uint16_t type_of(const std::string& name) const;
    void* unit_ptr(const std::string& name, const TypeInfo* type) const;
    uint64_t steps(const std::string& name) const;
    void wake(const std::string& name);              // Sim::wake, in the unit's domain
    void wake_all();                                 // (harness-only): Sim::wake_all, every domain
    bool in_flight(const std::vector<uint16_t>& shifted, const std::vector<uint16_t>& ignored) const;          // (harness-only): Sim::in_flight, any domain
    uint64_t digest() const;
    std::string sleepers() const;

    // the loop (threads.cpp)
    void thread_main(unsigned t);
    void run_thread(unsigned t, Cycle end);
    void publish(Dom& d, Cycle t0, Cycle n, unsigned par);
    void drain(uint32_t di, Cycle t0, Cycle n, unsigned par, bool any);
    void clear(Dom& d, Span s);
    uint32_t dom_index(const std::string& name) const;
};

}  // namespace sim::detail
