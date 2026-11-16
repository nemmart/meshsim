// test_request_tree.cpp: the request unit and the request tree (machine-spec 11; the RTL's rtl/tile/base/spec.md 11 and
// 19) from the specification, on the simulator's units at the RTL's parameters (src/units/reqbus.h at hop_latency 1,
// credit_latency 1 and a request queue of REQUEST_QUEUE_DEPTH = 4, as the chip's builder sets them; src/units/tile_ctl.h
// for the registers' side).
//
// Every expectation is the specification's, or, where it is silent, the RTL's (Snarky One's
// rtl/blocks/request_tree/README.md and rtl/blocks/request_unit/README.md, their "Timing", and the sources' headers:
// row_node.veryl, root.veryl, request_unit.veryl, primitives' round_robin_arbiter.veryl). None is the simulator's output.
//
// TWO BENCHES.
//   - THE TREE'S (`Bench`): the tree's units (the 67 request queues, the 8 row nodes, the root, the 3 receive queues)
//     with stand-ins for the tiles' ends (`Ends`): a stand-in presents a REQUEST_SEND as register_bus does, again in
//     every cycle until it is taken, and a REQUEST_RECEIVE load; it reads what a load presented in that cycle reads.
//     A cycle of the bench is a cycle of the RTL's `reg` interface: "presented in p" below.
//   - THE CHIP'S (`ChipRun`, tests/mach.h's MachConfig::chip()): programs written here on a compute tile's core, the
//     memory tile's and the master tile's, for the registers of 11.1 as a core meets them, with 6.4's timing: "issued
//     in t" below, the cycle the core raises the access in; a window's access is presented in t + 1. Every core's
//     accesses are read off its data port and the run is replayed in the model of the tree's bench (`replay`).
//
// THE TIMING (the RTL's READMEs; 6.4). A REQUEST_SEND presented in p and taken is pushed at the end of p. From an empty
// request queue and an idle tree its beat 0 is offered in p + 1, one beat a cycle; a compute tile's beat offered in u is
// offered to its target in u + 2 (a cycle a node: the row node, the root), a special tile's in u + 1. The request
// enters the receive queue as its last beat moves, and is read by a load presented from the next cycle:
//     a compute tile's: beats to the target in p + 3 .. p + 10, read by a load presented from p + 11;
//     a special tile's: p + 2 .. p + 9, from p + 10;
//     its entry leaves the request queue as its last beat moves, in p + 8.
// For a core, issuing the store in t (presented in t + 1) and the load in t' (presented in t' + 1): the request is read
// by a load issued from t + 11 (a compute tile's) or t + 10 (a special tile's).
//
// WHAT THE SIMULATOR CANNOT EXPRESS, and is not tested: the link's 16-bit beats, their ready and valid, a beat held
// while its link is not ready, and a sender that pauses inside a request: the simulator's request unit hands over whole
// requests and its units pass a beat's place, not its bits. What a beat's timing makes visible at the tiles' ends is
// tested: when a send is taken, when a request can be read, the order.
//
// The RTL's reset in mid-traffic (a request part sent or part received dropped on both sides) has no counterpart: a
// simulated machine is built reset.
#include <algorithm>
#include <array>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "base/fatal.h"
#include "config.h"
#include "harness.h"
#include "mach.h"
#include "rv32_asm.h"
#include "sim.h"
#include "units/reqbus.h"
#include "units/tile_ctl.h"

namespace {

// ---- The specification's numbers, written here (never read from the simulator's headers) ----
constexpr unsigned kMeshSize = 8, kTileCount = 64, kSenders = kTileCount + 3, kTargets = 3;
constexpr unsigned kMaster = 64, kMemory = 65, kTracer = 66;               // the special tiles' ids (2.1)
constexpr unsigned kRequestQueueDepth = 4;                                // REQUEST_QUEUE_DEPTH (19)
constexpr unsigned kReceiveQueueDepth[kTargets] = {128, 256, 128};        // RECEIVE_QUEUE_DEPTH at 8 x 8 (11.3)
// 11.3: a power of two at least TILE_COUNT x the target's per-tile limit (1, 3, 1) + 64, the least such (the RTL's)
static_assert(kReceiveQueueDepth[0] == 128 && 128 >= kTileCount * 1 + 64 && 64 < kTileCount * 1 + 64);
static_assert(kReceiveQueueDepth[1] == 256 && 256 >= kTileCount * 3 + 64 && 128 < kTileCount * 3 + 64);
static_assert(kReceiveQueueDepth[2] == kReceiveQueueDepth[0]);
constexpr unsigned kBeats = 8;                                            // a request's beats (11.3)
// The timing above, from the cycle a send is presented and taken in.
constexpr unsigned kLeaves = 8;                                           // its entry leaves the request queue
constexpr unsigned kReadCompute = 11, kReadSpecial = 10;                  // a load presented from then reads it

struct Request {
    uint32_t w[4] = {};
    bool operator==(const Request& o) const { return w[0] == o.w[0] && w[1] == o.w[1] && w[2] == o.w[2] && w[3] == o.w[3]; }
};
// Word 0's hardware fields (11.2): [6:0] the requester, [9:7] the target.
constexpr uint32_t word0(unsigned target, unsigned requester_written, uint32_t rest) {
    return (rest & ~0x3FFu) | (target & 7) << 7 | (requester_written & 0x7F);
}
constexpr unsigned target_of(uint32_t w0) { return w0 >> 7 & 7; }
constexpr unsigned requester_of(uint32_t w0) { return w0 & 0x7F; }
// A request as its receiver reads it: the tree's requester in place of what the sender wrote (11.2, 11.3).
Request stamped(Request r, unsigned sender) { r.w[0] = (r.w[0] & ~0x7Fu) | sender; return r; }

// ================================================================================================ the tree's bench
// The tiles' ends of the tree, for a row's eight tiles or for the three special tiles: what a request unit's `reg`
// side does with the tree's units (tile_ctl.h's TileCore does the same from a core's accesses).
struct Ends {
    unsigned n = 0;
    bool receivers = false;
    sim::LinkOut push[kMeshSize];
    sim::WireIn freed[kMeshSize];
    sim::LinkIn freed_ev[kMeshSize];
    sim::RegFileIn head[kTargets];
    sim::LinkOut pop[kTargets], credit[kTargets];
    // ---- the cycle's inputs, set by the test before it ----
    bool present[kMeshSize] = {};                // a REQUEST_SEND presented, with the request
    Request request[kMeshSize];
    bool load[kTargets] = {};                    // a REQUEST_RECEIVE load presented
    // ---- what the cycle showed ----
    bool taken[kMeshSize] = {};                  // the send was taken (reg_ready 1)
    bool left[kMeshSize] = {};                   // the request queue's head left in this cycle
    bool pending[kTargets] = {};                 // the receive queue as a load presented in this cycle reads it
    Request read[kTargets];
    // ---- the request queue's count, as the stall needs it (11.1) ----
    unsigned count[kMeshSize] = {};

