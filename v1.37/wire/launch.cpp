// wire-launch: makes the run's region, starts the driver and the clients, waits for them, reports each one's
// exit status, and stops the rest if one fails.
//
//   wire-launch --clients N [--window 64] [--lane-bytes 1] [--slots 4] [--link NAME] [--stall S] [--grace S]
//               [--debug WHO] [--debugger "gdb --args"]
//               -- DRIVER [ARGS...] -- CLIENT [ARGS...]
//
// The client command is started once for each client. In every argument, {k} becomes the client number
// ("driver" for the driver) and {p} the process number (0 the driver, k + 1 client k). Each command gets
// --clients N, --driver or --client k, --link NAME and --wire-fd FD appended.
//
// The region is anonymous (memfd), inherited by the children: nothing is left in /dev/shm, whatever happens.
// Every child is killed if the launcher dies (PR_SET_PDEATHSIG).
//
// --debug WHO (driver, or a client number) starts that one process under the debugger, with the terminal; the
// others get /dev/null for input and a process group of their own (so ^C reaches only the debugger), the
// launcher ignores ^C, and the stall watchdog is off unless --stall is given.
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "wire.h"

using namespace wire;

namespace {

volatile sig_atomic_t g_signal = 0;
void on_signal(int s) { g_signal = s; }

uint64_t now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

void sleep_ms(int ms) {
    timespec ts{ms / 1000, long(ms % 1000) * 1000000};
    nanosleep(&ts, nullptr);
}

[[noreturn]] void usage(const char* msg) {
    if (msg) fprintf(stderr, "wire-launch: %s\n", msg);
    fprintf(stderr,
            "usage: wire-launch --clients N [--window W] [--lane-bytes B] [--slots K] [--link NAME]\n"
            "                   [--stall SECONDS] [--grace SECONDS] [--debug driver|k] [--debugger \"gdb --args\"]\n"
            "                   -- DRIVER [ARGS...] -- CLIENT [ARGS...]\n");
    exit(2);
}

uint64_t need_u64(const char* name, const char* s) {
    errno = 0;
    char* end = nullptr;
    const unsigned long long v = strtoull(s, &end, 10);
    if (!*s || errno || *end) {
        char m[160];
        snprintf(m, sizeof m, "%s %s: not a number", name, s);
        usage(m);
    }
    return v;
}

std::string subst(const std::string& a, const std::string& k, const std::string& p) {
    std::string r;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a.compare(i, 3, "{k}") == 0) {
            r += k;
            i += 2;
        } else if (a.compare(i, 3, "{p}") == 0) {
            r += p;
            i += 2;
        } else {
            r += a[i];
        }
    }
    return r;
}

