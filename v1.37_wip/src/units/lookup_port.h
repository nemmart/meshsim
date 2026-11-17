// lookup_port.h: the cache's look-up port (../v1.4_wip/planning/CACHE-INTERFACE.md, "CI" below): a request port for
// each client and one shared reply bus (owner). Types and constants only, no logic: what the cache's controller, the
// Updater's controller and the stand-in cache compile against (updater_wip, WORKER5 and WORKER6). A dagger in CI (†)
// is a proposal the owner has not ruled on.
//
// THE FORMS (CI, "Terms"). A root, and a page's address, are 31 bits in 128-byte units: the form an index entry
// stores. An entry is 32 bits: [30:0] the address of the page it names, [31] the owned bit; NO_PAGE (all ones) is a
// hole at every level. Levels count from the top (owner): the entry at level 0 is in the root, the entry at level
// depth - 1 names the data page. Page p's entry at level L is entry ((p >> 10 * (depth - 1 - L)) & 1023) of the
// index page at level L: 32-bit word (i & 31) of its line (i >> 5), little-endian, as the file store keeps it.
//
// THE HANDSHAKE (CI section 1). A request moves in the cycle `valid` and `ready` are both 1; the client holds `valid`
// and the request until then. A client has one request outstanding (owner): its port's `ready` is 0 from its
// handshake until its reply. Each request has exactly one reply on the shared bus, in a cycle after its handshake,
// the next at the earliest; the bus has no ready: the client takes the reply in the cycle it is there with its
// number in `client`. At most one reply a cycle. Every output a flip-flop: the cycle convention is axi_port.h's.
#pragma once

#include <cstdint>

namespace units {
namespace lookup {

// ---- the operations (CI section 2)
enum Op : uint8_t { LOOK_UP = 0, UPDATE = 1, DISCARD = 2 };   // 3 reserved (†: undefined)

// ---- a reply's status (CI section 2). 0 to 3 are the fill port's numbers (machine-spec 3, 17.9.5).
enum Status : uint8_t { OK = 0, HOLE = 1, NOT_OWNED = 2, BEYOND = 3, LPDDR_ERROR = 4 };   // 5 to 7 reserved

// ---- the clients: a port each, numbered as its replies' `client`. A client has one number everywhere: its AXI ID
// (axi_port.h's Client). 0 is the cache's own number there, so port 0 is spare: a test's.
enum Client : uint8_t { UPDATER = 1, RW_ENGINE = 2, BINDER = 3 };
constexpr unsigned CLIENTS = 4;

// ---- the stored forms (machine-spec 23.5; UPDATER-INTERFACE.md)
constexpr uint32_t NO_PAGE = 0xFFFFFFFFu;        // a hole
constexpr uint32_t OWNED = 0x80000000u;          // [31]: the page is the file's own
constexpr uint32_t ADDRESS = 0x7FFFFFFFu;        // [30:0]: the page's address, 128-byte units
constexpr unsigned PAGE_UNITS = 32;              // a 4 KB page in 128-byte units: a page's address is a multiple
constexpr unsigned ENTRIES_PER_PAGE = 1024, ENTRIES_PER_LINE = 32, LINES_PER_PAGE = 32;
constexpr unsigned MAX_DEPTH = 3;

// ---- the fields' widths (CI section 1)
constexpr unsigned OP_BITS = 2, ROOT_BITS = 31, DEPTH_BITS = 2, PAGE_BITS = 30, LEVEL_BITS = 2, ENTRY_BITS = 32;
constexpr unsigned CLIENT_BITS = 2, STATUS_BITS = 3;

// ---- a request port's signals in one cycle: client to cache, but `ready`
struct Request {
    bool valid = false;
    uint8_t op = LOOK_UP;
    uint32_t root = 0;       // the file's root; for DISCARD the page's address ([4:0] ignored, †)
    uint8_t depth = 0;       // 0 to 3
    uint32_t page = 0;       // the page number in the file, < 2^(10 * depth)
    uint8_t level = 0;       // UPDATE: the level of the entry to change; ignored otherwise
    uint32_t entry = 0;      // UPDATE: the new entry; ignored otherwise
};
// ---- the shared reply bus's signals in one cycle: cache to every client
struct Reply {
    bool valid = false;
    uint8_t client = 0;      // whose reply
    uint8_t status = OK;
    uint8_t level = 0;       // where the walk stopped, or the level changed (CI section 2's tables)
    uint32_t entry = 0;      // the entry there (an UPDATE's: as it was before, †)
};

}  // namespace lookup
}  // namespace units