    Ends(sim::Ports& p, unsigned n_, bool receivers_) : n(n_), receivers(receivers_) {
        static const char* kPush[kMeshSize] = {"push0", "push1", "push2", "push3", "push4", "push5", "push6", "push7"};
        static const char* kFreed[kMeshSize] = {"freed0", "freed1", "freed2", "freed3", "freed4", "freed5", "freed6", "freed7"};
        static const char* kEv[kMeshSize] = {"ev0", "ev1", "ev2", "ev3", "ev4", "ev5", "ev6", "ev7"};
        for (unsigned i = 0; i < n; i++) { p.out(push[i], kPush[i], 2); p.in(freed[i], kFreed[i]); p.in(freed_ev[i], kEv[i], 0); }
        if (receivers) {
            static const char* kHead[kTargets] = {"head0", "head1", "head2"};
            static const char* kPop[kTargets] = {"pop0", "pop1", "pop2"};
            static const char* kCredit[kTargets] = {"credit0", "credit1", "credit2"};
            for (unsigned t = 0; t < kTargets; t++) { p.in(head[t], kHead[t]); p.out(pop[t], kPop[t], 0); p.out(credit[t], kCredit[t], 1); }
        }
    }
    sim::Wake step(sim::Ctx& c) {
        using units::reqbus::ReqRecv;
        for (unsigned i = 0; i < n; i++) {
            while (c.ready(freed_ev[i])) c.take(freed_ev[i]);
            left[i] = c.read(freed[i]) != 0;
            if (left[i]) count[i]--;
        }
        for (unsigned t = 0; receivers && t < kTargets; t++) {
            pending[t] = c.read(head[t], ReqRecv::R_PENDING) != 0;
            for (unsigned i = 0; i < 4; i++) read[t].w[i] = c.read(head[t], ReqRecv::R_W0 + i);
            if (load[t] && pending[t]) { c.put(pop[t]); c.put(credit[t], sim::Word(c.now())); }   // an empty queue: no pop, no credit
        }
        // 11.1: a REQUEST_SEND stalls while the queue is full and no entry leaves that cycle
        for (unsigned i = 0; i < n; i++) {
            taken[i] = present[i] && count[i] < kRequestQueueDepth;
            if (!taken[i]) continue;
            const Request& r = request[i];
            c.put(push[i], sim::Word(r.w[0]) | sim::Word(r.w[1]) << 32, sim::Word(r.w[2]) | sim::Word(r.w[3]) << 32);
            count[i]++;
        }
        return c.next();
    }
    uint64_t digest() const { return 0; }
};

const char* ends_name(unsigned e) {
    static const char* kNames[9] = {"ends.row0", "ends.row1", "ends.row2", "ends.row3", "ends.row4", "ends.row5", "ends.row6",
                                    "ends.row7", "ends.special"};
    return kNames[e];
}

struct Bench {
    units::reqbus::ReqBusConfig rc;
    sim::Sim m;
    Ends* ends[9] = {};
    uint64_t cycle = 0;                          // the cycle just run; the first is 1

    // `options`: the framework's debug orders (sim.h DEBUG CHECKS): no result may depend on them
    explicit Bench(const sim::Options& options = {}) : rc(config()), m(build(options)) {
        for (unsigned e = 0; e < 9; e++) ends[e] = &m.unit<Ends>(ends_name(e));
    }
    // The tree as the chip's builder sets it (tests/mach.cpp, the chip's; reqbus.h's header).
    static units::reqbus::ReqBusConfig config() {
        units::reqbus::ReqBusConfig c;
        c.hop_latency = 1; c.credit_latency = 1; c.fifo_depth = kRequestQueueDepth;
        for (unsigned t = 0; t < kTargets; t++) c.queue_depth[t] = kReceiveQueueDepth[t];
        return c;
    }
    sim::Sim build(const sim::Options& options) {
        using namespace units::reqbus;
        sim::Config cfg(options);
        ReqBusIO io = add_reqbus(cfg, rc, ~uint64_t(0), 7);
        for (unsigned e = 0; e < 9; e++) {
            const bool special = e == 8;
            Ends& x = cfg.add<Ends>(ends_name(e), special ? 3u : kMeshSize, special);
            for (unsigned i = 0; i < x.n; i++) {
                ReqLeaf& l = *io.leaf[kMeshSize * e + i];
                cfg.link(x.push[i], l.push, 1);
                cfg.connect(l.freed, x.freed[i]);
                cfg.link(l.freed_ev, x.freed_ev[i], 1);
            }
            for (unsigned t = 0; special && t < kTargets; t++) {
                cfg.connect(io.recv[t]->head, x.head[t]);
                cfg.link(x.pop[t], io.recv[t]->pop, 1);
                cfg.link(x.credit[t], io.root->qcredit[t], io.queue_credit_latency());
            }
        }
        return cfg.build();
    }
    Ends& of(unsigned sender) { return *ends[sender / kMeshSize]; }
    static unsigned at(unsigned sender) { return sender % kMeshSize; }

    // ---- a cycle's inputs ----
    void present(unsigned sender, const Request& r) { of(sender).present[at(sender)] = true; of(sender).request[at(sender)] = r; }
    void load(unsigned target) { ends[8]->load[target] = true; }
    // ---- one cycle; then what it showed. A send not taken stays presented, a load is of its one cycle ----
    void step() {
        for (unsigned e = 0; e < 9; e++) m.wake(ends_name(e));
        m.run(1);
        cycle++;
        for (unsigned e = 0; e < 9; e++)
            for (unsigned i = 0; i < ends[e]->n; i++) if (ends[e]->taken[i]) ends[e]->present[i] = false;
        for (unsigned t = 0; t < kTargets; t++) ends[8]->load[t] = false;
    }
    bool presented(unsigned sender) { return of(sender).present[at(sender)]; }      // still: stalled in the cycle just run
    bool taken(unsigned sender) { return of(sender).taken[at(sender)]; }
    bool left(unsigned sender) { return of(sender).left[at(sender)]; }
    bool pending(unsigned target) const { return ends[8]->pending[target]; }
    const Request& read(unsigned target) const { return ends[8]->read[target]; }

    // Presents r from the next cycle until it is taken; the cycle it was taken in (0: not within `max` cycles).
    uint64_t send(unsigned sender, const Request& r, unsigned max = 64) {
        present(sender, r);
        for (unsigned i = 0; i < max; i++) { step(); if (taken(sender)) return cycle; }
        return 0;
    }
    // Runs until `target`'s queue reads pending; the first such cycle (0: not within `max`).
    uint64_t until_pending(unsigned target, unsigned max) {
        for (unsigned i = 0; i < max; i++) { step(); if (pending(target)) return cycle; }
        return 0;
    }
};

Request request(unsigned target, uint32_t tag) {
    Request r;
    r.w[0] = word0(target, 0, tag << 10);
    r.w[1] = 0x11110000u ^ tag; r.w[2] = 0x22220000u ^ tag; r.w[3] = 0x33330000u ^ tag;
    return r;
}

// ============================================================================================ a model of the tree
// The request queues, the tree and the receive queues beat by beat, from the RTL's READMEs and its sources' headers:
// every decision of a cycle from the state at its start, then the edge.
//   - A request queue (request_unit.veryl): up to REQUEST_QUEUE_DEPTH requests; the head's beats offered one a cycle
//     from the cycle after its push; the entry leaves as its last beat moves. reg_ready is 1 unless the queue is full
//     at the start of the cycle and its head's last beat does not move in it.
//   - A node (row_node.veryl, root.veryl): a buffer of 8 beats an input, its ready 1 while it has room at the start of
//     the cycle. While it passes no request it chooses the first input at or after its pointer whose buffer holds a
//     beat (at the root: and whose request's target has a credit, or is reserved), the pointer then one past it, 0 at
//     the reset; it offers the first beat in the cycle it chooses, and each beat as it is in the buffer, until the
//     last has moved. A row node writes the requester in beat 0, the root a special tile's.
//   - The root's credits: receive_queue_depth a target at the reset; one taken at the choice; one back at the end of a
//     cycle with a pop. A reserved target's request takes none and leaves the buffer a beat a cycle.
//   - A receive queue (request_unit.veryl): the request enters as its eighth beat moves; a load reads the state at the
//     end of the cycle before; a REQUEST_RECEIVE on a non-empty queue pops at the end of its cycle.
struct Model {
    struct Node {
        std::deque<uint16_t> buffer[kMeshSize + 3];
        bool forwarding = false;
        unsigned current = 0, current_target = 0, beats_sent = 0, pointer = 0;
    };
    struct In { bool present[kSenders] = {}; Request request[kSenders]; bool load[kTargets] = {}; };
    struct Out { bool taken[kSenders] = {}; bool pending[kTargets] = {}; Request read[kTargets]; };

    std::deque<Request> queue[kSenders];
    unsigned beat[kSenders] = {};
    Node row[kMeshSize], root;
    unsigned credits[kTargets];
    uint16_t partial[kTargets][kBeats] = {};
    std::deque<Request> receive[kTargets];
    uint64_t dropped = 0, delivered = 0, stalled_sends = 0, credit_waits = 0, full_buffers = 0;

    Model() { for (unsigned t = 0; t < kTargets; t++) credits[t] = kReceiveQueueDepth[t]; }
    static uint16_t beat_of(const Request& r, unsigned k) { return uint16_t(r.w[k / 2] >> (16 * (k % 2))); }
    std::deque<uint16_t>& input_of(unsigned sender) {
        return sender < kTileCount ? row[sender / kMeshSize].buffer[sender % kMeshSize] : root.buffer[kMeshSize + sender - kTileCount];
    }

