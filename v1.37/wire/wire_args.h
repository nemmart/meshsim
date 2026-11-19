// The wire's arguments, parsed in one place for the chip's program, the driver and the stand-ins.
//
//   --clients N    how many ASICs are on the chain (owner)
//   --client k     which this process is, 0 to N - 1 (owner)
//   --driver       this process is the driver (owner: an explicit flag)
//   --link NAME    the run's name, checked against the region's
//   --wire-fd FD   the region, inherited from wire-launch (the launcher adds it)
//   --window W, --lane-bytes B   optional: checked against the region's, which the launcher sets
//
// Each may be written "--opt value" or "--opt=value". wire_parse_args takes them out of argv, so the program's
// own parser sees the rest.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wire {

class Wire;

struct WireArgs {
    bool present = false;  // any wire argument was given
    int clients = -1;
    int client = -1;
    bool driver = false;
    const char* link = nullptr;
    int fd = -1;
    uint32_t window = 0;
    uint32_t lane_bytes = 0;

    int process() const { return driver ? 0 : client + 1; }
};

// Takes the wire's arguments out of argv (and lowers *argc). Returns false with a message on a bad or
// inconsistent argument. With no wire argument at all, returns true with present == false.
bool wire_parse_args(int* argc, char** argv, WireArgs* a, char* err, size_t errlen);

// Attaches w with the parsed arguments.
bool wire_attach(Wire* w, const WireArgs& a, char* err, size_t errlen);

}  // namespace wire
