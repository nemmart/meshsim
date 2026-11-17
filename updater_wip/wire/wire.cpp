// The wire: see wire.h for the region, NOTES.md for why it is exact and cannot deadlock.
//
// Memory ordering, at every access between processes:
//   - The producer writes a window's bytes into its slot with plain stores, then stores sent = w + 1. The consumer
//     loads sent before reading the slot. RELEASE and ACQUIRE would make the bytes visible; the store is SEQ_CST
//     for the sleep handshake below (on x86 one locked instruction a window; loads cost the same either way).
//   - The consumer, done with a window, stores consumed (SEQ_CST, at least RELEASE); the producer loads it (at
//     least ACQUIRE) before writing into that slot again, so the consumer's reads come before the overwrite.
//   - final_w and gone: the same. final_w is stored after the last sent, so a consumer that sees it sees every
//     window before it.
//   - The sleep handshake is a store-then-load on each side: the waiter stores its sleeping flag and loads the
//     counter; the publisher stores the counter and loads the flag. With all four SEQ_CST they are in one total
//     order, so at least one side sees the other's store: a wake is never lost. (No fences: the thread sanitizer
//     does not model them, and this way it checks the same code that runs.) The futex wait is also timed.
//   - waiting_on, windows, cycles: read only by the launcher's watchdog and messages; RELAXED. state: RELEASE and
//     ACQUIRE, so a launcher that sees "finished" sees the final_w before it.
#include "wire.h"

#include <linux/futex.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>

namespace wire {

namespace {

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    asm volatile("yield" ::: "memory");
#endif
}

inline uint64_t now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

inline uint32_t* futex_addr(std::atomic<uint32_t>& a) { return reinterpret_cast<uint32_t*>(&a); }

// Shared (not FUTEX_PRIVATE): the word is in memory shared between processes.
void futex_wait(std::atomic<uint32_t>& word, uint32_t expected, uint64_t timeout_ns) {
    timespec ts;
    ts.tv_sec = time_t(timeout_ns / 1000000000ull);
    ts.tv_nsec = long(timeout_ns % 1000000000ull);
    syscall(SYS_futex, futex_addr(word), FUTEX_WAIT, expected, &ts, nullptr, 0);
}

void futex_wake(std::atomic<uint32_t>& word) {
    syscall(SYS_futex, futex_addr(word), FUTEX_WAKE, 0x7fffffff, nullptr, nullptr, 0);
}

// The publisher's half of the handshake: the counter was just stored (SEQ_CST).
inline void ring(std::atomic<uint32_t>& bell, std::atomic<uint32_t>& waiter_sleeping) {
    if (waiter_sleeping.load(std::memory_order_seq_cst) != 0) {
        bell.fetch_add(1, std::memory_order_release);
        futex_wake(bell);
    }
}

constexpr size_t round_up(size_t x, size_t a) { return (x + a - 1) / a * a; }

struct Layout {
    size_t proc_off, link_off, dir_stride, total;
};

Layout layout(const Params& p) {
    Layout L;
    L.proc_off = 2 * kLine;
    L.link_off = L.proc_off + size_t(p.clients + 1) * kLine;
    const size_t data = size_t(p.slots) * p.window * p.lane_bytes;
    L.dir_stride = 2 * kLine + round_up(data, kLine);
    L.total = L.link_off + size_t(p.clients) * 2 * L.dir_stride;
    return L;
}

uint8_t* dir_base(void* mem, int link, Dir d) {
    const Header* h = static_cast<const Header*>(mem);
    Params p{h->clients, h->window, h->lane_bytes, h->slots};
    const Layout L = layout(p);
    return static_cast<uint8_t*>(mem) + L.link_off + (size_t(link) * 2 + size_t(d)) * L.dir_stride;
}

// ---- Test hooks (check builds only): seeded random pauses at the library's own steps. ----
#ifdef WIRE_CHECK
// Per thread, so that wire-threads under the thread sanitizer has no race of its own here.
thread_local uint32_t g_pause_ppm = 0;
thread_local uint32_t g_pause_max_us = 0;
thread_local uint64_t g_pause_rng = 0;

void hook_setup(int process) {
    const char* e = getenv("WIRE_PAUSE");  // "ppm:max_us"
    if (!e) return;
    unsigned ppm = 0, us = 0;
    if (sscanf(e, "%u:%u", &ppm, &us) != 2) return;
    g_pause_ppm = ppm;
    g_pause_max_us = us ? us : 1;
    // Deliberately not reproducible: the point is to shake the order of events.
    g_pause_rng = now_ns() ^ (uint64_t(getpid()) << 32) ^ (uint64_t(process) << 56) ^ uint64_t(uintptr_t(&e));
}

inline void hook() {
    if (g_pause_ppm == 0) return;
    uint64_t x = (g_pause_rng += 0x9e3779b97f4a7c15ull);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= x >> 31;
    if (x % 1000000u < g_pause_ppm) {
        timespec ts{0, long((x >> 20) % g_pause_max_us) * 1000};
        nanosleep(&ts, nullptr);
    }
}
#else
inline void hook_setup(int) {}
inline void hook() {}
#endif

}  // namespace