    Out cycle(const In& in) {
        Out out;
        // ---- the decisions, from the state at the start of the cycle ----
        bool popped[kTargets];
        for (unsigned t = 0; t < kTargets; t++) {
            out.pending[t] = !receive[t].empty();
            if (out.pending[t]) out.read[t] = receive[t].front();
            popped[t] = in.load[t] && out.pending[t];
        }
        // the root
        int root_selected = -1;
        bool root_chose = false;
        unsigned root_target = root.current_target;
        if (root.forwarding) { if (!root.buffer[root.current].empty()) root_selected = int(root.current); }
        else {
            for (unsigned i = 0; i < kMeshSize + 3 && root_selected < 0; i++) {
                const unsigned k = (root.pointer + i) % (kMeshSize + 3);
                if (root.buffer[k].empty()) continue;
                const unsigned target = target_of(root.buffer[k].front());
                if (target < kTargets && credits[target] == 0) { credit_waits++; continue; }
                root_selected = int(k); root_target = target; root_chose = true;
            }
        }
        const bool root_moves = root_selected >= 0 && (root_target >= kTargets || receive[root_target].size() < kReceiveQueueDepth[root_target]);
        // the row nodes
        int row_selected[kMeshSize];
        bool row_chose[kMeshSize], row_moves[kMeshSize];
        for (unsigned r = 0; r < kMeshSize; r++) {
            Node& n = row[r];
            row_selected[r] = -1; row_chose[r] = false;
            if (n.forwarding) { if (!n.buffer[n.current].empty()) row_selected[r] = int(n.current); }
            else
                for (unsigned i = 0; i < kMeshSize && row_selected[r] < 0; i++) {
                    const unsigned k = (n.pointer + i) % kMeshSize;
                    if (!n.buffer[k].empty()) { row_selected[r] = int(k); row_chose[r] = true; }
                }
            row_moves[r] = row_selected[r] >= 0 && root.buffer[r].size() < kBeats;
            if (row_selected[r] >= 0 && !row_moves[r]) full_buffers++;
        }
        // the request queues
        bool sender_moves[kSenders], leaves[kSenders];
        for (unsigned s = 0; s < kSenders; s++) {
            sender_moves[s] = !queue[s].empty() && input_of(s).size() < kBeats;
            leaves[s] = sender_moves[s] && beat[s] == kBeats - 1;
            out.taken[s] = in.present[s] && !(queue[s].size() == kRequestQueueDepth && !leaves[s]);
            if (in.present[s] && !out.taken[s]) stalled_sends++;
        }
        // ---- the edge ----
        for (unsigned t = 0; t < kTargets; t++) if (popped[t]) { receive[t].pop_front(); credits[t]++; }
        if (root_chose) {
            root.forwarding = true; root.current = unsigned(root_selected); root.current_target = root_target;
            root.pointer = (unsigned(root_selected) + 1) % (kMeshSize + 3);
            if (root_target < kTargets) credits[root_target]--;
        }
        if (root_moves) {
            uint16_t b = root.buffer[root.current].front();
            root.buffer[root.current].pop_front();
            if (root.current >= kMeshSize && root.beats_sent == 0) b = uint16_t((b & ~0x7Fu) | (kTileCount + root.current - kMeshSize));
            if (root.current_target < kTargets) partial[root.current_target][root.beats_sent] = b;
            if (++root.beats_sent == kBeats) {
                root.beats_sent = 0; root.forwarding = false;
                if (root.current_target < kTargets) {
                    Request r;
                    for (unsigned k = 0; k < kBeats; k++) r.w[k / 2] |= uint32_t(partial[root.current_target][k]) << (16 * (k % 2));
                    receive[root.current_target].push_back(r);
                    delivered++;
                } else dropped++;
            }
        }
        for (unsigned r = 0; r < kMeshSize; r++) {
            Node& n = row[r];
            if (row_chose[r]) { n.forwarding = true; n.current = unsigned(row_selected[r]); n.pointer = (n.current + 1) % kMeshSize; }
            if (!row_moves[r]) continue;
            uint16_t b = n.buffer[n.current].front();
            n.buffer[n.current].pop_front();
            if (n.beats_sent == 0) b = uint16_t((b & ~0x7Fu) | (r * kMeshSize + n.current));
            root.buffer[r].push_back(b);
            if (++n.beats_sent == kBeats) { n.beats_sent = 0; n.forwarding = false; }
        }
        for (unsigned s = 0; s < kSenders; s++) {
            if (sender_moves[s]) {
                input_of(s).push_back(beat_of(queue[s].front(), beat[s]));
                if (++beat[s] == kBeats) { beat[s] = 0; queue[s].pop_front(); }
            }
            if (out.taken[s]) queue[s].push_back(in.request[s]);
        }
        return out;
    }
};

}  // namespace

// ============================================================================================== 11.2: the request
// A request's 128 bits arrive as sent, but the requester, word 0's [6:0], which the tree writes: a row node for a
// compute tile, r x MESH_SIZE + c, and the root for a special tile, TILE_COUNT + 0, 1 or 2 (11.3: "no request can claim
// another sender's id"). The target, word 0's [9:7], names the receiver: 0 the master tile, 1 the memory tile, 2 the
// tracer tile; the other queues stay empty.
TEST(request_words_requester_and_target) {
    struct Case { unsigned sender, target, written; };
    const Case cases[] = {{37, 1, 0x7F}, {0, 0, 0x55}, {63, 2, 0}, {kMaster, 2, 5}, {kMemory, 0, 0x7F}, {kTracer, 1, 0}, {kMaster, 0, 66}, {8, 0, 9}};
    for (const Case& k : cases) {
        Bench b;
        Request r;
        r.w[0] = word0(k.target, k.written, 0xA5C3F000u); r.w[1] = 0x89ABCDEFu; r.w[2] = 0x01234567u; r.w[3] = 0xFEDCBA98u;
        CHECK(b.send(k.sender, r) != 0);
        CHECK(b.until_pending(k.target, 40) != 0);
        CHECK(b.read(k.target) == stamped(r, k.sender));
        CHECK_EQ(requester_of(b.read(k.target).w[0]), k.sender);
        CHECK_EQ(target_of(b.read(k.target).w[0]), k.target);
        for (unsigned t = 0; t < kTargets; t++)
            if (t != k.target) { CHECK(!b.pending(t)); CHECK(b.read(t) == Request{}); }       // an empty queue reads 0 (11.1)
    }
}

// ================================================================================================ 11.3: the timing
// An unloaded request (the header's TIMING): presented and taken in p, its entry leaves the request queue in p + 8, and
// a load presented in p + 11 is the first to read it (a compute tile's: a cycle a node, two nodes), in p + 10 a special
// tile's (one node). A second request sent in the next cycle follows with no cycle between: 8 cycles later.
TEST(tree_timing_unloaded) {
    for (unsigned sender : {0u, 5u, 26u, 63u, kMaster, kMemory, kTracer}) {
        for (unsigned target = 0; target < kTargets; target++) {
            Bench b;
            const unsigned read_after = sender < kTileCount ? kReadCompute : kReadSpecial;
            const Request first = request(target, 1), second = request(target, 2);
            b.present(sender, first);
            b.step();
            CHECK(b.taken(sender));
            const uint64_t p = b.cycle;
            b.present(sender, second);
            uint64_t left_at = 0, read_at = 0;
            for (unsigned i = 0; i < 16 && !read_at; i++) {
                b.step();
                if (b.cycle == p + 1) CHECK(b.taken(sender));
                if (b.left(sender) && !left_at) left_at = b.cycle;
                if (b.pending(target)) read_at = b.cycle;
            }
            CHECK_EQ(left_at, p + kLeaves);
            CHECK_EQ(read_at, p + read_after);
            CHECK(b.read(target) == stamped(first, sender));
            // the pop: the second is the head a cycle later only once it is there, 8 cycles after the first
            b.load(target);
            b.step();                                                        // p + read_after + 1: the load's cycle, the pop
            CHECK(b.pending(target) && b.read(target) == stamped(first, sender));
            for (uint64_t c = b.cycle + 1; c < p + read_after + kBeats; c++) { b.step(); CHECK(!b.pending(target)); }
            b.step();
            CHECK_EQ(b.cycle, p + read_after + kBeats);
            CHECK(b.pending(target) && b.read(target) == stamped(second, sender));
        }
    }
}

