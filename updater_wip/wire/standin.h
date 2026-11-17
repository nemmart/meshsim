// A stand-in end, for testing the wire: not the driver, not a chip.
//
// Each cycle, in each direction it can send, it forwards what came from the other side some cycles before (a
// chain's node; an end of the chain reflects what it received), mixes in bursts of bytes of its own chosen
// from a state hashed from everything it received and when, and has long silences. Its traffic therefore
// depends on what it receives and in which cycle, so a byte a cycle early or late changes everything after.
//
// It keeps a digest of every byte received and sent, at every end, in every cycle. The processes on the wire and
// the one-process reference (wire-ref) must give the same digests.
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace wire {

struct StandInCfg {
    uint64_t seed = 1;
    int process = 0;  // 0 the driver, k + 1 client k
    int clients = 1;
    uint32_t lane_bytes = 1;  // at most 8
    uint64_t cycles = 1000000;
    bool stagger = true;  // each process finishes at a cycle of its own
};

constexpr uint64_t kCheckpoint = 1ull << 20;  // a digest line every this many cycles

class StandIn {
public:
    explicit StandIn(const StandInCfg& c);

    // The cycle at which it stops: it sends in cycles 0 to finish_at() - 1, zeros after.
    uint64_t finish_at() const { return finish_at_; }
    bool has_up() const { return out_[0].present; }
    bool has_down() const { return out_[1].present; }

    // This cycle's outputs (from the state before this cycle's inputs). B bytes each; zeros for an absent end.
    void outputs(uint64_t t, uint8_t* out_up, uint8_t* out_dn);
    // This cycle's inputs.
    void inputs(uint64_t t, const uint8_t* in_up, const uint8_t* in_dn);
    // After inputs(t): writes a checkpoint line if one is due.
    void checkpoint(uint64_t t, FILE* f) const;
    // The end line.
    void report(FILE* f) const;

private:
    struct Out {
        bool present = false;
        uint32_t len = 1;  // the delay line's length
        std::vector<uint64_t> line;
        uint32_t pos = 0;
        std::vector<uint64_t> q;  // what waits to be forwarded
        uint32_t qhead = 0, qcount = 0;
        uint64_t burst_left = 0;
        uint64_t phase_left = 0;
        bool active = false;
        uint64_t period = 1000;
        uint64_t key = 0;
        uint64_t drops = 0;
    };
    uint64_t rng();
    uint64_t pack(const uint8_t* b) const;
    void unpack(uint64_t w, uint8_t* b) const;

    StandInCfg c_;
    uint64_t mask_;
    uint64_t finish_at_;
    uint64_t rng_state_;
    uint64_t h_;
    Out out_[2];  // 0 up, 1 down
    uint64_t last_out_[2] = {0, 0};
    uint64_t din_[2] = {0, 0}, dout_[2] = {0, 0};
};

inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebull;
    x ^= x >> 31;
    return x;
}

// The cycle at which process p stops, for a run of `cycles` (the same in the processes and the reference).
uint64_t standin_finish(const StandInCfg& c);

}  // namespace wire
