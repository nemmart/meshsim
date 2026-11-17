#include "node_loop.h"

#include <time.h>
#include <unistd.h>

#include <cstring>
#include <vector>

namespace wire {

namespace {

uint64_t now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

void busy(uint64_t ns) {
    const uint64_t t0 = now_ns();
    while (now_ns() - t0 < ns) {
    }
}

// The cost's baseline: an end that loops back to itself after `window` cycles, with no other process.
struct Loopback {
    std::vector<uint8_t> ring;
    uint32_t B, W, pos = 0;
    Loopback(uint32_t w, uint32_t b) : ring(size_t(w) * b, 0), B(b), W(w) {}
    void exchange(const uint8_t* out, uint8_t* in) {
        uint8_t* s = ring.data() + size_t(pos) * B;
        for (uint32_t i = 0; i < B; ++i) {
            in[i] = s[i];
            s[i] = out[i];
        }
        pos = pos + 1 == W ? 0 : pos + 1;
    }
};

struct End {
    LinkEnd* le = nullptr;
    Loopback* lb = nullptr;
    void exchange(const uint8_t* out, uint8_t* in) {
        if (le)
            le->exchange(out, in);
        else
            lb->exchange(out, in);
    }
};

}  // namespace

int run_node(Wire* w, const NodeCfg& c, uint32_t window, FILE* out) {
    const uint32_t B = c.s.lane_bytes;
    const int p = c.s.process;
    StandIn s(c.s);
    const uint64_t T = c.null_traffic ? c.s.cycles : s.finish_at();

    Loopback lb_up(window, B), lb_dn(window, B);
    End up, dn;
    const bool has_up = p > 0, has_dn = p < c.s.clients;
    if (w) {
        up.le = w->up();
        dn.le = w->down();
    } else {
        up.lb = &lb_up;
        dn.lb = &lb_dn;
    }

    FILE* log = nullptr;
    if (c.log_dir) {
        char path[512];
        snprintf(path, sizeof path, "%s/p%d.bin", c.log_dir, p);
        log = fopen(path, "wb");
        if (!log) {
            fprintf(stderr, "wire-node: cannot write %s\n", path);
            return 1;
        }
    }

    uint64_t prng = now_ns() ^ (uint64_t(getpid()) << 20);  // pauses: deliberately not reproducible
    uint8_t ou[8] = {}, od[8] = {}, iu[8] = {}, id[8] = {};
    uint64_t sink = 0;
    const uint64_t t0 = now_ns();
    for (uint64_t t = 0; t < T; ++t) {
        if (t == c.hang_at)
            for (;;) busy(1000000);
        if (t == c.exit_at) {
            fflush(nullptr);
            _exit(c.exit_code);
        }
        if (c.pause_ppm) {
            prng = mix64(prng + 0x9e3779b97f4a7c15ull);
            if (prng % 1000000u < c.pause_ppm) {
                timespec ts{0, long((prng >> 24) % (c.pause_max_us ? c.pause_max_us : 1)) * 1000};
                nanosleep(&ts, nullptr);
            }
        }
        if (c.null_traffic) {
            if (has_up) up.exchange(ou, iu);
            if (has_dn) dn.exchange(od, id);
            sink += iu[0] ^ id[0];
        } else {
            s.outputs(t, ou, od);
            if (has_up) up.exchange(ou, iu);
            if (has_dn) dn.exchange(od, id);
            s.inputs(t, iu, id);
            s.checkpoint(t, out);
            if (log) {
                fwrite(iu, 1, B, log);
                fwrite(id, 1, B, log);
                fwrite(ou, 1, B, log);
                fwrite(od, 1, B, log);
            }
        }
        if (c.work_ns) busy(c.work_ns);
    }
    if (w) w->finish();
    const double secs = double(now_ns() - t0) / 1e9;
    if (!c.null_traffic) s.report(out);
    fprintf(out, "# p %d cycles %llu secs %.3f mcps %.3f sink %llu\n", p, (unsigned long long)T, secs,
            secs > 0 ? double(T) / secs / 1e6 : 0.0, (unsigned long long)(sink & 1));
    if (log) fclose(log);
    return 0;
}

}  // namespace wire
