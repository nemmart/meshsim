// test_regs: the register table behind mesh.h (tools/regs/regtable.h) against the committed files, against the
// simulator's own decode table (src/units/regmap.h) and against the specification, so header, document, simulator and
// the RTL's firmware can't drift. Run from v1.36/ (the files are read by path).
//   (a) fw/include/mesh.h and fw/REGISTERS.md are the generator's output byte for byte (tools/regs/gen_regs rewrites
//       them);
//   (b) the table and regmap.h agree: every window word's name and access, every Reg and PeripheralReg, the address
//       map, the event, status and error bits, every field's position;
//   (c) the table, and the macros of the generated mesh.h read back from its text, hold the specification's values:
//       the offsets and accesses of 5.5, the bits of 7.1, 7.2 and 7.4, the fields of 4.1, 5.4, 7.1, 8-13, 11.5 and
//       17.1, and the peripherals' words and bits of 17.6-17.9, all written out below from the specification's tables
//       (spec/machine-spec.md, and the RTL's base tile specification), never taken from the generator. The RTL firmware's
//       own definitions of the registers they share (the tile programs' fw/tile/tile_map.h and, in fw/rtl/src, the chip
//       firmware's src/lib.rs) are read too and must equal them; and mesh.h must compile, its structs' members at the
//       specification's offsets (the host C compiler, $CC or cc).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "../tools/regs/regs_gen.h"
#include "harness.h"
#include "units/regmap.h"

