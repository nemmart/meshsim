// wire-threads: the same library, with each process a thread of one process and the region in its memory, so
// that the thread sanitizer can see every access between the ends (it does not see across processes).
//
//   wire-threads --clients N --window W --lane-bytes B [--slots K] [stand-in options] [--out FILE]
//
// Writes the same digest lines as the processes do, for comparison with wire-ref.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "node_loop.h"
#include "opts.h"
#include "wire.h"

using namespace wire;

int main(int argc, char** argv) {
    NodeCfg c;
    const char* out_path = nullptr;
    Params P;
    for (int i = 1; i < argc; ++i) {
        const char* v;
        if (node_opt(argc, argv, &i, &c, &out_path)) continue;
        if ((v = opt_value(argc, argv, &i, "--clients")))
            P.clients = uint32_t(opt_need_u64("--clients", v));
        else if ((v = opt_value(argc, argv, &i, "--window")))
            P.window = uint32_t(opt_need_u64("--window", v));
        else if ((v = opt_value(argc, argv, &i, "--lane-bytes")))
            P.lane_bytes = uint32_t(opt_need_u64("--lane-bytes", v));
        else if ((v = opt_value(argc, argv, &i, "--slots")))
            P.slots = uint32_t(opt_need_u64("--slots", v));
        else {
            fprintf(stderr, "wire-threads: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    char err[256];
    if (!params_valid(P, err, sizeof err) || P.lane_bytes > 8) {
        fprintf(stderr, "wire-threads: %s\n", P.lane_bytes > 8 ? "--lane-bytes 1 to 8" : err);
        return 2;
    }
    if (c.log_dir) {
        fprintf(stderr, "wire-threads: --log is for wire-node\n");
        return 2;
    }
    const size_t size = region_size(P);
    void* mem = aligned_alloc(kLine, (size + kLine - 1) / kLine * kLine);
    region_init(mem, P, "threads", 0, 1);

    const int N = int(P.clients);
    std::vector<std::string> outs(size_t(N) + 1);
    std::vector<int> rc(size_t(N) + 1, 0);
    std::vector<std::thread> th;
    for (int p = 0; p <= N; ++p) {
        th.emplace_back([&, p] {
            Wire w;
            char e[256];
            if (!w.attach_memory(mem, size, p, e, sizeof e)) {
                fprintf(stderr, "wire-threads: %s\n", e);
                rc[size_t(p)] = 2;
                return;
            }
            NodeCfg nc = c;
            nc.s.process = p;
            nc.s.clients = N;
            nc.s.lane_bytes = P.lane_bytes;
            char* buf = nullptr;
            size_t len = 0;
            FILE* f = open_memstream(&buf, &len);
            rc[size_t(p)] = run_node(&w, nc, P.window, f);
            fclose(f);
            outs[size_t(p)].assign(buf, len);
            free(buf);
        });
    }
    for (std::thread& t : th) t.join();
    FILE* out = open_out(out_path);
    int r = 0;
    for (int p = 0; p <= N; ++p) {
        fputs(outs[size_t(p)].c_str(), out);
        if (rc[size_t(p)]) r = 1;
    }
    if (out != stdout) fclose(out);
    free(mem);
    return r;
}
