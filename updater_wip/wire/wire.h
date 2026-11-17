// The wire between processes: links of a chain joined by shared memory, with a sync every window.
//
// A link is two directions, each B bytes a cycle. A byte given in cycle t arrives at the other end in cycle
// t + W, W being the window: a process simulates a window, and what it sent in that window is what its
// neighbours receive in the next. A run is then the same to the cycle whatever the machine does.
//
// Process 0 is the driver; process k + 1 is client k. Link i joins process i (up) to process i + 1 (down).
//
// The region (one per run, made by wire-launch, inherited by its children):
//
//   Header      magic, version, size, the parameters, the run's name, the launcher's PID   launcher, once
//   AbortLine   reason, who, status                                                           launcher
//   ProcLine    one per process: state, PID, windows done, waiting on whom                    that process
//   per link, per direction (down, then up):
//     ProdLine  sent (windows published), final_w (where it stopped), bell, prod_sleeping   the producer
//     ConsLine  consumed (windows read), gone (reads no more), bell, cons_sleeping          the consumer
//     data      K slots of W x B bytes; window w is in slot w mod K                         the producer
//
// Every line is 128 bytes and every field has one writer (the bells are also rung by the launcher on an
// abort, with an atomic add). The memory ordering is in NOTES.md and at each access in wire.cpp.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace wire {

constexpr uint64_t kMagic = 0x3145524957534d4dull;  // "MMSWIRE1", little-endian
constexpr uint32_t kVersion = 1;
constexpr size_t kLine = 128;
constexpr uint64_t kNever = ~0ull;

constexpr uint32_t kMaxClients = 1024;
constexpr uint32_t kMaxWindow = 1u << 20;
constexpr uint32_t kMaxLaneBytes = 64;
constexpr uint32_t kMaxSlots = 64;

enum AbortReason : uint32_t {
    kAbortNone = 0,
    kAbortDied = 1,        // killed by a signal (status: the wait status)
    kAbortFailed = 2,      // exited with a status other than 0 before finishing (status: the exit code)
    kAbortUnfinished = 3,  // exited 0 without calling finish()
    kAbortStalled = 4,     // made no progress for the launcher's --stall (status: its windows done)
    kAbortLauncher = 5,    // the launcher was told to stop (status: the signal)
};

enum ProcState : uint32_t {
    kStateAbsent = 0,
    kStateAttached = 1,
    kStateFinished = 2,
    kStateFailed = 3,  // gave up on an abort
};

struct Params {
    uint32_t clients = 1;
    uint32_t window = 64;
    uint32_t lane_bytes = 1;
    uint32_t slots = 4;
};

// Returns false and writes a message if the parameters are out of range.
bool params_valid(const Params& p, char* err, size_t errlen);

struct alignas(kLine) Header {
    uint64_t magic;
    uint32_t version;
    uint32_t reserved0;
    uint64_t size;
    uint32_t clients, window, lane_bytes, slots;
    uint64_t run_id;
    int32_t launcher_pid;
    uint32_t reserved1;
    char name[64];
};

struct alignas(kLine) AbortLine {
    std::atomic<uint32_t> reason;
    std::atomic<int32_t> who;
    std::atomic<int32_t> status;
};

struct alignas(kLine) ProcLine {
    std::atomic<uint32_t> state;
    std::atomic<int32_t> pid;
    std::atomic<int32_t> waiting_on;  // process index, or -1
    std::atomic<uint64_t> windows;    // windows done (for the launcher's watchdog)
    std::atomic<uint64_t> cycles;     // cycles done
};

struct alignas(kLine) ProdLine {
    std::atomic<uint64_t> sent;           // windows published: windows < sent are in their slots
    std::atomic<uint64_t> final_w;        // kNever while running; windows >= final_w are zeros
    std::atomic<uint32_t> bell;           // futex word: changes when sent or final_w does
    std::atomic<uint32_t> prod_sleeping;  // the producer sleeps on the ConsLine's bell
};

struct alignas(kLine) ConsLine {
    std::atomic<uint64_t> consumed;       // windows read: slots of windows < consumed may be reused
    std::atomic<uint32_t> gone;           // the consumer reads no more
    std::atomic<uint32_t> bell;           // futex word: changes when consumed or gone does
    std::atomic<uint32_t> cons_sleeping;  // the consumer sleeps on the ProdLine's bell
};

static_assert(std::atomic<uint64_t>::is_always_lock_free, "the region needs lock-free atomics");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "the region needs lock-free atomics");
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t), "a futex word is 32 bits");

enum Dir : int { kDown = 0, kUp = 1 };

// The region's layout, from its parameters.
size_t region_size(const Params& p);
// Writes a fresh region into mem (region_size(p) bytes, zeroed or not).
void region_init(void* mem, const Params& p, const char* name, int32_t launcher_pid, uint64_t run_id);
// Checks a region's header against its size; fills p.
bool region_check(const void* mem, size_t size, Params* p, char* err, size_t errlen);