// ================================================================================================== 11.3: the order
// A sender's requests to one target arrive in the order sent, whatever the other senders do.
TEST(tree_order_from_one_sender) {
    Bench b;
    const unsigned senders[] = {9, 10, 49, kMemory};
    unsigned sent[4] = {}, got[4] = {};
    const unsigned kEach = 40;
    for (unsigned c = 0; c < 4000; c++) {
        for (unsigned i = 0; i < 4; i++)
            if (!b.presented(senders[i]) && sent[i] < kEach) b.present(senders[i], request(0, i << 8 | sent[i]++));
        b.load(0);
        b.step();
        if (!b.pending(0)) continue;
        const Request& r = b.read(0);
        unsigned i = 0;
        while (i < 4 && senders[i] != requester_of(r.w[0])) i++;
        CHECK(i < 4);
        CHECK(r == stamped(request(0, i << 8 | got[i]), senders[i]));
        got[i]++;
    }
    for (unsigned i = 0; i < 4; i++) CHECK_EQ(got[i], kEach);
}

// ======================================================================================== 11.3: the arbitration
// A row node round-robins over its inputs in column order, as the router's arbiters do (16.3): the first at or after
// its pointer, the pointer then one past it, 0 after the reset; a whole request before it chooses again, and a request
// can follow another with no cycle between. The eight tiles of a row send in the same cycle p, and each again at once:
// the master tile reads column 0's at p + 11, column c's 8 c cycles later, then each column's second in the same order.
TEST(row_node_round_robin) {
    for (unsigned r : {0u, 2u, 7u}) {
        Bench b;
        for (unsigned c = 0; c < kMeshSize; c++) b.present(r * kMeshSize + c, request(0, c));
        b.step();
        const uint64_t p = b.cycle;
        for (unsigned c = 0; c < kMeshSize; c++) { CHECK(b.taken(r * kMeshSize + c)); b.present(r * kMeshSize + c, request(0, 0x100 | c)); }
        unsigned got = 0;
        for (unsigned i = 0; i < 16 * kBeats + 16; i++) {
            b.load(0);                                                       // every request popped in its first cycle
            b.step();
            const bool expected = b.cycle >= p + kReadCompute && (b.cycle - p - kReadCompute) % kBeats == 0 && got < 2 * kMeshSize;
            CHECK_EQ(b.pending(0), expected);
            if (!expected) continue;
            const unsigned c = got % kMeshSize;
            CHECK(b.read(0) == stamped(request(0, (got / kMeshSize) << 8 | c), r * kMeshSize + c));
            got++;
        }
        CHECK_EQ(got, 2 * kMeshSize);
    }
}

// The root round-robins over the row nodes in row order, then the master, memory and tracer tiles. Column 0 of every
// row and the three special tiles send in the same cycle p. The special tiles' beats reach the root a cycle before the
// rows': in p + 2 it chooses among the three, from its pointer 0 the master tile's, its input 8; the pointer is then 9:
// the memory tile's, the tracer tile's, then, wrapping, rows 0 to 7. One request every 8 cycles, the first read by a load
// presented in p + 10.
TEST(root_round_robin) {
    Bench b;
    for (unsigned r = 0; r < kMeshSize; r++) b.present(r * kMeshSize, request(0, r));
    for (unsigned s = 0; s < 3; s++) b.present(kTileCount + s, request(0, 0x40 | s));
    b.step();
    const uint64_t p = b.cycle;
    unsigned order[kMeshSize + 3];
    for (unsigned s = 0; s < 3; s++) order[s] = kTileCount + s;
    for (unsigned r = 0; r < kMeshSize; r++) order[3 + r] = r * kMeshSize;
    unsigned got = 0;
    for (unsigned i = 0; i < (kMeshSize + 3) * kBeats + 16; i++) {
        b.load(0);
        b.step();
        const bool expected = b.cycle >= p + kReadSpecial && (b.cycle - p - kReadSpecial) % kBeats == 0 && got < kMeshSize + 3;
        CHECK_EQ(b.pending(0), expected);
        if (!expected) continue;
        const unsigned sender = order[got];
        CHECK(b.read(0) == stamped(request(0, sender < kTileCount ? sender / kMeshSize : 0x40 | (sender - kTileCount)), sender));
        got++;
    }
    CHECK_EQ(got, kMeshSize + 3);
}

// ====================================================================== 11.3: the reserved targets, 3 to 7
// A request for a reserved target is dropped at the root (11.2): no queue receives it, it takes no credit, and its
// beats leave the root's buffer one a cycle, so a request behind it arrives as behind any other: 8 cycles later than
// alone. (The specification names no error for it, and the RTL's root raises none.)
TEST(reserved_targets_are_dropped) {
    for (unsigned sender : {12u, kTracer}) {
        for (unsigned reserved = 3; reserved < 8; reserved++) {
            Bench b;
            const unsigned read_after = sender < kTileCount ? kReadCompute : kReadSpecial;
            b.present(sender, request(reserved, 7));
            b.step();
            CHECK(b.taken(sender));
            const uint64_t p = b.cycle;
            b.present(sender, request(1, 8));
            b.step();
            CHECK(b.taken(sender));
            for (unsigned i = 0; i < 40; i++) {
                b.step();
                const bool there = b.cycle >= p + read_after + kBeats;
                CHECK(!b.pending(0) && !b.pending(2));
                CHECK_EQ(b.pending(1), there);
                if (there) CHECK(b.read(1) == stamped(request(1, 8), sender));
            }
        }
    }
}

// ==================================================================== 11.3: back-pressure, the credits, the depths
// The tree drops nothing and each buffer's ready is its room. Nobody pops target `t`'s queue and one compute tile sends
// to it without end: the root has RECEIVE_QUEUE_DEPTH credits (128, 256, 128), a root's and a row node's buffer hold a
// request each and the request queue REQUEST_QUEUE_DEPTH = 4 (19), so exactly depth + 6 sends are taken and the next
// stalls for as long as nothing is popped; a load reads the first request all the while. Then the queue is popped to
// its end: every request arrives, in the order sent, none lost and none twice.
TEST(back_pressure_fills_to_the_depths_and_drops_nothing) {
    for (unsigned target = 0; target < kTargets; target++) {
        Bench b;
        const unsigned sender = 19 + target, held = kReceiveQueueDepth[target] + 2 + kRequestQueueDepth;
        unsigned sent = 0;
        for (unsigned i = 0; i < 12 * held; i++) {
            if (!b.presented(sender)) b.present(sender, request(target, sent++));
            b.step();
        }
        CHECK_EQ(sent, held + 1);                                            // the last is presented, and stalls
        CHECK(b.presented(sender));
        CHECK(b.pending(target) && b.read(target) == stamped(request(target, 0), sender));
        unsigned got = 0;
        for (unsigned i = 0; i < 12 * held && got < sent; i++) {
            b.load(target);
            b.step();
            if (!b.pending(target)) continue;
            CHECK(b.read(target) == stamped(request(target, got), sender));
            got++;
        }
        CHECK_EQ(got, sent);
        for (unsigned i = 0; i < 64; i++) { b.load(target); b.step(); CHECK(!b.pending(target)); }
    }
}

// A credit is taken when the root starts to forward and returned by the pop, pulsed in the pop's cycle and usable from
// the next. With the master tile's credits gone and everything behind full (above), the stalled send is presented in
// every cycle. A REQUEST_RECEIVE load presented in c pops: the root chooses the request in its buffer in c + 1, whose
// beat 0 leaves then; the row node's beat moves into that room in c + 2, the request queue's head's beat 0 in c + 3
// and its last in c + 10, the cycle the stalled send is taken in: no earlier, no later. Exactly one send is taken for
// one pop. (That an empty queue's REQUEST_RECEIVE returns no credit is the core's side: on the chip, below.)
TEST(credit_returned_by_the_pop_is_usable_the_next_cycle) {
    Bench b;
    const unsigned sender = 42, held = kReceiveQueueDepth[0] + 2 + kRequestQueueDepth;
    unsigned sent = 0;
    for (unsigned i = 0; i < 12 * held; i++) {
        if (!b.presented(sender)) b.present(sender, request(0, sent++));
        b.step();
    }
    CHECK_EQ(sent, held + 1);
    for (unsigned pops = 0; pops < 3; pops++) {
        b.load(0);
        b.step();
        const uint64_t c = b.cycle;
        CHECK(b.presented(sender));
        while (b.cycle < c + 9) { b.step(); CHECK(b.presented(sender)); }
        b.step();                                                            // c + 10
        CHECK(b.taken(sender));
        b.present(sender, request(0, sent++));
        for (unsigned i = 0; i < 60; i++) { b.step(); CHECK(b.presented(sender)); }   // one pop, one send
    }
}

