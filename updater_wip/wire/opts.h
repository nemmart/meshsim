// The test programs' own options (not the wire's: those are wire_args.h).
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "node_loop.h"

namespace wire {

inline bool opt_u64(const char* s, uint64_t* v) {
    if (!s || !*s) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long x = strtoull(s, &end, 0);
    if (errno || *end) return false;
    *v = x;
    return true;
}

// Takes "--name value" at argv[*i]; returns the value or null.
inline const char* opt_value(int argc, char** argv, int* i, const char* name) {
    if (strcmp(argv[*i], name) != 0) return nullptr;
    if (*i + 1 >= argc) {
        fprintf(stderr, "%s needs a value\n", name);
        exit(2);
    }
    return argv[++*i];
}

inline uint64_t opt_need_u64(const char* name, const char* s) {
    uint64_t v = 0;
    if (!opt_u64(s, &v)) {
        fprintf(stderr, "%s %s: not a number\n", name, s);
        exit(2);
    }
    return v;
}

// The stand-in's options, shared by wire-node, wire-ref and wire-threads. Returns true if argv[*i] was one.
inline bool node_opt(int argc, char** argv, int* i, NodeCfg* c, const char** out_path) {
    const char* v;
    if ((v = opt_value(argc, argv, i, "--seed"))) {
        c->s.seed = opt_need_u64("--seed", v);
    } else if ((v = opt_value(argc, argv, i, "--cycles"))) {
        c->s.cycles = opt_need_u64("--cycles", v);
    } else if (strcmp(argv[*i], "--no-stagger") == 0) {
        c->s.stagger = false;
    } else if (strcmp(argv[*i], "--wide") == 0) {
        c->s.wide = true;
    } else if (strcmp(argv[*i], "--null") == 0) {
        c->null_traffic = true;
        c->s.stagger = false;
    } else if ((v = opt_value(argc, argv, i, "--work-ns"))) {
        c->work_ns = opt_need_u64("--work-ns", v);
    } else if ((v = opt_value(argc, argv, i, "--pause"))) {
        unsigned a = 0, b = 0;
        if (sscanf(v, "%u:%u", &a, &b) != 2) {
            fprintf(stderr, "--pause PPM:MAX_US\n");
            exit(2);
        }
        c->pause_ppm = a;
        c->pause_max_us = b;
    } else if ((v = opt_value(argc, argv, i, "--hang-at"))) {
        c->hang_at = opt_need_u64("--hang-at", v);
    } else if ((v = opt_value(argc, argv, i, "--exit-at"))) {
        unsigned long long t = 0;
        int code = 0;
        if (sscanf(v, "%llu:%d", &t, &code) != 2) {
            fprintf(stderr, "--exit-at CYCLE:STATUS\n");
            exit(2);
        }
        c->exit_at = t;
        c->exit_code = code;
    } else if ((v = opt_value(argc, argv, i, "--log"))) {
        c->log_dir = v;
    } else if ((v = opt_value(argc, argv, i, "--out"))) {
        *out_path = v;
    } else {
        return false;
    }
    return true;
}

inline FILE* open_out(const char* path) {
    if (!path) return stdout;
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        exit(2);
    }
    return f;
}

}  // namespace wire