// ---- The region ----

bool params_valid(const Params& p, char* err, size_t n) {
    if (p.clients < 1 || p.clients > kMaxClients) {
        snprintf(err, n, "clients must be 1 to %u", kMaxClients);
        return false;
    }
    if (p.window < 1 || p.window > kMaxWindow) {
        snprintf(err, n, "the window must be 1 to %u cycles", kMaxWindow);
        return false;
    }
    if (p.lane_bytes < 1 || p.lane_bytes > kMaxLaneBytes) {
        snprintf(err, n, "the bytes a cycle must be 1 to %u", kMaxLaneBytes);
        return false;
    }
    if (p.slots < 2 || p.slots > kMaxSlots) {
        snprintf(err, n, "the slots must be 2 to %u (2 is the least that cannot deadlock)", kMaxSlots);
        return false;
    }
    if (layout(p).total > (size_t(1) << 32)) {
        snprintf(err, n, "the region would be over 4 GiB");
        return false;
    }
    return true;
}

size_t region_size(const Params& p) { return layout(p).total; }

Header* region_header(void* mem) { return static_cast<Header*>(mem); }
AbortLine* region_abort_line(void* mem) {
    return reinterpret_cast<AbortLine*>(static_cast<uint8_t*>(mem) + kLine);
}
ProcLine* region_proc(void* mem, int process) {
    return reinterpret_cast<ProcLine*>(static_cast<uint8_t*>(mem) + 2 * kLine + size_t(process) * kLine);
}
ProdLine* region_prod(void* mem, int link, Dir d) { return reinterpret_cast<ProdLine*>(dir_base(mem, link, d)); }
ConsLine* region_cons(void* mem, int link, Dir d) {
    return reinterpret_cast<ConsLine*>(dir_base(mem, link, d) + kLine);
}
uint8_t* region_data(void* mem, int link, Dir d) { return dir_base(mem, link, d) + 2 * kLine; }

void region_init(void* mem, const Params& p, const char* name, int32_t launcher_pid, uint64_t run_id) {
    const Layout L = layout(p);
    memset(mem, 0, L.total);
    Header* h = new (mem) Header{};
    h->magic = kMagic;
    h->version = kVersion;
    h->size = L.total;
    h->clients = p.clients;
    h->window = p.window;
    h->lane_bytes = p.lane_bytes;
    h->slots = p.slots;
    h->run_id = run_id;
    h->launcher_pid = launcher_pid;
    snprintf(h->name, sizeof h->name, "%s", name ? name : "");
    AbortLine* a = new (region_abort_line(mem)) AbortLine{};
    a->who.store(-1, std::memory_order_relaxed);
    for (uint32_t i = 0; i <= p.clients; ++i) {
        ProcLine* pl = new (region_proc(mem, int(i))) ProcLine{};
        pl->waiting_on.store(-1, std::memory_order_relaxed);
    }
    for (uint32_t i = 0; i < p.clients; ++i)
        for (Dir d : {kDown, kUp}) {
            ProdLine* pr = new (region_prod(mem, int(i), d)) ProdLine{};
            pr->final_w.store(kNever, std::memory_order_relaxed);
            new (region_cons(mem, int(i), d)) ConsLine{};
        }
    // No fence: the children see the region through fork and exec, the threads through their creation.
}

