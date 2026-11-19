// trace_log.h: the retire trace (sim/tests/TRACE-FORMAT.md) as fixed-size records, written in the cycle loop and formatted
// after the run. The log is permanent state allocated at configuration with a fixed capacity; a record past it is dropped
// and the log marked full (the harness reports that as a difference), it never grows (D6).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace units {

enum TraceKind : uint8_t {
    T_MEMW,      // w: addr, value                                 (replayed)
    T_GO,        //                                                (replayed)
    T_CMD,       //                                                (replayed)
    T_WAKE,      // flag: 1 go, 2 event
    T_REFUSED,   // REQ-REFUSED
    T_TRAP,      // w: mcause, mepc, mtval, pc
    T_RETIRE,    // w: pc, insn, rs1 as read, rs2 as read, rd's value; flag: rd written
    T_HALT,      // w: reason, pc
    T_UB,        // w: address, pc, width; flag: 0 load, 1 store
    T_FREED,     // REQ-FREED                                      (replayed)
};
// The order of a cycle's lines (TRACE-FORMAT: MEMW, GO, CMD, then the core's, then HALT, then REQ-FREED).
inline unsigned trace_rank(uint8_t k) {
    static constexpr uint8_t r[] = {0, 1, 2, 3, 3, 3, 3, 4, 4, 5};
    return r[k];
}

struct TraceRec {
    uint64_t cycle;
    uint8_t kind, flag;
    uint32_t w[5];
};

struct TraceLog {
    TraceRec* rec = nullptr;
    uint32_t n = 0, cap = 0;
    bool full = false;

    void add(uint64_t cycle, uint8_t kind, uint8_t flag = 0, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0,
             uint32_t d = 0, uint32_t e = 0) {
        if (n == cap) { full = true; return; }
        TraceRec& r = rec[n++];
        r.cycle = cycle; r.kind = kind; r.flag = flag;
        r.w[0] = a; r.w[1] = b; r.w[2] = c; r.w[3] = d; r.w[4] = e;
    }
};

// Configuration regime: own the storage, and format.
struct TraceBuffer {
    std::vector<TraceRec> storage;
    TraceLog log;
    explicit TraceBuffer(uint32_t capacity) : storage(capacity) { log.rec = storage.data(); log.cap = capacity; }
};
// The body's lines, in file order (stable by cycle, then rank).
std::vector<std::string> format_body(const TraceLog& log);
std::string format_line(const TraceRec& r);

}  // namespace units