namespace {

std::string slurp(const char* path) {
    std::string s;
    FILE* f = fopen(path, "rb");
    if (!f) return "<cannot read>";
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

// ================================================================ the specification's tables, written out
struct SpecReg { const char* name; unsigned offset; char access; };   // 'R', 'W' or 'B' (R/W)
// 5.5: the tile window
const SpecReg SPEC_TILE[] = {
    {"PARAMS", 0x000, 'R'}, {"STATUS", 0x004, 'R'}, {"ERRORS", 0x008, 'R'}, {"EVENTS", 0x00C, 'R'},
    {"EVENTS_PEEK", 0x010, 'R'}, {"EVENTS_MASK", 0x014, 'B'}, {"ARRIVE", 0x018, 'W'},
    {"DMA_CONFIG", 0x040, 'B'}, {"DMA_START", 0x044, 'W'},
    {"WRITE_HDR", 0x080, 'B'}, {"WRITE_DEST_ADDR", 0x084, 'B'}, {"WRITE_W0", 0x088, 'W'}, {"WRITE_W1", 0x08C, 'W'},
    {"WRITE_W2", 0x090, 'W'}, {"WRITE_SEND_32", 0x094, 'W'}, {"WRITE_SEND_64", 0x098, 'W'}, {"WRITE_SEND_128", 0x09C, 'W'},
    {"READ_HDR", 0x0C0, 'B'}, {"READ_DEST_ADDR", 0x0C4, 'B'}, {"READ_SEND", 0x0C8, 'W'}, {"READ_FREE", 0x0CC, 'R'},
    {"REQUEST_OUT_W0", 0x100, 'W'}, {"REQUEST_OUT_W1", 0x104, 'W'}, {"REQUEST_OUT_W2", 0x108, 'W'}, {"REQUEST_SEND", 0x10C, 'W'},
    {"REQUEST_IN_W0", 0x110, 'R'}, {"REQUEST_IN_W1", 0x114, 'R'}, {"REQUEST_IN_W2", 0x118, 'R'}, {"REQUEST_RECEIVE", 0x11C, 'R'},
    {"RECEIVE_CONFIG", 0x140, 'B'}, {"COUNTER_1", 0x144, 'B'}, {"COUNTER_2", 0x148, 'B'}, {"COUNTER_3", 0x14C, 'B'},
    {"COUNTER_4", 0x150, 'B'}, {"COUNTER_5", 0x154, 'B'}, {"COUNTER_6", 0x158, 'B'}, {"COUNTER_7", 0x15C, 'B'},
    {"GATHER_CONFIG", 0x180, 'B'},
};
// 5.5, 17.1: a peripheral's control block and DMA block
const SpecReg SPEC_PERIPHERAL[] = {
    {"PARAMS", 0x000, 'R'}, {"STATUS", 0x004, 'R'}, {"ERRORS", 0x008, 'R'}, {"EVENTS", 0x00C, 'R'},
    {"EVENTS_PEEK", 0x010, 'R'}, {"EVENTS_MASK", 0x014, 'B'}, {"START", 0x018, 'W'},
    {"DMA_HDR", 0x040, 'B'}, {"DMA_DEST_ADDR", 0x044, 'B'}, {"DMA_START", 0x048, 'W'},
};
// 17.6: Ingress/Egress's words
const SpecReg SPEC_INGRESS_EGRESS[] = {
    {"ERROR_LINES_0", 0x080, 'R'}, {"ERROR_LINES_1", 0x084, 'R'}, {"ERROR_LINES_2", 0x088, 'R'},
    {"ARRIVALS_0", 0x08C, 'R'}, {"ARRIVALS_1", 0x090, 'R'}, {"ARRIVE_EXPECT_0", 0x094, 'B'}, {"ARRIVE_EXPECT_1", 0x098, 'B'},
    {"RELEASE", 0x09C, 'W'}, {"WATCHDOG", 0x0A0, 'B'}, {"LED", 0x0A4, 'B'}, {"UART_TX", 0x0A8, 'W'}, {"UART_RX", 0x0AC, 'R'},
    {"UART_DIVISOR", 0x0B0, 'B'}, {"UART_STATUS", 0x0B4, 'B'},
    {"STREAM_OUT_W0", 0x100, 'W'}, {"STREAM_OUT_W1", 0x104, 'W'}, {"STREAM_OUT_W2", 0x108, 'W'}, {"STREAM_SEND", 0x10C, 'W'},
    {"STREAM_SEND_LAST", 0x110, 'W'}, {"STREAM_IN_W0", 0x114, 'R'}, {"STREAM_IN_W1", 0x118, 'R'}, {"STREAM_IN_W2", 0x11C, 'R'},
    {"STREAM_RECEIVE", 0x120, 'R'},
};
// 17.7: the RW engine's
const SpecReg SPEC_RW_ENGINE[] = {
    {"RW_CONFIG", 0x080, 'B'}, {"RW_LPDDR_ADDR", 0x084, 'B'}, {"RW_BUFFER_ADDR", 0x088, 'B'}, {"RW_REPLY_W0", 0x08C, 'W'},
    {"RW_REPLY_SEND", 0x090, 'W'},
};
// 17.8.1: the replayer's
const SpecReg SPEC_REPLAYER[] = {
    {"RUN_CONFIG", 0x080, 'B'}, {"ROWS_RUN", 0x084, 'R'}, {"EXPANSION_OFFSET", 0x088, 'R'}, {"EXPANSION_OPCODE", 0x08C, 'R'},
};
// 17.9.1: the Tracer's
const SpecReg SPEC_TRACER[] = {
    {"RUN_CONFIG", 0x080, 'B'}, {"ROWS_RUN", 0x084, 'R'}, {"EXPANSION_OFFSET", 0x088, 'R'}, {"EXPANSION_OPCODE", 0x08C, 'R'},
    {"STATE_LENGTH", 0x090, 'R'}, {"CODE_LENGTH", 0x094, 'R'}, {"READ_LOG_LENGTH", 0x098, 'R'}, {"WRITE_LOG_LENGTH", 0x09C, 'R'},
    {"HASH_MUL", 0x0A0, 'B'}, {"HASH_STEP", 0x0A4, 'B'}, {"HASH_LINES", 0x0A8, 'B'}, {"EXPANSION_TEMPLATE", 0x0AC, 'W'},
    {"STAT_CYCLES", 0x0B0, 'R'}, {"STAT_STALL_FETCH", 0x0B4, 'R'}, {"STAT_STALL_DATA", 0x0B8, 'R'}, {"STAT_STALL_MAP", 0x0BC, 'R'},
    {"STAT_STALL_OUTPUT", 0x0C0, 'R'}, {"STAT_MISS_I", 0x0C4, 'R'}, {"STAT_MISS_D", 0x0C8, 'R'}, {"STAT_PROBES", 0x0CC, 'R'},
};
struct SpecPeripheral { int type; const char* prefix; bool dma; const SpecReg* regs; int n; };
const SpecPeripheral SPEC_PERIPHERALS[] = {   // 17.1's types; 17.6-17.9: which has a DMA
    {1, "IE", false, SPEC_INGRESS_EGRESS, sizeof SPEC_INGRESS_EGRESS / sizeof(SpecReg)},
    {2, "RW", true, SPEC_RW_ENGINE, sizeof SPEC_RW_ENGINE / sizeof(SpecReg)},
    {3, "TR", true, SPEC_TRACER, sizeof SPEC_TRACER / sizeof(SpecReg)},
    {4, "RP", false, SPEC_REPLAYER, sizeof SPEC_REPLAYER / sizeof(SpecReg)},
};

// the bits: the group is mesh.h's macro prefix; n > 1 a run of bits, one per peripheral or counter
struct SpecBit { const char* group; const char* name; int bit, n; bool live; };
const SpecBit SPEC_BITS[] = {
    // 7.1 STATUS
    {"STATUS", "arrived", 0, 1, false}, {"STATUS", "dma_busy", 1, 1, false}, {"STATUS", "gather_busy", 2, 1, false},
    {"STATUS", "request_full", 3, 1, false},
    // 7.2 the tile's events
    {"EVENT", "release", 0, 1, false}, {"EVENT", "dma_done", 1, 1, false}, {"EVENT", "all_counters_done", 2, 1, false},
    {"EVENT", "request_pending", 3, 1, true}, {"EVENT", "peripheral", 4, 3, true}, {"EVENT", "counter_done", 7, 7, false},
    // 7.4 the tile's errors
    {"ERROR", "router_overflow", 0, 1, false}, {"ERROR", "bad_packet", 1, 1, false}, {"ERROR", "counter_underflow", 2, 1, false},
    {"ERROR", "drift", 3, 1, false}, {"ERROR", "gather_overflow", 4, 1, false}, {"ERROR", "read_overflow", 5, 1, false},
    {"ERROR", "early_arrival", 6, 1, false}, {"ERROR", "access_fault", 7, 1, false}, {"ERROR", "illegal_instruction", 8, 1, false},
    {"ERROR", "receive_underflow", 9, 1, false}, {"ERROR", "peripheral", 10, 3, true},
    // 17.1 every peripheral's
    {"P_STATUS", "busy", 0, 1, false}, {"P_STATUS", "dma_busy", 1, 1, false}, {"P_EVENT", "done", 0, 1, false},
    {"P_EVENT", "dma_done", 1, 1, false},
    // 17.6 Ingress/Egress
    {"IE_STATUS", "stream_last", 2, 1, false}, {"IE_ERROR", "tile_error", 0, 1, true}, {"IE_EVENT", "all_arrived", 2, 1, true},
    {"IE_EVENT", "watchdog", 3, 1, false}, {"IE_EVENT", "uart_rx", 4, 1, true}, {"IE_EVENT", "uart_tx", 5, 1, true},
    {"IE_EVENT", "stream_in", 6, 1, true}, {"IE_EVENT", "stream_out", 7, 1, true},
    // 17.7 the RW engine
    {"RW_ERROR", "lpddr_error", 0, 1, false},
    // 17.8.1 the replayer
    {"RP_EVENT", "code_credit", 2, 1, false}, {"RP_EVENT", "read_log_credit", 3, 1, false}, {"RP_EVENT", "write_log_credit", 4, 1, false},
    {"RP_EVENT", "expansion_credit", 5, 1, false}, {"RP_EVENT", "new_expansion", 6, 1, false},
    {"RP_ERROR", "queue_overrun", 0, 1, false}, {"RP_ERROR", "invalid_expansion", 1, 1, false}, {"RP_ERROR", "code_underflow", 2, 1, false},
    {"RP_ERROR", "read_log_underflow", 3, 1, false}, {"RP_ERROR", "write_log_underflow", 4, 1, false}, {"RP_ERROR", "leftover", 5, 1, false},
    // 17.9.1 the Tracer
    {"TR_EVENT", "advice_credit", 2, 1, false}, {"TR_EVENT", "expansion_credit", 3, 1, false}, {"TR_EVENT", "new_expansion", 4, 1, false},
    {"TR_EVENT", "state_ready", 5, 1, true}, {"TR_EVENT", "code_ready", 6, 1, true}, {"TR_EVENT", "read_log_ready", 7, 1, true},
    {"TR_EVENT", "write_log_ready", 8, 1, true},
    {"TR_ERROR", "queue_overrun", 0, 1, false}, {"TR_ERROR", "invalid_expansion", 1, 1, false}, {"TR_ERROR", "advice_underflow", 2, 1, false},
    {"TR_ERROR", "map_miss", 3, 1, false}, {"TR_ERROR", "store_not_writable", 4, 1, false}, {"TR_ERROR", "page_missing", 5, 1, false},
    {"TR_ERROR", "out_of_range", 6, 1, false}, {"TR_ERROR", "failed_assertion", 7, 1, false}, {"TR_ERROR", "misaligned", 8, 1, false},
    {"TR_ERROR", "write_back_error", 9, 1, false},
};

// the fields, as [hi:lo]
struct SpecField { const char* word; const char* name; int hi, lo; };
const SpecField SPEC_FIELDS[] = {
    // 4.1 the packet, at 8 x 8
    {"PACKET", "payload", 127, 0}, {"PACKET", "address", 159, 128}, {"PACKET", "size", 161, 160}, {"PACKET", "counter", 164, 162},
    {"PACKET", "mode", 167, 165}, {"PACKET", "src", 174, 168}, {"PACKET", "dst", 181, 175},
    // 5.4 header registers; 9, 8.3, 17.1 their own fields
    {"HDR", "size", 1, 0}, {"HDR", "counter", 4, 2}, {"HDR", "mode", 7, 5}, {"HDR", "dst", 14, 8},
    {"WRITE_HDR", "no_increment", 15, 15}, {"STREAM_HDR", "count", 31, 15}, {"DMA_HDR", "count", 26, 15}, {"DMA_HDR", "queue", 28, 27},
    // 7.1 PARAMS
    {"PARAMS", "tile_id", 6, 0}, {"PARAMS", "peripherals", 9, 7}, {"PARAMS", "receiver", 10, 10}, {"PARAMS", "request_target", 13, 11},
    {"PARAMS", "attach", 16, 14}, {"PARAMS", "barrier", 17, 17}, {"PARAMS", "boot_local", 18, 18},
    // 8.1, 10, 12.1, 12.2, 13
    {"DMA_CONFIG", "kind", 0, 0}, {"DMA_CONFIG", "start", 8, 1}, {"DMA_CONFIG", "count", 17, 9}, {"DMA_CONFIG", "gap", 20, 18},
    {"READ_SEND", "address", 20, 0}, {"READ_FREE", "free", 15, 0},
    {"RECEIVE_CONFIG", "bucket_digit", 2, 0}, {"RECEIVE_CONFIG", "histogram_digit", 5, 3}, {"COUNTER", "count", 19, 0},
    {"GATHER_CONFIG", "record_base", 20, 0}, {"GATHER_CONFIG", "value_size", 23, 21},
    // 8.3, 8.4, 12.5 the tables
    {"HIST", "count", 16, 0}, {"PTR", "address", 20, 0}, {"STREAM_SRC_ADDR", "address", 20, 0},
    {"GROUP_W0", "rotations", 17, 0}, {"GROUP_W0", "first_stream", 25, 18}, {"GROUP_W0", "streams", 29, 26}, {"GROUP_W1", "gap", 7, 0},
    // 11.2, 11.5 requests
    {"REQUEST", "requester", 6, 0}, {"REQUEST", "target", 9, 7}, {"REQUEST", "op", 17, 10}, {"REQUEST", "own", 31, 18},
    {"REPORT", "number", 25, 18}, {"MEMORY", "beats", 27, 18}, {"MEMORY", "data_counter", 30, 28},
    {"MEMORY_W3", "reply_addr", 23, 0}, {"MEMORY_W3", "reply_counter", 26, 24}, {"MEMORY_REPLY_W1", "counter", 2, 0},
    // 17.1 a peripheral's PARAMS
    {"P_PARAMS", "type", 7, 0}, {"P_PARAMS", "version", 15, 8}, {"P_PARAMS", "constants", 31, 16},
    // 17.6 Ingress/Egress
    {"LED", "led", 0, 0}, {"UART_TX", "byte", 7, 0}, {"UART_RX", "byte", 7, 0}, {"UART_RX", "valid", 8, 8},
    {"UART_DIVISOR", "divisor", 15, 0}, {"UART_STATUS", "tx_bytes", 4, 0}, {"UART_STATUS", "rx_bytes", 12, 8},
    {"UART_STATUS", "rx_overrun", 16, 16}, {"UART_STATUS", "framing_error", 17, 17},
    // 17.7 the RW engine, at LPDDR_ADDRESS_BITS 32 and RW_BUFFER_BYTES 16 KiB
    {"RW_CONFIG", "kind", 0, 0}, {"RW_CONFIG", "beats", 10, 1}, {"RW_LPDDR_ADDR", "address", 27, 0}, {"RW_BUFFER_ADDR", "address", 9, 0},
    // 17.8.1, 17.9.1 the replayer and the Tracer
    {"RUN_CONFIG", "rows", 23, 0}, {"RUN_CONFIG", "store_log", 24, 24}, {"RUN_CONFIG", "rd_prev", 25, 25},
    {"RUN_CONFIG", "tracer_compression", 28, 26}, {"ROWS_RUN", "rows", 23, 0}, {"EXPANSION_OFFSET", "offset", 15, 0},
    {"HASH_STEP", "step", 23, 0}, {"HASH_LINES", "k", 4, 0}, {"EXPANSION_TEMPLATE", "template", 6, 0}, {"EXPANSION_TEMPLATE", "valid", 31, 31},
    // 17.9.2 the files word
    {"FILES", "memory_root", 30, 0}, {"FILES", "memory_depth", 32, 31}, {"FILES", "map_root", 63, 33}, {"FILES", "map_depth", 65, 64},
    {"FILES", "log_k", 71, 66}, {"FILES", "io_size", 102, 72},
};

// numbers: 2.1, 4.1, 4.2, 5.1, 5.2, 8.1, 11.2, 11.5, 6.5, 17.1, 17.7 and chapter 19's defaults
struct SpecValue { const char* macro; uint32_t value; };
const SpecValue SPEC_VALUES[] = {
    {"SIZE_32", 0}, {"SIZE_64", 1}, {"SIZE_128", 2},
    {"MODE_WRITE", 0}, {"MODE_APPEND", 1}, {"MODE_BUCKET", 2}, {"MODE_AUGMENT", 3}, {"MODE_GATHER", 4}, {"MODE_COUNT", 5},
    {"TILE_MASTER", 64}, {"TILE_MEMORY", 65}, {"TILE_TRACER", 66}, {"TILE_BROADCAST", 126}, {"TILE_NONE", 127},
    {"ATTACH_MASTER", 0}, {"ATTACH_MEMORY", 7}, {"ATTACH_TRACER", 15},
    {"DMA_KIND_STREAM", 0}, {"DMA_KIND_GROUP", 1}, {"RW_KIND_READ", 0}, {"RW_KIND_WRITE", 1},
    {"TARGET_MASTER", 0}, {"TARGET_MEMORY", 1}, {"TARGET_TRACER", 2},
    {"OP_REPORT", 1}, {"OP_LOAD", 3}, {"OP_READ", 1}, {"OP_WRITE", 2}, {"REPORT_ERRORS", 24}, {"REPORT_MTVAL", 25}, {"LOAD_COUNTER", 7},
    {"PERIPHERAL_TYPE_NONE", 0}, {"PERIPHERAL_TYPE_INGRESS_EGRESS", 1}, {"PERIPHERAL_TYPE_RW_ENGINE", 2},
    {"PERIPHERAL_TYPE_TRACER", 3}, {"PERIPHERAL_TYPE_REPLAYER", 4},
    {"MESH_SIZE", 8}, {"TILE_COUNT", 64}, {"TILE_ID_BITS", 7}, {"IMEM_BYTES", 32768}, {"DMEM_BYTES", 32768},
    {"BOOT_ROM_BYTES", 256}, {"SCRATCHPAD_BYTES", 4u << 20}, {"WORD_ADDRESS_BITS", 21}, {"WINDOW_LOAD_LATENCY", 2},
    {"TABLE_LOAD_LATENCY", 1}, {"COUNTER_BITS", 20}, {"HIST_BITS", 17}, {"TOKEN_COUNT", 64}, {"GATHER_QUEUE_DEPTH", 64},
    {"READ_QUEUE_DEPTH", 64}, {"REQUEST_QUEUE_DEPTH", 4}, {"RECEIVE_QUEUE_DEPTH_MASTER", 128},
    {"RECEIVE_QUEUE_DEPTH_MEMORY", 256}, {"RECEIVE_QUEUE_DEPTH_TRACER", 128}, {"STREAM_BYTES", 4}, {"STREAM_DEPTH", 4},
    {"UART_FIFO_DEPTH", 16}, {"LPDDR_ADDRESS_BITS", 32}, {"RW_QUEUE_BEATS", 16}, {"RW_BUFFER_BYTES", 16384},
    {"REPLAY_QUEUE_ENTRIES", 256}, {"TRACER_WINDOW_ROWS", 65536}, {"RECORD_WORDS", 68},
    // the address map (5.1), region 0xFF (5.2) and the peripherals' regions (17.8.9, 17.9.2), as mesh.h names them
    {"DMEM_BASE", 0x00000000}, {"IMEM_BASE", 0x01000000}, {"BOOT_ROM_BASE", 0x02000000}, {"SCRATCHPAD_BASE", 0x03000000},
    {"DMEM_RESERVED", 16}, {"WINDOWS_BASE", 0xFFFFE000}, {"TILE_WINDOW_BASE", 0xFFFFF800},
    {"HIST_BASE", 0xFFFFE000}, {"PTR_BASE", 0xFFFFE400}, {"STREAM_SRC_ADDR_BASE", 0xFFFFE800},
    {"STREAM_DEST_ADDR_BASE", 0xFFFFEC00}, {"STREAM_HDR_BASE", 0xFFFFF000}, {"GROUP_BASE", 0xFFFFF400},
};
struct SpecPart { const char* macro; uint32_t offset; };   // 17.7, 17.8.9, 17.9.2: offsets in the peripheral's region
const SpecPart SPEC_PARTS[] = {
    {"RW_BUFFER", 0x000000},
    {"RP_STATE", 0x000000}, {"RP_STREAMS", 0x000010}, {"RP_IMAGE", 0x000020}, {"RP_RECORD_END", 0x000420},
    {"RP_DECODE_TABLE", 0x001000}, {"RP_ROM", 0x002000}, {"RP_QUEUE_CODE", 0x100000}, {"RP_QUEUE_READ_LOG", 0x200000},
    {"RP_QUEUE_WRITE_LOG", 0x300000}, {"RP_QUEUE_EXPANSION", 0x400000},
    {"TR_RECORD", 0x000000}, {"TR_DECODE_TABLE", 0x001000}, {"TR_ROM", 0x002000}, {"TR_QUEUE_ADVICE", 0x200000},
    {"TR_QUEUE_EXPANSION", 0x400000},
};

template <class T, size_t N> constexpr int count_of(const T (&)[N]) { return int(N); }
int access_of(char c) { return c == 'R' ? regs::R : c == 'W' ? regs::W : regs::RW; }

// ================================================================ reading #defines back from a header's text
// name (with its parameter list, as written: "HDR_SIZE(v)") -> the definition's text, up to its comment
std::map<std::string, std::string> defines(const std::string& text) {
    std::map<std::string, std::string> m;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(p, e - p);
        p = e + 1;
        if (line.compare(0, 8, "#define ") != 0) continue;
        size_t n = 8;
        int depth = 0;
        while (n < line.size() && (depth > 0 || line[n] != ' ')) { depth += line[n] == '(' ? 1 : line[n] == ')' ? -1 : 0; n++; }
        std::string value = line.substr(n);
        const size_t c = value.find("/*");
        if (c != std::string::npos) value.erase(c);
        const size_t a = value.find_first_not_of(' '), z = value.find_last_not_of(' ');
        m[line.substr(8, n - 8)] = a == std::string::npos ? "" : value.substr(a, z - a + 1);
    }
    return m;
}
// a definition that is one number (decimal or hex, an optional u), "(1 << N)" or "(-0xN)"; false if it is anything else
bool number(const std::string& s, uint32_t& v) {
    unsigned n;
    char tail[8];
    if (sscanf(s.c_str(), "(1 << %u)%1s", &n, tail) == 1) { v = 1u << n; return true; }
    if (sscanf(s.c_str(), "(-0x%x)%1s", &n, tail) == 1) { v = 0u - n; return true; }
    char* end;
    const unsigned long x = strtoul(s.c_str(), &end, 0);
    if (end == s.c_str()) return false;
    if (*end == 'u') end++;
    if (*end) return false;
    v = uint32_t(x);
    return true;
}
// mesh.h's value of a macro that is a number; fails the lookup with a value no specification holds
uint32_t value_of(const std::map<std::string, std::string>& m, const std::string& name) {
    const auto it = m.find(name);
    uint32_t v = 0;
    if (it == m.end() || !number(it->second, v)) return 0xDEADBEEF;
    return v;
}
// a field macro's position, from either of its forms: (((uint32_t)(v) & 0xMu) << LO) and (((w) >> LO) & 0xMu)
bool field_of(const std::map<std::string, std::string>& m, const std::string& name, unsigned& lo, unsigned& mask) {
    const auto it = m.find(name);
    if (it == m.end()) return false;
    char tail[8];
    if (sscanf(it->second.c_str(), "(((uint32_t)(v) & 0x%xu) << %u)%1s", &mask, &lo, tail) == 2) return true;
    return sscanf(it->second.c_str(), "(((w) >> %u) & 0x%xu)%1s", &lo, &mask, tail) == 2;
}
std::string upper(const char* s) { return regs::upper(s); }

// ================================================================ (a) the committed files
TEST(mesh_h_is_the_generators_output) {
    const std::string want = regs::mesh_h();
    CHECK(slurp("fw/include/mesh.h") == want);
}

TEST(registers_md_is_the_generators_output) {
    const std::string want = regs::registers_md();
    CHECK(slurp("fw/REGISTERS.md") == want);
}

// ================================================================ (b) the table against regmap.h
TEST(every_tile_window_word_agrees_with_regmap) {
    for (int i = 0; i < int(regs::WINDOW_WORDS); i++) {
        const regs::Reg* r = regs::reg_at(regs::TILE, regs::N_TILE, i);
        const units::RegInfo& m = units::reg_info(unsigned(i));
        CHECK_EQ(r != nullptr, m.access != units::A_NONE);
        CHECK_EQ(r != nullptr, m.name != nullptr);
        if (!r) continue;
        CHECK(std::string(r->name) == m.name);
        CHECK_EQ(r->access, int(m.access));   // R 1, W 2, R/W 3 in both
    }
    CHECK_EQ(int(regs::R), int(units::A_R));
    CHECK_EQ(int(regs::W), int(units::A_W));
    CHECK_EQ(int(regs::RW), int(units::A_RW));
}

TEST(register_indices_agree_with_regmap) {
    struct { const char* name; unsigned idx; } named[] = {
        {"PARAMS", units::PARAMS}, {"STATUS", units::STATUS}, {"ERRORS", units::ERRORS}, {"EVENTS", units::EVENTS},
        {"EVENTS_PEEK", units::EVENTS_PEEK}, {"EVENTS_MASK", units::EVENTS_MASK}, {"ARRIVE", units::ARRIVE},
        {"DMA_CONFIG", units::DMA_CONFIG}, {"DMA_START", units::DMA_START}, {"WRITE_HDR", units::WRITE_HDR},
        {"WRITE_DEST_ADDR", units::WRITE_DEST_ADDR}, {"WRITE_W0", units::WRITE_W0}, {"WRITE_W1", units::WRITE_W1},
        {"WRITE_W2", units::WRITE_W2}, {"WRITE_SEND_32", units::WRITE_SEND_32}, {"WRITE_SEND_64", units::WRITE_SEND_64},
        {"WRITE_SEND_128", units::WRITE_SEND_128}, {"READ_HDR", units::READ_HDR}, {"READ_DEST_ADDR", units::READ_DEST_ADDR},
        {"READ_SEND", units::READ_SEND}, {"READ_FREE", units::READ_FREE}, {"REQUEST_OUT_W0", units::REQUEST_OUT_W0},
        {"REQUEST_OUT_W1", units::REQUEST_OUT_W1}, {"REQUEST_OUT_W2", units::REQUEST_OUT_W2},
        {"REQUEST_SEND", units::REQUEST_SEND}, {"REQUEST_IN_W0", units::REQUEST_IN_W0},
        {"REQUEST_IN_W1", units::REQUEST_IN_W1}, {"REQUEST_IN_W2", units::REQUEST_IN_W2},
        {"REQUEST_RECEIVE", units::REQUEST_RECEIVE}, {"RECEIVE_CONFIG", units::RECEIVE_CONFIG},
        {"COUNTER_1", units::COUNTER_1}, {"COUNTER_7", units::COUNTER_7}, {"GATHER_CONFIG", units::GATHER_CONFIG},
    };
    for (const auto& n : named) {
        const regs::Reg* r = regs::reg_named(regs::TILE, regs::N_TILE, n.name);
        CHECK(r != nullptr);
        CHECK_EQ(unsigned(r->index), n.idx);
    }
    CHECK_EQ(unsigned(regs::TILE[regs::N_TILE - 1].index) + 1, unsigned(units::TILE_REGS));   // one past the last defined word
    struct { const char* name; unsigned idx; } periph[] = {
        {"PARAMS", units::P_PARAMS}, {"STATUS", units::P_STATUS}, {"ERRORS", units::P_ERRORS}, {"EVENTS", units::P_EVENTS},
        {"EVENTS_PEEK", units::P_EVENTS_PEEK}, {"EVENTS_MASK", units::P_EVENTS_MASK}, {"START", units::P_START},
        {"DMA_HDR", units::P_DMA_HDR}, {"DMA_DEST_ADDR", units::P_DMA_DEST_ADDR}, {"DMA_START", units::P_DMA_START},
    };
    CHECK_EQ(count_of(periph), regs::N_PERIPHERAL);
    for (const auto& n : periph) {
        const regs::Reg* r = regs::reg_named(regs::PERIPHERAL, regs::N_PERIPHERAL, n.name);
        CHECK(r != nullptr);
        CHECK_EQ(unsigned(r->index), n.idx);
    }
    CHECK_EQ(unsigned(regs::PERIPHERAL_OWN), unsigned(units::P_OWN));
    // every peripheral's own words: in blocks 2-7, in ascending order
    for (const regs::Peripheral& p : regs::PERIPHERALS)
        for (int k = 0; k < p.n_regs; k++) {
            CHECK(p.regs[k].index >= int(units::P_OWN) && p.regs[k].index < int(units::WINDOW_WORDS));
            CHECK(k == 0 || p.regs[k].index > p.regs[k - 1].index);
        }
    CHECK_EQ(regs::PERIPHERALS[0].type, int(units::PT_INGRESS_EGRESS));
    CHECK_EQ(regs::PERIPHERALS[1].type, int(units::PT_RW_ENGINE));
    CHECK_EQ(regs::PERIPHERALS[2].type, int(units::PT_TRACER_TYPE));
    CHECK_EQ(regs::PERIPHERALS[3].type, int(units::PT_REPLAYER_TYPE));
    CHECK_EQ(regs::value("PERIPHERAL_TYPE_NONE"), uint32_t(units::PT_NONE));
}

TEST(address_map_agrees_with_regmap) {
    CHECK_EQ(regs::DMEM_BASE >> 24, uint32_t(units::R_DMEM));
    CHECK_EQ(regs::IMEM_BASE, uint32_t(units::IMEM_BASE));
    CHECK_EQ(regs::IMEM_BASE >> 24, uint32_t(units::R_IMEM));
    CHECK_EQ(regs::BOOT_ROM_BASE, uint32_t(units::BOOTROM_BASE));
    CHECK_EQ(regs::BOOT_ROM_BASE >> 24, uint32_t(units::R_BOOTROM));
    CHECK_EQ(regs::SCRATCHPAD_BASE >> 24, uint32_t(units::R_SCRATCHPAD));
    CHECK_EQ(regs::peripheral_base(1) >> 24, uint32_t(units::R_PERIPH_1));
    CHECK_EQ(regs::peripheral_base(2) >> 24, uint32_t(units::R_PERIPH_2));
    CHECK_EQ(regs::peripheral_base(3) >> 24, uint32_t(units::R_PERIPH_3));
    CHECK_EQ(regs::WINDOWS_BASE >> 24, uint32_t(units::R_REGS));
    CHECK_EQ(regs::value("IMEM_BYTES"), uint32_t(units::MEM_BYTES));
    CHECK_EQ(regs::value("DMEM_BYTES"), uint32_t(units::MEM_BYTES));
    CHECK_EQ(regs::value("BOOT_ROM_BYTES"), uint32_t(units::BOOT_ROM_BYTES));
    CHECK_EQ(regs::DMEM_RESERVED, uint32_t(units::DMEM_FIRST));
    CHECK_EQ(regs::WINDOWS_BASE, uint32_t(units::REGS_BASE));
    CHECK_EQ(regs::WINDOW_BYTES, uint32_t(units::WINDOW_BYTES));
    CHECK_EQ(regs::WINDOW_WORDS, uint32_t(units::WINDOW_WORDS));
    CHECK_EQ(regs::BLOCK_WORDS, uint32_t(units::BLOCK_WORDS));
    CHECK_EQ(regs::BLOCKS, uint32_t(units::BLOCKS));
    CHECK_EQ(regs::TABLE_END, uint32_t(units::TABLE_END));
    CHECK_EQ(regs::TILE_WINDOW_BASE, uint32_t(units::TILE_WINDOW_BASE));
    CHECK_EQ(regs::peripheral_window_base(1), uint32_t(units::PERIPH_WINDOW_BASE));
    CHECK_EQ(units::window_index(regs::TILE_WINDOW_BASE), unsigned(units::WINDOW_TILE));
    CHECK_EQ(units::window_index(regs::TABLE_END), unsigned(units::WINDOW_UNMAPPED));
    for (int n = 1; n <= 3; n++) CHECK_EQ(units::periph_of(regs::peripheral_window_base(n)), unsigned(n));
    const uint32_t bases[] = {units::HIST_BASE, units::PTR_BASE, units::STREAM_SRC_ADDR_BASE, units::STREAM_DEST_ADDR_BASE,
                              units::STREAM_HDR_BASE, units::GROUP_BASE};
    const char* names[] = {"HIST", "PTR", "STREAM_SRC_ADDR", "STREAM_DEST_ADDR", "STREAM_HDR", "GROUP"};
    CHECK_EQ(regs::N_TABLES, 6);
    for (int t = 0; t < regs::N_TABLES; t++) {
        CHECK(std::string(regs::TABLES[t].name) == names[t]);
        CHECK_EQ(regs::TABLES[t].base, bases[t]);
        CHECK(units::in_tables(regs::TABLES[t].base));
        // the tables are back to back, and end at TABLE_END
        const uint32_t end = regs::TABLES[t].base + uint32_t(regs::TABLES[t].entries * regs::TABLES[t].words * 4);
        CHECK_EQ(end, t + 1 < regs::N_TABLES ? regs::TABLES[t + 1].base : uint32_t(units::TABLE_END));
    }
    CHECK_EQ(regs::value("WINDOW_LOAD_LATENCY"), uint32_t(units::WINDOW_LOAD_LATENCY));
    CHECK_EQ(regs::value("TABLE_LOAD_LATENCY"), uint32_t(units::TABLE_LOAD_LATENCY));
    // the tile ids
    CHECK_EQ(regs::value("MESH_SIZE"), uint32_t(units::MESH_SIZE));
    CHECK_EQ(regs::value("TILE_COUNT"), uint32_t(units::TILE_COUNT));
    CHECK_EQ(regs::value("TILE_ID_BITS"), uint32_t(units::TILE_ID_BITS));
    CHECK_EQ(regs::value("TILE_MASTER"), uint32_t(units::ID_MASTER));
    CHECK_EQ(regs::value("TILE_MEMORY"), uint32_t(units::ID_MEMORY));
    CHECK_EQ(regs::value("TILE_TRACER"), uint32_t(units::ID_TRACER));
    CHECK_EQ(regs::value("TILE_BROADCAST"), uint32_t(units::ID_BROADCAST));
    CHECK_EQ(regs::value("TILE_NONE"), uint32_t(units::ID_NONE));
    CHECK_EQ(regs::value("ATTACH_MASTER"), uint32_t(units::ATTACH_MASTER));
    CHECK_EQ(regs::value("ATTACH_MEMORY"), uint32_t(units::ATTACH_MEMORY));
    CHECK_EQ(regs::value("ATTACH_TRACER"), uint32_t(units::ATTACH_TRACER));
}

// the mask of a group's bits: all of them, or the live ones
uint32_t group_mask(const char* group, bool live_only) {
    uint32_t m = 0;
    for (const regs::Bit& b : regs::BITS)
        if (regs::same(b.group, group) && (!live_only || regs::same(b.kind, "live"))) m |= ((1u << b.n) - 1) << b.bit;
    return m;
}

TEST(bits_agree_with_regmap) {
    struct { const char* group; const char* name; uint32_t bit; } named[] = {
        {"STATUS", "arrived", units::ST_ARRIVED}, {"STATUS", "dma_busy", units::ST_DMA_BUSY},
        {"STATUS", "gather_busy", units::ST_GATHER_BUSY}, {"STATUS", "request_full", units::ST_REQUEST_FULL},
        {"EVENT", "release", units::EV_RELEASE}, {"EVENT", "dma_done", units::EV_DMA_DONE},
        {"EVENT", "all_counters_done", units::EV_ALL_COUNTERS_DONE}, {"EVENT", "request_pending", units::EV_REQUEST_PENDING},
        {"EVENT", "peripheral", units::EV_PERIPH_1}, {"EVENT", "counter_done", units::EV_COUNTER_1_DONE},
        {"ERROR", "router_overflow", units::ERR_ROUTER_OVERFLOW}, {"ERROR", "bad_packet", units::ERR_BAD_PACKET},
        {"ERROR", "counter_underflow", units::ERR_COUNTER_UNDERFLOW}, {"ERROR", "drift", units::ERR_DRIFT},
        {"ERROR", "gather_overflow", units::ERR_GATHER_OVERFLOW}, {"ERROR", "read_overflow", units::ERR_READ_OVERFLOW},
        {"ERROR", "early_arrival", units::ERR_EARLY_ARRIVAL}, {"ERROR", "access_fault", units::ERR_ACCESS_FAULT},
        {"ERROR", "illegal_instruction", units::ERR_ILLEGAL_INSTRUCTION},
        {"ERROR", "receive_underflow", units::ERR_RECEIVE_UNDERFLOW}, {"ERROR", "peripheral", units::ERR_PERIPH_1},
        {"P_STATUS", "busy", units::P_ST_BUSY}, {"P_STATUS", "dma_busy", units::P_ST_DMA_BUSY},
        {"P_EVENT", "done", units::P_EV_DONE}, {"P_EVENT", "dma_done", units::P_EV_DMA_DONE},
    };
    for (const auto& n : named) {
        const regs::Bit& b = regs::bit(n.group, n.name);
        CHECK(std::string(b.name) == n.name);   // found
        CHECK_EQ(uint32_t(b.bit), n.bit);
    }
    // every bit of the tile's groups is one of those: the masks
    CHECK_EQ(group_mask("STATUS", false), 0xFu);
    CHECK_EQ(group_mask("EVENT", false), uint32_t(units::EVENTS_VALID));
    CHECK_EQ(group_mask("EVENT", true), uint32_t(units::EVENTS_LIVE));
    CHECK_EQ(group_mask("ERROR", false), uint32_t(units::ERRORS_VALID));
    CHECK_EQ(group_mask("ERROR", true), uint32_t(units::ERRORS_LIVE));
    CHECK_EQ(regs::EVENTS_MASK_RESET, uint32_t(units::EVENTS_MASK_RESET));
    const regs::Bit& cd = regs::bit("EVENT", "counter_done");
    CHECK_EQ(cd.n, 7);
    CHECK_EQ((((1u << cd.n) - 1) << cd.bit) | 1u << regs::bit("EVENT", "all_counters_done").bit, uint32_t(units::COUNTER_EVENTS));
    for (unsigned n = 1; n <= 7; n++) CHECK_EQ(1u << (unsigned(cd.bit) - 1 + n), units::ev_counter_done(n));
    CHECK_EQ(regs::bit("EVENT", "peripheral").n, 3);
    CHECK_EQ(regs::bit("ERROR", "peripheral").n, 3);
}

TEST(fields_agree_with_regmap) {
    using regs::field;
    // the header registers (5.4)
    CHECK_EQ(uint32_t(field("HDR", "size").lo), uint32_t(units::HDR_SIZE_LO));
    CHECK_EQ(uint32_t(field("HDR", "counter").lo), uint32_t(units::HDR_COUNTER_LO));
    CHECK_EQ(uint32_t(field("HDR", "mode").lo), uint32_t(units::HDR_MODE_LO));
    CHECK_EQ(uint32_t(field("HDR", "dst").lo), uint32_t(units::HDR_DST_LO));
    CHECK_EQ(field("HDR", "dst").mask(), uint32_t(units::HDR_DST_MASK));
    CHECK_EQ(field("HDR", "size").mask() << field("HDR", "size").lo | field("HDR", "counter").mask() << field("HDR", "counter").lo |
                 field("HDR", "mode").mask() << field("HDR", "mode").lo,
             uint32_t(units::HDR_BLOCK_MASK));
    const uint32_t h = 2u << field("HDR", "size").lo | 5u << field("HDR", "counter").lo | 3u << field("HDR", "mode").lo |
                       0x55u << field("HDR", "dst").lo;
    CHECK_EQ(units::hdr_size(h), 2u);
    CHECK_EQ(units::hdr_counter(h), 5u);
    CHECK_EQ(units::hdr_mode(h), 3u);
    CHECK_EQ(units::hdr_dst(h), 0x55u);
    CHECK_EQ(uint32_t(field("WRITE_HDR", "no_increment").lo), uint32_t(units::HDR_OWN_LO));
    CHECK_EQ(1u << field("WRITE_HDR", "no_increment").lo, uint32_t(units::WRITE_HDR_NO_INCREMENT));
    CHECK_EQ(uint32_t(field("DMA_HDR", "count").lo), uint32_t(units::DMA_HDR_COUNT_LO));
    CHECK_EQ(field("DMA_HDR", "count").mask(), uint32_t(units::DMA_HDR_COUNT_MASK));
    CHECK_EQ(uint32_t(field("DMA_HDR", "queue").lo), uint32_t(units::DMA_HDR_QUEUE_LO));
    CHECK_EQ(field("DMA_HDR", "queue").mask(), uint32_t(units::DMA_HDR_QUEUE_MASK));
    CHECK_EQ(uint32_t(field("STREAM_HDR", "count").lo), uint32_t(units::STREAM_HDR_COUNT_LO));
    CHECK_EQ(field("STREAM_HDR", "count").mask(), uint32_t(units::STREAM_HDR_COUNT_MASK));
    // word addresses (5.4)
    for (const char* w : {"PTR", "STREAM_SRC_ADDR", "READ_SEND"}) CHECK_EQ(field(w, "address").mask(), uint32_t(units::WORD_ADDRESS_MASK));
    CHECK_EQ(field("GATHER_CONFIG", "record_base").mask(), uint32_t(units::WORD_ADDRESS_MASK));
    CHECK_EQ(regs::value("WORD_ADDRESS_BITS"), 21u);
    // DMA_CONFIG (8.1)
    CHECK_EQ(uint32_t(field("DMA_CONFIG", "kind").lo), uint32_t(units::DMA_KIND_LO));
    CHECK_EQ(uint32_t(field("DMA_CONFIG", "start").lo), uint32_t(units::DMA_START_LO));
    CHECK_EQ(field("DMA_CONFIG", "start").mask(), uint32_t(units::DMA_START_MASK));
    CHECK_EQ(uint32_t(field("DMA_CONFIG", "count").lo), uint32_t(units::DMA_COUNT_LO));
    CHECK_EQ(field("DMA_CONFIG", "count").mask(), uint32_t(units::DMA_COUNT_MASK));
    CHECK_EQ(uint32_t(field("DMA_CONFIG", "gap").lo), uint32_t(units::DMA_GAP_LO));
    CHECK_EQ(field("DMA_CONFIG", "gap").mask(), uint32_t(units::DMA_GAP_MASK));
    CHECK_EQ((1u << (field("DMA_CONFIG", "gap").lo + field("DMA_CONFIG", "gap").width)) - 1, uint32_t(units::DMA_CONFIG_BITS));
    // GROUP (8.4)
    CHECK_EQ(field("GROUP_W0", "rotations").mask(), uint32_t(units::GROUP_ROTATIONS_MASK));
    CHECK_EQ(uint32_t(field("GROUP_W0", "first_stream").lo), uint32_t(units::GROUP_FIRST_LO));
    CHECK_EQ(field("GROUP_W0", "first_stream").mask(), uint32_t(units::GROUP_FIRST_MASK));
    CHECK_EQ(uint32_t(field("GROUP_W0", "streams").lo), uint32_t(units::GROUP_STREAMS_LO));
    CHECK_EQ(field("GROUP_W0", "streams").mask(), uint32_t(units::GROUP_STREAMS_MASK));
    CHECK_EQ((1u << (field("GROUP_W0", "streams").lo + field("GROUP_W0", "streams").width)) - 1, uint32_t(units::GROUP_W0_BITS));
    CHECK_EQ(field("GROUP_W1", "gap").mask(), uint32_t(units::GROUP_GAP_MASK));
    // RECEIVE_CONFIG, the counters, HIST (12)
    CHECK_EQ(uint32_t(field("RECEIVE_CONFIG", "bucket_digit").lo), uint32_t(units::RECEIVE_BUCKET_LO));
    CHECK_EQ(uint32_t(field("RECEIVE_CONFIG", "histogram_digit").lo), uint32_t(units::RECEIVE_HIST_LO));
    CHECK_EQ((1u << (field("RECEIVE_CONFIG", "histogram_digit").lo + field("RECEIVE_CONFIG", "histogram_digit").width)) - 1,
             uint32_t(units::RECEIVE_CONFIG_BITS));
    CHECK_EQ(uint32_t(field("COUNTER", "count").width), uint32_t(units::COUNTER_BITS));
    CHECK_EQ(field("COUNTER", "count").mask(), uint32_t(units::COUNTER_MASK));
    CHECK_EQ(regs::value("COUNTER_BITS"), uint32_t(units::COUNTER_BITS));
    CHECK_EQ(uint32_t(field("HIST", "count").width), uint32_t(units::HIST_BITS));
    CHECK_EQ(field("HIST", "count").mask(), uint32_t(units::HIST_MASK));
    CHECK_EQ(regs::value("HIST_BITS"), uint32_t(units::HIST_BITS));
    // GATHER_CONFIG (13), READ_FREE (10)
    CHECK_EQ(uint32_t(field("GATHER_CONFIG", "value_size").lo), uint32_t(units::GATHER_VALUE_SIZE_LO));
    CHECK_EQ(field("GATHER_CONFIG", "value_size").mask(), uint32_t(units::GATHER_VALUE_SIZE_MASK));
    CHECK_EQ((1u << (field("GATHER_CONFIG", "value_size").lo + field("GATHER_CONFIG", "value_size").width)) - 1,
             uint32_t(units::GATHER_CONFIG_BITS));
    CHECK_EQ(field("READ_FREE", "free").mask(), uint32_t(units::READ_FREE_MASK));
    // a request's word 0 (11.2, 11.5)
    CHECK_EQ(field("REQUEST", "requester").mask(), uint32_t(units::REQ_REQUESTER_MASK));
    CHECK_EQ(uint32_t(field("REQUEST", "target").lo), uint32_t(units::REQ_TARGET_LO));
    CHECK_EQ(field("REQUEST", "target").mask(), uint32_t(units::REQ_TARGET_MASK));
    CHECK_EQ(uint32_t(field("REQUEST", "op").lo), uint32_t(units::REQ_OP_LO));
    CHECK_EQ(field("REQUEST", "op").mask(), uint32_t(units::REQ_OP_MASK));
    CHECK_EQ(uint32_t(field("REPORT", "number").lo), uint32_t(units::REQ_REPORT_NUMBER_LO));
    CHECK_EQ(uint32_t(field("REQUEST", "own").lo), uint32_t(units::REQ_REPORT_NUMBER_LO));
    CHECK_EQ(regs::value("TARGET_MASTER"), uint32_t(units::REQ_TARGET_MASTER));
    CHECK_EQ(regs::value("TARGET_MEMORY"), uint32_t(units::REQ_TARGET_MEMORY));
    CHECK_EQ(regs::value("TARGET_TRACER"), uint32_t(units::REQ_TARGET_TRACER));
    CHECK_EQ(regs::value("OP_REPORT"), uint32_t(units::REQ_OP_REPORT));
    CHECK_EQ(regs::value("OP_LOAD"), uint32_t(units::REQ_OP_LOAD));
    CHECK_EQ(regs::value("OP_READ"), uint32_t(units::REQ_OP_READ));
    CHECK_EQ(regs::value("OP_WRITE"), uint32_t(units::REQ_OP_WRITE));
    // PARAMS (7.1): the masks, and a value built by regmap.h read back through the table's fields
    CHECK_EQ(field("PARAMS", "tile_id").mask(), uint32_t(units::PARAMS_TILE_ID_MASK));
    CHECK_EQ(uint32_t(field("PARAMS", "peripherals").lo), uint32_t(units::PARAMS_PERIPHERALS_LO));
    CHECK_EQ(1u << field("PARAMS", "receiver").lo, uint32_t(units::PARAMS_RECEIVER));
    CHECK_EQ(uint32_t(field("PARAMS", "request_target").lo), uint32_t(units::PARAMS_REQUEST_TARGET_LO));
    CHECK_EQ(uint32_t(field("PARAMS", "attach").lo), uint32_t(units::PARAMS_ATTACH_LO));
    CHECK_EQ(1u << field("PARAMS", "barrier").lo, uint32_t(units::PARAMS_BARRIER));
    CHECK_EQ(1u << field("PARAMS", "boot_local").lo, uint32_t(units::PARAMS_BOOT_LOCAL));
    units::Straps s;
    s.tile_id = 66; s.peripherals = 5; s.receiver = true; s.request_target = 2; s.attach = 4; s.barrier = false; s.boot_local = true;
    const uint32_t p = units::params_value(s);
    auto get = [&](const char* name) { const regs::Field& f = field("PARAMS", name); return (p >> f.lo) & f.mask(); };
    CHECK_EQ(get("tile_id"), 66u);
    CHECK_EQ(get("peripherals"), 5u);
    CHECK_EQ(get("receiver"), 1u);
    CHECK_EQ(get("request_target"), 2u);
    CHECK_EQ(get("attach"), 4u);
    CHECK_EQ(get("barrier"), 0u);
    CHECK_EQ(get("boot_local"), 1u);
    s = units::Straps{};
    s.barrier = true;
    CHECK_EQ(units::params_value(s), 1u << field("PARAMS", "barrier").lo);
}

// ================================================================ (c) the table and mesh.h against the specification
// one window's registers against the specification's list: the same names, at the same offsets, with the same access
bool same_registers(const regs::Reg* list, int n, const SpecReg* spec, int n_spec) {
    if (n != n_spec) return false;
    for (int k = 0; k < n; k++)
        if (std::string(list[k].name) != spec[k].name || unsigned(4 * list[k].index) != spec[k].offset || list[k].access != access_of(spec[k].access))
            return false;
    return true;
}

TEST(the_table_holds_the_specifications_registers) {
    CHECK(same_registers(regs::TILE, regs::N_TILE, SPEC_TILE, count_of(SPEC_TILE)));
    CHECK(same_registers(regs::PERIPHERAL, regs::N_PERIPHERAL, SPEC_PERIPHERAL, count_of(SPEC_PERIPHERAL)));
    CHECK_EQ(regs::N_PERIPHERALS, count_of(SPEC_PERIPHERALS));
    for (int k = 0; k < regs::N_PERIPHERALS; k++) {
        const regs::Peripheral& p = regs::PERIPHERALS[k];
        const SpecPeripheral& s = SPEC_PERIPHERALS[k];
        CHECK_EQ(p.type, s.type);
        CHECK(std::string(p.prefix) == s.prefix);
        CHECK_EQ(p.dma, s.dma);
        CHECK(same_registers(p.regs, p.n_regs, s.regs, s.n));
    }
}

TEST(the_table_holds_the_specifications_bits_fields_and_values) {
    CHECK_EQ(regs::N_BITS, count_of(SPEC_BITS));
    for (int k = 0; k < regs::N_BITS; k++) {
        const regs::Bit& b = regs::BITS[k];
        const SpecBit& s = SPEC_BITS[k];
        CHECK(std::string(b.group) == s.group && std::string(b.name) == s.name);
        CHECK_EQ(b.bit, s.bit);
        CHECK_EQ(b.n, s.n);
        CHECK_EQ(regs::same(b.kind, "live"), s.live);
    }
    CHECK_EQ(regs::N_FIELDS, count_of(SPEC_FIELDS));
    for (int k = 0; k < regs::N_FIELDS; k++) {
        const regs::Field& f = regs::FIELDS[k];
        const SpecField& s = SPEC_FIELDS[k];
        CHECK(std::string(f.word) == s.word && std::string(f.name) == s.name);
        CHECK_EQ(f.lo, s.lo);
        CHECK_EQ(f.lo + f.width - 1, s.hi);
    }
    for (const regs::Value& v : regs::VALUES) {   // every value of the table is one the specification gives below
        bool found = false;
        for (const SpecValue& s : SPEC_VALUES)
            if (regs::same(v.macro, s.macro)) { found = true; CHECK_EQ(v.value, s.value); }
        CHECK(found);
    }
    CHECK_EQ(regs::N_REGION_PARTS, count_of(SPEC_PARTS));
    for (int k = 0; k < regs::N_REGION_PARTS; k++) {
        CHECK(std::string(regs::REGION_PARTS[k].macro) == SPEC_PARTS[k].macro);
        CHECK_EQ(regs::REGION_PARTS[k].offset, SPEC_PARTS[k].offset);
    }
}

// The generated header, read back from its text: what a program built with it gets.
TEST(mesh_h_holds_the_specifications_offsets_bits_fields_and_values) {
    const auto m = defines(regs::mesh_h());
    for (const SpecReg& r : SPEC_TILE) CHECK_EQ(value_of(m, std::string("OFF_") + r.name), r.offset);
    for (const SpecReg& r : SPEC_PERIPHERAL) CHECK_EQ(value_of(m, std::string("P_OFF_") + r.name), r.offset);
    for (const SpecPeripheral& p : SPEC_PERIPHERALS)
        for (int k = 0; k < p.n; k++) CHECK_EQ(value_of(m, std::string(p.prefix) + "_OFF_" + p.regs[k].name), p.regs[k].offset);
    for (const SpecBit& b : SPEC_BITS) {
        const std::string name = std::string(b.group) + "_" + upper(b.name);
        if (b.n == 1) {
            CHECK_EQ(value_of(m, name), 1u << b.bit);
        } else {   // NAME(n) is (1 << (K + (n))): bit K + 1 for n = 1
            const auto it = m.find(name + "(n)");
            CHECK(it != m.end());
            unsigned k = 0;
            char tail[8];
            CHECK(sscanf(it->second.c_str(), "(1 << (%u + (n)))%1s", &k, tail) == 1);
            CHECK_EQ(k + 1, unsigned(b.bit));
        }
    }
    CHECK(m.count("EVENTS_MASK_RESET") && m.at("EVENTS_MASK_RESET") == "(EVENT_RELEASE | EVENT_ALL_COUNTERS_DONE)");   // 18
    int macros = 0;
    for (const SpecField& f : SPEC_FIELDS) {
        const std::string name = std::string(f.word) + "_" + upper(f.name);
        const unsigned width = unsigned(f.hi - f.lo + 1), want_mask = width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1;
        const regs::Field& t = regs::field(f.word, f.name);
        unsigned lo = 0, mask = 0;
        if (t.macro == regs::DOC) {
            CHECK(!m.count(name) && !m.count(name + "(v)") && !m.count(name + "(w)"));
            continue;
        }
        macros++;
        if (t.macro == regs::FLAG) {
            CHECK_EQ(width, 1u);
            CHECK_EQ(value_of(m, name), 1u << f.lo);
            continue;
        }
        CHECK(field_of(m, name + (t.macro == regs::GET ? "(w)" : "(v)"), lo, mask));
        CHECK_EQ(lo, unsigned(f.lo));
        CHECK_EQ(mask, want_mask);
        if (t.macro == regs::BOTH) {
            CHECK(field_of(m, name + "_OF(w)", lo, mask));
            CHECK_EQ(lo, unsigned(f.lo));
            CHECK_EQ(mask, want_mask);
        }
    }
    CHECK(macros >= 50);
    for (const SpecValue& v : SPEC_VALUES) CHECK_EQ(value_of(m, v.macro), v.value);
    for (const SpecPart& p : SPEC_PARTS) {
        const auto it = m.find(p.macro);
        CHECK(it != m.end());
        unsigned off = 0;
        char tail[8];
        CHECK(sscanf(it->second.c_str(), "(PERIPHERAL_BASE(1) + 0x%x)%1s", &off, tail) == 1);
        CHECK_EQ(off, p.offset);
    }
    CHECK(m.count("PERIPHERAL_BASE(n)") && m.at("PERIPHERAL_BASE(n)") == "((0x03 + (n)) << 24)");                       // 5.1
    CHECK(m.count("PERIPHERAL_WINDOW_BASE(n)") && m.at("PERIPHERAL_WINDOW_BASE(n)") == "(0xFFFFF800 + 0x200 * (n))");   // 5.2
    CHECK(m.count("OFF_COUNTER(n)") && m.at("OFF_COUNTER(n)") == "(0x140 + 4 * (n))");                                  // 5.5
    CHECK(m.count("X0REL(off)") && m.at("X0REL(off)") == "((off) - 0x800)");
}

// The RTL firmware's own definitions (fw/tile/tile_map.h; fw/rtl/src, the chip's Rust firmware), for the registers and
// bits they share with mesh.h.
TEST(the_rtl_firmwares_definitions_agree) {
    // the tile programs' tile_map.h: a register is its x0-relative address, (-0x800 + offset)
    const std::string tile_map = slurp("fw/tile/tile_map.h");
    CHECK(tile_map != "<cannot read>");
    const auto t = defines(tile_map);
    int found = 0;
    for (const SpecReg& r : SPEC_TILE) {
        if (!t.count(r.name)) continue;
        found++;
        CHECK_EQ(value_of(t, r.name) + 0x800u, r.offset);
    }
    CHECK_EQ(found, 31);   // every register of the tile window but the seven counters, which it reaches by COUNTER(n)
    CHECK(t.count("COUNTER(n)") && t.at("COUNTER(n)") == "(-0x6C0 + 4 * (n))");
    const auto m = defines(regs::mesh_h());
    // its bits and values, under the names mesh.h uses too
    found = 0;
    for (const char* name : {"STATUS_ARRIVED", "STATUS_DMA_BUSY", "STATUS_GATHER_BUSY", "STATUS_REQUEST_FULL", "EVENT_RELEASE", "EVENT_DMA_DONE",
                             "EVENT_ALL_COUNTERS_DONE", "EVENT_REQUEST_PENDING", "ERROR_ROUTER_OVERFLOW", "ERROR_BAD_PACKET",
                             "ERROR_COUNTER_UNDERFLOW", "ERROR_DRIFT", "ERROR_GATHER_OVERFLOW", "ERROR_READ_OVERFLOW", "ERROR_EARLY_ARRIVAL",
                             "ERROR_ACCESS_FAULT", "ERROR_ILLEGAL_INSTRUCTION", "ERROR_RECEIVE_UNDERFLOW", "SIZE_32", "SIZE_64", "SIZE_128",
                             "MODE_WRITE", "MODE_APPEND", "MODE_BUCKET", "MODE_AUGMENT", "MODE_GATHER", "MODE_COUNT", "DMEM_BASE", "IMEM_BASE",
                             "SCRATCHPAD_BASE", "DMEM_RESERVED", "HIST_BASE", "PTR_BASE", "STREAM_SRC_ADDR_BASE", "STREAM_DEST_ADDR_BASE",
                             "STREAM_HDR_BASE", "GROUP_BASE", "OP_REPORT"}) {
        CHECK(t.count(name) == 1);
        CHECK_EQ(value_of(t, name), value_of(m, name));
        found++;
    }
    CHECK_EQ(found, 38);
    CHECK_EQ(value_of(t, "RESET_PC"), value_of(m, "BOOT_ROM_BASE"));
    CHECK(t.count("EVENT_PERIPHERAL(n)") && t.at("EVENT_PERIPHERAL(n)") == "(0x8 << (n))");       // bit 3 + n, as mesh.h's
    CHECK(t.count("EVENT_COUNTER_DONE(n)") && t.at("EVENT_COUNTER_DONE(n)") == "(0x40 << (n))");   // bit 6 + n
    CHECK(t.count("HEADER(mode, counter, size)") && t.at("HEADER(mode, counter, size)") == "((mode) << 5 | (counter) << 2 | (size))");
    CHECK_EQ(value_of(t, "HDR_DST"), unsigned(regs::field("HDR", "dst").lo));

    // the chip firmware's src/lib.rs: `pub const NAME: usize = 0x...;` an offset in the tile window, P_NAME in a peripheral's
    const std::string lib = slurp("fw/rtl/src/rtl/chip/test/programs/firmware/src/lib.rs");
    CHECK(lib != "<cannot read>");
    auto rust = [&](const std::string& name, uint32_t& v) {
        const std::string key = "pub const " + name + ": usize = ";
        const size_t p = lib.find(key);
        if (p == std::string::npos) return false;
        std::string s = lib.substr(p + key.size(), lib.find(';', p) - p - key.size());
        for (size_t u; (u = s.find('_')) != std::string::npos;) s.erase(u, 1);
        return number(s, v);
    };
    found = 0;
    uint32_t v = 0;
    for (const SpecReg& r : SPEC_TILE)
        if (rust(r.name, v)) { found++; CHECK_EQ(v, r.offset); }
    CHECK_EQ(found, 23);   // the registers the chip's firmware uses
    found = 0;
    for (const SpecReg& r : SPEC_PERIPHERAL)
        if (rust(std::string("P_") + r.name, v)) { found++; CHECK_EQ(v, r.offset); }
    CHECK_EQ(found, 7);
    CHECK(rust("TILE_WINDOW", v) && v == value_of(m, "TILE_WINDOW_BASE"));
    CHECK(rust("STREAM_SRC_ADDR", v) && v == value_of(m, "STREAM_SRC_ADDR_BASE"));
    CHECK(rust("STREAM_DEST_ADDR", v) && v == value_of(m, "STREAM_DEST_ADDR_BASE"));
    CHECK(rust("STREAM_HDR", v) && v == value_of(m, "STREAM_HDR_BASE"));
}

// mesh.h compiles, and its structs put every register where the specification does: a C file of the committed header
// and one _Static_assert per register, written from the lists above, through the host C compiler.
TEST(mesh_h_compiles_with_the_specifications_struct_offsets) {
    std::string c = "#include \"fw/include/mesh.h\"\n";
    auto asserts = [&](const char* type, const SpecReg* list, int n) {
        for (int k = 0; k < n; k++)
            c += regs::fmt("_Static_assert(__builtin_offsetof(%s, %s) == 0x%03X, \"%s.%s\");\n", type, list[k].name, list[k].offset, type, list[k].name);
        c += regs::fmt("_Static_assert(sizeof(%s) == 0x200, \"%s\");\n", type, type);
    };
    asserts("TileRegs", SPEC_TILE, count_of(SPEC_TILE));
    asserts("PeripheralRegs", SPEC_PERIPHERAL, count_of(SPEC_PERIPHERAL));
    const char* types[] = {"IngressEgressRegs", "RwEngineRegs", "TracerRegs", "ReplayerRegs"};
    for (int k = 0; k < count_of(SPEC_PERIPHERALS); k++) {
        const SpecPeripheral& p = SPEC_PERIPHERALS[k];
        asserts(types[k], SPEC_PERIPHERAL, p.dma ? count_of(SPEC_PERIPHERAL) : 7);   // without a DMA, the control block alone
        asserts(types[k], p.regs, p.n);
    }
    c += "_Static_assert(__builtin_offsetof(TableRegs, HIST) == 0x0000 && __builtin_offsetof(TableRegs, PTR) == 0x0400 &&\n"
         "               __builtin_offsetof(TableRegs, STREAM_SRC_ADDR) == 0x0800 && __builtin_offsetof(TableRegs, STREAM_DEST_ADDR) == 0x0C00 &&\n"
         "               __builtin_offsetof(TableRegs, STREAM_HDR) == 0x1000 && __builtin_offsetof(TableRegs, GROUP) == 0x1400 &&\n"
         "               sizeof(TableRegs) == 0x1600 && sizeof(((TableRegs *)0)->GROUP[0]) == 8, \"5.2\");\n";
    // the access qualifiers: a store to a read-only register must not compile, so the R members are const
    c += "_Static_assert(__builtin_types_compatible_p(__typeof__(((TileRegs *)0)->PARAMS), const volatile uint32_t), \"R\");\n";
    c += "_Static_assert(__builtin_types_compatible_p(__typeof__(((TileRegs *)0)->ARRIVE), volatile uint32_t), \"W\");\n";
    const char* path = "build/test_regs_mesh_h.c";
    FILE* f = fopen(path, "wb");
    CHECK(f != nullptr);
    fwrite(c.data(), 1, c.size(), f);
    CHECK(fclose(f) == 0);
    const std::string cmd = std::string("${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -fsyntax-only -I. ") + path + " 2> build/test_regs_mesh_h.log";
    const int rc = system(cmd.c_str());
    if (rc != 0) fprintf(stderr, "%s failed: %s", cmd.c_str(), slurp("build/test_regs_mesh_h.log").c_str());
    CHECK_EQ(rc, 0);
}

}  // namespace