bool region_check(const void* cmem, size_t size, Params* p, char* err, size_t n) {
    if (size < sizeof(Header)) {
        snprintf(err, n, "the region is too small (%zu bytes)", size);
        return false;
    }
    const Header* h = static_cast<const Header*>(cmem);
    if (h->magic != kMagic || h->version != kVersion) {
        snprintf(err, n, "the region is not a wire region of version %u", kVersion);
        return false;
    }
    Params q{h->clients, h->window, h->lane_bytes, h->slots};
    if (!params_valid(q, err, n)) return false;
    if (h->size != size || region_size(q) != size) {
        snprintf(err, n, "the region's size (%zu) does not match its header", size);
        return false;
    }
    *p = q;
    return true;
}

bool region_abort(void* mem, uint32_t reason, int who, int32_t status) {
    AbortLine* a = region_abort_line(mem);
    uint32_t none = kAbortNone;
    // who and status are written before reason is published; a second abort does not overwrite them.
    if (a->reason.load(std::memory_order_acquire) != kAbortNone) return false;
    a->who.store(who, std::memory_order_relaxed);
    a->status.store(status, std::memory_order_relaxed);
    if (!a->reason.compare_exchange_strong(none, reason, std::memory_order_acq_rel)) return false;
    const Header* h = region_header(mem);
    for (uint32_t i = 0; i < h->clients; ++i)
        for (Dir d : {kDown, kUp}) {
            ProdLine* pr = region_prod(mem, int(i), d);
            ConsLine* co = region_cons(mem, int(i), d);
            pr->bell.fetch_add(1, std::memory_order_release);
            co->bell.fetch_add(1, std::memory_order_release);
            futex_wake(pr->bell);
            futex_wake(co->bell);
        }
    return true;
}

void process_name(int process, char* buf, size_t n) {
    if (process == 0)
        snprintf(buf, n, "driver");
    else
        snprintf(buf, n, "client %d", process - 1);
}

void abort_text(void* mem, char* buf, size_t n) {
    AbortLine* a = region_abort_line(mem);
    const uint32_t reason = a->reason.load(std::memory_order_acquire);
    const int who = a->who.load(std::memory_order_relaxed);
    const int st = a->status.load(std::memory_order_relaxed);
    char name[32] = "?";
    int pid = 0;
    if (who >= 0 && uint32_t(who) <= region_header(mem)->clients) {
        process_name(who, name, sizeof name);
        pid = region_proc(mem, who)->pid.load(std::memory_order_relaxed);
    }
    switch (reason) {
    case kAbortDied:
        snprintf(buf, n, "%s (pid %d) died of signal %d", name, pid, st);
        break;
    case kAbortFailed:
        snprintf(buf, n, "%s (pid %d) exited with status %d", name, pid, st);
        break;
    case kAbortUnfinished:
        snprintf(buf, n, "%s (pid %d) exited without finishing", name, pid);
        break;
    case kAbortStalled:
        snprintf(buf, n, "%s (pid %d) made no progress (stuck at window %d)", name, pid, st);
        break;
    case kAbortLauncher:
        snprintf(buf, n, "the launcher was stopped by signal %d", st);
        break;
    default:
        snprintf(buf, n, "no abort");
    }
}

// ---- A process's view ----

Wire::~Wire() {
    if (process_ >= 0 && !finished_) finish();
    if (mapped_ && mem_) munmap(mem_, size_);
}

