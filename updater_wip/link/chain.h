// chain.h: a chain on the bench: the host's end, then nodes (node.h), each link a wire each way; a source for each
// of the host's streams and for each node's replies; a queue behind each stream at each node and behind the host's
// foreground; and the monitor. From the host every node must get the same words, each once, in order; at the host
// each node's replies must be whole and in order under their hops (HL: the host sorts what arrives by hops).
//
// Ends are numbered: 0 the host's; 1 + 2i node i's upper end; 2 + 2i its lower end. Link l joins end (l ? 2l : 0)
// and end 1 + 2l. Wires: 2l carries link l's bytes down the chain, 2l + 1 up. Sources: 0 and 1 the host's foreground
// and background, 2 + i node i's replies. Queues: 0 the host's foreground, 1 + 2i + s node i's stream s.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "bench.h"
#include "node.h"

namespace hl {

struct ChainConfig {
    Params p;
    unsigned nodes = 1;
    unsigned host_window = 0;              // the host's window, if not p's
    unsigned queue_words = 1024;           // a node's queue for each stream, and the host's
    unsigned fwd_words = 1024;             // a forwarding buffer (G16)
    unsigned out_words = 512;              // a node's outgoing queue: its own replies waiting
    std::vector<unsigned> delay;           // each wire's, in cycles; missing ones are 1
    unsigned words = 32;                   // the search's packets
    unsigned packets[3] = {};              // the search's budgets: the host's foreground, its background, each node's replies
    unsigned content = 0;                  // what the words hold: 0 counters, 1 zeros, 2 random
    unsigned still = 0;
};

struct ChainSource {                       // packets waiting to be sent, by their lengths
    std::vector<uint8_t> lens;             // every packet let out so far
    uint32_t made = 0;                     // those the link end has taken
    uint64_t made_words = 0, let_words = 0;
    uint32_t left = 0;                     // the search's: packets it may still let out
    bool waiting() const { return made < lens.size(); }
    void let(uint8_t length) { lens.push_back(length); let_words += length; }
    template <class A> void io(A& a) {
        uint32_t n = uint32_t(lens.size()) - made; a(n); a(made); a(made_words); a(left);
        lens.resize(made + n);
        for (uint32_t i = 0; i < n; i++) a(lens[made + i]);
    }
};

class Chain {
public:
    ChainConfig cfg;
    LinkEnd host;
    std::vector<ChainNode> node;
    std::vector<Wire> wire;                // 2 for each link
    std::vector<ChainSource> src;               // 2 + nodes
    std::vector<uint32_t> fill, most_fill; // 1 + 2 * nodes
    std::vector<uint64_t> got_down;        // [2i + s]: the words of the host's stream s delivered at node i
    std::vector<uint64_t> got_up;          // [i]: the words of node i's replies delivered at the host
    std::vector<uint8_t> stopped;          // [2 end + s]
    std::vector<uint32_t> waited;          // [end]
    uint32_t most_waited = 0;
    std::vector<uint32_t> turn;            // [end]: the cycles its next foreground packet has waited for the line, nothing excusing it
    uint32_t most_turn = 0;                // HL: "the longest a foreground packet waits is one background packet"
    // What one run of numbers costs the foreground (not state): the longest a foreground packet stood ready at an end,
    // not stopped, the link in step: with the window full; behind packets being sent again; in all.
    std::vector<uint32_t> fg_ready, fg_full, fg_again;   // [end]: the cycles so far for the packet now waiting
    uint32_t most_fg_ready = 0, most_fg_full = 0, most_fg_again = 0;
    uint64_t fg_ready_sum = 0, fg_sent = 0;
    bool flipped = false; uint64_t wrong_words = 0;
    bool gaps_allowed = false, must_report = false, excused = false;
    uint32_t budget[3] = {};
    uint64_t t = 0;
    std::string bad;
    bool stop = false;
    std::vector<uint8_t> started_v;        // [wire]: a packet started on it in the last cycle

    explicit Chain(const ChainConfig& c);
    void step();
    bool done(int hold = -1) const;
    long quiet(uint64_t bound, int hold = -1);   // hold: that stream's queues are never emptied again (bench.h)

