// axi_port.h: the path to LPDDR as its clients see it (../v1.4_wip/planning/AXI-INTERFACE.md, "AXI" below): a client's
// AXI port to the AXI arbiter, the arbiter's port to the LPDDR controller with its IDs, and the parameters of the
// arbiter and of the simulator's LPDDR model with their defaults. Types and constants only, no logic: what the cache's
// controller, the Updater's controller, the arbiter and the LPDDR model compile against (updater_wip, WORKER5 and
// WORKER6). A dagger in AXI (†) is a proposal the owner has not ruled on; the defaults here follow AXI as it stands.
//
// THE PORT (AXI section 1). AXI4 (ARM IHI 0022), the client the master, v1.36's `lpddr` port's rules (machine-spec 3):
// 128-bit data; a byte address of LPDDR_ADDRESS_BITS; INCR bursts of 16-byte beats (AxSIZE 4), 1 to 256 beats, none
// crossing a 4 KB boundary; every byte of a written beat enabled (WSTRB all ones); RREADY and BREADY 1; no ID signals
// on a client's port (the arbiter gives client c the ID c); no lock, cache, protection, QoS, region or user signals.
// The constants are not carried in the structs below: AxSIZE, AxBURST, WSTRB, RREADY, BREADY.
//
// A CYCLE. Every output of every port is a flip-flop or a constant (machine-spec 3). In cycle t each side's outputs
// are a function of its own registers alone; a valid/ready pair moves an item in t when both are 1 in t's outputs.
// At the edge that ends t, each side takes the other side's outputs of t, never its insides. So a simulator computes
// every unit's outputs of t first (MasterOut, SlaveOut), then steps every unit's edge with them.
#pragma once

#include <cstdint>

namespace units {
namespace axi {

// ---- the bus (AXI section 1)
constexpr unsigned BEAT_BYTES = 16;              // 128-bit data
constexpr unsigned AXSIZE = 4;                   // AxSIZE: 16 bytes a beat, always
constexpr unsigned AXBURST_INCR = 1;             // AxBURST: INCR, always
constexpr unsigned WSTRB_ALL = 0xFFFF;           // WSTRB: whole beats only (†)
constexpr unsigned MAX_BURST_BEATS = 256;        // AxLEN + 1
constexpr unsigned BOUNDARY_BYTES = 4096;        // no burst crosses one
constexpr unsigned LPDDR_ADDRESS_BITS = 32;      // machine-spec 19: 24 to 36, 32 by default

// The sizes in use (AXI, "The sizes in use"), each aligned to its own size (†).
constexpr unsigned LINE_BYTES = 128, LINE_BEATS = 8;
constexpr unsigned HALF_PAGE_BYTES = 2048, HALF_PAGE_BEATS = 128;
constexpr unsigned PAGE_BYTES = 4096, PAGE_BEATS = 256;
// A root, an index entry and a free page are addresses in 128-byte units (UPDATER-INTERFACE.md): byte = unit * 128.
constexpr unsigned UNIT_SHIFT = 7;

// ---- responses: RRESP and BRESP. Anything but OKAY is an error (no access is exclusive, so EXOKAY is one too).
enum Resp : uint8_t { OKAY = 0, EXOKAY = 1, SLVERR = 2, DECERR = 3 };

// ---- the clients and their IDs (AXI section 1, "IDs"; RTL-CHANGE-REQUESTS.md comment A). On a client's port there
// are no ID signals; on the controller's port client c's transactions carry ID c.
constexpr unsigned ID_BITS = 2;                  // †
enum Client : uint8_t { CACHE = 0, UPDATER = 1, RW_ENGINE = 2, BINDER = 3 };
constexpr unsigned CLIENTS = 4;                  // 1 << ID_BITS; a test may take a spare client's place

// ---- a port's signals in one cycle
// What the master drives: on a client's port the client, on the controller's port the arbiter. The ids are the
// controller's port's ARID and AWID, and are 0 on a client's port, which has none.
struct MasterOut {
    bool arvalid = false;
    uint64_t araddr = 0;                         // a byte address, a multiple of 16
    uint8_t arlen = 0;                           // the burst's beats less one
    uint8_t arid = 0;
    bool awvalid = false;
    uint64_t awaddr = 0;
    uint8_t awlen = 0;
    uint8_t awid = 0;
    bool wvalid = false;
    uint64_t wdata[2] = {};                      // [0] bits 63:0, [1] bits 127:64; byte b of the beat at bits 8b+7:8b
    bool wlast = false;
};
// What the slave drives: on a client's port the arbiter, on the controller's port the controller. RID and BID are the
// controller's port's, and 0 on a client's port.
struct SlaveOut {
    bool arready = false, awready = false, wready = false;
    bool rvalid = false;
    uint64_t rdata[2] = {};
    uint8_t rresp = OKAY;
    bool rlast = false;
    uint8_t rid = 0;
    bool bvalid = false;
    uint8_t bresp = OKAY;
    uint8_t bid = 0;
};

// ---- the arbiter's parameters (AXI section 3)
struct ArbiterConfig {
    unsigned turn_beats = 32;                    // TURN_BEATS: a piece's most beats; a longer burst is cut (†)
    unsigned client_bursts = 4;                  // CLIENT_BURSTS: a client's bursts outstanding in each direction (†)
};

// ---- the simulator's LPDDR model (AXI section 5)
struct LpddrTiming {
    unsigned read_latency = 70;                  // from AR's handshake to the burst's first beat, at the earliest (†)
    unsigned write_latency = 70;                 // from the later of AW's handshake and the last W beat's to B (†)
    unsigned max_reads = 16;                     // bursts taken and not answered, each direction; ready 0 beyond (†)
    unsigned max_writes = 16;
    // jitter: each burst's latency drawn from [jitter_lo, jitter_hi], each ready 0 one cycle in four, the R channel's
    // choice among the IDs that are due at random; AXI's rules and the order within an ID kept. Off: as above.
    bool jitter = false;
    unsigned jitter_lo = 20, jitter_hi = 200;
    uint32_t seed = 1;
};
// A fault a test sets on the model (AXI, "Errors"): every burst that touches bytes [first, last] in the direction
// given answers `resp`, for `bursts` bursts (0: every one). A faulty read's beats carry FAULT_DATA; a faulty write
// writes nothing.
struct LpddrFault {
    uint64_t first = 0, last = 0;
    bool reads = true, writes = true;
    uint8_t resp = SLVERR;
    unsigned bursts = 0;
};
constexpr uint64_t FAULT_DATA = 0xDEADBEEFDEADBEEFull;

}  // namespace axi
}  // namespace units