bool Wire::attach_fd(int fd, const char* link, int clients, int process, uint32_t window, uint32_t lane_bytes,
                     char* err, size_t n) {
    if (fd < 0) {
        snprintf(err, n, "no wire region: start this process with wire-launch");
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        snprintf(err, n, "the wire region (fd %d) cannot be read: %s", fd, strerror(errno));
        return false;
    }
    const size_t size = size_t(st.st_size);
    void* m = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) {
        snprintf(err, n, "the wire region (fd %d) cannot be mapped: %s", fd, strerror(errno));
        return false;
    }
    mem_ = m;
    size_ = size;
    mapped_ = true;
    if (!region_check(mem_, size_, &params_, err, n)) return false;
    const Header* h = region_header(mem_);
    if (link && strncmp(link, h->name, sizeof h->name) != 0) {
        snprintf(err, n, "the wire region is run '%s', not '%s'", h->name, link);
        return false;
    }
    if (clients >= 0 && uint32_t(clients) != params_.clients) {
        snprintf(err, n, "--clients %d, but the run has %u", clients, params_.clients);
        return false;
    }
    if (window && window != params_.window) {
        snprintf(err, n, "--window %u, but the run's window is %u", window, params_.window);
        return false;
    }
    if (lane_bytes && lane_bytes != params_.lane_bytes) {
        snprintf(err, n, "--lane-bytes %u, but the run's is %u", lane_bytes, params_.lane_bytes);
        return false;
    }
    return attach_common(process, err, n);
}

bool Wire::attach_memory(void* mem, size_t size, int process, char* err, size_t n) {
    mem_ = mem;
    size_ = size;
    mapped_ = false;
    if (!region_check(mem_, size_, &params_, err, n)) return false;
    return attach_common(process, err, n);
}

bool Wire::attach_common(int process, char* err, size_t n) {
    if (process < 0 || uint32_t(process) > params_.clients) {
        snprintf(err, n, "process %d is not on a chain of %u clients", process, params_.clients);
        return false;
    }
    ProcLine* pl = region_proc(mem_, process);
    uint32_t absent = kStateAbsent;
    if (!pl->state.compare_exchange_strong(absent, kStateAttached, std::memory_order_acq_rel)) {
        char name[32];
        process_name(process, name, sizeof name);
        snprintf(err, n, "%s is already attached to this run", name);
        return false;
    }
    pl->pid.store(int32_t(getpid()), std::memory_order_relaxed);
    process_ = process;
    me_ = pl;
    abort_ = region_abort_line(mem_);

    const uint32_t W = params_.window, B = params_.lane_bytes, K = params_.slots;
    auto setup = [&](LinkEnd& e, int link, Dir out_dir, int peer) {
        const Dir in_dir = out_dir == kDown ? kUp : kDown;
        e.wire_ = this;
        e.peer_ = peer;
        e.out_prod_ = region_prod(mem_, link, out_dir);
        e.out_cons_ = region_cons(mem_, link, out_dir);
        e.out_data_ = region_data(mem_, link, out_dir);
        e.in_prod_ = region_prod(mem_, link, in_dir);
        e.in_cons_ = region_cons(mem_, link, in_dir);
        e.in_data_ = region_data(mem_, link, in_dir);
        e.W_ = W;
        e.B_ = B;
        e.K_ = K;
        e.slot_bytes_ = size_t(W) * B;
    };
    has_up_ = process > 0;
    has_down_ = uint32_t(process) < params_.clients;
    if (has_up_) setup(up_, process - 1, kUp, process - 1);
    if (has_down_) setup(down_, process, kDown, process + 1);

    // Waiting: spin only if every process can have a core of its own.
    cpu_set_t cs;
    int cpus = 1;
    if (sched_getaffinity(0, sizeof cs, &cs) == 0) cpus = CPU_COUNT(&cs);
    uint64_t spin_us = 20;
    if (const char* e = getenv("WIRE_SPIN_US")) spin_us = strtoull(e, nullptr, 10);
    spin_ns_ = uint32_t(cpus) >= params_.clients + 1 ? spin_us * 1000 : 0;
    yields_ = 4;
    hook_setup(process);
    return true;
}

