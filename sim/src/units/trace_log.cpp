// trace_log.cpp: formatting the records as TRACE-FORMAT's lines (configuration regime: after the run).
#include "units/trace_log.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace units {

static std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char* f, ...) {
    char b[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

std::string format_line(const TraceRec& r) {
    unsigned long long c = (unsigned long long)r.cycle;
    const uint32_t* w = r.w;
    switch (r.kind) {
    case T_MEMW: return fmt("%llu MEMW %08x %08x", c, w[0], w[1]);
    case T_GO: return fmt("%llu GO", c);
    case T_CMD: return fmt("%llu CMD", c);
    case T_WAKE: return fmt("%llu WAKE %s", c, r.flag == 1 ? "go" : "event");
    case T_REFUSED: return fmt("%llu REQ-REFUSED", c);
    case T_TRAP: return fmt("%llu TRAP mcause=%08x mepc=%08x mtval=%08x pc=%08x", c, w[0], w[1], w[2], w[3]);
    case T_HALT: return fmt("%llu HALT %u pc=%08x", c, w[0], w[1]);
    case T_UB:
        return fmt("%llu UB controller: a misaligned %u-byte %s 0x%x at pc 0x%x", c, w[2], r.flag ? "store to" : "load from",
                   w[0], w[1]);
    case T_FREED: return fmt("%llu REQ-FREED", c);
    case T_RETIRE: {
        // As Observer.retire: rd's value if written; a load's or store's address, size and bytes (zero-extended).
        uint32_t pc = w[0], ins = w[1], a = w[2], b = w[3], res = w[4];
        uint32_t op = ins & 0x7F, rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7;
        int32_t s = int32_t(ins);
        std::string l = fmt("%llu %08x %08x", c, pc, ins);
        if (r.flag) l += fmt(" x%u=%08x", rd, res);
        if (op == 0x03 || op == 0x23) {
            uint32_t size = 1u << (f3 & 3);
            uint32_t ea = op == 0x03 ? a + uint32_t(s >> 20) : a + (uint32_t(s >> 25) << 5 | rd);
            uint32_t v = op == 0x03 ? res : b;
            if (size != 4) v &= (1u << 8 * size) - 1;
            l += fmt(" %s %08x %u %08x", op == 0x03 ? "ld" : "st", ea, size, v);
        }
        return l;
    }
    default: return fmt("%llu ??? kind %u", c, r.kind);
    }
}

std::vector<std::string> format_body(const TraceLog& log) {
    std::vector<const TraceRec*> p;
    p.reserve(log.n);
    for (uint32_t i = 0; i < log.n; i++) p.push_back(&log.rec[i]);
    std::stable_sort(p.begin(), p.end(), [](const TraceRec* x, const TraceRec* y) {
        if (x->cycle != y->cycle) return x->cycle < y->cycle;
        return trace_rank(x->kind) < trace_rank(y->kind);
    });
    std::vector<std::string> out;
    out.reserve(p.size());
    for (const TraceRec* r : p) out.push_back(format_line(*r));
    return out;
}

}  // namespace units
