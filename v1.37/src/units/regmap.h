// regmap.h: the address map, region 0xFF and the registers, as v1.36's specification gives them (machine-spec
// chapters 5, 7-13 and 17.1; the RTL's rtl/tile/base/spec.md, the same numbers). Constant data only; shared read-only by
// every unit. Every name is the specification's.
#pragma once

#include <cstdint>

namespace units {

// ---- the address map (5.1): an 8-bit region [31:24] and a 24-bit byte offset ---------------------------------------
enum : uint32_t {
    R_DMEM = 0x00, R_IMEM = 0x01, R_BOOTROM = 0x02, R_SCRATCHPAD = 0x03,
    R_PERIPH_1 = 0x04, R_PERIPH_2 = 0x05, R_PERIPH_3 = 0x06, R_REGS = 0xFF,
    MEM_BYTES = 0x8000,                          // IMEM_BYTES and DMEM_BYTES, 32 KiB each (19)
    MEM_WORDS = MEM_BYTES / 4,
    IMEM_BASE = 0x01000000,
    BOOTROM_BASE = 0x02000000,                   // the reset PC
    BOOT_ROM_BYTES = 256, BOOTROM_MAX_WORDS = BOOT_ROM_BYTES / 4,
    DMEM_FIRST = 0x10,                           // DMEM's reserved first 16 bytes (6.3)
};
inline uint32_t region(uint32_t a) { return a >> 24; }
inline uint32_t offset(uint32_t a) { return a & 0xFFFFFF; }

// ---- region 0xFF (5.2): 16 windows of 512 bytes from 0xFFFF_E000, picked by address bits [12:9] ---------------------
enum : uint32_t {
    REGS_BASE = 0xFFFFE000,                      // the mapped top 8 KiB; everything below it in region 0xFF is unmapped
    WINDOW_BYTES = 0x200,
    HIST_BASE = 0xFFFFE000,                      // windows 0, 1
    PTR_BASE = 0xFFFFE400,                       // windows 2, 3
    STREAM_SRC_ADDR_BASE = 0xFFFFE800,           // windows 4, 5
    STREAM_DEST_ADDR_BASE = 0xFFFFEC00,          // windows 6, 7
    STREAM_HDR_BASE = 0xFFFFF000,                // windows 8, 9
    GROUP_BASE = 0xFFFFF400,                     // window 10: GROUP[g] is two words, at +8g and +8g + 4
    TABLE_END = 0xFFFFF600,                      // window 11 is unmapped
    TILE_WINDOW_BASE = 0xFFFFF800,               // window 12
    PERIPH_WINDOW_BASE = 0xFFFFFA00,             // windows 13-15: peripheral n at 0xFFFF_F800 + 0x200 n
    WINDOW_TILE = 12, WINDOW_UNMAPPED = 11,
    WINDOW_WORDS = 128, BLOCK_WORDS = 16, BLOCKS = 8,
    WINDOW_LOAD_LATENCY = 2, TABLE_LOAD_LATENCY = 1,   // 6.4
};
inline bool in_regs(uint32_t a) { return a >= REGS_BASE; }                         // region 0xFF's mapped part
inline unsigned window_index(uint32_t a) { return (a >> 9) & 15; }                 // 0-15, for an address in_regs
inline bool in_tables(uint32_t a) { return a >= REGS_BASE && a < TABLE_END; }      // windows 0-10: loads take 1 cycle
inline bool in_windows(uint32_t a) { return a >= TILE_WINDOW_BASE; }               // windows 12-15: loads take 2 cycles
inline bool in_tile_window(uint32_t a) { return a >= TILE_WINDOW_BASE && a < PERIPH_WINDOW_BASE; }
inline unsigned periph_of(uint32_t a) { return a >= PERIPH_WINDOW_BASE ? window_index(a) - WINDOW_TILE : 0; }   // 1-3, or 0
inline unsigned window_word(uint32_t a) { return (a & (WINDOW_BYTES - 1)) >> 2; }  // 0-127: block << 4 | word
inline unsigned block_of(unsigned window_word) { return window_word >> 4; }
inline unsigned word_of(unsigned window_word) { return window_word & 15; }

// ---- the tile window (5.5), by window word: block b's word w is 16 b + w -------------------------------------------
enum Reg : unsigned {
    // block 0, tile_control_unit (7.1)
    PARAMS = 0, STATUS = 1, ERRORS = 2, EVENTS = 3, EVENTS_PEEK = 4, EVENTS_MASK = 5, ARRIVE = 6,
    // block 1, dma_unit (8.1)
    DMA_CONFIG = 16, DMA_START = 17,
    // block 2, write_unit (9)
    WRITE_HDR = 32, WRITE_DEST_ADDR = 33, WRITE_W0 = 34, WRITE_W1 = 35, WRITE_W2 = 36,
    WRITE_SEND_32 = 37, WRITE_SEND_64 = 38, WRITE_SEND_128 = 39,
    // block 3, read_unit (10)
    READ_HDR = 48, READ_DEST_ADDR = 49, READ_SEND = 50, READ_FREE = 51,
    // block 4, request_unit (11.1)
    REQUEST_OUT_W0 = 64, REQUEST_OUT_W1 = 65, REQUEST_OUT_W2 = 66, REQUEST_SEND = 67,
    REQUEST_IN_W0 = 68, REQUEST_IN_W1 = 69, REQUEST_IN_W2 = 70, REQUEST_RECEIVE = 71,
    // block 5, receive_unit (12.1)
    RECEIVE_CONFIG = 80, COUNTER_1 = 81, COUNTER_7 = 87,
    // block 6, gather_unit (13)
    GATHER_CONFIG = 96,
    // block 7 is unused: an access fault
    TILE_REGS = 97,                              // one past the last defined word
};
// What a word is for a load and for a store (5.4): R a store is an access fault; W a load reads 0; RW both.
enum Access : uint8_t { A_NONE = 0, A_R = 1, A_W = 2, A_RW = 3 };
struct RegInfo { const char* name; Access access; };
// Indexed by window word. An undefined word has no name and A_NONE: an access fault both ways (5.3).
inline const RegInfo& reg_info(unsigned i) {
    static constexpr RegInfo kNone{nullptr, A_NONE};
    static constexpr RegInfo b0[7] = {{"PARAMS", A_R}, {"STATUS", A_R}, {"ERRORS", A_R}, {"EVENTS", A_R},
                                      {"EVENTS_PEEK", A_R}, {"EVENTS_MASK", A_RW}, {"ARRIVE", A_W}};
    static constexpr RegInfo b1[2] = {{"DMA_CONFIG", A_RW}, {"DMA_START", A_W}};
    static constexpr RegInfo b2[8] = {{"WRITE_HDR", A_RW}, {"WRITE_DEST_ADDR", A_RW}, {"WRITE_W0", A_W}, {"WRITE_W1", A_W},
                                      {"WRITE_W2", A_W}, {"WRITE_SEND_32", A_W}, {"WRITE_SEND_64", A_W}, {"WRITE_SEND_128", A_W}};
    static constexpr RegInfo b3[4] = {{"READ_HDR", A_RW}, {"READ_DEST_ADDR", A_RW}, {"READ_SEND", A_W}, {"READ_FREE", A_R}};
    static constexpr RegInfo b4[8] = {{"REQUEST_OUT_W0", A_W}, {"REQUEST_OUT_W1", A_W}, {"REQUEST_OUT_W2", A_W}, {"REQUEST_SEND", A_W},
                                      {"REQUEST_IN_W0", A_R}, {"REQUEST_IN_W1", A_R}, {"REQUEST_IN_W2", A_R}, {"REQUEST_RECEIVE", A_R}};
    static constexpr RegInfo b5[8] = {{"RECEIVE_CONFIG", A_RW}, {"COUNTER_1", A_RW}, {"COUNTER_2", A_RW}, {"COUNTER_3", A_RW},
                                      {"COUNTER_4", A_RW}, {"COUNTER_5", A_RW}, {"COUNTER_6", A_RW}, {"COUNTER_7", A_RW}};
    static constexpr RegInfo b6[1] = {{"GATHER_CONFIG", A_RW}};
    const unsigned b = i >> 4, w = i & 15;
    switch (b) {
    case 0: return w < 7 ? b0[w] : kNone;
    case 1: return w < 2 ? b1[w] : kNone;
    case 2: return w < 8 ? b2[w] : kNone;
    case 3: return w < 4 ? b3[w] : kNone;
    case 4: return w < 8 ? b4[w] : kNone;
    case 5: return w < 8 ? b5[w] : kNone;
    case 6: return w < 1 ? b6[w] : kNone;
    default: return kNone;
    }
}

// ---- a peripheral's window (17.1), by window word -------------------------------------------------------------------
enum PeriphReg : unsigned {
    P_PARAMS = 0, P_STATUS = 1, P_ERRORS = 2, P_EVENTS = 3, P_EVENTS_PEEK = 4, P_EVENTS_MASK = 5, P_START = 6,
    P_DMA_HDR = 16, P_DMA_DEST_ADDR = 17, P_DMA_START = 18,      // block 1; without a DMA every word of it reads 0
    P_OWN = 32,                                                  // blocks 2-7: the peripheral's own words, 32-127
};
enum : uint32_t {
    P_ST_BUSY = 0, P_ST_DMA_BUSY = 1,                            // a peripheral's STATUS
    P_EV_DONE = 0, P_EV_DMA_DONE = 1,                            // a peripheral's EVENTS
    PT_NONE = 0, PT_INGRESS_EGRESS = 1, PT_RW_ENGINE = 2, PT_TRACER_TYPE = 3, PT_REPLAYER_TYPE = 4,   // PARAMS.type
};

// ---- header registers (5.4): the header block [7:0], dst [14:8], the register's own fields [31:15] ------------------
enum : uint32_t {
    HDR_SIZE_LO = 0, HDR_COUNTER_LO = 2, HDR_MODE_LO = 5, HDR_DST_LO = 8, HDR_OWN_LO = 15,
    HDR_BLOCK_MASK = 0xFF, HDR_DST_MASK = 0x7F,
    WRITE_HDR_NO_INCREMENT = 1u << 15,                            // 9
    // a peripheral's DMA_HDR (17.1): count [26:15], queue [28:27]
    DMA_HDR_COUNT_LO = 15, DMA_HDR_COUNT_MASK = 0xFFF, DMA_HDR_QUEUE_LO = 27, DMA_HDR_QUEUE_MASK = 3,
    DMA_HDR_BITS = 0x1FFFFFFC,                                   // what DMA_HDR stores: [28:2]; its size [1:0] is the DMA's (5.4)
    // STREAM_HDR (8.3): count [31:15]
    STREAM_HDR_COUNT_LO = 15, STREAM_HDR_COUNT_MASK = 0x1FFFF,
    WORD_ADDRESS_MASK = 0x1FFFFF,                                 // a 21-bit scratchpad word address (5.4)
};
inline uint32_t hdr_size(uint32_t h) { return h & 3; }
inline uint32_t hdr_counter(uint32_t h) { return h >> HDR_COUNTER_LO & 7; }
inline uint32_t hdr_mode(uint32_t h) { return h >> HDR_MODE_LO & 7; }
inline uint32_t hdr_dst(uint32_t h) { return h >> HDR_DST_LO & HDR_DST_MASK; }

// ---- the units' own fields ------------------------------------------------------------------------------------------
enum : uint32_t {
    // DMA_CONFIG (8.1): kind [0], start [8:1], count [17:9], gap [20:18]
    DMA_KIND_LO = 0, DMA_START_LO = 1, DMA_START_MASK = 0xFF, DMA_COUNT_LO = 9, DMA_COUNT_MASK = 0x1FF,
    DMA_GAP_LO = 18, DMA_GAP_MASK = 7, DMA_CONFIG_BITS = (1u << 21) - 1,
    // GROUP (8.4): word 0 rotations [17:0], first_stream [25:18], streams - 1 [29:26]; word 1 gap [7:0]
    GROUP_ROTATIONS_MASK = 0x3FFFF, GROUP_FIRST_LO = 18, GROUP_FIRST_MASK = 0xFF, GROUP_STREAMS_LO = 26,
    GROUP_STREAMS_MASK = 0xF, GROUP_W0_BITS = (1u << 30) - 1, GROUP_GAP_MASK = 0xFF,
    // RECEIVE_CONFIG (12.1): bucket_digit [2:0], histogram_digit [5:3]
    RECEIVE_BUCKET_LO = 0, RECEIVE_HIST_LO = 3, RECEIVE_CONFIG_BITS = 0x3F,
    COUNTER_BITS = 20, COUNTER_MASK = 0xFFFFF, HIST_BITS = 17, HIST_MASK = 0x1FFFF,
    // GATHER_CONFIG (13): record_base [20:0], value_size (k - 1) [23:21]
    GATHER_VALUE_SIZE_LO = 21, GATHER_VALUE_SIZE_MASK = 7, GATHER_CONFIG_BITS = (1u << 24) - 1,
    READ_FREE_MASK = 0xFFFF,                                      // 10
    // a request's word 0 (11.2, 11.5): requester [6:0], target [9:7], op [17:10], the operation's own [31:18]
    REQ_REQUESTER_MASK = 0x7F, REQ_TARGET_LO = 7, REQ_TARGET_MASK = 7, REQ_OP_LO = 10, REQ_OP_MASK = 0xFF,
    REQ_TARGET_MASTER = 0, REQ_TARGET_MEMORY = 1, REQ_TARGET_TRACER = 2,
    REQ_OP_REPORT = 1, REQ_OP_LOAD = 3, REQ_REPORT_NUMBER_LO = 18,   // the master tile's (11.5)
    REQ_OP_READ = 1, REQ_OP_WRITE = 2,                               // the memory tile's
};

// ---- the tile's EVENTS, STATUS, ERRORS and PARAMS (7) ---------------------------------------------------------------
enum : uint32_t {
    EV_RELEASE = 0, EV_DMA_DONE = 1, EV_ALL_COUNTERS_DONE = 2, EV_REQUEST_PENDING = 3, EV_PERIPH_1 = 4,
    EV_COUNTER_1_DONE = 7,                                        // counter n's at bit 6 + n
    EVENTS_VALID = 0x3FFF,
    EVENTS_LIVE = 1u << EV_REQUEST_PENDING | 7u << EV_PERIPH_1,
    EVENTS_MASK_RESET = 1u << EV_RELEASE | 1u << EV_ALL_COUNTERS_DONE,          // 0x5 (18)
    COUNTER_EVENTS = 0x7Fu << EV_COUNTER_1_DONE | 1u << EV_ALL_COUNTERS_DONE,   // what ARRIVE clears (7.2)
    ST_ARRIVED = 0, ST_DMA_BUSY = 1, ST_GATHER_BUSY = 2, ST_REQUEST_FULL = 3,
    ERR_ROUTER_OVERFLOW = 0, ERR_BAD_PACKET = 1, ERR_COUNTER_UNDERFLOW = 2, ERR_DRIFT = 3, ERR_GATHER_OVERFLOW = 4,
    ERR_READ_OVERFLOW = 5, ERR_EARLY_ARRIVAL = 6, ERR_ACCESS_FAULT = 7, ERR_ILLEGAL_INSTRUCTION = 8,
    ERR_RECEIVE_UNDERFLOW = 9, ERR_PERIPH_1 = 10,                 // peripheral n's, live, at bit 9 + n
    ERRORS_VALID = 0x1FFF, ERRORS_LIVE = 7u << ERR_PERIPH_1,
};
inline uint32_t ev_counter_done(unsigned n) { return 1u << (EV_COUNTER_1_DONE + n - 1); }   // n = 1-7

// PARAMS (7.1): the straps alone.
struct Straps {
    uint32_t tile_id = 0;          // [6:0]
    uint32_t peripherals = 0;      // [9:7], bit n - 1 for peripheral n
    bool receiver = false;         // [10] the request unit's receive side is connected
    uint32_t request_target = 0;   // [13:11] the target it receives for (11.2)
    uint32_t attach = 0;           // [16:14] bit 0 the master tile's, 1 the memory tile's, 2 the tracer tile's (16.1)
    bool barrier = false;          // [17] the barrier's nets are wired
    bool boot_local = false;       // [18] the tile starts from the image already in its IMEM (6.5)
};
enum : uint32_t {
    PARAMS_TILE_ID_MASK = 0x7F, PARAMS_PERIPHERALS_LO = 7, PARAMS_RECEIVER = 1u << 10, PARAMS_REQUEST_TARGET_LO = 11,
    PARAMS_ATTACH_LO = 14, PARAMS_BARRIER = 1u << 17, PARAMS_BOOT_LOCAL = 1u << 18,
};
inline uint32_t params_value(const Straps& s) {
    return (s.tile_id & PARAMS_TILE_ID_MASK) | (s.peripherals & 7) << PARAMS_PERIPHERALS_LO
           | (s.receiver ? uint32_t(PARAMS_RECEIVER) : 0u) | (s.request_target & 7) << PARAMS_REQUEST_TARGET_LO
           | (s.attach & 7) << PARAMS_ATTACH_LO | (s.barrier ? uint32_t(PARAMS_BARRIER) : 0u)
           | (s.boot_local ? uint32_t(PARAMS_BOOT_LOCAL) : 0u);
}

// ---- tile ids (2.1, 4.1) at 8 x 8 -----------------------------------------------------------------------------------
enum : uint32_t {
    MESH_SIZE = 8, TILE_COUNT = 64, TILE_ID_BITS = 7,
    ID_MASTER = TILE_COUNT, ID_MEMORY = TILE_COUNT + 1, ID_TRACER = TILE_COUNT + 2,
    ID_BROADCAST = (1u << TILE_ID_BITS) - 2, ID_NONE = (1u << TILE_ID_BITS) - 1,
    ATTACH_MASTER = 0, ATTACH_MEMORY = MESH_SIZE - 1, ATTACH_TRACER = (2 * MESH_SIZE - 1) % TILE_COUNT,   // 0, 7, 15
};

}  // namespace units