    unsigned ends() const { return 2 * cfg.nodes; }
    LinkEnd& end(unsigned e) { return e == 0 ? host : (e & 1) ? node[(e - 1) / 2].up : node[(e - 2) / 2].down; }
    const LinkEnd& end(unsigned e) const { return const_cast<Chain*>(this)->end(e); }
    unsigned upper(unsigned link) const { return link ? 2 * link : 0; }
    unsigned lower(unsigned link) const { return 1 + 2 * link; }

    // What the search and the kept cases use, the same on Link1.
    unsigned n_wires() const { return unsigned(wire.size()); }
    bool started(unsigned w) const { return started_v[w] != 0; }
    Wire& the_wire(unsigned w) { return wire[w]; }
    uint32_t qfill(unsigned q) const { return fill[q]; }
    static constexpr bool flat = true;     // its sources and queues have one number each
    void fate(unsigned w, Fate f) { wire[w].set_fate(f, host.nums); if (Wire::forges(f) && f != Fate::FORGE_ACK_DATA) excused = true; }
    unsigned n_sources() const { return unsigned(src.size()); }
    bool offerable(unsigned i) const { return src[i].left != 0 && !src[i].waiting(); }
    void offer(unsigned i) { if (src[i].left) { src[i].left--; src[i].let(uint8_t(cfg.words)); } }
    unsigned n_queues() const { return unsigned(fill.size()); }
    void drain(unsigned q, uint32_t n) { fill[q] -= n == 0 || n > fill[q] ? fill[q] : n; }

    void word(unsigned flow, uint64_t index, uint8_t out[WORD_BYTES]) const;   // flow: 0, 1 the host's streams; 2 + i node i's replies
    template <class A> void io(A& a) {
        host.io(a);
        for (auto& n : node) n.io(a);
        for (auto& w : wire) w.io(a);
        for (auto& s : src) s.io(a);
        for (auto& f : fill) a(f);
        for (auto& g : got_down) a(g);
        for (auto& g : got_up) a(g);
        for (auto& s : stopped) a(s);
        for (auto& w : waited) a(w);
        for (auto& w : turn) a(w);
        for (auto& s : started_v) a(s);
        a(gaps_allowed); a(must_report); a(excused); a(flipped); a(budget); a(t);
    }
    std::vector<uint8_t> save() { std::vector<uint8_t> v; Writer w{v}; io(w); return v; }
    void load(const std::vector<uint8_t>& bytes) { Reader rd{bytes.data()}; io(rd); }
    Hash128 canon() const;
    std::vector<uint8_t> whole();
    uint32_t turn_bound() const { return 2 * HEADER_BYTES + cfg.p.max_words * WORD_BYTES + (cfg.p.pass ? cfg.p.max_words * WORD_BYTES / cfg.p.pass + HEADER_BYTES : 0) + 2; }
    bool flip(unsigned e);
    uint32_t still_bound() const { return cfg.still ? cfg.still : 8 * (cfg.p.status_timer + cfg.p.zeros + 2 * (HEADER_BYTES + cfg.p.max_words * WORD_BYTES)); }

    // For the random runs: counts that are not state.
    uint64_t hop_wait_max = 0, end_wait_max = 0, reply_wait_max = 0;   // a foreground packet from the host to node 0, to the last node; a reply to the host
    std::vector<uint64_t> fg_let;          // when each foreground packet of the host was let out
    std::vector<uint64_t> fg_taken;        // and when the host's end took it to send
    std::vector<std::vector<uint64_t>> reply_let;   // and each node's reply packets
    std::vector<uint64_t> last_damage;     // [link]: the cycle a byte on it was last changed
    std::vector<uint8_t> dirty;            // [link]: damaged since its last delivery
    uint64_t recoveries = 0, recovery_sum = 0, recovery_max = 0;   // from a link's last damage to its next delivery, both ends in step
    uint64_t own_passed = 0;

private:
    void deliver_down(unsigned i);
    void deliver_up();
    std::vector<uint32_t> acked_was, last_was;
    std::vector<uint64_t> fg_packets_at;   // [i]: the host's foreground packets delivered at node i
    std::vector<uint64_t> replies_at;      // [i]: node i's reply packets delivered at the host
};

}  // namespace hl