std::vector<std::string> split_words(const char* s) {
    std::vector<std::string> w;
    std::string cur;
    for (const char* c = s; *c; ++c) {
        if (*c == ' ') {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += *c;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

struct Child {
    pid_t pid = -1;
    bool exited = false;
    int status = 0;
    bool killed_by_us = false;
};

}  // namespace

int main(int argc, char** argv) {
    Params P;
    std::string link;
    uint64_t stall_s = 300, grace_s = 2;
    bool stall_given = false;
    int debug = -1;  // process index
    std::string debugger = "gdb --args";
    int i = 1;
    bool clients_given = false;
    for (; i < argc; ++i) {
        const char* a = argv[i];
        auto val = [&]() -> const char* {
            if (i + 1 >= argc) usage("an option needs a value");
            return argv[++i];
        };
        if (strcmp(a, "--") == 0) break;
        if (strcmp(a, "--clients") == 0) {
            P.clients = uint32_t(need_u64(a, val()));
            clients_given = true;
        } else if (strcmp(a, "--window") == 0) {
            P.window = uint32_t(need_u64(a, val()));
        } else if (strcmp(a, "--lane-bytes") == 0) {
            P.lane_bytes = uint32_t(need_u64(a, val()));
        } else if (strcmp(a, "--slots") == 0) {
            P.slots = uint32_t(need_u64(a, val()));
        } else if (strcmp(a, "--link") == 0) {
            link = val();
        } else if (strcmp(a, "--stall") == 0) {
            stall_s = need_u64(a, val());
            stall_given = true;
        } else if (strcmp(a, "--grace") == 0) {
            grace_s = need_u64(a, val());
        } else if (strcmp(a, "--debug") == 0) {
            const char* w = val();
            debug = strcmp(w, "driver") == 0 ? 0 : int(need_u64("--debug", w)) + 1;
        } else if (strcmp(a, "--debugger") == 0) {
            debugger = val();
        } else {
            char m[160];
            snprintf(m, sizeof m, "unknown option %s", a);
            usage(m);
        }
    }
    if (!clients_given) usage("--clients is needed");
    char err[256];
    if (!params_valid(P, err, sizeof err)) usage(err);
    // The two commands.
    std::vector<std::string> drv, cli;
    if (i >= argc) usage("no driver command");
    for (++i; i < argc && strcmp(argv[i], "--") != 0; ++i) drv.push_back(argv[i]);
    if (i >= argc) usage("no client command");
    for (++i; i < argc; ++i) cli.push_back(argv[i]);
    if (drv.empty() || cli.empty()) usage("the driver and the client commands must not be empty");
    if (debug > int(P.clients)) usage("--debug: no such process");
    if (debug >= 0 && !stall_given) stall_s = 0;
    if (link.empty()) link = "run" + std::to_string(getpid());
    if (link.size() >= 64) usage("--link: at most 63 characters");

    // The region.
    const size_t size = region_size(P);
    const std::string mname = "meshsim-wire:" + link;
    const int fd = memfd_create(mname.c_str(), 0);  // not close-on-exec: the children inherit it
    if (fd < 0 || ftruncate(fd, off_t(size)) != 0) {
        fprintf(stderr, "wire-launch: cannot make the region: %s\n", strerror(errno));
        return 2;
    }
    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "wire-launch: cannot map the region: %s\n", strerror(errno));
        return 2;
    }
    uint64_t run_id = now_ns() ^ (uint64_t(getpid()) << 32);
    region_init(mem, P, link.c_str(), int32_t(getpid()), run_id);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    if (debug >= 0)
        signal(SIGINT, SIG_IGN);
    else
        sigaction(SIGINT, &sa, nullptr);

    const int N = int(P.clients);
    std::vector<Child> ch(size_t(N) + 1);
    const pid_t me = getpid();
    const uint64_t t_start = now_ns();
    for (int p = 0; p <= N; ++p) {
        const std::string k = p == 0 ? std::string("driver") : std::to_string(p - 1);
        std::vector<std::string> args;
        if (p == debug) args = split_words(debugger.c_str());
        for (const std::string& a : p == 0 ? drv : cli) args.push_back(subst(a, k, std::to_string(p)));
        args.push_back("--clients");
        args.push_back(std::to_string(N));
        if (p == 0) {
            args.push_back("--driver");
        } else {
            args.push_back("--client");
            args.push_back(k);
        }
        args.push_back("--link");
        args.push_back(link);
        args.push_back("--wire-fd");
        args.push_back(std::to_string(fd));
        std::vector<char*> av;
        for (std::string& a : args) av.push_back(a.data());
        av.push_back(nullptr);

        fflush(nullptr);
        const pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "wire-launch: fork: %s\n", strerror(errno));
            region_abort(mem, kAbortLauncher, -1, 0);
            break;
        }
        if (pid == 0) {
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != me) _exit(127);  // the launcher died before prctl
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            signal(SIGHUP, SIG_DFL);
            if (debug >= 0 && p != debug) {
                setpgid(0, 0);
                const int nul = open("/dev/null", O_RDONLY);
                if (nul >= 0) dup2(nul, 0);
            }
            execvp(av[0], av.data());
            fprintf(stderr, "wire-launch: cannot start %s: %s\n", av[0], strerror(errno));
            _exit(127);
        }
        ch[size_t(p)].pid = pid;
    }

    // Wait, watch, stop.
    int alive = 0;
    for (const Child& c : ch)
        if (c.pid > 0) ++alive;
    uint64_t last_sum = ~0ull, last_change = now_ns();
    uint64_t term_at = 0, kill_at = 0;
    bool term_sent = false, kill_sent = false;
    auto aborted = [&] { return region_abort_line(mem)->reason.load(std::memory_order_acquire) != kAbortNone; };
    auto arm = [&] {
        if (term_at == 0) term_at = now_ns() + grace_s * 1000000000ull;
    };
    if (alive < N + 1) arm();
    while (alive > 0) {
        int st = 0;
        const pid_t pid = waitpid(-1, &st, WNOHANG);
        if (pid > 0) {
            for (int p = 0; p <= N; ++p) {
                Child& c = ch[size_t(p)];
                if (c.pid != pid) continue;
                c.exited = true;
                c.status = st;
                --alive;
                const uint32_t state = region_proc(mem, p)->state.load(std::memory_order_acquire);
                const bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0 && state == kStateFinished;
                if (!ok && !c.killed_by_us) {
                    if (WIFSIGNALED(st))
                        region_abort(mem, kAbortDied, p, WTERMSIG(st));
                    else if (WEXITSTATUS(st) != 0)
                        region_abort(mem, kAbortFailed, p, WEXITSTATUS(st));
                    else
                        region_abort(mem, kAbortUnfinished, p, 0);
                    arm();
                }
            }
            continue;  // reap everything that is ready before sleeping
        }
        if (g_signal && !aborted()) {
            region_abort(mem, kAbortLauncher, -1, g_signal);
            arm();
            grace_s = 1;
        }
        // The watchdog: no process has moved for stall_s seconds.
        if (stall_s > 0 && !aborted()) {
            uint64_t sum = 0;
            for (int p = 0; p <= N; ++p) sum += region_proc(mem, p)->windows.load(std::memory_order_relaxed);
            const uint64_t t = now_ns();
            if (sum != last_sum) {
                last_sum = sum;
                last_change = t;
            } else if (t - last_change > stall_s * 1000000000ull) {
                // The culprit: the slowest process still running; among equals, one that is not waiting.
                int who = -1;
                uint64_t best = ~0ull;
                bool best_waits = true;
                for (int p = 0; p <= N; ++p) {
                    if (ch[size_t(p)].exited) continue;
                    ProcLine* pl = region_proc(mem, p);
                    if (pl->state.load(std::memory_order_acquire) == kStateFinished) continue;
                    const uint64_t w = pl->windows.load(std::memory_order_relaxed);
                    const bool waits = pl->waiting_on.load(std::memory_order_relaxed) >= 0;
                    if (w < best || (w == best && best_waits && !waits)) {
                        who = p;
                        best = w;
                        best_waits = waits;
                    }
                }
                region_abort(mem, kAbortStalled, who, int32_t(best));
                arm();
            }
        }
        const uint64_t t = now_ns();
        if (term_at && !term_sent && t >= term_at) {
            for (Child& c : ch)
                if (c.pid > 0 && !c.exited) {
                    c.killed_by_us = true;
                    kill(c.pid, SIGTERM);  // by PID: our own unreaped children
                    kill(c.pid, SIGCONT);
                }
            term_sent = true;
            kill_at = t + 1000000000ull;
        }
        if (term_sent && !kill_sent && t >= kill_at) {
            for (Child& c : ch)
                if (c.pid > 0 && !c.exited) kill(c.pid, SIGKILL);
            kill_sent = true;
        }
        sleep_ms(10);
    }
    const double secs = double(now_ns() - t_start) / 1e9;

    // The report.
    bool all_ok = true;
    for (int p = 0; p <= N; ++p) {
        const Child& c = ch[size_t(p)];
        char name[32];
        process_name(p, name, sizeof name);
        const uint32_t state = region_proc(mem, p)->state.load(std::memory_order_acquire);
        char what[128];
        if (c.pid <= 0) {
            snprintf(what, sizeof what, "not started");
            all_ok = false;
        } else if (WIFSIGNALED(c.status)) {
            snprintf(what, sizeof what, "signal %d%s", WTERMSIG(c.status),
                     c.killed_by_us ? " (stopped by the launcher)" : "");
            all_ok = false;
        } else {
            const int code = WEXITSTATUS(c.status);
            snprintf(what, sizeof what, "exit %d%s%s", code, code == 3 ? " (gave up)" : "",
                     code == 0 && state != kStateFinished ? " (without finishing)" : "");
            if (code != 0 || state != kStateFinished) all_ok = false;
        }
        fprintf(stderr, "wire-launch: %-10s pid %-7d %s\n", name, int(c.pid), what);
    }
    if (aborted()) {
        char why[200];
        abort_text(mem, why, sizeof why);
        fprintf(stderr, "wire-launch: run %s failed after %.2f s: %s\n", link.c_str(), secs, why);
        all_ok = false;
    } else {
        fprintf(stderr, "wire-launch: run %s %s after %.2f s\n", link.c_str(), all_ok ? "ok" : "failed", secs);
    }
    munmap(mem, size);
    close(fd);
    return all_ok ? 0 : 1;
}