Header* region_header(void* mem);
AbortLine* region_abort_line(void* mem);
ProcLine* region_proc(void* mem, int process);
ProdLine* region_prod(void* mem, int link, Dir d);
ConsLine* region_cons(void* mem, int link, Dir d);
uint8_t* region_data(void* mem, int link, Dir d);

// Sets the abort (if none is set yet) and rings every bell. Returns false if an abort was already set.
bool region_abort(void* mem, uint32_t reason, int who, int32_t status);

// "driver" or "client k".
void process_name(int process, char* buf, size_t n);
// One line on an abort: "client 1 (pid 123) died of signal 9".
void abort_text(void* mem, char* buf, size_t n);

class Wire;

// One end of one link. The simulator's loop calls exchange() on each end it has once every cycle.
class LinkEnd {
public:
    // This cycle's B bytes out; this cycle's B bytes in (those the far end gave in cycle t - W).
    void exchange(const uint8_t* out, uint8_t* in);
    uint8_t exchange(uint8_t out) {
        uint8_t in;
        exchange(&out, &in);
        return in;
    }
    uint64_t cycle() const { return t_; }

private:
    friend class Wire;
    void begin_window();
    void end_window();
    void finish();
    void publish(uint64_t n);
    uint64_t slot(uint64_t w) const;

    Wire* wire_ = nullptr;
    int peer_ = -1;
    ProdLine* out_prod_ = nullptr;  // the direction this end sends on
    ConsLine* out_cons_ = nullptr;
    uint8_t* out_data_ = nullptr;
    ProdLine* in_prod_ = nullptr;   // the direction this end receives on
    ConsLine* in_cons_ = nullptr;
    const uint8_t* in_data_ = nullptr;
    uint32_t W_ = 0, B_ = 0, K_ = 0;
    size_t slot_bytes_ = 0;

    uint64_t t_ = 0;    // the next cycle
    uint64_t win_ = 0;  // the current window
    uint32_t off_ = 0;  // the cycle within it
    uint8_t* out_slot_ = nullptr;
    const uint8_t* in_slot_ = nullptr;  // null: zeros
    bool in_done_ = false;   // the far end finished: zeros from here on
    bool out_free_ = false;  // the far end reads no more
#if defined(WIRE_PLANT) && WIRE_PLANT == 3
    uint8_t carry_[kMaxLaneBytes] = {};
#endif
};

// What a process sees of the wire.
class Wire {
public:
    Wire() = default;
    Wire(const Wire&) = delete;
    Wire& operator=(const Wire&) = delete;
    ~Wire();

    // Attaches the region inherited from wire-launch (args from wire_parse_args; see wire_args.h).
    // On failure returns false with a message.
    bool attach_fd(int fd, const char* link, int clients, int process, uint32_t window, uint32_t lane_bytes,
                   char* err, size_t errlen);
    // Attaches a region in this process's memory (wire-threads). The region outlives the Wire.
    bool attach_memory(void* mem, size_t size, int process, char* err, size_t errlen);

    LinkEnd* up() { return has_up_ ? &up_ : nullptr; }
    LinkEnd* down() { return has_down_ ? &down_ : nullptr; }

    // The process stops: its ends send zeros to the end of the current window, and its neighbours read zeros
    // from then on and never wait for it. Called by the destructor if not called before.
    void finish();

    int process() const { return process_; }
    uint32_t clients() const { return params_.clients; }
    uint32_t window() const { return params_.window; }
    uint32_t lane_bytes() const { return params_.lane_bytes; }
    uint32_t slots() const { return params_.slots; }

    // What happens when another process's failure is seen: by default a line on stderr and _exit(3).
    using AbortHandler = void (*)(const char* message);
    void set_abort_handler(AbortHandler h) { on_abort_ = h; }

private:
    friend class LinkEnd;
    bool attach_common(int process, char* err, size_t errlen);
    template <class Ready>
    void wait_until(Ready ready, std::atomic<uint32_t>& bell, std::atomic<uint32_t>& my_sleeping, int peer);
    void check_abort();
    [[noreturn]] void give_up();
    void progress(uint64_t windows, uint64_t cycles);

    void* mem_ = nullptr;
    size_t size_ = 0;
    bool mapped_ = false;
    Params params_;
    int process_ = -1;
    AbortLine* abort_ = nullptr;
    ProcLine* me_ = nullptr;
    LinkEnd up_, down_;
    bool has_up_ = false, has_down_ = false;
    bool finished_ = false;
    uint64_t spin_ns_ = 0;
    int yields_ = 0;
    AbortHandler on_abort_ = nullptr;
};

}  // namespace wire
