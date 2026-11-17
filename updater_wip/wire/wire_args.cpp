#include "wire_args.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wire.h"

namespace wire {

namespace {

bool parse_int(const char* s, long lo, long hi, long* out) {
    if (!s || !*s) return false;
    errno = 0;
    char* end = nullptr;
    const long v = strtol(s, &end, 10);
    if (errno || *end || v < lo || v > hi) return false;
    *out = v;
    return true;
}

}  // namespace

bool wire_parse_args(int* argc, char** argv, WireArgs* a, char* err, size_t n) {
    *a = WireArgs{};
    int out = 1;
    for (int i = 1; i < *argc; ++i) {
        const char* arg = argv[i];
        static const char* const kNames[] = {"--clients", "--client", "--link", "--wire-fd", "--window",
                                             "--lane-bytes"};
        const char* name = nullptr;
        const char* val = nullptr;
        bool takes_next = false;
        if (strcmp(arg, "--driver") == 0) {
            a->driver = true;
            a->present = true;
            continue;
        }
        for (const char* k : kNames) {
            const size_t len = strlen(k);
            if (strncmp(arg, k, len) == 0 && (arg[len] == 0 || arg[len] == '=')) {
                name = k;
                if (arg[len] == '=')
                    val = arg + len + 1;
                else
                    takes_next = true;
                break;
            }
        }
        if (!name) {
            argv[out++] = argv[i];
            continue;
        }
        if (takes_next) {
            if (i + 1 >= *argc) {
                snprintf(err, n, "%s needs a value", name);
                return false;
            }
            val = argv[++i];
        }
        a->present = true;
        long v = 0;
        if (strcmp(name, "--clients") == 0) {
            if (!parse_int(val, 1, kMaxClients, &v)) {
                snprintf(err, n, "--clients %s: must be 1 to %u", val, kMaxClients);
                return false;
            }
            a->clients = int(v);
        } else if (strcmp(name, "--client") == 0) {
            if (!parse_int(val, 0, kMaxClients - 1, &v)) {
                snprintf(err, n, "--client %s: must be a number from 0", val);
                return false;
            }
            a->client = int(v);
        } else if (strcmp(name, "--link") == 0) {
            if (!*val || strlen(val) >= 64) {
                snprintf(err, n, "--link: a name of 1 to 63 characters");
                return false;
            }
            a->link = val;
        } else if (strcmp(name, "--wire-fd") == 0) {
            if (!parse_int(val, 0, 1 << 20, &v)) {
                snprintf(err, n, "--wire-fd %s: not a file descriptor", val);
                return false;
            }
            a->fd = int(v);
        } else if (strcmp(name, "--window") == 0) {
            if (!parse_int(val, 1, kMaxWindow, &v)) {
                snprintf(err, n, "--window %s: must be 1 to %u", val, kMaxWindow);
                return false;
            }
            a->window = uint32_t(v);
        } else {
            if (!parse_int(val, 1, kMaxLaneBytes, &v)) {
                snprintf(err, n, "--lane-bytes %s: must be 1 to %u", val, kMaxLaneBytes);
                return false;
            }
            a->lane_bytes = uint32_t(v);
        }
    }
    argv[out] = nullptr;
    *argc = out;
    if (!a->present) return true;
    if (a->clients < 0) {
        snprintf(err, n, "--clients is needed");
        return false;
    }
    if (a->driver && a->client >= 0) {
        snprintf(err, n, "--driver and --client together");
        return false;
    }
    if (!a->driver && a->client < 0) {
        snprintf(err, n, "--client k or --driver is needed");
        return false;
    }
    if (!a->driver && a->client >= a->clients) {
        snprintf(err, n, "--client %d on a chain of %d clients", a->client, a->clients);
        return false;
    }
    return true;
}

bool wire_attach(Wire* w, const WireArgs& a, char* err, size_t n) {
    return w->attach_fd(a.fd, a.link, a.clients, a.process(), a.window, a.lane_bytes, err, n);
}

}  // namespace wire
