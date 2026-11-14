// regtable.h: the register table fw/include/mesh.h and fw/REGISTERS.md are generated from (regs_gen.h, gen_regs):
// v1.36's map, in the specification's names (spec/machine-spec.md chapters 2, 4, 5, 7-13, 17 and 19;
// spec/jolt/replayer.md 17.8 and spec/jolt/tracer.md 17.9; the RTL's base tile specification has the same numbers).
// Configuration-time data. The simulator's own decode table is src/units/regmap.h; tests/test_regs.cpp checks that the
// two agree, word by word and bit by bit, and this table against the specification's tables written out in the test.
// Every section number in a `doc` is the specification's.
#pragma once

#include <cstdint>

namespace regs {

// ---- the address map (5.1): an 8-bit region [31:24] and a 24-bit byte offset [23:0] ------------------------------------
struct Region { uint32_t lo, hi; const char* macro; const char* contents; const char* core; const char* packet; };
constexpr Region REGIONS[] = {
    {0x00, 0x00, "DMEM_BASE", "DMEM, `DMEM_BYTES`; its first 16 bytes reserved (6.3)", "load, store", "write"},
    {0x01, 0x01, "IMEM_BASE", "IMEM, `IMEM_BYTES`", "fetch; a load or store is an access fault", "write"},
    {0x02, 0x02, "BOOT_ROM_BASE", "the boot ROM, `BOOT_ROM_BYTES`; the reset PC is its base (6.5)",
     "fetch; a load or store is an access fault", "bad packet"},
    {0x03, 0x03, "SCRATCHPAD_BASE", "the scratchpad, `SCRATCHPAD_BYTES`: word address *w* is byte offset 4 *w* (5.4)",
     "none (an access fault): through the read unit and packets", "write"},
    {0x04, 0x06, "PERIPHERAL_BASE(n)", "peripherals 1, 2, 3: region 0x03 + *n* (17.1)",
     "none (an access fault): through their windows", "write, if present"},
    {0x07, 0xFE, nullptr, "unmapped", "access fault", "bad packet"},
    {0xFF, 0xFF, nullptr, "the windows: the tables, the tile's and the peripherals' (section 2)", "load, store, whole words",
     "bad packet"},
};
constexpr uint32_t DMEM_BASE = 0x00000000, IMEM_BASE = 0x01000000, BOOT_ROM_BASE = 0x02000000, SCRATCHPAD_BASE = 0x03000000;
constexpr uint32_t peripheral_base(int n) { return (0x03u + uint32_t(n)) << 24; }
constexpr uint32_t DMEM_RESERVED = 16;      // 6.3: the first byte an image may use

// ---- region 0xFF (5.2, 5.3): 16 windows of 512 bytes from 0xFFFF_E000; a register window is 8 blocks of 16 words ------
constexpr uint32_t WINDOWS_BASE = 0xFFFFE000, WINDOW_BYTES = 0x200, WINDOW_WORDS = 128, BLOCK_WORDS = 16, BLOCKS = 8,
                   TILE_WINDOW_BASE = 0xFFFFF800, TABLE_END = 0xFFFFF600;
constexpr uint32_t peripheral_window_base(int n) { return TILE_WINDOW_BASE + WINDOW_BYTES * uint32_t(n); }

enum Access : int { R = 1, W = 2, RW = 3 };

// the tables (5.2, 8.3, 8.4, 12.5): entry i at base + 4 i; GROUP[g] at + 8 g and + 8 g + 4
struct Table { const char* name; uint32_t base; int entries, words; int bits; const char* section; const char* doc; };
constexpr Table TABLES[] = {
    {"HIST", 0xFFFFE000, 256, 1, 17, "12.5", "[16:0] the count"},
    {"PTR", 0xFFFFE400, 256, 1, 21, "12.5", "[20:0] a word address"},
    {"STREAM_SRC_ADDR", 0xFFFFE800, 256, 1, 21, "8.3", "[20:0] the source: the first element's word address, aligned to the size"},
    {"STREAM_DEST_ADDR", 0xFFFFEC00, 256, 1, 32,
     "8.3", "[31:0] `WRITE` only: the first element's byte address at the receiver; each element advances it by the size"},
    {"STREAM_HDR", 0xFFFFF000, 256, 1, 32, "8.3", "the header block [7:0], `dst` [14:8], `count` [31:15]"},
    {"GROUP", 0xFFFFF400, 64, 2, 38,
     "8.4", "word 0: `rotations` [17:0], `first_stream` [25:18], `streams` [29:26]; word 1: `gap` [7:0]"},
};
constexpr int N_TABLES = sizeof(TABLES) / sizeof(TABLES[0]);

// ---- registers: `index` is the window word, 16 block + word (5.3); the offset from the window's base is 4 index -------
struct Reg { const char* name; int index; int access; const char* section; const char* reset; const char* doc; };
constexpr bool readable(const Reg& r) { return (r.access & R) != 0; }
constexpr bool writable(const Reg& r) { return (r.access & W) != 0; }

// the tile window (5.5), at 0xFFFF_F800
constexpr Reg TILE[] = {
    // block 0, tile_control_unit (7.1)
    {"PARAMS", 0, R, "7.1", "the straps", "the straps alone: tile_id, peripherals, receiver, request_target, attach, barrier, boot_local"},
    {"STATUS", 1, R, "7.1", "0", "the states: arrived [0], dma_busy [1], gather_busy [2], request_full [3]"},
    {"ERRORS", 2, R, "7.4", "0", "one bit per error, sticky until the reset; the peripherals' bits live"},
    {"EVENTS", 3, R, "7.2", "0", "one bit per event; a load consumes one count from each counted bit it returns set that EVENTS_MASK selects"},
    {"EVENTS_PEEK", 4, R, "7.2", "0", "the same value, consuming nothing"},
    {"EVENTS_MASK", 5, RW, "7.2", "0x5: release, all_counters_done", "the events that wake the core and that a load of EVENTS consumes"},
    {"ARRIVE", 6, W, "7.3", "-", "a store arrives at the barrier: sets STATUS.arrived; UB where PARAMS.barrier is 0"},
    // block 1, dma_unit (8.1)
    {"DMA_CONFIG", 16, RW, "8.1", "undefined", "the transfer: kind [0], start [8:1], count [17:9], gap [20:18]"},
    {"DMA_START", 17, W, "8.2", "-", "a store starts the transfer DMA_CONFIG describes: clears EVENTS.dma_done, sets STATUS.dma_busy"},
    // block 2, write_unit (9)
    {"WRITE_HDR", 32, RW, "9", "undefined", "the header block [7:0], its size the send's; dst [14:8], an absolute tile id; no_increment [15]"},
    {"WRITE_DEST_ADDR", 33, RW, "9", "undefined", "the next WRITE's address; advances by the size at each WRITE sent, unless no_increment"},
    {"WRITE_W0", 34, W, "9", "0", "payload bits [31:0]; cleared by each send"},
    {"WRITE_W1", 35, W, "9", "0", "payload bits [63:32]; cleared by each send"},
    {"WRITE_W2", 36, W, "9", "0", "payload bits [95:64]; cleared by each send"},
    {"WRITE_SEND_32", 37, W, "9", "-", "the top word, payload bits [31:0]; sends a 32-bit packet"},
    {"WRITE_SEND_64", 38, W, "9", "-", "the top word, payload bits [63:32]; sends a 64-bit packet, WRITE_W0 below"},
    {"WRITE_SEND_128", 39, W, "9", "-", "the top word, payload bits [127:96]; sends a 128-bit packet, WRITE_W0-W2 below"},
    // block 3, read_unit (10)
    {"READ_HDR", 48, RW, "10", "undefined", "the header block [7:0]: the next read's size and counter; its mode and dst are the unit's"},
    {"READ_DEST_ADDR", 49, RW, "10", "undefined", "the next read's address in DMEM; advances by the size at each READ_SEND"},
    {"READ_SEND", 50, W, "10", "-", "[20:0] a word address: queues a read of READ_HDR's size from it"},
    {"READ_FREE", 51, R, "10", "READ_QUEUE_DEPTH", "[15:0] the read queue's free entries"},
    // block 4, request_unit (11.1)
    {"REQUEST_OUT_W0", 64, W, "11.1", "0", "bits [31:0] of the next request; cleared by the push"},
    {"REQUEST_OUT_W1", 65, W, "11.1", "0", "bits [63:32] of the next request; cleared by the push"},
    {"REQUEST_OUT_W2", 66, W, "11.1", "0", "bits [95:64] of the next request; cleared by the push"},
    {"REQUEST_SEND", 67, W, "11.1", "-", "bits [127:96]; sends the request (stalls while the request queue is full and no entry leaves)"},
    {"REQUEST_IN_W0", 68, R, "11.1", "0 (empty)", "bits [31:0] of the oldest received request; 0 on an empty queue"},
    {"REQUEST_IN_W1", 69, R, "11.1", "0 (empty)", "bits [63:32] of the oldest received request"},
    {"REQUEST_IN_W2", 70, R, "11.1", "0 (empty)", "bits [95:64] of the oldest received request"},
    {"REQUEST_RECEIVE", 71, R, "11.1", "0 (empty)", "bits [127:96], and pops it; on an empty queue 0, and ERRORS.receive_underflow"},
    // block 5, receive_unit (12.1, 12.2)
    {"RECEIVE_CONFIG", 80, RW, "12.1", "undefined", "bucket_digit [2:0], histogram_digit [5:3]"},
    {"COUNTER_1", 81, RW, "12.2", "0", "[19:0] the count; a store of N adds N, and N > 0 arms the counter"},
    {"COUNTER_2", 82, RW, "12.2", "0", "counter 2"},
    {"COUNTER_3", 83, RW, "12.2", "0", "counter 3"},
    {"COUNTER_4", 84, RW, "12.2", "0", "counter 4"},
    {"COUNTER_5", 85, RW, "12.2", "0", "counter 5"},
    {"COUNTER_6", 86, RW, "12.2", "0", "counter 6"},
    {"COUNTER_7", 87, RW, "12.2", "0", "counter 7"},
    // block 6, gather_unit (13)
    {"GATHER_CONFIG", 96, RW, "13", "undefined", "record_base [20:0], value_size [23:21]"},
};
constexpr int N_TILE = sizeof(TILE) / sizeof(TILE[0]);
// the module that owns each block of the tile window, and its chapter (5.3); block 7 is unused
constexpr const char* TILE_BLOCK[] = {"tile_control_unit", "dma_unit", "write_unit", "read_unit", "request_unit", "receive_unit",
                                      "gather_unit", nullptr};
constexpr int TILE_BLOCK_CHAPTER[] = {7, 8, 9, 10, 11, 12, 13, 0};

// a peripheral's window (5.5, 17.1), at 0xFFFF_F800 + 0x200 n: the control block and the DMA block
constexpr Reg PERIPHERAL[] = {
    {"PARAMS", 0, R, "17.1", "the peripheral's constant", "type [7:0], version [15:8], the peripheral's constants [31:16]"},
    {"STATUS", 1, R, "17.1", "0", "busy [0], dma_busy [1]; above, the peripheral's"},
    {"ERRORS", 2, R, "17.1", "0", "the peripheral's errors, sticky until the reset (Ingress/Egress's tile_error is live)"},
    {"EVENTS", 3, R, "17.1", "0", "done [0], dma_done [1]; above, the peripheral's; a load consumes (7.2)"},
    {"EVENTS_PEEK", 4, R, "17.1", "0", "the same value, consuming nothing"},
    {"EVENTS_MASK", 5, RW, "17.1", "0", "the events that count toward the tile's EVENTS.peripheral_n and that a load consumes"},
    {"START", 6, W, "17.2", "-", "a store starts the peripheral's work; while STATUS.busy is 1, UB"},
    {"DMA_HDR", 16, RW, "17.4", "undefined", "the header block [7:0], its size the DMA's; dst [14:8]; count [26:15]; queue [28:27]"},
    {"DMA_DEST_ADDR", 17, RW, "17.4", "undefined", "the DMA's destination address, 16-byte aligned; advances by 16 at each WRITE"},
    {"DMA_START", 18, W, "17.4", "-", "a store starts the DMA's transfer; during a transfer, UB"},
};
constexpr int N_PERIPHERAL = sizeof(PERIPHERAL) / sizeof(PERIPHERAL[0]);
constexpr int PERIPHERAL_OWN = 32;   // words 32-127, blocks 2-7: the peripheral's own

// Ingress/Egress's words (17.6): the master tile's peripheral 1; no work, no DMA, no writes
constexpr Reg INGRESS_EGRESS[] = {
    {"ERROR_LINES_0", 32, R, "17.6", "0", "bit b: the error line of tile b (0-31)"},
    {"ERROR_LINES_1", 33, R, "17.6", "0", "bit b: the error line of tile 32 + b"},
    {"ERROR_LINES_2", 34, R, "17.6", "0", "bit b: the error line of tile 64 + b: [1] the memory tile's, [2] the tracer tile's; [0], the master tile's own, reads 0"},
    {"ARRIVALS_0", 35, R, "17.6", "0", "bit b: compute tile b has arrived"},
    {"ARRIVALS_1", 36, R, "17.6", "0", "bit b: compute tile 32 + b has arrived"},
    {"ARRIVE_EXPECT_0", 37, RW, "17.6", "0", "as ARRIVALS_0: the compute tiles the master tile waits for"},
    {"ARRIVE_EXPECT_1", 38, RW, "17.6", "0", "as ARRIVALS_1"},
    {"RELEASE", 39, W, "17.6", "-", "a store with bit 0 set releases the barrier: clears ARRIVALS, restarts the watchdog"},
    {"WATCHDOG", 40, RW, "17.6", "0 (off)", "a store sets the limit and the count, 0 stops it; a load returns the count"},
    {"LED", 41, RW, "17.6", "0", "[0] the led pin"},
    {"UART_TX", 42, W, "17.6", "-", "[7:0] a byte to send; stalls while the TX FIFO is full"},
    {"UART_RX", 43, R, "17.6", "0 (empty)", "[7:0] the oldest byte received, [8] valid; pops it"},
    {"UART_DIVISOR", 44, RW, "17.6", "UART_RESET_DIVISOR", "[15:0] the cycles per bit; 0 is UB"},
    {"UART_STATUS", 45, RW, "17.6", "0", "the TX FIFO's bytes [4:0], the RX FIFO's [12:8], rx_overrun [16], framing_error [17]; a store clears each error it writes 1"},
    {"STREAM_OUT_W0", 64, W, "17.6", "0", "word 0 of the next beat out; cleared by the push"},
    {"STREAM_OUT_W1", 65, W, "17.6", "0", "word 1 of the next beat out"},
    {"STREAM_OUT_W2", 66, W, "17.6", "0", "word 2 of the next beat out"},
    {"STREAM_SEND", 67, W, "17.6", "-", "the beat's top word; pushes the beat, TLAST 0; stalls while the transmit FIFO is full"},
    {"STREAM_SEND_LAST", 68, W, "17.6", "-", "the beat's top word; pushes the beat, TLAST 1"},
    {"STREAM_IN_W0", 69, R, "17.6", "0 (empty)", "word 0 of the oldest beat in"},
    {"STREAM_IN_W1", 70, R, "17.6", "0 (empty)", "word 1 of the oldest beat in"},
    {"STREAM_IN_W2", 71, R, "17.6", "0 (empty)", "word 2 of the oldest beat in"},
    {"STREAM_RECEIVE", 72, R, "17.6", "0 (empty)", "the oldest beat's top word; pops it"},
};
// the RW engine's words (17.7): the memory tile's peripheral 1; its DMA sends output queues 0 (a read's beats) and 1 (the reply)
constexpr Reg RW_ENGINE[] = {
    {"RW_CONFIG", 32, RW, "17.7", "undefined", "kind [0]: 0 a read, 1 a write; beats [10:1]"},
    {"RW_LPDDR_ADDR", 33, RW, "17.7", "undefined", "the first beat's LPDDR beat address (a byte address over 16)"},
    {"RW_BUFFER_ADDR", 34, RW, "17.7", "undefined", "a write's first beat in the write buffer: its offset over 16"},
    {"RW_REPLY_W0", 35, W, "17.7", "0", "the next reply's bits [31:0]; cleared by the push"},
    {"RW_REPLY_SEND", 36, W, "17.7", "-", "the reply's bits [63:32]; pushes the reply into output queue 1"},
};
// the replayer's words (17.8.1): every compute tile's peripheral 1; no DMA
constexpr Reg REPLAYER[] = {
    {"RUN_CONFIG", 32, RW, "17.8.1", "0", "rows [23:0], store_log [24], rd_prev [25], tracer_compression [28:26] (held, no effect here)"},
    {"ROWS_RUN", 33, R, "17.8.1", "0", "[23:0] the rows of this pass completed"},
    {"EXPANSION_OFFSET", 34, R, "17.8.1", "0", "[15:0] the row offset at which the last posted core-fed expansion starts"},
    {"EXPANSION_OPCODE", 35, R, "17.8.1", "0", "the code value of the instruction awaiting a core-fed expansion"},
};
// the Tracer's words (17.9.1): the tracer tile's peripheral 1; words 32-35 the replayer's; its DMA sends output queues 0-3
constexpr Reg TRACER[] = {
    {"RUN_CONFIG", 32, RW, "17.9.1", "0", "rows [23:0] (0-65,536), store_log [24] and rd_prev [25] (held, no effect here), tracer_compression [28:26]"},
    {"ROWS_RUN", 33, R, "17.9.1", "0", "[23:0] the rows of this run traced"},
    {"EXPANSION_OFFSET", 34, R, "17.9.1", "0", "[15:0] the row offset at which the last posted core-fed expansion starts"},
    {"EXPANSION_OPCODE", 35, R, "17.9.1", "0", "the code value of the instruction awaiting a core-fed expansion"},
    {"STATE_LENGTH", 36, R, "17.9.1", "68", "[15:0] output queue 0's length in entries: 68, a constant"},
    {"CODE_LENGTH", 37, R, "17.9.1", "0", "[15:0] the code stream's 128-bit words; 0 from START until the run's rows end"},
    {"READ_LOG_LENGTH", 38, R, "17.9.1", "0", "[15:0] the read log's"},
    {"WRITE_LOG_LENGTH", 39, R, "17.9.1", "0", "[15:0] the write log's"},
    {"HASH_MUL", 40, RW, "17.9.1", "0", "p1, the map's multiplier, odd"},
    {"HASH_STEP", 41, RW, "17.9.1", "0", "[23:0] p3, the probe's step, odd"},
    {"HASH_LINES", 42, RW, "17.9.1", "0", "[4:0] k: the map has 2^k lines"},
    {"EXPANSION_TEMPLATE", 43, W, "17.9.1", "-", "template [6:0], a core-fed template's number; valid [31]"},
    {"STAT_CYCLES", 44, R, "17.9.8", "0", "a statistics counter (17.9.8)"},
    {"STAT_STALL_FETCH", 45, R, "17.9.8", "0", "a statistics counter"},
    {"STAT_STALL_DATA", 46, R, "17.9.8", "0", "a statistics counter"},
    {"STAT_STALL_MAP", 47, R, "17.9.8", "0", "a statistics counter"},
    {"STAT_STALL_OUTPUT", 48, R, "17.9.8", "0", "a statistics counter"},
    {"STAT_MISS_I", 49, R, "17.9.8", "0", "a statistics counter"},
    {"STAT_MISS_D", 50, R, "17.9.8", "0", "a statistics counter"},
    {"STAT_PROBES", 51, R, "17.9.8", "0", "a statistics counter"},
};

// the peripherals (17.1's types): which tile has it as peripheral 1, its C struct and pointer, its bit macros' prefix
struct Peripheral {
    const char* name; int type; const char* section; const char* tile; bool dma;
    const char* c_struct; const char* c_pointer; const char* prefix; const Reg* regs; int n_regs;
};
constexpr Peripheral PERIPHERALS[] = {
    {"Ingress/Egress", 1, "17.6", "the master tile", false, "IngressEgressRegs", "ingress_egress", "IE", INGRESS_EGRESS,
     sizeof(INGRESS_EGRESS) / sizeof(Reg)},
    {"the RW engine", 2, "17.7", "the memory tile", true, "RwEngineRegs", "rw_engine", "RW", RW_ENGINE,
     sizeof(RW_ENGINE) / sizeof(Reg)},
    {"the Tracer", 3, "17.9", "the tracer tile", true, "TracerRegs", "tracer", "TR", TRACER, sizeof(TRACER) / sizeof(Reg)},
    {"the replayer", 4, "17.8", "every compute tile", false, "ReplayerRegs", "replayer", "RP", REPLAYER,
     sizeof(REPLAYER) / sizeof(Reg)},
};
constexpr int N_PERIPHERALS = sizeof(PERIPHERALS) / sizeof(PERIPHERALS[0]);

// the register at window word i of a list, or nullptr: an undefined word, an access fault both ways (5.3)
inline const Reg* reg_at(const Reg* list, int n, int i) {
    for (int k = 0; k < n; k++) if (list[k].index == i) return &list[k];
    return nullptr;
}
inline bool same(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return !*a && !*b; }
inline const Reg* reg_named(const Reg* list, int n, const char* name) {
    for (int k = 0; k < n; k++) if (same(list[k].name, name)) return &list[k];
    return nullptr;
}

// ---- fields (5.4: packed from bit 0; the bits above the last are reserved) ---------------------------------------------
// `word` is the register, table word or layout the field is in; `macro` what mesh.h makes of it:
//   SET   WORD_FIELD(v): the field's value in place, for a word software builds
//   GET   WORD_FIELD(w): the field, from a word software reads
//   BOTH  SET, and WORD_FIELD_OF(w), the field from the word
//   FLAG  WORD_FIELD: the one-bit field's mask
//   DOC   none: REGISTERS.md alone
enum Macro : int { SET, GET, BOTH, FLAG, DOC };
struct Field {
    const char* word; const char* name; int lo, width; int macro; const char* doc;
    constexpr uint32_t mask() const { return width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1; }
};
constexpr Field FIELDS[] = {
    // the packet (4.1): 256 bits a link; the defined bits are [181:0] at 8 x 8
    {"PACKET", "payload", 0, 128, DOC, "32, 64 or 128 bits, by size, from bit 0; the bits above the size are 0"},
    {"PACKET", "address", 128, 32, DOC, "the receiver's address (5.1); mode WRITE uses it, every other mode carries 0"},
    {"PACKET", "size", 160, 2, DOC, "the payload's width: 0 32 bits, 1 64, 2 128; 3 reserved"},
    {"PACKET", "counter", 162, 3, DOC, "the counter the packet decrements at the receiver (12.2): 0 none, 1-7 COUNTER_1-7"},
    {"PACKET", "mode", 165, 3, DOC, "what the receiver does (4.2)"},
    {"PACKET", "src", 168, 7, DOC, "the sender's tile id, stamped by the injecting hardware"},
    {"PACKET", "dst", 175, 7, DOC, "the destination's tile id"},
    // the header registers' layout (5.4): WRITE_HDR, READ_HDR, STREAM_HDR, a peripheral's DMA_HDR
    {"HDR", "size", 0, 2, BOTH, "the size: 0 32 bits, 1 64, 2 128 (ignored where the unit fills it: WRITE_HDR, a peripheral's DMA_HDR)"},
    {"HDR", "counter", 2, 3, BOTH, "the counter: 0 none, 1-7 COUNTER_1-7 at the receiver"},
    {"HDR", "mode", 5, 3, BOTH, "the mode (4.2) (ignored in READ_HDR)"},
    {"HDR", "dst", 8, 7, BOTH, "dst, a tile id (ignored in READ_HDR)"},
    {"WRITE_HDR", "no_increment", 15, 1, FLAG, "WRITE_DEST_ADDR stays as it is after a send, until the program clears the bit"},
    {"STREAM_HDR", "count", 15, 17, BOTH, "elements, 0 to 2^17 - 1"},
    {"DMA_HDR", "count", 15, 12, BOTH, "128-bit packets, 0-4095"},
    {"DMA_HDR", "queue", 27, 2, BOTH, "the peripheral's output queue"},
    // the tile's PARAMS (7.1)
    {"PARAMS", "tile_id", 0, 7, GET, "the tile id"},
    {"PARAMS", "peripherals", 7, 3, GET, "bit n - 1 for peripheral n"},
    {"PARAMS", "receiver", 10, 1, GET, "1 if the request unit's receive side is connected"},
    {"PARAMS", "request_target", 11, 3, GET, "the target it receives for (11.2)"},
    {"PARAMS", "attach", 14, 3, GET, "the special tiles attached here: bit 0 the master tile's, 1 the memory tile's, 2 the tracer tile's; on a special tile its own"},
    {"PARAMS", "barrier", 17, 1, GET, "1 if the barrier's nets are wired"},
    {"PARAMS", "boot_local", 18, 1, GET, "1 if the tile starts from the image already in its IMEM (6.5)"},
    // the units' registers (8.1, 10, 12.1, 12.2, 13)
    {"DMA_CONFIG", "kind", 0, 1, BOTH, "0 a stream transfer, 1 a group transfer"},
    {"DMA_CONFIG", "start", 1, 8, BOTH, "the first stream (0-255), or the first group (0-63)"},
    {"DMA_CONFIG", "count", 9, 9, BOTH, "streams (1-256), or groups (1-64); 0 an empty transfer"},
    {"DMA_CONFIG", "gap", 18, 3, BOTH, "stream transfers only: idle cycles after each stream (0-7)"},
    {"READ_SEND", "address", 0, 21, DOC, "a scratchpad word address, aligned to the size"},
    {"READ_FREE", "free", 0, 16, DOC, "the read queue's free entries"},
    {"RECEIVE_CONFIG", "bucket_digit", 0, 3, BOTH, "0 none, 1-4 byte d of the payload's low 32 bits, 1 the least significant; 5-7 reserved"},
    {"RECEIVE_CONFIG", "histogram_digit", 3, 3, BOTH, "count-on-arrival's digit, as bucket_digit"},
    {"COUNTER", "count", 0, 20, DOC, "the count, COUNTER_BITS bits"},
    {"GATHER_CONFIG", "record_base", 0, 21, BOTH, "the word address of the values' first record, aligned to a row (128 bits)"},
    {"GATHER_CONFIG", "value_size", 21, 3, BOTH, "k - 1, k the 64-bit records in a value (1-8)"},
    // the tables' words (8.3, 8.4, 12.5)
    {"HIST", "count", 0, 17, DOC, "the count, HIST_BITS bits"},
    {"PTR", "address", 0, 21, DOC, "a scratchpad word address"},
    {"STREAM_SRC_ADDR", "address", 0, 21, DOC, "the first element's word address"},
    {"GROUP_W0", "rotations", 0, 18, BOTH, "R, the rotations, at most 2^18 - 1"},
    {"GROUP_W0", "first_stream", 18, 8, BOTH, "the group's first stream"},
    {"GROUP_W0", "streams", 26, 4, BOTH, "t - 1, t the streams in the group (1-16)"},
    {"GROUP_W1", "gap", 0, 8, BOTH, "idle cycles after the group (0-255)"},
    // a request's word 0 (11.2, 11.5)
    {"REQUEST", "requester", 0, 7, GET, "the sender's tile id, written by the tree; software writes 0"},
    {"REQUEST", "target", 7, 3, BOTH, "the receiver: 0 the master tile, 1 the memory tile, 2 the tracer tile; 3-7 dropped at the root"},
    {"REQUEST", "op", 10, 8, BOTH, "the operation; 0 is reserved"},
    {"REQUEST", "own", 18, 14, BOTH, "the operation's own fields"},
    // the special tiles' requests (11.5): the operation's own fields, at their place in the word
    {"REPORT", "number", 18, 8, BOTH, "word 0: which data words the report carries; 24 and 25 are the boot ROM's"},
    {"MEMORY", "beats", 18, 10, BOTH, "word 0 of READ and WRITE: the beats, 0-1023"},
    {"MEMORY", "data_counter", 28, 3, BOTH, "word 0 of READ: the requester's counter that counts the data"},
    {"MEMORY_W3", "reply_addr", 0, 24, BOTH, "word 3: the reply's byte offset in the requester's DMEM"},
    {"MEMORY_W3", "reply_counter", 24, 3, BOTH, "word 3: the requester's counter that counts the reply; 0 for no reply"},
    {"MEMORY_REPLY_W1", "counter", 0, 3, GET, "a WRITE's reply, bits [34:32]: the memory tile's counter, armed, that counts the part's packets"},
    // a peripheral's PARAMS (17.1)
    {"P_PARAMS", "type", 0, 8, GET, "the peripheral's type"},
    {"P_PARAMS", "version", 8, 8, GET, "its version"},
    {"P_PARAMS", "constants", 16, 16, GET, "the peripheral's constants"},
    // Ingress/Egress (17.6)
    {"LED", "led", 0, 1, DOC, "the led pin"},
    {"UART_TX", "byte", 0, 8, DOC, "a byte to send"},
    {"UART_RX", "byte", 0, 8, GET, "the oldest byte received"},
    {"UART_RX", "valid", 8, 1, FLAG, "1 with a byte; 0 with the RX FIFO empty"},
    {"UART_DIVISOR", "divisor", 0, 16, DOC, "the cycles per bit"},
    {"UART_STATUS", "tx_bytes", 0, 5, GET, "the TX FIFO's bytes"},
    {"UART_STATUS", "rx_bytes", 8, 5, GET, "the RX FIFO's bytes"},
    {"UART_STATUS", "rx_overrun", 16, 1, FLAG, "a frame ended with the RX FIFO full; a store of 1 clears it"},
    {"UART_STATUS", "framing_error", 17, 1, FLAG, "a frame's stop bit was 0; a store of 1 clears it"},
    // the RW engine (17.7)
    {"RW_CONFIG", "kind", 0, 1, BOTH, "0 a read, 1 a write"},
    {"RW_CONFIG", "beats", 1, 10, BOTH, "the beats to move, 0-1023"},
    {"RW_LPDDR_ADDR", "address", 0, 28, DOC, "[LPDDR_ADDRESS_BITS - 5 : 0], 28 bits at 32: the first beat's LPDDR beat address"},
    {"RW_BUFFER_ADDR", "address", 0, 10, DOC, "[log2 RW_BUFFER_BYTES - 5 : 0], 10 bits at 16 KiB: the first beat's offset over 16"},
    // the replayer and the Tracer (17.8.1, 17.9.1)
    {"RUN_CONFIG", "rows", 0, 24, BOTH, "the rows the next pass or run does"},
    {"RUN_CONFIG", "store_log", 24, 1, FLAG, "the replayer's: the pass uses the write log (17.8.3)"},
    {"RUN_CONFIG", "rd_prev", 25, 1, FLAG, "the replayer's: the rows carry rd_prev (17.8.11)"},
    {"RUN_CONFIG", "tracer_compression", 26, 3, BOTH, "the Tracer's: [26] the code stream, [27] the read log, [28] the write log may go LRU-coded (17.9.6)"},
    {"ROWS_RUN", "rows", 0, 24, DOC, "the rows completed"},
    {"EXPANSION_OFFSET", "offset", 0, 16, DOC, "a row offset, 0-3,120"},
    {"HASH_STEP", "step", 0, 24, DOC, "p3"},
    {"HASH_LINES", "k", 0, 5, DOC, "the map has 2^k lines"},
    {"EXPANSION_TEMPLATE", "template", 0, 7, SET, "a core-fed template's number"},
    {"EXPANSION_TEMPLATE", "valid", 31, 1, FLAG, "the number is valid"},
    // the Tracer's files word, the record's word 66 (17.9.2): 128 bits
    {"FILES", "memory_root", 0, 31, DOC, "the root of the guest's memory file, in 128-byte units"},
    {"FILES", "memory_depth", 31, 2, DOC, "0-3"},
    {"FILES", "map_root", 33, 31, DOC, "the root of the jump-target map's file"},
    {"FILES", "map_depth", 64, 2, DOC, "0-3"},
    {"FILES", "log_k", 66, 6, DOC, "log2 K, K the proof's RAM in 8-byte cells, 0-32"},
    {"FILES", "io_size", 72, 31, DOC, "0x8000_0000 - lowest_address, in bytes"},
};
constexpr int N_FIELDS = sizeof(FIELDS) / sizeof(FIELDS[0]);
inline const Field& field(const char* word, const char* name) {
    for (const Field& f : FIELDS) if (same(f.word, word) && same(f.name, name)) return f;
    return FIELDS[0];   // unreachable for the names the generators and the test use
}

// ---- the bits of STATUS, EVENTS and ERRORS (7.1, 7.2, 7.4, 17.1, 17.6-17.9) ---------------------------------------------
// A bit's macro in mesh.h is its group and its name: STATUS_ARRIVED, EVENT_RELEASE, ERROR_DRIFT, RP_EVENT_CODE_CREDIT.
// `n` > 1: n bits from `bit`, one per counter or peripheral, the macro taking the number: EVENT_COUNTER_DONE(n).
struct Bit { const char* group; int bit, n; const char* name; const char* kind; const char* doc; };
constexpr Bit BITS[] = {
    // the tile's STATUS (7.1)
    {"STATUS", 0, 1, "arrived", "state", "set by ARRIVE; cleared by the release, which wins, and the reset (7.3)"},
    {"STATUS", 1, 1, "dma_busy", "state", "the DMA unit's transfer is in progress (8.2)"},
    {"STATUS", 2, 1, "gather_busy", "state", "the gather queue holds a request, or a request's last read is not yet taken (13)"},
    {"STATUS", 3, 1, "request_full", "state", "the request queue is full (11.1)"},
    // the tile's EVENTS (7.2)
    {"EVENT", 0, 1, "release", "counted, 1", "BARRIER_RELEASE (7.3)"},
    {"EVENT", 1, 1, "dma_done", "counted, 1", "the end of the tile's transfer (8.2); also cleared by DMA_START"},
    {"EVENT", 2, 1, "all_counters_done", "counted, 1",
     "a decrement that leaves all seven counters at 0 while the event is armed; also cleared by arming any counter, and by ARRIVE"},
    {"EVENT", 3, 1, "request_pending", "live", "a request in the tile's receive queue (11.1)"},
    {"EVENT", 4, 3, "peripheral", "live", "peripheral n's EVENTS & EVENTS_MASK is non-zero (17.2)"},
    {"EVENT", 7, 7, "counter_done", "counted, 1",
     "counter_n_done: a decrement that leaves counter n at 0 while the event is armed (12.2); also cleared by arming counter n, and by ARRIVE"},
    // the tile's ERRORS (7.4)
    {"ERROR", 0, 1, "router_overflow", "P", "router (16.3)"},
    {"ERROR", 1, 1, "bad_packet", "P", "receive_unit (4.2)"},
    {"ERROR", 2, 1, "counter_underflow", "P", "receive_unit (12.2)"},
    {"ERROR", 3, 1, "drift", "P", "dma_unit (8.4)"},
    {"ERROR", 4, 1, "gather_overflow", "P", "gather_unit (13)"},
    {"ERROR", 5, 1, "read_overflow", "P", "read_unit (10)"},
    {"ERROR", 6, 1, "early_arrival", "P", "tile_control_unit (7.3)"},
    {"ERROR", 7, 1, "access_fault", "P (H on a corrupted image)", "core (5.1)"},
    {"ERROR", 8, 1, "illegal_instruction", "P (H on a corrupted image)", "core (6.2)"},
    {"ERROR", 9, 1, "receive_underflow", "P", "request_unit (11.1)"},
    {"ERROR", 10, 3, "peripheral", "live", "peripheral n's ERRORS is non-zero (17.2)"},
    // every peripheral's standard bits (17.1)
    {"P_STATUS", 0, 1, "busy", "state", "the peripheral's work is in progress: its ownership state (17.2)"},
    {"P_STATUS", 1, 1, "dma_busy", "state", "its DMA's transfer is in progress (17.4)"},
    {"P_EVENT", 0, 1, "done", "counted, 1", "the end of the peripheral's work (17.2)"},
    {"P_EVENT", 1, 1, "dma_done", "counted, 1", "the end of its DMA's transfer (17.4)"},
    // Ingress/Egress (17.6)
    {"IE_STATUS", 2, 1, "stream_last", "state", "the TLAST of the oldest beat in; 0 while there is none"},
    {"IE_ERROR", 0, 1, "tile_error", "live", "any bit of ERROR_LINES is 1"},
    {"IE_EVENT", 2, 1, "all_arrived", "live", "(ARRIVALS & ARRIVE_EXPECT) = ARRIVE_EXPECT, and not 0"},
    {"IE_EVENT", 3, 1, "watchdog", "counted, 1", "the watchdog's expiry"},
    {"IE_EVENT", 4, 1, "uart_rx", "live", "a byte in the UART's RX FIFO"},
    {"IE_EVENT", 5, 1, "uart_tx", "live", "room in the UART's TX FIFO"},
    {"IE_EVENT", 6, 1, "stream_in", "live", "a beat in the host interface's receive FIFO"},
    {"IE_EVENT", 7, 1, "stream_out", "live", "room in the host interface's transmit FIFO"},
    // the RW engine (17.7)
    {"RW_ERROR", 0, 1, "lpddr_error", "P or H", "a read beat or a write response that is not OKAY"},
    // the replayer (17.8.1)
    {"RP_EVENT", 2, 1, "code_credit", "counted, 2", "the code queue's credit (17.8.8)"},
    {"RP_EVENT", 3, 1, "read_log_credit", "counted, 2", "the read-log queue's"},
    {"RP_EVENT", 4, 1, "write_log_credit", "counted, 2", "the write-log queue's"},
    {"RP_EVENT", 5, 1, "expansion_credit", "counted, 2", "the expansion queue's"},
    {"RP_EVENT", 6, 1, "new_expansion", "counted, 1", "the pass reached an instruction of decode class 1 or 2, whose expansion it waits for (17.8.6)"},
    {"RP_ERROR", 0, 1, "queue_overrun", "P", "a push into a full queue (17.8.8)"},
    {"RP_ERROR", 1, 1, "invalid_expansion", "P (H on a corrupted table or ROM)", "17.8.10"},
    {"RP_ERROR", 2, 1, "code_underflow", "P (H on a corrupted stream)", "the code stream (17.8.12)"},
    {"RP_ERROR", 3, 1, "read_log_underflow", "P (H on a corrupted stream)", "the read log (17.8.12)"},
    {"RP_ERROR", 4, 1, "write_log_underflow", "P (H on a corrupted stream)", "the write log (17.8.12)"},
    {"RP_ERROR", 5, 1, "leftover", "P (H on a corrupted stream)", "a pass ending with input left over (17.8.10)"},
    // the Tracer (17.9.1)
    {"TR_EVENT", 2, 1, "advice_credit", "counted, 2", "the read queue's credit (17.9.3)"},
    {"TR_EVENT", 3, 1, "expansion_credit", "counted, 2", "the expansion queue's, as the replayer's (17.8.8)"},
    {"TR_EVENT", 4, 1, "new_expansion", "counted, 1", "an instruction of decode class 1 or 2, whose expansion the Tracer waits for (17.9.4)"},
    {"TR_EVENT", 5, 1, "state_ready", "live", "output queue 0 holds a credit (17.9.6)"},
    {"TR_EVENT", 6, 1, "code_ready", "live", "output queue 1 holds a credit"},
    {"TR_EVENT", 7, 1, "read_log_ready", "live", "output queue 2 holds a credit"},
    {"TR_EVENT", 8, 1, "write_log_ready", "live", "output queue 3 holds a credit"},
    {"TR_ERROR", 0, 1, "queue_overrun", "P", "a push into a full read or expansion queue (17.9.3)"},
    {"TR_ERROR", 1, 1, "invalid_expansion", "P (H on a corrupted table, ROM or guest)", "17.9.4"},
    {"TR_ERROR", 2, 1, "advice_underflow", "P (H on corrupted advice)", "an advice row with the advice field at 0 (17.9.4)"},
    {"TR_ERROR", 3, 1, "map_miss", "H (P on a bad map)", "a jump target not in the map (17.9.4)"},
    {"TR_ERROR", 4, 1, "store_not_writable", "H (P on a bad file)", "a store to a line that is not writable (17.9.5)"},
    {"TR_ERROR", 5, 1, "page_missing", "H (P on a bad file)", "a fill beyond its file's depth, or a hole in the map (17.9.5)"},
    {"TR_ERROR", 6, 1, "out_of_range", "H (P on bad advice or a bad guest)", "a memory row outside the provable range (17.9.5)"},
    {"TR_ERROR", 7, 1, "failed_assertion", "H (P on bad advice)", "a VirtualAssert row whose condition is false (17.9.4)"},
    {"TR_ERROR", 8, 1, "misaligned", "H (P on a bad guest)", "a memory row whose address is misaligned (17.9.5)"},
    {"TR_ERROR", 9, 1, "write_back_error", "H", "a write-back's response error (17.9.5)"},
};
constexpr int N_BITS = sizeof(BITS) / sizeof(BITS[0]);
inline const Bit& bit(const char* group, const char* name) {
    for (const Bit& b : BITS) if (same(b.group, group) && same(b.name, name)) return b;
    return BITS[0];   // unreachable for the names the generators and the test use
}
constexpr uint32_t EVENTS_MASK_RESET = 1u << 0 | 1u << 2;   // 18: release and all_counters_done

// ---- values: sizes, modes, tile ids, targets, operations, peripheral types, parameters ---------------------------------
struct Value { const char* group; const char* macro; uint32_t value; const char* doc; };
constexpr Value VALUES[] = {
    // sizes (4.1)
    {"SIZE", "SIZE_32", 0, "32 bits"}, {"SIZE", "SIZE_64", 1, "64 bits"}, {"SIZE", "SIZE_128", 2, "128 bits; 3 is reserved"},
    // modes (4.2)
    {"MODE", "MODE_WRITE", 0, "writes the payload at the address: DMEM or IMEM, the scratchpad, or a peripheral's region"},
    {"MODE", "MODE_APPEND", 1, "writes the payload in the scratchpad at PTR[src], then steps PTR[src] by the payload's size"},
    {"MODE", "MODE_BUCKET", 2, "as APPEND, at PTR[t], t the payload's bucket digit"},
    {"MODE", "MODE_AUGMENT", 3, "writes one 64-bit record at PTR[src]: the payload's low 32 bits and its ordinal; 32 or 64 bits only"},
    {"MODE", "MODE_GATHER", 4, "a gather request, to the tile itself only; never counted: its counter field passes to the values"},
    {"MODE", "MODE_COUNT", 5, "writes nothing; counted by its counter field; 6 and 7 are reserved"},
    // tile ids (2.1, 4.1), at 8 x 8
    {"TILE", "TILE_MASTER", 64, "the master tile: TILE_COUNT"},
    {"TILE", "TILE_MEMORY", 65, "the memory tile: TILE_COUNT + 1"},
    {"TILE", "TILE_TRACER", 66, "the tracer tile: TILE_COUNT + 2"},
    {"TILE", "TILE_BROADCAST", 126, "broadcast: valid as a dst only; reaches every compute tile"},
    {"TILE", "TILE_NONE", 127, "none: as src, an idle packet"},
    {"TILE", "ATTACH_MASTER", 0, "the master tile's attach tile"},
    {"TILE", "ATTACH_MEMORY", 7, "the memory tile's: MESH_SIZE - 1"},
    {"TILE", "ATTACH_TRACER", 15, "the tracer tile's: (2 MESH_SIZE - 1) mod TILE_COUNT"},
    // DMA_CONFIG.kind (8.1) and RW_CONFIG.kind (17.7)
    {"DMA_KIND", "DMA_KIND_STREAM", 0, "DMA_CONFIG.kind: a stream transfer"},
    {"DMA_KIND", "DMA_KIND_GROUP", 1, "DMA_CONFIG.kind: a group transfer"},
    {"RW_KIND", "RW_KIND_READ", 0, "RW_CONFIG.kind: a read, from LPDDR into output queue 0"},
    {"RW_KIND", "RW_KIND_WRITE", 1, "RW_CONFIG.kind: a write, from the write buffer to LPDDR"},
    // the request's targets (11.2) and the special tiles' operations (11.5)
    {"TARGET", "TARGET_MASTER", 0, "the master tile"}, {"TARGET", "TARGET_MEMORY", 1, "the memory tile"},
    {"TARGET", "TARGET_TRACER", 2, "the tracer tile; targets 3-7 are reserved: dropped at the root"},
    {"OP", "OP_REPORT", 1, "the master tile's: a report, no reply; word 0 [25:18] its number, words 1-3 its data"},
    {"OP", "OP_LOAD", 3, "the master tile's: the boot ROM's, the tile waits for its next image (6.5)"},
    {"OP", "OP_READ", 1, "the memory tile's: beats of LPDDR to the requester"},
    {"OP", "OP_WRITE", 2, "the memory tile's: asks for a part of the write buffer"},
    {"OP", "REPORT_ERRORS", 24, "the boot ROM's handler's first report: words 1-3 ERRORS, mcause, mepc (6.5)"},
    {"OP", "REPORT_MTVAL", 25, "its second: words 1-3 mtval, STATUS, ra"},
    {"OP", "LOAD_COUNTER", 7, "the counter the boot ROM arms for the load, which the load's COUNT drains (6.5)"},
    // peripheral types, PARAMS.type (17.1)
    {"PTYPE", "PERIPHERAL_TYPE_NONE", 0, "none"},
    {"PTYPE", "PERIPHERAL_TYPE_INGRESS_EGRESS", 1, "Ingress/Egress (17.6)"},
    {"PTYPE", "PERIPHERAL_TYPE_RW_ENGINE", 2, "the RW engine (17.7)"},
    {"PTYPE", "PERIPHERAL_TYPE_TRACER", 3, "the Tracer (17.9)"},
    {"PTYPE", "PERIPHERAL_TYPE_REPLAYER", 4, "the replayer (17.8)"},
    // the parameters software needs (19), at their defaults
    {"PARAM", "MESH_SIZE", 8, "the mesh's side"},
    {"PARAM", "TILE_COUNT", 64, "the compute tiles: MESH_SIZE squared"},
    {"PARAM", "TILE_ID_BITS", 7, "a tile id's width"},
    {"PARAM", "IMEM_BYTES", 0x8000, "IMEM, 32 KiB"},
    {"PARAM", "DMEM_BYTES", 0x8000, "DMEM, 32 KiB"},
    {"PARAM", "BOOT_ROM_BYTES", 256, "the boot ROM"},
    {"PARAM", "SCRATCHPAD_BYTES", 0x400000, "the scratchpad, 4 MiB"},
    {"PARAM", "WORD_ADDRESS_BITS", 21, "a unit's scratchpad word address"},
    {"PARAM", "WINDOW_LOAD_LATENCY", 2, "a core load from a window, in cycles (6.4)"},
    {"PARAM", "TABLE_LOAD_LATENCY", 1, "a core load from a table"},
    {"PARAM", "COUNTER_BITS", 20, "a counter"},
    {"PARAM", "HIST_BITS", 17, "a HIST count"},
    {"PARAM", "TOKEN_COUNT", 64, "the read tokens"},
    {"PARAM", "GATHER_QUEUE_DEPTH", 64, "the gather queue: TOKEN_COUNT"},
    {"PARAM", "READ_QUEUE_DEPTH", 64, "the read unit's queue: at least 64; READ_FREE says"},
    {"PARAM", "REQUEST_QUEUE_DEPTH", 4, "the request unit's queue"},
    {"PARAM", "RECEIVE_QUEUE_DEPTH_MASTER", 128, "the master tile's receive queue (11.3): a tile has at most 1 request outstanding"},
    {"PARAM", "RECEIVE_QUEUE_DEPTH_MEMORY", 256, "the memory tile's: at most 3 outstanding"},
    {"PARAM", "RECEIVE_QUEUE_DEPTH_TRACER", 128, "the tracer tile's: at most 1 outstanding"},
    {"PARAM", "STREAM_BYTES", 4, "a host-interface beat's TDATA, in bytes (17.6)"},
    {"PARAM", "STREAM_DEPTH", 4, "each host-interface FIFO's beats"},
    {"PARAM", "UART_FIFO_DEPTH", 16, "each UART FIFO's bytes"},
    {"PARAM", "LPDDR_ADDRESS_BITS", 32, "an LPDDR byte address (17.7)"},
    {"PARAM", "RW_QUEUE_BEATS", 16, "the RW engine's output queue 0"},
    {"PARAM", "RW_BUFFER_BYTES", 0x4000, "the RW engine's write buffer, 16 KiB"},
    {"PARAM", "REPLAY_QUEUE_ENTRIES", 256, "each of the replayer's input queues, in 128-bit entries; a credit is half of it (17.8.8)"},
    {"PARAM", "TRACER_WINDOW_ROWS", 65536, "a run's largest rows (17.9.6)"},
    {"PARAM", "RECORD_WORDS", 68, "a window's record, in 128-bit words (17.8.2)"},
};
constexpr int N_VALUES = sizeof(VALUES) / sizeof(VALUES[0]);
inline uint32_t value(const char* macro) {
    for (const Value& v : VALUES) if (same(v.macro, macro)) return v.value;
    return 0xFFFFFFFFu;   // unreachable for the names the generators and the test use
}

// ---- the peripherals' regions (17.7, 17.8.9, 17.9.2): byte offsets in region 0x04 ---------------------------------------
struct RegionPart { const char* prefix; const char* macro; uint32_t offset; const char* doc; };
constexpr RegionPart REGION_PARTS[] = {
    {"RW", "RW_BUFFER", 0x000000, "the write buffer, RW_BUFFER_BYTES in rows of 16 bytes; WRITEs of 32, 64 and 128 bits"},
    {"RP", "RP_STATE", 0x000000, "the state word, 16 bytes; its write resets the pass (17.8.8)"},
    {"RP", "RP_STREAMS", 0x000010, "the stream word, 16 bytes"},
    {"RP", "RP_IMAGE", 0x000020, "the register image: image word i at + 16 i, 1 KiB"},
    {"RP", "RP_RECORD_END", 0x000420, "the record's words 66 and 67, 32 bytes: accepted and ignored"},
    {"RP", "RP_DECODE_TABLE", 0x001000, "the decode table, 4 KiB: entries 8 r to 8 r + 7 at + 16 r"},
    {"RP", "RP_ROM", 0x002000, "the ROM, 8 KiB: word j at + 16 j"},
    {"RP", "RP_QUEUE_CODE", 0x100000, "part 1: the code queue's pushes"},
    {"RP", "RP_QUEUE_READ_LOG", 0x200000, "part 2: the read-log queue's pushes"},
    {"RP", "RP_QUEUE_WRITE_LOG", 0x300000, "part 3: the write-log queue's pushes"},
    {"RP", "RP_QUEUE_EXPANSION", 0x400000, "part 4: the expansion queue's pushes"},
    {"TR", "TR_RECORD", 0x000000, "the record, 68 words of 16 bytes: word w at + 16 w"},
    {"TR", "TR_DECODE_TABLE", 0x001000, "the decode table, 4 KiB, laid out as the replayer's"},
    {"TR", "TR_ROM", 0x002000, "the ROM, 8 KiB: word j at + 16 j"},
    {"TR", "TR_QUEUE_ADVICE", 0x200000, "part 2: the read queue's pushes, the segment's advice"},
    {"TR", "TR_QUEUE_EXPANSION", 0x400000, "part 4: the expansion queue's pushes"},
};
constexpr int N_REGION_PARTS = sizeof(REGION_PARTS) / sizeof(REGION_PARTS[0]);

}  // namespace regs
