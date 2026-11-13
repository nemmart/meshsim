// regmap.h: the controller's address map and register block, as `ref` decodes them (sim/ref/meshsim/src/Registers.java,
// LinkWord.region/offset). Constant data only; shared read-only by every core.
#pragma once

#include <cstdint>

namespace units {

// ---- regions and windows (Registers.java: the address map) ------------------------------------------------------------
enum : uint32_t {
    R_DMEM = 0x00, R_PMEM = 0x01, R_BOOTROM = 0x02,
    MEM_BYTES = 0x8000,                          // data and program memory, 32 KB each (ControllerMemories.BYTES)
    MEM_WORDS = MEM_BYTES / 4,
    BOOTROM_BASE = 0x02000000, BOOTROM_MAX_WORDS = 64,
    DMEM_FIRST = 0x10,                           // data memory's reserved first 16 bytes (a store there is flagged)
    TABLE_BASE = 0xFFFF2000, PTR_BASE = 0xFFFF2400, TABLE_END = 0xFFFF3600,
    REG_BASE = 0xFFFFF800, BLOCK_WORDS = 128,
    WINDOW1_BASE = REG_BASE + 0x200,
};
inline uint32_t region(uint32_t a) { return a >> 24; }
inline uint32_t offset(uint32_t a) { return a & 0xFFFFFF; }
inline bool in_regs(uint32_t a) { return a >= REG_BASE; }                      // the block and the windows: 2-cycle loads
inline bool in_windows(uint32_t a) { return a >= WINDOW1_BASE; }
inline unsigned window_of(uint32_t a) { return in_windows(a) ? (a - REG_BASE) >> 9 : 0; }   // 1-3, or 0
inline bool in_tables(uint32_t a) { return a >= TABLE_BASE && a < TABLE_END; }

// ---- the block (Registers.java: the controller's block). A word index has a load register and a store register. -------
enum Reg : unsigned {
    PARAMS = 0, STATUS = 1, ERRORS = 2, PULSE_DONE = 3, PULSE_DMA = 4, EVENTS = 6, EVENTS_PEEK = 7, EVENTS_MASK = 8,
    EVENTS_REQUIRED = 9, ROUTER_HDR = 10, ROUTER_ADDR = 11, ROUTER_SET_W0 = 12, ROUTER_SET_W1 = 13, ROUTER_SET_W2 = 14,
    ROUTER_SEND_32 = 15, ROUTER_SEND_64 = 16, ROUTER_SEND_128 = 17,
    REQUEST_SET_W0 = 18, REQUEST_SET_W1 = 19, REQUEST_SET_W2 = 20, REQUEST_SEND = 21,      // stores
    REQUEST_GET_W0 = 18, REQUEST_GET_W1 = 19, REQUEST_GET_W2 = 20, REQUEST_RECEIVE = 21,  // loads, the same words
    DMA_CONFIG = 22, DMA_RECORD_BASE_ADDRESS = 23, WRITE_COUNTER_1 = 24, WRITE_COUNTER_7 = 30,
    SRAM_RD_ADDR_QUEUE = 31, SRAM_RD_DEST_ADDR = 32, SIM_EXIT = 126, TRACE = 127,
};
// What a word holds for a load or a store.
enum Access : uint8_t {
    A_NONE = 0,       // no register (a load: write-only or reserved; a store: read-only or reserved)
    A_M1 = 1,         // modelled (M1's subset, and M2's registers)
    A_OUT = 2,        // not modelled yet: fatal, named
};
struct RegInfo { const char* name; Access load, store; };
// Indexed by word. Reserved words (5, 33-125) have no name and A_NONE both ways.
inline const RegInfo& reg_info(unsigned i) {
    static constexpr RegInfo kNone{nullptr, A_NONE, A_NONE};
    static constexpr RegInfo t[33] = {
        {"PARAMS", A_M1, A_NONE}, {"STATUS", A_M1, A_NONE}, {"ERRORS", A_M1, A_NONE}, {"PULSE_DONE", A_NONE, A_M1},
        {"PULSE_DMA", A_NONE, A_M1}, kNone, {"EVENTS", A_M1, A_NONE}, {"EVENTS_PEEK", A_M1, A_NONE},
        {"EVENTS_MASK", A_M1, A_M1}, {"EVENTS_REQUIRED", A_M1, A_M1}, {"ROUTER_HDR", A_M1, A_M1},
        {"ROUTER_ADDR", A_M1, A_M1}, {"ROUTER_SET_W0", A_NONE, A_M1}, {"ROUTER_SET_W1", A_NONE, A_M1},
        {"ROUTER_SET_W2", A_NONE, A_M1}, {"ROUTER_SEND_32", A_NONE, A_M1}, {"ROUTER_SEND_64", A_NONE, A_M1},
        {"ROUTER_SEND_128", A_NONE, A_M1},
        {"REQUEST_SET_W0/REQUEST_GET_W0", A_OUT, A_M1}, {"REQUEST_SET_W1/REQUEST_GET_W1", A_OUT, A_M1},
        {"REQUEST_SET_W2/REQUEST_GET_W2", A_OUT, A_M1}, {"REQUEST_SEND/REQUEST_RECEIVE", A_OUT, A_M1},
        {"DMA_CONFIG", A_M1, A_M1}, {"DMA_RECORD_BASE_ADDRESS", A_M1, A_M1},
        {"WRITE_COUNTER_1", A_M1, A_M1}, {"WRITE_COUNTER_2", A_M1, A_M1}, {"WRITE_COUNTER_3", A_M1, A_M1},
        {"WRITE_COUNTER_4", A_M1, A_M1}, {"WRITE_COUNTER_5", A_M1, A_M1}, {"WRITE_COUNTER_6", A_M1, A_M1},
        {"WRITE_COUNTER_7", A_M1, A_M1}, {"SRAM_RD_ADDR_QUEUE", A_M1, A_M1}, {"SRAM_RD_DEST_ADDR", A_M1, A_M1},
    };
    static constexpr RegInfo kSimExit{"SIM_EXIT", A_NONE, A_M1}, kTrace{"TRACE", A_NONE, A_M1};
    return i < 33 ? t[i] : i == SIM_EXIT ? kSimExit : i == TRACE ? kTrace : kNone;
}
inline bool reserved_word(unsigned i) { return i == 5 || (i >= 33 && i <= 125); }

// ---- events, status, errors (Registers.java, TileControl) ---------------------------------------------------------------
enum : uint32_t {
    EV_COUNTERS_DONE = 7, EV_GO = 8, EV_COMMAND = 9, EV_RUN_DONE = 10, EV_REQUEST_ROOM = 11, EV_REQUEST_PENDING = 12,
    EV_PERIPH_1 = 13,
    EVENTS_VALID = 0xFFFF, EVENTS_LIVE = 1u << EV_REQUEST_PENDING | 7u << EV_PERIPH_1,
    EVENTS_MASK_RESET = 1u << EV_GO | 1u << EV_COUNTERS_DONE,
    COUNTER_EVENTS = 0x7F | 1u << EV_COUNTERS_DONE,
    ST_RD_RUNNING = 0, ST_RD_DONE = 1, ST_GATHER_BUSY = 2, ST_SEND_BUSY = 3, ST_DONE = 4, ST_LANDED = 5, ST_REQUEST_BUSY = 6,
    COND_BUS_ERROR = 9, COND_ILLEGAL = 10,                                       // ERRORS bits (Conditions.java)
    COND_RECV_UNDERFLOW = 14,                                                    // a REQUEST_RECEIVE of an empty queue
};

// PARAMS (TileControl.params): peripherals present [2:0], the tile [26:20], big-SRAM size [31:27].
inline uint32_t params_value(uint32_t periph_present, uint32_t tile, uint32_t mem_chunks) {
    return (periph_present & 7) | (tile & 0x7F) << 20 | (((mem_chunks >> 16) - 1) & 0x1F) << 27;
}

}  // namespace units
