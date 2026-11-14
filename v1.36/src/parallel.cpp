// parallel.cpp: independent chips on independent threads. The configuration regime: std::thread, any C++.
#include "parallel.h"

#include <pthread.h>
#include <sched.h>

#include <thread>

#include "base/fatal.h"

namespace sim {

void pin_to_cpu(int cpu) {
    if (cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (int e = pthread_setaffinity_np(pthread_self(), sizeof set, &set)) FATAL("pin to cpu %d: error %d", cpu, e);
}

namespace {
int cpu_for(const std::vector<int>& cpus, size_t k) { return cpus.empty() ? -1 : cpus[k % cpus.size()]; }
}  // namespace

void run_independent(const std::vector<Sim*>& sims, Cycle cycles, const std::vector<int>& cpus) {
    for (size_t a = 0; a < sims.size(); a++)
        for (size_t b = a + 1; b < sims.size(); b++)
            if (sims[a] == sims[b]) FATAL("run_independent: Sim %zu is also Sim %zu", a, b);
    std::vector<std::thread> t;
    t.reserve(sims.size());
    for (size_t k = 0; k < sims.size(); k++)
        t.emplace_back([&, k] { pin_to_cpu(cpu_for(cpus, k)); sims[k]->run(cycles); });
    for (auto& x : t) x.join();
}

void run_independent(unsigned n, const std::function<Sim(unsigned)>& build, const std::function<void(unsigned, Sim&)>& run,
                     const std::vector<int>& cpus) {
    std::vector<std::thread> t;
    t.reserve(n);
    for (unsigned k = 0; k < n; k++)
        t.emplace_back([&, k] {
            pin_to_cpu(cpu_for(cpus, k));
            Sim s = build(k);
            run(k, s);
        });
    for (auto& x : t) x.join();
}

}  // namespace sim