// A stalled input holds up its row behind it, and no other row: with the master tile's credits gone and row 2 full
// behind a request for it, a request from another tile of row 2 to the memory tile does not arrive, one from row 3
// arrives as on an idle tree, and so does a special tile's. A pop of the master tile's queue lets row 2's through.
TEST(a_stalled_input_holds_up_its_row_alone) {
    Bench b;
    const unsigned sender = 2 * kMeshSize + 1, held = kReceiveQueueDepth[0] + 2 + kRequestQueueDepth;
    unsigned sent = 0;
    for (unsigned i = 0; i < 12 * held; i++) {
        if (!b.presented(sender)) b.present(sender, request(0, sent++));
        b.step();
    }
    CHECK_EQ(sent, held + 1);
    const unsigned same_row = 2 * kMeshSize + 6, other_row = 3 * kMeshSize + 6;
    b.present(same_row, request(1, 0x51));
    b.present(other_row, request(1, 0x52));
    b.present(kTracer, request(1, 0x53));
    b.step();
    const uint64_t p = b.cycle;
    CHECK(b.taken(same_row) && b.taken(other_row) && b.taken(kTracer));
    // the tracer tile's first (a node fewer), read from p + 10; the other row's is at the root in p + 3, which passes
    // the tracer tile's until p + 9: chosen in p + 10, read from p + 18
    std::vector<Request> got;
    for (unsigned i = 0; i < 200; i++) {
        b.load(1);
        b.step();
        if (!b.pending(1)) continue;
        CHECK_EQ(b.cycle, got.empty() ? p + kReadSpecial : p + kReadSpecial + kBeats);
        got.push_back(b.read(1));
    }
    CHECK_EQ(got.size(), size_t(2));
    CHECK(got[0] == stamped(request(1, 0x53), kTracer));
    CHECK(got[1] == stamped(request(1, 0x52), other_row));
    // The row's buffer at the root holds tile 17's request for the master tile, and the row node passes its next, also
    // for the master tile. One pop of the master tile's queue moves the first on and the second into the root's buffer,
    // where it waits for a credit in its turn: the memory tile's request is still behind it.
    b.load(0);
    b.step();
    for (unsigned i = 0; i < 80; i++) { b.step(); CHECK(!b.pending(1)); }
    // A second pop, presented in c: the root passes tile 17's request in c + 1 .. c + 8, while the row node, which chose
    // column 6 after column 1, moves the memory tile's request into the room behind it from c + 2; the root chooses it in
    // c + 9, its last beat moves in c + 16, and a load presented in c + 17 reads it.
    b.load(0);
    b.step();
    const uint64_t c = b.cycle;
    while (b.cycle < c + 2 * kBeats) { b.step(); CHECK(!b.pending(1)); }
    b.step();
    CHECK(b.pending(1));
    CHECK(b.read(1) == stamped(request(1, 0x51), same_row));
}

// ======================================================================= 11.1, 11.3: the receive queue's protocol
// REQUEST_IN_W0-W2 and REQUEST_RECEIVE read the oldest request; a REQUEST_RECEIVE load pops it at the end of its
// cycle, so the next request is read from the next cycle, and a cycle with no load leaves it. An empty queue reads 0.
TEST(receive_queue_reads_the_oldest_and_pops_at_the_end_of_the_load) {
    Bench b;
    for (unsigned i = 0; i < 20; i++) { b.step(); CHECK(!b.pending(0)); CHECK(b.read(0) == Request{}); }
    const unsigned sender = 30;
    for (unsigned i = 0; i < 3; i++) CHECK(b.send(sender, request(0, i)) != 0);
    for (unsigned i = 0; i < 40; i++) b.step();
    for (unsigned i = 0; i < 3; i++) {
        CHECK(b.pending(0) && b.read(0) == stamped(request(0, i), sender));
        b.step();                                                            // a cycle with no load: the head stays
        CHECK(b.pending(0) && b.read(0) == stamped(request(0, i), sender));
        b.load(0);
        b.step();                                                            // the load's cycle reads it, and pops
        CHECK(b.pending(0) && b.read(0) == stamped(request(0, i), sender));
        b.step();                                                            // the next cycle: the next request, or none
    }
    CHECK(!b.pending(0));
    CHECK(b.read(0) == Request{});
}

// ============================================================================ 11.1-11.3: random traffic, every cycle
// Every sender and every receiver at random, against the model above, in every cycle: whether each presented send is
// taken (the request queue's stall, through the tree's back-pressure), and what a load of each receive queue reads.
// Random targets, reserved ones among them, and random requester fields; light traffic, then every sender at once
// with slow receivers, then the master tile's queue left unpopped until its credits are gone, then the drain. The model
// counts what the run met: stalled sends, a node held by a full buffer, the root skipping an input for a credit, drops.
TEST(tree_random_traffic_against_the_model) {
    for (uint64_t seed : {1u, 2u, 3u}) {
        sim::Options options;                                               // seed 2: the units of a level in a shuffled order;
        if (seed == 2) options.shuffle_seed = 11;                            //   seed 3: every unit stepped in every cycle
        if (seed == 3) options.step_all = true;
        test::Rng random(0x5EED0000 + seed);
        Bench b(options);
        Model model;
        Model::In in;
        uint32_t serial = 0;
        const unsigned kCycles = 24000;
        uint64_t requests = 0;
        for (unsigned c = 0; c < kCycles; c++) {
            const unsigned phase = c < 3000 ? 0 : c < 9000 ? 1 : c < 16000 ? 2 : 3;
            const unsigned send_one_in = phase == 0 ? 300 : phase == 3 ? 0 : 12;
            for (unsigned s = 0; s < kSenders; s++) {
                if (in.present[s] || !send_one_in || random.below(send_one_in) != 0) continue;
                Request r;
                const unsigned target = random.below(10) == 0 ? 3 + unsigned(random.below(5)) : unsigned(random.below(3));
                r.w[0] = word0(target, unsigned(random.below(128)), uint32_t(random.next()));
                r.w[1] = ++serial; r.w[2] = uint32_t(random.next()); r.w[3] = uint32_t(random.next());
                in.present[s] = true; in.request[s] = r;
                requests++;
            }
            const unsigned pop_one_in[4][kTargets] = {{2, 2, 2}, {5, 3, 9}, {0, 40, 3}, {2, 2, 2}};
            for (unsigned t = 0; t < kTargets; t++) in.load[t] = pop_one_in[phase][t] && random.below(pop_one_in[phase][t]) == 0;
            for (unsigned s = 0; s < kSenders; s++) if (in.present[s]) b.present(s, in.request[s]);
            for (unsigned t = 0; t < kTargets; t++) if (in.load[t]) b.load(t);
            b.step();
            const Model::Out out = model.cycle(in);
            for (unsigned s = 0; s < kSenders; s++) {
                if (b.taken(s) != out.taken[s]) {
                    test::fail(__FILE__, __LINE__, "seed %llu, cycle %u: sender %u's send is %s; the model: %s", (unsigned long long)seed,
                               c + 1, s, b.taken(s) ? "taken" : "not taken", out.taken[s] ? "taken" : "not taken");
                    return;
                }
                if (out.taken[s]) in.present[s] = false;
            }
            for (unsigned t = 0; t < kTargets; t++)
                if (b.pending(t) != out.pending[t] || !(b.read(t) == out.read[t])) {
                    test::fail(__FILE__, __LINE__, "seed %llu, cycle %u: target %u reads pending %d, %08x %08x %08x %08x; the model: %d, %08x %08x %08x %08x",
                               (unsigned long long)seed, c + 1, t, int(b.pending(t)), b.read(t).w[0], b.read(t).w[1], b.read(t).w[2], b.read(t).w[3],
                               int(out.pending[t]), out.read[t].w[0], out.read[t].w[1], out.read[t].w[2], out.read[t].w[3]);
                    return;
                }
        }
        std::printf("  seed %llu: %llu requests, %llu delivered, %llu dropped; %llu stalled sends, %llu cycles a row node held, %llu the root "
                    "skipping for a credit\n", (unsigned long long)seed, (unsigned long long)requests, (unsigned long long)model.delivered,
                    (unsigned long long)model.dropped, (unsigned long long)model.stalled_sends, (unsigned long long)model.full_buffers,
                    (unsigned long long)model.credit_waits);
        CHECK_EQ(model.delivered + model.dropped, requests);                 // nothing lost: every request is through
        CHECK(model.dropped > 100 && model.stalled_sends > 1000 && model.full_buffers > 1000 && model.credit_waits > 100);
        for (unsigned t = 0; t < kTargets; t++) CHECK(model.receive[t].empty() && model.credits[t] == kReceiveQueueDepth[t]);
    }
}

