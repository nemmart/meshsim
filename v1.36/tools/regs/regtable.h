// regtable.h: the register table mesh.h and REGISTERS.md are generated from, ported from v1.3's
// src/Registers.java (with the LinkWord and Conditions constants the generators use). Configuration-time data: the
// simulator's own decode table is src/units/regmap.h; tests/test_regs.cpp checks the two agree.
#pragma once

#include <cstdint>

namespace regs {

// ---- the address map (2.2, 4.11)
constexpr uint32_t DMEM_BASE = 0x00000000, DMEM_SIZE = 0x8000, PMEM_BASE = 0x01000000, PMEM_SIZE = 0x8000,
                   BOOTROM_BASE = 0x02000000, REGION_SRAM = 0x03000000;
// data memory's layout (4.12)
constexpr uint32_t DMEM_FIRST = 0x10, COMMAND_BASE = 0x7FC0, COMMAND_SLOTS = 16, REPLY_BASE = 0x7BC0, REPLY_WORDS = 256,
                   STACK_TOP = REPLY_BASE;
// the table window (8.9)
constexpr uint32_t TABLE_BASE = 0xFFFF2000, HIST_BASE = 0xFFFF2000, PTR_BASE = 0xFFFF2400, STREAM_SRC_BASE = 0xFFFF2800,
                   STREAM_DEST_BASE = 0xFFFF2C00, STREAM_COUNT_BASE = 0xFFFF3000, GROUP_BASE = 0xFFFF3400,
                   TABLE_END = 0xFFFF3600;
// the controller's block and the peripheral windows
constexpr uint32_t REG_BASE = 0xFFFFF800, BLOCK_WORDS = 128, WINDOW_WORDS = 128;
constexpr uint32_t window_base(int n) { return REG_BASE + 0x200u * n; }

// ---- the controller's block (8.1)
enum Access : int { R = 1, W = 2, RW = 3 };
struct Reg { const char* name; int index; int access; const char* doc; };
// Registers.java's order (REQUEST_GET_* after REQUEST_SEND: the same words, the load side).
constexpr Reg ALL[] = {
    {"PARAMS", 0, R, "constant: peripherals present [2:0], endpoint id [26:20], big-SRAM size [31:27] (8.2)"},
    {"STATUS", 1, R, "live bits, diagnostic (8.2)"},
    {"ERRORS", 2, R, "the errors (4.13), sticky until the reset; [13:11] live (8.3)"},
    {"PULSE_DONE", 3, W, "store 1: the done pulse (6.3); READY fires when a command has landed (8.6)"},
    {"PULSE_DMA", 4, W, "store 1: start the run in DMA_CONFIG, clearing EVENTS [10]; ignored while a run is running (8.6)"},
    {"EVENTS", 6, R, "pending events; a load returns them all and consumes the sticky ones under EVENTS_MASK (4.12, 8.10)"},
    {"EVENTS_PEEK", 7, R, "what a load of EVENTS would return; consumes nothing"},
    {"EVENTS_MASK", 8, RW, "the events that wake WFI on their own and that a load of EVENTS consumes; [8] | [7] at reset"},
    {"EVENTS_REQUIRED", 9, RW, "the events that wake WFI only all together; 0 at reset"},
    {"ROUTER_HDR", 10, RW, "no-increment [0], counter [21:19], mode [24:22], dst [31:25] of outgoing packets (8.4)"},
    {"ROUTER_ADDR", 11, RW, "the next packet's 32-bit address; + 4, 8 or 16 at each mode-0 send unless no-increment (8.4)"},
    {"ROUTER_SET_W0", 12, W, "payload bits 31-0 of the next packet; cleared by each send"},
    {"ROUTER_SET_W1", 13, W, "payload bits 63-32; cleared by each send"},
    {"ROUTER_SET_W2", 14, W, "payload bits 95-64; cleared by each send"},
    {"ROUTER_SEND_32", 15, W, "payload bits 31-0; sends a 32-bit packet (stalls while the holding register is occupied and not being injected)"},
    {"ROUTER_SEND_64", 16, W, "payload bits 63-32; sends a 64-bit packet with ROUTER_SET_W0 below"},
    {"ROUTER_SEND_128", 17, W, "payload bits 127-96; sends a 128-bit packet with ROUTER_SET_W0-W2 below"},
    {"REQUEST_SET_W0", 18, W, "store: the request's bits 31-0 (requestor [6:0] written 0, target [9:7]); cleared by each send"},
    {"REQUEST_SET_W1", 19, W, "store: the request's bits 63-32; cleared by each send"},
    {"REQUEST_SET_W2", 20, W, "store: the request's bits 95-64; cleared by each send"},
    {"REQUEST_SEND", 21, W, "store: the request's bits 127-96; pushes the request (stalls only while the request FIFO is full)"},
    {"REQUEST_GET_W0", 18, R, "load: the oldest received request's bits 31-0 (requestor [6:0], target [9:7]); doesn't pop; 0 when empty"},
    {"REQUEST_GET_W1", 19, R, "load: its bits 63-32; doesn't pop"},
    {"REQUEST_GET_W2", 20, R, "load: its bits 95-64; doesn't pop"},
    {"REQUEST_RECEIVE", 21, R, "load: its bits 127-96, and pops it; empty: 0 and receive underflow (ERRORS [14])"},
    {"DMA_CONFIG", 22, RW, "the run, bucket and histogram digits, k (8.5); a load returns what was stored"},
    {"DMA_RECORD_BASE_ADDRESS", 23, RW, "the run's record buffer, chunk address [20:0] (4.4, 4.9)"},
    {"WRITE_COUNTER_1", 24, RW, "write counter n (4.12), count [19:0]; a store adds N, N > 0 arms it; a load returns the count"},
    {"WRITE_COUNTER_2", 25, RW, "write counter 2"}, {"WRITE_COUNTER_3", 26, RW, "write counter 3"},
    {"WRITE_COUNTER_4", 27, RW, "write counter 4"}, {"WRITE_COUNTER_5", 28, RW, "write counter 5"},
    {"WRITE_COUNTER_6", 29, RW, "write counter 6"}, {"WRITE_COUNTER_7", 30, RW, "write counter 7"},
    {"SRAM_RD_ADDR_QUEUE", 31, RW, "store: queue a read at a chunk address [20:0], aligned to the size; load: free entries (4.3, 8.8)"},
    {"SRAM_RD_DEST_ADDR", 32, RW, "the read port's setup: data-memory byte offset [16:0] (advancing), size [18:17], counter [21:19] (8.8)"},
    {"SIM_EXIT", 126, W, "simulator only (8.15): a store ends a meshsim test with the stored value"},
    {"TRACE", 127, W, "simulator only (8.15): a store is logged with the cycle and the unit"},
};
constexpr int N_REGS = sizeof(ALL) / sizeof(ALL[0]);
constexpr bool readable(const Reg& r) { return (r.access & R) != 0; }
constexpr bool writable(const Reg& r) { return (r.access & W) != 0; }
// the register a store (load) reaches at block word i, or nullptr
inline const Reg* store_at(int i) { for (const Reg& r : ALL) if (r.index == i && writable(r)) return &r; return nullptr; }
inline const Reg* load_at(int i) { for (const Reg& r : ALL) if (r.index == i && readable(r)) return &r; return nullptr; }
inline const Reg& by_name(const char* n) {
    for (const Reg& r : ALL) { const char *a = r.name, *b = n; while (*a && *a == *b) { a++; b++; } if (!*a && !*b) return r; }
    return ALL[0];   // unreachable for the names the generators use
}
constexpr bool reserved_word(int i) { return i == 5 || (i >= 33 && i <= 125); }

// ---- the standard peripheral window (4.17.4, 8.11)
constexpr const char* STD_WORD[] = {"PARAMS", "STATUS", "ERRORS", "PULSE_RUN", "PULSE_DMA", "EVENTS", "EVENTS_PEEK",
                                    "EVENTS_MASK", "DMA_HDR", "DMA_ADDR"};
constexpr int STD_ACCESS[] = {R, R, R, W, W, R, R, RW, RW, RW};
constexpr int STD_WORDS = 10, STD_RESERVED_END = 32;

// ---- fields (bit positions), in Registers.java's order
struct Field {
    const char* word; const char* name; int lo, width; const char* doc;
    constexpr uint32_t mask() const { return width == 32 ? 0xFFFFFFFFu : (1u << width) - 1; }
};
constexpr Field FIELDS[] = {
    {"PARAMS", "PERIPH_PRESENT", 0, 3, "bit n - 1: peripheral n present"},
    {"PARAMS", "ID", 20, 7, "endpoint id: 0-63 a mesh tile, 64-66 C&C 1-3"},
    {"PARAMS", "SIZE", 27, 5, "big-SRAM size in 256 KB units - 1"},
    {"HDR", "SIZE", 17, 2, "size: 0 32 bits, 1 64, 2 128 (reserved where something else picks the size)"},
    {"HDR", "CTR", 19, 3, "counter: 0 uncounted, 1-7 WRITE_COUNTER_1-7 at the receiver"},
    {"HDR", "MODE", 22, 3, "mode (3.1): 0-5; 6-7 reserved"},
    {"HDR", "DST", 25, 7, "dst (STREAM_COUNT: the relative d)"},
    {"ROUTER_HDR", "NOINC", 0, 1, "no-increment: ROUTER_ADDR stays put after a send"},
    {"DMA_CFG", "KIND", 0, 1, "run kind: 0 stream run, 1 group run"},
    {"DMA_CFG", "START", 1, 8, "first stream (0-255) or first GROUP entry (0-63)"},
    {"DMA_CFG", "COUNT", 9, 9, "streams (1-256) or groups (1-64)"},
    {"DMA_CFG", "BUCKETDIG", 18, 3, "bucket digit (mode 2): 0 none, 1-4 key byte, 1 least significant"},
    {"DMA_CFG", "HISTDIG", 21, 3, "histogram digit (count-on-arrival, modes 1-3): 0 off, 1-4"},
    {"DMA_CFG", "KM1", 24, 3, "gather value size k - 1, in records"},
    {"STREAM_SRC", "CHUNK", 0, 21, "source: element 0's chunk address"},
    {"STREAM_SRC", "GAP", 21, 3, "stream runs: idle cycles after the stream (group runs ignore it)"},
    {"STREAM_COUNT", "N", 0, 17, "count: elements in the stream (0: sends nothing)"},
    {"GROUP_LO", "R", 0, 18, "rotations"},
    {"GROUP_HI", "START", 0, 8, "first stream"},
    {"GROUP_HI", "TM1", 8, 4, "streams in the group - 1"},
    {"GROUP_HI", "GAP", 12, 8, "gap after the group, cycles"},
    {"RD", "DEST", 0, 17, "the next result's byte offset in data memory"},
    {"RECORD_BASE", "CHUNK", 0, 21, "the record buffer's chunk address"},
    {"WRITE_COUNTER", "COUNT", 0, 20, "the count"},
    {"DMA_HDR", "COUNT", 0, 12, "128-bit words to send (v1.3)"},
    {"DMA_HDR", "QUEUE", 12, 2, "the peripheral's output queue (0-3)"},
    {"REQ", "REQUESTOR", 0, 7, "the sender's endpoint id, stamped by the tree (software writes 0)"},
    {"REQ", "TARGET", 7, 3, "which receiver the root delivers the request to"},
};
inline const Field& field(const char* word, const char* name) {
    auto eq = [](const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return !*a && !*b; };
    for (const Field& f : FIELDS) if (eq(f.word, word) && eq(f.name, name)) return f;
    return FIELDS[0];
}

// STATUS (8.2)
constexpr const char* ST_NAME[] = {"RD_RUNNING", "RD_DONE", "GATHER_BUSY", "SEND_BUSY", "DONE", "LANDED", "REQUEST_BUSY"};
constexpr int N_ST = 7;

// ---- events (4.12, 8.10)
constexpr int EV_COUNTERS_DONE = 7, EV_GO = 8, EV_COMMAND = 9, EV_RUN_DONE = 10, EV_REQUEST_ROOM = 11,
              EV_REQUEST_PENDING = 12, EV_PERIPH_1 = 13;
constexpr uint32_t EVENTS_VALID = 0xFFFF, EVENTS_LIVE = 1u << EV_REQUEST_PENDING | 7u << EV_PERIPH_1,
                   EVENTS_MASK_RESET = 1u << EV_GO | 1u << EV_COUNTERS_DONE;

// ---- the errors (Conditions.java): bits 0-14
constexpr const char* COND_NAME[] = {"router overflow", "bad packet", "unexpected arrival", "drift",
    "write-engine input overflow", "table request overflow", "GATHER queue overflow", "read-port error", "done early",
    "bus error", "illegal instruction", "peripheral 1", "peripheral 2", "peripheral 3", "receive underflow"};
constexpr int COND_RECV_UNDERFLOW = 14;

// ---- the link word (LinkWord.java): sizes and modes
constexpr int SIZE_32 = 0, SIZE_64 = 1, SIZE_128 = 2;
constexpr int M_WRITE = 0, M_SRC = 1, M_BUCKET = 2, M_AUG = 3, M_GATHER = 4, M_COUNT = 5;

// ---- software conventions (CONVENTIONS.md)
constexpr int DST_CC1 = 64, DST_CC2 = 65, DST_CC3 = 66, DST_BROADCAST = 126;
constexpr int READ_PORT_DEPTH = 64;

}  // namespace regs