void Wire::progress(uint64_t windows, uint64_t cycles) {
    me_->windows.store(windows, std::memory_order_relaxed);
    me_->cycles.store(cycles, std::memory_order_relaxed);
}

void Wire::check_abort() {
    if (abort_->reason.load(std::memory_order_acquire) != kAbortNone) give_up();
}

void Wire::give_up() {
    char why[160], name[32], msg[320];
    abort_text(mem_, why, sizeof why);
    process_name(process_, name, sizeof name);
    uint64_t cyc = 0;
    if (has_up_) cyc = up_.cycle();
    if (has_down_ && down_.cycle() > cyc) cyc = down_.cycle();
    snprintf(msg, sizeof msg, "wire: %s (pid %d) giving up at cycle %llu: %s", name, int(getpid()),
             (unsigned long long)cyc, why);
    me_->state.store(kStateFailed, std::memory_order_release);
    finished_ = true;  // nothing more is published
    if (on_abort_) on_abort_(msg);
    fprintf(stderr, "%s\n", msg);
    fflush(nullptr);
    _exit(3);
}

template <class Ready>
void Wire::wait_until(Ready ready, std::atomic<uint32_t>& bell, std::atomic<uint32_t>& my_sleeping, int peer) {
    if (ready()) return;
    me_->waiting_on.store(peer, std::memory_order_relaxed);
    const uint64_t t0 = now_ns();
    ++stats_.waits;
    if (spin_ns_) {
        for (;;) {
            for (int i = 0; i < 64; ++i) {
                if (ready()) goto done;
                cpu_relax();
            }
            if (now_ns() - t0 > spin_ns_) break;
        }
    }
    for (int i = 0; i < yields_; ++i) {
        sched_yield();
        if (ready()) goto done;
    }
    ++stats_.sleeps;
    for (;;) {
        check_abort();
        const uint32_t b = bell.load(std::memory_order_acquire);
        my_sleeping.store(1, std::memory_order_seq_cst);  // the waiter's half of the handshake
        if (ready()) break;
        futex_wait(bell, b, 100000000ull);  // 100 ms: for the abort check, not for correctness
        my_sleeping.store(0, std::memory_order_relaxed);
        if (ready()) break;
    }
    my_sleeping.store(0, std::memory_order_relaxed);
done:
    stats_.wait_ns += now_ns() - t0;
    me_->waiting_on.store(-1, std::memory_order_relaxed);
    check_abort();
}

void Wire::finish() {
    if (finished_ || process_ < 0) return;
    if (has_up_) up_.finish();
    if (has_down_) down_.finish();
    finished_ = true;
    me_->state.store(kStateFinished, std::memory_order_release);
}

// ---- A link end ----

uint64_t LinkEnd::slot(uint64_t w) const {
#if defined(WIRE_PLANT) && WIRE_PLANT == 4
    return K_ > 2 ? w % (K_ - 1) : 0;  // planted: an index that wraps wrongly
#else
    return w % K_;
#endif
}

void LinkEnd::publish(uint64_t n) {
    out_prod_->sent.store(n, std::memory_order_seq_cst);  // the window's bytes, then this
    ring(out_prod_->bell, out_cons_->cons_sleeping);
}