// ======================================================================================================= the chip
// The registers of 11.1 as a core meets them, on tests/mach.h's chip (the 64 compute tiles with the strap boot_local,
// the master, memory and tracer tiles, the request tree): programs written here, placed in IMEM before the first cycle
// (6.5), and the boot ROM's own requests (the tracer tile's LOAD; a tile's reports 24 and 25 after an error). Every
// core's accesses to the tile window are read off its data port, cycle by cycle, and the whole run is replayed in the
// model above: every REQUEST_SEND's cycle taken, every load's value.
namespace {

// The tile window's registers (5.3), as x0-relative addresses.
constexpr uint32_t kWindow = 0xFFFFF800;
constexpr uint32_t kStatus = kWindow + 0x004, kErrors = kWindow + 0x008, kEvents = kWindow + 0x00C;
constexpr uint32_t kRequestOutW0 = kWindow + 0x100, kRequestSend = kWindow + 0x10C;
constexpr uint32_t kRequestInW0 = kWindow + 0x110, kRequestReceive = kWindow + 0x11C;
constexpr uint32_t kStatusRequestFull = 1u << 3;                  // STATUS [3] (7.1)
constexpr uint32_t kEventRequestPending = 1u << 3;                // EVENTS [3] (7.2)
constexpr uint32_t kErrorReceiveUnderflow = 1u << 9;              // ERRORS [9] (7.4)
constexpr uint32_t kImemBase = 0x01000000;                        // region 0x01 (5.1)
constexpr unsigned kWindowLoadLatency = 2;                        // WINDOW_LOAD_LATENCY (6.4)
// A request's word 0 for the master tile (11.5): op at [17:10], REPORT's number at [25:18].
constexpr uint32_t kOpReport = 1, kOpLoad = 3;
constexpr uint32_t report_word0(uint32_t number) { return number << 18 | kOpReport << 10; }

// ---- programs ----
struct Program {
    std::vector<uint32_t> words;
    void li(unsigned rd, uint32_t v) {
        words.push_back(rv32_asm::LUI(rd, (v + 0x800) & 0xFFFFF000u));
        words.push_back(rv32_asm::ADDI(rd, rd, int32_t(v << 20) >> 20));
    }
    void store(unsigned rs, uint32_t address) { words.push_back(rv32_asm::SW(rs, 0, int32_t(address))); }
    void load(unsigned rd, uint32_t address) { words.push_back(rv32_asm::LW(rd, 0, int32_t(address))); }
    void nops(unsigned n) { for (unsigned i = 0; i < n; i++) words.push_back(rv32_asm::NOP); }
    size_t here() const { return words.size(); }
    void beqz(unsigned rs, size_t to) { words.push_back(rv32_asm::B(4 * (int32_t(to) - int32_t(here())), 0, rs, 0)); }
    void jump(size_t to) { words.push_back(rv32_asm::J(4 * (int32_t(to) - int32_t(here())), 0)); }
    void park() { const size_t at = here(); words.push_back(rv32_asm::WFI); jump(at); }
};

// An access to the tile window, off a core's data port: issued in `issued` (6.4's t), made, that is presented and
// taken, in `made` (a window's access is presented in t + 1; a stalled REQUEST_SEND later).
struct Access { unsigned unit; uint64_t issued, made; bool store; uint32_t address, value; };

struct ChipRun {
    std::unique_ptr<mach::Mach> m;
    units::TileCore* core[kSenders] = {};
    bool was_out[kSenders] = {}, open[kSenders] = {};
    uint32_t last_address[kSenders] = {};
    uint64_t last_make[kSenders] = {};
    std::vector<Access> trace[kSenders];
    uint64_t underflow_seen[kSenders] = {};                       // the first cycle after which ERRORS [9] reads 1 (0: never)

