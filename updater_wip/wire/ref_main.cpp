// wire-ref: the reference. The same stand-in ends, all stepped by one loop, joined by plain delay lines of
// W cycles. It shares the stand-in with the processes, and nothing of the wire.
//
//   wire-ref --clients N --window W --lane-bytes B [stand-in options: --seed --cycles --no-stagger --log --out]
//
// A process that finishes at cycle T sends zeros from T on; that is the whole of the finishing rule.
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "opts.h"
#include "standin.h"

using namespace wire;

int main(int argc, char** argv) {
    NodeCfg c;
    const char* out_path = nullptr;
    uint64_t clients = 1, window = 64, bytes = 1;
    for (int i = 1; i < argc; ++i) {
        const char* v;
        if (node_opt(argc, argv, &i, &c, &out_path)) continue;
        if ((v = opt_value(argc, argv, &i, "--clients")))
            clients = opt_need_u64("--clients", v);
        else if ((v = opt_value(argc, argv, &i, "--window")))
            window = opt_need_u64("--window", v);
        else if ((v = opt_value(argc, argv, &i, "--lane-bytes")))
            bytes = opt_need_u64("--lane-bytes", v);
        else {
            fprintf(stderr, "wire-ref: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (clients < 1 || clients > 1024 || window < 1 || bytes < 1 || bytes > 8 || c.null_traffic) {
        fprintf(stderr, "wire-ref: --clients 1 to 1024, --window >= 1, --lane-bytes 1 to 8\n");
        return 2;
    }
    const int N = int(clients);
    const uint32_t W = uint32_t(window), B = uint32_t(bytes);
    FILE* out = open_out(out_path);

    std::vector<std::unique_ptr<StandIn>> node;
    std::vector<uint64_t> fin(size_t(N) + 1);
    std::vector<FILE*> log(size_t(N) + 1, nullptr);
    uint64_t last = 0;
    for (int p = 0; p <= N; ++p) {
        StandInCfg s = c.s;
        s.process = p;
        s.clients = N;
        s.lane_bytes = B;
        node.emplace_back(new StandIn(s));
        fin[size_t(p)] = node.back()->finish_at();
        if (fin[size_t(p)] > last) last = fin[size_t(p)];
        if (c.log_dir) {
            char path[512];
            snprintf(path, sizeof path, "%s/p%d.bin", c.log_dir, p);
            log[size_t(p)] = fopen(path, "wb");
            if (!log[size_t(p)]) {
                fprintf(stderr, "wire-ref: cannot write %s\n", path);
                return 2;
            }
        }
    }
    // line[2 * i] is link i's down direction (process i to i + 1), line[2 * i + 1] its up direction.
    std::vector<std::vector<uint8_t>> line(size_t(2 * N), std::vector<uint8_t>(size_t(W) * B, 0));
    // Per process: in_up, in_dn, out_up, out_dn.
    std::vector<uint8_t> io(size_t(N + 1) * 4 * 8, 0);
    auto at = [&](int p, int k) { return io.data() + (size_t(p) * 4 + size_t(k)) * 8; };

    for (uint64_t t = 0; t < last; ++t) {
        for (int p = 0; p <= N; ++p) {
            if (t < fin[size_t(p)]) {
                node[size_t(p)]->outputs(t, at(p, 2), at(p, 3));
            } else {
                memset(at(p, 2), 0, B);
                memset(at(p, 3), 0, B);
            }
        }
        const size_t off = size_t(t % W) * B;
        for (int i = 0; i < N; ++i) {
            uint8_t* dn = line[size_t(2 * i)].data() + off;      // i -> i + 1
            uint8_t* up = line[size_t(2 * i + 1)].data() + off;  // i + 1 -> i
            for (uint32_t b = 0; b < B; ++b) {
                at(i + 1, 0)[b] = dn[b];  // sent W cycles ago
                dn[b] = at(i, 3)[b];
                at(i, 1)[b] = up[b];
                up[b] = at(i + 1, 2)[b];
            }
        }
        for (int p = 0; p <= N; ++p) {
            if (t >= fin[size_t(p)]) continue;
            node[size_t(p)]->inputs(t, at(p, 0), at(p, 1));
            node[size_t(p)]->checkpoint(t, out);
            if (FILE* f = log[size_t(p)]) {
                for (int k = 0; k < 4; ++k) fwrite(at(p, k), 1, B, f);
            }
        }
    }
    for (int p = 0; p <= N; ++p) {
        node[size_t(p)]->report(out);
        if (log[size_t(p)]) fclose(log[size_t(p)]);
    }
    if (out != stdout) fclose(out);
    return 0;
}