void LinkEnd::begin_window() {
    const uint64_t w = win_;
    // The output slot: free once the far end has read window w - K (consumed >= w - K + 1).
    if (!out_free_ && w >= K_) {
        const uint64_t need = w - K_ + 1;
        ConsLine* c = out_cons_;
        wire_->wait_until(
            [c, need] {
                return c->consumed.load(std::memory_order_seq_cst) >= need ||
                       c->gone.load(std::memory_order_seq_cst) != 0;
            },
            c->bell, out_prod_->prod_sleeping, peer_);
        if (c->gone.load(std::memory_order_acquire) != 0) out_free_ = true;
    }
    out_slot_ = out_data_ + slot(w) * slot_bytes_;
#ifdef WIRE_CHECK
    memset(out_slot_, 0xA5, slot_bytes_);  // poison: a read before the bytes are written shows
#endif
    // The input: the far end's window w - 1 (window 0 receives zeros).
    if (w == 0 || in_done_) {
        in_slot_ = nullptr;
    } else {
#if defined(WIRE_PLANT) && WIRE_PLANT == 5
        in_slot_ = in_data_ + slot(w) * slot_bytes_;  // planted: a window early, without waiting
#elif defined(WIRE_PLANT) && WIRE_PLANT == 2
        in_slot_ = in_data_ + slot(w - 1) * slot_bytes_;  // planted: a read without waiting
#else
        ProdLine* p = in_prod_;
        wire_->wait_until(
            [p, w] {
                return p->sent.load(std::memory_order_seq_cst) >= w ||
                       p->final_w.load(std::memory_order_seq_cst) <= w - 1;
            },
            p->bell, in_cons_->cons_sleeping, peer_);
        if (p->final_w.load(std::memory_order_acquire) <= w - 1) {
            in_done_ = true;
            in_slot_ = nullptr;
        } else {
            in_slot_ = in_data_ + slot(w - 1) * slot_bytes_;
        }
#endif
    }
    hook();
}

void LinkEnd::end_window() {
    const uint64_t w = win_;
    hook();
#if !(defined(WIRE_PLANT) && WIRE_PLANT == 1)
    publish(w + 1);
#endif
    if (w >= 1 && !in_done_) {
        in_cons_->consumed.store(w, std::memory_order_seq_cst);  // done reading the far end's window w - 1
        ring(in_cons_->bell, in_prod_->prod_sleeping);
    }
    hook();
    ++win_;
    off_ = 0;
    wire_->progress(win_, t_);
}

void LinkEnd::exchange(const uint8_t* out, uint8_t* in) {
    if (off_ == 0) begin_window();
    const uint32_t B = B_;
    uint8_t* o = out_slot_ + size_t(off_) * B;
#if defined(WIRE_PLANT) && WIRE_PLANT == 1
    if (off_ + 1 == W_) {  // planted: the window published before its last bytes are written
        publish(win_ + 1);
        hook();
    }
#endif
    for (uint32_t i = 0; i < B; ++i) o[i] = out[i];
#if defined(WIRE_PLANT) && WIRE_PLANT == 3
    // planted: a window one cycle out (the byte sent in cycle t arrives in t + W + 1)
    for (uint32_t i = 0; i < B; ++i) in[i] = off_ == 0 ? carry_[i] : (in_slot_ ? in_slot_[(off_ - 1) * B + i] : 0);
    if (off_ + 1 == W_)
        for (uint32_t i = 0; i < B; ++i) carry_[i] = in_slot_ ? in_slot_[off_ * B + i] : 0;
#else
    if (in_slot_) {
        const uint8_t* s = in_slot_ + size_t(off_) * B;
        for (uint32_t i = 0; i < B; ++i) in[i] = s[i];
    } else {
        for (uint32_t i = 0; i < B; ++i) in[i] = 0;
    }
#endif
    ++off_;
    ++t_;
    if (off_ == W_) end_window();
}

void LinkEnd::finish() {
    uint64_t f;
    if (off_ > 0) {
        // Mid-window: the rest of it is zeros, and it is published.
        memset(out_slot_ + size_t(off_) * B_, 0, size_t(W_ - off_) * B_);
        f = win_ + 1;
        publish(f);
    } else {
        f = win_;  // sent == win_ already
    }
    out_prod_->final_w.store(f, std::memory_order_seq_cst);
    ring(out_prod_->bell, out_cons_->cons_sleeping);
    in_cons_->gone.store(1, std::memory_order_seq_cst);
    ring(in_cons_->bell, in_prod_->prod_sleeping);
}

}  // namespace wire