    explicit ChipRun(const std::vector<std::pair<unsigned, Program>>& programs) {
        mach::MachConfig k = mach::MachConfig::chip();
        k.boot_local = true;                                       // every compute tile starts from its IMEM (6.5)
        k.check_errors = false;                                    // the runs set ERRORS on purpose
        m = std::make_unique<mach::Mach>(k);
        Program idle;
        idle.park();
        for (unsigned u = 0; u < kSenders; u++) {
            if (u == kTracer) continue;                            // the tracer tile is loaded at its LOAD (6.5): never, here
            const Program* p = &idle;
            for (const auto& [unit, program] : programs) if (unit == u) p = &program;
            m->place(u, kImemBase, p->words);
        }
        for (unsigned u = 0; u < kSenders; u++) core[u] = &m->c.tile(u).unit<units::TileCore>("core");
    }
    void run(unsigned cycles) {
        for (unsigned i = 0; i < cycles; i++) {
            m->c.step();
            const uint64_t t = uint64_t(m->c.cycle());
            for (unsigned u = 0; u < kSenders; u++) {
                const units::Rv32& rv = core[u]->rv;
                const bool raised = rv.lsu_out && (!was_out[u] || rv.lsu_addr != last_address[u] || rv.lsu_make_at != last_make[u]);
                was_out[u] = rv.lsu_out; last_address[u] = rv.lsu_addr; last_make[u] = rv.lsu_make_at;
                if (raised) {
                    open[u] = (rv.lsu_addr & ~0x1FFu) == kWindow;
                    if (open[u]) trace[u].push_back({u, t, 0, rv.lsu_store, rv.lsu_addr, rv.lsu_wdata});
                    // a store that cannot stall is made in the cycle the port says, and may be answered in it
                    if (open[u] && rv.lsu_store && rv.lsu_addr != kRequestSend) { trace[u].back().made = rv.lsu_make_at; open[u] = false; }
                }
                if (open[u] && rv.lsu_out && rv.lsu_made) {        // made in this cycle: a load's value is the port's
                    open[u] = false;
                    trace[u].back().made = t;
                    if (!rv.lsu_store) trace[u].back().value = rv.lsu_rdata;
                }
                if (!underflow_seen[u] && (m->c.tile(u).errors() & kErrorReceiveUnderflow)) underflow_seen[u] = t;
            }
        }
    }
    // Unit u's accesses to `address`, in order.
    std::vector<Access> at(unsigned u, uint32_t address) const {
        std::vector<Access> v;
        for (const Access& a : trace[u]) if (a.address == address) v.push_back(a);
        return v;
    }
};

// The run replayed in the model. False, with a failure reported, at the first difference.
//   - A REQUEST_SEND issued in t is presented from t + 1 until it is taken (6.4, 11.1), with REQUEST_OUT_W0-W2 as the
//     stores before it left them, 0 after a send (11.1).
//   - A load issued in t is presented in t + 1 and reads the state at the end of t (6.4): STATUS.request_full, the
//     request queue full; EVENTS.request_pending, REQUEST_IN_W0-W2 and REQUEST_RECEIVE, the receive queue of the
//     tile's target (unit 64 + k: target k), a tile with no receive side reading an empty one; a REQUEST_RECEIVE pops.
bool replay(const ChipRun& run, uint64_t cycles, std::vector<Request>* popped = nullptr) {
    Model model;
    size_t next[kSenders] = {};
    uint32_t out_w[kSenders][3] = {};
    Model::In in;
    for (uint64_t c = 1; c <= cycles; c++) {
        // the accesses made, or presented and stalled, in c
        const Access* now[kSenders] = {};
        for (unsigned u = 0; u < kSenders; u++) {
            const std::vector<Access>& tr = run.trace[u];
            if (next[u] < tr.size() && tr[next[u]].made != 0 && tr[next[u]].made < c) next[u]++;
            if (next[u] < tr.size() && tr[next[u]].issued < c) now[u] = &tr[next[u]];
        }
        for (unsigned t = 0; t < kTargets; t++) in.load[t] = false;
        bool full[kSenders];
        for (unsigned u = 0; u < kSenders; u++) {
            full[u] = model.queue[u].size() == kRequestQueueDepth;
            in.present[u] = now[u] && now[u]->store && now[u]->address == kRequestSend;
            if (in.present[u]) { for (unsigned i = 0; i < 3; i++) in.request[u].w[i] = out_w[u][i]; in.request[u].w[3] = now[u]->value; }
            if (now[u] && !now[u]->store && now[u]->address == kRequestReceive && u >= kTileCount) in.load[u - kTileCount] = true;
        }
        const Model::Out out = model.cycle(in);
        for (unsigned u = 0; u < kSenders; u++) {
            if (!now[u]) continue;
            const Access& a = *now[u];
            const bool made = a.made == c;
            if (a.store) {
                const bool expected = a.address == kRequestSend ? out.taken[u] : true;
                if (made != expected) {
                    test::fail(__FILE__, __LINE__, "unit %u, cycle %llu: a store to 0x%08x issued in %llu is %s; the model: %s", u,
                               (unsigned long long)c, a.address, (unsigned long long)a.issued, made ? "taken" : "not taken",
                               expected ? "taken" : "stalled");
                    return false;
                }
                if (!made) continue;
                if (a.address == kRequestSend) out_w[u][0] = out_w[u][1] = out_w[u][2] = 0;
                else if (a.address >= kRequestOutW0 && a.address < kRequestSend) out_w[u][(a.address - kRequestOutW0) / 4] = a.value;
                continue;
            }
            if (!made || a.issued + kWindowLoadLatency - 1 != c) {
                test::fail(__FILE__, __LINE__, "unit %u: a load of 0x%08x issued in %llu is made in %llu (6.4: a window's in t + 1)", u,
                           a.address, (unsigned long long)a.issued, (unsigned long long)a.made);
                return false;
            }
            const bool receiver = u >= kTileCount;
            const unsigned target = receiver ? u - kTileCount : 0;
            const bool pending = receiver && out.pending[target];
            uint32_t expected = 0, mask = 0xFFFFFFFFu;
            if (a.address == kStatus) { expected = full[u] ? kStatusRequestFull : 0; mask = kStatusRequestFull; }
            else if (a.address == kEvents) { expected = pending ? kEventRequestPending : 0; mask = kEventRequestPending; }
            else if (a.address >= kRequestInW0 && a.address <= kRequestReceive) {
                expected = pending ? out.read[target].w[(a.address - kRequestInW0) / 4] : 0;
                if (a.address == kRequestReceive && pending && popped) popped->push_back(out.read[target]);
            } else continue;
            if ((a.value & mask) != expected) {
                test::fail(__FILE__, __LINE__, "unit %u: a load of 0x%08x issued in %llu reads 0x%08x; the model: 0x%08x (mask 0x%08x)", u,
                           a.address, (unsigned long long)a.issued, a.value, expected, mask);
                return false;
            }
        }
    }
    return true;
}

// The programs of the runs below. `delay` moves the senders against the master tile's poll, a cycle a NOP.
constexpr unsigned kSendingTile = 9;
// Requests A to F of the sending tile, all for the master tile: A's four words, with a requester (0x7F) that is not its
// own; B to E store nothing to REQUEST_OUT_W0-W2 after the send before them, only their word 3; F stores word 0 alone.
constexpr uint32_t kA[4] = {0x5A5A3C7Fu, 0x0A0A1111u, 0x0A0A2222u, 0x0A0A3333u};
constexpr uint32_t kBare3[4] = {0x0B0B3333u, 0x0C0C3333u, 0x0D0D3333u, 0x0E0E3333u};           // B, C, D, E
constexpr uint32_t kF0 = 0x00F0FC00u, kF3 = 0x0F0F3333u;
constexpr uint32_t kM[4] = {0x00770403u, 0x0E0E1111u, 0x0E0E2222u, 0x0E0E4444u};               // the memory tile's
static_assert(target_of(kA[0]) == 0 && target_of(kF0) == 0 && target_of(kM[0]) == 0 && requester_of(kA[0]) == 0x7F);

// A send is answered the cycle after it is taken (6.4), so a core's sends are two cycles apart at the least, and four
// are in the queue of REQUEST_QUEUE_DEPTH = 4 only as the first leaves, 8 cycles after it was taken: the fifth, E, is
// presented in that very cycle (the tie: "no entry leaves that cycle"), and the sixth, F, into a full queue.
Program sending_tile(unsigned delay) {
    Program p;
    for (unsigned i = 0; i < 4; i++) p.li(10 + i, kA[i]);
    for (unsigned i = 0; i < 4; i++) p.li(14 + i, kBare3[i]);
    p.li(18, kF0); p.li(19, kF3);
    p.nops(40 + delay);                                            // the tracer tile's LOAD is through by then
    p.load(20, kStatus);                                           // an empty queue
    for (unsigned i = 0; i < 3; i++) p.store(10 + i, kRequestOutW0 + 4 * i);
    p.store(13, kRequestSend);                                     // A
    for (unsigned i = 0; i < 4; i++) p.store(14 + i, kRequestSend);   // B, C, D, E: REQUEST_OUT_W0-W2 cleared by each push
    p.store(18, kRequestOutW0);
    p.store(19, kRequestSend);                                     // F: stalls until B has left
    p.load(21, kStatus);                                           // C, D, E and F: full
    p.nops(80);
    p.load(22, kStatus);                                           // the queue has emptied
    p.nops(200);
    p.load(23, kRequestInW0);                                      // no receive side: an empty queue
    p.load(24, kRequestReceive);                                   // and its underflow
    p.park();
    return p;
}
Program memory_tile(unsigned delay) {
    Program p;
    for (unsigned i = 0; i < 4; i++) p.li(10 + i, kM[i]);
    p.nops(200 + delay);
    for (unsigned i = 0; i < 3; i++) p.store(10 + i, kRequestOutW0 + 4 * i);
    p.store(13, kRequestSend);
    p.park();
    return p;
}
// The master tile: polls its receive queue, EVENTS then REQUEST_IN_W0, and receives each request it finds.
Program master_tile() {
    Program p;
    const size_t loop = p.here();
    p.load(8, kEvents);
    p.load(5, kRequestInW0);
    p.beqz(5, loop);
    p.load(6, kRequestInW0 + 4);
    p.load(7, kRequestInW0 + 8);
    p.load(8, kEvents);
    p.load(9, kRequestReceive);
    p.jump(loop);
    return p;
}

}  // namespace

