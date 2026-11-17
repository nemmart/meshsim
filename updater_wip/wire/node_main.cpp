// wire-node: a stand-in end as a process on the wire, started by wire-launch.
//
//   wire-node [stand-in options] --clients N (--driver | --client k) --link NAME --wire-fd FD
//   wire-node --no-wire --nw-window W --nw-bytes B [stand-in options]    (the cost with no wire)
//
// Stand-in options: --seed S --cycles C --no-stagger --null --work-ns NS --pause PPM:MAX_US --hang-at T
//                   --log DIR --out FILE
#include <cstdio>
#include <cstring>

#include "node_loop.h"
#include "opts.h"
#include "wire.h"
#include "wire_args.h"

using namespace wire;

int main(int argc, char** argv) {
    WireArgs wa;
    char err[256];
    if (!wire_parse_args(&argc, argv, &wa, err, sizeof err)) {
        fprintf(stderr, "wire-node: %s\n", err);
        return 2;
    }
    NodeCfg c;
    const char* out_path = nullptr;
    bool no_wire = false;
    uint64_t nw_window = 64, nw_bytes = 1;
    for (int i = 1; i < argc; ++i) {
        const char* v;
        if (node_opt(argc, argv, &i, &c, &out_path)) continue;
        if (strcmp(argv[i], "--no-wire") == 0) {
            no_wire = true;
        } else if ((v = opt_value(argc, argv, &i, "--nw-window"))) {
            nw_window = opt_need_u64("--nw-window", v);
        } else if ((v = opt_value(argc, argv, &i, "--nw-bytes"))) {
            nw_bytes = opt_need_u64("--nw-bytes", v);
        } else {
            fprintf(stderr, "wire-node: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    FILE* out = open_out(out_path);
    if (no_wire) {
        if (nw_window < 1 || nw_bytes < 1 || nw_bytes > 8) {
            fprintf(stderr, "wire-node: --nw-window >= 1, --nw-bytes 1 to 8\n");
            return 2;
        }
        c.s.process = 1;  // a node with both ends
        c.s.clients = 2;
        c.s.lane_bytes = uint32_t(nw_bytes);
        const int r = run_node(nullptr, c, uint32_t(nw_window), out);
        if (out != stdout) fclose(out);
        return r;
    }
    if (!wa.present) {
        fprintf(stderr, "wire-node: no wire arguments: start it with wire-launch (or give --no-wire)\n");
        return 2;
    }
    Wire w;
    if (!wire_attach(&w, wa, err, sizeof err)) {
        fprintf(stderr, "wire-node: %s\n", err);
        return 2;
    }
    if (w.lane_bytes() > 8) {
        fprintf(stderr, "wire-node: the stand-in sends at most 8 bytes a cycle\n");
        return 2;
    }
    c.s.process = w.process();
    c.s.clients = int(w.clients());
    c.s.lane_bytes = w.lane_bytes();
    const int r = run_node(&w, c, w.window(), out);
    if (out != stdout) fclose(out);
    return r;
}