// 11.1 on the chip. The sending tile's requests back to back: the first four are taken as they are presented; the
// fifth is presented in the cycle the first leaves the full queue, 8 cycles after it was taken (the tree idle), and is
// taken in it; the sixth meets a full queue and stalls, the core with it, until the second has left, 8 cycles later,
// and is taken in that cycle: not dropped. STATUS.request_full reads 1 while the queue holds four. A push clears
// REQUEST_OUT_W0-W2. The master tile reads each request with the tree's requester, EVENTS.request_pending live, and
// pops it with REQUEST_RECEIVE. A REQUEST_RECEIVE on a tile with no receive side reads 0 and sets
// ERRORS.receive_underflow, which the boot ROM's handler finds (6.5; the tile's error line then stops the master tile, whose
// Ingress/Egress raises its own error: 17.6, so the handler's reports stay in the master tile's queue). The whole run
// agrees with the model.
TEST(chip_request_unit_registers) {
    ChipRun run({{kSendingTile, sending_tile(0)}, {kMemory, memory_tile(0)}, {kMaster, master_tile()}});
    const unsigned kCycles = 900;
    run.run(kCycles);
    std::vector<Request> popped;
    CHECK(replay(run, kCycles, &popped));

    // ---- the sends ----
    const std::vector<Access> sends = run.at(kSendingTile, kRequestSend);
    CHECK(sends.size() >= 6);
    const uint64_t a = sends[0].made;
    for (unsigned i = 0; i < 4; i++) {
        CHECK_EQ(sends[i].made, sends[i].issued + 1);              // taken as presented: in effect at the end of t + 1 (6.4)
        CHECK(sends[i].made < a + kLeaves);                        // A, B, C and D are all in the queue before A leaves
    }
    CHECK_EQ(sends[4].issued + 1, a + kLeaves);                    // E is presented in the cycle A leaves the full queue in
    CHECK_EQ(sends[4].made, a + kLeaves);                          //   and is taken in it
    CHECK(sends[5].issued + 1 < a + 2 * kLeaves);                  // F is presented while B, C, D and E fill the queue
    CHECK_EQ(sends[5].made, a + 2 * kLeaves);                      //   and is taken in the cycle B leaves in
    CHECK_EQ(run.core[kSendingTile]->stall_request, a + 2 * kLeaves - (sends[5].issued + 1));   // the core's stalled cycles
    // ---- STATUS.request_full: an empty queue; C, D, E and F; empty again ----
    const std::vector<Access> status = run.at(kSendingTile, kStatus);
    CHECK(status.size() >= 3);
    CHECK(status[0].made < a);
    CHECK_EQ(status[0].value & kStatusRequestFull, 0u);
    CHECK(status[1].made > a + 2 * kLeaves && status[1].made <= a + 3 * kLeaves);   // C leaves in a + 24
    CHECK_EQ(status[1].value & kStatusRequestFull, kStatusRequestFull);
    CHECK(status[2].made > a + 6 * kLeaves);                       // F has left by a + 48
    CHECK_EQ(status[2].value & kStatusRequestFull, 0u);
    // ---- what the master tile received, in order ----
    CHECK(popped.size() >= 8);
    const Request expected[8] = {
        {{kOpLoad << 10 | kTracer, 0, 0, 0}},                      // the tracer tile's boot ROM's LOAD (6.5, 11.5)
        {{(kA[0] & ~0x7Fu) | kSendingTile, kA[1], kA[2], kA[3]}},
        {{kSendingTile, 0, 0, kBare3[0]}},
        {{kSendingTile, 0, 0, kBare3[1]}},
        {{kSendingTile, 0, 0, kBare3[2]}},
        {{kSendingTile, 0, 0, kBare3[3]}},
        {{kF0 | kSendingTile, 0, 0, kF3}},
        {{(kM[0] & ~0x7Fu) | kMemory, kM[1], kM[2], kM[3]}},
    };
    for (unsigned i = 0; i < 8; i++) {
        if (!(popped[i] == expected[i]))
            test::fail(__FILE__, __LINE__, "the master tile's request %u: %08x %08x %08x %08x, expected %08x %08x %08x %08x", i, popped[i].w[0],
                       popped[i].w[1], popped[i].w[2], popped[i].w[3], expected[i].w[0], expected[i].w[1], expected[i].w[2], expected[i].w[3]);
        CHECK(popped[i] == expected[i]);
    }
    CHECK_EQ(popped.size(), size_t(8));
    // ---- EVENTS.request_pending: 1 in the load before each REQUEST_RECEIVE, and both levels met in the poll ----
    unsigned levels[2] = {};
    for (const Access& e : run.at(kMaster, kEvents)) levels[(e.value & kEventRequestPending) != 0]++;
    CHECK(levels[0] > 20 && levels[1] >= 8);
    // ---- the empty queue: reads of 0, the underflow; the master tile's pops set no error ----
    const std::vector<Access> in0 = run.at(kSendingTile, kRequestInW0), receive = run.at(kSendingTile, kRequestReceive);
    CHECK_EQ(in0.size(), size_t(1));
    CHECK_EQ(receive.size(), size_t(1));
    CHECK_EQ(in0[0].value, 0u);
    CHECK_EQ(receive[0].value, 0u);
    CHECK(run.underflow_seen[kSendingTile] > receive[0].issued && run.underflow_seen[kSendingTile] <= receive[0].issued + 3);
    for (unsigned u = 0; u < kSenders; u++) if (u != kSendingTile) CHECK_EQ(run.underflow_seen[u], uint64_t(0));
    // the boot ROM's handler (6.5) loads ERRORS for its report 24, whose word 1 it is, and sends it to the master tile
    const std::vector<Access> errors = run.at(kSendingTile, kErrors), out_w0 = run.at(kSendingTile, kRequestOutW0);
    CHECK_EQ(errors.size(), size_t(1));
    CHECK_EQ(errors[0].value, kErrorReceiveUnderflow);
    CHECK(out_w0.size() >= 3 && out_w0[2].issued > receive[0].issued);
    CHECK_EQ(out_w0[2].value, report_word0(24));
}

// 11.1, 11.3: a REQUEST_RECEIVE on a receiving tile's empty queue returns 0, sets ERRORS.receive_underflow, and
// returns no credit. The master tile's first instruction is the load, before any request has arrived (the model says
// so); its boot ROM's handler then sends its two reports and parks (6.5), so nobody pops the master tile's queue, which
// holds three requests: the tracer tile's LOAD and those two. The sending tile sends without end: the root has
// RECEIVE_QUEUE_DEPTH - 3 = 125 credits left, the root's and the row node's buffers hold a request each and the request
// queue 4, so exactly 131 sends are taken and the next stalls; a credit for the underflow would take one more.
TEST(chip_receive_underflow_on_an_empty_receive_queue) {
    Program master;
    master.load(9, kRequestReceive);
    master.park();
    Program sender;
    sender.words.push_back(rv32_asm::ADDI(11, 0, 0));              // the boot ROM leaves no register an image may use (6.5)
    const size_t loop = sender.here();
    sender.store(11, kRequestSend);                                // word 0 is 0: the master tile's, target 0; word 3 counts
    sender.words.push_back(rv32_asm::ADDI(11, 11, 1));
    sender.jump(loop);
    ChipRun run({{kMaster, master}, {kSendingTile, sender}});
    const unsigned kCycles = 2000;
    run.run(kCycles);
    CHECK(replay(run, kCycles));
    const std::vector<Access> receive = run.at(kMaster, kRequestReceive);
    CHECK_EQ(receive.size(), size_t(1));
    CHECK_EQ(receive[0].value, 0u);
    CHECK(run.underflow_seen[kMaster] > receive[0].issued && run.underflow_seen[kMaster] <= receive[0].issued + 3);
    CHECK_EQ(run.m->c.tile(kMaster).errors(), kErrorReceiveUnderflow);
    CHECK_EQ(run.at(kMaster, kRequestSend).size(), size_t(2));     // the handler's reports 24 and 25
    CHECK_EQ(run.at(kTracer, kRequestSend).size(), size_t(1));     // the tracer tile's LOAD
    const std::vector<Access> sends = run.at(kSendingTile, kRequestSend);
    const unsigned taken = kReceiveQueueDepth[0] - 3 + 2 + kRequestQueueDepth;
    CHECK_EQ(sends.size(), size_t(taken + 1));
    for (unsigned i = 0; i < taken; i++) { CHECK(sends[i].made != 0); CHECK_EQ(sends[i].value, i); }
    CHECK_EQ(sends[taken].made, uint64_t(0));                      // presented, and stalled to the end of the run
    CHECK(sends[taken].issued + 400 < kCycles);
}

// The timing end to end, on cores (the header's TIMING; 6.4): a request whose REQUEST_SEND a compute tile issues in t
// is read by a load the master tile issues from t + 11, and not by one issued in t + 10; a special tile's, from t + 10
// and not in t + 9. The senders are moved a cycle at a time against the master tile's poll, so that its loads meet both
// cycles of each.
TEST(chip_request_timing_end_to_end) {
    uint64_t first_read[2] = {~uint64_t(0), ~uint64_t(0)}, last_empty[2] = {};
    for (unsigned delay = 0; delay < 12; delay++) {
        ChipRun run({{kSendingTile, sending_tile(delay)}, {kMemory, memory_tile(delay)}, {kMaster, master_tile()}});
        run.run(420);
        CHECK(replay(run, 420));
        const uint64_t issued[2] = {run.at(kSendingTile, kRequestSend).at(0).issued, run.at(kMemory, kRequestSend).at(0).issued};
        const uint32_t word0[2] = {(kA[0] & ~0x7Fu) | kSendingTile, (kM[0] & ~0x7Fu) | kMemory};
        const unsigned read_after[2] = {kReadCompute, kReadSpecial};
        for (unsigned k = 0; k < 2; k++) {
            bool read = false;
            uint64_t empty_before = 0;
            for (const Access& l : run.at(kMaster, kRequestInW0)) {
                if (l.issued + 40 < issued[k] || l.issued > issued[k] + 40) continue;
                if (l.value == word0[k]) {
                    CHECK(l.issued >= issued[k] + read_after[k]);
                    if (!read) first_read[k] = std::min(first_read[k], l.issued - issued[k]);
                    read = true;
                } else if (!read && l.issued >= issued[k]) {
                    CHECK_EQ(l.value, 0u);
                    CHECK(l.issued < issued[k] + read_after[k]);
                    empty_before = l.issued - issued[k];
                }
            }
            CHECK(read);
            last_empty[k] = std::max(last_empty[k], empty_before);
        }
    }
    CHECK_EQ(first_read[0], uint64_t(kReadCompute));
    CHECK_EQ(last_empty[0], uint64_t(kReadCompute - 1));
    CHECK_EQ(first_read[1], uint64_t(kReadSpecial));
    CHECK_EQ(last_empty[1], uint64_t(kReadSpecial - 1));
}
